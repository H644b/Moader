#pragma once
#include <exception>
#include <string>
#ifdef __SWITCH__
#include <switch.h>
#include <cstdio>
#else
#include <thread>
#endif

namespace moader {
// Keep the native worker within the plugin's demonstrated scheduling contract.
// A pthread additionally requests priority 0x3b and SetThreadCoreMask. HTTP ZIP
// extraction needs much more stack than the plugin's small IPC dispatcher.
class WebWorker {
#ifdef __SWITCH__
    Thread thread_{};
    bool created_ = false, started_ = false, exited_ = false;
    static std::string failure(const char* stage, Result result) {
        char message[64];
        std::snprintf(message, sizeof(message), "Web worker %s: 0x%08X", stage, result);
        return message;
    }
#else
    std::thread thread_;
#endif
public:
    WebWorker() = default;
    WebWorker(const WebWorker&) = delete;
    WebWorker& operator=(const WebWorker&) = delete;
    ~WebWorker() {
        // As with std::thread, destruction cannot detach a worker that borrows
        // its owner's memory. The owner must complete stop/join first.
        if (joinable()) std::terminate();
    }
    bool joinable() const {
#ifdef __SWITCH__
        return created_; // Includes cleanup retained after a failed start/close.
#else
        return thread_.joinable();
#endif
    }
    bool start(void (*entry)(void*), void* context, std::string& error) {
        if (joinable()) {
            if (error.empty()) error = "Web worker cleanup is pending. Stop it before restarting.";
            return false;
        }
#ifdef __SWITCH__
        constexpr size_t StackSize = 0x40000; // 256 KiB; ZIP alone has 64 KiB of local buffers.
        Result rc = threadCreate(&thread_, entry, context, nullptr, StackSize, 0x2c, -2);
        if (R_FAILED(rc)) { error = failure("create", rc); return false; }
        created_ = true;
        rc = threadStart(&thread_);
        if (R_FAILED(rc)) {
            const Result close_rc = threadClose(&thread_);
            if (R_SUCCEEDED(close_rc)) { thread_ = {}; created_ = false; }
            error = failure("start", rc);
            if (R_FAILED(close_rc)) error += "; " + failure("close", close_rc);
            return false;
        }
        started_ = true;
#else
        try { thread_ = std::thread(entry, context); }
        catch (const std::exception& e) {
            error = std::string("Web worker start: ") + e.what(); return false;
        }
#endif
        return true;
    }
    bool join(std::string& error) {
        if (!joinable()) return true;
#ifdef __SWITCH__
        if (started_ && !exited_) {
            const Result rc = threadWaitForExit(&thread_);
            if (R_FAILED(rc)) { error = failure("wait", rc); return false; }
            exited_ = true;
        }
        const Result rc = threadClose(&thread_);
        if (R_FAILED(rc)) { error = failure("close", rc); return false; }
        thread_ = {}; created_ = started_ = exited_ = false;
#else
        try { thread_.join(); }
        catch (const std::exception& e) {
            error = std::string("Web worker join: ") + e.what(); return false;
        }
#endif
        return true;
    }
};
}
