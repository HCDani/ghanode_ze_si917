/*
 * Recovery ladder: a dead MQTT session first restarts Wi-Fi. If no publish
 * gets through before the next failure, or RECOVERY_CONNECT_FAILS connects in
 * a row fail after a Wi-Fi restart or rejoin, the MCU is reset, but only once
 * RECOVERY_RESET_GRACE_MS have passed without an acknowledged publish. The
 * reading queue is in RAM, so a reset inside that window would throw away the
 * readings it is there to bridge.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GHANODE_RECOVERY_H_
#define GHANODE_RECOVERY_H_

#include <stdbool.h>
#include <stdint.h>

#define RECOVERY_CONNECT_FAILS  3
#define RECOVERY_RESET_GRACE_MS (10 * 60 * 1000)

enum recovery_action {
	RECOVERY_NONE,
	RECOVERY_RESTART_WIFI,
	RECOVERY_RESET_MCU,
};

struct recovery {
	bool recovered_once;
	uint32_t fails_after_recovery;
	/* Uptime of the last acknowledged publish, or of boot. */
	int64_t last_ok_ms;
};

void recovery_init(struct recovery *r, int64_t now_ms);

/* Wi-Fi came back after a loss or a restart. */
void recovery_wifi_rejoined(struct recovery *r);

/* The broker accepted a connect. */
void recovery_connected(struct recovery *r);

/*
 * A session ended. published: the broker acknowledged at least one reading.
 * dead: the session died without a disconnect event.
 */
enum recovery_action recovery_session_ended(struct recovery *r, bool published, bool dead,
					    int64_t now_ms);

/* A connect failed cleanly (refused or timed out), not because Wi-Fi was down. */
enum recovery_action recovery_connect_failed(struct recovery *r, int64_t now_ms);

#endif /* GHANODE_RECOVERY_H_ */
