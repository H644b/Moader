#pragma once
#include <string>
#include <utility>
#include <vector>

namespace moader {
enum class GamePresence { Unchecked, Present, Absent, Unknown };
using StorePresence = std::vector<std::pair<std::string, GamePresence>>;
}
