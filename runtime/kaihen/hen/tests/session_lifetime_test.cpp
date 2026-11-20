// SPDX-License-Identifier: GPL-2.0-only
// Actual plugin runtime and SM proxy loops, real SDK clone/close/IPC packing,
// counted fake kernel. FS's quota/peer teardown are a MODEL, not device proof.
#include <array>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "../source/sm_proxy.cpp"
#include "../../libkaihen/source/plugin_runtime.cpp"
struct LifetimeClock {
    static u64 now() { return 0; }
    static u64 toNs(u64 value) { return value * 32; }
    static u64 toTicks(u64 value) { return value / 32 + (value % 32 != 0); }
};
#define KAIHEN_GAME_ARM_TEST_CLOCK LifetimeClock
#include "../source/game_arm.cpp"
#undef KAIHEN_GAME_ARM_TEST_CLOCK

namespace kernel {
using Wire = std::array<u8, 0x100>;
thread_local Wire tls;
thread_local unsigned realm = 1;
enum class Kind { Session, Event, Port, Master, Fs, Other, Process, Reader, Monitor };
struct Request { Wire input{}, output{}; unsigned caller = 0; bool replied = false; Result result = 0; };
struct Object {
    Kind kind;
    unsigned owner = 0, clients = 0, servers = 0;
    bool closed = false, signalled = false;
    std::vector<std::shared_ptr<Request>> requests;
    std::shared_ptr<Request> current;
    std::vector<Handle> accepts;
};
struct Entry { std::shared_ptr<Object> object; unsigned owner; bool server; };
struct Task { ThreadFunc entry; void* arg; unsigned owner; bool started = false; std::thread thread; };
struct SmState { Service service{}; unsigned refs = 0; std::map<u64, Handle> overrides; };
std::mutex lock;
std::condition_variable changed;
std::map<Handle, Entry> handles;
std::map<Thread*, std::shared_ptr<Task>> tasks;
std::map<Mutex*, std::unique_ptr<std::mutex>> mutexes;
std::map<unsigned, SmState> sm;
Handle next_handle = 10, migration_port = 0;
unsigned fs_live = 0, fs_peak = 0;
constexpr unsigned FsBudget = 16;
unsigned fail_wait = 0, fail_close = 0, fail_start = 0, wait_calls = 0, close_calls = 0;
constexpr Result WaitFailure = MAKERESULT(345, 221), CloseFailure = MAKERESULT(345, 222), StartFailure = MAKERESULT(345, 223);
unsigned abandon_lookup = 0;
thread_local std::shared_ptr<Request> active_request;
constexpr u64 MemoryBase = 0x200000, Domain = MemoryBase + 0x80, Table = MemoryBase + 0x1000;
constexpr u64 DonorObject = MemoryBase + 0x4100, StockObject = MemoryBase + 0x4200, OtherObject = MemoryBase + 0x4300;
std::array<u8, 0x8000> memory;
Service monitor_service{};
bool full_key_r = false;
bool full_key_stale = false;
u64 full_key_samples = 0;
u64 application_pid = 0;
u64 application_program = 0;
bool policy_read_ready = false;
unsigned policy_writes = 0;
template <typename T> void put(u64 address, const T& value) {
    assert(address >= MemoryBase && address + sizeof(T) <= MemoryBase + memory.size());
    std::memcpy(memory.data() + address - MemoryBase, &value, sizeof(value));
}

Handle add(const std::shared_ptr<Object>& object, unsigned owner, bool server = false) {
    const Handle handle = next_handle++;
    handles.emplace(handle, Entry{object, owner, server});
    if (server) ++object->servers; else ++object->clients;
    return handle;
}
void close(Handle handle) {
    auto it = handles.find(handle);
    assert(it != handles.end());
    auto entry = it->second;
    handles.erase(it);
    if (entry.server) --entry.object->servers; else --entry.object->clients;
    if (entry.object->kind == Kind::Fs && !entry.object->clients && !entry.object->closed) {
        entry.object->closed = true; --fs_live;
    }
    changed.notify_all();
}
void processExit(unsigned owner) {
    std::vector<Handle> dying;
    for (const auto& [handle, entry] : handles) if (entry.owner == owner) dying.push_back(handle);
    for (Handle handle : dying) close(handle);
}
Handle service(Kind kind, unsigned owner) {
    if (kind == Kind::Fs) {
        if (fs_live == FsBudget) return 0;
        fs_peak = std::max(fs_peak, ++fs_live);
    }
    auto object = std::make_shared<Object>(); object->kind = kind; object->owner = owner;
    return add(object, owner);
}
std::pair<Handle, Handle> session(unsigned server_owner, unsigned client_owner) {
    auto object = std::make_shared<Object>(); object->kind = Kind::Session; object->owner = server_owner;
    return {add(object, server_owner, true), add(object, client_owner)};
}
void transfer(Wire& wire, unsigned destination, bool receive) {
    auto message = hipcParseRequest(wire.data());
    for (u32 i = 0; i < message.meta.num_copy_handles; ++i) {
        if (message.data.copy_handles[i] == CUR_PROCESS_HANDLE) {
            auto process = std::make_shared<Object>(); process->kind = Kind::Process; process->owner = realm;
            if (receive) message.data.copy_handles[i] = add(process, destination);
            continue;
        }
        auto found = handles.find(message.data.copy_handles[i]); assert(found != handles.end());
        if (receive) message.data.copy_handles[i] = add(found->second.object, destination, found->second.server);
    }
    for (u32 i = 0; i < message.meta.num_move_handles; ++i) {
        const Handle old = message.data.move_handles[i];
        auto found = handles.find(old); assert(found != handles.end());
        if (receive) message.data.move_handles[i] = add(found->second.object, destination, found->second.server);
        // Reference model, not a native Switch 2 observation:
        // Atmosphere's kern_k_server_session.cpp SendReply/CleanupServerHandles
        // consumes moved handles even when the destination has closed.
        // https://github.com/Atmosphere-NX/Atmosphere/blob/master/libraries/libmesosphere/source/kern_k_server_session.cpp
        close(old);
    }
}
void reply(const std::shared_ptr<Object>& object) {
    auto request = object->current; assert(request);
    request->output = tls;
    bool alive = false;
    for (const auto& [handle, entry] : handles) {
        (void)handle;
        if (entry.object == object && !entry.server && entry.owner == request->caller) alive = true;
    }
    transfer(request->output, request->caller, alive);
    request->result = alive ? 0 : KERNELRESULT(ConnectionClosed);
    request->replied = true;
    object->current.reset();
    changed.notify_all();
}
void boundedWait(std::unique_lock<std::mutex>& held, auto predicate) {
    assert(changed.wait_for(held, std::chrono::seconds(5), predicate));
}
unsigned owned(unsigned owner) {
    std::lock_guard guard(lock); unsigned count = 0;
    for (const auto& [handle, entry] : handles) { (void)handle; count += entry.owner == owner; }
    return count;
}
void finishProxies(SmProxy& proxy) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        assert(!proxy.collectFinishedSessions());
        { std::lock_guard guard(lock); if (tasks.empty()) return; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(false && "proxy threads did not finish");
}
} // namespace kernel

