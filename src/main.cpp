/* DW3 Model Viewer: Digimon World 2003's battle models, their textures,
   bones and motions, in the spirit of Half-Life's model viewer.

   dw3mv [file.bin]            opens the viewer (and a model)
   dw3mv --shot out.bmp [--motion N] [--key K] file.bin
                               draws one frame of the model to a BMP and exits
   dw3mv --check               reads every model of the disc and prints what it found
   dw3mv --effect ID           saves 2D effect ID's frames as PNGs and exits
   dw3mv --effect-users        prints which Digimon's action scripts start each 2D effect

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
#include "custom.h"
#include "effect.h"
#include "model.h"
#include "stage.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
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
   the folder given, the one picked last (data_dir.txt by the exe), data/
   (from the exe in build/) or next to the exe. Whether one was found. */
static bool find_data(const std::string &given) {
    std::vector<std::string> tries = {g_exe_dir + "../data", g_exe_dir + "data"};
    std::ifstream saved(g_exe_dir + "data_dir.txt");
    std::string line;
    if (saved && std::getline(saved, line) && !line.empty()) tries.insert(tries.begin(), line);
    if (!given.empty()) tries.insert(tries.begin(), given);
    for (const std::string &dir : tries) {
        std::ifstream names(dir + "/names.txt");
        if (!names) continue;
        g_data = dir + "/files";
        g_names.clear();
        g_game = GameData();
        for (std::string line; std::getline(names, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            g_names.push_back(line);
        }
        g_game.load(g_data);
        return true;
    }
    g_data = tries[0] + "/files";
    return false;
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

static GLuint rgba_texture(GLuint tex, const std::vector<uint32_t> &px, int w, int h) {
    if (!tex) GL(glGenTextures)(1, &tex);
    GL(glBindTexture)(GL_TEXTURE_2D, tex);
    GL(glPixelStorei)(GL_UNPACK_ALIGNMENT, 4);
    GL(glTexImage2D)(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return tex;
}

static const char *VERSION = "0.4.2";

/* The exe's icon (res/dw3mv.rc's icon 1) as a texture, for the About window; 0 without one */
static GLuint icon_texture(int size) {
#ifdef _WIN32
    HICON icon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, size, size, 0);
    if (!icon) return 0;
    ICONINFO info;
    GLuint tex = 0;
    if (GetIconInfo(icon, &info)) {
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = size, bi.bmiHeader.biHeight = -size; /* top-down */
        bi.bmiHeader.biPlanes = 1, bi.bmiHeader.biBitCount = 32, bi.bmiHeader.biCompression = BI_RGB;
        std::vector<uint32_t> px(size * size);
        HDC dc = GetDC(nullptr);
        if (GetDIBits(dc, info.hbmColor, 0, size, px.data(), &bi, DIB_RGB_COLORS)) {
            for (uint32_t &c : px) c = (c & 0xFF00FF00) | (c >> 16 & 0xFF) | (c & 0xFF) << 16; /* BGRA to RGBA */
            tex = rgba_texture(0, px, size, size);
            GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
        ReleaseDC(nullptr, dc);
        DeleteObject(info.hbmColor);
        DeleteObject(info.hbmMask);
    }
    DestroyIcon(icon);
    return tex;
#else
    return 0;
#endif
}

/* The About window: what the viewer is, its version, license and credits */
static void about_window(bool &open, GLuint &icon) {
    if (!open) return;
    if (!icon) icon = icon_texture(96);
    ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(1);
    if (!ImGui::Begin("About DW3 Model Viewer", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                                     ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    if (icon) {
        ImGui::Image((ImTextureID)(intptr_t)icon, ImVec2(96, 96));
        ImGui::SameLine();
    }
    ImGui::BeginGroup();
    ImGui::Text("DW3 Model Viewer");
    ImGui::TextDisabled("Version %s", VERSION);
    ImGui::Spacing();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 320);
    ImGui::TextWrapped("A viewer for Digimon World 2003's battle models: their meshes, textures, bones, "
                       "motions, faces, cameras, action scripts and 2D effects.");
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::Separator();
    ImGui::Text("(c) 2026 Feerdus - MIT License");
    auto link = [](const char *label, const char *url) {
        ImGui::TextColored(ImVec4(0.45f, 0.7f, 1, 1), "%s", label);
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemClicked()) SDL_OpenURL(url);
    };
    link("github.com/Feerdus95/dw3-model-viewer", "https://github.com/Feerdus95/dw3-model-viewer");
    ImGui::Spacing();
    ImGui::Text("Built on:");
    link("  dw3_decomp (MIT, juandav)", "https://github.com/ReGame-Labs/dw3_decomp");
    link("  Dear ImGui (MIT)", "https://github.com/ocornut/imgui");
    link("  SDL3 (zlib)", "https://libsdl.org");
    ImGui::Spacing();
    ImGui::TextDisabled("Not affiliated with Bandai. No game data is included:");
    ImGui::TextDisabled("it reads your own copy of the game.");
    ImGui::Spacing();
    if (ImGui::Button("Close", ImVec2(120, 0))) open = false;
    ImGui::End();
}

static void save_bmp(const std::string &path, const std::vector<uint32_t> &px, int w, int h) {
    SDL_Surface *s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ABGR8888, (void *)px.data(), w * 4);
    if (s) {
        SDL_SaveBMP(s, path.c_str());
        SDL_DestroySurface(s);
    }
}

/* An effect's frames as PNGs, every other field (the game draws 30 frames a
   second) on a 320x240 screen, its origin in the middle, with a transparent
   background: <exe>/effects/effect_<id>/frame_NNN.png. Returns how many.
   Each frame is drawn over black and over white and its alpha is what the
   two differ by: opaque texels stay opaque, averaged ones are half, and the
   added ones (no alpha of their own) take their brightness as alpha, which
   over black is exactly the game's. */
static const int FX_W = 320, FX_H = 240;
/* the Effects tab's canvas: three screens across, the effect's point in its middle */
static const int FXC_W = 960, FXC_H = 720;
static const uint32_t FX_BG = 0xFF302824;

static int export_effect(const Effect &fx, std::string &dir) {
    std::vector<uint32_t> px(FX_W * FX_H), white(FX_W * FX_H);
    dir = g_exe_dir + "effects";
    SDL_CreateDirectory(dir.c_str());
    dir += "/effect_" + std::to_string(fx.id);
    SDL_CreateDirectory(dir.c_str());
    int n = 0;
    for (int f = 0; f < fx.duration; f += 2, n++) {
        std::fill(px.begin(), px.end(), 0xFF000000u);
        std::fill(white.begin(), white.end(), 0xFFFFFFFFu);
        fx.draw(f, px.data(), FX_W, FX_H, FX_W / 2, FX_H / 2);
        fx.draw(f, white.data(), FX_W, FX_H, FX_W / 2, FX_H / 2);
        for (int i = 0; i < FX_W * FX_H; i++) {
            int a = 0;
            for (int c = 0; c < 24; c += 8) a = std::max(a, 255 - (int)(white[i] >> c & 255) + (int)(px[i] >> c & 255));
            a = std::clamp(a, 0, 255);
            uint32_t out = 0;
            for (int c = 0; c < 24; c += 8)
                if (a) out |= (uint32_t)std::min(255, (int)(px[i] >> c & 255) * 255 / a) << c;
            px[i] = out | (uint32_t)a << 24;
        }
        char name[32];
        snprintf(name, sizeof name, "/frame_%03d.png", n);
        SDL_Surface *s = SDL_CreateSurfaceFrom(FX_W, FX_H, SDL_PIXELFORMAT_ABGR8888, px.data(), FX_W * 4);
        if (s) SDL_SavePNG(s, (dir + name).c_str()), SDL_DestroySurface(s);
    }
    return n;
}

/* Who starts each sprite effect: the Digimon (by name) whose action scripts
   have its line. The effects no script names (the hits' own, digivolving,
   items) are the battle's own */
static std::map<int, std::set<std::string>> effect_users() {
    std::map<int, std::set<std::string>> out;
    std::map<int, std::vector<uint8_t>> files;
    for (const Fighter &f : g_game.fighters) {
        if (f.effectsFid <= 0) continue;
        std::vector<uint8_t> &file = files[f.effectsFid];
        if (file.empty() && !read_file(fid_path(f.effectsFid), file)) continue;
        Archive a{file.data(), file.size()};
        if (f.effectsEntry >= a.count() || !a.entry(f.effectsEntry)) continue;
        for (const std::vector<ScriptLine> &sc : read_scripts(a.entry(f.effectsEntry), a.entry_size(f.effectsEntry)))
            for (const ScriptLine &ln : sc)
                if (ln.effect >= 0) out[ln.effect].insert(f.name);
    }
    return out;
}

/* The effects with own images in ROOT (<root>/<id>/, or its critical/) */
static std::set<int> own_effects(const std::string &root) {
    std::set<int> out;
    int n = 0;
    char **dirs = SDL_GlobDirectory(root.c_str(), "*", 0, &n);
    for (int i = 0; i < n; i++) {
        char *end;
        long id = strtol(dirs[i], &end, 10);
        if (*end || end == dirs[i]) continue;
        for (const char *sub : {"", "/critical"}) {
            int k = 0;
            char **pngs = SDL_GlobDirectory((root + "/" + dirs[i] + sub).c_str(), "*.png", SDL_GLOB_CASEINSENSITIVE, &k);
            SDL_free(pngs);
            if (k > 0) out.insert((int)id);
        }
    }
    SDL_free(dirs);
    return out;
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
    bool stageView = false;          /* a fight stage: its background, no grid */
    float stageBg[3] = {0, 0, 0};
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
    if (v.stageView)
        GL(glClearColor)(v.stageBg[0], v.stageBg[1], v.stageBg[2], 1);
    else
        GL(glClearColor)(0.18f, 0.2f, 0.24f, 1);
    GL(glClear)(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    M4 proj = perspective(v.fov, (float)w / std::max(h, 1), v.cam.dist * 0.01f, v.cam.dist * 50);
    M4 vp = mul(proj, v.cam.view());
    if (v.grid && !v.stageView) {
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

/* A fight stage, from inside: the arena's middle, a little above the ground */
static void frame_stage(View &v) {
    v.cam.target[0] = 0, v.cam.target[1] = 1500, v.cam.target[2] = 0;
    v.cam.dist = 9000, v.cam.pitch = 0.2f, v.cam.yaw = 0.6f;
    v.fov = 0.8f;
    v.gameCamera = -1;
}

/* A 4-bit polygon's texel at its corner K, as the GPU reads it */
static uint16_t corner_texel(const Model &m, const Poly &p, int k) {
    int u = p.uv[k][0], v = p.uv[k][1];
    int i = m.vram[(tpage_y(p.tpage) + v) * 1024 + tpage_x(p.tpage) + u / 4] >> (u & 3) * 4 & 15;
    return m.vram[clut_y(p.clut) * 1024 + clut_x(p.clut) + i];
}

/* --stages: each stage read, built back (and through an OBJ) and read
   again; what changed of its polygons and their corners' texels */
static int check_stages() {
    std::vector<uint8_t> table;
    read_file(fid_path(459), table);
    int bad = 0;
    auto corners = [](const Model &m) {
        std::vector<uint32_t> out;
        for (size_t b = 1; b < m.bones.size(); b++)
            for (const Poly &p : m.meshes[m.bones[b].mesh].polys)
                for (int k = 0; k < (p.quad ? 4 : 3); k++) {
                    uint16_t t = corner_texel(m, p, k);
                    out.push_back((uint32_t)b << 17 | (p.abr ? t : (t & 0x7FFF) | (t ? 0 : 0x10000)));
                }
        std::sort(out.begin(), out.end());
        return out;
    };
    for (const StageInfo &si : read_stage_infos(table)) {
        std::vector<uint8_t> disc, built, again;
        read_file(fid_path(si.fid), disc);
        Model m, m2, m3;
        if (!m.load("", disc, nullptr, si.boneEntry)) {
            printf("stage %2d: %s\n", si.id, m.error.c_str());
            bad++;
            continue;
        }
        StageEdit e;
        e.fromModel(m, si);
        StageInfo info = si, info2 = si;
        StageBuild made, made2;
        if (!e.build(disc, si.boneEntry, built, info, made) || !m2.load("", built, nullptr, si.boneEntry)) {
            printf("stage %2d: %s%s\n", si.id, e.error.c_str(), m2.error.c_str());
            bad++;
            continue;
        }
        std::string dir = g_exe_dir + "stages/check";
        StageEdit e2 = e;
        bool obj = e.exportObj(dir, "check") && e2.importObj(dir + "/check.obj") &&
                   e2.build(disc, si.boneEntry, again, info2, made2) && m3.load("", again, nullptr, si.boneEntry);
        std::vector<uint32_t> a = corners(m), b = corners(m2), c = obj ? corners(m3) : std::vector<uint32_t>();
        int same = 0, same3 = 0;
        for (size_t i = 0; i < std::min(a.size(), b.size()); i++) same += a[i] == b[i];
        for (size_t i = 0; i < std::min(a.size(), c.size()); i++) same3 += a[i] == c[i];
        bool ok = a.size() == b.size() && obj && a.size() == c.size() && memcmp(info.noBounds, si.noBounds, 8) == 0;
        printf("stage %2d: %2d bones, %4d corners: built %4d same, OBJ %4d same; %3d pieces%s, %6d bytes%s\n", si.id,
               (int)m.bones.size() - 1, (int)a.size(), same, same3, made.islands,
               made.shrink < 1 ? " (shrunk)" : "", (int)built.size(), ok ? "" : "  BAD");
        bad += !ok;
    }
    /* harder: stage 13 with all of 8's and 24's parts, and with its images changed (new texels, 16 colors a piece) */
    std::vector<StageInfo> infos = read_stage_infos(table);
    if (infos.size() > 24) {
        StageEdit e[3];
        for (int k = 0; k < 3; k++) {
            std::vector<uint8_t> f;
            Model m;
            const StageInfo &si = infos[k == 0 ? 13 : k == 1 ? 8 : 24];
            read_file(fid_path(si.fid), f);
            if (m.load("", f, nullptr, si.boneEntry)) e[k].fromModel(m, si);
        }
        StageEdit mixed = e[0], edited = e[0];
        for (int k = 1; k < 3; k++)
            for (int p = 0; p < (int)e[k].parts.size(); p++) mixed.append(e[k], p, "x_");
        for (StageImage &im : edited.images) {
            im.source = -1;
            for (uint32_t &c : im.px) c ^= 0x00102030u;
        }
        std::vector<uint8_t> disc, out;
        read_file(fid_path(infos[13].fid), disc);
        for (StageEdit *t : {&mixed, &edited}) {
            StageInfo info = infos[13];
            StageBuild made;
            Model m;
            bool ok = t->build(disc, info.boneEntry, out, info, made) && m.load("", out, nullptr, info.boneEntry);
            printf("stage 13 %s: %d parts -> %d bones, %d pieces, shrunk to %.2f, %d bytes %s%s\n", t == &mixed ? "+ 8 + 24" : "edited",
                   (int)t->parts.size(), ok ? (int)m.bones.size() - 1 : 0, made.islands, made.shrink, (int)out.size(),
                   ok ? "" : "BAD ", t->error.c_str());
            bad += !ok;
        }
    }
    return bad != 0;
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
    bool check = false, users = false, stages = false;
    int effect = -1;
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
        else if (a == "--stages") stages = true;
        else if (a == "--effect" && i + 1 < argc) effect = atoi(argv[++i]);
        else if (a == "--effect-users") users = true;
        else open_path = a;
    }
    const char *base = SDL_GetBasePath();
    g_exe_dir = base ? base : "";
    find_data(data);

    if (check || users || stages || !shot.empty() || effect >= 0) attach_console();
    if (stages) return check_stages();
    if (users) {
        std::map<int, std::set<std::string>> by = effect_users();
        for (int id : effect_ids()) {
            printf("%4d %2d", id, (int)by[id].size());
            for (const std::string &n : by[id]) printf(" %s", n.c_str());
            printf("\n");
        }
        return 0;
    }
    if (effect >= 0) {
        Effect fx;
        if (!fx.load(effect, g_data)) {
            fprintf(stderr, "%s\n", fx.error.c_str());
            return 1;
        }
        std::string dir;
        int n = export_effect(fx, dir);
        printf("effect %d: %d animations, %d fields -> %d frames in %s\n", effect, (int)fx.anims.size(), fx.duration, n, dir.c_str());
        return 0;
    }
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
    bool aboutOpen = false;
    GLuint aboutIcon = 0;
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
        if (open_model(scene, view, name, path, status, fid, fighter)) view.stageView = false;
    };
    if (!open_path.empty()) open_entry(open_path, shot_fighter);

    /* the Effects tab: a sprite effect on a 320x240 screen, its origin in the middle */
    Effect fx;
    std::vector<int> fxIds = effect_ids();
    std::vector<uint32_t> fxPx(FXC_W * FXC_H);
    bool fxFitPending = false; /* fit the view to the effect when next drawn */
    bool fxActive = false; /* the Effects tab is open: the view shows the effect, not the model */
    float fxZoom = 2, fxPan[2] = {0, 0}; /* screen pixels per effect pixel; the view's middle from the point */
    GLuint fxTex = 0;
    float fxTime = 0;
    int fxDrawn = -1;
    bool fxPlaying = true, fxLoop = true, fxSelect = false;
    std::string fxDir; /* the last export's folder */
    /* its own images for the native port (custom.h), in <root>/<id>/ (the
       port's data/mods/effects, kept in effects_dir.txt by the exe) */
    CustomEffect own;
    bool ownShow = false, ownCritical = false, ownDirty = false;
    std::string ownRoot = g_data + "/../mods/effects";
    {
        std::ifstream f(g_exe_dir + "effects_dir.txt");
        std::string line;
        if (f && std::getline(f, line) && !line.empty()) ownRoot = line;
        else /* the port's, beside the viewer's folder (both built from their sources) */
            for (const char *dir : {"../../native/data/mods/effects", "../native/data/mods/effects"})
                if (SDL_GetPathInfo((g_exe_dir + dir).c_str(), nullptr)) {
                    ownRoot = g_exe_dir + dir;
                    break;
                }
    }
    struct Picked { /* what the file dialogs gave, maybe from another thread */
        std::mutex m;
        std::vector<std::string> files;
        std::string folder, data, obj;
    } picked;
    std::set<int> ownIds; /* the effects with own images in ownRoot */

    /* the Stages tab: a fight stage, edited (stage.h), saved for the native
       port in its data/mods (ownRoot's parent): <fid>.bin and the stages'
       table, 0459.bin */
    std::vector<StageInfo> stageInfos; /* the disc's table */
    std::set<int> stageFids, stageMods; /* the stages' model files; those with a mod */
    int stageId = -1, stageFromId = -1, stagePart = -1;
    StageEdit stageEdit, stageFrom;
    StageInfo stageInfo; /* the stage's entry, as edited */
    StageBuild stageMade;
    std::vector<uint8_t> stageDisc; /* its file on the disc, what builds start from */
    bool stageDirty = false;
    std::string stageDir; /* the last export's folder */
    auto mods_root = [&]() {
        size_t e = ownRoot.find_last_of("/\\");
        return e == std::string::npos ? ownRoot : ownRoot.substr(0, e);
    };
    auto mod_path = [&](int fid) {
        char b[16];
        snprintf(b, sizeof b, "/%04d.bin", fid);
        return mods_root() + b;
    };
    auto stage_tables = [&]() {
        std::vector<uint8_t> t;
        read_file(fid_path(459), t);
        stageInfos = read_stage_infos(t);
        stageFids.clear(), stageMods.clear();
        for (const StageInfo &si : stageInfos) {
            stageFids.insert(si.fid);
            if (SDL_GetPathInfo(mod_path(si.fid).c_str(), nullptr)) stageMods.insert(si.fid);
        }
    };
    stage_tables();
    /* a stage as the port has it: its mod's model and table entry, else the disc's */
    auto stage_load = [&](int id, StageEdit &edit, StageInfo &info, std::vector<uint8_t> &bytes) {
        info = stageInfos[id];
        std::vector<uint8_t> table;
        if (read_file(mod_path(459), table)) {
            std::vector<StageInfo> mod = read_stage_infos(table);
            if (id < (int)mod.size()) memcpy(info.bg, mod[id].bg, 3), memcpy(info.noBounds, mod[id].noBounds, 8);
        }
        if (!read_file(mod_path(info.fid), bytes) && !read_file(fid_path(info.fid), bytes)) {
            status = "can't read " + fid_path(info.fid);
            return false;
        }
        Model m;
        if (!m.load(fid_name(info.fid), bytes, nullptr, info.boneEntry)) {
            status = "stage " + std::to_string(id) + ": " + m.error;
            return false;
        }
        edit.fromModel(m, info);
        return true;
    };
    auto own_load = [&]() {
        own.load(ownRoot + "/" + std::to_string(fx.id) + (ownCritical ? "/critical" : ""));
        ownShow = !own.frames.empty(), ownDirty = false, fxDrawn = -1;
        ownIds = own_effects(ownRoot);
    };
    /* the list's groups: the effects no script starts or several Digimon's
       do, and each Digimon's own (its Black King, MP and MF forms with it) */
    bool fxGrouped = false;
    std::map<int, std::set<std::string>> fxUsers;
    std::vector<int> fxCommon;
    std::map<std::string, std::vector<int>> fxByDigimon;
    auto group_effects = [&]() {
        fxUsers = effect_users();
        for (int id : fxIds) {
            std::set<std::string> base;
            for (std::string n : fxUsers[id]) {
                if (n.rfind("BK ", 0) == 0) n = n.substr(3);
                if (n.size() > 3 && (n.compare(n.size() - 3, 3, " MP") == 0 || n.compare(n.size() - 3, 3, " MF") == 0)) n.resize(n.size() - 3);
                base.insert(n);
            }
            if (base.size() == 1) fxByDigimon[*base.begin()].push_back(id);
            else fxCommon.push_back(id);
        }
        fxGrouped = true;
    };
    auto fx_length = [&]() { return ownShow ? own.duration() : fx.duration; };
    auto open_effect = [&](int id, bool select) {
        if (!fx.load(id, g_data)) status = fx.error;
        fxTime = 0, fxDrawn = -1, fxPlaying = true, fxSelect = select;
        fxDir.clear();
        if (fx.id >= 0) own_load();
        fxFitPending = true;
    };
    auto effect_item = [&](int id) {
        char l[64];
        const std::set<std::string> &u = fxUsers[id];
        if (u.size() > 1) snprintf(l, sizeof l, "effect %d  (%d Digimon)", id, (int)u.size());
        else if (u.empty()) snprintf(l, sizeof l, "effect %d  (the battle's own)", id);
        else snprintf(l, sizeof l, "effect %d", id);
        if (ImGui::Selectable(l, fx.id == id)) open_effect(id, false);
        if (u.size() > 1 && ImGui::IsItemHovered()) {
            std::string all;
            for (const std::string &n : u) all += (all.empty() ? "" : ", ") + n;
            ImGui::SetTooltip("%s", all.c_str());
        }
    };
    auto fx_render = [&](int t, uint32_t bg) {
        std::fill(fxPx.begin(), fxPx.end(), bg);
        if (fx.id >= 0 && ownShow) own.draw(t, fxPx.data(), FXC_W, FXC_H, FXC_W / 2, FXC_H / 2);
        else if (fx.id >= 0) fx.draw(t, fxPx.data(), FXC_W, FXC_H, FXC_W / 2, FXC_H / 2);
    };
    /* the view on all the effect draws over its length, in the view's W x H */
    auto fx_fit = [&](float w, float h) {
        int box[4] = {FXC_W, FXC_H, -1, -1};
        for (int t = 0; t < std::max(fx_length(), 1); t += 2) {
            fx_render(t, 0);
            for (int y = 0; y < FXC_H; y++)
                for (int x = 0; x < FXC_W; x++)
                    if (fxPx[y * FXC_W + x]) {
                        box[0] = std::min(box[0], x), box[1] = std::min(box[1], y);
                        box[2] = std::max(box[2], x), box[3] = std::max(box[3], y);
                    }
        }
        if (box[2] < 0) box[0] = (FXC_W - FX_W) / 2, box[1] = (FXC_H - FX_H) / 2, box[2] = box[0] + FX_W, box[3] = box[1] + FX_H;
        fxPan[0] = (box[0] + box[2]) / 2.0f - FXC_W / 2, fxPan[1] = (box[1] + box[3]) / 2.0f - FXC_H / 2;
        fxZoom = std::clamp(0.8f * std::min(w / (box[2] - box[0] + 1), h / (box[3] - box[1] + 1)), 0.25f, 8.0f);
        fxDrawn = -1;
    };

    /* the stage's model in the view, as the game will draw it */
    auto stage_show = [&](std::vector<uint8_t> bytes, bool reframe) {
        auto m = std::make_unique<Model>();
        if (!m->load(fid_name(stageInfo.fid), std::move(bytes), nullptr, stageInfo.boneEntry)) {
            status = m->error;
            return;
        }
        scene.fid = -1, scene.path.clear(), scene.fighters.clear(), scene.fighter = 0;
        scene.model = std::move(m);
        scene.selected = -1;
        upload(scene);
        view.stageView = true;
        for (int k = 0; k < 3; k++) view.stageBg[k] = stageEdit.bg[k] / 255.0f;
        if (reframe) frame_stage(view);
        view.texCombo = 0;
    };
    auto stage_open = [&](int id) {
        std::vector<uint8_t> bytes;
        if (!stage_load(id, stageEdit, stageInfo, bytes)) return;
        read_file(fid_path(stageInfo.fid), stageDisc);
        stageId = id, stagePart = -1, stageDirty = false, stageMade = StageBuild();
        for (int k = 0; k < (int)stageEdit.parts.size(); k++) stageMade.firstBone.push_back(k + 1);
        status.clear();
        stage_show(std::move(bytes), true);
    };
    /* after an edit: built, and shown as built */
    auto stage_rebuild = [&]() {
        std::vector<uint8_t> bytes;
        if (!stageEdit.build(stageDisc, stageInfo.boneEntry, bytes, stageInfo, stageMade)) {
            status = stageEdit.error;
            return false;
        }
        stageDirty = true;
        status = stageMade.warning;
        stage_show(std::move(bytes), false);
        if (stagePart >= 0 && stagePart < (int)stageMade.firstBone.size()) scene.selected = stageMade.firstBone[stagePart];
        return true;
    };
    /* the stage's model and its entry of the table to the mods folder; the
       stages that share its model (0 and 1, 34 and 56) take its bones'
       bounds check too */
    auto stage_save = [&]() {
        std::vector<uint8_t> bytes, table;
        if (!stageEdit.build(stageDisc, stageInfo.boneEntry, bytes, stageInfo, stageMade)) {
            status = stageEdit.error;
            return;
        }
        SDL_CreateDirectory(mods_root().c_str());
        if (!read_file(mod_path(459), table)) read_file(fid_path(459), table);
        std::vector<StageInfo> now = read_stage_infos(table);
        for (StageInfo si : now) {
            if (si.fid != stageInfo.fid) continue;
            memcpy(si.noBounds, stageInfo.noBounds, 8);
            if (si.id == stageId) memcpy(si.bg, stageInfo.bg, 3);
            write_stage_info(table, si);
        }
        std::ofstream(mod_path(stageInfo.fid), std::ios::binary).write((const char *)bytes.data(), (std::streamsize)bytes.size());
        std::ofstream(mod_path(459), std::ios::binary).write((const char *)table.data(), (std::streamsize)table.size());
        stageDirty = false;
        stageMods.insert(stageInfo.fid);
        status = "saved " + mod_path(stageInfo.fid) + ": the game reads it when it starts";
    };
    /* back to the disc's: the mod's model gone, its table entries the disc's */
    auto stage_revert = [&]() {
        SDL_RemovePath(mod_path(stageInfo.fid).c_str());
        stageMods.erase(stageInfo.fid);
        std::vector<uint8_t> table, disc;
        if (read_file(mod_path(459), table) && read_file(fid_path(459), disc)) {
            for (const StageInfo &si : stageInfos)
                if (si.fid == stageInfo.fid) write_stage_info(table, si);
            if (table == disc) SDL_RemovePath(mod_path(459).c_str());
            else std::ofstream(mod_path(459), std::ios::binary).write((const char *)table.data(), (std::streamsize)table.size());
        }
        stage_open(stageId);
    };

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
            if (!io.WantCaptureMouse && fxActive) { /* the effect: dragged and zoomed */
                if (e.type == SDL_EVENT_MOUSE_MOTION && e.motion.state)
                    fxPan[0] -= e.motion.xrel / fxZoom, fxPan[1] -= e.motion.yrel / fxZoom;
                else if (e.type == SDL_EVENT_MOUSE_WHEEL)
                    fxZoom = std::clamp(fxZoom * powf(1.15f, e.wheel.y), 0.25f, 16.0f);
            } else if (!io.WantCaptureMouse) {
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
            if (e.type == SDL_EVENT_KEY_DOWN && !io.WantCaptureKeyboard && fxActive && e.key.key == SDLK_F) fxFitPending = true;
            else if (e.type == SDL_EVENT_KEY_DOWN && !io.WantCaptureKeyboard && scene.model) {
                if (e.key.key == SDLK_SPACE) view.playing = !view.playing;
                if (e.key.key == SDLK_F) view.stageView ? frame_stage(view) : frame_model(scene, view);
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

        if (fx.id >= 0 && fxPlaying) { /* in ticks, 60 a second, as FIGHTSTG_battle.frames counts */
            fxTime += dt * 60 * view.speed;
            if (fxTime >= fx_length()) fxTime = fxLoop ? 0 : (float)fx_length();
        }
        {
            std::lock_guard<std::mutex> lock(picked.m);
            if (!picked.files.empty()) {
                int n = own.import(picked.files);
                ownIds = own_effects(ownRoot);
                status = own.error.empty() ? "imported " + std::to_string(n) + " PNGs to " + own.dir : own.error;
                ownShow = ownShow || n > 0, ownDirty = ownDirty || n > 0, fxDrawn = -1;
                picked.files.clear();
            }
            if (!picked.folder.empty()) {
                ownRoot = picked.folder;
                std::ofstream(g_exe_dir + "effects_dir.txt") << ownRoot << "\n";
                own_load();
                stage_tables();
                picked.folder.clear();
            }
            if (!picked.obj.empty()) {
                if (stageId >= 0 && stageEdit.importObj(picked.obj)) {
                    std::string warning = stageEdit.error;
                    stagePart = -1;
                    if (stage_rebuild() && !warning.empty()) status = warning;
                } else if (stageId >= 0) {
                    status = stageEdit.error;
                }
                picked.obj.clear();
            }
            if (!picked.data.empty()) { /* another data folder: everything read again from it */
                if (std::ifstream(picked.data + "/names.txt")) {
                    std::ofstream(g_exe_dir + "data_dir.txt") << picked.data << "\n";
                    find_data(picked.data);
                    scene.model.reset(), scene.fighters.clear(), scene.fid = -1;
                    fx = Effect(), listed = false;
                    stage_tables(), stageId = -1, stageFromId = -1, view.stageView = false;
                    fxGrouped = false, fxCommon.clear(), fxByDigimon.clear();
                    status.clear();
                } else {
                    status = "no names.txt in " + picked.data + ": pick the data folder tools/setup_disc.py wrote";
                }
                picked.data.clear();
            }
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
        fxActive = false;
        if (scene.model && !scene.fighters.empty() && !scene.fighters[scene.fighter]->name.empty())
            ImGui::Text("%s  (%s)", scene.fighters[scene.fighter]->name.c_str(), scene.model->name.c_str());
        else if (scene.model)
            ImGui::Text("%s", scene.model->name.c_str());
        else
            ImGui::TextWrapped("Pick a model, or drop a .BIN on the window.");
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize("About").x - ImGui::GetStyle().FramePadding.x * 2);
        if (ImGui::SmallButton("About")) aboutOpen = true;
        if (!status.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", status.c_str());
        /* SameLine only when the next widget, w wide, fits the panel */
        auto same_line = [](float w) {
            float x = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x;
            if (x + w <= ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) ImGui::SameLine();
        };
        auto bw = [](const char *l) {
            return ImGui::CalcTextSize(l, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2;
        };
        if (ImGui::BeginTabBar("tabs")) {
            if (ImGui::BeginTabItem("Files")) {
                if (!listed) {
                    list = scan_models();
                    listed = true;
                }
                if (ImGui::Button("Data folder...")) {
                    SDL_ShowOpenFolderDialog(
                        [](void *u, const char *const *l, int) {
                            if (!l || !*l) return;
                            Picked *p = (Picked *)u;
                            std::lock_guard<std::mutex> lock(p->m);
                            p->data = *l;
                        },
                        &picked, win, nullptr, false);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("The data folder tools/setup_disc.py wrote (with names.txt). Now: %s",
                                      g_data.substr(0, g_data.size() - 6).c_str());
                if (list.empty()) ImGui::TextWrapped("No models found in %s: run tools/setup_disc.py with your disc, or pick its data folder.", g_data.c_str());
                ImGui::InputTextWithHint("##filter", "filter", filter, sizeof filter);
                ImGui::BeginChild("list");
                for (const Entry &en : list) {
                    if (stageFids.count(en.fid)) continue; /* in the Stages tab */
                    std::string label = (en.label.empty() ? "" : en.label + "  ") + en.name + "  #" + std::to_string(en.fid);
                    std::string lo = label, f = filter;
                    std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);
                    std::transform(f.begin(), f.end(), f.begin(), ::tolower);
                    if (!f.empty() && lo.find(f) == std::string::npos) continue;
                    bool sel = scene.model && !view.stageView && scene.model->name == en.name;
                    if (ImGui::Selectable(label.c_str(), sel) && open_model(scene, view, en.name, fid_path(en.fid), status, en.fid))
                        view.stageView = false, stageId = -1;
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Stages")) {
                if (stageId >= 0 && view.stageView) {
                    const StageInfo &si = stageInfos[stageId];
                    std::string shared;
                    for (const StageInfo &o : stageInfos)
                        if (o.fid == si.fid && o.id != si.id) shared += (shared.empty() ? "" : ", ") + std::to_string(o.id);
                    ImGui::Text("Stage %d: %s", stageId, stage_look(stageId));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    ImGui::TextWrapped("%s #%d, bone list entry %d%s%s", fid_name(si.fid).c_str(), si.fid, si.boneEntry,
                                       shared.empty() ? "" : "; its model is stage ", shared.c_str());
                    ImGui::TextWrapped("%s", stageMods.count(si.fid) ? ("mod: " + mod_path(si.fid)).c_str() : "the disc's (no mod)");
                    ImGui::PopStyleColor();
                    float bgc[3] = {stageEdit.bg[0] / 255.0f, stageEdit.bg[1] / 255.0f, stageEdit.bg[2] / 255.0f};
                    if (ImGui::ColorEdit3("background", bgc)) {
                        for (int k = 0; k < 3; k++) stageEdit.bg[k] = stageInfo.bg[k] = (uint8_t)lroundf(bgc[k] * 255), view.stageBg[k] = bgc[k];
                        stageDirty = true;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("What shows through the panorama's gaps (the stage's bgColor)");
                    if (ImGui::Button("Export OBJ")) {
                        char nm[16];
                        snprintf(nm, sizeof nm, "stage_%02d", stageId);
                        stageDir = g_exe_dir + "stages";
                        SDL_CreateDirectory(stageDir.c_str());
                        stageDir += std::string("/") + nm;
                        status = stageEdit.exportObj(stageDir, nm) ? "exported to " + stageDir : "can't write " + stageDir;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("An OBJ with its MTL and PNGs (y up, 1 unit = 100 of the game's), for Blender and the like.\n"
                                          "Keep the objects' names to keep their parts; a material's _abrN is its semi-transparency.");
                    if (!stageDir.empty()) {
                        same_line(bw("Open folder"));
                        if (ImGui::Button("Open folder##stage")) SDL_OpenURL(("file:///" + stageDir).c_str());
                    }
                    same_line(bw("Import OBJ..."));
                    if (ImGui::Button("Import OBJ...")) {
                        static const SDL_DialogFileFilter OBJ = {"OBJ models", "obj"};
                        SDL_ShowOpenFileDialog(
                            [](void *u, const char *const *l, int) {
                                if (!l || !*l) return;
                                Picked *p = (Picked *)u;
                                std::lock_guard<std::mutex> lock(p->m);
                                p->obj = *l;
                            },
                            &picked, win, &OBJ, 1, stageDir.empty() ? nullptr : stageDir.c_str(), false);
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Its objects become the stage's parts: textures repacked into the stage's\n"
                                          "three pages, 16 colors each piece, shrunk when they don't fit");
                    if (ImGui::Button(stageDirty ? "Save to game*" : "Save to game")) stage_save();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Writes %s and the stages' table, 0459.bin,\nwhich the native port reads instead of the disc's",
                                          mod_path(si.fid).c_str());
                    if (stageMods.count(si.fid) || stageDirty) {
                        same_line(bw("Revert to the disc's"));
                        if (ImGui::Button("Revert to the disc's")) stage_revert();
                    }
                    int polys = 0;
                    for (const StagePart &pt : stageEdit.parts) polys += (int)pt.faces.size();
                    ImGui::Text("%d parts, %d polygons", (int)stageEdit.parts.size(), polys);
                    if (stageMade.islands) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("textures: %d pieces%s", stageMade.islands, stageMade.shrink < 1 ? ", shrunk to fit" : "");
                    }
                    ImGui::SeparatorText("Parts");
                    ImGui::BeginChild("parts", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 8), ImGuiChildFlags_Borders);
                    int drop = -1;
                    bool rebuild = false;
                    for (int k = 0; k < (int)stageEdit.parts.size(); k++) {
                        StagePart &pt = stageEdit.parts[k];
                        ImGui::PushID(k);
                        rebuild |= ImGui::Checkbox("##nb", &pt.noBounds);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("No bounds check: drawn even when its box seems off screen\n(the ground and the panorama; 8 bones at most)");
                        ImGui::SameLine();
                        char l[96];
                        snprintf(l, sizeof l, "%s  (%d polys)", pt.name.c_str(), (int)pt.faces.size());
                        if (ImGui::Selectable(l, stagePart == k, 0, ImVec2(ImGui::GetContentRegionAvail().x - bw("x") - 8, 0))) {
                            stagePart = stagePart == k ? -1 : k;
                            scene.selected = stagePart >= 0 && stagePart < (int)stageMade.firstBone.size() ? stageMade.firstBone[stagePart] : -1;
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("x")) drop = k;
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove");
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                    if (drop >= 0) {
                        stageEdit.remove(drop);
                        stagePart = -1;
                        rebuild = true;
                    }
                    if (stagePart >= 0 && stagePart < (int)stageEdit.parts.size()) {
                        StagePart &pt = stageEdit.parts[stagePart];
                        ImGui::DragFloat3("move", pt.move, 10, -40000, 40000, "%.0f");
                        rebuild |= ImGui::IsItemDeactivatedAfterEdit();
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("In the game's units: x, y (down), z");
                        ImGui::DragFloat("turn", &pt.turn, 0.5f, -180, 180, "%.1f deg");
                        rebuild |= ImGui::IsItemDeactivatedAfterEdit();
                        ImGui::DragFloat("scale", &pt.scale, 0.01f, 0.05f, 20, "%.2f");
                        rebuild |= ImGui::IsItemDeactivatedAfterEdit();
                    }
                    if (rebuild) stage_rebuild();
                    ImGui::SeparatorText("Add from a stage");
                    std::string from = stageFromId >= 0 ? std::to_string(stageFromId) + "  " + stage_look(stageFromId) : "pick a stage";
                    if (ImGui::BeginCombo("##from", from.c_str())) {
                        for (const StageInfo &o : stageInfos) {
                            std::string l = std::to_string(o.id) + "  " + stage_look(o.id);
                            StageInfo oi;
                            std::vector<uint8_t> ob;
                            if (ImGui::Selectable(l.c_str(), o.id == stageFromId) && stage_load(o.id, stageFrom, oi, ob)) stageFromId = o.id;
                        }
                        ImGui::EndCombo();
                    }
                    if (stageFromId >= 0) {
                        ImGui::BeginChild("from", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 6), ImGuiChildFlags_Borders);
                        for (int k = 0; k < (int)stageFrom.parts.size(); k++) {
                            ImGui::PushID(k);
                            if (ImGui::SmallButton("add")) {
                                char pre[8];
                                snprintf(pre, sizeof pre, "s%02d_", stageFromId);
                                stageEdit.append(stageFrom, k, pre);
                                stagePart = (int)stageEdit.parts.size() - 1;
                                stage_rebuild();
                            }
                            ImGui::SameLine();
                            ImGui::Text("%s  (%d polys)", stageFrom.parts[k].name.c_str(), (int)stageFrom.parts[k].faces.size());
                            ImGui::PopID();
                        }
                        ImGui::EndChild();
                    }
                }
                ImGui::SeparatorText("Stages");
                ImGui::BeginChild("stagelist");
                for (const StageInfo &si : stageInfos) {
                    char l[96];
                    snprintf(l, sizeof l, "%2d  %s%s", si.id, stage_look(si.id), stageMods.count(si.fid) ? "  (mod)" : "");
                    if (ImGui::Selectable(l, si.id == stageId && view.stageView)) stage_open(si.id);
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
                    ImGui::TextDisabled("Click a motion to play it, a camera to look through it, an effect to see it.");
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
                            bool active = ln.motion > 0 || ln.camera >= 0 || ln.effect >= 0;
                            if (!active) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                            if (ImGui::Selectable(ln.text.c_str(), false) && active) {
                                if (ln.motion > 0 && ln.motion <= (int)mm.motions.size()) {
                                    mm.setMotion(ln.motion);
                                    view.playing = true;
                                }
                                if (ln.effect >= 0) open_effect(ln.effect, true);
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
            if (ImGui::BeginTabItem("Effects", nullptr, fxSelect ? ImGuiTabItemFlags_SetSelected : 0)) {
                fxSelect = false;
                fxActive = true;
                if (fx.id >= 0) {
                    if (fxFitPending) fx_fit(io.DisplaySize.x - panel, io.DisplaySize.y), fxFitPending = false;
                    int t = (int)fxTime;
                    if (t != fxDrawn) {
                        fx_render(t, FX_BG);
                        fxTex = rgba_texture(fxTex, fxPx, FXC_W, FXC_H);
                        fxDrawn = t;
                    }
                    /* in the view, where the model is: the canvas, the 320x240 screen
                       around the effect's point (what Export PNGs and own images
                       span) and the point */
                    ImDrawList *dl = ImGui::GetBackgroundDrawList();
                    float cx = (io.DisplaySize.x - panel) / 2 - fxPan[0] * fxZoom, cy = io.DisplaySize.y / 2 - fxPan[1] * fxZoom;
                    dl->AddImage((ImTextureID)(intptr_t)fxTex, ImVec2(cx - FXC_W / 2 * fxZoom, cy - FXC_H / 2 * fxZoom),
                                 ImVec2(cx + FXC_W / 2 * fxZoom, cy + FXC_H / 2 * fxZoom));
                    dl->AddRect(ImVec2(cx - FX_W / 2 * fxZoom, cy - FX_H / 2 * fxZoom), ImVec2(cx + FX_W / 2 * fxZoom, cy + FX_H / 2 * fxZoom),
                                IM_COL32(255, 255, 255, 60));
                    dl->AddLine(ImVec2(cx - 8, cy), ImVec2(cx + 8, cy), IM_COL32(255, 220, 80, 160));
                    dl->AddLine(ImVec2(cx, cy - 8), ImVec2(cx, cy + 8), IM_COL32(255, 220, 80, 160));
                    if (ImGui::Button("Fit (F)")) fxFitPending = true;
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Drag to move, wheel to zoom. The box is the 320x240 screen around the effect's point (the cross).");
                    ImGui::TextWrapped("effect %d: sheet %d, %d sprite animations, %d fields", fx.id, fx.sheetIndex,
                                       (int)fx.anims.size(), fx.duration);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    ImGui::TextWrapped("animations %s #%d entry %d, sheet %s #%d entry %d", fid_name(fx.fid).c_str(), fx.fid,
                                       fx.entry, fid_name(fx.sheetFid).c_str(), fx.sheetFid, fx.sheetEntry);
                    ImGui::PopStyleColor();
                    if (ImGui::Button(fxPlaying ? "Pause" : "Play")) fxPlaying = !fxPlaying;
                    same_line(bw("Restart"));
                    if (ImGui::Button("Restart")) fxTime = 0;
                    same_line(bw("Loop") + ImGui::GetFrameHeight());
                    ImGui::Checkbox("Loop##fx", &fxLoop);
                    same_line(bw("Export PNGs"));
                    if (ImGui::Button("Export PNGs")) {
                        int n = export_effect(fx, fxDir);
                        status = "saved " + std::to_string(n) + " frames to effects/effect_" + std::to_string(fx.id);
                    }
                    if (!fxDir.empty()) {
                        same_line(bw("Open folder"));
                        if (ImGui::Button("Open folder")) SDL_OpenURL(("file:///" + fxDir).c_str());
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", fxDir.c_str());
                    }
                    ImGui::SliderFloat("speed##fx", &view.speed, 0.05f, 4, "%.2fx");
                    float ft = fxTime;
                    if (ImGui::SliderFloat("tick", &ft, 0, (float)std::max(fx_length() - 1, 0), "%.0f")) fxTime = ft, fxPlaying = false;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("1 tick = 1/60 s, as the battle counts time");
                    if (ImGui::CollapsingHeader("Own images (native port)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        bool changed = false;
                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                        ImGui::TextWrapped("%s", own.dir.c_str());
                        ImGui::PopStyleColor();
                        if (ImGui::Button("Mods folder...")) {
                            SDL_ShowOpenFolderDialog(
                                [](void *u, const char *const *list, int) {
                                    if (!list || !*list) return;
                                    Picked *p = (Picked *)u;
                                    std::lock_guard<std::mutex> lock(p->m);
                                    p->folder = *list;
                                },
                                &picked, win, ownRoot.c_str(), false);
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The native port's data/mods/effects folder");
                        same_line(bw("Open"));
                        if (ImGui::Button("Open##own")) {
                            SDL_CreateDirectory(own.dir.c_str());
                            SDL_OpenURL(("file:///" + own.dir).c_str());
                        }
                        same_line(bw("Hit") + ImGui::GetFrameHeight());
                        if (ImGui::RadioButton("Hit", !ownCritical)) ownCritical = false, own_load();
                        same_line(bw("Critical hit") + ImGui::GetFrameHeight());
                        if (ImGui::RadioButton("Critical hit", ownCritical)) ownCritical = true, own_load();
                        if (ImGui::Checkbox("Show them", &ownShow)) fxTime = 0, fxDrawn = -1;
                        same_line(bw("Import PNGs..."));
                        if (ImGui::Button("Import PNGs...")) {
                            static const SDL_DialogFileFilter PNG = {"PNG images", "png"};
                            SDL_ShowOpenFileDialog(
                                [](void *u, const char *const *list, int) {
                                    if (!list) return;
                                    Picked *p = (Picked *)u;
                                    std::lock_guard<std::mutex> lock(p->m);
                                    for (; *list; list++) p->files.push_back(*list);
                                },
                                &picked, win, &PNG, 1, nullptr, true);
                        }
                        same_line(bw("Save*"));
                        if (ImGui::Button(ownDirty ? "Save*" : "Save")) {
                            if (own.save()) ownDirty = false, ownIds = own_effects(ownRoot), status = "saved " + own.dir + "/effect.json";
                            else status = own.error;
                        }
                        same_line(bw("Reload"));
                        if (ImGui::Button("Reload")) own_load();
                        ImGui::TextWrapped("%d frames, %d ticks = %.2f s (the original: %d ticks = %.2f s)", (int)own.frames.size(),
                                    own.duration(), own.duration() / 60.0f, fx.duration, fx.duration / 60.0f);
                        /* two to a row, each half the panel with its label */
                        float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2 - bw("scale");
                        ImGui::PushItemWidth(half);
                        changed |= ImGui::DragFloat("x", &own.x, 0.5f, -320, 320, "%.1f");
                        ImGui::SameLine();
                        changed |= ImGui::DragFloat("y", &own.y, 0.5f, -240, 240, "%.1f");
                        changed |= ImGui::DragFloat("scale", &own.scale, 0.01f, 0.05f, 8, "%.2f");
                        ImGui::SameLine();
                        int blend = own.add;
                        if (ImGui::Combo("blend", &blend, "alpha\0add\0")) own.add = blend != 0, changed = true;
                        ImGui::PopItemWidth();
                        int playing = ownShow ? own.frameAt((int)fxTime) : -1, move = -1, dir = 0, drop = -1, dup = -1;
                        if (!own.frames.empty() && ImGui::BeginTable("ownframes", 4, ImGuiTableFlags_RowBg)) {
                            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
                            ImGui::TableSetupColumn("image", ImGuiTableColumnFlags_WidthStretch);
                            ImGui::TableSetupColumn("ticks", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 6);
                            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
                            ImGui::TableHeadersRow();
                            for (int i = 0; i < (int)own.frames.size(); i++) {
                                CustomFrame &fr = own.frames[i];
                                ImGui::PushID(i);
                                ImGui::TableNextRow();
                                if (i == playing) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(70, 90, 140, 160));
                                ImGui::TableNextColumn();
                                ImGui::Text("%d", i);
                                ImGui::TableNextColumn();
                                ImGui::SetNextItemWidth(-FLT_MIN);
                                if (ImGui::BeginCombo("##file", fr.file.c_str())) {
                                    for (const auto &it : own.images)
                                        if (ImGui::Selectable(it.first.c_str(), it.first == fr.file)) fr.file = it.first, changed = true;
                                    ImGui::EndCombo();
                                }
                                ImGui::TableNextColumn();
                                ImGui::SetNextItemWidth(-FLT_MIN);
                                if (ImGui::InputInt("##ticks", &fr.ticks)) fr.ticks = std::clamp(fr.ticks, 1, 600), changed = true;
                                ImGui::TableNextColumn();
                                if (ImGui::SmallButton("^")) move = i, dir = -1;
                                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Up");
                                ImGui::SameLine();
                                if (ImGui::SmallButton("v")) move = i, dir = 1;
                                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Down");
                                ImGui::SameLine();
                                if (ImGui::SmallButton("+")) dup = i;
                                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy");
                                ImGui::SameLine();
                                if (ImGui::SmallButton("x")) drop = i;
                                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove");
                                ImGui::PopID();
                            }
                            ImGui::EndTable();
                        }
                        if (move >= 0 && move + dir >= 0 && move + dir < (int)own.frames.size())
                            std::swap(own.frames[move], own.frames[move + dir]), changed = true;
                        if (dup >= 0) {
                            CustomFrame copy = own.frames[dup];
                            own.frames.insert(own.frames.begin() + dup + 1, copy), changed = true;
                        }
                        if (drop >= 0) own.frames.erase(own.frames.begin() + drop), changed = true;
                        if (changed) ownDirty = true, fxDrawn = -1;
                        if (!own.error.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", own.error.c_str());
                    }
                    ImGui::Separator();
                } else {
                    ImGui::TextWrapped("Pick an effect: the battle's 2D effects, its sparks, hits and techniques' sprites.");
                }
                if (!fxGrouped) group_effects(), ownIds = own_effects(ownRoot);
                if (ImGui::BeginTabBar("fxlists")) {
                    if (ImGui::BeginTabItem("Original")) {
                        ImGui::BeginChild("fxlist");
                        if (ImGui::CollapsingHeader("Common", ImGuiTreeNodeFlags_DefaultOpen)) {
                            ImGui::TextDisabled("The battle's own (hits, digivolving, items) and those several Digimon start");
                            for (int id : fxCommon) effect_item(id);
                        }
                        if (ImGui::CollapsingHeader("By Digimon", ImGuiTreeNodeFlags_DefaultOpen)) {
                            for (const auto &it : fxByDigimon) {
                                bool open = std::find(it.second.begin(), it.second.end(), fx.id) != it.second.end();
                                ImGui::SetNextItemOpen(open, ImGuiCond_Once);
                                if (ImGui::TreeNode(it.first.c_str())) {
                                    for (int id : it.second) effect_item(id);
                                    ImGui::TreePop();
                                }
                            }
                        }
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }
                    char modTab[32];
                    snprintf(modTab, sizeof modTab, "Mod (%d)###mod", (int)ownIds.size());
                    if (ImGui::BeginTabItem(modTab)) {
                        ImGui::BeginChild("fxmods");
                        if (ownIds.empty()) ImGui::TextWrapped("No effect has own images in %s yet.", ownRoot.c_str());
                        for (int id : std::set<int>(ownIds)) effect_item(id); /* a copy: opening one rescans ownIds */
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
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
        about_window(aboutOpen, aboutIcon);
        ImGui::Render();

        if (fxActive) { /* the effect, drawn by ImGui over the cleared view */
            GL(glViewport)(0, 0, ww, wh);
            GL(glClearColor)((FX_BG & 255) / 255.0f, (FX_BG >> 8 & 255) / 255.0f, (FX_BG >> 16 & 255) / 255.0f, 1);
            GL(glClear)(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        } else {
            render(scene, view, ww - (int)(panel * ww / std::max(io.DisplaySize.x, 1.0f)), wh);
        }
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
