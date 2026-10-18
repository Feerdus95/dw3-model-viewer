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
    if (!parse(p, root) || root.type != '{') return false;
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
