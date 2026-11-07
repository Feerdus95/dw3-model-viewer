/* An effect's own images for the native port: see custom.h */
#include "custom.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

/* --- a small JSON reader: numbers, strings, arrays and objects --- */

static void ws(const char *&p) {
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
}

static bool parse_string(const char *&p, std::string &out) {
    for (p++; *p && *p != '"'; p++) {
        if (*p == '\\' && p[1]) {
            p++;
            if (*p == 'n') { out += '\n'; continue; }
            if (*p == 't') { out += '\t'; continue; }
            if (*p == 'r') { out += '\r'; continue; }
            if (*p == 'u' && p[1] && p[2] && p[3] && p[4]) { /* only the ASCII this tool's writers make */
                out += (char)strtol(std::string(p + 1, 4).c_str(), nullptr, 16);
                p += 4;
                continue;
            }
        }
        out += *p;
    }
    if (*p != '"') return false;
    p++;
    return true;
}

bool json_parse(const char *&p, Json &j) {
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
            if (!json_parse(p, j.items.back().second)) return false;
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

static uint32_t next_image_uid() {
    static uint32_t uid = 0;
    return ++uid;
}

const char *const EFFECT_LEVELS[5] = {"", "rookie", "champion", "ultimate", "mega"};

std::string own_dir(const std::string &root, int id, int level, bool critical) {
    std::string d = root + "/" + std::to_string(id);
    if (level > 0) d += std::string("/") + EFFECT_LEVELS[level];
    return critical ? d + "/critical" : d;
}

std::vector<OwnFolder> own_order(const std::string &root, int id, int level, bool critical) {
    std::vector<OwnFolder> out;
    for (int l = std::clamp(level, 0, 4); l >= 0; l--) {
        if (critical) out.push_back({own_dir(root, id, l, true), l, true});
        out.push_back({own_dir(root, id, l, false), l, false});
    }
    return out;
}

bool own_plays(const std::string &dir) {
    int n = 0;
    SDL_free(SDL_GlobDirectory(dir.c_str(), "*.png", SDL_GLOB_CASEINSENSITIVE, &n));
    if (n == 0) return false;
    std::ifstream f(dir + "/effect.json", std::ios::binary);
    if (!f) return true;
    std::string text((std::istreambuf_iterator<char>(f)), {});
    const char *p = text.c_str();
    Json root;
    if (!json_parse(p, root)) return true;
    const Json *a = root.get("active");
    return !(a && a->type == 'n' && a->num == 0);
}

std::string own_stamp(const std::string &dir) {
    std::string out;
    int n = 0;
    char **names = SDL_GlobDirectory(dir.c_str(), "*", 0, &n);
    for (int i = 0; i < n; i++) {
        SDL_PathInfo info;
        if (SDL_GetPathInfo((dir + "/" + names[i]).c_str(), &info))
            out += std::string(names[i]) + ":" + std::to_string(info.size) + ":" + std::to_string(info.modify_time) + ";";
    }
    SDL_free(names);
    return out;
}

std::vector<std::string> pngs_at(const std::string &path) {
    std::vector<std::string> out;
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path.c_str(), &info)) return out;
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
        int n = 0;
        char **names = SDL_GlobDirectory(path.c_str(), "*.png", SDL_GLOB_CASEINSENSITIVE, &n);
        for (int i = 0; i < n; i++) out.push_back(path + "/" + names[i]);
        SDL_free(names);
    } else if (path.size() > 4 && SDL_strcasecmp(path.c_str() + path.size() - 4, ".png") == 0) {
        out.push_back(path);
    }
    return out;
}

std::map<int, int> levels_load(const std::string &root) {
    std::map<int, int> out;
    std::ifstream f(root + "/stages.json", std::ios::binary);
    if (!f) return out;
    std::string text((std::istreambuf_iterator<char>(f)), {});
    const char *p = text.c_str();
    Json j;
    if (!json_parse(p, j)) return out;
    for (int l = 1; l < 5; l++)
        if (const Json *ids = j.get(EFFECT_LEVELS[l]))
            for (const auto &it : ids->items) out[(int)it.second.num] = l;
    return out;
}

