/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MOADER_NATIVE_LOG_SNAPSHOT_H
#define MOADER_NATIVE_LOG_SNAPSHOT_H
#include "moader/runtime_log_wire.h"
/* Metadata uses a one-attempt seqlock: plan_lock owners never wait on a
 * logger. The scratch area and publication fields belong to publish_lock. */
struct lfs_log_snapshot {
 uint64_t route_revision,generation,epoch_low,epoch_high;
 uint64_t sequence,last_cursor,last_generation,last_epoch_low,last_epoch_high;
 uint32_t last_report_failed,last_result,next_slot,publish_lock;
 struct MoaderLogHeader header;
 uint8_t payload[MOADER_LOG_PAYLOAD];
};
_Static_assert(sizeof(struct lfs_log_snapshot)==65704,"Snapshot context ABI");
#endif
