// SPDX-License-Identifier: GPL-2.0-only
#include "plugin_reload.hpp"
#include "../../common/sd_writer.hpp"
#include "../../libkaihen/source/plugin_startup.h"
#include <cstdio>
#include <cstring>

namespace {
constexpr u64 Second = 1000000000ull;
constexpr Result ProcessNotFound = MAKERESULT(15, 1);
constexpr u64 MaxExecutable = 64 * 1024 * 1024;
bool elapsed(u64 tick, u64 ns) { return armGetSystemTick() - tick >= armNsToTicks(ns); }
void close(Handle& handle) { if (handle != INVALID_HANDLE) svcCloseHandle(handle); handle = INVALID_HANDLE; }

void deleteArtifact(Handle fs, const char* path) {
    if (!path[0]) return;
    Service root = {};
    serviceCreate(&root, fs);
    FsFileSystem sd = {};
    if (R_SUCCEEDED(serviceDispatch(&root, 18, .out_num_objects = 1, .out_objects = &sd.s))) {
        // FS may still deny deletion of a backing object it retains. Leave
        // that file intact; never force-close another owner's object.
        fsFsDeleteFile(&sd, path);
        fsFsCommit(&sd);
        fsFsClose(&sd);
    }
}

// The staged bytes are read twice: verify their digest before packaging and
// verify the package itself after flush. NSO section ranges are bounded before
// the old process is asked to drain.
Result digestFile(Handle fs, const char* path, u8 hash[32], u64* size_out = nullptr) {
    SdReader reader;
    u64 size = 0;
    R_TRY(reader.begin(fs, path, &size));
    if (!size || size > MaxExecutable + SignedPolicy::Capacity + 0x1000) return reload::Invalid;
    Sha256Context sha;
    sha256ContextCreate(&sha);
    u8 buffer[4096];
    for (u64 left = size; left;) {
        u64 read = 0;
        const size_t wanted = left < sizeof(buffer) ? left : sizeof(buffer);
        R_TRY(reader.read(buffer, wanted, &read));
        if (read != wanted) return reload::Invalid;
        sha256ContextUpdate(&sha, buffer, read);
        left -= read;
    }
    sha256ContextGetHash(&sha, hash);
    if (size_out) *size_out = size;
    return 0;
}

Result stageExecutable(Handle fs, const char* source, const char* target) {
    SdReader reader;
    u64 size = 0;
    R_TRY(reader.begin(fs, source, &size));
    if (size < 0x100 || size > MaxExecutable) return reload::Invalid;
    u8 header[0x100];
    u64 read = 0;
    R_TRY(reader.read(header, sizeof(header), &read));
    if (read != sizeof(header) || std::memcmp(header, "NSO0", 4)) return reload::Invalid;
    u32 flags = 0;
    std::memcpy(&flags, header + 0x0c, 4);
    u64 previous_end = 0x100;
    for (unsigned section = 0; section < 3; ++section) {
        u32 offset, unpacked, packed;
        std::memcpy(&offset, header + 0x10 + 0x10 * section, 4);
        std::memcpy(&unpacked, header + 0x18 + 0x10 * section, 4);
        std::memcpy(&packed, header + 0x60 + 4 * section, 4);
        const u64 extent = flags & (1u << section) ? packed : unpacked;
        if (!extent || !unpacked || unpacked > MaxExecutable || offset < previous_end ||
            offset > size || extent > size - offset) return reload::Invalid;
        previous_end = offset + extent;
    }
    SdWriter writer;
    R_TRY(writer.begin(fs, target, size));
    R_TRY(writer.write(header, sizeof(header)));
    Sha256Context sha;
    sha256ContextCreate(&sha);
    sha256ContextUpdate(&sha, header, sizeof(header));
    u8 buffer[4096];
    for (u64 left = size - sizeof(header); left;) {
        const size_t wanted = left < sizeof(buffer) ? left : sizeof(buffer);
        R_TRY(reader.read(buffer, wanted, &read));
        if (read != wanted) return reload::Invalid;
        sha256ContextUpdate(&sha, buffer, read);
        R_TRY(writer.write(buffer, read));
        left -= read;
    }
    R_TRY(writer.finish());
    u8 expected[32], actual[32];
    sha256ContextGetHash(&sha, expected);
    R_TRY(digestFile(fs, target, actual));
    return std::memcmp(expected, actual, sizeof(actual)) ? reload::Invalid : 0;
}
} // namespace

