// Actual __SWITCH__ WebWorker/WebServer with SDK calls faked using host threads.
#include <switch.h>
#include "moader/web.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <thread>
#include <utility>

namespace {
struct NativeThread {
    ThreadFunc entry;
    void* argument;
    std::thread running;
    bool started = false, waited = false;
};
struct Fake {
    Result create_error = 0, start_error = 0, wait_error = 0, close_error = 0;
    unsigned creates = 0, starts = 0, waits = 0, closes = 0;
    std::map<Thread*, std::unique_ptr<NativeThread>> threads;
} fake;
std::set<int> descriptors() {
    std::set<int> result;
    for (int fd = 0; fd < 128; ++fd) if (fcntl(fd, F_GETFD) >= 0) result.insert(fd);
    return result;
}
int listener_since(const std::set<int>& before) {
    const auto after = descriptors();
    std::set<int> added;
    for (int fd : after) if (!before.count(fd)) added.insert(fd);
    assert(added.size() == 1); return *added.begin();
}
int connect_to(uint16_t port) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0); assert(fd >= 0);
    sockaddr_in address{}; address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(port);
    assert(!connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)));
    const timeval timeout{2, 0};
    assert(!setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)));
    assert(!setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)));
    return fd;
}
void send_all(int fd, const std::string& bytes) {
    size_t at = 0;
    while (at < bytes.size()) {
        const auto count = send(fd, bytes.data() + at, bytes.size() - at, 0);
        assert(count > 0); at += count;
    }
}
std::string request(uint16_t port, const std::string& method, const std::string& path,
                    const std::string& body = "", unsigned status = 200) {
    const int fd = connect_to(port);
    send_all(fd, method + ' ' + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
        "\r\nAuthorization: Bearer 001234\r\nContent-Length: " + std::to_string(body.size()) +
        "\r\n\r\n" + body);
    std::string result; char buffer[8192];
    for (;;) {
        const auto count = recv(fd, buffer, sizeof(buffer), 0); assert(count >= 0);
        if (!count) break;
        result.append(buffer, count);
    }
    close(fd);
    assert(result.rfind("HTTP/1.1 " + std::to_string(status) + ' ', 0) == 0);
    const auto split = result.find("\r\n\r\n"); assert(split != std::string::npos);
    return result.substr(split + 4);
}
template<typename Operation> void fails(Operation operation, const std::string& text) {
    bool failed = false;
    try { operation(); }
    catch (const std::runtime_error& e) { failed = true; assert(std::string(e.what()).find(text) != std::string::npos); }
    assert(failed);
}
void check_recovery(moader::WebServer& server) {
    assert(server.start("127.0.0.1", 0));
    assert(server.running() && server.port());
    assert(!request(server.port(), "GET", "/api/state").empty());
    server.stop(); server.stop();
    assert(!server.running() && !server.port() && fake.threads.empty());
}
}

extern "C" Result threadCreate(Thread* thread, ThreadFunc entry, void* argument,
                               void* stack, size_t stack_size, int priority, int core) {
    ++fake.creates;
    assert(!stack && stack_size == 0x40000 && priority == 0x2c && core == -2);
    assert(!fake.threads.count(thread));
    if (auto error = std::exchange(fake.create_error, 0)) return error;
    auto native = std::make_unique<NativeThread>(); native->entry = entry; native->argument = argument;
    fake.threads.emplace(thread, std::move(native)); thread->handle = fake.creates;
    return 0;
}
extern "C" Result threadStart(Thread* thread) {
    ++fake.starts;
    auto& native = *fake.threads.at(thread); assert(!native.started);
    if (auto error = std::exchange(fake.start_error, 0)) return error;
    native.running = std::thread(native.entry, native.argument); native.started = true; return 0;
}
extern "C" Result threadWaitForExit(Thread* thread) {
    ++fake.waits;
    auto& native = *fake.threads.at(thread); assert(native.started && !native.waited);
    if (auto error = std::exchange(fake.wait_error, 0)) return error;
    native.running.join(); native.waited = true; return 0;
}
extern "C" Result threadClose(Thread* thread) {
    ++fake.closes;
    auto& native = *fake.threads.at(thread); assert(!native.started || native.waited);
    if (auto error = std::exchange(fake.close_error, 0)) return error;
    fake.threads.erase(thread); *thread = {}; return 0;
}

