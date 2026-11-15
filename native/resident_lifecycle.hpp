#pragma once
#include <kaihen/plugin.h>
#include "control.hpp"
#include <mutex>

namespace moader_native {
constexpr Result ReloadBusy = KAIHEN_PLUGIN_LIFECYCLE_BUSY;
// Context lifetime and lifecycle requests share this lock. The control thread
// only reserves work; the owner performs listener/worker shutdown outside it.
struct ResidentControl {
    std::mutex mutex;
    Context* context = nullptr;
    u32 state = KaihenPluginLifecycleState_Starting;
    Result result = 0;
    u64 operation = 0;
    bool healthy = false, abort_requested = false, commit_pending = false;
    bool exit_requested = false, web_was_running = false;
    bool web_start_error = false;
    u16 web_port = 0;
};

inline Result resident_lifecycle(const KaihenPluginLifecycleRequest* request,
                                  KaihenPluginLifecycleReply* reply, void* data) {
    if (!request || !reply || request->reserved) return BadInput;
    auto& control = *static_cast<ResidentControl*>(data);
    std::unique_lock<std::mutex> lock(control.mutex, std::defer_lock);
    // This internal notification is delivered once, after the Commit reply.
    // It cannot be dropped as Busy if the owner holds the lock briefly.
    if (request->command == KaihenPluginLifecycle_CommitAcknowledged) lock.lock();
    else if (!lock.try_lock()) return ReloadBusy;
    switch (request->command) {
        case KaihenPluginLifecycle_Query:
        case KaihenPluginLifecycle_Health:
            if (request->operation_id) return BadInput;
            break;
        case KaihenPluginLifecycle_Prepare:
            if (!request->operation_id) return BadInput;
            if (request->operation_id == control.operation) break;
            if (control.state != KaihenPluginLifecycleState_Running || !control.context || !control.healthy)
                return ReloadBusy;
            if (!control.context->library.begin_quiesce()) return ReloadBusy;
            control.operation = request->operation_id;
            control.web_was_running = control.context->web.running();
            control.web_port = control.context->web.port();
            control.abort_requested = control.commit_pending = control.exit_requested = false;
            control.state = KaihenPluginLifecycleState_Draining;
            break;
        case KaihenPluginLifecycle_Poll:
            if (!request->operation_id || request->operation_id != control.operation) return BadInput;
            break;
        case KaihenPluginLifecycle_Abort:
            if (!request->operation_id || request->operation_id != control.operation || control.commit_pending || control.exit_requested)
                return BadInput;
            if (control.state == KaihenPluginLifecycleState_Draining || control.state == KaihenPluginLifecycleState_Quiesced)
                control.abort_requested = true;
            else if (control.state != KaihenPluginLifecycleState_Running) return BadInput;
            break;
        case KaihenPluginLifecycle_Commit:
            if (!request->operation_id || request->operation_id != control.operation || control.abort_requested)
                return BadInput;
            if (control.exit_requested || control.commit_pending) break;
            if (control.state != KaihenPluginLifecycleState_Quiesced) return ReloadBusy;
            control.commit_pending = true;
            break;
        // The server delivers this only after replying to Commit successfully.
        case KaihenPluginLifecycle_CommitAcknowledged:
            if (!request->operation_id || request->operation_id != control.operation || !control.commit_pending)
                return BadInput;
            control.exit_requested = true;
            control.state = KaihenPluginLifecycleState_Exiting;
            break;
        default: return BadInput;
    }
    *reply = {KAIHEN_PLUGIN_LIFECYCLE_VERSION,
        KAIHEN_PLUGIN_LIFECYCLE_COOPERATIVE |
            (control.healthy && control.state == KaihenPluginLifecycleState_Running ? KAIHEN_PLUGIN_LIFECYCLE_HEALTHY : 0u),
        control.state, control.result, control.operation, 0};
    return 0;
}

inline void resident_health(ResidentControl& control, const moader::LibrarySnapshot& view) {
    std::lock_guard<std::mutex> lock(control.mutex);
    control.healthy = !control.web_start_error && view.inventory_ready && view.inventory_error.empty() &&
        view.runtime.connected && !view.runtime.result && view.runtime.applied_generation == view.generation;
    if (control.state == KaihenPluginLifecycleState_Starting) control.state = KaihenPluginLifecycleState_Running;
    control.result = control.web_start_error ? BadInput : view.runtime.result;
}

// Return true only after the control thread has acknowledged Commit. This
// function is called by the resident owner, never a PluginService thread.
inline bool resident_lifecycle_step(ResidentControl& control, Context& context) {
    bool abort = false, drain = false, web_was_running = false;
    u16 port = 0;
    {
        std::lock_guard<std::mutex> lock(control.mutex);
        if (control.exit_requested) return true;
        abort = control.abort_requested;
        drain = control.state == KaihenPluginLifecycleState_Draining;
        web_was_running = control.web_was_running; port = control.web_port;
    }
    if (abort) {
        if (web_was_running && !context.web.running() && !context.web.start("0.0.0.0", port))
            throw std::runtime_error(context.web.last_error());
        context.library.cancel_quiesce();
        std::lock_guard<std::mutex> lock(control.mutex);
        control.abort_requested = false;
        control.state = KaihenPluginLifecycleState_Running;
    } else if (drain) {
        context.web.stop(); // Joins the worker, or throws without claiming drain.
        std::lock_guard<std::mutex> lock(control.mutex);
        control.state = KaihenPluginLifecycleState_Quiesced;
    }
    return false;
}
}
