#include <switch.h>
#include <kaihen.h>
#include "moader/ipc.hpp"
#include "moader/model.hpp"
#include "foreground.hpp"
#include "reload_client.hpp"
#include "ui.hpp"
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
            reload.supported=R_SUCCEEDED(capability)&&caps.version>=1&&(caps.flags&HEN_PLUGIN_RELOAD_AVAILABLE);
            reload.manager_flags=R_SUCCEEDED(capability)?caps.flags:HEN_PLUGIN_RELOAD_BLOCKED;
        }
        return rc;
    }
    Result submit(const HenPluginReloadRequest& in,HenPluginReloadStatus& out){return henReloadPlugin(&hen,&in,&out);}
    Result get_status(u64 operation,HenPluginReloadStatus& out){return henGetPluginReloadStatus(&hen,operation,&out);}
} reload_transport;
void sync_reload_ui(){
    ui.reload_available=!foreground&&reload.can_submit();ui.reload_busy=reload.active;
    ui.reload_error=reload.timed_out||reload.status.phase==HenPluginReloadPhase_Failed||reload.status.phase==HenPluginReloadPhase_Quarantined;
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
    service_missing=false;modern_hen=false;
    if(serviceIsActive(&service))serviceClose(&service);
    Result rc=open_hen();if(R_FAILED(rc))return rc;
    HenStatus status{};rc=henGetStatus(&hen,&status);if(R_FAILED(rc))return rc;
    modern_hen=status.protocol_version>=7;
    if(modern_hen){
        HenReloadCapabilities caps{};rc=henGetReloadCapabilities(&hen,&caps);
        if(R_FAILED(rc)){reload.detail=moader_native::ReloadClient::transport_text("Could not inspect kaiHEN reload capability",rc);sync_reload_ui();return rc;}
        reload.supported=caps.version>=1&&(caps.flags&HEN_PLUGIN_RELOAD_AVAILABLE);reload.manager_flags=caps.flags;
        HenPluginInfo info{};rc=reload_transport.get_info(info);
        if(R_FAILED(rc)){
            reload.info_valid=false;reload.detail=moader_native::ReloadClient::transport_text("Could not inspect managed Moader; foreground fallback paused",rc);sync_reload_ui();return rc;
        }
        reload.observe(info);
        if(info.operation_id){HenPluginReloadStatus operation{};
            if(R_SUCCEEDED(henGetPluginReloadStatus(&hen,info.operation_id,&operation))&&operation.instance_id==info.instance_id)
                reload.status=operation;
        }else reload.status={};
        if(moader_native::ReloadClient::unmanaged(info)){service_missing=true;sync_reload_ui();return MAKERESULT(Module_Libnx,LibnxError_NotFound);}
        if(!moader_native::ReloadClient::attachable(info)){
            reload.detail="kaiHEN is reloading Moader or has blocked this plugin. Foreground fallback is paused; press Y to check again.";
            if(info.operation_id){HenPluginReloadStatus operation{};
                if(R_SUCCEEDED(henGetPluginReloadStatus(&hen,info.operation_id,&operation))&&
                   (operation.phase==HenPluginReloadPhase_Failed||operation.phase==HenPluginReloadPhase_Quarantined))
                    reload.detail=moader_native::ReloadClient::result_text("Resident reload failed; foreground fallback paused",operation.result,operation.restoration_result);
            }
            sync_reload_ui();return MAKERESULT(Module_Libnx,LibnxError_BadInput);
        }
        reload.detail=reload.supported?"Reload restarts the resident Moader plugin and verifies the new process before reconnecting.":"Install the new kaiHEN build with plugin reload enabled.";
        const auto name=moader_name();rc=henOpenPlugin(&hen,&name,&service);sync_reload_ui();return rc;
    }
    reload.unavailable(false);sync_reload_ui();
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
void save_debug(){if(ui.connected)refresh_debug();
    std::ofstream file("sdmc:/moader/app-diagnostics.txt",std::ios::binary);
    file<<"Moader 0.1.0-dev\nMode: "<<(foreground?"foreground":"resident")<<"\nControl service reachable: "<<ui.connected<<"\nSummary freshness: "<<(ui.connected?"current":"last-known; service disconnected")<<"\nLibrary generation: "<<ui.summary.generation
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
    for(const auto& line:ui.events)file<<line<<'\n';
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
    ui.mods=std::move(mods);ui.regroup(old_game);ui.address=address();
    if(was_pending)message("");
    ui.runtime_detail=text_chunks(moader::ipc::RuntimeDetail,0,0,latest.detail);refresh_debug();ui.connected=true;refresh_game_source();return 0;
}
void update(Result& connection){const auto rc=refresh();if(R_FAILED(rc)){connection=rc;ui.connected=false;ui.confirm_remove=false;error(rc);}}
void begin_reload(){
    if(foreground){reload.unavailable(true);sync_reload_ui();message(reload.detail,true);return;}
    if(reload.begin(reload_transport,monotonic_ms())){
        // HEN owns the operation. The old direct session is expected to die.
        if(serviceIsActive(&service))serviceClose(&service);
        ui.connected=false;ui.confirm_remove=false;ui.game_source_available=false;
        ui.inventory_refresh_pending=true;ui.message.clear();
    }else message(reload.detail,true);
    sync_reload_ui();
}
void poll_reload(Result& connection){
    if(!reload.active)return;
    reload.poll(reload_transport,monotonic_ms());sync_reload_ui();
    if(reload.ready){
        // Check publication again immediately before opening a fresh session.
        HenPluginInfo current{};const auto checked=reload_transport.get_info(current);
        if(R_SUCCEEDED(checked)&&moader_native::ReloadClient::attachable(current)&&
           current.instance_id==reload.request.instance_id&&current.operation_id==reload.status.operation_id&&
           current.process_id==reload.status.new_process_id&&current.generation==reload.status.generation){
            const auto name=moader_name();connection=henOpenPlugin(&hen,&name,&service);
            if(R_SUCCEEDED(connection))connection=refresh();
            if(R_SUCCEEDED(connection)){
                reload.connected();sync_reload_ui();message(reload.detail);
            }else {
                ui.connected=false;if(serviceIsActive(&service))serviceClose(&service);
                // The operation remains query-only; reconnect retries never submit.
                reload.ready=false;reload.detail=moader_native::ReloadClient::transport_text("New process is healthy but the service could not be opened",connection);sync_reload_ui();
            }
        }else reload.ready=false;
    }else if(!reload.active){message(reload.detail,true);}
}

}
int main(){
    Framebuffer framebuffer{};Result rc=framebufferCreate(&framebuffer,nwindowGetDefault(),1280,720,PIXEL_FORMAT_RGBA_8888,2);
    if(R_FAILED(rc))return 1;
    rc=framebufferMakeLinear(&framebuffer);if(R_FAILED(rc)){framebufferClose(&framebuffer);return 1;}
    padConfigureInput(1,HidNpadStyleSet_NpadStandard);PadState pad;padInitializeDefault(&pad);
    moader_ui::Renderer renderer;Result connection=establish();if(R_SUCCEEDED(connection))connection=refresh();
    if(R_FAILED(connection)){ui.connected=false;if(modern_hen)message(reload.detail,true);else error(connection);}
    u64 next_refresh=0,last_tick=armGetSystemTick(),repeat_at=0;u64 previous_direction=0;
    while(appletMainLoop()){
        padUpdate(&pad);u64 down=padGetButtonsDown(&pad);const u64 held=padGetButtons(&pad),now=armGetSystemTick();
        if(down&HidNpadButton_Plus)break;
        poll_reload(connection);
        constexpr u64 directions=HidNpadButton_Up|HidNpadButton_Down|HidNpadButton_Left|HidNpadButton_Right;
        const u64 direction=held&directions;
        if(direction!=previous_direction){repeat_at=now+armNsToTicks(350000000);previous_direction=direction;}
        else if(direction&&now>=repeat_at){down|=direction;repeat_at=now+armNsToTicks(110000000);}
        uint32_t local=0;
        if(down&HidNpadButton_B)local|=moader_ui::BackInput;
        if(down&HidNpadButton_ZL)local|=moader_ui::DebugInput;
        if(down&HidNpadButton_Up)local|=moader_ui::UpInput;
        if(down&HidNpadButton_Down)local|=moader_ui::DownInput;
        if(down&HidNpadButton_A)local|=moader_ui::ActivateInput;
        if(down&HidNpadButton_StickL)local|=moader_ui::SettingsInput;
        if(down&HidNpadButton_Left)local|=moader_ui::LeftInput;
        if(down&HidNpadButton_Right)local|=moader_ui::RightInput;
        const auto local_actions=ui.local_input(local,renderer.scroll_limit(ui,moader_ui::View::Debug));
        if((local&moader_ui::SettingsInput)&&ui.view==moader_ui::View::Settings)refresh_game_source();
        if(local_actions&moader_ui::RefreshEvents)refresh_debug();
        if(local_actions&moader_ui::SaveReport)save_debug();
        if(local_actions&moader_ui::SavePreferences)save_preferences();
        if(local_actions&moader_ui::ReloadService)begin_reload();
        if(local_actions&moader_ui::SaveGameSource){
            const moader::ipc::Selection input{ui.summary.generation,ui.selected(),ui.game_source};
            const auto result=sendIn(moader::ipc::SetGameSource,input);
            if(R_FAILED(result))error(result);else message("Game copy saved. Digital selection does not verify ownership.");
            update(connection);
        }
        if(!ui.connected){if(down&HidNpadButton_Y){
            if(reload.active){reload.resume(monotonic_ms());sync_reload_ui();message("Checking the existing reload operation. No new request was sent.");}
            else {message("");connection=establish();if(R_SUCCEEDED(connection))update(connection);else if(modern_hen)message(reload.detail,true);else error(connection);}
        }}
        else{
            if(down&HidNpadButton_ZR){ui.view=ui.view==moader_ui::View::Connection?moader_ui::View::Library:moader_ui::View::Connection;ui.confirm_remove=false;if(ui.view==moader_ui::View::Connection)pairing();}
            if(down&HidNpadButton_Y){u32 enabled=ui.summary.flags&moader::ipc::WebRunning?0:1;const auto result=sendIn(moader::ipc::WebEnabled,enabled);
                if(R_FAILED(result))error(result);else message(enabled?"Web interface started. ZR shows the address and pairing code.":"Web interface stopped.");
                update(connection);if(ui.view==moader_ui::View::Connection&&enabled)pairing();}
            if(ui.view==moader_ui::View::Details){
                if(down&HidNpadButton_Up)ui.detail_first=ui.detail_first?ui.detail_first-1:0;
                if(down&HidNpadButton_Down)ui.detail_first=std::min(ui.detail_first+1,renderer.scroll_limit(ui,moader_ui::View::Details));
            }else if(ui.view==moader_ui::View::Library){
                if(down&HidNpadButton_Left){ui.rail_focus=true;ui.confirm_remove=false;}
                if(down&HidNpadButton_Right){ui.rail_focus=false;ui.confirm_remove=false;}
                if(down&HidNpadButton_L)ui.move_game(-1);
                if(down&HidNpadButton_R)ui.move_game(1);
                if(down&HidNpadButton_Up){if(ui.rail_focus)ui.move_game(-1);else ui.move_row(-1);}
                if(down&HidNpadButton_Down){if(ui.rail_focus)ui.move_game(1);else ui.move_row(1);}
                if((down&HidNpadButton_StickR)&&ui.selected()<ui.mods.size()){ui.view=moader_ui::View::Details;ui.detail_first=0;ui.confirm_remove=false;}
                if((down&HidNpadButton_A)&&ui.rail_focus)ui.rail_focus=false;
                else if((down&HidNpadButton_A)&&ui.selected()<ui.mods.size()){
                    if(ui.inventory_unavailable())message("Library inventory is unavailable. Changes are paused; inspect Debug and wait for a successful refresh.",true);
                    else {
                    const moader::ipc::Selection input{ui.summary.generation,ui.selected(),ui.mods[ui.selected()].data.flags&moader::ipc::Requested?0u:1u};
                    const auto result=sendIn(moader::ipc::SetEnabled,input);if(R_FAILED(result))error(result);else message("Request saved. Apply state shows the runtime acknowledgement.");
                    ui.confirm_remove=false;update(connection);
                    }
                }
                if((down&HidNpadButton_X)&&!ui.rail_focus&&ui.selected()<ui.mods.size()){
                    if(ui.inventory_unavailable())message("Library inventory is unavailable. Removal is paused until refresh succeeds.",true);
                    else if(ui.confirm_remove){const moader::ipc::Selection input{ui.summary.generation,ui.selected(),0};const auto result=sendIn(moader::ipc::Remove,input);
                        if(R_FAILED(result))error(result);else message("Removed from the library. Files remain in recovery storage.");ui.confirm_remove=false;update(connection);
                    }else ui.confirm_remove=true;
                }
            }
            if(now>=next_refresh){next_refresh=now+armNsToTicks(2000000000);update(connection);}
        }
        u32 stride=0;auto* pixels=static_cast<uint32_t*>(framebufferBegin(&framebuffer,&stride));
        if(!pixels)break;
        renderer.draw(pixels,1280,720,stride,ui,float(armTicksToNs(now-last_tick))/1000000000.f);last_tick=now;
        if((down&HidNpadButton_Minus)||(local_actions&moader_ui::SaveScreenshot))save_screenshot(pixels,stride);
        framebufferEnd(&framebuffer);
    }
    foreground.reset();if(serviceIsActive(&service))serviceClose(&service);if(serviceIsActive(&hen))serviceClose(&hen);framebufferClose(&framebuffer);return 0;
}
