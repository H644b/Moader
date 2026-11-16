// SPDX-License-Identifier: GPL-2.0-only
#include <cstring>
#include <new>
#include "server.hpp"

namespace {

/// The service space that HEN registers mig:usr in.
constexpr u32 MigrationServiceSpace = 1;
/// How long the loop sleeps when no request is waiting.
constexpr u64 IdleSleepNs = 1000000;

bool isRequest(u32 type) {
    return type == CmifCommandType_Request || type == CmifCommandType_RequestWithContext;
}

bool hasBuffers(const HipcMetadata& meta) {
    return meta.num_send_statics || meta.num_recv_statics || meta.num_send_buffers ||
        meta.num_recv_buffers || meta.num_exch_buffers;
}

} // namespace

HenServer::HenServer() = default;

HenServer::~HenServer() {
    ounce::closeHandle(&reload_shell_);
    ounce::closeHandle(&fs_root_);
    ounce::closeHandle(&sm_session_);
    if (settings_ready_) {
        setsysExit();
    }
    for (Handle& session : sessions_) {
        ounce::closeHandle(&session);
    }
    for (Handle& session : homebrew_sessions_) {
        ounce::closeHandle(&session);
    }
    for (Handle& process : homebrew_processes_) {
        ounce::closeHandle(&process);
    }
    ounce::closeHandle(&port_);
    for (PluginRecord& plugin : plugins_) {
        if (!censusControlBlocked(plugin)) ounce::closeHandle(&plugin.control_session);
    }
    // A pending/failed join still borrows its heap state and control session.
    // The kernel will reclaim them when HEN exits; do not free them early.
    if (census_relay_ && census_relay_->ready()) delete census_relay_;
}

Result HenServer::initialize() {
    reload_instance_ = randomGet64();
    if (!reload_instance_) reload_instance_ = armGetSystemTick() | 1;
    census_.initialize(reload_instance_, henCensusFrequency());
    game_.setCensus(&census_);
    sm_proxy_.setCensus(&census_);
    status_.protocol_version = HEN_PROTOCOL_VERSION;
    Result rc = svcGetProcessId(&status_.process_id, CUR_PROCESS_HANDLE);
    if (R_SUCCEEDED(rc)) {
        rc = setsysInitialize();
        settings_ready_ = R_SUCCEEDED(rc);
    }
    if (R_SUCCEEDED(rc)) {
        rc = smRegisterServiceInSpace(&port_, MigrationServiceSpace, smEncodeName("mig:usr"),
                                      false, MaxSessions);
    }
    status_.initialization_result = rc;
    census_.emit(KaihenCensusSource_Hen, KaihenCensusEvent_Boot,
        HEN_PROTOCOL_VERSION, MaxSessions, 0, rc,
        KaihenCensusFlag_ResultKnown | (status_.process_id ? KaihenCensusFlag_OwnerKnown : 0), status_.process_id);
    censusCounts();
    if (R_SUCCEEDED(rc)) {
        census_relay_ = new (std::nothrow) CensusRelay;
        if (!census_relay_) census_.emit(KaihenCensusSource_Hen, KaihenCensusEvent_RelayState,
            0, CensusRelay::Failed, 0, MAKERESULT(Module_Libnx, LibnxError_OutOfMemory), KaihenCensusFlag_ResultKnown);
    }
    return rc;
}

//-----------------------------------------------------------------------------
// The loop
//-----------------------------------------------------------------------------

Result HenServer::run() {
    for (;;) {
        WaitList waits;
        collectWaits(waits);

        std::memset(armGetTls(), 0, ounce::MessageSize);
        s32 index = -1;
        const Result rc = svcReplyAndReceive(&index, waits.handles, waits.count,
                                             INVALID_HANDLE, 0);
        if (index < 0 || index >= waits.count) {
            if (rc == KERNELRESULT(TimedOut)) {
                stepBetweenRequests();
                continue;
            }
            if (R_FAILED(rc)) {
                census_.emit(KaihenCensusSource_Hen, KaihenCensusEvent_ServerFailure, 1, 0, 0, rc, KaihenCensusFlag_ResultKnown);
                return rc;
            }
            continue;
        }
        if (R_FAILED(rc) && !(index != waits.port_index && rc == KERNELRESULT(ConnectionClosed))) {
            census_.emit(KaihenCensusSource_Hen, KaihenCensusEvent_ServerFailure, 2, index, 0, rc, KaihenCensusFlag_ResultKnown);
            return rc;
        }

        if (index == waits.port_index) {
            acceptSession();
            continue;
        }
        const SessionRef session = waits.sessions[index];
        if (rc == KERNELRESULT(ConnectionClosed)) {
            closeSession(session, 1);
            continue;
        }
        const Result serve_result = serveSession(session);
        if (R_FAILED(serve_result)) {
            census_.emit(KaihenCensusSource_Hen, KaihenCensusEvent_ServerFailure, 3, index, 0,
                serve_result, KaihenCensusFlag_ResultKnown);
            return serve_result;
        }
        // Census queries are scalar memory copies, including their dispatch
        // tail. They must not advance a pending native reload operation.
        if (!census_query_) advanceReload();
    }
}

