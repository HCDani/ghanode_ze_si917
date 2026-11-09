/*
 * MQTT session, publishing and recovery. Everything here runs on the network
 * thread; the MQTT library calls mqtt_evt_handler() from mqtt_input() on the
 * same thread, so session state needs no locking.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/sys/reboot.h>

#include "app.h"
#include "backoff.h"
#include "mqtt_link.h"
#include "payload.h"
#include "recovery.h"
#include "secrets.h"
#include "sntp_sync.h"
#include "wifi.h"

LOG_MODULE_REGISTER(mqtt_link, LOG_LEVEL_INF);

/* QoS 1, so a reading is only treated as delivered once the broker acknowledges
 * it. One that is not is sent again in the next session, so a subscriber can
 * see a reading twice, with the same Sensor ID and timestamp.
 */
#define PUBLISH_QOS MQTT_QOS_1_AT_LEAST_ONCE

/* "AABBCCDDEEFF" + NUL */
#define SENSOR_ID_LEN     13
#define MQTT_JSON_MAX_LEN 208
#define MQTT_BUF_LEN      256

#define MQTT_CONNACK_WAIT_MS      10000
#define MQTT_PUBLISH_WAIT_MS      10000
#define MQTT_PUBLISH_REJECT_LIMIT 3
#define MQTT_PUBLISH_RETRY_MS     1000
#define MQTT_RETRY_DELAY_MIN_MS   5000
#define MQTT_RETRY_DELAY_MAX_MS   60000
/* How often the loop looks at the reading queue and the Wi-Fi state. */
#define POLL_SLICE_MS             500

static uint8_t rx_buf[MQTT_BUF_LEN];
static uint8_t tx_buf[MQTT_BUF_LEN];
static char payload[MQTT_JSON_MAX_LEN];
static char sensor_id[SENSOR_ID_LEN] = "000000000000";

/* Taken from the queue but not yet acknowledged. Kept across sessions. */
static struct sensor_reading pending;
static bool has_pending;

static struct mqtt_client client;
static struct net_sockaddr_storage broker;
static struct mqtt_utf8 username = MQTT_UTF8_LITERAL(SECRET_MQTT_USERNAME);
static struct mqtt_utf8 password = MQTT_UTF8_LITERAL(SECRET_MQTT_PASSWORD);

#if defined(CONFIG_GHANODE_MQTT_TLS)
#define CA_CERT_TAG 1

/* mbed TLS parses PEM only with the terminating NUL counted in the length. */
static const char ca_cert[] = {
#include "isrg_root_x1.pem.inc"
	'\0'
};
static const sec_tag_t sec_tags[] = {CA_CERT_TAG};
#endif

/* Session state, written by mqtt_evt_handler(). */
static bool connack_received;
static bool mqtt_connected;
static uint16_t inflight_id;
static bool puback_received;

static void mqtt_evt_handler(struct mqtt_client *const c, const struct mqtt_evt *evt)
{
	ARG_UNUSED(c);

	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		connack_received = true;
		mqtt_connected = (evt->result == 0);
		if (mqtt_connected) {
			LOG_INF("Connected to MQTT broker");
		} else {
			LOG_ERR("MQTT broker refused the connection: %d", evt->result);
		}
		break;
	case MQTT_EVT_DISCONNECT:
		LOG_INF("Disconnected from MQTT broker: %d", evt->result);
		mqtt_connected = false;
		break;
	case MQTT_EVT_PUBACK:
		if ((evt->result == 0) && (evt->param.puback.message_id == inflight_id)) {
			puback_received = true;
		}
		break;
	default:
		break;
	}
}

static void mqtt_setup(void)
{
	struct net_sockaddr_in *broker4 = (struct net_sockaddr_in *)&broker;

	broker4->sin_family = NET_AF_INET;
	broker4->sin_port = net_htons(CONFIG_GHANODE_MQTT_BROKER_PORT);
	if (net_addr_pton(NET_AF_INET, SECRET_MQTT_BROKER_ADDR, &broker4->sin_addr) < 0) {
		LOG_ERR("Bad broker address %s", SECRET_MQTT_BROKER_ADDR);
	}

#if defined(CONFIG_GHANODE_MQTT_TLS)
	int ret = tls_credential_add(CA_CERT_TAG, TLS_CREDENTIAL_CA_CERTIFICATE, ca_cert,
				     sizeof(ca_cert));

	if (ret < 0) {
		LOG_ERR("Failed to add the CA certificate: %d", ret);
	}
#endif

	/* The broker drops a session when another client connects with the same ID. */
	wifi_mac_string(sensor_id, sizeof(sensor_id));
	LOG_INF("STA mac=%s (sensor id), broker %s:%d%s", sensor_id, SECRET_MQTT_BROKER_ADDR,
		CONFIG_GHANODE_MQTT_BROKER_PORT,
		IS_ENABLED(CONFIG_GHANODE_MQTT_TLS) ? " over TLS as " SECRET_MQTT_TLS_HOSTNAME
						    : "");
}

