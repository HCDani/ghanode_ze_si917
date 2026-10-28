/*
 * Wi-Fi station bring-up and loss detection.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/hostname.h>
#include <zephyr/net/icmp.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>

#include "app.h"
#include "secrets.h"
#include "wifi.h"

LOG_MODULE_REGISTER(wifi, LOG_LEVEL_INF);

#define WIFI_SCAN_TIMEOUT_MS    30000
#define WIFI_CONNECT_TIMEOUT_MS 30000
#define WIFI_DHCP_TIMEOUT_MS    30000
#define WIFI_RETRY_DELAY_MIN_MS 5000
#define WIFI_RETRY_DELAY_MAX_MS 60000
#define ICMP_PROBE_TIMEOUT_MS   10000

/* wifi_events bits, posted from net_mgmt callbacks. */
#define WIFI_EVT_SCAN_DONE    BIT(0)
#define WIFI_EVT_CONNECTED    BIT(1)
#define WIFI_EVT_CONNECT_FAIL BIT(2)
#define WIFI_EVT_IPV4_BOUND   BIT(3)

#define WIFI_MGMT_EVENTS                                                                           \
	(NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE | NET_EVENT_WIFI_CONNECT_RESULT |   \
	 NET_EVENT_WIFI_DISCONNECT_RESULT)

static K_EVENT_DEFINE(wifi_events);
static struct net_mgmt_event_callback wifi_mgmt_cb;
static struct net_mgmt_event_callback ipv4_mgmt_cb;
static struct net_if *wifi_iface;

/* Set once associated with a lease, so a disconnect during a join attempt is not a loss. */
static atomic_t link_up;

/* Strongest AP matching the SSID in the last scan. Written only by the scan callback. */
static struct {
	bool found;
	uint8_t channel;
	int8_t rssi;
	enum wifi_security_type security;
	uint8_t bssid[WIFI_MAC_ADDR_LEN];
} target;

static void handle_scan_result(const struct wifi_scan_result *res)
{
	const size_t wanted_len = sizeof(SECRET_WIFI_SSID) - 1;

	LOG_INF("  ssid=\"%.*s\" ch=%u rssi=%d sec=%s bssid=%02X:%02X:%02X:%02X:%02X:%02X",
		res->ssid_length, res->ssid, res->channel, res->rssi,
		wifi_security_txt(res->security), res->mac[0], res->mac[1], res->mac[2],
		res->mac[3], res->mac[4], res->mac[5]);

	if ((res->ssid_length != wanted_len) ||
	    (memcmp(res->ssid, SECRET_WIFI_SSID, wanted_len) != 0)) {
		return;
	}
	/* Prefer stronger RSSI; on a tie keep the first match. */
	if (target.found && (res->rssi <= target.rssi)) {
		return;
	}
	target.found = true;
	target.rssi = res->rssi;
	target.channel = res->channel;
	target.security = res->security;
	memcpy(target.bssid, res->mac, sizeof(target.bssid));
}

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
				    struct net_if *iface)
{
	const struct wifi_status *status = cb->info;

	ARG_UNUSED(iface);

	switch (mgmt_event) {
	case NET_EVENT_WIFI_SCAN_RESULT:
		handle_scan_result(cb->info);
		break;
	case NET_EVENT_WIFI_SCAN_DONE:
		k_event_post(&wifi_events, WIFI_EVT_SCAN_DONE);
		break;
	case NET_EVENT_WIFI_CONNECT_RESULT:
		k_event_post(&wifi_events,
			     status->status == 0 ? WIFI_EVT_CONNECTED : WIFI_EVT_CONNECT_FAIL);
		break;
	case NET_EVENT_WIFI_DISCONNECT_RESULT:
		/*
		 * After beacon loss the NWP roams and rejoins on its own; this
		 * event means it gave up, and only a fresh join recovers.
		 */
		if (atomic_cas(&link_up, 1, 0)) {
			LOG_ERR("Wi-Fi link lost (reason %d)", status->status);
			k_event_post(&net_events, NET_EVT_WIFI_DOWN);
		}
		break;
	default:
		break;
	}
}