void HenServer::collectWaits(WaitList& waits) const {
    waits.count = 0;
    waits.port_index = -1;
    if (port_ != INVALID_HANDLE) {
        waits.port_index = waits.count;
        waits.handles[waits.count++] = port_;
    }
    for (u32 index = 0; index < MaxSessions; index++) {
        if (sessions_[index] != INVALID_HANDLE) {
            waits.sessions[waits.count] = {SessionKind::Control, index};
            waits.handles[waits.count++] = sessions_[index];
        }
    }
    for (u32 index = 0; index < MaxSessions; index++) {
        if (homebrew_sessions_[index] != INVALID_HANDLE) {
            waits.sessions[waits.count] = {SessionKind::Homebrew, index};
            waits.handles[waits.count++] = homebrew_sessions_[index];
        }
    }
}

/// Advances game arming between incoming requests.
void HenServer::stepBetweenRequests() {
    advanceReload();
    sm_proxy_.sampleCensus();
    advanceCensusRelay();
    // A diagnostic batch starts only while the gate is idle. Keep it idle
    // until the worker has returned and its join/close are confirmed.
    if (!census_relay_ || census_relay_->ready()) game_.step();
    svcSleepThread(IdleSleepNs);
}

bool HenServer::censusControlBlocked(const PluginRecord& plugin) const {
    return census_relay_ && census_relay_->blocksControl(plugin.control_session, plugin.process_id);
}

void HenServer::advanceCensusRelay() {
    if (!census_relay_) return;
    const u64 now = armGetSystemTick();
    if (census_relay_->poll(now)) {
        const auto& report = census_relay_->report();
        if (report.process_id == census_target_pid_ && report.boot_id == reload_instance_) {
            census_header_delivered_ |= report.header_acknowledged;
            if (report.acknowledged > census_delivered_) census_delivered_ = report.acknowledged;
        }
        // Successful delivery must not create new records, or the exporter
        // would continuously generate its own backlog.
        if (report.state != CensusRelay::Complete) census_.emit(KaihenCensusSource_Hen,
            KaihenCensusEvent_RelayState, report.process_id,
            u64(report.state) | (u64(report.unexpected_handles) << 32) | (u64(report.retained_handles) << 48),
            report.acknowledged, report.result,
            KaihenCensusFlag_ResultKnown | KaihenCensusFlag_After | KaihenCensusFlag_OwnerKnown,
            report.process_id);
    }
    const u64 exited = game_.lastObservedExit();
    if (exited && exited != census_exit_snapshot_) {
        // A retained channel is one of the states under investigation. Take
        // a local slot snapshot even when none of those channels has closed.
        censusCounts();
        census_exit_snapshot_ = exited;
    }
    if (!census_relay_->ready() || reload_journal_.active || !game_.diagnosticsIdle() ||
        now - census_last_export_ < armNsToTicks(100000000)) return;
    const u64 current = game_.status().process_id;
    const bool observed_host_exit = exited && (!current || current == exited);
    if (!observed_host_exit)
        for (Handle session : homebrew_sessions_) if (session != INVALID_HANDLE) return;
    auto* plugin = findPlugin("moader");
    if (!plugin || !plugin->process_id || plugin->control_session == INVALID_HANDLE ||
        census_relay_->disabledFor(plugin->process_id) || censusControlBlocked(*plugin)) return;
    if (plugin->process_id != census_target_pid_) {
        census_target_pid_ = plugin->process_id;
        census_delivered_ = 0;
        census_header_delivered_ = false;
    }
    const auto header = census_.header();
    u64 next = census_delivered_ + 1;
    if (next < header.first_sequence) next = header.first_sequence;
    if (census_header_delivered_ && next >= header.next_sequence) return;
    KaihenCensusRecord batch[CensusRelay::BatchCapacity]{};
    u32 count = 0;
    while (count < CensusRelay::BatchCapacity && next < header.next_sequence) {
        if (!census_.read(next++, batch[count])) return;
        ++count;
    }
    census_last_export_ = now;
    const Result rc = census_relay_->start(plugin->control_session, plugin->process_id,
        header, batch, count, now);
    if (R_FAILED(rc)) census_.emit(KaihenCensusSource_Hen, KaihenCensusEvent_RelayState,
        plugin->process_id, census_relay_->report().state, census_delivered_, rc,
        KaihenCensusFlag_ResultKnown | KaihenCensusFlag_After);
}

