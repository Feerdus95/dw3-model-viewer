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
    bool marks = false; /* a cross where each effect of a script is */
    bool playing = true, loop = true;
    float speed = 1, ticks = 0;
    int texCombo = 0;
    float texZoom = 2;
    bool uvs = true;
    bool stageView = false;          /* a fight stage: its background, no grid */
    float stageBg[3] = {0, 0, 0};
    float pfog[4] = {}, pfogz[2] = {}; /* the props' own fog when set (alpha 0: none): colour and amount; where it starts and ends */
};

static GLuint g_mesh, g_line, g_lineVao, g_lineVbo, g_sprite, g_prop, g_propVao, g_propVbo;

/* A stage prop on the view: its PNG's texture on a quad (top left, top right,
   bottom left, bottom right corners, in the view's world), times a tint */
struct PropQuad {
    GLuint tex = 0;
    std::vector<float> v; /* 9 floats a vertex: position, uv, tint, how much fog it takes */
};

static void pq_vertex(PropQuad &q, const float p[3], float u, float v, const float tint[3], float fog) {
    const float o[9] = {p[0], p[1], p[2], u, v, tint[0], tint[1], tint[2], fog};
    q.v.insert(q.v.end(), o, o + 9);
}

/* four corners (top left, top right, bottom left, bottom right) as two triangles */
static void pq_quad(PropQuad &q, const float c[4][3], const float tint[3], float fog) {
    static const int TRI[6] = {0, 1, 2, 1, 3, 2};
    static const float UV[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    for (int k : TRI) pq_vertex(q, c[k], UV[k][0], UV[k][1], tint, fog);
}

/* A sprite effect's picture (the 320x240 screen around its point) as the
   battle draws it: at its point in the view's world, or at a screen position
   (SCREEN 1 in front of everything, 2 behind it), sorted among the models by
   its depth unless FRONT, the layer drawn over them */
struct SpriteDraw {
    GLuint tex = 0;
    float pos[3] = {0, 0, 0};
    int screen = 0;
    bool front = false;
};

/* A model other than the scene's (an effect model), posed in the scene's
   space by PRE (its root's matrix, in the model's own PSX space) */
struct ExtraModel {
    Scene *scene = nullptr;
    M4 pre;
};

/* A billboard's corners (top left, top right, bottom left, bottom right) in
   the view's world. The port's props have y up and z as the game's, which the
   view has the other way (-z); a billboard faces the camera (YAW) unless fixed */
static void prop_corners(const StageProp &p, float yaw, float c[4][3], float bx = 0, float bz = 0) {
    float w = p.size[0] * p.scale, h = p.size[1] * p.scale, a = p.fixed ? p.rot * 0.017453293f : yaw;
    float rx = cosf(a) * w * 0.5f, rz = -sinf(a) * w * 0.5f;
    for (int k = 0; k < 4; k++) {
        float side = k & 1 ? 1.0f : -1.0f;
        c[k][0] = p.pos[0] + rx * side + (k < 2 ? bx : 0), c[k][1] = p.pos[1] + (k < 2 ? h : 0), c[k][2] = -p.pos[2] + rz * side - (k < 2 ? bz : 0);
    }
}

/* The wind as the port has it (stagefx.c's wind_at), T in frames, and how far it bends a prop of height H whose
   sway is p's: the tip's offset is the wind times this (the port's world) */
static void prop_wind(float t, float wind, float out[2]) {
    float gust = 0.6f + 0.4f * sinf(t * 0.011f) * sinf(t * 0.0037f + 1.0f), dir = 0.6f + 0.5f * sinf(t * 0.0013f);
    out[0] = cosf(dir) * 18.0f * wind * gust, out[1] = sinf(dir) * 18.0f * wind * gust;
}
static float prop_bend(const StageProp &p, float t, float h) {
    return p.sway / 18.0f * h * 0.3f * (0.5f + sinf(t * 0.042f + (p.pos[0] + p.pos[2]) * 0.0004f));
}

/* Falling particles as the port moves them (stagefx.c), in its world: y down, z not negated. The stage's leaves
   are the same with the arena's numbers. */
struct PSim {
    float p[3], size, phase, spin, angle, fall;
    int shade;
};
struct PSimCfg {
    float c[3], radius, top, fall, size[2], wind, above, outer, floor;
};
static float psim_rnd() {
    static uint32_t s = 0x2003;
    s = s * 1664525u + 1013904223u;
    return (s >> 8) / 16777216.0f;
}
static void psim_spawn(const PSimCfg &e, PSim &l, bool anywhere, const float wind[2]) {
    float a = psim_rnd() * 6.2831853f, r = sqrtf(psim_rnd()) * e.radius;
    l.p[0] = e.c[0] + cosf(a) * r, l.p[2] = e.c[2] + sinf(a) * r;
    l.p[1] = -(e.c[1] + (anywhere ? psim_rnd() * e.top : e.top + psim_rnd() * e.above));
    if (!anywhere) l.p[0] -= wind[0] * 120.0f, l.p[2] -= wind[1] * 120.0f;
    l.size = e.size[0] + psim_rnd() * (e.size[1] - e.size[0]);
    l.phase = psim_rnd() * 6.2831853f, l.spin = (psim_rnd() - 0.5f) * 0.12f, l.angle = psim_rnd() * 6.2831853f;
    l.fall = e.fall * (5.0f + psim_rnd() * 6.0f), l.shade = (int)(psim_rnd() * 5) % 5;
}
/* frame T of COUNT of them (all anew if V isn't that size); PLAY: they move */
static void psim_step(const PSimCfg &e, std::vector<PSim> &v, int count, float t, bool play) {
    float wind[2];
    prop_wind(t, e.wind, wind);
    if ((int)v.size() != count) {
        v.resize(count);
        for (PSim &l : v) psim_spawn(e, l, true, wind);
    }
    if (!play) return;
    for (PSim &l : v) {
        float sway = sinf(t * 0.045f + l.phase), dx = l.p[0] - e.c[0], dz = l.p[2] - e.c[2];
        l.p[0] += wind[0] + sway * 9.0f;
        l.p[2] += wind[1] + cosf(t * 0.037f + l.phase) * 6.0f;
        l.p[1] += l.fall * (0.7f + 0.3f * fabsf(sway));
        l.angle += l.spin;
        if (l.p[1] > -e.c[1] - e.floor || dx * dx + dz * dz > e.radius * e.radius * e.outer) psim_spawn(e, l, false, wind);
    }
}

/* A model's point (the OBJ's x, y, z) in the view's world: scaled, turned, set down, bent by WW * BEND at its height squared */
static void prop_model_point(const StageProp &p, const ObjMesh &m, float x, float y, float z, const float ww[2], float bend, float out[3]) {
    float a = p.rot * 0.017453293f, ca = cosf(a), sa = sinf(a), lx = x * p.scale, ly = (y - m.miny) * p.scale, lz = z * p.scale;
    float rel = std::clamp(ly / (m.h * p.scale), 0.0f, 1.0f);
    out[0] = p.pos[0] + lx * ca + lz * sa + ww[0] * bend * rel * rel;
    out[1] = p.pos[1] + ly;
    out[2] = -(p.pos[2] - lx * sa + lz * ca + ww[1] * bend * rel * rel);
}

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

/* A bone's place in the model's space: its pose, after the edit previewed on it */
static M4 bone_pose(const Scene &s, int b) {
    M4 w = from(s.model->pose[b].world);
    return b >= s.previewFirst && b < s.previewEnd ? mul(from(s.preview), w) : w;
}
/* ... and in the view's world */
static M4 bone_world(const Scene &s, int b) { return mul(PSX_TO_GL, bone_pose(s, b)); }

/* The sprite effects' pictures, far to near: each a quad as tall as the view (the PSX screen is 240 lines)
   at its point's depth, so the models in front of it hide it */
static void draw_sprites(const std::vector<SpriteDraw> &sprites, const M4 &vp, int w, int h) {
    if (sprites.empty()) return;
    struct Q {
        const SpriteDraw *s;
        float c[3];
    };
    std::vector<Q> qs;
    for (const SpriteDraw &s : sprites) {
        Q q = {&s, {0, 0, 0}};
        if (s.screen) { /* the point is a screen pixel from the middle */
            q.c[0] = s.pos[0] * 2.0f / 240 * h / w;
            q.c[1] = -s.pos[1] * 2.0f / 240;
            q.c[2] = s.screen == 1 ? -1.0f : 0.99999f;
        } else {
            const float *m = vp.m, *p = s.pos;
            float cw = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
            if (cw < 1e-3f) continue;
            q.c[0] = (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]) / cw;
            q.c[1] = (m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]) / cw;
            q.c[2] = (m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]) / cw;
        }
        qs.push_back(q);
    }
    /* the front layer last, the rest far to near */
    std::stable_sort(qs.begin(), qs.end(), [](const Q &a, const Q &b) {
        if (a.s->front != b.s->front) return !a.s->front;
        return a.c[2] > b.c[2];
    });
    GL(glUseProgram)(g_sprite);
    GL(glBindVertexArray)(g_lineVao);
    GL(glActiveTexture)(GL_TEXTURE0);
    GL(glUniform1i)(GL(glGetUniformLocation)(g_sprite, "tex"), 0);
    GL(glUniform2f)(GL(glGetUniformLocation)(g_sprite, "h"), 320.0f / 240 * h / w, 1.0f);
    GL(glEnable)(GL_BLEND);
    GL(glBlendFunc)(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    GL(glDepthFunc)(GL_LEQUAL);
    GL(glDepthMask)(GL_FALSE);
    for (const Q &q : qs) {
        if (q.s->front || (q.s->screen == 1)) GL(glDisable)(GL_DEPTH_TEST);
        else GL(glEnable)(GL_DEPTH_TEST);
        GL(glBindTexture)(GL_TEXTURE_2D, q.s->tex);
        GL(glUniform3f)(GL(glGetUniformLocation)(g_sprite, "c"), q.c[0], q.c[1], q.c[2]);
        GL(glDrawArrays)(GL_TRIANGLE_STRIP, 0, 4);
    }
    GL(glDisable)(GL_BLEND);
    GL(glDepthMask)(GL_TRUE);
}

