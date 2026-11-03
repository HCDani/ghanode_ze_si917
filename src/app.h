/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GHANODE_APP_H_
#define GHANODE_APP_H_

#include <zephyr/kernel.h>

#include "reading.h"

/* Filled by the sensor thread, drained by the network thread. */
extern struct k_msgq reading_queue;

/* net_events bits. Set from net_mgmt callbacks and the recovery path. */
#define NET_EVT_WIFI_DOWN BIT(0)

extern struct k_event net_events;

#endif /* GHANODE_APP_H_ */