void HenServer::acceptSession() {
    Handle session = INVALID_HANDLE;
    const Result rc = svcAcceptSession(&session, port_);
    if (R_FAILED(rc)) {
        ++census_session_failures_;
        census_.emit(KaihenCensusSource_Hen, KaihenCensusEvent_SessionCreated, UINT32_MAX, 0, 0,
            rc, KaihenCensusFlag_ResultKnown | KaihenCensusFlag_After);
        censusCounts();
        return;
    }
    for (u32 index = 0; index < MaxSessions; index++) {
        Handle& available = sessions_[index];
        if (available == INVALID_HANDLE) {
            available = session;
            census_.emit(KaihenCensusSource_Hen, KaihenCensusEvent_SessionCreated,
                (u64(1) << 32) | index, session, 0, rc, KaihenCensusFlag_ResultKnown | KaihenCensusFlag_After);
            censusCounts();
            return;
        }
    }
    ++census_session_failures_;
    census_.emit(KaihenCensusSource_Hen, KaihenCensusEvent_SessionCreated,
        (u64(1) << 32) | UINT32_MAX, session, 0, rc, KaihenCensusFlag_ResultKnown | KaihenCensusFlag_After);
    censusClose(session, 1, 5, UINT32_MAX);
    censusCounts();
}

/// Answers the request that the session sent.
Result HenServer::serveSession(SessionRef session) {
    const ReceivedRequest request = receiveRequest();
    census_query_ = request.command_id == KAIHEN_CENSUS_HEADER_COMMAND ||
                    request.command_id == KAIHEN_CENSUS_RECORD_COMMAND;
    if (request.type == CmifCommandType_Close) {
        closeSession(session);
        return 0;
    }

    CommandReply reply = serveRequest(request, session);
    if (!reply.keeps_request_handles) {
        for (u32 index = 0; index < request.num_copy_handles; index++) {
            svcCloseHandle(request.copy_handles[index]);
        }
    }

    void* message = armGetTls();
    const u32 num_copy_handles = reply.copy_handle != INVALID_HANDLE ? 1 : 0;
    ounce::writeReply(message, reply.result, reply.data, reply.data_size,
                      &reply.copy_handle, num_copy_handles);
    const Result rc = ounce::sendReply(sessionHandle(session));
    if (reply.close_copy_after_send) {
        censusClose(reply.copy_handle, 4, 3, session.index);
    }
    if (rc == KERNELRESULT(ConnectionClosed)) {
        closeSession(session, 3);
        return 0;
    }
    return rc;
}

Handle& HenServer::sessionHandle(SessionRef session) {
    return session.kind == SessionKind::Homebrew ? homebrew_sessions_[session.index]
                                                 : sessions_[session.index];
}

void HenServer::censusClose(Handle& handle, u32 caller, u32 reason, u32 slot) {
    if (handle == INVALID_HANDLE) return;
    const Handle original = handle;
    const Result rc = svcCloseHandle(handle);
    handle = INVALID_HANDLE; // preserve the existing close policy
    if (R_FAILED(rc)) ++census_close_failures_;
    census_.emit(KaihenCensusSource_Hen,
        R_SUCCEEDED(rc) ? KaihenCensusEvent_SessionClosed : KaihenCensusEvent_SessionCloseFailure,
        original, slot, (static_cast<u64>(reason) << 32) | caller, rc,
        KaihenCensusFlag_ResultKnown | KaihenCensusFlag_After);
}

void HenServer::censusCounts() {
    u64 controls = 0, homebrew = 0, processes = 0;
    for (u32 index = 0; index < MaxSessions; index++) {
        controls += sessions_[index] != INVALID_HANDLE;
        homebrew += homebrew_sessions_[index] != INVALID_HANDLE;
        processes += homebrew_processes_[index] != INVALID_HANDLE;
    }
    census_.emit(KaihenCensusSource_Hen, KaihenCensusEvent_HenCounts,
        (controls << 32) | homebrew, processes,
        (census_close_failures_ << 32) | (census_session_failures_ & UINT32_MAX), 0, KaihenCensusFlag_Cached);
}

void HenServer::censusFsClone(u32 caller, Handle root, Handle clone, Result result) {
    census_.emit(KaihenCensusSource_Hen, KaihenCensusEvent_FsClone,
        caller, root, clone, result, KaihenCensusFlag_ResultKnown | KaihenCensusFlag_After);
}

void HenServer::closeSession(SessionRef session, u32 reason) {
    censusClose(sessionHandle(session), session.kind == SessionKind::Homebrew ? 2 : 1, reason, session.index);
    if (session.kind == SessionKind::Homebrew) {
        censusClose(homebrew_processes_[session.index], 3, reason, session.index);
    }
    censusCounts();
}

