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
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace moader {
namespace {
std::runtime_error io_error(const std::string& what) {
    return std::runtime_error(what + ": " + std::strerror(errno));
}
bool exists(const std::string& path) { struct stat st{}; return lstat(path.c_str(), &st) == 0; }
std::string read_small(const std::string& path, size_t limit) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::string out(limit + 1, '\0');
    file.read(&out[0], out.size()); out.resize(file.gcount());
    if (out.size() > limit) throw std::runtime_error("Metadata is too large: " + path);
    return out;
}
std::vector<std::string> entries(const std::string& path) {
    DIR* dir = opendir(path.c_str());
    if (!dir) { if (errno == ENOENT) return {}; throw io_error("Open directory " + path); }
    std::vector<std::string> out;
    while (auto* e = readdir(dir)) if (e->d_name[0] != '.') out.emplace_back(e->d_name);
    closedir(dir); std::sort(out.begin(), out.end()); return out;
}
bool is_directory(const std::string& path) {
    struct stat st{};
    if (lstat(path.c_str(), &st)) return false;
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
    } else throw std::runtime_error("Mod contains a symbolic link or unsupported file type.");
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
Library::Library(std::string root, Backend& backend, GameNameLookup names)
    : root_(std::move(root)), backend_(backend), name_lookup_(std::move(names)) {
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
    std::lock_guard<std::mutex> lock(mutex_);
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
    std::lock_guard<std::mutex> lock(mutex_); return game_name_locked(title);
}
std::string Library::mod_path(const std::string& title, const std::string& id) const {
    if (!valid_title_id(title) || !valid_mod_id(id)) throw std::invalid_argument("Invalid game Title ID or mod ID.");
    return root_ + "/library/" + lowercase(title) + '/' + id;
}
std::vector<Mod> Library::scan() {
    std::vector<Mod> mods;
    size_t inventory = 0;
    const auto status = backend_.status();
    for (const auto& title : entries(root_ + "/library")) {
        if (!valid_title_id(title) || !is_directory(root_ + "/library/" + title)) continue;
        for (const auto& id : entries(root_ + "/library/" + title)) {
            if (!valid_mod_id(id)) continue;
            const auto path = mod_path(title, id);
            if (!is_directory(path) || exists(path + "/.removed")) continue;
            Mod mod; mod.title_id = lowercase(title); mod.id = id;
            recover_atomic_file(path + "/.enabled");
            recover_atomic_file(path + "/.activation-history");
            mod.name = read_small(path + "/mod.meta", 120);
            if (mod.name.empty()) mod.name = id;
            mod.requested = exists(path + "/.enabled");
            mod.has_romfs = exists(path + "/romfs") || exists(path + "/romfs.bin");
            mod.has_exefs = exists(path + "/exefs") || exists(path + "/exefs_patches") || exists(path + "/exefs.nsp");
            try {
                for (const auto& part : {"romfs", "romfs.bin", "exefs", "exefs_patches", "exefs.nsp"})
                    if (exists(path + '/' + part)) walk(path, part, mod, inventory);
                if (mod.requested || exists(path + "/.activation-history")) {
                    if (!publish_error_.empty()) {
                        mod.applied = ApplyState::Error; mod.detail = publish_error_;
                    } else if (status.result) {
                        mod.applied = ApplyState::Error; mod.detail = status.detail;
                    } else if (!status.connected || status.applied_generation < generation_) {
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
            } catch (const std::exception& e) { mod.valid_source = false; mod.applied = ApplyState::Error; mod.detail = e.what(); }
            mods.push_back(std::move(mod));
            if (mods.size() >= 512) return mods;
        }
    }
    return mods;
}
std::vector<Mod> Library::list() { std::lock_guard<std::mutex> lock(mutex_); return scan(); }
LibrarySnapshot Library::snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    return {generation_, scan(), backend_.status()};
}
uint64_t Library::generation() const { std::lock_guard<std::mutex> lock(mutex_); return generation_; }
void Library::event(const std::string& message) {
    events_.push_back(message);
    if (events_.size() > 128) events_.erase(events_.begin());
}
void Library::publish() {
    if (generation_ == std::numeric_limits<uint64_t>::max()) throw std::runtime_error("Library generation exhausted; inspect the saved state.");
    ++generation_; atomic_write(root_ + "/generation", std::to_string(generation_));
    publish_error_.clear();
    try { backend_.publish(generation_, scan()); }
    catch (const std::exception& e) { publish_error_ = e.what(); event("Apply failed: " + publish_error_); throw; }
}
void Library::synchronize() {
    std::lock_guard<std::mutex> lock(mutex_);
    publish_error_.clear();
    try { backend_.publish(generation_,scan()); }
    catch (const std::exception& e) { publish_error_ = e.what(); event("Apply failed: " + publish_error_); throw; }
}
Mod Library::set_enabled(const std::string& title, const std::string& id, bool enabled,
                         std::optional<uint64_t> expected_generation) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (expected_generation && *expected_generation != generation_)
        throw std::runtime_error("The library changed. Refresh before changing this mod.");
    const auto path = mod_path(title, id);
    auto mods = scan();
    auto it = std::find_if(mods.begin(), mods.end(), [&](const Mod& m) { return m.title_id == lowercase(title) && m.id == id; });
    if (it == mods.end()) throw std::runtime_error("Mod was not found. Refresh the library.");
    if (enabled && !it->valid_source) throw std::runtime_error(it->detail);
    if (it->requested == enabled) return *it;
    it->requested = enabled; check_conflicts(mods);
    if (enabled) {
        atomic_write(path + "/.activation-history", "1");
        atomic_write(path + "/.enabled", "1");
    }
    else if (unlink((path + "/.enabled").c_str())) throw io_error("Disable mod");
    publish(); event((enabled ? "Enabled request: " : "Disabled request: ") + title + '/' + id);
    mods = scan();
    return *std::find_if(mods.begin(), mods.end(), [&](const Mod& m) { return m.title_id == lowercase(title) && m.id == id; });
}
void Library::remove(const std::string& title, const std::string& id,
                     std::optional<uint64_t> expected_generation) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (expected_generation && *expected_generation != generation_)
        throw std::runtime_error("The library changed. Refresh before removing this mod.");
    const auto path = mod_path(title, id);
    if (!is_directory(path) || exists(path + "/.removed")) throw std::runtime_error("Mod was not found.");
    const auto destination = root_ + "/.trash/" + lowercase(title) + '-' + id + '-' + std::to_string(generation_ + 1) + ".json";
    const auto recovery = "{\"titleId\":" + json_string(lowercase(title)) + ",\"id\":" + json_string(id) + ",\"path\":" + json_string(path) + '}';
    if (exists(destination) && read_small(destination,2048) != recovery)
        throw std::runtime_error("A different recovery record already exists; inspect the library.");
    // Old RomFS and CodeFS plans can reopen these exact paths after evicting a
    // file handle. Keep payloads in place until their current-boot owners exit.
    if (!exists(destination)) atomic_write(destination,recovery);
    atomic_write(path + "/.removed",std::to_string(generation_ + 1));
    publish(); event("Removed from library: " + title + '/' + id);
}
std::string Library::snapshot_json() {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto status = backend_.status();
    std::string out = "{\"generation\":" + std::to_string(generation_) +
        ",\"runtime\":{\"connected\":" + (status.connected ? "true" : "false") +
        ",\"backend\":" + json_string(status.backend) + ",\"detail\":" + json_string(status.detail) +
        ",\"result\":" + std::to_string(status.result) + ",\"appliedGeneration\":" + std::to_string(status.applied_generation) + "},\"mods\":[";
    const auto mods = scan(); bool first = true;
    for (const auto& mod : mods) { if (!first) out += ','; first = false; out += mod_json(mod); }
    out += "],\"settings\":" + preferences_json(preferences_) + ",\"gameNames\":{";
    std::map<std::string, std::string> names;
    for (const auto& mod : mods) if (!names.count(mod.title_id)) names.emplace(mod.title_id, game_name_locked(mod.title_id));
    first = true;
    for (const auto& name : names) if (!name.second.empty()) {
        if (!first) out += ',';
        first = false;
        out += json_string(name.first) + ':' + json_string(name.second);
    }
    return out + "}}";
}
std::vector<std::string> Library::diagnostic_lines() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto lines = events_;
    const auto native = backend_.events(); lines.insert(lines.end(),native.begin(),native.end());
    return lines;
}
std::string Library::events_json() {
    std::string out = "["; bool first = true;
    for (const auto& line : diagnostic_lines()) { if (!first) out += ','; first = false; out += json_string(line); }
    return out + ']';
}
}
