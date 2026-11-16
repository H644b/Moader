// SPDX-License-Identifier: GPL-2.0-only
#include <cassert>
#include <cstring>
#include <cstdio>
#include <initializer_list>
#include "../../libkaihen/source/plugin_server.cpp"

namespace {
alignas(16) u8 tls[0x100];
unsigned callbacks, replies, closes, creates;
Result reply_transport = 0, callback_result = 0;
size_t callback_size = sizeof(KaihenCensusAck);
bool overwrite_tls = false;
u32 expected_command;
u8 expected_input[64];
}
extern "C" {
void* armGetTls() { return tls; }
void mutexLock(Mutex*) {}
void mutexUnlock(Mutex*) {}
Result svcCloseHandle(Handle h) { assert(h == 77); ++closes; return 0; }
Result svcSignalEvent(Handle) { return 0; }
Result svcCreateEvent(Handle* w, Handle* r) { *w = 60; *r = 61; return 0; }
Result svcClearEvent(Handle) { return 0; }
Result svcCreateSession(Handle* s, Handle* c, u32, u64) {
    ++creates; *s = 50; *c = 51; return 0;
}
Result svcReplyAndReceive(s32*, const Handle*, s32 n, Handle h, u64 timeout) {
    assert(n == 0 && h == 20 && timeout == 0);
    ++replies;
    return R_SUCCEEDED(reply_transport) ? KERNELRESULT(TimedOut) : reply_transport;
}
Result threadCreate(Thread*, ThreadFunc, void*, void*, size_t, int, int) { return 0; }
Result threadStart(Thread*) { return 0; }
Result threadClose(Thread*) { return 0; }
Result threadWaitForExit(Thread*) { return 0; }
}
namespace {
Result receiver(u32 command, const void* input, size_t size, void* output, size_t* out_size, void* ctx) {
    assert(ctx == &callbacks && command == expected_command && size == 64);
    assert(!std::memcmp(input, expected_input, 64));
    if (overwrite_tls) {
        std::memset(tls, 0xa5, sizeof(tls));
        assert(!std::memcmp(input, expected_input, 64)); // input is not borrowed TLS
    }
    ++callbacks;
    const KaihenCensusAck ack{91, command == KAIHEN_CENSUS_HEADER_COMMAND ? 0u : 7u};
    std::memcpy(output, &ack, sizeof(ack));
    *out_size = callback_size;
    return callback_result;
}
void request(u32 command, u32 bytes = 64, bool handles = false, bool pid = false) {
    std::memset(tls, 0, sizeof(tls));
    CmifRequestFormat format{};
    format.request_id = command;
    format.data_size = bytes;
    format.num_handles = handles ? 1 : 0;
    format.send_pid = pid;
    auto msg = cmifMakeRequest(tls, format);
    if (handles) msg.hipc.copy_handles[0] = 77;
    for (size_t i = 0; i < sizeof(expected_input); ++i) expected_input[i] = u8(i + 1);
    std::memcpy(msg.data, expected_input, bytes < 64 ? bytes : 64);
    expected_command = command;
    callbacks = replies = closes = 0;
}
Result response(KaihenCensusAck* ack = nullptr) {
    const auto hipc = hipcParseRequest(tls);
    assert(!hipc.meta.num_copy_handles && !hipc.meta.num_move_handles);
    const auto* header = static_cast<const CmifOutHeader*>(cmifGetAlignedDataStart(hipc.data.data_words, tls));
    assert(header->magic == CMIF_OUT_HEADER_MAGIC);
    if (ack && R_SUCCEEDED(header->result)) std::memcpy(ack, header + 1, sizeof(*ack));
    return header->result;
}
void rejected(PluginService& service) {
    assert(!serveControlRequest(&service));
    assert(R_FAILED(response()) && callbacks == 0 && replies == 1);
}
}
int main() {
    PluginService service{};
    service.control_session = 20;
    service.diagnostics_handler = receiver;
    service.diagnostics_context = &callbacks;
    for (u32 command : {KAIHEN_CENSUS_HEADER_COMMAND, KAIHEN_CENSUS_RECORD_COMMAND}) {
        request(command);
        assert(isCensusFrame(ounce::parseRequest(tls)));
        overwrite_tls = true;
        assert(!serveControlRequest(&service));
        overwrite_tls = false;
        KaihenCensusAck ack{};
        assert(!response(&ack) && ack.boot_id == 91);
        assert(ack.sequence == (command == KAIHEN_CENSUS_HEADER_COMMAND ? 0 : 7));
        assert(callbacks == 1 && replies == 1 && !closes && !creates);
    }
    request(KAIHEN_CENSUS_HEADER_COMMAND, 60); rejected(service);
    assert(response() == KAIHEN_CENSUS_RECEIVER_REJECTED);
    request(KAIHEN_CENSUS_HEADER_COMMAND, 80); rejected(service);
    request(KAIHEN_CENSUS_HEADER_COMMAND);
    auto parsed = ounce::parseRequest(tls);
    assert(parsed.data_size > 64);
    const_cast<u8*>(parsed.data)[64] = 1;
    rejected(service); // nonzero alignment padding is not a frame
    request(KAIHEN_CENSUS_HEADER_COMMAND, 64, true); rejected(service);
    assert(closes == 1); // malformed transferred handles still closed once
    request(KAIHEN_CENSUS_HEADER_COMMAND, 64, false, true); rejected(service);
    request(KAIHEN_CENSUS_HEADER_COMMAND + 10); rejected(service);
    request(KAIHEN_CENSUS_HEADER_COMMAND);
    service.diagnostics_handler = nullptr; rejected(service);
    assert(response() == MAKERESULT(Module_Libnx, LibnxError_BadInput));
    service.diagnostics_handler = receiver;
    request(KAIHEN_CENSUS_RECORD_COMMAND);
    callback_size = 8;
    assert(!serveControlRequest(&service) && response() == KAIHEN_CENSUS_RECEIVER_REJECTED && callbacks == 1);
    callback_size = sizeof(KaihenCensusAck);
    request(KAIHEN_CENSUS_HEADER_COMMAND);
    callback_result = MAKERESULT(Module_Libnx, LibnxError_BadInput);
    assert(!serveControlRequest(&service) && response() == KAIHEN_CENSUS_RECEIVER_REJECTED);
    request(KAIHEN_CENSUS_RECORD_COMMAND);
    callback_result = MAKERESULT(345, 222);
    assert(!serveControlRequest(&service) && response() == callback_result);
    callback_result = 0;
    request(KAIHEN_CENSUS_RECORD_COMMAND);
    reply_transport = KERNELRESULT(ConnectionClosed);
    assert(serveControlRequest(&service) == reply_transport && callbacks == 1 && replies == 1);
    assert(!creates); // diagnostic path never creates another client session
    std::puts("census control: real CMIF dispatch, strict frames, owned input, legacy/error replies, no new session passed");
}
