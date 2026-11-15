// Use the actual SDK NS/NACP/settings declarations. Only newlib lock typedefs
// needed while parsing those headers are supplied for the macOS host.
#include <cstdint>
typedef uint32_t _LOCK_T;
typedef struct { uint32_t lock, counter; } _LOCK_RECURSIVE_T;
#include "native/game_names.hpp"
#include <cassert>
#include <functional>
#include <vector>

namespace {
constexpr u64 DirectId = 0x0100123456780000ull;
constexpr Result Failure = 0x5010;
struct Fake {
    Result ns_result = 0, control_result = 0, records_result = 0, set_result = 0, language_result = 0, convert_result = 0;
    unsigned initialized = 0, exited = 0, reads = 0, record_reads = 0, set_initialized = 0, set_exited = 0;
    SetLanguage language = SetLanguage_ENUS;
    u64 expected_id = DirectId, actual_size = sizeof(NacpStruct);
    u8 expected_acd_index = 0;
    s32 invalid_count = 0;
    std::vector<u64> records{DirectId};
    NacpStruct nacp{};
} fake;
void name(NacpLanguageEntry& entry, const char* value) {
    assert(std::strlen(value) < sizeof(entry.name));
    std::strcpy(entry.name, value);
}
void reset() { fake = {}; name(fake.nacp.lang_data.lang[0], "English fixture"); }
void fails(const std::function<void()>& operation, const char* fragment) {
    try { operation(); assert(false && "expected failure"); }
    catch (const std::exception& error) { assert(std::string(error.what()).find(fragment) != std::string::npos); }
}
std::string lookup(const char* id = "0100123456780000") { return moader_native::installed_game_name(id); }
}

extern "C" Result nsInitialize(void) { ++fake.initialized; fake.record_reads = 0; return fake.ns_result; }
extern "C" void nsExit(void) { ++fake.exited; }
extern "C" Result nsListApplicationRecord(NsApplicationRecord* records, s32 count, s32 offset, s32* actual) {
    assert(count == 32 && offset == static_cast<s32>(fake.record_reads * 32));
    ++fake.record_reads;
    if (R_FAILED(fake.records_result)) return fake.records_result;
    if (fake.invalid_count) { *actual = fake.invalid_count; return 0; }
    *actual = 0;
    while (*actual < count && size_t(offset + *actual) < fake.records.size()) {
        records[*actual].application_id = fake.records[offset + *actual]; ++*actual;
    }
    return 0;
}
extern "C" Result nsGetApplicationControlData2(NsApplicationControlSource source, u64 id,
        NsApplicationControlData* buffer, size_t size, u8 flag, u8 index, u64* actual, u32* auxiliary) {
    ++fake.reads;
    assert(source == NsApplicationControlSource_Storage && id == fake.expected_id && flag == 1 && index == fake.expected_acd_index);
    assert(size == sizeof(NsApplicationControlData) && *actual == 0 && *auxiliary == 0);
    const auto* bytes = reinterpret_cast<const unsigned char*>(buffer);
    for (size_t i = 0; i < size; ++i) assert(bytes[i] == 0);
    if (R_FAILED(fake.control_result)) return fake.control_result;
    buffer->nacp = fake.nacp; *actual = fake.actual_size;
    *auxiliary = 1; // Device success returned 1, contradicting the header's comment.
    return 0;
}
extern "C" Result setInitialize(void) { ++fake.set_initialized; return fake.set_result; }
extern "C" void setExit(void) { ++fake.set_exited; }
extern "C" Result setGetSystemLanguage(u64* code) { *code = 42; return fake.language_result; }
extern "C" Result setMakeLanguage(u64 code, SetLanguage* language) {
    assert(code == 42); *language = fake.language; return fake.convert_result;
}

