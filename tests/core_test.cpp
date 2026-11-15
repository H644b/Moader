#include "moader/library.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace fs = std::filesystem;
static void expect(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> static std::string rejects(F f, const char* message) {
    try { f(); } catch (const std::exception& e) { return e.what(); }
    throw std::runtime_error(message);
}
static std::string read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    expect(bool(file), "Expected extracted payload or fixture is missing.");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
struct RecordingBackend : moader::Backend {
    moader::RuntimeStatus state;
    moader::RuntimeStatus status() const override { return state; }
    void publish(uint64_t generation, const std::vector<moader::Mod>&) override {
        if (state.connected) state.applied_generation = generation;
    }
};
int main(int argc, char** argv) {
    try {
        expect(argc == 3, "Pass fixtures and an empty test directory.");
        const std::string fixtures = argv[1], root = argv[2], title = "0400f43008c44001";
        expect(!fs::exists(root), "Test directory must be new.");
        RecordingBackend backend;
        unsigned name_lookups = 0;
        moader::Library library(root, backend, [&](const std::string& id) {
            ++name_lookups;
            return id == title ? std::string("Installed game \"fixture\"") : std::string();
        });
        moader::atomic_write(root + "/publication","old");
        moader::atomic_write(root + "/publication","new");
        { std::ifstream file(root + "/publication"); std::string value; file >> value; expect(value == "new","Metadata replacement must work on non-replacing rename filesystems."); }
        fs::rename(root + "/publication",root + "/publication.previous");
        { std::ofstream file(root + "/publication.tmp"); file << "uncommitted"; }
        moader::recover_atomic_file(root + "/publication");
        { std::ifstream file(root + "/publication"); std::string value; file >> value; expect(value == "new","Interrupted publication must restore the last committed metadata."); }
        expect(!fs::exists(root + "/publication.tmp") && !fs::exists(root + "/publication.previous"),"Recovered metadata must retire abandoned files.");
        expect(library.list().empty(), "Fresh library is empty.");
        auto prefs = library.preferences();
        expect(prefs.game_names && !prefs.reduced_motion && !prefs.web_on_start && prefs.web_port == 7712 && prefs.follow_log,
               "Initial preferences must be usable without enabling networking automatically.");
        const auto before_settings = library.generation();
        prefs = {false, true, true, 8721, false}; library.set_preferences(prefs);
        moader::Preferences stale_values; stale_values.game_names = true;
        library.update_preferences(stale_values, 1);
        expect(library.preferences().web_port == 8721 && library.preferences().reduced_motion && !library.preferences().follow_log,
               "A partial setting update must preserve another client's saved fields.");
        library.set_preferences(prefs);
        expect(library.generation() == before_settings, "Display/network preferences must not publish game mod routing.");
        auto invalid = prefs; invalid.web_port = 7713;
        rejects([&] { library.set_preferences(invalid); }, "Instance-lock port must not be selected for the web server.");
        expect(library.preferences().web_port == 8721, "Invalid settings must leave the committed preferences unchanged.");
        expect(library.game_name("invalid").empty() && name_lookups == 0, "Invalid game IDs must not reach the metadata service.");
        expect(library.game_name("0400F43008C44001") == "Installed game \"fixture\"" &&
               library.game_name(title) == "Installed game \"fixture\"" && name_lookups == 1,
               "Actual metadata lookup must be cached by canonical game identity.");
        expect(library.game_name("0400000000010000").empty(), "Unavailable names must fall back to the real Title ID.");
        auto first = library.import_zip(title, "texture", fixtures + "/good.zip", "Texture example");
        const auto snapshot = library.snapshot_json();
        expect(snapshot.find("\"gameNames\":{\"" + title + "\":\"Installed game \\\"fixture\\\"\"}") != std::string::npos,
               "Browser snapshot must publish metadata names with JSON escaping.");
        expect(first.files.size() == 2 && !first.requested && first.has_romfs, "ZIP import indexes payload and starts disabled.");
        auto enabled = library.set_enabled(title, "texture", true);
        expect(enabled.requested && enabled.applied == moader::ApplyState::Pending, "Unavailable backend must not report active.");
        library.import_zip(title, "collision", fixtures + "/good.zip", "Conflicting texture");
        const auto stale = library.generation() - 1;
        rejects([&] { library.set_enabled(title, "texture", false, stale); }, "Stale controller selection must reject.");
        rejects([&] { library.remove(title, "texture", stale); }, "Stale removal must reject.");
        rejects([&] { library.set_enabled(title, "collision", true); }, "Conflicting enabled file must reject.");
        expect(!fs::exists(root + "/library/" + title + "/collision/.enabled"), "Rejected enable must leave settings disabled.");
        rejects([&] { library.import_zip(title, "texture", fixtures + "/good.zip", "overwrite"); }, "Existing mod must not be overwritten.");
        backend.state.connected = true;
        library.set_enabled(title, "texture", false);
        enabled = library.set_enabled(title, "texture", true);
        expect(enabled.applied == moader::ApplyState::ReopenContent, "Published RomFS routing must retain its cache qualification.");
        library.import_zip(title, "executable", fixtures + "/exefs.zip", "Code example");
        enabled = library.set_enabled(title, "executable", true);
        expect(enabled.applied == moader::ApplyState::RelaunchGame, "Executable replacement must require game relaunch.");
        library.import_zip("0400F43008C44001", "wrapped", fixtures + "/wrapped.zip", "Wrapped example");
        for (const char* name : {"traversal", "case-collision", "symlink", "bad-crc", "underdeclared", "metadata-only"}) {
            rejects([&] { library.import_zip(title, name, fixtures + '/' + name + ".zip", name); }, "Unsafe ZIP must reject.");
            expect(!fs::exists(root + "/library/" + title + '/' + name), "Failed import must stay outside library.");
        }
        expect(!fs::exists(root + "/escape"), "Traversal must not escape extraction directory.");
        moader::Library boundaries(root + "/boundary-tests", backend);
        for (const char* name : {"boundary", "boundary-repeated", "boundary-empty-final", "boundary-final"}) {
            const auto mod = boundaries.import_zip(title, name, fixtures + '/' + name + ".zip", name);
            expect(!mod.requested && mod.has_romfs && mod.files.size() == 1,
                   "A valid boundary ZIP must import as one disabled payload.");
            expect(read_file(root + "/boundary-tests/library/" + title + '/' + name + "/romfs/boundary.bin") ==
                   read_file(fixtures + '/' + name + ".bin"), "Boundary ZIP must extract the exact original bytes.");
        }
        for (const char* name : {"boundary-truncated", "boundary-no-end", "boundary-trailing", "boundary-trailing-unread", "boundary-corrupt", "boundary-crc", "boundary-underdeclared"}) {
            const auto error = rejects([&] { boundaries.import_zip(title, name, fixtures + '/' + name + ".zip", name); },
                                       "Malformed boundary ZIP must reject.");
            expect(!fs::exists(root + "/boundary-tests/library/" + title + '/' + name),
                   "Failed boundary import must stay outside the active library.");
            if (std::string(name) == "boundary-corrupt" || std::string(name) == "boundary-no-end") {
                expect(error.find("romfs/boundary.bin") != std::string::npos && error.find("zlib ") != std::string::npos,
                       "Decoder failure must identify the entry and zlib status.");
            }
        }
        library.remove(title, "collision");
        expect(fs::exists(root + "/library/" + title + "/collision/.removed"), "Removed mod must be hidden without invalidating mounted paths.");
        expect(fs::exists(root + "/library/" + title + "/collision/romfs/config.txt"), "An older mounted generation must still be able to open its payload.");
        expect(!fs::is_empty(root + "/.trash"), "Removal must retain a recoverable copy.");
        const auto generation = library.generation();
        moader::Library reopened(root, backend);
        const auto reopened_prefs = reopened.preferences();
        expect(!reopened_prefs.game_names && reopened_prefs.reduced_motion && reopened_prefs.web_on_start &&
               reopened_prefs.web_port == 8721 && !reopened_prefs.follow_log,
               "All shared preferences must survive app restart.");
        expect(reopened.generation() == generation && reopened.list().size() == 3, "Reopening preserves settings and library.");
        rejects([] { moader::checked_relative_path("a/../b"); }, "Parent traversal must reject.");
        rejects([] { moader::checked_relative_path("a\\b"); }, "Windows separator must reject.");
        rejects([&] { library.remove(title, "../escape"); }, "Mod ID traversal must reject.");
        std::cout << "PASS: import, 32 KiB DEFLATE boundaries, conflicts, publication states, recovery, persistence and hostile ZIP fixtures\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