//-----------------------------------------------------------------------------
// Requests
//-----------------------------------------------------------------------------

/// Copies the request out of the message buffer. A request that is not
/// accepted still has its handles, so that they can be closed.
HenServer::ReceivedRequest HenServer::receiveRequest() const {
    void* message = armGetTls();
    const ounce::CmifRequest parsed = ounce::parseRequest(message);
    const HipcMetadata& meta = parsed.hipc.meta;

    ReceivedRequest request = {};
    request.type = meta.type;
    if (parsed.valid) request.command_id = parsed.command_id;
    request.census_words = meta.num_data_words;
    request.census_scalar = !hasBuffers(meta) && !meta.num_copy_handles &&
        !meta.num_move_handles && !meta.send_pid && meta.type == CmifCommandType_Request;
    request.num_copy_handles = meta.num_copy_handles < MaxCopyHandles ? meta.num_copy_handles
                                                                      : MaxCopyHandles;
    for (u32 index = 0; index < request.num_copy_handles; index++) {
        request.copy_handles[index] = parsed.hipc.data.copy_handles[index];
    }
    // HEN takes no moved handles; close them now.
    for (u32 index = 0; index < meta.num_move_handles; index++) {
        svcCloseHandle(parsed.hipc.data.move_handles[index]);
    }

    request.accepted = parsed.valid && !hasBuffers(meta) && !meta.num_move_handles;
    if (request.accepted) {
        request.data_size = parsed.data_size < sizeof(request.data) ? parsed.data_size
                                                                    : sizeof(request.data);
        std::memcpy(request.data, parsed.data, request.data_size);
    }
    return request;
}

/// Answers a request. The status commands answer on both kinds of session;
/// the others on one kind only. Anything else is BadInput.
HenServer::CommandReply HenServer::serveRequest(const ReceivedRequest& request,
                                                SessionRef session) {
    CommandReply reply;
    if (!request.accepted) {
        return reply;
    }
    if (request.type == CmifCommandType_Control &&
        request.command_id == ounce::CmifControl_QueryPointerBufferSize) {
        // HEN has no pointer buffer.
        const u16 pointer_buffer_size = 0;
        reply.result = 0;
        reply.setData(pointer_buffer_size);
        return reply;
    }
    if (!isRequest(request.type)) {
        return reply;
    }

    switch (request.command_id) {
    case KAIHEN_CENSUS_HEADER_COMMAND:
    case KAIHEN_CENSUS_RECORD_COMMAND: {
        const HenCensus::Query result = census_.query(request.command_id, request.census_scalar,
            request.census_words, request.data, request.data_size, reply.data, reply.data_size);
        reply.result = result == HenCensus::Query::Success ? 0 :
            MAKERESULT(Module_Libnx, result == HenCensus::Query::Unavailable ? LibnxError_NotFound : LibnxError_BadInput);
        return reply;
    }
    case HenControlCommand_GetStatus:
        if (!request.num_copy_handles) {
            reply.result = 0;
            reply.setData(status_);
        }
        return reply;
    case HenControlCommand_GetGameStatus:
        if (!request.num_copy_handles) {
            reply.result = 0;
            reply.setData(game_.status());
        }
        return reply;
    case HenControlCommand_MoaderGameTrace:
        if (!request.num_copy_handles) {
            reply.result = 0;
            reply.setData(game_.trace());
        }
        return reply;
    default:
        break;
    }

    if (session.kind == SessionKind::Control) {
        serveControlCommand(request, reply);
    } else {
        serveHomebrewCommand(request, session.index, reply);
    }
    return reply;
}

