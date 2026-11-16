/* SPDX-License-Identifier: ISC */
#pragma once

#include <stdint.h>
#include <stddef.h>

/* Optional diagnostic extension. This is independent of protocol 7 and is
 * feature-detected. Reading or accepting these frames must only copy memory:
 * no service acquisition, FS/PM/RO call, cleanup, or native handle transfer. */
#define KAIHEN_CENSUS_MAGIC UINT32_C(0x31534348) /* HCS1, little endian */
#define KAIHEN_CENSUS_SCHEMA 1
#define KAIHEN_CENSUS_CAPACITY 64
#define KAIHEN_CENSUS_HEADER_COMMAND UINT32_C(0x4d4f0010)
#define KAIHEN_CENSUS_RECORD_COMMAND UINT32_C(0x4d4f0011)
#define KAIHEN_CENSUS_HEADER_OVERWRITTEN UINT32_C(1)
#define KAIHEN_CENSUS_SOURCE_MASK(source) (UINT32_C(1) << (source))
#define KAIHEN_CENSUS_KNOWN_SOURCE_MASK UINT32_C(14)
/* Completed receiver replies: Busy confirms the frame was not accepted and
 * may be sent later; Rejected is terminal for this diagnostic stream. */
#define KAIHEN_CENSUS_RECEIVER_BUSY (UINT32_C(345) | (UINT32_C(9) << 9))
#define KAIHEN_CENSUS_RECEIVER_REJECTED (UINT32_C(345) | (UINT32_C(10) << 9))

enum KaihenCensusSource {
    KaihenCensusSource_Hen = 1,
    KaihenCensusSource_Sm = 2,
    KaihenCensusSource_Game = 3,
};
enum KaihenCensusFlags {
    KaihenCensusFlag_OwnerKnown = 1,
    KaihenCensusFlag_Cached = 2,
    KaihenCensusFlag_ResultKnown = 4,
    KaihenCensusFlag_Before = 8,
    KaihenCensusFlag_After = 16,
    KaihenCensusFlag_Incomplete = 32,
};
enum KaihenCensusEvent {
    KaihenCensusEvent_Boot = 1,
    KaihenCensusEvent_SessionCreated = 2,
    KaihenCensusEvent_SessionClosed = 3,
    KaihenCensusEvent_SessionCloseFailure = 4,
    KaihenCensusEvent_FsClone = 5,
    KaihenCensusEvent_HenCounts = 6,
    KaihenCensusEvent_ServerFailure = 7,
    KaihenCensusEvent_RelayState = 8,
    KaihenCensusEvent_SmCreated = 16,
    KaihenCensusEvent_SmStarted = 17,
    KaihenCensusEvent_SmEnded = 18,
    KaihenCensusEvent_SmReaped = 19,
    KaihenCensusEvent_SmFailure = 20,
    KaihenCensusEvent_SmLookupBegin = 21,
    KaihenCensusEvent_SmLookupEnd = 22,
    KaihenCensusEvent_SmCounts = 23,
    KaihenCensusEvent_SmMailboxLoss = 24,
    KaihenCensusEvent_GameState = 32,
    KaihenCensusEvent_GameInput = 33,
    KaihenCensusEvent_GameCodeIdentity = 34,
    KaihenCensusEvent_GameDonorIdentity = 35,
    KaihenCensusEvent_GameProcessGone = 36,
    KaihenCensusEvent_GameDomainMatch = 37,
    KaihenCensusEvent_GamePackageClosed = 38,
    KaihenCensusEvent_GameFault = 39,
};

typedef struct {
    uint32_t magic;
    uint16_t schema, header_bytes;
    uint16_t record_bytes, capacity;
    uint32_t flags;
    uint64_t boot_id;
    uint64_t first_sequence, next_sequence, overwritten;
    uint32_t record_count, source_mask;
    uint64_t tick_frequency;
} KaihenCensusHeader;

typedef struct {
    uint64_t sequence, tick, owner_pid;
    uint32_t source, event, result, flags;
    uint64_t a, b, c;
} KaihenCensusRecord;

/* Header acknowledgement has sequence 0; a record acknowledgement echoes its
 * committed sequence. A relay never replays an uncertain native request. */
typedef struct {
    uint64_t boot_id, sequence;
} KaihenCensusAck;

#ifdef __cplusplus
static_assert(sizeof(KaihenCensusHeader) == 64);
static_assert(sizeof(KaihenCensusRecord) == 64);
static_assert(sizeof(KaihenCensusAck) == 16);
static_assert(offsetof(KaihenCensusRecord, a) == 40);
#else
_Static_assert(sizeof(KaihenCensusHeader) == 64, "census header layout");
_Static_assert(sizeof(KaihenCensusRecord) == 64, "census record layout");
_Static_assert(sizeof(KaihenCensusAck) == 16, "census acknowledgement layout");
#endif