bool levels_save(const std::string &root, const std::map<int, int> &levels) {
    SDL_CreateDirectory(root.c_str());
    std::ofstream f(root + "/stages.json", std::ios::binary);
    if (!f) return false;
    f << "{";
    for (int l = 1; l < 5; l++) {
        f << (l > 1 ? ",\n" : "\n") << "  \"" << EFFECT_LEVELS[l] << "\": [";
        bool first = true;
        for (const auto &it : levels)
            if (it.second == l) f << (first ? "" : ", ") << it.first, first = false;
        f << "]";
    }
    f << "\n}\n";
    return true;
}

/* names in the order a person numbers them: 2.png before 10.png */
static bool natural_less(const std::string &a, const std::string &b) {
    size_t i = 0, k = 0;
    while (i < a.size() && k < b.size()) {
        if (isdigit((unsigned char)a[i]) && isdigit((unsigned char)b[k])) {
            size_t i2 = i, k2 = k;
            while (i2 < a.size() && isdigit((unsigned char)a[i2])) i2++;
            while (k2 < b.size() && isdigit((unsigned char)b[k2])) k2++;
            unsigned long x = strtoul(a.c_str() + i, nullptr, 10), y = strtoul(b.c_str() + k, nullptr, 10);
            if (x != y) return x < y;
            i = i2, k = k2;
        } else {
            if (a[i] != b[k]) return a[i] < b[k];
            i++, k++;
        }
    }
    return a.size() - i < b.size() - k;
}

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
    x = y = 0, scale = 1, add = false, hasJson = false, active = true;

    int n = 0;
    char **names = SDL_GlobDirectory(dir.c_str(), "*.png", SDL_GLOB_CASEINSENSITIVE, &n);
    std::vector<std::string> sorted(names, names + (names ? n : 0));
    SDL_free(names);
    std::sort(sorted.begin(), sorted.end());
    for (const std::string &name : sorted) {
        CustomImage img = load_png(dir + "/" + name, error);
        if (img.w > 0) img.uid = next_image_uid(), images[name] = std::move(img);
    }

    std::ifstream f(dir + "/effect.json", std::ios::binary);
    if (f) {
        std::string text((std::istreambuf_iterator<char>(f)), {});
        const char *p = text.c_str();
        Json root;
        if (!json_parse(p, root) || root.type != '{') {
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
            if (const Json *v = root.get("active"); v && v->type == 'n') active = v->num != 0;
            if (const Json *snd = root.get("sound")) {
                if (const Json *v = snd->get("file"); v && v->type == 's') sound = v->str;
                if (const Json *v = snd->get("volume"); v && v->type == 'n' && v->num >= 0 && v->num <= 4) soundVolume = (float)v->num;
            }
        }
    }
    if (!hasJson)
        for (const auto &it : images) frames.push_back({it.first, 2});
    return !images.empty() || !sound.empty();
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
    f << ",\n  \"scale\": " << b << ",\n  \"blend\": \"" << (add ? "add" : "alpha") << "\"" << (active ? "" : ",\n  \"active\": 0");
    if (!sound.empty()) {
        std::string name;
        for (char c : sound) name += c == '"' || c == '\\' ? std::string("\\") + c : std::string(1, c);
        snprintf(b, sizeof b, "%g", soundVolume);
        f << ",\n  \"sound\": {\"file\": \"" << name << "\", \"volume\": " << b << "}";
    }
    f << "\n}\n";
    hasJson = true;
    return true;
}

bool CustomEffect::import_sound(const std::string &path) {
    size_t slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    SDL_CreateDirectory(dir.c_str());
    std::string from = path;
    std::replace(from.begin(), from.end(), '\\', '/');
    if (from != dir + "/" + name && !SDL_CopyFile(path.c_str(), (dir + "/" + name).c_str())) { /* not onto itself */
        error = "can't copy " + path + ": " + SDL_GetError();
        return false;
    }
    sound = name;
    return true;
}

void sound_preview(const std::string &path, float volume) {
    static SDL_AudioStream *stream;
    SDL_AudioSpec spec;
    Uint8 *data = nullptr;
    Uint32 len = 0;
    if (!SDL_LoadWAV(path.c_str(), &spec, &data, &len)) return;
    SDL_DestroyAudioStream(stream); /* null the first time */
    stream = nullptr;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) && (stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr))) {
        SDL_SetAudioStreamGain(stream, volume);
        SDL_PutAudioStreamData(stream, data, (int)len);
        SDL_FlushAudioStream(stream);
        SDL_ResumeAudioStreamDevice(stream);
    }
    SDL_free(data);
}

