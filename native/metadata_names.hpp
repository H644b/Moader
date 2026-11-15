#pragma once
#include <switch/types.h>
#include <switch/nacp.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <zlib.h>

namespace moader_metadata {
static_assert(sizeof(NacpStruct) == 0x4000);
static_assert(sizeof(NacpLanguageEntry) == 0x300);
static_assert(sizeof(NacpLanguageEntryData) == 0x3000);
static_assert(offsetof(NacpStruct, titles_data_format) == 0x3215);
static_assert(offsetof(NacpStruct, acd_index) == 0x3216);
static_assert(offsetof(NacpLanguageEntryData, compressed_data.buffer) == 2);

struct NameResult {
    bool valid = false;
    std::string name, error;
    size_t language_index = 0, language_count = 0;
    unsigned format = 0, compressed_bytes = 0;
};

// NACP names are bounded UTF-8 strings. Do not return malformed sequences,
// unterminated arrays, or C0/C1 control characters to the report/console.
inline bool valid_name(const char* name, size_t length) {
    if (!length || length >= sizeof(NacpLanguageEntry::name)) return false;
    for (size_t at = 0; at < length;) {
        const auto first = static_cast<unsigned char>(name[at++]);
        uint32_t point = first, minimum = 0;
        size_t rest = 0;
        if (first < 0x80) {
            if (first < 0x20 || first == 0x7f) return false;
            continue;
        }
        if (first >= 0xc2 && first <= 0xdf) { point &= 0x1f; rest = 1; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { point &= 0x0f; rest = 2; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { point &= 7; rest = 3; minimum = 0x10000; }
        else return false;
        if (rest > length - at) return false;
        for (size_t i = 0; i < rest; ++i) {
            const auto next = static_cast<unsigned char>(name[at++]);
            if ((next & 0xc0) != 0x80) return false;
            point = (point << 6) | (next & 0x3f);
        }
        if (point < minimum || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff) ||
            (point >= 0x80 && point <= 0x9f)) return false;
    }
    return true;
}

inline NameResult parse_name(const NacpStruct& nacp, size_t available, size_t preferred_index = 0) {
    NameResult out;
    if (available < sizeof(NacpStruct)) { out.error = "Incomplete NACP"; return out; }
    out.format = nacp.titles_data_format;
    const NacpLanguageEntry* entries = nullptr;
    std::unique_ptr<std::array<NacpLanguageEntry, 32>> expanded;
    if (out.format == 0) {
        entries = nacp.lang_data.lang;
        out.language_count = 16;
    } else if (out.format == 1) {
        out.compressed_bytes = nacp.lang_data.compressed_data.buffer_size;
        if (!out.compressed_bytes || out.compressed_bytes > sizeof(nacp.lang_data.compressed_data.buffer)) {
            out.error = "Invalid compressed title length"; return out;
        }
        expanded.reset(new (std::nothrow) std::array<NacpLanguageEntry, 32>{});
        if (!expanded) { out.error = "Cannot allocate expanded titles"; return out; }
        z_stream stream{};
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) { out.error = "Cannot initialize title decompression"; return out; }
        stream.next_in = const_cast<Bytef*>(nacp.lang_data.compressed_data.buffer);
        stream.avail_in = out.compressed_bytes;
        stream.next_out = reinterpret_cast<Bytef*>(expanded->data());
        stream.avail_out = sizeof(*expanded);
        const int result = inflate(&stream, Z_FINISH);
        const bool complete = result == Z_STREAM_END && stream.total_in == out.compressed_bytes &&
            stream.total_out == sizeof(*expanded);
        const int ended = inflateEnd(&stream);
        if (!complete || ended != Z_OK) { out.error = "Invalid or incomplete raw DEFLATE titles"; return out; }
        entries = expanded->data();
        out.language_count = expanded->size();
    } else { out.error = "Unsupported title data format"; return out; }

    bool invalid = false;
    const auto select = [&](size_t index) {
        const auto& entry = entries[index];
        const auto* end = static_cast<const char*>(std::memchr(entry.name, 0, sizeof(entry.name)));
        if (!end) { invalid = true; return false; }
        const size_t length = static_cast<size_t>(end - entry.name);
        if (!length) return false;
        if (!valid_name(entry.name, length)) { invalid = true; return false; }
        out.name.assign(entry.name, length); out.language_index = index; out.valid = true; return true;
    };
    if (preferred_index < out.language_count && select(preferred_index)) return out;
    for (size_t i = 0; i < out.language_count; ++i)
        if (i != preferred_index && select(i)) return out;
    out.error = invalid ? "No valid UTF-8 title entry" : "No nonempty title entry";
    return out;
}
}
