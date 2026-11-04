/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GHANODE_SNTP_SYNC_H_
#define GHANODE_SNTP_SYNC_H_

#include <stdbool.h>
#include <stdint.h>

/*
 * Queries the SNTP server if a sync is due: at once until the first success,
 * then every CONFIG_GHANODE_SNTP_RESYNC_S. A failed query is retried after
 * CONFIG_GHANODE_SNTP_RETRY_S. Blocks the caller for up to a few seconds.
 * Network thread only.
 */
void sntp_sync_if_due(void);

/* True once any SNTP query has succeeded. Network thread only. */
bool sntp_synced(void);

/*
 * Converts an uptime to milliseconds since 1970 UTC. Returns false before the
 * first sync. Network thread only.
 */
bool sntp_uptime_to_epoch_ms(int64_t uptime_ms, int64_t *epoch_ms);

#endif /* GHANODE_SNTP_SYNC_H_ */