static int poll_socket(int timeout_ms)
{
	struct zsock_pollfd fds = {
#if defined(CONFIG_GHANODE_MQTT_TLS)
		.fd = client.transport.tls.sock,
#else
		.fd = client.transport.tcp.sock,
#endif
		.events = ZSOCK_POLLIN,
	};

	return zsock_poll(&fds, 1, timeout_ms);
}

static bool wifi_down(void)
{
	return k_event_test(&net_events, NET_EVT_WIFI_DOWN) != 0;
}

/*
 * Each connect opens a new socket on a fresh ephemeral port, so a reconnect
 * never collides with a session the broker still holds from before a drop.
 */
static int mqtt_connect_and_wait(void)
{
	int64_t deadline;
	int ret;

	mqtt_client_init(&client);
	client.broker = &broker;
	client.evt_cb = mqtt_evt_handler;
	client.client_id.utf8 = (const uint8_t *)sensor_id;
	client.client_id.size = SENSOR_ID_LEN - 1;
	client.user_name = &username;
	client.password = &password;
	client.protocol_version = MQTT_VERSION_3_1_1;
	client.rx_buf = rx_buf;
	client.rx_buf_size = sizeof(rx_buf);
	client.tx_buf = tx_buf;
	client.tx_buf_size = sizeof(tx_buf);
#if defined(CONFIG_GHANODE_MQTT_TLS)
	/* The broker is dialled by address, so the certificate is matched to this name. */
	client.transport.type = MQTT_TRANSPORT_SECURE;
	client.transport.tls.config.peer_verify = TLS_PEER_VERIFY_REQUIRED;
	client.transport.tls.config.sec_tag_list = sec_tags;
	client.transport.tls.config.sec_tag_count = ARRAY_SIZE(sec_tags);
	client.transport.tls.config.hostname = SECRET_MQTT_TLS_HOSTNAME;
#else
	client.transport.type = MQTT_TRANSPORT_NON_SECURE;
#endif

	connack_received = false;
	mqtt_connected = false;

	ret = mqtt_connect(&client);
	if (ret < 0) {
		LOG_ERR("Failed to connect to MQTT broker: %d", ret);
		return ret;
	}

	deadline = k_uptime_get() + MQTT_CONNACK_WAIT_MS;
	while (!connack_received) {
		const int64_t remaining = deadline - k_uptime_get();

		if (remaining <= 0) {
			LOG_ERR("No CONNACK within %u ms", MQTT_CONNACK_WAIT_MS);
			(void)mqtt_abort(&client);
			return -ETIMEDOUT;
		}
		if (wifi_down()) {
			(void)mqtt_abort(&client);
			return -ENETDOWN;
		}
		if (poll_socket(MIN(remaining, POLL_SLICE_MS)) > 0) {
			ret = mqtt_input(&client);
			if (ret < 0) {
				return ret;
			}
		}
	}

	if (!mqtt_connected) {
		(void)mqtt_abort(&client);
		return -ECONNREFUSED;
	}
	return 0;
}

/*
 * Without a time sync a subscriber can only time a reading on arrival, so only
 * a fresh one may go out. An older one would get the wrong time.
 */
static bool too_old_to_send_unstamped(const struct sensor_reading *reading)
{
	return !sntp_synced() &&
	       ((k_uptime_get() - reading->uptime_ms) > CONFIG_GHANODE_SAMPLE_INTERVAL_MS);
}

static int publish_reading(const struct sensor_reading *reading)
{
	static uint16_t next_id;
	struct mqtt_publish_param param = {0};
	int64_t epoch_ms;
	const bool stamped = sntp_uptime_to_epoch_ms(reading->uptime_ms, &epoch_ms);
	const int n = payload_format(payload, sizeof(payload), sensor_id, reading,
				     stamped ? &epoch_ms : NULL);

	if (n < 0) {
		LOG_ERR("MQTT JSON format failed");
		return n;
	}

	/* Message ID 0 is reserved. */
	next_id = (next_id == UINT16_MAX) ? 1 : next_id + 1;
	inflight_id = next_id;
	puback_received = false;

	param.message.topic.topic.utf8 = (const uint8_t *)CONFIG_GHANODE_MQTT_TOPIC;
	param.message.topic.topic.size = sizeof(CONFIG_GHANODE_MQTT_TOPIC) - 1;
	param.message.topic.qos = PUBLISH_QOS;
	param.message.payload.data = (uint8_t *)payload;
	param.message.payload.len = n;
	param.message_id = inflight_id;

	int ret = mqtt_publish(&client, &param);

	if (ret == 0) {
		LOG_INF("MQTT publish: %s", payload);
	}
	return ret;
}

/*
 * Publishes until the session ends. Returns 0 when the broker or Wi-Fi reports
 * a disconnect, and -EPIPE when the session is dead without any disconnect
 * event. *published is set once the broker acknowledges a reading.
 */
