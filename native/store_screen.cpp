#include "canvas.hpp"
#include <algorithm>

namespace moader_ui {
namespace {
namespace icon = moader::brand;
const Art game_art(const State& s, const StoreGame& g) {
    for (const auto& item : s.games)
        if (hexid(item.id) == g.id)
            return item.art;
    return {};
}
void picture(Canvas& c, const Release& r, unsigned index, float x, float y, float w, float h,
             bool contain = false) {
    auto img = index < r.previews.size() ? Store::preview(r.previews[index]) : Art{};
    if (img)
        c.image(img, x, y, w, h, 10, 0, contain);
    else {
        c.rect(x, y, w, h, panel, 10);
        c.symbol(icon::layers, x + w / 2 - 26, y + h / 2 - 26, 52, mint);
    }
}
void game_cover(Canvas& c, const State& s, const StoreGame& g, float x, float y, float side) {
    if (auto art = game_art(s, g))
        c.image(art, x, y, side, side, 8);
    else {
        c.rect(x, y, side, side, selected, 8);
        c.symbol(icon::mark, x + side * .25f, y + side * .25f, side * .5f, mint);
    }
}
void store_button(Canvas& c, float x, float y, const std::string& label, bool focus, float maxw = 290) {
    float w = std::min(maxw, c.width(label, 19, Strong) + 38);
    c.box(x, y, w, 43, 9, focus ? mint : panel, focus ? mint : rule, 1.3);
    c.text(x + w / 2, y + 12, label, 19, focus ? base : paper, Strong, w - 23, Center);
}
void card(Canvas& c, const Release& r, unsigned index, float x, float y, float width, bool focus, bool large) {
    float h = large ? 316 : 129;
    c.box(x, y, width, h, 11, rgb(0x1b2621), focus ? mint : rule, focus ? 2 : 1);
    float iw = large ? width - 2 : 144, ih = large ? 192 : h - 2;
    picture(c, r, 0, x + 1, y + 1, iw, ih);
    float tx = large ? x + 16 : x + 160, ty = large ? y + 210 : y + 15, tw = large ? width - 32 : width - 177;
    auto lines = c.wrap(r.title, large ? 26 : 23, tw, Strong);
    for (unsigned i = 0; i < std::min(2u, unsigned(lines.size())); ++i)
        c.text(tx, ty + i * 26, lines[i], large ? 26 : 23, paper, Strong, tw);
    c.text(tx, large ? y + 271 : y + 75, "by " + r.author, 17, soft, Text, tw);
    c.text(tx, large ? y + 294 : y + 101, r.sources.front().label, 16, muted, Text, tw);
    c.hit(x, y, width, h, HitType::StoreItem, index);
}
void footer(Canvas& c, const std::string& back, const std::string& action, bool tabs) {
    if (tabs) {
        c.key(81, "L", "");
        c.key(119, "R", "Browse sections");
        c.hit(60, 745, 42, 58, HitType::Key, PreviousInput);
        c.hit(102, 745, 195, 58, HitType::Key, NextInput);
    }
    const float action_x = action.empty() ? 1376.f : 1376 - c.width(action, 21, Strong) - 25;
    const float back_x = (action.empty() ? 1376.f : action_x - 46) - c.width(back, 21, Text) - 25;
    c.key(back_x, "B", back);
    c.hit(back_x - 21, 744, c.width(back, 21, Text) + 48, 58, HitType::Key, BackInput);
    if (!action.empty()) {
        c.key(action_x, "A", action, true);
        c.hit(action_x - 21, 744, 1397 - action_x, 58, HitType::Key, ActivateInput);
    }
}
} // namespace
void store_screen(Canvas& c, const State& s) {
    const auto& store = s.store;
    if (store.games.empty()) {
        c.text(64, 140, "Community store", 49, paper, Display);
        c.text(64, 254, "No catalogs available.", 22, soft);
        footer(c, "Library", "", false);
        return;
    }
    const auto& g = store.games[store.game];
    if (store.page == Shelf::Games) {
        c.text(64, 137, "Choose a game", 50, paper, Display, 1000, Left, -1.65);
        c.text(64, 208, "Browse its community mods.", 22, soft);
        unsigned first = store.row / 5 * 5;
        for (unsigned i = first; i < std::min(unsigned(store.games.size()), first + 5); ++i) {
            float x = 64 + (i - first) * 270.5f;
            auto& item = store.games[i];
            game_cover(c, s, item, x, 275, 230);
            if (i == store.row)
                c.ring(x - 6, 269, 242, 242, 14, mint, 3.6);
            float sz = 23;
            auto lines = c.wrap(item.title, sz, 230, Strong);
            while (lines.size() > 2 && sz > 15)
                lines = c.wrap(item.title, --sz, 230, Strong);
            for (unsigned k = 0; k < std::min(3u, unsigned(lines.size())); ++k)
                c.text(x, 528 + k * 25, lines[k], sz, i == store.row ? mint : paper, Strong, 230);
            c.text(x, 609, std::to_string(item.mods.size()) + " mods", 18, muted);
            c.hit(x - 4, 268, 238, 380, HitType::StoreGame, i);
        }
        footer(c, "Back to store", "Choose game", false);
        return;
    }
    if (store.page == Shelf::Release) {
        const auto& m = g.mods[store.release];
        game_cover(c, s, g, 65, 124, 40);
        c.text(119, 133, g.title, 21, soft, Strong, 1020);
        picture(c, m, store.frame, 64, 203, 704, 396, true);
        auto lines = c.wrap(m.title, 43, 559, Display);
        unsigned n = std::min(2u, unsigned(lines.size()));
        for (unsigned i = 0; i < n; ++i)
            c.text(815, 202 + i * 46, lines[i], 43, paper, Display, 559, Left, -1.22);
        float y = 217 + n * 46;
        c.text(815, y, "by " + m.author, 22, mint, Strong, 559);
        auto desc = c.wrap(m.summary, 23, 559);
        for (unsigned i = 0; i < std::min(3u, unsigned(desc.size())); ++i)
            c.text(815, y + 47 + i * 29, desc[i], 23, paper, Text, 559);
        c.paragraph(815, y + 62 + std::min(3u, unsigned(desc.size())) * 29, m.description, 18, 559, 3, soft);
        c.rect(815, 509, 561, 1, rule);
        std::vector<std::pair<std::string, std::string>> facts{{"Category", m.category}};
        if (!m.version.empty())
            facts.emplace_back("Version", m.version);
        if (!m.published.empty())
            facts.emplace_back("Published", m.published);
        float gap = 560.f / facts.size();
        for (unsigned i = 0; i < facts.size(); ++i) {
            c.text(815 + i * gap, 532, facts[i].first, 16, muted, Strong, gap - 8);
            c.text(815 + i * gap, 561, facts[i].second, 21, paper, Strong, gap - 8);
        }
        c.text(815, 613, "Community hosts", 16, muted, Strong);
        unsigned start = store.mirror / 2 * 2;
        for (unsigned i = start; i < std::min(unsigned(m.sources.size()), start + 2); ++i) {
            float x = 815 + 280 * (i - start);
            auto label = (i == store.mirror ? "Open on " : "") + m.sources[i].label;
            store_button(c, x, 643, label, i == store.mirror, 268);
            c.hit(x, 641, 268, 48, HitType::StoreMirror, i);
        }
        c.text(64, 626, "Creator preview", 19, soft, Strong);
        if (m.previews.size() > 1) {
            c.arrow(632, 639, -1, 7);
            c.arrow(751, 639, 1, 7);
            c.text(692, 630, std::to_string(store.frame + 1) + " / " + std::to_string(m.previews.size()), 17,
                   muted, Text, 78, Center);
            c.hit(605, 611, 54, 54, HitType::Key, LeftInput);
            c.hit(723, 611, 55, 54, HitType::Key, RightInput);
        }
        footer(c, "Back to store", "Open host page", false);
        return;
    }
    c.text(64, 124, "Mod store", 51, paper, Display, 620, Left, -1.55);
    c.box(868, 112, 508, 63, 10, panel, rule);
    game_cover(c, s, g, 881, 122, 43);
    c.text(938, 119, "Change game   ·   R stick", 15, muted, Strong);
    c.text(938, 141, g.title, 21, paper, Strong, 384);
    for (int k = -7; k <= 7; ++k)
        c.rect(1352 + k, 145 - std::abs(k) * .64f, 2, 2, soft, 1);
    c.hit(868, 112, 508, 63, HitType::Key, DetailInput);
    float xx = 64;
    auto tabs = store.tabs();
    unsigned start = store.tab > 4 ? store.tab - 4 : 0;
    for (unsigned i = start; i < tabs.size(); ++i) {
        float ww = c.width(tabs[i], 21, i == store.tab ? Strong : Text);
        if (xx + ww > 1376)
            break;
        c.text(xx, 194, tabs[i], 21, i == store.tab ? mint : soft, i == store.tab ? Strong : Text);
        if (i == store.tab)
            c.rect(xx, 224, ww, 3.5, mint, 1.75);
        c.hit(xx - 5, 182, ww + 15, 47, HitType::StoreTab, i);
        xx += ww + 38;
    }
    c.rect(64, 227, 1312, 1, rule);
    if (store.page == Shelf::Hosts) {
        c.text(64, 255, "Community hosts", 33, paper, Display);
        c.text(64, 300, "Choose a host to see its releases for this game.", 20, soft);
        auto hosts = store.hosts();
        unsigned first = store.row / 3 * 3;
        for (unsigned i = first; i < std::min(unsigned(hosts.size()), first + 3); ++i) {
            float y = 354 + (i - first) * 112;
            bool focus = i == store.row;
            c.box(64, y, 1312, 99, 11, panel, focus ? mint : rule, focus ? 2 : 1);
            c.rect(82, y + 20, 58, 58, selected, 10);
            c.symbol(icon::globe, 95, y + 33, 32, mint);
            c.text(162, y + 19, hosts[i], 29, paper, Strong);
            unsigned count = 0;
            for (auto& m : g.mods)
                if (std::any_of(m.sources.begin(), m.sources.end(),
                                [&](const SourceLink& l) { return l.label == hosts[i]; })) {
                    if (count < 3)
                        picture(c, m, 0, 965 + count * 76, y + 24, 69, 51);
                    ++count;
                }
            c.text(163, y + 61, "Community mod host", 17, muted);
            c.text(1327, y + 37, std::to_string(count) + (count == 1 ? " mod" : " mods"), 21, soft, Strong, 135,
                   Right);
            c.arrow(1352, y + 48, 1, 8, mint);
            c.hit(64, y, 1312, 99, HitType::StoreItem, i);
        }
        footer(c, "Discover", "Browse host", true);
        return;
    }
    auto items = store.matches();
    if (items.empty()) {
        c.text(64, 272, "No releases for this game", 32, paper, Display);
        footer(c, "Library", "", true);
        return;
    }
    if (store.page == Shelf::Discover) {
        const auto& m = g.mods[items[0]];
        c.box(64, 247, 1312, 239, 13, rgb(0x19394a), store.row == 0 ? mint : rule, 2);
        picture(c, m, 0, 632, 248, 743, 237);
        c.text(92, 270, m.title, 47, paper, Display, 522, Left, -1.6);
        c.text(94, 328, "by " + m.author + "   ·   " + m.sources.front().label, 20, mint, Strong, 522);
        c.paragraph(94, 367, m.summary, 20, 500, 2, paper);
        store_button(c, 94, 426, "View mod", true);
        c.hit(64, 247, 1312, 239, HitType::StoreItem, 0);
        if (items.size() > 1)
            c.text(64, 508, "More for this game", 29, paper, Strong);
        unsigned first = store.row ? 1 + (store.row - 1) / 3 * 3 : 1;
        for (unsigned i = first; i < std::min(unsigned(items.size()), first + 3); ++i)
            card(c, g.mods[items[i]], i, 64 + (i - first) * 444, 554, 424, i == store.row, false);
    } else {
        c.text(64, 256, store.page == Shelf::Host ? store.host : tabs[store.tab], 32, paper, Display);
        unsigned first = store.row / 3 * 3;
        for (unsigned i = first; i < std::min(unsigned(items.size()), first + 3); ++i)
            card(c, g.mods[items[i]], i, 64 + (i - first) * 444, 315, 424, i == store.row, true);
    }
    footer(c,
           store.page == Shelf::Host       ? "Hosts"
           : store.page == Shelf::Discover ? "Library"
                                           : "Discover",
           "View mod", true);
}
} // namespace moader_ui
