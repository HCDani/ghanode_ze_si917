/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include "backoff.h"

ZTEST_SUITE(backoff, NULL, NULL, NULL, NULL, NULL);

ZTEST(backoff, test_doubles_up_to_the_cap)
{
	const uint32_t expected[] = {10000, 20000, 40000, 60000, 60000};
	uint32_t delay = 5000;

	for (size_t i = 0; i < ARRAY_SIZE(expected); i++) {
		delay = next_retry_delay(delay, 60000);
		zassert_equal(delay, expected[i], "step %zu: %u", i, delay);
	}
}

ZTEST(backoff, test_never_exceeds_the_cap)
{
	zassert_equal(next_retry_delay(40000, 60000), 60000);
	zassert_equal(next_retry_delay(60000, 60000), 60000);
}
