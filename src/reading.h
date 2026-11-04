/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GHANODE_READING_H_
#define GHANODE_READING_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>

struct sensor_reading {
	/* k_uptime_get() when the sample was taken; turned into UTC at publish time. */
	int64_t uptime_ms;
	float temperature_c;
	float humidity_pct;
	int light_lux;
};

/*
 * Puts a reading into the queue. When the queue is full the oldest unpublished
 * reading is dropped to make room. Returns true when that happened. Only one
 * thread may put.
 */
bool reading_queue_put(struct k_msgq *queue, const struct sensor_reading *reading);

#endif /* GHANODE_READING_H_ */
