/* DW3 Model Viewer: Digimon World 2003's battle models, their textures,
   bones and motions, in the spirit of Half-Life's model viewer.

   dw3mv [file.bin]            opens the viewer (and a model)
   dw3mv --shot out.bmp [--motion N] [--key K] file.bin
                               draws one frame of the model to a BMP and exits
   dw3mv --check               reads every model of the disc and prints what it found

   dw3mv --data DIR            reads the data from DIR instead

   The data is what tools/setup_disc.py reads from your disc: the models'
   files (files/<fid>.bin) and the disc's names (names.txt). */
#define GL_GLEXT_PROTOTYPES
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_main.h>
#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl3.h"
#include "model.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#endif

/* A window app has no console: the command-line modes print to the one they
   were started from */
static void attach_console() {
#ifdef _WIN32
    if (_isatty(1) || GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) != FILE_TYPE_UNKNOWN) return; /* redirected */
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
#endif
}

/* An error the user has to see: a message box in the window app */
static int fatal(const char *what) {
    fprintf(stderr, "%s\n", what);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DW3 Model Viewer", what, nullptr);
    return 1;
}

/* --- OpenGL 3.3, through SDL (opengl32 only exports 1.1) --- */
#define GL_FUNCS(X)                                                                                              \
    X(glEnable) X(glDisable) X(glViewport) X(glClear) X(glClearColor) X(glGenTextures) X(glDeleteTextures)        \
    X(glBindTexture) X(glTexImage2D) X(glTexParameteri) X(glPixelStorei) X(glReadPixels) X(glDrawArrays)          \
    X(glActiveTexture) X(glBlendFunc) X(glBlendEquation) X(glDepthMask) X(glDepthFunc) X(glCullFace)              \
    X(glFrontFace) X(glPolygonMode) X(glCreateShader) X(glShaderSource) X(glCompileShader) X(glGetShaderiv)       \
    X(glGetShaderInfoLog) X(glCreateProgram) X(glAttachShader) X(glLinkProgram) X(glGetProgramiv)                 \
    X(glGetProgramInfoLog) X(glUseProgram) X(glGetUniformLocation) X(glUniform1i) X(glUniform1f) X(glUniform3f)   \
    X(glUniform4f) X(glUniform3fv) X(glUniformMatrix4fv) X(glGenVertexArrays) X(glBindVertexArray) X(glGenBuffers)                \
    X(glDeleteBuffers) X(glBindBuffer) X(glBufferData) X(glEnableVertexAttribArray) X(glVertexAttribPointer)     \
    X(glVertexAttribIPointer) X(glGenFramebuffers) X(glBindFramebuffer) X(glFramebufferTexture2D)                \
    X(glGenRenderbuffers) X(glBindRenderbuffer) X(glRenderbufferStorage) X(glFramebufferRenderbuffer)

#define X(f) static decltype(&f) p_##f;
GL_FUNCS(X)
#undef X
#define GL(f) p_##f

static bool gl_load() {
#define X(f) if (!(p_##f = (decltype(&f))SDL_GL_GetProcAddress(#f))) return false;
    GL_FUNCS(X)
#undef X
    return true;
}

/* --- shaders --- */

/* mode: 1 textured, 2 lit, 4-8 texel depth, 16 semi-transparent, 32-64 its abr */
static const char *MESH_VS = R"(#version 330 core
layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 nrm;
layout(location = 2) in vec2 uv_in;
layout(location = 3) in vec4 color;
layout(location = 4) in uvec4 tex;    /* CLUT x, y, page x, y */
layout(location = 5) in uvec4 limits; /* u, v min, max */
layout(location = 6) in uint mode;
uniform mat4 mvp, world;
uniform vec3 light;
uniform int lighting; /* 0 none, 1 the viewer's light, 2 the stage's (the GTE's NCS) */
uniform vec3 ldir[3], lcol[3], amb;
out vec2 uv;
out vec3 col;
flat out uvec4 t, lim;
flat out uint md;
void main() {
    gl_Position = mvp * vec4(pos, 1.0);
    uv = uv_in; t = tex; lim = limits; md = mode;
    col = color.rgb * 255.0;
    if ((mode & 2u) != 0u) {
        if (lighting == 1) {
            vec3 n = normalize(mat3(world) * nrm);
            col = vec3(128.0 * (0.45 + 0.75 * max(dot(n, light), 0.0)));
        } else if (lighting == 2) {
            /* FIGHTSTG_lightMesh: the light matrix through the bone's, each
               light's color by how much the normal faces it, plus the back
               color, clamped as the GTE does */
            vec3 n = mat3(world) * nrm, c = amb;
            for (int i = 0; i < 3; i++) c += lcol[i] * max(dot(ldir[i], n), 0.0);
            col = min(c, vec3(255.0));
        } else {
            col = vec3(128.0);
        }
    }
}
)";

static const char *MESH_FS = R"(#version 330 core
uniform usampler2D vram;
uniform int pass;     /* 0 the opaque, 1 the semi-transparent of abr */
uniform int abr;
uniform int textures; /* 0 off, 1 nearest, 2 bilinear */
uniform vec4 tint;    /* a highlight, alpha its amount */
in vec2 uv;
in vec3 col;
flat in uvec4 t, lim;
flat in uint md;
out vec4 frag;
uint fetch(int x, int y) { return texelFetch(vram, ivec2(x & 1023, y & 511), 0).r; }
uint texel(int u, int v) {
    u = clamp(u, int(lim.x), int(lim.z)) & 255;
    v = clamp(v, int(lim.y), int(lim.w)) & 255;
    uint depth = (md >> 2) & 3u;
    if (depth == 0u) {
        uint w = fetch(int(t.z) + u / 4, int(t.w) + v);
        return fetch(int(t.x) + int((w >> uint((u & 3) * 4)) & 15u), int(t.y));
    } else if (depth == 1u) {
        uint w = fetch(int(t.z) + u / 2, int(t.w) + v);
        return fetch(int(t.x) + int((w >> uint((u & 1) * 8)) & 255u), int(t.y));
    }
    return fetch(int(t.z) + u, int(t.w) + v);
}
vec3 rgb(uint p) { return vec3(p & 31u, (p >> 5) & 31u, (p >> 10) & 31u) / 31.0; }
void main() {
    vec3 c;
    bool semi = (md & 16u) != 0u;
    if ((md & 1u) != 0u && textures != 0) {
        uint pix;
        if (textures == 1) {
            pix = texel(int(floor(uv.x)), int(floor(uv.y)));
            if (pix == 0u) discard;
            c = rgb(pix);
        } else {
            vec2 at = uv - 0.5, f = fract(at);
            ivec2 i = ivec2(floor(at));
            uint p00 = texel(i.x, i.y), p10 = texel(i.x + 1, i.y), p01 = texel(i.x, i.y + 1), p11 = texel(i.x + 1, i.y + 1);
            vec4 a = mix(mix(vec4(rgb(p00), p00 != 0u), vec4(rgb(p10), p10 != 0u), f.x),
                         mix(vec4(rgb(p01), p01 != 0u), vec4(rgb(p11), p11 != 0u), f.x), f.y);
            if (a.a < 0.5) discard;
            c = a.rgb / a.a;
            pix = f.x < 0.5 ? (f.y < 0.5 ? p00 : p01) : (f.y < 0.5 ? p10 : p11);
        }
        semi = semi && (pix & 0x8000u) != 0u;
        c = min(c * col / 128.0, 1.0);
    } else {
        c = (md & 2u) != 0u ? min(col / 160.0, 1.0) : col / 255.0;
    }
    if (pass == 0 && semi) discard;
    if (pass == 1 && (!semi || int((md >> 5) & 3u) != abr)) discard;
    c = mix(c, tint.rgb, tint.a);
    frag = vec4(c, abr == 0 ? 0.5 : abr == 3 ? 0.25 : 1.0);
    if (pass == 1 && abr == 3) frag.rgb *= 0.25;
}
)";

