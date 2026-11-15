#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace moader {
struct Preferences {
    bool game_names = true;
    bool reduced_motion = false;
    bool web_on_start = false;
    uint16_t web_port = 7712;
    bool follow_log = true;
};
std::string preferences_json(const Preferences& preferences);
enum class ApplyState { Disabled, Pending, Active, ReopenContent, RelaunchGame, Error };
struct Mod {
    std::string title_id, id, name;
    bool requested = false;
    ApplyState applied = ApplyState::Disabled;
    bool has_romfs = false, has_exefs = false;
    uint64_t bytes = 0;
    std::vector<std::string> files;
    std::string detail;
    bool valid_source = true;
};
struct RuntimeStatus {
    bool connected = false;
    uint64_t applied_generation = 0;
    std::string backend = "unavailable";
    std::string detail = "The live filesystem backend has not been attached.";
    uint32_t result = 0;
};
// A backend must acknowledge a generation only after its routing is published.
// The manager never equates writing settings with applying them to a game.
class Backend {
public:
    virtual ~Backend() = default;
    virtual RuntimeStatus status() const = 0;
    virtual void publish(uint64_t generation, const std::vector<Mod>& mods) = 0;
    virtual std::vector<std::string> events() const { return {}; }
};
class UnavailableBackend final : public Backend {
public:
    RuntimeStatus status() const override { return {}; }
    void publish(uint64_t, const std::vector<Mod>&) override {}
};
bool valid_title_id(const std::string& value);
bool valid_mod_id(const std::string& value);
std::string checked_relative_path(const std::string& value);
std::string json_string(const std::string& value);
const char* apply_state_name(ApplyState value);
}
