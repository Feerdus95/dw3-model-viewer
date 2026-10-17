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
layout(location = 2) in vec3 tint;
uniform mat4 mvp;
out vec2 uv;
out vec3 col;
void main() { gl_Position = mvp * vec4(pos, 1.0); uv = uv_in; col = tint; }
)";

static const char *PROP_FS = R"(#version 330 core
uniform sampler2D tex;
in vec2 uv;
in vec3 col;
out vec4 frag;
void main() {
    vec4 t = texture(tex, uv);
    if (t.a < 0.5) discard;
    frag = vec4(t.rgb * col, 1.0);
}
)";