/// The commands of the bootstrap's control session.
void HenServer::serveControlCommand(const ReceivedRequest& request, CommandReply& reply) {
    const u32 copies = request.num_copy_handles;
    const Handle* handles = request.copy_handles;

    switch (request.command_id) {
    case HenControlCommand_ConfigurePluginReload:
        if (copies == 1 && request.data_size >= sizeof(HenPluginLaunchConfig) && !reload_configured_) {
            HenPluginLaunchConfig config;
            std::memcpy(&config, request.data, sizeof(config));
            if (config.program_id && config.storage_id == NcmStorageId_BuiltInSystem &&
                !config.reserved[0] && !config.reserved[1] && !config.reserved[2]) {
                reload_shell_ = handles[0];
                reload_spec_ = {config.program_id, config.flags, config.storage_id, {0, 0, 0}};
                reload_configured_ = true;
                reply.result = 0;
                reply.keeps_request_handles = true;
            }
        }
        break;
    case HenControlCommand_ConfigureGame:
        if (!copies && request.data_size >= sizeof(HenGameConfig)) {
            HenGameConfig configuration;
            std::memcpy(&configuration, request.data, sizeof(configuration));
            reply.result = game_.configure(configuration);
        }
        break;
    case HenControlCommand_AdoptGameHandles0:
        if (copies == 8) {
            reply.result = game_.adoptHandles0(handles);
            reply.keeps_request_handles = R_SUCCEEDED(reply.result);
        }
        break;
    case HenControlCommand_AdoptGameHandles1:
        if (copies == 7) {
            reply.result = game_.adoptHandles1(handles);
            reply.keeps_request_handles = R_SUCCEEDED(reply.result);
        }
        break;
    case HenControlCommand_AdoptLoaderRoot:
        if (copies == 1) {
            reply.result = game_.adoptLoaderRoot(handles[0]);
            reply.keeps_request_handles = R_SUCCEEDED(reply.result);
        }
        break;
    case HenControlCommand_StartGameArm:
        if (copies == 1 && fs_root_ != INVALID_HANDLE) {
            reply.result = startGameArm(handles[0]);
            reply.keeps_request_handles = R_SUCCEEDED(reply.result);
        }
        break;
    case HenControlCommand_CreateHomebrewSession:
        if (copies == 1) {
            reply.result = createHomebrewSession(handles[0], &reply.copy_handle);
            reply.close_copy_after_send = R_SUCCEEDED(reply.result);
            reply.keeps_request_handles = R_SUCCEEDED(reply.result);
        }
        break;
    case HenControlCommand_AdoptFilesystemRoot:
        if (copies == 1) {
            reply.result = adoptFilesystemRoot(handles[0]);
            reply.keeps_request_handles = R_SUCCEEDED(reply.result);
        }
        break;
    case HenControlCommand_AdoptServiceManager:
        if (copies == 1) {
            reply.result = adoptServiceManager(handles[0]);
            reply.keeps_request_handles = R_SUCCEEDED(reply.result);
        }
        break;
    case HenControlCommand_ReleaseControlPort:
        if (!copies) {
            reply.result = releaseControlPort();
        }
        break;
    case HenControlCommand_CreateServiceManagerSession:
        // A plugin's SM session, on the proxy as a homebrew program's is.
        if (!copies) {
            reply.result = sm_proxy_.openSession(&reply.copy_handle);
            reply.close_copy_after_send = R_SUCCEEDED(reply.result);
        }
        break;
    case HenControlCommand_CloneFilesystemRoot:
        // A plugin's FS root: a session of its own, not HEN's root.
        if (!copies && fs_root_ != INVALID_HANDLE) {
            reply.result = cmifCloneCurrentObject(fs_root_, &reply.copy_handle);
            censusFsClone(1, fs_root_, reply.copy_handle, reply.result);
            reply.close_copy_after_send = R_SUCCEEDED(reply.result);
        }
        break;
    case HenControlCommand_AdoptPlugin:
        if (copies == 1 && request.data_size >= sizeof(HenPluginRecord)) {
            HenPluginRecord plugin;
            std::memcpy(&plugin, request.data, sizeof(plugin));
            reply.result = adoptPlugin(plugin, handles[0]);
            reply.keeps_request_handles = R_SUCCEEDED(reply.result);
        }
        break;
    default:
        break;
    }
}

