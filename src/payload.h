/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GHANODE_PAYLOAD_H_
#define GHANODE_PAYLOAD_H_

#include <stddef.h>
#include <stdint.h>

#include "reading.h"

/*
 * Formats a reading as JSON. Key names and spacing match the payload of the
 * FreeRTOS node. Humidity is rounded to whole %RH.
 * epoch_ms, when not NULL, is the sample time in milliseconds since 1970 and
 * is added as a "timestamp" in UTC, whole seconds.
 * Returns the length written, -EMSGSIZE if it does not fit in len, or
 * -EINVAL for a time that cannot be formatted.
 */
int payload_format(char *buf, size_t len, const char *sensor_id,
		   const struct sensor_reading *reading, const int64_t *epoch_ms);

#endif /* GHANODE_PAYLOAD_H_ */
