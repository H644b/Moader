// Production Library and ZIP code with deterministic filesystem/decoder faults.
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <zlib.h>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>
#include "moader/library.hpp"

namespace {
std::string fail_stat_path, fail_open_path, fail_read_path;
int open_errno = EIO;
std::atomic<unsigned> stat_calls{0};
std::map<DIR*, std::string> open_paths;
int observed_lstat(const char* path, struct stat* st) {
    ++stat_calls;
    if (path == fail_stat_path) { errno = EIO; return -1; }
    return ::lstat(path, st);
}
DIR* observed_opendir(const char* path) {
    if (path == fail_open_path) { errno = open_errno; return nullptr; }
    auto* dir = ::opendir(path);
    if (dir) open_paths[dir] = path;
    return dir;
}
dirent* observed_readdir(DIR* dir) {
    if (!fail_read_path.empty() && open_paths.at(dir) == fail_read_path) { errno = EIO; return nullptr; }
    return ::readdir(dir);
}
int observed_closedir(DIR* dir) { open_paths.erase(dir); return ::closedir(dir); }
std::mutex decode_mutex;
std::condition_variable decode_cv;
bool decode_entered = false, decode_release = false;
int paused_inflate(z_streamp stream, int flush) {
    { std::unique_lock<std::mutex> lock(decode_mutex);
      if (!decode_entered) {
          decode_entered = true; decode_cv.notify_all();
          decode_cv.wait(lock, [] { return decode_release; });
      }
    }
    return ::inflate(stream, flush);
}
}
#define lstat observed_lstat
#define opendir observed_opendir
#define readdir observed_readdir
#define closedir observed_closedir
#include "../source/library.cpp"
#undef lstat
#undef opendir
#undef readdir
#undef closedir
#define inflate paused_inflate
#include "../source/zip.cpp"
#undef inflate

