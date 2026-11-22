#include "update.hpp"
#include "bounded_json.hpp"
#include "sha256.hpp"
#include "third_party/picojson.h"
#include "moader/library.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace moader_ui {
namespace {
constexpr size_t ManifestLimit = 32 * 1024;
using Version = std::array<unsigned, 3>;
using File = std::unique_ptr<FILE, decltype(&std::fclose)>;
std::optional<Version> version_parts(const std::string& text) {
    Version parts{};
    const char* start = text.data();
    const char* end = start + text.size();
    for (unsigned i = 0; i < parts.size(); ++i) {
        const auto [next, ec] = std::from_chars(start, end, parts[i]);
        if (ec != std::errc{} || (next - start > 1 && *start == '0')) return {};
        if (i + 1 == parts.size()) return next == end ? std::optional(parts) : std::nullopt;
        if (next == end || *next != '.') return {};
        start = next + 1;
    }
    return {};
}
bool present(const std::string& path) {
    struct stat st{};
    if (!lstat(path.c_str(), &st)) {
        if (!S_ISREG(st.st_mode)) throw std::runtime_error("An update path is occupied by another file type.");
        return true;
    }
    if (errno != ENOENT) throw std::runtime_error("Could not read the SD card.");
    return false;
}
void remove_if_present(const std::string& path) {
    if (unlink(path.c_str()) && errno != ENOENT) throw std::runtime_error("Could not clean up the update on the SD card.");
}
std::string read_small(const std::string& path, size_t limit) {
    File f(std::fopen(path.c_str(), "rb"), std::fclose);
    if (!f) throw std::runtime_error("Could not open the update settings on the SD card.");
    std::string bytes(limit + 1, '\0');
    const auto n = std::fread(bytes.data(), 1, bytes.size(), f.get());
    if (n > limit || std::ferror(f.get())) throw std::runtime_error("Could not read the update settings on the SD card.");
    bytes.resize(n);
    return bytes;
}
picojson::value parse_object(const std::string& bytes, size_t limit) {
    picojson::value root;
    std::string error;
    if (bounded_json(bytes, limit)) {
        const auto end = picojson::parse(root, bytes.begin(), bytes.end(), &error);
        if (error.empty() && root.is<picojson::object>() &&
            std::all_of(end, bytes.end(), [](char c){ return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }))
            return root;
    }
    throw std::runtime_error("Moader could not read the release information.");
}
bool valid_app_path(const std::string& path) {
    if (!path.starts_with('/') || !path.ends_with(".nro") || path.size() > 650 ||
        path.ends_with(".moader-previous.nro") || path.find("//") != path.npos ||
        path.find("/../") != path.npos || path.find("/./") != path.npos) return false;
    return std::none_of(path.begin(), path.end(), [](unsigned char c) {
        return c < 32 || c == 127 || c == ':' || c == '\\';
    });
}
bool matches(const std::string& path, const UpdateAsset& asset, bool plugin) {
    if (!present(path)) return false;
    File f(std::fopen(path.c_str(), "rb"), std::fclose);
    if (!f) throw std::runtime_error("Could not read an update file on the SD card.");
    char buffer[16384];
    size_t total = 0;
    Sha256 hash;
    while (const auto n = std::fread(buffer, 1, sizeof(buffer), f.get())) {
        if (!total && (n < 20 || std::memcmp(buffer + (plugin ? 0 : 16), plugin ? "NSO0" : "NRO0", 4))) return false;
        total += n;
        if (total > asset.bytes) return false;
        hash.update(buffer, n);
    }
    if (std::ferror(f.get())) throw std::runtime_error("Could not verify a file on the SD card.");
    return total == asset.bytes && hash.finish() == asset.sha256;
}
std::string manifest_from_github_release(const picojson::value& root) {
    using Value = picojson::value;
    if (root.get("draft") != Value(false) || root.get("prerelease") != Value(false) ||
        !root.get("tag_name").is<std::string>() || !root.get("assets").is<picojson::array>())
        throw std::runtime_error("GitHub has no stable Moader release available.");
    const auto tag = root.get("tag_name").get<std::string>();
    if (!tag.starts_with('v') || !version_parts(tag.substr(1))) throw std::runtime_error("Unrecognized release tag.");
    picojson::object assets;
    for (const auto& item : root.get("assets").get<picojson::array>()) {
        if (!item.is<picojson::object>() || !item.get("name").is<std::string>()) continue;
        const auto& name = item.get("name").get<std::string>();
        if (name != "Moader.nro" && name != "moader.nso") continue;
        if (assets.count(name) || !item.get("digest").is<std::string>() ||
            !item.get("digest").get<std::string>().starts_with("sha256:"))
            throw std::runtime_error("The release does not supply both verified downloads.");
        assets[name] = Value(picojson::object{{"bytes", item.get("size")},
            {"sha256", Value(item.get("digest").get<std::string>().substr(7))}});
    }
    return Value(picojson::object{{"schema", Value(1.0)}, {"version", Value(tag.substr(1))},
        {"tag", Value(tag)}, {"prerelease", Value(false)}, {"repository", Value(std::string("H644b/Moader"))},
        {"assets", Value(assets)}}).serialize();
}
} // namespace