extern "C" {
void* armGetTls() { return kernel::tls.data(); }
void mutexLock(Mutex* mutex) {
    std::mutex* native;
    { std::lock_guard guard(kernel::lock); auto& value = kernel::mutexes[mutex]; if (!value) value = std::make_unique<std::mutex>(); native = value.get(); }
    native->lock();
}
void mutexUnlock(Mutex* mutex) {
    std::mutex* native;
    { std::lock_guard guard(kernel::lock); native = kernel::mutexes.at(mutex).get(); }
    native->unlock();
}
Result svcCloseHandle(Handle handle) {
    std::lock_guard guard(kernel::lock);
    assert(kernel::handles.at(handle).owner == kernel::realm);
    kernel::close(handle); return 0;
}
Result svcCreateSession(Handle* server, Handle* client, u32, u64) {
    std::lock_guard guard(kernel::lock);
    const auto pair = kernel::session(kernel::realm, kernel::realm); *server = pair.first; *client = pair.second; return 0;
}
Result svcCreateEvent(Handle* write, Handle* read) {
    std::lock_guard guard(kernel::lock);
    auto object = std::make_shared<kernel::Object>(); object->kind = kernel::Kind::Event;
    *write = kernel::add(object, kernel::realm); *read = kernel::add(object, kernel::realm); return 0;
}
Result svcSignalEvent(Handle handle) {
    std::lock_guard guard(kernel::lock); kernel::handles.at(handle).object->signalled = true; kernel::changed.notify_all(); return 0;
}
Result svcClearEvent(Handle handle) {
    std::lock_guard guard(kernel::lock); kernel::handles.at(handle).object->signalled = false; return 0;
}
Result svcWaitSynchronization(s32* index, const Handle* handles, s32 count, u64) {
    std::unique_lock held(kernel::lock); assert(count == 1);
    auto object = kernel::handles.at(handles[0]).object;
    kernel::boundedWait(held, [&] { return object->signalled; }); *index = 0; return 0;
}
Result svcAcceptSession(Handle* session, Handle port) {
    std::lock_guard guard(kernel::lock); auto object = kernel::handles.at(port).object;
    assert(!object->accepts.empty()); *session = object->accepts.front(); object->accepts.erase(object->accepts.begin()); return 0;
}
Result svcReplyAndReceive(s32* index, const Handle* list, s32 count, Handle target, u64) {
    using namespace kernel;
    std::unique_lock held(lock);
    if (target) reply(handles.at(target).object);
    if (!count) { *index = -1; return KERNELRESULT(TimedOut); }
    const auto ready = [&]() -> int {
        for (s32 i = 0; i < count; ++i) {
            const auto object = handles.at(list[i]).object;
            if (object->kind == Kind::Port && !object->accepts.empty()) return i;
            if (object->kind == Kind::Session && (!object->requests.empty() || !object->clients)) return i;
        }
        return -1;
    };
    boundedWait(held, [&] { return ready() >= 0; });
    *index = ready(); auto object = handles.at(list[*index]).object;
    if (object->kind == Kind::Port) return 0;
    if (object->requests.empty()) return KERNELRESULT(ConnectionClosed);
    object->current = object->requests.front(); object->requests.erase(object->requests.begin());
    active_request = object->current; tls = object->current->input; return 0;
}
Result svcSendSyncRequest(Handle handle) {
    using namespace kernel;
    std::unique_lock held(lock);
    auto found = handles.find(handle); if (found == handles.end()) return KERNELRESULT(ConnectionClosed);
    assert(found->second.owner == realm && !found->second.server);
    auto object = found->second.object;
    auto message = hipcParseRequest(tls.data());
    if (object->kind == Kind::Session) {
        if (!object->servers) return KERNELRESULT(ConnectionClosed);
        auto request = std::make_shared<kernel::Request>(); request->input = tls; request->caller = realm;
        transfer(request->input, object->owner, true);
        object->requests.push_back(request); changed.notify_all();
        boundedWait(held, [&] { return request->replied || !object->servers; });
        if (!request->replied) return KERNELRESULT(ConnectionClosed);
        tls = request->output; return request->result;
    }
    if (message.meta.type == CmifCommandType_Close) {
        if (!object->closed && object->kind == Kind::Fs) --fs_live;
        object->closed = true; return 0;
    }
    assert(!object->closed);
    const auto* header = static_cast<const CmifInHeader*>(cmifGetAlignedDataStart(message.data.data_words, tls.data()));
    assert(header->magic == CMIF_IN_HEADER_MAGIC);
    if (message.meta.type == CmifCommandType_Control) {
        if (header->command_id == 3) { ounce::writePointerBufferSizeReply(tls.data()); return 0; }
        if (header->command_id == 0) { const u32 id = 1; ounce::writeReply(tls.data(), 0, &id, sizeof(id)); return 0; }
        assert(header->command_id == 2 || header->command_id == 4);
        const Handle clone = service(object->kind, realm);
        ounce::sm::writeReply(tls.data(), ounce::sm::Protocol::Cmif,
            clone ? 0 : MAKERESULT(345, 240), clone); return 0;
    }
    if (object->kind == Kind::Monitor) {
        assert(header->command_id == 7); ounce::writeReply(tls.data(), MAKERESULT(15, 1), nullptr, 0); return 0;
    }
    if (object->kind == Kind::Reader) {
        RoProcessMemoryRequest input; std::memcpy(&input, header + 1, sizeof(input));
        assert(input.process_id == 8 && input.address >= MemoryBase && input.address + input.size <= MemoryBase + memory.size());
        if (header->command_id == RoProcessReadCommand)
            std::memcpy(hipcGetBufferAddress(message.data.recv_buffers), memory.data() + input.address - MemoryBase, input.size);
        else {
            assert(header->command_id == RoProcessWriteCommand);
            std::memcpy(memory.data() + input.address - MemoryBase, hipcGetBufferAddress(message.data.send_buffers), input.size);
        }
        ounce::writeReply(tls.data(), 0, nullptr, 0); return 0;
    }
    if (object->kind == Kind::Fs) {
        assert(header->command_id == 10);
        const Handle child = service(Kind::Fs, realm);
        ounce::sm::writeReply(tls.data(), ounce::sm::Protocol::Cmif, child ? 0 : MAKERESULT(345, 240), child); return 0;
    }
    assert(object->kind == Kind::Master && header->command_id == 1);
    SmServiceName name; std::memcpy(&name, header + 1, sizeof(name));
    if (abandon_lookup) { assert(active_request && active_request->caller == abandon_lookup); processExit(abandon_lookup); abandon_lookup = 0; }
    const Handle service_handle = service(!std::strncmp(name.name, "fsp-srv", 8) ? Kind::Fs : Kind::Other, realm);
    ounce::sm::writeReply(tls.data(), ounce::sm::Protocol::Cmif,
        service_handle ? 0 : MAKERESULT(345, 240), service_handle); return 0;
}
Result threadCreate(Thread* thread, ThreadFunc entry, void* arg, void*, size_t, int, int) {
    std::lock_guard guard(kernel::lock);
    auto task = std::make_shared<kernel::Task>(); task->entry = entry; task->arg = arg; task->owner = kernel::realm;
    assert(!kernel::tasks.count(thread)); kernel::tasks[thread] = task; return 0;
}
Result threadStart(Thread* thread) {
    std::shared_ptr<kernel::Task> task;
    { std::lock_guard guard(kernel::lock); if (kernel::fail_start) { --kernel::fail_start; return kernel::StartFailure; } task = kernel::tasks.at(thread); task->started = true; }
    task->thread = std::thread([task] { kernel::realm = task->owner; task->entry(task->arg); }); return 0;
}
Result threadWaitForExit(Thread* thread) {
    std::shared_ptr<kernel::Task> task;
    { std::lock_guard guard(kernel::lock); ++kernel::wait_calls; if (kernel::fail_wait) { --kernel::fail_wait; return kernel::WaitFailure; } task = kernel::tasks.at(thread); assert(task->started); }
    if (task->thread.joinable()) task->thread.join(); return 0;
}
Result threadClose(Thread* thread) {
    std::lock_guard guard(kernel::lock); ++kernel::close_calls;
    if (kernel::fail_close) { --kernel::fail_close; return kernel::CloseFailure; }
    auto task = kernel::tasks.at(thread); assert(!task->thread.joinable()); kernel::tasks.erase(thread); return 0;
}
void smAddOverrideHandle(SmServiceName name, Handle handle) { kernel::sm[kernel::realm].overrides[smServiceNameToU64(name)] = handle; }
Result smInitialize() {
    auto& state = kernel::sm[kernel::realm]; if (state.refs++) return 0;
    const auto found = state.overrides.find(smServiceNameToU64(smEncodeName("sm:")));
    Handle handle;
    if (found != state.overrides.end()) handle = found->second;
    else { std::lock_guard guard(kernel::lock); handle = kernel::service(kernel::Kind::Other, kernel::realm); }
    serviceCreate(&state.service, handle); state.service.own_handle = found == state.overrides.end(); return 0;
}
void smExit() { auto& state = kernel::sm[kernel::realm]; assert(state.refs); if (!--state.refs) serviceClose(&state.service); }
Result smRegisterServiceInSpace(Handle* out, u32 space, SmServiceName, bool, s32) {
    std::lock_guard guard(kernel::lock); assert(space == 1 && !kernel::migration_port);
    auto object = std::make_shared<kernel::Object>(); object->kind = kernel::Kind::Port; object->owner = kernel::realm;
    *out = kernel::migration_port = kernel::add(object, kernel::realm); kernel::changed.notify_all(); return 0;
}
Result smUnregisterServiceInSpace(u32 space, SmServiceName) { std::lock_guard guard(kernel::lock); assert(space == 1); kernel::migration_port = 0; return 0; }
Result svcGetProcessId(u64* pid, Handle handle) { std::lock_guard guard(kernel::lock); auto object = kernel::handles.at(handle).object; assert(object->kind == kernel::Kind::Process); *pid = object->owner; return 0; }
Result fsStorageRead(FsStorage*, s64, void* output, u64 size) { std::memset(output, 0, size); return 0; }
void fsFsClose(FsFileSystem* fs) { serviceClose(&fs->s); }
Result svcCancelSynchronization(Handle) { return 0; }
void svcSleepThread(s64) { std::this_thread::yield(); }
Service* pmdmntGetServiceSession() { return &kernel::monitor_service; }
Result pmdmntGetProgramId(u64* program, u64 pid) {
    if (pid == kernel::application_pid) {
        *program = kernel::application_program;
        return 0;
    }
    Handle ignored[8]{};
    return serviceDispatchInOut(&kernel::monitor_service, 7, pid, *program,
                                .out_handles = ignored);
}
Result pmdmntGetProcessId(u64*, u64) { return MAKERESULT(15, 1); }
Result pmdmntGetApplicationProcessId(u64* pid) {
    if (!kernel::application_pid) return MAKERESULT(15, 1);
    *pid = kernel::application_pid;
    return 0;
}
Result hidInitialize() { return 0; }
void hidExit() {}
Result hidActivateNpadWithRevision(u32) { return 0; }
Result hidSetSupportedNpadStyleSet(u32) { return 0; }
Result hidSetSupportedNpadIdType(const HidNpadIdType*, size_t) { return 0; }
size_t hidGetNpadStatesSystemExt(HidNpadIdType, HidNpadSystemExtState*, size_t) { return 0; }
size_t hidGetNpadStatesFullKey(HidNpadIdType id, HidNpadFullKeyState* states, size_t count) {
    if (!kernel::full_key_r || id != HidNpadIdType_No1 || count != 1) return 0;
    *states = {};
    states->sampling_number = kernel::full_key_stale ? 1 : ++kernel::full_key_samples;
    states->attributes = HidNpadAttribute_IsConnected;
    if (kernel::full_key_stale || kernel::full_key_samples >= 2)
        states->buttons = HidNpadButton_R;
    return 1;
}
size_t hidGetNpadStatesHandheld(HidNpadIdType, HidNpadHandheldState*, size_t) { return 0; }
size_t hidGetNpadStatesJoyDual(HidNpadIdType, HidNpadJoyDualState*, size_t) { return 0; }
size_t hidGetNpadStatesJoyRight(HidNpadIdType, HidNpadJoyRightState*, size_t) { return 0; }
}
Result readSavedPolicy(Handle, const char*, SignedPolicy*) { return MAKERESULT(2, 1); }
Result readSignedPolicy(Handle, u64, u8, u8, SignedPolicy* out) {
    if (!kernel::policy_read_ready) return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    out->size = 1;
    out->bytes[0] = 0x42;
    return 0;
}
Result writeExeFsPackage(Handle, const char*, const char*, const SignedPolicy& policy, u8*) {
    assert(policy.size == 1 && policy.bytes[0] == 0x42);
    ++kernel::policy_writes;
    return 0;
}
Result saveSignedPolicy(Handle, const char*, const SignedPolicy& policy) {
    assert(policy.size == 1 && policy.bytes[0] == 0x42);
    return 0;
}