int CustomEffect::import(const std::vector<std::string> &paths) {
    std::vector<std::string> sorted = paths;
    std::sort(sorted.begin(), sorted.end(), natural_less);
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
        img.uid = next_image_uid(), images[name] = std::move(img);
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

const CustomImage *CustomEffect::imageAt(int tick) const {
    int f = frameAt(tick);
    if (f < 0) return nullptr;
    auto it = images.find(frames[f].file);
    return it == images.end() ? nullptr : &it->second;
}

/* where it reaches on a canvas at the PSX's own 320x240 pixels, for the view's fit (the view itself draws the image on the card, mod_tex): the image
   240 * scale screen pixels high, its middle at the point moved by (x, y), blended by its alpha or added */
void CustomEffect::draw(int tick, uint32_t *rgba, int w, int h, int ox, int oy) const {
    const CustomImage *found = imageAt(tick);
    if (!found) return;
    const CustomImage &img = *found;
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

/* --- a stage's props --- */

static void jvec(const Json &o, const char *key, float *out, size_t n) {
    const Json *v = o.get(key);
    if (v && v->type == '[')
        for (size_t i = 0; i < n && i < v->items.size(); i++) out[i] = (float)v->items[i].second.num;
}

static float jnum(const Json &o, const char *key, float def) {
    const Json *v = o.get(key);
    return v && v->type == 'n' ? (float)v->num : def;
}

bool props_load(const std::string &dir, std::vector<StageProp> &props, PropsMeta &meta) {
    props.clear();
    meta = PropsMeta();
    std::ifstream f(dir + "/props.json", std::ios::binary);
    if (!f) return false;
    std::string text((std::istreambuf_iterator<char>(f)), {});
    const char *p = text.c_str();
    Json root;
    if (!json_parse(p, root) || root.type != '{') return false;
    meta.wind = jnum(root, "wind", 1);
    meta.leaves = (int)jnum(root, "leaves", -1);
    if (const Json *fg = root.get("fog"); fg && fg->type == '{') {
        meta.fog = true;
        jvec(*fg, "color", meta.fogColor, 3);
        meta.fogAmount = jnum(*fg, "amount", meta.fogAmount), meta.fogFrom = jnum(*fg, "from", meta.fogFrom);
    }
    for (int pass = 0; pass < 3; pass++) {
        const Json *list = root.get(pass == 0 ? "props" : pass == 1 ? "emitters" : "models");
        if (!list) continue;
        for (const auto &it : list->items) {
            const Json &o = it.second;
            const Json *img = o.get("image");
            if (!img || img->type != 's') continue;
            StageProp s;
            s.emitter = pass == 1, s.model = pass == 2;
            s.image = img->str;
            if (s.model) {
                const Json *ob = o.get("model");
                if (!ob || ob->type != 's') continue;
                s.obj = ob->str;
            }
            jvec(o, "pos", s.pos, 3), jvec(o, "size", s.size, 2), jvec(o, "tint", s.tint, 3);
            s.fog = jnum(o, "fog", 1);
            if (s.emitter) {
                s.count = (int)jnum(o, "count", 20), s.fall = jnum(o, "fall", 1), s.wind = jnum(o, "wind", 1);
                if (const Json *a = o.get("area")) {
                    jvec(*a, "center", s.pos, 3);
                    s.radius = jnum(*a, "radius", s.radius), s.top = jnum(*a, "top", s.top);
                }
            } else {
                s.scale = jnum(o, "scale", 1), s.sway = jnum(o, "sway", 0), s.rot = jnum(o, "rot", 0);
                const Json *fc = o.get("facing");
                s.fixed = fc && fc->str == "fixed";
            }
            props.push_back(s);
        }
    }
    return !props.empty();
}

bool props_save(const std::string &dir, const std::vector<StageProp> &props, const PropsMeta &meta) {
    if (props.empty() && meta.leaves < 0) {
        SDL_RemovePath((dir + "/props.json").c_str());
        return true;
    }
    SDL_CreateDirectory(dir.c_str());
    std::ofstream f(dir + "/props.json", std::ios::binary);
    if (!f) return false;
    char b[320];
    auto name = [](const std::string &s) {
        std::string out;
        for (char c : s) out += c == '"' || c == '\\' ? std::string("\\") + c : std::string(1, c);
        return out;
    };
    f << "{\n";
    if (meta.wind != 1) {
        snprintf(b, sizeof b, " \"wind\": %g,\n", meta.wind);
        f << b;
    }
    if (meta.leaves >= 0) {
        snprintf(b, sizeof b, " \"leaves\": %d,\n", meta.leaves);
        f << b;
    }
    if (meta.fog) {
        snprintf(b, sizeof b, " \"fog\": {\"color\": [%g, %g, %g], \"amount\": %g, \"from\": %g},\n", meta.fogColor[0], meta.fogColor[1], meta.fogColor[2],
                 meta.fogAmount, meta.fogFrom);
        f << b;
    }
    for (int pass = 0; pass < 3; pass++) {
        f << (pass == 0 ? " \"props\": [" : pass == 1 ? ",\n \"emitters\": [" : ",\n \"models\": [");
        bool first = true;
        for (const StageProp &s : props) {
            if ((s.model ? 2 : s.emitter ? 1 : 0) != pass) continue;
            f << (first ? "\n" : ",\n") << "  {\n   \"image\": \"" << name(s.image) << "\"";
            first = false;
            if (s.model) f << ",\n   \"model\": \"" << name(s.obj) << "\"";
            else {
                snprintf(b, sizeof b, ",\n   \"size\": [%g, %g]", s.size[0], s.size[1]);
                f << b;
            }
            if (s.tint[0] != 255 || s.tint[1] != 255 || s.tint[2] != 255) {
                snprintf(b, sizeof b, ",\n   \"tint\": [%g, %g, %g]", s.tint[0], s.tint[1], s.tint[2]);
                f << b;
            }
            if (s.fog != 1) {
                snprintf(b, sizeof b, ",\n   \"fog\": %g", s.fog);
                f << b;
            }
            if (s.emitter) {
                snprintf(b, sizeof b, ",\n   \"count\": %d,\n   \"area\": {\"center\": [%g, %g, %g], \"radius\": %g, \"top\": %g},\n   \"fall\": %g,\n   \"wind\": %g",
                         s.count, s.pos[0], s.pos[1], s.pos[2], s.radius, s.top, s.fall, s.wind);
            } else if (s.model) {
                snprintf(b, sizeof b, ",\n   \"pos\": [%g, %g, %g],\n   \"scale\": %g,\n   \"sway\": %g,\n   \"rot\": %g", s.pos[0], s.pos[1], s.pos[2], s.scale,
                         s.sway, s.rot);
            } else {
                snprintf(b, sizeof b, ",\n   \"pos\": [%g, %g, %g],\n   \"scale\": %g,\n   \"sway\": %g,\n   \"rot\": %g,\n   \"facing\": \"%s\"",
                         s.pos[0], s.pos[1], s.pos[2], s.scale, s.sway, s.rot, s.fixed ? "fixed" : "camera");
            }
            f << b << "\n  }";
        }
        f << "\n ]";
    }
    f << "\n}\n";
    return (bool)f;
}

/* --- an OBJ --- */

static std::string read_text(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), {});
}