/// The commands of a homebrew process's session.
void HenServer::serveHomebrewCommand(const ReceivedRequest& request, u32 index,
                                     CommandReply& reply) {
    if (request.num_copy_handles) {
        return;
    }

    switch (request.command_id) {
    case HenHomebrewCommand_GetReloadCapabilities: {
        HenReloadCapabilities caps = {1, reload_configured_ ? HEN_PLUGIN_RELOAD_AVAILABLE : 0,
                                      reload_instance_, reload_journal_.active};
        if (reload_journal_.active) caps.flags |= HEN_PLUGIN_RELOAD_BUSY;
        if (reload_journal_.blocked) caps.flags |= HEN_PLUGIN_RELOAD_BLOCKED;
        reply.result = 0;
        reply.setData(caps);
        break;
    }
    case HenHomebrewCommand_GetPluginInfo: {
        HenPluginName name;
        if (request.data_size < sizeof(name)) break;
        std::memcpy(&name, request.data, sizeof(name));
        HenPluginInfo info = {};
        reply.result = getPluginInfo(name, info);
        if (R_SUCCEEDED(reply.result)) reply.setData(info);
        break;
    }
    case HenHomebrewCommand_ReloadPlugin: {
        HenPluginReloadRequest input;
        if (request.data_size < sizeof(input)) break;
        std::memcpy(&input, request.data, sizeof(input));
        HenPluginReloadStatus output = {};
        reply.result = submitReload(index, input, output);
        if (R_SUCCEEDED(reply.result)) reply.setData(output);
        break;
    }
    case HenHomebrewCommand_GetPluginReloadStatus: {
        u64 id = 0;
        if (request.data_size < sizeof(id)) break;
        std::memcpy(&id, request.data, sizeof(id));
        advanceReload();
        auto* entry = reload_journal_.find(id);
        reply.result = entry ? 0 : reload::Expired;
        if (entry) reply.setData(entry->status);
        break;
    }
    case HenHomebrewCommand_GetPluginReloadDiagnostics: {
        u64 id = 0;
        if (request.data_size < sizeof(id)) break;
        std::memcpy(&id, request.data, sizeof(id));
        // Only copy the snapshot published by advanceReload. In particular,
        // never probe the gate or issue RO/FS IPC from the query thread.
        const auto* entry = reload_journal_.find(id);
        reply.result = entry ? 0 : reload::Expired;
        if (entry) reply.setData(entry->diagnostics);
        break;
    }
    case HenHomebrewCommand_GetFilesystemRoot:
        if (fs_root_ == INVALID_HANDLE) {
            reply.result = MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
            break;
        }
        reply.result = cmifCloneCurrentObject(fs_root_, &reply.copy_handle);
        censusFsClone(2, fs_root_, reply.copy_handle, reply.result);
        reply.close_copy_after_send = R_SUCCEEDED(reply.result);
        break;
    case HenHomebrewCommand_GetSettingsSession:
        reply.result = cmifCloneCurrentObject(setsysGetServiceSession()->session,
                                              &reply.copy_handle);
        reply.close_copy_after_send = R_SUCCEEDED(reply.result);
        break;
    case HenHomebrewCommand_GetProcessHandle:
        // The process that RO initialized this session for. HEN keeps it.
        reply.result = 0;
        reply.copy_handle = homebrew_processes_[index];
        break;
    case HenHomebrewCommand_GetServiceManager:
        // A session of the program's own on the SM proxy, never the master:
        // SM closes a session after a malformed request, and the master
        // serves every program.
        reply.result = sm_proxy_.openSession(&reply.copy_handle);
        reply.close_copy_after_send = R_SUCCEEDED(reply.result);
        break;
    case HenHomebrewCommand_GetPluginCount:
        removeExitedPlugins();
        reply.result = 0;
        reply.setData(plugin_count_);
        break;
    case HenHomebrewCommand_GetPluginName: {
        u32 plugin = 0;
        if (request.data_size < sizeof(plugin)) {
            break;
        }
        std::memcpy(&plugin, request.data, sizeof(plugin));
        if (plugin >= plugin_count_) {
            reply.result = MAKERESULT(Module_Libnx, LibnxError_NotFound);
            break;
        }
        reply.result = 0;
        reply.setData(plugins_[plugin].name);
        break;
    }
    case HenHomebrewCommand_OpenPlugin: {
        HenPluginName name;
        if (request.data_size < sizeof(name)) {
            break;
        }
        std::memcpy(&name, request.data, sizeof(name));
        removeExitedPlugins();
        reply.result = openPlugin(name, &reply.copy_handle);
        reply.close_copy_after_send = R_SUCCEEDED(reply.result);
        break;
    }
    default:
        break;
    }
}

//-----------------------------------------------------------------------------
// Control commands
//-----------------------------------------------------------------------------

/// Starts game arming on a clone of HEN's FS root. The game arm closes the
/// bootstrap's process handle once it has started.
Result HenServer::startGameArm(Handle bootstrap_process) {
    Handle root = INVALID_HANDLE;
    Result rc = cmifCloneCurrentObject(fs_root_, &root);
    censusFsClone(3, fs_root_, root, rc);
    if (R_FAILED(rc)) {
        return rc;
    }
    rc = game_.start(root, bootstrap_process);
    if (R_FAILED(rc)) {
        svcCloseHandle(root);
    }
    return rc;
}

/// Creates a session for a process that RO initialized, and keeps the
/// process handle for GetProcessHandle.
Result HenServer::createHomebrewSession(Handle copied_process, Handle* out_client) {
    for (u32 index = 0; index < MaxSessions; index++) {
        if (homebrew_sessions_[index] != INVALID_HANDLE) {
            continue;
        }
        Handle server = INVALID_HANDLE;
        Handle client = INVALID_HANDLE;
        Result rc = svcCreateSession(&server, &client, 0, 0);
        census_.emit(KaihenCensusSource_Hen, KaihenCensusEvent_SessionCreated,
            (u64(2) << 32) | index, server, copied_process, rc, KaihenCensusFlag_ResultKnown | KaihenCensusFlag_After);
        if (R_FAILED(rc)) ++census_session_failures_;
        if (R_SUCCEEDED(rc)) {
            homebrew_sessions_[index] = server;
            homebrew_processes_[index] = copied_process;
            *out_client = client;
        }
        censusCounts();
        return rc;
    }
    ++census_session_failures_;
    census_.emit(KaihenCensusSource_Hen, KaihenCensusEvent_SessionCreated, (u64(2) << 32) | UINT32_MAX, 0, copied_process,
        MAKERESULT(Module_Libnx, LibnxError_OutOfMemory), KaihenCensusFlag_ResultKnown | KaihenCensusFlag_After);
    censusCounts();
    return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
}

