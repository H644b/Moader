// SPDX-License-Identifier: GPL-2.0-only
#include "census_relay.hpp"
#include <switch/arm/counter.h>
#include <cstring>

namespace {
constexpr Result Invalid = MAKERESULT(Module_Libnx, LibnxError_BadInput);
constexpr Result Busy = MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized);
constexpr Result Deadline = MAKERESULT(Module_Libnx, LibnxError_Timeout);
constexpr u64 TransferTimeoutNs = 2000000000;
}

Result CensusRelay::start(Handle control, u64 pid, const KaihenCensusHeader& header,
                         const KaihenCensusRecord* records, u32 count, u64 now) {
    if (!ready() || disabledFor(pid)) return Busy;
    if (control == INVALID_HANDLE || !pid || !header.boot_id || count > BatchCapacity ||
        (count && !records)) return Invalid;
    control_ = control;
    header_ = header;
    count_ = count;
    if (count) std::memcpy(records_, records, count * sizeof(*records));
    start_tick_ = now;
    created_ = started_ = joined_ = closed_ = notified_ = join_failed_ = false;
    finished_.store(false, std::memory_order_relaxed);
    transfer_result_ = 0;
    acknowledged_ = 0;
    header_acknowledged_ = false;
    unsupported_reply_ = false;
    backpressure_reply_ = false;
    report_ = {Pending, 0, pid, header.boot_id, 0, false, false};
    Result rc = threadCreate(&thread_, worker, this, nullptr, 0x4000, 0x2c, -2);
    if (R_FAILED(rc)) {
        report_.state = Failed;
        report_.result = rc;
        disabled_pid_ = pid;
        return rc;
    }
    created_ = true;
    rc = threadStart(&thread_);
    if (R_SUCCEEDED(rc)) {
        started_ = true;
        return 0;
    }
    // No request ran, but a failed thread close still owns its descriptor.
    report_.result = rc;
    disabled_pid_ = pid;
    const Result close_result = threadClose(&thread_);
    closed_ = R_SUCCEEDED(close_result);
    report_.state = closed_ ? Failed : Retained;
    join_failed_ = !closed_;
    return rc;
}

Result CensusRelay::exchange(u32 command, const void* frame, KaihenCensusAck& acknowledgement) {
    auto* const message = static_cast<u8*>(armGetTls());
    std::memset(message, 0, 0x100);
    CmifRequestFormat format{};
    format.request_id = command;
    format.data_size = 64;
    const auto request = cmifMakeRequest(message, format);
    std::memcpy(request.data, frame, 64);
    const Result transport = svcSendSyncRequest(control_);
    if (R_FAILED(transport)) return transport; // TLS is not a confirmed reply

    HipcHeader hipc{};
    std::memcpy(&hipc, message, sizeof(hipc));
    if (hipc.has_special_header) {
        HipcSpecialHeader special{};
        std::memcpy(&special, message + sizeof(hipc), sizeof(special));
        const u32 count = special.num_copy_handles + special.num_move_handles;
        const size_t offset = sizeof(hipc) + sizeof(special) + (special.send_pid ? 8 : 0);
        // HIPC's two four-bit counts bound this list to thirty handles. A
        // completed native reply owns these received references even when its
        // scalar envelope is invalid. Close once; retain any failed closes.
        if (count > 30 || offset + count * sizeof(Handle) > 0x100) return Invalid;
        Result close_failure = 0;
        for (u32 i = 0; i < count; ++i) {
            Handle handle;
            std::memcpy(&handle, message + offset + i * sizeof(handle), sizeof(handle));
            ++unexpected_handles_;
            const Result rc = svcCloseHandle(handle);
            if (R_FAILED(rc)) {
                unexpected_[retained_handles_++] = handle;
                if (!close_failure) close_failure = rc;
            }
        }
        return close_failure ? close_failure : Invalid;
    }
    if (hipc.type || hipc.num_send_statics || hipc.num_send_buffers || hipc.num_recv_buffers ||
        hipc.num_exch_buffers || hipc.recv_static_mode || hipc.padding || hipc.recv_list_offset ||
        (hipc.num_data_words != 8 && hipc.num_data_words != 12)) return Invalid;
    CmifOutHeader reply{};
    std::memcpy(&reply, message + 16, sizeof(reply));
    if (reply.magic != CMIF_OUT_HEADER_MAGIC || reply.version || reply.token) return Invalid;
    const bool success = R_SUCCEEDED(reply.result);
    if (hipc.num_data_words != (success ? 12 : 8)) return Invalid;
    for (size_t i = 8; i < 16; ++i) if (message[i]) return Invalid;
    const size_t payload_end = 32 + (success ? sizeof(KaihenCensusAck) : 0);
    const size_t declared_end = 8 + hipc.num_data_words * 4;
    for (size_t i = payload_end; i < declared_end; ++i) if (message[i]) return Invalid;
    if (!success) {
        unsupported_reply_ = command == KAIHEN_CENSUS_HEADER_COMMAND && reply.result == Invalid;
        backpressure_reply_ = reply.result == KAIHEN_CENSUS_RECEIVER_BUSY;
        return reply.result;
    }
    std::memcpy(&acknowledgement, message + 32, sizeof(acknowledgement));
    return 0;
}

