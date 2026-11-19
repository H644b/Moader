#pragma once
#include "artwork.hpp"
#include "game_names.hpp"
#include "moader/game_icon.hpp"

namespace moader_native {
struct InstalledIcon {
    std::string encoded;
    moader_ui::Art artwork;
};

inline InstalledIcon read_installed_icon(const std::string& title) {
    using namespace game_names_detail;
    static_assert(sizeof(NsApplicationControlData::icon) == moader::MaxGameIconBytes);
    const auto routing = parse_id(title);
    ControlSession session;
    const auto id = metadata_identity(routing);
    std::string failures;

    // Some Switch 2 titles return 0x5010 for Full even when Compact succeeds.
    for (auto variant : {IconSize::Compact, IconSize::Full}) {
        try {
            auto data = std::make_unique<NsApplicationControlData>();
            const auto size = read_control(id, variant, *data);
            const std::string_view icon(reinterpret_cast<const char*>(data->icon), size - sizeof(data->nacp));
            if (icon.size() < 4) throw std::runtime_error("No image in control data");
            if (!moader::game_icon_media_type(icon)) {
                char detail[96];
                std::snprintf(detail, sizeof(detail), "Unsupported image (%zu bytes, header %02X%02X%02X%02X)",
                              icon.size(), data->icon[0], data->icon[1], data->icon[2], data->icon[3]);
                throw std::runtime_error(detail);
            }
            auto artwork = moader_ui::decode_art(data->icon, icon.size());
            if (!artwork) throw std::runtime_error("Image could not be decoded");
            return {std::string(icon), std::move(artwork)};
        } catch (const std::runtime_error& e) {
            failures += variant == IconSize::Compact ? "Compact icon (" : ", full icon (";
            failures += e.what();
            failures += ')';
        }
    }
    throw std::runtime_error(failures);
}

inline std::string installed_game_art(const std::string& title) {
    return read_installed_icon(title).encoded;
}
} // namespace moader_native
