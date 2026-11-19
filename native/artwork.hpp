#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace moader_ui {
struct Artwork {
    unsigned width{}, height{};
    std::vector<uint32_t> pixels;
    uint32_t tint{};
};
using Art = std::shared_ptr<const Artwork>;
Art decode_art(const uint8_t* bytes, size_t length);
} // namespace moader_ui
