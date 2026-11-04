/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/ztest.h>

#include "timesync.h"

/* 2026-11-04T12:55:00Z */
#define SYNC_EPOCH_S  1793796900ULL
#define SYNC_EPOCH_MS 1793796900000LL
#define SYNC_UPTIME   600000LL
/* 0.5 s in NTP's 2^-32 s fractions */
#define HALF_SECOND   0x80000000U

ZTEST_SUITE(timesync, NULL, NULL, NULL, NULL, NULL);

ZTEST(timesync, test_no_time_before_the_first_sync)
{
	const struct timesync ts = {0};
	int64_t epoch_ms = 0;

	zassert_false(timesync_to_epoch_ms(&ts, SYNC_UPTIME, &epoch_ms));
}

ZTEST(timesync, test_implausible_answer_is_rejected)
{
	struct timesync ts = {0};
	int64_t epoch_ms = 0;

	zassert_equal(timesync_set(&ts, TIMESYNC_MIN_EPOCH_S - 1U, 0, SYNC_UPTIME, NULL),
		      -ERANGE);
	zassert_false(timesync_to_epoch_ms(&ts, SYNC_UPTIME, &epoch_ms));
}

ZTEST(timesync, test_fraction_becomes_milliseconds)
{
	struct timesync ts = {0};
	int64_t epoch_ms = 0;

	zassert_ok(timesync_set(&ts, SYNC_EPOCH_S, HALF_SECOND, SYNC_UPTIME, NULL));
	zassert_true(timesync_to_epoch_ms(&ts, SYNC_UPTIME, &epoch_ms));
	zassert_equal(epoch_ms, SYNC_EPOCH_MS + 500);
}

/* A reading queued during an outage, before the node got its first time. */
ZTEST(timesync, test_reading_taken_before_the_sync)
{
	struct timesync ts = {0};
	int64_t epoch_ms = 0;

	zassert_ok(timesync_set(&ts, SYNC_EPOCH_S, 0, SYNC_UPTIME, NULL));
	zassert_true(timesync_to_epoch_ms(&ts, SYNC_UPTIME - 20000, &epoch_ms));
	zassert_equal(epoch_ms, SYNC_EPOCH_MS - 20000);
}

ZTEST(timesync, test_reading_taken_after_the_sync)
{
	struct timesync ts = {0};
	int64_t epoch_ms = 0;

	zassert_ok(timesync_set(&ts, SYNC_EPOCH_S, 0, SYNC_UPTIME, NULL));
	zassert_true(timesync_to_epoch_ms(&ts, SYNC_UPTIME + 3600000, &epoch_ms));
	zassert_equal(epoch_ms, SYNC_EPOCH_MS + 3600000);
}

/* An hour later the local clock has drifted 250 ms; the resync reports and applies that. */
ZTEST(timesync, test_resync_reports_the_step)
{
	struct timesync ts = {0};
	int64_t step_ms = -1;
	int64_t epoch_ms = 0;

	zassert_ok(timesync_set(&ts, SYNC_EPOCH_S, 0, SYNC_UPTIME, &step_ms));
	zassert_equal(step_ms, 0);

	zassert_ok(timesync_set(&ts, SYNC_EPOCH_S + 3600U, 0, SYNC_UPTIME + 3599750, &step_ms));
	zassert_equal(step_ms, 250);
	zassert_true(timesync_to_epoch_ms(&ts, SYNC_UPTIME + 3599750, &epoch_ms));
	zassert_equal(epoch_ms, SYNC_EPOCH_MS + 3600000);
}

ZTEST(timesync, test_rejected_resync_keeps_the_previous_time)
{
	struct timesync ts = {0};
	int64_t epoch_ms = 0;

	zassert_ok(timesync_set(&ts, SYNC_EPOCH_S, 0, SYNC_UPTIME, NULL));
	zassert_equal(timesync_set(&ts, 0, 0, SYNC_UPTIME + 1000, NULL), -ERANGE);
	zassert_true(timesync_to_epoch_ms(&ts, SYNC_UPTIME, &epoch_ms));
	zassert_equal(epoch_ms, SYNC_EPOCH_MS);
}
