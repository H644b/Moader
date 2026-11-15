#pragma once
#include <switch.h>
#include "moader/ipc.hpp"
#include "moader/library.hpp"
#include "moader/web.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
namespace moader_native {
constexpr Result BadInput = MAKERESULT(Module_Libnx, LibnxError_BadInput);
struct Context {
    moader::Library& library;
    moader::WebServer& web;
    const std::string& token;
    u32 ip = 0;
    Result last_result = 0;
    std::string error;
    std::optional<moader::LibrarySnapshot> cached;
    bool foreground = false;
    bool network_ready = false;
    std::vector<std::string> diagnostics;
};
void copy_text(char* out, size_t capacity, const std::string& text) {
    if (!capacity) return;
    size_t n = std::min(capacity - 1, text.size());
    while (n && n < text.size() && (static_cast<unsigned char>(text[n]) & 0xc0) == 0x80) --n;
    std::memset(out, 0, capacity); std::memcpy(out, text.data(), n);
}
std::string pairing_token() {
    moader::recover_atomic_file("sdmc:/moader/pairing-code");
    std::ifstream file("sdmc:/moader/pairing-code", std::ios::binary);
    std::string token;
    if (file) std::getline(file, token);
    bool valid = token.size() == 6;
    for (char c : token) if (c < '0' || c > '9') valid = false;
    if (valid) return token;
    u32 random;
    do { randomGet(&random, sizeof(random)); } while (random >= 4294000000u);
    char code[7]; std::snprintf(code, sizeof(code), "%06u", random % 1000000u); token = code;
    moader::atomic_write("sdmc:/moader/pairing-code", token);
    return token;
}
Result command(u32 id, const void* input, size_t input_size, void* output, size_t* output_size, void* data) {
    auto& context = *static_cast<Context*>(data);
    try {
        if (id == moader::ipc::Summary) {
            if (context.network_ready && R_FAILED(nifmGetCurrentIpAddress(&context.ip))) context.ip = 0;
            context.cached = context.library.snapshot();
            const auto& snapshot = *context.cached;
            moader::ipc::SummaryReply reply{}; reply.version = moader::ipc::Version;
            reply.flags = (context.web.running() ? moader::ipc::WebRunning : 0u) |
                (snapshot.runtime.connected ? moader::ipc::BackendConnected : 0u) |
                (context.foreground ? moader::ipc::Foreground : 0u);
            reply.generation = snapshot.generation; reply.applied_generation = snapshot.runtime.applied_generation;
            reply.mod_count = snapshot.mods.size(); reply.port = context.web.running() ? context.web.port() : context.library.preferences().web_port; reply.ipv4 = context.ip;
            reply.last_result = context.last_result ? context.last_result : snapshot.runtime.result;
            copy_text(reply.detail, sizeof(reply.detail), snapshot.runtime.connected ? "Routing connected" : "Backend not attached");
            std::memcpy(output, &reply, sizeof(reply)); *output_size = sizeof(reply); return 0;
        }
        if (id == moader::ipc::WebEnabled) {
            if (input_size < sizeof(u32)) return BadInput;
            u32 enabled; std::memcpy(&enabled, input, sizeof(enabled));
            if (enabled > 1) return BadInput;
            if (enabled && !context.web.start("0.0.0.0",context.library.preferences().web_port)) throw std::runtime_error(context.web.last_error());
            if (!enabled) context.web.stop();
            context.error.clear(); context.last_result = 0; return 0;
        }
        if (id == moader::ipc::GetPreferences) {
            const auto p = context.library.preferences();
            moader::ipc::PreferencesReply reply{
                (p.game_names ? moader::ipc::GameNames : 0u) | (p.reduced_motion ? moader::ipc::ReducedMotion : 0u) |
                (p.web_on_start ? moader::ipc::WebOnStart : 0u) | (p.follow_log ? moader::ipc::FollowLog : 0u), p.web_port};
            std::memcpy(output,&reply,sizeof(reply)); *output_size=sizeof(reply); return 0;
        }
        if (id == moader::ipc::SetPreferences) {
            if (input_size != sizeof(moader::ipc::PreferencesReply) && input_size != sizeof(moader::ipc::PreferencesRequest)) return BadInput;
            moader::ipc::PreferencesRequest wire{0,0,31};std::memcpy(&wire,input,input_size);
            if (!wire.mask || (wire.mask & ~31u)) throw std::invalid_argument("Invalid preference field mask.");
            if ((wire.flags & ~15u) || wire.port < 1024 || wire.port > 65535 || wire.port == 7713)
                throw std::invalid_argument("Port must be 1024–65535, excluding runtime port 7713.");
            moader::Preferences p;p.game_names=wire.flags&moader::ipc::GameNames;
            p.reduced_motion=wire.flags&moader::ipc::ReducedMotion;p.web_on_start=wire.flags&moader::ipc::WebOnStart;
            p.follow_log=wire.flags&moader::ipc::FollowLog;p.web_port=static_cast<uint16_t>(wire.port);
            context.library.update_preferences(p,wire.mask);context.error.clear();context.last_result=0;return 0;
        }
        if (id == moader::ipc::PairingCode) {
            std::memset(output, 0, 64); std::memcpy(output, context.token.data(), context.token.size()); *output_size = 64; return 0;
        }
        if (id == moader::ipc::LastError) {
            copy_text(static_cast<char*>(output), 64, context.error); *output_size = 64; return 0;
        }
        if (id == moader::ipc::DiagnosticsInfo) {
            context.diagnostics = context.library.diagnostic_lines();
            const u32 count = context.diagnostics.size();
            std::memcpy(output, &count, sizeof(count)); *output_size = sizeof(count); return 0;
        }
        if (input_size < sizeof(moader::ipc::Selection)) return BadInput;
        moader::ipc::Selection selected; std::memcpy(&selected, input, sizeof(selected));
        auto chunk = [&](const std::string& text, size_t offset) {
            if (offset > text.size()) return BadInput;
            // Preserve bytes across chunk boundaries; the client assembles UTF-8
            // before decoding, so no multi-byte character is dropped or replaced.
            std::memset(output, 0, 64);
            std::memcpy(output, text.data()+offset, std::min<size_t>(63,text.size()-offset));
            *output_size = 64; return Result(0);
        };
        if (id == moader::ipc::GetDiagnostic) {
            if (selected.index >= context.diagnostics.size()) return BadInput;
            return chunk(context.diagnostics[selected.index], selected.value);
        }
        if (id == moader::ipc::RuntimeDetail) {
            if (!context.cached) return BadInput;
            return chunk(context.cached->runtime.detail, selected.value);
        }
        if (!context.cached || selected.generation != context.library.generation() ||
            selected.generation != context.cached->generation || selected.index >= context.cached->mods.size())
            throw std::runtime_error("Library changed. Refresh and select the mod again.");
        const auto& mod = context.cached->mods[selected.index];
        if (id == moader::ipc::GetGameName) return chunk(context.library.game_name(mod.title_id),selected.value);
        if (id == moader::ipc::GetModText) {
            const u32 field = selected.value >> 16, offset = selected.value & 0xffff;
            if (field > moader::ipc::DetailText) return BadInput;
            return chunk(field == moader::ipc::NameText ? mod.name : field == moader::ipc::IdText ? mod.id : mod.detail, offset);
        }
        if (id == moader::ipc::GetMod) {
            moader::ipc::ModReply reply{};
            reply.title_id = std::stoull(mod.title_id, nullptr, 16); reply.bytes = mod.bytes;
            reply.file_count = mod.files.size(); reply.applied = static_cast<u32>(mod.applied);
            reply.flags = (mod.requested ? moader::ipc::Requested : 0u) |
                (mod.has_romfs ? moader::ipc::Romfs : 0u) | (mod.has_exefs ? moader::ipc::Exefs : 0u);
            copy_text(reply.name, sizeof(reply.name), mod.name);
            std::memcpy(output, &reply, sizeof(reply)); *output_size = sizeof(reply); return 0;
        }
        if (id == moader::ipc::SetEnabled && selected.value <= 1)
            context.library.set_enabled(mod.title_id, mod.id, selected.value != 0, selected.generation);
        else if (id == moader::ipc::Remove)
            context.library.remove(mod.title_id, mod.id, selected.generation);
        else return BadInput;
        context.error.clear(); context.last_result = 0; return 0;
    } catch (const std::exception& e) {
        context.error = e.what(); context.last_result = BadInput; return BadInput;
    }
}
}
