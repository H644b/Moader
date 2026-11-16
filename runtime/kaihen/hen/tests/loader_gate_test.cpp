// SPDX-License-Identifier: GPL-2.0-only
// Actual LoaderGate + RetainedMemory<RoProcess>, with real SDK CMIF/HIPC
// packing. Only Horizon kernel/RO endpoints and elapsed time are simulated.
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include "../../common/loader_gate.hpp"
#include "../../libkaihen/source/cmif_server.hpp"
#include "../source/reload_state.hpp"

namespace fixture {
constexpr u64 Base = 0x100000;
constexpr u64 Millisecond = 1000000;
constexpr u64 Delay = 75 * Millisecond;
constexpr Result BadInput = MAKERESULT(Module_Libnx, LibnxError_BadInput);
alignas(16) u8 tls[0x100];
std::array<u8, 0x8000> bytes;
ounce::LoaderGateConfig config;
u64 ticks, settle_at, wake_at;
bool pending, signalled, never_settle, selected_written;
bool inject_conflict, corrupt_wait_write;
bool same_parent;
bool fail_restore_signal;
unsigned ro_reads, ro_writes, storage_writes, cancellations, cookie_writes;
u64 first_selection_tick;
std::array<u8, 0x100> real_request;
enum class Receipt { Empty, Real } receipt;

template <typename T> T get(u64 address) {
    assert(address >= Base && address + sizeof(T) <= Base + bytes.size());
    T value;
    std::memcpy(&value, bytes.data() + address - Base, sizeof(value));
    return value;
}
template <typename T> void put(u64 address, const T& value) {
    assert(address >= Base && address + sizeof(T) <= Base + bytes.size());
    std::memcpy(bytes.data() + address - Base, &value, sizeof(value));
}
void progress() {
    if (settle_at && ticks >= settle_at && !never_settle) {
        settle_at = 0;
        for (u64 waiter : config.waiters) put<u64>(waiter + 0x10, 0);
        auto view = get<ounce::LoaderSessionView>(config.server);
        const u64 old_parent = view.parent;
        if (!same_parent) view.parent = old_parent == config.waiters[0] ? config.waiters[1] : config.waiters[0];
        put<u64>(view.parent + 0x18, get<u64>(view.parent + 0x18) + 1);
        view.receive_result = receipt == Receipt::Empty ? 3 : 0;
        put(config.server, view);
        if (receipt == Receipt::Real) {
            std::memcpy(bytes.data() + config.request - Base, real_request.data(), real_request.size());
            pending = false;
        }
    }
    if (wake_at && ticks >= wake_at) {
        wake_at = 0;
        auto view = get<ounce::LoaderSessionView>(config.server);
        view.receive_result = 0; // the actual receive overwrote the armed cookie
        put(config.server, view);
    }
}
struct Clock {
    static u64 now() { return ticks; }
    static u64 toNs(u64 value) { return value * 32; }
    static u64 toTicks(u64 ns) { return ns / 32 + (ns % 32 != 0); }
};
using Memory = ounce::RetainedMemory<ounce::MemoryWriter::RoProcess>;
using Gate = ounce::LoaderGate<ounce::MemoryWriter::RoProcess, Clock>;
constexpr ounce::RetainedMemoryConfig MemoryConfig{25, 0x100, 0x200};

void reset() {
    bytes.fill(0);
    std::memset(tls, 0, sizeof(tls));
    config = {};
    config.server = Base + 0x100;
    config.vptr = 0xabc;
    config.domain = 0xdef;
    config.metadata = 0x123;
    config.request = Base + 0x500;
    for (unsigned i = 0; i != 7; ++i) {
        config.waiters[i] = Base + 0x1000 + i * 0x110;
        config.workers[i] = 100 + i;
        put<u64>(config.waiters[i] + 0x18, 10 + i);
    }
    config.receive = config.original_wait = 20;
    config.park = 21;
    config.server_copy = 22;
    config.event_read = 23;
    config.event_write = 24;
    ounce::LoaderSessionView view = {};
    view.vptr = config.vptr;
    view.parent = config.waiters[0];
    view.wait = config.original_wait;
    view.tag = 1;
    view.state = 1;
    view.receive = config.receive;
    view.request = config.request;
    view.capacity = 0x100;
    view.domain = config.domain;
    view.metadata = config.metadata;
    put(config.server, view);
    put<u32>(config.request, 0x01020304);
    ticks = settle_at = wake_at = first_selection_tick = 0;
    pending = signalled = never_settle = selected_written = false;
    inject_conflict = corrupt_wait_write = false;
    same_parent = false;
    fail_restore_signal = false;
    ro_reads = ro_writes = storage_writes = cancellations = cookie_writes = 0;
    receipt = Receipt::Empty;
    for (unsigned i = 0; i < real_request.size(); ++i) real_request[i] = i ^ 0x55;
}
Result timedPark(Gate& gate) {
    Result rc = gate.beginPark();
    if (R_FAILED(rc)) return rc;
    bool complete = false;
    do {
        rc = gate.pollPark(complete);
        if (R_FAILED(rc) || complete) return rc;
        svcSleepThread(Millisecond);
    } while (true); // the gate enforces its real one-second deadline
}
Result timedRequest(Gate& gate, void* output) {
    Result rc = gate.beginStep();
    if (R_FAILED(rc)) return rc;
    bool complete = false;
    do {
        rc = gate.pollStep(output, complete);
        if (R_FAILED(rc) || complete) return rc;
        svcSleepThread(Millisecond);
    } while (true);
}

struct PmKernel {
    bool completed = false, did_join = false;
    Result wait_error = 0, close_error = 0;
    unsigned waits = 0, closes = 0;
    u64 now() const { return ticks; }
    bool deadline(u64 start, u64 ns) const { return Clock::toNs(ticks - start) >= ns; }
    bool cancelled() const { return false; }
    void pause() { svcSleepThread(Millisecond); }
    bool finished() const { return completed; }
    Result wait() { ++waits; return wait_error; }
    Result close() { ++closes; return close_error; }
    void joined() { did_join = true; }
};
} // namespace fixture

