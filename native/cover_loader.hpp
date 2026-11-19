#pragma once
#include "game_art.hpp"
#include "ui.hpp"
#include "../source/web_worker.hpp"
#include <atomic>
#include <map>

namespace moader_native {
// Resolve one visible cover at a time away from the input/render thread.
class CoverLoader {
    moader::WebWorker worker;
    std::atomic<bool> ready{false};
    uint64_t title = 0;
    moader_ui::Art result;
    std::map<uint64_t, uint64_t> retry_after;
    static void load(void* ctx) {
        auto& self = *static_cast<CoverLoader*>(ctx);
        try {
            char id[17];
            std::snprintf(id, sizeof(id), "%016llX", (unsigned long long)self.title);
            auto bytes = installed_game_art(id);
            self.result = moader_ui::decode_art(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
        } catch (const std::exception&) {
            self.result.reset();
        }
        self.ready.store(true, std::memory_order_release);
    }

  public:
    ~CoverLoader() {
        std::string error;
        worker.join(error);
    }
    void poll(moader_ui::State& s, uint64_t ms) {
        std::string error;
        if (worker.joinable()) {
            if (!ready.load(std::memory_order_acquire) || !worker.join(error))
                return;
            if (result) {
                for (auto& g : s.games)
                    if (g.id == title) {
                        g.art = std::move(result);
                        break;
                    }
            } else
                retry_after[title] = ms + 60000;
            result.reset();
            ready = false;
        }
        int n = s.games.size();
        if (!n)
            return;
        for (int i = 0; i < n; ++i) {
            const int d = std::abs(i - int(s.game));
            if (std::min(d, n - d) > 3)
                s.games[i].art.reset();
        }
        for (int offset : {0, -1, 1, -2, 2, -3, 3}) {
            auto& g = s.games[(int(s.game) + offset % n + n) % n];
            if (g.art)
                continue;
            auto it = retry_after.find(g.id);
            if (it != retry_after.end() && it->second > ms)
                continue;
            title = g.id;
            ready.store(false, std::memory_order_relaxed);
            if (!worker.start(load, this, error)) {
                ready = true;
                retry_after[title] = ms + 60000;
            }
            break;
        }
    }
};
} // namespace moader_native
