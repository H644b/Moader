// SPDX-License-Identifier: GPL-2.0-only
// Actual HenServer scheduler/registry + actual relay + SDK CMIF + production
// plugin-control receiver. GameArm/PluginReload and Horizon execution are
// narrow deterministic boundaries, not a Switch kernel/FS capacity model.
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <new>
#include <optional>
#include <vector>
#include <switch.h>
#include <switch/arm/counter.h>
extern "C" {
#include <switch/kernel/random.h>
#include <switch/kernel/jit.h>
#include <switch/services/set.h>
#include <switch/services/fs.h>
}
struct SchedulerGateClock {
    static u64 now() { return 0; }
    static u64 toNs(u64 value) { return value * 32; }
    static u64 toTicks(u64 value) { return value / 32; }
};
// Reuse the existing test-only friend, without adding a production seam.
#define KAIHEN_GAME_ARM_TEST_CLOCK SchedulerGateClock
#include "../source/game_arm.hpp"
#undef KAIHEN_GAME_ARM_TEST_CLOCK
#include "../source/sm_proxy.hpp"
#include "../source/plugin_reload.hpp"
#include "../source/census_relay.hpp"
#include "../../common/control.h"
#include "../../libkaihen/source/cmif_server.hpp"
// Dependencies/standard headers are parsed normally first. Access is relaxed
// only for this fixture's actual HenServer declaration; production is frozen.
#define private public
#include "../source/server.hpp"
#undef private

struct GameArmLifetimeTest {
    static void observedExit(GameArm& game, u64 pid) { game.census_last_exit_pid_ = pid; }
};

namespace scheduler_fixture {
struct Environment {
    u64 now = armNsToTicks(100000000) + 10;
    u64 selected_pid = 0;
    bool gate_idle = true, process_exists = true, kernel_exited = false;
    unsigned game_steps = 0, lease_attempts = 0, process_checks = 0;
    unsigned creates = 0, starts = 0, waits = 0, closes = 0, handle_closes = 0;
    unsigned service_acquisitions = 0, sleeps = 0, sends = 0, replies = 0;
    unsigned pid_queries = 0;
    Result wait_failure = 0, close_failure = 0;
    u64 busy_sequence = 0, failed_sequence = 0;
    bool busy_sent = false;
    ThreadFunc worker = nullptr;
    void* argument = nullptr;
    std::vector<u64> attempted, accepted;
    std::vector<Handle> closed_handles;
};
Environment* environment;
u64 now() { assert(environment); return environment->now; }
}
// The production scheduler's clock call is substituted in this TU only. The
// real counter header was already parsed; no native instruction is executed.
#define armGetSystemTick scheduler_fixture::now
#include "../source/server.cpp"
#undef armGetSystemTick
#include "../source/census_relay.cpp"
#include "../../libkaihen/source/plugin_server.cpp"
#include "../../../../include/moader/host_census.hpp"

namespace scheduler_fixture {
alignas(16) u8 tls[0x100];
PluginService endpoint;
moader::HostCensus* receiver;
Result receive(u32 command, const void* input, size_t bytes, void* output,
               size_t* output_bytes, void*) {
    auto& env = *environment;
    *output_bytes = 0;
    if (command == KAIHEN_CENSUS_RECORD_COMMAND) {
        KaihenCensusRecord record; assert(bytes == sizeof(record)); std::memcpy(&record, input, bytes);
        env.attempted.push_back(record.sequence);
        if (record.sequence == env.busy_sequence && !env.busy_sent) {
            env.busy_sent = true;
            return KAIHEN_CENSUS_RECEIVER_BUSY;
        }
    }
    KaihenCensusAck ack{};
    bool busy = false;
    if (!receiver->receive(command, input, bytes, ack, &busy))
        return busy ? KAIHEN_CENSUS_RECEIVER_BUSY : KAIHEN_CENSUS_RECEIVER_REJECTED;
    if (command == KAIHEN_CENSUS_RECORD_COMMAND) env.accepted.push_back(ack.sequence);
    std::memcpy(output, &ack, sizeof(ack)); *output_bytes = sizeof(ack);
    return 0;
}
}

