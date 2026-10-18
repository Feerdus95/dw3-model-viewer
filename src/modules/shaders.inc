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

/* A sprite effect's picture: a quad given in clip space (its middle C, half size H), no vertex data */
static const char *SPRITE_VS = R"(#version 330 core
uniform vec3 c;
uniform vec2 h;
out vec2 uv;
void main() {
    vec2 q = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    uv = vec2(q.x, 1.0 - q.y);
    gl_Position = vec4(c.xy + (q * 2.0 - 1.0) * h, c.z, 1.0);
}
)";

static const char *SPRITE_FS = R"(#version 330 core
uniform sampler2D tex;
in vec2 uv;
out vec4 frag;
void main() { frag = texture(tex, uv); }
)";

/* A stage prop (a billboard's PNG), a quad in the world cut out by its alpha */
static const char *PROP_VS = R"(#version 330 core
layout(location = 0) in vec3 pos;
layout(location = 1) in vec2 uv_in;
layout(location = 2) in vec4 tint; /* the colour, and how much fog it takes */
uniform mat4 mvp;
out vec2 uv;
out vec4 col;
out float vz;
void main() { gl_Position = mvp * vec4(pos, 1.0); uv = uv_in; col = tint; vz = gl_Position.w; }
)";

/* The same props from tables, the card doing the work: the port's PINST_VS (gl.c) in the view's world (y up, z the other way).
   mode 0: a billboard of the table; 1: a particle of an emitter; 2: a leaf of the stage; 3: a vertex of a model (its mesh kept
   on the card, one instance each) */
