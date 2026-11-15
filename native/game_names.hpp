#pragma once
extern "C" {
#include <switch/result.h>
#include <switch/services/ns.h>
#include <switch/services/set.h>
}
#include "metadata_names.hpp"
#include <array>
#include <algorithm>
#include <vector>
#include <memory>
#include <string>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <mutex>

namespace moader_native {
namespace game_names_detail {
inline std::mutex& ns_query_mutex() { static std::mutex mutex; return mutex; }
inline std::runtime_error failure(const char* operation, Result rc) {
    char text[128]; std::snprintf(text, sizeof(text), "%s returned 0x%08X", operation, rc);
    return std::runtime_error(text);
}
inline u64 parse_id(const std::string& title) {
    if (title.size() != 16) throw std::invalid_argument("Invalid game Title ID");
    u64 id = 0;
    for (unsigned char c : title) {
        unsigned digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else throw std::invalid_argument("Invalid game Title ID");
        id = (id << 4) | digit;
    }
    if (!id) throw std::invalid_argument("Invalid game Title ID");
    return id;
}
inline size_t preferred_language() {
    // Owning SDK nacpGetLanguageEntry mapping; parser handles empty/invalid entries.
    if (R_FAILED(setInitialize())) return 0;
    struct Guard { ~Guard() { setExit(); } } guard;
    u64 code = 0;
    SetLanguage language = SetLanguage_ENUS;
    if (R_FAILED(setGetSystemLanguage(&code)) || R_FAILED(setMakeLanguage(code, &language))) return 0;
    constexpr std::array<unsigned, 18> entries{{2, 0, 3, 4, 7, 6, 14, 12, 8, 10, 11, 13, 1, 9, 5, 14, 13, 15}};
    return static_cast<unsigned>(language) < entries.size() ? entries[language] : 0;
}
struct MetadataIdentity { u64 application_id; u8 acd_index; };
inline MetadataIdentity metadata_identity(u64 routing_id) {
    // Exact records win, including native 04 applications. A nonexact match
    // requires a completed bounded scan: unseen records could be ambiguous.
    constexpr size_t PageSize = 32, PageLimit = 16;
    std::vector<u64> ids;
    bool complete = false;
    for (size_t page = 0; page < PageLimit; ++page) {
        std::array<NsApplicationRecord, PageSize> records{};
        s32 count = 0;
        const Result rc = nsListApplicationRecord(records.data(), PageSize, page * PageSize, &count);
        if (R_FAILED(rc)) throw failure("Game application records", rc);
        if (count < 0 || count > static_cast<s32>(PageSize))
            throw std::runtime_error("Game application records returned an invalid count");
        for (s32 i = 0; i < count; ++i) {
            const u64 id = records[i].application_id;
            if (id == routing_id) return {id, 0};
            if (id && std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
        }
        if (count < static_cast<s32>(PageSize)) { complete = true; break; }
    }
    if (!complete) throw std::runtime_error("Game application records scan reached its limit; metadata identity unresolved");
    MetadataIdentity selected{};
    if ((routing_id >> 56) == 4) for (u64 id : ids) {
        if ((id >> 56) != 1) continue;
        // Documented forward NX -> Ounce update relationship, preserving all
        // other bits. Support only delta 0 and the demonstrated Edition index 1.
        // ProgramIndex is not a general-purpose ACD selector or low-byte mask.
        const u64 forward = (id & 0x00ffffffffffffffull) | 0x0400000000000000ull;
        if (routing_id < forward || routing_id - forward > 1) continue;
        if (selected.application_id) throw std::runtime_error("Game metadata identity is ambiguous across application records");
        selected = {id, static_cast<u8>(routing_id == forward ? 0 : 1)};
    }
    if (!selected.application_id) throw std::runtime_error("No supported record-backed game metadata identity");
    return selected;
}
inline std::string read_name(u64 application_id, u8 acd_index) {
    auto control = std::make_unique<NsApplicationControlData>();
    u64 actual = 0;
    u32 auxiliary = 0; // Opaque: actual console returned 1 on successful requests.
    const Result rc = nsGetApplicationControlData2(NsApplicationControlSource_Storage, application_id,
        control.get(), sizeof(*control), 1, acd_index, &actual, &auxiliary);
    if (R_FAILED(rc)) throw failure("Installed control metadata", rc);
    if (actual < sizeof(NacpStruct) || actual > sizeof(*control))
        throw std::runtime_error("Installed control metadata returned an invalid output size");
    if (acd_index == 1 && (control->nacp.acd_index != 1 || control->nacp.apparent_platform != 1))
        throw std::runtime_error("Installed Edition control metadata did not match Edition fields");
    const auto parsed = moader_metadata::parse_name(control->nacp, static_cast<size_t>(actual), preferred_language());
    if (!parsed.valid) throw std::runtime_error("Installed control metadata: " + parsed.error);
    return parsed.name;
}
}
// Read installed control metadata. No network lookup, title launch or writes.
inline std::string installed_game_name(const std::string& title) {
    std::lock_guard<std::mutex> lock(game_names_detail::ns_query_mutex());
    const u64 id = game_names_detail::parse_id(title);
    const Result initialized = nsInitialize();
    if (R_FAILED(initialized)) throw game_names_detail::failure("Game metadata service", initialized);
    struct NsGuard { ~NsGuard() { nsExit(); } } guard;
    const auto metadata = game_names_detail::metadata_identity(id);
    return game_names_detail::read_name(metadata.application_id, metadata.acd_index);
}
}