// GameArm gate/PM engine is intentionally not run. The actual scheduler must
// consult diagnosticsIdle and suppress step/lease admission at its real sites.
GameArm::~GameArm() = default;
bool GameArm::diagnosticsIdle() const { return scheduler_fixture::environment->gate_idle; }
HenGameStatus GameArm::status() const {
    HenGameStatus value{}; value.process_id = scheduler_fixture::environment->selected_pid; return value;
}
void GameArm::step() { scheduler_fixture::environment->game_steps++; }
bool GameArm::processExists(u64) { scheduler_fixture::environment->process_checks++; return scheduler_fixture::environment->process_exists; }
bool GameArm::acquireReloadLease(PluginLaunchContext&) { scheduler_fixture::environment->lease_attempts++; assert(false); return false; }
void GameArm::releaseReloadLease() { assert(false); }
// No active Reload worker is created. These definitions satisfy the actual
// admission method's references; reaching them is outside this fixture.
Result PluginReload::start(const Input&, const HenPluginReloadStatus&) { assert(false); return 1; }
HenPluginReloadStatus PluginReload::status() { assert(false); return {}; }
HenPluginReloadDiagnostics PluginReload::diagnostics() { assert(false); return {}; }
bool PluginReload::join() { assert(false); return false; }
void SmProxy::sampleCensus() {} // worker mailboxes have their own production tests
Result SmProxy::openSession(Handle*) { assert(false); return 1; }

extern "C" {
void* armGetTls() { return scheduler_fixture::tls; }
void mutexLock(Mutex*) {}
void mutexUnlock(Mutex*) {}
Result threadCreate(Thread* thread, ThreadFunc entry, void* argument, void*, size_t, int, int) {
    auto& env = *scheduler_fixture::environment;
    assert(!env.worker); ++env.creates; thread->handle = 80;
    env.worker = entry; env.argument = argument; env.kernel_exited = false;
    return 0;
}
Result threadStart(Thread*) { ++scheduler_fixture::environment->starts; return 0; }
Result threadWaitForExit(Thread*) { assert(false && "scheduler may not block on a worker"); return 1; }
Result svcWaitSynchronization(s32* index, const Handle* handles, s32 count, u64 timeout) {
    auto& env = *scheduler_fixture::environment;
    assert(count == 1 && handles[0] == 80 && timeout == 0); ++env.waits;
    *index = 0;
    return env.kernel_exited ? env.wait_failure : KERNELRESULT(TimedOut);
}
Result threadClose(Thread*) {
    auto& env = *scheduler_fixture::environment; ++env.closes;
    if (env.close_failure) return env.close_failure;
    env.worker = nullptr; env.argument = nullptr; return 0;
}
Result svcCloseHandle(Handle handle) {
    auto& env = *scheduler_fixture::environment; ++env.handle_closes; env.closed_handles.push_back(handle); return 0;
}
void svcSleepThread(s64) { ++scheduler_fixture::environment->sleeps; }
Result svcGetProcessId(u64* pid, Handle handle) {
    ++scheduler_fixture::environment->pid_queries; assert(handle == 77); *pid = 9000; return 0;
}
void setsysExit() { assert(false); }
Result svcCreateEvent(Handle*, Handle*) { assert(false); return 1; }
Result svcSignalEvent(Handle) { assert(false); return 1; }
Result svcClearEvent(Handle) { assert(false); return 1; }
Result svcCreateSession(Handle*, Handle*, u32, u64) { assert(false && "diagnostic export created a session"); return 1; }
Result smGetServiceWrapper(Service*, SmServiceName) {
    ++scheduler_fixture::environment->service_acquisitions; assert(false); return 1;
}
Result smGetServiceOriginal(Handle*, SmServiceName) {
    ++scheduler_fixture::environment->service_acquisitions; assert(false); return 1;
}
Result fsStorageRead(FsStorage*, s64, void*, u64) { assert(false); return 1; }
Result fsStorageWrite(FsStorage*, s64, const void*, u64) { assert(false); return 1; }
Result svcReplyAndReceive(s32*, const Handle*, s32 count, Handle reply, u64 timeout) {
    assert(count == 0 && reply == 20 && timeout == 0); ++scheduler_fixture::environment->replies;
    return KERNELRESULT(TimedOut);
}
Result svcSendSyncRequest(Handle handle) {
    using namespace scheduler_fixture;
    auto& env = *environment; assert(handle == 42 || handle == 43); ++env.sends;
    const auto request = ounce::parseRequest(tls);
    assert(request.valid && ounce::hasOnlyInlineData(request) && isCensusFrame(request));
    if (request.command_id == KAIHEN_CENSUS_RECORD_COMMAND) {
        KaihenCensusRecord record; std::memcpy(&record, request.data, sizeof(record));
        if (record.sequence == env.failed_sequence) return KERNELRESULT(ConnectionClosed);
    }
    assert(!serveControlRequest(&endpoint)); // actual production CMIF server
    return 0;
}
}

