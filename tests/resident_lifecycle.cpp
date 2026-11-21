// Run the actual resident entry point with SDK declarations and fake Horizon
// calls. The library, publication, web server and instance lock remain real.
#include <cstdint>
typedef uint32_t _LOCK_T;
typedef struct { uint32_t lock, counter; } _LOCK_RECURSIVE_T;
#define main moader_resident_main
#include "../native/service.cpp"
#undef main
#include "../native/foreground.hpp"
#include "moader/runtime_wire.h"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <future>
#include <chrono>

namespace {
constexpr Result Failure = 0x5010;
struct Fake {
    PluginServiceHandler handler = nullptr;
    PluginLifecycleHandler lifecycle = nullptr;
    void* context = nullptr;
    void* lifecycle_context = nullptr;
    bool ready = false, auto_web = false, poll_error = false;
    bool reload = false, abort_reload = false, stop_failure = false;
    Result fs_result = 0, socket_result = 0, signal_result = 0, start_result = 0, nifm_result = 0;
    unsigned fs_init = 0, fs_exit = 0, mounts = 0, unmounts = 0;
    unsigned socket_init = 0, resident_socket_init = 0, socket_exit = 0, nifm_init = 0, nifm_exit = 0;
    unsigned starts = 0, stops = 0, runtime_exit = 0, kicks = 0, sleeps = 0;
} fake;
Result call(u32 id, const void* input = nullptr, size_t size = 0, void* output = nullptr) {
    size_t actual = 0;
    return fake.handler(id, input, size, output, &actual, fake.context);
}
KaihenPluginLifecycleReply lifecycle(u32 command, u64 operation = 0, Result expected = 0) {
    KaihenPluginLifecycleRequest request{command, 0, operation};
    KaihenPluginLifecycleReply reply{};
    assert(fake.lifecycle(&request, &reply, fake.lifecycle_context) == expected);
    return reply;
}
void boot_gate() {
    moader::ipc::SummaryReply summary{};
    assert(call(moader::ipc::Summary, nullptr, 0, &summary) == MAKERESULT(Module_Libnx, LibnxError_NotInitialized));
    // Repeated early toggles cannot override the saved auto-start decision.
    const u32 off = 0;
    for (unsigned i = 0; i < 32; ++i)
        assert(R_FAILED(call(moader::ipc::WebEnabled, &off, sizeof(off))));
    char error[64]{};
    assert(R_SUCCEEDED(call(moader::ipc::LastError, nullptr, 0, error)));
    assert(std::string(error).find("starting") != std::string::npos);
}
void ready_state() {
    moader::ipc::SummaryReply summary{};
    assert(R_SUCCEEDED(call(moader::ipc::Summary, nullptr, 0, &summary)));
    assert(summary.version == moader::ipc::Version);
    assert(!(summary.flags & moader::ipc::Foreground));
    assert(bool(summary.flags & moader::ipc::WebRunning) == fake.auto_web);
    assert(!(summary.flags & moader::ipc::BackendConnected));
    if (fake.poll_error) {
        char error[64]{};
        assert(R_SUCCEEDED(call(moader::ipc::LastError, nullptr, 0, error)));
        assert(std::string(error).find("transient poll fixture") != std::string::npos);
    }
    char pairing[64]{};
    assert(R_SUCCEEDED(call(moader::ipc::PairingCode, nullptr, 0, pairing)));
    assert(std::string(pairing) == "001234");
    for (u32 enabled : {1u, 0u, 1u, 0u}) {
        assert(R_SUCCEEDED(call(moader::ipc::WebEnabled, &enabled, sizeof(enabled))));
        assert(R_SUCCEEDED(call(moader::ipc::Summary, nullptr, 0, &summary)));
        assert(bool(summary.flags & moader::ipc::WebRunning) == bool(enabled));
    }
    // A zero RNG fixture still produces the required nonzero service epoch.
    std::ifstream file("sdmc:/moader/runtime/catalogue.bin", std::ios::binary);
    MoaderCatalogueHeader header{};
    assert(file.read(reinterpret_cast<char*>(&header), sizeof(header)));
    assert(header.epoch_low == 1 && header.epoch_high == 0);
    // GUI/browser operations do not release the lifetime lock.
    try { moader_native::InstanceLock second; assert(false); }
    catch (const std::runtime_error&) {}
}
unsigned unused_port() {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0); assert(fd >= 0);
    sockaddr_in address{}; address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(!::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)));
    socklen_t size = sizeof(address);
    assert(!getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size));
    const unsigned port = ntohs(address.sin_port); close(fd); return port;
}
void fixture(const std::string& root) {
    std::filesystem::create_directory(root);
    std::filesystem::current_path(root);
    std::filesystem::create_directories("sdmc:/moader");
    moader::atomic_write("sdmc:/moader/pairing-code", "001234");
    moader::atomic_write("sdmc:/moader/preferences", std::string("1 1 0 ") +
        (fake.auto_web ? "1 " : "0 ") + std::to_string(unused_port()) + " 1");
}
}

