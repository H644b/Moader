#pragma once
#include "ui.hpp"
#include "../source/web_worker.hpp"
#include <atomic>
#include <utility>

namespace moader_native {
using StorePresence = std::vector<std::pair<std::string, moader_ui::Presence>>;
class StoreInventory {
    using Query = void (*)(StorePresence&, const std::atomic<bool>&);
    Query query;
    moader::WebWorker worker;
    StorePresence titles;
    std::atomic<bool> done{false}, cancel{false};
    bool was_open = false;
    unsigned revision = 0;
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
        if (!open || !was_open || store.inventory_revision != revision)
            cancel = true;
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
        titles.clear();
        for (const auto& g : store.games)
            if (g.presence == moader_ui::Presence::Unchecked)
                titles.emplace_back(g.id, moader_ui::Presence::Unknown);
        if (titles.empty())
            return;
        revision = store.inventory_revision;
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
