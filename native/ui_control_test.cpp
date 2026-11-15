// Host test of the ACTUAL native control dispatcher with only Switch primitives stubbed.
#include "control.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <unistd.h>
int main(){
    static_assert(sizeof(moader::ipc::SummaryReply)==64&&sizeof(moader::ipc::ModReply)==64);
    const auto root=std::filesystem::canonical(std::filesystem::temp_directory_path())/("moader-native-control-"+std::to_string(getpid()));
    std::filesystem::create_directories(root/"library/0400f43008c44001/fixture/romfs");
    {std::ofstream f(root/"library/0400f43008c44001/fixture/romfs/test.bin");f<<"fixture";}
    std::string name(62,'a');name+="é — café ";name+=std::string(40,'b');
    {std::ofstream f(root/"library/0400f43008c44001/fixture/mod.meta");f<<name;}
    const std::string game_name="Synthetic installed game "+std::string(80,'x')+" café";
    moader::UnavailableBackend backend;moader::Library library(root.string(),backend,[&](const std::string&){return game_name;});library.synchronize();
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
    std::filesystem::remove_all(root);std::cout<<"Native dispatcher: 64-byte ABI, UTF-8 chunk reassembly, leading-zero code, events, offset bounds, stale generations, preference persistence/validation, stale-client masked updates and game-name chunks passed.\n";
}
