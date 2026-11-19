#include "catalog_fetch.hpp"
#include <curl/curl.h>
#include <memory>
#include <limits>
#ifdef __SWITCH__
#include <switch.h>
#endif
namespace moader_ui {
namespace {
struct Download {
    size_t limit;
    std::string bytes;
    static size_t write(char* data, size_t size, size_t count, void* context) {
        auto& out = *static_cast<Download*>(context);
        if (!size || count > (out.limit - out.bytes.size()) / size)
            return 0;
        try {
            out.bytes.append(data, size * count);
        } catch (...) {
            return 0;
        }
        return size * count;
    }
};
int progress(void* p, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return static_cast<const std::atomic<bool>*>(p)->load(std::memory_order_relaxed);
}
} // namespace
std::string fetch_catalog_asset(const std::string& url, size_t limit, const std::atomic<bool>& cancel) {
    if (!url.starts_with("https://") || url.size() > 2048 || cancel || !limit)
        return {};
#ifdef __SWITCH__
    if (R_FAILED(socketInitializeDefault()))
        return {};
    struct Sockets {
        ~Sockets() { socketExit(); }
    } sockets;
#endif
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        return {};
    struct CurlContext {
        ~CurlContext() { curl_global_cleanup(); }
    } global;
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), curl_easy_cleanup);
    if (!handle)
        return {};
    CURL* h = handle.get();
    Download out{limit, {}};
    curl_easy_setopt(h, CURLOPT_URL, url.c_str());
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(h, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(h, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(h, CURLOPT_PROTOCOLS, long(CURLPROTO_HTTPS));
    curl_easy_setopt(h, CURLOPT_REDIR_PROTOCOLS, long(CURLPROTO_HTTPS));
#endif
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(h, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(h, CURLOPT_SSLVERSION, long(CURL_SSLVERSION_TLSv1_2));
    curl_easy_setopt(h, CURLOPT_NETRC, long(CURL_NETRC_IGNORED));
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, 9000L);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, 4000L);
    curl_easy_setopt(h, CURLOPT_MAXFILESIZE_LARGE, curl_off_t(limit));
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, Download::write);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(h, CURLOPT_XFERINFODATA, &cancel);
    curl_easy_setopt(h, CURLOPT_USERAGENT, "Moader/0.2.0 catalog");
    if (curl_easy_perform(h) != CURLE_OK)
        return {};
    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    return status == 200 ? std::move(out.bytes) : std::string{};
}
} // namespace moader_ui
