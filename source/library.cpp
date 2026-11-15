#include "moader/library.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace moader {
namespace {
struct InventoryIoError : std::runtime_error { using std::runtime_error::runtime_error; };
InventoryIoError io_error(const std::string& what) {
    return InventoryIoError(what + ": " + std::strerror(errno));
}
bool exists(const std::string& path) {
    struct stat st{};
    if (!lstat(path.c_str(), &st)) return true;
    if (errno == ENOENT) return false;
    throw io_error("Inspect " + path);
}
std::string read_small(const std::string& path, size_t limit) {
    std::unique_ptr<FILE, decltype(&std::fclose)> file(std::fopen(path.c_str(), "rb"), std::fclose);
    if (!file) { if (errno == ENOENT) return {}; throw io_error("Read metadata " + path); }
    std::string out(limit + 1, '\0');
    const auto count = std::fread(out.data(), 1, out.size(), file.get());
    if (std::ferror(file.get())) throw io_error("Read metadata " + path);
    out.resize(count);
    if (out.size() > limit) throw std::runtime_error("Metadata is too large: " + path);
    return out;
}
std::vector<std::string> entries(const std::string& path) {
    DIR* dir = opendir(path.c_str());
    if (!dir) throw io_error("Open directory " + path);
    std::vector<std::string> out;
    try {
        while (true) {
            errno = 0;
            auto* e = readdir(dir);
            if (!e) { if (errno) throw io_error("Read directory " + path); break; }
            if (e->d_name[0] != '.') out.emplace_back(e->d_name);
        }
    } catch (...) { closedir(dir); throw; }
    if (closedir(dir)) throw io_error("Close directory " + path);
    std::sort(out.begin(), out.end()); return out;
}
bool is_directory(const std::string& path) {
    struct stat st{};
    if (lstat(path.c_str(), &st)) throw io_error("Inspect directory " + path);
    return S_ISDIR(st.st_mode);
}
std::string lowercase(std::string value) {
    for (char& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return value;
}
void walk(const std::string& root, const std::string& rel, Mod& mod, size_t& inventory, unsigned depth = 0) {
    if (depth > 32 || mod.files.size() > 8192) throw std::runtime_error("Mod exceeds the directory or file limit.");
    const auto full = root + '/' + rel;
    struct stat st{};
    if (lstat(full.c_str(), &st)) throw io_error("Inspect mod file");
    if (S_ISDIR(st.st_mode)) {
        for (const auto& name : entries(full)) walk(root, rel + '/' + name, mod, inventory, depth + 1);
    } else if (S_ISREG(st.st_mode)) {
        if (mod.files.size() >= 8192 || inventory >= 16384)
            throw std::runtime_error("The library supports 16384 indexed files and 8192 files per mod.");
        checked_relative_path(rel);
        mod.files.push_back(rel); mod.bytes += st.st_size; ++inventory;
        // Detect externally replaced payload metadata even when total bytes and
        // path names stay the same. No payload contents are read during a scan.
        for (unsigned char c : rel) mod.inventory_stamp = (mod.inventory_stamp ^ c) * 1099511628211ull;
        mod.inventory_stamp = (mod.inventory_stamp ^ uint64_t(st.st_size)) * 1099511628211ull;
        mod.inventory_stamp = (mod.inventory_stamp ^ uint64_t(st.st_mtime)) * 1099511628211ull;
    } else throw std::runtime_error("Mod contains a symbolic link or unsupported file type.");
}
bool same_inventory(const std::vector<Mod>& a, const std::vector<Mod>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto& x = a[i]; const auto& y = b[i];
        if (x.title_id != y.title_id || x.id != y.id || x.name != y.name ||
            x.requested != y.requested || x.was_activated != y.was_activated ||
            x.has_romfs != y.has_romfs || x.has_exefs != y.has_exefs || x.bytes != y.bytes ||
            x.valid_source != y.valid_source || x.files != y.files || x.detail != y.detail ||
            x.inventory_stamp != y.inventory_stamp || x.load_allowed != y.load_allowed ||
            x.availability_detail != y.availability_detail || x.game_source != y.game_source) return false;
    }
    return true;
}
void check_conflicts(const std::vector<Mod>& mods) {
    std::map<std::string, std::string> owners;
    for (const auto& mod : mods) if (mod.requested) {
        for (const auto& file : mod.files) {
            const auto key = mod.title_id + '/' + lowercase(file);
            auto found = owners.find(key);
            if (found != owners.end() && found->second != mod.id)
                throw std::runtime_error("File conflict: " + file + " is provided by " + found->second + " and " + mod.id + '.');
            owners[key] = mod.id;
        }
    }
}
std::string mod_json(const Mod& mod) {
    return "{\"titleId\":" + json_string(mod.title_id) + ",\"id\":" + json_string(mod.id) +
        ",\"name\":" + json_string(mod.name) + ",\"requested\":" + (mod.requested ? "true" : "false") +
        ",\"applied\":" + json_string(apply_state_name(mod.applied)) +
        ",\"romfs\":" + (mod.has_romfs ? "true" : "false") + ",\"exefs\":" + (mod.has_exefs ? "true" : "false") +
        ",\"bytes\":" + std::to_string(mod.bytes) + ",\"fileCount\":" + std::to_string(mod.files.size()) +
        ",\"gameSource\":" + json_string(game_source_name(static_cast<GameSource>(mod.game_source))) +
        ",\"detail\":" + json_string(mod.detail) + '}';
}
}

