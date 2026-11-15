// Host test of the ACTUAL native control dispatcher with only Switch primitives stubbed.
#include "control.hpp"
#include "ui.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <unistd.h>
class TestBackend final : public moader::Backend {
public:
    moader::RuntimeStatus state{true,0,"fixture","FS fixture accepted routing.",0};
    moader::RuntimeStatus status() const override { return state; }
    void publish(uint64_t generation,const std::vector<moader::Mod>&) override { state.applied_generation=generation; }
};
int main(){
    static_assert(sizeof(moader::ipc::SummaryReply)==64&&sizeof(moader::ipc::ModReply)==64);
    static_assert(moader::ipc::Version==1&&moader::ipc::GetGameName==13&&moader::ipc::GetGameSource==14&&moader::ipc::SetGameSource==15);
    const auto root=std::filesystem::canonical(std::filesystem::temp_directory_path())/("moader-native-control-"+std::to_string(getpid()));
    std::filesystem::create_directories(root/"library/0400f43008c44001/fixture/romfs");
    {std::ofstream f(root/"library/0400f43008c44001/fixture/romfs/test.bin");f<<"fixture";}
    std::string name(62,'a');name+="é — café ";name+=std::string(40,'b');
    {std::ofstream f(root/"library/0400f43008c44001/fixture/mod.meta");f<<name;}
    const std::string game_name="Synthetic installed game "+std::string(80,'x')+" café";
    TestBackend backend;moader::Library library(root.string(),backend,[&](const std::string&){return game_name;});library.synchronize();
    const std::string token="000042";moader::WebServer web(library,root.string(),token,{});moader_native::Context context{library,web,token};
    auto call=[&](u32 id,const void* input,size_t size,void* out){size_t produced=0;auto rc=moader_native::command(id,input,size,out,&produced,&context);assert(produced<=64);return rc;};
    moader::ipc::SummaryReply summary{};assert(call(moader::ipc::Summary,nullptr,0,&summary)==0&&summary.mod_count==1);
    moader::ipc::Selection selected{summary.generation,0,0};moader::ipc::ModReply mod{};assert(call(moader::ipc::GetMod,&selected,sizeof(selected),&mod)==0);
    std::string assembled;for(unsigned offset=0;;offset+=63){selected.value=(moader::ipc::NameText<<16)|offset;char chunk[64]{};assert(call(moader::ipc::GetModText,&selected,sizeof(selected),chunk)==0);size_t n=strnlen(chunk,64);assert(n<=63);assembled.append(chunk,n);if(n<63)break;}
    assert(assembled==name); // First UTF-8 character intentionally crosses a 63-byte boundary.
    assembled.clear();for(unsigned offset=0;;offset+=63){selected.value=offset;char chunk[64]{};assert(call(moader::ipc::GetGameName,&selected,sizeof(selected),chunk)==0);size_t n=strnlen(chunk,64);assert(n<=63);assembled.append(chunk,n);if(n<63)break;}assert(assembled==game_name);
    moader::ipc::PreferencesReply prefs{};assert(call(moader::ipc::GetPreferences,nullptr,0,&prefs)==0&&prefs.flags==9&&prefs.port==7712);
    const uint64_t generation=library.generation();prefs={15,8123};assert(call(moader::ipc::SetPreferences,&prefs,sizeof(prefs),nullptr)==0&&library.generation()==generation);
    moader::ipc::PreferencesReply saved{};assert(call(moader::ipc::GetPreferences,nullptr,0,&saved)==0&&saved.flags==15&&saved.port==8123);
    moader::Library reopened(root.string(),backend);auto disk=reopened.preferences();assert(disk.game_names&&disk.reduced_motion&&disk.web_on_start&&disk.follow_log&&disk.web_port==8123);
    for(unsigned invalid:{1023u,7713u,65536u}){prefs.port=invalid;assert(call(moader::ipc::SetPreferences,&prefs,sizeof(prefs),nullptr)!=0);}
    prefs={16,8123};assert(call(moader::ipc::SetPreferences,&prefs,sizeof(prefs),nullptr)!=0);
    assert(call(moader::ipc::Summary,nullptr,0,&summary)==0&&summary.port==8123); // Listener off reports preferred next-start port.
    // A stale native client toggles only names after another client changes port/log.
    auto newer=library.preferences();newer.web_port=9001;newer.follow_log=false;library.update_preferences(newer,moader::ipc::PortField|moader::ipc::FollowField);
    moader::ipc::PreferencesRequest stale{saved.flags&~moader::ipc::GameNames,saved.port,moader::ipc::NamesField};
    assert(call(moader::ipc::SetPreferences,&stale,sizeof(stale),nullptr)==0);
    assert(call(moader::ipc::GetPreferences,nullptr,0,&saved)==0&&!(saved.flags&moader::ipc::GameNames)&&!(saved.flags&moader::ipc::FollowLog)&&saved.port==9001);
    for(unsigned invalid_mask:{0u,32u,0xffffffffu}){stale.mask=invalid_mask;assert(call(moader::ipc::SetPreferences,&stale,sizeof(stale),nullptr)!=0);}
    stale.mask=moader::ipc::PortField;stale.port=65536;assert(call(moader::ipc::SetPreferences,&stale,sizeof(stale),nullptr)!=0);
    char code[64]{};assert(call(moader::ipc::PairingCode,nullptr,0,code)==0&&std::string(code)=="000042");
    library.set_enabled("0400f43008c44001","fixture",true);
    assert(call(moader::ipc::Summary,nullptr,0,&summary)==0);selected.generation=summary.generation;
    u32 events=0;assert(call(moader::ipc::DiagnosticsInfo,nullptr,0,&events)==0&&events>0);
    selected.value=0;selected.index=0;char event[64]{};assert(call(moader::ipc::GetDiagnostic,&selected,sizeof(selected),event)==0&&event[0]);
    selected.index=events;assert(call(moader::ipc::GetDiagnostic,&selected,sizeof(selected),event)!=0);
    selected.index=0;selected.value=0xffff;assert(call(moader::ipc::GetModText,&selected,sizeof(selected),event)!=0);
    selected.generation=summary.generation+1;selected.value=0;assert(call(moader::ipc::GetMod,&selected,sizeof(selected),&mod)!=0);
    // A stale selection remains an operation error, never a backend failure.
    assert(context.selection_error&&context.last_result!=0&&!context.error.empty());
    char last[64]{};assert(call(moader::ipc::LastError,nullptr,0,last)==0&&std::string(last).find("Library changed")!=std::string::npos);
    assert(call(moader::ipc::Summary,nullptr,0,&summary)==0&&summary.last_result==0&&(summary.flags&moader::ipc::BackendConnected));
    assert(context.selection_error); // Summary is observational and keeps LastError available.
    selected.generation=summary.generation;
    assert(call(moader::ipc::GetMod,&selected,sizeof(selected),&mod)==0);
    assert(!context.selection_error&&context.error.empty()&&context.last_result==0);
    // Genuine runtime failures survive unrelated operation state and success.
    backend.state.result=0x1234;backend.state.detail="FS fixture rejected the catalogue.";
    context.last_result=moader_native::BadInput;context.error="Unrelated operation failure";
    context.selection_error=true; // Simulate a newer service error replacing the stale one.
    assert(call(moader::ipc::Summary,nullptr,0,&summary)==0&&summary.last_result==0x1234);
    assert(call(moader::ipc::GetMod,&selected,sizeof(selected),&mod)==0);
    assert(context.error=="Unrelated operation failure"&&!context.selection_error);
    assert(call(moader::ipc::Summary,nullptr,0,&summary)==0&&summary.last_result==0x1234);
    moader::ipc::PreferencesRequest valid_preferences{9,9001,moader::ipc::NamesField};
    assert(call(moader::ipc::SetPreferences,&valid_preferences,sizeof(valid_preferences),nullptr)==0&&context.error.empty());
    assert(call(moader::ipc::Summary,nullptr,0,&summary)==0&&summary.last_result==0x1234);
    backend.state.result=0;backend.state.detail="FS fixture accepted routing.";
    // Actual rescan failure retains the prior complete inventory and exposes a
    // separate additive flag, including when inventory_ready remains true.
    std::filesystem::rename(root/"library",root/"saved-library");
    {std::ofstream broken(root/"library");broken<<"synthetic invalid directory";}
    bool scan_rejected=false;try { library.rescan(); } catch(const std::exception&) { scan_rejected=true; }
    assert(scan_rejected);
    assert(call(moader::ipc::Summary,nullptr,0,&summary)==0&&summary.mod_count==1&&summary.last_result==0);
    assert(summary.flags&moader::ipc::InventoryUnavailable);
    assert(context.cached->inventory_ready&&!context.cached->inventory_error.empty());
    std::string inventory_text;
    for(unsigned offset=0;;offset+=63){selected.value=offset;char chunk[64]{};assert(call(moader::ipc::RuntimeDetail,&selected,sizeof(selected),chunk)==0);size_t n=strnlen(chunk,64);inventory_text.append(chunk,n);if(n<63)break;}
    assert(inventory_text.find("Library inventory unavailable")!=std::string::npos&&inventory_text.find("FS fixture accepted routing")!=std::string::npos);
    std::filesystem::remove(root/"library");std::filesystem::rename(root/"saved-library",root/"library");assert(library.rescan());
    assert(call(moader::ipc::Summary,nullptr,0,&summary)==0&&!(summary.flags&moader::ipc::InventoryUnavailable));
    // No successful initial inventory must not masquerade as first-upload
    // onboarding, even though it legitimately has no cached mod rows yet.
    {std::ofstream broken(root/"library/0400f43008c44001/fixture/mod.meta");broken<<std::string(121,'x');}
    moader::Library initial_failed(root.string(),backend);
    moader::WebServer initial_web(initial_failed,root.string(),token,{});
    moader_native::Context initial_context{initial_failed,initial_web,token};
    moader::ipc::SummaryReply initial_summary{};size_t initial_size=0;
    assert(moader_native::command(moader::ipc::Summary,nullptr,0,&initial_summary,&initial_size,&initial_context)==0);
    assert((initial_summary.flags&moader::ipc::InventoryUnavailable)&&initial_summary.mod_count==0&&initial_summary.last_result==0);
    assert(!initial_context.cached->inventory_ready);
    {std::ofstream restored(root/"library/0400f43008c44001/fixture/mod.meta");restored<<name;}
    // Physical-card eligibility is a per-game pause, not an FS/backend error.
    static_assert(static_cast<unsigned>(moader::ApplyState::Unavailable)==6);
    moader::Library game_blocked(root.string(),backend,{},[](const std::string&,moader::GameSource){return moader::GameAvailability{false,"Insert the physical game card. Mods are paused."};});
    moader::WebServer blocked_web(game_blocked,root.string(),token,{});
    moader_native::Context blocked_context{game_blocked,blocked_web,token};
    moader::ipc::SummaryReply blocked_summary{};size_t blocked_size=0;
    assert(moader_native::command(moader::ipc::Summary,nullptr,0,&blocked_summary,&blocked_size,&blocked_context)==0);
    assert(blocked_summary.last_result==0&&!(blocked_summary.flags&moader::ipc::InventoryUnavailable));
    moader::ipc::Selection blocked_selection{blocked_summary.generation,0,0};moader::ipc::ModReply blocked_mod{};
    assert(moader_native::command(moader::ipc::GetMod,&blocked_selection,sizeof(blocked_selection),&blocked_mod,&blocked_size,&blocked_context)==0);
    assert(blocked_mod.applied==6&&(blocked_mod.flags&moader::ipc::Requested));
    blocked_selection.value=moader::ipc::DetailText<<16;char blocked_detail[64]{};
    assert(moader_native::command(moader::ipc::GetModText,&blocked_selection,sizeof(blocked_selection),blocked_detail,&blocked_size,&blocked_context)==0);
    assert(std::string(blocked_detail).find("physical game card")!=std::string::npos);
    // Actual UI state recovery keeps list/index generation coherent, updates
    // live runtime errors, and blocks changes while a selection race retries.
    moader_ui::State view;view.summary=summary;view.mods.emplace_back();view.mods[0].id="retained";view.confirm_remove=true;
    auto newer_summary=summary;newer_summary.generation+=1;newer_summary.mod_count=0;newer_summary.last_result=0x5678;
    view.retain_inventory(newer_summary);
    assert(view.connected&&view.inventory_unavailable()&&!view.confirm_remove);
    assert(view.mods.size()==1&&view.mods[0].id=="retained"&&view.summary.generation==summary.generation&&view.summary.mod_count==1);
    assert(view.summary.last_result==0x5678); // Inventory recovery never hides a genuine FS error.
    view.inventory_refresh_pending=false;view.summary.flags|=moader::ipc::InventoryUnavailable;assert(view.inventory_unavailable());
    view.summary.flags&=~moader::ipc::InventoryUnavailable;assert(!view.inventory_unavailable());
    // Real game-copy dispatcher: four-byte query, title-scoped persistence,
    // enum/index/input/generation validation, and no false runtime rejection.
    assert(call(moader::ipc::Summary,nullptr,0,&summary)==0);
    moader::ipc::Selection copy{summary.generation,0,0};u32 source=99;
    size_t source_size=0;
    assert(moader_native::command(moader::ipc::GetGameSource,&copy,sizeof(copy),&source,&source_size,&context)==0&&source_size==4&&source==moader::ipc::CopyUnspecified);
    copy.value=1;assert(call(moader::ipc::GetGameSource,&copy,sizeof(copy),&source)!=0);copy.value=0;
    assert(call(moader::ipc::SetGameSource,&copy,sizeof(copy)-1,nullptr)!=0);
    copy.index=summary.mod_count;assert(call(moader::ipc::SetGameSource,&copy,sizeof(copy),nullptr)!=0);copy.index=0;
    copy.value=3;assert(call(moader::ipc::SetGameSource,&copy,sizeof(copy),nullptr)!=0);
    assert(library.game_source("0400f43008c44001")==moader::GameSource::Unspecified);
    copy.value=moader::ipc::CopyCartridge;copy.generation=summary.generation+1;
    assert(call(moader::ipc::SetGameSource,&copy,sizeof(copy),nullptr)!=0);
    copy.generation=summary.generation;assert(call(moader::ipc::SetGameSource,&copy,sizeof(copy),nullptr)==0&&context.error.empty());
    assert(library.game_source("0400f43008c44001")==moader::GameSource::Cartridge);
    moader::Library persisted(root.string(),backend);assert(persisted.game_source("0400f43008c44001")==moader::GameSource::Cartridge);
    assert(call(moader::ipc::SetGameSource,&copy,sizeof(copy),nullptr)!=0); // Old generation after save.
    assert(call(moader::ipc::Summary,nullptr,0,&summary)==0&&summary.last_result==0);
    copy.generation=summary.generation;copy.value=0;assert(call(moader::ipc::GetGameSource,&copy,sizeof(copy),&source)==0&&source==moader::ipc::CopyCartridge);
    copy.value=moader::ipc::CopyDigital;assert(call(moader::ipc::SetGameSource,&copy,sizeof(copy),nullptr)==0);
    moader::Library digital_persisted(root.string(),backend);assert(digital_persisted.game_source("0400f43008c44001")==moader::GameSource::Digital);
    assert(call(moader::ipc::Summary,nullptr,0,&summary)==0);copy.generation=summary.generation;copy.value=0;
    assert(call(moader::ipc::SetGameSource,&copy,sizeof(copy),nullptr)==0);
    moader::Library reset_persisted(root.string(),backend);assert(reset_persisted.game_source("0400f43008c44001")==moader::GameSource::Unspecified);
    std::filesystem::remove_all(root);std::cout<<"Native dispatcher: 64-byte ABI, UTF-8 chunks, preferences, operation/runtime error separation, inventory/UI retention, game availability, and game-copy selection/persistence/bounds passed.\n";
}
