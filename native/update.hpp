#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace moader_ui {
enum class UpdatePhase { Idle, Checking, Current, Available, Downloading, Installing, Restarting,
                         RestartNeeded, Recovery, Reopen, Failed };
struct UpdateStatus {
    UpdatePhase phase = UpdatePhase::Idle;
    std::string running_version, latest, detail;
    uint64_t received = 0, total = 0;
    bool automatic = false, prompt = false, later = false;
    bool installing() const {
        return phase == UpdatePhase::Downloading || phase == UpdatePhase::Installing ||
               phase == UpdatePhase::Restarting;
    }
};

struct UpdateAsset { size_t bytes; std::string sha256; };
struct AppRelease { std::string version, manifest; UpdateAsset app, plugin; };
AppRelease parse_app_release(const std::string& manifest);
int compare_release_version(const std::string& stable, const std::string& running);
std::string latest_app_metadata(const std::atomic<bool>& cancel);
void download_app_asset(const AppRelease&, bool plugin, const std::string& destination,
                        const std::atomic<bool>& cancel, std::atomic<uint64_t>& received);

// The release controls neither the destination on SD nor the names of the two files.
class UpdateFiles {
    std::string sd, directory, pending, app_path;
    bool find_launch_path = false;
    std::string stage(bool plugin) const;
    std::string target(bool plugin) const;
    std::string backup(bool plugin) const;
public:
    UpdateFiles(std::string sd_root, const std::string& launch_path);
    bool automatic() const;
    void set_automatic(bool) const;
    bool recover(AppRelease&); // Read the journal without writing to either installed file.
    void download(const AppRelease&, const std::atomic<bool>&, std::atomic<uint64_t>&);
    void prepare(const AppRelease&) const;
    void repair(const AppRelease&, const std::atomic<bool>&, std::atomic<uint64_t>&) const;
    void install(const AppRelease&) const;
    void discard_uncommitted() const;
    void retire_superseded() const;
    void finish() const;
};
} // namespace moader_ui