extern "C" {
void* armGetTls() { return fixture::tls; }
Result svcGetProcessId(u64* pid, Handle process) { assert(process == 27); *pid = 8; return 0; }
Result fsStorageRead(FsStorage*, s64 offset, void* buffer, u64 size) {
    assert(offset == fixture::MemoryConfig.pointer_offset && size == sizeof(u64));
    std::memset(buffer, 0, size);
    return 0;
}
Result fsStorageWrite(FsStorage*, s64, const void*, u64) { ++fixture::storage_writes; return fixture::BadInput; }
Result svcSendSyncRequest(Handle handle) {
    using namespace fixture;
    progress();
    auto parsed = hipcParseRequest(tls);
    const auto* header = static_cast<const CmifInHeader*>(cmifGetAlignedDataStart(parsed.data.data_words, tls));
    assert(header->magic == CMIF_IN_HEADER_MAGIC);
    if (parsed.meta.type == CmifCommandType_Control && header->command_id == 3) {
        assert(handle == 25 || handle == 26);
        ounce::writePointerBufferSizeReply(tls);
        return 0;
    }
    assert(handle == 26);
    assert(parsed.meta.num_copy_handles == 1 && parsed.data.copy_handles[0] == 27);
    RoProcessMemoryRequest input;
    std::memcpy(&input, header + 1, sizeof(input));
    assert(input.process_id == 8 && !input.reserved);
    assert(input.address >= Base && input.address + input.size <= Base + bytes.size());
    if (header->command_id == RoProcessReadCommand) {
        ++ro_reads;
        assert(parsed.meta.num_recv_buffers == 1);
        void* output = hipcGetBufferAddress(parsed.data.recv_buffers);
        assert(hipcGetBufferSize(parsed.data.recv_buffers) >= input.size);
        std::memcpy(output, bytes.data() + input.address - Base, input.size);
    } else {
        assert(header->command_id == RoProcessWriteCommand);
        ++ro_writes;
        assert(parsed.meta.num_send_buffers == 1 && input.size <= 256);
        const void* source = hipcGetBufferAddress(parsed.data.send_buffers);
        assert(hipcGetBufferSize(parsed.data.send_buffers) >= input.size);
        std::memcpy(bytes.data() + input.address - Base, source, input.size);
        if (input.address == config.server + 0x44 && input.size == 1 && get<u8>(input.address) == 0xff)
            ++cookie_writes;
        if (input.address == config.server + 0x30 && corrupt_wait_write) put<u32>(input.address, 0xdead);
        for (u64 waiter : config.waiters) {
            if (input.address == waiter + 0x10 && get<u64>(input.address) == config.server) {
                selected_written = true;
                first_selection_tick = ticks;
                if (inject_conflict) {
                    auto view = get<ounce::LoaderSessionView>(config.server);
                    view.metadata ^= 1;
                    put(config.server, view);
                }
            }
        }
    }
    ounce::writeReply(tls, 0, nullptr, 0);
    return 0;
}
Result svcCancelSynchronization(Handle worker) {
    using namespace fixture;
    assert(worker >= 100 && worker < 107);
    ++cancellations;
    if (selected_written && !settle_at) settle_at = ticks + Clock::toTicks(Delay);
    return 0;
}
Result svcWaitSynchronization(s32* index, const Handle* handles, s32 count, u64 timeout) {
    using namespace fixture;
    assert(count == 1 && timeout == 0);
    progress();
    const bool ready = handles[0] == config.server_copy ? pending : signalled;
    assert(handles[0] == config.server_copy || handles[0] == config.event_read);
    *index = ready ? 0 : -1;
    return ready ? 0 : KERNELRESULT(TimedOut);
}
Result svcSignalEvent(Handle event) {
    using namespace fixture;
    assert(event == config.event_write);
    if (fail_restore_signal) return BadInput;
    signalled = true;
    wake_at = ticks + Clock::toTicks(25 * Millisecond);
    return 0;
}
Result svcClearEvent(Handle event) { assert(event == fixture::config.event_read); fixture::signalled = false; return 0; }
void svcSleepThread(s64 ns) {
    assert(ns > 0);
    fixture::ticks += fixture::Clock::toTicks(ns);
    fixture::progress();
}
}

