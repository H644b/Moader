#pragma once
#include "artwork.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace moader_ui {
inline constexpr bool StoreAvailable = true;
inline constexpr char StoreFeed[] = "https://moader.timsfun.com/catalog.json";
inline constexpr char StoreFiles[] = "https://moader.timsfun.com/files/";
inline constexpr uint64_t StorePackageLimit = 128ull << 20;
struct SourceLink {
    std::string label, url;
};
struct Package {
    std::string url, sha256;
    uint64_t bytes = 0;
};
struct Release {
    std::string id, title, author, category, summary, description, version, published;
    std::vector<SourceLink> sources;
    std::vector<std::string> previews;
    Package package;
    std::string license;
};
enum class Presence { Unchecked, Present, Absent, Unknown };
struct StoreGame {
    std::string id, title;
    std::vector<Release> mods;
    Presence presence = Presence::Unchecked;
};
enum class Shelf { Browse, Release };
enum class CatalogStatus { Loading, Ready, Unavailable };
enum class StoreFocus { Results, Search, Filter };
struct StoreEntry {
    unsigned game = 0, release = 0;
    bool operator==(const StoreEntry&) const = default;
};
struct Store {
    std::vector<StoreGame> games;
    std::vector<StoreEntry> results;
    unsigned row = 0, frame = 0, description_first = 0, inventory_revision = 0;
    StoreEntry detail;
    Shelf page = Shelf::Browse;
    StoreFocus focus = StoreFocus::Results;
    std::string query, next_page;
    CatalogStatus catalog_status = CatalogStatus::Loading;
    bool catalog_failed = false, owned = false;
    bool load_catalog(const std::string& json, bool hosted_only = false);
    bool merge_catalog(const std::string& json, bool hosted_only = false);
    void start();
    void rebuild_results();
    void set_query(std::string);
    void set_owned(bool);
    bool back();
    uint32_t input(uint32_t buttons);
    static Art preview(const std::string& url);
    static std::string next_preview();
    static void finish_preview(const std::string& url, Art art);
};
enum class InstallPhase { Idle, Downloading, Installing, Installed, Failed };
struct StoreTransfer {
    InstallPhase phase = InstallPhase::Idle;
    std::string game, id, message;
    uint64_t received = 0, total = 0;
    bool busy() const { return phase == InstallPhase::Downloading || phase == InstallPhase::Installing; }
};
} // namespace moader_ui
