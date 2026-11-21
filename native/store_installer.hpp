#pragma once
#include "catalog_fetch.hpp"
#include "../source/web_worker.hpp"

namespace moader_native {
class StoreInstaller {
    moader::WebWorker worker;
    moader_ui::Release mod;
    std::string title, code, error;
    unsigned port = 0;
    std::atomic<bool> cancel{false}, importing{false}, done{false};
    std::atomic<uint64_t> received{0};
    static void run(void* ctx) {
        auto& t = *static_cast<StoreInstaller*>(ctx);
        try {
            moader_ui::install_catalog_package(t.mod, t.title, t.port, t.code, t.cancel, t.received,
                                               t.importing);
        } catch (const std::exception& e) {
            t.error = e.what();
        }
        t.done.store(true, std::memory_order_release);
    }

  public:
    bool temporary_listener = false;
    bool active() const { return worker.joinable(); }
    ~StoreInstaller() { stop(); }
    void stop() {
        cancel = true;
        std::string ignored;
        worker.join(ignored);
        code.clear();
    }
    bool start(moader_ui::StoreTransfer& t, const std::string& game, const moader_ui::Release& release,
               unsigned listener_port, const std::string& pairing) {
        if (active())
            return false;
        mod = release;
        title = game;
        port = listener_port;
        code = pairing;
        cancel = false;
        importing = false;
        done = false;
        received = 0;
        error.clear();
        t = {moader_ui::InstallPhase::Downloading, game, mod.id, {}, 0, mod.package.bytes};
        if (worker.start(run, this, error))
            return true;
        t.phase = moader_ui::InstallPhase::Failed;
        t.message = error;
        done = true;
        code.clear();
        return false;
    }
    bool poll(moader_ui::StoreTransfer& t) {
        if (!active())
            return false;
        t.received = received.load();
        if (importing.load())
            t.phase = moader_ui::InstallPhase::Installing;
        std::string issue;
        if (!done.load(std::memory_order_acquire) || !worker.join(issue))
            return false;
        t.received = received.load();
        t.phase = error.empty() ? moader_ui::InstallPhase::Installed : moader_ui::InstallPhase::Failed;
        t.message = error;
        code.clear();
        return true;
    }
};
} // namespace moader_native
