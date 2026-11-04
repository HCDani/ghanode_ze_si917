/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include "reading.h"

#define QUEUE_LEN 3

K_MSGQ_DEFINE(queue, sizeof(struct sensor_reading), QUEUE_LEN, 8);

static void purge_queue(void *fixture)
{
	ARG_UNUSED(fixture);
	k_msgq_purge(&queue);
}

static void put_n(int n)
{
	for (int i = 0; i < n; i++) {
		const struct sensor_reading r = {.uptime_ms = i};

		(void)reading_queue_put(&queue, &r);
	}
}

ZTEST_SUITE(reading_queue, NULL, NULL, purge_queue, NULL, NULL);

ZTEST(reading_queue, test_put_into_empty_queue_drops_nothing)
{
	const struct sensor_reading r = {.uptime_ms = 20000, .temperature_c = 21.0f};
	struct sensor_reading out;

	zassert_false(reading_queue_put(&queue, &r));
	zassert_ok(k_msgq_get(&queue, &out, K_NO_WAIT));
	zassert_equal(out.uptime_ms, 20000);
	zassert_equal(out.temperature_c, 21.0f);
}

ZTEST(reading_queue, test_readings_come_out_oldest_first)
{
	struct sensor_reading out;

	put_n(QUEUE_LEN);
	for (int i = 0; i < QUEUE_LEN; i++) {
		zassert_ok(k_msgq_get(&queue, &out, K_NO_WAIT));
		zassert_equal(out.uptime_ms, i);
	}
}

ZTEST(reading_queue, test_full_queue_drops_the_oldest)
{
	const struct sensor_reading newest = {.uptime_ms = 100};
	struct sensor_reading out;

	put_n(QUEUE_LEN);
	zassert_true(reading_queue_put(&queue, &newest));
	zassert_equal(k_msgq_num_used_get(&queue), QUEUE_LEN);

	zassert_ok(k_msgq_get(&queue, &out, K_NO_WAIT));
	zassert_equal(out.uptime_ms, 1);
	zassert_ok(k_msgq_get(&queue, &out, K_NO_WAIT));
	zassert_equal(out.uptime_ms, 2);
	zassert_ok(k_msgq_get(&queue, &out, K_NO_WAIT));
	zassert_equal(out.uptime_ms, 100);
}