static const char *LINE_VS = R"(#version 330 core
layout(location = 0) in vec3 pos;
layout(location = 1) in vec4 color;
uniform mat4 mvp;
out vec4 col;
void main() { gl_Position = mvp * vec4(pos, 1.0); col = color; }
)";

static const char *LINE_FS = R"(#version 330 core
in vec4 col;
out vec4 frag;
void main() { frag = col; }
)";

static GLuint compile(const char *vs, const char *fs) {
    GLuint p = GL(glCreateProgram)();
    const char *src[2] = {vs, fs};
    GLenum type[2] = {GL_VERTEX_SHADER, GL_FRAGMENT_SHADER};
    for (int i = 0; i < 2; i++) {
        GLuint s = GL(glCreateShader)(type[i]);
        GLint ok;
        GL(glShaderSource)(s, 1, &src[i], nullptr);
        GL(glCompileShader)(s);
        GL(glGetShaderiv)(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048];
            GL(glGetShaderInfoLog)(s, sizeof log, nullptr, log);
            fatal(log);
            return 0;
        }
        GL(glAttachShader)(p, s);
    }
    GLint ok;
    GL(glLinkProgram)(p);
    GL(glGetProgramiv)(p, GL_LINK_STATUS, &ok);
    return ok ? p : 0;
}

/* --- small matrix math, column-major --- */

struct M4 {
    float m[16];
};

static M4 mul(const M4 &a, const M4 &b) {
    M4 r;
    for (int c = 0; c < 4; c++)
        for (int i = 0; i < 4; i++)
            r.m[c * 4 + i] = a.m[i] * b.m[c * 4] + a.m[4 + i] * b.m[c * 4 + 1] + a.m[8 + i] * b.m[c * 4 + 2] + a.m[12 + i] * b.m[c * 4 + 3];
    return r;
}

static M4 from(const float *f) {
    M4 r;
    memcpy(r.m, f, sizeof r.m);
    return r;
}

static M4 perspective(float fovy, float aspect, float n, float f) {
    float t = 1.0f / tanf(fovy / 2);
    M4 r = {};
    r.m[0] = t / aspect, r.m[5] = t, r.m[10] = (f + n) / (n - f), r.m[11] = -1, r.m[14] = 2 * f * n / (n - f);
    return r;
}

/* The camera, orbiting its target; the PSX's y is down and its z away from
   the camera, so the model goes through diag(1, -1, -1) */
struct Camera {
    float yaw = 0.6f, pitch = 0.25f, dist = 1500, target[3] = {0, 300, 0};
    M4 view() const {
        float e[3] = {target[0] + dist * cosf(pitch) * sinf(yaw), target[1] + dist * sinf(pitch),
                      target[2] + dist * cosf(pitch) * cosf(yaw)};
        float f[3] = {target[0] - e[0], target[1] - e[1], target[2] - e[2]};
        float fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
        for (float &x : f) x /= fl;
        float s[3] = {f[1] * 0 - f[2] * 1, f[2] * 0 - f[0] * 0, f[0] * 1 - f[1] * 0}; /* f x up */
        float sl = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
        for (float &x : s) x /= sl;
        float u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};
        M4 r = {{s[0], u[0], -f[0], 0, s[1], u[1], -f[1], 0, s[2], u[2], -f[2], 0, 0, 0, 0, 1}};
        r.m[12] = -(s[0] * e[0] + s[1] * e[1] + s[2] * e[2]);
        r.m[13] = -(u[0] * e[0] + u[1] * e[1] + u[2] * e[2]);
        r.m[14] = f[0] * e[0] + f[1] * e[1] + f[2] * e[2];
        return r;
    }
};

static const M4 PSX_TO_GL = {{1, 0, 0, 0, 0, -1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1}};

/* --- the files --- */

static std::string g_data;                  /* data/files */
static std::vector<std::string> g_names;    /* the disc's names, by fid */
static std::string g_exe_dir;
static GameData g_game;                     /* fighters, stages, names */

