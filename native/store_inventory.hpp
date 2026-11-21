#pragma once
#include "ui.hpp"
#include "../source/web_worker.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <utility>

namespace moader_native {
using moader::StorePresence;
class StoreInventory {
    using Query = void (*)(StorePresence&, const std::atomic<bool>&);
    Query query;
    moader::WebWorker worker;
    StorePresence titles;
    std::atomic<bool> done{false}, cancel{false};
    bool was_open = false;
    unsigned revision = 0;
    std::chrono::steady_clock::time_point next_check{};
    static void read(void* ctx) {
        auto& task = *static_cast<StoreInventory*>(ctx);
        try {
            task.query(task.titles, task.cancel);
        } catch (const std::exception&) {
        }
        task.done.store(true, std::memory_order_release);
    }

  public:
    explicit StoreInventory(Query query) : query(query) {}
    ~StoreInventory() {
        cancel = true;
        std::string ignored;
        worker.join(ignored);
    }
    void poll(moader_ui::State& s) {
        auto& store = s.store;
        const bool open = s.view == moader_ui::View::Store && store.owned;
        if (!open || !was_open || store.inventory_revision != revision) {
            cancel = true;
            next_check = {};
        }
        was_open = open;
        std::string error;
        if (worker.joinable()) {
            if (!done.load(std::memory_order_acquire) || !worker.join(error))
                return;
            if (open && !cancel) {
                for (const auto& [id, value] : titles)
                    for (auto& g : store.games)
                        if (g.id == id)
                            g.presence = value;
                store.rebuild_results();
            }
        }
        if (!open)
            return;
        const auto now = std::chrono::steady_clock::now();
        const bool refresh = now >= next_check;
        titles.clear();
        bool library_match = false;
        for (auto& g : store.games) {
            if (!refresh && g.presence != moader_ui::Presence::Unchecked) continue;
            const auto id = std::strtoull(g.id.c_str(), nullptr, 16);
            if (std::any_of(s.mods.begin(), s.mods.end(), [&](const auto& m) { return m.data.title_id == id; })) {
                if (g.presence != moader_ui::Presence::Present) {
                    g.presence = moader_ui::Presence::Present;
                    library_match = true;
                }
            } else {
                titles.emplace_back(g.id, moader_ui::Presence::Unknown);
            }
        }
        if (library_match) store.rebuild_results();
        revision = store.inventory_revision;
        if (refresh) next_check = now + std::chrono::seconds(30);
        if (titles.empty())
            return;
        done = false;
        cancel = false;
        if (!worker.start(read, this, error)) {
            done = true;
            for (auto& g : store.games)
                if (g.presence == moader_ui::Presence::Unchecked)
                    g.presence = moader_ui::Presence::Unknown;
            store.rebuild_results();
        }
    }
};
} // namespace moader_native