/* The scene in the view at X, Y (from the window's bottom left), W x H pixels */
static void render(Scene &s, View &v, int x, int y, int w, int h, const std::vector<SpriteDraw> &sprites = {},
                   const std::vector<ExtraModel> &extras = {}, const std::vector<PropQuad> &props = {},
                   const std::vector<float> &propLines = {}) {
    if (v.stageView)
        GL(glClearColor)(v.stageBg[0], v.stageBg[1], v.stageBg[2], 1);
    else
        GL(glClearColor)(0.18f, 0.2f, 0.24f, 1);
    GL(glClear)(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    GL(glViewport)(x, y, w, h);
    M4 vp = mul(perspective(v.fov, (float)w / std::max(h, 1), v.cam.dist * 0.01f, v.cam.dist * 50), v.cam.view());
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
    GLint loc_mvp = GL(glGetUniformLocation)(g_mesh, "mvp"), loc_world = GL(glGetUniformLocation)(g_mesh, "world");
    GLint loc_pass = GL(glGetUniformLocation)(g_mesh, "pass"), loc_abr = GL(glGetUniformLocation)(g_mesh, "abr");
    GLint loc_tint = GL(glGetUniformLocation)(g_mesh, "tint");
    /* a model: its bones through PRE, opaque first and then the semi-transparent, each blending its own way */
    auto draw_model = [&](Scene &sc, const M4 &pre) {
        Model &m = *sc.model;
        GL(glBindTexture)(GL_TEXTURE_2D, sc.vram);
        GL(glBindVertexArray)(sc.vao);
        auto draw_bones = [&]() {
            for (size_t b = 1; b < m.bones.size(); b++) {
                if (!m.pose[b].visible || sc.hidden[b] || !sc.count[b]) continue;
                M4 pose = mul(pre, bone_pose(sc, (int)b)), mvp = mul(vp, mul(PSX_TO_GL, pose));
                GL(glUniformMatrix4fv)(loc_mvp, 1, GL_FALSE, mvp.m);
                GL(glUniformMatrix4fv)(loc_world, 1, GL_FALSE, pose.m);
                int mark = b < sc.mark.size() ? sc.mark[b] : 0;
                if (mark == 2) GL(glUniform4f)(loc_tint, 1, 0.85f, 0.2f, 0.35f);
                else GL(glUniform4f)(loc_tint, 0.55f, 0.8f, 1, mark == 1 ? 0.22f : 0);
                GL(glDrawArrays)(GL_TRIANGLES, sc.first[b], sc.count[b]);
            }
        };
        GL(glUniform1i)(loc_pass, 0);
        GL(glUniform1i)(loc_abr, -1);
        GL(glDepthMask)(GL_TRUE);
        draw_bones();
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
    };
    const M4 same = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    draw_model(s, same);
    for (const ExtraModel &e : extras)
        if (e.scene && e.scene->model && e.scene->vao) draw_model(*e.scene, e.pre);
    GL(glDisable)(GL_CULL_FACE);
    GL(glPolygonMode)(GL_FRONT_AND_BACK, GL_FILL);
    if (!props.empty()) {
        GL(glUseProgram)(g_prop);
        GL(glUniformMatrix4fv)(GL(glGetUniformLocation)(g_prop, "mvp"), 1, GL_FALSE, vp.m);
        GL(glUniform1i)(GL(glGetUniformLocation)(g_prop, "tex"), 0);
        GL(glUniform4f)(GL(glGetUniformLocation)(g_prop, "fog"), v.pfog[0], v.pfog[1], v.pfog[2], v.pfog[3]);
        GL(glUniform2f)(GL(glGetUniformLocation)(g_prop, "fogz"), v.pfogz[0], v.pfogz[1]);
        GL(glActiveTexture)(GL_TEXTURE0);
        GL(glBindVertexArray)(g_propVao);
        GL(glBindBuffer)(GL_ARRAY_BUFFER, g_propVbo);
        for (const PropQuad &q : props) {
            if (q.v.empty()) continue;
            GL(glBufferData)(GL_ARRAY_BUFFER, q.v.size() * 4, q.v.data(), GL_STREAM_DRAW);
            GL(glBindTexture)(GL_TEXTURE_2D, q.tex);
            GL(glDrawArrays)(GL_TRIANGLES, 0, (GLsizei)(q.v.size() / 9));
        }
    }
    if (v.skeleton) {
        std::vector<float> l;
        for (size_t b = 1; b < m.bones.size(); b++) {
            const float *w = m.pose[b].world, *pw = m.pose[m.bones[b].parent].world;
            float a[3] = {pw[12], -pw[13], -pw[14]}, c[3] = {w[12], -w[13], -w[14]};
            bool sel = b < s.mark.size() ? s.mark[b] == 2 : (int)b == s.selected;
            line(l, a, c, sel ? 1.0f : 0.2f, sel ? 0.9f : 1.0f, sel ? 0.2f : 0.4f);
        }
        GL(glDisable)(GL_DEPTH_TEST);
        draw_lines(l, vp);
    }
    if (v.marks) { /* where the effects are, against the model: a cross each */
        std::vector<float> l;
        float k = v.cam.dist * 0.03f;
        auto cross = [&](const float p[3], float r, float g, float b) {
            for (int a = 0; a < 3; a++) {
                float lo[3] = {p[0], p[1], p[2]}, hi[3] = {p[0], p[1], p[2]};
                lo[a] -= k, hi[a] += k;
                line(l, lo, hi, r, g, b);
            }
        };
        for (const SpriteDraw &sp : sprites)
            if (!sp.screen) cross(sp.pos, 1, 0.3f, 0.9f);
        for (const ExtraModel &e : extras) {
            float p[3] = {e.pre.m[12], -e.pre.m[13], -e.pre.m[14]};
            cross(p, 0.3f, 0.9f, 1);
        }
        GL(glDisable)(GL_DEPTH_TEST);
        draw_lines(l, vp);
    }
    draw_sprites(sprites, vp, w, h);
    if (!propLines.empty()) {
        GL(glDisable)(GL_DEPTH_TEST);
        draw_lines(propLines, vp);
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