static bool read_file(const std::string &path, std::vector<uint8_t> &out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

static std::string fid_path(int fid) {
    char b[16];
    snprintf(b, sizeof b, "/%04d.bin", fid);
    return g_data + b;
}

static std::string fid_name(int fid) {
    return fid < (int)g_names.size() ? g_names[fid] : "#" + std::to_string(fid);
}

/* The data tools/setup_disc.py writes: files/<fid>.bin and names.txt, in
   data/ (from the exe in build/) or next to the exe */
static void find_data(const std::string &given) {
    std::vector<std::string> tries = {g_exe_dir + "../data", g_exe_dir + "data"};
    if (!given.empty()) tries.insert(tries.begin(), given);
    for (const std::string &dir : tries) {
        std::ifstream names(dir + "/names.txt");
        if (!names) continue;
        g_data = dir + "/files";
        for (std::string line; std::getline(names, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            g_names.push_back(line);
        }
        g_game.load(g_data);
        return;
    }
    g_data = tries[0] + "/files";
}

/* The disc's models: the files there whose names start M (FIGHT/MODEL,
   FIGHT/EFFECT) and that have a bone list */
struct Entry {
    int fid;
    std::string name;
    std::string label; /* who it is: the fighters' names, or the technique's */
};

static std::vector<Entry> scan_models() {
    std::vector<Entry> out;
    std::vector<uint8_t> bytes;
    for (int fid = 1; fid < (int)g_names.size(); fid++) {
        const std::string &n = g_names[fid];
        if (n.empty() || n[0] != 'M' || !read_file(fid_path(fid), bytes)) continue;
        Archive a{bytes.data(), bytes.size()};
        if (a.count() > 0 && (size_t)a.count() * 4 <= a.size && find_bone_entry(a) >= 0) out.push_back({fid, n, g_game.fileLabel(fid, n)});
    }
    return out;
}

/* --- the model on the card --- */

struct GVert {
    float pos[3], nrm[3], uv[2];
    uint8_t color[4];
    uint16_t tex[4], limits[4];
    uint32_t mode;
};

struct Scene {
    std::unique_ptr<Model> model;
    std::vector<int> first, count; /* each bone's vertices */
    std::vector<bool> hidden;
    int selected = -1;
    GLuint vao = 0, vbo = 0, vram = 0;
    float radius = 500, center[3] = {0, 0, 0};
    int fid = -1;
    std::string path;
    std::vector<const Fighter *> fighters; /* who uses the file */
    int fighter = 0;                       /* the one shown */
};

static void upload_vram(Scene &s) {
    GL(glBindTexture)(GL_TEXTURE_2D, s.vram);
    GL(glPixelStorei)(GL_UNPACK_ALIGNMENT, 2);
    GL(glTexImage2D)(GL_TEXTURE_2D, 0, GL_R16UI, 1024, 512, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, s.model->vram.data());
    GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

static void upload(Scene &s) {
    Model &m = *s.model;
    std::vector<GVert> v;
    s.first.assign(m.bones.size(), 0);
    s.count.assign(m.bones.size(), 0);
    s.hidden.assign(m.bones.size(), false);
    for (size_t b = 1; b < m.bones.size(); b++) {
        const Mesh &mesh = m.meshes[m.bones[b].mesh];
        s.first[b] = (int)v.size();
        for (const Poly &p : mesh.polys) {
            int umin = 255, vmin = 255, umax = 0, vmax = 0;
            int n = p.quad ? 4 : 3;
            for (int k = 0; k < n; k++) {
                umin = std::min<int>(umin, p.uv[k][0]), umax = std::max<int>(umax, p.uv[k][0]);
                vmin = std::min<int>(vmin, p.uv[k][1]), vmax = std::max<int>(vmax, p.uv[k][1]);
            }
            static const int TRI[2][3] = {{0, 1, 2}, {1, 3, 2}}; /* a PSX quad: 0 1 / 2 3 */
            for (int t = 0; t < (p.quad ? 2 : 1); t++) {
                for (int k : TRI[t]) {
                    GVert g = {};
                    const Vec3s &pv = mesh.verts[p.idx[k]];
                    g.pos[0] = pv.x, g.pos[1] = pv.y, g.pos[2] = pv.z;
                    if (p.lit) {
                        const Vec3s &pn = mesh.normals[p.nrm[p.gouraud ? k : 0]];
                        g.nrm[0] = pn.x / 4096.0f, g.nrm[1] = pn.y / 4096.0f, g.nrm[2] = pn.z / 4096.0f;
                    }
                    g.uv[0] = p.uv[k][0] + 0.5f, g.uv[1] = p.uv[k][1] + 0.5f;
                    g.color[0] = p.color[0], g.color[1] = p.color[1], g.color[2] = p.color[2], g.color[3] = 255;
                    g.tex[0] = (uint16_t)clut_x(p.clut), g.tex[1] = (uint16_t)clut_y(p.clut);
                    g.tex[2] = (uint16_t)tpage_x(p.tpage), g.tex[3] = (uint16_t)tpage_y(p.tpage);
                    g.limits[0] = (uint16_t)umin, g.limits[1] = (uint16_t)vmin, g.limits[2] = (uint16_t)umax, g.limits[3] = (uint16_t)vmax;
                    g.mode = (p.textured ? 1 : 0) | (p.lit ? 2 : 0) | (tpage_depth(p.tpage) & 3) << 2 |
                             (p.abr ? 16 : 0) | ((p.abr ? p.abr - 1 : 0) & 3) << 5;
                    v.push_back(g);
                }
            }
        }
        s.count[b] = (int)v.size() - s.first[b];
    }
    if (!s.vao) {
        GL(glGenVertexArrays)(1, &s.vao);
        GL(glGenBuffers)(1, &s.vbo);
        GL(glGenTextures)(1, &s.vram);
    }
    GL(glBindVertexArray)(s.vao);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, s.vbo);
    GL(glBufferData)(GL_ARRAY_BUFFER, v.size() * sizeof(GVert), v.data(), GL_STATIC_DRAW);
    for (int i = 0; i < 7; i++) GL(glEnableVertexAttribArray)(i);
    GL(glVertexAttribPointer)(0, 3, GL_FLOAT, GL_FALSE, sizeof(GVert), (void *)offsetof(GVert, pos));
    GL(glVertexAttribPointer)(1, 3, GL_FLOAT, GL_FALSE, sizeof(GVert), (void *)offsetof(GVert, nrm));
    GL(glVertexAttribPointer)(2, 2, GL_FLOAT, GL_FALSE, sizeof(GVert), (void *)offsetof(GVert, uv));
    GL(glVertexAttribPointer)(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GVert), (void *)offsetof(GVert, color));
    GL(glVertexAttribIPointer)(4, 4, GL_UNSIGNED_SHORT, sizeof(GVert), (void *)offsetof(GVert, tex));
    GL(glVertexAttribIPointer)(5, 4, GL_UNSIGNED_SHORT, sizeof(GVert), (void *)offsetof(GVert, limits));
    GL(glVertexAttribIPointer)(6, 1, GL_UNSIGNED_INT, sizeof(GVert), (void *)offsetof(GVert, mode));
    upload_vram(s);

    /* the camera's target: the posed model's box */
    float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
    for (size_t b = 1; b < m.bones.size(); b++) {
        if (!m.pose[b].visible) continue;
        const float *w = m.pose[b].world;
        for (const Vec3s &pv : m.meshes[m.bones[b].mesh].verts) {
            float p[3];
            for (int i = 0; i < 3; i++) p[i] = w[i] * pv.x + w[4 + i] * pv.y + w[8 + i] * pv.z + w[12 + i];
            p[1] = -p[1], p[2] = -p[2];
            for (int i = 0; i < 3; i++) lo[i] = std::min(lo[i], p[i]), hi[i] = std::max(hi[i], p[i]);
        }
    }
    if (lo[0] <= hi[0]) {
        float r = 0;
        for (int i = 0; i < 3; i++) s.center[i] = (lo[i] + hi[i]) / 2, r = std::max(r, hi[i] - lo[i]);
        s.radius = std::max(r / 2, 50.0f);
    }
}

/* --- the textures, decoded on the CPU for the textures' tab --- */

static uint32_t rgba(uint16_t p) {
    uint32_t r = (p & 31) * 255 / 31, g = (p >> 5 & 31) * 255 / 31, b = (p >> 10 & 31) * 255 / 31;
    return r | g << 8 | b << 16 | (p ? 0xFF000000u : 0);
}

/* A texture page as its polygons see it: 256 texels wide (64 halfwords at 16-bit) */
static std::vector<uint32_t> decode_page(const std::vector<uint16_t> &vram, uint16_t tpage, uint16_t clut, int w, int h) {
    std::vector<uint32_t> out(w * h);
    int px = tpage_x(tpage), py = tpage_y(tpage), cx = clut_x(clut), cy = clut_y(clut), depth = tpage_depth(tpage);
    auto at = [&](int x, int y) { return vram[(y & 511) * 1024 + (x & 1023)]; };
    for (int v = 0; v < h; v++)
        for (int u = 0; u < w; u++) {
            uint16_t p;
            if (depth == 0)
                p = at(cx + (at(px + u / 4, py + v) >> (u & 3) * 4 & 15), cy);
            else if (depth == 1)
                p = at(cx + (at(px + u / 2, py + v) >> (u & 1) * 8 & 255), cy);
            else
                p = at(px + u, py + v);
            out[v * w + u] = rgba(p);
        }
    return out;
}

static GLuint rgba_texture(GLuint tex, const std::vector<uint32_t> &px, int w, int h) {
    if (!tex) GL(glGenTextures)(1, &tex);
    GL(glBindTexture)(GL_TEXTURE_2D, tex);
    GL(glPixelStorei)(GL_UNPACK_ALIGNMENT, 4);
    GL(glTexImage2D)(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return tex;
}

static void save_bmp(const std::string &path, const std::vector<uint32_t> &px, int w, int h) {
    SDL_Surface *s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ABGR8888, (void *)px.data(), w * 4);
    if (s) {
        SDL_SaveBMP(s, path.c_str());
        SDL_DestroySurface(s);
    }
}

/* --- the viewer --- */

struct View {
    Camera cam;
    int textures = 1; /* 0 off, 1 as the PSX, 2 bilinear */
    int lighting = 2; /* 0 off, 1 the viewer's light, 2 a stage's */
    int stage = 0;
    float fov = 0.8f; /* vertical, radians; a game camera's from its projection */
    int gameCamera = -1;
    bool face = true;
    bool wireframe = false, skeleton = false, cull = false, grid = true;
    bool playing = true, loop = true;
    float speed = 1, ticks = 0;
    int texCombo = 0;
    float texZoom = 2;
    bool uvs = true;
};

static GLuint g_mesh, g_line, g_lineVao, g_lineVbo;

static void draw_lines(const std::vector<float> &v, const M4 &mvp) {
    if (v.empty()) return;
    GL(glUseProgram)(g_line);
    GL(glUniformMatrix4fv)(GL(glGetUniformLocation)(g_line, "mvp"), 1, GL_FALSE, mvp.m);
    GL(glBindVertexArray)(g_lineVao);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, g_lineVbo);
    GL(glBufferData)(GL_ARRAY_BUFFER, v.size() * 4, v.data(), GL_STREAM_DRAW);
    GL(glDrawArrays)(GL_LINES, 0, (GLsizei)(v.size() / 7));
}

