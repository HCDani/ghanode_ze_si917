/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GHANODE_PAYLOAD_H_
#define GHANODE_PAYLOAD_H_

#include <stddef.h>

#include "reading.h"

/*
 * Formats a reading as the JSON the GHA pipeline ingests. Key names and
 * spacing are part of that contract. Humidity is rounded to whole %RH.
 * Returns the length written, or -EMSGSIZE if it does not fit in len.
 */
int payload_format(char *buf, size_t len, const char *sensor_id,
		   const struct sensor_reading *reading);

#endif /* GHANODE_PAYLOAD_H_ */
