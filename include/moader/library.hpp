#pragma once
#include "model.hpp"
#include <mutex>
#include <optional>
#include <functional>
#include <map>

namespace moader {
struct LibrarySnapshot {
    uint64_t generation;
    std::vector<Mod> mods;
    RuntimeStatus runtime;
    bool inventory_ready = true;
    std::string inventory_error;
};
struct GameAvailability {
    bool load_allowed = true;
    std::string detail;
};
enum class GameSource : uint32_t { Unspecified, Cartridge, Digital };
const char* game_source_name(GameSource source);
class Library {
public:
    using GameNameLookup = std::function<std::string(const std::string&)>;
    using AvailabilityLookup = std::function<GameAvailability(const std::string&, GameSource)>;
    Library(std::string root, Backend& backend, GameNameLookup names = {}, AvailabilityLookup availability = {});
    // list/rescan explicitly inspect SD contents. Ordinary snapshots use the
    // last complete inventory and never wait for ZIP extraction or a rescan.
    std::vector<Mod> list();
    LibrarySnapshot snapshot();
    bool rescan();
    bool refresh_availability();
    GameSource game_source(const std::string& title) const;
    void set_game_source(const std::string& title, GameSource source,
                         std::optional<uint64_t> expected_generation = {});
    void synchronize();
    Mod set_enabled(const std::string& title, const std::string& id, bool enabled,
                    std::optional<uint64_t> expected_generation = {});
    void remove(const std::string& title, const std::string& id,
                std::optional<uint64_t> expected_generation = {});
    Mod import_zip(const std::string& title, const std::string& id,
                   const std::string& archive, const std::string& display_name);
    uint64_t generation() const;
    RuntimeStatus runtime() const { return backend_.status(); }
    std::string snapshot_json();
    std::string events_json();
    std::vector<std::string> diagnostic_lines();
    Preferences preferences() const;
    void set_preferences(const Preferences& preferences);
    // Bits: 1 names, 2 motion, 4 startup, 8 port, 16 log follow.
    void update_preferences(const Preferences& values, uint32_t mask);
    std::string game_name(const std::string& title);
    // Reserve a cooperative service shutdown only between library operations.
    // No routing/files are withdrawn. Readers remain valid while writes pause.
    bool begin_quiesce();
    void cancel_quiesce();
    bool quiescing() const;
private:
    std::string root_;
    Backend& backend_;
    uint64_t generation_ = 0;
    mutable std::mutex mutex_;
    std::mutex operation_mutex_, names_mutex_;
    bool importing_ = false; // Guarded by operation_mutex_; availability may proceed during extraction.
    bool quiescing_ = false; // Guarded by mutex_; changed while holding operation_mutex_.
    std::vector<Mod> inventory_;
    bool inventory_ready_ = false;
    std::string inventory_error_;
    std::vector<std::string> events_;
    std::string publish_error_;
    Preferences preferences_;
    GameNameLookup name_lookup_;
    AvailabilityLookup availability_lookup_;
    std::map<std::string, std::string> game_names_;
    std::string game_name_locked(const std::string& title);
    std::vector<Mod> scan();
    Mod scan_mod(const std::string& title, const std::string& id, size_t& inventory);
    GameSource read_game_source(const std::string& title);
    void gate(std::vector<Mod>& mods, bool verified_inventory = true);
    std::vector<Mod> cached_inventory() const;
    void commit(std::vector<Mod> mods, bool advance, bool verified_inventory = true);
    void scan_failed(const std::string& error);
    void event(const std::string& message);
    std::string mod_path(const std::string& title, const std::string& id) const;
};
void make_directories(const std::string& path);
void atomic_write(const std::string& path, const std::string& bytes);
void recover_atomic_file(const std::string& path);
void extract_mod_zip(const std::string& archive, const std::string& destination);
}
