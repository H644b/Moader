// Synthetic host captures of the SAME software renderer shipped in Moader.nro.
// No capture here represents Switch execution or verified game routing.
#include "ui.hpp"
#include "ui_bitmap.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
using namespace moader_ui;
namespace {
State fixture(){
    State s;s.connected=true;s.preferences_available=true;s.address="http://192.0.2.10:7712";s.pairing="000042";
    s.summary.flags=moader::ipc::WebRunning|moader::ipc::Foreground;s.summary.generation=12;s.summary.applied_generation=0;
    const char* names[]={"Lumiose texture pack","Evening lighting","Battle camera adjustments","High-resolution signage","Pokémon music restoration","A very long mod name with Unicode — café résumé texture restoration"};
    for(unsigned i=0;i<6;++i){Mod m;m.data.title_id=i==5?0x0100123400001000ULL:0x0400F43008C44001ULL;m.data.bytes=(i+1)*2457600;m.data.file_count=24+i*8;m.data.flags=moader::ipc::Romfs|(i<2?moader::ipc::Requested:0);m.data.applied=i<2?1:0;m.name=names[i];m.id="synthetic-fixture-"+std::to_string(i);
        m.detail=i<2?"Enable request saved. Waiting for the live filesystem backend to acknowledge this generation.":"Disabled by request. Original game files remain in use.";s.mods.push_back(m);}
    s.regroup();s.events={"SYNTHETIC HOST FIXTURE", "Library generation 12 saved.","Lumiose texture pack: enable requested.","Backend not attached. Requests remain pending.","Web listener started on port 7712."};
    s.runtime_detail="The live filesystem backend has not been attached.";return s;
}
void capture(const std::filesystem::path& file,const State& s,int w=1280,int h=720){
    std::vector<uint32_t> pixels(w*h);Renderer renderer;for(int i=0;i<45;++i)renderer.draw(pixels.data(),w,h,w*4,s,1.f/60);
    std::ofstream out(file,std::ios::binary);out<<"P6\n"<<w<<" "<<h<<"\n255\n";
    for(auto p:pixels){out.put(p&255);out.put((p>>8)&255);out.put((p>>16)&255);}out.close();if(!out)throw std::runtime_error("capture write failed");
}
}
int main(int argc,char** argv){
    std::filesystem::path dir=argc>1?argv[1]:"native/review";std::filesystem::create_directories(dir);
    auto s=fixture();assert(s.games.size()==2&&s.selected()==0);s.move_game(1);assert(s.selected()==5);s.move_game(-1);s.move_row(-1);assert(s.selected()==4);s.move_row(1);assert(s.selected()==0);
    capture(dir/"library-720.ppm",s);capture(dir/"library-docked-1080.ppm",s,1920,1080);
    s.confirm_remove=true;capture(dir/"remove-720.ppm",s);s.confirm_remove=false;
    s.view=View::Connection;capture(dir/"connection-720.ppm",s);s.summary.flags&=~moader::ipc::WebRunning;capture(dir/"web-off-720.ppm",s);s.summary.flags|=moader::ipc::WebRunning;
    s.view=View::Debug;capture(dir/"debug-720.ppm",s);
    s.view=View::Library;s.summary.flags|=moader::ipc::BackendConnected;s.summary.last_result=0x1234;s.runtime_detail="Runtime rejected this generation. Check the event history and report before trying again.";capture(dir/"runtime-error-720.ppm",s);
    s=fixture();s.mods.clear();s.regroup();capture(dir/"empty-720.ppm",s);
    s.connected=false;s.message="Service disconnected. Press Y to reconnect.";s.message_error=true;capture(dir/"disconnected-720.ppm",s);
    s=fixture();s.connected=false;s.summary.flags|=moader::ipc::BackendConnected;
    s.message="Service disconnected. Displayed values are last-known evidence.";s.message_error=true;
    s.preferences.flags&=~moader::ipc::FollowLog;
    for(unsigned i=0;i<20;++i)s.events.push_back("Historical library event "+std::to_string(i));
    Renderer recovery;const unsigned recovery_limit=recovery.scroll_limit(s,View::Debug);assert(recovery_limit>3);
    capture(dir/"disconnected-after-live-720.ppm",s);
    assert(s.local_input(DebugInput,recovery_limit)==0&&s.view==View::Debug); // Never asks IPC to refresh after loss.
    assert(s.local_input(DownInput,recovery_limit)==0&&s.debug_first==3);
    assert(s.local_input(ActivateInput,recovery_limit)==SaveReport); // Local report remains available.
    capture(dir/"disconnected-debug-720.ppm",s);
    assert(s.local_input(BackInput,recovery_limit)==0&&s.view==View::Library);
    s=fixture();s.game=1;s.view=View::Details;s.mods[5].detail=std::string(180,'x')+"\nLong descriptions, identifiers and paths wrap within the detail pane.\n"+std::string(900,'W');capture(dir/"long-details-720.ppm",s);s.detail_first=18;capture(dir/"details-scrolled-720.ppm",s);
    s=fixture();s.mods[0].data.applied=3;s.mods[1].data.applied=4;s.mods[2].data.applied=2;s.mods[3].data.applied=5;s.summary.flags|=moader::ipc::BackendConnected;capture(dir/"apply-states-720.ppm",s);
    s=fixture();s.rail_focus=true;capture(dir/"game-focus-720.ppm",s);
    s=fixture();s.mods[0].game_name="Pokémon Legends: Z-A";s.regroup();capture(dir/"game-names-720.ppm",s);
    s.preferences.flags&=~moader::ipc::GameNames;capture(dir/"title-ids-720.ppm",s);
    s=fixture();assert(s.local_input(SettingsInput,0)==0&&s.view==View::Settings);capture(dir/"settings-720.ppm",s);capture(dir/"settings-docked-1080.ppm",s,1920,1080);
    assert(s.local_input(ActivateInput,0)==SavePreferences&&s.preference_mask==moader::ipc::NamesField&&!(s.preferences.flags&moader::ipc::GameNames));
    s.settings_row=1;assert(s.local_input(ActivateInput,0)==SavePreferences&&s.preference_mask==moader::ipc::MotionField&&(s.preferences.flags&moader::ipc::ReducedMotion));
    s.settings_row=2;assert(s.local_input(ActivateInput,0)==SavePreferences&&s.preference_mask==moader::ipc::FollowField&&!(s.preferences.flags&moader::ipc::FollowLog));
    s.settings_row=3;assert(s.local_input(ActivateInput,0)==SavePreferences&&s.preference_mask==moader::ipc::StartupField&&(s.preferences.flags&moader::ipc::WebOnStart));
    s.settings_row=4;assert(s.local_input(ActivateInput,0)==0&&s.view==View::Advanced);capture(dir/"advanced-720.ppm",s);
    assert(s.local_input(ActivateInput,0)==0&&s.port_edit);s.port_draft=7713;s.port_digit=4;capture(dir/"port-editor-720.ppm",s);
    assert(s.local_input(ActivateInput,0)==0&&s.port_edit&&s.message_error);capture(dir/"port-invalid-720.ppm",s);
    s.port_draft=8123;assert(s.local_input(ActivateInput,0)==SavePreferences&&s.preference_mask==moader::ipc::PortField&&!s.port_edit&&s.preferences.port==8123);
    s.message="Settings saved. The port applies on the next listener start.";s.message_error=false;
    s.advanced_row=2;assert(s.local_input(ActivateInput,0)==SaveReport);s.advanced_row=3;assert(s.local_input(ActivateInput,0)==SaveScreenshot);
    assert(s.local_input(BackInput,0)==0&&s.view==View::Settings);s.settings_row=5;assert(s.local_input(ActivateInput,0)==0&&s.view==View::Credits);capture(dir/"credits-720.ppm",s);
    s.local_input(BackInput,0);s.connected=false;capture(dir/"settings-offline-720.ppm",s);s.settings_row=0;const auto old_preferences=s.preferences;assert(s.local_input(ActivateInput,0)==0&&s.preferences.flags==old_preferences.flags);
    s=fixture();s.view=View::Advanced;s.connected=false;capture(dir/"advanced-offline-720.ppm",s);
    assert(s.local_input(ActivateInput,0)==0&&!s.port_edit);
    s=fixture();s.view=View::Advanced;s.preferences_available=false;capture(dir/"advanced-unsupported-720.ppm",s);
    assert(s.local_input(ActivateInput,0)==0&&!s.port_edit);
    s=fixture();s.view=View::Advanced;s.advanced_row=1;assert(s.local_input(ActivateInput,0)==RefreshEvents&&s.view==View::Debug&&s.debug_return==View::Advanced);capture(dir/"advanced-runtime-720.ppm",s);
    assert(s.local_input(BackInput,0)==0&&s.view==View::Advanced);
    s=fixture();s.preferences.flags|=moader::ipc::ReducedMotion;Renderer motion;std::vector<uint32_t> initial(1280*720),settled(initial.size());s.view=View::Settings;
    motion.draw(initial.data(),1280,720,1280*4,s,1.f/60);s.settings_row=4;motion.draw(initial.data(),1280,720,1280*4,s,1.f/60);for(int i=0;i<30;++i)motion.draw(settled.data(),1280,720,1280*4,s,1.f/60);assert(initial==settled);
    s=fixture();s.events.assign(30,"Synthetic follow-log event");Renderer follow;const unsigned latest=follow.scroll_limit(s,View::Debug);s.local_input(DebugInput,latest);assert(s.debug_first==latest&&!s.log_paused);s.local_input(UpInput,latest);assert(s.debug_first==latest-3&&s.log_paused);s.local_input(DownInput,latest);assert(!s.log_paused);
    s=fixture();s.rail_focus=true;
    std::vector<uint32_t> padded(1320*720,0xaabbccdd);Renderer renderer;renderer.draw(padded.data(),1280,720,1320*4,s,.016f);
    for(int y=0;y<720;++y)for(int x=1280;x<1320;++x)assert(padded[y*1320+x]==0xaabbccdd);
    auto bmp=bitmap(padded.data(),1280,720,1320*4);assert(bmp.size()==2764854&&bmp.substr(0,2)=="BM");
    std::ofstream screenshot(dir/"synthetic-screenshot.bmp",std::ios::binary);screenshot.write(bmp.data(),bmp.size());screenshot.close();
    const uint32_t colors[]={0xff0000ff,0xff00ff00,0xff123456,0xffff0000,0xffffffff,0xffabcdef};
    const auto small=bitmap(colors,2,2,12);assert(small.size()==70);assert((unsigned char)small[54]==255&&(unsigned char)small[55]==0&&(unsigned char)small[56]==0);
    assert((unsigned char)small[62]==0&&(unsigned char)small[63]==0&&(unsigned char)small[64]==255);
    auto begin=std::chrono::steady_clock::now();for(int i=0;i<60;++i)renderer.draw(padded.data(),1280,720,1320*4,s,1.f/60);
    const double sparse_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/60;
    s=fixture();s.view=View::Debug;s.preferences.flags&=~moader::ipc::FollowLog;s.events.clear();
    for(unsigned i=0;i<128;++i)s.events.push_back("SYNTHETIC MAX-HISTORY EVENT "+std::to_string(i)+" "+std::string(512,'W')+" "+std::string(440,'x'));
    const size_t initial_min_event=s.events.front().size(),initial_max_event=s.events.back().size();
    capture(dir/"dense-history-720.ppm",s);
    Renderer dense;auto frame=[&](){auto start=std::chrono::steady_clock::now();dense.draw(padded.data(),1280,720,1320*4,s,1.f/60);return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();};
    const double cold_ms=frame();const uint64_t builds=dense.event_layout_builds();assert(builds==1);
    begin=std::chrono::steady_clock::now();for(int i=0;i<60;++i){frame();dense.scroll_limit(s,View::Debug);}const double warm_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/60;
    assert(dense.event_layout_builds()==builds); // Warm frames and scroll bounds share the cached layout.
    s.events[4]+=" changed";const double changed_ms=frame();assert(dense.event_layout_builds()==builds+1);
    std::vector<uint32_t> docked(1920*1080);begin=std::chrono::steady_clock::now();dense.draw(docked.data(),1920,1080,1920*4,s,1.f/60);
    const double scale_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();assert(dense.event_layout_builds()==builds+2);
    std::ofstream performance(dir/"performance.json");performance<<"{\n  \"kind\": \"synthetic host; not Switch performance\",\n  \"events\": 128,\n  \"initial_event_bytes_min\": "<<initial_min_event<<",\n  \"initial_event_bytes_max\": "<<initial_max_event<<",\n  \"sparse_warm_frame_ms\": "<<sparse_ms<<",\n  \"dense_cold_frame_ms\": "<<cold_ms<<",\n  \"dense_warm_frame_ms\": "<<warm_ms<<",\n  \"changed_event_frame_ms\": "<<changed_ms<<",\n  \"changed_scale_frame_ms\": "<<scale_ms<<",\n  \"warm_frames\": 60,\n  \"warm_layout_builds\": 0,\n  \"event_change_layout_builds\": 1,\n  \"scale_change_layout_builds\": 1\n}\n";
    std::cout<<"Dense 128-event history: cold "<<cold_ms<<" ms; warm "<<warm_ms<<" ms; event invalidation "<<changed_ms<<" ms; scale invalidation "<<scale_ms<<" ms (host only).\n";
    std::cout<<"Navigation/grouping and padded-stride checks passed; synthetic capture matrix written.\nHost render average: "<<sparse_ms<<" ms (not device performance).\n";
}
