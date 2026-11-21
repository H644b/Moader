// Live opt-in smoke test of published packages with the production local importer.
#include "catalog_fetch.hpp"
#include "store.hpp"
#include "moader/web.hpp"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>
namespace fs = std::filesystem;
using namespace moader_ui;
int main(int argc, char** argv) {
    assert(argc >= 4);
    std::vector<std::string> requested(argv + 3, argv + argc);
    std::atomic<bool> cancel{false}, importing{false};
    std::atomic<uint64_t> received{0};
    Store store;
    assert(store.load_catalog(fetch_catalog_asset(argv[1], 1 << 20, cancel)));
    moader::UnavailableBackend backend;
    fs::path root(argv[2]);
    moader::Library lib(root.string(), backend);
    moader::WebServer server(lib, root.string(), "102938", {});
    assert(server.start("127.0.0.1", 0));
    unsigned successes = 0;
    for (const auto& g : store.games)
        for (auto m : g.mods) {
            if (std::find(requested.begin(), requested.end(), m.id) == requested.end())
                continue;
            assert(m.package.bytes);
            auto run = [&] {
                received = 0;
                importing = false;
                install_catalog_package(m, g.id, server.port(), "102938", cancel, received, importing);
            };
            auto reject = [&] {
                try {
                    run();
                    throw std::logic_error("unexpected success");
                } catch (const std::runtime_error& e) {
                    std::cout << "Expected rejection: " << e.what() << '\n';
                }
                assert(fs::is_empty(root / ".uploads"));
                assert(lib.snapshot().mods.size() == successes);
            };
            auto orig = m.package;
            if (m.id == requested.front()) {
                m.package.sha256 = std::string(64, 'a');
                reject();
                assert(!importing);
                m.package = orig;
                ++m.package.bytes;
                reject();
                assert(!importing);
                m.package = orig;
                --m.package.bytes;
                reject();
                assert(!importing);
                m.package = orig;
                cancel = true;
                reject();
                cancel = false;
            }
            run();
            ++successes;
            assert(received == m.package.bytes && importing && fs::is_empty(root / ".uploads"));
            auto mods = lib.snapshot().mods;
            auto item = std::find_if(mods.begin(), mods.end(), [&](auto& a) { return a.id == m.id; });
            assert(item != mods.end() && !item->requested && item->has_romfs);
            std::cout << item->name << " imported disabled: " << item->files.size() << " files, " << received
                      << " verified package bytes." << std::endl;
            if (m.id == requested.front())
                reject();
        }
    assert(successes == requested.size());
}