struct GameArmLifetimeTest {
    static void run(Handle root) {
        using namespace kernel;
        realm = 1; memory.fill(0);
        const unsigned baseline = fs_live;
        GameArm game;
        assert(!cmifCloneCurrentObject(root, &game.filesystem_root_));
        {
            std::lock_guard guard(lock);
            game.reader_session_ = service(Kind::Reader, 1);
            game.filesystem_process_ = service(Kind::Process, 1);
            handles.at(game.filesystem_process_).object->owner = 8;
            game.memory_config_.storage = service(Kind::Other, 1);
            game.monitor_ = service(Kind::Monitor, 1);
        }
        serviceCreate(pmdmntGetServiceSession(), game.monitor_);
        game.memory_.emplace(game.memory_config_, game.reader_session_, game.filesystem_process_);
        assert(!game.memory_->begin());
        game.mount_config_.domain = Domain; game.mount_config_.table = Table; game.mount_config_.root_id = 1;
        game.current_mount_ = game.mount_config_;
        put<u64>(Table + 0x20, DonorObject); put<u64>(Table + 0x28, 0x5678);
        put<u64>(DonorObject, 0xa550); put<u64>(StockObject, 0xa550);
        game.package_.emplace(*game.memory_, game.filesystem_root_);
        assert(!game.package_->open("@Sdcard:/fixture.nsp", game.current_mount_));
        game.mount_.emplace(*game.memory_, game.current_mount_);
        game.code_ = {Table + 0x60, StockObject, 0x5678};
        put<u64>(game.code_.address + 0x20, StockObject); put<u64>(game.code_.address + 0x28, 0x5678);
        assert(!game.exchangeGameCode()); // actual exchange and installed-identity tracking
        assert(game.code_.object == game.current_mount_.donor_object);
        put<u64>(Domain + 0x20, game.code_.address); put<u64>(game.code_.address, Domain + 0x20);
        put<u64>(game.code_.address + 0x18, Domain);
        game.gate_.emplace(*game.memory_, game.gate_config_);
        game.state_ = GameArm::State::WaitExit; game.started_ = true; game.status_.process_id = 800;
        assert(fs_live == baseline + 2); // GameArm root + active package session
        game.step(); // PID gone, but the actual installed CodeFS still exists
        assert(game.package_ && game.state_ == GameArm::State::WaitExit);
        PluginLaunchContext lease{};
        assert(game.acquireReloadLease(lease));
        put<u64>(game.code_.address + 0x20, OtherObject); // same slot reused for another object
        game.step(); // reload lease keeps the original tracking/resources pinned
        assert(game.package_ && fs_live == baseline + 2);
        game.releaseReloadLease(); game.step(); // production host-exit path
        assert(!game.package_ && game.state_ == GameArm::State::OpenPackage && game.status_.serial == 1);
        assert(fs_live == baseline + 1); // package session really closed in the counted kernel
        game.step(); assert(game.state_ == GameArm::State::WaitForInput);
        ounce::DomainEntry recycled[] = {{0x100, 0x200, 1}};
        assert(!containsEntry(recycled, 1, {0x100, 0x300, 1}));
        full_key_r = true;
        full_key_samples = 0;
        assert(game.sampleR() == GameArm::InputChoice::Pressed);
        assert(game.trace().point == GameTracePoint_InputSample);
        assert(game.trace().value == 0x201); // FullKey, player one
        full_key_stale = true;
        full_key_samples = 0;
        assert(game.sampleR() == GameArm::InputChoice::Unknown);
        full_key_stale = false;
        full_key_r = false;
        // First-launch capture must wait through a still-closing Seedsow
        // process, then bind the native game when PM reports it.
        game.state_ = GameArm::State::WaitProcess;
        game.capture_pending_ = true;
        game.active_title_ = 0;
        game.status_.phase = HenGamePhase_WaitingForProcess;
        application_pid = 700;
        application_program = 0x0100c9100b06a000ull; // Seedsow Demo
        game.step();
        assert(game.state_ == GameArm::State::WaitProcess);
        assert(game.active_title_ == 0 && game.status_.process_id == 0);
        assert(game.status_.result == 0);
        application_program = 0x0400000000010000ull;
        game.step();
        assert(game.state_ == GameArm::State::CapturePolicy);
        assert(game.active_title_ == application_program);
        assert(game.status_.process_id == application_pid);
        // A first native launch without an observed R still captures policy.
        // It must ignore the Switch 1 entrypoint and leave that game stock.
        game.state_ = GameArm::State::WaitForInput;
        game.capture_pending_ = false;
        game.capture_probe_next_tick_ = 0;
        game.active_title_ = 0;
        game.status_.process_id = 0;
        full_key_r = false;
        application_program = 0x0100c9100b06a000ull;
        game.step();
        assert(game.state_ == GameArm::State::WaitForInput);
        assert(game.status_.process_id == 0);
        application_program = 0x0400000000010000ull;
        game.capture_probe_next_tick_ = 0;
        game.step();
        assert(game.state_ == GameArm::State::CapturePolicy);
        assert(game.trace().point == GameTracePoint_AutomaticPolicyCapture);
        assert(game.status_.process_id == application_pid);
        policy_read_ready = true;
        const unsigned previous_writes = policy_writes;
        game.step();
        assert(game.policy_ready_ && policy_writes == previous_writes + 1);
        assert(game.status_.phase == HenGamePhase_WaitingForExit);
        assert(game.trace().point == GameTracePoint_PolicyCaptured);
        policy_read_ready = false;
        application_pid = 0;
        application_program = 0;
    }
};

