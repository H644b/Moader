#pragma once
#include "control.hpp"
#include "instance.hpp"
#include "game_names.hpp"
#include "game_availability.hpp"
#include "moader/runtime_backend.hpp"
#include "web_assets.hpp"

namespace moader_native {
class SocketSession {
public:
    SocketSession() { if (R_FAILED(socketInitializeDefault())) throw std::runtime_error("Network service initialization failed."); }
    ~SocketSession() { socketExit(); }
    SocketSession(const SocketSession&) = delete;
    SocketSession& operator=(const SocketSession&) = delete;
};
inline std::array<uint64_t,2> session_epoch() {
    std::array<uint64_t,2> epoch{}; randomGet(epoch.data(),sizeof(epoch));
    if (!(epoch[0] | epoch[1])) epoch[0] = 1;
    return epoch;
}
struct Foreground {
    SocketSession sockets;
    InstanceLock instance;
    moader::RuntimeBackend backend;
    moader::Library library;
    std::string token;
    moader::WebServer web;
    Context context;
    bool network_ready = false;
    Foreground() : backend("sdmc:/moader",session_epoch(),[] {
            FsStorage storage{};
            if (R_SUCCEEDED(fsOpenDataStorageByCurrentProcess(&storage))) fsStorageClose(&storage);
        }), library("sdmc:/moader",backend,installed_game_name,installed_game_availability), token(pairing_token()),
        web(library,"sdmc:/moader",token,moader_web_assets()), context{library,web,token} {
        context.foreground = true;
        network_ready = R_SUCCEEDED(nifmInitialize(NifmServiceType_User));
        try {
            context.network_ready = network_ready;
            try { library.synchronize(); } catch (const std::exception& e) { context.error = e.what(); }
            const auto preferences = library.preferences();
            if (preferences.web_on_start && !web.start("0.0.0.0",preferences.web_port)) {
                context.error = web.last_error(); context.last_result = BadInput;
            }
        } catch (...) {
            // A throwing constructor does not run ~Foreground. Release the
            // separately acquired nifm reference before member unwinding.
            if (network_ready) nifmExit();
            throw;
        }
    }
    void poll() {
        try { backend.poll(); library.refresh_availability(); }
        catch (const std::exception& e) { context.error = e.what(); context.last_result = BadInput; }
    }
    ~Foreground() { web.stop(); if (network_ready) nifmExit(); }
};
}
