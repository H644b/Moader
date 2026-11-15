#include "reload_client.hpp"
#include <cassert>
#include <iostream>
using moader_native::ReloadClient;
struct FakeHen {
    HenPluginInfo info{77,101,3,0,HEN_PLUGIN_RELOAD_AVAILABLE,HenPluginReloadPhase_Idle};
    HenPluginReloadStatus status{77,10,101,0,3,HenPluginReloadPhase_Preparing,HEN_PLUGIN_RELOAD_BUSY,0,0};
    unsigned submissions=0,queries=0;uint32_t info_result=0,submit_result=0,status_result=0;
    bool reserve=true,caps_valid=true;uint32_t manager_flags=HEN_PLUGIN_RELOAD_AVAILABLE;ReloadClient* observer=nullptr;
    HenPluginReloadRequest sent{};
    uint32_t get_info(HenPluginInfo& out){++queries;out=info;if(!info_result&&observer)observer->observe_manager(manager_flags,caps_valid);return info_result;}
    uint32_t submit(const HenPluginReloadRequest& request,HenPluginReloadStatus& out){++submissions;sent=request;out=status;if(reserve){info.operation_id=status.operation_id;info.phase=status.phase;info.flags=HEN_PLUGIN_RELOAD_AVAILABLE|status.flags;manager_flags|=HEN_PLUGIN_RELOAD_BUSY;}return submit_result;}
    uint32_t get_status(uint64_t id,HenPluginReloadStatus& out){assert(id==status.operation_id);++queries;out=status;return status_result;}
};
ReloadClient client(){ReloadClient c;c.supported=true;c.observe_manager(HEN_PLUGIN_RELOAD_AVAILABLE,true);return c;}
int main(){
    {FakeHen h;auto c=client();assert(c.begin(h,500));assert(h.submissions==1&&h.sent.nonce==500);
        assert(h.sent.instance_id==77&&h.sent.expected_process_id==101&&h.sent.expected_generation==3);
        assert(!c.begin(h,501)&&h.submissions==1);c.poll(h,500);assert(!c.ready&&c.active);
        // IPC publication before runtime health is insufficient.
        h.info.process_id=102;h.info.generation=4;h.info.flags=HEN_PLUGIN_RELOAD_AVAILABLE;h.info.phase=HenPluginReloadPhase_Health;
        h.status.new_process_id=102;h.status.generation=4;h.status.phase=HenPluginReloadPhase_Health;
        c.poll(h,750);assert(!c.ready);
        h.status.phase=HenPluginReloadPhase_Complete;h.status.flags=HEN_PLUGIN_RELOAD_OLD_EXITED;h.info.phase=HenPluginReloadPhase_Complete;
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
    {FakeHen h;auto c=client();assert(c.can_exit());assert(c.begin(h,100)&&!c.can_exit());
        h.status.phase=HenPluginReloadPhase_Quarantined;h.status.flags=HEN_PLUGIN_RELOAD_BLOCKED|HEN_PLUGIN_RELOAD_OLD_EXITED;
        h.status.result=0xea01;h.status.restoration_result=0x1759;h.info.flags=h.status.flags;h.info.phase=h.status.phase;
        c.poll(h,100);assert(!c.active&&c.shutdown_required()&&!c.can_exit()&&!c.can_submit());
    }
    {FakeHen h;auto c=client();h.reserve=false;h.submit_result=0x123;assert(c.begin(h,100));
        assert(h.info.operation_id==0&&h.info.phase==HenPluginReloadPhase_Idle);
        c.poll(h,100);assert(!c.active&&c.can_exit()&&!c.shutdown_required()&&h.submissions==1);
    }
    {FakeHen h;auto c=client();assert(c.begin(h,100));h.status.phase=HenPluginReloadPhase_Failed;h.status.flags=0;h.status.result=0x123;h.status.restoration_result=0x456;
        h.info.flags=HEN_PLUGIN_RELOAD_BLOCKED;h.info.phase=HenPluginReloadPhase_Failed;c.poll(h,100);
        assert(!c.active&&!c.ready&&c.detail.find("00000123")!=std::string::npos&&c.detail.find("00000456")!=std::string::npos);
    }
    {FakeHen h;auto c=client();assert(c.begin(h,100));h.info.instance_id=78;c.poll(h,100);assert(c.active&&c.timed_out&&!c.ready&&h.submissions==1);}
    {FakeHen h;auto c=client();h.observer=&c;
        h.info={77,0,3,10,HEN_PLUGIN_RELOAD_BLOCKED|HEN_PLUGIN_RELOAD_OLD_EXITED,HenPluginReloadPhase_Quarantined};
        h.manager_flags=HEN_PLUGIN_RELOAD_AVAILABLE|HEN_PLUGIN_RELOAD_BLOCKED;h.status_result=0x456;
        c.observe(h.info);c.observe_manager(h.manager_flags,true);
        assert(!c.can_exit()&&c.shutdown_required()&&!c.active);
        c.poll_presence(h,100);assert(!c.can_exit()&&c.shutdown_required()&&c.exit_state_unknown()&&h.submissions==0);
        h.info_result=0x999;c.poll_presence(h,350);assert(c.shutdown_required()&&!c.can_exit());
        // Even a fresh HEN with safe publication cannot prove old Loader recovery.
        h.info={78,103,1,0,HEN_PLUGIN_RELOAD_AVAILABLE,HenPluginReloadPhase_Idle};
        h.info_result=0;h.manager_flags=HEN_PLUGIN_RELOAD_AVAILABLE;c.poll_presence(h,600);
        assert(!c.can_exit()&&c.shutdown_required()&&h.submissions==0);
    }
    {FakeHen h;auto c=client();h.observer=&c;
        h.info.operation_id=10;h.info.flags|=HEN_PLUGIN_RELOAD_BUSY;h.info.phase=HenPluginReloadPhase_Launching;
        h.status.phase=HenPluginReloadPhase_Launching;h.manager_flags|=HEN_PLUGIN_RELOAD_BUSY;
        c.observe(h.info);c.observe_manager(h.manager_flags,true);
        c.poll_presence(h,100);assert(c.in_progress()&&!c.can_exit()&&!c.active&&h.submissions==0&&!c.request.nonce);
        h.info={77,102,4,10,HEN_PLUGIN_RELOAD_AVAILABLE,HenPluginReloadPhase_Complete};
        h.status={77,10,101,102,4,HenPluginReloadPhase_Complete,HEN_PLUGIN_RELOAD_OLD_EXITED,0,0};
        h.manager_flags=HEN_PLUGIN_RELOAD_AVAILABLE;c.poll_presence(h,350);
        assert(!c.in_progress()&&c.can_exit()&&!c.ready&&h.submissions==0);
    }
    {FakeHen h;auto c=client();h.observer=&c;assert(c.begin(h,100));
        h.info_result=0x456;h.status.phase=HenPluginReloadPhase_Failed;h.status.flags=HEN_PLUGIN_RELOAD_BUSY;h.status.result=0x123;
        c.poll(h,100);assert(c.active&&c.in_progress()&&!c.can_exit()&&c.exit_state_unknown()&&!c.shutdown_required());
        h.info_result=0;h.info.flags=HEN_PLUGIN_RELOAD_AVAILABLE;h.info.phase=HenPluginReloadPhase_Failed;
        h.status.flags=0;h.manager_flags=HEN_PLUGIN_RELOAD_AVAILABLE;c.poll(h,350);
        assert(!c.active&&c.can_exit()&&!c.exit_state_unknown()&&h.submissions==1);
    }
    {FakeHen h;auto c=client();h.observer=&c;h.caps_valid=false;c.observe_manager(0,false);
        assert(c.exit_state_unknown()&&!c.shutdown_required()&&!c.can_exit());
        c.poll_presence(h,100);assert(c.exit_state_unknown()&&!c.shutdown_required()&&!c.can_exit());
        h.caps_valid=true;c.poll_presence(h,350);assert(c.can_exit()&&!c.exit_state_unknown()&&h.submissions==0);
        c.observe_manager(HEN_PLUGIN_RELOAD_BLOCKED,true);c.observe_manager(0,false);
        assert(c.shutdown_required()&&!c.can_exit()); // A failed query never erases confirmed BLOCKED.
    }
    {FakeHen h;auto c=client();h.observer=&c;h.info_result=0x456;c.observe_manager(0,false);
        c.poll_presence(h,100);auto before=h.queries;c.poll_presence(h,200);assert(h.queries==before);
        c.poll_presence(h,45100);assert(c.timed_out&&!c.can_exit());before=h.queries;
        c.poll_presence(h,60000);assert(h.queries==before);c.resume(60001);
        h.info_result=0;c.poll_presence(h,60001);assert(c.can_exit()&&!c.timed_out&&h.submissions==0);
    }
    {FakeHen h;auto c=client();h.observer=&c;h.submit_result=0x123;
        assert(c.begin(h,100));assert(h.reserve&&h.info.operation_id==10&&(h.manager_flags&HEN_PLUGIN_RELOAD_BUSY));
        c.poll(h,100);assert(c.active&&!c.can_exit()&&c.status.operation_id==10&&h.submissions==1);
    }
    {FakeHen h;auto c=client();h.observer=&c;c.observe(h.info);c.observe_manager(HEN_PLUGIN_RELOAD_BUSY,true);
        h.manager_flags=HEN_PLUGIN_RELOAD_BUSY;c.poll_presence(h,100);assert(c.in_progress()&&!c.can_exit());
        h.manager_flags=HEN_PLUGIN_RELOAD_AVAILABLE;c.poll_presence(h,350);assert(c.can_exit()&&h.submissions==0);
    }
    {FakeHen h;auto c=client();h.observer=&c;c.observe(h.info);
        c.observe_status({77,10,101,0,3,HenPluginReloadPhase_Quarantined,HEN_PLUGIN_RELOAD_BLOCKED,0xea01,0x1759});
        assert(c.shutdown_required());h.info={78,103,1,0,HEN_PLUGIN_RELOAD_AVAILABLE,HenPluginReloadPhase_Idle};
        c.poll_presence(h,100);assert(c.shutdown_required()&&!c.can_exit()&&c.status.restoration_result==0x1759);
    }
    {FakeHen h;auto c=client();h.observer=&c;h.info={77,102,4,10,HEN_PLUGIN_RELOAD_AVAILABLE,HenPluginReloadPhase_Complete};
        c.observe(h.info);h.status.instance_id=78;c.poll_presence(h,100);
        assert(c.exit_state_unknown()&&!c.can_exit()&&!c.shutdown_required()&&h.submissions==0);
        h.status={77,10,101,102,4,HenPluginReloadPhase_Complete,0,0,0};c.poll_presence(h,350);assert(c.can_exit());
    }
    {FakeHen h;ReloadClient c;h.observer=&c;
        // Framebuffer setup fails before ordinary presence monitoring starts.
        h.info={77,0,3,10,HEN_PLUGIN_RELOAD_BLOCKED|HEN_PLUGIN_RELOAD_OLD_EXITED,HenPluginReloadPhase_Quarantined};
        h.manager_flags=HEN_PLUGIN_RELOAD_BLOCKED;h.status_result=0x456;
        assert(!c.inspect_exit(h,100)&&c.shutdown_required()&&c.exit_state_unknown());
        assert(h.queries>=2&&h.submissions==0);
    }
    {FakeHen h;auto c=client();h.observer=&c;
        assert(c.inspect_exit(h,100));const auto queries=h.queries;
        h.info.operation_id=10;h.info.flags|=HEN_PLUGIN_RELOAD_BUSY;h.info.phase=HenPluginReloadPhase_Launching;
        h.status.phase=HenPluginReloadPhase_Launching;h.manager_flags|=HEN_PLUGIN_RELOAD_BUSY;
        // Ordinary polling would skip this interval after a clear snapshot.
        assert(!c.poll_presence(h,101)&&h.queries==queries);
        assert(!c.inspect_exit(h,101)&&c.in_progress()&&h.queries>queries&&h.submissions==0);
    }
    std::cout<<"Native reload client: single submission, identity guard, health gate, lost reply/no-op, cold/external presence, fresh exit queries, pending cleanup, sticky quarantine, bounded retry and legacy cases passed.\n";
}
