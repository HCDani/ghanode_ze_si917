/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "recovery.h"

static enum recovery_action escalate(struct recovery *r, int64_t now_ms)
{
	if (!r->recovered_once) {
		r->recovered_once = true;
		return RECOVERY_RESTART_WIFI;
	}
	if ((now_ms - r->last_ok_ms) < RECOVERY_RESET_GRACE_MS) {
		return RECOVERY_NONE;
	}
	return RECOVERY_RESET_MCU;
}

void recovery_init(struct recovery *r, int64_t now_ms)
{
	r->recovered_once = false;
	r->fails_after_recovery = 0;
	r->last_ok_ms = now_ms;
}

void recovery_wifi_rejoined(struct recovery *r)
{
	/*
	 * A rejoin after a lost link is a recovery too, so connects that keep
	 * failing afterwards still end in a reset once the grace period is over.
	 */
	r->recovered_once = true;
	r->fails_after_recovery = 0;
}

void recovery_connected(struct recovery *r)
{
	r->fails_after_recovery = 0;
}

enum recovery_action recovery_session_ended(struct recovery *r, bool published, bool dead,
					    int64_t now_ms)
{
	if (published) {
		r->recovered_once = false;
		r->last_ok_ms = now_ms;
	}
	return dead ? escalate(r, now_ms) : RECOVERY_NONE;
}

enum recovery_action recovery_connect_failed(struct recovery *r, int64_t now_ms)
{
	/*
	 * A clean failure normally means the broker is down. Right after a
	 * Wi-Fi restart it can also mean the link is wedged.
	 */
	if (r->recovered_once && (++r->fails_after_recovery >= RECOVERY_CONNECT_FAILS)) {
		return escalate(r, now_ms);
	}
	return RECOVERY_NONE;
}
