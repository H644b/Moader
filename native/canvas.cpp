#define STB_TRUETYPE_IMPLEMENTATION
#include "canvas.hpp"
#include "native_font.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace moader_ui {
namespace {
unsigned rune(const std::string& s, size_t& i) {
    unsigned c = (unsigned char)s[i++];
    if (c < 128)
        return c;
    unsigned n = c >= 240 ? 3 : c >= 224 ? 2 : c >= 192 ? 1 : 0;
    if (!n)
        return 0xfffd;
    unsigned v = c & ((1u << (6 - n)) - 1);
    while (n--) {
        if (i >= s.size() || ((unsigned char)s[i] & 192) != 128)
            return 0xfffd;
        v = (v << 6) | ((unsigned char)s[i++] & 63);
    }
    return v;
}
inline Color mix8(Color a, Color b, unsigned t) {
    unsigned u = 256 - t;
    return ((((a & 0xff00ff) * u + (b & 0xff00ff) * t) >> 8) & 0xff00ff) |
           ((((a & 0xff00) * u + (b & 0xff00) * t) >> 8) & 0xff00) | 0xff000000;
}
float inset(float py, float height, float radius) {
    const float d = std::max({radius - py, py - height + radius, 0.f});
    return radius - std::sqrt(std::max(0.f, radius * radius - d * d));
}
} // namespace
Color blend(Color a, Color b, float t) { return mix8(a, b, unsigned(std::clamp(t, 0.f, 1.f) * 256)); }
std::string hexid(uint64_t id) {
    char b[17];
    std::snprintf(b, sizeof(b), "%016llX", (unsigned long long)id);
    return b;
}
std::string size_text(uint64_t n) {
    char b[40];
    if (n < 1024)
        std::snprintf(b, sizeof(b), "%llu B", (unsigned long long)n);
    else if (n < 1048576)
        std::snprintf(b, sizeof(b), "%.1f KiB", double(n) / 1024);
    else
        std::snprintf(b, sizeof(b), "%.1f MiB", double(n) / 1048576);
    return b;
}
std::string game_name(const State& s, const Game& g) {
    return (s.preferences.flags & moader::ipc::GameNames) && !g.name.empty() ? g.name : hexid(g.id);
}
std::string count_text(const State& s, const Game& g) {
    unsigned on = 0;
    for (auto i : g.mods)
        if (s.mods[i].data.flags & moader::ipc::Requested)
            ++on;
    return std::to_string(g.mods.size()) + (g.mods.size() == 1 ? " mod   ·   " : " mods   ·   ") +
           std::to_string(on) + " requested";
}
const char* applied_label(unsigned n) {
    static constexpr const char* names[] = {"Disabled",      "Pending", "Accepted",        "Reopen content",
                                            "Relaunch game", "Error",   "Game unavailable"};
    return n < 7 ? names[n] : "Unknown";
}
Color applied_color(unsigned n) { return n == 2 ? good : n == 5 || n == 6 ? bad : n == 0 ? muted : pending; }
Canvas::Canvas() {
    const unsigned char* data[] = {font_text, font_strong, font_display, font_fallback};
    for (unsigned i = 0; i < 4; ++i)
        stbtt_InitFont(&fonts[i], data[i], 0);
}
void Canvas::begin(uint32_t* p, int width, int height, int pitch) {
    pixels = p;
    w = width;
    h = height;
    stride = pitch / 4;
    scale = std::min(w / 1440.f, h / 810.f);
    alpha = 1;
    hits.clear();
}
Canvas::Glyph& Canvas::glyph(unsigned cp, int px, Weight weight) {
    const auto key = (uint64_t(weight) << 56) | (uint64_t(px) << 32) | cp;
    auto it = glyphs.find(key);
    if (it != glyphs.end())
        return it->second;
    const auto* f = &fonts[weight];
    int index = stbtt_FindGlyphIndex(f, cp);
    if (!index) {
        f = &fonts[3];
        index = stbtt_FindGlyphIndex(f, cp);
        if (!index)
            index = stbtt_FindGlyphIndex(f, 0xfffd);
    }
    const float fs = stbtt_ScaleForMappingEmToPixels(f, float(px));
    int advance = 0;
    Glyph g;
    stbtt_GetGlyphHMetrics(f, index, &advance, nullptr);
    g.advance = advance * fs;
    auto* bitmap = stbtt_GetGlyphBitmap(f, fs, fs, index, &g.w, &g.h, &g.x, &g.y);
    if (bitmap) {
        g.alpha.assign(bitmap, bitmap + g.w * g.h);
        stbtt_FreeBitmap(bitmap, nullptr);
    }
    if (glyphs.size() >= 8192)
        glyphs.clear();
    return glyphs.emplace(key, std::move(g)).first->second;
}
void Canvas::pixel(int x, int y, Color color, uint32_t a) {
    if (x < 0 || y < 0 || x >= w || y >= h || !a)
        return;
    auto& dest = pixels[y * stride + x];
    dest = a >= 255 ? color : mix8(dest, color, a);
}
void Canvas::rect(float x, float y, float width, float height, Color c, float radius) {
    if (width <= 0 || height <= 0 || alpha < .003f)
        return;
    const float left = x * scale, top = y * scale, W = width * scale, H = height * scale,
                r = std::min({radius * scale, W / 2, H / 2});
    const unsigned opacity = std::lround(alpha * 255);
    const int y0 = std::max(0, int(std::round(top))), y1 = std::min(h, int(std::round(top + H)));
    for (int py = y0; py < y1; ++py) {
        const float off = r ? inset(py + .5f - top, H, r) : 0;
        int x0 = std::clamp(int(std::round(left + off)), 0, w),
            x1 = std::clamp(int(std::round(left + W - off)), 0, w);
        if (x1 > x0) {
            auto* row = pixels + py * stride;
            if (opacity == 255)
                std::fill(row + x0, row + x1, c);
            else
                for (int px = x0; px < x1; ++px)
                    row[px] = mix8(row[px], c, opacity);
        }
    }
}
void Canvas::ring(float x, float y, float W, float H, float r, Color color, float stroke) {
    const float left = x * scale, top = y * scale, ww = W * scale, hh = H * scale, rr = r * scale,
                sw = stroke * scale;
    for (int py = std::max(0, int(std::floor(top))); py < std::min(h, int(std::ceil(top + hh))); ++py) {
        const float dy = py + .5f - top;
        if (dy < 0 || dy > hh)
            continue;
        const auto outer = inset(dy, hh, rr);
        const bool solid = dy < sw || dy >= hh - sw;
        const float inner = solid ? ww / 2 : sw + inset(dy - sw, hh - 2 * sw, std::max(0.f, rr - sw));
        const auto draw = [&](float a, float b) {
            const int from = std::max(0, int(std::floor(a))), to = std::min(w, int(std::ceil(b)));
            for (int px = from; px < to; ++px)
                pixel(px, py, color,
                      unsigned(std::clamp(std::min(px + 1.f, b) - std::max(float(px), a), 0.f, 1.f) * alpha *
                               255));
        };
        if (solid)
            draw(left + outer, left + ww - outer);
        else {
            draw(left + outer, left + inner);
            draw(left + ww - inner, left + ww - outer);
        }
    }
}
void Canvas::box(float x, float y, float W, float H, float r, Color fill, Color edge, float sw) {
    rect(x, y, W, H, fill, r);
    ring(x, y, W, H, r, edge, sw);
}
void Canvas::mask(const moader::brand::Mask& m, float x, float y, float W, float H, Color color) {
    int width = std::max(1, int(std::round(W * scale))), height = std::max(1, int(std::round(H * scale)));
    int x0 = std::lround(x * scale), y0 = std::lround(y * scale);
    for (int dy = 0; dy < height; ++dy) {
        const float sy = std::clamp((dy + .5f) * m.h / height - .5f, 0.f, float(m.h - 1));
        const int iy = int(sy);
        for (int dx = 0; dx < width; ++dx) {
            const float sx = std::clamp((dx + .5f) * m.w / width - .5f, 0.f, float(m.w - 1));
            const int ix = int(sx), i1 = std::min(ix + 1, m.w - 1);
            auto sample = [&](int yy) {
                return std::lerp(float(m.a[yy * m.w + ix]), float(m.a[yy * m.w + i1]), sx - ix);
            };
            pixel(x0 + dx, y0 + dy, color,
                  unsigned(std::lerp(sample(iy), sample(std::min(iy + 1, m.h - 1)), sy - iy) * alpha));
        }
    }
}
void Canvas::image(const Art& a, float x, float y, float W, float H, float r, float shade, bool contain) {
    if (!a || alpha < .004f)
        return;
    if (contain) {
        rect(x, y, W, H, base, r);
        float fit = std::min(W / a->width, H / a->height);
        x += (W - fit * a->width) / 2;
        y += (H - fit * a->height) / 2;
        W = fit * a->width;
        H = fit * a->height;
    }
    const float left = x * scale, top = y * scale, ww = W * scale, hh = H * scale, rr = r * scale;
    const float f = 1 / std::max(ww / a->width, hh / a->height), ox = (a->width - ww * f) / 2,
                oy = (a->height - hh * f) / 2;
    const unsigned d = unsigned(std::clamp(shade, 0.f, 1.f) * 256), opa = std::lround(255 * alpha);
    for (int py = std::max(0, int(std::round(top))); py < std::min(h, int(std::round(top + hh))); ++py) {
        const float dy = py + .5f - top, off = inset(dy, hh, rr);
        const float sy = std::clamp(oy + dy * f - .5f, 0.f, float(a->height - 1));
        const unsigned iy = unsigned(sy);
        const auto* row = &a->pixels[iy * a->width];
        const auto* next = &a->pixels[std::min(iy + 1, a->height - 1) * a->width];
        unsigned ty = unsigned((sy - iy) * 256);
        const int from = std::max(0, int(std::round(left + off))),
                  to = std::min(w, int(std::round(left + ww - off)));
        for (int px = from; px < to; ++px) {
            float sx = std::clamp(ox + (px + .5f - left) * f - .5f, 0.f, float(a->width - 1));
            unsigned ix = unsigned(sx), j = std::min(ix + 1, a->width - 1), tx = unsigned((sx - ix) * 256);
            auto c = mix8(mix8(row[ix], row[j], tx), mix8(next[ix], next[j], tx), ty);
            if (d)
                c = mix8(c, rgb(0x091712), d);
            auto& dst = pixels[py * stride + px];
            dst = opa == 255 ? c : mix8(dst, c, opa);
        }
    }
}
void Canvas::background(Color tint, bool open) {
    if (bgw != w || bgh != h || bg_tint != tint || bg_open != open) {
        bgw = w;
        bgh = h;
        bg_tint = tint;
        bg_open = open;
        backdrop.resize(size_t(w) * h);
        const float cx = (open ? 288 : 720) * scale, cy = (open ? 290 : 372) * scale;
        if (open)
            tint = blend(tint, base, .3f);
        Color palette[256];
        for (unsigned i = 0; i < 256; ++i)
            palette[i] = mix8(tint, base, i);
        for (int y = 0; y < h; ++y) {
            float y2 = (y - cy) * (y - cy);
            for (int x = 0; x < w; ++x) {
                float dx = x - cx;
                backdrop[y * w + x] =
                    palette[std::min(255, int(std::sqrt(dx * dx + y2) * 255 / (860 * scale)))];
            }
        }
    }
    for (int y = 0; y < h; ++y)
        std::memcpy(pixels + y * stride, backdrop.data() + y * w, w * sizeof(Color));
}
float Canvas::width(const std::string& s, float sz, Weight wt, float tracking) {
    int px = std::max(1, int(std::lround(sz * scale)));
    float v = 0;
    for (size_t i = 0; i < s.size();) {
        v += glyph(rune(s, i), px, wt).advance / scale;
        if (i < s.size())
            v += tracking;
    }
    return v;
}
void Canvas::text(float x, float y, const std::string& src, float sz, Color c, Weight wt, float maxw,
                  Align align, float tracking) {
    if (src.empty() || alpha < .004f || maxw <= 0)
        return;
    float length = width(src, sz, wt, tracking);
    if (length > maxw) {
        sz = std::max(sz * .71f, sz * maxw / length);
        length = width(src, sz, wt, tracking);
    }
    // Ellipsize on a codepoint boundary after fitting moderately long labels.
    std::string clipped;
    const std::string* value = &src;
    if (length > maxw + .5f) {
        float used = 0, ell = width("…", sz, wt);
        int px = std::max(1, int(std::lround(sz * scale)));
        for (size_t i = 0; i < src.size();) {
            size_t start = i;
            auto cp = rune(src, i);
            float advance = glyph(cp, px, wt).advance / scale + tracking;
            if (used + advance + ell > maxw)
                break;
            used += advance;
            clipped += src.substr(start, i - start);
        }
        clipped += "…";
        value = &clipped;
        length = width(clipped, sz, wt, tracking);
    }
    if (align == Center)
        x -= length / 2;
    else if (align == Right)
        x -= length;
    const int px = std::max(1, int(std::lround(sz * scale))), baseline = std::lround((y + sz * .755f) * scale);
    float cursor = x * scale;
    for (size_t i = 0; i < value->size();) {
        auto& g = glyph(rune(*value, i), px, wt);
        int xx = std::lround(cursor) + g.x, yy = baseline + g.y;
        for (int gy = 0; gy < g.h; ++gy)
            for (int gx = 0; gx < g.w; ++gx)
                pixel(xx + gx, yy + gy, c, unsigned(g.alpha[gy * g.w + gx] * alpha));
        cursor += g.advance + tracking * scale;
    }
}
std::vector<std::string> Canvas::wrap(const std::string& s, float sz, float maxw, Weight wt) {
    std::vector<std::string> lines;
    std::string row, word;
    auto flush = [&]() {
        if (word.empty())
            return;
        if (!row.empty() && width(row + " " + word, sz, wt) > maxw) {
            lines.push_back(row);
            row.clear();
        }
        if (!row.empty())
            row += ' ';
        if (width(word, sz, wt) > maxw) {
            float used = width(row, sz, wt);
            int px = std::max(1, int(std::lround(sz * scale)));
            for (size_t i = 0; i < word.size();) {
                size_t start = i;
                float adv = glyph(rune(word, i), px, wt).advance / scale;
                if (used + adv > maxw && !row.empty()) {
                    lines.push_back(row);
                    row.clear();
                    used = 0;
                }
                row.append(word, start, i - start);
                used += adv;
            }
        } else
            row += word;
        word.clear();
    };
    for (size_t i = 0; i < s.size();) {
        size_t from = i;
        unsigned cp = rune(s, i);
        if (cp == ' ' || cp == '\n' || cp == '\r') {
            flush();
            if (cp == '\n') {
                lines.push_back(row);
                row.clear();
            }
        } else
            word.append(s, from, i - from);
    }
    flush();
    if (!row.empty())
        lines.push_back(row);
    return lines;
}
void Canvas::paragraph(float x, float y, const std::string& s, float sz, float maxw, unsigned count, Color c,
                       float leading) {
    auto lines = wrap(s, sz, maxw);
    for (unsigned i = 0; i < std::min(count, unsigned(lines.size())); ++i)
        text(x, y + i * (leading ? leading : sz + 6), lines[i], sz, c, Text, maxw);
}
void Canvas::arrow(float x, float y, int direction, float size, Color color) {
    for (float step = -size; step <= size; step += .65f)
        rect(x + direction * (size * .36f - std::abs(step) * .65f) - 1, y + step - 1, 2.1, 2.1, color, 1);
}
void Canvas::key(float x, const std::string& letter, const std::string& label, bool primary) {
    if (letter.size() == 1 && letter != "L" && letter != "R") {
        if (primary)
            rect(x - 14, 759, 28, 28, mint, 14);
        else
            ring(x - 14, 759, 28, 28, 14, soft, 1.5);
        text(x, 765, letter, 18, primary ? base : paper, Strong, 28, Center);
    } else {
        auto ww = std::max(30.f, width(letter, 15, Strong) + 12);
        ring(x - ww / 2, 761, ww, 23, 6, muted, 1.4);
        text(x, 766, letter, 15, paper, Strong, ww, Center);
    }
    if (!label.empty())
        text(x + 25, 763, label, 21, primary ? paper : soft, primary ? Strong : Text, 260);
}
} // namespace moader_ui
