#pragma once
#include "library.hpp"
#include "store_presence.hpp"
#include <atomic>
#include <memory>

namespace moader {
struct WebAsset { std::string path, type, contents; };
// No listener exists until start() succeeds. The console owns that switch.
class WebServer {
public:
    using GameLookup = std::function<void(StorePresence&)>;
    WebServer(Library& library, std::string root, std::string pairing_token,
              std::vector<WebAsset> assets, std::function<std::string(const std::string&)> artwork = {}, GameLookup games = {});
    ~WebServer();
    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;
    bool start(const std::string& address = "0.0.0.0", uint16_t port = 7712);
    void stop();
    bool running() const;
    uint16_t port() const;
    std::string last_error() const;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
