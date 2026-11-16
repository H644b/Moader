// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <atomic>
#include <optional>
#include <switch.h>
#include "../../common/plugin_launch.hpp"
#include "../../common/policy_package.hpp"
#include "reload_state.hpp"
#include "lifecycle_client.hpp"
#include "reload_diagnostics.hpp"

class PluginReload {
    static_assert(std::atomic<u32>::is_always_lock_free, "gate progress callback must never block");
  public:
    struct Input {
        PluginLaunchContext context;
        PluginLaunchSpec spec;
        HenPluginReloadRequest request;
        Handle shell;
        Handle old_control; // borrowed: registry is pinned until worker completes
        Handle sm; // owned, new plugin's SM proxy session
        Handle fs; // owned, new plugin's cloned FS root
        u64 previous_package_operation;
    };
    PluginReload() { mutexInit(&lock_); }
    Result start(const Input& input, const HenPluginReloadStatus& status);
    HenPluginReloadStatus status();
    HenPluginReloadDiagnostics diagnostics();
    bool finished() const { return finished_.load(std::memory_order_acquire); }
    bool join();
    bool succeeded() const { return success_; }
    bool quarantined() const { return quarantined_; }
    Handle takeControl() { Handle value = new_control_; new_control_ = INVALID_HANDLE; return value; }

  private:
    using Memory = ounce::RetainedMemory<ounce::MemoryWriter::RoProcess>;
    using Gate = ounce::LoaderGate<ounce::MemoryWriter::RoProcess>;
    using Package = ounce::NativePackage<ounce::MemoryWriter::RoProcess>;
    static void worker(void* argument);
    static void pmWorker(void* argument);
    Result preflight();
    Result prepare();
    Result drain();
    bool abort();
    Result commit();
    Result waitExit();
    void oldExited();
    Result launch();
    Result handoff();
    Result health();
    bool cleanup();
    Result fail(Result rc, bool quarantine);
    void phase(u32 value);
    void diagnosticStage(u32 stage, u32 flags = 0);
    void captureGate(Result result, bool restoration);
    Result lifecycle(u32 command, KaihenPluginLifecycleReply& out, Handle control);
    struct LifecycleIo {
        PluginReload& owner;
        Handle control;
        Result call(u32 command, KaihenPluginLifecycleReply& reply) { return owner.lifecycle(command, reply, control); }
        u64 now() const { return armGetSystemTick(); }
        bool deadline(u64 start, u64 ns) const { return now() - start >= armNsToTicks(ns); }
        bool cancelled() const { return owner.expired(); }
        void pause() const { svcSleepThread(1000000); }
    };
    struct PmCompletionIo {
        PluginReload& owner;
        u64 now() const { return armGetSystemTick(); }
        bool deadline(u64 start, u64 ns) const { return now() - start >= armNsToTicks(ns); }
        bool cancelled() const { return owner.expired(); }
        void pause() const { svcSleepThread(1000000); }
        bool finished() const { return owner.pm_finished_.load(std::memory_order_acquire); }
        Result wait() { return threadWaitForExit(&owner.pm_thread_); }
        Result close() { return threadClose(&owner.pm_thread_); }
        void joined() {
            owner.pm_joined_ = true;
            owner.diagnosticStage(HenReloadLaunchStage_ProcessStart, HEN_RELOAD_DIAG_PM_JOINED);
        }
    };
    Result waitLifecycle(u32 command, u32 state, u64 timeout);
    Result processProgram(u64 pid, u64* program);
    bool waitGone(u64 pid, u64 timeout);
    bool expired() const;
    void releaseOwned();
    template <typename Io> friend Result reload::run(Io&);

    Mutex lock_ = {};
    Input input_ = {};
    HenPluginReloadStatus status_ = {};
    HenPluginReloadDiagnostics diagnostics_ = {};
    std::atomic<u32> live_gate_step_{0};
    Thread thread_ = {}, pm_thread_ = {};
    bool created_ = false, joined_ = false, pm_created_ = false, pm_started_ = false, pm_joined_ = false;
    bool success_ = false, quarantined_ = false, touched_old_ = false;
    std::atomic<bool> finished_{false}, pm_finished_{false}, cancelled_{false};
    u64 stage_tick_ = 0;
    std::atomic<u64> pm_pid_{0};
    Result pm_result_ = 0;
    SignedPolicy policy_ = {};
    std::optional<Memory> memory_;
    std::optional<Gate> gate_;
    std::optional<Package> package_;
    Handle new_control_ = INVALID_HANDLE;
    char staged_file_[160] = {}, package_file_[160] = {}, package_path_[176] = {};
};
