#pragma once
#include "model.hpp"
#include <array>
#include <functional>
#include <mutex>
#include <utility>

namespace moader {
// Publishes immutable, checked catalogues. Only a matching native receipt
// establishes that FS accepted this service's generation in the current boot.
class RuntimeBackend final : public Backend {
public:
    RuntimeBackend(std::string root, std::array<uint64_t,2> epoch,
                   std::function<void()> kick = {});
    RuntimeStatus status() const override;
    void publish(uint64_t generation, const std::vector<Mod>& mods) override;
    void poll();
    std::vector<std::string> events() const override;
private:
    std::string root_;
    std::array<uint64_t,2> epoch_;
    std::function<void()> kick_;
    mutable std::mutex mutex_;
    uint64_t requested_generation_ = 0;
    mutable uint64_t log_sequence_ = 0, log_end_ = 0, log_generation_ = 0;
    mutable uint32_t log_crc_ = 0;
    mutable bool log_report_failed_ = false;
    mutable std::vector<std::pair<uint64_t,std::string>> log_rows_;
};
}
