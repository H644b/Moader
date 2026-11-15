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
constexpr const char* SelectionChangedMessage = "Library changed. Refresh and select the mod again.";
static_assert(static_cast<u32>(moader::GameSource::Unspecified)==moader::ipc::CopyUnspecified &&
    static_cast<u32>(moader::GameSource::Cartridge)==moader::ipc::CopyCartridge &&
    static_cast<u32>(moader::GameSource::Digital)==moader::ipc::CopyDigital);
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
    bool selection_error = false;
};
void clear_operation_error(Context& context) {
    context.error.clear(); context.last_result = 0; context.selection_error = false;
}
void clear_selection_error(Context& context) {
    // A service poll may have replaced LastError since the selection failed.
    // A successful row read must not clear that newer, unrelated failure.
    if (context.selection_error && context.last_result == BadInput && context.error == SelectionChangedMessage)
        clear_operation_error(context);
    else context.selection_error = false;
}
std::string runtime_detail(const moader::LibrarySnapshot& snapshot) {
    auto text = snapshot.runtime.detail;
    if (!snapshot.inventory_ready || !snapshot.inventory_error.empty()) {
        std::string inventory = "Library inventory unavailable. ";
        inventory += snapshot.inventory_error.empty() ? "The initial inventory is not ready." : snapshot.inventory_error;
        inventory += " Retaining the last known inventory; this refresh has not removed saved mod files.";
        // Put the actionable inventory failure in the short debug preview when
        // runtime health is good. Genuine runtime failures keep first priority.
        text = snapshot.runtime.result ? text + "\n\n" + inventory : inventory + "\n\nRuntime: " + text;
    }
    return text;
}
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
    bool selection_failure = false;
    try {
        if (id == moader::ipc::Summary) {
            if (context.network_ready && R_FAILED(nifmGetCurrentIpAddress(&context.ip))) context.ip = 0;
            context.cached = context.library.snapshot();
            const auto& snapshot = *context.cached;
            moader::ipc::SummaryReply reply{}; reply.version = moader::ipc::Version;
            reply.flags = (context.web.running() ? moader::ipc::WebRunning : 0u) |
                (snapshot.runtime.connected ? moader::ipc::BackendConnected : 0u) |
                (context.foreground ? moader::ipc::Foreground : 0u) |
                (!snapshot.inventory_ready || !snapshot.inventory_error.empty() ? moader::ipc::InventoryUnavailable : 0u);
            reply.generation = snapshot.generation; reply.applied_generation = snapshot.runtime.applied_generation;
            reply.mod_count = snapshot.mods.size(); reply.port = context.web.running() ? context.web.port() : context.library.preferences().web_port; reply.ipv4 = context.ip;
            reply.last_result = snapshot.runtime.result;
            copy_text(reply.detail, sizeof(reply.detail), snapshot.runtime.result ? "Runtime error" :
                snapshot.runtime.connected ? "Routing connected" : "Backend not attached");
            std::memcpy(output, &reply, sizeof(reply)); *output_size = sizeof(reply); return 0;
        }
        if (id == moader::ipc::WebEnabled) {
            if (input_size < sizeof(u32)) return BadInput;
            u32 enabled; std::memcpy(&enabled, input, sizeof(enabled));
            if (enabled > 1) return BadInput;
            if (enabled && !context.web.start("0.0.0.0",context.library.preferences().web_port)) throw std::runtime_error(context.web.last_error());
            if (!enabled) context.web.stop();
            clear_operation_error(context); return 0;
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
            context.library.update_preferences(p,wire.mask);clear_operation_error(context);return 0;
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
            return chunk(runtime_detail(*context.cached), selected.value);
        }
        if (!context.cached || selected.generation != context.library.generation() ||
            selected.generation != context.cached->generation || selected.index >= context.cached->mods.size()) {
            selection_failure = true;
            throw std::runtime_error(SelectionChangedMessage);
        }
        const auto& mod = context.cached->mods[selected.index];
        if (id == moader::ipc::GetGameSource) {
            if (selected.value) return BadInput;
            const u32 source = static_cast<u32>(context.library.game_source(mod.title_id));
            std::memcpy(output,&source,sizeof(source)); *output_size=sizeof(source);
            clear_selection_error(context); return 0;
        }
        if (id == moader::ipc::SetGameSource) {
            if (selected.value > moader::ipc::CopyDigital) throw std::invalid_argument("Choose Unspecified, Cartridge or Digital for this game.");
            context.library.set_game_source(mod.title_id,static_cast<moader::GameSource>(selected.value),selected.generation);
            clear_operation_error(context); return 0;
        }
        if (id == moader::ipc::GetGameName) {
            const auto result = chunk(context.library.game_name(mod.title_id),selected.value);
            if (!result) clear_selection_error(context);
            return result;
        }
        if (id == moader::ipc::GetModText) {
            const u32 field = selected.value >> 16, offset = selected.value & 0xffff;
            if (field > moader::ipc::DetailText) return BadInput;
            const auto result = chunk(field == moader::ipc::NameText ? mod.name : field == moader::ipc::IdText ? mod.id : mod.detail, offset);
            if (!result) clear_selection_error(context);
            return result;
        }
        if (id == moader::ipc::GetMod) {
            moader::ipc::ModReply reply{};
            reply.title_id = std::stoull(mod.title_id, nullptr, 16); reply.bytes = mod.bytes;
            reply.file_count = mod.files.size(); reply.applied = static_cast<u32>(mod.applied);
            reply.flags = (mod.requested ? moader::ipc::Requested : 0u) |
                (mod.has_romfs ? moader::ipc::Romfs : 0u) | (mod.has_exefs ? moader::ipc::Exefs : 0u);
            copy_text(reply.name, sizeof(reply.name), mod.name);
            std::memcpy(output, &reply, sizeof(reply)); *output_size = sizeof(reply);
            clear_selection_error(context);
            return 0;
        }
        if (id == moader::ipc::SetEnabled && selected.value <= 1)
            context.library.set_enabled(mod.title_id, mod.id, selected.value != 0, selected.generation);
        else if (id == moader::ipc::Remove)
            context.library.remove(mod.title_id, mod.id, selected.generation);
        else return BadInput;
        clear_operation_error(context); return 0;
    } catch (const std::exception& e) {
        context.error = e.what(); context.last_result = BadInput; context.selection_error = selection_failure; return BadInput;
    }
}
}
