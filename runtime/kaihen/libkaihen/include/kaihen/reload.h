/* SPDX-License-Identifier: ISC */
#pragma once
#include <switch/types.h>
#include <switch/result.h>

#define KAIHEN_PLUGIN_LIFECYCLE_VERSION 1
#define KAIHEN_PLUGIN_CONTROL_LIFECYCLE 0x100
#define KAIHEN_PLUGIN_LIFECYCLE_BUSY MAKERESULT(345, 200)

typedef enum {
    KaihenPluginLifecycle_Query = 0,
    KaihenPluginLifecycle_Prepare = 1,
    KaihenPluginLifecycle_Poll = 2,
    KaihenPluginLifecycle_Abort = 3,
    KaihenPluginLifecycle_Commit = 4,
    KaihenPluginLifecycle_Health = 5,
    /* INTERNAL notification after the successful Commit reply was sent.
     * Never accepted from IPC. Only this notification permits owner exit. */
    KaihenPluginLifecycle_CommitAcknowledged = 6,
} KaihenPluginLifecycleCommand;

typedef enum {
    KaihenPluginLifecycleState_Running = 0,
    KaihenPluginLifecycleState_Draining = 1,
    KaihenPluginLifecycleState_Quiesced = 2,
    KaihenPluginLifecycleState_Exiting = 3,
    KaihenPluginLifecycleState_Starting = 4,
    KaihenPluginLifecycleState_Failed = 5,
} KaihenPluginLifecycleState;

#define KAIHEN_PLUGIN_LIFECYCLE_COOPERATIVE (1u << 0)
#define KAIHEN_PLUGIN_LIFECYCLE_HEALTHY (1u << 1)

typedef struct {
    u32 command;
    u32 reserved;
    u64 operation_id;
} KaihenPluginLifecycleRequest;

typedef struct {
    u32 version;
    u32 flags;
    u32 state;
    Result result;
    u64 operation_id;
    u64 reserved;
} KaihenPluginLifecycleReply;

/* Query/Health use request operation_id zero and may report the plugin's
 * current/last operation. Other requests require a matching nonzero ticket.
 * Abort may acknowledge while Draining/Quiesced: HEN polls until Running and
 * HEALTHY. Commit may reply Quiesced; only CommitAcknowledged permits exit.
 * Fast control-thread callback. Queue owner-thread work; never stop/join the
 * PluginService here. Prepare must deny busy imports before changing state.
 * Commit returns an acknowledgement BEFORE the owner exits. All requests for
 * an operation are idempotent. Health reports HEALTHY only after application
 * initialization and its runtime acknowledgement, separately from IPC ready. */
typedef Result (*PluginLifecycleHandler)(const KaihenPluginLifecycleRequest* request,
                                        KaihenPluginLifecycleReply* reply, void* context);

typedef enum {
    HenPluginReloadPhase_Idle = 0,
    HenPluginReloadPhase_Preparing = 1,
    HenPluginReloadPhase_Draining = 2,
    HenPluginReloadPhase_Exiting = 3,
    HenPluginReloadPhase_Launching = 4,
    HenPluginReloadPhase_Handoff = 5,
    HenPluginReloadPhase_Health = 6,
    HenPluginReloadPhase_Complete = 7,
    HenPluginReloadPhase_Failed = 8,
    HenPluginReloadPhase_Quarantined = 9,
} HenPluginReloadPhase;

#define HEN_PLUGIN_RELOAD_AVAILABLE (1u << 0)
#define HEN_PLUGIN_RELOAD_BUSY (1u << 1)
#define HEN_PLUGIN_RELOAD_BLOCKED (1u << 2)
#define HEN_PLUGIN_RELOAD_OLD_EXITED (1u << 3)

typedef struct {
    u64 instance_id;
    u64 process_id;
    u64 generation;
    u64 operation_id;
    u32 flags;
    u32 phase;
} HenPluginInfo;
/* GetPluginInfo succeeds with all fields zero except instance_id for true
 * absence. Reloading/failed/quarantined managed records are never absence. */

/* Name is the exact registry basename, without .nso. No path is accepted.
 * Nonce is nonzero and monotonically increasing per caller in this HEN
 * instance. Query info and preserve this request when a reply is lost. */
typedef struct {
    char name[64];
    u64 instance_id;
    u64 expected_process_id;
    u64 expected_generation;
    u64 nonce;
} HenPluginReloadRequest;

typedef struct {
    u64 instance_id;
    u64 operation_id;
    u64 old_process_id;
    u64 new_process_id;
    u64 generation;
    u32 phase;
    u32 flags;
    Result result;
    Result restoration_result;
} HenPluginReloadStatus;

typedef struct {
    u32 version;
    u32 flags;
    u64 instance_id;
    u64 operation_id;
} HenReloadCapabilities;

#ifdef __cplusplus
static_assert(sizeof(KaihenPluginLifecycleRequest) == 16);
static_assert(sizeof(KaihenPluginLifecycleReply) == 32);
static_assert(sizeof(HenPluginInfo) == 40);
static_assert(sizeof(HenPluginReloadRequest) == 96);
static_assert(sizeof(HenPluginReloadStatus) == 56);
static_assert(sizeof(HenReloadCapabilities) == 24);
#endif