Result HenServer::adoptFilesystemRoot(Handle copied_root) {
    if (fs_root_ != INVALID_HANDLE) {
        return MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized);
    }
    fs_root_ = copied_root;
    return 0;
}

Result HenServer::adoptServiceManager(Handle copied_session) {
    if (sm_session_ != INVALID_HANDLE) {
        return MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized);
    }
    sm_session_ = copied_session;
    sm_proxy_.setMaster(sm_session_);
    return 0;
}

Result HenServer::releaseControlPort() {
    if (port_ == INVALID_HANDLE) {
        return 0;
    }
    const Result rc = smUnregisterServiceInSpace(MigrationServiceSpace, smEncodeName("mig:usr"));
    if (R_FAILED(rc)) {
        return rc;
    }
    ounce::closeHandle(&port_);
    return 0;
}

Result HenServer::adoptPlugin(const HenPluginRecord& plugin, Handle control_session) {
    const HenPluginName& name = plugin.name;
    if (!name.name[0] || !std::memchr(name.name, '\0', sizeof(name.name))) {
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }
    for (u32 index = 0; index < plugin_count_; index++) {
        if (!std::strcmp(plugins_[index].name.name, name.name)) {
            return MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized);
        }
    }
    if (plugin_count_ == HEN_MAX_PLUGINS) {
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    }
    plugins_[plugin_count_++] = {name, plugin.process_id, control_session};
    return 0;
}

//-----------------------------------------------------------------------------
// Homebrew commands
//-----------------------------------------------------------------------------

/// Asks the named plugin for a new client session.
Result HenServer::openPlugin(const HenPluginName& name, Handle* out_session) {
    if (!std::memchr(name.name, '\0', sizeof(name.name))) {
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }
    for (u32 index = 0; index < plugin_count_; index++) {
        if (std::strcmp(plugins_[index].name.name, name.name)) {
            continue;
        }
        if (censusControlBlocked(plugins_[index])) return reload::Busy;
        if (plugins_[index].operation_id) {
            const auto* operation = reload_journal_.find(plugins_[index].operation_id);
            if ((operation && (operation->status.flags & (HEN_PLUGIN_RELOAD_BUSY | HEN_PLUGIN_RELOAD_BLOCKED))) ||
                plugins_[index].control_session == INVALID_HANDLE) return reload::Busy;
        }
        if (plugins_[index].control_session == INVALID_HANDLE) return reload::Busy;
        Service control = {};
        serviceCreate(&control, plugins_[index].control_session);
        control.own_handle = false;
        const Result rc = serviceDispatch(&control, HEN_PLUGIN_CONTROL_CREATE_SESSION,
            .out_handle_attrs = {SfOutHandleAttr_HipcCopy},
            .out_handles = out_session,
        );
        // The plugin closed its control session, so it no longer serves.
        if (rc == KERNELRESULT(ConnectionClosed)) {
            if (plugins_[index].operation_id || game_.processExists(plugins_[index].process_id)) {
                ounce::closeHandle(&plugins_[index].control_session);
                return rc;
            }
            removePlugin(index);
            return MAKERESULT(Module_Libnx, LibnxError_NotFound);
        }
        return rc;
    }
    return MAKERESULT(Module_Libnx, LibnxError_NotFound);
}

void HenServer::removePlugin(u32 index) {
    if (censusControlBlocked(plugins_[index])) return;
    svcCloseHandle(plugins_[index].control_session);
    plugin_count_--;
    for (u32 next = index; next < plugin_count_; next++) {
        plugins_[next] = plugins_[next + 1];
    }
    plugins_[plugin_count_] = {};
}

/// Stops listing the plugins whose process has exited. It runs before the
/// count is returned and before a plugin is opened, so a list read between
/// the two can still name a plugin that exits in the meantime.
void HenServer::removeExitedPlugins() {
    for (u32 index = plugin_count_; index > 0; index--) {
        if (!plugins_[index - 1].operation_id && !censusControlBlocked(plugins_[index - 1]) &&
            !game_.processExists(plugins_[index - 1].process_id)) {
            removePlugin(index - 1);
        }
    }
}

HenServer::PluginRecord* HenServer::findPlugin(const char* name) {
    for (u32 index = 0; index < plugin_count_; ++index)
        if (!std::strcmp(plugins_[index].name.name, name)) return &plugins_[index];
    return nullptr;
}

