#pragma once
#include "game_availability.hpp"
#include "store_inventory.hpp"

namespace moader_native {
inline void read_store_games(StorePresence& titles, const std::atomic<bool>& cancel) {
    using namespace game_availability_detail;
    using moader_ui::Presence;
    std::vector<u64> records;
    bool complete = false, card_known = true;
    Card card;
    {
        std::lock_guard lock(game_names_detail::ns_query_mutex());
        NsSession ns;
        try {
            card = read_card();
        } catch (const std::exception&) {
            card_known = false;
        }
        for (int page = 0; page < 128 && !cancel; ++page) {
            std::array<NsApplicationRecord, 32> rows{};
            s32 count = 0;
            auto rc = nsListApplicationRecord(rows.data(), rows.size(), page * rows.size(), &count);
            if (R_FAILED(rc) || count < 0 || count > s32(rows.size()))
                break;
            for (int i = 0; i < count; ++i)
                if (rows[i].application_id)
                    records.push_back(rows[i].application_id);
            if (count < s32(rows.size())) {
                complete = true;
                break;
            }
        }
    }
    for (auto& [title, presence] : titles) {
        if (cancel)
            return;
        const auto id = game_names_detail::parse_id(title);
        if (std::find(card.applications.begin(), card.applications.end(), id) != card.applications.end()) {
            presence = Presence::Present;
            continue;
        }
        const bool candidate = std::any_of(records.begin(), records.end(), [&](u64 record) {
            if (record == id)
                return true;
            const u64 edition = (record & 0x00ffffffffffffffULL) | 0x0400000000000000ULL;
            return record >> 56 == 1 && id >> 56 == 4 && id >= edition && id - edition <= 1;
        });
        if (!candidate && complete) {
            presence = card_known ? Presence::Absent : Presence::Unknown;
            continue;
        }
        std::lock_guard lock(game_names_detail::ns_query_mutex());
        bool physical_unknown = !card_known;
        if (card.inserted) {
            try {
                if (query(id, moader::GameSource::Cartridge).load_allowed) {
                    presence = Presence::Present;
                    continue;
                }
            } catch (const std::exception&) {
                physical_unknown = true;
            }
        }
        try {
            // Reuse the Library's base Program and Edition identity checks. An archived
            // Home tile, an update or an existing mods folder does not prove installation.
            if (query(id, moader::GameSource::Digital).load_allowed)
                presence = Presence::Present;
            else
                presence = physical_unknown ? Presence::Unknown : Presence::Absent;
        } catch (const std::exception&) {
            presence = Presence::Unknown;
        }
    }
}
} // namespace moader_native
