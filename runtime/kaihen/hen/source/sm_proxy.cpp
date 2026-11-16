// SPDX-License-Identifier: GPL-2.0-only
#include "sm_proxy.hpp"

#include <cstring>
#include <new>

using ounce::sm::Command;
using ounce::sm::Protocol;
using ounce::sm::Request;
using ounce::sm::RequestKind;

namespace {

/// The result for a well-formed request that the proxy does not serve.
constexpr Result UnsupportedCommand = MAKERESULT(Module_Libnx, LibnxError_NotFound);

/// Holds a mutex for a scope.
class LockGuard {
  public:
    explicit LockGuard(Mutex* mutex) : mutex_(mutex) { mutexLock(mutex_); }
    ~LockGuard() { mutexUnlock(mutex_); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;

  private:
    Mutex* mutex_;
};

/// Closes the handles that a request carried. The proxy accepts no handles,
/// but the kernel has already put them into HEN.
void closeCarriedHandles(void* message) {
    const HipcParsedRequest hipc = hipcParseRequest(message);
    for (u32 index = 0; index < hipc.meta.num_copy_handles; index++)
        svcCloseHandle(hipc.data.copy_handles[index]);
    for (u32 index = 0; index < hipc.meta.num_move_handles; index++)
        svcCloseHandle(hipc.data.move_handles[index]);
}

} // namespace

void SmProxy::setMaster(Handle master) {
    LockGuard guard(&lock_);
    master_ = master;
}

/// Joins and frees the sessions whose thread has ended. Under lock_.
Result SmProxy::reapFinishedSessions() {
    sampleCensus();
    Session** link = &sessions_;
    while (Session* session = *link) {
        if (session->running.load(std::memory_order_acquire)) {
            link = &session->next;
            continue;
        }
        sampleSessionCensus(*session); // ended publisher is stable before deletion
        if (session->thread_started && !session->thread_exited) {
            const Result rc = threadWaitForExit(&session->thread);
            if (R_FAILED(rc)) {
                if (census_) census_->emit(KaihenCensusSource_Sm, KaihenCensusEvent_SmFailure,
                    session->census_id, 5, 0, rc, KaihenCensusFlag_ResultKnown);
                return rc;
            }
            session->thread_exited = true;
            if (census_) classifyCensus(*session);
        }
        const Result rc = threadClose(&session->thread);
        if (R_FAILED(rc)) {
            if (census_) census_->emit(KaihenCensusSource_Sm, KaihenCensusEvent_SmFailure,
                session->census_id, 6, session->thread_exited, rc, KaihenCensusFlag_ResultKnown);
            sampleCensus();
            return rc;
        }
        if (census_) census_->emit(KaihenCensusSource_Sm, KaihenCensusEvent_SmReaped,
            session->census_id, ++census_reaped_, census_loss_, rc, KaihenCensusFlag_ResultKnown | KaihenCensusFlag_After);
        if (census_cursor_ == session) census_cursor_ = session->next;
        if (census_) classifyCensus(*session, true);
        *link = session->next;
        delete session;
    }
    sampleCensus();
    return 0;
}

Result SmProxy::collectFinishedSessions() {
    LockGuard guard(&lock_);
    return reapFinishedSessions();
}

Result SmProxy::openSession(Handle* out_client) {
    LockGuard guard(&lock_);
    if (master_ == INVALID_HANDLE)
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    const Result reap = reapFinishedSessions();
    if (R_FAILED(reap)) return reap;

    Session* session = new (std::nothrow) Session;
    if (!session) {
        if (census_) census_->emit(KaihenCensusSource_Sm, KaihenCensusEvent_SmFailure,
            0, 1, 0, MAKERESULT(Module_Libnx, LibnxError_OutOfMemory), KaihenCensusFlag_ResultKnown);
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    }
    session->census_id = census_next_id_++;

    Handle client = INVALID_HANDLE;
    Result rc = svcCreateSession(&session->server, &client, 0, 0);
    if (census_) census_->emit(KaihenCensusSource_Sm, KaihenCensusEvent_SmCreated,
        session->census_id, session->server, client, rc, KaihenCensusFlag_ResultKnown | KaihenCensusFlag_After);
    if (R_FAILED(rc)) {
        delete session;
        return rc;
    }

    session->proxy = this;
    session->running.store(true, std::memory_order_release);
    rc = threadCreate(&session->thread, sessionMain, session, nullptr,
                      ThreadStackSize, ThreadPriority, -2);
    if (R_FAILED(rc) && census_) census_->emit(KaihenCensusSource_Sm, KaihenCensusEvent_SmFailure,
        session->census_id, 3, 0, rc, KaihenCensusFlag_ResultKnown);
    bool thread_created = R_SUCCEEDED(rc);
    if (R_SUCCEEDED(rc)) {
        rc = threadStart(&session->thread);
        session->thread_started = R_SUCCEEDED(rc);
        if (census_) census_->emit(KaihenCensusSource_Sm,
            R_SUCCEEDED(rc) ? KaihenCensusEvent_SmStarted : KaihenCensusEvent_SmFailure,
            session->census_id, 4, 0, rc, KaihenCensusFlag_ResultKnown | KaihenCensusFlag_After);
    }
    if (R_FAILED(rc)) {
        const Result server_close = svcCloseHandle(session->server);
        const Result client_close = svcCloseHandle(client);
        if (census_ && R_FAILED(server_close)) census_->emit(KaihenCensusSource_Sm, KaihenCensusEvent_SmFailure,
            session->census_id, 8, session->server, server_close, KaihenCensusFlag_ResultKnown);
        if (census_ && R_FAILED(client_close)) census_->emit(KaihenCensusSource_Sm, KaihenCensusEvent_SmFailure,
            session->census_id, 9, client, client_close, KaihenCensusFlag_ResultKnown);
        session->server = INVALID_HANDLE;
        session->running.store(false, std::memory_order_release);
        const Result close_rc = thread_created ? threadClose(&session->thread) : 0;
        if (thread_created && R_FAILED(close_rc)) {
            if (census_) census_->emit(KaihenCensusSource_Sm, KaihenCensusEvent_SmFailure,
                session->census_id, 6, 0, close_rc, KaihenCensusFlag_ResultKnown);
            // Keep the descriptor's backing context after an unstarted
            // thread could not be closed. A later collection resumes it.
            session->next = sessions_;
            sessions_ = session;
            if (census_) classifyCensus(*session);
            sampleCensus();
            return rc;
        }
        delete session;
        return rc;
    }

    session->next = sessions_;
    sessions_ = session;
    if (census_) classifyCensus(*session);
    sampleCensus();
    *out_client = client;
    return 0;
}

void SmProxy::sessionMain(void* argument) {
    Session* session = static_cast<Session*>(argument);
    session->proxy->serve(*session);
}

void SmProxy::serve(Session& session) {
    Handle reply_target = INVALID_HANDLE;
    Result receive_result = 0;
    for (;;) {
        s32 index = -1;
        const Result rc = svcReplyAndReceive(&index, &session.server, 1, reply_target, UINT64_MAX);
        reply_target = INVALID_HANDLE;
        // SessionClosed: the program closed its end (or exited).
        receive_result = rc;
        if (R_FAILED(rc)) {
            session.census_end_reason = rc == KERNELRESULT(ConnectionClosed) ? 1 : 4;
            break;
        }
        if (!respond(session))
            break;
        reply_target = session.server;
    }
    const Result close_result = svcCloseHandle(session.server);
    session.server = INVALID_HANDLE;
    HenCensusSmMailbox::View last = {};
    session.census_mailbox.sample(last); // this worker is its sole writer
    session.census_mailbox.publish(HenCensusSmMailbox::Ended, last.name, last.lookup,
                                   receive_result, close_result, session.census_end_reason);
    session.running.store(false, std::memory_order_release);
}

bool SmProxy::respond(Session& session) {
    void* const message = armGetTls();
    // A forwarded request reuses the message buffer: decode the program's
    // request completely first.
    const Request request = ounce::sm::decodeRequest(message);

    switch (request.kind) {
    case RequestKind::Close:
        session.census_end_reason = 2;
        return false;
    case RequestKind::Invalid:
        session.census_end_reason = 3;
        closeCarriedHandles(message);
        return false;
    case RequestKind::QueryPointerBufferSize:
        ounce::sm::writePointerBufferSizeReply(message);
        return true;
    case RequestKind::UnsupportedCommand:
        ounce::sm::writeReply(message, request.protocol, UnsupportedCommand);
        return true;
    case RequestKind::Command:
        break;
    }

    Handle handle = INVALID_HANDLE;
    Result rc = 0;
    switch (request.command) {
    case Command::RegisterClient:
    case Command::DetachClient:
        // The master is registered once, as FS. A program must not change
        // that registration for the others.
        rc = 0;
        break;
    case Command::GetServiceHandle:
    {
        HenCensusSmMailbox::View last = {};
        session.census_mailbox.sample(last);
        const u64 lookup = last.lookup + 1;
        u64 name = 0;
        std::memcpy(&name, &request.name, sizeof(name));
        session.census_mailbox.publish(HenCensusSmMailbox::LookupPending, name, lookup);
        rc = lookUpService(request.name, &handle);
        session.census_mailbox.publish(HenCensusSmMailbox::LookupReturned, name, lookup, rc);
        break;
    }
    case Command::RegisterService:
    case Command::UnregisterService:
        // A registration through the master would be FS's, and would
        // outlive the program. Programs register with SM sessions of their
        // own.
        rc = UnsupportedCommand;
        break;
    }
    ounce::sm::writeReply(message, request.protocol, rc,
                          R_SUCCEEDED(rc) ? handle : INVALID_HANDLE);
    return true;
}

void SmProxy::classifyCensus(Session& session, bool remove) {
    const auto adjust = [&](u32 state, bool add) {
        if (state == UINT32_MAX) return;
        const unsigned category = (state & 1) ? 0 : 1;
        if (add) census_current_[category]++; else census_current_[category]--;
        if (state & 2) { if (add) census_current_[2]++; else census_current_[2]--; }
        if (state & 4) { if (add) census_current_[3]++; else census_current_[3]--; }
    };
    const u32 state = remove ? UINT32_MAX :
        (session.running.load(std::memory_order_acquire) ? 1u : 0u) |
        (session.thread_exited ? 2u : 0u) | (!session.thread_started ? 4u : 0u);
    if (state != session.census_class) {
        adjust(session.census_class, false); adjust(state, true); session.census_class = state;
    }
}

void SmProxy::sampleSessionCensus(Session& item) {
    if (!census_) return;
    classifyCensus(item);
    Session* session = &item;
    HenCensusSmMailbox::View view = {};
    if (!session->census_mailbox.sample(view)) return;
    if (view.revision == session->census_revision) return;
    const u64 skipped = (view.revision - session->census_revision) / 2;
    if (skipped > 1) {
        census_loss_ += skipped - 1;
        census_->emit(KaihenCensusSource_Sm, KaihenCensusEvent_SmMailboxLoss,
            session->census_id, skipped - 1, census_loss_, 0, KaihenCensusFlag_Cached);
    }
    if (view.failures != session->census_failures) {
        census_->emit(KaihenCensusSource_Sm, KaihenCensusEvent_SmFailure,
            session->census_id, (view.failures << 32) | 7, view.failure_name, view.failure_result,
            KaihenCensusFlag_ResultKnown | KaihenCensusFlag_Cached |
                (view.failures > session->census_failures + 1 ? KaihenCensusFlag_Incomplete : 0));
        session->census_failures = view.failures;
    }
    if (session->census_pending && view.lookup != session->census_pending) {
        census_->emit(KaihenCensusSource_Sm, KaihenCensusEvent_SmLookupEnd,
            session->census_id, 0, session->census_pending, 0,
            KaihenCensusFlag_Cached | KaihenCensusFlag_Incomplete);
        session->census_pending = 0;
    }
    if (view.stage == HenCensusSmMailbox::LookupPending && view.lookup != session->census_pending) {
        census_->emit(KaihenCensusSource_Sm, KaihenCensusEvent_SmLookupBegin,
            session->census_id, view.name, view.lookup, 0, KaihenCensusFlag_Cached | KaihenCensusFlag_Before);
        session->census_pending = view.lookup;
    } else if (view.stage != HenCensusSmMailbox::LookupPending && session->census_pending) {
        const bool known = view.lookup == session->census_pending && view.stage == HenCensusSmMailbox::LookupReturned;
        census_->emit(KaihenCensusSource_Sm, KaihenCensusEvent_SmLookupEnd,
            session->census_id, view.name, view.lookup, known ? view.result : 0,
            KaihenCensusFlag_Cached | (known ? KaihenCensusFlag_ResultKnown : KaihenCensusFlag_Incomplete),
            0);
        session->census_pending = 0;
    }
    if (view.stage == HenCensusSmMailbox::Ended && !session->census_ended) {
        census_->emit(KaihenCensusSource_Sm, KaihenCensusEvent_SmEnded,
            session->census_id, (u64(view.end_reason) << 32) | view.result, view.name, view.close_result,
            KaihenCensusFlag_Cached | KaihenCensusFlag_ResultKnown | KaihenCensusFlag_After);
        session->census_ended = true;
    }
    session->census_revision = view.revision;
}

void SmProxy::sampleCensus() {
    if (!census_) return;
    // Existing session registry is owner-thread-only. Workers only publish
    // atomic mailboxes. At most 16 contexts are inspected per idle sample.
    if (!census_cursor_) census_cursor_ = sessions_;
    for (unsigned budget = 0; census_cursor_ && budget < 16; budget++) {
        Session* current = census_cursor_;
        census_cursor_ = current->next;
        sampleSessionCensus(*current);
    }
    const bool incomplete = census_cursor_ != nullptr;
    const u64 running = census_current_[0], ended = census_current_[1];
    const u64 retained = (census_current_[2] << 32) | census_current_[3];
    if (running != census_counts_[0] || ended != census_counts_[1] || retained != census_counts_[2] ||
        (census_incomplete_ && !incomplete)) {
        census_->emit(KaihenCensusSource_Sm, KaihenCensusEvent_SmCounts, running, ended, retained,
            0, KaihenCensusFlag_Cached | (incomplete ? KaihenCensusFlag_Incomplete : 0));
        census_counts_[0] = running; census_counts_[1] = ended; census_counts_[2] = retained;
        census_incomplete_ = incomplete;
    }
}

Result SmProxy::lookUpService(const SmServiceName& name, Handle* out_handle) {
    Service master;
    serviceCreate(&master, master_);
    master.own_handle = false;
    return serviceDispatchIn(&master, static_cast<u32>(Command::GetServiceHandle), name,
        .out_handle_attrs = { SfOutHandleAttr_HipcMove },
        .out_handles = out_handle,
    );
}
