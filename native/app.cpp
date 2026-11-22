#include <switch.h>
#include <kaihen.h>
#include "moader/ipc.hpp"
#include "moader/model.hpp"
#include "foreground.hpp"
#include "reload_client.hpp"
#include "ui.hpp"
#include "cover_loader.hpp"
#include "store_client.hpp"
#include "store_games.hpp"
#include "store_inventory.hpp"
#include "store_installer.hpp"
#include "update_client.hpp"
#include <cmath>
#include "ui_bitmap.hpp"
#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <map>

extern "C" { size_t __nx_heap_size = 0x4000000; }
namespace {
Service hen{}, service{};
moader_ui::State ui;
bool service_missing=false;
std::unique_ptr<moader_native::Foreground> foreground;
moader_native::ReloadClient reload;
bool modern_hen=false;
bool hen_protocol_known=false;
HenPluginReloadDiagnostics reload_diagnostics{};
bool reload_diagnostics_valid=false;
Result reload_diagnostics_result=MAKERESULT(Module_Libnx,LibnxError_NotInitialized);
u64 monotonic_ms(){return armTicksToNs(armGetSystemTick())/1000000;}
HenPluginName moader_name(){HenPluginName name{};std::memcpy(name.name,"moader",7);return name;}
Result open_hen(){
    if(serviceIsActive(&hen))serviceClose(&hen);
    Service ro{};Result rc=kaihenGetRoSession(&ro);
    if(R_SUCCEEDED(rc))rc=kaihenOpenHomebrewSession(&ro,&hen);
    return rc;
}
struct ReloadTransport {
    Result get_info(HenPluginInfo& out){
        const auto name=moader_name();Result rc=henGetPluginInfo(&hen,&name,&out);
        // A lost HEN session is recovered only for these read-only queries.
        if(R_FAILED(rc)&&R_SUCCEEDED(open_hen()))rc=henGetPluginInfo(&hen,&name,&out);
        if(R_SUCCEEDED(rc)){
            HenReloadCapabilities caps{};const auto capability=henGetReloadCapabilities(&hen,&caps);
            const bool valid=R_SUCCEEDED(capability)&&caps.version==1&&caps.instance_id==out.instance_id&&!(caps.flags&~7u);
            reload.supported=valid&&(caps.flags&HEN_PLUGIN_RELOAD_AVAILABLE);
            reload.observe_manager(caps.flags,valid);
        }else reload.observe_manager(0,false);
        return rc;
    }
    Result submit(const HenPluginReloadRequest& in,HenPluginReloadStatus& out){return henReloadPlugin(&hen,&in,&out);}
    Result get_status(u64 operation,HenPluginReloadStatus& out){return henGetPluginReloadStatus(&hen,operation,&out);}
} reload_transport;
void sync_reload_ui(){
    ui.reload_available=!foreground&&reload.can_submit();ui.reload_busy=reload.in_progress();
    ui.reload_error=reload.timed_out||reload.status.phase==HenPluginReloadPhase_Failed||reload.status.phase==HenPluginReloadPhase_Quarantined;
    ui.reload_shutdown_required=reload.shutdown_required();
    ui.reload_exit_unknown=reload.exit_state_unknown();
    ui.reload_detail=reload.detail;
}
Result dispatch(u32 id,const void* input,size_t input_size,void* output,size_t capacity) {
    if(foreground){size_t size=0;const Result rc=moader_native::command(id,input,input_size,output,&size,&foreground->context);
        if(R_SUCCEEDED(rc)&&size>capacity)return MAKERESULT(Module_Libnx,LibnxError_BadInput);
        return rc;}
    return serviceDispatchImpl(&service,id,input,input_size,output,capacity,{});
}
template<class T>Result sendOut(u32 id,T& value){return dispatch(id,nullptr,0,&value,sizeof(value));}
template<class T>Result sendIn(u32 id,const T& value){return dispatch(id,&value,sizeof(value),nullptr,0);}
template<class I,class O>Result sendInOut(u32 id,const I& input,O& output){return dispatch(id,&input,sizeof(input),&output,sizeof(output));}
void message(const std::string& text,bool failed=false){ui.message=text;ui.message_error=failed;}
void error(Result rc){char text[64]{};
    if((foreground||serviceIsActive(&service))&&R_SUCCEEDED(sendOut(moader::ipc::LastError,text))&&text[0])message(text,true);
    else{char code[64];std::snprintf(code,sizeof(code),"Operation failed: 0x%08X. Press Y to check the service.",rc);message(code,true);}
}
Result connect(){
    if(foreground){reload.unavailable(true);sync_reload_ui();return 0;}
    service_missing=false;
    if(serviceIsActive(&service))serviceClose(&service);
    Result rc=open_hen();if(R_FAILED(rc))return rc;
    HenStatus status{};rc=henGetStatus(&hen,&status);if(R_FAILED(rc))return rc;
    hen_protocol_known=true;
    modern_hen=status.protocol_version>=7;
    if(modern_hen){
        HenReloadCapabilities caps{};rc=henGetReloadCapabilities(&hen,&caps);
        if(R_FAILED(rc)||caps.version!=1||(caps.flags&~7u)){
            reload.observe_manager(0,false);
            if(R_SUCCEEDED(rc))rc=MAKERESULT(Module_Libnx,LibnxError_BadInput);
            reload.detail=moader_native::ReloadClient::transport_text("Could not inspect kaiHEN reload capability",rc);sync_reload_ui();return rc;}
        reload.supported=caps.flags&HEN_PLUGIN_RELOAD_AVAILABLE;reload.observe_manager(caps.flags,true);
        HenPluginInfo info{};rc=reload_transport.get_info(info);
        if(R_FAILED(rc)){
            reload.info_valid=false;reload.detail=moader_native::ReloadClient::transport_text("Could not inspect managed Moader; foreground fallback paused",rc);sync_reload_ui();return rc;
        }
        reload.observe(info);
        if(info.operation_id){HenPluginReloadStatus operation{};
            if(R_SUCCEEDED(henGetPluginReloadStatus(&hen,info.operation_id,&operation))&&
               operation.instance_id==info.instance_id&&operation.operation_id==info.operation_id)
                reload.observe_status(operation);
        }else reload.observe_status({});
        if(moader_native::ReloadClient::unmanaged(info)){
            if(!reload.can_exit()){
                reload.detail="kaiHEN has unresolved Loader activity. Foreground fallback is paused; press Y to check again.";
                sync_reload_ui();return MAKERESULT(Module_Libnx,LibnxError_BadInput);
            }
            service_missing=true;sync_reload_ui();return MAKERESULT(Module_Libnx,LibnxError_NotFound);}
        if(!moader_native::ReloadClient::attachable(info)){
            reload.detail="kaiHEN is reloading Moader or has blocked this plugin. Foreground fallback is paused; press Y to check again.";
            if(reload.status.operation_id==info.operation_id&&reload.status.instance_id==info.instance_id&&
               (reload.status.phase==HenPluginReloadPhase_Failed||reload.status.phase==HenPluginReloadPhase_Quarantined))
                reload.detail=moader_native::ReloadClient::result_text("Resident reload failed; foreground fallback paused",reload.status.result,reload.status.restoration_result);
            sync_reload_ui();return MAKERESULT(Module_Libnx,LibnxError_BadInput);
        }
        reload.detail=reload.supported?"Reload restarts the resident Moader plugin and verifies the new process before reconnecting.":"Install the new kaiHEN build with plugin reload enabled.";
        const auto name=moader_name();rc=henOpenPlugin(&hen,&name,&service);sync_reload_ui();return rc;
    }
    reload.observe_manager(0,true);reload.unavailable(false);sync_reload_ui();
    u32 count=0;rc=henGetPluginCount(&hen,&count);if(R_FAILED(rc))return rc;
    for(u32 i=0;i<count;++i){HenPluginName name{};rc=henGetPluginName(&hen,i,&name);if(R_FAILED(rc))return rc;
        if(!std::strncmp(name.name,"moader",sizeof(name.name)))return henOpenPlugin(&hen,&name,&service);}
    service_missing=true;return MAKERESULT(Module_Libnx,LibnxError_NotFound);
}
Result establish(){
    // No enumeration miss or UI reconnect may create a second owner while a
    // submitted reload is unresolved, including after the polling deadline.
    if(reload.active)return MAKERESULT(Module_Libnx,LibnxError_BadInput);
    const Result rc=connect();if(R_SUCCEEDED(rc)||!service_missing)return rc;
    try{foreground=std::make_unique<moader_native::Foreground>();reload.unavailable(true);sync_reload_ui();return 0;}
    catch(const std::exception& e){message(e.what(),true);return MAKERESULT(Module_Libnx,LibnxError_BadInput);}
}
std::string text_chunks(u32 command,u32 index,u32 field,const std::string& fallback,size_t limit=65535,
                        std::optional<uint64_t> generation=std::nullopt){
    std::string text;for(unsigned offset=0;offset<limit;offset+=moader::ipc::TextChunkBytes){
        moader::ipc::Selection input{generation.value_or(ui.summary.generation),index,(field<<16)|offset};char chunk[64]{};
        if(R_FAILED(sendInOut(command,input,chunk)))return text.empty()?fallback:text;
        const size_t n=strnlen(chunk,sizeof(chunk));if(n>63)return fallback;
        text.append(chunk,n);if(n<63)return text;
    }return text;
}
void refresh_debug(){
    if(foreground)ui.events=foreground->library.diagnostic_lines();
    else{u32 count=0;if(R_SUCCEEDED(sendOut(moader::ipc::DiagnosticsInfo,count))){
            ui.events.clear();for(u32 i=count>128?count-128:0;i<count;++i)ui.events.push_back(text_chunks(moader::ipc::GetDiagnostic,i,0,"",4096));
        }else{ui.events.clear();std::ifstream file("sdmc:/moader/runtime/runtime.log",std::ios::binary);
            if(file){file.seekg(0,std::ios::end);const auto end=file.tellg();const std::streamoff start=end>65536?static_cast<std::streamoff>(end)-65536:0;
                file.seekg(start);std::string line;if(start)std::getline(file,line);
                while(std::getline(file,line))if(!line.empty()){ui.events.push_back(line.substr(0,1024));if(ui.events.size()>128)ui.events.erase(ui.events.begin());}}
        }}
}
void refresh_reload_diagnostics(){
    reload_diagnostics_valid=false;
    if(!modern_hen||!reload.status.operation_id)return;
    HenPluginInfo current{};reload_diagnostics_result=reload_transport.get_info(current);
    if(R_FAILED(reload_diagnostics_result))return;
    if(current.instance_id!=reload.status.instance_id){reload_diagnostics_result=MAKERESULT(Module_Libnx,LibnxError_BadInput);return;}
    HenPluginReloadDiagnostics next{};
    reload_diagnostics_result=henGetPluginReloadDiagnostics(&hen,reload.status.operation_id,&next);
    if(R_FAILED(reload_diagnostics_result))return;
    if(next.version!=1||next.operation_id!=reload.status.operation_id||next.stage>HenReloadLaunchStage_Health||
       (next.flags&~511u)){reload_diagnostics_result=MAKERESULT(Module_Libnx,LibnxError_BadInput);return;}
    reload_diagnostics=next;reload_diagnostics_valid=true;
}
void save_debug(const moader_native::CoverLoader& covers){if(ui.connected)refresh_debug();refresh_reload_diagnostics();sync_reload_ui();
    std::ofstream file("sdmc:/moader/app-diagnostics.txt",std::ios::binary);
    file<<"Moader "<<MOADER_APP_VERSION<<"\nMode: "<<(foreground?"foreground":"resident")<<"\nControl service reachable: "<<ui.connected<<"\nSummary freshness: "<<(ui.connected?"current":"last-known; service disconnected")<<"\nLibrary generation: "<<ui.summary.generation
        <<"\nFS acknowledged generation: "<<ui.summary.applied_generation<<"\nBackend channel connected (summary): "<<bool(ui.summary.flags&moader::ipc::BackendConnected)
        <<"\nRuntime result: 0x"<<std::hex<<ui.summary.last_result<<std::dec
        <<"\nLibrary inventory: "<<(ui.inventory_unavailable()?"unavailable or retained; changes paused":"current")
        <<"\nLast interface message: "<<ui.message<<"\nInterface message is an error: "<<ui.message_error
        <<"\nRuntime: "<<ui.runtime_detail
        <<"\nReload: "<<reload.detail<<"\nReload operation: "<<reload.status.operation_id
        <<"\nReload phase: "<<reload.status.phase<<"\nReload status verified: "<<bool(reload.status.operation_id)
        <<"\nReload outcome pending: "<<reload.active<<"\nReload IPC result: 0x"<<std::hex<<reload.transport_result;
    if(reload.status.operation_id)file<<"\nReload result: 0x"<<reload.status.result<<"\nReload restoration result: 0x"<<reload.status.restoration_result;
    else file<<"\nReload primary and restoration results: unknown";
    file<<std::dec<<"\n\n";
    file<<"HEN reload API available: "<<reload.supported
        <<"\nHEN plugin identity verified: "<<reload.info_valid
        <<"\nHEN instance: "<<reload.info.instance_id
        <<"\nPlugin PID: "<<reload.info.process_id<<"\nPlugin generation: "<<reload.info.generation
        <<"\nReload old PID: "<<reload.status.old_process_id<<"\nReload new PID: "<<reload.status.new_process_id
        <<"\nReload flags: "<<reload.status.flags
        <<"\nReload outcome unverified: "<<(reload.uncertain||reload.timed_out||reload.shutdown_required())
        <<"\nPower cycle required: "<<reload.shutdown_required()<<"\n\n";
    file<<"Gate diagnostics verified: "<<reload_diagnostics_valid
        <<"\nGate diagnostic query result: 0x"<<std::hex<<reload_diagnostics_result<<std::dec<<'\n';
    if(reload_diagnostics_valid){const auto& d=reload_diagnostics;
        file<<"Gate diagnostic operation: "<<d.operation_id<<"\nGate launch stage: "<<d.stage
            <<"\nGate diagnostic flags: "<<d.flags
            <<"\nPrimary gate step: 0x"<<std::hex<<d.primary_gate_step
            <<"\nRestoration gate step: 0x"<<d.restoration_gate_step
            <<"\nGate primary result: 0x"<<d.primary_result<<"\nGate restoration result: 0x"<<d.restoration_result
            <<"\nPM result: 0x"<<d.pm_result<<std::dec
            <<"\nPM result finished: "<<bool(d.flags&HEN_RELOAD_DIAG_PM_FINISHED)
            <<"\nMatching mounts: "<<d.matching_mounts<<"\nCached gate probe result: 0x"<<std::hex<<d.probe_result<<std::dec
            <<"\nCached gate state: "<<d.probe_state<<"\nCached gate wait kind: "<<d.probe_wait_kind
            <<"\nCached selected waiter mask: "<<d.probe_selected_mask<<"\nCached receive result: "<<d.probe_receive_result<<'\n';
    }
    file<<'\n';
    for(const auto& line:ui.events)file<<line<<'\n';
    for (const auto& game : ui.games) {
        const auto problem = covers.error_for(game.id);
        if (problem.empty()) continue;
        char id[17];
        std::snprintf(id, sizeof(id), "%016llX", static_cast<unsigned long long>(game.id));
        file << "Game icon " << id << ": " << problem << '\n';
    }
    file.close();
    message(file?"Saved sdmc:/moader/app-diagnostics.txt":"Could not save the diagnostic report.",!file);
}
void save_screenshot(const uint32_t* pixels,u32 stride){
    try {
        moader::atomic_write("sdmc:/moader/app-screenshot.bmp",moader_ui::bitmap(pixels,1280,720,stride));
        message("Saved sdmc:/moader/app-screenshot.bmp");
    } catch(const std::exception& e) {
        message(std::string("Could not save the screenshot: ")+e.what(),true);
    }
}
std::string address(){char ip[INET_ADDRSTRLEN]{};
    if(!ui.summary.ipv4||!inet_ntop(AF_INET,&ui.summary.ipv4,ip,sizeof(ip)))return "LAN address unavailable";
    return std::string("http://")+ip+':'+std::to_string(ui.summary.port?ui.summary.port:7712);
}
void pairing(){char raw[64]{};const auto rc=sendOut(moader::ipc::PairingCode,raw);
    if(R_FAILED(rc)){ui.pairing="Unavailable";error(rc);return;}
    ui.pairing=std::string(raw,strnlen(raw,sizeof(raw)));bool valid=ui.pairing.size()==6;
    for(char c:ui.pairing)if(c<'0'||c>'9')valid=false;
    if(!valid){ui.pairing="Unavailable";message("The service did not return a six-digit pairing code.",true);}
}
void search_store() {
    SwkbdConfig keyboard{};
    if (R_FAILED(swkbdCreate(&keyboard, 0))) {
        message("The search keyboard could not be opened.", true);
        return;
    }
    swkbdConfigMakePresetDefault(&keyboard);
    swkbdConfigSetHeaderText(&keyboard, "Search mods");
    swkbdConfigSetGuideText(&keyboard, "Mod, game, creator or category");
    swkbdConfigSetInitialText(&keyboard, ui.store.query.c_str());
    swkbdConfigSetOkButtonText(&keyboard, "Search");
    swkbdConfigSetStringLenMax(&keyboard, 80);
    swkbdConfigSetStringLenMin(&keyboard, 0);
    swkbdConfigSetReturnButtonFlag(&keyboard, false);
    char text[321]{};
    const auto rc = swkbdShow(&keyboard, text, sizeof(text));
    swkbdClose(&keyboard);
    if (R_SUCCEEDED(rc))
        ui.store.set_query(text);
    else if (rc != MAKERESULT(Module_Libnx, LibnxError_LibAppletBadExit))
        message("The search keyboard is unavailable.", true);
}
void restore_store_listener(moader_native::StoreInstaller& install) {
    if (install.temporary_listener) {
        const u32 off = 0;
        const auto rc = sendIn(moader::ipc::WebEnabled, off);
        if (R_FAILED(rc))
            message("Could not restore the listener. Check Connection in Settings.", true);
        else
            install.temporary_listener = false;
    }
}
void begin_store_install(moader_native::StoreInstaller& install) {
    using namespace moader_ui;
    if (install.active() || ui.store.page != Shelf::Release || ui.has_store_mod(ui.store.detail))
        return;
    const auto& g = ui.store.games[ui.store.detail.game];
    const auto& mod = g.mods[ui.store.detail.release];
    auto fail = [&](const char* msg) {
        ui.transfer = {InstallPhase::Failed, g.id, mod.id, msg, 0, mod.package.bytes};
        restore_store_listener(install);
    };
    moader::ipc::SummaryReply status{};
    if (!ui.connected || ui.inventory_unavailable() || R_FAILED(sendOut(moader::ipc::Summary, status)) ||
        status.flags & moader::ipc::InventoryUnavailable) {
        fail("Reconnect to your library and try again.");
        return;
    }
    char code[64]{};
    if (R_FAILED(sendOut(moader::ipc::PairingCode, code)) || strnlen(code, sizeof(code)) != 6) {
        fail("Could not authorize the local import.");
        return;
    }
    if (!(status.flags & moader::ipc::WebRunning)) {
        const u32 on = 1;
        if (R_FAILED(sendIn(moader::ipc::WebEnabled, on))) {
            fail("Could not start the local import service.");
            return;
        }
        install.temporary_listener = true;
        if (R_FAILED(sendOut(moader::ipc::Summary, status)) || !(status.flags & moader::ipc::WebRunning)) {
            fail("The local import service did not start.");
            return;
        }
    }
    if (!install.start(ui.transfer, g.id, mod, status.port, code))
        restore_store_listener(install);
}
void refresh_preferences(){
    moader::ipc::PreferencesReply p{};
    ui.preferences_available=R_SUCCEEDED(sendOut(moader::ipc::GetPreferences,p))&&!(p.flags&~15u)&&p.port>=1024&&p.port<=65535&&p.port!=7713;
    if(ui.preferences_available)ui.preferences=p;
}
void refresh_game_source(){
    ui.game_source_available=false;
    if(!ui.connected||ui.inventory_unavailable()||ui.selected()>=ui.mods.size())return;
    const moader::ipc::Selection input{ui.summary.generation,ui.selected(),0};u32 source=0;
    if(R_SUCCEEDED(sendInOut(moader::ipc::GetGameSource,input,source))&&source<=moader::ipc::CopyDigital){
        ui.game_source=source;ui.game_source_title=ui.mods[ui.selected()].data.title_id;ui.game_source_available=true;
    }
}
void save_preferences(){
    const moader::ipc::PreferencesRequest request{ui.preferences.flags,ui.preferences.port,ui.preference_mask};
    const auto result=sendIn(moader::ipc::SetPreferences,request);
    if(R_FAILED(result)){error(result);refresh_preferences();}
    else {refresh_preferences();message("Settings saved. A changed web port applies the next time the listener starts.");}
}
Result refresh(){
    if(foreground)foreground->poll();
    moader::ipc::SummaryReply next{};Result rc=sendOut(moader::ipc::Summary,next);if(R_FAILED(rc))return rc;
    if(next.version!=moader::ipc::Version)return MAKERESULT(Module_Libnx,LibnxError_BadInput);
    if(ui.confirm_remove&&next.generation!=ui.summary.generation){ui.confirm_remove=false;message("The library changed; removal was cancelled.");}
    const uint64_t old_generation=ui.summary.generation;
    const std::string old_id=ui.selected()<ui.mods.size()?ui.mods[ui.selected()].id:"";
    const uint64_t old_game=ui.game<ui.games.size()?ui.games[ui.game].id:0;
    auto retain=[&](const moader::ipc::SummaryReply& latest,const std::string& detail){
        ui.retain_inventory(latest); ui.address=address();
        ui.runtime_detail=text_chunks(moader::ipc::RuntimeDetail,0,0,latest.detail);
        message(detail,true); return Result(0);
    };
    if((next.flags&moader::ipc::InventoryUnavailable)&&!next.mod_count&&!ui.mods.empty())
        return retain(next,"Library inventory is unavailable. Keeping the previous list until a complete refresh succeeds.");
    refresh_preferences();
    std::map<uint64_t,std::string> game_names;
    std::vector<moader_ui::Mod> mods;mods.reserve(next.mod_count);
    for(u32 i=0;i<next.mod_count;++i){moader::ipc::Selection input{next.generation,i,0};moader_ui::Mod mod;
        rc=sendInOut(moader::ipc::GetMod,input,mod.data);
        if(R_FAILED(rc)){
            moader::ipc::SummaryReply latest{};
            if(R_FAILED(sendOut(moader::ipc::Summary,latest))||latest.version!=moader::ipc::Version)return rc;
            return retain(latest,"Library refresh changed or failed. Showing the previous list; changes are paused until refresh succeeds.");
        }
        mod.data.name[31]=0;
        const bool cached=old_generation==next.generation&&i<ui.mods.size()&&ui.mods[i].data.title_id==mod.data.title_id;
        mod.name=cached?ui.mods[i].name:text_chunks(moader::ipc::GetModText,i,moader::ipc::NameText,mod.data.name,65535,next.generation);
        mod.id=cached?ui.mods[i].id:text_chunks(moader::ipc::GetModText,i,moader::ipc::IdText,"Unavailable from this service",65535,next.generation);
        // Runtime detail can change without a new saved-library generation.
        mod.detail=text_chunks(moader::ipc::GetModText,i,moader::ipc::DetailText,"Inspect apply state. The service does not supply additional details.",65535,next.generation);
        auto [name,inserted]=game_names.try_emplace(mod.data.title_id);
        if(inserted)name->second=text_chunks(moader::ipc::GetGameName,i,0,"",65535,next.generation);
        mod.game_name=name->second;mods.push_back(std::move(mod));
    }
    moader::ipc::SummaryReply latest{};rc=sendOut(moader::ipc::Summary,latest);if(R_FAILED(rc))return rc;
    if(latest.version!=moader::ipc::Version)return MAKERESULT(Module_Libnx,LibnxError_BadInput);
    if(latest.generation!=next.generation||latest.mod_count!=next.mod_count)
        return retain(latest,"The library changed during refresh. Keeping the previous list and retrying.");
    const bool was_pending=ui.inventory_refresh_pending;
    ui.summary=latest;ui.inventory_refresh_pending=false;
    ui.mods=std::move(mods);ui.regroup(old_game);
    if(ui.game<ui.games.size()&&ui.games[ui.game].id==old_game){
        const auto& group=ui.games[ui.game].mods;
        auto it=std::find_if(group.begin(),group.end(),[&](unsigned i){return ui.mods[i].id==old_id;});
        if(it!=group.end())ui.row=it-group.begin();
        else if(ui.view==moader_ui::View::Details){ui.view=moader_ui::View::Mods;ui.confirm_remove=false;}
    }
    ui.address=address();
    if(was_pending)message("");
    ui.runtime_detail=text_chunks(moader::ipc::RuntimeDetail,0,0,latest.detail);refresh_debug();ui.connected=true;refresh_game_source();return 0;
}
void update(Result& connection){const auto rc=refresh();if(R_FAILED(rc)){connection=rc;ui.connected=false;ui.confirm_remove=false;error(rc);}}
bool begin_reload(){
    if(foreground){reload.unavailable(true);sync_reload_ui();message(reload.detail,true);return false;}
    const bool submitted=reload.begin(reload_transport,monotonic_ms());
    if(submitted){
        // HEN owns the operation. The old direct session is expected to die.
        if(serviceIsActive(&service))serviceClose(&service);
        ui.connected=false;ui.confirm_remove=false;ui.game_source_available=false;
        ui.inventory_refresh_pending=true;ui.message.clear();
    }else message(reload.detail,true);
    sync_reload_ui();
    return submitted;
}
void poll_reload(Result& connection, moader_native::UpdateClient& updater){
    if(!modern_hen||foreground)return;
    if(!reload.active){
        reload.poll_presence(reload_transport,monotonic_ms());sync_reload_ui();
        updater.restarted(ui.updater,false,reload.detail);
        return;
    }
    reload.poll(reload_transport,monotonic_ms());sync_reload_ui();
    if(reload.ready){
        // Check publication again immediately before opening a fresh session.
        HenPluginInfo current{};const auto checked=reload_transport.get_info(current);
        if(R_SUCCEEDED(checked)&&moader_native::ReloadClient::attachable(current)&&
           current.instance_id==reload.request.instance_id&&current.operation_id==reload.status.operation_id&&
           current.process_id==reload.status.new_process_id&&current.generation==reload.status.generation){
            const auto name=moader_name();connection=henOpenPlugin(&hen,&name,&service);
            const bool opened=R_SUCCEEDED(connection);
            const bool installing_update=ui.updater.phase==moader_ui::UpdatePhase::Restarting;
            if(opened)connection=refresh();
            if(R_SUCCEEDED(connection)||(opened&&installing_update)){
                // The manager verified the replacement's health and we opened that exact process.
                // A new app/plugin pair may have changed its library protocol.
                if(R_FAILED(connection)){serviceClose(&service);ui.connected=false;}
                reload.connected();sync_reload_ui();message(installing_update?"":reload.detail);
                updater.restarted(ui.updater,true,{});
                if(installing_update){
                    ui.view=moader_ui::View::Updates;ui.settings_row=0;ui.tab_focus=ui.nav_focus=false;
                }
            }else {
                ui.connected=false;if(serviceIsActive(&service))serviceClose(&service);
                // The operation remains query-only; reconnect retries never submit.
                reload.ready=false;reload.detail=moader_native::ReloadClient::transport_text("New process is healthy but the service could not be opened",connection);sync_reload_ui();
            }
        }else reload.ready=false;
    }else if(!reload.active){message(reload.detail,true);updater.restarted(ui.updater,false,reload.detail);}
}

bool allow_hbmenu_return(){
    if(foreground)return reload.can_exit();
    // Framebuffer initialization can fail before establish(). Obtain protocol
    // identity here without constructing a foreground writer or submitting work.
    if(!hen_protocol_known){
        HenStatus status{};Result rc=open_hen();
        if(R_SUCCEEDED(rc))rc=henGetStatus(&hen,&status);
        if(R_FAILED(rc)){
            reload.observe_manager(0,false);sync_reload_ui();return false;
        }
        hen_protocol_known=true;modern_hen=status.protocol_version>=7;
        if(!modern_hen)reload.observe_manager(0,true);
    }
    const bool allowed=modern_hen?reload.inspect_exit(reload_transport,monotonic_ms()):reload.can_exit();
    sync_reload_ui();return allowed;
}

int framebuffer_failure(PadState& pad,Framebuffer* framebuffer){
    bool allowed=allow_hbmenu_return();
    // No framebuffer is available for recovery text. Retain the same bounded
    // query-only monitoring and controller retry instead of an unsafe return.
    // Forced system termination remains outside this voluntary-exit guard.
    while(!allowed&&appletMainLoop()){
        padUpdate(&pad);const auto down=padGetButtonsDown(&pad);
        if(down&(HidNpadButton_Y|HidNpadButton_Plus))allowed=allow_hbmenu_return();
        else if(modern_hen){
            reload.poll_presence(reload_transport,monotonic_ms());sync_reload_ui();
            if(reload.can_exit())allowed=allow_hbmenu_return();
        }
        svcSleepThread(16000000);
    }
    if(serviceIsActive(&service))serviceClose(&service);
    if(serviceIsActive(&hen))serviceClose(&hen);
    if(framebuffer)framebufferClose(framebuffer);
    return 1;
}

}
int main(int argc, char** argv){
    padConfigureInput(1,HidNpadStyleSet_NpadStandard);PadState pad;padInitializeDefault(&pad);
    Framebuffer framebuffer{};Result rc=framebufferCreate(&framebuffer,nwindowGetDefault(),1280,720,PIXEL_FORMAT_RGBA_8888,2);
    if(R_FAILED(rc))return framebuffer_failure(pad,nullptr);
    rc=framebufferMakeLinear(&framebuffer);if(R_FAILED(rc))return framebuffer_failure(pad,&framebuffer);
    hidInitializeTouchScreen();
    struct Touch {bool down=false, cancel=false; u32 id=0; float x=0,y=0,last_x=0,last_y=0;} touch;
    moader_native::CoverLoader covers;
    moader_native::StoreClient store_client;
    moader_native::StoreInstaller store_installer;
    moader_native::UpdateClient updater("sdmc:", argc > 0 && argv && argv[0] ? argv[0] : "");
    updater.open(ui.updater, MOADER_APP_VERSION);
    if(ui.updater.phase==moader_ui::UpdatePhase::Recovery)ui.view=moader_ui::View::Updates;
    moader_native::StoreInventory store_inventory(moader_native::read_store_games);
    moader_ui::Renderer renderer;Result connection=establish();if(R_SUCCEEDED(connection))connection=refresh();
    if(R_FAILED(connection)){ui.connected=false;if(modern_hen)message(reload.detail,true);else error(connection);}
    u64 next_refresh=0,last_tick=armGetSystemTick(),repeat_at=0;u64 previous_direction=0;
    // System termination remains under the OS's control. The guards below
    // cover this app's voluntary return paths; they cannot repair Loader or
    // guarantee safety if the system forcibly terminates the host process.
    while(appletMainLoop()){
        padUpdate(&pad);u64 down=padGetButtonsDown(&pad);const u64 held=padGetButtons(&pad),now=armGetSystemTick();
        poll_reload(connection,updater);
        const bool can_prompt=!store_installer.active()&&!ui.confirm_remove&&!ui.reload_busy&&
                              !ui.reload_shutdown_required&&!ui.reload_exit_unknown;
        const bool prompt_was_open=ui.updater.prompt;
        if(updater.poll(ui.updater,monotonic_ms(),can_prompt)){
            if(!ui.connected||!ui.reload_available||!begin_reload()||!reload.active)
                updater.restarted(ui.updater,false,reload.detail);
        }
        if(!prompt_was_open && ui.updater.prompt){down=0;if(touch.down)touch.cancel=true;}
        if(down&HidNpadButton_Plus){
            if(ui.updater.installing())message("Keep Moader open until the update finishes.");
            else if(allow_hbmenu_return())break;
            else message(reload.shutdown_required()?
                "Loader recovery is unverified. Save Debug and a screen, then hold POWER for 20 seconds to shut down.":
                reload.exit_state_unknown()?"Loader state is unverified. Press Y to query it again; returning to hbmenu is paused.":
                "Reload is in progress. Wait for its result before returning to hbmenu.",true);
        }
        if((down&HidNpadButton_Y)&&modern_hen&&!reload.can_exit()){
            reload.resume(monotonic_ms());poll_reload(connection,updater);
            message(reload.shutdown_required()?
                "Loader recovery remains unverified. Save Debug and a screen, then force the console off with POWER.":
                "Checking the existing Loader state. No new reload request was sent.",reload.shutdown_required());
            down&=~u64(HidNpadButton_Y);
        }
        constexpr u64 directions=HidNpadButton_AnyUp|HidNpadButton_AnyDown|HidNpadButton_AnyLeft|HidNpadButton_AnyRight;
        const u64 direction=held&directions;
        if(direction!=previous_direction){repeat_at=now+armNsToTicks(350000000);previous_direction=direction;}
        else if(direction&&now>=repeat_at){down|=direction;repeat_at=now+armNsToTicks(110000000);}
        uint32_t local=0;
        if(down&HidNpadButton_B)local|=moader_ui::BackInput;
        if(down&HidNpadButton_ZL)local|=moader_ui::DebugInput;
        if(down&HidNpadButton_AnyUp)local|=moader_ui::UpInput;
        if(down&HidNpadButton_AnyDown)local|=moader_ui::DownInput;
        if(down&HidNpadButton_A)local|=moader_ui::ActivateInput;
        if(down&HidNpadButton_StickL)local|=moader_ui::SettingsInput;
        if(down&HidNpadButton_AnyLeft)local|=moader_ui::LeftInput;
        if(down&HidNpadButton_AnyRight)local|=moader_ui::RightInput;
        using namespace moader_ui;
        if(down&HidNpadButton_L)local|=PreviousInput;
        if(down&HidNpadButton_R)local|=NextInput;
        if(down&HidNpadButton_X)local|=RemoveInput;
        if(down&HidNpadButton_Y)local|=WebInput;
        if(down&HidNpadButton_ZR)local|=ConnectionInput;
        if(down&HidNpadButton_StickR)local|=DetailInput;
        if(local && !ui.reload_busy && !ui.reload_shutdown_required)ui.message.clear();
        auto actions=ui.local_input(local,local?renderer.scroll_limit(ui,ui.view):0);
        HidTouchScreenState ts{};
        if(hidGetTouchScreenStates(&ts,1)){
            if(ts.count>0){
                const auto& t=ts.touches[0];
                if(!touch.down){touch.down=true;touch.id=t.finger_id;touch.x=t.x;touch.y=t.y;}
                if(ts.count!=1||t.finger_id!=touch.id||local)touch.cancel=true;
                touch.last_x=t.x;touch.last_y=t.y;
            }else if(touch.down){
                float dx=touch.last_x-touch.x,dy=touch.last_y-touch.y;
                if(!touch.cancel&&!local){
                    ui.message.clear();
                    if(ui.view==View::Library&&std::abs(dx)>60&&std::abs(dx)>std::abs(dy)*1.5f)
                        actions|=ui.local_input(dx<0?NextInput:PreviousInput,0);
                    else if(std::abs(dy)>55&&std::abs(dy)>std::abs(dx)*1.5f)
                        actions|=ui.local_input(dy<0?DownInput:UpInput,renderer.scroll_limit(ui,ui.view));
                    else if(std::abs(dx)<23&&std::abs(dy)<23)actions|=renderer.tap(ui,touch.last_x/1280.f,touch.last_y/720.f);
                }
                touch={};
            }
        }
        if (store_installer.active() && (actions & (ToggleWeb | ReloadService))) {
            actions &= ~(ToggleWeb | ReloadService);
            message("Wait for the Store installation to finish.", true);
        }
        if(actions&RefreshEvents)refresh_debug();
        if(actions&SaveReport)save_debug(covers);
        if(actions&SavePreferences)save_preferences();
        if(actions&CheckUpdates)updater.check(ui.updater,true);
        if((actions&ExitUpdatedApp)&&allow_hbmenu_return())break;
        if(actions&ToggleAutoUpdates)updater.toggle(ui.updater);
        if(actions&InstallUpdate){
            if(store_installer.active())message("Finish the Store download before updating Moader.",true);
            else {
                if(!ui.connected&&!foreground&&reload.can_exit()){
                    connection=connect();
                    if(R_SUCCEEDED(connection))update(connection);
                }
                updater.install(ui.updater,ui.connected&&ui.reload_available&&!ui.reload_busy);
            }
        }
        if(actions&ReloadService)begin_reload();
        if(actions&GetGameSource)refresh_game_source();
        if(actions&GetPairing)pairing();
        if((actions&SaveGameSource)&&ui.can_save_game_source()){
            const moader::ipc::Selection input{ui.summary.generation,ui.selected(),ui.game_source};
            const auto result=sendIn(moader::ipc::SetGameSource,input);
            if(R_FAILED(result))error(result);else message("Game copy saved.");
            update(connection);
        }
        if(actions&Reconnect){
            if(reload.active){reload.resume(monotonic_ms());sync_reload_ui();}
            else {message("");connection=establish();if(R_SUCCEEDED(connection))update(connection);else if(modern_hen)message(reload.detail,true);else error(connection);}
        }
        if((actions&ToggleWeb)&&ui.connected){
            u32 enabled=ui.summary.flags&moader::ipc::WebRunning?0:1;auto result=sendIn(moader::ipc::WebEnabled,enabled);
            if(R_FAILED(result))error(result);else message(enabled?"Web listener started.":"Web listener stopped.");
            update(connection);if(enabled&&(ui.summary.flags&moader::ipc::WebRunning))pairing();
        }
        if((actions&(ToggleMod|RemoveMod))&&ui.connected&&!ui.inventory_unavailable()&&ui.selected()<ui.mods.size()){
            const bool remove=actions&RemoveMod;
            moader::ipc::Selection input{ui.summary.generation,ui.selected(),remove||ui.mods[ui.selected()].data.flags&moader::ipc::Requested?0u:1u};
            auto result=sendIn(remove?moader::ipc::Remove:moader::ipc::SetEnabled,input);
            if(R_FAILED(result))error(result);else message(remove?"Mod removed. Files are in recovery storage.":"Request saved. The runtime state shows its acknowledgement.");
            update(connection);
        }
        if (ui.view == View::Store && (actions & SearchStore)) search_store();
        if (ui.view == View::Store && (actions & InstallStoreMod)) begin_store_install(store_installer);
        if (store_installer.poll(ui.transfer)) {
            restore_store_listener(store_installer);
            update(connection);
            if (ui.view != View::Store)
                message(ui.transfer.phase == InstallPhase::Installed ? "Store mod added to your Library." : ui.transfer.message,
                        ui.transfer.phase == InstallPhase::Failed);
            if (ui.transfer.phase == InstallPhase::Installed) ui.transfer = {};
        }
        if(ui.connected&&now>=next_refresh){next_refresh=now+armNsToTicks(2000000000);update(connection);}
        covers.poll(ui,monotonic_ms());
        store_client.poll(ui);
        store_inventory.poll(ui);
        u32 stride=0;auto* pixels=static_cast<uint32_t*>(framebufferBegin(&framebuffer,&stride));
        if(!pixels){
            if(!ui.updater.installing()&&reload.can_exit()&&allow_hbmenu_return())break;
            // A missing framebuffer is not permission to return through an
            // unverified Loader. Keep report/input handling alive for recovery.
            svcSleepThread(16000000);
            continue;
        }
        renderer.draw(pixels,1280,720,stride,ui,float(armTicksToNs(now-last_tick))/1000000000.f);last_tick=now;
        if((down&HidNpadButton_Minus)||(actions&moader_ui::SaveScreenshot))save_screenshot(pixels,stride);
        framebufferEnd(&framebuffer);
    }
    store_installer.stop();
    restore_store_listener(store_installer);
    foreground.reset();if(serviceIsActive(&service))serviceClose(&service);if(serviceIsActive(&hen))serviceClose(&hen);framebufferClose(&framebuffer);return 0;
}
