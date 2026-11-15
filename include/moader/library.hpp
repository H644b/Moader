#pragma once
#include "model.hpp"
#include <mutex>
#include <optional>
#include <functional>
#include <map>

namespace moader {
struct LibrarySnapshot { uint64_t generation; std::vector<Mod> mods; RuntimeStatus runtime; };
class Library {
public:
    using GameNameLookup = std::function<std::string(const std::string&)>;
    Library(std::string root, Backend& backend, GameNameLookup names = {});
    std::vector<Mod> list();
    LibrarySnapshot snapshot();
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
private:
    std::string root_;
    Backend& backend_;
    uint64_t generation_ = 0;
    mutable std::mutex mutex_;
    std::vector<std::string> events_;
    std::string publish_error_;
    Preferences preferences_;
    GameNameLookup name_lookup_;
    std::map<std::string, std::string> game_names_;
    std::string game_name_locked(const std::string& title);
    std::vector<Mod> scan();
    void publish();
    void event(const std::string& message);
    std::string mod_path(const std::string& title, const std::string& id) const;
};
void make_directories(const std::string& path);
void atomic_write(const std::string& path, const std::string& bytes);
void recover_atomic_file(const std::string& path);
void extract_mod_zip(const std::string& archive, const std::string& destination);
}
