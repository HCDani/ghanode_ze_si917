/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "reading.h"

bool reading_slot_put(struct k_msgq *slot, const struct sensor_reading *reading)
{
	struct sensor_reading discarded;
	const bool dropped = (k_msgq_get(slot, &discarded, K_NO_WAIT) == 0);

	(void)k_msgq_put(slot, reading, K_NO_WAIT);
	return dropped;
}