int main() {
    using namespace fixture;
    // Before PM creation: the old synchronous empty step exhausts 30
    // observations without giving the delayed FS waiter elapsed time.
    reset();
    Memory memory(MemoryConfig, 26, 27);
    assert(!memory.begin());
    Gate synchronous(memory, config);
    bool pm_created = false;
    const Result park = synchronous.park();
    if (!park) pm_created = true;
    assert(park == KERNELRESULT(TimedOut) && !pm_created && !ticks);
    assert(selected_written && ro_reads && ro_writes && !storage_writes);
    assert(synchronous.restore() == BadInput);

    // Dedicated timed empty parking completes the same delayed receipt.
    reset();
    Memory timed_memory(MemoryConfig, 26, 27);
    assert(!timed_memory.begin());
    Gate timed(timed_memory, config);
    assert(!timedPark(timed));
    assert(Clock::toNs(ticks) >= Delay && Clock::toNs(ticks) < 1000000000ull);
    assert(get<u32>(config.request) == 0x01020304);
    assert(!timed.restore() && timed.restored() && !timed.pendingRestoreWithReceipt());
    assert(get<ounce::LoaderSessionView>(config.server).wait == config.original_wait);
    assert(!signalled && !storage_writes && cookie_writes == 1);
    const auto reads_after_restore = ro_reads;
    for (unsigned i = 0; i != 100; ++i) assert(timed.cachedProbe().read_result == 0);
    assert(ro_reads == reads_after_restore); // cached diagnostics do not issue RO requests

    // A different epoch on the same parent is also a valid empty receipt.
    reset(); same_parent = true;
    Memory epoch_memory(MemoryConfig, 26, 27);
    assert(!epoch_memory.begin());
    Gate epoch_gate(epoch_memory, config);
    assert(!timedPark(epoch_gate));
    assert(!epoch_gate.restore() && cookie_writes == 1 && epoch_gate.restored());

    // Poll calls before the scheduled observation do not issue extra IPC.
    reset();
    Memory paced_memory(MemoryConfig, 26, 27);
    assert(!paced_memory.begin());
    Gate paced(paced_memory, config);
    assert(!paced.beginPark());
    bool complete = false;
    assert(!paced.pollPark(complete) && !complete);
    const unsigned initial_reads = ro_reads;
    for (unsigned i = 0; i != 100; ++i) assert(!paced.pollPark(complete) && !complete);
    assert(initial_reads == ro_reads && !ticks);

    // A captured successful gate result is still a recorded value. Execute
    // actual restoration failure, then the production fail/finalize helpers.
    reset();
    Memory restore_failure_memory(MemoryConfig, 26, 27);
    assert(!restore_failure_memory.begin());
    Gate restore_failure_gate(restore_failure_memory, config);
    assert(!timedPark(restore_failure_gate));
    HenPluginReloadDiagnostics successful_primary = {};
    successful_primary.version = 1;
    reload::captureGateDiagnostics(successful_primary, 0, false, restore_failure_gate.diagnosticStep(),
                                    restore_failure_gate.cachedProbe(), config.original_wait, config.park);
    const u32 success_step = successful_primary.primary_gate_step;
    fail_restore_signal = true;
    const Result restore_failure = restore_failure_gate.restore();
    assert(restore_failure == BadInput);
    reload::captureGateDiagnostics(successful_primary, restore_failure, true, restore_failure_gate.diagnosticStep(),
                                    restore_failure_gate.cachedProbe(), config.original_wait, config.park);
    HenPluginReloadStatus restore_status = {};
    restore_status.restoration_result = restore_failure;
    reload::recordFailure(restore_status, successful_primary, restore_failure, true);
    bool worker_success = false, worker_quarantined = true;
    reload::finalizeWorker(false, false, worker_success, worker_quarantined, restore_status, &successful_primary);
    assert(successful_primary.flags & HEN_RELOAD_DIAG_PRIMARY_CAPTURED);
    assert(successful_primary.primary_result == 0 && successful_primary.primary_gate_step == success_step);
    assert(successful_primary.restoration_result == BadInput && restore_status.result == BadInput);
    assert(restore_status.phase == HenPluginReloadPhase_Quarantined && !worker_success);
    // A later supervisor deadline also cannot overwrite the recorded pair.
    reload::finalizeWorker(false, true, worker_success, worker_quarantined, restore_status, &successful_primary);
    assert(successful_primary.primary_result == 0 && successful_primary.restoration_result == BadInput);
    assert(restore_status.result == BadInput);

    // Post-PM counterexample: park succeeded; PM can be pending with PID0,
    // while a synchronous real request gives the identical result pair.
    reset();
    Memory post_memory(MemoryConfig, 26, 27);
    assert(!post_memory.begin());
    Gate post(post_memory, config);
    assert(!timedPark(post));
    pm_created = true;
    const u64 pm_pid = 0; // pending launch, not proof that PM was never created
    receipt = Receipt::Real;
    pending = true;
    std::array<u8, 0x100> output = {};
    const u64 before = ticks;
    assert(post.step(output.data()) == KERNELRESULT(TimedOut));
    assert(ticks == before && pm_created && !pm_pid);
    assert(post.restore() == BadInput);

    // Actual gate failure followed by successful restoration: the delayed
    // waiter settles after the primary timeout but before cleanup begins.
    reset();
    Memory late_memory(MemoryConfig, 26, 27);
    assert(!late_memory.begin());
    Gate late_gate(late_memory, config);
    assert(!timedPark(late_gate));
    receipt = Receipt::Real; pending = true;
    const Result gate_failure = late_gate.step(output.data());
    assert(gate_failure == KERNELRESULT(TimedOut));
    HenPluginReloadDiagnostics late_diagnostics = {};
    late_diagnostics.version = 1;
    reload::captureGateDiagnostics(late_diagnostics, gate_failure, false, late_gate.diagnosticStep(),
                                    late_gate.cachedProbe(), config.original_wait, config.park);
    svcSleepThread(Delay);
    assert(!late_gate.restore() && late_gate.restored());
    reload::captureGateDiagnostics(late_diagnostics, 0, true, late_gate.diagnosticStep(),
                                    late_gate.cachedProbe(), config.original_wait, config.park);
    const Result wait_failure = MAKERESULT(345, 221), close_failure = MAKERESULT(345, 222);
    for (unsigned mode = 0; mode != 3; ++mode) {
        PmKernel pm;
        pm.completed = mode != 0;
        pm.wait_error = mode == 1 ? wait_failure : 0;
        pm.close_error = mode == 2 ? close_failure : 0;
        const Result completion = reload::completePmLaunch(pm, gate_failure, 3 * Millisecond);
        assert(completion == gate_failure && !pm.did_join);
        assert(pm.waits == (mode != 0) && pm.closes == (mode == 2));
        reload::Journal journal;
        HenPluginReloadRequest input = {};
        std::strcpy(input.name, "moader");
        input.instance_id = 1; input.expected_process_id = 212; input.expected_generation = 1; input.nonce = 1;
        reload::Journal::Entry* entry = nullptr;
        assert(!journal.begin(80, input, &entry));
        entry->diagnostics = late_diagnostics;
        entry->diagnostics.operation_id = entry->status.operation_id;
        entry->diagnostics.flags |= HEN_RELOAD_DIAG_PM_CREATED | HEN_RELOAD_DIAG_PM_STARTED;
        if (pm.completed) entry->diagnostics.flags |= HEN_RELOAD_DIAG_PM_FINISHED;
        entry->status.flags |= HEN_PLUGIN_RELOAD_OLD_EXITED;
        reload::recordFailure(entry->status, entry->diagnostics, completion, !pm.did_join);
        worker_success = false; worker_quarantined = !pm.did_join;
        reload::finalizeWorker(false, false, worker_success, worker_quarantined, entry->status, &entry->diagnostics);
        journal.finish(entry->status.operation_id, worker_quarantined);
        assert(entry->status.result == gate_failure && !entry->status.restoration_result);
        assert(entry->diagnostics.primary_result == gate_failure && !entry->diagnostics.restoration_result);
        assert(!(entry->diagnostics.flags & HEN_RELOAD_DIAG_PM_JOINED));
        assert(!entry->status.new_process_id && journal.blocked && journal.active == entry->status.operation_id);
    }
    // Without an earlier gate failure, surface the PM error itself.
    PmKernel pending_pm;
    assert(reload::completePmLaunch(pending_pm, 0, 3 * Millisecond) == reload::Deadline);
    PmKernel failed_join;
    failed_join.completed = true; failed_join.wait_error = wait_failure;
    assert(reload::completePmLaunch(failed_join, 0, 3 * Millisecond) == wait_failure);
    PmKernel joined_pm;
    joined_pm.completed = true;
    assert(!reload::completePmLaunch(joined_pm, 0, 3 * Millisecond) && joined_pm.did_join);

    // Timed real request uses the established beginStep/pollStep contract.
    reset();
    Memory request_memory(MemoryConfig, 26, 27);
    assert(!request_memory.begin());
    Gate request_gate(request_memory, config);
    assert(!timedPark(request_gate));
    receipt = Receipt::Real;
    pending = true;
    assert(!timedRequest(request_gate, output.data()));
    assert(output == real_request && !pending);
    assert(!request_gate.restore() && request_gate.restored());
    assert(!request_memory.restore() && !storage_writes);

    // No progress remains bounded and restoration's selected-waiter guard
    // is preserved; the new API does not turn uncertainty into success.
    reset(); never_settle = true;
    Memory stuck_memory(MemoryConfig, 26, 27);
    assert(!stuck_memory.begin());
    Gate stuck(stuck_memory, config);
    assert(timedPark(stuck) == KERNELRESULT(TimedOut));
    assert(Clock::toNs(ticks) >= 1000000000ull && Clock::toNs(ticks) <= 1025000000ull);
    assert(stuck.diagnosticStep() == ounce::GateStep_StepDeadline);
    const auto primary_step = stuck.diagnosticStep();
    HenPluginReloadDiagnostics diagnostic = {};
    diagnostic.version = 1; diagnostic.operation_id = 1;
    diagnostic.stage = HenReloadLaunchStage_ParkPoll;
    diagnostic.flags = HEN_RELOAD_DIAG_PARK_ATTEMPTED;
    reload::captureGateDiagnostics(diagnostic, KERNELRESULT(TimedOut), false, primary_step,
                                    stuck.cachedProbe(), config.original_wait, config.park);
    assert(stuck.restore() == BadInput);
    assert(primary_step != stuck.diagnosticStep()); // why snapshots must preserve both
    reload::captureGateDiagnostics(diagnostic, BadInput, true, stuck.diagnosticStep(),
                                    stuck.cachedProbe(), config.original_wait, config.park);
    assert(diagnostic.primary_gate_step == primary_step);
    assert(diagnostic.restoration_gate_step == ounce::GateStep_RestoreReceiptSelected);
    assert(diagnostic.primary_result == KERNELRESULT(TimedOut) && diagnostic.restoration_result == BadInput);
    assert(diagnostic.probe_selected_mask && diagnostic.probe_wait_kind == 2);
    assert((diagnostic.flags & (HEN_RELOAD_DIAG_PRIMARY_CAPTURED | HEN_RELOAD_DIAG_RESTORE_ATTEMPTED)) ==
                              (HEN_RELOAD_DIAG_PRIMARY_CAPTURED | HEN_RELOAD_DIAG_RESTORE_ATTEMPTED));
    assert(!(diagnostic.flags & (HEN_RELOAD_DIAG_PARK_COMPLETED | HEN_RELOAD_DIAG_PM_CREATED)));

    // No progress after PM/start remains elapsed-time bounded too.
    reset();
    Memory real_stuck_memory(MemoryConfig, 26, 27);
    assert(!real_stuck_memory.begin());
    Gate real_stuck(real_stuck_memory, config);
    assert(!timedPark(real_stuck));
    receipt = Receipt::Real; pending = true; never_settle = true;
    const u64 real_start = ticks;
    assert(timedRequest(real_stuck, output.data()) == KERNELRESULT(TimedOut));
    assert(Clock::toNs(ticks - real_start) >= 1000000000ull && Clock::toNs(ticks - real_start) <= 1025000000ull);
    assert(real_stuck.restore() == BadInput);

    // Session identity and RO write readback checks are not relaxed.
    reset();
    auto wrong = get<ounce::LoaderSessionView>(config.server);
    wrong.metadata ^= 1; put(config.server, wrong);
    Memory wrong_memory(MemoryConfig, 26, 27);
    assert(!wrong_memory.begin());
    Gate wrong_gate(wrong_memory, config);
    assert(wrong_gate.beginPark() == BadInput && !ro_writes);
    assert(wrong_gate.diagnosticStep() == ounce::GateStep_ParkInspect);
    reset(); corrupt_wait_write = true;
    Memory corrupt_memory(MemoryConfig, 26, 27);
    assert(!corrupt_memory.begin());
    Gate corrupt_gate(corrupt_memory, config);
    assert(corrupt_gate.beginPark() == BadInput && !cancellations);
    assert(corrupt_gate.diagnosticStep() == ounce::GateStep_ParkPublishWait);
    reset(); inject_conflict = true;
    Memory conflict_memory(MemoryConfig, 26, 27);
    assert(!conflict_memory.begin());
    Gate conflict_gate(conflict_memory, config);
    assert(timedPark(conflict_gate) == BadInput);
    assert(conflict_gate.restore() == BadInput); // never restore a recycled/wrong session

    std::puts("actual RO LoaderGate: pre/post-PM synchronous exhaustion reproduced; timed parking/request, receipts, bounded failure and invariant checks passed");
}
