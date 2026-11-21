#pragma once
#include "artwork.hpp"
#include <string>
#include <vector>

namespace moader_ui {
inline constexpr bool StoreAvailable = true;
struct SourceLink {
    std::string label, url;
};
struct Release {
    std::string id, title, author, category, summary, description, version, published;
    std::vector<SourceLink> sources;
    std::vector<std::string> previews;
};
struct StoreGame {
    std::string id, title;
    std::vector<Release> mods;
};
enum class Shelf { Discover, Category, Hosts, Host, Games, Release };
enum class CatalogStatus { Loading, Ready, Unavailable };
struct Store {
    std::vector<StoreGame> games;
    unsigned game = 0, row = 0, tab = 0, release = 0, frame = 0, mirror = 0;
    Shelf page = Shelf::Discover, origin = Shelf::Discover, picker_origin = Shelf::Discover;
    std::string host, preferred_title;
    CatalogStatus catalog_status = CatalogStatus::Loading;
    bool catalog_failed = false;
    bool load_catalog(const std::string& json);
    bool merge_catalog(const std::string& json);
    void start(uint64_t preferred);
    std::vector<std::string> tabs() const;
    std::vector<std::string> hosts() const;
    std::vector<unsigned> matches() const;
    void select_tab(unsigned index);
    bool back();
    bool input(uint32_t buttons);
    std::string host_url() const;
    static Art preview(const std::string& url);
    static std::string next_preview();
    static void finish_preview(const std::string& url, Art art);
};
} // namespace moader_ui
