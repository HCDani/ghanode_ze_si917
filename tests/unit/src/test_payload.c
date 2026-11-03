/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "payload.h"

#define SENSOR_ID "8C65A319AC98"

ZTEST_SUITE(payload, NULL, NULL, NULL, NULL, NULL);

/* The exact text the GHA ingest pipeline parses. */
ZTEST(payload, test_matches_the_ingest_contract)
{
	const struct sensor_reading r = {
		.temperature_c = 23.44f,
		.humidity_pct = 47.2f,
		.light_lux = 767,
	};
	const char *expected = "{\"Sensor ID\": \"8C65A319AC98\", \"temperature\":23.4, "
			       "\"humidity\": 47, \"light\": 767}";
	char buf[208];
	const int n = payload_format(buf, sizeof(buf), SENSOR_ID, &r);

	zassert_equal(n, (int)strlen(expected));
	zassert_str_equal(buf, expected);
}

ZTEST(payload, test_humidity_rounds_half_up)
{
	const struct sensor_reading r = {.temperature_c = 20.0f, .humidity_pct = 49.5f};
	char buf[208];

	zassert_true(payload_format(buf, sizeof(buf), SENSOR_ID, &r) > 0);
	zassert_not_null(strstr(buf, "\"humidity\": 50,"), "%s", buf);
}

ZTEST(payload, test_negative_temperature)
{
	const struct sensor_reading r = {.temperature_c = -3.24f, .humidity_pct = 0.4f};
	char buf[208];

	zassert_true(payload_format(buf, sizeof(buf), SENSOR_ID, &r) > 0);
	zassert_not_null(strstr(buf, "\"temperature\":-3.2,"), "%s", buf);
	zassert_not_null(strstr(buf, "\"humidity\": 0,"), "%s", buf);
}

ZTEST(payload, test_too_small_buffer_is_rejected)
{
	const struct sensor_reading r = {0};
	char buf[16];

	zassert_equal(payload_format(buf, sizeof(buf), SENSOR_ID, &r), -EMSGSIZE);
}

/* The largest values the sensors can report still fit the MQTT payload buffer. */
ZTEST(payload, test_worst_case_fits)
{
	const struct sensor_reading r = {
		.temperature_c = -46.85f,
		.humidity_pct = 100.0f,
		.light_lux = 100000,
	};
	char buf[208];

	zassert_true(payload_format(buf, sizeof(buf), "FFFFFFFFFFFF", &r) > 0);
}
