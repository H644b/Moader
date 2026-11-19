#pragma once
#include <cstddef>
#include <string_view>

namespace moader {
inline constexpr std::size_t MaxGameIconBytes = 0x20000;

inline const char* game_icon_media_type(std::string_view icon) {
    if (icon.size() >= 4 && icon.compare(0, 3, "\xff\xd8\xff", 3) == 0) return "image/jpeg";
    if (icon.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0) return "image/png";
    return nullptr;
}
} // namespace moader
