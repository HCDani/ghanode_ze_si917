/*
 * ghanode: SiWx917 sensor node publishing Si7021 and light readings over MQTT.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/fatal.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>

#include "app.h"
#include "mqtt_link.h"
#include "sensors.h"
#include "wifi.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define NET_STACK_SIZE    4096
#define SENSOR_STACK_SIZE 2048
/* The sensor thread outranks the network thread so a slow reconnect never delays a sample. */
#define SENSOR_PRIORITY   K_PRIO_PREEMPT(10)
#define NET_PRIORITY      K_PRIO_PREEMPT(11)

/* Stack high-water marks are logged after the first sample, then hourly. */
#define STACK_REPORT_EVERY_N_SAMPLES (3600000U / CONFIG_GHANODE_SAMPLE_INTERVAL_MS)

/* One slot. The payload has no timestamp, so a backlog would be stamped with the
 * reconnect time. A newer sample replaces the unpublished one.
 */
K_MSGQ_DEFINE(reading_queue, sizeof(struct sensor_reading), 1, 4);
K_EVENT_DEFINE(net_events);

/* Written by the fatal error handler for a debugger to read. */
const char *volatile fatal_thread_name;

static void sensor_thread_fn(void *p1, void *p2, void *p3);
static void net_thread_fn(void *p1, void *p2, void *p3);

/* Started from main() once the sensors are up. */
K_THREAD_DEFINE(sensor_tid, SENSOR_STACK_SIZE, sensor_thread_fn, NULL, NULL, NULL,
		SENSOR_PRIORITY, 0, SYS_FOREVER_MS);
K_THREAD_DEFINE(net_tid, NET_STACK_SIZE, net_thread_fn, NULL, NULL, NULL, NET_PRIORITY, 0,
		SYS_FOREVER_MS);

/* Only this thread puts. A sample that is still queued has not been published,
 * so it is dropped in favour of the new one.
 */
static void enqueue_reading(const struct sensor_reading *reading)
{
	struct sensor_reading discarded;

	if (k_msgq_get(&reading_queue, &discarded, K_NO_WAIT) == 0) {
		LOG_INF("Dropped an unpublished reading");
	}
	(void)k_msgq_put(&reading_queue, reading, K_NO_WAIT);
}

static void log_stack_high_water(void)
{
	size_t net_unused = 0;
	size_t sensor_unused = 0;

	(void)k_thread_stack_space_get(net_tid, &net_unused);
	(void)k_thread_stack_space_get(sensor_tid, &sensor_unused);
	LOG_INF("Stack unused at high-water: net %zu of %u B, sensor %zu of %u B", net_unused,
		NET_STACK_SIZE, sensor_unused, SENSOR_STACK_SIZE);
}

/*
 * Producer: samples on a fixed period and never touches the network, so
 * readings keep coming while Wi-Fi or the broker is down.
 */
static void sensor_thread_fn(void *p1, void *p2, void *p3)
{
	int64_t next_wake = k_uptime_get();
	uint32_t samples = 0;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		struct sensor_reading reading = {0};
		float volts = 0.0f;
		int ret;

		ret = si7021_read(&reading.temperature_c, &reading.humidity_pct);
		if (ret == 0) {
			LOG_INF("Si7021: %.2f C, %.2f %%RH", (double)reading.temperature_c,
				(double)reading.humidity_pct);

			ret = light_read(&volts, &reading.light_lux);
			if (ret == 0) {
				LOG_INF("Light on GPIO_25: %.3f V, %d lux", (double)volts,
					reading.light_lux);
			} else {
				LOG_ERR("Light: ADC read failed: %d", ret);
			}
			enqueue_reading(&reading);
		} else {
			/*
			 * A failed measurement would leave 0 C / 0 %RH, which passes
			 * the pipeline's numeric check as a real reading.
			 */
			LOG_ERR("Si7021: measurement failed: %d, reading skipped", ret);
		}

		if ((samples % STACK_REPORT_EVERY_N_SAMPLES) == 0U) {
			log_stack_high_water();
		}
		samples++;

		/* An absolute wake-up keeps the period fixed however long the reads took. */
		next_wake += CONFIG_GHANODE_SAMPLE_INTERVAL_MS;
		k_sleep(K_TIMEOUT_ABS_MS(next_wake));
	}
}

static void net_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	network_loop();
}

/*
 * Stack overflows (MPU guard) and other fatal errors end up here. Memory may
 * already be corrupt, so only record the thread for a debugger and stop.
 */
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);

	fatal_thread_name = k_thread_name_get(k_current_get());
	LOG_PANIC();
	LOG_ERR("Fatal error %u in thread %s, halting", reason,
		fatal_thread_name != NULL ? fatal_thread_name : "?");

	(void)arch_irq_lock();
	for (;;) {
	}
}

int main(void)
{
	int ret;

	/* The NWP has already booted (POST_KERNEL) before any ULP peripheral is touched here. */
	LOG_INF("ghanode starting");

	ret = sensors_init();
	if (ret < 0) {
		LOG_ERR("Sensor bring-up failed: %d", ret);
		return 0;
	}

	ret = wifi_init();
	if (ret < 0) {
		LOG_ERR("Wi-Fi init failed: %d", ret);
		return 0;
	}

	k_thread_name_set(sensor_tid, "sensor");
	k_thread_name_set(net_tid, "net");
	k_thread_start(sensor_tid);
	k_thread_start(net_tid);
	return 0;
}