namespace {
using namespace kernel;
struct Resident { unsigned pid; Handle control; std::array<Service, 3> fs; };
Handle root, master;
SmProxy proxy;
Handle cloneRoot() { Handle handle = 0; assert(!cmifCloneCurrentObject(root, &handle)); return handle; }
void openFsPool(std::array<Service, 3>& pool, Handle borrowed) {
    serviceCreate(&pool[0], borrowed); pool[0].own_handle = false;
    assert(!serviceConvertToDomain(&pool[0]));
    for (unsigned i = 1; i != pool.size(); ++i) assert(!serviceClone(&pool[0], &pool[i]));
}
Resident start(unsigned pid, Handle fs, Handle sm_client) {
    realm = 1; Resident resident{pid, 0, {}};
    std::thread process([&] {
        realm = pid; sm[pid] = {};
        Handle ready = 0, control = 0;
        assert(!pluginRuntimeInitializeWithService(&ready, &control));
        assert(!svcSignalEvent(ready));
    });
    Handle startup;
    {
        std::unique_lock held(lock); boundedWait(held, [] { return migration_port != 0; });
        auto port = handles.at(migration_port).object;
        auto pair = session(pid, 1); startup = pair.second; port->accepts.push_back(pair.first); changed.notify_all();
    }
    Service service_object = {}; serviceCreate(&service_object, startup);
    Handle identity[8]{};
    assert(!serviceDispatch(&service_object, PluginStartupCommand_GetIdentity,
        .out_handle_attrs = {SfOutHandleAttr_HipcCopy}, .out_handles = identity));
    u64 peer_pid = 0; assert(!svcGetProcessId(&peer_pid, identity[0]) && peer_pid == pid); svcCloseHandle(identity[0]);
    Handle release = 0, ready = 0;
    assert(!pluginAdoptHandles(&service_object, sm_client, fs, &release, &ready, &resident.control));
    assert(!svcSignalEvent(release));
    assert(!svcWaitSynchronizationSingle(ready, 5000000000ull));
    svcCloseHandle(release); svcCloseHandle(ready); svcCloseHandle(startup);
    process.join();
    // Exact bootstrap/reload ownership: handoff copies, then HEN closes its
    // local references. PluginRuntimeExit owns the received copies.
    svcCloseHandle(fs); svcCloseHandle(sm_client);
    realm = pid; openFsPool(resident.fs, g_received.fs_root);
    realm = 1; return resident;
}
void stop(Resident& resident) {
    realm = resident.pid;
    for (auto& service : resident.fs) serviceClose(&service);
    pluginRuntimeExit(); // actual production closeReceivedHandles path
    assert(!owned(resident.pid));
    realm = 1; svcCloseHandle(resident.control);
}
void lookup(Handle sm_client, const char* name, unsigned pid) {
    realm = pid;
    Service sm_service = {}; serviceCreate(&sm_service, sm_client);
    Handle output[8] = {};
    const auto encoded = smEncodeName(name);
    assert(!serviceDispatchIn(&sm_service, 1, encoded,
        .out_handle_attrs = {SfOutHandleAttr_HipcMove}, .out_handles = output));
    Service service_object = {}; serviceCreate(&service_object, output[0]); serviceClose(&service_object);
    realm = 1;
}
void waitForEnded() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        // Worker close of the server proves serve() passed its final use;
        // collection's real threadWaitForExit then proves SDK thread exit.
        { std::lock_guard guard(lock); bool servers = false; for (const auto& [h, entry] : handles) { (void)h; servers |= entry.server && entry.object->kind == Kind::Session; } if (!servers) return; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(false);
}
}

