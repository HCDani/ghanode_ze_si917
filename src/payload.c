/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>

#include "payload.h"

int payload_format(char *buf, size_t len, const char *sensor_id,
		   const struct sensor_reading *reading)
{
	const int n = snprintf(buf, len,
			       "{\"Sensor ID\": \"%s\", \"temperature\":%.1f, \"humidity\": %d, "
			       "\"light\": %d}",
			       sensor_id, (double)reading->temperature_c,
			       (int)(reading->humidity_pct + 0.5f), reading->light_lux);

	if ((n <= 0) || ((size_t)n >= len)) {
		return -EMSGSIZE;
	}
	return n;
}
