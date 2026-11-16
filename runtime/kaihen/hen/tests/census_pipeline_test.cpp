// SPDX-License-Identifier: GPL-2.0-only
// Production ring -> relay -> real CMIF server -> resident cache. Scripted
// worker scheduling and a loopback SVC seam do not model kernel concurrency.
#include <cassert>
#include <cstdio>
#include <cstring>
#include "../source/census.hpp"
#include "../source/census_relay.cpp"
#include "../../libkaihen/source/plugin_server.cpp"
#include "../../../../include/moader/host_census.hpp"

namespace {
alignas(16) u8 tls[0x100];
PluginService endpoint{};
ThreadFunc entry;
void* argument;
unsigned requests, replies, waits, closes;
bool bad_ack = false;
bool short_ack = false;
}
extern "C" {
void* armGetTls() { return tls; }
void mutexLock(Mutex*) {}
void mutexUnlock(Mutex*) {}
Result threadCreate(Thread* thread, ThreadFunc f, void* a, void*, size_t, int, int) {
    thread->handle = 80; entry = f; argument = a; return 0;
}
Result threadStart(Thread*) { return 0; }
Result threadWaitForExit(Thread*) { assert(false); return 1; }
Result svcWaitSynchronization(s32* index, const Handle* handles, s32 count, u64 timeout) {
    assert(count == 1 && handles[0] == 80 && !timeout); ++waits; *index = 0; return 0;
}
Result threadClose(Thread*) { ++closes; return 0; }
Result svcCreateEvent(Handle*, Handle*) { assert(false); return 1; }
Result svcSignalEvent(Handle) { assert(false); return 1; }
Result svcClearEvent(Handle) { assert(false); return 1; }
Result svcCloseHandle(Handle) { assert(false); return 1; }
Result svcCreateSession(Handle*, Handle*, u32, u64) { assert(false); return 1; }
Result svcReplyAndReceive(s32*, const Handle*, s32 count, Handle reply, u64 timeout) {
    assert(!count && reply == 20 && !timeout); ++replies;
    return KERNELRESULT(TimedOut);
}
Result svcSendSyncRequest(Handle h) {
    assert(h == 42); ++requests;
    assert(isCensusFrame(ounce::parseRequest(tls))); // includes real SDK padding
    assert(!serveControlRequest(&endpoint));
    return 0;
}
}
namespace {
Result receive(u32 command, const void* input, size_t bytes, void* output, size_t* output_bytes, void* context) {
    auto& cache = *static_cast<moader::HostCensus*>(context);
    KaihenCensusAck ack{}; bool busy = false;
    *output_bytes = 0;
    if (!cache.receive(command, input, bytes, ack, &busy))
        return busy ? KAIHEN_CENSUS_RECEIVER_BUSY : KAIHEN_CENSUS_RECEIVER_REJECTED;
    if (bad_ack) ++ack.boot_id;
    std::memcpy(output, &ack, sizeof(ack)); *output_bytes = short_ack ? 8 : sizeof(ack); return 0;
}
}
int main() {
    HenCensus producer;
    producer.initialize(99, 31250000);
    for (u64 i = 1; i <= 80; ++i)
        producer.emit(KaihenCensusSource_Hen, KaihenCensusEvent_HenCounts, i, 0, 0, 0, 0, 0, i);
    moader::HostCensus cache(123);
    endpoint.control_session = 20;
    endpoint.diagnostics_handler = receive;
    endpoint.diagnostics_context = &cache;
    CensusRelay relay;
    u64 delivered = 0;
    bool changed = false;
    while (delivered + 1 < producer.header().next_sequence) {
        const auto h = producer.header();
        u64 next = delivered + 1 < h.first_sequence ? h.first_sequence : delivered + 1;
        KaihenCensusRecord batch[CensusRelay::BatchCapacity]{};
        u32 count = 0;
        while (count < CensusRelay::BatchCapacity && next < h.next_sequence)
            assert(producer.read(next++, batch[count++]));
        assert(!relay.start(42, 211, h, batch, count, 100));
        if (!changed) {
            // New records committed after the immutable batch was copied.
            for (u64 i = 81; i <= 90; ++i)
                producer.emit(KaihenCensusSource_Sm, KaihenCensusEvent_SmCounts, i, 0, 0, 0, 0, 0, i);
            changed = true;
        }
        std::memset(tls, 0xa5, sizeof(tls)); // dirty first IPC buffer must not leak into padding
        entry(argument);
        assert(relay.poll(101) && relay.report().state == CensusRelay::Complete);
        delivered = relay.report().acknowledged;
    }
    moader::HostCensus::Snapshot s;
    assert(cache.snapshot(s) && s.available && s.header.boot_id == 99 && s.last_sequence == 90);
    assert(s.header.first_sequence == 27 && s.header.overwritten == 26);
    assert(s.sequence_gaps == 2 && s.records[0].sequence == 27 && s.records[s.count - 1].sequence == 90);
    assert(s.accepted_records == 72 && s.rejected_frames == 0 && requests == replies && waits == closes);
    // A malformed success ACK stops the sender even if the receiver accepted
    // the header. It never silently replays the uncertain frame.
    CensusRelay malformed;
    bad_ack = true;
    assert(!malformed.start(42, 212, producer.header(), nullptr, 0, 200));
    entry(argument); assert(malformed.poll(201));
    assert(malformed.report().state == CensusRelay::Failed && malformed.blocksControl(42, 212));
    CensusRelay contract;
    bad_ack = false; short_ack = true;
    assert(!contract.start(42, 213, producer.header(), nullptr, 0, 300));
    entry(argument); assert(contract.poll(301));
    assert(contract.report().state == CensusRelay::Failed &&
        contract.report().result == KAIHEN_CENSUS_RECEIVER_REJECTED && contract.blocksControl(42, 213));
    std::puts("census pipeline: production ring/SDK/dispatcher/cache, dirty TLS, wrap and in-flight mutation passed");
}
