// Real installed SDK declarations; only host newlib lock typedefs are supplied.
#include <cstdint>
typedef uint32_t _LOCK_T;
typedef struct { uint32_t lock, counter; } _LOCK_RECURSIVE_T;
#include "native/store_games.hpp"
#include <cassert>
#include <cstring>
#include <iostream>

namespace {
constexpr u64 Id = 0x0100123456780000ull;
constexpr Result Error = 0x5010;
using Source = moader::GameSource;
struct Fake {
    const char* fail = "";
    u64 id = Id;
    std::vector<u64> records{Id};
    std::vector<NsApplicationContentMetaStatus> rows{{NcmContentMetaType_Application, NcmStorageId_GameCard, 0, 0, 7, Id}};
    std::vector<u64> card_ids{Id}, later_card_ids;
    std::vector<bool> insertion;
    int record_count = -999, content_count = -999, page_count = -999, card_count = -999;
    bool program = true, change_content = false, change_count = false;
    u8 install_type = NcmContentInstallType_Full, key_type = NcmContentMetaType_Application;
    u64 key_id = Id;
    u32 key_version = 7;
    u8 acd_index = 1, apparent_platform = 1;
    unsigned control_reads = 0;
    unsigned ns_init = 0, ns_exit = 0, ncm_init = 0, ncm_exit = 0;
    unsigned record_reads = 0, insertion_reads = 0, card_reads = 0, count_reads = 0, page_reads = 0;
    unsigned card_failure_at = 0;
    unsigned db_open = 0, db_close = 0, cs_open = 0, cs_close = 0, program_reads = 0, has_reads = 0;
    std::vector<NcmStorageId> storages;
} fake;
Result result(const char* stage) { return std::strcmp(fake.fail, stage) == 0 ? Error : 0; }
void reset() { fake = {}; }
moader::GameAvailability lookup(Source source = Source::Unspecified, const char* title = "0100123456780000") {
    return moader_native::installed_game_availability(title, source);
}
void blocked(Source source, const char* fragment, const char* title = "0100123456780000") {
    auto value = lookup(source, title);
    if (value.load_allowed || value.detail.find(fragment) == std::string::npos) {
        std::cerr << "Expected block containing " << fragment << "; actual: " << value.load_allowed << ' ' << value.detail << '\n';
        std::abort();
    }
}
void noncard() { fake.rows[0].storageID = NcmStorageId_SdCard; }
void no_content_queries() {
    assert(fake.count_reads == 0 && fake.page_reads == 0 && fake.ncm_init == 0 && fake.ncm_exit == 0);
    assert(fake.db_open == 0 && fake.cs_open == 0 && fake.program_reads == 0 && fake.has_reads == 0);
}
}

