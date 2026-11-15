#pragma once
extern "C" {
#include <switch/result.h>
#include <switch/services/ns.h>
#include <switch/services/ncm.h>
}
#include "moader/library.hpp"
#include "game_names.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace moader_native {
namespace game_availability_detail {
inline void checked(const char* stage, Result result) {
    if (R_SUCCEEDED(result)) return;
    char text[160];
    std::snprintf(text, sizeof(text), "%s returned 0x%08X", stage, result);
    throw std::runtime_error(text);
}
inline u64 parse_id(const std::string& title) {
    if (title.size() != 16) throw std::runtime_error("Invalid game Title ID");
    u64 id = 0;
    for (unsigned char c : title) {
        unsigned digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else throw std::runtime_error("Invalid game Title ID");
        id = (id << 4) | digit;
    }
    if ((id >> 56 != 1 && id >> 56 != 4) || (id & 0x00ffffffffffffffull) < 0x10000)
        throw std::runtime_error("Invalid game Title ID");
    return id;
}
struct NsSession {
    NsSession() { checked("Game availability NS initialization", nsInitialize()); }
    ~NsSession() { nsExit(); }
    NsSession(const NsSession&) = delete;
    NsSession& operator=(const NsSession&) = delete;
};
struct Card {
    bool inserted = false;
    std::vector<u64> applications;
    bool operator==(const Card& other) const {
        return inserted == other.inserted && applications == other.applications;
    }
};
inline Card read_card() {
    Card card;
    checked("Game-card insertion query", nsIsGameCardInserted(&card.inserted));
    if (!card.inserted) return card;
    // No pagination offset exists. An oversized/invalid total is unknown,
    // never evidence that this title is absent from the card.
    std::array<u64, 64> ids{};
    s32 total = 0;
    checked("Game-card application query", nsListApplicationIdOnGameCard(ids.data(), ids.size(), &total));
    if (total <= 0 || total > static_cast<s32>(ids.size()))
        throw std::runtime_error("Game-card application query returned an incomplete or invalid count");
    card.applications.assign(ids.begin(), ids.begin() + total);
    if (std::find(card.applications.begin(), card.applications.end(), 0) != card.applications.end())
        throw std::runtime_error("Game-card application query returned an invalid identity");
    std::sort(card.applications.begin(), card.applications.end());
    card.applications.erase(std::unique(card.applications.begin(), card.applications.end()), card.applications.end());
    bool inserted_after = false;
    checked("Game-card insertion recheck", nsIsGameCardInserted(&inserted_after));
    if (!inserted_after) throw std::runtime_error("Game card changed during verification; try again");
    return card;
}
using Rows = std::vector<NsApplicationContentMetaStatus>;
inline auto row_identity(const NsApplicationContentMetaStatus& row) {
    return std::make_tuple(row.meta_type, row.storageID, row.application_id, row.version, row.rights_check);
}
inline Rows read_content(u64 id) {
    static_assert(sizeof(NsApplicationContentMetaStatus) == 16, "Unexpected NS content-status layout");
    s32 expected = 0;
    checked("Application content count", nsCountApplicationContentMeta(id, &expected));
    if (expected < 0 || expected > 256) throw std::runtime_error("Application content count exceeds the verification bound");
    Rows rows;
    while (static_cast<s32>(rows.size()) < expected) {
        std::array<NsApplicationContentMetaStatus, 32> page{};
        const s32 capacity = std::min<s32>(page.size(), expected - rows.size());
        s32 actual = 0;
        checked("Application content page", nsListApplicationContentMetaStatus(id, rows.size(), page.data(), capacity, &actual));
        if (actual <= 0 || actual > capacity) throw std::runtime_error("Application content page returned an incomplete or invalid count");
        rows.insert(rows.end(), page.begin(), page.begin() + actual);
    }
    s32 after = 0;
    checked("Application content count recheck", nsCountApplicationContentMeta(id, &after));
    if (after != expected) throw std::runtime_error("Application content changed during verification; try again");
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return row_identity(a) < row_identity(b); });
    return rows;
}
inline bool same_content(const Rows& a, const Rows& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (row_identity(a[i]) != row_identity(b[i])) return false;
    return true;
}
struct NcmSession {
    NcmSession() { checked("Game availability NCM initialization", ncmInitialize()); }
    ~NcmSession() { ncmExit(); }
    NcmSession(const NcmSession&) = delete;
    NcmSession& operator=(const NcmSession&) = delete;
};
inline bool program_present(u64 id, const NsApplicationContentMetaStatus& row) {
    NcmContentMetaDatabase db{};
    const auto storage = static_cast<NcmStorageId>(row.storageID);
    checked("Base content database", ncmOpenContentMetaDatabase(&db, storage));
    struct DbGuard { NcmContentMetaDatabase& db; ~DbGuard() { ncmContentMetaDatabaseClose(&db); } } db_guard{db};
    NcmContentMetaKey key{};
    checked("Base content key", ncmContentMetaDatabaseGetLatestContentMetaKey(&db, &key, id));
    if (key.id != id || key.type != NcmContentMetaType_Application || key.version != row.version)
        throw std::runtime_error("Base application content identity changed or is inconsistent");
    if (key.install_type != NcmContentInstallType_Full)
        throw std::runtime_error("Base application content is not a verified full installation");
    NcmContentId content{};
    // This checks the resolved BASE owner's Program zero, including when an
    // Edition routes through a different program ID. It does not claim that
    // the Edition executable/update or every launch dependency is available.
    checked("Base Program content", ncmContentMetaDatabaseGetContentIdByTypeAndIdOffset(
        &db, &content, &key, NcmContentType_Program, 0));
    NcmContentStorage cs{};
    checked("Base content storage", ncmOpenContentStorage(&cs, storage));
    struct StorageGuard { NcmContentStorage& cs; ~StorageGuard() { ncmContentStorageClose(&cs); } } storage_guard{cs};
    bool present = false;
    checked("Base Program presence", ncmContentStorageHas(&cs, &present, &content));
    return present;
}
struct Observation {
    Card card;
    bool base_present = false;
};
inline moader::GameAvailability decide(u64 id, moader::GameSource source, const Observation& observation) {
    if (source != moader::GameSource::Unspecified && source != moader::GameSource::Cartridge && source != moader::GameSource::Digital)
        return {false, "Invalid Game copy setting."};
    if (source != moader::GameSource::Digital) {
        if (!observation.card.inserted) return {false, source == moader::GameSource::Cartridge
            ? "Insert this game's cartridge to activate its mods."
            : "Insert this game's cartridge, or choose your actual digital copy in Game copy."};
        if (std::find(observation.card.applications.begin(), observation.card.applications.end(), id) == observation.card.applications.end())
            return {false, source == moader::GameSource::Cartridge
                ? "The inserted cartridge is for a different game. Insert this game's cartridge."
                : "The inserted cartridge does not match. Insert this game's cartridge, or choose your actual digital copy in Game copy."};
    }
    if (!observation.base_present) return {false, source == moader::GameSource::Digital
        ? "The selected digital copy has no available installed base Program. Update data alone is insufficient."
        : "The base game Program is unavailable. Update data alone is insufficient."};
    return {true, {}};
}
inline moader::GameAvailability query(u64 id, moader::GameSource source) {
    if (source != moader::GameSource::Unspecified && source != moader::GameSource::Cartridge && source != moader::GameSource::Digital)
        return {false, "Invalid Game copy setting."};
    NsSession ns;
    Observation observation;
    if (source != moader::GameSource::Digital) {
        observation.card = read_card();
        // A known empty slot needs no identity/content query. Do not turn it
        // into an unrelated metadata error or attempt to inspect replacements.
        if (!observation.card.inserted) return decide(id, source, observation);
    }
    const auto identity = game_names_detail::metadata_identity(id);
    if (identity.acd_index == 1)
        (void)game_names_detail::read_name(identity.application_id, 1);
    // Use the existing bounded record-backed mapping and validated Edition
    // control fields, then test the base owner's card and Program. An ACD
    // selector is never reused as a Program id-offset.
    id = identity.application_id;
    if (source != moader::GameSource::Digital &&
            std::find(observation.card.applications.begin(), observation.card.applications.end(), id) == observation.card.applications.end())
        return decide(id, source, observation);
    const auto before = read_content(id);
    std::vector<NsApplicationContentMetaStatus> candidates;
    for (const auto& row : before) {
        if (row.meta_type != NcmContentMetaType_Application) continue;
        if (row.application_id != id) throw std::runtime_error("Base application owner does not match the exact game identity");
        const bool noncard = row.storageID == NcmStorageId_BuiltInUser || row.storageID == NcmStorageId_SdCard;
        if (noncard || (source != moader::GameSource::Digital && row.storageID == NcmStorageId_GameCard))
            candidates.push_back(row);
    }
    if (!candidates.empty()) {
        NcmSession ncm;
        for (const auto& row : candidates)
            if (program_present(id, row)) observation.base_present = true;
    }
    if (!same_content(before, read_content(id)))
        throw std::runtime_error("Application content changed during verification; try again");
    if (source != moader::GameSource::Digital && !(observation.card == read_card()))
        throw std::runtime_error("Game card changed during verification; try again");
    return decide(id, source, observation);
}
}

// Digital is an explicit user declaration supplied by the application, never
// an entitlement conclusion inferred from storage or opaque NS rights flags.
inline moader::GameAvailability installed_game_availability(const std::string& title, moader::GameSource source) {
    try {
        const u64 id = game_availability_detail::parse_id(title);
        // Shared with the name resolver; never call installed_game_name here.
        std::lock_guard<std::mutex> lock(game_names_detail::ns_query_mutex());
        return game_availability_detail::query(id, source);
    } catch (const std::exception& error) {
        return {false, std::string("Could not verify game availability: ") + error.what() + "."};
    }
}
}
