/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>

#include "payload.h"

/* "2026-11-04T12:55:00Z" + NUL */
#define UTC_TEXT_LEN 21
#define SECS_PER_DAY 86400
/* 9999-12-31T23:59:59Z, the last time with a four-digit year. */
#define UTC_MAX_EPOCH_S 253402300799LL

/*
 * Days since 1970-01-01 to a proleptic Gregorian date (Howard Hinnant's
 * civil_from_days), restricted to days >= 0.
 */
static void civil_from_days(int64_t days, int *year, int *month, int *day)
{
	const int64_t z = days + 719468;
	const int64_t era = z / 146097;
	const int64_t doe = z - era * 146097;
	const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	const int64_t mp = (5 * doy + 2) / 153;
	const int64_t m = (mp < 10) ? (mp + 3) : (mp - 9);

	*year = (int)(yoe + era * 400 + ((m <= 2) ? 1 : 0));
	*month = (int)m;
	*day = (int)(doy - (153 * mp + 2) / 5 + 1);
}

static int format_utc(char *buf, size_t len, int64_t epoch_ms)
{
	const int64_t secs = epoch_ms / 1000;
	const int64_t sod = secs % SECS_PER_DAY;
	int year;
	int month;
	int day;
	int n;

	if ((epoch_ms < 0) || (secs > UTC_MAX_EPOCH_S)) {
		return -EINVAL;
	}
	civil_from_days(secs / SECS_PER_DAY, &year, &month, &day);
	n = snprintf(buf, len, "%04d-%02d-%02dT%02d:%02d:%02dZ", year, month, day,
		     (int)(sod / 3600), (int)((sod / 60) % 60), (int)(sod % 60));
	if ((n <= 0) || ((size_t)n >= len)) {
		return -EINVAL;
	}
	return 0;
}

int payload_format(char *buf, size_t len, const char *sensor_id,
		   const struct sensor_reading *reading, const int64_t *epoch_ms)
{
	const double temperature = (double)reading->temperature_c;
	const int humidity = (int)(reading->humidity_pct + 0.5f);
	char utc[UTC_TEXT_LEN];
	int n;

	if (epoch_ms == NULL) {
		n = snprintf(buf, len,
			     "{\"Sensor ID\": \"%s\", \"temperature\":%.1f, \"humidity\": %d, "
			     "\"light\": %d}",
			     sensor_id, temperature, humidity, reading->light_lux);
	} else {
		if (format_utc(utc, sizeof(utc), *epoch_ms) < 0) {
			return -EINVAL;
		}
		n = snprintf(buf, len,
			     "{\"Sensor ID\": \"%s\", \"temperature\":%.1f, \"humidity\": %d, "
			     "\"light\": %d, \"timestamp\": \"%s\"}",
			     sensor_id, temperature, humidity, reading->light_lux, utc);
	}

	if ((n <= 0) || ((size_t)n >= len)) {
		return -EMSGSIZE;
	}
	return n;
}