extern "C" Result pluginRuntimeInitializeWithService(Handle* ready, Handle* control) { *ready = 10; *control = 11; return 0; }
extern "C" void pluginRuntimeExit(void) { ++fake.runtime_exit; }
extern "C" Result pluginServiceStart(PluginService* service, Handle control, PluginServiceHandler handler, void* context) {
    assert(control == 11); ++fake.starts; fake.handler = handler; fake.context = context;
    service->started = R_SUCCEEDED(fake.start_result); return fake.start_result;
}
extern "C" Result pluginServiceStartWithLifecycle(PluginService* service, Handle control,
    PluginServiceHandler handler, void* context, PluginLifecycleHandler lifecycle, void* lifecycle_context) {
    fake.lifecycle = lifecycle; fake.lifecycle_context = lifecycle_context;
    KaihenPluginLifecycleRequest query{KaihenPluginLifecycle_Query, 0, 0};
    KaihenPluginLifecycleReply reply{};
    assert(R_SUCCEEDED(lifecycle(&query, &reply, lifecycle_context)));
    assert(reply.state == KaihenPluginLifecycleState_Starting && !(reply.flags & KAIHEN_PLUGIN_LIFECYCLE_HEALTHY));
    return pluginServiceStart(service, control, handler, context);
}
extern "C" Result pluginServiceStop(PluginService* service) {
    assert(service->started);
    moader::ipc::SummaryReply summary{};
    assert(R_FAILED(call(moader::ipc::Summary, nullptr, 0, &summary)));
    ++fake.stops;
    if (fake.stop_failure) return Failure; // Unjoined worker retains endpoint ownership.
    service->started = false; return 0;
}
extern "C" Result svcSignalEvent(Handle handle) {
    assert(handle == 10 && !fake.fs_init && !fake.socket_init);
    boot_gate(); fake.ready = true; return fake.signal_result;
}
extern "C" Result fsInitialize(void) { assert(fake.ready); ++fake.fs_init; return fake.fs_result; }
extern "C" void fsExit(void) { ++fake.fs_exit; }
extern "C" Result fsdevMountSdmc(void) { ++fake.mounts; return 0; }
extern "C" Result fsdevUnmountAll(void) { ++fake.unmounts; return 0; }
extern "C" const SocketInitConfig* socketGetDefaultInitConfig(void) {
    static const SocketInitConfig config = {
        0x8000, 0x10000, 0x8000, 0x10000, 0x2400, 0xA500, 2, 3, BsdServiceType_User
    };
    return &config;
}
extern "C" Result socketInitialize(const SocketInitConfig* config) {
    ++fake.socket_init;
    if (config) {
        ++fake.resident_socket_init;
        const auto& defaults = *socketGetDefaultInitConfig();
        assert(config->num_bsd_sessions == 1);
        assert(config->bsd_service_type == defaults.bsd_service_type);
        assert(config->tcp_tx_buf_size == defaults.tcp_tx_buf_size);
        assert(config->tcp_rx_buf_size == defaults.tcp_rx_buf_size);
        assert(config->tcp_tx_buf_max_size == defaults.tcp_tx_buf_max_size);
        assert(config->tcp_rx_buf_max_size == defaults.tcp_rx_buf_max_size);
        assert(config->udp_tx_buf_size == defaults.udp_tx_buf_size);
        assert(config->udp_rx_buf_size == defaults.udp_rx_buf_size);
        assert(config->sb_efficiency == defaults.sb_efficiency);
    }
    return fake.socket_result;
}
extern "C" void socketExit(void) { ++fake.socket_exit; }
extern "C" Result nifmInitialize(NifmServiceType) { ++fake.nifm_init; return fake.nifm_result; }
extern "C" void nifmExit(void) { ++fake.nifm_exit; }
extern "C" Result nifmGetCurrentIpAddress(u32* ip) { *ip = htonl(INADDR_LOOPBACK); return 0; }
extern "C" void randomGet(void* bytes, size_t size) { std::memset(bytes, 0, size); }
extern "C" Result fsOpenDataStorageByCurrentProcess(FsStorage*) {
    if (++fake.kicks == 2 && fake.poll_error) throw std::runtime_error("transient poll fixture");
    if (fake.reload) {
        std::ifstream catalogue("sdmc:/moader/runtime/catalogue.bin", std::ios::binary);
        MoaderCatalogueHeader header{};
        assert(catalogue.read(reinterpret_cast<char*>(&header), sizeof(header)));
        MoaderRuntimeAck ack{};
        std::memcpy(ack.magic, MOADER_ACK_MAGIC, 8); ack.version = MOADER_WIRE_VERSION;
        ack.flags = 1; ack.epoch_low = header.epoch_low; ack.epoch_high = header.epoch_high;
        ack.generation = header.generation;
        ack.crc = moader_crc32(&ack, offsetof(MoaderRuntimeAck, crc));
        std::fstream out("sdmc:/moader/runtime/ack.bin", std::ios::binary | std::ios::in | std::ios::out);
        assert(out.write(reinterpret_cast<const char*>(&ack), sizeof(ack)));
    }
    return Failure; // No RomFS is normal for this independent plugin process.
}
extern "C" void fsStorageClose(FsStorage*) { assert(false); }
extern "C" void svcSleepThread(s64 ns) {
    assert(ns == 1000000000); ++fake.sleeps;
    if (fake.reload) {
        if (fake.stop_failure && fake.sleeps == 3) {
            auto& endpoint = *static_cast<moader_native::ResidentControl*>(fake.lifecycle_context);
            assert(endpoint.state == KaihenPluginLifecycleState_Failed && !endpoint.exit_requested);
            assert(endpoint.context && endpoint.context->library.quiescing());
            assert(fake.stops == 1 && fake.runtime_exit == 0 && fake.fs_exit == 0 && fake.socket_exit == 0);
            try { moader_native::InstanceLock duplicate; assert(false); }
            catch (const std::runtime_error&) {}
            char error[64]{};
            assert(R_SUCCEEDED(call(moader::ipc::LastError, nullptr, 0, error)));
            assert(std::string(error).find("ownership retained") != std::string::npos);
            // This child exits atomically, without pretending a failed join
            // released live stack/Context resources through normal teardown.
            std::_Exit(0);
        }
        if (fake.sleeps == 1 || (fake.abort_reload && fake.sleeps == 3)) {
            const auto health = lifecycle(KaihenPluginLifecycle_Health);
            assert(health.state == KaihenPluginLifecycleState_Running);
            assert(health.flags & KAIHEN_PLUGIN_LIFECYCLE_HEALTHY);
            const auto prepared = lifecycle(KaihenPluginLifecycle_Prepare, fake.sleeps == 1 ? 42 : 43);
            assert(prepared.state == KaihenPluginLifecycleState_Draining);
            assert(!(prepared.flags & KAIHEN_PLUGIN_LIFECYCLE_HEALTHY));
            const u32 enabled = 1;
            assert(call(moader::ipc::WebEnabled, &enabled, sizeof(enabled)) == moader_native::ReloadBusy);
            lifecycle(KaihenPluginLifecycle_Prepare, fake.sleeps == 1 ? 42 : 43); // Duplicate reservation.
            return;
        }
        const u64 operation = fake.abort_reload && fake.sleeps == 4 ? 43 : 42;
        assert(lifecycle(KaihenPluginLifecycle_Poll, operation).state == KaihenPluginLifecycleState_Quiesced);
        lifecycle(KaihenPluginLifecycle_Commit, operation + 1, moader_native::BadInput);
        if (fake.abort_reload && fake.sleeps == 2) {
            lifecycle(KaihenPluginLifecycle_Abort, operation);
            return;
        }
        assert(lifecycle(KaihenPluginLifecycle_Commit, operation).state == KaihenPluginLifecycleState_Quiesced);
        auto& endpoint = *static_cast<moader_native::ResidentControl*>(fake.lifecycle_context);
        assert(endpoint.commit_pending && !endpoint.exit_requested);
        lifecycle(KaihenPluginLifecycle_Commit, operation); // Idempotent before acknowledgement.
        lifecycle(KaihenPluginLifecycle_CommitAcknowledged, operation);
        assert(endpoint.exit_requested);
        return;
    }
    if (fake.sleeps == 1) { ready_state(); return; }
    assert(fake.kicks >= 3); // Poll retried after the injected failure.
    throw std::runtime_error("bounded host shutdown fixture");
}
// The empty library and idle Store never query games, saves or play history.
extern "C" Result pdmqryInitialize(void) { assert(false); return Failure; }
extern "C" void pdmqryExit(void) { assert(false); }
extern "C" Result pdmqryQueryPlayStatisticsByApplicationId(u64, bool, PdmPlayStatistics*) { assert(false); return Failure; }
extern "C" Result fsOpenSaveDataInfoReader(FsSaveDataInfoReader*, FsSaveDataSpaceId) { assert(false); return Failure; }
extern "C" Result fsSaveDataInfoReaderRead(FsSaveDataInfoReader*, FsSaveDataInfo*, size_t, s64*) { assert(false); return Failure; }
extern "C" void fsSaveDataInfoReaderClose(FsSaveDataInfoReader*) { assert(false); }
extern "C" Result nsInitialize(void) { assert(false); return Failure; }
extern "C" void nsExit(void) { assert(false); }
extern "C" Result nsListApplicationRecord(NsApplicationRecord*, s32, s32, s32*) { assert(false); return Failure; }
extern "C" Result nsIsGameCardInserted(bool*) { assert(false); return Failure; }
extern "C" Result nsListApplicationIdOnGameCard(u64*, s32, s32*) { assert(false); return Failure; }
extern "C" Result nsCountApplicationContentMeta(u64, s32*) { assert(false); return Failure; }
extern "C" Result nsListApplicationContentMetaStatus(u64, s32, NsApplicationContentMetaStatus*, s32, s32*) { assert(false); return Failure; }
extern "C" Result ncmInitialize(void) { assert(false); return Failure; }
extern "C" void ncmExit(void) { assert(false); }
extern "C" Result ncmOpenContentMetaDatabase(NcmContentMetaDatabase*, NcmStorageId) { assert(false); return Failure; }
extern "C" void ncmContentMetaDatabaseClose(NcmContentMetaDatabase*) { assert(false); }
extern "C" Result ncmContentMetaDatabaseGetLatestContentMetaKey(NcmContentMetaDatabase*, NcmContentMetaKey*, u64) { assert(false); return Failure; }
extern "C" Result ncmContentMetaDatabaseGetContentIdByTypeAndIdOffset(NcmContentMetaDatabase*, NcmContentId*, const NcmContentMetaKey*, NcmContentType, u8) { assert(false); return Failure; }
extern "C" Result ncmOpenContentStorage(NcmContentStorage*, NcmStorageId) { assert(false); return Failure; }
extern "C" void ncmContentStorageClose(NcmContentStorage*) { assert(false); }
extern "C" Result ncmContentStorageHas(NcmContentStorage*, bool*, const NcmContentId*) { assert(false); return Failure; }
extern "C" Result nsGetApplicationControlData2(NsApplicationControlSource, u64, NsApplicationControlData*, size_t, u8, u8, u64*, u32*) { assert(false); return Failure; }
extern "C" Result setInitialize(void) { assert(false); return Failure; }
extern "C" void setExit(void) { assert(false); }
extern "C" Result setGetSystemLanguage(u64*) { assert(false); return Failure; }
extern "C" Result setMakeLanguage(u64, SetLanguage*) { assert(false); return Failure; }

