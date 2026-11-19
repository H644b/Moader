#include "store.hpp"
#include "ui.hpp"
#include "store_assets.hpp"
#include "third_party/picojson.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <deque>

namespace moader_ui {
namespace {
std::deque<std::pair<std::string, Art>> preview_cache;
std::deque<std::string> preview_queue;
std::string fetching;
bool bounded_json(const std::string& json) {
    if (json.size() > 1024 * 1024)
        return false;
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (char c : json) {
        if (quoted) {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
        } else if (c == '"')
            quoted = true;
        else if (c == '{' || c == '[') {
            if (++depth > 24)
                return false;
        } else if (c == '}' || c == ']') {
            if (depth == 0)
                return false;
            --depth;
        }
    }
    return !quoted && depth == 0;
}
using Object = picojson::value;
using Array = picojson::array;
std::string field(const Object& o, const char* k, size_t max = 2048) {
    const auto& v = o.get(k);
    return v.is<std::string>() && v.get<std::string>().size() <= max ? v.get<std::string>() : "";
}
const Array& array(const Object& o, const char* key) {
    static const Array none;
    const auto& v = o.get(key);
    return v.is<Array>() ? v.get<Array>() : none;
}
bool https_url(const std::string& s) {
    return s.starts_with("https://") && s.size() > 8 && s.size() < 2049 &&
           s.find_first_of(" \t\r\n\\") == s.npos;
}
template <class T> void unique_push(std::vector<T>& out, const T& v) {
    if (std::find(out.begin(), out.end(), v) == out.end())
        out.push_back(v);
}
unsigned step(unsigned v, int d, unsigned n) { return n ? unsigned((int(v) + d + int(n)) % int(n)) : 0; }
} // namespace
bool Store::load_catalog(const std::string& json) {
    if (!bounded_json(json))
        return false;
    Object root;
    std::string error;
    const auto end = picojson::parse(root, json.begin(), json.end(), &error);
    if (!error.empty() || !std::all_of(end, json.end(), [](unsigned char c) { return std::isspace(c); }) ||
        !root.is<picojson::object>() || field(root, "format") != "moader.catalog.v1")
        return false;
    std::vector<StoreGame> next;
    const auto& list = array(root, "games");
    if (list.size() > 256)
        return false;
    for (const auto& entry : list) {
        if (!entry.is<picojson::object>())
            return false;
        StoreGame g{field(entry, "titleId", 16), field(entry, "title", 200), {}};
        if (g.id.size() != 16 || (g.id.substr(0, 2) != "01" && g.id.substr(0, 2) != "04") ||
            !std::all_of(g.id.begin(), g.id.end(), [](unsigned char c) { return std::isxdigit(c); }) ||
            g.title.empty())
            return false;
        for (auto& c : g.id)
            c = std::toupper(static_cast<unsigned char>(c));
        if (std::any_of(next.begin(), next.end(), [&](const StoreGame& s) { return s.id == g.id; }))
            return false;
        if (array(entry, "mods").size() > 512)
            return false;
        for (const auto& m : array(entry, "mods")) {
            if (!m.is<picojson::object>())
                return false;
            Release r{field(m, "id", 100),
                      field(m, "title", 200),
                      field(m, "author", 250),
                      field(m, "category", 40),
                      field(m, "summary"),
                      field(m, "description", 4096),
                      field(m, "version", 64),
                      field(m, "published", 64),
                      {},
                      {}};
            if (r.title.empty() || r.author.empty() || r.category.empty() || r.id.empty())
                return false;
            for (const auto& s : array(m, "sources")) {
                if (!s.is<picojson::object>())
                    return false;
                auto url = field(s, "url"), label = field(s, "label", 120);
                if (!https_url(url) || label.empty() || r.sources.size() >= 12)
                    return false;
                r.sources.push_back({label, url});
            }
            if (r.sources.empty())
                return false;
            for (const auto& p : array(m, "previews")) {
                if (!p.is<std::string>() || !https_url(p.get<std::string>()) || r.previews.size() >= 12)
                    return false;
                r.previews.push_back(p.get<std::string>());
            }
            if (std::any_of(g.mods.begin(), g.mods.end(), [&](const Release& old) { return old.id == r.id; }))
                return false;
            g.mods.push_back(std::move(r));
        }
        next.push_back(std::move(g));
    }
    games = std::move(next);
    game = row = tab = release = frame = mirror = 0;
    page = origin = picker_origin = Shelf::Discover;
    host.clear();
    return true;
}
bool Store::merge_catalog(const std::string& json) {
    Store incoming;
    if (!incoming.load_catalog(json))
        return false;
    const auto old_tabs = tabs();
    std::string active = tab < old_tabs.size() ? old_tabs[tab] : "Discover";
    for (auto& item : incoming.games) {
        auto existing =
            std::find_if(games.begin(), games.end(), [&](const StoreGame& g) { return item.id == g.id; });
        if (existing == games.end()) {
            if (games.size() < 256)
                games.push_back(std::move(item));
            continue;
        }
        for (auto& incoming_mod : item.mods) {
            const auto repeated = [&](const Release& current) {
                return current.sources.front().url == incoming_mod.sources.front().url;
            };
            if (existing->mods.size() < 512 &&
                std::none_of(existing->mods.begin(), existing->mods.end(), repeated))
                existing->mods.push_back(std::move(incoming_mod));
        }
    }
    const auto now = tabs();
    auto current = std::find(now.begin(), now.end(), active);
    if (current != now.end())
        tab = current - now.begin();
    return true;
}
void Store::start(uint64_t preferred) {
    initialized = true;
    if (games.empty())
        load_catalog(store_assets::catalog);
    char id[17];
    std::snprintf(id, sizeof(id), "%016llX", (unsigned long long)preferred);
    for (unsigned i = 0; i < games.size(); ++i)
        if (games[i].id == id) {
            game = i;
            break;
        }
    select_tab(0);
}
std::vector<std::string> Store::tabs() const {
    std::vector<std::string> result{"Discover"};
    if (game < games.size())
        for (const auto& mod : games[game].mods)
            unique_push(result, mod.category);
    result.push_back("Hosts");
    return result;
}
std::vector<std::string> Store::hosts() const {
    std::vector<std::string> result;
    if (game < games.size())
        for (const auto& m : games[game].mods)
            for (const auto& l : m.sources)
                unique_push(result, l.label);
    return result;
}
std::vector<unsigned> Store::matches() const {
    std::vector<unsigned> result;
    if (game >= games.size())
        return result;
    const auto categories = tabs();
    const auto& mods = games[game].mods;
    for (unsigned i = 0; i < mods.size(); ++i) {
        if (page == Shelf::Category && (tab >= categories.size() || mods[i].category != categories[tab]))
            continue;
        if (page == Shelf::Host && !std::any_of(mods[i].sources.begin(), mods[i].sources.end(),
                                                [&](const SourceLink& s) { return s.label == host; }))
            continue;
        result.push_back(i);
    }
    return result;
}
void Store::select_tab(unsigned i) {
    const auto count = tabs().size();
    tab = std::min(i, unsigned(count - 1));
    page = tab == 0 ? Shelf::Discover : tab == count - 1 ? Shelf::Hosts : Shelf::Category;
    row = 0;
}
bool Store::back() {
    switch (page) {
    case Shelf::Release:
        page = origin;
        break;
    case Shelf::Games:
        page = picker_origin;
        row = 0;
        break;
    case Shelf::Host:
        page = Shelf::Hosts;
        row = 0;
        break;
    case Shelf::Category:
    case Shelf::Hosts:
        select_tab(0);
        break;
    default:
        return true;
    }
    return false;
}
bool Store::input(uint32_t b) {
    if (games.empty())
        return false;
    if (b & DetailInput) {
        if (page != Shelf::Games) {
            picker_origin = page == Shelf::Release ? origin : page;
            page = Shelf::Games;
            row = game;
        }
        return false;
    }
    if (page == Shelf::Games) {
        if (b & (LeftInput | UpInput))
            row = step(row, -1, games.size());
        if (b & (RightInput | DownInput))
            row = step(row, 1, games.size());
        if (b & ActivateInput) {
            game = row;
            select_tab(0);
        }
        return false;
    }
    if (page == Shelf::Release) {
        const auto& r = games[game].mods[release];
        if (b & LeftInput)
            frame = step(frame, -1, r.previews.size());
        if (b & RightInput)
            frame = step(frame, 1, r.previews.size());
        if (b & (UpInput | DownInput))
            mirror = step(mirror, (b & UpInput) ? -1 : 1, r.sources.size());
        return b & ActivateInput;
    }
    if (b & (PreviousInput | NextInput)) {
        select_tab(step(tab, b & PreviousInput ? -1 : 1, tabs().size()));
        return false;
    }
    const auto items = matches();
    const auto host_list = hosts();
    const unsigned count = page == Shelf::Hosts ? host_list.size() : items.size();
    if (!count)
        return false;
    if (b & (LeftInput | UpInput))
        row = step(row, -1, count);
    if (b & (RightInput | DownInput))
        row = step(row, 1, count);
    if (b & ActivateInput) {
        if (page == Shelf::Hosts) {
            host = host_list[row];
            page = Shelf::Host;
            row = 0;
        } else {
            release = items[row];
            origin = page;
            page = Shelf::Release;
            frame = mirror = 0;
            if (origin == Shelf::Host) {
                const auto& sources = games[game].mods[release].sources;
                for (unsigned i = 0; i < sources.size(); ++i)
                    if (sources[i].label == host)
                        mirror = i;
            }
        }
    }
    return false;
}
std::string Store::host_url() const {
    if (page != Shelf::Release || game >= games.size() || release >= games[game].mods.size())
        return {};
    const auto& links = games[game].mods[release].sources;
    return mirror < links.size() ? links[mirror].url : "";
}
Art Store::preview(const std::string& url) {
    for (const auto& p : preview_cache)
        if (p.first == url)
            return p.second;
    for (const auto& img : store_assets::images)
        if (img.url == url) {
            auto result = decode_art(img.bytes, img.length);
            finish_preview(url, result);
            return result;
        }
    if (url != fetching && preview_queue.size() < 16 &&
        std::find(preview_queue.begin(), preview_queue.end(), url) == preview_queue.end())
        preview_queue.push_back(url);
    return {};
}
std::string Store::next_preview() {
    if (preview_queue.empty())
        return {};
    fetching = std::move(preview_queue.front());
    preview_queue.pop_front();
    return fetching;
}
void Store::finish_preview(const std::string& url, Art art) {
    if (fetching == url)
        fetching.clear();
    if (preview_cache.size() >= 12)
        preview_cache.pop_front();
    preview_cache.emplace_back(url, std::move(art));
}
} // namespace moader_ui