extern "C" Result nsInitialize(void) { ++fake.ns_init; return result("ns_init"); }
extern "C" void nsExit(void) { ++fake.ns_exit; }
extern "C" Result nsGetApplicationControlData2(NsApplicationControlSource source, u64 id,
        NsApplicationControlData* buffer, size_t size, u8 flag, u8 index, u64* actual, u32* auxiliary) {
    ++fake.control_reads;
    assert(source == NsApplicationControlSource_Storage && id == fake.id && flag == 1 && index == 1);
    assert(size == sizeof(*buffer));
    if (result("control")) return Error;
    buffer->nacp.acd_index = fake.acd_index;
    buffer->nacp.apparent_platform = fake.apparent_platform;
    std::strcpy(buffer->nacp.lang_data.lang[0].name, "Base application fixture");
    *actual = sizeof(NacpStruct); *auxiliary = 1; return 0;
}
extern "C" Result setInitialize(void) { return 0; }
extern "C" void setExit(void) {}
extern "C" Result setGetSystemLanguage(u64* code) { *code = 1; return 0; }
extern "C" Result setMakeLanguage(u64, SetLanguage* language) { *language = SetLanguage_ENUS; return 0; }
extern "C" Result nsListApplicationRecord(NsApplicationRecord* rows, s32 count, s32 offset, s32* actual) {
    ++fake.record_reads;
    assert(count == 32 && offset >= 0 && offset % 32 == 0);
    if (result("records")) return Error;
    if (fake.record_count != -999) { *actual = fake.record_count; return 0; }
    *actual = 0;
    while (*actual < count && size_t(offset + *actual) < fake.records.size()) {
        rows[*actual].application_id = fake.records[offset + *actual]; ++*actual;
    }
    return 0;
}
extern "C" Result nsIsGameCardInserted(bool* out) {
    const size_t index = fake.insertion_reads++;
    if (result("inserted")) return Error;
    *out = fake.insertion.empty() ? true : fake.insertion[std::min(index, fake.insertion.size() - 1)];
    return 0;
}
extern "C" Result nsListApplicationIdOnGameCard(u64* ids, s32 capacity, s32* actual) {
    ++fake.card_reads;
    assert(capacity == 64);
    if (result("card_ids") || fake.card_reads == fake.card_failure_at) return Error;
    if (fake.card_count != -999) { *actual = fake.card_count; return 0; }
    const auto& values = fake.card_reads > 1 && !fake.later_card_ids.empty() ? fake.later_card_ids : fake.card_ids;
    *actual = values.size();
    for (s32 i = 0; i < std::min(capacity, *actual); ++i) ids[i] = values[i];
    return 0;
}
extern "C" Result nsCountApplicationContentMeta(u64 id, s32* actual) {
    assert(id == fake.id); ++fake.count_reads;
    if (result("count")) return Error;
    *actual = fake.content_count == -999 ? fake.rows.size() : fake.content_count;
    if (fake.change_count && fake.count_reads > 1) ++*actual;
    return 0;
}
extern "C" Result nsListApplicationContentMetaStatus(u64 id, s32 index,
        NsApplicationContentMetaStatus* rows, s32 capacity, s32* actual) {
    assert(id == fake.id && index >= 0); ++fake.page_reads;
    if (result("page")) return Error;
    if (fake.page_count != -999) { *actual = fake.page_count; return 0; }
    *actual = 0;
    while (*actual < capacity && size_t(index + *actual) < fake.rows.size()) {
        rows[*actual] = fake.rows[index + *actual];
        if (fake.change_content && fake.page_reads > 1) ++rows[*actual].version;
        ++*actual;
    }
    return 0;
}
extern "C" Result ncmInitialize(void) { ++fake.ncm_init; return result("ncm_init"); }
extern "C" void ncmExit(void) { ++fake.ncm_exit; }
extern "C" Result ncmOpenContentMetaDatabase(NcmContentMetaDatabase* db, NcmStorageId storage) {
    if (result("db_open")) return Error;
    ++fake.db_open; fake.storages.push_back(storage); db->s.session = 42; return 0;
}
extern "C" void ncmContentMetaDatabaseClose(NcmContentMetaDatabase* db) { assert(db->s.session == 42); ++fake.db_close; }
extern "C" Result ncmContentMetaDatabaseGetLatestContentMetaKey(NcmContentMetaDatabase*, NcmContentMetaKey* key, u64 id) {
    assert(id == fake.id);
    if (result("key")) return Error;
    key->id = fake.key_id; key->version = fake.key_version;
    key->type = fake.key_type; key->install_type = fake.install_type; return 0;
}
extern "C" Result ncmContentMetaDatabaseGetContentIdByTypeAndIdOffset(NcmContentMetaDatabase*,
        NcmContentId* content, const NcmContentMetaKey* key, NcmContentType type, u8 offset) {
    ++fake.program_reads;
    assert(key->id == fake.id && key->type == NcmContentMetaType_Application);
    assert(type == NcmContentType_Program && offset == 0);
    content->c[0] = 99; return result("program");
}
extern "C" Result ncmOpenContentStorage(NcmContentStorage* cs, NcmStorageId storage) {
    assert(storage == fake.storages.back());
    if (result("cs_open")) return Error;
    ++fake.cs_open; cs->s.session = 43; return 0;
}
extern "C" void ncmContentStorageClose(NcmContentStorage* cs) { assert(cs->s.session == 43); ++fake.cs_close; }
extern "C" Result ncmContentStorageHas(NcmContentStorage*, bool* out, const NcmContentId* content) {
    assert(content->c[0] == 99); ++fake.has_reads;
    if (result("has")) return Error;
    *out = fake.program; return 0;
}

