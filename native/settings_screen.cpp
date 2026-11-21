#include "canvas.hpp"
#include <algorithm>
#include <cstdio>

namespace moader_ui {
namespace {
namespace icon = moader::brand;
constexpr float X = 448, R = 1376, W = 928;
const char* const tabs[] = {"General", "Connection", "Advanced", "Controls", "Credits"};
const icon::Mask* const symbols[] = {&icon::general, &icon::wifi, &icon::gear, &icon::controls, &icon::info};
const char* const pref_titles[] = {"Show game names", "Reduce motion", "Follow debug log",
                                   "Start web automatically"};
const char* const pref_desc[] = {"Use installed names when available. Title IDs are the fallback.",
                                 "Move focus and change screens immediately.",
                                 "Follow new events until you scroll up to read older entries.",
                                 "Start the web listener when its host app or service opens."};
constexpr unsigned pref_bits[] = {moader::ipc::GameNames, moader::ipc::ReducedMotion, moader::ipc::FollowLog,
                                  moader::ipc::WebOnStart};
constexpr const char* copies[] = {"Check inserted card", "Cartridge or game-key card", "My digital copy"};
void heading(Canvas& c, const std::string& title, const std::string& sub) {
    c.text(X, 145, title, 47, paper, Display, W, Left, -1.6);
    c.text(X + 1, 211, sub, 20, soft, Text, W - 3);
}
void focus(Canvas& c, const State& s, unsigned i, float top, float h) {
    if (!s.tab_focus && s.settings_row == i)
        c.rect(X - 15, top + 14, 3, h - 33, mint, 1.5);
}
void row(Canvas& c, const State& s, unsigned index, float y, const std::string& title,
         const std::string& description, const std::string& value = "") {
    focus(c, s, index, y, 79);
    c.text(X + 1, y + 10, title, 24, paper, Strong, 640, Left, -.42);
    c.text(X + 1, y + 45, description, 17, muted, Text, 680);
    if (!value.empty())
        c.text(R - 32, y + 24, value, 19, soft, Strong, 230, Right);
    c.arrow(R - 7, y + 35, 1, 7, muted);
    c.rect(X, y + 76, W, 1, rule);
    c.hit(X - 20, y, W + 20, 78, HitType::Setting, index);
}
void toggle(Canvas& c, const State& s, unsigned index, unsigned pref, float y) {
    bool live = s.connected && s.preferences_available, on = s.preferences.flags & pref_bits[pref];
    focus(c, s, index, y, 79);
    c.text(X + 1, y + 10, pref_titles[pref], 24, paper, Strong, 660, Left, -.42);
    c.text(X + 1, y + 45, pref_desc[pref], 17, muted, Text, 730);
    c.text(R - 69, y + 27, live ? (on ? "On" : "Off") : "Unavailable", 18, live && on ? mint : soft, Strong,
           150, Right);
    c.rect(R - 56, y + 24, 49, 26, live && on ? mint : rgb(0x4d5e54), 13);
    c.rect(R - 56 + (on ? 27.5 : 4.5), y + 28.5, 17, 17, on && live ? base : paper, 8.5);
    c.rect(X, y + 76, W, 1, rule);
    c.hit(X - 20, y, W + 20, 78, HitType::Setting, index);
}
void button(Canvas& c, float x, float y, const std::string& label, unsigned key, bool primary = true) {
    float w = c.width(label, 19, Strong) + 40;
    c.box(x, y, w, 45, 9, primary ? mint : panel, primary ? mint : rule);
    c.text(x + w / 2, y + 12, label, 19, primary ? base : paper, Strong, w - 20, Center);
    c.hit(x, y, w, 45, HitType::Key, key);
}
void controls(Canvas& c, const State& s) {
    bool browser = s.view == View::BrowserControls;
    heading(c, browser ? "Browser controls" : "Console controls",
            browser ? "Library actions available after pairing." : "Controller shortcuts available in Moader.");
    if (browser) {
        const char* names[] = {"Upload mod", "Search and refresh", "Inspect and change mods",
                               "Copy and download", "Pair and disconnect"};
        const char* info[] = {
            "Choose a ZIP, enter the game Title ID and mod name, then upload it to your Switch.",
            "Find mods by name or rescan the library after copying files to the SD card.",
            "Inspect the runtime state, enable or disable a mod, or confirm removal.",
            "Copy a Title ID or the debug log. Download a session log from Advanced.",
            "Enter the six-digit code shown in Connection, or disconnect the browser."};
        for (int i = 0; i < 5; ++i) {
            float y = 255 + i * 72;
            c.text(X, y + 7, names[i], 22, paper, Strong);
            c.text(X, y + 38, info[i], 17, muted, Text, W);
            c.rect(X, y + 63, W, 1, rule);
        }
        button(c, X, 637, "Console controls", ActivateInput);
        return;
    }
    const char* keys[] = {"D-pad", "L / R", "A",       "X, then X", "B",     "Y",
                          "ZR",    "ZL",    "L stick", "R stick",   "Minus", "Plus"};
    const char* labels[] = {"Move focus",
                            "Browse games",
                            "Open or toggle in details",
                            "Confirm removal",
                            "Go back or cancel",
                            "Toggle web or reconnect",
                            "Address and pairing code",
                            "Open debug log",
                            "Open Settings",
                            "View selected mod",
                            "Save screenshot",
                            "Return to hbmenu"};
    for (unsigned i = 0; i < 12; ++i) {
        float x = X + (i / 6) * 466, y = 265 + (i % 6) * 55,
              ww = std::max(55.f, c.width(keys[i], 16, Strong) + 21);
        c.ring(x, y, ww, 30, 6, rule, 1.3);
        c.text(x + ww / 2, y + 7, keys[i], 16, mint, Strong, ww - 5, Center);
        c.text(x + ww + 15, y + 5, labels[i], 19, paper, Text, 440 - ww);
    }
    button(c, X, 632, "Browser controls", ActivateInput, false);
}
} // namespace
void settings_screen(Canvas& c, const State& s, const std::vector<std::string>& events) {
    const auto cat = tab_for(s.view);
    c.text(64, 145, "Settings", 50, paper, Display, 302, Left, -1.6);
    for (unsigned i = 0; i < 5; ++i) {
        float y = 229 + i * 62;
        if (i == cat) {
            c.rect(64, y, 298, 52, selected, 10);
            if (s.tab_focus)
                c.ring(64, y, 298, 52, 10, mint, 2);
        }
        c.symbol(*symbols[i], 83, y + 14, 24, i == cat ? mint : muted);
        c.text(122, y + 14, tabs[i], 23, i == cat ? mint : soft, i == cat ? Strong : Text, 235);
        c.hit(62, y - 2, 303, 56, HitType::Tab, i);
    }
    c.rect(394, 141, 1, 539, rule);
    c.rect(65, 583, 293, 1, rule);
    c.text(139, 609, "Selected game", 15, muted, Strong);
    if (s.game < s.games.size()) {
        const auto& g = s.games[s.game];
        if (g.art)
            c.image(g.art, 66, 607, 59, 59, 6);
        else
            c.symbol(icon::mark, 74, 615, 43, mint);
        auto lines = c.wrap(game_name(s, g), 18, 219, Strong);
        for (unsigned i = 0; i < std::min(2u, unsigned(lines.size())); ++i)
            c.text(139, 635 + i * 22, lines[i], 18, paper, Strong, 219);
    } else
        c.text(139, 635, "No game selected", 18, soft, Text, 225);
    const auto title =
        s.game < s.games.size() ? game_name(s, s.games[s.game]) : "Choose a game in the library.";
    switch (s.view) {
    case View::Settings:
        heading(c, "General",
                s.connected && s.preferences_available
                    ? "Changes save on this console as you make them."
                    : "Saved preferences are unavailable until the service reconnects.");
        for (unsigned i = 0; i < 4; ++i)
            toggle(c, s, i, i, 257 + 79 * i);
        row(c, s, 4, 573, "Game copy", title, "Choose copy");
        break;
    case View::GameCopy: {
        heading(c, "Game copy",
                s.game < s.games.size() ? "Choose the copy you play for " + title + "." : title);
        const char* desc[] = {"Check for the matching card when this game is selected.",
                              "Keep the matching physical card inserted, including game-key cards.",
                              "Choose this for a copy you can play without inserting its card."};
        for (unsigned i = 0; i < 3; ++i) {
            float y = 273 + i * 102;
            focus(c, s, i, y, 98);
            c.text(X + 1, y + 8, copies[i], 25, paper, Strong);
            c.text(X + 1, y + 46, desc[i], 18, soft, Text, W - 96);
            c.ring(R - 34, y + 19, 24, 24, 12, dim, 2);
            if (s.can_save_game_source() && s.game_source == i)
                c.rect(R - 28, y + 25, 12, 12, mint, 6);
            c.rect(X, y + 89, W, 1, rule);
            c.hit(X - 16, y, W + 16, 90, HitType::Setting, i);
        }
        c.text(X, 622,
               s.can_save_game_source()
                   ? "The selection saves immediately for this game."
                   : "Game-copy controls require a current library and a supporting service.",
               19, muted, Text, W);
        break;
    }
    case View::Connection: {
        heading(c, "Connection", "Pair a browser on the same network as your Switch.");
        bool live = s.connected && (s.summary.flags & moader::ipc::WebRunning);
        c.rect(X, 263, W, 159, panel, 13);
        c.rect(X + 552, 285, 1, 112, rule);
        c.text(X + 24, 288, "Local address", 17, soft, Strong);
        c.text(X + 24, 329,
               !s.connected ? "Service disconnected"
               : live       ? s.address
                            : "Web listener is off",
               32, paper, Strong, 508, Left, -.5);
        c.text(X + 580, 288, "Pairing code", 17, soft, Strong);
        auto pin = live ? s.pairing : "—  —  —";
        if (live && pin.size() == 6)
            pin.insert(3, " ");
        c.text(X + 580, 322, pin.empty() ? "Unavailable" : pin, 46, live ? mint : dim, Strong, 320);
        row(c, s, 0, 442, "Web listener", "Y starts or stops browser access on this console.",
            !s.connected ? "Reconnect"
            : live       ? "Stop listener"
                         : "Start listener");
        toggle(c, s, 1, 3, 521);
        row(c, s, 2, 604, "Browser companion", "Uploads, search, library controls and diagnostics.",
            "View controls");
        break;
    }
    case View::Advanced: {
        heading(c, "Advanced", "Manage the listener, inspect the runtime and export diagnostics.");
        const char* names[] = {"Web listener port", "Runtime details", "Export debug report",
                               "Save current screenshot", "Reload Moader service"};
        const char* desc[] = {"Changes take effect when the web listener next starts.",
                              "Connection, generations, result and recent events.",
                              "Save the library and the latest available runtime evidence.",
                              "Save a capture of the console interface to the SD card.",
                              "Restart the resident service and wait for its health check."};
        const char* value[] = {"Edit port", "View session", "SD card", "SD card", "Resident only"};
        for (unsigned i = 0; i < 5; ++i)
            row(c, s, i, 257 + 79 * i, names[i], desc[i], value[i]);
        break;
    }
    case View::Port: {
        heading(c, "Web listener port", "The port changes the next time you start the listener.");
        c.text(X, 287, "Saved port", 19, muted, Strong);
        char num[8];
        std::snprintf(num, sizeof(num), "%05u", s.port_draft % 100000);
        c.box(X, 327, 224, 58, 9, panel, rule);
        for (unsigned i = 0; i < 5; ++i) {
            float x = X + 19 + i * 38;
            if (i == s.port_digit)
                c.rect(x - 5, 334, 33, 43, selected, 5);
            c.text(x + 10, 341, std::string(1, num[i]), 31, i == s.port_digit ? mint : paper, Strong, 34,
                   Center);
            c.hit(x - 7, 328, 38, 58, HitType::Digit, i);
        }
        c.text(X, 419, "Choose 1024 to 65535. Port 7713 is reserved for the runtime.", 20, soft, Text, W);
        c.rect(X, 480, W, 1, rule);
        c.text(X, 518, "Left and right select a digit. Up and down change it.", 19, muted);
        button(c, X, 591, "Save port", ActivateInput);
        button(c, X + 153, 591, "Cancel", BackInput, false);
        button(c, X + 312, 591, "Digit +", UpInput, false);
        button(c, X + 425, 591, "Digit −", DownInput, false);
        break;
    }
    case View::Debug: {
        heading(c, s.connected ? "Current session" : "Last known session",
                s.connected ? "Service status and the latest available runtime evidence."
                            : "The service disconnected. You can still read events and save a report.");
        char code[16];
        std::snprintf(code, sizeof(code), "0x%08X", s.summary.last_result);
        bool live = s.summary.flags & moader::ipc::BackendConnected;
        const char* labels[] = {
            "Listener",           "Runtime backend",
            "Runtime connection", s.summary.last_result ? "Runtime error" : "Runtime result",
            "Library generation", "Applied generation"};
        std::string values[] = {s.summary.flags & moader::ipc::WebRunning ? s.address : "Web off",
                                "Filesystem",
                                !s.connected ? "Last known"
                                : live       ? "Connected"
                                             : "Disconnected",
                                code,
                                std::to_string(s.summary.generation),
                                std::to_string(s.summary.applied_generation)};
        for (unsigned i = 0; i < 6; ++i) {
            float x = X + i % 3 * 316, y = 263 + i / 3 * 70;
            c.text(x, y, labels[i], 16, i == 3 && s.summary.last_result ? bad : muted, Strong);
            c.text(x, y + 26, values[i], 23,
                   i == 3 && s.summary.last_result ? bad
                   : i == 2 && live && s.connected ? good
                                                   : paper,
                   Strong, 290);
        }
        c.text(X, 409, s.runtime_detail, 17, soft, Text, W);
        c.rect(X, 441, W, 1, rule);
        bool follow = (s.preferences.flags & moader::ipc::FollowLog) && !s.log_paused;
        c.text(X, 459, "Recent events", 27, paper, Strong);
        c.text(R, 463, follow ? "Following latest" : "History", 17, follow ? good : pending, Strong, 240,
               Right);
        unsigned limit = events.size() > 4 ? events.size() - 4 : 0,
                 first = follow ? limit : std::min(limit, s.debug_first);
        if (events.empty())
            c.text(X, 516, "No events recorded.", 19, muted);
        for (unsigned i = first; i < std::min(unsigned(events.size()), first + 4); ++i) {
            float y = 510 + (i - first) * 30;
            c.text(X, y, std::to_string(i + 1), 17, dim, Text, 45);
            c.text(X + 51, y, events[i], 19, soft, Text, 860);
        }
        button(c, X, 643, "Save report", ActivateInput);
        button(c, X + 172, 643, "Earlier", UpInput, false);
        button(c, X + 298, 643, "Later", DownInput, false);
        break;
    }
    case View::Reload:
        heading(c, "Reload Moader service",
                "Restart the resident service and reconnect after its health check.");
        c.text(X, 286, "Service status", 18, muted, Strong);
        c.rect(X, 345, 13, 13, s.reload_error ? bad : s.reload_available ? good : dim, 6.5);
        c.text(X + 28, 331,
               s.reload_busy        ? "Reload in progress"
               : s.reload_error     ? "Reload needs attention"
               : s.reload_available ? "Ready to reload"
                                    : "Unavailable",
               38, paper, Strong, W - 32);
        c.paragraph(X, 410, s.reload_detail, 21, W, 3);
        c.rect(X, 510, W, 1, rule);
        c.text(X, 547, "Library and game-copy preferences are kept on the SD card.", 19, muted);
        button(c, X, 617, "Reload service", ActivateInput, s.reload_available && s.connected && !s.reload_busy);
        break;
    case View::Controls:
    case View::BrowserControls:
        controls(c, s);
        break;
    case View::Credits: {
        heading(c, "Credits", "Moader and the projects behind it.");
        const char* names[] = {"Moader",
                               "kaiHEN  /  ounce-devkit  /  libounce",
                               "ns2jb-modloader  /  ns2jb",
                               "devkitPro  /  zlib",
                               "DM Sans  /  Bricolage Grotesque  /  Noto Sans",
                               "stb  /  picojson"};
        const char* info[] = {
            "Nintendo Switch mod manager · 3.0.0",
            "Homebrew environment, console libraries and system services.",
            "Filesystem mod-loader foundation and project research.",
            "Compiler, platform libraries and ZIP decoding.",
            "Interface type, wordmark outlines and Unicode fallback · SIL Open Font License 1.1.",
            "Font and image rasterization and community catalog parsing."};
        for (unsigned i = 0; i < 6; ++i) {
            float y = 254 + i * 69;
            c.text(X, y, names[i], 22, paper, Strong, W);
            c.text(X, y + 31, info[i], 16, muted, Text, W);
            c.rect(X, y + 55, W, 1, rule);
        }
        break;
    }
    default:
        break;
    }
    c.key(82, "←", "Sections");
    c.hit(58, 741, 173, 61, HitType::Key, LeftInput);
    c.key(1145, "B", "Back");
    c.hit(1118, 742, 100, 63, HitType::Key, BackInput);
    c.key(1265, "A", "Select", true);
    c.hit(1238, 742, 133, 63, HitType::Key, ActivateInput);
}
} // namespace moader_ui
