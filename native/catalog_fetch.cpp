#include "catalog_fetch.hpp"
#include "third_party/picojson.h"
#include <curl/curl.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#ifdef __SWITCH__
#include <switch.h>
#elif defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/evp.h>
#endif

namespace moader_ui {
namespace {
using Handle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
struct Network {
    static inline std::mutex mutex;
    static inline unsigned users = 0;
    Network() {
        std::lock_guard lock(mutex);
        if (!users) {
#ifdef __SWITCH__
            if (R_FAILED(socketInitializeDefault()))
                throw std::runtime_error("Network service is unavailable.");
#endif
            if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
#ifdef __SWITCH__
                socketExit();
#endif
                throw std::runtime_error("Could not initialize the download client.");
            }
        }
        ++users;
    }
    ~Network() {
        std::lock_guard lock(mutex);
        if (--users)
            return;
        curl_global_cleanup();
#ifdef __SWITCH__
        socketExit();
#endif
    }
};
Handle handle() {
    Handle h(curl_easy_init(), curl_easy_cleanup);
    if (!h)
        throw std::runtime_error("Could not start the download client.");
    curl_easy_setopt(h.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h.get(), CURLOPT_NETRC, long(CURL_NETRC_IGNORED));
    return h;
}
int progress(void* p, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return static_cast<const std::atomic<bool>*>(p)->load(std::memory_order_relaxed);
}
using Write = size_t (*)(char*, size_t, size_t, void*);
void public_get(const std::string& url, size_t limit, const std::atomic<bool>& cancel, Write write, void* out,
                bool package) {
    if (!url.starts_with("https://") || url.size() > 2048 || !limit || cancel)
        throw std::runtime_error("The download could not be started.");
    auto h = handle();
    auto c = h.get();
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(c, CURLOPT_PROTOCOLS, long(CURLPROTO_HTTPS));
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, long(CURLPROTO_HTTPS));
#endif
    const bool hosted = url.starts_with(StoreFeed) || url.starts_with(StoreFiles);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, hosted ? 0L : 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_SSLVERSION, long(CURL_SSLVERSION_TLSv1_2));
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, package ? 1800L : 15L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 40L);
    curl_easy_setopt(c, CURLOPT_MAXFILESIZE_LARGE, curl_off_t(limit));
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &cancel);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "Moader/3.5.0 catalog");
    auto rc = curl_easy_perform(c);
    if (rc != CURLE_OK)
        throw std::runtime_error(std::string("Download failed. ") + curl_easy_strerror(rc));
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    if (status != 200)
        throw std::runtime_error("The publisher did not return a complete download.");
}
struct Buffer {
    size_t limit;
    bool discard_extra;
    std::string bytes;
    static size_t write(char* data, size_t size, size_t count, void* context) noexcept {
        auto& out = *static_cast<Buffer*>(context);
        if (!size || count > SIZE_MAX / size)
            return 0;
        const size_t n = size * count, room = out.limit - out.bytes.size();
        if (n > room && !out.discard_extra)
            return 0;
        try {
            out.bytes.append(data, std::min(n, room));
        } catch (...) {
            return 0;
        }
        return n;
    }
};
class Sha256 {
#ifdef __SWITCH__
    Sha256Context ctx;
#elif defined(__APPLE__)
    CC_SHA256_CTX ctx;
#else
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx{EVP_MD_CTX_new(), EVP_MD_CTX_free};
#endif
  public:
    Sha256() {
#ifdef __SWITCH__
        sha256ContextCreate(&ctx);
#elif defined(__APPLE__)
        CC_SHA256_Init(&ctx);
#else
        if (!ctx || !EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr))
            throw std::runtime_error("SHA-256 is unavailable.");
#endif
    }
    void update(const char* data, size_t n) {
#ifdef __SWITCH__
        sha256ContextUpdate(&ctx, data, n);
#elif defined(__APPLE__)
        CC_SHA256_Update(&ctx, data, static_cast<CC_LONG>(n));
#else
        if (!EVP_DigestUpdate(ctx.get(), data, n))
            throw std::runtime_error("Could not verify download.");
#endif
    }
    std::string finish() {
        unsigned char digest[32];
#ifdef __SWITCH__
        sha256ContextGetHash(&ctx, digest);
#elif defined(__APPLE__)
        CC_SHA256_Final(digest, &ctx);
#else
        if (!EVP_DigestFinal_ex(ctx.get(), digest, nullptr))
            throw std::runtime_error("Could not verify download.");
#endif
        constexpr char hex[] = "0123456789abcdef";
        std::string out;
        for (auto b : digest) {
            out.push_back(hex[b >> 4]);
            out.push_back(hex[b & 15]);
        }
        return out;
    }
};
class LocalImport {
    const std::string base;
    Handle h = handle();
    curl_slist* headers = nullptr;

