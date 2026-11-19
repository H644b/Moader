// Host the installed-icon reader with the real SDK layouts and a synthetic title record.
#include <cstdint>
typedef uint32_t _LOCK_T;
typedef struct { uint32_t lock, counter; } _LOCK_RECURSIVE_T;
#include "native/cover_loader.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <thread>
#include <utility>

namespace {
constexpr u64 Base = 0x0100123456780000;
constexpr u64 Edition = 0x0400123456780001;
constexpr Result Unavailable = 0x5010;

struct Reply {
    Result code = Unavailable;
    std::string icon;
    std::optional<u64> size;
    bool wrong_edition = false;
};
struct {
    u64 record = Base;
    u8 index = 1;
    Reply compact, full;
    int sessions = 0;
    std::vector<u8> requests;
} sdk;

std::string read_file(const char* path) {
    std::ifstream in(path, std::ios::binary);
    assert(in);
    return {std::istreambuf_iterator<char>(in), {}};
}

template <typename Done>
void poll_until(moader_native::CoverLoader& covers, moader_ui::State& library, uint64_t ms, Done done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!done() && std::chrono::steady_clock::now() < deadline) {
        covers.poll(library, ms);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(done());
}
}
extern "C" Result nsInitialize() { assert(sdk.sessions == 0); ++sdk.sessions; return 0; }
extern "C" void nsExit() { assert(sdk.sessions == 1); --sdk.sessions; }
extern "C" Result nsListApplicationRecord(NsApplicationRecord* records, s32 count, s32 offset, s32* actual) {
    assert(sdk.sessions == 1 && count > 0 && !offset);
    records[0].application_id = sdk.record;
    *actual = 1;
    return 0;
}
extern "C" Result nsGetApplicationControlData2(NsApplicationControlSource source, u64 id,
        NsApplicationControlData* data, size_t capacity, u8 flag, u8 index, u64* actual, u32* auxiliary) {
    assert(sdk.sessions == 1 && id == sdk.record && index == sdk.index);
    assert(source == NsApplicationControlSource_Storage && flag <= 1);
    assert(capacity >= sizeof(*data) && !*actual && !*auxiliary);
    sdk.requests.push_back(flag);
    const auto& reply = flag == 1 ? sdk.compact : sdk.full;
    if (R_FAILED(reply.code)) return reply.code;
    data->nacp.acd_index = reply.wrong_edition ? 0 : index;
    data->nacp.apparent_platform = reply.wrong_edition ? 0 : index;
    assert(reply.icon.size() <= sizeof(data->icon));
    std::memcpy(data->icon, reply.icon.data(), reply.icon.size());
    *actual = reply.size.value_or(sizeof(data->nacp) + reply.icon.size());
    *auxiliary = 1;
    return 0;
}

int main(int argc, char** argv) {
    assert(argc == 3);
    const auto jpeg = read_file(argv[1]);
    const auto png = read_file(argv[2]);
    constexpr char EditionTitle[] = "0400123456780001";
    // Device reports for an Edition: flag 0 returns 0x5010; flag 1 returns
    // 123034 bytes of control data. The picture here is the project's own JPEG.
    auto padded = jpeg;
    padded.resize(123034 - sizeof(NacpStruct), '\0');
    sdk.compact = {0, padded};
    if (moader_native::installed_game_art(EditionTitle) != padded) {
        std::cerr << "Did not retrieve the installed Edition icon when only the compact request succeeds.\n";
        return 1;
    }
    assert(sdk.sessions == 0 && sdk.requests == std::vector<u8>{1});
    assert(!png.empty());
    moader_ui::State library;
    library.games.push_back({Edition, {}, "Installed game", {}});
    moader_native::CoverLoader covers;
    poll_until(covers, library, 1, [&] { return bool(library.games[0].art); });
    assert(library.games[0].art->width == 256 && library.games[0].art->height == 256);

    const std::vector<u8> both{1, 0};
    sdk.full = {0, jpeg};
    for (auto compact : {Reply{Unavailable}, Reply{0, ""}, Reply{0, "bad format"},
                         Reply{0, std::string("\xff\xd8\xff\xd9", 4)},  // JPEG marker without a decodable image.
                         Reply{0, jpeg, sizeof(NacpStruct) - 1}, Reply{0, jpeg, sizeof(NsApplicationControlData) + 1},
                         Reply{0, jpeg, {}, true}}) {
        sdk.requests.clear();
        sdk.compact = std::move(compact);
        assert(moader_native::installed_game_art(EditionTitle) == jpeg);
        assert(sdk.requests == both && sdk.sessions == 0);
    }

    // The same edition index must be used on fallback. A base image cannot
    // stand in for an Edition when both responses report mismatched fields.
    sdk.requests.clear();
    sdk.compact = sdk.full = {0, jpeg, {}, true};
    try {
        (void)moader_native::installed_game_art(EditionTitle);
        assert(false);
    } catch (const std::runtime_error& e) {
        assert(std::string(e.what()).find("Edition fields") != std::string::npos);
    }
    assert(sdk.requests == both && sdk.sessions == 0);

    // Keep failures debuggable while the placeholder is on screen, and recover on retry.
    sdk.requests.clear();
    sdk.compact = {Unavailable};
    sdk.full = {0, "unrecognized"};
    library.games[0].art.reset();
    poll_until(covers, library, 10, [&] { return !covers.error_for(Edition).empty(); });
    assert(covers.error_for(Edition).find("0x00005010") != std::string_view::npos);
    assert(covers.error_for(Edition).find("756E7265") != std::string_view::npos);
    covers.poll(library, 60009);
    assert(sdk.requests == both);
    sdk.compact = {0, png};
    poll_until(covers, library, 60010, [&] { return bool(library.games[0].art); });
    assert(covers.error_for(Edition).empty());
    assert(library.games[0].art->width == 96 && library.games[0].art->height == 96);

    // Native Switch 2 applications and base Switch applications use their own records.
    sdk.index = 0;
    for (const auto& title : {"0400123456780000", "0100123456780000"}) {
        sdk.record = moader_native::game_names_detail::parse_id(title);
        sdk.requests.clear();
        assert(moader_native::installed_game_art(title) == png);
        assert(sdk.requests == std::vector<u8>{1} && sdk.sessions == 0);
    }
    std::puts("Installed icons: compact and full requests, SDK bounds, Edition identity, real image decoding, carousel retry and diagnostics passed.");
    return 0;
}
