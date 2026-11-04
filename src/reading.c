/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "reading.h"

bool reading_queue_put(struct k_msgq *queue, const struct sensor_reading *reading)
{
	struct sensor_reading discarded;
	bool dropped = false;

	while (k_msgq_put(queue, reading, K_NO_WAIT) != 0) {
		if (k_msgq_get(queue, &discarded, K_NO_WAIT) == 0) {
			dropped = true;
		}
	}
	return dropped;
}
