/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include "reading.h"

K_MSGQ_DEFINE(slot, sizeof(struct sensor_reading), 1, 4);

static void purge_slot(void *fixture)
{
	ARG_UNUSED(fixture);
	k_msgq_purge(&slot);
}

ZTEST_SUITE(reading_slot, NULL, NULL, purge_slot, NULL, NULL);

ZTEST(reading_slot, test_put_into_empty_slot_drops_nothing)
{
	const struct sensor_reading r = {.temperature_c = 21.0f};
	struct sensor_reading out;

	zassert_false(reading_slot_put(&slot, &r));
	zassert_ok(k_msgq_get(&slot, &out, K_NO_WAIT));
	zassert_equal(out.temperature_c, 21.0f);
}

ZTEST(reading_slot, test_newer_reading_replaces_unpublished_one)
{
	const struct sensor_reading older = {.temperature_c = 21.0f};
	const struct sensor_reading newer = {.temperature_c = 22.0f};
	struct sensor_reading out;

	zassert_false(reading_slot_put(&slot, &older));
	zassert_true(reading_slot_put(&slot, &newer));

	zassert_ok(k_msgq_get(&slot, &out, K_NO_WAIT));
	zassert_equal(out.temperature_c, 22.0f);
	zassert_equal(k_msgq_num_used_get(&slot), 0);
}
