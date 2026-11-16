// SPDX-License-Identifier: GPL-2.0-only
#include <cassert>
#include <cstring>
#include <cstdio>
#include "../../libkaihen/source/plugin_server.cpp"

namespace {
alignas(16) u8 tls[0x100];
bool reply_sent = false, ack = false;
unsigned callbacks = 0;
Result send_result = 0;
Thread* fail_wait = nullptr;
Thread* fail_close = nullptr;
unsigned waits = 0, thread_closes = 0, handle_closes = 0;
}
extern "C" {
void* armGetTls() { return tls; }
void mutexLock(Mutex*) {}
void mutexUnlock(Mutex*) {}
Result svcCloseHandle(Handle) { ++handle_closes; return 0; }
Result svcSignalEvent(Handle) { return 0; }
Result svcCreateEvent(Handle* write, Handle* read) { *write = 60; *read = 61; return 0; }
Result svcClearEvent(Handle) { return 0; }
Result svcCreateSession(Handle* server, Handle* client, u32, u64) { *server = 50; *client = 51; return 0; }
Result svcReplyAndReceive(s32*, const Handle*, s32, Handle, u64) {
    assert(!ack); // post-reply callback must not have run early
    reply_sent = R_SUCCEEDED(send_result);
    return R_SUCCEEDED(send_result) ? KERNELRESULT(TimedOut) : send_result;
}
Result threadCreate(Thread*, ThreadFunc, void*, void*, size_t, int, int) { return 0; }
Result threadStart(Thread*) { return 0; }
Result threadClose(Thread* thread) { ++thread_closes; return thread == fail_close ? MAKERESULT(345, 222) : 0; }
Result threadWaitForExit(Thread* thread) { ++waits; return thread == fail_wait ? MAKERESULT(345, 221) : 0; }
}
namespace {
Result lifecycle(const KaihenPluginLifecycleRequest* input, KaihenPluginLifecycleReply* output, void*) {
    ++callbacks;
    assert(input->operation_id == 19);
    if (input->command == KaihenPluginLifecycle_CommitAcknowledged) {
        assert(reply_sent);
        ack = true;
        return 0;
    }
    assert(input->command == KaihenPluginLifecycle_Commit);
    assert(!reply_sent && !ack);
    *output = {1, KAIHEN_PLUGIN_LIFECYCLE_COOPERATIVE, KaihenPluginLifecycleState_Quiesced, 0, 19, 0};
    return 0;
}
void request(u32 command) {
    std::memset(tls, 0, sizeof(tls));
    CmifRequestFormat format = {};
    format.request_id = KAIHEN_PLUGIN_CONTROL_LIFECYCLE;
    format.data_size = sizeof(KaihenPluginLifecycleRequest);
    auto message = cmifMakeRequest(tls, format);
    const KaihenPluginLifecycleRequest input = {command, 0, 19};
    std::memcpy(message.data, &input, sizeof(input));
    reply_sent = ack = false;
    callbacks = 0;
}
}
int main() {
    PluginService service = {};
    service.control_session = 20;
    service.lifecycle_handler = lifecycle;
    request(KaihenPluginLifecycle_Commit);
    assert(!serveControlRequest(&service));
    assert(reply_sent && ack && callbacks == 2);
    request(KaihenPluginLifecycle_Commit);
    send_result = KERNELRESULT(ConnectionClosed);
    assert(serveControlRequest(&service) == send_result);
    assert(!reply_sent && !ack && callbacks == 1);
    request(KaihenPluginLifecycle_CommitAcknowledged);
    send_result = 0;
    assert(!serveControlRequest(&service));
    assert(reply_sent && !ack && !callbacks); // remote internal notification rejected
    request(KaihenPluginLifecycle_Commit);
    service.lifecycle_handler = nullptr;
    assert(!serveControlRequest(&service));
    assert(reply_sent && !ack && !callbacks); // legacy plugin has no shutdown command
    PluginService owned = {};
    owned.started = true;
    owned.control_thread_state = owned.worker_thread_state = {true, true, false, false};
    owned.stop_write = 40; owned.stop_read = 41; owned.wake_write = 42; owned.wake_read = 43;
    handle_closes = waits = thread_closes = 0;
    fail_wait = &owned.control_thread;
    assert(R_FAILED(pluginServiceStop(&owned)) && owned.started && !handle_closes && !thread_closes);
    Handle extra = INVALID_HANDLE;
    assert(createClientSession(&owned, &extra) == KAIHEN_PLUGIN_LIFECYCLE_BUSY && extra == INVALID_HANDLE);
    fail_wait = nullptr;
    fail_close = &owned.control_thread;
    assert(R_FAILED(pluginServiceStop(&owned)) && owned.started && !handle_closes);
    assert(owned.control_thread_state.exited && !owned.control_thread_state.closed);
    fail_close = nullptr;
    fail_wait = &owned.worker_thread;
    assert(R_FAILED(pluginServiceStop(&owned)) && owned.started && !handle_closes);
    assert(owned.control_thread_state.closed && !owned.worker_thread_state.exited);
    fail_wait = nullptr;
    fail_close = &owned.worker_thread;
    assert(R_FAILED(pluginServiceStop(&owned)) && owned.started && !handle_closes);
    assert(owned.worker_thread_state.exited && !owned.worker_thread_state.closed);
    fail_close = nullptr;
    assert(!pluginServiceStop(&owned) && !owned.started && handle_closes == 4);
    assert(waits == 4 && thread_closes == 4); // completed joins never repeated
    std::puts("plugin lifecycle server: real dispatch, post-reply acknowledgement and legacy/remote rejection passed");
    std::puts("plugin stop: wait/close failures retain ownership and partial joins resume without double close");
}
