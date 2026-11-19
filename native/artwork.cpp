#include "artwork.hpp"
#include <algorithm>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 2048
#include "third_party/stb_image.h"

namespace moader_ui {
Art decode_art(const uint8_t* bytes, size_t length) {
    if (!bytes || !length || length > 2 * 1024 * 1024)
        return {};
    int w, h, components;
    if (!stbi_info_from_memory(bytes, length, &w, &h, &components) || w < 1 || h < 1 || w > 2048 || h > 2048 ||
        int64_t(w) * h > 2097152)
        return {};
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> rgba(
        stbi_load_from_memory(bytes, length, &w, &h, &components, 4), stbi_image_free);
    if (!rgba)
        return {};
    auto a = std::make_shared<Artwork>();
    const float factor = std::min(1.f, 640.f / std::max(w, h));
    a->width = std::max(1u, unsigned(w * factor));
    a->height = std::max(1u, unsigned(h * factor));
    a->pixels.resize(a->width * a->height);
    uint64_t r = 0, g = 0, b = 0;
    for (unsigned y = 0; y < a->height; ++y)
        for (unsigned x = 0; x < a->width; ++x) {
            const auto* p = rgba.get() + 4 * ((y * h / a->height) * w + x * w / a->width);
            uint32_t c[3];
            for (unsigned j = 0; j < 3; ++j)
                c[j] = (p[j] * p[3] + 24 * (255 - p[3])) / 255;
            a->pixels[y * a->width + x] = c[0] | c[1] << 8 | c[2] << 16 | 0xff000000;
            r += c[0];
            g += c[1];
            b += c[2];
        }
    const auto n = a->pixels.size();
    a->tint = (14 + r / n / 5) | ((24 + g / n / 5) << 8) | ((22 + b / n / 5) << 16) | 0xff000000;
    return a;
}
} // namespace moader_ui