namespace scheduler_fixture {
struct Fixture {
    Environment env;
    HenServer server;
    moader::HostCensus cache{123};
    Fixture() {
        environment = &env; receiver = &cache;
        endpoint = {}; endpoint.control_session = 20;
        endpoint.diagnostics_handler = receive;
        server.census_.initialize(12, 31250000);
        for (u64 i = 1; i <= 3; i++) server.census_.emit(KaihenCensusSource_Hen, KaihenCensusEvent_HenCounts, i);
        server.census_relay_ = new CensusRelay;
        server.reload_instance_ = 12; server.reload_configured_ = true;
        server.plugin_count_ = 1;
        std::strcpy(server.plugins_[0].name.name, "moader");
        server.plugins_[0].control_session = 42; server.plugins_[0].process_id = 211;
    }
    ~Fixture() {
        // Modeled whole-process teardown AFTER assertions. A retained fake
        // worker has no OS thread and is never invoked again. Production's
        // retention is tested before this harness-only memory disposal.
        env.worker = nullptr; env.argument = nullptr;
        auto* relay = server.census_relay_; server.census_relay_ = nullptr; delete relay;
        for (auto& plugin : server.plugins_) plugin.control_session = INVALID_HANDLE;
        for (auto& handle : server.homebrew_sessions_) handle = INVALID_HANDLE;
        for (auto& handle : server.homebrew_processes_) handle = INVALID_HANDLE;
        assert(env.service_acquisitions == 0);
    }
    void tick() { server.stepBetweenRequests(); }
    void runWorker(bool exited = true) {
        assert(env.worker); const auto worker = env.worker; void* const argument = env.argument;
        worker(argument); env.kernel_exited = exited;
    }
    void interval() { env.now += armNsToTicks(100000001); }
    void pinnedAdmission() {
        const auto sends = env.sends, handles = env.handle_closes, processes = env.process_checks;
        HenPluginName name{}; std::strcpy(name.name, "moader"); Handle opened = 0;
        assert(server.openPlugin(name, &opened) == reload::Busy && opened == 0);
        server.removePlugin(0); server.removeExitedPlugins();
        assert(server.plugin_count_ == 1 && server.plugins_[0].control_session == 42);
        assert(env.handle_closes == handles && env.process_checks == processes && env.sends == sends);
        HenPluginReloadRequest request{};
        request.instance_id = 12; request.nonce = 1; request.expected_process_id = 211; request.expected_generation = 1;
        std::strcpy(request.name, "moader"); HenPluginReloadStatus output{};
        // A homebrew client arriving after a batch began may ask to reload.
        // Its existing channel/process binding is supplied, not acquired here.
        const Handle previous_channel = server.homebrew_sessions_[0];
        const Handle previous_process = server.homebrew_processes_[0];
        server.homebrew_sessions_[0] = 55; server.homebrew_processes_[0] = 77;
        assert(server.submitReload(0, request, output) == reload::Busy);
        server.homebrew_sessions_[0] = previous_channel;
        server.homebrew_processes_[0] = previous_process;
        assert(env.lease_attempts == 0 && env.sends == sends);
    }
};
}

