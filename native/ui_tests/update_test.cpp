#include "update_client.hpp"
#include "sha256.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;
using namespace moader_ui;
static std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
static void put(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << bytes;
    assert(out.good());
}
static void rejects(auto action) {
    try { action(); }
    catch (const std::exception&) { return; }
    assert(false && "expected failure");
}
static std::string hash(const std::string& data) {
    Sha256 h;
    h.update(data.data(), data.size());
    return h.finish();
}
static const std::string app_path = "/switch/My Moader/Moader.nro";
static const std::string plugin_path = "/kaihen/plugins/moader.nso";
#ifndef MOADER_LIVE_UPDATE
static const std::string app = std::string(16, '\0') + "NRO0" + std::string(50, 'x');
static const std::string plugin = "NSO0" + std::string(60, 'y');
static std::string manifest() {
    const auto asset = [](const std::string& b) {
        return "{\"bytes\":" + std::to_string(b.size()) + ",\"sha256\":\"" + hash(b) + "\"}";
    };
    return "{\"schema\":1,\"version\":\"3.10.0\",\"tag\":\"v3.10.0\",\"repository\":\"H644b/Moader\","
           "\"prerelease\":false,\"assets\":{\"Moader.nro\":" + asset(app) + ",\"moader.nso\":" + asset(plugin) + "}}";
}
static bool corrupt = false, offline = false;
static std::atomic<unsigned> checks{0}, downloads{0};
namespace moader_ui {
std::string latest_app_metadata(const std::atomic<bool>&) {
    ++checks;
    if (offline) throw std::runtime_error("Offline");
    return manifest();
}
void download_app_asset(const AppRelease&, bool is_plugin, const std::string& path, const std::atomic<bool>&,
                        std::atomic<uint64_t>& received) {
    ++downloads;
    if (offline) throw std::runtime_error("Offline");
    const auto& bytes = is_plugin ? plugin : app;
    auto saved = bytes;
    if (corrupt && is_plugin) saved.back() ^= 1;
    put(path, saved);
    received += bytes.size();
}
}
#endif

