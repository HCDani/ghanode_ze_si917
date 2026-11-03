/*
 * Recovery ladder: a dead MQTT session first restarts Wi-Fi. If no publish
 * gets through before the next failure, or RECOVERY_CONNECT_FAILS connects in
 * a row fail after a Wi-Fi restart or rejoin, the MCU is reset.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GHANODE_RECOVERY_H_
#define GHANODE_RECOVERY_H_

#include <stdbool.h>
#include <stdint.h>

#define RECOVERY_CONNECT_FAILS 3

enum recovery_action {
	RECOVERY_NONE,
	RECOVERY_RESTART_WIFI,
	RECOVERY_RESET_MCU,
};

struct recovery {
	bool recovered_once;
	uint32_t fails_after_recovery;
};

void recovery_init(struct recovery *r);

/* Wi-Fi came back after a loss or a restart. */
void recovery_wifi_rejoined(struct recovery *r);

/* The broker accepted a connect. */
void recovery_connected(struct recovery *r);

/*
 * A session ended. published: the broker acknowledged at least one reading.
 * dead: the session died without a disconnect event.
 */
enum recovery_action recovery_session_ended(struct recovery *r, bool published, bool dead);

/* A connect failed cleanly (refused or timed out), not because Wi-Fi was down. */
enum recovery_action recovery_connect_failed(struct recovery *r);

#endif /* GHANODE_RECOVERY_H_ */
