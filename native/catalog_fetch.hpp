#pragma once
#include "store.hpp"
#include <atomic>
#include <string>

namespace moader_ui {
std::string fetch_catalog_asset(const std::string& url, size_t limit, const std::atomic<bool>& cancel);
// Public HTTPS package data and local library credentials use independent handles.
void install_catalog_package(const Release&, const std::string& game_id, unsigned port,
                             const std::string& pairing, const std::atomic<bool>& cancel,
                             std::atomic<uint64_t>& received, std::atomic<bool>& importing);
} // namespace moader_ui