Result PluginReload::start(const Input& input, const HenPluginReloadStatus& status) {
    if (created_ && !joined_) return reload::Busy;
    input_ = input;
    status_ = status;
    stage_tick_ = armGetSystemTick();
    created_ = joined_ = pm_created_ = pm_joined_ = false;
    success_ = quarantined_ = touched_old_ = false;
    finished_.store(false);
    pm_finished_.store(false);
    cancelled_.store(false);
    pm_pid_.store(0);
    new_control_ = INVALID_HANDLE;
    Result rc = threadCreate(&thread_, worker, this, nullptr, 0x20000, 0x2c, -2);
    if (R_FAILED(rc)) return rc;
    created_ = true;
    rc = threadStart(&thread_);
    if (R_FAILED(rc)) {
        threadClose(&thread_);
        created_ = false;
    }
    return rc;
}

HenPluginReloadStatus PluginReload::status() {
    mutexLock(&lock_);
    if (!finished_.load(std::memory_order_acquire) && elapsed(stage_tick_, 60 * Second)) {
        cancelled_.store(true);
        status_.phase = HenPluginReloadPhase_Quarantined;
        status_.flags |= HEN_PLUGIN_RELOAD_BLOCKED;
        status_.result = reload::Deadline;
    }
    // Even a late PM completion remains tracked. Never free/reuse its state.
    if (pm_pid_.load(std::memory_order_acquire)) status_.new_process_id = pm_pid_.load();
    const auto value = status_;
    mutexUnlock(&lock_);
    return value;
}

bool PluginReload::join() {
    if (!finished() || !created_) return false;
    if (!joined_) {
        Result rc = threadWaitForExit(&thread_);
        if (R_SUCCEEDED(rc)) rc = threadClose(&thread_);
        if (R_FAILED(rc)) { fail(rc, true); return false; }
        joined_ = true;
    }
    return true;
}

void PluginReload::worker(void* value) {
    auto& self = *static_cast<PluginReload*>(value);
    const Result rc = reload::run(self);
    self.success_ = R_SUCCEEDED(rc) && !self.cancelled_.load();
    if (self.cancelled_.load()) self.fail(reload::Deadline, true);
    if (!self.quarantined_) self.releaseOwned();
    // Cleanup can outlive its deadline. Finalize and publish finished under
    // the supervisor's lock so cancellation cannot slip between the two.
    mutexLock(&self.lock_);
    reload::finalizeWorker(R_SUCCEEDED(rc), self.cancelled_.load(), self.success_, self.quarantined_, self.status_);
    self.finished_.store(true, std::memory_order_release);
    mutexUnlock(&self.lock_);
}

void PluginReload::phase(u32 value) {
    mutexLock(&lock_);
    if (!cancelled_.load()) { status_.phase = value; stage_tick_ = armGetSystemTick(); }
    mutexUnlock(&lock_);
}
bool PluginReload::expired() const { return cancelled_.load(); }
Result PluginReload::fail(Result rc, bool quarantine) {
    mutexLock(&lock_);
    quarantined_ |= quarantine || cancelled_.load();
    status_.result = rc;
    status_.phase = quarantined_ ? HenPluginReloadPhase_Quarantined : HenPluginReloadPhase_Failed;
    if (quarantined_) status_.flags |= HEN_PLUGIN_RELOAD_BLOCKED;
    mutexUnlock(&lock_);
    return rc;
}

Result PluginReload::lifecycle(u32 command, KaihenPluginLifecycleReply& out, Handle control) {
    if (expired()) return reload::Deadline;
    Service service = {};
    serviceCreate(&service, control);
    const auto request = reload::lifecycleRequest(command, status_.operation_id);
    Result rc = serviceDispatchInOut(&service, KAIHEN_PLUGIN_CONTROL_LIFECYCLE, request, out);
    if (expired()) return reload::Deadline;
    if (R_FAILED(rc)) return rc;
    if (!reload::validLifecycleReply(request, out)) return reload::Capability;
    return out.result;
}