int main(int argc, char** argv) {
    assert(argc == 3);
    const std::string root = argv[1];
    moader::UnavailableBackend backend;
    moader::Library library(root, backend);
    moader::WebServer server(library, root, "001234", {});
    const auto baseline = descriptors();

    fake.create_error = 0x1234;
    assert(!server.start("127.0.0.1", 0));
    assert(server.last_error() == "Web worker create: 0x00001234");
    assert(fake.starts == 0 && fake.closes == 0 && fake.threads.empty());
    assert(!server.running() && !server.port() && descriptors() == baseline);
    check_recovery(server);

    fake.start_error = 0x2345;
    const unsigned before_start_close = fake.closes, before_start_wait = fake.waits;
    assert(!server.start("127.0.0.1", 0));
    assert(server.last_error() == "Web worker start: 0x00002345");
    assert(fake.closes == before_start_close + 1 && fake.waits == before_start_wait);
    assert(fake.threads.empty() && descriptors() == baseline);
    check_recovery(server);

    fake.start_error = 0x2345; fake.close_error = 0x4567;
    assert(!server.start("127.0.0.1", 0));
    assert(server.last_error() == "Web worker start: 0x00002345; Web worker close: 0x00004567");
    assert(!server.running() && !server.port() && descriptors() == baseline && fake.threads.size() == 1);
    const unsigned retained_creates = fake.creates, retained_waits = fake.waits;
    assert(!server.start("127.0.0.1", 0) && fake.creates == retained_creates);
    server.stop(); assert(fake.waits == retained_waits && fake.threads.empty());
    check_recovery(server);

    assert(server.start("127.0.0.1", 0));
    const int retained_listener = listener_since(baseline);
    fake.wait_error = 0x3456;
    const unsigned before_wait_close = fake.closes;
    fails([&] { server.stop(); }, "Web worker wait: 0x00003456");
    assert(server.last_error() == "Web worker wait: 0x00003456");
    assert(!server.running() && fake.closes == before_wait_close && fcntl(retained_listener, F_GETFD) >= 0);
    assert(!server.start("127.0.0.1", 0));
    server.stop(); assert(fake.threads.empty() && descriptors() == baseline);
    check_recovery(server);

    assert(server.start("127.0.0.1", 0));
    const int close_listener = listener_since(baseline);
    fake.close_error = 0x4567;
    fails([&] { server.stop(); }, "Web worker close: 0x00004567");
    assert(!server.running() && fcntl(close_listener, F_GETFD) >= 0 && fake.threads.size() == 1);
    const unsigned joined_waits = fake.waits;
    assert(!server.start("127.0.0.1", 0));
    server.stop(); assert(fake.waits == joined_waits && fake.threads.empty() && descriptors() == baseline);

    for (unsigned i = 0; i < 10; ++i) {
        assert(server.start("127.0.0.1", 0));
        const auto before_client = descriptors();
        const int client = connect_to(server.port());
        send_all(client, "GET /api/state HTTP/1.1\r\n");
        // Observe acceptance so stop must interrupt a real partial request.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (descriptors().size() < before_client.size() + 2 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        assert(descriptors().size() >= before_client.size() + 2);
        const auto start = std::chrono::steady_clock::now(); server.stop();
        assert(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
        close(client);
        assert(fake.threads.empty() && descriptors() == baseline);
    }

    // Exercise actual compressed upload/import through the native-selected worker.
    std::ifstream input(argv[2], std::ios::binary);
    const std::string zip{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    input.close();
    assert(!zip.empty() && server.start("127.0.0.1", 0));
    const auto port = server.port();
    const auto begin = request(port, "POST", "/api/upload/begin?titleId=0400f43008c44001&id=worker-upload&name=Worker&size=" + std::to_string(zip.size()), "", 201);
    const auto key = begin.find("\"uploadId\":\""); assert(key != std::string::npos);
    const auto start = key + std::strlen("\"uploadId\":\"");
    const auto end = begin.find('"', start); assert(end != std::string::npos);
    const auto route = "/api/upload/" + begin.substr(start, end - start);
    request(port, "PUT", route + "?offset=0", zip);
    request(port, "POST", route, "", 201);
    assert(library.list().size() == 1 && library.list()[0].bytes > 65536);
    server.stop(); assert(fake.threads.empty() && descriptors() == baseline);
    std::cout << "SDK-selected web worker: create/start/wait/close failures, ownership, HTTP, 10 active-client stops and compressed ZIP import passed\n";
}
