// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <cstring>
#include <switch/result.h>
#include "../../libkaihen/include/kaihen/reload.h"
#include "reload_diagnostics.hpp"

namespace reload {
// Dedicated results, independent of libnx's evolving error enum.
constexpr Result Busy = MAKERESULT(345, 1);
constexpr Result Stale = MAKERESULT(345, 2);
constexpr Result Capability = MAKERESULT(345, 3);
constexpr Result Expired = MAKERESULT(345, 4);
constexpr Result Invalid = MAKERESULT(345, 5);
constexpr Result Exhausted = MAKERESULT(345, 6);
constexpr Result Deadline = MAKERESULT(345, 7);
constexpr Result Identity = MAKERESULT(345, 8);

inline Result preservePrimary(Result primary, Result later) {
    return R_FAILED(primary) ? primary : later;
}

// Performs the real PM completion policy with injectable kernel/clock calls.
// On timeout/wait/close failure it retains all ownership: joined() is never
// called, and an earlier gate error wins over the later cleanup error.
template <typename Io>
Result completePmLaunch(Io& io, Result primary, u64 timeout) {
    const u64 start = io.now();
    while (!io.finished() && !io.deadline(start, timeout) && !io.cancelled()) io.pause();
    if (!io.finished()) return preservePrimary(primary, Deadline);
    Result result = io.wait();
    if (R_SUCCEEDED(result)) result = io.close();
    if (R_FAILED(result)) return preservePrimary(primary, result);
    io.joined();
    return primary;
}

inline bool validName(const char name[64]) {
    for (unsigned i = 0; i != 64; ++i) {
        const char c = name[i];
        if (!c) return i != 0;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return false;
}

// Called under the same owner lock used by the supervisor's cancellation.
// finished must be published before releasing that lock, closing the window
// between checking cancellation and exposing completion.
inline void finalizeWorker(bool eligible, bool cancelled, bool& success, bool& quarantined,
                           HenPluginReloadStatus& status, HenPluginReloadDiagnostics* diagnostics = nullptr) {
    quarantined |= cancelled;
    success = eligible && !quarantined;
    if (cancelled) status.result = preservePrimary(status.result, Deadline);
    if (quarantined) {
        status.phase = HenPluginReloadPhase_Quarantined;
        status.flags |= HEN_PLUGIN_RELOAD_BLOCKED;
    }
    if (diagnostics && R_FAILED(status.result)) recordUncapturedPrimary(*diagnostics, status.result);
}

inline bool publishable(const HenPluginReloadStatus& status, bool success, bool quarantined) {
    return success && !quarantined && !(status.flags & HEN_PLUGIN_RELOAD_BLOCKED) &&
           (status.flags & HEN_PLUGIN_RELOAD_OLD_EXITED) && status.old_process_id &&
           R_SUCCEEDED(status.result) && R_SUCCEEDED(status.restoration_result) &&
           status.new_process_id && status.new_process_id != status.old_process_id;
}

template <typename Worker>
bool completedSnapshot(Worker& worker, HenPluginReloadStatus& status) {
    status = worker.status();
    if (!worker.finished() || !worker.join()) return false;
    // The worker may have completed between the first snapshot and finished.
    status = worker.status();
    return true;
}

class Journal {
  public:
    struct Entry {
        u64 caller = 0;
        HenPluginReloadRequest request = {};
        HenPluginReloadStatus status = {};
        HenPluginReloadDiagnostics diagnostics = {};
    };
    struct Caller { u64 pid = 0; u64 high = 0; };
    static constexpr unsigned Capacity = 8;
    Entry entries[Capacity] = {};
    Caller callers[32] = {};
    unsigned next = 0;
    u64 serial = 0;
    u64 active = 0;
    bool blocked = false;

    Entry* find(u64 id) {
        for (auto& entry : entries) if (id && entry.status.operation_id == id) return &entry;
        return nullptr;
    }
    Result lookup(u64 caller, const HenPluginReloadRequest& request, Entry** out) {
        *out = nullptr;
        if (!caller || !request.nonce || !validName(request.name)) return Invalid;
        for (auto& entry : entries) {
            if (entry.caller != caller || entry.request.nonce != request.nonce) continue;
            if (std::memcmp(&entry.request, &request, sizeof(request))) return Invalid;
            *out = &entry;
            return 0;
        }
        for (const auto& owner : callers)
            if (owner.pid == caller && request.nonce <= owner.high) return Expired;
        return active || blocked ? Busy : 0;
    }
    Result begin(u64 caller, const HenPluginReloadRequest& request, Entry** out) {
        Result rc = lookup(caller, request, out);
        if (R_FAILED(rc) || *out) return rc;
        Caller* owner = nullptr;
        for (auto& slot : callers) if (slot.pid == caller) owner = &slot;
        if (!owner) for (auto& slot : callers) if (!slot.pid) { owner = &slot; break; }
        if (!owner || serial == UINT64_MAX) return Exhausted;
        *owner = {caller, request.nonce};
        auto& entry = entries[next++ % Capacity];
        entry = {};
        entry.caller = caller;
        entry.request = request;
        entry.status.instance_id = request.instance_id;
        entry.status.operation_id = ++serial;
        entry.status.old_process_id = request.expected_process_id;
        entry.status.generation = request.expected_generation;
        entry.status.phase = HenPluginReloadPhase_Preparing;
        entry.status.flags = HEN_PLUGIN_RELOAD_BUSY;
        entry.diagnostics.version = 1;
        entry.diagnostics.operation_id = entry.status.operation_id;
        entry.diagnostics.probe_result = MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
        active = serial;
        *out = &entry;
        return 0;
    }
    void finish(u64 id, bool quarantine) {
        if (auto* entry = find(id)) {
            entry->status.flags &= ~HEN_PLUGIN_RELOAD_BUSY;
            if (quarantine) entry->status.flags |= HEN_PLUGIN_RELOAD_BLOCKED;
        }
        blocked |= quarantine;
        // Keep the quarantined operation pinned, including a late PM PID.
        active = quarantine ? id : 0;
    }
};

// Production orchestration, also executed with fault-injecting host adapters.
// Each adapter owns its pending IPC/thread storage beyond a deadline. A
// failure after prepare may only restore availability after acknowledged abort.
template <typename Io> Result run(Io& io) {
    Result rc = io.preflight();
    if (R_FAILED(rc)) return io.fail(rc, false);
    io.phase(HenPluginReloadPhase_Draining);
    rc = io.prepare();
    if (R_FAILED(rc)) return io.fail(rc, !io.abort());
    rc = io.drain();
    if (R_FAILED(rc)) return io.fail(rc, !io.abort());
    io.phase(HenPluginReloadPhase_Exiting);
    rc = io.commit();
    if (R_FAILED(rc)) return io.fail(rc, true);
    rc = io.waitExit();
    if (R_FAILED(rc)) return io.fail(rc, true);
    io.oldExited();
    io.phase(HenPluginReloadPhase_Launching);
    rc = io.launch();
    if (R_FAILED(rc)) return io.fail(rc, !io.cleanup());
    io.phase(HenPluginReloadPhase_Handoff);
    rc = io.handoff();
    if (R_FAILED(rc)) return io.fail(rc, !io.cleanup());
    io.phase(HenPluginReloadPhase_Health);
    rc = io.health();
    if (R_FAILED(rc)) return io.fail(rc, !io.cleanup());
    return 0; // owner loop publishes before marking Complete
}
} // namespace reload