static void ipv4_mgmt_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
				    struct net_if *iface)
{
	ARG_UNUSED(cb);
	ARG_UNUSED(iface);

	if (mgmt_event == NET_EVENT_IPV4_DHCP_BOUND) {
		k_event_post(&wifi_events, WIFI_EVT_IPV4_BOUND);
	}
}

/* The NWP's default region is US, which only scans channels 1-11. */
static void wifi_set_region(void)
{
	struct wifi_reg_domain reg = {
		.oper = WIFI_MGMT_SET,
	};
	int ret;

	memcpy(reg.country_code, CONFIG_GHANODE_WIFI_COUNTRY, WIFI_COUNTRY_CODE_LEN);
	ret = net_mgmt(NET_REQUEST_WIFI_REG_DOMAIN, wifi_iface, &reg, sizeof(reg));
	if (ret < 0) {
		LOG_ERR("Failed to set Wi-Fi region %s: %d", CONFIG_GHANODE_WIFI_COUNTRY, ret);
		return;
	}
	LOG_INF("Wi-Fi region set from country %s", CONFIG_GHANODE_WIFI_COUNTRY);
}

int wifi_init(void)
{
	int ret;

	wifi_iface = net_if_get_wifi_sta();
	if (wifi_iface == NULL) {
		wifi_iface = net_if_get_default();
	}
	if (wifi_iface == NULL) {
		LOG_ERR("No Wi-Fi interface");
		return -ENODEV;
	}

	net_mgmt_init_event_callback(&wifi_mgmt_cb, wifi_mgmt_event_handler, WIFI_MGMT_EVENTS);
	net_mgmt_add_event_callback(&wifi_mgmt_cb);
	net_mgmt_init_event_callback(&ipv4_mgmt_cb, ipv4_mgmt_event_handler,
				     NET_EVENT_IPV4_DHCP_BOUND);
	net_mgmt_add_event_callback(&ipv4_mgmt_cb);

	ret = net_hostname_set(SECRET_WIFI_DHCP_HOSTNAME, sizeof(SECRET_WIFI_DHCP_HOSTNAME) - 1);
	if (ret < 0) {
		LOG_WRN("Failed to set DHCP hostname: %d", ret);
	}

	wifi_set_region();

	/* DHCP restarts by itself every time the interface comes back up. */
	net_dhcpv4_start(wifi_iface);
	return 0;
}

static int scan_for_target(void)
{
	int ret;

	memset(&target, 0, sizeof(target));
	k_event_clear(&wifi_events, WIFI_EVT_SCAN_DONE);

	/*
	 * The SiWx91x driver rejects an active scan whose dwell time is outside
	 * 5-1000 ms, and a request with no parameters does not fill one in.
	 */
	struct wifi_scan_params scan = {
		.scan_type = WIFI_SCAN_TYPE_ACTIVE,
		.bands = BIT(WIFI_FREQ_BAND_2_4_GHZ),
		.dwell_time_active = 100,
	};

	LOG_INF("Scanning for 2.4 GHz APs (looking for \"%s\")...", SECRET_WIFI_SSID);
	ret = net_mgmt(NET_REQUEST_WIFI_SCAN, wifi_iface, &scan, sizeof(scan));
	if (ret < 0) {
		LOG_ERR("Wi-Fi scan request failed: %d", ret);
		return ret;
	}

	if (k_event_wait(&wifi_events, WIFI_EVT_SCAN_DONE, false,
			 K_MSEC(WIFI_SCAN_TIMEOUT_MS)) == 0) {
		LOG_ERR("Wi-Fi scan timed out");
		return -ETIMEDOUT;
	}

	if (!target.found) {
		LOG_ERR("No matching AP \"%s\" in scan results", SECRET_WIFI_SSID);
		return -ENOENT;
	}
	return 0;
}

