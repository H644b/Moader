#include "ui.hpp"
#include "native_font.hpp"
#include "../branding/mark-alpha.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb_truetype.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>

namespace moader_ui {
uint32_t State::local_input(uint32_t buttons,unsigned debug_limit) {
    uint32_t actions=0;preference_mask=0;
    const bool can_save=connected&&preferences_available;
    auto unavailable=[&](){message=connected?"This service does not support preferences. Update the resident service.":"Reconnect the service to change saved preferences.";message_error=true;};
    auto open_debug=[&](){view=View::Debug;confirm_remove=false;log_paused=false;debug_first=preferences.flags&moader::ipc::FollowLog?debug_limit:0;if(connected)actions|=RefreshEvents;};
    if(buttons&SettingsInput){view=view==View::Settings?View::Library:View::Settings;confirm_remove=false;port_edit=false;}
    if(buttons&BackInput){
        if(port_edit)port_edit=false;
        else if(view==View::Advanced||view==View::Credits)view=View::Settings;
        else if(view==View::Debug)view=debug_return;
        else view=View::Library;
        confirm_remove=false;rail_focus=false;
    }
    if(buttons&DebugInput){if(view==View::Debug)view=debug_return;else{debug_return=(view==View::Advanced||view==View::Settings)?view:View::Library;open_debug();}}
    if(view==View::Debug){
        if(buttons&UpInput){if((preferences.flags&moader::ipc::FollowLog)&&!log_paused)debug_first=debug_limit;debug_first=debug_first>3?debug_first-3:0;log_paused=true;}
        if(buttons&DownInput){debug_first=std::min(debug_first+3,debug_limit);log_paused=!(preferences.flags&moader::ipc::FollowLog)||debug_first<debug_limit;}
        if(buttons&ActivateInput)actions|=SaveReport;
    }else if(view==View::Settings){
        if(buttons&UpInput)settings_row=(settings_row+6)%7;
        if(buttons&DownInput)settings_row=(settings_row+1)%7;
        if(buttons&ActivateInput){
            if(settings_row<4){if(can_save){const uint32_t bits[]={moader::ipc::GameNames,moader::ipc::ReducedMotion,moader::ipc::FollowLog,moader::ipc::WebOnStart};const uint32_t fields[]={moader::ipc::NamesField,moader::ipc::MotionField,moader::ipc::FollowField,moader::ipc::StartupField};preferences.flags^=bits[settings_row];preference_mask=fields[settings_row];actions|=SavePreferences;}else unavailable();}
            else if(settings_row==4){
                if(can_save_game_source()){game_source=(game_source+1)%3;actions|=SaveGameSource;}
                else {message="Select a game with a current inventory and available game-copy controls.";message_error=true;}
            }else if(settings_row==5){view=View::Advanced;advanced_row=0;}else view=View::Credits;
        }
    }else if(view==View::Advanced){
        if(port_edit){
            if(buttons&LeftInput)port_digit=(port_digit+4)%5;
            if(buttons&RightInput)port_digit=(port_digit+1)%5;
            unsigned factor=1;for(unsigned i=port_digit;i<4;++i)factor*=10;
            const unsigned digit=(port_draft/factor)%10;
            if(buttons&UpInput)port_draft=port_draft-digit*factor+((digit+1)%10)*factor;
            if(buttons&DownInput)port_draft=port_draft-digit*factor+((digit+9)%10)*factor;
            if(buttons&ActivateInput){if(!can_save)unavailable();else if(port_draft<1024||port_draft>65535||port_draft==7713){message="Choose port 1024–65535, excluding runtime port 7713.";message_error=true;}
                else{preferences.port=port_draft;port_edit=false;preference_mask=moader::ipc::PortField;actions|=SavePreferences;}}
        }else{
            if(buttons&UpInput)advanced_row=(advanced_row+4)%5;
            if(buttons&DownInput)advanced_row=(advanced_row+1)%5;
            if(buttons&ActivateInput){if(advanced_row==0){if(can_save){port_draft=preferences.port;port_digit=0;port_edit=true;}else unavailable();}
                else if(advanced_row==1){debug_return=View::Advanced;open_debug();}
                else if(advanced_row==2)actions|=SaveReport;else if(advanced_row==3)actions|=SaveScreenshot;
                else if(reload_available&&!reload_busy&&connected)actions|=ReloadService;
                else {message=reload_detail;message_error=!reload_busy;}}
        }
    }
    return actions;
}
void State::regroup(uint64_t preferred) {
    if (!preferred && game < games.size()) preferred = games[game].id;
    games.clear();
    for (unsigned i=0;i<mods.size();++i) {
        auto it=std::find_if(games.begin(),games.end(),[&](const Game& g){return g.id==mods[i].data.title_id;});
        if (it==games.end()) { games.push_back({mods[i].data.title_id,{},mods[i].game_name}); it=games.end()-1; }
        it->mods.push_back(i);
    }
    game=0;
    for(unsigned i=0;i<games.size();++i) if(games[i].id==preferred) game=i;
    row=game<games.size() ? std::min(row,unsigned(games[game].mods.size()-1)) : 0;
}
void State::move_game(int delta) {
    if(games.empty()) return;
    game=(int(game)+delta+games.size())%games.size(); row=0; detail_first=0; confirm_remove=false;game_source_available=false;
}
void State::move_row(int delta) {
    if(game>=games.size()) return;
    row=(int(row)+delta+games[game].mods.size())%games[game].mods.size(); detail_first=0; confirm_remove=false;
}
unsigned State::selected() const { return game<games.size() && row<games[game].mods.size() ? games[game].mods[row] : std::numeric_limits<unsigned>::max(); }
namespace {
using Color=uint32_t;
constexpr Color rgb(unsigned c) {return ((c>>16)&255)|((c>>8)&255)<<8|(c&255)<<16|0xff000000u;}
constexpr Color canvas=rgb(0xeef2f3),surface=rgb(0xffffff),subtle=rgb(0xf6f8f9),ink=rgb(0x1e303a),muted=rgb(0x536772),line=rgb(0xdce4e8),action=rgb(0x175f79),selection=rgb(0xe8f2f6),good=rgb(0x28654e),warn=rgb(0x8a551c),bad=rgb(0xa93f38),badbg=rgb(0xfff1ef),logbg=rgb(0x182b35),logink=rgb(0xdbe9f0),logmuted=rgb(0xa3bcc9),focus=rgb(0x4d99ba);
std::string hexid(uint64_t id) {char b[20];std::snprintf(b,sizeof(b),"%016llX",(unsigned long long)id);return b;}
std::string size_text(uint64_t n) {char b[40]; if(n>=1048576) std::snprintf(b,sizeof(b),"%.1f MiB",double(n)/1048576);else std::snprintf(b,sizeof(b),"%.1f KiB",double(n)/1024);return b;}
const char* apply(unsigned s) {const char* names[]={"Disabled","Pending","Active","Reopen content","Relaunch game","Error","Game unavailable"};return s<7?names[s]:"Unknown";}
Color state_color(unsigned s) {return s==2?good:s==5?bad:s==0?muted:warn;}
unsigned codepoint(const std::string& s,size_t& i) {
    unsigned c=(unsigned char)s[i++]; if(c<128) return c;
    unsigned n=c>=240?3:c>=224?2:c>=192?1:0; unsigned v=c&((1u<<(6-n))-1);
    if(!n) return 0xfffd;
    while(n--) {if(i>=s.size() || ((unsigned char)s[i]&192)!=128) return 0xfffd;v=(v<<6)|((unsigned char)s[i++]&63);}
    return v;
}
}
struct Renderer::Impl {
    stbtt_fontinfo font{};
    struct Glyph {int w=0,h=0,x=0,y=0;float advance=0;std::vector<unsigned char> alpha;};
    std::map<uint64_t,Glyph> glyphs;
    std::vector<std::string> cached_events, cached_event_lines;
    float cached_event_scale=-1;
    uint64_t event_builds=0;
    const std::vector<std::string>& event_lines(const State& s){
        // The font and 17px / 306px diagnostic layout are fixed for this renderer.
        // Scale and source contents are the only mutable layout inputs.
        if(cached_event_scale!=scale || cached_events!=s.events){
            ++event_builds;cached_events=s.events;cached_event_scale=scale;cached_event_lines.clear();
            for(const auto& event:cached_events){auto lines=wrap(event,17,306);cached_event_lines.insert(cached_event_lines.end(),lines.begin(),lines.end());cached_event_lines.emplace_back();}
        }
        return cached_event_lines;
    }

