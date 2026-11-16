// SPDX-License-Identifier: GPL-2.0-only
#include <cassert>
#include <cstdio>
#include <thread>
#include "../source/census.hpp"
#include "../../libkaihen/source/cmif_server.hpp"

namespace { alignas(16) unsigned char tls[0x100]; }
extern "C" void* armGetTls() { return tls; }

int main() {
    HenCensus census;
    KaihenCensusRecord record = {};
    assert(!census.read(0, record));
    census.initialize(99, 31250000);
    for (unsigned i = 0; i < 80; i++)
        census.emit(KaihenCensusSource_Hen, KaihenCensusEvent_SessionCreated, i, 2, 3, 0,
            KaihenCensusFlag_ResultKnown, 0, 100 + i);
    auto header = census.header();
    assert(header.magic == KAIHEN_CENSUS_MAGIC && header.schema == 1);
    assert(header.first_sequence == 17 && header.next_sequence == 81);
    assert(header.record_count == 64 && header.overwritten == 16 && header.flags == 1);
    assert(header.source_mask == KAIHEN_CENSUS_SOURCE_MASK(KaihenCensusSource_Hen));
    assert(!census.read(16, record) && !census.read(81, record));
    for (uint64_t i = 17; i < 81; i++) {
        assert(census.read(i, record));
        assert(record.sequence == i && record.a == i - 1 && record.tick == 99 + i);
    }
    // Run actual SDK request packing and the actual production scalar query.
    for (const uint32_t command : {KAIHEN_CENSUS_HEADER_COMMAND, KAIHEN_CENSUS_RECORD_COMMAND}) {
        std::memset(tls, 0, sizeof(tls));
        CmifRequestFormat format = {}; format.request_id = command;
        format.data_size = command == KAIHEN_CENSUS_RECORD_COMMAND ? 8 : 0;
        auto wire = cmifMakeRequest(tls, format);
        const uint64_t sequence = 80;
        if (format.data_size) std::memcpy(wire.data, &sequence, 8);
        auto parsed = ounce::parseRequest(tls);
        std::array<unsigned char, 64> output = {}; size_t bytes = 0;
        const auto query = [&](bool scalar, uint32_t words, size_t size) {
            return census.query(command, scalar, words, parsed.data, size, output.data(), bytes);
        };
        assert(parsed.valid && ounce::hasOnlyInlineData(parsed));
        assert(query(true, parsed.hipc.meta.num_data_words, parsed.data_size) == HenCensus::Query::Success && bytes == 64);
        if (format.data_size) {
            std::memcpy(&record, output.data(), 64); assert(record.sequence == 80);
        } else {
            KaihenCensusHeader returned; std::memcpy(&returned, output.data(), 64); assert(returned.boot_id == 99);
        }
        assert(query(false, parsed.hipc.meta.num_data_words, parsed.data_size) == HenCensus::Query::Malformed && bytes == 0);
        assert(query(true, parsed.hipc.meta.num_data_words + 1, parsed.data_size + 4) == HenCensus::Query::Malformed);
        assert(query(true, parsed.hipc.meta.num_data_words, parsed.data_size - 1) == HenCensus::Query::Malformed);
    }
    unsigned char output[64]; size_t bytes;
    uint64_t gone = 16;
    assert(census.query(KAIHEN_CENSUS_RECORD_COMMAND, true, 10, &gone, 16, output, bytes) == HenCensus::Query::Unavailable);
    assert(census.header().next_sequence == header.next_sequence); // no query mutation

    // Real concurrent publication/sampling. Every successful SC snapshot must
    // have a self-consistent name/lookup pair; a sampler never spins for it.
    HenCensusSmMailbox mailbox;
    std::atomic<bool> done{false};
    std::thread worker([&] {
        for (uint64_t i = 1; i <= 10000; i++) {
            mailbox.publish(HenCensusSmMailbox::LookupPending, i, i);
            mailbox.publish(HenCensusSmMailbox::LookupReturned, i, i, i == 10000 ? 123 : 0);
        }
        mailbox.publish(HenCensusSmMailbox::Ended, 10000, 10000, 45, 67);
        done.store(true);
    });
    while (!done.load()) {
        HenCensusSmMailbox::View view;
        if (mailbox.sample(view)) assert(!(view.revision & 1) && view.name == view.lookup);
    }
    worker.join();
    HenCensusSmMailbox::View final;
    assert(mailbox.sample(final));
    assert(final.stage == HenCensusSmMailbox::Ended && final.result == 45 && final.close_result == 67);
    assert(final.failures == 1 && final.failure_name == 10000 && final.failure_result == 123);
    std::puts("HEN census: production wrap/read, real CMIF scalar validation and concurrent atomic mailbox passed");
}
