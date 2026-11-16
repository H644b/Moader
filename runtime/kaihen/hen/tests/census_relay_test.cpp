// SPDX-License-Identifier: GPL-2.0-only
#include <cassert>
#include <cstdio>
#include <cstring>
#include "../source/census_relay.cpp"
#include "../../libkaihen/source/cmif_server.hpp"

namespace {
alignas(16) u8 tls[0x100];
ThreadFunc entry;
void* argument;
unsigned sends, waits, closes, creates;
unsigned fail_at;
Result transport_failure, reply_failure, create_failure, start_failure, wait_failure, close_failure;
bool wrong_ack;
bool exit_pending;
unsigned unexpected_copy, unexpected_move;
bool reply_pid, short_reply, oversized_reply, bad_magic, bad_version, bad_padding, error_with_ack;
Result received_close_failure;
unsigned received_closes;
}
extern "C" {
void* armGetTls() { return tls; }
Result threadCreate(Thread* thread, ThreadFunc f, void* a, void*, size_t, int, int) {
    ++creates; thread->handle = 80; entry = f; argument = a; return create_failure;
}
Result threadStart(Thread*) { return start_failure; }
Result threadWaitForExit(Thread*) { assert(false); return 1; }
Result svcWaitSynchronization(s32* index, const Handle* handles, s32 count, u64 timeout) {
    assert(count == 1 && handles[0] == 80 && !timeout); ++waits;
    *index = 0; return exit_pending ? KERNELRESULT(TimedOut) : wait_failure;
}
Result threadClose(Thread*) { ++closes; return close_failure; }
Result svcCloseHandle(Handle handle) { assert(handle == 77 || handle == 78); ++received_closes; return received_close_failure; }
Result svcSendSyncRequest(Handle control) {
    assert(control == 42);
    const auto request = ounce::parseRequest(tls);
    assert(request.valid && ounce::hasOnlyInlineData(request));
    assert(request.command_id == KAIHEN_CENSUS_HEADER_COMMAND || request.command_id == KAIHEN_CENSUS_RECORD_COMMAND);
    ++sends;
    if (sends == fail_at && R_FAILED(transport_failure)) return transport_failure;
    u64 sequence = 0;
    if (request.command_id == KAIHEN_CENSUS_RECORD_COMMAND) {
        KaihenCensusRecord record{};
        std::memcpy(&record, request.data, sizeof(record)); sequence = record.sequence;
    }
    const KaihenCensusAck ack{wrong_ack ? 99u : 12u, sequence};
    const Result rc = sends == fail_at ? reply_failure : 0;
    ounce::writeReply(tls, rc, R_SUCCEEDED(rc) ? &ack : nullptr, R_SUCCEEDED(rc) ? sizeof(ack) : 0);
    if (unexpected_copy || unexpected_move || reply_pid) {
        std::memset(tls, 0, sizeof(tls));
        const auto out = hipcMakeRequestInline(tls, .num_data_words=12, .send_pid=reply_pid,
            .num_copy_handles=unexpected_copy, .num_move_handles=unexpected_move);
        if (unexpected_copy) out.copy_handles[0] = 77;
        if (unexpected_move) out.move_handles[0] = 78;
        auto* h = static_cast<CmifOutHeader*>(cmifGetAlignedDataStart(out.data_words,tls));
        *h = {CMIF_OUT_HEADER_MAGIC,0,0,0}; std::memcpy(h+1,&ack,sizeof(ack));
    } else {
        auto* h = reinterpret_cast<CmifOutHeader*>(tls + 16);
        if (short_reply) reinterpret_cast<HipcHeader*>(tls)->num_data_words=0;
        if (oversized_reply || error_with_ack) reinterpret_cast<HipcHeader*>(tls)->num_data_words=16;
        if (bad_magic) h->magic = 0;
        if (bad_version) h->version = 2;
        if (bad_padding) tls[48] = 1;
    }
    return 0;
}
}
namespace {
const KaihenCensusHeader header{KAIHEN_CENSUS_MAGIC, 1, 64, 64, 64, 0, 12, 1, 5, 0, 4, 2, 31250000};
KaihenCensusRecord records[4] = {{1}, {2}, {3}, {4}};
void reset() {
    sends = waits = closes = creates = fail_at = 0;
    transport_failure = reply_failure = create_failure = start_failure = wait_failure = close_failure = 0;
    wrong_ack = false;
    exit_pending = reply_pid = short_reply = oversized_reply = bad_magic = bad_version = bad_padding = error_with_ack = false;
    unexpected_copy = unexpected_move = received_closes = 0; received_close_failure = 0;
}
void run() { assert(entry); entry(argument); }
void start(CensusRelay& relay) {
    assert(!relay.start(42, 211, header, records, 4, 100));
    assert(relay.ownsControl(42, 211) && !relay.ready() && !sends);
}
}
int main() {
    reset();
    CensusRelay relay;
    start(relay);
    assert(!relay.poll(101) && !waits && !closes);
    assert(R_FAILED(relay.start(42, 211, header, records, 4, 101)) && creates == 1);
    run();
    assert(relay.ownsControl(42, 211)); // completion publication is not a joined worker
    assert(relay.poll(102) && relay.ready() && !relay.ownsControl(42, 211));
    assert(relay.report().state == CensusRelay::Complete && relay.report().acknowledged == 4);
    assert(relay.report().header_acknowledged && sends == 5 && waits == 1 && closes == 1);
    assert(!relay.poll(103) && waits == 1 && closes == 1);

    reset();
    CensusRelay failed;
    fail_at = 3; transport_failure = KERNELRESULT(ConnectionClosed);
    start(failed); run(); assert(failed.poll(102));
    assert(sends == 3 && failed.report().acknowledged == 1 && failed.report().state == CensusRelay::Failed);
    assert(failed.blocksControl(42, 211) && failed.disabledFor(211) && failed.ready());
    assert(R_FAILED(failed.start(42, 211, header, records, 4, 103)) && sends == 3);
    assert(R_FAILED(failed.start(43, 212, header, records, 4, 104)));
    assert(failed.blocksControl(42, 211)); // another target never clears poison

    reset();
    CensusRelay legacy;
    fail_at = 1; reply_failure = MAKERESULT(Module_Libnx, LibnxError_BadInput);
    start(legacy); run(); assert(legacy.poll(102));
    assert(legacy.report().state == CensusRelay::Unsupported && legacy.disabledFor(211));
    assert(!legacy.blocksControl(42, 211) && sends == 1);

    reset();
    CensusRelay busy;
    fail_at = 3; reply_failure = KAIHEN_CENSUS_RECEIVER_BUSY;
    start(busy); run(); assert(busy.poll(102));
    assert(busy.report().state == CensusRelay::Deferred && busy.report().acknowledged == 1);
    assert(!busy.disabledFor(211) && !busy.blocksControl(42, 211));
    fail_at = 0; reply_failure = 0; sends = 0;
    assert(!busy.start(42, 211, header, records + 1, 3, 103));
    run(); assert(busy.poll(104) && busy.report().acknowledged == 4 && sends == 4);

    reset();
    CensusRelay malformed;
    wrong_ack = true;
    start(malformed); run(); assert(malformed.poll(102));
    assert(malformed.report().state == CensusRelay::Failed && malformed.blocksControl(42, 211));
    assert(sends == 1); // bad success acknowledgement is not a legacy error

    for (unsigned variant = 0; variant < 9; ++variant) {
        reset(); CensusRelay invalid;
        if (variant == 0) short_reply = true;
        if (variant == 1) oversized_reply = true;
        if (variant == 2) bad_magic = true;
        if (variant == 3) bad_version = true;
        if (variant == 4) bad_padding = true;
        if (variant == 5) reply_pid = true;
        if (variant == 6) unexpected_copy = 1;
        if (variant == 7) unexpected_move = 1;
        if (variant == 8) { error_with_ack = true; fail_at = 1; reply_failure = MAKERESULT(Module_Libnx,LibnxError_BadInput); }
        start(invalid); run(); assert(invalid.poll(102));
        assert(invalid.report().state == CensusRelay::Failed && invalid.blocksControl(42,211) && sends == 1);
        assert(received_closes == (variant == 6 || variant == 7 ? 1u : 0u));
    }

    reset(); CensusRelay leaked;
    unexpected_copy = 1; received_close_failure = MAKERESULT(345,224);
    start(leaked); run(); assert(leaked.poll(102));
    assert(leaked.report().state == CensusRelay::Retained && !leaked.ready());
    assert(leaked.report().unexpected_handles == 1 && leaked.report().retained_handles == 1);
    assert(!leaked.poll(103) && received_closes == 1); // failed closes retain ownership without replay

    reset(); CensusRelay exiting;
    start(exiting); run(); exit_pending = true;
    assert(!exiting.poll(101) && !closes && exiting.ownsControl(42,211));
    assert(exiting.poll(100 + armNsToTicks(2000000000)) && !closes && exiting.report().timed_out);
    exit_pending = false;
    assert(exiting.poll(100 + armNsToTicks(3000000000)) && exiting.blocksControl(42,211));

    reset();
    CensusRelay timeout;
    start(timeout);
    assert(timeout.poll(100 + armNsToTicks(2000000000)) && !waits && !closes);
    assert(timeout.report().state == CensusRelay::Retained && timeout.ownsControl(42, 211));
    assert(!timeout.poll(100 + armNsToTicks(3000000000)) && !sends);
    run(); assert(timeout.poll(100 + armNsToTicks(4000000000)));
    assert(timeout.ready() && timeout.report().timed_out && timeout.disabledFor(211));
    assert(timeout.blocksControl(42, 211)); // late success never silently enables replay

    reset();
    CensusRelay bad_wait;
    start(bad_wait); run(); wait_failure = MAKERESULT(345, 221);
    assert(bad_wait.poll(102) && !bad_wait.ready() && bad_wait.ownsControl(42, 211));
    assert(bad_wait.report().state == CensusRelay::Retained && waits == 1 && !closes);
    assert(!bad_wait.poll(103) && waits == 1); // uncertain joins are not automatically replayed

    reset();
    CensusRelay bad_close;
    start(bad_close); run(); close_failure = MAKERESULT(345, 222);
    assert(bad_close.poll(102) && !bad_close.ready() && bad_close.ownsControl(42, 211));
    assert(waits == 1 && closes == 1 && !bad_close.poll(103) && waits == 1 && closes == 1);

    reset();
    CensusRelay unstarted;
    start_failure = MAKERESULT(345, 223); close_failure = MAKERESULT(345, 222);
    assert(R_FAILED(unstarted.start(42, 211, header, records, 4, 100)));
    assert(unstarted.ownsControl(42, 211) && !unstarted.ready() && !sends && !waits && closes == 1);
    assert(!unstarted.poll(101) && closes == 1);
    std::puts("census relay: real SDK messages, one worker, partial ACK, legacy detection, timeout and retained join ownership passed");
}