static int try_join(void)
{
	struct wifi_connect_req_params params = {
		.ssid = (const uint8_t *)SECRET_WIFI_SSID,
		.ssid_length = sizeof(SECRET_WIFI_SSID) - 1,
		.psk = (const uint8_t *)SECRET_WIFI_PASSWORD,
		.psk_length = sizeof(SECRET_WIFI_PASSWORD) - 1,
		.band = WIFI_FREQ_BAND_2_4_GHZ,
		.mfp = WIFI_MFP_OPTIONAL,
	};
	uint32_t evt;
	int ret;

	ret = scan_for_target();
	if (ret < 0) {
		return ret;
	}

	/* WPA3 APs (also in transition mode) scan as SAE and need PMF. */
	if (target.security == WIFI_SECURITY_TYPE_SAE) {
		params.security = WIFI_SECURITY_TYPE_SAE_AUTO;
		params.mfp = WIFI_MFP_REQUIRED;
	} else {
		params.security = target.security;
	}
	if (params.security == WIFI_SECURITY_TYPE_NONE) {
		params.psk = NULL;
		params.psk_length = 0;
	}
	/* The SiWx91x driver ignores the BSSID, so only the channel is pinned. */
	params.channel = target.channel;

	LOG_INF("Joining \"%s\" ch=%u rssi=%d sec=%s bssid=%02X:%02X:%02X:%02X:%02X:%02X",
		SECRET_WIFI_SSID, target.channel, target.rssi, wifi_security_txt(params.security),
		target.bssid[0], target.bssid[1], target.bssid[2], target.bssid[3],
		target.bssid[4], target.bssid[5]);

	k_event_clear(&wifi_events,
		      WIFI_EVT_CONNECTED | WIFI_EVT_CONNECT_FAIL | WIFI_EVT_IPV4_BOUND);
	ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, wifi_iface, &params, sizeof(params));
	if (ret < 0) {
		LOG_ERR("Wi-Fi connect request failed: %d", ret);
		return ret;
	}

	evt = k_event_wait(&wifi_events, WIFI_EVT_CONNECTED | WIFI_EVT_CONNECT_FAIL, false,
			   K_MSEC(WIFI_CONNECT_TIMEOUT_MS));
	if ((evt & WIFI_EVT_CONNECTED) == 0) {
		LOG_ERR("Wi-Fi association %s", evt == 0 ? "timed out" : "failed");
		return -ECONNREFUSED;
	}

	if (k_event_wait(&wifi_events, WIFI_EVT_IPV4_BOUND, false,
			 K_MSEC(WIFI_DHCP_TIMEOUT_MS)) == 0) {
		LOG_ERR("No DHCP lease within %u ms", WIFI_DHCP_TIMEOUT_MS);
		(void)net_mgmt(NET_REQUEST_WIFI_DISCONNECT, wifi_iface, NULL, 0);
		return -ETIMEDOUT;
	}

	atomic_set(&link_up, 1);
	return 0;
}

void wifi_join(void)
{
	uint32_t retry_delay_ms = WIFI_RETRY_DELAY_MIN_MS;

	/* Rescan on every attempt so the node can move to another AP if the first is gone. */
	while (try_join() < 0) {
		LOG_ERR("Wi-Fi join failed, retrying in %u s", retry_delay_ms / 1000U);
		k_msleep(retry_delay_ms);
		retry_delay_ms = next_retry_delay(retry_delay_ms, WIFI_RETRY_DELAY_MAX_MS);
	}
	LOG_INF("Wi-Fi client connected");
}

void wifi_restart(void)
{
	atomic_set(&link_up, 0);
	(void)net_mgmt(NET_REQUEST_WIFI_DISCONNECT, wifi_iface, NULL, 0);
	wifi_join();
}

void wifi_mac_string(char *buf, size_t len)
{
	const struct net_linkaddr *mac = net_if_get_link_addr(wifi_iface);

	if ((mac == NULL) || (mac->len < 6)) {
		snprintf(buf, len, "000000000000");
		return;
	}
	snprintf(buf, len, "%02X%02X%02X%02X%02X%02X", mac->addr[0], mac->addr[1], mac->addr[2],
		 mac->addr[3], mac->addr[4], mac->addr[5]);
}