int compare_release_version(const std::string& stable, const std::string& running) {
    const auto suffix = running.find_first_of("-+");
    const auto a = version_parts(stable), b = version_parts(running.substr(0, suffix));
    if (!a || !b) throw std::runtime_error("The release version could not be read.");
    if (*a != *b) return *a > *b ? 1 : -1;
    return suffix != running.npos && running[suffix] == '-' ? 1 : 0;
}
AppRelease parse_app_release(const std::string& bytes) {
    const auto root = parse_object(bytes, 256 * 1024);
    if (!root.contains("schema") && root.contains("tag_name"))
        return parse_app_release(manifest_from_github_release(root));
    if (bytes.size() > ManifestLimit) throw std::runtime_error("The release manifest is too large.");
    if (root.get("schema") != picojson::value(1.0) ||
        root.get("repository") != picojson::value(std::string("H644b/Moader")) ||
        root.get("prerelease") != picojson::value(false) || !root.get("version").is<std::string>())
        throw std::runtime_error("The download is missing a stable Moader release manifest.");
    const auto version = root.get("version").get<std::string>();
    if (!version_parts(version) || root.get("tag") != picojson::value("v" + version) ||
        !root.get("assets").is<picojson::object>()) throw std::runtime_error("The release metadata is incomplete.");
    auto asset = [&](const char* name, size_t limit) -> UpdateAsset {
        const auto& entry = root.get("assets").get(name);
        if (entry.is<picojson::object>() && entry.get("bytes").is<double>() && entry.get("sha256").is<std::string>()) {
            const auto size = entry.get("bytes").get<double>();
            const auto& digest = entry.get("sha256").get<std::string>();
            if (size >= 32 && size <= limit && std::floor(size) == size && digest.size() == 64 &&
                digest.find_first_not_of("0123456789abcdef") == digest.npos) return {size_t(size), digest};
        }
        throw std::runtime_error(std::string("The release has no verified ") + name + ".");
    };
    return {version, bytes, asset("Moader.nro", 32 * 1024 * 1024), asset("moader.nso", 16 * 1024 * 1024)};
}

