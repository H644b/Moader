#include "moader/web.hpp"
#include "web_worker.hpp"
#ifdef __SWITCH__
#include <switch.h>
#endif
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace moader {
namespace {
constexpr size_t MaxBody = 1 << 20, MaxHeaders = 16384;
struct Request { std::string method, path, body; std::map<std::string, std::string> headers, query; };
struct Reply { int code = 200; std::string type = "application/json", body; };
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::string lower(std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A'; return s; }
uint64_t number(const std::string& value) {
    require(!value.empty() && value.size() < 20, "Invalid numeric field.");
    for (char c : value) require(c >= '0' && c <= '9', "Invalid numeric field.");
    return std::stoull(value);
}
std::string decode(const std::string& text) {
    auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = text[i];
        if (c == '%') { require(i + 2 < text.size(), "Invalid URL escape.");
            int a = digit(text[i + 1]), b = digit(text[i + 2]); require(a >= 0 && b >= 0, "Invalid URL escape."); c = a * 16 + b; i += 2; }
        else if (c == '+') c = ' ';
        require(c >= 32 && c != 127, "URL contains control characters."); out += c;
    }
    return out;
}
bool equal_token(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char difference = 0;
    for (size_t i = 0; i < a.size(); ++i) difference |= static_cast<unsigned char>(a[i] ^ b[i]);
    return difference == 0;
}
class PairingLimiter {
    struct Attempts { unsigned failed; std::chrono::steady_clock::time_point expires; };
    std::map<uint32_t, Attempts> peers;
public:
    // A fixed window begins with the first failure. Successful authentication
    // clears partial failures, but cannot bypass an already locked peer.
    int authenticate(uint32_t peer, bool valid, std::chrono::steady_clock::time_point now) {
        for (auto it = peers.begin(); it != peers.end();) {
            if (now >= it->second.expires) it = peers.erase(it); else ++it;
        }
        auto attempts = peers.find(peer);
        if (attempts != peers.end() && attempts->second.failed >= 5) return 429;
        if (valid) {
            if (attempts != peers.end()) peers.erase(attempts);
            return 200;
        }
        // Do not evict a locked peer when the bounded table is full.
        if (attempts == peers.end()) {
            if (peers.size() >= 64) return 429;
            attempts = peers.emplace(peer, Attempts{0, now + std::chrono::seconds(30)}).first;
        }
        ++attempts->second.failed;
        return 401;
    }
    void clear() { peers.clear(); }
};
Reply app_screenshot(const std::string& root) {
    constexpr size_t MaxScreenshot = 4u << 20;
    const auto path = root + "/app-screenshot.bmp";
    struct stat before{};
    if (lstat(path.c_str(), &before)) {
        if (errno == ENOENT) return {404, "application/json", "{\"error\":\"No app screenshot yet. Press Minus in Moader on your Switch.\"}"};
        throw std::runtime_error("Cannot inspect the app screenshot.");
    }
    require(S_ISREG(before.st_mode) && before.st_nlink == 1, "App screenshot must be a regular file without links.");
    require(before.st_size > 0 && static_cast<uint64_t>(before.st_size) <= MaxScreenshot, "App screenshot must be at most 4 MiB.");
    int flags = O_RDONLY | O_NONBLOCK;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int fd = open(path.c_str(), flags);
    require(fd >= 0, "Cannot open the app screenshot.");
    std::string bytes;
    try {
        struct stat opened{};
        require(!fstat(fd, &opened) && S_ISREG(opened.st_mode) && opened.st_nlink == 1 &&
                opened.st_dev == before.st_dev && opened.st_ino == before.st_ino && opened.st_size == before.st_size,
                "App screenshot changed. Capture it again.");
        char buffer[8192];
        while (true) {
            const auto n = read(fd, buffer, sizeof(buffer));
            if (n < 0 && errno == EINTR) continue;
            require(n >= 0, "Cannot read the app screenshot.");
            if (!n) break;
            require(bytes.size() + static_cast<size_t>(n) <= MaxScreenshot, "App screenshot exceeds 4 MiB.");
            bytes.append(buffer, n);
        }
        require(bytes.size() == static_cast<uint64_t>(before.st_size), "App screenshot changed. Capture it again.");
    } catch (...) { close(fd); throw; }
    close(fd);
    return {200, "image/bmp", std::move(bytes)};
}
Request receive(int fd) {
    std::string bytes; char buffer[8192]; size_t end;
    while ((end = bytes.find("\r\n\r\n")) == std::string::npos) {
        const auto n = recv(fd, buffer, sizeof(buffer), 0); require(n > 0, "HTTP headers were not received.");
        bytes.append(buffer, n); require(bytes.size() <= MaxHeaders + sizeof(buffer), "HTTP headers exceed 16 KiB.");
    }
    require(end <= MaxHeaders, "HTTP headers exceed 16 KiB.");
    Request req;
    std::istringstream lines(bytes.substr(0, end + 2)); std::string line, target, version, extra;
    require(bool(std::getline(lines, line)) && !line.empty() && line.back() == '\r', "Invalid HTTP request line.");
    line.pop_back(); std::istringstream first(line);
    require(bool(first >> req.method >> target >> version) && !(first >> extra) && version == "HTTP/1.1", "Use HTTP/1.1.");
    require(target.size() <= 2048 && !target.empty() && target[0] == '/', "Invalid request target.");
    while (std::getline(lines, line)) {
        require(!line.empty() && line.back() == '\r', "HTTP headers must use CRLF line endings.");
        line.pop_back();
        const auto colon = line.find(':'); require(colon != std::string::npos && colon > 0 && colon < 80, "Invalid HTTP header.");
        std::string key = lower(line.substr(0, colon)), value = line.substr(colon + 1);
        for (char c : key) require((c >= 'a' && c <= 'z') || c == '-', "Invalid HTTP header name.");
        auto begin = value.find_first_not_of(" \t"), finish = value.find_last_not_of(" \t");
        value = begin == std::string::npos ? "" : value.substr(begin, finish - begin + 1);
        for (unsigned char c : value) require(c >= 32 && c != 127, "Invalid HTTP header value.");
        require(req.headers.emplace(key, value).second && req.headers.size() <= 48, "Duplicate or excessive HTTP headers.");
    }
    require(req.headers.count("host") && !req.headers["host"].empty(), "HTTP Host is required.");
    require(!req.headers.count("transfer-encoding") && !req.headers.count("expect"), "Chunked requests and Expect are unsupported.");
    const size_t length = req.headers.count("content-length") ? number(req.headers["content-length"]) : 0;
    require(length <= MaxBody, "Use upload chunks of at most 1 MiB.");
    req.body = bytes.substr(end + 4); require(req.body.size() <= length, "Unexpected data after the HTTP body.");
    while (req.body.size() < length) {
        const auto n = recv(fd, buffer, std::min(sizeof(buffer), length - req.body.size()), 0);
        require(n > 0, "HTTP body was not fully received."); req.body.append(buffer, n);
    }
    const auto question = target.find('?'); req.path = target.substr(0, question);
    if (question != std::string::npos) {
        size_t start = question + 1;
        while (start < target.size()) {
            auto amp = target.find('&', start), stop = amp == std::string::npos ? target.size() : amp;
            const auto equals = target.find('=', start);
            require(equals != std::string::npos && equals < stop, "Query parameter needs a value.");
            require(req.query.emplace(decode(target.substr(start, equals - start)), decode(target.substr(equals + 1, stop - equals - 1))).second,
                "Duplicate query parameter."); start = stop + 1;
        }
    }
    return req;
}
void send_reply(int fd, const Reply& reply) {
    const char* reason = reply.code == 200 ? "OK" : reply.code == 201 ? "Created" : reply.code == 202 ? "Accepted" :
        reply.code == 401 ? "Unauthorized" : reply.code == 403 ? "Forbidden" : reply.code == 404 ? "Not Found" : reply.code == 429 ? "Too Many Requests" : "Bad Request";
    const std::string head = "HTTP/1.1 " + std::to_string(reply.code) + ' ' + reason + "\r\nContent-Type: " + reply.type +
        "\r\nContent-Length: " + std::to_string(reply.body.size()) +
        "\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
        "Content-Security-Policy: default-src 'self'; img-src 'self' blob:; object-src 'none'; frame-ancestors 'none'; base-uri 'none'\r\n\r\n";
    const auto bytes = head + reply.body;
    size_t at = 0;
    while (at < bytes.size()) {
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        const auto n = send(fd, bytes.data() + at, bytes.size() - at, flags);
        if (n <= 0) break;
        at += n;
    }
}
}
class WebServer::Impl {
public:
    Library& library;
    std::string root, token, error;
    std::vector<WebAsset> assets;
    std::function<std::string(const std::string&)> artwork;
    struct Upload { std::string file, title, id, name; uint64_t size = 0, received = 0; };
    std::map<std::string, Upload> uploads;
    PairingLimiter pairing_attempts;
    std::atomic<bool> enabled{false};
    std::mutex client_mutex;
    int client = -1;
    int listener = -1; uint16_t actual_port = 0; unsigned next_upload = 0;
    WebWorker worker;
    Impl(Library& lib, std::string r, std::string t, std::vector<WebAsset> a, std::function<std::string(const std::string&)> art) : library(lib), root(std::move(r)), token(std::move(t)), assets(std::move(a)), artwork(std::move(art)) {
        require(token.size() == 6 && token.find_first_not_of("0123456789") == std::string::npos,
                "Pairing code must contain six digits.");
        make_directories(root + "/.uploads");
    }
    Reply route(const Request& req, uint32_t peer) {
        if (req.path.compare(0, 5, "/api/") != 0) {
            if (req.method == "GET") for (const auto& asset : assets) if (asset.path == req.path) return {200, asset.type, asset.contents};
            return {404, "application/json", "{\"error\":\"Page not found.\"}"};
        }
        const auto auth = req.headers.find("authorization");
        const auto authenticated = pairing_attempts.authenticate(peer,
            auth != req.headers.end() && equal_token(auth->second, "Bearer " + token), std::chrono::steady_clock::now());
        if (authenticated == 429)
            return {429, "application/json", "{\"error\":\"Too many pairing attempts. Wait 30 seconds, then use the six-digit code on your Switch.\"}"};
        if (authenticated == 401)
            return {401, "application/json", "{\"error\":\"Enter the six-digit pairing code shown on your Switch.\"}"};
        const auto origin = req.headers.find("origin");
        if (origin != req.headers.end() && origin->second != "http://" + req.headers.at("host"))
            return {403, "application/json", "{\"error\":\"Open Moader at the address shown on your Switch.\"}"};
        if (req.method == "GET" && req.path == "/api/app-screenshot") {
            require(req.query.empty(), "App screenshot does not accept path or query parameters.");
            return app_screenshot(root);
        }
        if (req.method == "GET" && req.path == "/api/state") return {200, "application/json", library.snapshot_json()};
        if (req.method == "POST" && req.path == "/api/library/rescan") {
            require(library.rescan(), "An import or another library operation is in progress. Try again when it finishes.");
            return {200, "application/json", library.snapshot_json()};
        }
        if (req.method == "GET" && req.path == "/api/events") return {200, "application/json", library.events_json()};
        if (req.method == "POST" && req.path == "/api/game-source") {
            require(req.query.count("titleId") && req.query.count("source") &&
                    req.query.size() == 2 + req.query.count("generation"), "Choose a game and its copy type.");
            const auto& source = req.query.at("source");
            require(source == "unspecified" || source == "cartridge" || source == "digital", "Choose a valid game-copy setting.");
            const auto expected = req.query.count("generation") ? std::optional<uint64_t>(number(req.query.at("generation"))) : std::nullopt;
            library.set_game_source(req.query.at("titleId"), source == "cartridge" ? GameSource::Cartridge :
                source == "digital" ? GameSource::Digital : GameSource::Unspecified, expected);
            return {200, "application/json", library.snapshot_json()};
        }
        if (req.method == "GET" && req.path == "/api/game-art") {
            require(req.query.size()==1 && req.query.count("titleId") && valid_title_id(req.query.at("titleId")), "Choose one valid game Title ID.");
            auto title=lower(req.query.at("titleId"));
            const auto snapshot=library.snapshot();
            if(artwork && std::any_of(snapshot.mods.begin(),snapshot.mods.end(),[&](const Mod& m){return lower(m.title_id)==title;})){
                try{
                    auto jpeg=artwork(title);
                    if(!jpeg.empty() && jpeg.size()<=0x20000)return {200,"image/jpeg",std::move(jpeg)};
                }catch(const std::exception&){}
            }
            return {404,"application/json",R"({"error":"Game artwork unavailable."})"};
        }
        if (req.method == "GET" && req.path == "/api/game-name") {
            require(req.query.size() == 1 && req.query.count("titleId") && valid_title_id(req.query.at("titleId")),
                    "Choose one valid game Title ID.");
            const auto title = lower(req.query.at("titleId"));
            const auto name = library.game_name(title);
            return {200, "application/json", "{\"titleId\":" + json_string(title) + ",\"name\":" +
                (name.empty() ? "null" : json_string(name)) + '}'};
        }
        if (req.path == "/api/settings" && req.method == "GET")
            return {200, "application/json", preferences_json(library.preferences())};
        if (req.path == "/api/settings" && req.method == "POST") {
            require(!req.query.empty(), "Choose a setting to save.");
            auto preferences = library.preferences();
            uint32_t mask = 0;
            for (const auto& setting : req.query) {
                if (setting.first == "webPort") {
                    const auto port = number(setting.second);
                    require(port >= 1024 && port <= 65535 && port != 7713,
                        "Choose a web port from 1024 to 65535, except reserved port 7713.");
                    preferences.web_port = static_cast<uint16_t>(port);
                    mask |= 8;
                    continue;
                }
                require(setting.second == "0" || setting.second == "1", "Use 0 or 1 for a setting.");
                const bool enabled = setting.second == "1";
                if (setting.first == "gameNames") { preferences.game_names = enabled; mask |= 1; }
                else if (setting.first == "reducedMotion") { preferences.reduced_motion = enabled; mask |= 2; }
                else if (setting.first == "webOnStart") { preferences.web_on_start = enabled; mask |= 4; }
                else if (setting.first == "followLog") { preferences.follow_log = enabled; mask |= 16; }
                else throw std::runtime_error("Unknown setting.");
            }
            library.update_preferences(preferences, mask);
            return {200, "application/json", preferences_json(library.preferences())};
        }
        if (req.method == "POST" && req.path == "/api/mod/enable") {
            const auto enabled_value = req.query.at("enabled"); require(enabled_value == "0" || enabled_value == "1", "enabled must be 0 or 1.");
            const std::optional<uint64_t> expected = req.query.count("generation") ? std::optional<uint64_t>(number(req.query.at("generation"))) : std::nullopt;
            const auto mod = library.set_enabled(req.query.at("titleId"), req.query.at("id"), enabled_value == "1", expected);
            return {mod.applied == ApplyState::Pending ? 202 : 200, "application/json", library.snapshot_json()};
        }
        if (req.method == "POST" && req.path == "/api/mod/remove") {
            const std::optional<uint64_t> expected = req.query.count("generation") ? std::optional<uint64_t>(number(req.query.at("generation"))) : std::nullopt;
            library.remove(req.query.at("titleId"), req.query.at("id"), expected); return {200, "application/json", library.snapshot_json()};
        }
        if (req.method == "POST" && req.path == "/api/upload/begin") {
            require(uploads.size() < 2, "Finish or cancel an existing upload first.");
            const auto title = lower(req.query.at("titleId")), id = req.query.at("id"), name = req.query.at("name");
            const auto size = number(req.query.at("size"));
            require(valid_title_id(title) && valid_mod_id(id), "Invalid game Title ID or mod ID.");
            require(!name.empty() && name.size() <= 120 && size >= 22 && size <= (128u << 20), "ZIP must be at most 128 MiB and have a name.");
            const auto key = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + '-' + std::to_string(++next_upload);
            const auto file = root + "/.uploads/" + key + ".zip";
            FILE* fp = std::fopen(file.c_str(), "wbx"); require(fp != nullptr, "Cannot create upload on SD.");
            const int closed = std::fclose(fp); require(!closed, "Cannot close upload file.");
            uploads.emplace(key, Upload{file, title, id, name, size, 0});
            return {201, "application/json", "{\"uploadId\":" + json_string(key) + ",\"chunkBytes\":1048576}"};
        }
        if (req.path.compare(0, 12, "/api/upload/") == 0) {
            const auto key = req.path.substr(12); auto found = uploads.find(key);
            require(found != uploads.end(), "Upload session is unavailable. Begin a new upload.");
            auto& upload = found->second;
            if (req.method == "PUT") {
                require(number(req.query.at("offset")) == upload.received, "Upload offset changed; query the session before retrying.");
                require(!req.body.empty() && upload.received + req.body.size() <= upload.size, "Upload exceeds its declared size.");
                FILE* fp = std::fopen(upload.file.c_str(), "ab"); require(fp != nullptr, "Cannot open SD upload.");
                const size_t wrote = std::fwrite(req.body.data(), 1, req.body.size(), fp);
                const int closed = std::fclose(fp);
                if (wrote != req.body.size() || closed) {
                    const auto file = upload.file; uploads.erase(found); unlink(file.c_str());
                    throw std::runtime_error("SD upload write failed. Begin a new upload.");
                }
                upload.received += wrote;
                return {200, "application/json", "{\"received\":" + std::to_string(upload.received) + '}'};
            }
            if (req.method == "GET") return {200, "application/json", "{\"received\":" + std::to_string(upload.received) + ",\"size\":" + std::to_string(upload.size) + '}'};
            if (req.method == "POST") {
                require(upload.received == upload.size, "Upload has not finished.");
                library.import_zip(upload.title, upload.id, upload.file, upload.name);
                unlink(upload.file.c_str()); uploads.erase(found);
                return {201, "application/json", library.snapshot_json()};
            }
            if (req.method == "DELETE") { unlink(upload.file.c_str()); uploads.erase(found); return {200, "application/json", "{\"cancelled\":true}"}; }
        }
        return {404, "application/json", "{\"error\":\"Operation not found.\"}"};
    }
    void loop() {
        while (enabled) {
            pollfd ready{listener, POLLIN, 0};
            if (poll(&ready, 1, 250) <= 0 || !(ready.revents & POLLIN)) continue;
            sockaddr_in peer{}; socklen_t peer_size = sizeof(peer);
            const int fd = accept(listener, reinterpret_cast<sockaddr*>(&peer), &peer_size);
            if (fd < 0) { if (!enabled) break; continue; }
            const int flags = fcntl(fd, F_GETFL, 0);
            if (flags < 0 || fcntl(fd, F_SETFL, flags & ~O_NONBLOCK)) { close(fd); continue; }
            {
                std::lock_guard<std::mutex> lock(client_mutex);
                if (!enabled) { close(fd); break; }
                client = fd;
            }
            timeval timeout{5, 0}; setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
            int one = 1; setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
            try { send_reply(fd, route(receive(fd), peer.sin_addr.s_addr)); }
            catch (const std::exception& e) { send_reply(fd, {400, "application/json", "{\"error\":" + json_string(e.what()) + '}'}); }
            {
                std::lock_guard<std::mutex> lock(client_mutex);
                shutdown(fd, SHUT_RDWR); close(fd); client = -1;
            }
        }
    }
};
WebServer::WebServer(Library& library, std::string root, std::string token, std::vector<WebAsset> assets, std::function<std::string(const std::string&)> artwork)
    : impl_(new Impl(library, std::move(root), std::move(token), std::move(assets), std::move(artwork))) {}
WebServer::~WebServer() { stop(); }
bool WebServer::start(const std::string& address, uint16_t port) {
    if (impl_->enabled) return true;
    if (impl_->worker.joinable()) {
        if (impl_->error.empty()) impl_->error = "Web worker cleanup is pending. Stop it before restarting.";
        return false;
    }
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(port);
    if (inet_pton(AF_INET, address.c_str(), &addr.sin_addr) != 1) { impl_->error = "Choose an IPv4 address."; return false; }
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { impl_->error = "Cannot create the web socket."; return false; }
    int one = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK)) {
        close(fd); impl_->error = "Cannot configure the web listener."; return false;
    }
    timeval timeout{1, 0}; setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) || listen(fd, 4)) {
        close(fd); impl_->error = "Cannot listen on this address and port."; return false;
    }
    socklen_t length = sizeof(addr);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &length)) {
        close(fd); impl_->error = "Cannot read the web listener address."; return false;
    }
    impl_->pairing_attempts.clear();
    impl_->actual_port = ntohs(addr.sin_port); impl_->listener = fd; impl_->enabled = true;
    auto rollback = [&] {
        impl_->enabled = false; close(fd); impl_->listener = -1; impl_->actual_port = 0;
    };
    try {
        if (!impl_->worker.start([](void* context) { static_cast<Impl*>(context)->loop(); }, impl_.get(), impl_->error)) {
            rollback(); return false;
        }
    } catch (const std::exception& e) {
        rollback(); impl_->error = e.what(); return false;
    }
    impl_->error.clear(); return true;
}
void WebServer::stop() {
    impl_->enabled = false;
    if (impl_->listener >= 0) shutdown(impl_->listener, SHUT_RDWR);
    {
        std::lock_guard<std::mutex> lock(impl_->client_mutex);
        if (impl_->client >= 0) shutdown(impl_->client, SHUT_RDWR);
    }
    // A failed wait does not establish worker exit. Retain its listener and
    // state, report the stage/result, and require an explicit successful stop
    // before restart. Never close a descriptor that the worker may still use.
    if (!impl_->worker.join(impl_->error)) throw std::runtime_error(impl_->error);
    if (impl_->listener >= 0) close(impl_->listener);
    impl_->listener = -1; impl_->actual_port = 0;
}
bool WebServer::running() const { return impl_->enabled; }
uint16_t WebServer::port() const { return impl_->actual_port; }
std::string WebServer::last_error() const { return impl_->error; }
}