namespace fs = std::filesystem;
using namespace std::chrono_literals;
struct Recording final : moader::Backend {
    std::atomic<uint64_t> accepted{0};
    unsigned publications = 0;
    bool fail_publication = false;
    std::vector<moader::Mod> offered;
    moader::RuntimeStatus status() const override { return {true, accepted.load(), "synthetic", "Synthetic accepted routing", 0}; }
    void publish(uint64_t generation, const std::vector<moader::Mod>& mods) override {
        if (fail_publication) throw std::runtime_error("Synthetic publication failure");
        offered = mods; ++publications; accepted = generation;
    }
};
template<class F> std::string rejects(F action) {
    try { action(); } catch (const std::exception& e) { return e.what(); }
    throw std::runtime_error("Expected operation to reject");
}
int main(int argc, char** argv) {
    assert(argc == 3);
    const std::string root = argv[1], title = "0100000000000000";
    const std::string title_path = root + "/library/" + title;
    const std::string mod_path = title_path + "/synthetic";
    fs::create_directories(mod_path + "/romfs");
    for (unsigned i = 0; i < 800; ++i) std::ofstream(mod_path + "/romfs/file" + std::to_string(i)) << "fixture";
    Recording backend;
    std::atomic<bool> initial_card{true};
    moader::Library library(root, backend, {}, [&](const std::string&, moader::GameSource) -> moader::GameAvailability {
        return {initial_card.load(), "Insert the matching cartridge."};
    });
    auto good = library.snapshot();
    assert(good.inventory_ready && good.inventory_error.empty() && good.mods.size() == 1 && good.mods[0].files.size() == 800);
    stat_calls = 0;
    for (unsigned i = 0; i < 10; ++i) { library.snapshot(); library.snapshot_json(); }
    assert(stat_calls == 0); // Polling must not inspect the 800 disabled payloads.
    auto retained = [&] {
        auto view = library.snapshot();
        assert(view.inventory_ready && !view.inventory_error.empty() && view.generation == good.generation);
        assert(view.mods.size() == 1 && view.mods[0].files == good.mods[0].files && backend.publications == 0);
        assert(rejects([&] { library.set_enabled(title, "synthetic", true); }).find("read completely") != std::string::npos);
    };
    for (const auto& path : {title_path, mod_path, mod_path + "/.enabled", mod_path + "/romfs/file1"}) {
        fail_stat_path = path; rejects([&] { library.rescan(); }); fail_stat_path.clear(); retained();
        assert(library.rescan() && library.snapshot().inventory_error.empty());
    }
    for (int error : {EIO, ENOENT}) {
        fail_open_path = root + "/library"; open_errno = error;
        rejects([&] { library.rescan(); }); fail_open_path.clear(); retained(); assert(library.rescan());
    }
    for (const auto& path : {root + "/library", mod_path + "/romfs"}) {
        fail_read_path = path; rejects([&] { library.rescan(); }); fail_read_path.clear(); retained(); assert(library.rescan());
    }
    fail_stat_path = title_path;
    moader::Library unready(root, backend);
    assert(!unready.snapshot().inventory_ready && !unready.snapshot().inventory_error.empty());
    fail_stat_path.clear();
    assert(unready.rescan() && unready.snapshot().inventory_ready);
    backend.publications = 0;

    library.set_enabled(title, "synthetic", true);
    good = library.snapshot();
    // Pause inside the actual decoder: readers and settings must complete while
    // extraction is still paused; competing writes must reject instead of wait.
    auto importing = std::async(std::launch::async, [&] { return library.import_zip(title, "uploaded", argv[2], "Synthetic upload"); });
    { std::unique_lock<std::mutex> lock(decode_mutex);
      if (!decode_cv.wait_for(lock, 5s, [] { return decode_entered; })) {
          decode_release = true; decode_cv.notify_all(); throw std::runtime_error("Import never reached decoder");
      }
    }
    auto reading = std::async(std::launch::async, [&] { return library.snapshot(); });
    auto settings = std::async(std::launch::async, [&] { auto p = library.preferences(); p.reduced_motion = true; library.set_preferences(p); });
    auto changing = std::async(std::launch::async, [&] { return rejects([&] { library.set_enabled(title, "synthetic", true); }); });
    const bool read_ready = reading.wait_for(500ms) == std::future_status::ready;
    const bool settings_ready = settings.wait_for(500ms) == std::future_status::ready;
    const bool change_ready = changing.wait_for(500ms) == std::future_status::ready;
    const bool still_importing = importing.wait_for(0ms) == std::future_status::timeout;
    const bool refused_shutdown = !library.begin_quiesce() && !library.quiescing();
    const bool rescan_busy = !library.rescan();
    initial_card = false;
    const bool withdrew_during_import = library.refresh_availability() && !backend.offered[0].requested;
    const bool withdrawal_kept_import_paused = importing.wait_for(0ms) == std::future_status::timeout;
    { std::lock_guard<std::mutex> lock(decode_mutex); decode_release = true; } decode_cv.notify_all();
    assert(read_ready && settings_ready && change_ready && still_importing && refused_shutdown && rescan_busy && withdrew_during_import && withdrawal_kept_import_paused);
    auto during = reading.get(); settings.get();
    assert(during.mods.size() == 1 && during.generation == good.generation);
    assert(changing.get().find("in progress") != std::string::npos);
    assert(importing.wait_for(5s) == std::future_status::ready && importing.get().files.size() == 1);
    assert(library.snapshot().mods.size() == 2 && library.generation() > good.generation);
    assert(library.preferences().reduced_motion);
    assert(!backend.offered[0].requested && library.snapshot().mods[0].requested); // Final import must not resurrect routing.

    // A manual rescan commits a complete changed inventory and invalidates old
    // index-based native selections. A stable rescan does not republish routing.
    const auto previous = library.generation();
    fs::create_directories(title_path + "/added/romfs");
    std::ofstream(title_path + "/added/romfs/file") << "external copy";
    assert(library.snapshot().mods.size() == 2);
    assert(library.rescan() && library.snapshot().mods.size() == 3 && library.generation() > previous);
    rejects([&] { library.remove(title, "synthetic", previous); });
    const auto publications = backend.publications;
    assert(library.rescan() && backend.publications == publications);

    Recording gated_backend;
    bool card_present = false, query_fails = false;
    moader::Library gated(root + "/gated", gated_backend, {}, [&](const std::string&, moader::GameSource) -> moader::GameAvailability {
        if (query_fails) throw std::runtime_error("Synthetic metadata query failed");
        return {card_present, card_present ? "" : "Insert this game's cartridge. Mods are paused until it is available."};
    });
    gated.import_zip(title, "card-mod", argv[2], "Card mod");
    auto mod = gated.set_enabled(title, "card-mod", true);
    assert(mod.requested && !mod.load_allowed && mod.applied == moader::ApplyState::Unavailable);
    assert(mod.detail.find("cartridge") != std::string::npos && !gated_backend.offered[0].requested);
    assert(fs::exists(root + "/gated/library/" + title + "/card-mod/.enabled"));
    card_present = true; assert(gated.refresh_availability());
    assert(gated.snapshot().mods[0].requested && gated_backend.offered[0].requested);
    card_present = false; assert(gated.refresh_availability());
    assert(!gated_backend.offered[0].requested && gated.snapshot().mods[0].applied == moader::ApplyState::Unavailable);
    query_fails = true; assert(gated.refresh_availability());
    assert(!gated_backend.offered[0].requested && gated.snapshot().mods[0].detail.find("Could not verify") != std::string::npos);
    query_fails = false; card_present = true; assert(gated.refresh_availability());
    assert(gated_backend.offered[0].requested);
    fail_stat_path = root + "/gated/library/" + title + "/card-mod/romfs/payload.bin";
    rejects([&] { gated.rescan(); });
    assert(!gated.snapshot().inventory_error.empty());
    gated_backend.fail_publication = true;
    rejects([&] { gated.refresh_availability(); });
    auto failed_withdrawal = gated.snapshot();
    assert(failed_withdrawal.inventory_ready && !failed_withdrawal.inventory_error.empty());
    assert(failed_withdrawal.mods[0].applied == moader::ApplyState::Unavailable);
    assert(failed_withdrawal.mods[0].detail.find("withdrawal failed") != std::string::npos);
    assert(gated_backend.offered[0].requested); // Old routing remains unconfirmed.
    gated_backend.fail_publication = false;
    assert(gated.refresh_availability() && !gated_backend.offered[0].requested);
    assert(!gated.snapshot().inventory_error.empty()); // Withdrawal is not a successful inventory scan.
    fail_stat_path.clear(); card_present = false;
    assert(gated.rescan() && gated.snapshot().inventory_error.empty());
    const auto source_generation = gated.generation();
    gated.set_game_source(title, moader::GameSource::Cartridge, source_generation);
    assert(gated.game_source(title) == moader::GameSource::Cartridge && gated.generation() > source_generation);
    rejects([&] { gated.set_game_source(title, moader::GameSource::Digital, source_generation); });
    rejects([&] { gated.set_game_source(title, static_cast<moader::GameSource>(3)); });
    moader::Library reopened(root + "/gated", gated_backend);
    assert(reopened.game_source(title) == moader::GameSource::Cartridge);
    // Draining reserves the existing writer without withdrawing its catalogue
    // or changing on-disk requests. No later mutation can restart that writer.
    const auto before_shutdown = gated.snapshot();
    const auto before_publications = gated_backend.publications;
    assert(gated.begin_quiesce() && gated.quiescing());
    assert(gated.begin_quiesce());
    assert(!gated.rescan() && !gated.refresh_availability());
    assert(gated.snapshot().generation == before_shutdown.generation);
    assert(gated.snapshot().mods.size() == before_shutdown.mods.size());
    assert(rejects([&] { gated.synchronize(); }).find("reloading") != std::string::npos);
    assert(rejects([&] { gated.set_enabled(title, "card-mod", false); }).find("reloading") != std::string::npos);
    assert(rejects([&] { gated.remove(title, "card-mod"); }).find("reloading") != std::string::npos);
    assert(rejects([&] { gated.import_zip(title, "another", argv[2], "Another"); }).find("reloading") != std::string::npos);
    assert(rejects([&] { gated.set_game_source(title, moader::GameSource::Digital); }).find("reloading") != std::string::npos);
    assert(rejects([&] { gated.set_preferences(gated.preferences()); }).find("reloading") != std::string::npos);
    assert(gated_backend.publications == before_publications);
    assert(fs::exists(root + "/gated/library/" + title + "/card-mod/.enabled"));
    gated.cancel_quiesce();
    assert(!gated.quiescing() && gated_backend.publications == before_publications);
    gated.set_preferences(gated.preferences());
    assert(gated.rescan());
    std::cout << "PASS: cached polling, retained inventories on stat/open/read faults, initial unavailability, nonblocking import/settings, busy writes, coherent generations and game-availability publication gate\n";
}
