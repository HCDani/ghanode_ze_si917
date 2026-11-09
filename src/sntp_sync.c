/*
 * Wall-clock time from SNTP. Readings carry the uptime they were sampled at;
 * the latest sync turns that into UTC when they are published, so readings
 * taken before the first sync or during an outage still get the right time.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/sntp.h>

#include "secrets.h"
#include "sntp_sync.h"
#include "timesync.h"

LOG_MODULE_REGISTER(sntp_sync, LOG_LEVEL_INF);

#define SNTP_QUERY_TIMEOUT_MS 3000

static struct timesync sync;
static int64_t next_attempt_ms;

static void sntp_sync_now(void)
{
	struct sntp_time answer;
	int64_t now;
	int64_t step_ms = 0;
	int ret;

	ret = sntp_simple(SECRET_SNTP_SERVER, SNTP_QUERY_TIMEOUT_MS, &answer);
	now = k_uptime_get();
	if (ret == 0) {
		ret = timesync_set(&sync, answer.seconds, answer.fraction, now, &step_ms);
	}

	if (ret < 0) {
		LOG_WRN("SNTP query to %s failed: %d, retrying in %u s", SECRET_SNTP_SERVER,
			ret, CONFIG_GHANODE_SNTP_RETRY_S);
		next_attempt_ms = now + (CONFIG_GHANODE_SNTP_RETRY_S * 1000LL);
		return;
	}

	LOG_INF("SNTP: %llu s since 1970 from %s, clock stepped %lld ms", answer.seconds,
		SECRET_SNTP_SERVER, step_ms);
	next_attempt_ms = now + (CONFIG_GHANODE_SNTP_RESYNC_S * 1000LL);
}

void sntp_sync_if_due(void)
{
	if (k_uptime_get() >= next_attempt_ms) {
		sntp_sync_now();
	}
}

void sntp_sync_if_unsynced(void)
{
	if (!sync.valid) {
		sntp_sync_now();
	}
}

bool sntp_synced(void)
{
	return sync.valid;
}

bool sntp_uptime_to_epoch_ms(int64_t uptime_ms, int64_t *epoch_ms)
{
	return timesync_to_epoch_ms(&sync, uptime_ms, epoch_ms);
}
