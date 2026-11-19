// Temporary integration-test driver. It always binds loopback.
#include "moader/web.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
int main(int argc, char** argv) {
    if (argc != 4) return 1;
    moader::UnavailableBackend backend;
    moader::Library library(argv[1], backend);
    auto cover = [path = std::string(argv[3])](const std::string& title) {
        if (title != "0400f43008c44001") return std::string{};
        std::ifstream fixture(path, std::ios::binary);
        if (!fixture) throw std::runtime_error("Fixture icon unavailable");
        return std::string(std::istreambuf_iterator<char>(fixture), {});
    };
    moader::WebServer server(library, argv[1], argv[2], {{"/", "text/plain", "Moader HTTP integration test"}}, cover);
    if (server.running() || server.port() || server.start("not-an-ip", 0) || server.last_error().empty()) return 2;
    if (!server.start("127.0.0.1", 0) || !server.last_error().empty()) return 2;
    moader::WebServer collision(library, argv[1], argv[2], {});
    if (collision.start("127.0.0.1", server.port()) || collision.running() || collision.last_error().empty()) return 4;
    const auto original_port = server.port();
    if (!server.start("127.0.0.1", 0) || server.port() != original_port) return 5;
    std::cout << server.port() << std::endl;
    std::string line;
    while (std::getline(std::cin, line) && line == "restart") {
        server.stop(); server.stop();
        if (server.running() || server.port()) return 6;
        if (!collision.start("127.0.0.1", original_port) || !collision.last_error().empty()) return 7;
        collision.stop();
        if (!server.start("127.0.0.1", original_port)) return 8;
        std::cout << server.port() << std::endl;
    }
    server.stop();
    return server.running() || server.port() ? 3 : 0;
}
