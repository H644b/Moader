// SPDX-License-Identifier: GPL-2.0-only
// Actual bootstrap serving loop and PM worker; only gate, mount and SDK calls
// are simulated. The host pthread runtime must never be re-entered by the loop.
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <thread>
#include <switch.h>
#include "../source/pm_launch.hpp"
#include "../source/launch_mount.hpp"

namespace fixture {
constexpr Result Failure = MAKERESULT(Module_Libnx, LibnxError_BadInput);
alignas(16) thread_local u8 tls[0x200];
Service shell{};
Result launch_result;
unsigned sleeps, trywait_calls;
ounce::PmLaunchRequest* publish_on_sleep;

u64 forbiddenTryWait(u64, u64, u64) {
    ++trywait_calls;
    assert(false && "pthread_trywait re-entered while serving Loader");
    return 1;
}

struct Launch {
    BootstrapInput input{};
    ounce::PmLaunchRequest pm{};
    Result result = 0;
    u64 thread = 7;
    u32 mounts = 0;
    bool exchanged = false;
    BootstrapOperation stage = BootstrapOperation_Launch;
    void mark(BootstrapOperation operation) { stage = operation; }
    Launch() {
        input.pthread_trywait = forbiddenTryWait;
        pm.shell_session = 42;
        pm.program_id = 0x040000000000003a;
        pm.flags = 0x14;
        pm.storage_id = 3;
    }
};

struct Gate {
    unsigned pending_calls = 0, requests = 0;
    unsigned delay = 0;
    bool never_ready = false;
    Result pending_result = 0;
    Result step_result = 0;
    Result pending(bool& ready) {
        ++pending_calls;
        ready = !never_ready && pending_calls > delay;
        return pending_result;
    }
    Result step(u8* request) {
        // An unrelated first request must not count toward the two mounts.
        request[0] = ++requests > 1;
        return step_result;
    }
};

struct Mount {
    unsigned snapshots = 0, exchanges = 0;
    Result snapshot_result = 0, exchange_result = 0;
    Result snapshot(ounce::DomainEntry* entries, u32& count) {
        entries[0] = {++snapshots, 2, 3};
        count = 1;
        return snapshot_result;
    }
    Result inspectRequest(const u8* request, bool& target) {
        target = request[0];
        return 0;
    }
    Result added(const ounce::DomainEntry*, u32, const ounce::DomainEntry* after,
                 u32 count, ounce::DomainEntry& out) {
        assert(count == 1);
        out = after[0];
        return 0;
    }
    Result exchange(const ounce::DomainEntry& entry) {
        assert(entry.address == snapshots);
        ++exchanges;
        return exchange_result;
    }
};

void reset() {
    sleeps = trywait_calls = 0;
    launch_result = 0;
    publish_on_sleep = nullptr;
}
} // namespace fixture

extern "C" {
void* armGetTls() { return fixture::tls; }
Service* pmshellGetServiceSession() { return &fixture::shell; }
Result svcSendSyncRequest(Handle handle) {
    // serviceCreate's optional pointer-buffer query has no simulated server.
    assert(handle == 42);
    return fixture::Failure;
}
Result pmshellLaunchProgram(u32 flags, const NcmProgramLocation* location, u64* pid) {
    assert(flags == 0x14 && location->storageID == 3);
    assert(location->program_id == 0x040000000000003a);
    *pid = 1234;
    return fixture::launch_result;
}
void svcSleepThread(s64 ns) {
    assert(ns == 1000000);
    ++fixture::sleeps;
    if (fixture::publish_on_sleep && fixture::sleeps == 3)
        ounce::launchFromSdkThread(fixture::publish_on_sleep);
}
}

int main() {
    using namespace fixture;
    // These internal request and public host layouts must not drift in this fix.
    static_assert(sizeof(ounce::PmLaunchRequest) == 48);
    static_assert(offsetof(ounce::PmLaunchRequest, completed) == 40);

    reset();
    Launch success;
    ounce::launchFromSdkThread(&success.pm);
    assert(__atomic_load_n(&success.pm.completed, __ATOMIC_ACQUIRE) == 1);
    assert(success.pm.pid == 1234 && success.pm.result == 0);
    Gate delayed{.delay = 5};
    Mount mounted;
    ounce::exchangeLaunchMount(&success, delayed, mounted);
    // A successful PM reply can precede CodeFS: still serve the second target.
    assert(success.exchanged && success.mounts == 2 && mounted.exchanges == 1);
    assert(delayed.requests == 3 && sleeps == 5 && trywait_calls == 0);

    reset();
    Launch null_trywait;
    null_trywait.input.pthread_trywait = nullptr;
    ounce::launchFromSdkThread(&null_trywait.pm);
    Gate null_delayed{.delay = 2};
    Mount null_mount;
    ounce::exchangeLaunchMount(&null_trywait, null_delayed, null_mount);
    assert(null_trywait.exchanged && null_trywait.mounts == 2 && sleeps == 2);

    reset();
    Launch failed;
    launch_result = Failure;
    publish_on_sleep = &failed.pm;
    Gate waiting{.never_ready = true};
    Mount unused;
    ounce::exchangeLaunchMount(&failed, waiting, unused);
    // Actual PM worker publication stops waiting, without thread-runtime calls.
    assert(sleeps == 3 && waiting.pending_calls == 4 && trywait_calls == 0);
    assert(failed.pm.result == Failure && !failed.exchanged && unused.exchanges == 0);

    reset();
    Launch unpublished;
    unpublished.pm.result = Failure; // In-progress fields are not completion.
    Gate bounded{.never_ready = true};
    ounce::exchangeLaunchMount(&unpublished, bounded, unused);
    assert(bounded.pending_calls == 30000 && sleeps == 30000 && !trywait_calls);

    reset();
    Launch gate_failure;
    Gate bad_gate{.pending_result = Failure};
    ounce::exchangeLaunchMount(&gate_failure, bad_gate, unused);
    assert(gate_failure.result == Failure && !gate_failure.exchanged && !sleeps);

    Launch exchange_failure;
    Gate ready;
    Mount bad_mount{.exchange_result = Failure};
    ounce::exchangeLaunchMount(&exchange_failure, ready, bad_mount);
    assert(exchange_failure.result == Failure && !exchange_failure.exchanged);
    assert(exchange_failure.mounts == 2 && bad_mount.exchanges == 1);

    // The actual PM worker publishes PID/result before completion. The caller
    // observes them only after an acquire, before joining the host test thread.
    reset();
    Launch concurrent;
    std::thread worker([&] { ounce::launchFromSdkThread(&concurrent.pm); });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!__atomic_load_n(&concurrent.pm.completed, __ATOMIC_ACQUIRE)) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
    assert(concurrent.pm.pid == 1234 && concurrent.pm.result == 0);
    worker.join();
    std::puts("bootstrap: real Loader serving loop avoids pthread re-entry; PM publication, early success/failure, bounds and transport failures passed");
}
