// Host input checks and captures of the production framebuffer renderer.
#include "ui.hpp"
#include "ui_bitmap.hpp"
#include "store_assets.hpp"
#include "store_inventory.hpp"
#include <chrono>
#include <thread>
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
using namespace moader_ui;
namespace {
namespace fs = std::filesystem;
fs::path catalog_root;
const char* const names[] = {"Animal Crossing: New Horizons", "The Legend of Zelda: Tears of the Kingdom",
                             "Pokémon Legends: Z-A", "Mario Kart World", "Metroid Dread"};
const char* const icons[] = {"island", "hyrule", "lumiose", "circuit", "dread"};
constexpr uint64_t ids[] = {0x01006F8002326000, 0x0100F2C0115B6000, 0x0400F43008C44001, 0x0400C3F00006E000,
                            0x010093801237C000};
State fixture(const fs::path& assets) {
    State s;
    s.connected = true;
    s.preferences_available = true;
    s.address = "http://192.0.2.42:7712";
    s.pairing = "041826";
    s.summary.flags = moader::ipc::BackendConnected;
    s.summary.generation = 12;
    s.summary.applied_generation = 11;
    const char* const mods[] = {"Lumiose texture pack",    "Evening lighting",  "Battle camera",
                                "High-resolution signage", "Night reflections", "Window lights"};
    for (unsigned i = 0; i < 5; ++i)
        for (unsigned j = 0; j < (i == 2 ? 4u : 2u); ++j) {
            Mod m;
            m.data.title_id = ids[i];
            m.data.bytes = (j + 1) * 2457600;
            m.data.file_count = 24 + j * 8;
            m.data.flags = moader::ipc::Romfs | (j < 2 ? moader::ipc::Requested : 0);
            m.data.applied = j < 2 ? j + 1 : 0;
            m.game_name = names[i];
            m.name = mods[j];
            m.id = j ? "sample-" + std::to_string(j) : "lumiose-texture-pack";
            m.detail = "The request is saved. Moader is waiting for the runtime to acknowledge it.";
            s.mods.push_back(m);
        }
    s.regroup();
    s.game = 2;
    s.game_source_available = true;
    s.game_source_title = ids[2];
    if (!assets.empty())
        for (unsigned i = 0; i < 5; ++i) {
            std::ifstream f(assets / (std::string(icons[i]) + ".jpg"), std::ios::binary);
            std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(f), {}};
            s.games[i].art = decode_art(bytes.data(), bytes.size());
            assert(s.games[i].art);
        }
    s.runtime_detail = "Filesystem runtime connected.";
    s.events = {"Runtime accepted routing for generation 11.", "Enable requested for Lumiose texture pack.",
                "Library generation 12 saved."};
    return s;
}
void capture(const fs::path& p, const State& s, int w = 1280, int h = 720) {
    Renderer r;
    std::vector<uint32_t> pixels(w * h);
    r.draw(pixels.data(), w, h, w * 4, s, 1.f / 60);
    if (s.view == View::Store) {
        for (auto url = Store::next_preview(); !url.empty(); url = Store::next_preview()) {
            const auto& images = store_assets::images;
            auto image = std::find_if(std::begin(images), std::end(images),
                                      [&](const auto& item) { return item.url == url; });
            Art art;
            if (image != std::end(images))
                art = decode_art(image->bytes, image->length);
            auto marker = url.find("/previews/");
            if (!art && !catalog_root.empty() && marker != url.npos && url.find("..") == url.npos) {
                std::ifstream in(catalog_root / url.substr(marker + 1), std::ios::binary);
                std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), {}};
                if (!bytes.empty())
                    art = decode_art(bytes.data(), bytes.size());
            }
            Store::finish_preview(url, std::move(art));
        }
        r.draw(pixels.data(), w, h, w * 4, s, 1.f / 60);
    }
    std::ofstream out(p, std::ios::binary);
    out << "P6\n" << w << " " << h << "\n255\n";
    for (auto px : pixels) {
        out.put(px & 255);
        out.put(px >> 8 & 255);
        out.put(px >> 16 & 255);
    }
    assert(out.good());
}
void tests(State s) {
    const auto original = s.selected();
    s.local_input(RightInput, 0);
    assert(s.game == 3);
    s.local_input(PreviousInput, 0);
    assert(s.selected() == original);
    s.local_input(ActivateInput, 0);
    assert(s.view == View::Mods);
    s.local_input(DownInput, 0);
    auto row = s.row;
    auto index = s.selected();
    assert(s.local_input(ActivateInput, 0) == 0 && s.view == View::Details);
    assert(s.local_input(ActivateInput, 0) == ToggleMod);
    s.local_input(RemoveInput, 0);
    assert(s.confirm_remove);
    s.local_input(BackInput, 0);
    assert(!s.confirm_remove && s.view == View::Details);
    s.local_input(RemoveInput, 0);
    assert(s.local_input(RemoveInput, 0) == RemoveMod);
    s.local_input(BackInput, 0);
    assert(s.row == row && s.selected() == index && s.view == View::Mods);
    s.local_input(BackInput, 0);
    assert(s.view == View::Library && s.game == 2);
    s.local_input(SettingsInput, 0);
    assert(s.view == View::Settings);
    for (unsigned i = 0; i < 4; ++i) {
        s.settings_row = i;
        assert(s.local_input(ActivateInput, 0) == SavePreferences);
    }
    s.game_source_available = true;
    s.game_source_title = s.games[s.game].id;
    s.settings_row = 4;
    assert(s.local_input(ActivateInput, 0) == GetGameSource);
    s.settings_row = 2;
    assert(s.local_input(ActivateInput, 0) == SaveGameSource && s.game_source == 2);
    s.summary.flags |= moader::ipc::InventoryUnavailable;
    assert(s.local_input(ActivateInput, 0) == 0);
    s.summary.flags &= ~moader::ipc::InventoryUnavailable;
    s.open_tab(2);
    s.local_input(ActivateInput, 0);
    assert(s.view == View::Port);
    s.port_draft = 7713;
    assert(!s.local_input(ActivateInput, 0) && s.view == View::Port && s.message_error);
    s.port_draft = 8123;
    assert(s.local_input(ActivateInput, 0) == SavePreferences && s.preferences.port == 8123);
    s.settings_row = 2;
    assert(s.local_input(ActivateInput, 0) == SaveReport);
    s.settings_row = 3;
    assert(s.local_input(ActivateInput, 0) == SaveScreenshot);
    s.settings_row = 4;
    s.local_input(ActivateInput, 0);
    assert(s.view == View::Reload && !s.local_input(ActivateInput, 0));
    s.reload_available = true;
    assert(s.local_input(ActivateInput, 0) == ReloadService);
    s.reload_busy = true;
    assert(!s.local_input(ActivateInput, 0));
    s.connected = false;
    s.local_input(DebugInput, 10);
    assert(s.view == View::Debug && s.local_input(ActivateInput, 10) == SaveReport);
    s.open_tab(1);
    assert(s.local_input(ActivateInput, 0) == Reconnect);
    s.connected = true;
    assert(s.local_input(ActivateInput, 0) == ToggleWeb);
    s.summary.flags |= moader::ipc::WebRunning;
    assert(s.open_tab(1) == GetPairing);
    s.open_tab(0);
    s.local_input(LeftInput, 0);
    s.local_input(DownInput, 0);
    assert(s.view == View::Connection && s.tab_focus);
    s = fixture({});
    s.view = View::Details;
    s.mods[s.selected()].detail = std::string(800, 'W');
    Renderer r;
    assert(r.scroll_limit(s, s.view) > 2);
    s.retain_inventory(s.summary);
    assert(!s.local_input(ActivateInput, 0) && !s.local_input(RemoveInput, 0) && !s.confirm_remove);
    s = fixture({});
    s.view = View::Library;
    std::vector<uint32_t> padded(1320 * 720, 0x12345678);
    r.draw(padded.data(), 1280, 720, 1320 * 4, s, .016f);
    for (int y = 0; y < 720; ++y)
        for (int x = 1280; x < 1320; ++x)
            assert(padded[y * 1320 + x] == 0x12345678);
    assert(StoreAvailable);
    assert(r.tap(s, 1145.f / 1440, 48.f / 810) == 0 && s.view == View::Store && s.store.games.empty());
    s.local_input(BackInput, 0);
    r.draw(padded.data(), 1280, 720, 1320 * 4, s, 1);
    r.tap(s, .5, .45);
    assert(s.view == View::Mods);
    r.draw(padded.data(), 1280, 720, 1320 * 4, s, 1);
    assert(r.tap(s, 1145.f / 1440, 48.f / 810) == 0 && s.view == View::Store);
    s.local_input(BackInput, 0);
    s.local_input(UpInput, 0);
    s.local_input(LeftInput, 0);
    s.local_input(ActivateInput, 0);
    assert(s.view == View::Store && s.store.games.empty());
    s.local_input(BackInput, 0);
    s.local_input(UpInput, 0);
    s.local_input(RightInput, 0);
    s.local_input(ActivateInput, 0);
    assert(s.view == View::Settings && s.store.games.empty());
    s.view = View::Debug;
    s.events.assign(128, std::string(400, 'W'));
    r.draw(padded.data(), 1280, 720, 1320 * 4, s, .016f);
    auto limit = r.scroll_limit(s, View::Debug);
    auto builds = r.event_layout_builds();
    assert(limit > 128);
    r.draw(padded.data(), 1280, 720, 1320 * 4, s, .016f);
    assert(r.event_layout_builds() == builds);
    s.local_input(UpInput, limit);
    assert(s.log_paused && s.debug_first == limit - 3);
    s.events.back() += " update";
    r.scroll_limit(s, View::Debug);
    assert(r.event_layout_builds() == builds + 1);
    auto bmp = bitmap(padded.data(), 1280, 720, 1320 * 4);
    assert(bmp.size() > 1280 * 720 * 3 && bmp[0] == 'B' && bmp[1] == 'M');
    s = fixture({});
    s.view = View::Details;
    s.mods.clear();
    s.regroup();
    assert(s.view == View::Library);
    const std::string catalog =
        R"({"format":"moader.catalog.v1","games":[{"titleId":"0100000000001234","title":"Test game","mods":[{"id":"sample","title":"Meadow","author":"Artist","category":"Visuals","sources":[{"label":"Workshop","url":"https://example.org/mod"}],"download":{"url":"https://example.org/mod.zip","bytes":242,"sha256":")" +
        std::string(64, 'a') + R"("}}]}]})";
    State browser;
    browser.connected = true;
    browser.view = View::Store;
    assert(browser.store.merge_catalog(catalog));
    browser.store.set_query("test meadow");
    assert(browser.store.results.size() == 1);
    r.draw(padded.data(), 1280, 720, 1320 * 4, browser, 1);
    assert(r.tap(browser, 650.f / 1440, 145.f / 810) == SearchStore);
    r.tap(browser, 1034.f / 1440, 145.f / 810);
    assert(browser.store.query.empty());
    r.tap(browser, 1200.f / 1440, 145.f / 810);
    assert(browser.store.owned && browser.store.results.empty());
    moader_native::StoreInventory inventory(
        [](moader_native::StorePresence& entries, const std::atomic<bool>&) {
            for (auto& e : entries)
                e.second = Presence::Present;
        });
    for (int i = 0; i < 100 && browser.store.results.empty(); ++i) {
        inventory.poll(browser);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(browser.games.empty() && browser.store.results.size() == 1);
    r.draw(padded.data(), 1280, 720, 1320 * 4, browser, 1);
    r.tap(browser, 997.f / 1440, 310.f / 810);
    assert(browser.store.page == Shelf::Release);
    r.draw(padded.data(), 1280, 720, 1320 * 4, browser, 1);
    assert(r.tap(browser, 1000.f / 1440, 598.f / 810) == InstallStoreMod);
    browser.transfer.phase = InstallPhase::Downloading;
    assert(browser.local_input(ActivateInput, 0) == 0);
    browser.transfer = {};
    Mod installed{};
    installed.id = "sample";
    installed.data.title_id = 0x0100000000001234ull;
    browser.mods = {installed};
    assert(browser.local_input(ActivateInput, 0) == 0);
    browser.transfer = {InstallPhase::Installed, "0100000000001234", "sample", {}, 242, 242};
    browser.mods.clear();
    r.draw(padded.data(), 1280, 720, 1320 * 4, browser, 1);
    assert(r.tap(browser, 1000.f / 1440, 598.f / 810) == InstallStoreMod);
    assert(browser.local_input(ActivateInput, 0) == InstallStoreMod);
    for (auto [from, to] : std::vector<std::pair<std::string, std::string>>{
             {"242", "242.5"},
             {"242", "134217729"},
             {std::string(64, 'a'), "ZZ"},
             {"https://example.org/mod.zip", "http://example.org/mod.zip"},
             {"https://example.org/mod.zip", "https://user@example.org/mod.zip"},
             {"sample", "bad/id"}}) {
        auto invalid = catalog;
        invalid.replace(invalid.find(from), from.size(), to);
        Store parsed;
        assert(parsed.load_catalog(invalid));
        assert(!parsed.games[0].mods[0].package.bytes);
    }
    Store canonical;
    assert(canonical.load_catalog(catalog, true));
    assert(!canonical.games.front().mods.front().package.bytes);
    auto official = catalog;
    official.replace(official.find("https://example.org/mod.zip"), std::string("https://example.org/mod.zip").size(),
                     std::string(StoreFiles) + "test/package.zip");
    assert(canonical.load_catalog(official, true));
    assert(canonical.games.front().mods.front().package.bytes == 242);
    official.insert(1, std::string("\"next\":\"") + StoreFeed + "?after=00000000-0000-0000-0000-000000000001\",");
    assert(canonical.merge_catalog(official, true) && !canonical.next_page.empty());
    official.replace(official.find(StoreFeed), std::string(StoreFeed).size(), "https://example.org/catalog.json");
    assert(!canonical.merge_catalog(official, true));
    Store st;
    assert(!st.load_catalog("{}"));
    st.start();
    assert(st.games.empty() && st.catalog_status == CatalogStatus::Loading);
    assert(st.merge_catalog(store_assets::catalog));
    assert(st.games.size() == 5 && st.results.size() > 5);
    const auto count = st.results.size();
    assert(st.input(RemoveInput) == SearchStore);
    st.set_query("  pOk");
    assert(!st.results.empty() && st.results.size() < count);
    st.input(ActivateInput);
    assert(st.page == Shelf::Release && !st.games[st.detail.game].mods[st.detail.release].sources.empty());
    assert(st.input(ActivateInput) == 0); // Old catalogs have no package metadata.
    st.back();
    assert(st.query == "pOk");
    st.set_query("unknown-title-1234");
    assert(st.results.empty());
    st.set_query("");
    st.input(NextInput);
    assert(st.owned && st.results.empty());
    st.games[2].presence = Presence::Present;
    st.rebuild_results();
    assert(st.results.size() == st.games[2].mods.size());
    st.input(ActivateInput);
    assert(st.detail.game == 2);
    st.input(RightInput);
    assert(st.frame < st.games[2].mods[st.detail.release].previews.size());
    assert(!st.back() && st.owned && st.row == 0);
    st.input(NextInput);
    assert(st.results.size() == count);
    st.input(UpInput);
    assert(st.input(ActivateInput) == SearchStore);
    st.input(RightInput);
    st.input(ActivateInput);
    assert(st.owned);
}
} // namespace
int main(int argc, char** argv) {
    fs::path dir = argc > 1 ? argv[1] : "native/review";
    fs::create_directories(dir);
    State s = fixture(argc > 2 ? argv[2] : "");
    tests(s);
    std::string catalog = store_assets::catalog;
    if (argc > 3) {
        catalog_root = fs::path(argv[3]).parent_path();
        std::ifstream f(argv[3]);
        catalog.assign(std::istreambuf_iterator<char>(f), {});
        Store published;
        assert(published.load_catalog(catalog) && !published.results.empty());
        published.input(ActivateInput);
        assert(published.input(ActivateInput) == InstallStoreMod);
    }
    capture(dir / "library-720.ppm", s);
    capture(dir / "library-reference.ppm", s, 1440, 810);
    capture(dir / "library-docked-1080.ppm", s, 1920, 1080);
    s.nav_focus = true;
    capture(dir / "header-focus-720.ppm", s);
    s.nav_focus = false;
    s.view = View::Mods;
    capture(dir / "mods-720.ppm", s);
    s.view = View::Details;
    capture(dir / "details-720.ppm", s);
    s.confirm_remove = true;
    capture(dir / "remove-720.ppm", s);
    s.confirm_remove = false;
    for (const auto& [v, file] :
         std::vector<std::pair<View, const char*>>{{View::Settings, "settings"},
                                                   {View::Advanced, "advanced"},
                                                   {View::GameCopy, "game-copy"},
                                                   {View::Port, "port"},
                                                   {View::Debug, "runtime"},
                                                   {View::Reload, "reload"},
                                                   {View::Controls, "controls"},
                                                   {View::BrowserControls, "browser-controls"},
                                                   {View::Credits, "credits"},
                                                   {View::Connection, "connection"}}) {
        s.view = v;
        capture(dir / (std::string(file) + "-720.ppm"), s);
    }
    s.summary.flags |= moader::ipc::WebRunning;
    capture(dir / "connection-on-720.ppm", s);
    s.view = View::Debug;
    s.summary.last_result = 0x1234;
    capture(dir / "runtime-error-720.ppm", s);
    s.summary.last_result = 0;
    s.view = View::Store;
    s.store.start();
    assert(s.store.merge_catalog(catalog));
    s.store.catalog_status = CatalogStatus::Ready;
    capture(dir / "store-720.ppm", s);
    capture(dir / "store-1080.ppm", s, 1920, 1080);
    s.store.input(ActivateInput);
    capture(dir / "store-detail-720.ppm", s);
    const auto& g = s.store.games[s.store.detail.game];
    const auto& mod = g.mods[s.store.detail.release];
    const auto total = mod.package.bytes ? mod.package.bytes : 130000;
    s.transfer = {InstallPhase::Downloading, g.id, mod.id, {}, total / 2, total};
    capture(dir / "store-download-720.ppm", s);
    s.store.back();
    s.store.set_owned(true);
    s.transfer = {};
    for (auto& entry : s.store.games) entry.presence = Presence::Absent;
    s.store.games[s.store.detail.game].presence = Presence::Present;
    s.store.rebuild_results();
    capture(dir / "store-filter-720.ppm", s);
    s.store.set_query("no such mod");
    capture(dir / "store-empty-720.ppm", s);
    s.mods.clear();
    s.regroup();
    s.view = View::Library;
    capture(dir / "empty-720.ppm", s);
    s.connected = false;
    capture(dir / "disconnected-720.ppm", s);
    std::cout
        << "Carousel, touch, settings, runtime, package actions, community Store and framebuffer checks passed.\n";
}