int main() {
    using moader_ui::Presence;
    auto store_check = [] {
        moader_native::StorePresence games{{"010000000000F00D", Presence::Unknown},
                                           {"0100123456780000", Presence::Unknown}};
        const std::atomic<bool> cancel{false};
        moader_native::read_store_games(games, cancel);
        return games;
    };
    reset();
    fake.records.clear();
    auto store = store_check();
    assert(store[0].second == Presence::Absent &&
           store[1].second == Presence::Present); // Inserted card without a Home record.
    reset();
    fake.insertion = {false};
    noncard();
    assert(store_check()[1].second ==
           Presence::Present); // Installed digital game, zero mod folders involved.
    reset();
    fake.insertion = {false};
    noncard();
    fake.rows[0].meta_type = NcmContentMetaType_Patch;
    assert(store_check()[1].second == Presence::Absent);
    reset();
    fake.insertion = {false};
    noncard();
    fake.program = false;
    assert(store_check()[1].second == Presence::Absent); // Archived tile.
    reset();
    fake.insertion = {false};
    noncard();
    fake.fail = "has";
    assert(store_check()[1].second == Presence::Unknown);
    reset();
    noncard();
    fake.fail = "inserted";
    assert(store_check()[1].second == Presence::Present); // Digital discovery remains independent.
    reset(); assert(lookup().load_allowed);
    assert(fake.ns_init == 1 && fake.ns_exit == 1 && fake.card_reads == 2);
    no_content_queries();
    reset(); assert(lookup(Source::Cartridge).load_allowed);
    no_content_queries();
    // Physical admission is current-card presence, independent of the shape
    // or availability of installed-content inventory and Program probes.
    for (Source source : {Source::Unspecified, Source::Cartridge}) {
        reset(); fake.rows.clear(); assert(lookup(source).load_allowed); no_content_queries();
        reset(); fake.rows[0].meta_type = NcmContentMetaType_Patch;
        assert(lookup(source).load_allowed); no_content_queries();
        reset(); fake.rows[0].storageID = NcmStorageId_None;
        assert(lookup(source).load_allowed); no_content_queries();
        reset(); fake.program = false; assert(lookup(source).load_allowed); no_content_queries();
        for (const char* unused_stage : {"count", "page", "ncm_init", "db_open", "key", "program", "cs_open", "has"}) {
            reset(); fake.fail = unused_stage; assert(lookup(source).load_allowed); no_content_queries();
        }
    }
    reset(); noncard(); assert(lookup().load_allowed); // Downloaded key-card base still requires matching card.
    reset(); noncard(); fake.insertion = {false}; blocked(Source::Cartridge, "Insert this game's cartridge");
    assert(fake.ncm_init == 0 && fake.record_reads == 0);
    reset(); noncard(); fake.insertion = {false}; blocked(Source::Unspecified, "actual digital copy");
    reset(); fake.card_ids = {Id + 1}; blocked(Source::Cartridge, "different game"); assert(fake.count_reads == 0);
    reset(); fake.card_ids = {Id + 1}; blocked(Source::Unspecified, "actual digital copy");
    reset(); fake.card_ids = {Id + 1, Id, Id}; assert(lookup().load_allowed);
    // Digital source selection deliberately does not query the unrelated card slot.
    reset(); noncard(); fake.fail = "inserted"; assert(lookup(Source::Digital).load_allowed);
    assert(fake.insertion_reads == 0 && fake.card_reads == 0 && fake.storages == std::vector<NcmStorageId>{NcmStorageId_SdCard});
    assert(fake.ncm_init == 1 && fake.ncm_exit == 1 && fake.has_reads == 1);
    assert(fake.db_open == fake.db_close && fake.cs_open == fake.cs_close);
    reset(); noncard(); fake.rows[0].storageID = NcmStorageId_BuiltInUser; assert(lookup(Source::Digital).load_allowed);
    reset(); blocked(Source::Digital, "no available installed base Program"); assert(fake.ncm_init == 0);
    reset(); noncard(); fake.rows.push_back(fake.rows[0]); fake.rows.back().storageID = NcmStorageId_GameCard;
    assert(lookup(Source::Digital).load_allowed && fake.has_reads == 1); // Both-copy source choice excludes card storage.
    for (u8 meta : {u8(NcmContentMetaType_Patch), u8(NcmContentMetaType_AddOnContent), u8(NcmContentMetaType_Delta), u8(NcmContentMetaType_DataPatch)}) {
        reset(); noncard(); fake.rows[0].meta_type = meta;
        blocked(Source::Digital, "Update data alone"); assert(fake.ncm_init == 0);
    }
    reset(); fake.rows.clear(); blocked(Source::Digital, "no available installed base Program");
    reset(); noncard(); fake.program = false; blocked(Source::Digital, "no available installed base Program");
    for (u8 install : {u8(NcmContentInstallType_FragmentOnly), u8(NcmContentInstallType_Unknown)}) {
        reset(); noncard(); fake.install_type = install; blocked(Source::Digital, "full installation"); assert(fake.program_reads == 0);
    }
    reset(); noncard(); fake.key_type = NcmContentMetaType_Patch; blocked(Source::Digital, "inconsistent");
    reset(); noncard(); ++fake.key_id; blocked(Source::Digital, "inconsistent");
    reset(); noncard(); ++fake.key_version; blocked(Source::Digital, "inconsistent");
    reset(); noncard(); ++fake.rows[0].application_id; blocked(Source::Digital, "owner does not match");
    reset(); noncard(); fake.rows[0].rights_check = 255; assert(lookup(Source::Digital).load_allowed); // Opaque flags are not interpreted.
    reset(); fake.insertion = {true, false}; blocked(Source::Cartridge, "changed during verification");
    reset(); fake.insertion = {true, true, false}; blocked(Source::Cartridge, "changed during verification");
    reset(); fake.insertion = {true, true, true, false}; blocked(Source::Cartridge, "changed during verification");
    reset(); fake.later_card_ids = {Id + 1}; blocked(Source::Cartridge, "changed during verification");
    reset(); fake.card_failure_at = 2; blocked(Source::Cartridge, "0x00005010"); assert(fake.ns_exit == 1);
    reset(); noncard(); fake.change_content = true; blocked(Source::Digital, "content changed");
    reset(); noncard(); fake.change_count = true; blocked(Source::Digital, "content changed");
    for (int count : {-1, 0, 65}) {
        reset(); fake.card_count = count; blocked(Source::Cartridge, "incomplete or invalid count");
    }
    reset(); fake.card_ids = {0}; blocked(Source::Cartridge, "invalid identity");
    for (int count : {-1, 257}) {
        reset(); noncard(); fake.content_count = count; blocked(Source::Digital, "verification bound");
    }
    for (int count : {-1, 0, 2}) {
        reset(); noncard(); fake.page_count = count; blocked(Source::Digital, "incomplete or invalid count");
    }
    for (int count : {-1, 33}) {
        reset(); fake.record_count = count; blocked(Source::Cartridge, "invalid count");
    }
    // Exact native 04 owner wins over a plausible NX parent.
    reset(); fake.id = fake.key_id = 0x0400123456780000ull;
    fake.records = {Id, fake.id}; fake.card_ids = {fake.id}; fake.rows[0].application_id = fake.id;
    assert(lookup(Source::Cartridge, "0400123456780000").load_allowed);
    reset(); assert(lookup(Source::Cartridge, "0400123456780000").load_allowed && fake.control_reads == 0);
    reset(); assert(lookup(Source::Cartridge, "0400123456780001").load_allowed && fake.control_reads == 1);
    reset(); noncard(); assert(lookup(Source::Digital, "0400123456780001").load_allowed && fake.control_reads == 1);
    reset(); fake.acd_index = 0; blocked(Source::Cartridge, "Edition fields", "0400123456780001");
    reset(); fake.apparent_platform = 0; blocked(Source::Cartridge, "Edition fields", "0400123456780001");
    reset(); fake.fail = "control"; blocked(Source::Cartridge, "0x00005010", "0400123456780001"); assert(fake.ns_exit == 1);
    reset(); fake.records = {Id, Id + 1}; blocked(Source::Cartridge, "ambiguous", "0400123456780001");
    reset(); blocked(Source::Cartridge, "No supported record-backed", "0400123456780002");
    reset(); fake.records.assign(512, Id + 1); blocked(Source::Cartridge, "scan reached its limit"); assert(fake.record_reads == 16);
    for (Source source : {Source::Unspecified, Source::Cartridge}) for (bool patch_only : {false, true}) {
        reset(); fake.id = fake.key_id = 0x0100F43008C44000ull;
        fake.records = fake.card_ids = {fake.id}; fake.rows[0].application_id = fake.id;
        if (patch_only) fake.rows[0].meta_type = NcmContentMetaType_Patch;
        else fake.rows.clear();
        assert(lookup(source, "0400F43008C44001").load_allowed && fake.control_reads == 1);
        assert(fake.card_reads == 2 && fake.ns_exit == 1); no_content_queries();
    }
    reset(); fake.records.assign(32, Id + 1); fake.records.push_back(Id); assert(lookup().load_allowed && fake.record_reads == 2);
    for (const char* title : {"", "0", "0000000000000000", "0500123456780000", "010012345678000g", "01001234567800000", "0100000000000001"}) {
        reset(); blocked(Source::Digital, "Invalid game Title ID", title); assert(fake.ns_init == 0);
    }
    reset(); blocked(static_cast<Source>(99), "Invalid Game copy"); assert(fake.ns_init == 0);
    for (const char* stage : {"ns_init", "records", "inserted", "card_ids"}) {
        reset(); fake.fail = stage; blocked(Source::Cartridge, "0x00005010");
        assert(fake.ns_exit == (std::strcmp(stage, "ns_init") != 0));
        no_content_queries();
    }
    for (const char* stage : {"ns_init", "records", "count", "page", "ncm_init", "db_open", "key", "program", "cs_open", "has"}) {
        reset(); noncard(); fake.fail = stage; blocked(Source::Digital, "0x00005010");
        assert(fake.ns_exit == (std::strcmp(stage, "ns_init") != 0));
        assert(fake.ncm_exit == (fake.ncm_init && std::strcmp(stage, "ncm_init") != 0));
        assert(fake.db_open == fake.db_close && fake.cs_open == fake.cs_close);
    }
    // More than one bounded status page; non-Application rows never enter NCM.
    reset(); noncard(); auto patch = fake.rows[0]; patch.meta_type = NcmContentMetaType_Patch;
    fake.rows.insert(fake.rows.begin(), 40, patch); assert(lookup(Source::Digital).load_allowed && fake.page_reads == 4 && fake.has_reads == 1);
    std::cout << "Game availability: matching-card admission without installed-content probes, digital base proof, snapshots, bounds, NX/Ounce/Edition identities and cleanup passed.\n";
}
