/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include "recovery.h"

static struct recovery r;

static void reset_ladder(void *fixture)
{
	ARG_UNUSED(fixture);
	recovery_init(&r);
}

ZTEST_SUITE(recovery, NULL, NULL, reset_ladder, NULL, NULL);

/* A broker that is down from boot only backs off; nothing has been recovered yet. */
ZTEST(recovery, test_connect_failures_from_boot_never_escalate)
{
	for (int i = 0; i < 10; i++) {
		zassert_equal(recovery_connect_failed(&r), RECOVERY_NONE);
	}
}

ZTEST(recovery, test_three_failed_connects_after_rejoin_reset)
{
	recovery_wifi_rejoined(&r);

	zassert_equal(recovery_connect_failed(&r), RECOVERY_NONE);
	zassert_equal(recovery_connect_failed(&r), RECOVERY_NONE);
	zassert_equal(recovery_connect_failed(&r), RECOVERY_RESET_MCU);
}

ZTEST(recovery, test_dead_session_restarts_wifi_then_resets)
{
	recovery_connected(&r);
	zassert_equal(recovery_session_ended(&r, false, true), RECOVERY_RESTART_WIFI);

	recovery_wifi_rejoined(&r);
	recovery_connected(&r);
	zassert_equal(recovery_session_ended(&r, false, true), RECOVERY_RESET_MCU);
}

/* A publish that got through proves the restart worked, so the ladder starts over. */
ZTEST(recovery, test_publish_resets_the_ladder)
{
	recovery_wifi_rejoined(&r);
	recovery_connected(&r);
	zassert_equal(recovery_session_ended(&r, true, true), RECOVERY_RESTART_WIFI);
}

ZTEST(recovery, test_clean_disconnect_does_not_escalate)
{
	recovery_wifi_rejoined(&r);
	recovery_connected(&r);
	zassert_equal(recovery_session_ended(&r, false, false), RECOVERY_NONE);
}

ZTEST(recovery, test_successful_connect_resets_the_fail_count)
{
	recovery_wifi_rejoined(&r);
	zassert_equal(recovery_connect_failed(&r), RECOVERY_NONE);
	zassert_equal(recovery_connect_failed(&r), RECOVERY_NONE);

	recovery_connected(&r);
	zassert_equal(recovery_session_ended(&r, false, false), RECOVERY_NONE);

	zassert_equal(recovery_connect_failed(&r), RECOVERY_NONE);
	zassert_equal(recovery_connect_failed(&r), RECOVERY_NONE);
	zassert_equal(recovery_connect_failed(&r), RECOVERY_RESET_MCU);
}
