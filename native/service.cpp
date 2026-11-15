#include <switch.h>
#include <kaihen/plugin.h>
#include "moader/ipc.hpp"
#include "moader/library.hpp"
#include "moader/web.hpp"
#include "moader/runtime_backend.hpp"
#include "web_assets.hpp"
#include "control.hpp"
#include "instance.hpp"
#include "game_names.hpp"
#include "game_availability.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>

extern "C" {
u32 __nx_applet_type = AppletType_None;
size_t __nx_heap_size = 0x4000000;
u32 __nx_fs_num_sessions = 3;
void __appInit(void) {}
void __appExit(void) {}
}
using namespace moader_native;

namespace {
// HEN's five-second ready window covers the IPC transport, not an SD scan or
// network setup. Until initialization finishes, answer without exposing Context.
// In particular, an early WebEnabled must not race the saved auto-start setting.
struct ResidentControl {
    std::mutex mutex;
    Context* context = nullptr;
};
Result resident_command(u32 id, const void* input, size_t input_size,
                        void* output, size_t* output_size, void* data) {
    auto& control = *static_cast<ResidentControl*>(data);
    std::lock_guard<std::mutex> lock(control.mutex);
    if (!control.context) {
        *output_size = 0;
        if (id == moader::ipc::LastError) {
            copy_text(static_cast<char*>(output), 64,
                      "Resident service is starting. Press Y to reconnect.");
            *output_size = 64;
            return 0;
        }
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    }
    return command(id, input, input_size, output, output_size, control.context);
}
struct ContextBinding {
    ResidentControl& control;
    ContextBinding(ResidentControl& control, Context& context) : control(control) {
        std::lock_guard<std::mutex> lock(control.mutex);
        control.context = &context;
    }
    ~ContextBinding() {
        // Wait for the current command before destroying anything it borrows.
        std::lock_guard<std::mutex> lock(control.mutex);
        control.context = nullptr;
    }
};
}
int main() {
    Handle ready = INVALID_HANDLE, control = INVALID_HANDLE;
    Result rc = pluginRuntimeInitializeWithService(&ready, &control);
    if (R_FAILED(rc)) return rc;
    bool fs_ready = false, mounted = false, sockets_ready = false, nifm_ready = false;
    PluginService service{};
    ResidentControl endpoint;
    try {
        rc = pluginServiceStart(&service, control, resident_command, &endpoint);
        if (R_SUCCEEDED(rc)) rc = svcSignalEvent(ready);
        if (R_SUCCEEDED(rc)) { rc = fsInitialize(); fs_ready = R_SUCCEEDED(rc); }
        if (R_SUCCEEDED(rc)) { rc = fsdevMountSdmc(); mounted = R_SUCCEEDED(rc); }
        if (R_SUCCEEDED(rc)) { rc = socketInitializeDefault(); sockets_ready = R_SUCCEEDED(rc); }
        if (R_SUCCEEDED(rc)) {
            std::array<uint64_t,2> epoch{}; randomGet(epoch.data(),sizeof(epoch));
            if (!(epoch[0] | epoch[1])) epoch[0] = 1;
            InstanceLock instance;
            moader::RuntimeBackend backend("sdmc:/moader",epoch,[] {
                // An ordinary read-only FS request wakes the resident hook. Its
                // original result is immaterial: this process need not have RomFS.
                FsStorage storage{};
                if (R_SUCCEEDED(fsOpenDataStorageByCurrentProcess(&storage))) fsStorageClose(&storage);
            });
            moader::Library library("sdmc:/moader", backend, installed_game_name, installed_game_availability);
            const auto token = pairing_token();
            moader::WebServer web(library, "sdmc:/moader", token, moader_web_assets());
            Context context{library, web, token};
            nifm_ready = R_SUCCEEDED(nifmInitialize(NifmServiceType_User));
            context.network_ready = nifm_ready;
            if (nifm_ready) nifmGetCurrentIpAddress(&context.ip);
            try { library.synchronize(); }
            catch (const std::exception& e) { context.error = e.what(); context.last_result = BadInput; }
            const auto preferences = library.preferences();
            if (preferences.web_on_start && !web.start("0.0.0.0",preferences.web_port)) {
                context.error = web.last_error(); context.last_result = BadInput;
            }
            ContextBinding binding(endpoint, context);
            unsigned inventory_ticks = 0;
            while (true) {
                try { backend.poll(); }
                catch (const std::exception& e) {
                    // A transient receipt/FS failure must not terminate the LAN
                    // listener. Surface it, and retry on the next poll.
                    std::lock_guard<std::mutex> lock(endpoint.mutex);
                    context.error = e.what(); context.last_result = BadInput;
                }
                // Availability checks never walk payloads. A periodic SD scan
                // runs on this owner thread; IPC continues serving the last
                // complete cached inventory during the scan or an import.
                try {
                    library.refresh_availability();
                } catch (const std::exception& e) {
                    std::lock_guard<std::mutex> lock(endpoint.mutex);
                    context.error = e.what(); context.last_result = BadInput;
                }
                if (++inventory_ticks >= 30) {
                    inventory_ticks = 0;
                    try { library.rescan(); }
                    catch (const std::exception& e) {
                        std::lock_guard<std::mutex> lock(endpoint.mutex);
                        context.error = e.what(); context.last_result = BadInput;
                    }
                }
                svcSleepThread(1000000000);
            }
        }
    } catch (const std::exception& e) {
        if (mounted) {
            FILE* file = std::fopen("sdmc:/moader/service-error.txt", "w");
            if (file) { std::fprintf(file, "%s\n", e.what()); std::fclose(file); }
        }
        rc = BadInput;
    }
    if (service.started) pluginServiceStop(&service);
    if (nifm_ready) nifmExit();
    if (sockets_ready) socketExit();
    if (mounted) fsdevUnmountAll();
    if (fs_ready) fsExit();
    pluginRuntimeExit(); return rc;
}