Result PluginReload::processProgram(u64 pid, u64* program) {
    Service monitor = {};
    serviceCreate(&monitor, input_.context.monitor);
    return serviceDispatchInOut(&monitor, 7, pid, *program);
}

Result PluginReload::preflight() {
    u64 program = 0;
    R_TRY(processProgram(input_.request.expected_process_id, &program));
    if (program != input_.spec.program_id) return reload::Identity;
    KaihenPluginLifecycleReply reply = {};
    LifecycleIo query_io{*this, input_.old_control};
    R_TRY(reload::lifecycleRetryBusy(query_io, KaihenPluginLifecycle_Query, reply, 5 * Second));
    if (reply.state != KaihenPluginLifecycleState_Running ||
        !(reply.flags & KAIHEN_PLUGIN_LIFECYCLE_HEALTHY)) return reload::Busy;
    R_TRY(readSignedPolicy(input_.context.loader_root, input_.spec.program_id,
                          input_.spec.storage_id, 1, &policy_));
    char source[128];
    std::snprintf(source, sizeof(source), "/kaihen/plugins/%s.nso", input_.request.name);
    std::snprintf(staged_file_, sizeof(staged_file_), "/kaihen/plugin-packages/reload-%016llx-%016llx.nso",
                  (unsigned long long)status_.instance_id, (unsigned long long)status_.operation_id);
    std::snprintf(package_file_, sizeof(package_file_), "/kaihen/plugin-packages/reload-%016llx-%016llx.nsp",
                  (unsigned long long)status_.instance_id, (unsigned long long)status_.operation_id);
    std::snprintf(package_path_, sizeof(package_path_), "@Sdcard:%s", package_file_);
    R_TRY(stageExecutable(input_.fs, source, staged_file_));
    u8 first[32], second[32];
    R_TRY(writeExeFsPackage(input_.fs, staged_file_, package_file_, policy_, first));
    R_TRY(digestFile(input_.fs, package_file_, second));
    if (std::memcmp(first, second, 32) || expired()) return reload::Invalid;
    memory_.emplace(input_.context.memory, input_.context.reader, input_.context.filesystem_process);
    R_TRY(memory_->begin());
    // Open and pin the exact staged package before the old process stops.
    package_.emplace(*memory_, input_.fs);
    auto mount = input_.context.mount;
    mount.title = input_.spec.program_id;
    R_TRY(package_->open(package_path_, mount));
    input_.context.mount = mount;
    return 0;
}

Result PluginReload::prepare() {
    touched_old_ = true;
    KaihenPluginLifecycleReply reply = {};
    Result rc = lifecycle(KaihenPluginLifecycle_Prepare, reply, input_.old_control);
    // Resident's explicit fast Busy denial guarantees no state was changed.
    if (rc == KAIHEN_PLUGIN_LIFECYCLE_BUSY) touched_old_ = false;
    return rc;
}
Result PluginReload::waitLifecycle(u32 command, u32 state, u64 timeout) {
    LifecycleIo io{*this, input_.old_control};
    return reload::lifecycleWait(io, command, state, false, timeout);
}
Result PluginReload::drain() { return waitLifecycle(KaihenPluginLifecycle_Poll, KaihenPluginLifecycleState_Quiesced, 10 * Second); }
bool PluginReload::abort() {
    if (!touched_old_) return true;
    LifecycleIo io{*this, input_.old_control};
    return reload::lifecycleAbort(io, 10 * Second);
}
Result PluginReload::commit() {
    LifecycleIo io{*this, input_.old_control};
    return reload::lifecycleCommit(io, 5 * Second);
}
bool PluginReload::waitGone(u64 pid, u64 timeout) {
    const u64 start = armGetSystemTick();
    do {
        u64 program = 0;
        const Result rc = processProgram(pid, &program);
        if (rc == ProcessNotFound) return true;
        if (R_FAILED(rc) || program != input_.spec.program_id || expired()) return false;
        svcSleepThread(1000000);
    } while (!elapsed(start, timeout));
    return false;
}
Result PluginReload::waitExit() { return waitGone(input_.request.expected_process_id, 5 * Second) ? 0 : reload::Deadline; }
void PluginReload::oldExited() {
    mutexLock(&lock_);
    status_.flags |= HEN_PLUGIN_RELOAD_OLD_EXITED;
    mutexUnlock(&lock_);
    if (input_.previous_package_operation) {
        char previous[160];
        std::snprintf(previous, sizeof(previous), "/kaihen/plugin-packages/reload-%016llx-%016llx.nsp",
                      (unsigned long long)status_.instance_id,
                      (unsigned long long)input_.previous_package_operation);
        deleteArtifact(input_.fs, previous);
    }
}