    uint32_t* pixels=nullptr;int width=0,height=0,stride=0;float scale=1,opacity=1;
    float row_y=226,rail_y=132,menu_y=184,panel=0;View previous=View::Library;bool initialized=false;
    Impl(){stbtt_InitFont(&font,moader_font,stbtt_GetFontOffsetForIndex(moader_font,0));}
    Glyph& glyph(unsigned cp,int px) {
        const uint64_t key=(uint64_t(px)<<32)|cp;auto found=glyphs.find(key);if(found!=glyphs.end())return found->second;
        Glyph g;float fs=stbtt_ScaleForPixelHeight(&font,float(px));int advance=0;
        int index=stbtt_FindGlyphIndex(&font,cp); if(!index) index=stbtt_FindGlyphIndex(&font,0xfffd);
        stbtt_GetGlyphHMetrics(&font,index,&advance,nullptr);g.advance=advance*fs;
        unsigned char* bitmap=stbtt_GetGlyphBitmap(&font,0,fs,index,&g.w,&g.h,&g.x,&g.y);
        if(bitmap){g.alpha.assign(bitmap,bitmap+g.w*g.h);stbtt_FreeBitmap(bitmap,nullptr);}
        // Bounded cache for arbitrary uploaded names; no per-frame allocations after warmup.
        if(glyphs.size()>4096)glyphs.clear();
        return glyphs.emplace(key,std::move(g)).first->second;
    }
    void pixel(int x,int y,Color c,float alpha=1){
        if(x<0||y<0||x>=width||y>=height)return;
        unsigned a=unsigned(std::clamp(alpha*opacity,0.f,1.f)*255);if(!a)return;
        auto& d=pixels[y*stride+x]; if(a==255){d=c;return;}
        unsigned r=((c&255)*a+(d&255)*(255-a)+127)/255;
        unsigned g=(((c>>8)&255)*a+((d>>8)&255)*(255-a)+127)/255;
        unsigned b=(((c>>16)&255)*a+((d>>16)&255)*(255-a)+127)/255;d=r|(g<<8)|(b<<16)|0xff000000u;
    }
    void rect(float x,float y,float w,float h,Color c,float radius=0){
        const int x0=std::max(0,int(std::floor(x*scale))),y0=std::max(0,int(std::floor(y*scale)));
        const int x1=std::min(width,int(std::ceil((x+w)*scale))),y1=std::min(height,int(std::ceil((y+h)*scale)));
        if(radius==0 && opacity==1){for(int py=y0;py<y1;++py)std::fill(pixels+py*stride+x0,pixels+py*stride+x1,c);return;}
        float cx=(x+w/2)*scale,cy=(y+h/2)*scale,r=radius*scale;
        for(int py=y0;py<y1;++py)for(int px=x0;px<x1;++px){
            float dx=std::max(std::abs(px+.5f-cx)-(w*scale/2-r),0.f),dy=std::max(std::abs(py+.5f-cy)-(h*scale/2-r),0.f);
            pixel(px,py,c,radius?std::clamp(r+.5f-std::sqrt(dx*dx+dy*dy),0.f,1.f):1);
        }
    }
    void outline(float x,float y,float w,float h,Color c,float r=7){rect(x,y,w,h,c,r);rect(x+2,y+2,w-4,h-4,surface,std::max(0.f,r-2));}
    float measure(const std::string& text,float size){float out=0;int px=std::max(1,int(std::lround(size*scale)));for(size_t i=0;i<text.size();){out+=glyph(codepoint(text,i),px).advance/scale;}return out;}
    void text(float x,float y,const std::string& text,float size,Color c,float maxw=10000){
        int px=std::max(1,int(std::lround(size*scale)));float cursor=x*scale;
        // Top-aligned line box; Noto's ascent, not a guessed cap height, determines baseline.
        int ascent;stbtt_GetFontVMetrics(&font,&ascent,nullptr,nullptr);float baseline=y*scale+ascent*stbtt_ScaleForPixelHeight(&font,float(px));
        for(size_t i=0;i<text.size();) {auto cp=codepoint(text,i);auto& g=glyph(cp,px);if(cursor+g.advance>(x+maxw)*scale)break;
            for(int gy=0;gy<g.h;++gy)for(int gx=0;gx<g.w;++gx)pixel(int(std::lround(cursor))+g.x+gx,int(std::lround(baseline))+g.y+gy,c,g.alpha[gy*g.w+gx]/255.f);
            cursor+=g.advance;
        }
    }
    std::string ellipsis(const std::string& s,float size,float w){if(measure(s,size)<=w)return s;std::string out;for(size_t i=0;i<s.size();){size_t from=i;codepoint(s,i);auto next=out+s.substr(from,i-from);if(measure(next+"…",size)>w)break;out=next;}return out+"…";}
    std::vector<std::string> wrap(const std::string& s,float size,float w){
        std::vector<std::string> lines;std::string current,word;
        auto flushword=[&](){if(word.empty())return;if(!current.empty()&&measure(current+" "+word,size)>w){lines.push_back(current);current.clear();}
            if(measure(word,size)>w){float used=measure(current,size);int px=std::max(1,int(std::lround(size*scale)));for(size_t i=0;i<word.size();){size_t start=i;auto cp=codepoint(word,i);auto c=word.substr(start,i-start);const float advance=glyph(cp,px).advance/scale;if(used+advance>w){lines.push_back(current);current.clear();used=0;}current+=c;used+=advance;}}
            else {if(!current.empty())current+=' ';current+=word;}word.clear();};
        for(size_t i=0;i<s.size();){size_t start=i;unsigned cp=codepoint(s,i);if(cp==' '||cp=='\n'||cp=='\r'){flushword();if(cp=='\n'){lines.push_back(current);current.clear();}}else word+=s.substr(start,i-start);}
        flushword();if(!current.empty())lines.push_back(current);return lines;
    }
    void paragraph(float x,float y,const std::string& s,float size,Color c,float w,unsigned count,unsigned first=0){auto lines=wrap(s,size,w);for(unsigned i=first;i<lines.size()&&i<first+count;++i)text(x,y+(i-first)*(size+5),lines[i],size,c,w);}
    void dot(float x,float y,Color c){rect(x,y,7,7,c,3.5);}
    void status(float x,float y,const std::string& label,Color c){dot(x,y+8,c);text(x+15,y,label,19,c);}
    void key(float x,float y,const std::string& k,const std::string& label,Color fg=muted){
        float kw=std::max(27.f,measure(k,16)+12);rect(x,y,kw,27,line,5);text(x+(kw-measure(k,16))/2,y+2,k,16,ink);text(x+kw+8,y+1,label,18,fg);
    }
    void button(float x,float y,float w,const std::string& label,Color bg=selection,Color fg=action){rect(x,y,w,40,bg,7);text(x+14,y+7,label,19,fg,w-24);}
    void layers(float x,float y,Color c){for(int i=0;i<3;++i){rect(x+i*3,y+i*6,18,2,c,1);rect(x+i*3,y+i*6,2,9,c,1);rect(x+16+i*3,y+i*6,2,9,c,1);}}
    void mark(float x,float y,float size,Color color){
        using namespace moader::branding;
        const int side=std::max(1,int(std::lround(size*scale)));
        for(int dy=0;dy<side;++dy)for(int dx=0;dx<side;++dx){
            float alpha=0;
            if(kMarkWidth%side==0 && kMarkHeight%side==0){
                const int sx=kMarkWidth/side,sy=kMarkHeight/side;unsigned sum=0;
                for(int yy=0;yy<sy;++yy)for(int xx=0;xx<sx;++xx)sum+=kMarkAlpha[(dy*sy+yy)*kMarkWidth+dx*sx+xx];
                alpha=float(sum)/float(sx*sy*255);
            }else{
                const float source_x=(dx+.5f)*kMarkWidth/side-.5f,source_y=(dy+.5f)*kMarkHeight/side-.5f;
                const int x0=int(std::floor(source_x)),y0=int(std::floor(source_y));const float fx=source_x-x0,fy=source_y-y0;
                auto sample=[&](int xx,int yy){return float(kMarkAlpha[std::clamp(yy,0,kMarkHeight-1)*kMarkWidth+std::clamp(xx,0,kMarkWidth-1)]);};
                alpha=((1-fy)*((1-fx)*sample(x0,y0)+fx*sample(x0+1,y0))+fy*((1-fx)*sample(x0,y0+1)+fx*sample(x0+1,y0+1)))/255.f;
            }
            pixel(int(std::lround(x*scale))+dx,int(std::lround(y*scale))+dy,color,alpha);
        }
    }
    void diagnostics(const State& s){
        rect(920,72,360,572,logbg);text(944,94,"Runtime & debug",23,logink);
        const bool live=s.connected&&(s.summary.flags&moader::ipc::BackendConnected);
        status(944,134,!s.connected?"Runtime unavailable":s.summary.last_result?"Runtime error":live?"Routing connected":"Backend not attached",!s.connected?logmuted:s.summary.last_result?rgb(0xf2b0a5):live?rgb(0x9ad2b6):rgb(0xe6bd80));
        paragraph(944,168,!s.connected?"Service disconnected. Values and events below are last-known evidence.":s.summary.last_result?"The filesystem runtime reported a failure. Inspect the runtime result and events.":live?"Apply state follows runtime acknowledgement.":"Requests stay pending until a live backend acknowledges them.",18,logmuted,310,3);
        rect(944,248,312,1,rgb(0x38505e));text(944,264,s.view==View::Debug?((s.preferences.flags&moader::ipc::FollowLog)&&!s.log_paused?"Events · following latest":"Event history · paused"):"Recent events",19,logink);
        const auto& lines=event_lines(s);
        if(lines.empty())paragraph(944,304,"No events yet. Library changes and runtime errors appear here.",18,logmuted,302,4);
        else {unsigned begin=(s.preferences.flags&moader::ipc::FollowLog)&&!s.log_paused?(lines.size()>10?unsigned(lines.size()-10):0u):std::min(s.debug_first,lines.size()>10?unsigned(lines.size()-10):0u);
            for(unsigned i=begin;i<lines.size()&&i<begin+10;++i)text(944,302+(i-begin)*23,lines[i],17,logink,306);
            if(s.view==View::Debug)text(944,542,"Line "+std::to_string(begin+1)+" / "+std::to_string(lines.size()),16,logmuted);
        }
        rect(944,570,312,1,rgb(0x38505e));text(944,583,std::string(s.connected&&!s.inventory_unavailable()?"Library  ":"Last library  ")+std::to_string(s.summary.generation)+"    FS ack  "+std::to_string(s.summary.applied_generation),17,logmuted,310);
        char result[40];std::snprintf(result,sizeof(result),s.connected?"Runtime result  0x%08X":"Last-known result  0x%08X",s.summary.last_result);text(944,608,result,17,logmuted);

    }
    void render(const State& s,float dt){
        const bool reduced=s.preferences.flags&moader::ipc::ReducedMotion;
        float ease=reduced?1.f:1-std::exp(-std::clamp(dt,0.f,.1f)*20);
        float target=226+(s.row%4)*60.f;float targetrail=132+(s.game%5)*88.f;
        const float targetmenu=s.view==View::Settings?176+s.settings_row*55.f:184+s.advanced_row*64.f;
        if(!initialized){row_y=target;rail_y=targetrail;menu_y=targetmenu;previous=s.view;initialized=true;}
        row_y+=(target-row_y)*ease;rail_y+=(targetrail-rail_y)*ease;menu_y+=(targetmenu-menu_y)*ease;
        if(previous!=s.view){panel=reduced?0:12;menu_y=targetmenu;previous=s.view;}panel*=1-ease;
        rect(0,0,1280,720,canvas);rect(0,0,1280,72,surface);rect(0,71,1280,1,line);
        mark(24,20,32,action);text(62,17,"Moader",29,ink);text(183,27,"0.1.0-dev",16,muted);
        status(444,25,!s.connected?"Web unknown":s.summary.flags&moader::ipc::WebRunning?"Web on":"Web off",s.connected&&(s.summary.flags&moader::ipc::WebRunning)?good:muted);
        text(574,26,s.connected?s.address:"Service disconnected",18,muted,438);
        text(1056,27,!s.connected?"Service unavailable":s.summary.flags&moader::ipc::Foreground?"Foreground app":"Resident service",17,muted,200);
        rect(223,72,1,572,line);rect(224,72,696,572,surface);
        text(24,94,s.connected?"Games":"Last-known games",s.connected?22:19,ink);text(173,99,std::to_string(s.games.size()),17,muted);
        if(s.games.empty())paragraph(24,146,s.inventory_unavailable()?"Waiting for a verified library inventory.":"Your games appear here after the first mod upload.",18,muted,174,5);
        const unsigned gamefirst=s.game/5*5;
        for(unsigned i=gamefirst;i<s.games.size()&&i<gamefirst+5;++i){float y=132+(i-gamefirst)*88;
            if(i==s.game){if(s.rail_focus)outline(10,rail_y-3,204,81,focus);rect(14,y,196,75,surface,7);}
            const bool named=(s.preferences.flags&moader::ipc::GameNames)&&!s.games[i].name.empty();
            if(named){auto names=wrap(s.games[i].name,19,174);for(unsigned n=0;n<names.size()&&n<2;++n)text(26,y+5+n*23,n==1?ellipsis(names[n],19,174):names[n],19,i==s.game?action:ink,174);
                text(26,y+51,std::to_string(s.games[i].mods.size())+" installed mods",16,muted,178);}
            else{text(26,y+9,hexid(s.games[i].id),17,i==s.game?action:ink,175);text(26,y+39,std::to_string(s.games[i].mods.size())+" installed mods",18,muted,178);}

        }
        text(24,594,s.connected?"L / R  Change game":"ZL  Saved diagnostics",17,muted);
        if(!s.connected&&s.view!=View::Debug&&s.view!=View::Settings&&s.view!=View::Advanced&&s.view!=View::Credits){
            text(250,108+panel,"Moader disconnected",30,ink);paragraph(250,165+panel,"The local service is unavailable. Check kaiHEN, then press Y to reconnect.",22,muted,620,4);
            button(250,304,180,"Y  Check again");
        }else if(s.view==View::Settings){
            text(250,103+panel,"Settings",29,ink);
            text(250,147+panel,s.connected&&s.preferences_available?"Preferences are saved on this console.":"Saved preferences are unavailable. Credits and diagnostics remain open.",18,muted,626);
            const char* labels[]={"Show game names","Reduce motion","Follow latest events","Start web automatically","Game copy","Advanced","Credits"};
            const char* copy_help=s.game_source==moader::ipc::CopyDigital?"Your copy selection; ownership is not verified.":
                s.game_source==moader::ipc::CopyCartridge?"Physical cartridge or game-key card must be inserted.":"Default checks the matching card. Game-key copies use Cartridge.";
            const char* descriptions[]={"Use installed game names; fall back to Title IDs.","Keep focus and panel changes immediate.","Follow the newest events until you scroll up.","Start the listener when the Moader service opens.",copy_help,"Service reload, web port and diagnostic exports.","Project, runtime, libraries and font licenses."};
            const uint32_t bits[]={moader::ipc::GameNames,moader::ipc::ReducedMotion,moader::ipc::FollowLog,moader::ipc::WebOnStart};
            for(unsigned i=0;i<7;++i){float y=176+i*55;
                if(i==s.settings_row)rect(250,y,644,53,selection,5);
                const auto label=i==4&&s.game<s.games.size()?std::string("Game copy · ")+hexid(s.games[s.game].id):std::string(labels[i]);
                text(264,y+5,label,21,ink,450);text(264,y+31,descriptions[i],17,muted,606);
                if(i<4)text(795,y+8,s.connected&&s.preferences_available?(s.preferences.flags&bits[i]?"On":"Off"):"Unavailable",18,s.connected&&s.preferences_available?action:muted,96);
                else if(i==4){const char* choices[]={"Unspecified","Cartridge","Digital"};text(753,y+8,s.can_save_game_source()&&s.game_source<3?choices[s.game_source]:"Unavailable",18,s.can_save_game_source()?action:muted,140);}
                else text(804,y+8,"A  Open",18,action,88);
            }
            rect(249,menu_y,646,2,focus);rect(249,menu_y+51,646,2,focus);
            text(250,601,"Up / Down  Select     A  Change or open     B  Library",18,muted,630);
        }else if(s.view==View::Advanced){
            text(250,103+panel,"Advanced",29,ink);
            text(250,147+panel,"The web port changes on the next listener start.",19,muted,626);
            const char* labels[]={"Web listener port","Runtime details","Export debug report","Save current screenshot","Reload Moader service"};
            const char* descriptions[]={"A edits digits. Port 7713 is reserved for the runtime.","Inspect runtime status and scroll historical events.","Save app-diagnostics.txt, including last-known evidence.","Save app-screenshot.bmp once; no background writes.","Restart the resident plugin; wait for its runtime health check."};
            for(unsigned i=0;i<5;++i){float y=184+i*64;if(i==s.advanced_row)rect(250,y,644,61,selection,5);
                text(264,y+6,labels[i],21,ink,420);text(264,y+35,descriptions[i],17,muted,606);
                if(i==0){
                    const bool available=s.connected&&s.preferences_available;
                    if(!available)text(777,y+9,"Unavailable",18,muted,105);
                    else{char port[8];std::snprintf(port,sizeof(port),s.port_edit?"%05u":"%u",s.port_edit?s.port_draft:s.preferences.port);text(777,y+7,port,24,action,105);
                        if(s.port_edit){const std::string before(port,s.port_digit),digit(port+s.port_digit,1);rect(777+measure(before,24),y+35,measure(digit,24),2,action);}}
                }
                else if(i==4)text(765,y+8,s.reload_busy?"In progress":s.reload_available&&s.connected?"A  Reload":"Unavailable",18,s.reload_busy?warn:s.reload_available&&s.connected?action:muted,127);
                else text(805,y+8,"A  Open",18,action,88);
            }
            rect(249,menu_y,646,2,focus);rect(249,menu_y+59,646,2,focus);
            paragraph(250,510,s.reload_detail,18,s.reload_error?bad:s.reload_busy?warn:muted,625,3);
            text(250,580,s.port_edit?"Left / Right  Digit     Up / Down  Value":"Up / Down  Select     A  Open or export",18,muted);
            text(250,609,s.port_edit?"A  Save port     B  Cancel edit":"B  Settings",18,muted);
        }else if(s.view==View::Credits){
            text(250,103+panel,"Credits",29,ink);text(250,148+panel,"Moader 0.1.0-dev · built for kaiHEN on Switch 2",20,muted,625);
            const char* titles[]={"kaiHEN / ounce-devkit / libounce","ns2jb-modloader / ns2jb","devkitPro / zlib","Noto Sans Regular","stb_truetype"};
            const char* notes[]={"Homebrew runtime, system services and framebuffer.","Retained runtime foundation and acquisition research.","Compiler, platform libraries and ZIP decoding.","The Noto Project Authors · SIL Open Font License 1.1.","Pinned font rasterizer · MIT / public-domain dual license."};
            for(unsigned i=0;i<5;++i){float y=207+i*66;text(250,y,titles[i],22,ink,625);text(250,y+31,notes[i],18,muted,625);}
            text(250,564,"Original notices and source provenance are retained.",19,muted,625);
            text(250,603,"B  Settings",18,muted);
        }else if(s.view==View::Connection){
            text(250,102+panel,"Connect from your computer",29,ink);
            paragraph(250,151+panel,"Open this local address in a browser on the same network.",21,muted,624,2);
            rect(250,225+panel,644,71,subtle,10);text(268,244+panel,s.address,29,ink,607);
            text(250,324+panel,"Six-digit pairing code",21,muted);
            const bool ready=s.summary.flags&moader::ipc::WebRunning;
            text(250,356+panel,ready?s.pairing:"Web is off",ready?64:42,ready?action:muted,620);
            paragraph(250,451+panel,ready?"Enter all six digits, including any leading zero. Only share this code with trusted devices.":"Press Y to start the web interface. Your pairing code will appear here.",21,muted,622,3);
            paragraph(250,556+panel,s.summary.flags&moader::ipc::Foreground?"Keep Moader open while using the web interface.":"The resident service remains available after this app closes.",19,muted,622,2);
        }else if(s.view==View::Debug){
            text(250,103+panel,s.connected?"Runtime diagnostics":"Saved diagnostics",29,ink);
            paragraph(250,155+panel,s.connected?"Scroll the event history in the right pane. Save a report to share the current library and runtime evidence.":"The service disconnected. You can scroll the last-known events and save a local report while it is unavailable.",22,muted,622,3);
            button(250,273+panel,234,"A  Save debug report");
            text(250,341+panel,"Report location",19,muted);text(250,372+panel,"sdmc:/moader/app-diagnostics.txt",21,ink,623);
            paragraph(250,430+panel,s.runtime_detail.empty()?"No runtime detail reported.":s.runtime_detail,21,s.summary.last_result?bad:muted,622,3);
            text(250,523,"Historical events can predate this run.",18,muted);
            text(250,562,"Minus  Save screenshot to SD",19,action);
            text(250,594,"Up / Down  Scroll events     B  Back",18,muted,626);
        }else if(s.view==View::Details && s.selected()<s.mods.size()){
            const auto& m=s.mods[s.selected()];text(250,103+panel,"Mod details",29,ink);
            std::string detail=m.name+"\n\nMod ID: "+m.id+"\nTitle ID: "+hexid(m.data.title_id)+"\n"+std::to_string(m.data.file_count)+" files · "+size_text(m.data.bytes)+"\n\n"+m.detail;
            auto lines=wrap(detail,22,620);unsigned start=std::min(s.detail_first,lines.size()>15?unsigned(lines.size()-15):0u);
            for(unsigned i=start;i<lines.size()&&i<start+15;++i)text(250,157+panel+(i-start)*28,lines[i],22,ink,620);
            text(250,600,"Up / Down  Scroll details     B  Back to library",18,muted);
        }else{
            text(250,101+panel,"Mod library",29,ink);
            text(250,146+panel,s.game<s.games.size()?ellipsis((s.preferences.flags&moader::ipc::GameNames)&&!s.games[s.game].name.empty()?s.games[s.game].name:hexid(s.games[s.game].id),19,525):s.inventory_unavailable()?"Saved library is not currently available":"Upload your first mod to get started",19,muted,620);
            if(s.inventory_unavailable())text(250,170+panel,s.mods.empty()?"Library inventory unavailable":"Retained library · changes paused until refresh succeeds",17,warn,644);
            if(s.mods.empty()){
                layers(250,230,action);text(290,220,s.inventory_unavailable()?"Library unavailable":"Your library is ready",26,ink);
                paragraph(250,281,s.inventory_unavailable()?"No verified mod list is available. Saved files may still be present. Check Debug and wait for a successful inventory refresh before making changes.":"Turn on the web interface, connect from your computer, and upload a mod ZIP for its game.",23,muted,610,4);
                button(250,417,252,s.inventory_unavailable()?"ZL  Inventory diagnostics":"ZR  Connection details");
                paragraph(250,505,s.inventory_unavailable()?"An unavailable inventory is separate from the filesystem runtime status.":"Enable requests and actual runtime apply state are shown separately for every mod.",20,muted,615,3);
            }else if(s.game<s.games.size()){
                rect(250,194,644,32,subtle,4);text(262,198,"Mod",17,muted);text(606,198,"Apply state",17,muted);text(795,198,"Requested",17,muted);
                unsigned first=s.row/4*4;const auto& indexes=s.games[s.game].mods;
                for(unsigned i=first;i<indexes.size()&&i<first+4;++i){const auto& m=s.mods[indexes[i]];float y=226+(i-first)*60.f;
                    if(i==s.row){rect(250,y,644,59,selection,5);if(!s.rail_focus){rect(249,row_y,646,2,focus);rect(249,row_y+57,646,2,focus);}}
                    text(262,y+6,ellipsis(m.name,21,322),21,ink,322);
                    text(262,y+34,std::to_string(m.data.file_count)+" files · "+size_text(m.data.bytes),16,muted,322);
                    status(606,y+17,apply(m.data.applied),state_color(m.data.applied));
                    const bool on=m.data.flags&moader::ipc::Requested;rect(810,y+18,36,22,on?action:rgb(0x84949c),11);rect(on?826:813,y+21,16,16,surface,8);text(854,y+17,on?"On":"Off",17,muted,37);
                }
                const auto& m=s.mods[s.selected()];rect(250,479,644,1,line);
                text(250,490,ellipsis(m.name,22,620),22,ink,620);
                if(s.confirm_remove){rect(250,528,644,102,badbg,7);text(264,536,"Remove this mod from the library?",21,bad,610);
                    text(264,567,"Files stay in recovery storage.",18,ink);button(264,593,203,"X  Confirm remove",bad,surface);text(487,600,"B  Cancel",18,muted);
                }else{
                    paragraph(250,524,m.detail.empty()?"No additional details. Apply state is reported by the runtime.":m.detail,19,muted,625,2);
                    button(250,589,191,s.inventory_unavailable()?"Changes paused":m.data.flags&moader::ipc::Requested?"A  Disable request":"A  Enable request");
                    text(461,598,s.inventory_unavailable()?"ZL  Diagnostics":"X  Remove",19,s.inventory_unavailable()?warn:bad);text(666,598,"R stick  Full details",18,action);
                }
                text(795,148,std::to_string(s.row+1)+" / "+std::to_string(indexes.size()),17,muted,99);
            }
        }
        // Persistent diagnostic pane is drawn last; focus uses an inset border without hiding its content.
        diagnostics(s);
        if(s.view==View::Debug){rect(920,72,2,572,focus);rect(1278,72,2,572,focus);rect(920,72,360,2,focus);rect(920,642,360,2,focus);}
        rect(0,644,1280,76,surface);rect(0,644,1280,1,line);
        if(!s.message.empty())text(24,650,ellipsis(s.message,18,1230),18,s.message_error?bad:ink,1230);
        else text(24,650,!s.connected?"Status is unavailable. Press Y to reconnect.":s.summary.flags&moader::ipc::Foreground?"Web access stays available while Moader is open.":"Library changes are saved independently of runtime acknowledgement.",18,muted,1230);
        key(24,685,"Y",s.connected?(s.summary.flags&moader::ipc::WebRunning?"Web off":"Web on"):"Reconnect");
        if(s.connected)key(180,685,"ZR","Connect");
        key(342,685,"ZL","Debug");key(480,685,"L stick","Settings");key(671,685,"B","Back");key(806,685,"D-pad","Navigate");key(1054,685,"+","hbmenu");

    }
};
uint64_t Renderer::event_layout_builds() const {return impl->event_builds;}
unsigned Renderer::scroll_limit(const State& s,View view){
    unsigned count=0;
    if(view==View::Debug){count=impl->event_lines(s).size();return count>10?count-10:0;}
    if(view==View::Details&&s.selected()<s.mods.size()){const auto& m=s.mods[s.selected()];
        const auto detail=m.name+"\n\nMod ID: "+m.id+"\nTitle ID: "+hexid(m.data.title_id)+"\n"+std::to_string(m.data.file_count)+" files · "+size_text(m.data.bytes)+"\n\n"+m.detail;
        count=impl->wrap(detail,22,620).size();return count>15?count-15:0;}return 0;
}
Renderer::Renderer():impl(std::make_unique<Impl>()){}Renderer::~Renderer()=default;
void Renderer::draw(uint32_t* p,int w,int h,int stride,const State& s,float dt){impl->pixels=p;impl->width=w;impl->height=h;impl->stride=stride/4;impl->scale=std::min(w/1280.f,h/720.f);impl->render(s,dt);}
}
