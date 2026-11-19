#include "canvas.hpp"
#include <algorithm>
#include <cmath>

namespace moader_ui {
namespace {
namespace icon = moader::brand;
float ramp(float t, float a, float b) {
    float x = std::clamp((t - a) / (b - a), 0.f, 1.f);
    return x * x * (3 - 2 * x);
}
float piece(float x, float a, float b, float c, float d) {
    return x <= 1 ? std::lerp(a, b, x) : x <= 2 ? std::lerp(b, c, x - 1) : std::lerp(c, d, x - 2);
}
Color tint(const Game& g) {
    if (g.art)
        return blend(rgb(0x2a3e35), g.art->tint, .17f);
    constexpr Color palette[] = {rgb(0x283832), rgb(0x293b31), rgb(0x233541), rgb(0x383329), rgb(0x27343b)};
    return palette[(g.id ^ (g.id >> 23)) % 5];
}
void cover(Canvas& c, const State& s, const Game& g, float cx, float cy, float size, float shade, float focus) {
    const float x = cx - size / 2, y = cy - size / 2;
    if (focus > .015f) {
        float opacity = c.alpha;
        c.alpha *= focus;
        c.ring(x - 6, y - 6, size + 12, size + 12, 15, mint, 3.6);
        c.alpha = opacity;
    }
    if (g.art) {
        c.image(g.art, x, y, size, size, 10, shade);
        return;
    }
    Color ground = blend(tint(g), base, shade);
    c.box(x, y, size, size, 10, ground, blend(ground, soft, .22));
    c.symbol(icon::mark, x + .34f * size, y + .22f * size, .32f * size,
             blend(ground, mint, .72f - shade * .5f));
    c.text(cx, y + .67f * size, game_name(s, g), size * .079f, paper, Strong, size * .86f, Center, -.55);
    if (size > 150)
        c.text(cx, y + .81f * size, hexid(g.id), size * .042f, muted, Text, size * .85f, Center, .35);
}
void nav_button(Canvas& c, float x, float w, const char* label, const icon::Mask& glyph, bool on, bool focus,
                bool enabled) {
    if (on || focus)
        c.box(x, 23, w, 50, 10, selected, mint, focus ? 2.5f : 1.5f);
    else
        c.ring(x, 23, w, 50, 10, enabled ? rule : rgb(0x344039), 1.3);
    c.symbol(glyph, x + 15, 35, 25, enabled ? (on ? mint : soft) : disabled);
    c.text(x + 48, 37, label, 21, enabled ? (on ? mint : paper) : disabled, Strong, w - 51);
}
void header(Canvas& c, const State& s) {
    c.mask(icon::logoMint, 64, 30, 182.58f, 36, mint);
    c.mask(icon::logoPaper, 64, 30, 182.58f, 36, paper);
    c.rect(273, 32, 1, 33, rule);
    c.text(297, 37, "Library", 21, soft, Strong);
    c.hit(62, 18, 310, 60, HitType::Home);
    if (s.view != View::Library) {
        c.arrow(387, 48, 1, 6, muted);
        c.text(405, 37,
               settings_view(s.view)   ? "Settings"
               : s.view == View::Store ? "Store"
                                       : "Mods",
               21, paper, Strong);
        if (s.view == View::Store && (s.store.page == Shelf::Games || s.store.page == Shelf::Release)) {
            c.arrow(476, 48, 1, 6, muted);
            c.text(492, 37, s.store.page == Shelf::Games ? "Games" : "Mod details", 21, muted);
        }
        if (s.view == View::Details) {
            c.hit(397, 17, 76, 60, HitType::Mods);
            c.arrow(476, 48, 1, 6, muted);
            c.text(492, 37, "Mod details", 21, muted);
        }
    }
    if (settings_view(s.view)) {
        c.symbol(icon::controls, 923, 36, 24, muted);
        c.text(1059, 39, "On console", 18, muted, Text, 108, Right);
    }
    nav_button(c, 1082, 129, "Store", icon::bag, s.view == View::Store, s.nav_focus && s.nav_store,
               StoreAvailable || s.view == View::Store);
    if (StoreAvailable)
        c.hit(1082, 23, 129, 50, HitType::Store);
    nav_button(c, 1228, 148, "Settings", icon::gear, settings_view(s.view), s.nav_focus && !s.nav_store, true);
    c.hit(1228, 23, 148, 50, HitType::Settings);
    c.rect(64, 91, 1312, 1, rule);
}
const char* payload(const Mod& m) {
    unsigned f = m.data.flags;
    return (f & moader::ipc::Romfs) && (f & moader::ipc::Exefs) ? "RomFS + ExeFS"
           : f & moader::ipc::Romfs                             ? "RomFS"
           : f & moader::ipc::Exefs                             ? "ExeFS"
                                                                : "Unknown";
}
std::vector<std::string> detail_lines(Canvas& c, const Mod& m) {
    std::string full = m.detail;
    if (c.width(m.name, 34, Display) > 868)
        full += "\n" + m.name;
    if (c.width(m.id, 15) > 868)
        full += "\n" + m.id;
    return c.wrap(full, 22, 867);
}
void library_footer(Canvas& c, const State& s) {
    if (s.view == View::Library) {
        if (s.nav_focus) {
            c.key(82, "↓", "Return to games");
            c.hit(59, 744, 210, 60, HitType::Key, DownInput);
            c.key(1223, "A", s.nav_store ? "Store" : "Settings", true);
            c.hit(1197, 744, 181, 61, HitType::Key, ActivateInput);
            return;
        }
        if (s.games.size() > 1) {
            c.key(81, "L", "");
            c.key(119, "R", "Browse games");
            c.hit(58, 745, 43, 59, HitType::Key, PreviousInput);
            c.hit(101, 745, 155, 59, HitType::Key, NextInput);
        }
        if (!s.games.empty()) {
            c.key(1223, "A", "Open game", true);
            c.hit(1197, 744, 184, 61, HitType::Key, ActivateInput);
        } else {
            c.key(82, "ZR", "Connection");
            c.hit(60, 745, 185, 60, HitType::Key, ConnectionInput);
        }
    } else if (s.view == View::Mods) {
        c.key(81, "↑", "");
        c.key(119, "↓", "Select mod");
        c.key(1060, "B", "Library");
        c.key(1220, "A", "Mod details", true);
        c.hit(59, 744, 42, 60, HitType::Key, UpInput);
        c.hit(101, 744, 154, 60, HitType::Key, DownInput);
        c.hit(1038, 744, 135, 60, HitType::Key, BackInput);
        c.hit(1194, 744, 184, 60, HitType::Key, ActivateInput);
    } else {
        c.key(82, "X", "Remove");
        c.hit(60, 744, 134, 60, HitType::Key, RemoveInput);
        if (s.selected() < s.mods.size()) {
            c.key(292, "A",
                  s.mods[s.selected()].data.flags & moader::ipc::Requested ? "Disable mod" : "Enable mod",
                  true);
            c.hit(266, 744, 201, 60, HitType::Key, ActivateInput);
        }
        c.key(1204, "B", "Back to mods");
        c.hit(1180, 744, 201, 60, HitType::Key, BackInput);
    }
}
} // namespace
struct Renderer::Impl {
    Canvas c;
    float position = 0, open = 0, inside = 0;
    bool started = false;
    std::vector<std::string> cached_events, rendered_events;
    float log_scale = -1;
    uint64_t event_builds = 0;
    const std::vector<std::string>& events(const State& s) {
        if (cached_events == s.events && log_scale == c.scale)
            return rendered_events;
        ++event_builds;
        log_scale = c.scale;
        cached_events = s.events;
        rendered_events.clear();
        for (const auto& e : s.events) {
            auto lines = c.wrap(e, 19, 850);
            rendered_events.insert(rendered_events.end(), lines.begin(), lines.end());
        }
        return rendered_events;
    }
    void library(const State& s, float dt) {
        const int n = s.games.size();
        bool expanded = s.view == View::Mods || s.view == View::Details;
        const float targetOpen = expanded ? 1.f : 0.f, targetDetails = s.view == View::Details ? 1.f : 0.f;
        if (!n) {
            c.text(64, 125, "Game library", 51, paper, Display, 950, Left, -1.8);
            c.symbol(icon::collection, 685, 265, 70, mint);
            c.text(720, 374,
                   !s.connected                ? "Service disconnected"
                   : s.inventory_unavailable() ? "Library unavailable"
                                               : "Your library is empty",
                   42, paper, Display, 850, Center, -1.25);
            c.text(720, 452,
                   !s.connected                ? "Press Y to reconnect. Your saved mods stay on the SD card."
                   : s.inventory_unavailable() ? "Check the SD card. Moader will retry the library scan."
                                               : "Add a mod on the SD card or pair a browser to upload a ZIP.",
                   22, soft, Text, 1040, Center);
            library_footer(c, s);
            started = false;
            return;
        }
        float dest = s.game;
        if (n > 2)
            dest += std::round((position - dest) / n) * n;
        const float ease = 1.f - std::exp(-std::clamp(dt, 0.f, .15f) * 13);
        auto ease_to = [&](float value, float end) {
            if (!started || (s.preferences.flags & moader::ipc::ReducedMotion) || std::abs(end - value) < .002f)
                return end;
            return std::lerp(value, end, ease);
        };
        position = ease_to(position, dest);
        open = ease_to(open, targetOpen);
        inside = ease_to(inside, targetDetails);
        started = true;
        const float fade = 1 - ramp(open, 0, .55f);
        if (fade > 0) {
            c.alpha = fade;
            c.text(64, 125 - open * 13, "Game library", 51, paper, Display, 950, Left, -1.8);
            const auto count = std::to_string(n) + (n == 1 ? " game" : " games");
            auto cw = c.width(count, 21, Strong);
            c.symbol(icon::collection, 1376 - cw - 34, 141, 24, muted);
            c.text(1376, 142, count, 21, soft, Strong, 230, Right);
            c.alpha = 1;
        }
        struct Tile {
            unsigned i;
            float d;
        };
        std::vector<Tile> tiles;
        for (int i = 0; i < n; ++i) {
            float d = i - position;
            if (n > 2)
                d -= std::round(d / n) * n;
            if (std::abs(d) < 2.64f)
                tiles.push_back({unsigned(i), d});
        }
        std::sort(tiles.begin(), tiles.end(),
                  [](const auto& a, const auto& b) { return std::abs(a.d) > std::abs(b.d); });
        for (const auto& t : tiles) {
            const auto& game = s.games[t.i];
            float d = t.d, ad = std::abs(d);
            float side = piece(ad, 338, 259, 221, 211),
                  cx = 720 + std::copysign(piece(ad, 0, 351, 647, 918), d), cy = 373 + piece(ad, 0, 5, 6, 6);
            float shadow = piece(ad, 0, .18, .50, .65), focus = std::clamp(1 - ad * 1.85f, 0.f, 1.f);
            if (t.i == s.game && open > 0) {
                cx = std::lerp(cx, 228.f, open);
                cy = std::lerp(cy, 305.f, open) - 20 * std::sin(3.141593f * open);
                side = std::lerp(side, 324.f, open);
                focus *= 1 - ramp(open, 0, .7);
                shadow *= 1 - open;
            } else {
                cx += std::copysign(open * 105, d);
                c.alpha = 1 - ramp(open, 0, .65);
            }
            if (c.alpha > .005f)
                cover(c, s, game, cx, cy, side, shadow, s.nav_focus ? 0 : focus);
            if (fade > .005f && ad > .05f && ad < 2.3f) {
                c.alpha = fade * ramp(ad, .15, .85) * (1 - .55f * ramp(ad, 1, 2.18));
                auto label = game_name(s, game);
                auto colon = label.find(": ");
                if (colon != label.npos && c.width(label, 19, Strong) > side + 26)
                    label = label.substr(colon + 2);
                c.text(cx, cy + side / 2 + 15, label, 19, soft, Strong, side + 26, Center, -.22);
            }
            if (s.view == View::Library)
                c.hit(cx - side / 2, cy - side / 2, side, side + 27, HitType::Game, t.i);
            c.alpha = 1;
        }
        if (fade > .005f) {
            for (const auto& t : tiles) {
                float vis = 1 - ramp(std::abs(t.d), .04f, .51f);
                if (vis <= 0)
                    continue;
                c.alpha = vis * fade;
                float x = 720 + t.d * 112;
                c.text(x, 572 + open * 16, game_name(s, s.games[t.i]), 49, paper, Display, 1240, Center, -1.65);
                c.text(x, 635 + open * 16, count_text(s, s.games[t.i]), 22, soft, Text, 1150, Center, -.2);
            }
            c.alpha = fade;
            if (n > 1) {
                const int dots = std::min(n, 9), first = std::clamp(int(s.game) - dots / 2, 0, n - dots);
                for (int i = 0; i < dots; ++i) {
                    float x = 720 + (i - (dots - 1) * .5f) * 26, w = first + i == int(s.game) ? 20 : 6;
                    c.rect(x - w / 2, 687, w, 5.5, w > 6 ? mint : rgb(0x52635a), 2.75);
                    c.hit(x - 12, 671, 24, 37, HitType::Dot, first + i);
                }
                for (int sign : {-1, 1}) {
                    float x = 720 + 668 * sign;
                    c.box(x - 24, 354, 48, 48, 24, rgb(0x15211c), rgb(0x829a8b));
                    c.arrow(x, 378, sign, 10, paper);
                    c.hit(x - 31, 344, 62, 68, HitType::Key, sign < 0 ? PreviousInput : NextInput);
                }
            }
            c.hit(264, 554, 912, 109, HitType::Game, s.game);
            c.alpha = 1;
        }
        if (open > .05f) {
            const auto& g = s.games[s.game];
            float a = ramp(open, .26, .91);
            c.alpha = a;
            c.rect(443, 143, 1, 530, rule);
            const std::string label = game_name(s, g);
            float sz = 40;
            auto lines = c.wrap(label, sz, 356, Display);
            if (lines.size() > 2) {
                sz = 33;
                lines = c.wrap(label, sz, 356, Display);
            }
            if (lines.size() == 2) {
                float best = 356;
                for (size_t i = label.find(' '); i != label.npos; i = label.find(' ', i + 1)) {
                    auto l = label.substr(0, i), r = label.substr(i + 1);
                    float width = std::max(c.width(l, sz, Display), c.width(r, sz, Display));
                    if (width < best) {
                        lines = {l, r};
                        best = width;
                    }
                }
            }
            unsigned count = std::min(3u, unsigned(lines.size()));
            for (unsigned j = 0; j < count; ++j)
                c.text(66, 501 + j * (sz + 3), lines[j], sz, paper, Display, 356, Left, -1.08);
            c.text(68, 501 + count * (sz + 3) + 14, count_text(s, g), 20, soft, Text, 350);
            c.alpha = ramp(open, .25, .93) * (1 - ramp(inside, 0, .66));
            if (c.alpha > .003f)
                mods(s, g, 496 + (1 - ramp(open, .2, .98)) * 67 - inside * 32);
            c.alpha = ramp(inside, .20, .94);
            if (c.alpha > .003f && s.selected() < s.mods.size())
                details(s, 496 + (1 - c.alpha) * 42);
            c.alpha = 1;
        }
        library_footer(c, s);
    }
    void mods(const State& s, const Game& g, float x) {
        c.text(x, 147, "Installed mods", 49, paper, Display, 810, Left, -1.6);
        c.text(x + 20, 232, "Mod", 17, muted, Strong);
        c.text(x + 462, 232, "Requested", 17, muted, Strong);
        c.text(x + 632, 232, "Runtime", 17, muted, Strong);
        unsigned first = std::min(s.row / 4 * 4, unsigned(g.mods.size()));
        for (unsigned i = first; i < std::min(unsigned(g.mods.size()), first + 4); ++i) {
            float y = 267 + (i - first) * 95;
            const auto& m = s.mods[g.mods[i]];
            bool focus = i == s.row;
            if (focus)
                c.box(x, y, 880, 86, 12, selected, mint, 2.4);
            else
                c.rect(x + 3, y + 85, 875, 1, rule);
            c.rect(x + 18, y + 19, 47, 47, focus ? rgb(0x31493b) : rgb(0x25342d), 9);
            c.symbol(m.data.flags & moader::ipc::Romfs ? icon::layers : icon::code, x + 30, y + 31, 23,
                     focus ? mint : soft);
            c.text(x + 82, y + 18, m.name, 25, paper, Strong, 355, Left, -.52);
            c.text(x + 83, y + 53,
                   std::string(payload(m)) + "  ·  " + std::to_string(m.data.file_count) + " files", 16.5,
                   muted, Text, 354);
            bool on = m.data.flags & moader::ipc::Requested;
            c.text(x + 462, y + 32, on ? "On" : "Off", 21, on ? paper : muted, on ? Strong : Text, 137);
            Color color = applied_color(m.data.applied);
            c.rect(x + 632, y + 38.5, 9, 9, color, 4.5);
            c.text(x + 652, y + 32, applied_label(m.data.applied), 21, color, Text, 179);
            c.arrow(x + 850, y + 42, 1, 7, focus ? paper : muted);
            c.hit(x, y, 880, 86, HitType::Mod, i);
        }
        if (g.mods.size() > 4)
            c.text(1376, 663, std::to_string(s.row + 1) + " / " + std::to_string(g.mods.size()), 17, muted,
                   Text, 150, Right);
    }
    void details(const State& s, float x) {
        const auto& m = s.mods[s.selected()];
        c.text(x, 151, m.name, 47, paper, Display, 870, Left, -1.5);
        auto lines = detail_lines(c, m);
        unsigned start = std::min(s.detail_first, lines.size() > 2 ? unsigned(lines.size() - 2) : 0u);
        for (unsigned i = start; i < std::min(unsigned(lines.size()), start + 2); ++i)
            c.text(x, 222 + (i - start) * 30, lines[i], 22, soft, Text, 867);
        if (lines.size() > 2)
            c.text(x + 875, 284, std::to_string(start + 1) + " / " + std::to_string(lines.size()) + "  ↑↓", 14,
                   muted, Text, 180, Right);
        c.rect(x, 308, 880, 1, rule);
        c.text(x, 343, "Requested", 17, muted, Strong);
        c.text(x + 415, 343, "Runtime", 17, muted, Strong);
        c.text(x, 377, m.data.flags & moader::ipc::Requested ? "On" : "Off", 31, paper, Strong);
        Color status = applied_color(m.data.applied);
        c.rect(x + 415, 385, 11, 11, status, 5.5);
        c.text(x + 441, 377, applied_label(m.data.applied), 31, status, Strong, 430);
        c.text(x, 434, std::string("Title ID   ") + hexid(m.data.title_id), 19, soft, Text, 870);
        c.rect(x, 485, 880, 1, rule);
        c.text(x, 520, "Content", 17, muted, Strong);
        c.text(x + 415, 520, "Package", 17, muted, Strong);
        c.text(x, 553, payload(m), 25, paper, Strong);
        c.text(x + 415, 553, std::to_string(m.data.file_count) + " files  ·  " + size_text(m.data.bytes), 25,
               paper, Strong, 450);
        c.text(x, 614, "Mod ID", 17, muted, Strong);
        c.text(x, 644, m.id, 21, soft, Text, 873);
    }
    void render(const State& s, float dt) {
        bool library_view = !settings_view(s.view) && s.view != View::Store;
        Color ground = library_view && s.game < s.games.size() ? tint(s.games[s.game]) : rgb(0x25362d);
        if (!library_view)
            c.rect(0, 0, 1440, 810, base);
        else
            c.background(ground, library_view ? s.view != View::Library : true);
        if (library_view)
            library(s, dt);
        else if (s.view == View::Store)
            store_screen(c, s);
        else
            settings_screen(c, s, s.view == View::Debug ? events(s) : empty_events);
        header(c, s);
        c.rect(64, 730, 1312, 1, rule);
        std::string notice = s.message;
        if (s.reload_shutdown_required)
            notice = "Loader recovery is unverified. Save Debug and a screen, then hold POWER for 20 seconds.";
        else if (s.reload_exit_unknown || s.reload_busy)
            notice = s.reload_detail;
        else if (s.inventory_unavailable() && library_view)
            notice = "Library refresh pending. Mod changes are paused until the next complete scan.";
        else if (!s.connected && notice.empty())
            notice = "Service disconnected. Press Y to reconnect.";
        if (!notice.empty())
            c.text(720, 705, notice, 16, s.message_error || s.reload_shutdown_required ? bad : soft, Text, 1305,
                   Center);
        if (s.confirm_remove && s.selected() < s.mods.size()) {
            c.hits.clear();
            c.alpha = .79f;
            c.rect(0, 0, 1440, 810, base);
            c.alpha = 1;
            c.box(368, 265, 704, 266, 15, panel, rule, 1.5);
            c.text(407, 307, "Remove this mod?", 38, paper, Display, 625, Left, -1);
            c.text(409, 372, s.mods[s.selected()].name, 24, paper, Strong, 623);
            c.text(409, 421, "The files move to recovery storage on the SD card.", 18, soft, Text, 625);
            c.text(409, 473, "B   Keep mod", 20, mint, Strong);
            c.text(1031, 473, "X   Confirm removal", 20, bad, Strong, 290, Right);
            c.hit(389, 452, 245, 65, HitType::Key, BackInput);
            c.hit(785, 452, 268, 65, HitType::Key, RemoveInput);
        }
    }
    const std::vector<std::string> empty_events;
};
Renderer::Renderer() : impl(std::make_unique<Impl>()) {}
Renderer::~Renderer() = default;
uint64_t Renderer::event_layout_builds() const { return impl->event_builds; }
unsigned Renderer::scroll_limit(const State& s, View view) {
    if (view == View::Debug) {
        auto n = impl->events(s).size();
        return n > 4 ? unsigned(n - 4) : 0;
    }
    if (view == View::Details && s.selected() < s.mods.size()) {
        auto n = detail_lines(impl->c, s.mods[s.selected()]).size();
        return n > 2 ? unsigned(n - 2) : 0;
    }
    return 0;
}
void Renderer::draw(uint32_t* p, int w, int h, int stride, const State& s, float dt) {
    impl->c.begin(p, w, h, stride);
    impl->render(s, dt);
}
uint32_t Renderer::tap(State& s, float xx, float yy) {
    const float x = xx * 1440, y = yy * 810;
    for (auto i = impl->c.hits.rbegin(); i != impl->c.hits.rend(); ++i) {
        if (x < i->x || y < i->y || x >= i->x + i->w || y >= i->y + i->h)
            continue;
        const unsigned v = i->value;
        switch (i->type) {
        case HitType::Game:
        case HitType::Dot:
            if (v < s.games.size()) {
                if (v != s.game)
                    s.move_game(int(v) - int(s.game));
                s.nav_focus = false;
                if (i->type == HitType::Game)
                    s.view = View::Mods;
            }
            break;
        case HitType::Tab:
            s.tab_focus = false;
            return s.open_tab(v);
        case HitType::Setting:
            s.tab_focus = false;
            s.settings_row = v;
            return s.local_input(ActivateInput, 0);
        case HitType::Digit:
            s.port_digit = v;
            break;
        case HitType::Home:
            s.view = View::Library;
            s.nav_focus = s.tab_focus = false;
            break;
        case HitType::Mods:
            s.view = View::Mods;
            break;
        case HitType::Mod:
            if (s.game < s.games.size() && v < s.games[s.game].mods.size()) {
                s.row = v;
                s.view = View::Details;
                s.detail_first = 0;
            }
            break;
        case HitType::Settings:
            return s.local_input(SettingsInput, 0);
        case HitType::Key:
            return s.local_input(v, scroll_limit(s, s.view));
        case HitType::Store:
            if (StoreAvailable) {
                s.store.start(s.game < s.games.size() ? s.games[s.game].id : 0);
                s.view = View::Store;
            }
            break;
        case HitType::StoreTab:
            s.store.select_tab(v);
            break;
        case HitType::StoreGame:
        case HitType::StoreItem:
            s.store.row = v;
            return s.local_input(ActivateInput, 0);
        case HitType::StoreMirror:
            s.store.mirror = v;
            return s.local_input(ActivateInput, 0);
        }
        return 0;
    }
    return 0;
}
} // namespace moader_ui