static int publish_until_disconnected(bool *published)
{
	bool in_flight = false;
	int64_t ack_deadline = 0;
	int64_t retry_at = 0;
	uint32_t rejections = 0;
	int ret;

	while (true) {
		if (wifi_down()) {
			(void)mqtt_abort(&client);
			return 0;
		}
		if (!mqtt_connected) {
			return 0;
		}

		if (!in_flight) {
			sntp_sync_if_due();
		}

		if (!has_pending && (k_msgq_get(&reading_queue, &pending, K_NO_WAIT) == 0)) {
			has_pending = true;
		}

		if (has_pending && !in_flight && too_old_to_send_unstamped(&pending)) {
			LOG_INF("No SNTP time, dropped a reading taken %lld s ago",
				(k_uptime_get() - pending.uptime_ms) / 1000);
			has_pending = false;
			continue;
		}

		if (has_pending && !in_flight && (k_uptime_get() >= retry_at)) {
			ret = publish_reading(&pending);
			if (ret == 0) {
				in_flight = true;
				ack_deadline = k_uptime_get() + MQTT_PUBLISH_WAIT_MS;
			} else if ((ret == -EMSGSIZE) || (ret == -EINVAL)) {
				has_pending = false;
			} else if (!mqtt_connected) {
				return 0;
			} else {
				rejections++;
				LOG_ERR("MQTT publish rejected: %d (%u in a row)", ret, rejections);
				if (rejections >= MQTT_PUBLISH_REJECT_LIMIT) {
					(void)mqtt_abort(&client);
					return -EPIPE;
				}
				retry_at = k_uptime_get() + MQTT_PUBLISH_RETRY_MS;
			}
		}

		if (poll_socket(POLL_SLICE_MS) > 0) {
			ret = mqtt_input(&client);
			if (ret < 0) {
				LOG_ERR("MQTT input failed: %d", ret);
				return 0;
			}
		}

		ret = mqtt_live(&client);
		if ((ret < 0) && (ret != -EAGAIN)) {
			LOG_ERR("MQTT keep-alive failed: %d", ret);
			(void)mqtt_abort(&client);
			return 0;
		}

		/* mqtt_live() sends a PINGREQ each keep-alive period; one unanswered is tolerated. */
		if (client.unacked_ping > 1) {
			LOG_ERR("No PINGRESP for a whole keep-alive period");
			(void)mqtt_abort(&client);
			return -EPIPE;
		}

		if (in_flight && puback_received) {
			in_flight = false;
			has_pending = false;
			*published = true;
			rejections = 0;
		} else if (in_flight && (k_uptime_get() >= ack_deadline)) {
			LOG_ERR("MQTT publish not acknowledged in %u ms", MQTT_PUBLISH_WAIT_MS);
			(void)mqtt_abort(&client);
			return -EPIPE;
		}
	}
}

static void apply_recovery(enum recovery_action action, const char *reason)
{
	switch (action) {
	case RECOVERY_RESTART_WIFI:
		LOG_ERR("%s, restarting Wi-Fi", reason);
		k_event_post(&net_events, NET_EVT_WIFI_DOWN);
		break;
	case RECOVERY_RESET_MCU:
		LOG_ERR("%s after a Wi-Fi restart and nothing published for %u min, "
			"resetting the MCU",
			reason, RECOVERY_RESET_GRACE_MS / 60000U);
		LOG_PANIC();
		sys_reboot(SYS_REBOOT_COLD);
		break;
	default:
		break;
	}
}

void network_loop(void)
{
	struct recovery recovery;
	uint32_t retry_delay_ms = MQTT_RETRY_DELAY_MIN_MS;
	int ret;

	recovery_init(&recovery, k_uptime_get());

	wifi_join();
	wifi_log_diagnostics();
	mqtt_setup();

	while (true) {
		if (wifi_down()) {
			LOG_ERR("Wi-Fi down, rejoining");
			wifi_restart();
			k_event_clear(&net_events, NET_EVT_WIFI_DOWN);
			wifi_log_diagnostics();
			recovery_wifi_rejoined(&recovery);
		}

		sntp_sync_if_due();

		ret = mqtt_connect_and_wait();
		if (ret == 0) {
			bool published = false;

			retry_delay_ms = MQTT_RETRY_DELAY_MIN_MS;
			recovery_connected(&recovery);
			/*
			 * Without a sync the publish loop drops every queued reading
			 * older than one sample period, so a pending retry must not
			 * wait out its delay now that the network is back.
			 */
			sntp_sync_if_unsynced();
			ret = publish_until_disconnected(&published);
			apply_recovery(recovery_session_ended(&recovery, published, ret < 0,
							      k_uptime_get()),
				       "MQTT session dead without a disconnect");
			continue;
		}

		if (ret == -ENETDOWN) {
			continue;
		}

		apply_recovery(recovery_connect_failed(&recovery, k_uptime_get()),
			       "MQTT connect keeps failing");
		LOG_ERR("MQTT not connected, retrying in %u s", retry_delay_ms / 1000U);
		/* Wakes early if Wi-Fi is lost, so the rejoin is not held up by the backoff. */
		(void)k_event_wait(&net_events, NET_EVT_WIFI_DOWN, false, K_MSEC(retry_delay_ms));
		retry_delay_ms = next_retry_delay(retry_delay_ms, MQTT_RETRY_DELAY_MAX_MS);
	}
}
