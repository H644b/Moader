// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <switch.h>
#include "../../common/control.h"
#include "game_arm.hpp"
#include "sm_proxy.hpp"
#include "plugin_reload.hpp"
#include "census_relay.hpp"
#include "../../libkaihen/source/cmif_server.hpp"

/// HEN's IPC server. It serves the mig:usr control port, on which the
/// bootstrap hands HEN its handles and configuration, and the homebrew
/// sessions that it creates for processes that RO initialized. Between
/// requests its loop advances game arming.
class HenServer {
  public:
    HenServer();
    ~HenServer();
    HenServer(const HenServer&) = delete;
    HenServer& operator=(const HenServer&) = delete;

    /// Registers mig:usr.
    Result initialize();

    /// Serves requests until a kernel call fails.
    Result run();
    // Main-thread cached producer, also used by the separately owned relay.
    HenCensus census_;

  private:
    static constexpr u32 MaxSessions = 16;
    /// The most handles a request can copy (HIPC's limit).
    static constexpr u32 MaxCopyHandles = 15;

    enum class SessionKind {
        Control,  ///< A session on mig:usr.
        Homebrew, ///< A session from CreateHomebrewSession.
    };

    struct SessionRef {
        SessionKind kind;
        u32 index;
    };

    /// The sessions that the loop waits on, and the port before them while
    /// HEN has it.
    struct WaitList {
        Handle handles[1 + 2 * MaxSessions];
        SessionRef sessions[1 + 2 * MaxSessions]; ///< sessions[i] for handles[i], except the port.
        s32 count;
        s32 port_index; ///< -1 once the port is released.
    };

    /// A request, copied out of the message buffer. Commands that make IPC
    /// calls of their own overwrite the buffer.
    struct ReceivedRequest {
        bool accepted;   ///< A valid CMIF request with inline data and copied handles only.
        u32 type;        ///< CmifCommandType
        u32 command_id;
        u32 num_copy_handles;
        Handle copy_handles[MaxCopyHandles];
        u8 data[ounce::MessageSize];
        size_t data_size;
        bool census_scalar = false;
        u32 census_words = 0;
    };

    /// What a command answers.
    struct CommandReply {
        Result result = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        u8 data[sizeof(HenPluginName)] = {}; ///< The largest reply.
        size_t data_size = 0;
        /// A handle the reply copies to the client, or INVALID_HANDLE.
        Handle copy_handle = INVALID_HANDLE;
        /// The copied handle is a temporary of HEN's: close it once sent.
        bool close_copy_after_send = false;
        /// HEN keeps the handles that the request copied; otherwise they are closed.
        bool keeps_request_handles = false;

        template <typename T>
        void setData(const T& value) {
            static_assert(sizeof(T) <= sizeof(data));
            __builtin_memcpy(data, &value, sizeof(T));
            data_size = sizeof(T);
        }
    };

    struct PluginRecord {
        HenPluginName name = {};
        u64 process_id = 0;
        Handle control_session = INVALID_HANDLE;
        u64 generation = 1;
        u64 operation_id = 0;
        u64 package_operation_id = 0;
    };

    // The loop
    void collectWaits(WaitList& waits) const;
    void stepBetweenRequests();
    void advanceCensusRelay();
    bool censusControlBlocked(const PluginRecord& plugin) const;
    void acceptSession();
    Result serveSession(SessionRef session);
    Handle& sessionHandle(SessionRef session);
    void closeSession(SessionRef session, u32 reason = 2);
    void censusCounts();
    void censusClose(Handle& handle, u32 caller, u32 reason, u32 slot);
    void censusFsClone(u32 caller, Handle root, Handle clone, Result result);

    // Requests
    ReceivedRequest receiveRequest() const;
    CommandReply serveRequest(const ReceivedRequest& request, SessionRef session);
    void serveControlCommand(const ReceivedRequest& request, CommandReply& reply);
    void serveHomebrewCommand(const ReceivedRequest& request, u32 index, CommandReply& reply);

    // Control commands
    Result startGameArm(Handle bootstrap_process);
    Result createHomebrewSession(Handle copied_process, Handle* out_client);
    Result adoptFilesystemRoot(Handle copied_root);
    Result adoptServiceManager(Handle copied_session);
    Result adoptPlugin(const HenPluginRecord& plugin, Handle control_session);
    Result releaseControlPort();

    // Homebrew commands
    Result openPlugin(const HenPluginName& name, Handle* out_session);
    void removePlugin(u32 index);
    void removeExitedPlugins();
    Result getPluginInfo(const HenPluginName& name, HenPluginInfo& out);
    Result submitReload(u32 caller, const HenPluginReloadRequest& request, HenPluginReloadStatus& out);
    void advanceReload();
    PluginRecord* findPlugin(const char* name);

    Handle port_ = INVALID_HANDLE;
    Handle fs_root_ = INVALID_HANDLE;
    /// FS's SM session. Only the SM proxy uses it: programs get proxy
    /// sessions (GetServiceManager).
    Handle sm_session_ = INVALID_HANDLE;
    SmProxy sm_proxy_;
    bool settings_ready_ = false;
    Handle sessions_[MaxSessions] = {};
    Handle homebrew_sessions_[MaxSessions] = {};
    Handle homebrew_processes_[MaxSessions] = {};
    PluginRecord plugins_[HEN_MAX_PLUGINS] = {};
    u32 plugin_count_ = 0;
    HenStatus status_ = {};
    GameArm game_ = {};
    PluginReload reload_;
    reload::Journal reload_journal_;
    Handle reload_shell_ = INVALID_HANDLE;
    PluginLaunchSpec reload_spec_ = {};
    bool reload_configured_ = false;
    u64 reload_instance_ = 0;
    u64 census_session_failures_ = 0, census_close_failures_ = 0;
    bool census_query_ = false;
    // Heap-backed: an unconfirmed worker may outlive server teardown. Its
    // copied frame and borrowed control stay owned until the process exits.
    CensusRelay* census_relay_ = nullptr;
    u64 census_target_pid_ = 0, census_delivered_ = 0, census_last_export_ = 0;
    bool census_header_delivered_ = false;
    u64 census_exit_snapshot_ = 0;
};
