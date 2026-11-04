/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GHANODE_TIMESYNC_H_
#define GHANODE_TIMESYNC_H_

#include <stdbool.h>
#include <stdint.h>

/* Anything earlier is a server or parse error: 2025-01-01T00:00:00Z. */
#define TIMESYNC_MIN_EPOCH_S 1735689600ULL

/* Pairs one SNTP answer with the uptime it arrived at. Zero-initialise before use. */
struct timesync {
	bool valid;
	int64_t epoch_ms;
	int64_t uptime_ms;
};

/*
 * Records an SNTP answer (seconds and 2^-32 fractions since 1970) received at
 * uptime_ms. Returns -ERANGE and keeps the previous sync if the time is
 * implausible. *step_ms, if not NULL, gets how far the new sync moves the
 * clock compared to the previous one (0 on the first sync).
 */
int timesync_set(struct timesync *ts, uint64_t seconds, uint32_t fraction, int64_t uptime_ms,
		 int64_t *step_ms);

/*
 * Converts an uptime, earlier or later than the sync, to milliseconds since
 * 1970 UTC. Returns false if there has been no sync yet.
 */
bool timesync_to_epoch_ms(const struct timesync *ts, int64_t uptime_ms, int64_t *epoch_ms);

#endif /* GHANODE_TIMESYNC_H_ */
