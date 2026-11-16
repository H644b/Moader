// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <kaihen/census.h>
#ifdef LIBNX_HOST_TEST_TLS
#include <chrono>
#else
#include <switch/arm/counter.h>
#endif

inline uint64_t henCensusTick() {
#ifdef LIBNX_HOST_TEST_TLS
    return std::chrono::steady_clock::now().time_since_epoch().count();
#else
    return armGetSystemTick();
#endif
}
inline uint64_t henCensusFrequency() {
#ifdef LIBNX_HOST_TEST_TLS
    return static_cast<uint64_t>(std::chrono::steady_clock::period::den /
                                 std::chrono::steady_clock::period::num);
#else
    return armGetSystemTickFreq();
#endif
}

// Main-thread writer/readers only. Fixed storage; no services, allocations,
// locks, cleanup, or clock reads in header()/read()/query().
class HenCensus {
  public:
    enum class Query { Success, Malformed, Unavailable };
    void initialize(uint64_t boot_id, uint64_t tick_frequency) {
        records_ = {};
        header_ = {KAIHEN_CENSUS_MAGIC, KAIHEN_CENSUS_SCHEMA, 64, 64,
                   KAIHEN_CENSUS_CAPACITY, 0, boot_id, 1, 1, 0, 0, 0, tick_frequency};
    }
    KaihenCensusHeader header() const { return header_; }
    bool read(uint64_t sequence, KaihenCensusRecord& out) const {
        if (!header_.boot_id || sequence < header_.first_sequence || sequence >= header_.next_sequence)
            return false;
        const auto& record = records_[(sequence - 1) % KAIHEN_CENSUS_CAPACITY];
        if (record.sequence != sequence) return false;
        out = record;
        return true;
    }
    void emit(uint32_t source, uint32_t event, uint64_t a = 0, uint64_t b = 0,
              uint64_t c = 0, uint32_t result = 0, uint32_t flags = 0,
              uint64_t owner_pid = 0, uint64_t tick = henCensusTick()) {
        if (!header_.boot_id || source < 1 || source > 3 ||
            header_.next_sequence == std::numeric_limits<uint64_t>::max()) return;
        const uint64_t sequence = header_.next_sequence++;
        records_[(sequence - 1) % KAIHEN_CENSUS_CAPACITY] =
            {sequence, tick, owner_pid, source, event, result, flags, a, b, c};
        header_.source_mask |= KAIHEN_CENSUS_SOURCE_MASK(source);
        if (header_.record_count < KAIHEN_CENSUS_CAPACITY) header_.record_count++;
        else {
            header_.first_sequence++;
            header_.overwritten++;
            header_.flags |= KAIHEN_CENSUS_HEADER_OVERWRITTEN;
        }
    }

    // Scalar CMIF uses 16 reserved alignment bytes in its word count. With
    // no descriptors/handles, the actual aligned header leaves 8 trailing
    // bytes. Their contents are padding, not an additional scalar input.
    Query query(uint32_t command, bool scalar, uint32_t words, const void* input,
                size_t parsed_bytes, void* output, size_t& output_bytes) const {
        output_bytes = 0;
        const bool get_header = command == KAIHEN_CENSUS_HEADER_COMMAND;
        const bool get_record = command == KAIHEN_CENSUS_RECORD_COMMAND;
        const size_t bytes = get_record ? 8 : 0;
        if ((!get_header && !get_record) || !scalar || words != (32 + bytes) / 4 ||
            parsed_bytes != bytes + 8 || !output || (get_record && !input)) return Query::Malformed;
        if (get_header) {
            if (!header_.boot_id) return Query::Unavailable;
            std::memcpy(output, &header_, sizeof(header_));
        } else {
            uint64_t sequence;
            std::memcpy(&sequence, input, sizeof(sequence));
            KaihenCensusRecord record;
            if (!read(sequence, record)) return Query::Unavailable;
            std::memcpy(output, &record, sizeof(record));
        }
        output_bytes = 64;
        return Query::Success;
    }

  private:
    KaihenCensusHeader header_ = {};
    std::array<KaihenCensusRecord, KAIHEN_CENSUS_CAPACITY> records_ = {};
};

// One worker writer, main-thread sampler. All payload fields are atomic and
// sequentially consistent: failed single-attempt snapshots never read C++
// non-atomic worker memory. Revision gaps count coalesced mailbox transitions.
class HenCensusSmMailbox {
  public:
    enum Stage : uint32_t { Idle, LookupPending, LookupReturned, Ended };
    struct View {
        uint64_t revision, tick, name, lookup, failures, failure_name;
        uint32_t stage, result, close_result, failure_result, end_reason;
    };
    void publish(Stage stage, uint64_t name, uint64_t lookup, uint32_t result = 0,
                 uint32_t close_result = 0, uint32_t end_reason = 0) {
        revision_.fetch_add(1);
        tick_.store(henCensusTick()); name_.store(name); lookup_.store(lookup);
        stage_.store(stage); result_.store(result); close_result_.store(close_result);
        end_reason_.store(end_reason);
        if (stage == LookupReturned && result) {
            failures_.fetch_add(1); failure_name_.store(name); failure_result_.store(result);
        }
        revision_.fetch_add(1);
    }
    bool sample(View& out) const {
        const uint64_t before = revision_.load();
        if (before & 1) return false;
        View view = {before, tick_.load(), name_.load(), lookup_.load(), failures_.load(),
                     failure_name_.load(), stage_.load(), result_.load(), close_result_.load(), failure_result_.load(), end_reason_.load()};
        if (revision_.load() != before) return false;
        out = view;
        return true;
    }
  private:
    std::atomic<uint64_t> revision_{0}, tick_{0}, name_{0}, lookup_{0}, failures_{0}, failure_name_{0};
    std::atomic<uint32_t> stage_{Idle}, result_{0}, close_result_{0}, failure_result_{0}, end_reason_{0};
};
