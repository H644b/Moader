// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <atomic>
#include <switch.h>
#include <kaihen/census.h>

// One diagnostic batch at a time, over an existing plugin-control endpoint.
// The owner must pin that endpoint while ownsControl() is true. This object
// must remain alive if a native call or worker join cannot be confirmed.
class CensusRelay {
  public:
    static constexpr u32 BatchCapacity = 8;
    enum State : u32 { Idle, Pending, Complete, Unsupported, Failed, Retained, Deferred };
    struct Report {
        State state = Idle;
        Result result = 0;
        u64 process_id = 0, boot_id = 0, acknowledged = 0;
        bool header_acknowledged = false;
        bool timed_out = false;
        u32 unexpected_handles = 0, retained_handles = 0;
    };

    Result start(Handle borrowed_control, u64 process_id, const KaihenCensusHeader& header,
                 const KaihenCensusRecord* records, u32 count, u64 now);
    // Main-loop maintenance only, never called by a passive census query.
    // Returns true once when a batch becomes terminal or ownership is retained.
    bool poll(u64 now);
    bool ownsControl(Handle control, u64 process_id) const {
        return created_ && !closed_ && control_ == control && report_.process_id == process_id;
    }
    bool ready() const { return (!created_ || closed_) && !retained_handles_; }
    bool disabledFor(u64 pid) const { return poisoned_ || (disabled_pid_ && disabled_pid_ == pid); }
    bool blocksControl(Handle control, u64 pid) const {
        return ownsControl(control, pid) || (poisoned_ && report_.process_id == pid);
    }
    const Report& report() const { return report_; }

  private:
    static void worker(void* context);
    Result transfer();
    Result exchange(u32 command, const void* frame, KaihenCensusAck& acknowledgement);
    Thread thread_{};
    Handle control_ = INVALID_HANDLE;
    KaihenCensusHeader header_{};
    KaihenCensusRecord records_[BatchCapacity]{};
    u32 count_ = 0;
    u64 start_tick_ = 0, disabled_pid_ = 0;
    bool created_ = false, started_ = false, joined_ = false, closed_ = false;
    bool notified_ = false, join_failed_ = false, poisoned_ = false;
    std::atomic<bool> finished_{false};
    Result transfer_result_ = 0;
    u64 acknowledged_ = 0;
    bool header_acknowledged_ = false;
    bool unsupported_reply_ = false;
    bool backpressure_reply_ = false;
    Handle unexpected_[30]{};
    u32 unexpected_handles_ = 0, retained_handles_ = 0;
    Report report_{};
};
