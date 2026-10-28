/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GHANODE_WIFI_H_
#define GHANODE_WIFI_H_

#include <stddef.h>

/* Registers event callbacks, sets the region and hostname, starts DHCP. */
int wifi_init(void);

/* Blocks until associated and holding a DHCP lease, retrying with backoff. */
void wifi_join(void);

/* Drops the association and joins again. */
void wifi_restart(void);

/* Logs the IP configuration and NWP firmware version, and pings the broker. */
void wifi_log_diagnostics(void);

/* STA MAC as 12 upper-case hex digits. */
void wifi_mac_string(char *buf, size_t len);

#endif /* GHANODE_WIFI_H_ */