void make_directories(const std::string& path) {
    if (path.empty()) throw std::invalid_argument("Empty directory path.");
    for (size_t i = 1; i <= path.size(); ++i) if (i == path.size() || path[i] == '/') {
        const auto part = path.substr(0, i);
        if (part.back() == ':') continue;
        struct stat st{};
        if (!lstat(part.c_str(), &st)) {
            if (!S_ISDIR(st.st_mode)) throw std::runtime_error("A directory path is a link or file: " + part);
        } else if (errno != ENOENT || mkdir(part.c_str(), 0777)) throw io_error("Create directory " + part);
    }
}
void atomic_write(const std::string& path, const std::string& bytes) {
    recover_atomic_file(path);
    const auto temporary = path + ".tmp";
    // No existing temporary file is opened through a possible symlink.
    const int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) throw io_error("Create temporary metadata");
    size_t at = 0;
    while (at < bytes.size()) {
        const auto n = write(fd, bytes.data() + at, bytes.size() - at);
        if (n <= 0) { const auto error = io_error("Write metadata"); close(fd); unlink(temporary.c_str()); throw error; }
        at += n;
    }
    if (fsync(fd) && errno != ENOSYS && errno != EINVAL) {
        const auto error = io_error("Flush metadata"); close(fd); unlink(temporary.c_str()); throw error;
    }
    if (close(fd)) { unlink(temporary.c_str()); throw io_error("Close metadata"); }
#if defined(__SWITCH2__) || defined(MOADER_TEST_NONREPLACING_RENAME)
    // Horizon RenameFile does not provide POSIX destination replacement. A
    // complete old file remains recoverable until the new name is published.
    // Native catalogue readers retain old routing across the brief name gap.
    const auto previous = path + ".previous";
    const bool had_previous = exists(path);
    if (had_previous && rename(path.c_str(),previous.c_str())) {
        const auto error = io_error("Preserve previous metadata"); unlink(temporary.c_str()); throw error;
    }
    if (rename(temporary.c_str(),path.c_str())) {
        const auto error = io_error("Publish metadata");
        if (had_previous && !exists(path)) rename(previous.c_str(),path.c_str());
        unlink(temporary.c_str()); throw error;
    }
    if (had_previous) unlink(previous.c_str());
#else
    if (rename(temporary.c_str(), path.c_str())) { const auto error = io_error("Publish metadata"); unlink(temporary.c_str()); throw error; }
