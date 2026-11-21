// Host input checks and captures of the production framebuffer renderer.
#include "ui.hpp"
#include "ui_bitmap.hpp"
#include "store_assets.hpp"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
using namespace moader_ui;
namespace {
namespace fs = std::filesystem;
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
            Store::finish_preview(url, image == std::end(images) ? Art{} : decode_art(image->bytes, image->length));
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
    Store st;
    assert(!st.load_catalog("{}"));
    st.start(ids[2]);
    assert(st.games.empty() && st.catalog_status == CatalogStatus::Loading);
    assert(st.merge_catalog(store_assets::catalog));
    assert(st.games.size() == 5 && st.game == 2);
    st.input(ActivateInput);
    assert(st.page == Shelf::Release && !st.host_url().empty());
    st.input(RightInput);
    assert(st.frame == 1);
    st.back();
    st.select_tab(st.tabs().size() - 1);
    assert(st.page == Shelf::Hosts);
    st.input(ActivateInput);
    assert(st.page == Shelf::Host && !st.matches().empty());
    st.input(ActivateInput);
    assert(st.page == Shelf::Release && st.host_url().starts_with("https://"));
    st.back();
    assert(st.page == Shelf::Host);
    st.input(DetailInput);
    st.input(LeftInput);
    st.input(ActivateInput);
    assert(st.game == 1 && st.page == Shelf::Discover);
}
} // namespace
int main(int argc, char** argv) {
    fs::path dir = argc > 1 ? argv[1] : "native/review";
    fs::create_directories(dir);
    State s = fixture(argc > 2 ? argv[2] : "");
    tests(s);
    if (argc > 3) {
        std::ifstream f(argv[3]);
        std::string json{std::istreambuf_iterator<char>(f), {}};
        Store template_store;
        assert(template_store.load_catalog(json) && !template_store.games.empty());
        template_store.input(ActivateInput);
        assert(template_store.host_url().starts_with("https://"));
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
    s.store.start(ids[2]);
    assert(s.store.merge_catalog(store_assets::catalog));
    capture(dir / "store-720.ppm", s);
    s.store.input(ActivateInput);
    capture(dir / "store-detail-720.ppm", s);
    s.store.back();
    s.store.select_tab(s.store.tabs().size() - 1);
    capture(dir / "store-hosts-720.ppm", s);
    s.store.input(DetailInput);
    capture(dir / "store-games-720.ppm", s);
    s.mods.clear();
    s.regroup();
    s.view = View::Library;
    capture(dir / "empty-720.ppm", s);
    s.connected = false;
    capture(dir / "disconnected-720.ppm", s);
    std::cout
        << "Carousel, touch, settings, runtime, package actions, community Store and framebuffer checks passed.\n";
}