int main() {
    reset(); assert(lookup() == "English fixture");
    assert(fake.initialized == 1 && fake.exited == 1 && fake.reads == 1 && fake.set_exited == 1);
    reset(); fake.language = SetLanguage_JA; name(fake.nacp.lang_data.lang[2], "日本語の名前");
    assert(lookup() == "日本語の名前");
    reset(); fake.language = SetLanguage_ENGB; name(fake.nacp.lang_data.lang[1], "British fixture");
    assert(lookup() == "British fixture");
    reset(); fake.language = static_cast<SetLanguage>(31); assert(lookup() == "English fixture");
    reset(); fake.language = SetLanguage_JA; assert(lookup() == "English fixture"); // Empty preferred entry.
    reset(); fake.set_result = Failure; assert(lookup() == "English fixture" && fake.set_exited == 0);
    reset(); fake.language_result = Failure; assert(lookup() == "English fixture" && fake.set_exited == 1);
    reset(); fake.convert_result = Failure; assert(lookup() == "English fixture" && fake.set_exited == 1);
    reset(); fake.ns_result = Failure; fails([] { lookup(); }, "0x00005010");
    assert(fake.reads == 0 && fake.exited == 0);
    reset(); fake.control_result = Failure; fails([] { lookup(); }, "Installed control metadata returned 0x00005010");
    assert(fake.exited == 1 && fake.reads == 1 && fake.set_initialized == 0);
    for (u64 invalid_size : {u64(sizeof(NacpStruct) - 1), u64(sizeof(NsApplicationControlData) + 1)}) {
        reset(); fake.actual_size = invalid_size; fails([] { lookup(); }, "invalid output size");
        assert(fake.exited == 1 && fake.set_initialized == 0);
    }
    reset(); fake.nacp = {}; fails([] { lookup(); }, "No nonempty title entry"); assert(fake.exited == 1);
    reset(); fake.nacp.titles_data_format = 3; fails([] { lookup(); }, "Unsupported title data format");
    reset(); std::memset(fake.nacp.lang_data.lang[0].name, 'x', 512);
    fails([] { lookup(); }, "No valid UTF-8 title entry");
    // Real raw DEFLATE through the production resolver, including preferred language.
    reset(); fake.language = SetLanguage_JA;
    std::array<NacpLanguageEntry, 32> entries{};
    name(entries[0], "Compressed English"); name(entries[2], "圧縮された名前");
    z_stream stream{};
    assert(deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) == Z_OK);
    fake.nacp = {}; fake.nacp.titles_data_format = 1;
    stream.next_in = reinterpret_cast<Bytef*>(entries.data()); stream.avail_in = sizeof(entries);
    stream.next_out = fake.nacp.lang_data.compressed_data.buffer;
    stream.avail_out = sizeof(fake.nacp.lang_data.compressed_data.buffer);
    assert(deflate(&stream, Z_FINISH) == Z_STREAM_END);
    fake.nacp.lang_data.compressed_data.buffer_size = stream.total_out;
    assert(deflateEnd(&stream) == Z_OK);
    assert(lookup() == "圧縮された名前");
    --fake.nacp.lang_data.compressed_data.buffer_size;
    fails([] { lookup(); }, "DEFLATE");
    for (const char* invalid : {"", "0", "0000000000000000", "0100123456780000x", "+100123456780000", "0x00123456780000", "01001234567800gg"}) {
        reset(); fails([&] { lookup(invalid); }, "Invalid game Title ID"); assert(fake.initialized == 0);
    }
    reset(); fake.expected_id = 0x0100abcdef120000ull;
    fake.records = {fake.expected_id};
    assert(lookup("0100ABCDEF120000") == "English fixture");
    // Exact native identity wins over even a plausible cross-platform record.
    reset(); fake.expected_id = 0x0400123456780001ull;
    fake.records = {DirectId, fake.expected_id}; assert(lookup("0400123456780001") == "English fixture");
    reset(); assert(lookup("0400123456780000") == "English fixture"); // Documented delta 0.
    reset(); fake.expected_acd_index = 1; fake.nacp.acd_index = 1; fake.nacp.apparent_platform = 1;
    assert(lookup("0400123456780001") == "English fixture"); // Bounded index 1, validated Edition.
    assert(fake.expected_id == DirectId && fake.expected_acd_index == 1 && fake.reads == 1);
    reset(); fake.expected_acd_index = 1;
    fails([] { lookup("0400123456780001"); }, "did not match Edition fields");
    assert(fake.reads == 1 && fake.exited == 1); // No ACD0 retry to silently label it as the base game.
    reset(); fake.expected_acd_index = 1; fake.nacp.acd_index = 1;
    fails([] { lookup("0400123456780001"); }, "did not match Edition fields");
    reset(); fake.expected_acd_index = 1; fake.control_result = Failure;
    fails([] { lookup("0400123456780001"); }, "0x00005010"); assert(fake.reads == 1);
    reset(); fake.records = {DirectId, DirectId + 1};
    fails([] { lookup("0400123456780001"); }, "ambiguous"); assert(fake.reads == 0);
    reset(); fake.records = {DirectId, DirectId}; fake.expected_acd_index = 1;
    fake.nacp.acd_index = 1; fake.nacp.apparent_platform = 1;
    assert(lookup("0400123456780001") == "English fixture"); // Duplicates are not distinct parents.
    for (const char* unsupported : {"0400123456780002", "0500123456780001", "0100123456780001", "0400ffffffffffff"}) {
        reset(); fails([&] { lookup(unsupported); }, "No supported record-backed"); assert(fake.reads == 0);
    }
    reset(); fake.records.clear(); fails([] { lookup(); }, "No supported record-backed");
    reset(); fake.records_result = Failure; fails([] { lookup(); }, "Game application records returned 0x00005010");
    assert(fake.exited == 1 && fake.reads == 0);
    for (s32 invalid_count : {-1, 33}) {
        reset(); fake.invalid_count = invalid_count; fails([] { lookup(); }, "invalid count"); assert(fake.reads == 0);
    }
    // Full first page must not imply complete enumeration or unique association.
    reset(); fake.records.assign(32, DirectId); fake.expected_acd_index = 1;
    fake.nacp.acd_index = 1; fake.nacp.apparent_platform = 1;
    assert(lookup("0400123456780001") == "English fixture" && fake.record_reads == 2);
    reset(); fake.records.assign(32, DirectId); fake.records.push_back(DirectId + 1);
    fails([] { lookup("0400123456780001"); }, "ambiguous"); assert(fake.record_reads == 2 && fake.reads == 0);
    reset(); fake.records.assign(512, DirectId);
    fails([] { lookup("0400123456780001"); }, "scan reached its limit");
    assert(fake.record_reads == 16 && fake.reads == 0);
    reset(); fake.records.assign(512, DirectId); assert(lookup() == "English fixture" && fake.record_reads == 1);
    reset(); fake.records = {0x01ffffffffffffffull};
    fails([] { lookup("0400000000000000"); }, "No supported record-backed"); // No wrapping subtraction/addition.
    std::puts("Production game-name resolver: SDK calls/layout, small icon, opaque auxiliary, language/fallback, format-aware parser, failures and lifecycle passed.");
}
