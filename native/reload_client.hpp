#pragma once
#include <kaihen/reload.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace moader_native {
// One destructive submission. Every recovery path below is read-only.
class ReloadClient {
public:
    HenPluginInfo info{};
    HenPluginReloadRequest request{};
    HenPluginReloadStatus status{};
    bool supported=false, info_valid=false, active=false, uncertain=false, timed_out=false, ready=false;
    uint32_t manager_flags=0, transport_result=0;
    std::string detail="Install the new kaiHEN build to reload the resident service.";
    static constexpr uint64_t PollMs=250, TimeoutMs=45000;

    static bool pending_phase(uint32_t phase) {
        return phase>=HenPluginReloadPhase_Preparing&&phase<=HenPluginReloadPhase_Health;
    }
    static bool attachable(const HenPluginInfo& i) {
        return i.process_id && !(i.flags&(HEN_PLUGIN_RELOAD_BUSY|HEN_PLUGIN_RELOAD_BLOCKED)) &&
            (i.phase==HenPluginReloadPhase_Idle || i.phase==HenPluginReloadPhase_Complete ||
             (i.phase==HenPluginReloadPhase_Failed && !(i.flags&HEN_PLUGIN_RELOAD_OLD_EXITED)));
    }
    static bool unmanaged(const HenPluginInfo& i) {
        return !i.process_id && !i.operation_id && !i.flags && i.phase==HenPluginReloadPhase_Idle;
    }
    bool in_progress() const {
        return active || (manager_flags&HEN_PLUGIN_RELOAD_BUSY) || (info.flags&HEN_PLUGIN_RELOAD_BUSY) ||
            (status.flags&HEN_PLUGIN_RELOAD_BUSY) || pending_phase(info.phase) ||
            (status.operation_id&&pending_phase(status.phase));
    }
    bool shutdown_required() const {
        return sticky_shutdown_ || fatal_status(status) || info.phase==HenPluginReloadPhase_Quarantined ||
            (info.flags&HEN_PLUGIN_RELOAD_BLOCKED) || (manager_flags&HEN_PLUGIN_RELOAD_BLOCKED);
    }
    bool exit_state_unknown() const {
        return uncertain || manager_unknown_ || info_unknown_ || status_unknown_;
    }
    bool can_exit() const { return !in_progress()&&!shutdown_required()&&!exit_state_unknown(); }
    bool can_submit() const {
        return supported&&info_valid&&can_exit()&&(info.flags&HEN_PLUGIN_RELOAD_AVAILABLE)&&attachable(info);
    }
    const char* phase_name() const {
        switch(status.phase) {
            case HenPluginReloadPhase_Preparing:return "Checking service";
            case HenPluginReloadPhase_Draining:return "Draining service";
            case HenPluginReloadPhase_Exiting:return "Stopping old process";
            case HenPluginReloadPhase_Launching:return "Starting new process";
            case HenPluginReloadPhase_Handoff:return "Publishing service";
            case HenPluginReloadPhase_Health:return "Checking runtime health";
            case HenPluginReloadPhase_Complete:return "Reload complete";
            case HenPluginReloadPhase_Failed:return "Reload failed";
            case HenPluginReloadPhase_Quarantined:return "Reload blocked";
            default:return "Waiting for kaiHEN";
        }
    }
    void observe(const HenPluginInfo& value) {
        info=value;info_valid=true;info_unknown_=false;presence_enabled_=true;
        sticky_shutdown_ |= value.phase==HenPluginReloadPhase_Quarantined || bool(value.flags&HEN_PLUGIN_RELOAD_BLOCKED);
    }
    // A failed query preserves the last authoritative flags. It must never
    // manufacture BLOCKED or erase a previously confirmed hazard.
    void observe_manager(uint32_t flags,bool valid) {
        presence_enabled_=true;manager_unknown_=!valid;
        if(valid){manager_flags=flags;manager_known_=true;sticky_shutdown_ |= bool(flags&HEN_PLUGIN_RELOAD_BLOCKED);}
    }
    void observe_status(const HenPluginReloadStatus& value) {
        sticky_shutdown_ |= fatal_status(value);
        // Keep the confirmed failure's precise results if another HEN instance
        // or unrelated operation is observed later. A restart is not recovery.
        if(fatal_status(status)&&(status.instance_id!=value.instance_id||status.operation_id!=value.operation_id))return;
        status=value;
    }
    void unavailable(bool foreground) {
        info_valid=false;supported=false;
        detail=foreground?"Reload is unavailable in foreground mode. Install and start the resident service.":
            "Install the new kaiHEN build to reload the resident service.";
    }
    template<class Transport> bool begin(Transport& transport,uint64_t now) {
        if(active||!supported)return false;
        HenPluginInfo current{};const auto rc=transport.get_info(current);
        if(rc){transport_result=rc;info_valid=false;info_unknown_=true;presence_enabled_=true;
            detail=transport_text("Could not inspect resident service",rc);return false;}
        observe(current);
        if(!can_submit()){detail="Reload unavailable: kaiHEN reports this service busy, blocked or not published.";return false;}
        request={};std::memcpy(request.name,"moader",7);
        request.instance_id=info.instance_id;request.expected_process_id=info.process_id;
        request.expected_generation=info.generation;
        nonce_=now>nonce_?now:nonce_+1;if(!nonce_)nonce_=1;request.nonce=nonce_;
        baseline_operation_=info.operation_id;status={};active=true;uncertain=false;timed_out=false;ready=false;status_unknown_=false;
        deadline_=now+TimeoutMs;next_poll_=now;transport_result=0;
        HenPluginReloadStatus reply{};const auto submitted=transport.submit(request,reply);
        if(submitted){transport_result=submitted;uncertain=true;detail=transport_text("Reload reply unavailable; checking kaiHEN",submitted);}
        else if(matches(reply))accept(reply);
        else {uncertain=true;detail="Reload reply could not be verified. Checking kaiHEN; the request will not be repeated.";}
        return true;
    }
    // Reopen observation, never resubmit. Also works for cold/external state.
    void resume(uint64_t now) {
        timed_out=false;
        if(active){deadline_=now+TimeoutMs;next_poll_=now;}
        presence_waiting_=false;presence_next_=now;
    }
    template<class Transport> void poll(Transport& transport,uint64_t now) {
        if(!active||timed_out||now<next_poll_)return;
        next_poll_=now+PollMs;ready=false;
        HenPluginInfo current{};const auto info_rc=transport.get_info(current);
        if(!info_rc){observe(current);
            if(current.instance_id!=request.instance_id){
                info_unknown_=true;timed_out=true;detail="kaiHEN restarted. Reload outcome is unknown; query again after checking the resident service.";return;
            }
            // HEN assigns the sticky registry operation BEFORE starting native
            // work. A clear same-instance snapshot proves pre-reservation denial.
            if(uncertain&&current.operation_id==baseline_operation_&&current.process_id==request.expected_process_id&&
               current.generation==request.expected_generation&&attachable(current)&&manager_clear()){
                active=false;uncertain=false;ready=false;status_unknown_=false;
                detail=transport_text("Reload was not accepted; the current service is still available",transport_result);return;
            }
        }else {transport_result=info_rc;info_valid=false;info_unknown_=true;}
        uint64_t operation=status.operation_id;
        if(!operation&&!info_rc&&current.operation_id!=baseline_operation_)operation=current.operation_id;
        bool verified=false;
        if(operation){HenPluginReloadStatus reply{};const auto rc=transport.get_status(operation,reply);
            if(!rc&&matches(reply)){accept(reply);verified=true;status_unknown_=false;}
            else {status_unknown_=true;if(rc)transport_result=rc;}
        }
        if(!active)return;
        if(verified&&status.phase==HenPluginReloadPhase_Complete&&!info_rc&&manager_clear()&&attachable(current)&&
           current.operation_id==status.operation_id&&current.process_id==status.new_process_id&&current.process_id!=request.expected_process_id&&
           current.generation==status.generation&&current.generation>request.expected_generation){
            ready=true;detail="Runtime health confirmed. Reconnecting to the new Moader process.";
        }
        if(now>=deadline_){ready=false;timed_out=true;detail="Reload outcome is not yet verified. Press Y to query kaiHEN again; the request will not be repeated.";}
    }
    // Every voluntary exit attempt refreshes state even between scheduled
    // presence polls. This also enables observation before normal app setup.
    template<class Transport> bool inspect_exit(Transport& transport,uint64_t now) {
        presence_enabled_=true;resume(now);
        if(active)poll(transport,now);else poll_presence(transport,now);
        return can_exit();
    }
    // Query-only monitoring for a cold-open/external operation or failed query.
    // The caller may run this every frame even while no own request is active.
    // Observation stops after 45 seconds of unresolved state until resume().
    template<class Transport> bool poll_presence(Transport& transport,uint64_t now) {
        if(active||!presence_enabled_||timed_out||now<presence_next_)return false;
        presence_next_=now+PollMs;
        HenPluginInfo current{};const auto rc=transport.get_info(current);
        if(rc){info_valid=false;info_unknown_=true;transport_result=rc;
            detail=transport_text("Could not verify current loader state; press Y to query again",rc);
        }else {
            observe(current);
            const bool safe=manager_clear()&&(attachable(current)||unmanaged(current));
            bool verified=false,identity_mismatch=false;
            if(current.operation_id){HenPluginReloadStatus reply{};const auto query=transport.get_status(current.operation_id,reply);
                if(!query&&reply.instance_id==current.instance_id&&reply.operation_id==current.operation_id){
                    observe_status(reply);status_unknown_=false;verified=true;
                    detail=(reply.phase==HenPluginReloadPhase_Failed||reply.phase==HenPluginReloadPhase_Quarantined)?
                        result_text(phase_name(),reply.result,reply.restoration_result):phase_name();
                }else {status_unknown_=true;identity_mismatch=!query;if(query)transport_result=query;}
            }
            // Authoritative safe publication plus a clear manager resolves stale
            // progress/unknown observations even if an old ticket expired. A
            // confirmed fatal record remains pinned for this app lifetime.
            const bool known_fatal=fatal_status(status);
            if(safe&&!known_fatal&&!identity_mismatch&&(!verified||!current.operation_id)){
                status={};status_unknown_=false;
            }
            if(!current.operation_id&&!safe&&(!unmanaged(current)||!manager_clear()))status_unknown_=true;
            if(!verified&&known_fatal)status_unknown_=true;
        }
        const bool unresolved=in_progress()||shutdown_required()||exit_state_unknown();
        if(unresolved){
            if(!presence_waiting_){presence_waiting_=true;presence_deadline_=now+TimeoutMs;}
            if(now>=presence_deadline_){timed_out=true;detail="Loader state remains unverified. Save Debug; press Y to query the existing state again.";}
        }else {presence_waiting_=false;timed_out=false;presence_next_=now+1000;}
        return true;
    }
    void connected() {
        active=false;ready=false;uncertain=false;timed_out=false;
        detail="Moader reloaded. The new process passed its runtime health check.";
    }
    static std::string transport_text(const char* title,uint32_t result) {
        char text[192];std::snprintf(text,sizeof(text),"%s. IPC result 0x%08X.",title,result);return text;
    }
    static std::string result_text(const char* title,uint32_t primary,uint32_t restoration) {
        char text[192];std::snprintf(text,sizeof(text),"%s. Result 0x%08X; restoration 0x%08X.",title,primary,restoration);return text;
    }
private:
    uint64_t nonce_=0,baseline_operation_=0,deadline_=0,next_poll_=0,presence_next_=0,presence_deadline_=0;
    bool manager_known_=false,manager_unknown_=false,info_unknown_=false,status_unknown_=false;
    bool presence_enabled_=false,presence_waiting_=false,sticky_shutdown_=false;
    bool manager_clear() const {return manager_known_&&!manager_unknown_&&!(manager_flags&(HEN_PLUGIN_RELOAD_BUSY|HEN_PLUGIN_RELOAD_BLOCKED));}
    static bool fatal_status(const HenPluginReloadStatus& value) {
        return value.restoration_result||value.phase==HenPluginReloadPhase_Quarantined||(value.flags&HEN_PLUGIN_RELOAD_BLOCKED);
    }
    bool matches(const HenPluginReloadStatus& value) const {
        return value.instance_id==request.instance_id&&value.operation_id&&value.operation_id!=baseline_operation_&&value.old_process_id==request.expected_process_id;
    }
    void accept(const HenPluginReloadStatus& value) {
        observe_status(value);uncertain=false;
        if(value.phase==HenPluginReloadPhase_Failed||value.phase==HenPluginReloadPhase_Quarantined){
            // Failed is observable before the worker's cleanup/join completes.
            // BUSY is authoritative and cannot be treated as terminal success.
            active=(value.phase==HenPluginReloadPhase_Failed)&&(value.flags&HEN_PLUGIN_RELOAD_BUSY);
            ready=false;detail=result_text(phase_name(),value.result,value.restoration_result);
        }else detail=phase_name();
    }
};
}
