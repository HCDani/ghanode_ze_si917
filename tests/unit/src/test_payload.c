/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "payload.h"

#define SENSOR_ID "8C65A319AC98"
/* 2026-11-04T12:55:00.999Z */
#define SAMPLE_EPOCH_MS 1793796900999LL

ZTEST_SUITE(payload, NULL, NULL, NULL, NULL, NULL);

/* The exact text the FreeRTOS node published. */
ZTEST(payload, test_matches_the_freertos_payload)
{
	const struct sensor_reading r = {
		.temperature_c = 23.44f,
		.humidity_pct = 47.2f,
		.light_lux = 767,
	};
	const char *expected = "{\"Sensor ID\": \"8C65A319AC98\", \"temperature\":23.4, "
			       "\"humidity\": 47, \"light\": 767}";
	char buf[208];
	const int n = payload_format(buf, sizeof(buf), SENSOR_ID, &r, NULL);

	zassert_equal(n, (int)strlen(expected));
	zassert_str_equal(buf, expected);
}

/* Once SNTP has synced, the sample time is appended in UTC, truncated to whole seconds. */
ZTEST(payload, test_timestamp_is_appended_in_utc)
{
	const struct sensor_reading r = {
		.temperature_c = 23.44f,
		.humidity_pct = 47.2f,
		.light_lux = 767,
	};
	const int64_t epoch_ms = SAMPLE_EPOCH_MS;
	const char *expected = "{\"Sensor ID\": \"8C65A319AC98\", \"temperature\":23.4, "
			       "\"humidity\": 47, \"light\": 767, "
			       "\"timestamp\": \"2026-11-04T12:55:00Z\"}";
	char buf[208];
	const int n = payload_format(buf, sizeof(buf), SENSOR_ID, &r, &epoch_ms);

	zassert_equal(n, (int)strlen(expected));
	zassert_str_equal(buf, expected);
}

static void assert_utc(int64_t epoch_s, const char *utc)
{
	const struct sensor_reading r = {0};
	const int64_t epoch_ms = epoch_s * 1000;
	char expected[48];
	char buf[208];

	snprintf(expected, sizeof(expected), "\"timestamp\": \"%s\"}", utc);
	zassert_true(payload_format(buf, sizeof(buf), SENSOR_ID, &r, &epoch_ms) > 0);
	zassert_not_null(strstr(buf, expected), "%s", buf);
}

/* Epoch start, a year end, a leap day, a non-leap century and the last four-digit year. */
ZTEST(payload, test_utc_calendar_edges)
{
	assert_utc(0, "1970-01-01T00:00:00Z");
	assert_utc(1735689599, "2024-12-31T23:59:59Z");
	assert_utc(1835481599, "2028-02-29T23:59:59Z");
	assert_utc(4107542400, "2100-03-01T00:00:00Z");
	assert_utc(253402300799, "9999-12-31T23:59:59Z");
}

ZTEST(payload, test_time_out_of_range_is_rejected)
{
	const struct sensor_reading r = {0};
	const int64_t before_1970 = -1;
	const int64_t after_9999 = 253402300800000LL;
	char buf[208];

	zassert_equal(payload_format(buf, sizeof(buf), SENSOR_ID, &r, &before_1970), -EINVAL);
	zassert_equal(payload_format(buf, sizeof(buf), SENSOR_ID, &r, &after_9999), -EINVAL);
}

ZTEST(payload, test_humidity_rounds_half_up)
{
	const struct sensor_reading r = {.temperature_c = 20.0f, .humidity_pct = 49.5f};
	char buf[208];

	zassert_true(payload_format(buf, sizeof(buf), SENSOR_ID, &r, NULL) > 0);
	zassert_not_null(strstr(buf, "\"humidity\": 50,"), "%s", buf);
}

ZTEST(payload, test_negative_temperature)
{
	const struct sensor_reading r = {.temperature_c = -3.24f, .humidity_pct = 0.4f};
	char buf[208];

	zassert_true(payload_format(buf, sizeof(buf), SENSOR_ID, &r, NULL) > 0);
	zassert_not_null(strstr(buf, "\"temperature\":-3.2,"), "%s", buf);
	zassert_not_null(strstr(buf, "\"humidity\": 0,"), "%s", buf);
}

ZTEST(payload, test_too_small_buffer_is_rejected)
{
	const struct sensor_reading r = {0};
	const int64_t epoch_ms = SAMPLE_EPOCH_MS;
	char buf[16];

	zassert_equal(payload_format(buf, sizeof(buf), SENSOR_ID, &r, NULL), -EMSGSIZE);
	zassert_equal(payload_format(buf, sizeof(buf), SENSOR_ID, &r, &epoch_ms), -EMSGSIZE);
}

/* The largest values the sensors can report still fit the MQTT payload buffer. */
ZTEST(payload, test_worst_case_fits)
{
	const struct sensor_reading r = {
		.temperature_c = -46.85f,
		.humidity_pct = 100.0f,
		.light_lux = 100000,
	};
	/* 2099-12-31T23:59:59Z */
	const int64_t epoch_ms = 4102444799000LL;
	char buf[208];

	zassert_true(payload_format(buf, sizeof(buf), "FFFFFFFFFFFF", &r, &epoch_ms) > 0);
}