void PluginReload::pmWorker(void* value) {
    auto& self = *static_cast<PluginReload*>(value);
    // Only this worker uses libounce's PM-shell global. All monitor and
    // cleanup operations use explicit local Services, never rebind it.
    serviceCreate(pmshellGetServiceSession(), self.input_.shell);
    NcmProgramLocation location = {};
    location.program_id = self.input_.spec.program_id;
    location.storageID = self.input_.spec.storage_id;
    u64 pid = 0;
    self.pm_result_ = pmshellLaunchProgram(self.input_.spec.flags, &location, &pid);
    self.pm_pid_.store(pid, std::memory_order_release);
    self.pm_finished_.store(true, std::memory_order_release);
}

Result PluginReload::launch() {
    gate_.emplace(*memory_, input_.context.gate);
    Result rc = gate_->park();
    if (R_SUCCEEDED(rc)) {
        rc = threadCreate(&pm_thread_, pmWorker, this, nullptr, 0x8000, 0x2c, -2);
        if (R_SUCCEEDED(rc)) {
            pm_created_ = true;
            rc = threadStart(&pm_thread_);
            if (R_FAILED(rc)) { threadClose(&pm_thread_); pm_created_ = false; }
        }
    }
    bool exchanged = false;
    if (R_SUCCEEDED(rc)) {
        const u64 start = armGetSystemTick();
        ounce::DomainMount<ounce::MemoryWriter::RoProcess> mount(*memory_, input_.context.mount);
        u32 matches = 0;
        while (!elapsed(start, 30 * Second) && !expired()) {
            if (pm_finished_.load(std::memory_order_acquire) && R_FAILED(pm_result_)) { rc = pm_result_; break; }
            bool ready = false;
            rc = gate_->pending(ready);
            if (R_FAILED(rc)) break;
            if (!ready) { svcSleepThread(1000000); continue; }
            ounce::DomainEntry before[ounce::MaxDomainEntries], after[ounce::MaxDomainEntries], added;
            u32 before_count = 0, after_count = 0;
            rc = mount.snapshot(before, before_count);
            alignas(16) u8 request[ounce::LoaderRequestSize] = {};
            if (R_SUCCEEDED(rc)) rc = gate_->step(request);
            if (R_SUCCEEDED(rc)) rc = mount.snapshot(after, after_count);
            if (R_FAILED(rc)) break;
            ounce::CodeMountRequest decoded = {};
            rc = ounce::decodeCodeMountRequest(request, input_.context.mount.root_id, decoded);
            if (R_FAILED(rc)) break;
            // Unrelated Loader requests complete normally and never count.
            if (!ounce::matchesCodeMount(decoded, input_.spec.program_id, input_.spec.storage_id)) continue;
            if (++matches != 2) continue;
            rc = mount.added(before, before_count, after, after_count, added);
            if (R_SUCCEEDED(rc)) rc = mount.exchange(added);
            exchanged = R_SUCCEEDED(rc);
            break;
        }
        if (R_SUCCEEDED(rc) && !exchanged) rc = reload::Deadline;
    }
    const Result restore = gate_->restore();
    const Result channel = memory_->restore();
    mutexLock(&lock_);
    status_.restoration_result = R_FAILED(restore) ? restore : channel;
    mutexUnlock(&lock_);
    if (R_FAILED(restore) || R_FAILED(channel)) return R_FAILED(rc) ? rc : status_.restoration_result;
    if (pm_created_) {
        const u64 start = armGetSystemTick();
        while (!pm_finished_.load(std::memory_order_acquire) && !elapsed(start, 5 * Second) && !expired()) svcSleepThread(1000000);
        if (!pm_finished_.load(std::memory_order_acquire)) return reload::Deadline;
        Result join = threadWaitForExit(&pm_thread_);
        if (R_SUCCEEDED(join)) join = threadClose(&pm_thread_);
        if (R_FAILED(join)) return join;
        pm_joined_ = true;
    }
    if (R_FAILED(rc)) return rc;
    if (R_FAILED(pm_result_) || !pm_pid_.load() || pm_pid_.load() == input_.request.expected_process_id)
        return R_FAILED(pm_result_) ? pm_result_ : reload::Identity;
    u64 program = 0;
    R_TRY(processProgram(pm_pid_.load(), &program));
    if (program != input_.spec.program_id) return reload::Identity;
    Service monitor = {};
    serviceCreate(&monitor, input_.context.monitor);
    const u64 pid = pm_pid_.load();
    return serviceDispatchIn(&monitor, 1, pid);
}