int main(int argc, char** argv) {
    assert(argc >= 2);
    const std::string sd = argv[1];
    const auto app_target = sd + app_path, plugin_target = sd + plugin_path;
    put(app_target, "original app");
    put(plugin_target, "original plugin");
    UpdateFiles files(sd, "sdmc:" + app_path);
    std::atomic<bool> cancel{false};
    std::atomic<uint64_t> bytes{0};
    assert(!files.automatic());
    files.set_automatic(true);
    assert(UpdateFiles(sd, app_path).automatic());
    files.set_automatic(false);
    const auto release = parse_app_release(latest_app_metadata(cancel));
    assert(!compare_release_version(release.version, release.version));
    assert(compare_release_version("3.10.0", "3.9.99") > 0 && compare_release_version("3.2.0", "3.11.0") < 0);
    assert(compare_release_version("3.5.0", "3.5.0-rc.1") > 0 && !compare_release_version("3.5.0", "3.5.0+test"));
    rejects([&] { compare_release_version("3.10.1-rc1", "3.0.0"); });
    rejects([&] { parse_app_release(release.manifest + "bad"); });
    rejects([&] { parse_app_release("{}"); });
    rejects([&] { parse_app_release(std::string(1000, '[')); });
#ifdef MOADER_LIVE_UPDATE
    assert(argc == 3);
    const auto direct = parse_app_release(read(argv[2]));
    assert(direct.version == release.version && direct.app.sha256 == release.app.sha256 &&
           direct.plugin.sha256 == release.plugin.sha256);
    files.download(release, cancel, bytes);
    files.prepare(release);
    files.install(release);
    assert(hash(read(app_target)) == release.app.sha256 && hash(read(plugin_target)) == release.plugin.sha256);
    assert(bytes == release.app.bytes + release.plugin.bytes);
    files.finish();
    std::cout << "Verified and installed official v" << release.version << " app and plugin in temporary SD tree ("
              << bytes << " bytes).\n";
#else
    assert(compare_release_version(release.version, "3.9.3") > 0);
    auto preview = release.manifest;
    preview.replace(preview.find("false"), 5, "true");
    rejects([&] { parse_app_release(preview); });
    auto wrong_repository = release.manifest;
    wrong_repository.replace(wrong_repository.find("H644b"), 5, "other");
    rejects([&] { parse_app_release(wrong_repository); });
    std::string api = "{\"draft\":false,\"prerelease\":false,\"tag_name\":\"v3.10.0\",\"assets\":[";
    api += "{\"name\":\"Moader.nro\",\"size\":" + std::to_string(app.size()) + ",\"digest\":\"sha256:" + hash(app) + "\"},";
    api += "{\"name\":\"moader.nso\",\"size\":" + std::to_string(plugin.size()) + ",\"digest\":\"sha256:" + hash(plugin) + "\"}]}";
    assert(parse_app_release(api).plugin.sha256 == release.plugin.sha256);
    api.replace(api.find("sha256:"), 7, "sha512:");
    rejects([&] { parse_app_release(api); });
    UpdateFiles unknown(sd, "sdmc:/../other.nro");
    rejects([&] { unknown.download(release, cancel, bytes); });
    corrupt = true;
    files.download(release, cancel, bytes);
    rejects([&] { files.prepare(release); });
    assert(read(app_target) == "original app" && read(plugin_target) == "original plugin");
    corrupt = false;
    files.download(release, cancel, bytes);
    const fs::path dir = fs::path(sd) / "moader/updates";
    put(dir / "app.new", app.substr(0, app.size() - 1));
    rejects([&] { files.prepare(release); });
    put(dir / "app.new", app);
    files.prepare(release);
    put(app_target + ".moader-previous.nro", "in use");
    rejects([&] { files.install(release); });
    assert(read(app_target) == "original app" && read(plugin_target) == "original plugin");
    assert(read(dir / "plugin.new") == plugin);
    fs::remove(app_target + ".moader-previous.nro");
    fs::rename(plugin_target, dir / "plugin.previous");
    fs::rename(dir / "plugin.new", plugin_target);
    fs::rename(app_target, app_target + ".moader-previous.nro");
    UpdateFiles interrupted(sd, "sdmc:" + app_path + ".moader-previous.nro");
    AppRelease saved;
    assert(interrupted.recover(saved));
    offline = true;
    interrupted.repair(saved, cancel, bytes);
    interrupted.install(saved);
    assert(read(app_target) == app && read(plugin_target) == plugin);
    assert(read(app_target + ".moader-previous.nro") == "original app");
    interrupted.finish();
    assert(!interrupted.recover(saved) && !fs::exists(app_target + ".moader-previous.nro"));
    offline = false;
    put(app_target, "old"); put(plugin_target, "old");
    files.download(release, cancel, bytes); files.prepare(release);
    fs::remove(dir / "app.new");
    rejects([&] { files.install(release); });
    assert(read(plugin_target) == "old");
    const auto before_repair = downloads.load();
    files.repair(release, cancel, bytes); files.install(release); files.finish();
    assert(downloads == before_repair + 1);
    assert(read(app_target) == app && read(plugin_target) == plugin);

    using P = UpdatePhase;
    moader_native::UpdateClient client(sd, app_path);
    UpdateStatus ui;
    client.open(ui, "3.9.0");
    uint64_t now = 10;
    auto wait_for = [&](auto predicate, bool may_prompt = true) {
        for (unsigned i = 0; i < 2000; ++i) {
            bool restart = client.poll(ui, now, may_prompt);
            if (predicate(restart)) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(false && "worker did not finish");
    };
    const auto checks_before = checks.load();
    assert(!client.poll(ui, now, true) && checks == checks_before && ui.phase == P::Idle);
    client.toggle(ui);
    wait_for([&](bool) { return ui.phase == P::Available; }, false);
    assert(!ui.prompt && downloads == before_repair + 1);
    client.poll(ui, now, true);
    assert(ui.prompt && ui.latest == "3.10.0");
    ui.prompt = false; // Later. An automatic recheck should stay quiet for the same release.
    now += 6 * 3600000;
    client.poll(ui, now, true);
    wait_for([&](bool) { return ui.phase == P::Available; });
    assert(!ui.prompt);
    client.check(ui, true);
    wait_for([&](bool){ return ui.prompt; });
    client.install(ui, false);
    assert(!ui.prompt && ui.phase == P::Available && !ui.detail.empty());
    put(app_target, "old"); put(plugin_target, "old");
    client.install(ui, true);
    wait_for([](bool restart){ return restart; });
    assert(ui.phase == P::Restarting && read(plugin_target) == plugin && read(app_target) == app);
    client.restarted(ui, false, "Plugin health check failed");
    assert(ui.phase == P::RestartNeeded && fs::exists(dir / "pending"));
    offline = true;
    client.install(ui, true);
    wait_for([](bool restart){ return restart; });
    client.restarted(ui, true, {});
    assert(ui.phase == P::Reopen && !fs::exists(dir / "pending"));
    moader_native::UpdateClient reopened(sd, app_path);
    UpdateStatus latest;
    reopened.open(latest, release.version);
    assert(latest.automatic && latest.phase == P::Idle);
    auto settle = [&] {
        for (int i = 0; i < 1000; ++i) {
            reopened.poll(latest, now, true);
            if (latest.phase != P::Checking) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(false);
    };
    settle();
    assert(latest.phase == P::Failed && !latest.prompt);
    offline = false; reopened.check(latest, true); settle();
    assert(latest.phase == P::Current && !latest.prompt);
    put(app_target, "previous usable app");
    files.download(release, cancel, bytes); files.prepare(release);
    fs::rename(app_target, app_target + ".moader-previous.nro");
    moader_native::UpdateClient superseded(sd, "/switch/Another Moader.nro");
    UpdateStatus newer;
    superseded.open(newer, "4.0.0");
    assert(newer.phase == P::Idle && read(app_target) == "previous usable app" && !fs::exists(dir / "pending"));
    std::cout << "Passed update verification, interrupted installs, recovery, prompts and restart handoff.\n";
#endif
}
