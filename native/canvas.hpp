#pragma once
#include "ui.hpp"
#include "../branding/mark-alpha.hpp"
#include "third_party/stb_truetype.h"
#include <map>

namespace moader_ui {
using Color = uint32_t;
constexpr Color rgb(unsigned v) { return ((v >> 16) & 255) | (v & 0xff00) | ((v & 255) << 16) | 0xff000000; }
constexpr Color base = rgb(0x121918), paper = rgb(0xf3f6ed), mint = rgb(0x9cf4ce), soft = rgb(0xb6c3ba),
                muted = rgb(0x9daea4), dim = rgb(0x829289), rule = rgb(0x35463e), selected = rgb(0x22382e),
                panel = rgb(0x1d2c24), disabled = rgb(0x738078), good = rgb(0x94dbb8), pending = rgb(0xf7c577),
                bad = rgb(0xffaa98);
Color blend(Color a, Color b, float mix);
std::string hexid(uint64_t id);
std::string size_text(uint64_t);
std::string game_name(const State&, const Game&);
std::string count_text(const State&, const Game&);
const char* applied_label(unsigned);
Color applied_color(unsigned);
enum Weight { Text, Strong, Display };
enum Align { Left, Center, Right };
enum class HitType {
    Key,
    Game,
    Dot,
    Mod,
    Tab,
    Setting,
    Digit,
    Home,
    Mods,
    Settings,
    Store,
    StoreTab,
    StoreItem,
    StoreGame,
    StoreMirror
};
struct Hit {
    float x, y, w, h;
    HitType type;
    unsigned value;
};
struct Canvas {
    uint32_t* pixels{};
    int w{}, h{}, stride{};
    float scale = 1, alpha = 1;
    std::vector<Hit> hits;
    Canvas();
    void begin(uint32_t*, int, int, int);
    void rect(float x, float y, float width, float height, Color color, float radius = 0);
    void box(float x, float y, float width, float height, float radius, Color fill, Color border,
             float stroke = 1);
    void ring(float x, float y, float width, float height, float radius, Color color, float stroke = 2);
    void mask(const moader::brand::Mask&, float x, float y, float width, float height, Color color);
    void image(const Art&, float x, float y, float width, float height, float radius = 10, float shade = 0,
               bool contain = false);
    void background(Color tint, bool opened);
    void symbol(const moader::brand::Mask& m, float x, float y, float size, Color c) {
        mask(m, x, y, size, size, c);
    }
    void arrow(float x, float y, int direction, float size = 8, Color color = soft);
    void key(float x, const std::string& letter, const std::string& label, bool primary = false);
    float width(const std::string&, float sz, Weight weight = Text, float tracking = 0);
    void text(float x, float y, const std::string&, float sz, Color c = paper, Weight wt = Text,
              float maxw = 1440, Align align = Left, float tracking = 0);
    std::vector<std::string> wrap(const std::string&, float size, float maxw, Weight = Text);
    void paragraph(float x, float y, const std::string&, float size, float maxw, unsigned lines, Color c = soft,
                   float leading = 0);
    void hit(float x, float y, float w, float h, HitType type, unsigned value = 0) {
        hits.push_back({x, y, w, h, type, value});
    }

  private:
    stbtt_fontinfo fonts[4]{};
    struct Glyph {
        int w = 0, h = 0, x = 0, y = 0;
        float advance = 0;
        std::vector<uint8_t> alpha;
    };
    std::map<uint64_t, Glyph> glyphs;
    Glyph& glyph(unsigned cp, int px, Weight weight);
    void pixel(int x, int y, Color, uint32_t coverage);
    std::vector<uint32_t> backdrop;
    int bgw = 0, bgh = 0;
    Color bg_tint = 0;
    bool bg_open = false;
};
void settings_screen(Canvas&, const State&, const std::vector<std::string>&);
void store_screen(Canvas&, const State&);
} // namespace moader_ui