Result PluginReload::handoff() {
    // This independent SM proxy session can wait in GetService without
    // blocking HEN's main thread. The supervisor retains it on a timeout.
    Service sm = {}, plugin = {};
    serviceCreate(&sm, input_.sm);
    const SmServiceName name = smEncodeName("mig:usr");
    Handle endpoint = INVALID_HANDLE;
    Result rc = serviceDispatchIn(&sm, 1, name,
        .out_handle_attrs = {SfOutHandleAttr_HipcMove}, .out_handles = &endpoint);
    if (R_FAILED(rc)) return rc;
    serviceCreate(&plugin, endpoint);
    Handle process = INVALID_HANDLE;
    rc = serviceDispatch(&plugin, PluginStartupCommand_GetIdentity,
        .out_handle_attrs = {SfOutHandleAttr_HipcCopy}, .out_handles = &process);
    u64 pid = 0;
    if (R_SUCCEEDED(rc)) rc = svcGetProcessId(&pid, process);
    close(process);
    if (R_SUCCEEDED(rc) && pid != pm_pid_.load()) rc = reload::Identity;
    Handle release = INVALID_HANDLE, ready = INVALID_HANDLE;
    if (R_SUCCEEDED(rc) && !expired()) rc = pluginAdoptHandles(&plugin, input_.sm, input_.fs, &release, &ready, &new_control_);
    if (R_SUCCEEDED(rc)) rc = svcSignalEvent(release);
    if (R_SUCCEEDED(rc)) rc = svcWaitSynchronizationSingle(ready, 5 * Second);
    close(release);
    close(ready);
    close(endpoint);
    if (R_SUCCEEDED(rc) && new_control_ == INVALID_HANDLE) rc = reload::Capability;
    return expired() ? reload::Deadline : rc;
}

Result PluginReload::health() {
    LifecycleIo io{*this, new_control_};
    return reload::lifecycleWait(io, KaihenPluginLifecycle_Health, KaihenPluginLifecycleState_Running, true, 30 * Second);
}

bool PluginReload::cleanup() {
    // No forced termination of the OLD plugin exists. Only this operation's
    // unpublished, pre-health candidate may be reaped through PID-targeted PM.
    if (expired() || R_FAILED(status_.restoration_result) || (pm_created_ && !pm_joined_)) return false;
    const u64 pid = pm_pid_.load();
    if (!pid) return true;
    if (pid == input_.request.expected_process_id) return false;
    u64 program = 0;
    Result rc = processProgram(pid, &program);
    if (rc == ProcessNotFound) return true;
    if (R_FAILED(rc) || program != input_.spec.program_id) return false;
    Service shell = {};
    serviceCreate(&shell, input_.shell);
    rc = serviceDispatchIn(&shell, 1, pid);
    return R_SUCCEEDED(rc) && waitGone(pid, 5 * Second);
}

void PluginReload::releaseOwned() {
    if (package_) package_->close();
    package_.reset();
    gate_.reset();
    memory_.reset();
    // Files are unique to this operation. Never delete while a launch or
    // restore remains uncertain; quarantined paths deliberately stay pinned.
    deleteArtifact(input_.fs, staged_file_);
    // A successful plugin keeps its backing NSP until a later reload proves
    // that exact process exited. The registry records its operation ID.
    if (!success_) deleteArtifact(input_.fs, package_file_);
    close(input_.sm);
    close(input_.fs);
    if (!success_) close(new_control_);
}
