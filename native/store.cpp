#include "store.hpp"
#include "ui.hpp"
#include "third_party/picojson.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <tuple>
#include <sstream>
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
    if (!s.starts_with("https://") || s.size() > 2048 ||
        std::any_of(s.begin(), s.end(), [](unsigned char c) { return c <= 32 || c == 127 || c == '\\'; }))
        return false;
    auto authority = s.substr(8, s.find_first_of("/?#", 8) - 8);
    return !authority.empty() && authority.find('@') == authority.npos;
}
std::string folded(std::string value) {
    for (char& c : value)
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    return value;
}
Package package_for(const Object& m, const Release& release) {
    const auto& d = m.get("download");
    if (!d.is<picojson::object>() || release.id.size() > 48 ||
        release.id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos ||
        release.title.size() > 120 ||
        std::any_of(release.title.begin(), release.title.end(),
                    [](unsigned char c) { return c < 32 || c == 127; }))
        return {};
    auto url = field(d, "url"), hash = folded(field(d, "sha256", 64));
    const auto& length = d.get("bytes");
    if (!https_url(url) || hash.size() != 64 || hash.find_first_not_of("0123456789abcdef") != hash.npos ||
        !length.is<double>())
        return {};
    double n = length.get<double>();
    if (!std::isfinite(n) || n != std::floor(n) || n < 22 || n > StorePackageLimit)
        return {};
    return {std::move(url), std::move(hash), static_cast<uint64_t>(n)};
}
unsigned step(unsigned v, int d, unsigned n) {
    return n ? unsigned((int(v) + d + int(n)) % int(n)) : 0;
}
} // namespace
bool Store::load_catalog(const std::string& json, bool hosted_only) {
    if (!bounded_json(json))
        return false;
    Object root;
    std::string error;
    const auto end = picojson::parse(root, json.begin(), json.end(), &error);
    if (!error.empty() || !std::all_of(end, json.end(), [](unsigned char c) { return std::isspace(c); }) ||
        !root.is<picojson::object>() || field(root, "format") != "moader.catalog.v1")
        return false;
    auto cursor = field(root, "next");
    if (!cursor.empty()) {
        const auto prefix = std::string(StoreFeed) + "?after=";
        if (!cursor.starts_with(prefix) || cursor.size() != prefix.size() + 36 ||
            cursor.find_first_not_of("0123456789abcdef-", prefix.size()) != cursor.npos)
            return false;
    }
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
                      {},
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
            r.package = package_for(m, r);
            if (hosted_only) {
                if (!r.package.url.starts_with(StoreFiles)) r.package = {};
                std::erase_if(r.previews, [](const std::string& link) { return !link.starts_with(StoreFiles); });
            }
            r.license = field(m, "license", 100);
            if (std::any_of(g.mods.begin(), g.mods.end(), [&](const Release& old) { return old.id == r.id; }))
                return false;
            g.mods.push_back(std::move(r));
        }
        next.push_back(std::move(g));
    }
    next_page = std::move(cursor);
    games = std::move(next);
    page = Shelf::Browse;
    row = frame = 0;
    results.clear();
    rebuild_results();
    return true;
}
bool Store::merge_catalog(const std::string& json, bool hosted_only) {
    Store incoming;
    if (!incoming.load_catalog(json, hosted_only))
        return false;
    next_page = std::move(incoming.next_page);
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
    rebuild_results();
    return true;
}
void Store::start() {
    *this = Store{};
    preview_queue.clear();
    preview_cache.clear();
    fetching.clear();
}
void Store::rebuild_results() {
    StoreEntry current = row < results.size() ? results[row] : StoreEntry{~0u, ~0u};
    results.clear();
    std::istringstream search(folded(query));
    std::vector<std::string> terms;
    for (std::string term; search >> term;)
        terms.push_back(std::move(term));
    for (unsigned g = 0; g < games.size(); ++g) {
        const auto& group = games[g];
        if (owned && group.presence != Presence::Present)
            continue;
        const std::string game_name = folded(group.title);
        for (unsigned i = 0; i < group.mods.size(); ++i) {
            const auto& m = group.mods[i];
            if (!terms.empty()) {
                const auto text =
                    game_name + "\n" + folded(m.title) + "\n" + folded(m.author) + "\n" + folded(m.category);
                if (!std::all_of(terms.begin(), terms.end(),
                                 [&](const auto& word) { return text.find(word) != text.npos; }))
                    continue;
            }
            results.push_back({g, i});
        }
    }
    std::stable_sort(results.begin(), results.end(), [&](StoreEntry a, StoreEntry b) {
        const auto& ga = games[a.game];
        const auto& gb = games[b.game];
        return std::tie(ga.title, ga.mods[a.release].title) < std::tie(gb.title, gb.mods[b.release].title);
    });
    auto it = std::find(results.begin(), results.end(), current);
    row = it == results.end() ? 0 : unsigned(it - results.begin());
}
void Store::set_query(std::string text) {
    auto first = text.find_first_not_of(" \t\r\n");
    query = first == text.npos ? "" : text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
    results.clear();
    rebuild_results();
    focus = StoreFocus::Results;
}
void Store::set_owned(bool enable) {
    if (owned == enable)
        return;
    owned = enable;
    if (owned) {
        ++inventory_revision;
        for (auto& g : games)
            if (g.presence != Presence::Present) g.presence = Presence::Unchecked;
    }
    results.clear();
    rebuild_results();
}
bool Store::back() {
    if (page == Shelf::Browse)
        return true;
    page = Shelf::Browse;
    focus = StoreFocus::Results;
    return false;
}
uint32_t Store::input(uint32_t b) {
    if (page == Shelf::Release) {
        const auto& r = games[detail.game].mods[detail.release];
        if (b & (LeftInput | RightInput))
            frame = step(frame, b & LeftInput ? -1 : 1, r.previews.size());
        return (b & ActivateInput) && r.package.bytes ? uint32_t(InstallStoreMod) : 0u;
    }
    if (b & RemoveInput)
        return SearchStore;
    if (b & NextInput) {
        set_owned(!owned);
        return 0;
    }
    if (focus != StoreFocus::Results) {
        if (b & (LeftInput | RightInput))
            focus = focus == StoreFocus::Search ? StoreFocus::Filter : StoreFocus::Search;
        if (b & DownInput)
            focus = StoreFocus::Results;
        if (b & ActivateInput) {
            if (focus == StoreFocus::Search)
                return SearchStore;
            if (focus == StoreFocus::Filter)
                set_owned(!owned);
        }
        return 0;
    }
    if (b & UpInput) {
        if (row)
            --row;
        else
            focus = StoreFocus::Search;
    }
    if ((b & DownInput) && row + 1 < results.size())
        ++row;
    if ((b & LeftInput) && row >= 4)
        row -= 4;
    if ((b & RightInput) && !results.empty())
        row = std::min<unsigned>(row + 4, results.size() - 1);
    if ((b & ActivateInput) && row < results.size()) {
        detail = results[row];
        frame = description_first = 0;
        page = Shelf::Release;
    }
    return 0;
}
Art Store::preview(const std::string& url) {
    for (const auto& p : preview_cache)
        if (p.first == url)
            return p.second;
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