Result CensusRelay::transfer() {
    KaihenCensusAck ack{};
    Result rc = exchange(KAIHEN_CENSUS_HEADER_COMMAND, &header_, ack);
    if (R_FAILED(rc)) return rc;
    if (ack.boot_id != header_.boot_id || ack.sequence) return Invalid;
    header_acknowledged_ = true;
    for (u32 i = 0; i < count_; ++i) {
        ack = {};
        rc = exchange(KAIHEN_CENSUS_RECORD_COMMAND, &records_[i], ack);
        if (R_FAILED(rc)) return rc;
        if (ack.boot_id != header_.boot_id || ack.sequence != records_[i].sequence) return Invalid;
        acknowledged_ = ack.sequence;
    }
    return 0;
}

void CensusRelay::worker(void* context) {
    auto& self = *static_cast<CensusRelay*>(context);
    self.transfer_result_ = self.transfer();
    self.finished_.store(true, std::memory_order_release);
}

bool CensusRelay::poll(u64 now) {
    if (notified_ || !created_ || closed_ || join_failed_) return false;
    const auto deadline = [&] {
        if (!report_.timed_out && now - start_tick_ >= armNsToTicks(TransferTimeoutNs)) {
            // There is no safe cancellation of SendSyncRequest. Keep the worker,
            // buffer and borrowed control endpoint alive; never replay it.
            report_.timed_out = true;
            report_.state = Retained;
            report_.result = Deadline;
            disabled_pid_ = report_.process_id;
            poisoned_ = true;
            return true;
        }
        return false;
    };
    if (!finished_.load(std::memory_order_acquire)) return deadline();
    report_.acknowledged = acknowledged_;
    report_.header_acknowledged = header_acknowledged_;
    report_.unexpected_handles = unexpected_handles_;
    report_.retained_handles = retained_handles_;
    Result rc = 0;
    if (started_ && !joined_) {
        // finished_ precedes SDK TLS cleanup. Observe real kernel thread exit
        // with a zero-timeout wait; never block HEN on threadWaitForExit().
        s32 index = -1;
        rc = svcWaitSynchronization(&index, &thread_.handle, 1, 0);
        if (rc == KERNELRESULT(TimedOut)) return deadline();
        if (R_SUCCEEDED(rc) && index != 0) rc = Invalid;
        if (R_SUCCEEDED(rc)) joined_ = true;
    }
    if (R_SUCCEEDED(rc)) {
        rc = threadClose(&thread_);
        if (R_SUCCEEDED(rc)) closed_ = true;
    }
    notified_ = true;
    if (R_FAILED(rc)) {
        join_failed_ = true;
        disabled_pid_ = report_.process_id;
        poisoned_ = true;
        report_.state = Retained;
        report_.result = rc;
        return true;
    }
    if (report_.timed_out) {
        // A late completion does not silently re-enable this channel.
        report_.state = Failed;
        return true;
    }
    report_.result = transfer_result_;
    if (backpressure_reply_) report_.state = Deferred;
    else if (R_SUCCEEDED(transfer_result_)) report_.state = Complete;
    else {
        disabled_pid_ = report_.process_id;
        // An explicit unsupported command is a completed legacy reply. Other
        // failures are not permission to reuse an uncertain control channel.
        const bool unsupported = unsupported_reply_;
        report_.state = retained_handles_ ? Retained : (unsupported ? Unsupported : Failed);
        poisoned_ = !unsupported;
    }
    return true;
}
