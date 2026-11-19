#pragma once
#include "catalog_fetch.hpp"
#include "store.hpp"
#include "../source/web_worker.hpp"
#include <atomic>
#include <algorithm>
#include "ui.hpp"
#include <deque>
#include <fstream>
#include <iterator>

namespace moader_native {
class StoreClient {
    moader::WebWorker worker;
    std::atomic<bool> done{false}, cancel{false};
    std::deque<std::string> feeds;
    bool configured = false, is_feed = false;
    std::string url, data;
    moader_ui::Art result;
    static void fetch(void* context) {
        auto& c = *static_cast<StoreClient*>(context);
        try {
            auto bytes =
                moader_ui::fetch_catalog_asset(c.url, c.is_feed ? 1024 * 1024 : 2 * 1024 * 1024, c.cancel);
            if (c.is_feed)
                c.data = std::move(bytes);
            else
                c.result = moader_ui::decode_art(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
        } catch (const std::exception&) {
            c.data.clear();
            c.result.reset();
        }
        c.done.store(true, std::memory_order_release);
    }
    void configure() {
        configured = true;
        std::ifstream file("sdmc:/moader/store/sources.txt", std::ios::binary | std::ios::ate);
        if (!file || file.tellg() < 0 || file.tellg() > 32768)
            return;
        file.seekg(0);
        for (std::string line; feeds.size() < 16 && std::getline(file, line);) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.starts_with("https://") && line.size() <= 2048 &&
                std::find(feeds.begin(), feeds.end(), line) == feeds.end())
                feeds.push_back(line);
        }
    }

  public:
    ~StoreClient() {
        cancel = true;
        std::string ignored;
        worker.join(ignored);
    }
    void poll(moader_ui::State& s) {
        const bool open = s.view == moader_ui::View::Store;
        if (!open && !worker.joinable())
            return;
        if (!open)
            cancel.store(true, std::memory_order_relaxed);
        std::string error;
        if (worker.joinable()) {
            if (!done.load(std::memory_order_acquire) || !worker.join(error))
                return;
            if (is_feed) {
                if (cancel)
                    feeds.push_front(url);
                else if (!s.store.merge_catalog(data) && open) {
                    s.message = "A community catalog is unavailable. Other hosts are still available.";
                    s.message_error = true;
                }
            } else
                moader_ui::Store::finish_preview(url, std::move(result));
            data.clear();
            result.reset();
        }
        if (!open)
            return;
        if (!s.store.initialized)
            s.store.start(s.game < s.games.size() ? s.games[s.game].id : 0);
        if (!configured)
            configure();
        is_feed = !feeds.empty();
        if (is_feed) {
            url = std::move(feeds.front());
            feeds.pop_front();
        } else {
            url = moader_ui::Store::next_preview();
            if (url.empty())
                return;
        }
        cancel = false;
        done = false;
        if (!worker.start(fetch, this, error)) {
            if (!is_feed)
                moader_ui::Store::finish_preview(url, {});
            done = true;
            s.message = "Community catalog loading is unavailable.";
            s.message_error = true;
        }
    }
};
} // namespace moader_native
