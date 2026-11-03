/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GHANODE_BACKOFF_H_
#define GHANODE_BACKOFF_H_

#include <stdint.h>

static inline uint32_t next_retry_delay(uint32_t delay_ms, uint32_t max_ms)
{
	return (delay_ms * 2U > max_ms) ? max_ms : delay_ms * 2U;
}

#endif /* GHANODE_BACKOFF_H_ */