static void log_ip_config(void)
{
	struct net_in_addr *addr = net_if_ipv4_get_global_addr(wifi_iface, NET_ADDR_PREFERRED);
	struct net_in_addr gw = net_if_ipv4_get_gw(wifi_iface);
	struct net_in_addr mask;
	char addr_str[NET_IPV4_ADDR_LEN];
	char gw_str[NET_IPV4_ADDR_LEN];
	char mask_str[NET_IPV4_ADDR_LEN];

	if (addr == NULL) {
		LOG_ERR("STA has no IPv4 address");
		return;
	}
	mask = net_if_ipv4_get_netmask_by_addr(wifi_iface, addr);

	net_addr_ntop(NET_AF_INET, addr, addr_str, sizeof(addr_str));
	net_addr_ntop(NET_AF_INET, &gw, gw_str, sizeof(gw_str));
	net_addr_ntop(NET_AF_INET, &mask, mask_str, sizeof(mask_str));
	LOG_INF("STA ip=%s gw=%s mask=%s host=%s", addr_str, gw_str, mask_str, net_hostname_get());
}

static void log_nwp_version(void)
{
	struct wifi_version version = {0};

	if (net_mgmt(NET_REQUEST_WIFI_VERSION, wifi_iface, &version, sizeof(version)) == 0) {
		LOG_INF("NWP fw %s, driver %s", version.fw_version, version.drv_version);
	}
}

static K_SEM_DEFINE(ping_sem, 0, 1);

static enum net_verdict icmp_reply_handler(struct net_icmp_ctx *ctx, struct net_pkt *pkt,
					   struct net_icmp_ip_hdr *ip_hdr,
					   struct net_icmp_hdr *icmp_hdr, void *user_data)
{
	ARG_UNUSED(ctx);
	ARG_UNUSED(pkt);
	ARG_UNUSED(ip_hdr);
	ARG_UNUSED(icmp_hdr);
	ARG_UNUSED(user_data);

	k_sem_give(&ping_sem);
	return NET_OK;
}

/* Tells a routing problem apart from a broker that refuses MQTT. */
static void icmp_probe_broker(void)
{
	struct net_icmp_ctx ctx;
	struct net_sockaddr_in dst = {
		.sin_family = NET_AF_INET,
	};
	const int64_t t0 = k_uptime_get();
	int ret;

	if (net_addr_pton(NET_AF_INET, SECRET_MQTT_BROKER_ADDR, &dst.sin_addr) < 0) {
		LOG_ERR("ICMP: bad broker address %s", SECRET_MQTT_BROKER_ADDR);
		return;
	}

	ret = net_icmp_init_ctx(&ctx, NET_AF_INET, NET_ICMPV4_ECHO_REPLY, 0, icmp_reply_handler);
	if (ret < 0) {
		LOG_ERR("ICMP: init failed: %d", ret);
		return;
	}

	k_sem_reset(&ping_sem);
	LOG_INF("ICMP: ping %s ...", SECRET_MQTT_BROKER_ADDR);
	ret = net_icmp_send_echo_request(&ctx, wifi_iface, (struct net_sockaddr *)&dst, NULL,
					 NULL);
	if (ret < 0) {
		LOG_ERR("ICMP: send failed: %d", ret);
	} else if (k_sem_take(&ping_sem, K_MSEC(ICMP_PROBE_TIMEOUT_MS)) < 0) {
		LOG_ERR("ICMP: no reply after %u ms", ICMP_PROBE_TIMEOUT_MS);
	} else {
		LOG_INF("ICMP: reply after %lld ms", k_uptime_get() - t0);
	}

	(void)net_icmp_cleanup_ctx(&ctx);
}

void wifi_log_diagnostics(void)
{
	log_ip_config();
	log_nwp_version();
	icmp_probe_broker();
}