static const char *PGPU_VS = R"(#version 330 core
layout(location = 0) in vec3 a0;   /* billboard: its foot (the port's world); model: the vertex */
layout(location = 1) in vec4 a1;   /* billboard: width, height, sway, turn; model: u, v */
layout(location = 2) in vec4 a2;   /* billboard: tint, fog */
layout(location = 3) in float a3;  /* billboard: 1 flat in the world */
layout(location = 4) in vec3 vnrm;
layout(location = 5) in vec4 mi0;  /* model instance: foot, scale */
layout(location = 6) in vec4 mi1;  /* turn, sway, fog */
layout(location = 7) in vec4 mi2;  /* tint */
uniform mat4 mvp;
uniform int mode;
uniform vec3 right;   /* a billboard's width, facing the camera */
uniform float t;      /* the wind's clock (0: still) */
uniform float clock;  /* the particles' */
uniform vec2 ww;      /* the wind (the port's world) */
uniform vec2 mmesh;   /* a model's lowest y and height */
uniform vec4 pc;      /* particles: centre, radius */
uniform vec4 pa;      /* top, fall, wind, how far above the top they start */
uniform vec4 pb;      /* sizes, where they land, count */
uniform vec4 ptint;   /* tint, fog */
out vec2 uv;
out vec4 col;
out float vz;
const int TRI[6] = int[6](0, 1, 2, 1, 3, 2);
const vec3 PAL[5] = vec3[5](vec3(58, 102, 34), vec3(74, 118, 38), vec3(110, 120, 40), vec3(128, 86, 36), vec3(96, 70, 34));
uint hsh(uint x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }
float rnd(uint x) { return float(hsh(x) >> 8) / 16777216.0; }
vec2 windat(float tt, float w) {
    float gust = 0.6 + 0.4 * sin(tt * 0.011) * sin(tt * 0.0037 + 1.0);
    float dir = 0.6 + 0.5 * sin(tt * 0.0013);
    return vec2(cos(dir), sin(dir)) * 18.0 * w * gust;
}
void put(vec3 p, vec2 tc, vec4 c) { gl_Position = mvp * vec4(p, 1.0); uv = tc; col = c; vz = gl_Position.w; }
void main() {
    int k = gl_VertexID % 6;
    if (mode == 0) {
        int c = TRI[k];
        float w = a1.x, h = a1.y, side = (c & 1) == 1 ? 1.0 : -1.0;
        float a = a1.w * 0.017453293;
        vec3 r = a3 > 0.5 ? vec3(cos(a), 0.0, -sin(a)) : right;
        float bend = a1.z / 18.0 * h * 0.3 * (0.5 + sin(t * 0.042 + (a0.x + a0.z) * 0.0004));
        vec3 p = vec3(a0.x, a0.y, -a0.z) + r * (w * 0.5 * side);
        if (c < 2) p += vec3(ww.x * bend, h, -ww.y * bend);
        put(p, vec2(float(c & 1), float(c >> 1)), a2);
        return;
    }
    if (mode == 3) {
        float sc = mi0.w, a = mi1.x * 0.017453293, ca = cos(a), sa = sin(a), height = mmesh.y * sc;
        float lx = a0.x * sc, ly = (a0.y - mmesh.x) * sc, lz = a0.z * sc;
        float rel = clamp(ly / height, 0.0, 1.0);
        float bend = mi1.y / 18.0 * height * 0.3 * (0.5 + sin(t * 0.042 + (mi0.x + mi0.z) * 0.0004));
        vec2 wd = ww * (bend * rel * rel);
        vec3 p = vec3(mi0.x + lx * ca + lz * sa + wd.x, mi0.y + ly, -(mi0.z - lx * sa + lz * ca + wd.y));
        float nx = vnrm.x * ca + vnrm.z * sa, nz = -vnrm.x * sa + vnrm.z * ca;
        float shade = 0.55 + 0.45 * max((nx * 0.35 + vnrm.y * 0.8 + nz * 0.45) / 0.98, 0.0);
        put(p, a1.xy, vec4(mi2.rgb * shade, mi1.z));
        return;
    }
    uint i = uint(gl_InstanceID);
    if (float(i) >= pb.w) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); uv = vec2(0.0); col = vec4(0.0); vz = 1.0; return; }
    float vfall = pa.y * (5.0 + rnd(i * 7u + 1u) * 6.0) * 0.89;
    float L = max((pa.x + pa.w * 0.5 - pb.z) / vfall, 1.0);
    float ph = clock + rnd(i * 7u + 2u) * L, cyc = floor(ph / L), age = ph - cyc * L;
    uint hs = i * 7919u + uint(cyc) * 104729u + 13u;
    float a0r = rnd(hs) * 6.2831853, r = sqrt(rnd(hs + 1u)) * pc.w;
    float psize = pb.x + rnd(hs + 2u) * (pb.y - pb.x), phase = rnd(hs + 3u) * 6.2831853;
    float spin = (rnd(hs + 4u) - 0.5) * 0.12, ang = rnd(hs + 5u) * 6.2831853 + spin * age;
    float ts = clock - age;
    vec2 w0 = windat(clock - age * 0.5, pa.z), ws = windat(ts, pa.z) * 120.0;
    float dx = w0.x * age + 9.0 / 0.045 * (cos(0.045 * ts + phase) - cos(0.045 * clock + phase));
    float dz = w0.y * age + 6.0 / 0.037 * (sin(0.037 * clock + phase) - sin(0.037 * ts + phase));
    vec3 wp = vec3(pc.x + cos(a0r) * r - ws.x + dx, -(pc.y + pa.x + pa.w * 0.5 - vfall * age), pc.z + sin(a0r) * r - ws.y + dz);
    vec3 at = vec3(wp.x, -wp.y, -wp.z);
    float half_ = psize * 0.5, flip = 0.25 + 0.75 * abs(cos(t * 0.06 + phase * 2.0)), cs = cos(ang), sn = sin(ang), lx, ly;
    int c = TRI[k];
    vec4 cc;
    vec2 tc;
    if (mode == 1) {
        lx = (c & 1) == 1 ? 1.0 : -1.0, ly = c < 2 ? -1.0 : 1.0;
        cc = ptint;
        tc = vec2(float(c & 1), float(c >> 1));
    } else { /* a diamond: its apex, right, left, base */
        lx = c == 1 ? 0.45 : c == 2 ? -0.45 : 0.0, ly = c == 0 ? -1.0 : c == 3 ? 1.0 : 0.0;
        cc = vec4(PAL[int(rnd(hs + 6u) * 5.0) % 5] / 255.0, 1.0);
        tc = vec2(0.5);
    }
    lx *= flip;
    float pdx = (lx * cs - ly * sn) * half_, pdy = (lx * sn + ly * cs) * half_;
    put(at + right * pdx + vec3(0.0, -pdy, 0.0), tc, cc);
}
)";

static const char *PROP_FS = R"(#version 330 core
uniform sampler2D tex;
uniform vec4 fog;  /* the list's own fog: colour, how much */
uniform vec2 fogz; /* where it starts and is thickest, in depth */
in vec2 uv;
in vec4 col;
in float vz;
out vec4 frag;
void main() {
    vec4 t = texture(tex, uv);
    if (t.a < 0.5) discard;
    vec3 c = t.rgb * col.rgb;
    if (fog.a > 0.0) c = mix(c, fog.rgb, fog.a * col.a * smoothstep(fogz.x, fogz.y, vz));
    frag = vec4(c, 1.0);
}
)";
