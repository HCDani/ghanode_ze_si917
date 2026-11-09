/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include "recovery.h"

#define BOOT_MS     1000
#define IN_GRACE    (BOOT_MS + 60000)
#define AFTER_GRACE (BOOT_MS + RECOVERY_RESET_GRACE_MS)

static struct recovery r;

static void reset_ladder(void *fixture)
{
	ARG_UNUSED(fixture);
	recovery_init(&r, BOOT_MS);
}

ZTEST_SUITE(recovery, NULL, NULL, reset_ladder, NULL, NULL);

/* A broker that is down from boot only backs off; nothing has been recovered yet. */
ZTEST(recovery, test_connect_failures_from_boot_never_escalate)
{
	for (int i = 0; i < 10; i++) {
		zassert_equal(recovery_connect_failed(&r, AFTER_GRACE + i), RECOVERY_NONE);
	}
}

ZTEST(recovery, test_three_failed_connects_after_rejoin_reset_after_grace)
{
	recovery_wifi_rejoined(&r);

	zassert_equal(recovery_connect_failed(&r, AFTER_GRACE), RECOVERY_NONE);
	zassert_equal(recovery_connect_failed(&r, AFTER_GRACE), RECOVERY_NONE);
	zassert_equal(recovery_connect_failed(&r, AFTER_GRACE), RECOVERY_RESET_MCU);
}

/*
 * The 5 Nov board run: the access point came back, but the route to the broker
 * took minutes longer. Inside the grace period the node keeps retrying, so the
 * queue survives; once it is over, the next failure resets.
 */
ZTEST(recovery, test_failed_connects_after_rejoin_wait_out_the_grace_period)
{
	recovery_wifi_rejoined(&r);

	for (int i = 0; i < 10; i++) {
		zassert_equal(recovery_connect_failed(&r, IN_GRACE + i), RECOVERY_NONE);
	}
	zassert_equal(recovery_connect_failed(&r, AFTER_GRACE), RECOVERY_RESET_MCU);
}

ZTEST(recovery, test_dead_session_restarts_wifi_then_resets_after_grace)
{
	recovery_connected(&r);
	zassert_equal(recovery_session_ended(&r, false, true, IN_GRACE), RECOVERY_RESTART_WIFI);

	recovery_wifi_rejoined(&r);
	recovery_connected(&r);
	zassert_equal(recovery_session_ended(&r, false, true, IN_GRACE), RECOVERY_NONE);

	recovery_connected(&r);
	zassert_equal(recovery_session_ended(&r, false, true, AFTER_GRACE), RECOVERY_RESET_MCU);
}

/* A publish that got through proves the restart worked, so the ladder starts over. */
ZTEST(recovery, test_publish_resets_the_ladder)
{
	recovery_wifi_rejoined(&r);
	recovery_connected(&r);
	zassert_equal(recovery_session_ended(&r, true, true, AFTER_GRACE), RECOVERY_RESTART_WIFI);
}

/* The grace period counts from the last acknowledged publish, not from boot. */
ZTEST(recovery, test_publish_restarts_the_grace_period)
{
	const int64_t published_at = AFTER_GRACE;

	recovery_connected(&r);
	(void)recovery_session_ended(&r, true, false, published_at);

	recovery_wifi_rejoined(&r);
	for (int i = 0; i < RECOVERY_CONNECT_FAILS; i++) {
		zassert_equal(recovery_connect_failed(&r, published_at + 60000), RECOVERY_NONE);
	}
	zassert_equal(recovery_connect_failed(&r, published_at + RECOVERY_RESET_GRACE_MS),
		      RECOVERY_RESET_MCU);
}

ZTEST(recovery, test_clean_disconnect_does_not_escalate)
{
	recovery_wifi_rejoined(&r);
	recovery_connected(&r);
	zassert_equal(recovery_session_ended(&r, false, false, AFTER_GRACE), RECOVERY_NONE);
}

ZTEST(recovery, test_successful_connect_resets_the_fail_count)
{
	recovery_wifi_rejoined(&r);
	zassert_equal(recovery_connect_failed(&r, AFTER_GRACE), RECOVERY_NONE);
	zassert_equal(recovery_connect_failed(&r, AFTER_GRACE), RECOVERY_NONE);

	recovery_connected(&r);
	zassert_equal(recovery_session_ended(&r, false, false, AFTER_GRACE), RECOVERY_NONE);

	zassert_equal(recovery_connect_failed(&r, AFTER_GRACE), RECOVERY_NONE);
	zassert_equal(recovery_connect_failed(&r, AFTER_GRACE), RECOVERY_NONE);
	zassert_equal(recovery_connect_failed(&r, AFTER_GRACE), RECOVERY_RESET_MCU);
}
