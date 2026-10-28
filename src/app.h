/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GHANODE_APP_H_
#define GHANODE_APP_H_

#include <zephyr/kernel.h>

struct sensor_reading {
	float temperature_c;
	float humidity_pct;
	int light_lux;
};

/* Filled by the sensor thread, drained by the network thread. */
extern struct k_msgq reading_queue;

/* net_events bits. Set from net_mgmt callbacks and the recovery path. */
#define NET_EVT_WIFI_DOWN BIT(0)

extern struct k_event net_events;

static inline uint32_t next_retry_delay(uint32_t delay_ms, uint32_t max_ms)
{
	return (delay_ms * 2U > max_ms) ? max_ms : delay_ms * 2U;
}

#endif /* GHANODE_APP_H_ */
