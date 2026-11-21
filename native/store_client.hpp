#pragma once
#include "catalog_fetch.hpp"
#include "store.hpp"
#include "../source/web_worker.hpp"
#include <atomic>
#include "ui.hpp"

namespace moader_native {
class StoreClient {
    moader::WebWorker worker;
    std::atomic<bool> done{false}, cancel{false};
    std::string feed;
    bool was_open = false, is_feed = false, received_catalog = false;
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
  public:
    ~StoreClient() {
        cancel = true;
        std::string ignored;
        worker.join(ignored);
    }
    void poll(moader_ui::State& s) {
        const bool open = s.view == moader_ui::View::Store;
        if (open && !was_open) {
            s.store.start();
            feed = moader_ui::StoreFeed;
            received_catalog = false;
        } else if (!open && was_open) {
            cancel.store(true, std::memory_order_relaxed);
        }
        was_open = open;
        if (!open && !worker.joinable())
            return;
        std::string error;
        if (worker.joinable()) {
            if (!done.load(std::memory_order_acquire) || !worker.join(error))
                return;
            if (open && !cancel.load(std::memory_order_relaxed)) {
                if (!is_feed)
                    moader_ui::Store::finish_preview(url, std::move(result));
                else if (s.store.merge_catalog(data, true)) {
                    received_catalog = true;
                    if (!s.store.next_page.empty()) {
                        if (s.store.next_page > url) feed = s.store.next_page;
                        else s.store.catalog_failed = true;
                    }
                } else
                    s.store.catalog_failed = true;
            }
            data.clear();
            result.reset();
        }
        if (!open)
            return;
        is_feed = !feed.empty();
        if (is_feed) {
            url.swap(feed);
            feed.clear();
        } else {
            s.store.catalog_status = received_catalog ? moader_ui::CatalogStatus::Ready
                                                      : moader_ui::CatalogStatus::Unavailable;
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
