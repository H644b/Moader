#pragma once
#include "game_names.hpp"

namespace moader_native {
// The name and cover must resolve to the same installed application/Edition.
inline std::string installed_game_art(const std::string& title) {
    using namespace game_names_detail;
    std::lock_guard<std::mutex> lock(ns_query_mutex());
    const auto routing = parse_id(title);
    if (R_FAILED(nsInitialize()))
        return {};
    struct Guard {
        ~Guard() { nsExit(); }
    } guard;
    const auto id = metadata_identity(routing);
    auto data = std::make_unique<NsApplicationControlData>();
    u64 size = 0;
    u32 auxiliary = 0;
    if (R_FAILED(nsGetApplicationControlData2(NsApplicationControlSource_Storage, id.application_id, data.get(),
                                              sizeof(*data), 0, id.acd_index, &size, &auxiliary)) ||
        size <= sizeof(data->nacp) || size > sizeof(*data))
        return {};
    if (id.acd_index == 1 && (data->nacp.acd_index != 1 || data->nacp.apparent_platform != 1))
        return {};
    const size_t count = size - sizeof(data->nacp);
    if (count < 4 || data->icon[0] != 0xff || data->icon[1] != 0xd8)
        return {};
    return {reinterpret_cast<const char*>(data->icon), count};
}
} // namespace moader_native
