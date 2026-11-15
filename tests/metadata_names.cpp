#include "native/metadata_names.hpp"
#include <cassert>
#include <cstdio>
#include <vector>

using moader_metadata::parse_name;
using Entries = std::array<NacpLanguageEntry, 32>;
namespace {
std::vector<unsigned char> compress_raw(const void* data, size_t size, int window = -MAX_WBITS) {
    z_stream stream{};
    assert(deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, window, 8, Z_DEFAULT_STRATEGY) == Z_OK);
    std::vector<unsigned char> out(deflateBound(&stream, size));
    stream.next_in = const_cast<Bytef*>(static_cast<const Bytef*>(data)); stream.avail_in = size;
    stream.next_out = out.data(); stream.avail_out = out.size();
    assert(deflate(&stream, Z_FINISH) == Z_STREAM_END); out.resize(stream.total_out);
    assert(deflateEnd(&stream) == Z_OK); return out;
}
NacpStruct compressed_bytes(const std::vector<unsigned char>& bytes) {
    NacpStruct nacp{}; nacp.titles_data_format = 1;
    assert(bytes.size() <= sizeof(nacp.lang_data.compressed_data.buffer));
    nacp.lang_data.compressed_data.buffer_size = bytes.size();
    std::memcpy(nacp.lang_data.compressed_data.buffer, bytes.data(), bytes.size()); return nacp;
}
NacpStruct compressed(const Entries& entries) { return compressed_bytes(compress_raw(entries.data(), sizeof(entries))); }
void name(NacpLanguageEntry& entry, const std::string& text) {
    assert(text.size() < sizeof(entry.name)); std::memcpy(entry.name, text.c_str(), text.size() + 1);
}
void invalid(const NacpStruct& nacp, const char* fragment = nullptr) {
    const auto result = parse_name(nacp, sizeof(nacp));
    assert(!result.valid && result.name.empty() && !result.error.empty());
    if (fragment) assert(result.error.find(fragment) != std::string::npos);
}
}
int main() {
    // Actual installed SDK structs, not a duplicated test-only interpretation.
    static_assert(sizeof(NacpStruct) == 0x4000 && sizeof(Entries) == 0x6000);
    static_assert(offsetof(NacpStruct, apparent_platform) == 0x3217);
    NacpStruct ordinary{};
    name(ordinary.lang_data.lang[0], "Pokémon Legends: Z-A");
    name(ordinary.lang_data.lang[2], "ポケモンレジェンズ Z-A");
    auto result = parse_name(ordinary, sizeof(ordinary), 2);
    assert(result.valid && result.name == "ポケモンレジェンズ Z-A" && result.language_index == 2 && result.language_count == 16);
    assert(parse_name(ordinary, sizeof(ordinary), 15).name == "Pokémon Legends: Z-A");
    assert(parse_name(ordinary, sizeof(ordinary), 1000).language_index == 0);
    assert(!parse_name(ordinary, sizeof(ordinary) - 1).valid);
    name(ordinary.lang_data.lang[0], std::string(511, 'a'));
    assert(parse_name(ordinary, sizeof(ordinary)).name.size() == 511);
    std::memset(ordinary.lang_data.lang[0].name, 'x', sizeof(ordinary.lang_data.lang[0].name));
    assert(parse_name(ordinary, sizeof(ordinary)).language_index == 2); // validated fallback
    ordinary.lang_data.lang[2] = {};
    invalid(ordinary, "UTF-8");
    ordinary = {}; invalid(ordinary, "nonempty");

    Entries titles{};
    name(titles[0], "English title"); name(titles[17], "ชื่อเกม"); name(titles[31], "Final slot 🐾");
    auto nacp = compressed(titles);
    nacp.acd_index = 1; nacp.apparent_platform = 2;
    const auto unchanged = nacp;
    result = parse_name(nacp, sizeof(nacp), 31);
    assert(result.valid && result.name == "Final slot 🐾" && result.language_index == 31 && result.language_count == 32 && result.format == 1);
    assert(result.compressed_bytes == nacp.lang_data.compressed_data.buffer_size);
    assert(std::memcmp(&nacp, &unchanged, sizeof(nacp)) == 0); // no overwrite of NACP/header
    assert(parse_name(nacp, sizeof(nacp), 17).name == "ชื่อเกม");
    assert(parse_name(nacp, sizeof(nacp), 30).name == "English title");
    titles = {}; invalid(compressed(titles), "nonempty");

    auto damaged = unchanged;
    damaged.lang_data.compressed_data.buffer_size = 0; invalid(damaged, "length");
    damaged.lang_data.compressed_data.buffer_size = 0xffff; invalid(damaged, "length");
    damaged = unchanged; --damaged.lang_data.compressed_data.buffer_size; invalid(damaged, "DEFLATE");
    damaged = unchanged;
    damaged.lang_data.compressed_data.buffer[damaged.lang_data.compressed_data.buffer_size++] = 0x55;
    invalid(damaged, "DEFLATE"); // reject trailing bytes after the complete stream
    damaged = unchanged;
    std::memset(damaged.lang_data.compressed_data.buffer, 0xff, damaged.lang_data.compressed_data.buffer_size);
    invalid(damaged, "DEFLATE");
    std::vector<unsigned char> short_output(sizeof(Entries) - 1, 0), long_output(sizeof(Entries) + 1, 0), oversized_output(1024 * 1024, 0);
    invalid(compressed_bytes(compress_raw(short_output.data(), short_output.size())), "DEFLATE");
    invalid(compressed_bytes(compress_raw(long_output.data(), long_output.size())), "DEFLATE");
    invalid(compressed_bytes(compress_raw(oversized_output.data(), oversized_output.size())), "DEFLATE");
    invalid(compressed_bytes(compress_raw(titles.data(), sizeof(titles), MAX_WBITS)), "DEFLATE"); // zlib wrapper is not raw DEFLATE
    damaged = {}; name(damaged.lang_data.lang[0], "A plausible fake title"); damaged.titles_data_format = 1;
    invalid(damaged); // must never reinterpret compressed-format bytes as text
    damaged = {}; name(damaged.lang_data.lang[0], "Unsupported format fake title"); damaged.titles_data_format = 2;
    invalid(damaged, "Unsupported");

    for (const std::string& bad : {std::string("line\nbreak"), std::string("tab\tname"), std::string("\x7f"),
            std::string("\x80"), std::string("\xc0\xaf"), std::string("\xe2\x82"), std::string("\xed\xa0\x80"),
            std::string("\xf4\x90\x80\x80"), std::string("\xc2\x9f")}) {
        ordinary = {}; name(ordinary.lang_data.lang[0], bad); invalid(ordinary, "UTF-8");
        titles = {}; name(titles[0], bad); invalid(compressed(titles), "UTF-8");
    }
    // Poison-filled bounded compressed fields exercise malformed stream handling
    // under ASan/UBSan, including lengths at the field's exact upper bound.
    unsigned random = 1;
    for (unsigned iteration = 0; iteration < 64; ++iteration) {
        NacpStruct noise{}; noise.titles_data_format = 1;
        noise.lang_data.compressed_data.buffer_size = iteration == 63 ? sizeof(noise.lang_data.compressed_data.buffer) : 1 + iteration;
        for (auto& byte : noise.lang_data.compressed_data.buffer) { random = random * 1664525u + 1013904223u; byte = random >> 24; }
        invalid(noise, "DEFLATE");
    }
    std::puts("PASS: actual SDK NACP layouts, ordinary/compressed language selection, exact bounds, hostile DEFLATE, no raw compressed-name fallback, and UTF-8 validation");
}