bool obj_load(const std::string &path, ObjMesh &mesh) {
    std::string text = read_text(path);
    std::vector<float> pos, uv, nrm;
    mesh.v.clear();
    const char *p = text.c_str();
    while (*p) {
        const char *line = p;
        while (*p && *p != '\n') p++;
        const char *end = p;
        if (line[0] == 'v' && (line[1] == ' ' || line[1] == 't' || line[1] == 'n')) {
            int kind = line[1] == ' ' ? 0 : line[1] == 't' ? 1 : 2, need = kind == 1 ? 2 : 3;
            const char *q = line + (kind == 0 ? 1 : 2);
            std::vector<float> &arr = kind == 0 ? pos : kind == 1 ? uv : nrm;
            for (int i = 0; i < need; i++) {
                char *e;
                arr.push_back((float)strtod(q, &e));
                q = e;
            }
        } else if (line[0] == 'f' && line[1] == ' ') {
            struct Ix {
                int v, t, n;
            };
            std::vector<Ix> face;
            const char *q = line + 1;
            int np = (int)pos.size() / 3, nu = (int)uv.size() / 2, nn = (int)nrm.size() / 3;
            auto num = [&]() {
                char *e;
                long v = strtol(q, &e, 10);
                if (e == q) return 0L;
                q = e;
                return v;
            };
            while (face.size() < 64) {
                while (q < end && (*q == ' ' || *q == '\t')) q++;
                if (q >= end || *q == '\r') break;
                Ix x = {(int)num(), 0, 0};
                if (*q == '/') {
                    q++;
                    x.t = (int)num();
                    if (*q == '/') q++, x.n = (int)num();
                }
                if (!x.v) break;
                x.v = x.v < 0 ? np + x.v : x.v - 1, x.t = x.t < 0 ? nu + x.t : x.t - 1, x.n = x.n < 0 ? nn + x.n : x.n - 1;
                if (x.v < 0 || x.v >= np) break;
                face.push_back(x);
            }
            for (size_t i = 1; i + 1 < face.size(); i++) {
                const Ix tri[3] = {face[0], face[i], face[i + 1]};
                const float *a = &pos[tri[0].v * 3], *b = &pos[tri[1].v * 3], *c = &pos[tri[2].v * 3];
                float u0 = b[0] - a[0], u1 = b[1] - a[1], u2 = b[2] - a[2], w0 = c[0] - a[0], w1 = c[1] - a[1], w2 = c[2] - a[2];
                float fn[3] = {u1 * w2 - u2 * w1, u2 * w0 - u0 * w2, u0 * w1 - u1 * w0};
                float l = sqrtf(fn[0] * fn[0] + fn[1] * fn[1] + fn[2] * fn[2]);
                if (l > 1e-9f) fn[0] /= l, fn[1] /= l, fn[2] /= l;
                else fn[0] = 0, fn[1] = 1, fn[2] = 0;
                for (const Ix &x : tri) {
                    bool hasT = x.t >= 0 && x.t < nu, hasN = x.n >= 0 && x.n < nn;
                    float o[8] = {pos[x.v * 3], pos[x.v * 3 + 1], pos[x.v * 3 + 2], hasT ? uv[x.t * 2] : 0, hasT ? 1 - uv[x.t * 2 + 1] : 0,
                                  hasN ? nrm[x.n * 3] : fn[0], hasN ? nrm[x.n * 3 + 1] : fn[1], hasN ? nrm[x.n * 3 + 2] : fn[2]};
                    mesh.v.insert(mesh.v.end(), o, o + 8);
                }
            }
        }
        if (*p) p++;
    }
    if (mesh.v.empty()) return false;
    float lo = 1e30f, hi = -1e30f;
    for (size_t i = 1; i < mesh.v.size(); i += 8) lo = std::min(lo, mesh.v[i]), hi = std::max(hi, mesh.v[i]);
    mesh.miny = lo, mesh.h = std::max(hi - lo, 1e-3f);
    return true;
}

