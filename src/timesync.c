/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>

#include "timesync.h"

int timesync_set(struct timesync *ts, uint64_t seconds, uint32_t fraction, int64_t uptime_ms,
		 int64_t *step_ms)
{
	int64_t epoch_ms;

	if (seconds < TIMESYNC_MIN_EPOCH_S) {
		return -ERANGE;
	}

	epoch_ms = (int64_t)(seconds * 1000U) + (int64_t)(((uint64_t)fraction * 1000U) >> 32);

	if (step_ms != NULL) {
		int64_t before = epoch_ms;

		if (!timesync_to_epoch_ms(ts, uptime_ms, &before)) {
			before = epoch_ms;
		}
		*step_ms = epoch_ms - before;
	}

	ts->epoch_ms = epoch_ms;
	ts->uptime_ms = uptime_ms;
	ts->valid = true;
	return 0;
}

bool timesync_to_epoch_ms(const struct timesync *ts, int64_t uptime_ms, int64_t *epoch_ms)
{
	if (!ts->valid) {
		return false;
	}
	*epoch_ms = ts->epoch_ms + (uptime_ms - ts->uptime_ms);
	return true;
}
