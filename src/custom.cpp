/* An effect's own images for the native port: see custom.h */
#include "custom.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

/* --- a small JSON reader: numbers, strings, arrays and objects --- */

struct Json {
    char type = 0; /* 'n', 's', '[', '{', 0 null */
    double num = 0;
    std::string str;
    std::vector<std::pair<std::string, Json>> items; /* an array's keys are empty */

    const Json *get(const char *key) const {
        for (const auto &it : items)
            if (it.first == key) return &it.second;
        return nullptr;
    }
};

static void ws(const char *&p) {
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
}

static bool parse_string(const char *&p, std::string &out) {
    for (p++; *p && *p != '"'; p++) {
        if (*p == '\\' && p[1]) p++;
        out += *p;
    }
    if (*p != '"') return false;
    p++;
    return true;
}

static bool parse(const char *&p, Json &j) {
    ws(p);
    if (*p == '[' || *p == '{') {
        char close = *p == '[' ? ']' : '}';
        j.type = *p++;
        for (;;) {
            std::string key;
            ws(p);
            if (*p == close) return p++, true;
            if (j.type == '{') {
                if (*p != '"' || !parse_string(p, key)) return false;
                ws(p);
                if (*p++ != ':') return false;
            }
            j.items.emplace_back(key, Json());
            if (!parse(p, j.items.back().second)) return false;
            ws(p);
            if (*p == ',') p++;
            else if (*p != close) return false;
        }
    }
    if (*p == '"') return j.type = 's', parse_string(p, j.str);
    if (strncmp(p, "null", 4) == 0) return p += 4, true;
    char *end;
    j.num = strtod(p, &end);
    if (end == p) return false;
    j.type = 'n', p = end;
    return true;
}

/* --- the folder --- */

static CustomImage load_png(const std::string &path, std::string &error) {
    CustomImage img;
    SDL_Surface *png = SDL_LoadPNG(path.c_str());
    SDL_Surface *s = png ? SDL_ConvertSurface(png, SDL_PIXELFORMAT_RGBA32) : nullptr;
    if (png) SDL_DestroySurface(png);
    if (!s) {
        error = path + ": " + SDL_GetError();
        return img;
    }
    img.w = s->w, img.h = s->h;
    img.px.resize((size_t)s->w * s->h);
    for (int y = 0; y < s->h; y++) memcpy(&img.px[(size_t)y * s->w], (uint8_t *)s->pixels + y * s->pitch, s->w * 4);
    SDL_DestroySurface(s);
    return img;
}

bool CustomEffect::load(const std::string &d) {
    dir = d;
    frames.clear(), images.clear(), error.clear();
    x = y = 0, scale = 1, add = false, hasJson = false;

    int n = 0;
    char **names = SDL_GlobDirectory(dir.c_str(), "*.png", SDL_GLOB_CASEINSENSITIVE, &n);
    std::vector<std::string> sorted(names, names + (names ? n : 0));
    SDL_free(names);
    std::sort(sorted.begin(), sorted.end());
    for (const std::string &name : sorted) {
        CustomImage img = load_png(dir + "/" + name, error);
        if (img.w > 0) images[name] = std::move(img);
    }

    std::ifstream f(dir + "/effect.json", std::ios::binary);
    if (f) {
        std::string text((std::istreambuf_iterator<char>(f)), {});
        const char *p = text.c_str();
        Json root;
        if (!parse(p, root) || root.type != '{') {
            error = dir + "/effect.json isn't JSON";
        } else {
            hasJson = true;
            if (const Json *fr = root.get("frames"))
                for (const auto &it : fr->items) {
                    const Json &e = it.second;
                    if (e.items.size() < 2 || e.items[0].second.type != 's') continue;
                    frames.push_back({e.items[0].second.str, std::max(1, (int)e.items[1].second.num)});
                }
            if (const Json *v = root.get("x")) x = (float)v->num;
            if (const Json *v = root.get("y")) y = (float)v->num;
            if (const Json *v = root.get("scale"); v && v->num > 0) scale = (float)v->num;
            if (const Json *v = root.get("blend")) add = v->str == "add";
        }
    }
    if (!hasJson)
        for (const auto &it : images) frames.push_back({it.first, 2});
    return !images.empty();
}