static void line(std::vector<float> &v, const float *a, const float *b, float r, float g, float bl) {
    float c[2][7] = {{a[0], a[1], a[2], r, g, bl, 1}, {b[0], b[1], b[2], r, g, bl, 1}};
    v.insert(v.end(), &c[0][0], &c[0][0] + 14);
}

static void render(Scene &s, View &v, int w, int h) {
    GL(glViewport)(0, 0, w, h);
    GL(glClearColor)(0.18f, 0.2f, 0.24f, 1);
    GL(glClear)(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    M4 proj = perspective(v.fov, (float)w / std::max(h, 1), v.cam.dist * 0.01f, v.cam.dist * 50);
    M4 vp = mul(proj, v.cam.view());
    if (v.grid) {
        std::vector<float> g;
        float step = 250, n = 8;
        for (int i = -8; i <= 8; i++) {
            float a[3] = {i * step, 0, -n * step}, b[3] = {i * step, 0, n * step};
            float c[3] = {-n * step, 0, i * step}, d[3] = {n * step, 0, i * step};
            float k = i == 0 ? 0.5f : 0.3f;
            line(g, a, b, k, k, k);
            line(g, c, d, k, k, k);
        }
        GL(glEnable)(GL_DEPTH_TEST);
        GL(glDepthMask)(GL_TRUE);
        draw_lines(g, vp);
    }
    if (!s.model) return;
    Model &m = *s.model;
    GL(glEnable)(GL_DEPTH_TEST);
    GL(glDepthFunc)(GL_LEQUAL);
    if (v.cull) {
        GL(glEnable)(GL_CULL_FACE);
        GL(glFrontFace)(GL_CW);
        GL(glCullFace)(GL_BACK);
    }
    GL(glPolygonMode)(GL_FRONT_AND_BACK, v.wireframe ? GL_LINE : GL_FILL);
    GL(glUseProgram)(g_mesh);
    GL(glActiveTexture)(GL_TEXTURE0);
    GL(glBindTexture)(GL_TEXTURE_2D, s.vram);
    GL(glUniform1i)(GL(glGetUniformLocation)(g_mesh, "vram"), 0);
    GL(glUniform1i)(GL(glGetUniformLocation)(g_mesh, "textures"), v.textures);
    int lighting = v.lighting == 2 && g_game.stages.empty() ? 1 : v.lighting;
    GL(glUniform1i)(GL(glGetUniformLocation)(g_mesh, "lighting"), lighting);
    if (lighting == 2) {
        const StageLights &l = g_game.stages[std::clamp(v.stage, 0, (int)g_game.stages.size() - 1)];
        GL(glUniform3fv)(GL(glGetUniformLocation)(g_mesh, "ldir"), 3, &l.dir[0][0]);
        GL(glUniform3fv)(GL(glGetUniformLocation)(g_mesh, "lcol"), 3, &l.color[0][0]);
        GL(glUniform3fv)(GL(glGetUniformLocation)(g_mesh, "amb"), 1, l.ambient);
    }
    /* the light: over the camera's shoulder, in the model's (PSX) space */
    float lx = sinf(v.cam.yaw + 0.5f), lz = cosf(v.cam.yaw + 0.5f);
    GL(glUniform3f)(GL(glGetUniformLocation)(g_mesh, "light"), lx * 0.7f, -0.6f, -lz * 0.7f);
    GL(glBindVertexArray)(s.vao);
    GLint loc_mvp = GL(glGetUniformLocation)(g_mesh, "mvp"), loc_world = GL(glGetUniformLocation)(g_mesh, "world");
    GLint loc_pass = GL(glGetUniformLocation)(g_mesh, "pass"), loc_abr = GL(glGetUniformLocation)(g_mesh, "abr");
    GLint loc_tint = GL(glGetUniformLocation)(g_mesh, "tint");
    auto draw_bones = [&]() {
        for (size_t b = 1; b < m.bones.size(); b++) {
            if (!m.pose[b].visible || s.hidden[b] || !s.count[b]) continue;
            M4 mvp = mul(vp, mul(PSX_TO_GL, from(m.pose[b].world)));
            GL(glUniformMatrix4fv)(loc_mvp, 1, GL_FALSE, mvp.m);
            GL(glUniformMatrix4fv)(loc_world, 1, GL_FALSE, m.pose[b].world);
            float t = (int)b == s.selected ? 0.35f : 0;
            GL(glUniform4f)(loc_tint, 1, 0.85f, 0.2f, t);
            GL(glDrawArrays)(GL_TRIANGLES, s.first[b], s.count[b]);
        }
    };
    GL(glUniform1i)(loc_pass, 0);
    GL(glUniform1i)(loc_abr, -1);
    GL(glDepthMask)(GL_TRUE);
    draw_bones();
    /* the semi-transparent, each blending its own way */
    GL(glEnable)(GL_BLEND);
    GL(glDepthMask)(GL_FALSE);
    GL(glUniform1i)(loc_pass, 1);
    for (int a = 0; a < 4; a++) {
        GL(glBlendEquation)(a == 2 ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD);
        if (a == 0)
            GL(glBlendFunc)(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        else
            GL(glBlendFunc)(GL_ONE, GL_ONE);
        GL(glUniform1i)(loc_abr, a);
        draw_bones();
    }
    GL(glBlendEquation)(GL_FUNC_ADD);
    GL(glDisable)(GL_BLEND);
    GL(glDepthMask)(GL_TRUE);
    GL(glDisable)(GL_CULL_FACE);
    GL(glPolygonMode)(GL_FRONT_AND_BACK, GL_FILL);
    if (v.skeleton) {
        std::vector<float> l;
        for (size_t b = 1; b < m.bones.size(); b++) {
            const float *w = m.pose[b].world, *pw = m.pose[m.bones[b].parent].world;
            float a[3] = {pw[12], -pw[13], -pw[14]}, c[3] = {w[12], -w[13], -w[14]};
            bool sel = (int)b == s.selected;
            line(l, a, c, sel ? 1.0f : 0.2f, sel ? 0.9f : 1.0f, sel ? 0.2f : 0.4f);
        }
        GL(glDisable)(GL_DEPTH_TEST);
        draw_lines(l, vp);
    }
}

static void frame_model(Scene &s, View &v) {
    v.cam.target[0] = s.center[0], v.cam.target[1] = s.center[1], v.cam.target[2] = s.center[2];
    v.cam.dist = s.radius * 3.4f;
    v.fov = 0.8f;
    v.gameCamera = -1;
}

/* A fighter's camera: at camPos looking at camRef, in the battle's space.
   The game negates their y and z into the PSX's (FIGHTSTG_getFighterView),
   which the viewer's own flip takes back. There the fighter stands at its
   side's home (FIGHTSTG_faceModel): a partner 0x1400 + distance towards the
   viewer's +z, turned round to face the enemy, an enemy as far the other way;
   both raised by height. The viewer's model stands at the origin, so the
   camera comes into its space. Its projection is on a 240-line screen. */
static void apply_camera(View &v, const Fighter &f, int index) {
    const GameCamera &c = f.cameras[index];
    float home = (float)(0x1400 + f.distance) * (f.enemy ? -1 : 1);
    auto local = [&](const int16_t *p, float *out) {
        float x = p[0], y = p[1] - (float)f.height, z = p[2] - home;
        out[0] = f.enemy ? x : -x, out[1] = y, out[2] = f.enemy ? z : -z; /* a partner's turn: 0x800 about y */
    };
    float eye[3], ref[3];
    local(c.pos, eye);
    local(c.ref, ref);
    float d[3] = {eye[0] - ref[0], eye[1] - ref[1], eye[2] - ref[2]};
    float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len < 1) return;
    for (int i = 0; i < 3; i++) v.cam.target[i] = ref[i];
    v.cam.dist = len;
    v.cam.pitch = asinf(d[1] / len);
    v.cam.yaw = atan2f(d[0], d[2]);
    if (c.proj > 0) v.fov = 2 * atanf(120.0f / c.proj);
    v.gameCamera = index;
}

static bool open_model(Scene &s, View &v, const std::string &name, const std::string &path, std::string &status,
                       int fid = -1, int fighter = 0) {
    std::vector<uint8_t> bytes;
    if (!read_file(path, bytes)) {
        status = "can't read " + path;
        return false;
    }
    std::vector<const Fighter *> fighters = g_game.forFile(fid);
    fighter = fighters.empty() ? 0 : std::clamp(fighter, 0, (int)fighters.size() - 1);
    auto m = std::make_unique<Model>();
    if (!m->load(name, bytes, fighters.empty() ? nullptr : fighters[fighter])) {
        /* a fighter whose entries don't read: the file on its own */
        m = std::make_unique<Model>();
        fighters.clear();
        fighter = 0;
        if (!m->load(name, std::move(bytes))) {
            status = name + ": " + m->error;
            return false;
        }
    }
    s.fid = fid, s.path = path, s.fighters = fighters, s.fighter = fighter;
    s.model = std::move(m);
    s.selected = -1;
    upload(s);
    frame_model(s, v);
    v.texCombo = 0;
    v.ticks = 0;
    status = "";
    return true;
}

static void bone_tree(Scene &s, int b) {
    Model &m = *s.model;
    std::vector<int> kids;
    for (size_t c = 1; c < m.bones.size(); c++)
        if (m.bones[c].parent == b && (int)c != b) kids.push_back((int)c);
    char label[64];
    const Mesh *mesh = b ? &m.meshes[m.bones[b].mesh] : nullptr;
    if (b == 0)
        snprintf(label, sizeof label, "root");
    else
        snprintf(label, sizeof label, "bone %d  (%d polys)", b, (int)mesh->polys.size());
    ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (kids.empty()) fl |= ImGuiTreeNodeFlags_Leaf;
    if (b == s.selected) fl |= ImGuiTreeNodeFlags_Selected;
    ImGui::PushID(b);
    if (b) {
        bool shown = !s.hidden[b];
        if (ImGui::Checkbox("##v", &shown)) s.hidden[b] = !shown;
        ImGui::SameLine();
    }
    bool open = ImGui::TreeNodeEx(label, fl);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) s.selected = s.selected == b ? -1 : b;
    if (b && ImGui::IsItemHovered()) {
        const Bone &bone = m.bones[b];
        const Pose &p = m.pose[b];
        ImGui::SetTooltip("mesh entry %d, keys entry %d\n%d vertices, %d normals\npos %d %d %d\nrot %d %d %d\nscale %d %d %d",
                          mesh->entry, bone.keyEntry, (int)mesh->verts.size(), (int)mesh->normals.size(), p.pos.x, p.pos.y,
                          p.pos.z, p.rot.x, p.rot.y, p.rot.z, p.scale.x, p.scale.y, p.scale.z);
    }
    if (open) {
        for (int c : kids) bone_tree(s, c);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

int main(int argc, char **argv) {
    std::string shot, open_path, data;
    int shot_motion = 1, shot_key = 0, shot_cam = -1, shot_stage = 0, shot_fighter = 0;
    bool check = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--shot" && i + 1 < argc) shot = argv[++i];
        else if (a == "--motion" && i + 1 < argc) shot_motion = atoi(argv[++i]);
        else if (a == "--key" && i + 1 < argc) shot_key = atoi(argv[++i]);
        else if (a == "--cam" && i + 1 < argc) shot_cam = atoi(argv[++i]);
        else if (a == "--stage" && i + 1 < argc) shot_stage = atoi(argv[++i]);
        else if (a == "--fighter" && i + 1 < argc) shot_fighter = atoi(argv[++i]);
        else if (a == "--data" && i + 1 < argc) data = argv[++i];
        else if (a == "--check") check = true;
        else open_path = a;
    }
    const char *base = SDL_GetBasePath();
    g_exe_dir = base ? base : "";
    find_data(data);

    if (check || !shot.empty()) attach_console();
    if (check) {
        std::vector<Entry> list = scan_models();
        int bad = 0;
        for (const Entry &e : list) {
            std::vector<uint8_t> bytes;
            read_file(fid_path(e.fid), bytes);
            std::vector<const Fighter *> fs = g_game.forFile(e.fid);
            Model m;
            if (!m.load(e.name, std::move(bytes), fs.empty() ? nullptr : fs[0])) {
                printf("%04d %-13s ERROR %s\n", e.fid, e.name.c_str(), m.error.c_str());
                bad++;
                continue;
            }
            size_t polys = 0;
            for (const Mesh &me : m.meshes) polys += me.polys.size();
            printf("%04d %-13s bones %3d polys %5d tims %d motions %2d scripts %2d  %s\n", e.fid, e.name.c_str(),
                   (int)m.bones.size() - 1, (int)polys, (int)m.tims.size(), (int)m.motions.size(), (int)m.scripts.size(),
                   e.label.c_str());
        }
        printf("%d models, %d unread\n", (int)list.size(), bad);
        return bad != 0;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return fatal(SDL_GetError());
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_Window *win = SDL_CreateWindow("DW3 Model Viewer", 1440, 900,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                           (shot.empty() ? 0 : SDL_WINDOW_HIDDEN));
    if (!win) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        win = SDL_CreateWindow("DW3 Model Viewer", 1440, 900, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    }
    SDL_GLContext ctx = win ? SDL_GL_CreateContext(win) : nullptr;
    if (!ctx || !gl_load()) {
        return fatal((std::string("This needs a graphics card with OpenGL 3.3.\n") + SDL_GetError()).c_str());
    }
    SDL_GL_SetSwapInterval(1);
    g_mesh = compile(MESH_VS, MESH_FS);
    g_line = compile(LINE_VS, LINE_FS);
    if (!g_mesh || !g_line) return 1;
    GL(glGenVertexArrays)(1, &g_lineVao);
    GL(glGenBuffers)(1, &g_lineVbo);
    GL(glBindVertexArray)(g_lineVao);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, g_lineVbo);
    GL(glEnableVertexAttribArray)(0);
    GL(glEnableVertexAttribArray)(1);
    GL(glVertexAttribPointer)(0, 3, GL_FLOAT, GL_FALSE, 28, (void *)0);
    GL(glVertexAttribPointer)(1, 4, GL_FLOAT, GL_FALSE, 28, (void *)12);

    Scene scene;
    View view;
    std::string status;
    std::vector<Entry> list;
    bool listed = false;
    auto open_entry = [&](const std::string &path, int fighter) {
        std::string name = path.substr(path.find_last_of("/\\") + 1);
        int fid = -1;
        /* a fid's file goes by its disc name */
        if (name.size() == 8 && name.substr(4) == ".bin" && atoi(name.c_str()) > 0) {
            fid = atoi(name.c_str());
            name = fid_name(fid);
        }
        open_model(scene, view, name, path, status, fid, fighter);
    };
    if (!open_path.empty()) open_entry(open_path, shot_fighter);

    if (!shot.empty()) { /* one frame, drawn off screen */
        if (!scene.model) {
            fprintf(stderr, "%s\n", status.c_str());
            return 1;
        }
        scene.model->setMotion(shot_motion);
        for (int i = 0; i < shot_key; i++) {
            scene.model->evaluate();
            scene.model->advance(1);
            scene.model->stepFace();
        }
        scene.model->evaluate();
        upload_vram(scene);
        const int W = 960, H = 720;
        GLuint fbo, rb[2];
        GL(glGenFramebuffers)(1, &fbo);
        GL(glBindFramebuffer)(GL_FRAMEBUFFER, fbo);
        GL(glGenRenderbuffers)(2, rb);
        GL(glBindRenderbuffer)(GL_RENDERBUFFER, rb[0]);
        GL(glRenderbufferStorage)(GL_RENDERBUFFER, GL_RGBA8, W, H);
        GL(glFramebufferRenderbuffer)(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb[0]);
        GL(glBindRenderbuffer)(GL_RENDERBUFFER, rb[1]);
        GL(glRenderbufferStorage)(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, W, H);
        GL(glFramebufferRenderbuffer)(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb[1]);
        view.skeleton = false;
        view.stage = shot_stage;
        if (shot_cam >= 0 && !scene.fighters.empty() && shot_cam < (int)scene.fighters[scene.fighter]->cameras.size())
            apply_camera(view, *scene.fighters[scene.fighter], shot_cam);
        render(scene, view, W, H);
        std::vector<uint32_t> px(W * H), flipped(W * H);
        GL(glReadPixels)(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        for (int y = 0; y < H; y++) memcpy(&flipped[y * W], &px[(H - 1 - y) * W], W * 4);
        save_bmp(shot, flipped, W, H);
        printf("%s: motion %d key %d -> %s\n", scene.model->name.c_str(), shot_motion, shot_key, shot.c_str());
        return 0;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    float dpi = SDL_GetWindowDisplayScale(win);
    if (dpi > 0) {
        ImGui::GetStyle().ScaleAllSizes(dpi);
        io.FontGlobalScale = dpi;
    }
    ImGui_ImplSDL3_InitForOpenGL(win, ctx);
    ImGui_ImplOpenGL3_Init("#version 330");

    GLuint texView = 0;
    std::string texKey;
    std::vector<uint32_t> texPixels;
    int texW = 0, texH = 0;
    char filter[64] = "";
    uint64_t last = SDL_GetTicksNS();
    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) running = false;
            if (e.type == SDL_EVENT_DROP_FILE) open_entry(e.drop.data, 0);
            if (!io.WantCaptureMouse) {
                if (e.type == SDL_EVENT_MOUSE_MOTION && (e.motion.state & SDL_BUTTON_LMASK)) {
                    view.cam.yaw -= e.motion.xrel * 0.008f;
                    view.cam.pitch = std::clamp(view.cam.pitch + e.motion.yrel * 0.008f, -1.5f, 1.5f);
                } else if (e.type == SDL_EVENT_MOUSE_MOTION && (e.motion.state & (SDL_BUTTON_RMASK | SDL_BUTTON_MMASK))) {
                    /* pan in the view's plane */
                    float k = view.cam.dist * 0.0015f, cy = cosf(view.cam.yaw), sy = sinf(view.cam.yaw);
                    view.cam.target[0] -= e.motion.xrel * k * cy;
                    view.cam.target[2] += e.motion.xrel * k * sy;
                    view.cam.target[1] += e.motion.yrel * k;
                } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                    view.cam.dist *= powf(0.88f, e.wheel.y);
                }
            }
            if (e.type == SDL_EVENT_KEY_DOWN && !io.WantCaptureKeyboard && scene.model) {
                if (e.key.key == SDLK_SPACE) view.playing = !view.playing;
                if (e.key.key == SDLK_F) frame_model(scene, view);
            }
        }
        uint64_t now = SDL_GetTicksNS();
        float dt = (now - last) / 1e9f;
        last = now;

        /* the motion: one keyframe a PSX field (60 a second), as FIGHTSTG steps it */
        if (scene.model && view.playing && scene.model->keyframeCount > 0) {
            Model &m = *scene.model;
            view.ticks += dt * 60 * view.speed;
            int n = (int)view.ticks;
            view.ticks -= n;
            bool faced = false;
            for (int i = 0; i < std::min(n, 8); i++) {
                m.advance(1);
                if (m.motionDone && view.loop) m.setMotion(m.motion);
                m.evaluate();
                if (view.face) faced |= m.stepFace();
            }
            if (faced) upload_vram(scene);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        int ww, wh;
        SDL_GetWindowSizeInPixels(win, &ww, &wh);
        float panel = 420 * (dpi > 0 ? dpi : 1);
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - panel, 0));
        ImGui::SetNextWindowSize(ImVec2(panel, io.DisplaySize.y));
        ImGui::Begin("##panel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
        if (scene.model && !scene.fighters.empty() && !scene.fighters[scene.fighter]->name.empty())
            ImGui::Text("%s  (%s)", scene.fighters[scene.fighter]->name.c_str(), scene.model->name.c_str());
        else if (scene.model)
            ImGui::Text("%s", scene.model->name.c_str());
        else
            ImGui::TextWrapped("Pick a model, or drop a .BIN on the window.");
        if (!status.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", status.c_str());
        if (ImGui::BeginTabBar("tabs")) {
            if (ImGui::BeginTabItem("Files")) {
                if (!listed) {
                    list = scan_models();
                    listed = true;
                }
                if (list.empty()) ImGui::TextWrapped("No models found in %s: run tools/setup_disc.py with your disc.", g_data.c_str());
                ImGui::InputTextWithHint("##filter", "filter", filter, sizeof filter);
                ImGui::BeginChild("list");
                for (const Entry &en : list) {
                    std::string label = (en.label.empty() ? "" : en.label + "  ") + en.name + "  #" + std::to_string(en.fid);
                    std::string lo = label, f = filter;
                    std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);
                    std::transform(f.begin(), f.end(), f.begin(), ::tolower);
                    if (!f.empty() && lo.find(f) == std::string::npos) continue;
                    bool sel = scene.model && scene.model->name == en.name;
                    if (ImGui::Selectable(label.c_str(), sel)) open_model(scene, view, en.name, fid_path(en.fid), status, en.fid);
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (scene.model && ImGui::BeginTabItem("Model")) {
                Model &m = *scene.model;
                size_t polys = 0, verts = 0;
                for (const Mesh &me : m.meshes) polys += me.polys.size(), verts += me.verts.size();
                ImGui::Text("%d entries, %d bones, %d polygons, %d vertices", m.arc.count(), (int)m.bones.size() - 1,
                            (int)polys, (int)verts);
                ImGui::Text("bone list: entry %d, textures: %d, motions: %d", m.boneEntry, m.texEntry, m.motionEntry);
                ImGui::Separator();
                ImGui::Text("Textures");
                ImGui::SameLine();
                ImGui::RadioButton("off", &view.textures, 0);
                ImGui::SameLine();
                ImGui::RadioButton("PSX", &view.textures, 1);
                ImGui::SameLine();
                ImGui::RadioButton("bilinear", &view.textures, 2);
                ImGui::Text("Lights  ");
                ImGui::SameLine();
                ImGui::RadioButton("none", &view.lighting, 0);
                ImGui::SameLine();
                ImGui::RadioButton("viewer", &view.lighting, 1);
                ImGui::SameLine();
                ImGui::RadioButton("stage", &view.lighting, 2);
                if (view.lighting == 2 && !g_game.stages.empty()) {
                    ImGui::SliderInt("fight stage", &view.stage, 0, (int)g_game.stages.size() - 1);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A fight stage's three lights and ambient color (SFSTDATA.PRO)");
                }
                ImGui::Checkbox("Face", &view.face);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("The blinking eyes and the other animated face parts");
                ImGui::SameLine();
                ImGui::Checkbox("Wireframe", &view.wireframe);
                ImGui::SameLine();
                ImGui::Checkbox("Skeleton", &view.skeleton);
                ImGui::Checkbox("Back-face culling", &view.cull);
                ImGui::SameLine();
                ImGui::Checkbox("Grid", &view.grid);
                if (ImGui::Button("Show all")) std::fill(scene.hidden.begin(), scene.hidden.end(), false);
                ImGui::SameLine();
                if (ImGui::Button("Frame (F)")) frame_model(scene, view);
                ImGui::Separator();
                ImGui::BeginChild("bones");
                bone_tree(scene, 0);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (scene.model && ImGui::BeginTabItem("Motions")) {
                Model &m = *scene.model;
                if (m.motions.empty()) {
                    ImGui::TextWrapped("No motions in this file: it shows its first keys.");
                } else {
                    if (ImGui::Button(view.playing ? "Pause (space)" : "Play (space)")) view.playing = !view.playing;
                    ImGui::SameLine();
                    if (ImGui::Button("Step")) {
                        view.playing = false;
                        m.advance(1);
                        m.evaluate();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Restart")) m.setMotion(m.motion);
                    ImGui::SameLine();
                    ImGui::Checkbox("Loop", &view.loop);
                    ImGui::SliderFloat("speed", &view.speed, 0.05f, 4, "%.2fx");
                    int key = m.keyframe;
                    if (ImGui::SliderInt("keyframe", &key, 0, std::max(m.keyframeCount - 1, 0))) {
                        view.playing = false;
                        m.keyframe = key;
                        m.motionDone = false;
                        m.evaluate();
                    }
                    ImGui::Text("frame %d%s%s", m.frame, m.blending ? "  (blending)" : "", m.motionDone ? "  done" : "");
                    ImGui::Separator();
                    ImGui::BeginChild("motions");
                    for (int i = 1; i <= (int)m.motions.size(); i++) {
                        const std::vector<MotionStep> &st = m.motions[i - 1];
                        int frames = 0;
                        for (const MotionStep &x : st)
                            if (x.index != 0x7FFF) frames += std::max<int>(x.count, 0);
                        char label[64];
                        snprintf(label, sizeof label, "motion %2d   %d steps, %d frames", i, (int)st.size() - 1, frames);
                        if (ImGui::Selectable(label, m.motion == i)) {
                            m.setMotion(i);
                            view.playing = true;
                        }
                    }
                    ImGui::EndChild();
                }
                ImGui::EndTabItem();
            }
            if (scene.model && ImGui::BeginTabItem("Fighter")) {
                Model &m = *scene.model;
                if (scene.fighters.empty()) {
                    ImGui::TextWrapped("No fighter uses this file in the fighters' file (SMDLDATA.PRO): it has no "
                                       "name, face, cameras or action scripts there.");
                } else {
                    /* the fighters that use this file: each may name other entries */
                    if (scene.fighters.size() > 1) {
                        std::string cur = std::to_string(scene.fighters[scene.fighter]->id) + " " + scene.fighters[scene.fighter]->name;
                        if (ImGui::BeginCombo("fighter", cur.c_str())) {
                            for (int i = 0; i < (int)scene.fighters.size(); i++) {
                                std::string l = std::to_string(scene.fighters[i]->id) + " " + scene.fighters[i]->name +
                                                (scene.fighters[i]->enemy ? "  (enemy)" : "  (partner)");
                                if (ImGui::Selectable(l.c_str(), i == scene.fighter)) {
                                    std::string name = m.name;
                                    open_model(scene, view, name, scene.path, status, scene.fid, i);
                                }
                            }
                            ImGui::EndCombo();
                        }
                    }
                    const Fighter &f = *scene.fighters[scene.fighter];
                    Model &mm = *scene.model;
                    ImGui::Text("fighter %d, %s (kind 0x%X)", f.id, f.enemy ? "an enemy" : "a partner", f.kind);
                    ImGui::Text("entries: bones %d, motions %d, scripts %d", f.modelEntry, f.motionsEntry, f.effectsEntry);
                    int parts = 0;
                    for (bool u : f.faceUsed) parts += u;
                    ImGui::Text("face: %d parts", parts);
                    ImGui::SeparatorText("Cameras");
                    if (ImGui::Button("Free")) frame_model(scene, view);
                    for (int i = 0; i < (int)f.cameras.size(); i++) {
                        static const char *HINT[12] = {"", "", "", "", "", "", "portrait", "portrait, 2nd idle",
                                                       "battle", "battle", "battle", "battle"};
                        char l[48];
                        snprintf(l, sizeof l, "%d%s%s", i, !f.enemy && *HINT[i] ? " " : "", !f.enemy ? HINT[i] : "");
                        if (i % 4) ImGui::SameLine();
                        if (ImGui::RadioButton(l, view.gameCamera == i)) apply_camera(view, f, i);
                    }
                    ImGui::SeparatorText("Action scripts");
                    ImGui::TextDisabled("Click a motion to play it, a camera to look through it.");
                    ImGui::BeginChild("scripts");
                    for (int i = 0; i < (int)mm.scripts.size(); i++) {
                        const std::vector<ScriptLine> &sc = mm.scripts[i];
                        char h[48];
                        snprintf(h, sizeof h, "script %d  (%d commands)", i, (int)sc.size());
                        if (!ImGui::CollapsingHeader(h)) continue;
                        ImGui::PushID(i);
                        for (int k = 0; k < (int)sc.size(); k++) {
                            const ScriptLine &ln = sc[k];
                            ImGui::PushID(k);
                            bool active = ln.motion > 0 || ln.camera >= 0;
                            if (!active) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                            if (ImGui::Selectable(ln.text.c_str(), false) && active) {
                                if (ln.motion > 0 && ln.motion <= (int)mm.motions.size()) {
                                    mm.setMotion(ln.motion);
                                    view.playing = true;
                                }
                                int c = ln.camera >= 100 ? ln.camera - 100 : ln.camera;
                                if (c >= 0 && c < (int)f.cameras.size() && (ln.camera >= 100) == f.enemy)
                                    apply_camera(view, f, c);
                            }
                            if (!active) ImGui::PopStyleColor();
                            ImGui::PopID();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                }
                ImGui::EndTabItem();
            }
            if (scene.model && ImGui::BeginTabItem("Textures")) {
                Model &m = *scene.model;
                /* the texture page and CLUT pairs its polygons read */
                std::vector<std::pair<uint16_t, uint16_t>> combos;
                std::set<std::pair<uint16_t, uint16_t>> seen;
                for (const Mesh &me : m.meshes)
                    for (const Poly &p : me.polys)
                        if (p.textured && seen.insert({(uint16_t)(p.tpage & 0x19F), p.clut}).second)
                            combos.push_back({(uint16_t)(p.tpage & 0x19F), p.clut});
                for (const Tim &t : m.tims)
                    ImGui::Text("TIM %dx%d halfwords at %d,%d%s", t.w, t.h, t.x, t.y, t.rlen ? " (RLEN)" : "");
                if (combos.empty()) {
                    ImGui::Text("No textured polygons.");
                } else {
                    view.texCombo = std::min(view.texCombo, (int)combos.size() - 1);
                    for (int i = 0; i < (int)combos.size(); i++) {
                        char label[64];
                        static const char *DEPTH[] = {"4-bit", "8-bit", "16-bit", "?"};
                        snprintf(label, sizeof label, "%s page %d,%d CLUT %d,%d", DEPTH[tpage_depth(combos[i].first)],
                                 tpage_x(combos[i].first), tpage_y(combos[i].first), clut_x(combos[i].second),
                                 clut_y(combos[i].second));
                        ImGui::RadioButton(label, &view.texCombo, i);
                    }
                    auto [tp, cl] = combos[view.texCombo];
                    int w = 256, h = 256;
                    std::string key = m.name + std::to_string(tp) + "/" + std::to_string(cl);
                    if (key != texKey) {
                        texPixels = decode_page(m.vram, tp, cl, w, h);
                        texView = rgba_texture(texView, texPixels, w, h);
                        texKey = key, texW = w, texH = h;
                    }
                    ImGui::Checkbox("UVs", &view.uvs);
                    ImGui::SameLine();
                    ImGui::SliderFloat("zoom", &view.texZoom, 1, 4, "%.1fx");
                    if (ImGui::Button("Save BMP")) {
                        char name[96];
                        snprintf(name, sizeof name, "%s_%04x_%04x.bmp", m.name.substr(0, m.name.find('.')).c_str(), tp, cl);
                        save_bmp(g_exe_dir + name, texPixels, texW, texH);
                        status = std::string("saved ") + name;
                    }
                    ImGui::BeginChild("tex", ImVec2(0, 0), 0, ImGuiWindowFlags_HorizontalScrollbar);
                    ImVec2 o = ImGui::GetCursorScreenPos();
                    float z = view.texZoom;
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    dl->AddRectFilled(o, ImVec2(o.x + texW * z, o.y + texH * z), IM_COL32(40, 40, 48, 255));
                    ImGui::Image((ImTextureID)(intptr_t)texView, ImVec2(texW * z, texH * z));
                    if (view.uvs) {
                        for (size_t b = 1; b < m.bones.size(); b++) {
                            for (const Poly &p : m.meshes[m.bones[b].mesh].polys) {
                                if (!p.textured || (p.tpage & 0x19F) != tp || p.clut != cl) continue;
                                static const int ORDER[2][4] = {{0, 1, 2, -1}, {0, 1, 3, 2}};
                                const int *ord = ORDER[p.quad];
                                int n = p.quad ? 4 : 3;
                                ImU32 c = (int)b == scene.selected ? IM_COL32(255, 220, 60, 255) : IM_COL32(80, 255, 120, 160);
                                for (int k = 0; k < n; k++) {
                                    const uint8_t *a = p.uv[ord[k]], *bb = p.uv[ord[(k + 1) % n]];
                                    dl->AddLine(ImVec2(o.x + (a[0] + 0.5f) * z, o.y + (a[1] + 0.5f) * z),
                                                ImVec2(o.x + (bb[0] + 0.5f) * z, o.y + (bb[1] + 0.5f) * z), c);
                                }
                            }
                        }
                    }
                    ImGui::EndChild();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
        ImGui::Render();

        render(scene, view, ww - (int)(panel * ww / std::max(io.DisplaySize.x, 1.0f)), wh);
        GL(glViewport)(0, 0, ww, wh);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(win);
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
