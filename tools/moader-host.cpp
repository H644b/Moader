// Local development server. Its test backend never changes a Switch.
#include "moader/web.hpp"
#include <csignal>
#include <fstream>
#include <iostream>
#include <thread>

volatile std::sig_atomic_t stopping = 0;
static void stop(int) { stopping = 1; }
struct PreviewBackend final : moader::Backend {
    moader::RuntimeStatus status() const override {
        return {false,0,"host-preview","Local preview with a test library. No Switch runtime is connected."};
    }
    void publish(uint64_t,const std::vector<moader::Mod>&) override {}
};
int main(int argc, char** argv) {
    if (argc != 5) { std::cerr << "Usage: moader-host ROOT WEB_DIRECTORY PORT TEST_TOKEN\n"; return 1; }
    try {
        PreviewBackend backend; moader::Library library(argv[1],backend);
        std::vector<moader::WebAsset> assets;
        for (const auto& info : std::vector<std::pair<std::string,std::string>>{{"index.html","text/html; charset=utf-8"},{"style.css","text/css; charset=utf-8"},{"app.js","application/javascript; charset=utf-8"},{"icon.svg","image/svg+xml"}}) {
            std::ifstream file(std::string(argv[2]) + '/' + info.first,std::ios::binary);
            if (!file) throw std::runtime_error("Missing web asset: " + info.first);
            std::string bytes((std::istreambuf_iterator<char>(file)),{});
            assets.push_back({info.first == "index.html" ? "/" : '/' + info.first,info.second,std::move(bytes)});
        }
        moader::WebServer server(library,argv[1],argv[4],std::move(assets));
        if (!server.start("127.0.0.1",std::stoi(argv[3]))) throw std::runtime_error(server.last_error());
        std::signal(SIGINT,stop); std::signal(SIGTERM,stop);
        std::cout << "Moader local preview: http://127.0.0.1:" << server.port() << std::endl;
        while (!stopping) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        server.stop();
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
