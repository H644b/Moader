#pragma once
#include <cstdint>

namespace moader::ipc {
constexpr uint32_t Version = 1;
enum Command : uint32_t { Summary, WebEnabled, GetMod, SetEnabled, Remove, PairingCode, LastError, GetModText, DiagnosticsInfo, GetDiagnostic, RuntimeDetail, GetPreferences, SetPreferences, GetGameName };
// Additive optional commands; Version 1 clients/services retain their original ABI.
// Text replies are NUL-terminated 63-byte chunks. Selection.value: field in
// bits 16..31, byte offset in bits 0..15. Diagnostic chunks use value as offset.
enum TextField : uint32_t { NameText, IdText, DetailText };
constexpr uint32_t TextChunkBytes = 63;
enum PreferenceFlag : uint32_t { GameNames = 1, ReducedMotion = 2, WebOnStart = 4, FollowLog = 8 };
struct PreferencesReply { uint32_t flags, port; };
static_assert(sizeof(PreferencesReply) == 8);
// Changed-field mask is intentionally separate from PreferenceFlag.
enum PreferenceField : uint32_t { NamesField=1, MotionField=2, StartupField=4, PortField=8, FollowField=16 };
struct PreferencesRequest { uint32_t flags, port, mask; };
static_assert(sizeof(PreferencesRequest) == 12);
enum Flag : uint32_t { WebRunning = 1, BackendConnected = 2, Requested = 4, Romfs = 8, Exefs = 16, Foreground = 32 };
struct Selection { uint64_t generation; uint32_t index; uint32_t value; };
struct SummaryReply {
    uint32_t version, flags;
    uint64_t generation, applied_generation;
    uint32_t mod_count;
    uint16_t port, reserved;
    uint32_t ipv4, last_result;
    char detail[24];
};
struct ModReply {
    uint64_t title_id, bytes;
    uint32_t file_count, flags, applied, reserved;
    char name[32];
};
static_assert(sizeof(Selection) == 16);
static_assert(sizeof(SummaryReply) == 64);
static_assert(sizeof(ModReply) == 64);
}
