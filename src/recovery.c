/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "recovery.h"

static enum recovery_action escalate(struct recovery *r)
{
	if (!r->recovered_once) {
		r->recovered_once = true;
		return RECOVERY_RESTART_WIFI;
	}
	return RECOVERY_RESET_MCU;
}

void recovery_init(struct recovery *r)
{
	r->recovered_once = false;
	r->fails_after_recovery = 0;
}

void recovery_wifi_rejoined(struct recovery *r)
{
	/*
	 * A rejoin after a lost link is a recovery too, so connects that keep
	 * failing afterwards still end in a reset.
	 */
	r->recovered_once = true;
	r->fails_after_recovery = 0;
}

void recovery_connected(struct recovery *r)
{
	r->fails_after_recovery = 0;
}

enum recovery_action recovery_session_ended(struct recovery *r, bool published, bool dead)
{
	if (published) {
		r->recovered_once = false;
	}
	return dead ? escalate(r) : RECOVERY_NONE;
}

enum recovery_action recovery_connect_failed(struct recovery *r)
{
	/*
	 * A clean failure normally means the broker is down. Right after a
	 * Wi-Fi restart it can also mean the link is wedged.
	 */
	if (r->recovered_once && (++r->fails_after_recovery >= RECOVERY_CONNECT_FAILS)) {
		return escalate(r);
	}
	return RECOVERY_NONE;
}
