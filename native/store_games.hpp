#pragma once
#include "game_availability.hpp"
#include "moader/store_presence.hpp"
extern "C" {
#include <switch/services/fs.h>
#include <switch/services/pdm.h>
}
#include <atomic>
#include <fcntl.h>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

namespace moader_native {
using moader::StorePresence;
namespace store_games_detail {
inline bool save_titles(FsSaveDataSpaceId space, std::set<u64>& known, const std::atomic<bool>& cancel) {
    FsSaveDataInfoReader reader{};
    if (R_FAILED(fsOpenSaveDataInfoReader(&reader, space))) return false;
    struct Close { FsSaveDataInfoReader& reader; ~Close() { fsSaveDataInfoReaderClose(&reader); } } close{reader};
    std::array<FsSaveDataInfo, 32> rows{};
    while (!cancel) {
        s64 count = 0;
        if (R_FAILED(fsSaveDataInfoReaderRead(&reader, rows.data(), rows.size(), &count)) ||
            count < 0 || count > s64(rows.size())) return false;
        if (!count) return true;
        for (s64 i = 0; i < count; ++i)
            if (rows[i].save_data_type == FsSaveDataType_Account || rows[i].save_data_type == FsSaveDataType_Device)
                known.insert(rows[i].application_id);
    }
    return false;
}

inline bool registered_titles(std::set<u64>& known, const std::atomic<bool>& cancel) {
    using namespace game_availability_detail;
    std::lock_guard lock(game_names_detail::ns_query_mutex());
    NsSession ns;
    bool card_known = true;
    try {
        const auto card = read_card();
        known.insert(card.applications.begin(), card.applications.end());
    } catch (const std::exception&) { card_known = false; }
    std::array<NsApplicationRecord, 32> rows{};
    for (s32 page = 0; page < 128 && !cancel; ++page) {
        s32 count = 0;
        if (R_FAILED(nsListApplicationRecord(rows.data(), rows.size(), page * rows.size(), &count)) ||
            count < 0 || count > s32(rows.size())) return false;
        for (s32 i = 0; i < count; ++i) known.insert(rows[i].application_id);
        if (count < s32(rows.size())) return card_known;
    }
    return false;
}

// Switch 2 Editions can share a base game's HOME record and save owner. Use
// the console's control data before associating a catalog Edition with that record.
inline void verify_edition(u64 owner, u8 index) {
    std::lock_guard lock(game_names_detail::ns_query_mutex());
    game_availability_detail::NsSession ns;
    auto control = std::make_unique<NsApplicationControlData>();
    game_names_detail::read_control({owner, index}, game_names_detail::IconSize::Compact, *control);
}

inline void scan(StorePresence& titles, const std::atomic<bool>& cancel, const std::string& remembered) {
    using moader::GamePresence;
    std::set<u64> known;
    bool records_complete = false;
    try { records_complete = registered_titles(known, cancel); } catch (const std::exception&) {}
    if (cancel) return;
    const bool saves_complete = save_titles(FsSaveDataSpaceId_User, known, cancel);
    save_titles(FsSaveDataSpaceId_SdUser, known, cancel);
    if (cancel) return;
    const bool history_available = R_SUCCEEDED(pdmqryInitialize());
    struct History { bool open; ~History() { if (open) pdmqryExit(); } } history{history_available};
    auto recognized = [&](u64 id) {
        if (known.count(id)) return true;
        PdmPlayStatistics play{};
        return history_available && R_SUCCEEDED(pdmqryQueryPlayStatisticsByApplicationId(id, false, &play)) &&
               play.program_id == id && (play.total_launches || play.playtime);
    };
    for (auto& [title, presence] : titles) {
        if (cancel) return;
        const u64 id = game_names_detail::parse_id(title);
        char key[17];
        std::snprintf(key, sizeof(key), "%016llx", static_cast<unsigned long long>(id));
        const auto file = remembered + '/' + key;
        struct stat st{};
        if (!remembered.empty() && !lstat(file.c_str(), &st) && S_ISREG(st.st_mode)) {
            presence = GamePresence::Present;
            continue;
        }
        bool found = recognized(id);
        if (!found && id >> 56 == 4) {
            const u64 base = (id & UINT64_C(0x00ffffffffffffff)) | UINT64_C(0x0100000000000000);
            const bool same = recognized(base), previous = recognized(base - 1);
            if (same != previous) {
                try { verify_edition(same ? base : base - 1, same ? 0 : 1); found = true; }
                catch (const std::exception&) { presence = GamePresence::Unknown; continue; }
            }
        }
        if (!found) {
            presence = records_complete && saves_complete && history_available ? GamePresence::Absent : GamePresence::Unknown;
            continue;
        }
        presence = GamePresence::Present;
        // One immutable marker per title also works when the browser and NRO
        // discover the same card concurrently. Cache failures leave live results usable.
        if (!remembered.empty()) {
            mkdir(remembered.c_str(), 0777);
            const int fd = open(file.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0666);
            if (fd >= 0) close(fd);
        }
    }
}
} // namespace store_games_detail

inline void read_store_games(StorePresence& titles, const std::atomic<bool>& cancel) {
    store_games_detail::scan(titles, cancel, "sdmc:/moader/known-games");
}
inline void web_store_games(StorePresence& titles) {
    const std::atomic<bool> cancel{false};
    read_store_games(titles, cancel);
}
} // namespace moader_native
