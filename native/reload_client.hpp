#pragma once
#include <kaihen/reload.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace moader_native {
// The controller owns one submission. Transport errors never turn into a second
// destructive call: recovery reads HEN's registry and operation record only.
class ReloadClient {
public:
    HenPluginInfo info{};
    HenPluginReloadRequest request{};
    HenPluginReloadStatus status{};
    bool supported=false, info_valid=false, active=false, uncertain=false, timed_out=false, ready=false;
    uint32_t manager_flags=0;
    uint32_t transport_result=0;
    std::string detail="Install the new kaiHEN build to reload the resident service.";
    static constexpr uint64_t PollMs=250, TimeoutMs=45000;

    static bool attachable(const HenPluginInfo& i) {
        return i.process_id &&
            !(i.flags&(HEN_PLUGIN_RELOAD_BUSY|HEN_PLUGIN_RELOAD_BLOCKED)) &&
            (i.phase==HenPluginReloadPhase_Idle || i.phase==HenPluginReloadPhase_Complete ||
             (i.phase==HenPluginReloadPhase_Failed && !(i.flags&HEN_PLUGIN_RELOAD_OLD_EXITED)));
    }
    static bool unmanaged(const HenPluginInfo& i) {
        return !i.process_id && !i.operation_id && !i.flags && i.phase==HenPluginReloadPhase_Idle;
    }
    bool can_submit() const { return supported && info_valid && !active && !(manager_flags&(HEN_PLUGIN_RELOAD_BUSY|HEN_PLUGIN_RELOAD_BLOCKED)) &&
            (info.flags&HEN_PLUGIN_RELOAD_AVAILABLE) && attachable(info); }
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
    void observe(const HenPluginInfo& value) { info=value;info_valid=true; }
    void unavailable(bool foreground) {
        info_valid=false;supported=false;
        detail=foreground?"Reload is unavailable in foreground mode. Install and start the resident service.":
            "Install the new kaiHEN build to reload the resident service.";
    }
    template<class Transport> bool begin(Transport& transport,uint64_t now) {
        if(active||!supported)return false;
        HenPluginInfo current{};const auto rc=transport.get_info(current);
        if(rc){transport_result=rc;info_valid=false;detail=transport_text("Could not inspect resident service",rc);return false;}
        observe(current);
        if(!can_submit()){detail="Reload unavailable: kaiHEN reports this service busy, blocked or not published.";return false;}
        request={};std::memcpy(request.name,"moader",7);
        request.instance_id=info.instance_id;request.expected_process_id=info.process_id;
        request.expected_generation=info.generation;
        // System monotonic milliseconds seed the session; subsequent attempts
        // strictly increase even if multiple actions occur within one tick.
        nonce_=now>nonce_?now:nonce_+1;if(!nonce_)nonce_=1;request.nonce=nonce_;
        baseline_operation_=info.operation_id;status={};active=true;uncertain=false;timed_out=false;ready=false;
        deadline_=now+TimeoutMs;next_poll_=now;transport_result=0;
        HenPluginReloadStatus reply{};const auto submitted=transport.submit(request,reply);
        if(submitted){transport_result=submitted;uncertain=true;detail=transport_text("Reload reply unavailable; checking kaiHEN",submitted);}
        else if(matches(reply))accept(reply);
        else {uncertain=true;detail="Reload reply could not be verified. Checking kaiHEN; the request will not be repeated.";}
        return true;
    }
    void resume(uint64_t now) {if(active){timed_out=false;deadline_=now+TimeoutMs;next_poll_=now;}}
    template<class Transport> void poll(Transport& transport,uint64_t now) {
        if(!active||timed_out||now<next_poll_)return;
        next_poll_=now+PollMs;
        HenPluginInfo current{};const auto info_rc=transport.get_info(current);
        if(!info_rc){observe(current);
            if(current.instance_id!=request.instance_id){
                timed_out=true;detail="kaiHEN restarted. Reload outcome is unknown; restart this app after checking the resident service.";return;
            }
        }
        uint64_t operation=status.operation_id;
        if(!operation&&!info_rc&&current.operation_id!=baseline_operation_)operation=current.operation_id;
        if(operation){HenPluginReloadStatus reply{};const auto rc=transport.get_status(operation,reply);
            if(!rc&&matches(reply))accept(reply);else if(rc)transport_result=rc;
        }else if(info_rc)transport_result=info_rc;
        if(!active)return;
        if(status.phase==HenPluginReloadPhase_Complete&&!info_rc&&attachable(current)&&
           current.operation_id==status.operation_id&&current.process_id==status.new_process_id&&current.process_id!=request.expected_process_id&&
           current.generation==status.generation&&current.generation>request.expected_generation){
            ready=true;detail="Runtime health confirmed. Reconnecting to the new Moader process.";
        }
        if(now>=deadline_){ready=false;timed_out=true;detail="Reload outcome is not yet verified. Press Y to query kaiHEN again; the request will not be repeated.";}
    }
    void connected() {active=false;ready=false;uncertain=false;timed_out=false;detail="Moader reloaded. The new process passed its runtime health check.";}
    static std::string transport_text(const char* title,uint32_t result) {
        char text[192];std::snprintf(text,sizeof(text),"%s. IPC result 0x%08X.",title,result);return text;
    }
    static std::string result_text(const char* title,uint32_t primary,uint32_t restoration) {
        char text[192];std::snprintf(text,sizeof(text),"%s. Result 0x%08X; restoration 0x%08X.",title,primary,restoration);return text;
    }
private:
    uint64_t nonce_=0, baseline_operation_=0, deadline_=0, next_poll_=0;
    bool matches(const HenPluginReloadStatus& value) const {
        return value.instance_id==request.instance_id&&value.operation_id&&
            value.operation_id!=baseline_operation_&&value.old_process_id==request.expected_process_id;
    }
    void accept(const HenPluginReloadStatus& value) {
        status=value;uncertain=false;
        if(value.phase==HenPluginReloadPhase_Failed||value.phase==HenPluginReloadPhase_Quarantined){
            active=false;ready=false;detail=result_text(phase_name(),value.result,value.restoration_result);
        }else detail=phase_name();
    }
};
}
