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
    std::string read_error;
    struct Failure {
        uint64_t retry_after;
        std::string detail;
    };
    std::map<uint64_t, Failure> failures;
    static void load(void* ctx) {
        auto& self = *static_cast<CoverLoader*>(ctx);
        try {
            char id[17];
            std::snprintf(id, sizeof(id), "%016llX", (unsigned long long)self.title);
            self.result = read_installed_icon(id).artwork;
        } catch (const std::exception& e) {
            self.result.reset();
            self.read_error = e.what();
        }
        self.ready.store(true, std::memory_order_release);
    }

  public:
    ~CoverLoader() {
        std::string error;
        worker.join(error);
    }
    std::string_view error_for(uint64_t id) const {
        const auto it = failures.find(id);
        return it == failures.end() ? std::string_view{} : std::string_view(it->second.detail);
    }
    void poll(moader_ui::State& s, uint64_t ms) {
        std::string error;
        if (worker.joinable()) {
            if (!ready.load(std::memory_order_acquire) || !worker.join(error))
                return;
            if (result) {
                failures.erase(title);
                for (auto& g : s.games)
                    if (g.id == title) {
                        g.art = std::move(result);
                        break;
                    }
            } else {
                failures[title] = {ms + 60000, std::move(read_error)};
            }
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
            auto it = failures.find(g.id);
            if (it != failures.end() && it->second.retry_after > ms)
                continue;
            title = g.id;
            read_error.clear();
            ready.store(false, std::memory_order_relaxed);
            if (!worker.start(load, this, error)) {
                read_error = error;
                failures[title] = {ms + 60000, error};
                ready = true;
            }
            break;
        }
    }
};
} // namespace moader_native
