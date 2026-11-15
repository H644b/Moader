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
#include "resident_lifecycle.hpp"
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
Result resident_command(u32 id, const void* input, size_t input_size,
                        void* output, size_t* output_size, void* data) {
    auto& control = *static_cast<ResidentControl*>(data);
    std::lock_guard<std::mutex> lock(control.mutex);
    if (control.state != KaihenPluginLifecycleState_Starting &&
        control.state != KaihenPluginLifecycleState_Running) {
        *output_size = 0;
        if (id == moader::ipc::LastError) {
            copy_text(static_cast<char*>(output), 64,
                control.state == KaihenPluginLifecycleState_Failed && control.context ? control.context->error :
                "Resident reload in progress. Waiting for HEN to reconnect.");
            *output_size = 64;
            return 0;
        }
        return ReloadBusy;
    }
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
    const Result rc = command(id, input, input_size, output, output_size, control.context);
    if (R_SUCCEEDED(rc) && id == moader::ipc::WebEnabled && control.context->web.running())
        control.web_start_error = false;
    return rc;
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
        rc = pluginServiceStartWithLifecycle(&service, control, resident_command, &endpoint,
                                             resident_lifecycle, &endpoint);
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
                std::lock_guard<std::mutex> lock(endpoint.mutex);
                endpoint.web_start_error = true;
            }
            ContextBinding binding(endpoint, context);
            unsigned inventory_ticks = 0;
            while (true) {
                try {
                    if (resident_lifecycle_step(endpoint, context)) {
                        const Result stopped = pluginServiceStop(&service);
                        if (R_FAILED(stopped) && service.started) {
                            // A thread that has not joined may still borrow the
                            // endpoint. Keep Context and the writer lock alive.
                            std::lock_guard<std::mutex> lock(endpoint.mutex);
                            endpoint.state = KaihenPluginLifecycleState_Failed;
                            endpoint.result = stopped;
                            endpoint.exit_requested = false;
                            context.error = "Plugin worker shutdown failed; ownership retained.";
                            context.last_result = stopped;
                        } else break;
                    }
                } catch (const std::exception& e) {
                    // A failed drain/abort is not proof of worker exit. Keep
                    // ownership and the Context alive for diagnosis; never
                    // release the single-writer lock to a competing process.
                    std::lock_guard<std::mutex> lock(endpoint.mutex);
                    endpoint.state = KaihenPluginLifecycleState_Failed;
                    endpoint.result = BadInput;
                    context.error = e.what(); context.last_result = BadInput;
                }
                if (library.quiescing()) {
                    svcSleepThread(1000000000);
                    continue;
                }
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
                resident_health(endpoint, library.snapshot());
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