Result HenServer::getPluginInfo(const HenPluginName& name, HenPluginInfo& out) {
    if (!reload::validName(name.name)) return reload::Invalid;
    advanceReload();
    removeExitedPlugins();
    out = {};
    out.instance_id = reload_instance_;
    // Explicit successful absence is the ONLY new-client fallback signal.
    auto* plugin = findPlugin(name.name);
    if (!plugin) return 0;
    out.process_id = plugin->process_id;
    out.generation = plugin->generation;
    out.operation_id = plugin->operation_id;
    out.flags = reload_configured_ ? HEN_PLUGIN_RELOAD_AVAILABLE : 0;
    if (auto* entry = reload_journal_.find(plugin->operation_id)) {
        out.phase = entry->status.phase;
        out.flags |= entry->status.flags;
    }
    return 0;
}

Result HenServer::submitReload(u32 caller, const HenPluginReloadRequest& request, HenPluginReloadStatus& out) {
    advanceReload();
    u64 caller_pid = 0;
    R_TRY(svcGetProcessId(&caller_pid, homebrew_processes_[caller]));
    reload::Journal::Entry* entry = nullptr;
    R_TRY(reload_journal_.lookup(caller_pid, request, &entry));
    if (entry) { out = entry->status; return 0; }
    if (census_relay_ && !census_relay_->ready()) return reload::Busy;
    if (!reload_configured_) return reload::Capability;
    if (request.instance_id != reload_instance_) return reload::Stale;
    auto* plugin = findPlugin(request.name);
    if (!plugin || !plugin->process_id || plugin->process_id != request.expected_process_id ||
        plugin->generation != request.expected_generation || plugin->generation == UINT64_MAX ||
        plugin->control_session == INVALID_HANDLE) return reload::Stale;
    if (censusControlBlocked(*plugin)) return reload::Busy;
    PluginLaunchContext context = {};
    if (!game_.acquireReloadLease(context)) return reload::Busy;
    Handle fs = INVALID_HANDLE, sm = INVALID_HANDLE;
    Result rc = cmifCloneCurrentObject(fs_root_, &fs);
    censusFsClone(4, fs_root_, fs, rc);
    if (R_SUCCEEDED(rc)) rc = sm_proxy_.openSession(&sm);
    if (R_SUCCEEDED(rc)) rc = reload_journal_.begin(caller_pid, request, &entry);
    if (R_FAILED(rc)) {
        ounce::closeHandle(&fs);
        ounce::closeHandle(&sm);
        game_.releaseReloadLease();
        return rc;
    }
    plugin->operation_id = entry->status.operation_id;
    const PluginReload::Input input = {context, reload_spec_, request, reload_shell_, plugin->control_session, sm, fs,
                                      plugin->package_operation_id};
    rc = reload_.start(input, entry->status);
    if (R_FAILED(rc)) {
        ounce::closeHandle(&fs);
        ounce::closeHandle(&sm);
        entry->status.result = rc;
        entry->diagnostics.primary_result = rc;
        entry->status.phase = HenPluginReloadPhase_Failed;
        reload_journal_.finish(entry->status.operation_id, false);
        game_.releaseReloadLease();
    }
    out = entry->status;
    // A reserved ticket remains queryable even if creating its worker failed.
    return 0;
}

void HenServer::advanceReload() {
    if (!reload_journal_.active) return;
    auto* entry = reload_journal_.find(reload_journal_.active);
    if (!entry) return;
    const bool complete = reload::completedSnapshot(reload_, entry->status);
    entry->diagnostics = reload_.diagnostics();
    if (!complete) return;
    auto* plugin = findPlugin(entry->request.name);
    bool quarantine = reload_.quarantined();
    if (reload::publishable(entry->status, reload_.succeeded(), quarantine) &&
        plugin && plugin->generation != UINT64_MAX) {
        ounce::closeHandle(&plugin->control_session);
        plugin->control_session = reload_.takeControl();
        plugin->process_id = entry->status.new_process_id;
        plugin->package_operation_id = entry->status.operation_id;
        ++plugin->generation;
        entry->status.generation = plugin->generation;
        entry->status.phase = HenPluginReloadPhase_Complete;
        entry->status.result = 0;
    } else if (reload_.succeeded()) {
        // An impossible registry/generation conflict cannot turn a healthy
        // but unpublished process into an untracked writer.
        quarantine = true;
        entry->status.phase = HenPluginReloadPhase_Quarantined;
        entry->status.result = reload::Stale;
    }
    if (entry->status.phase != HenPluginReloadPhase_Complete &&
        (entry->status.flags & HEN_PLUGIN_RELOAD_OLD_EXITED)) {
        if (plugin) { ounce::closeHandle(&plugin->control_session); plugin->process_id = 0; }
    }
    reload_journal_.finish(entry->status.operation_id, quarantine);
    if (!quarantine) game_.releaseReloadLease();
}