int main() {
    using namespace scheduler_fixture;
    {
        Fixture f;
        f.server.homebrew_sessions_[0] = 55; f.server.homebrew_processes_[0] = 77; f.tick();
        assert(!f.env.creates && !f.env.sends);
        f.server.homebrew_sessions_[0] = 0;
        f.server.homebrew_processes_[0] = 0;
        f.server.reload_journal_.active = 999; f.tick();
        assert(!f.env.creates);
        f.server.reload_journal_.active = 0; f.env.gate_idle = false; f.tick();
        assert(!f.env.creates);
        f.env.gate_idle = true;
        const auto steps = f.env.game_steps; f.tick();
        assert(f.env.creates == 1 && f.env.game_steps == steps && !f.env.sends);
        f.env.process_exists = false; f.pinnedAdmission();
        f.tick(); assert(f.env.creates == 1 && f.env.game_steps == steps);
        f.runWorker(false); // finished publication alone is NOT kernel exit
        f.tick();
        assert(f.server.census_delivered_ == 0 && !f.env.closes && f.env.game_steps == steps);
        assert(f.server.census_relay_->ownsControl(42, 211)); f.pinnedAdmission();
        f.env.kernel_exited = true; f.tick();
        assert(f.server.census_delivered_ == 3 && f.server.census_header_delivered_);
        assert(f.env.closes == 1 && f.env.game_steps == steps + 1);
        assert(f.env.accepted == std::vector<u64>({1, 2, 3}));
        f.server.removeExitedPlugins();
        assert(!f.server.plugin_count_ && f.env.process_checks == 1);
        assert(std::count(f.env.closed_handles.begin(), f.env.closed_handles.end(), 42) == 1);
    }
    {
        Fixture f; f.env.busy_sequence = 2; f.tick(); f.runWorker(); f.tick();
        assert(f.server.census_delivered_ == 1 && f.server.census_relay_->report().state == CensusRelay::Deferred);
        assert(f.env.accepted == std::vector<u64>({1}));
        f.interval(); f.tick(); f.runWorker(); f.tick();
        assert(f.env.accepted == std::vector<u64>({1, 2, 3, 4})); // includes the single Deferred event
        assert(f.env.attempted == std::vector<u64>({1, 2, 2, 3, 4}));
        assert(f.server.census_delivered_ == f.env.accepted.back());
        assert(f.env.creates == 2); // one retry batch, never replay accepted record 1
    }
    {
        Fixture f;
        f.server.homebrew_sessions_[0] = 55; f.server.homebrew_processes_[0] = 77;
        f.env.selected_pid = 800; f.env.gate_idle = false;
        GameArmLifetimeTest::observedExit(f.server.game_, 800);
        f.tick();
        assert(f.server.census_exit_snapshot_ == 800 && !f.env.creates);
        const auto after_snapshot = f.server.census_.header().next_sequence;
        KaihenCensusRecord counts{};
        assert(f.server.census_.read(after_snapshot - 1, counts));
        assert(counts.event == KaihenCensusEvent_HenCounts && counts.a == 1 && counts.b == 1);
        f.tick(); assert(f.server.census_.header().next_sequence == after_snapshot); // one exit snapshot
        f.env.gate_idle = true; f.tick();
        assert(f.env.creates == 1); // observed exit permits capture of still-retained channels
        f.runWorker(); f.tick();
        assert(f.server.census_delivered_ == 4 && f.server.homebrew_sessions_[0] == 55);
        assert(!f.env.pid_queries && !f.env.process_checks && !f.env.handle_closes);
    }
    {
        Fixture f;
        f.server.homebrew_sessions_[0] = 55; f.server.homebrew_processes_[0] = 77;
        GameArmLifetimeTest::observedExit(f.server.game_, 800);
        f.env.selected_pid = 801; f.tick();
        assert(!f.env.creates); // a different known live host restores the zero-channel guard
        assert(!f.env.pid_queries && !f.env.process_checks);
        f.server.homebrew_sessions_[0] = f.server.homebrew_processes_[0] = 0;
        f.tick(); assert(f.env.creates == 1); f.runWorker(); f.tick();
        assert(!f.env.pid_queries && !f.env.process_checks);
    }
    {
        Fixture f;
        f.server.homebrew_sessions_[0] = 55; f.server.homebrew_processes_[0] = 77;
        GameArmLifetimeTest::observedExit(f.server.game_, 800);
        f.env.selected_pid = 0; f.tick(); // cached exit survives the modeled package-reset status
        assert(f.env.creates == 1 && f.server.game_.lastObservedExit() == 800);
        assert(!f.env.pid_queries && !f.env.process_checks); f.runWorker(); f.tick();
    }
    {
        Fixture f; f.env.failed_sequence = 2; f.tick(); f.runWorker(); f.tick();
        assert(f.server.census_delivered_ == 1 && f.server.census_relay_->ready());
        f.env.process_exists = false; f.pinnedAdmission();
        const auto creates = f.env.creates;
        // Deliberate private registry replacement tests the defensive new-PID
        // poison boundary; production removal itself correctly refused above.
        f.server.plugins_[0].process_id = 212; f.server.plugins_[0].control_session = 43;
        f.interval(); f.tick();
        assert(f.env.creates == creates && f.server.census_relay_->disabledFor(212));
    }
    for (bool fail_close : {false, true}) {
        Fixture f; f.tick(); f.runWorker();
        if (fail_close) f.env.close_failure = MAKERESULT(345, 222);
        else f.env.wait_failure = MAKERESULT(345, 221);
        const auto steps = f.env.game_steps; f.tick();
        assert(!f.server.census_relay_->ready() && f.env.game_steps == steps);
        f.env.process_exists = false; f.pinnedAdmission();
        const auto waits = f.env.waits, closes = f.env.closes;
        f.env.wait_failure = f.env.close_failure = 0; f.interval(); f.tick();
        assert(f.env.waits == waits && f.env.closes == closes && f.env.creates == 1);
        assert(f.server.census_relay_->ownsControl(42, 211));
    }
    {
        Fixture f; f.tick(); const auto steps = f.env.game_steps;
        f.env.now += armNsToTicks(2000000001); f.tick();
        assert(f.server.census_relay_->report().timed_out && f.server.census_delivered_ == 0);
        assert(f.env.game_steps == steps && !f.env.sends); f.pinnedAdmission();
    }
    std::puts("actual HenServer scheduler/registry + relay/CMIF/receiver: admission, cached-exit exception, ownership pinning, ACK cursor, Busy resume and boot poison passed");
}