int main(int argc, char** argv) {
    assert(argc == 2 || argc == 3);
    if (argc == 3) {
        assert(std::string(argv[2]) == "--stop-failure");
        fake = {}; fake.reload = true; fake.stop_failure = true; fake.auto_web = true;
        fixture(std::filesystem::absolute(argv[1]).string() + "/stop-failure");
        moader_resident_main();
        assert(false); // The owner must stay alive until the child checks it.
    }
    {
        moader_native::ResidentControl endpoint;
        endpoint.operation = 99; endpoint.commit_pending = true;
        endpoint.state = KaihenPluginLifecycleState_Quiesced;
        std::unique_lock<std::mutex> owner(endpoint.mutex);
        auto acknowledged = std::async(std::launch::async, [&] {
            const KaihenPluginLifecycleRequest request{KaihenPluginLifecycle_CommitAcknowledged, 0, 99};
            KaihenPluginLifecycleReply reply{};
            return moader_native::resident_lifecycle(&request, &reply, &endpoint);
        });
        assert(acknowledged.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout);
        owner.unlock();
        assert(acknowledged.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        assert(R_SUCCEEDED(acknowledged.get()) && endpoint.exit_requested);
    }
    const auto root = std::filesystem::absolute(argv[1]).string();
    for (unsigned scenario = 0; scenario < 6; ++scenario) {
        fake = {}; fake.auto_web = scenario == 0; fake.poll_error = scenario < 2;
        if (scenario == 2) fake.fs_result = Failure;
        if (scenario == 3) fake.socket_result = Failure;
        if (scenario == 4) fake.signal_result = Failure;
        if (scenario == 5) fake.start_result = Failure;
        fixture(root + '/' + std::to_string(scenario));
        assert(R_FAILED(moader_resident_main())); // Successful cases use bounded test shutdown.
        assert(fake.starts == 1 && fake.stops == (scenario == 5 ? 0u : 1u));
        assert(fake.runtime_exit == 1);
        assert(fake.fs_exit == (scenario < 4 && scenario != 2 ? 1u : 0u));
        assert(fake.unmounts == fake.mounts);
        assert(fake.socket_exit == (scenario < 2 ? 1u : 0u));
        assert(fake.resident_socket_init == (scenario < 2 || scenario == 3 ? 1u : 0u));
        assert(fake.nifm_exit == fake.nifm_init);
        assert(fake.sleeps == (scenario < 2 ? 2u : 0u));
        moader_native::InstanceLock released;
    }
    for (unsigned scenario = 0; scenario < 2; ++scenario) {
        fake = {}; fake.reload = true; fake.abort_reload = scenario == 1; fake.auto_web = true;
        fixture(root + "/reload-" + std::to_string(scenario));
        const auto preferences = [] {
            std::ifstream file("sdmc:/moader/preferences");
            return std::string((std::istreambuf_iterator<char>(file)), {});
        }();
        assert(R_SUCCEEDED(moader_resident_main()));
        assert(fake.stops == 1 && fake.runtime_exit == 1 && fake.sleeps == (scenario ? 4u : 2u));
        assert(fake.resident_socket_init == 1);
        moader_native::InstanceLock released;
        std::ifstream pairing("sdmc:/moader/pairing-code"), saved("sdmc:/moader/preferences");
        assert(std::string((std::istreambuf_iterator<char>(pairing)), {}) == "001234");
        assert(std::string((std::istreambuf_iterator<char>(saved)), {}) == preferences);
    }
    for (unsigned scenario = 0; scenario < 2; ++scenario) {
        fake = {}; fake.auto_web = scenario == 0;
        if (scenario == 1) fake.nifm_result = Failure;
        fixture(root + "/foreground-" + std::to_string(scenario));
        {
            moader_native::Foreground foreground;
            assert(foreground.context.foreground);
            assert(foreground.context.network_ready == (scenario == 0));
            assert(foreground.web.running() == fake.auto_web);
            assert(moader_native::session_epoch()[0] == 1);
            try { moader_native::Foreground duplicate; assert(false); }
            catch (const std::runtime_error&) {}
            assert(fake.socket_init == 2 && fake.socket_exit == 1);
            assert(fake.resident_socket_init == 0);
            assert(fake.nifm_init == 1 && fake.nifm_exit == 0);
        }
        assert(fake.socket_exit == 2);
        assert(fake.nifm_exit == (scenario == 0 ? 1u : 0u));
        moader_native::InstanceLock released;
    }
    std::cout << "resident lifecycle checks passed (real core/web; fake Horizon transport)\n";
}
