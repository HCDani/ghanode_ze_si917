/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GHANODE_READING_H_
#define GHANODE_READING_H_

#include <stdbool.h>

#include <zephyr/kernel.h>

struct sensor_reading {
	float temperature_c;
	float humidity_pct;
	int light_lux;
};

/*
 * Puts a reading into a one-slot queue. A reading still in the slot has not
 * been published and is dropped in favour of the new one. Returns true when
 * that happened. Only one thread may put.
 */
bool reading_slot_put(struct k_msgq *slot, const struct sensor_reading *reading);

#endif /* GHANODE_READING_H_ */