UpdateFiles::UpdateFiles(std::string sd_root, const std::string& launched)
    : sd(std::move(sd_root)), directory(sd + "/moader/updates"), pending(directory + "/pending") {
    auto path = launched.starts_with("sdmc:") ? launched.substr(5) : launched;
    if (path.starts_with("switch/") || path.starts_with("kaihen/")) path.insert(0, "/");
    for (auto pos = path.find("//"); pos != path.npos; pos = path.find("//", pos)) path.erase(pos, 1);
    if (valid_app_path(path)) app_path = path;
    else find_launch_path = launched.empty() || launched == "Moader.nro" || launched.starts_with("nxlink") ||
                            path.ends_with(".moader-previous.nro");
}
std::string UpdateFiles::target(bool plugin) const { return sd + (plugin ? "/kaihen/plugins/moader.nso" : app_path); }
std::string UpdateFiles::stage(bool plugin) const { return directory + (plugin ? "/plugin.new" : "/app.new"); }
std::string UpdateFiles::backup(bool plugin) const {
    return plugin ? directory + "/plugin.previous" : target(false) + ".moader-previous.nro";
}
bool UpdateFiles::automatic() const {
    const auto path = directory + "/automatic";
    moader::recover_atomic_file(path);
    return present(path) && read_small(path, 2) == "1";
}
void UpdateFiles::set_automatic(bool on) const {
    moader::make_directories(directory);
    moader::atomic_write(directory + "/automatic", on ? "1" : "0");
}
bool UpdateFiles::recover(AppRelease& release) {
    moader::recover_atomic_file(pending);
    if (!present(pending)) return false;
    const auto data = read_small(pending, ManifestLimit + 652);
    const auto split = data.find('\n');
    const auto path = data.substr(0, split);
    if (split == data.npos || !valid_app_path(path)) throw std::runtime_error("The saved update path is invalid.");
    release = parse_app_release(data.substr(split + 1));
    app_path = path;
    return true;
}
void UpdateFiles::download(const AppRelease& release, const std::atomic<bool>& cancel,
                           std::atomic<uint64_t>& received) {
    if (find_launch_path) {
        app_path.clear();
        for (const char* p : {"/switch/Moader.nro", "/kaihen/programs/Moader.nro"}) {
            if (!present(sd + p)) continue;
            if (!app_path.empty()) { app_path.clear(); break; }
            app_path = p;
        }
    }
    if (app_path.empty() || !present(target(false)))
        throw std::runtime_error("Open Moader from its NRO on the SD card, then try again.");
    if (!present(target(true))) throw std::runtime_error("Install and start the resident Moader plugin first.");
    moader::make_directories(directory);
    if (present(pending) || present(backup(false)) || present(backup(true)))
        throw std::runtime_error("A previous update needs recovery. Reopen Moader to continue.");
    for (bool plugin : {false, true}) {
        remove_if_present(stage(plugin));
        download_app_asset(release, plugin, stage(plugin), cancel, received);
    }
}
void UpdateFiles::prepare(const AppRelease& release) const {
    for (bool plugin : {false, true})
        if (!matches(stage(plugin), plugin ? release.plugin : release.app, plugin))
            throw std::runtime_error("The downloaded files failed verification. Check for updates and try again.");
    moader::atomic_write(pending, app_path + "\n" + release.manifest);
}
void UpdateFiles::repair(const AppRelease& release, const std::atomic<bool>& cancel,
                         std::atomic<uint64_t>& received) const {
    for (bool plugin : {false, true}) {
        const auto& asset = plugin ? release.plugin : release.app;
        if (matches(target(plugin), asset, plugin) || matches(stage(plugin), asset, plugin)) continue;
        remove_if_present(stage(plugin));
        download_app_asset(release, plugin, stage(plugin), cancel, received);
    }
}
void UpdateFiles::install(const AppRelease& release) const {
    // Both an interrupted rename and a fully installed pair can be resumed offline.
    std::array<bool, 2> installed{};
    for (bool plugin : {false, true}) {
        const auto& asset = plugin ? release.plugin : release.app;
        installed[plugin] = matches(target(plugin), asset, plugin);
        if (!installed[plugin] && !matches(stage(plugin), asset, plugin))
            throw std::runtime_error("A staged file is missing or damaged. Restore both files from the official release.");
    }
    bool plugin_replaced = false;
    try {
        for (bool plugin : {true, false}) {
            if (installed[plugin]) continue;
            const auto dest = target(plugin), old = backup(plugin), next = stage(plugin);
            if (present(dest) && (present(old) || std::rename(dest.c_str(), old.c_str())))
                throw std::runtime_error("Could not move the previous file on the SD card. Retry the update.");
            if (std::rename(next.c_str(), dest.c_str())) {
                if (present(old)) std::rename(old.c_str(), dest.c_str());
                throw std::runtime_error("Could not install the downloaded file. Check the SD card and retry.");
            }
            if (plugin) plugin_replaced = true;
        }
    } catch (...) {
        // If the app was locked, restore the original pair while retaining both verified downloads.
        if (plugin_replaced && present(backup(true)) && !std::rename(target(true).c_str(), stage(true).c_str())) {
            if (std::rename(backup(true).c_str(), target(true).c_str()))
                std::rename(stage(true).c_str(), target(true).c_str());
        }
        throw;
    }
}
void UpdateFiles::discard_uncommitted() const {
    if (present(pending)) return;
    remove_if_present(stage(false));
    remove_if_present(stage(true));
}
void UpdateFiles::retire_superseded() const {
    // A newer app was opened manually. Preserve the last usable filename if power
    // was lost between the two renames of the older transaction.
    for (bool plugin : {false, true}) {
        if (!present(target(plugin)) && std::rename(backup(plugin).c_str(), target(plugin).c_str()))
            throw std::runtime_error("Restore the missing app or plugin on the SD card, then reopen Moader.");
    }
    finish();
}
void UpdateFiles::finish() const {
    for (bool plugin : {false, true}) {
        remove_if_present(stage(plugin));
        remove_if_present(backup(plugin));
    }
    remove_if_present(pending);
}
} // namespace moader_ui