bool CustomEffect::save() {
    SDL_CreateDirectory(dir.c_str());
    std::ofstream f(dir + "/effect.json", std::ios::binary);
    if (!f) {
        error = "can't write " + dir + "/effect.json";
        return false;
    }
    char b[64];
    f << "{\n  \"frames\": [";
    for (size_t i = 0; i < frames.size(); i++) {
        std::string name;
        for (char c : frames[i].file) name += c == '"' || c == '\\' ? std::string("\\") + c : std::string(1, c);
        f << (i ? ",\n" : "\n") << "    [\"" << name << "\", " << frames[i].ticks << "]";
    }
    snprintf(b, sizeof b, "%g", x);
    f << "\n  ],\n  \"x\": " << b;
    snprintf(b, sizeof b, "%g", y);
    f << ",\n  \"y\": " << b;
    snprintf(b, sizeof b, "%g", scale);
    f << ",\n  \"scale\": " << b << ",\n  \"blend\": \"" << (add ? "add" : "alpha") << "\"\n}\n";
    hasJson = true;
    return true;
}

int CustomEffect::import(const std::vector<std::string> &paths) {
    std::vector<std::string> sorted = paths;
    std::sort(sorted.begin(), sorted.end());
    SDL_CreateDirectory(dir.c_str());
    int n = 0;
    for (const std::string &path : sorted) {
        size_t slash = path.find_last_of("/\\");
        std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
        std::string to = dir + "/" + name;
        CustomImage img = load_png(path, error);
        if (img.w == 0) continue;
        if (SDL_strcasecmp(path.c_str(), to.c_str()) != 0 && !SDL_CopyFile(path.c_str(), to.c_str())) {
            error = to + ": " + SDL_GetError();
            continue;
        }
        images[name] = std::move(img);
        frames.push_back({name, 2});
        n++;
    }
    return n;
}

int CustomEffect::duration() const {
    int t = 0;
    for (const CustomFrame &f : frames) t += f.ticks;
    return t;
}

int CustomEffect::frameAt(int tick) const {
    int t = 0;
    for (size_t i = 0; i < frames.size(); i++)
        if (tick < (t += frames[i].ticks)) return (int)i;
    return frames.empty() ? -1 : (int)frames.size() - 1;
}

/* as the port draws it: the image 240 * scale screen pixels high, its middle
   at the point moved by (x, y), blended by its alpha or added */
void CustomEffect::draw(int tick, uint32_t *rgba, int w, int h, int ox, int oy) const {
    int f = frameAt(tick);
    if (f < 0) return;
    auto it = images.find(frames[f].file);
    if (it == images.end()) return;
    const CustomImage &img = it->second;
    float dh = 240.0f * scale, dw = dh * img.w / img.h;
    float x0 = ox + x - dw / 2, y0 = oy + y - dh / 2;
    for (int py = std::max(0, (int)floorf(y0)); py < std::min(h, (int)ceilf(y0 + dh)); py++) {
        int sy = (int)((py + 0.5f - y0) * img.h / dh);
        if (sy < 0 || sy >= img.h) continue;
        for (int px = std::max(0, (int)floorf(x0)); px < std::min(w, (int)ceilf(x0 + dw)); px++) {
            int sx = (int)((px + 0.5f - x0) * img.w / dw);
            if (sx < 0 || sx >= img.w) continue;
            uint32_t s = img.px[(size_t)sy * img.w + sx], &d = rgba[py * w + px];
            int a = s >> 24, out = 0xFF000000;
            if (!a) continue;
            for (int c = 0; c < 24; c += 8) {
                int sc = s >> c & 255, dc = d >> c & 255;
                int v = add ? dc + sc * a / 255 : (sc * a + dc * (255 - a)) / 255;
                out |= std::min(v, 255) << c;
            }
            d = (uint32_t)out;
        }
    }
}
