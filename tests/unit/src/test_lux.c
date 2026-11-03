/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include "lux.h"

ZTEST_SUITE(lux, NULL, NULL, NULL, NULL, NULL);

ZTEST(lux, test_full_scale_is_dark)
{
	zassert_equal(ldr_volts_to_lux(3.3f), 0);
	zassert_equal(ldr_volts_to_lux(3.299f), 0);
}

ZTEST(lux, test_zero_volts_is_capped)
{
	zassert_equal(ldr_volts_to_lux(0.0f), LIGHT_LUX_MAX);
	zassert_equal(ldr_volts_to_lux(-0.1f), LIGHT_LUX_MAX);
}

/* At R10 (15 kOhm) the LDR reads 10 lux: V = 3.3 * 15000 / (15000 + 4700). */
ZTEST(lux, test_r10_reads_ten_lux)
{
	zassert_equal(ldr_volts_to_lux(3.3f * 15000.0f / 19700.0f), 10);
}

ZTEST(lux, test_more_light_lowers_the_voltage)
{
	zassert_true(ldr_volts_to_lux(1.0f) > ldr_volts_to_lux(2.0f));
	zassert_true(ldr_volts_to_lux(2.0f) > ldr_volts_to_lux(3.0f));
}

ZTEST(lux, test_near_zero_volts_is_capped)
{
	zassert_equal(ldr_volts_to_lux(0.0001f), LIGHT_LUX_MAX);
}
