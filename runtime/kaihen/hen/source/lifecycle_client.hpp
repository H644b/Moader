// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "reload_state.hpp"

// Transport and clock adapters allow the actual asynchronous owner contract
// to be exercised on the host without mirroring it in a fake HEN manager.
namespace reload {
inline KaihenPluginLifecycleRequest lifecycleRequest(u32 command, u64 operation) {
    return {command, 0, command == KaihenPluginLifecycle_Query || command == KaihenPluginLifecycle_Health ? 0 : operation};
}
inline bool validLifecycleReply(const KaihenPluginLifecycleRequest& request, const KaihenPluginLifecycleReply& reply) {
    const bool query = request.command == KaihenPluginLifecycle_Query || request.command == KaihenPluginLifecycle_Health;
    return reply.version == KAIHEN_PLUGIN_LIFECYCLE_VERSION && !reply.reserved &&
           (reply.flags & KAIHEN_PLUGIN_LIFECYCLE_COOPERATIVE) &&
           (query || reply.operation_id == request.operation_id);
}
template <typename Io>
Result lifecycleRetryBusy(Io& io, u32 command, KaihenPluginLifecycleReply& reply, u64 timeout) {
    const u64 start = io.now();
    do {
        if (io.cancelled()) return Deadline;
        Result rc = io.call(command, reply);
        if (rc != KAIHEN_PLUGIN_LIFECYCLE_BUSY) return rc;
        io.pause();
    } while (!io.deadline(start, timeout));
    return Deadline;
}

template <typename Io>
Result lifecycleWait(Io& io, u32 command, u32 state, bool healthy, u64 timeout) {
    const u64 start = io.now();
    do {
        if (io.cancelled()) return Deadline;
        KaihenPluginLifecycleReply reply = {};
        Result rc = io.call(command, reply);
        if (rc != KAIHEN_PLUGIN_LIFECYCLE_BUSY) {
            if (R_FAILED(rc)) return rc;
            if (reply.state == state && (!healthy || (reply.flags & KAIHEN_PLUGIN_LIFECYCLE_HEALTHY))) return 0;
            if (reply.state == KaihenPluginLifecycleState_Failed) return Invalid;
        }
        io.pause();
    } while (!io.deadline(start, timeout));
    return Deadline;
}

template <typename Io>
bool lifecycleAbort(Io& io, u64 timeout) {
    const u64 start = io.now();
    bool accepted = false;
    do {
        if (io.cancelled()) return false;
        KaihenPluginLifecycleReply reply = {};
        const Result rc = io.call(accepted ? KaihenPluginLifecycle_Poll : KaihenPluginLifecycle_Abort, reply);
        if (rc != KAIHEN_PLUGIN_LIFECYCLE_BUSY) {
            if (R_FAILED(rc)) return false;
            accepted = true;
            if (reply.state == KaihenPluginLifecycleState_Running &&
                (reply.flags & KAIHEN_PLUGIN_LIFECYCLE_HEALTHY)) return true;
            if (reply.state == KaihenPluginLifecycleState_Failed) return false;
        }
        io.pause();
    } while (!io.deadline(start, timeout));
    return false;
}

template <typename Io>
Result lifecycleCommit(Io& io, u64 timeout) {
    KaihenPluginLifecycleReply reply = {};
    const Result rc = lifecycleRetryBusy(io, KaihenPluginLifecycle_Commit, reply, timeout);
    if (R_FAILED(rc)) return rc;
    return reply.state == KaihenPluginLifecycleState_Quiesced ||
           reply.state == KaihenPluginLifecycleState_Exiting ? 0 : Invalid;
}
} // namespace reload
