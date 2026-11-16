#pragma once
// The wire contract has one owner; host builds also use this canonical header.
#include "../../runtime/kaihen/libkaihen/include/kaihen/census.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace moader {
static_assert(std::atomic<uint64_t>::is_always_lock_free, "Census CONTROL counters must not use a hidden lock.");
// This cache is independent of Library/Context. The CONTROL callback does one
// non-waiting lock attempt and fixed-memory work only. Serialization is separate.
class HostCensus {
public:
    enum class Status : uint32_t { None, Header, Record, RepeatedHeader, BadSize,
        BadCommand, BadHeader, StaleHeader, NoHeader, BadRecord, DuplicateOrReordered,
        EarlierTickRecord, RetiredBoot };
    struct Snapshot {
        bool available = false;
        KaihenCensusHeader header{}, last_header_frame{};
        KaihenCensusRecord last_record_frame{};
        std::array<KaihenCensusRecord, KAIHEN_CENSUS_CAPACITY> records{};
        uint32_t count = 0, last_command = 0, last_frame_bytes = 0;
        Status last_status = Status::None;
        uint64_t receiver_id = 0, generation = 0, last_record_boot_id = 0,
                 last_record_generation = 0, accepted_records = 0, rejected_frames = 0,
                 repeated_headers = 0, sequence_gaps = 0, last_sequence = 0,
                 busy_frames = 0, tick_regressions = 0;
    };
    explicit HostCensus(uint64_t receiver_id = 0) noexcept { state_.receiver_id = receiver_id; }
    // Success returns exactly the ACK specified by the wire contract. Failure
    // leaves ack untouched; no stale/duplicate record is acknowledged as fresh.
    bool receive(uint32_t command, const void* input, size_t bytes, KaihenCensusAck& ack,
                 bool* busy = nullptr) noexcept {
        if (busy) *busy = false;
        if (lock_.test_and_set(std::memory_order_acquire)) {
            if (busy) *busy = true;
            busy_.fetch_add(1, std::memory_order_relaxed); return false;
        }
        Guard guard{lock_};
        state_.last_command = command;
        state_.last_frame_bytes = bytes > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(bytes);
        if (bytes != 64 || !input) return reject(Status::BadSize);
        if (command == KAIHEN_CENSUS_HEADER_COMMAND) {
            KaihenCensusHeader h; std::memcpy(&h, input, sizeof(h));
            state_.last_header_frame = h;
            if (!valid(h)) return reject(Status::BadHeader);
            const bool reset = !state_.available || h.boot_id != state_.header.boot_id;
            if (reset) {
                for (uint64_t boot : retired_) if (boot && boot == h.boot_id) return reject(Status::RetiredBoot);
                if (state_.available) { retired_[retired_at_] = state_.header.boot_id; retired_at_ = (retired_at_ + 1) % retired_.size(); }
                ++state_.generation;
                state_.count = 0; state_.records = {}; state_.last_sequence = 0;
                last_tick_ = 0;
            } else {
                const auto& old = state_.header;
                if (h.capacity != old.capacity || h.tick_frequency != old.tick_frequency ||
                    h.first_sequence < old.first_sequence || h.next_sequence < old.next_sequence ||
                    h.overwritten < old.overwritten || (h.source_mask & old.source_mask) != old.source_mask)
                    return reject(Status::StaleHeader);
                if (!std::memcmp(&h, &old, sizeof(h))) {
                    ++state_.repeated_headers; state_.last_status = Status::RepeatedHeader;
                    ack = {h.boot_id, 0}; return true;
                }
            }
            state_.available = true; state_.header = h;
            uint32_t kept = 0;
            for (uint32_t i = 0; i < state_.count; ++i)
                if (state_.records[i].sequence >= h.first_sequence) state_.records[kept++] = state_.records[i];
            state_.count = kept;
            state_.last_status = Status::Header; ack = {h.boot_id, 0}; return true;
        }
        if (command != KAIHEN_CENSUS_RECORD_COMMAND) return reject(Status::BadCommand);
        KaihenCensusRecord r; std::memcpy(&r, input, sizeof(r)); state_.last_record_frame = r;
        state_.last_record_boot_id = state_.header.boot_id;
        state_.last_record_generation = state_.generation;
        if (!state_.available) return reject(Status::NoHeader);
        const auto& h = state_.header;
        const bool event_ok = (r.source == KaihenCensusSource_Hen && r.event >= 1 && r.event <= 8) ||
            (r.source == KaihenCensusSource_Sm && r.event >= 16 && r.event <= 24) ||
            (r.source == KaihenCensusSource_Game && r.event >= 32 && r.event <= 39);
        if (!event_ok || (r.flags & ~UINT32_C(63)) ||
            ((r.flags & KaihenCensusFlag_Before) && (r.flags & KaihenCensusFlag_After)) ||
            !(h.source_mask & KAIHEN_CENSUS_SOURCE_MASK(r.source)) ||
            r.sequence < h.first_sequence || r.sequence >= h.next_sequence)
            return reject(Status::BadRecord);
        if (r.sequence <= state_.last_sequence) return reject(Status::DuplicateOrReordered);
        // A main-thread sample of an SM mailbox can carry an earlier worker
        // timestamp. Sequence orders publication; preserve and flag tick order.
        const bool earlier_tick = state_.last_sequence && r.tick < last_tick_;
        const uint64_t expected = state_.last_sequence ? state_.last_sequence + 1 : h.first_sequence;
        if (r.sequence > expected) state_.sequence_gaps += r.sequence - expected;
        // Header consistency ensures at most capacity entries within its window.
        if (state_.count == state_.records.size()) return reject(Status::BadRecord);
        state_.records[state_.count++] = r; state_.last_sequence = r.sequence; last_tick_ = r.tick;
        ++state_.accepted_records;
        if (earlier_tick) ++state_.tick_regressions;
        state_.last_status = earlier_tick ? Status::EarlierTickRecord : Status::Record;
        ack = {h.boot_id, r.sequence}; return true;
    }
    bool snapshot(Snapshot& out) const noexcept {
        if (lock_.test_and_set(std::memory_order_acquire)) return false;
        Guard guard{lock_}; out = state_; out.busy_frames = busy_.load(std::memory_order_relaxed); return true;
    }
private:
    struct Guard { std::atomic_flag& lock; ~Guard() { lock.clear(std::memory_order_release); } };
    static bool valid(const KaihenCensusHeader& h) noexcept {
        return h.magic == KAIHEN_CENSUS_MAGIC && h.schema == KAIHEN_CENSUS_SCHEMA &&
            h.header_bytes == sizeof(h) && h.record_bytes == sizeof(KaihenCensusRecord) &&
            h.capacity && h.capacity <= KAIHEN_CENSUS_CAPACITY && h.boot_id && h.tick_frequency &&
            !(h.flags & ~KAIHEN_CENSUS_HEADER_OVERWRITTEN) && !(h.source_mask & ~KAIHEN_CENSUS_KNOWN_SOURCE_MASK) &&
            h.first_sequence && h.next_sequence >= h.first_sequence &&
            h.next_sequence - h.first_sequence == h.record_count && h.record_count <= h.capacity &&
            h.overwritten == h.first_sequence - 1 &&
            (!h.overwritten || h.record_count == h.capacity) &&
            bool(h.flags & KAIHEN_CENSUS_HEADER_OVERWRITTEN) == bool(h.overwritten) &&
            (!h.record_count || h.source_mask);
    }
    bool reject(Status s) noexcept { ++state_.rejected_frames; state_.last_status = s; return false; }
    mutable std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    std::atomic<uint64_t> busy_{0};
    Snapshot state_{};
    std::array<uint64_t, KAIHEN_CENSUS_CAPACITY> retired_{};
    size_t retired_at_ = 0;
    uint64_t last_tick_ = 0;
};
}
