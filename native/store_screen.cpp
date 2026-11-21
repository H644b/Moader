#include "canvas.hpp"
#include <algorithm>

namespace moader_ui {
namespace {
namespace icon = moader::brand;

void picture(Canvas& c, const Release& mod, unsigned index, float x, float y, float w, float h,
             bool contain) {
    auto art = index < mod.previews.size() ? Store::preview(mod.previews[index]) : Art{};
    c.rect(x, y, w, h, panel, 10);
    if (art)
        c.image(art, x, y, w, h, 10, 0, contain);
    else {
        float side = std::min(54.f, h * .48f);
        c.symbol(icon::layers, x + (w - side) / 2, y + (h - side) / 2, side, muted);
    }
}
void footer(Canvas& c, bool browse, const std::string& primary) {
    if (browse) {
        c.key(80, "X", "Search");
        c.hit(58, 742, 140, 63, HitType::Key, RemoveInput);
        c.key(234, "R", "Game filter");
        c.hit(213, 742, 170, 63, HitType::Key, NextInput);
    }
    const float action = 1376 - c.width(primary, 21, Strong) - 24;
    const auto back = browse ? "Library" : "Back";
    const float prev = (primary.empty() ? 1376 : action - 48) - c.width(back, 21) - 25;
    c.key(prev, "B", back);
    c.hit(prev - 20, 742, c.width(back, 21) + 50, 63, HitType::Key, BackInput);
    if (!primary.empty()) {
        c.key(action, "A", primary, true);
        c.hit(action - 20, 742, 1396 - action, 63, HitType::Key, ActivateInput);
    }
}
void toolbar(Canvas& c, const Store& s) {
    c.text(64, 121, "Mod store", 51, paper, Display, 430, Left, -1.55);
    const bool search = s.focus == StoreFocus::Search, filter = s.focus == StoreFocus::Filter;
    c.box(532, 112, 533, 62, 9, panel, search ? mint : rule, search ? 2.5f : 1.3f);
    c.ring(554, 130, 19, 19, 10, search ? mint : soft, 2);
    for (int i = 0; i != 8; ++i)
        c.rect(569 + i, 145 + i, 2.7, 2.7, search ? mint : soft);
    c.text(595, 131, s.query.empty() ? "Search mods" : s.query, 22, s.query.empty() ? soft : paper, Text,
           s.query.empty() ? 438 : 395);
    c.hit(532, 112, 533, 62, HitType::Key, RemoveInput);
    if (!s.query.empty()) {
        c.text(1035, 128, "×", 27, soft, Strong, 32, Center);
        c.hit(1007, 112, 58, 62, HitType::StoreClear);
    }
    c.box(1081, 112, 295, 62, 9, s.owned ? selected : panel, filter || s.owned ? mint : rule,
          filter ? 2.5f : 1.3f);
    c.symbol(icon::collection, 1100, 130, 26, s.owned ? mint : soft);
    c.text(1140, 132, "On this Switch", 21, s.owned ? mint : paper, Strong, 178);
    c.rect(1314, 133, 42, 22, s.owned ? mint : dim, 11);
    c.rect(1317 + (s.owned ? 19 : 0), 136, 16, 16, base, 8);
    c.hit(1081, 112, 295, 62, HitType::Key, NextInput);
}
std::string mod_count(unsigned count) {
    return std::to_string(count) + (count == 1 ? " mod" : " mods");
}
void browse(Canvas& c, const State& state) {
    const auto& s = state.store;
    toolbar(c, s);
    const bool checking = s.owned && std::any_of(s.games.begin(), s.games.end(), [](const StoreGame& g) {
                              return g.presence == Presence::Unchecked;
                          });
    const bool unknown = s.owned && std::any_of(s.games.begin(), s.games.end(), [](const StoreGame& g) {
                             return g.presence == Presence::Unknown;
                         });
    const auto count = unsigned(s.results.size());
    if (!count) {
        const char* title = "No mods in this catalog";
        const char* hint = "Published mods will appear here.";
        if (s.catalog_status == CatalogStatus::Unavailable) {
            title = "The catalog is offline";
            hint = "Check your connection and reopen the Store.";
        } else if (s.catalog_status == CatalogStatus::Loading || checking) {
            title = checking ? "Checking games on this Switch…" : "Loading the catalog…";
            hint = checking ? "Checking installed games and the inserted game card."
                            : "Getting releases from community hosts.";
        } else if (!s.query.empty()) {
            title = "No matching mods";
            hint = unknown ? "Some games could not be checked. Try browsing with the game filter off."
                           : "Try another title, game, creator or category.";
        } else if (s.owned) {
            title = unknown ? "Some games could not be checked" : "No mods for games on this Switch";
            hint = "Turn off the game filter to browse the full catalog.";
        }
        c.rect(64, 248, 1312, 1, rule);
        c.symbol(icon::bag, 67, 308, 62, mint);
        c.text(64, 396, title, 39, paper, Display, 1240, Left, -1);
        c.text(65, 463, hint, 23, soft, Text, 1215);
        std::string action = s.focus == StoreFocus::Search   ? "Search"
                             : s.focus == StoreFocus::Filter ? "Filter"
                                                             : "";
        footer(c, true, action);
        return;
    }
    auto chosen = s.results[s.row];
    const auto& group = s.games[chosen.game];
    const auto& selected_mod = group.mods[chosen.release];
    picture(c, selected_mod, 0, 64, 211, 706, 332, false);
    c.text(64, 565, group.title, 21, mint, Strong, 705);
    c.text(64, 605, selected_mod.title, 41, paper, Display, 702, Left, -.9);
    c.paragraph(65, 658, selected_mod.summary, 20, 699, 2, soft, 26);
    c.rect(796, 211, 1, 498, rule);
    const auto heading = s.query.empty() ? (s.owned ? "On this Switch" : "All mods") : "Search results";
    c.text(826, 214, heading, 29, paper, Strong, 362);
    c.text(1376, 222, mod_count(count), 18, muted, Text, 162, Right);
    const unsigned first = s.row / 4 * 4;
    for (unsigned i = first; i < std::min(count, first + 4); ++i) {
        auto item = s.results[i];
        const auto& g = s.games[item.game];
        const auto& m = g.mods[item.release];
        const float y = 265 + (i - first) * 98;
        const bool hot = i == s.row && s.focus == StoreFocus::Results;
        if (hot)
            c.box(822, y, 554, 86, 12, selected, mint, 2);
        else
            c.rect(838, y + 85, 526, 1, rule);
        picture(c, m, 0, 837, y + 12, 91, 61, false);
        c.text(945, y + 14, m.title, 23, hot ? paper : soft, Strong, 390);
        c.text(945, y + 48, g.title, 18, muted, Text, 390);
        if (hot)
            c.arrow(1350, y + 42, 1, 7, mint);
        c.hit(822, y, 554, 86, HitType::StoreItem, i);
    }
    if (count > 4)
        c.text(1374, 676,
               std::to_string(first + 1) + "–" + std::to_string(std::min(count, first + 4)) + " of " +
                   std::to_string(count),
               17, muted, Text, 200, Right);
    if (checking || unknown)
        c.text(827, 677, checking ? "Checking installed games…" : "Some games could not be checked.", 17,
               muted, Text, 361);
    std::string action = s.focus == StoreFocus::Search   ? "Search"
                         : s.focus == StoreFocus::Filter ? "Filter"
                                                         : "View mod";
    footer(c, true, action);
    if (state.transfer.busy()) {
        const auto& t = state.transfer;
        const auto percent = t.total ? 100 * t.received / t.total : 0;
        c.text(470, 764,
               t.phase == InstallPhase::Installing ? "Installing mod…"
                                                   : "Downloading  " + std::to_string(percent) + "%",
               18, mint, Strong, 390);
    }
}
struct Description {
    std::vector<std::string> lines;
    unsigned visible;
    unsigned limit() const { return lines.size() > visible ? unsigned(lines.size()) - visible : 0; }
};
Description description(Canvas& c, const Release& m, unsigned title_lines) {
    return {c.wrap(m.description.empty() ? m.summary : m.description, 21, 557), title_lines > 1 ? 3u : 5u};
}
void details(Canvas& c, const State& s) {
    const auto& st = s.store;
    const auto& g = st.games[st.detail.game];
    const auto& m = g.mods[st.detail.release];
    c.text(64, 128, g.title, 30, paper, Strong, 935);
    c.text(1376, 136, m.category, 22, mint, Strong, 365, Right);
    c.rect(64, 183, 1312, 1, rule);
    picture(c, m, st.frame, 64, 213, 712, 401, true);
    if (m.previews.size() > 1) {
        c.text(64, 642,
               "Preview " + std::to_string(st.frame + 1) + " of " + std::to_string(m.previews.size()), 19,
               soft, Strong);
        c.arrow(684, 654, -1, 8);
        c.arrow(752, 654, 1, 8);
        c.hit(651, 621, 60, 63, HitType::Key, LeftInput);
        c.hit(717, 621, 59, 63, HitType::Key, RightInput);
    }
    c.text(64, 676, "Hosted by " + m.sources.front().label, 17, muted, Text, 500);
    if (!m.published.empty())
        c.text(776, 676, m.published, 17, muted, Text, 186, Right);
    auto lines = c.wrap(m.title, 42, 553, Display);
    unsigned n = std::min<unsigned>(2, lines.size());
    for (unsigned i = 0; i < n; ++i)
        c.text(814, 213 + 48 * i, lines[i], 42, paper, Display, 559, Left, -1.1);
    float y = 222 + n * 48;
    c.text(815, y, "by " + m.author, 22, mint, Strong, 558);
    auto desc = description(c, m, n);
    unsigned first = std::min(st.description_first, desc.limit());
    for (unsigned i = first; i < std::min<unsigned>(desc.lines.size(), first + desc.visible); ++i)
        c.text(815, y + 43 + (i - first) * 27, desc.lines[i], 21, soft, Text, 557);
    if (desc.limit()) {
        c.text(1376, 437, "Scroll for details", 16, muted, Text, 220, Right);
        c.hit(815, 420, 275, 36, HitType::Key, UpInput);
        c.hit(1095, 420, 281, 36, HitType::Key, DownInput);
    }
    c.rect(815, 459, 561, 1, rule);
    const std::string size = m.package.bytes ? size_text(m.package.bytes) : "Unavailable";
    c.text(815, 480, "Version", 17, muted, Strong);
    c.text(815, 507, m.version.empty() ? "—" : m.version, 21, paper, Strong, 169);
    c.text(999, 480, "Download", 17, muted, Strong);
    c.text(999, 507, size, 21, paper, Strong, 161);
    c.text(1172, 480, "License", 17, muted, Strong);
    c.text(1172, 507, m.license.empty() ? "See publisher" : m.license, 17, paper, Strong, 204);
    const auto& t = s.transfer;
    const bool same = t.game == g.id && t.id == m.id;
    const bool installed = s.has_store_mod(st.detail) || (same && t.phase == InstallPhase::Installed);
    const bool failed = same && t.phase == InstallPhase::Failed && !installed;
    const bool available =
        !installed && !t.busy() && m.package.bytes && s.connected && !s.inventory_unavailable();
    const std::string button =
        installed          ? "Installed"
        : t.busy()         ? (same ? (t.phase == InstallPhase::Installing ? "Installing…" : "Downloading…")
                                   : "Another download is in progress")
        : !m.package.bytes ? "Download unavailable"
        : !s.connected || s.inventory_unavailable() ? "Library unavailable"
        : failed                                    ? "Retry installation"
                                                    : "Download & install";
    c.box(815, 566, 561, 65, 10, available ? mint : panel, available ? mint : rule, 1.5);
    c.text(1095, 586, button, 23, available ? base : soft, Strong, 530, Center);
    if (available)
        c.hit(815, 566, 561, 65, HitType::Key, ActivateInput);
    if (same && t.busy()) {
        float fraction = t.total ? std::min(1., double(t.received) / double(t.total)) : 0;
        c.rect(815, 648, 561, 6, rule, 3);
        if (fraction > 0)
            c.rect(815, 648, 561 * fraction, 6, mint, 3);
        c.text(815, 671,
               t.phase == InstallPhase::Installing ? "Importing verified game files…"
                                                   : size_text(t.received) + " of " + size_text(t.total),
               18, soft);
    } else {
        std::string hint = installed      ? "Manage this mod in your Library."
                           : failed       ? t.message
                           : available    ? "Adds the mod to your Library, switched off."
                           : !s.connected ? "Connect to the Moader service to install."
                                          : "";
        c.paragraph(815, 653, hint, 18, 554, 2, failed ? bad : muted, 24);
    }
    footer(c, false, available ? "Install" : "");
}
} // namespace
unsigned store_scroll_limit(Canvas& c, const Store& s) {
    if (s.page != Shelf::Release)
        return 0;
    const auto& m = s.games[s.detail.game].mods[s.detail.release];
    return description(c, m, c.wrap(m.title, 42, 553, Display).size()).limit();
}
void store_screen(Canvas& c, const State& s) {
    if (s.store.page == Shelf::Release)
        details(c, s);
    else
        browse(c, s);
}
} // namespace moader_ui