std::string obj_texture(const std::string &path) {
    std::string text = read_text(path), dir = path.substr(0, path.find_last_of("/\\") + 1);
    size_t m = text.find("mtllib ");
    if (m == std::string::npos) return "";
    size_t e = text.find_first_of("\r\n", m);
    std::string mtl = text.substr(m + 7, e == std::string::npos ? std::string::npos : e - m - 7);
    std::string mt = read_text(dir + mtl);
    size_t k = mt.find("map_Kd ");
    if (k == std::string::npos) return "";
    size_t le = mt.find_first_of("\r\n", k);
    std::string tex = mt.substr(k + 7, le == std::string::npos ? std::string::npos : le - k - 7);
    size_t sp = tex.find_last_of(" /\\");
    return sp == std::string::npos ? tex : tex.substr(sp + 1);
}

/* --- the sound library --- */

std::string sound_name(const std::string &file) {
    size_t slash = file.find_last_of("/\\");
    std::string stem = slash == std::string::npos ? file : file.substr(slash + 1);
    size_t dot = stem.find_last_of('.');
    if (dot != std::string::npos) stem = stem.substr(0, dot);
    std::string out;
    for (char c : stem) {
        c = (char)tolower((unsigned char)c);
        out += (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ? c : '_';
    }
    if (out.size() > 32) out.resize(32);
    return out.empty() ? "sound" : out;
}

int SoundLibrary::find(const std::string &name) const {
    for (size_t i = 0; i < sounds.size(); i++)
        if (sounds[i].name == name) return (int)i;
    return -1;
}

bool SoundLibrary::load(const std::string &d) {
    dir = d;
    sounds.clear();
    replace.clear();
    error.clear();
    std::ifstream f(dir + "/sounds.json", std::ios::binary);
    if (!f) return true;
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const char *p = text.c_str();
    Json root;
    if (!json_parse(p, root) || root.type != '{') {
        error = dir + "/sounds.json isn't JSON";
        return false;
    }
    if (const Json *s = root.get("sounds"))
        for (const auto &it : s->items) {
            SoundEntry e;
            e.name = it.first;
            if (const Json *v = it.second.get("file"); v && v->type == 's') e.file = v->str;
            if (const Json *v = it.second.get("volume"); v && v->type == 'n' && v->num >= 0 && v->num <= 4) e.volume = (float)v->num;
            if (!e.file.empty()) sounds.push_back(e);
        }
    if (const Json *r = root.get("replace"))
        for (const auto &it : r->items)
            if (it.second.type == 's') replace[atoi(it.first.c_str())] = it.second.str;
    return true;
}

static std::string json_text(const std::string &s) {
    std::string out;
    for (char c : s) out += c == '"' || c == '\\' ? std::string("\\") + c : std::string(1, c);
    return out;
}

bool SoundLibrary::save() {
    SDL_CreateDirectory(dir.c_str());
    std::ofstream f(dir + "/sounds.json", std::ios::binary);
    if (!f) {
        error = "can't write " + dir + "/sounds.json";
        return false;
    }
    char b[64];
    f << "{\n  \"schema_version\": 1,\n  \"sounds\": {";
    for (size_t i = 0; i < sounds.size(); i++) {
        snprintf(b, sizeof b, "%g", sounds[i].volume);
        f << (i ? ",\n" : "\n") << "    \"" << json_text(sounds[i].name) << "\": {\"file\": \"" << json_text(sounds[i].file) << "\", \"volume\": " << b << "}";
    }
    f << (sounds.empty() ? "" : "\n  ") << "},\n  \"replace\": {";
    bool first = true;
    for (const auto &r : replace) {
        f << (first ? "\n" : ",\n") << "    \"" << r.first << "\": \"" << json_text(r.second) << "\"";
        first = false;
    }
    f << (first ? "" : "\n  ") << "}\n}\n";
    return true;
}

std::string SoundLibrary::import(const std::string &path) {
    size_t slash = path.find_last_of("/\\");
    std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
    SDL_CreateDirectory(dir.c_str());
    std::error_code ec;
    /* the same file picked from the library's own folder is already there (copying it onto itself would empty it) */
    if (!std::filesystem::equivalent(std::filesystem::u8path(path), std::filesystem::u8path(dir + "/" + file), ec) &&
        !SDL_CopyFile(path.c_str(), (dir + "/" + file).c_str())) {
        error = "can't copy " + path + ": " + SDL_GetError();
        return "";
    }
    std::string base = sound_name(file), name = base;
    for (int n = 2; find(name) >= 0; n++) {
        std::string tail = "_" + std::to_string(n);
        name = base.substr(0, 32 - tail.size()) + tail;
    }
    sounds.push_back({name, file, 1});
    return name;
}

void SoundLibrary::remove(const std::string &name) {
    int i = find(name);
    if (i < 0) return;
    std::string file = sounds[i].file;
    sounds.erase(sounds.begin() + i);
    bool shared = false; /* another name may use the same file */
    for (const SoundEntry &e : sounds) shared |= e.file == file;
    if (!shared) SDL_RemovePath((dir + "/" + file).c_str());
    for (auto it = replace.begin(); it != replace.end();) it = it->second == name ? replace.erase(it) : std::next(it);
}
