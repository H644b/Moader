#pragma once
#include "update.hpp"
#include "../source/web_worker.hpp"
#include <exception>
#include <stdexcept>

namespace moader_native {
class UpdateClient {
    using Phase = moader_ui::UpdatePhase;
    using Status = moader_ui::UpdateStatus;
    enum class Job { Check, Download, Recover };
    moader_ui::UpdateFiles files;
    moader::WebWorker worker;
    moader_ui::AppRelease release{};
    std::atomic<bool> done{false}, cancel{false}, installing{false}, journal{false};
    std::atomic<uint64_t> received{0};
    Job job = Job::Check;
    std::string failure, prompted;
    bool manual = false, offer = false, awaiting_restart = false;
    uint64_t next_check = 0;
    static constexpr uint64_t Hour = 3600000;

    static void run(void* context) {
        auto& self = *static_cast<UpdateClient*>(context);
        try {
            if (self.job == Job::Check) {
                self.release = moader_ui::parse_app_release(moader_ui::latest_app_metadata(self.cancel));
            } else {
                if (self.job == Job::Download) {
                    self.files.download(self.release, self.cancel, self.received);
                    if (self.cancel) throw std::runtime_error("Download cancelled.");
                    self.installing = true;
                    self.files.prepare(self.release);
                    self.journal = true;
                } else self.files.repair(self.release, self.cancel, self.received);
                self.files.install(self.release);
            }
        } catch (const std::exception& e) {
            self.failure = e.what();
            if (self.job == Job::Download && !self.journal) {
                try { self.files.discard_uncommitted(); }
                catch (const std::exception& cleanup) { self.failure += std::string(" ") + cleanup.what(); }
            }
        }
        self.done.store(true, std::memory_order_release);
    }
    void start(Job task, Status& s) {
        if (worker.joinable()) return;
        job = task;
        cancel = done = false;
        installing = task == Job::Recover;
        received = 0;
        failure.clear();
        s.detail.clear();
        s.received = 0;
        s.phase = task == Job::Check ? Phase::Checking : task == Job::Download ? Phase::Downloading : Phase::Installing;
        if (!worker.start(run, this, failure)) {
            done = true;
            s.phase = journal ? Phase::Recovery : Phase::Failed;
            s.detail = failure;
        }
    }
public:
    UpdateClient(const std::string& sd, const std::string& path) : files(sd, path) {}
    ~UpdateClient() { cancel = true; std::string ignored; worker.join(ignored); }
    void open(Status& s, const std::string& installed) {
        s.running_version = installed;
        try {
            s.automatic = files.automatic();
            if (files.recover(release)) {
                if (moader_ui::compare_release_version(release.version, installed) < 0) {
                    files.retire_superseded();
                    return;
                }
                journal = true;
                s.phase = Phase::Recovery;
                s.latest = release.version;
                s.total = release.app.bytes + release.plugin.bytes;
                s.detail = "A previous update was interrupted. Choose Finish update to resume.";
            }
        } catch (const std::exception& e) { s.phase = Phase::Failed; s.detail = e.what(); }
    }
    void toggle(Status& s) {
        try {
            files.set_automatic(!s.automatic);
            s.automatic = !s.automatic;
            if (s.automatic) next_check = 0;
            else if (!manual) offer = false;
        } catch (const std::exception& e) { s.detail = e.what(); }
    }
    void check(Status& s, bool requested) {
        if (journal || worker.joinable() || s.phase == Phase::Reopen) return;
        manual = requested;
        offer = false;
        start(Job::Check, s);
    }
    void install(Status& s, bool can_restart) {
        s.prompt = false;
        offer = false;
        if (worker.joinable() || (s.phase != Phase::Available && s.phase != Phase::Recovery &&
                                  s.phase != Phase::RestartNeeded)) return;
        if (!can_restart) {
            s.detail = "Connect the resident plugin with reload support, then try again.";
            return;
        }
        start(journal ? Job::Recover : Job::Download, s);
    }
    // Returns true once per completed installation. The app submits the existing managed restart.
    bool poll(Status& s, uint64_t now, bool may_prompt) {
        if (worker.joinable()) {
            if (job != Job::Check) {
                s.received = received.load(std::memory_order_relaxed);
                if (installing.load(std::memory_order_relaxed)) s.phase = Phase::Installing;
            }
            if (!done.load(std::memory_order_acquire)) return false;
            std::string join_error;
            if (!worker.join(join_error)) { s.detail = join_error; return false; }
            if (!failure.empty()) {
                s.phase = journal ? Phase::Recovery : Phase::Failed;
                s.detail = failure;
                next_check = now + Hour;
            } else if (job == Job::Check) {
                next_check = now + 6 * Hour;
                s.latest = release.version;
                s.total = release.app.bytes + release.plugin.bytes;
                try {
                    s.phase = moader_ui::compare_release_version(s.latest, s.running_version) > 0 ? Phase::Available : Phase::Current;
                    offer = s.phase == Phase::Available && (manual || (s.automatic && prompted != s.latest));
                } catch (const std::exception& e) { s.phase = Phase::Failed; s.detail = e.what(); }
            } else {
                awaiting_restart = true;
                s.phase = Phase::Restarting;
                return true;
            }
        }
        if (offer && may_prompt) {
            offer = false;
            prompted = s.latest;
            s.prompt = true;
            s.later = false;
        }
        if (s.automatic && !journal && !s.prompt && now >= next_check && s.phase != Phase::Reopen) {
            next_check = now + Hour;
            check(s, false);
        }
        return false;
    }
    void restarted(Status& s, bool healthy, const std::string& reason) {
        if (!awaiting_restart) return;
        awaiting_restart = false;
        if (!healthy) {
            s.phase = Phase::RestartNeeded;
            s.detail = reason;
            return;
        }
        s.phase = Phase::Reopen;
        s.detail.clear();
        try { files.finish(); journal = false; }
        catch (const std::exception& e) { s.detail = e.what(); }
    }
};
} // namespace moader_native
