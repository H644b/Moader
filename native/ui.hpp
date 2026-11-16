#pragma once
#include "moader/ipc.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace moader_ui {
struct Mod { moader::ipc::ModReply data{}; std::string name, id, detail, game_name; };
struct Game { uint64_t id; std::vector<unsigned> mods; std::string name; };
enum class View { Library, Connection, Debug, Details, Settings, Advanced, Credits };
enum LocalInput : uint32_t { BackInput=1, DebugInput=2, UpInput=4, DownInput=8, ActivateInput=16, SettingsInput=32, LeftInput=64, RightInput=128 };
enum LocalAction : uint32_t { RefreshEvents=1, SaveReport=2, SavePreferences=4, SaveScreenshot=8, SaveGameSource=16, ReloadService=32 };
struct State {
    moader::ipc::SummaryReply summary{};
    moader::ipc::PreferencesReply preferences{moader::ipc::GameNames|moader::ipc::FollowLog,7712};
    std::vector<Mod> mods;
    std::vector<Game> games;
    std::vector<std::string> events;
    std::string address, pairing, message, runtime_detail;
    unsigned game = 0, row = 0, debug_first = 0, detail_first = 0;
    View view = View::Library, debug_return = View::Library;
    uint32_t preference_mask=0;
    unsigned settings_row=0, advanced_row=0, port_digit=0, port_draft=7712;
    bool preferences_available=false, port_edit=false, log_paused=false;
    bool rail_focus = false, confirm_remove = false, connected = false, message_error = false;
    bool inventory_refresh_pending = false;
    bool reload_available=false, reload_busy=false, reload_error=false;
    bool reload_shutdown_required=false;
    bool reload_exit_unknown=false;
    std::string reload_detail="Install the new kaiHEN build to reload the resident service.";
    uint32_t game_source = moader::ipc::CopyUnspecified;
    uint64_t game_source_title = 0;
    bool game_source_available = false;
    bool inventory_unavailable() const {
        return inventory_refresh_pending || (summary.flags & moader::ipc::InventoryUnavailable);
    }
    bool can_save_game_source() const {
        const auto index=selected();
        return connected && !inventory_unavailable() && game_source_available && index<mods.size() &&
            mods[index].data.title_id==game_source_title;
    }
    // Keep list/index identity together while still updating live backend status.
    void retain_inventory(const moader::ipc::SummaryReply& latest) {
        const auto generation = summary.generation;
        summary = latest; summary.generation = generation; summary.mod_count = mods.size();
        inventory_refresh_pending = true; confirm_remove = false; connected = true;
        game_source_available = false;
    }
    uint32_t local_input(uint32_t buttons,unsigned debug_limit);
    void regroup(uint64_t preferred = 0);
    void move_game(int delta);
    void move_row(int delta);
    unsigned selected() const;
};
class Renderer {
public:
    Renderer(); ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    // RGBA8, byte stride. Coordinates are derived from the 1280 x 720 workbench.
    uint64_t event_layout_builds() const;
    unsigned scroll_limit(const State&, View);
    void draw(uint32_t* pixels, int width, int height, int stride, const State&, float seconds);
private:
    struct Impl; std::unique_ptr<Impl> impl;
};
}
