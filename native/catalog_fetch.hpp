#pragma once
#include <atomic>
#include <cstddef>
#include <string>

namespace moader_ui {
// Fetch public metadata or a preview. Pairing credentials never enter this client.
std::string fetch_catalog_asset(const std::string& url, size_t limit, const std::atomic<bool>& cancel);
} // namespace moader_ui
