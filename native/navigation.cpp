#include "ui.hpp"
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <map>

namespace moader_ui {
using namespace moader::ipc;
static constexpr View tabs[] = {View::Settings, View::Connection, View::Advanced, View::Controls,
                                View::Credits};
bool settings_view(View v) {
    return v != View::Library && v != View::Mods && v != View::Details && v != View::Store;
}
unsigned tab_for(View v) {
    if (v == View::GameCopy)
        return 0;
    if (v == View::Port || v == View::Debug || v == View::Reload)
        return 2;
    if (v == View::BrowserControls)
        return 3;
    for (unsigned i = 0; i < 5; ++i)
        if (tabs[i] == v)
            return i;
    return 0;
}
bool State::has_store_mod(const StoreEntry& entry) const {
    const auto& g = store.games[entry.game];
    const auto& m = g.mods[entry.release];
    const auto id = std::strtoull(g.id.c_str(), nullptr, 16);
    return std::any_of(mods.begin(), mods.end(),
                       [&](const Mod& item) { return item.data.title_id == id && item.id == m.id; });
}
unsigned State::rows() const {
    switch (view) {
    case View::Settings:
    case View::Advanced:
        return 5;
    case View::Connection:
    case View::GameCopy:
        return 3;
    case View::Controls:
    case View::BrowserControls:
    case View::Reload:
    case View::Debug:
        return 1;
    default:
        return 0;
    }
}
uint32_t State::open_tab(unsigned index) {
    view = tabs[std::min(index, 4u)];
    settings_row = 0;
    nav_focus = false;
    confirm_remove = false;
    return !connected                                                 ? 0u
           : view == View::Settings                                   ? uint32_t(GetGameSource)
           : view == View::Connection && (summary.flags & WebRunning) ? uint32_t(GetPairing)
                                                                      : 0u;
}
uint32_t State::local_input(uint32_t b, unsigned limit) {
    preference_mask = 0;
    if (!b)
        return 0;
    auto warn = [&](const std::string& text) {
        message = text;
        message_error = true;
        return 0u;
    };
    auto show_settings = [&](unsigned t) {
        if (!settings_view(view))
            settings_return = view;
        tab_focus = false;
        nav_focus = false;
        return open_tab(t);
    };
    auto debug = [&]() {
        view = View::Debug;
        tab_focus = false;
        confirm_remove = false;
        log_paused = false;
        debug_first = preferences.flags & FollowLog ? limit : 0u;
        return connected ? uint32_t(RefreshEvents) : 0u;
    };
    if (confirm_remove) {
        if (b & RemoveInput) {
            confirm_remove = false;
            return connected && !inventory_unavailable() ? uint32_t(RemoveMod) : 0u;
        }
        if (b & (BackInput | SettingsInput | DebugInput | ConnectionInput | LeftInput | RightInput | UpInput |
                 DownInput))
            confirm_remove = false;
        return 0;
    }
    if (b & BackInput) {
        nav_focus = false;
        tab_focus = false;
        switch (view) {
        case View::Details:
            view = View::Mods;
            break;
        case View::Mods:
            view = View::Library;
            break;
        case View::GameCopy:
            view = View::Settings;
            settings_row = 4;
            break;
        case View::Port:
            view = View::Advanced;
            settings_row = 0;
            break;
        case View::Reload:
            view = View::Advanced;
            settings_row = 4;
            break;
        case View::Debug:
            view = debug_return;
            settings_row = view == View::Advanced ? 1 : 0;
            break;
        case View::BrowserControls:
            view = View::Controls;
            settings_row = 0;
            break;
        case View::Store:
            if (store.back())
                view = View::Library;
            break;
        default:
            view = settings_view(view) ? settings_return : View::Library;
            break;
        }
        return 0;
    }
    if (b & SettingsInput) {
        if (settings_view(view)) {
            view = settings_return;
            nav_focus = tab_focus = false;
            return 0;
        }
        return show_settings(0);
    }
    if (b & ConnectionInput) {
        if (view == View::Connection) {
            view = settings_return;
            tab_focus = false;
            return 0;
        }
        return show_settings(1);
    }
    if (b & DebugInput) {
        if (view == View::Debug) {
            view = debug_return;
            return 0;
        }
        debug_return = view;
        if (!settings_view(view))
            settings_return = view;
        return debug();
    }
    if (b & WebInput) {
        if (!connected)
            return Reconnect;
        if (view != View::Connection)
            show_settings(1);
        return ToggleWeb;
    }
    if (view == View::Store) {
        if (store.page == Shelf::Release) {
            if (b & UpInput && store.description_first)
                --store.description_first;
            if (b & DownInput && store.description_first < limit)
                ++store.description_first;
        }
        auto action = store.input(b);
        if (action == InstallStoreMod &&
            (!connected || inventory_unavailable() || transfer.busy() || has_store_mod(store.detail)))
            return 0;
        return action;
    }
    if (view == View::Library) {
        if (b & UpInput) {
            nav_focus = true;
            nav_store = false;
            return 0;
        }
        if (nav_focus) {
            if (b & DownInput)
                nav_focus = false;
            if (StoreAvailable && (b & LeftInput))
                nav_store = true;
            if (b & RightInput)
                nav_store = false;
            if (b & ActivateInput) {
                if (StoreAvailable && nav_store) {
                    nav_focus = false;
                    view = View::Store;
                    return 0;
                }
                return show_settings(0);
            }
            return 0;
        }
        if (b & (LeftInput | PreviousInput))
            move_game(-1);
        if (b & (RightInput | NextInput))
            move_game(1);
        if ((b & (ActivateInput | DetailInput)) && selected() < mods.size())
            view = View::Mods;
        return 0;
    }
    if (view == View::Mods || view == View::Details) {
        if (selected() >= mods.size()) {
            view = View::Library;
            return 0;
        }
        if (view == View::Mods) {
            if (b & UpInput)
                move_row(-1);
            if (b & DownInput)
                move_row(1);
            if (b & (ActivateInput | DetailInput)) {
                view = View::Details;
                detail_first = 0;
            }
        } else {
            if (b & UpInput)
                detail_first = detail_first ? detail_first - 1 : 0;
            if (b & DownInput)
                detail_first = std::min(limit, detail_first + 1);
            if (b & ActivateInput) {
                if (!connected || inventory_unavailable())
                    return warn("Changes are paused until the library reconnects and refreshes.");
                return ToggleMod;
            }
        }
        if (b & RemoveInput) {
            if (!connected || inventory_unavailable())
                return warn("Removal requires a current library. Reconnect or refresh first.");
            confirm_remove = true;
        }
        return 0;
    }
    if (view == View::Port) {
        if (b & LeftInput)
            port_digit = (port_digit + 4) % 5;
        if (b & RightInput)
            port_digit = (port_digit + 1) % 5;
        unsigned factor = 1;
        for (unsigned i = port_digit; i < 4; ++i)
            factor *= 10;
        const auto digit = port_draft / factor % 10;
        if (b & UpInput)
            port_draft = port_draft - digit * factor + (digit + 1) % 10 * factor;
        if (b & DownInput)
            port_draft = port_draft - digit * factor + (digit + 9) % 10 * factor;
        if (b & ActivateInput) {
            if (!connected || !preferences_available)
                return warn("Reconnect to save the listener port.");
            if (port_draft < 1024 || port_draft > 65535 || port_draft == 7713)
                return warn("Choose a port from 1024 to 65535, excluding 7713.");
            preferences.port = port_draft;
            preference_mask = PortField;
            view = View::Advanced;
            settings_row = 0;
            return SavePreferences;
        }
        return 0;
    }
    if (b & LeftInput) {
        tab_focus = true;
        return 0;
    }
    if (tab_focus) {
        const int step = (b & DownInput) ? 1 : (b & UpInput) ? -1 : 0;
        if (step)
            return open_tab((int(tab_for(view)) + step + 5) % 5);
        if (b & (RightInput | ActivateInput))
            tab_focus = false;
        return 0;
    }
    if (view == View::Debug) {
        if (b & UpInput) {
            if ((preferences.flags & FollowLog) && !log_paused)
                debug_first = limit;
            debug_first = debug_first > 3 ? debug_first - 3 : 0;
            log_paused = true;
        }
        if (b & DownInput) {
            debug_first = std::min(debug_first + 3, limit);
            log_paused = !(preferences.flags & FollowLog) || debug_first < limit;
        }
        return b & ActivateInput ? uint32_t(SaveReport) : 0u;
    }
    const auto count = rows();
    if (count) {
        if (b & UpInput)
            settings_row = (settings_row + count - 1) % count;
        if (b & DownInput)
            settings_row = (settings_row + 1) % count;
    }
    if (!(b & ActivateInput))
        return 0;
    if (view == View::Controls || view == View::BrowserControls) {
        view = view == View::Controls ? View::BrowserControls : View::Controls;
        return 0;
    }
    if (view == View::Advanced) {
        switch (settings_row) {
        case 0:
            if (!connected || !preferences_available)
                return warn("Reconnect a service with preference support to edit the port.");
            view = View::Port;
            port_draft = preferences.port;
            port_digit = 0;
            return 0;
        case 1:
            debug_return = View::Advanced;
            return debug();
        case 2:
            return SaveReport;
        case 3:
            return SaveScreenshot;
        case 4:
            view = View::Reload;
            settings_row = 0;
            return 0;
        }
    }
    if (view == View::Reload) {
        if (!reload_available || reload_busy || !connected)
            return warn(reload_detail);
        return ReloadService;
    }
    if (view == View::GameCopy) {
        if (!can_save_game_source())
            return warn("Select a game with a current library and available game-copy controls.");
        game_source = settings_row;
        return SaveGameSource;
    }
    if (view == View::Connection) {
        if (settings_row == 0)
            return connected ? ToggleWeb : Reconnect;
        if (settings_row == 2) {
            view = View::BrowserControls;
            settings_row = 0;
            return 0;
        }
    }
    if (view == View::Settings || view == View::Connection) {
        if (view == View::Settings && settings_row == 4) {
            view = View::GameCopy;
            settings_row = std::min(2u, game_source);
            return connected ? uint32_t(GetGameSource) : 0u;
        }
        if (!connected || !preferences_available)
            return warn("Reconnect a service with preference support to change this setting.");
        const unsigned i = view == View::Connection ? 3 : settings_row;
        constexpr uint32_t bits[] = {GameNames, ReducedMotion, FollowLog, WebOnStart};
        constexpr uint32_t fields[] = {NamesField, MotionField, FollowField, StartupField};
        preferences.flags ^= bits[i];
        preference_mask = fields[i];
        return SavePreferences;
    }
    return 0;
}
void State::regroup(uint64_t preferred) {
    if (!preferred && game < games.size())
        preferred = games[game].id;
    std::vector<Game> next;
    std::map<uint64_t, unsigned> index;
    for (unsigned i = 0; i < mods.size(); ++i) {
        auto [it, added] = index.try_emplace(mods[i].data.title_id, next.size());
        if (added) {
            Art art;
            for (const auto& prev : games)
                if (prev.id == it->first) {
                    art = prev.art;
                    break;
                }
            next.push_back({it->first, {}, mods[i].game_name, std::move(art)});
        }
        next[it->second].mods.push_back(i);
    }
    games = std::move(next);
    auto found = index.find(preferred);
    game = found == index.end() ? 0 : found->second;
    row = game < games.size() ? std::min(row, unsigned(games[game].mods.size() - 1)) : 0;
    if (found == index.end() && (view == View::Mods || view == View::Details)) {
        view = View::Library;
        confirm_remove = false;
    }
    if (found == index.end())
        settings_return = View::Library;
}
void State::move_game(int delta) {
    if (games.empty())
        return;
    auto next = unsigned((int(game) + delta + int(games.size())) % int(games.size()));
    if (next == game)
        return;
    game = next;
    row = 0;
    detail_first = 0;
    confirm_remove = false;
    game_source_available = false;
}
void State::move_row(int delta) {
    if (game >= games.size())
        return;
    const auto count = int(games[game].mods.size());
    row = (int(row) + delta + count) % count;
    detail_first = 0;
    confirm_remove = false;
}
unsigned State::selected() const {
    return game < games.size() && row < games[game].mods.size() ? games[game].mods[row]
                                                                : std::numeric_limits<unsigned>::max();
}
} // namespace moader_ui
