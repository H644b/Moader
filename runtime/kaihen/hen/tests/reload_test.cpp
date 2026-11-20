// SPDX-License-Identifier: GPL-2.0-only
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "../source/reload_state.hpp"
#include "../source/lifecycle_client.hpp"
#include "../../libkaihen/include/kaihen/protocol.h"
#include "../../libkaihen/source/cmif_server.hpp"
#include "../../common/code_mount_request.hpp"

namespace {
HenPluginReloadRequest request(u64 nonce = 1) {
    HenPluginReloadRequest out = {};
    std::strcpy(out.name, "moader");
    out.instance_id = 41;
    out.expected_process_id = 52;
    out.expected_generation = 1;
    out.nonce = nonce;
    return out;
}

void journal() {
    reload::Journal journal;
    reload::Journal::Entry* entry = nullptr;
    auto input = request();
    assert(!journal.begin(80, input, &entry));
    const auto operation = entry->status.operation_id;
    assert(operation && journal.active == operation);
    assert(!journal.lookup(80, input, &entry) && entry->status.operation_id == operation);
    auto changed = input;
    ++changed.expected_generation;
    assert(journal.lookup(80, changed, &entry) == reload::Invalid);
    assert(journal.lookup(81, input, &entry) == reload::Busy);
    assert(journal.lookup(80, request(2), &entry) == reload::Busy);
    journal.find(operation)->status.phase = HenPluginReloadPhase_Complete;
    journal.finish(operation, false);
    // Lost terminal reply reuses the ticket, even though the target's
    // generation has already changed outside the journal.
    assert(!journal.begin(80, input, &entry) && entry->status.operation_id == operation);
    assert(journal.serial == 1 && !journal.active);
    for (u64 nonce = 2; nonce <= 12; ++nonce) {
        assert(!journal.begin(80, request(nonce), &entry));
        journal.finish(entry->status.operation_id, false);
    }
    assert(journal.lookup(80, input, &entry) == reload::Expired);
    assert(!journal.find(operation));
    assert(!journal.begin(80, request(13), &entry));
    auto blocked = entry->status.operation_id;
    journal.finish(blocked, true);
    assert(journal.active == blocked && journal.blocked);
    assert(journal.find(blocked)->status.flags & HEN_PLUGIN_RELOAD_BLOCKED);
    assert(journal.lookup(80, request(14), &entry) == reload::Busy);
    assert(!journal.lookup(80, request(13), &entry));
    // Malformed requests never allocate a nonce/operation.
    reload::Journal fresh;
    auto malformed = request();
    std::strcpy(malformed.name, "../moader");
    assert(fresh.begin(80, malformed, &entry) == reload::Invalid && fresh.serial == 0);
    std::memset(malformed.name, 'x', sizeof(malformed.name));
    assert(fresh.begin(80, malformed, &entry) == reload::Invalid);
    malformed = request(0);
    assert(fresh.begin(80, malformed, &entry) == reload::Invalid);
    for (u64 caller = 1; caller <= 32; ++caller) {
        assert(!fresh.begin(caller, request(), &entry));
        fresh.finish(entry->status.operation_id, false);
    }
    assert(fresh.begin(33, request(), &entry) == reload::Exhausted);
}

// Executes the exact production orchestration with observable effects and
// failures, including uncertainty that is NOT equivalent to process exit.
struct Io {
    unsigned failure = 0, calls = 0;
    bool abort_ok = true, cleanup_ok = true, stopped = false, gone = false;
    bool launched = false, quarantined = false, aborted = false, cleaned = false;
    u32 current_phase = HenPluginReloadPhase_Preparing;
    Result result = 0;
    Result restoration_result = 0;
    std::vector<unsigned> actions;
    Result action(unsigned id) {
        ++calls; actions.push_back(id);
        return failure == id ? MAKERESULT(345, 100 + id) : 0;
    }
    void phase(u32 value) { current_phase = value; }
    Result preflight() { assert(!stopped && !launched); return action(1); }
    Result prepare() { assert(!launched); return action(2); }
    Result drain() { return action(3); }
    bool abort() { assert(!stopped && !launched); aborted = true; return abort_ok; }
    Result commit() { stopped = failure != 4; return action(4); }
    Result waitExit() { assert(stopped); return action(5); }
    void oldExited() { assert(stopped); gone = true; }
    Result launch() { assert(gone); launched = true; return action(6); }
    Result handoff() { assert(gone && launched); return action(7); }
    Result health() { assert(gone && launched); return action(8); }
    bool cleanup() { assert(gone); cleaned = true; return cleanup_ok; }
    Result fail(Result rc, bool quarantine) {
        result = rc; quarantined = quarantine;
        current_phase = quarantine ? HenPluginReloadPhase_Quarantined : HenPluginReloadPhase_Failed;
        return rc;
    }
};

void flow() {
    Io happy;
    assert(!reload::run(happy));
    assert(happy.calls == 8 && happy.gone && happy.launched && !happy.quarantined);
    assert(happy.current_phase == HenPluginReloadPhase_Health); // owner publishes next
    for (unsigned failure = 1; failure <= 8; ++failure) {
        Io io;
        io.failure = failure;
        assert(R_FAILED(reload::run(io)));
        assert(io.calls == failure && io.result == MAKERESULT(345, 100 + failure));
        assert(io.quarantined == (failure == 4 || failure == 5));
        if (failure < 4) assert(!io.stopped && !io.launched);
        if (failure < 6) assert(!io.launched);
        assert(io.aborted == (failure == 2 || failure == 3));
        assert(io.cleaned == (failure >= 6));
    }
    for (unsigned failure : {2u, 3u}) {
        Io io; io.failure = failure; io.abort_ok = false;
        assert(R_FAILED(reload::run(io)) && io.quarantined && !io.launched);
    }
    for (unsigned failure : {6u, 7u, 8u}) {
        Io io; io.failure = failure; io.cleanup_ok = false;
        io.restoration_result = MAKERESULT(345, 250);
        assert(R_FAILED(reload::run(io)) && io.quarantined);
        assert(io.result == MAKERESULT(345, 100 + failure));
        assert(io.restoration_result == MAKERESULT(345, 250));
        assert(io.calls == failure); // one attempt; no rollback launch/retry
    }
}

struct LifecycleIo {
    struct Step { u32 command; Result result; u32 state; bool healthy = false; };
    std::vector<Step> steps;
    size_t cursor = 0;
    u64 clock = 0;
    bool cancel = false;
    Result call(u32 command, KaihenPluginLifecycleReply& reply) {
        assert(cursor < steps.size());
        const auto step = steps[cursor++];
        assert(command == step.command);
        reply = {1, KAIHEN_PLUGIN_LIFECYCLE_COOPERATIVE |
                    (step.healthy ? KAIHEN_PLUGIN_LIFECYCLE_HEALTHY : 0), step.state, 0, 19, 0};
        return step.result;
    }
    u64 now() { return clock; }
    void pause() { ++clock; }
    bool deadline(u64 start, u64 timeout) { return clock - start >= timeout; }
    bool cancelled() { return cancel; }
};
void lifecycleContract() {
    const auto query = reload::lifecycleRequest(KaihenPluginLifecycle_Query, 19);
    const auto health = reload::lifecycleRequest(KaihenPluginLifecycle_Health, 19);
    const auto commit = reload::lifecycleRequest(KaihenPluginLifecycle_Commit, 19);
    assert(!query.operation_id && !health.operation_id && commit.operation_id == 19);
    KaihenPluginLifecycleReply reply = {1, KAIHEN_PLUGIN_LIFECYCLE_COOPERATIVE, 0, 0, 82, 0};
    assert(reload::validLifecycleReply(query, reply)); // last operation, not request op
    assert(reload::validLifecycleReply(health, reply));
    assert(!reload::validLifecycleReply(commit, reply));
    reply.operation_id = 19;
    assert(reload::validLifecycleReply(commit, reply));
    LifecycleIo commit_io{{{KaihenPluginLifecycle_Commit, KAIHEN_PLUGIN_LIFECYCLE_BUSY, 0},
                           {KaihenPluginLifecycle_Commit, 0, KaihenPluginLifecycleState_Quiesced}}};
    assert(!reload::lifecycleCommit(commit_io, 10) && commit_io.cursor == 2);
    LifecycleIo lost{{{KaihenPluginLifecycle_Commit, KERNELRESULT(ConnectionClosed), 0}}};
    assert(reload::lifecycleCommit(lost, 10) == KERNELRESULT(ConnectionClosed) && lost.cursor == 1);
    LifecycleIo abort_io{{{KaihenPluginLifecycle_Abort, 0, KaihenPluginLifecycleState_Quiesced},
                         {KaihenPluginLifecycle_Poll, KAIHEN_PLUGIN_LIFECYCLE_BUSY, 0},
                         {KaihenPluginLifecycle_Poll, 0, KaihenPluginLifecycleState_Draining},
                         {KaihenPluginLifecycle_Poll, 0, KaihenPluginLifecycleState_Running},
                         {KaihenPluginLifecycle_Poll, 0, KaihenPluginLifecycleState_Running, true}}};
    assert(reload::lifecycleAbort(abort_io, 10) && abort_io.cursor == 5);
    LifecycleIo health_io{{{KaihenPluginLifecycle_Health, KAIHEN_PLUGIN_LIFECYCLE_BUSY, 0},
                          {KaihenPluginLifecycle_Health, 0, KaihenPluginLifecycleState_Starting},
                          {KaihenPluginLifecycle_Health, 0, KaihenPluginLifecycleState_Running},
                          {KaihenPluginLifecycle_Health, 0, KaihenPluginLifecycleState_Running, true}}};
    assert(!reload::lifecycleWait(health_io, KaihenPluginLifecycle_Health, KaihenPluginLifecycleState_Running, true, 10));
    assert(health_io.cursor == 4);
    LifecycleIo busy{{{KaihenPluginLifecycle_Query, KAIHEN_PLUGIN_LIFECYCLE_BUSY, 0},
                     {KaihenPluginLifecycle_Query, KAIHEN_PLUGIN_LIFECYCLE_BUSY, 0}}};
    assert(reload::lifecycleRetryBusy(busy, KaihenPluginLifecycle_Query, reply, 2) == reload::Deadline);
    assert(busy.cursor == 2);
    LifecycleIo cancelled;
    cancelled.cancel = true;
    assert(!reload::lifecycleAbort(cancelled, 10) && !cancelled.cursor);
}

void publicationRaces() {
    struct Worker {
        HenPluginReloadStatus live = {};
        bool done = false;
        unsigned reads = 0;
        HenPluginReloadStatus status() {
            ++reads;
            auto snapshot = live;
            if (!done) {
                // Completion occurs after the snapshot but before the
                // caller's finished check, exactly the reported race.
                live.new_process_id = 53;
                live.flags |= HEN_PLUGIN_RELOAD_OLD_EXITED;
                done = true;
            }
            return snapshot;
        }
        bool finished() { return done; }
        bool join() { assert(done); return true; }
    } worker;
    worker.live.old_process_id = 52;
    HenPluginReloadStatus snapshot = {};
    assert(reload::completedSnapshot(worker, snapshot));
    assert(worker.reads == 2 && snapshot.new_process_id == 53);
    assert(reload::publishable(snapshot, true, false));
    auto broken = snapshot;
    broken.new_process_id = 0;
    assert(!reload::publishable(broken, true, false));
    broken = snapshot;
    broken.restoration_result = reload::Deadline;
    assert(!reload::publishable(broken, true, false));

    // Real host threads place the supervisor deadline inside blocked
    // cleanup, then use the SAME finalization/publish guard as production.
    std::mutex mutex;
    std::condition_variable condition;
    bool cleanup_blocked = false, cleanup_allowed = false, cancelled = false;
    bool success = true, quarantined = false, finished = false;
    std::thread thread([&] {
        std::unique_lock lock(mutex);
        cleanup_blocked = true;
        condition.notify_all();
        condition.wait(lock, [&] { return cleanup_allowed; });
        reload::finalizeWorker(true, cancelled, success, quarantined, snapshot);
        finished = true; // published under supervisor lock, as production
    });
    {
        std::unique_lock lock(mutex);
        condition.wait(lock, [&] { return cleanup_blocked; });
        assert(!finished);
        cancelled = true;
        cleanup_allowed = true;
        condition.notify_all();
    }
    thread.join();
    assert(finished && !success && quarantined && snapshot.result == reload::Deadline);
    assert(snapshot.phase == HenPluginReloadPhase_Quarantined);
    assert(!reload::publishable(snapshot, success, quarantined));
}

void wire() {
    static_assert(HenHomebrewCommand_GetStatus == 0);
    static_assert(HenHomebrewCommand_GetFilesystemRoot == 16);
    static_assert(HenHomebrewCommand_OpenPlugin == 22);
    static_assert(sizeof(HenStatus) == 16 && sizeof(HenPluginName) == 64);
    static_assert(sizeof(HenPluginReloadStatus) <= 64);
    static_assert(sizeof(HenPluginReloadStatus) == 56);
    static_assert(sizeof(HenPluginReloadDiagnostics) == 64);
    static_assert(HenHomebrewCommand_GetLoaderRoot == 23);
    static_assert(HenHomebrewCommand_GetPluginReloadDiagnostics == 28);
    alignas(16) u8 message[0x100] = {};
    CmifRequestFormat format = {};
    format.request_id = HenHomebrewCommand_ReloadPlugin;
    format.data_size = sizeof(HenPluginReloadRequest);
    auto built = cmifMakeRequest(message, format);
    auto input = request();
    std::memcpy(built.data, &input, sizeof(input));
    auto parsed = ounce::parseRequest(message);
    assert(parsed.valid && parsed.command_id == HenHomebrewCommand_ReloadPlugin);
    assert(parsed.data_size >= sizeof(input));
    assert(!std::memcmp(parsed.data, &input, sizeof(input)));
    format = {};
    format.request_id = HenHomebrewCommand_GetPluginReloadDiagnostics;
    format.data_size = sizeof(u64);
    built = cmifMakeRequest(message, format);
    const u64 operation = 19;
    std::memcpy(built.data, &operation, sizeof(operation));
    parsed = ounce::parseRequest(message);
    assert(parsed.valid && parsed.command_id == 28 && parsed.data_size >= sizeof(operation));
    HenPluginReloadDiagnostics diagnostics = {};
    diagnostics.version = 1; diagnostics.operation_id = operation;
    diagnostics.primary_gate_step = 0x1100;
    diagnostics.restoration_gate_step = 0xe01;
    ounce::writeReply(message, 0, &diagnostics, sizeof(diagnostics));
    CmifResponse response = {};
    assert(!cmifParseResponse(&response, message, false, sizeof(diagnostics)));
    assert(!std::memcmp(response.data, &diagnostics, sizeof(diagnostics)));
    ounce::CodeMountRequest mount = {};
    mount.open_code = true;
    mount.arguments = {1, 1, {}, 0x040000000000003a};
    assert(ounce::matchesCodeMount(mount, 0x040000000000003a, 1));
    assert(!ounce::matchesCodeMount(mount, 0x040000000000003b, 1));
    assert(!ounce::matchesCodeMount(mount, 0x040000000000003a, 2));
    mount.arguments.content_attributes = 0;
    assert(!ounce::matchesCodeMount(mount, 0x040000000000003a, 1));
}
}
int main() {
    journal(); flow(); lifecycleContract(); publicationRaces(); wire();
    std::puts("reload: real orchestration failure matrix, idempotency/expiry/quarantine and ABI envelopes passed");
}