  public:
    LocalImport(unsigned port, const std::string& code) : base("http://127.0.0.1:" + std::to_string(port)) {
        if (!port || port > 65535 || code.size() != 6 || code.find_first_not_of("0123456789") != code.npos)
            throw std::runtime_error("The library connection is unavailable.");
        headers = curl_slist_append(nullptr, ("Authorization: Bearer " + code).c_str());
        if (!headers)
            throw std::bad_alloc();
        auto all = curl_slist_append(headers, "Expect:");
        if (!all) {
            curl_slist_free_all(headers);
            throw std::bad_alloc();
        }
        headers = all;
        curl_easy_setopt(h.get(), CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(h.get(), CURLOPT_PROXY, "");
        curl_easy_setopt(h.get(), CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(h.get(), CURLOPT_CONNECTTIMEOUT, 4L);
    }
    ~LocalImport() {
        h.reset();
        curl_slist_free_all(headers);
    }
    std::string escape(const std::string& s) {
        std::unique_ptr<char, decltype(&curl_free)> p(curl_easy_escape(h.get(), s.data(), s.size()),
                                                      curl_free);
        if (!p)
            throw std::bad_alloc();
        return p.get();
    }
    std::string request(const char* method, const std::string& path, const std::string& body = {},
                        bool commit = false) {
        auto url = base + path;
        Buffer out{8192, true, {}};
        curl_easy_setopt(h.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(h.get(), CURLOPT_CUSTOMREQUEST, method);
        curl_easy_setopt(h.get(), CURLOPT_POSTFIELDSIZE_LARGE, curl_off_t(body.size()));
        curl_easy_setopt(h.get(), CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(h.get(), CURLOPT_WRITEFUNCTION, Buffer::write);
        curl_easy_setopt(h.get(), CURLOPT_WRITEDATA, &out);
        curl_easy_setopt(h.get(), CURLOPT_TIMEOUT, commit ? 600L : 20L);
        if (curl_easy_perform(h.get()) != CURLE_OK)
            throw std::runtime_error(commit ? "Library reply lost. Check your Library before retrying."
                                            : "The local library connection was interrupted.");
        long status = 0;
        curl_easy_getinfo(h.get(), CURLINFO_RESPONSE_CODE, &status);
        if (status < 200 || status >= 300) {
            picojson::value response;
            if (picojson::parse(response, out.bytes).empty() && response.is<picojson::object>() &&
                response.get("error").is<std::string>())
                throw std::runtime_error(response.get("error").get<std::string>().substr(0, 200));
            throw std::runtime_error("The library could not import the package.");
        }
        return out.bytes;
    }
};
struct Upload {
    LocalImport& local;
    std::string path;
    uint64_t total, offset = 0;
    std::atomic<uint64_t>& received;
    const std::atomic<bool>& cancel;
    Sha256 hash;
    std::string chunk;
    std::exception_ptr failure;
    static constexpr size_t ChunkSize = 256 * 1024;
    void flush() {
        if (chunk.empty())
            return;
        if (cancel)
            throw std::runtime_error("Download cancelled.");
        local.request("PUT", path + "?offset=" + std::to_string(offset), chunk);
        offset += chunk.size();
        chunk.clear();
    }
    static size_t write(char* data, size_t size, size_t count, void* context) noexcept {
        auto& u = *static_cast<Upload*>(context);
        try {
            if (u.cancel || !size || count > (u.total - u.offset - u.chunk.size()) / size)
                throw std::runtime_error("The download exceeds its catalog size or was cancelled.");
            const size_t n = size * count;
            u.hash.update(data, n);
            for (size_t at = 0; at < n;) {
                const auto take = std::min(ChunkSize - u.chunk.size(), n - at);
                u.chunk.append(data + at, take);
                at += take;
                if (u.chunk.size() == ChunkSize)
                    u.flush();
            }
            u.received = u.offset + u.chunk.size();
            return n;
        } catch (...) {
            u.failure = std::current_exception();
            return 0;
        }
    }
};
} // namespace
std::string fetch_catalog_asset(const std::string& url, size_t limit, const std::atomic<bool>& cancel) {
    try {
        Network network;
        Buffer out{limit, false, {}};
        public_get(url, limit, cancel, Buffer::write, &out, false);
        return std::move(out.bytes);
    } catch (const std::exception&) {
        return {};
    }
}
void install_catalog_package(const Release& mod, const std::string& game, unsigned port,
                             const std::string& code, const std::atomic<bool>& cancel,
                             std::atomic<uint64_t>& received, std::atomic<bool>& importing) {
    const auto& p = mod.package;
    if (p.bytes < 22 || p.bytes > StorePackageLimit || p.sha256.size() != 64 || cancel)
        throw std::runtime_error("This release has no verified package.");
    Network network;
    LocalImport local(port, code);
    Upload upload{local, {}, p.bytes, 0, received, cancel, {}, {}, {}};
    upload.chunk.reserve(Upload::ChunkSize);
    const auto reply = local.request(
        "POST", "/api/upload/begin?titleId=" + local.escape(game) + "&id=" + local.escape(mod.id) +
                    "&name=" + local.escape(mod.title) + "&size=" + std::to_string(p.bytes));
    picojson::value result;
    if (!picojson::parse(result, reply).empty() || !result.is<picojson::object>() ||
        !result.get("uploadId").is<std::string>())
        throw std::runtime_error("The library could not start an import.");
    const auto key = result.get("uploadId").get<std::string>();
    if (key.empty() || key.size() > 80 || key.find_first_not_of("0123456789-") != key.npos)
        throw std::runtime_error("The library returned an invalid upload ID.");
    upload.path = "/api/upload/" + key;
    try {
        public_get(p.url, p.bytes, cancel, Upload::write, &upload, true);
        if (upload.offset + upload.chunk.size() != p.bytes || upload.hash.finish() != p.sha256)
            throw std::runtime_error("The package does not match its catalog size or checksum.");
        upload.flush();
        if (cancel)
            throw std::runtime_error("Download cancelled.");
        importing = true;
        local.request("POST", upload.path, {}, true);
    } catch (...) {
        try {
            local.request("DELETE", upload.path);
        } catch (...) {
        }
        if (upload.failure)
            std::rethrow_exception(upload.failure);
        throw;
    }
}
} // namespace moader_ui
