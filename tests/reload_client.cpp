#include "reload_client.hpp"
#include <cassert>
#include <iostream>
using moader_native::ReloadClient;
struct FakeHen {
    HenPluginInfo info{77,101,3,0,HEN_PLUGIN_RELOAD_AVAILABLE,HenPluginReloadPhase_Idle};
    HenPluginReloadStatus status{77,10,101,0,3,HenPluginReloadPhase_Preparing,HEN_PLUGIN_RELOAD_BUSY,0,0};
    unsigned submissions=0,queries=0;uint32_t info_result=0,submit_result=0,status_result=0;
    HenPluginReloadRequest sent{};
    uint32_t get_info(HenPluginInfo& out){++queries;out=info;return info_result;}
    uint32_t submit(const HenPluginReloadRequest& request,HenPluginReloadStatus& out){++submissions;sent=request;out=status;info.operation_id=status.operation_id;return submit_result;}
    uint32_t get_status(uint64_t id,HenPluginReloadStatus& out){assert(id==status.operation_id);++queries;out=status;return status_result;}
};
ReloadClient client(){ReloadClient c;c.supported=true;return c;}
int main(){
    {FakeHen h;auto c=client();assert(c.begin(h,500));assert(h.submissions==1&&h.sent.nonce==500);
        assert(h.sent.instance_id==77&&h.sent.expected_process_id==101&&h.sent.expected_generation==3);
        assert(!c.begin(h,501)&&h.submissions==1);c.poll(h,500);assert(!c.ready&&c.active);
        // IPC publication before runtime health is insufficient.
        h.info.process_id=102;h.info.generation=4;h.info.flags=HEN_PLUGIN_RELOAD_AVAILABLE;h.info.phase=HenPluginReloadPhase_Health;
        h.status.new_process_id=102;h.status.generation=4;h.status.phase=HenPluginReloadPhase_Health;
        c.poll(h,750);assert(!c.ready);
        h.status.phase=HenPluginReloadPhase_Complete;h.info.phase=HenPluginReloadPhase_Complete;
        c.poll(h,1000);assert(c.ready&&c.active);c.connected();assert(!c.active);
        h.status={77,11,102,0,4,HenPluginReloadPhase_Preparing,HEN_PLUGIN_RELOAD_BUSY,0,0};
        assert(c.begin(h,500));assert(h.sent.nonce==501&&h.submissions==2);
    }
    {FakeHen h;auto c=client();h.submit_result=0xF601;assert(c.begin(h,100));assert(c.active&&c.uncertain);
        h.info.flags=HEN_PLUGIN_RELOAD_BUSY;h.info.phase=HenPluginReloadPhase_Draining;
        h.status.phase=HenPluginReloadPhase_Draining;c.poll(h,100);assert(c.status.operation_id==10&&!c.uncertain);
        assert(!c.begin(h,200)&&h.submissions==1);
        h.status_result=0xF601;c.poll(h,45100);assert(c.timed_out&&c.active);
        auto queries=h.queries;c.poll(h,60000);assert(h.queries==queries);
        c.resume(60001);c.poll(h,60001);assert(h.queries>queries&&h.submissions==1);
    }
    {FakeHen h;auto c=client();h.submit_result=0xF601;h.status_result=0xF601;assert(c.begin(h,100));
        // A missing enumeration or blank info never grants foreground ownership.
        h.info={77,0,3,10,HEN_PLUGIN_RELOAD_BLOCKED,HenPluginReloadPhase_Failed};
        assert(!ReloadClient::unmanaged(h.info)&&!ReloadClient::attachable(h.info));
        c.poll(h,45100);assert(c.active&&c.timed_out&&!c.can_submit()&&h.submissions==1);
    }
    {FakeHen h;auto c=client();c.manager_flags=HEN_PLUGIN_RELOAD_BUSY;assert(!c.begin(h,99)&&h.submissions==0);c.manager_flags=0;h.info.flags|=HEN_PLUGIN_RELOAD_BUSY;assert(!c.begin(h,100)&&h.submissions==0);
        h.info.flags=HEN_PLUGIN_RELOAD_BLOCKED;assert(!c.begin(h,101)&&h.submissions==0);
        h.info={77,0,3,5,0,HenPluginReloadPhase_Failed};assert(!ReloadClient::unmanaged(h.info));
        h.info={77,0,0,0,0,HenPluginReloadPhase_Idle};assert(ReloadClient::unmanaged(h.info));
        h.info={77,101,1,0,0,HenPluginReloadPhase_Idle};assert(ReloadClient::attachable(h.info)); // Existing service survives disabled reload capability.
    }
    {FakeHen h;ReloadClient c;assert(!c.begin(h,100)&&h.submissions==0&&h.queries==0);c.unavailable(false);
        assert(c.detail.find("new kaiHEN")!=std::string::npos);c.unavailable(true);assert(c.detail.find("foreground")!=std::string::npos);
    }
    {FakeHen h;auto c=client();assert(c.begin(h,100));
        // A non-destructive preflight failure leaves the same service usable.
        h.status.phase=HenPluginReloadPhase_Failed;h.status.flags=0;h.status.result=0x123;
        h.info.phase=HenPluginReloadPhase_Failed;h.info.flags=HEN_PLUGIN_RELOAD_AVAILABLE;
        c.poll(h,100);assert(!c.active&&!c.ready&&ReloadClient::attachable(h.info)&&c.can_submit());
        h.info.flags|=HEN_PLUGIN_RELOAD_OLD_EXITED;assert(!ReloadClient::attachable(h.info));
    }
    {FakeHen h;auto c=client();assert(c.begin(h,100));h.status.phase=HenPluginReloadPhase_Failed;h.status.result=0x123;h.status.restoration_result=0x456;
        h.info.flags=HEN_PLUGIN_RELOAD_BLOCKED;h.info.phase=HenPluginReloadPhase_Failed;c.poll(h,100);
        assert(!c.active&&!c.ready&&c.detail.find("00000123")!=std::string::npos&&c.detail.find("00000456")!=std::string::npos);
    }
    {FakeHen h;auto c=client();assert(c.begin(h,100));h.info.instance_id=78;c.poll(h,100);assert(c.active&&c.timed_out&&!c.ready&&h.submissions==1);}
    std::cout<<"Native reload client: single submission, identity guard, health gate, lost reply, timeout, busy, legacy, foreground and result cases passed.\n";
}