#endif
}
void recover_atomic_file(const std::string& path) {
    const auto previous = path + ".previous", temporary = path + ".tmp";
    auto regular = [](const std::string& file) {
        struct stat st{};
        if (lstat(file.c_str(),&st)) { if (errno == ENOENT) return false; throw io_error("Inspect metadata publication"); }
        if (!S_ISREG(st.st_mode)) throw std::runtime_error("Metadata publication contains a link or non-file: " + file);
        return true;
    };
    const bool present = regular(path), backup = regular(previous);
    if (!present && backup && rename(previous.c_str(),path.c_str())) throw io_error("Recover previous metadata");
    if (present && backup && unlink(previous.c_str())) throw io_error("Retire previous metadata");
    if (regular(temporary) && unlink(temporary.c_str())) throw io_error("Retire uncommitted metadata");
}
Library::Library(std::string root, Backend& backend, GameNameLookup names, AvailabilityLookup availability)
    : root_(std::move(root)), backend_(backend), name_lookup_(std::move(names)), availability_lookup_(std::move(availability)) {
    make_directories(root_ + "/library"); make_directories(root_ + "/.incoming"); make_directories(root_ + "/.trash");
    recover_atomic_file(root_ + "/generation");
    const auto previous = read_small(root_ + "/generation", 24);
    if (!previous.empty()) {
        size_t used = 0; generation_ = std::stoull(previous, &used);
        if (used != previous.size()) throw std::runtime_error("Invalid saved generation.");
    }
    recover_atomic_file(root_ + "/preferences");
    const auto saved = read_small(root_ + "/preferences", 128);
    if (!saved.empty()) {
        std::istringstream input(saved); unsigned version, names, motion, web, port, follow; std::string extra;
        if (!(input >> version >> names >> motion >> web >> port >> follow) || (input >> extra) ||
            version != 1 || names > 1 || motion > 1 || web > 1 || follow > 1 ||
            port < 1024 || port > 65535 || port == 7713)
            throw std::runtime_error("Saved preferences are invalid. Inspect sdmc:/moader/preferences.");
        preferences_ = {bool(names), bool(motion), bool(web), uint16_t(port), bool(follow)};
    }
    try { inventory_ = scan(); gate(inventory_); inventory_ready_ = true; }
    catch (const std::exception& e) { scan_failed(e.what()); }
}
Preferences Library::preferences() const {
    std::lock_guard<std::mutex> lock(mutex_); return preferences_;
}
void Library::set_preferences(const Preferences& p) {
    update_preferences(p, 31);
}
void Library::update_preferences(const Preferences& values, uint32_t mask) {
    if (!mask || mask > 31) throw std::runtime_error("Choose a valid setting to update.");
    if ((mask & 8) && (values.web_port < 1024 || values.web_port == 7713))
        throw std::runtime_error("Choose a web port from 1024 to 65535, except reserved port 7713.");
    std::unique_lock<std::mutex> lock(mutex_);
    if (quiescing_) throw std::runtime_error("The resident service is reloading. Reconnect when it finishes.");
    auto p = preferences_;
    if (mask & 1) p.game_names = values.game_names;
    if (mask & 2) p.reduced_motion = values.reduced_motion;
    if (mask & 4) p.web_on_start = values.web_on_start;
    if (mask & 8) p.web_port = values.web_port;
    if (mask & 16) p.follow_log = values.follow_log;
    atomic_write(root_ + "/preferences", "1 " + std::to_string(p.game_names) + ' ' +
        std::to_string(p.reduced_motion) + ' ' + std::to_string(p.web_on_start) + ' ' +
        std::to_string(p.web_port) + ' ' + std::to_string(p.follow_log) + '\n');
    preferences_ = p;
    lock.unlock();
    event("Settings saved.");
}
std::string Library::game_name_locked(const std::string& title) {
    if (!valid_title_id(title)) return {};
    const auto key = lowercase(title);
    const auto known = game_names_.find(key);
    if (known != game_names_.end()) return known->second;
    std::string name;
    if (name_lookup_) try { name = name_lookup_(key); } catch (const std::exception& e) {
        event("Game name unavailable for " + key + ": " + e.what());
    }
    if (name.size() > 512) name.clear();
    for (unsigned char c : name) if (c < 32 || c == 127) { name.clear(); break; }
    if (game_names_.size() < 1024) game_names_.emplace(key, name);
    return name;
}
std::string Library::game_name(const std::string& title) {
    std::lock_guard<std::mutex> lock(names_mutex_); return game_name_locked(title);
}
std::string Library::mod_path(const std::string& title, const std::string& id) const {
    if (!valid_title_id(title) || !valid_mod_id(id)) throw std::invalid_argument("Invalid game Title ID or mod ID.");
    return root_ + "/library/" + lowercase(title) + '/' + id;
}
const char* game_source_name(GameSource source) {
    switch (source) {
    case GameSource::Unspecified: return "unspecified";
    case GameSource::Cartridge: return "cartridge";
    case GameSource::Digital: return "digital";
    }
    return "unspecified";
}
GameSource Library::read_game_source(const std::string& title) {
    const auto path = root_ + "/library/" + lowercase(title) + "/.game-source";
    recover_atomic_file(path);
    const auto saved = read_small(path, 1);
    if (saved.empty() || saved == "0") return GameSource::Unspecified;
    if (saved == "1") return GameSource::Cartridge;
    if (saved == "2") return GameSource::Digital;
    throw std::runtime_error("Saved game-copy selection is invalid. Inspect the game's .game-source file.");
}
GameSource Library::game_source(const std::string& title) const {
    if (!valid_title_id(title)) throw std::invalid_argument("Invalid game Title ID.");
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& mod : inventory_) if (mod.title_id == lowercase(title))
        return static_cast<GameSource>(mod.game_source);
    return GameSource::Unspecified;
}
void Library::set_game_source(const std::string& title, GameSource source,
                              std::optional<uint64_t> expected_generation) {
    if (static_cast<uint32_t>(source) > 2) throw std::invalid_argument("Choose a valid game-copy setting.");
    if (!valid_title_id(title)) throw std::invalid_argument("Invalid game Title ID.");
    std::unique_lock<std::mutex> operation(operation_mutex_, std::try_to_lock);
    if (!operation.owns_lock() || importing_) throw std::runtime_error("An import or another library operation is in progress. Try again when it finishes.");
    if (quiescing()) throw std::runtime_error("The resident service is reloading. Reconnect when it finishes.");
    if (expected_generation && *expected_generation != generation())
        throw std::runtime_error("The library changed. Refresh before changing the game copy.");
    auto mods = cached_inventory(); bool found = false;
    for (auto& mod : mods) if (mod.title_id == lowercase(title)) {
        mod.game_source = static_cast<uint32_t>(source); found = true;
    }
    if (!found) throw std::runtime_error("The game has no mods in the library. Refresh and choose a game.");
    atomic_write(root_ + "/library/" + lowercase(title) + "/.game-source", std::to_string(static_cast<uint32_t>(source)));
    commit(std::move(mods), true);
    event("Game copy selected: " + lowercase(title) + " / " + game_source_name(source));
}
Mod Library::scan_mod(const std::string& title, const std::string& id, size_t& inventory) {
    const auto path = mod_path(title, id);
    Mod mod; mod.title_id = lowercase(title); mod.id = id;
    mod.game_source = static_cast<uint32_t>(read_game_source(title));
    recover_atomic_file(path + "/.enabled");
    recover_atomic_file(path + "/.activation-history");
    mod.name = read_small(path + "/mod.meta", 120);
    if (mod.name.empty()) mod.name = id;
    mod.requested = exists(path + "/.enabled");
    mod.was_activated = exists(path + "/.activation-history");
    mod.has_romfs = exists(path + "/romfs") || exists(path + "/romfs.bin");
    mod.has_exefs = exists(path + "/exefs") || exists(path + "/exefs_patches") || exists(path + "/exefs.nsp");
    try {
        for (const auto& part : {"romfs", "romfs.bin", "exefs", "exefs_patches", "exefs.nsp"})
            if (exists(path + '/' + part)) walk(path, part, mod, inventory);
    } catch (const InventoryIoError&) {
        // A failed read is not proof that a mod or the whole library is empty.
        throw;
    } catch (const std::exception& e) {
        mod.valid_source = false; mod.applied = ApplyState::Error; mod.detail = e.what();
    }
    return mod;
}
std::vector<Mod> Library::scan() {
    std::vector<Mod> mods;
    size_t inventory = 0;
    for (const auto& title : entries(root_ + "/library")) {
        if (!valid_title_id(title) || !is_directory(root_ + "/library/" + title)) continue;
        for (const auto& id : entries(root_ + "/library/" + title)) {
            if (!valid_mod_id(id)) continue;
            const auto path = mod_path(title, id);
            if (!is_directory(path) || exists(path + "/.removed")) continue;
            if (mods.size() == 512) throw std::runtime_error("The library supports at most 512 mods; the previous inventory was retained.");
            mods.push_back(scan_mod(title, id, inventory));
        }
    }
    return mods;
}
void Library::gate(std::vector<Mod>& mods, bool verified_inventory) {
    std::string inventory_error;
    if (!verified_inventory) {
        std::lock_guard<std::mutex> lock(mutex_);
        inventory_error = inventory_error_;
    }
    std::map<std::string, GameAvailability> games;
    for (auto& mod : mods) {
        mod.load_allowed = true; mod.availability_detail.clear();
        if (mod.requested && !inventory_error.empty()) {
            mod.load_allowed = false;
            mod.availability_detail = "The library scan failed. Refresh the library to verify its contents before restoring mod routing.";
            continue;
        }
        if (!mod.requested || !availability_lookup_) continue;
        auto [it, inserted] = games.try_emplace(mod.title_id);
        if (inserted) {
            try { it->second = availability_lookup_(mod.title_id, static_cast<GameSource>(mod.game_source)); }
            catch (const std::exception& e) {
                it->second = {false, std::string("Could not verify game availability: ") + e.what() + ". Mods are paused."};
            }
            if (!it->second.load_allowed && it->second.detail.empty())
                it->second.detail = "The game is unavailable. Its mods are paused.";
        }
        mod.load_allowed = it->second.load_allowed;
        mod.availability_detail = it->second.detail;
    }
}
std::vector<Mod> Library::cached_inventory() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!inventory_ready_ || !inventory_error_.empty())
        throw std::runtime_error("The library could not be read completely. Refresh the library before changing mods. " + inventory_error_);
    return inventory_;
}
void Library::scan_failed(const std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (inventory_error_ != error) {
        events_.push_back("Library scan failed; previous inventory retained: " + error);
        if (events_.size() > 128) events_.erase(events_.begin());
    }
    inventory_error_ = error;
}
void Library::commit(std::vector<Mod> mods, bool advance, bool verified_inventory) {
    // Only the operation owner calls commit. No state mutex is held during
    // catalogue construction, metadata flushes, NS queries or the FS wakeup.
    gate(mods, verified_inventory);
    uint64_t next = generation();
    if (advance) {
        if (next == std::numeric_limits<uint64_t>::max())
            throw std::runtime_error("Library generation exhausted; inspect the saved state.");
        ++next;
        atomic_write(root_ + "/generation", std::to_string(next));
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        generation_ = next; inventory_ = mods;
        if (verified_inventory) { inventory_ready_ = true; inventory_error_.clear(); }
        publish_error_.clear();
    }
    // The saved request survives card removal. Blocked mods are never offered
    // to a backend as enabled, so it cannot open their replacement payloads.
    for (auto& mod : mods) if (!mod.load_allowed) mod.requested = false;
    try { backend_.publish(next, mods); }
    catch (const std::exception& e) {
        { std::lock_guard<std::mutex> lock(mutex_); publish_error_ = e.what(); }
        event("Apply failed: " + std::string(e.what())); throw;
    }
}
std::vector<Mod> Library::list() {
    if (!rescan()) throw std::runtime_error("An import or another library operation is in progress. Try again when it finishes.");
    return snapshot().mods;
}
LibrarySnapshot Library::snapshot() {
    const auto status = backend_.status();
    LibrarySnapshot out; std::string publication_error;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        out = {generation_, inventory_, status, inventory_ready_, inventory_error_};
        publication_error = publish_error_;
    }
    for (auto& mod : out.mods) {
        if (!mod.valid_source) continue;
        mod.applied = ApplyState::Disabled; mod.detail.clear();
        if (mod.requested && !mod.load_allowed) {
            mod.applied = ApplyState::Unavailable; mod.detail = mod.availability_detail;
            if (!publication_error.empty())
                mod.detail += " Routing withdrawal failed: " + publication_error + ". Previous routing may remain in use.";
            else if (status.result)
                mod.detail += " " + status.detail;
            else if (!status.connected || status.applied_generation < out.generation)
                mod.detail += " Waiting for FS to confirm routing without this mod. Previously opened content may remain in use.";
            else
                mod.detail += " FS accepted routing without this mod. Already loaded changes may require a game relaunch.";
        } else if (mod.requested || mod.was_activated) {
            if (!publication_error.empty()) {
                mod.applied = ApplyState::Error; mod.detail = publication_error;
            } else if (status.result) {
                mod.applied = ApplyState::Error; mod.detail = status.detail;
            } else if (!status.connected || status.applied_generation < out.generation) {
                mod.applied = ApplyState::Pending; mod.detail = status.detail;
            } else {
                mod.applied = mod.has_exefs ? ApplyState::RelaunchGame : ApplyState::ReopenContent;
                if (!mod.requested) mod.detail = mod.has_exefs
                    ? "Disabled for new launches. Relaunch the game to remove previously loaded executable changes."
                    : "Disabled for later reads where live routing is supported. Cached assets and pinned mounts may need content to reopen.";
                else mod.detail = mod.has_exefs ? "Executable changes take effect on the next game launch."
                    : "Routing is ready. Same-sized file replacements can affect later reads; cached assets, additions and size changes may need content to reopen.";
            }
        }
    }
    return out;
}
uint64_t Library::generation() const { std::lock_guard<std::mutex> lock(mutex_); return generation_; }
void Library::event(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back(message);
    if (events_.size() > 128) events_.erase(events_.begin());
}
bool Library::begin_quiesce() {
    std::unique_lock<std::mutex> operation(operation_mutex_, std::try_to_lock);
    if (!operation.owns_lock() || importing_) return false;
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) return false;
    quiescing_ = true;
    return true;
}
void Library::cancel_quiesce() {
    std::lock_guard<std::mutex> operation(operation_mutex_);
    std::lock_guard<std::mutex> lock(mutex_);
    quiescing_ = false;
}
bool Library::quiescing() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return quiescing_;
}
void Library::synchronize() {
    std::lock_guard<std::mutex> operation(operation_mutex_);
    if (quiescing()) throw std::runtime_error("The resident service is reloading. Reconnect when it finishes.");
    if (importing_) throw std::runtime_error("An import is in progress. Try again when it finishes.");
    std::vector<Mod> mods;
    try { mods = scan(); }
    catch (const std::exception& e) { scan_failed(e.what()); throw; }
    commit(std::move(mods), false);
}
bool Library::rescan() {
    std::unique_lock<std::mutex> operation(operation_mutex_, std::try_to_lock);
    if (!operation.owns_lock() || importing_ || quiescing()) return false;
    std::vector<Mod> mods;
    try { mods = scan(); }
    catch (const std::exception& e) { scan_failed(e.what()); throw; }
    gate(mods);
    bool changed, retry;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        changed = !same_inventory(inventory_, mods);
        retry = !inventory_ready_ || !publish_error_.empty();
        if (!changed && !retry) { inventory_error_.clear(); return true; }
    }
    commit(std::move(mods), changed || retry);
    event("Library refreshed from SD.");
    return true;
}
bool Library::refresh_availability() {
    std::unique_lock<std::mutex> operation(operation_mutex_, std::try_to_lock);
    if (!operation.owns_lock() || quiescing()) return false;
    std::vector<Mod> mods;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!inventory_ready_) return true;
        mods = inventory_;
    }
    gate(mods, false);
    bool changed;
    { std::lock_guard<std::mutex> lock(mutex_); changed = !same_inventory(inventory_, mods) || !publish_error_.empty(); }
    if (changed) {
        commit(std::move(mods), true, false);
        event("Game availability changed; routing updated.");
    }
    return true;
}
Mod Library::set_enabled(const std::string& title, const std::string& id, bool enabled,
                         std::optional<uint64_t> expected_generation) {
    std::unique_lock<std::mutex> operation(operation_mutex_, std::try_to_lock);
    if (!operation.owns_lock() || importing_) throw std::runtime_error("An import or another library operation is in progress. Try again when it finishes.");
    if (quiescing()) throw std::runtime_error("The resident service is reloading. Reconnect when it finishes.");
    if (expected_generation && *expected_generation != generation())
        throw std::runtime_error("The library changed. Refresh before changing this mod.");
    const auto path = mod_path(title, id);
    auto mods = cached_inventory();
    auto it = std::find_if(mods.begin(), mods.end(), [&](const Mod& m) { return m.title_id == lowercase(title) && m.id == id; });
    if (it == mods.end()) throw std::runtime_error("Mod was not found. Refresh the library.");
    if (enabled && !it->valid_source) throw std::runtime_error(it->detail);
    if (it->requested == enabled) {
        gate(mods);
        bool changed;
        { std::lock_guard<std::mutex> lock(mutex_); changed = !same_inventory(inventory_, mods); }
        if (changed) commit(std::move(mods), true);
    } else {
        it->requested = enabled; check_conflicts(mods);
        if (enabled) {
            atomic_write(path + "/.activation-history", "1");
            atomic_write(path + "/.enabled", "1"); it->was_activated = true;
        } else if (unlink((path + "/.enabled").c_str())) throw io_error("Disable mod");
        commit(std::move(mods), true);
        event((enabled ? "Enabled request: " : "Disabled request: ") + title + '/' + id);
    }
    for (const auto& mod : snapshot().mods) if (mod.title_id == lowercase(title) && mod.id == id) return mod;
    throw std::runtime_error("Mod was not found after updating its request.");
}
void Library::remove(const std::string& title, const std::string& id,
                     std::optional<uint64_t> expected_generation) {
    std::unique_lock<std::mutex> operation(operation_mutex_, std::try_to_lock);
    if (!operation.owns_lock() || importing_) throw std::runtime_error("An import or another library operation is in progress. Try again when it finishes.");
    if (quiescing()) throw std::runtime_error("The resident service is reloading. Reconnect when it finishes.");
    if (expected_generation && *expected_generation != generation())
        throw std::runtime_error("The library changed. Refresh before removing this mod.");
    const auto path = mod_path(title, id);
    auto mods = cached_inventory();
    const auto it = std::find_if(mods.begin(), mods.end(), [&](const Mod& m) { return m.title_id == lowercase(title) && m.id == id; });
    if (it == mods.end() || !is_directory(path) || exists(path + "/.removed")) throw std::runtime_error("Mod was not found.");
    const auto next = generation() + 1;
    if (!next) throw std::runtime_error("Library generation exhausted; inspect the saved state.");
    const auto destination = root_ + "/.trash/" + lowercase(title) + '-' + id + '-' + std::to_string(next) + ".json";
    const auto recovery = "{\"titleId\":" + json_string(lowercase(title)) + ",\"id\":" + json_string(id) + ",\"path\":" + json_string(path) + '}';
    if (exists(destination) && read_small(destination,2048) != recovery)
        throw std::runtime_error("A different recovery record already exists; inspect the library.");
    // Preserve payload paths still pinned by older RomFS and CodeFS owners.
    if (!exists(destination)) atomic_write(destination,recovery);
    atomic_write(path + "/.removed",std::to_string(next));
    mods.erase(it); commit(std::move(mods), true);
    event("Removed from library: " + title + '/' + id);
}
std::string Library::snapshot_json() {
    const auto view = snapshot(); const auto& status = view.runtime;
    const auto settings = preferences();
    std::string out = "{\"generation\":" + std::to_string(view.generation) +
        ",\"inventory\":{\"ready\":" + (view.inventory_ready ? "true" : "false") +
        ",\"error\":" + json_string(view.inventory_error) + "},\"runtime\":{\"connected\":" + (status.connected ? "true" : "false") +
        ",\"backend\":" + json_string(status.backend) + ",\"detail\":" + json_string(status.detail) +
        ",\"result\":" + std::to_string(status.result) + ",\"appliedGeneration\":" + std::to_string(status.applied_generation) + "},\"mods\":[";
    bool first = true;
    for (const auto& mod : view.mods) { if (!first) out += ','; first = false; out += mod_json(mod); }
    out += "],\"settings\":" + preferences_json(settings) + ",\"gameNames\":{";
    std::map<std::string, std::string> names;
    for (const auto& mod : view.mods) if (!names.count(mod.title_id)) names.emplace(mod.title_id, game_name(mod.title_id));
    first = true;
    for (const auto& name : names) if (!name.second.empty()) {
        if (!first) out += ',';
        first = false;
        out += json_string(name.first) + ':' + json_string(name.second);
    }
    return out + "}}";
}
std::vector<std::string> Library::diagnostic_lines() {
    std::vector<std::string> lines;
    { std::lock_guard<std::mutex> lock(mutex_); lines = events_; }
    const auto native = backend_.events(); lines.insert(lines.end(),native.begin(),native.end());
    return lines;
}
std::string Library::events_json() {
    std::string out = "["; bool first = true;
    for (const auto& line : diagnostic_lines()) { if (!first) out += ','; first = false; out += json_string(line); }
    return out + ']';
}
}