int main() {
    using namespace kernel;
    { std::lock_guard guard(lock); root = service(Kind::Fs, 1); master = service(Kind::Master, 1); }
    proxy.setMaster(master);
    Handle sm_client = 0; assert(!proxy.openSession(&sm_client));
    auto resident = start(212, cloneRoot(), sm_client);
    const unsigned steady_fs = fs_live;
    assert(steady_fs == 5); // retained HEN root + raw override + three SDK sessions
    for (unsigned cycle = 0; cycle != 20; ++cycle) {
        // hbmenu/NRO has its own borrowed root and SDK pool.
        realm = 1; Handle host_root = cloneRoot(), host_sm = 0;
        assert(!proxy.openSession(&host_sm));
        { std::lock_guard guard(lock); handles.at(host_root).owner = 1000 + cycle; handles.at(host_sm).owner = 1000 + cycle; }
        realm = 1000 + cycle; std::array<Service, 3> host_pool{}; openFsPool(host_pool, host_root); realm = 1;
        lookup(host_sm, "fsp-srv", 1000 + cycle);
        for (unsigned reload = 0; reload != 2; ++reload) {
            const Handle staged_root = cloneRoot(); Handle staged_sm = 0;
            assert(!proxy.openSession(&staged_sm)); // preflight before stopping old
            stop(resident);
            resident = start(213 + cycle * 2 + reload, staged_root, staged_sm);
            assert(fs_live == steady_fs + 4);
        }
        realm = 1000 + cycle;
        for (auto& service_object : host_pool) serviceClose(&service_object);
        svcCloseHandle(host_root); svcCloseHandle(host_sm);
        assert(!owned(1000 + cycle)); realm = 1;
        // Normal next game's independent service connection, without HEN/R.
        Handle game_root;
        { std::lock_guard guard(lock); game_root = service(Kind::Fs, 3000 + cycle); }
        realm = 3000 + cycle; Service game{}; serviceCreate(&game, game_root);
        assert(!serviceConvertToDomain(&game)); serviceClose(&game); realm = 1;
        assert(fs_live == steady_fs && fs_peak <= FsBudget);
    }
    stop(resident); finishProxies(proxy); assert(fs_live == 1);
    GameArmLifetimeTest::run(root); assert(fs_live == 1);
    // HOME/process exit while a service lookup is outstanding: execute the
    // real proxy reply path and kernel-model move cleanup, not just encoding.
    assert(!proxy.openSession(&sm_client));
    { std::lock_guard guard(lock); handles.at(sm_client).owner = 9000; abandon_lookup = 9000; }
    realm = 9000;
    Service dying_sm = {}; serviceCreate(&dying_sm, sm_client);
    const auto fs_name = smEncodeName("fsp-srv"); Handle abandoned_output[8] = {};
    assert(serviceDispatchIn(&dying_sm, 1, fs_name,
        .out_handle_attrs = {SfOutHandleAttr_HipcMove}, .out_handles = abandoned_output) == KERNELRESULT(ConnectionClosed));
    assert(!owned(9000)); realm = 1; finishProxies(proxy); assert(fs_live == 1);
    // Wait/close failures retain the real proxy context; no new allocation
    // follows a failed reap, and a partial completed wait is not repeated.
    assert(!proxy.openSession(&sm_client)); svcCloseHandle(sm_client); waitForEnded();
    fail_wait = 1; const unsigned before_wait = wait_calls;
    Handle rejected = INVALID_HANDLE;
    assert(proxy.openSession(&rejected) == WaitFailure && tasks.size() == 1 && rejected == INVALID_HANDLE);
    fail_close = 1;
    assert(proxy.collectFinishedSessions() == CloseFailure && tasks.size() == 1);
    const unsigned joined_waits = wait_calls;
    assert(joined_waits == before_wait + 2);
    assert(!proxy.collectFinishedSessions() && tasks.empty() && wait_calls == joined_waits);
    fail_start = fail_close = 1;
    assert(proxy.openSession(&sm_client) == StartFailure && tasks.size() == 1);
    assert(!proxy.collectFinishedSessions() && tasks.empty()); // unstarted: no wait
    svcCloseHandle(root); svcCloseHandle(master);
    assert(!fs_live && handles.empty());
    std::puts("actual plugin runtime/SM proxy: 40 reloads, host exits, next-game service admission, bounded clone/handle counts and retained failed joins passed");
}
