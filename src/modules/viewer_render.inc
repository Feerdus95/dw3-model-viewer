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
    bool blendJoints = false; /* the joints with a JointBlend drawn blended (experimental) */
    bool playing = true, loop = true;
    float speed = 1, ticks = 0;
    int texCombo = 0;
    float texZoom = 2;
    bool uvs = true;
    bool stageView = false;          /* a fight stage: its background, no grid */
    float stageBg[3] = {0, 0, 0};
};

static GLuint g_mesh, g_line, g_lineVao, g_lineVbo, g_sprite;

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
                   const std::vector<ExtraModel> &extras = {}) {
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
    GLint loc_mvpO = GL(glGetUniformLocation)(g_mesh, "mvpO"), loc_worldO = GL(glGetUniformLocation)(g_mesh, "worldO");
    GLint loc_blend = GL(glGetUniformLocation)(g_mesh, "blendOn"), loc_fade = GL(glGetUniformLocation)(g_mesh, "blendFade");
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
                /* a joint blended: the vertices with a weight also follow their bone's partners (a partner that isn't shown: itself) */
                float on = 0, fade[3] = {1, 1, 1};
                float mvpO[3][16], worldO[3][16];
                for (int k = 0; k < 3; k++) memcpy(mvpO[k], mvp.m, sizeof mvpO[k]), memcpy(worldO[k], pose.m, sizeof worldO[k]);
                const BoneBlend &bb = sc.blend.bone[b];
                if (v.blendJoints && bb.n && !g_blendBridge) { /* the bridge stands in for the weld */
                    on = 1;
                    for (int k = 0; k < bb.n; k++) {
                        if (!m.pose[bb.partner[k]].visible) continue;
                        M4 pose2 = mul(pre, mul(bone_pose(sc, bb.partner[k]), from(bb.rel[k]))), mvp2 = mul(vp, mul(PSX_TO_GL, pose2));
                        memcpy(mvpO[k], mvp2.m, sizeof mvpO[k]), memcpy(worldO[k], pose2.m, sizeof worldO[k]);
                        if (g_blendFade) fade[k] = blend_fade(pose.m, pose2.m);
                    }
                }
                GL(glUniformMatrix4fv)(loc_mvpO, 3, GL_FALSE, &mvpO[0][0]);
                GL(glUniformMatrix4fv)(loc_worldO, 3, GL_FALSE, &worldO[0][0]);
                GL(glUniform1f)(loc_blend, on);
                GL(glUniform3fv)(loc_fade, 1, fade);
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
        if (v.blendJoints && g_blendBridge && !sc.bridges.empty()) {
            /* --weld-bridge: per pair, a sleeve from ring a to ring b along a Hermite curve that leaves a and enters b along the joint's axis; the first
               half takes a's ring's attributes, the second b's. Built in the model's space, so the bones' own matrices are the identity here. */
            const int SEG = 8;
            std::vector<GVert> out;
            for (const Bridge &br : sc.bridges) {
                if (!m.pose[br.a].visible || !m.pose[br.b].visible || sc.hidden[br.a] || sc.hidden[br.b]) continue;
                M4 pa = mul(pre, bone_pose(sc, br.a)), pb = mul(pre, bone_pose(sc, br.b));
                auto dir = [](const M4 &p, const float *a, float *o) {
                    float l = 0;
                    for (int i = 0; i < 3; i++) o[i] = p.m[i] * a[0] + p.m[4 + i] * a[1] + p.m[8 + i] * a[2], l += o[i] * o[i];
                    l = std::max(sqrtf(l), 1e-6f);
                    for (int i = 0; i < 3; i++) o[i] /= l;
                };
                float tA[3], tB[3];
                dir(pa, br.axA, tA), dir(pb, br.axB, tB);
                size_t n = br.va.size();
                std::vector<std::vector<GVert>> col(n); /* each ring vertex's column of SEG+1 points */
                for (size_t i = 0; i < n; i++) {
                    float A[3], B[3], nA[3], nB[3];
                    blend_point(pa.m, br.va[i], A), blend_point(pb.m, br.vb[i], B);
                    dir(pa, br.ga[i].nrm, nA), dir(pb, br.gb[i].nrm, nB);
                    float L = sqrtf((B[0] - A[0]) * (B[0] - A[0]) + (B[1] - A[1]) * (B[1] - A[1]) + (B[2] - A[2]) * (B[2] - A[2]));
                    for (int s = 0; s <= SEG; s++) {
                        float t = (float)s / SEG, t2 = t * t, t3 = t2 * t;
                        float h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t, h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
                        GVert g = s * 2 <= SEG ? br.ga[i] : br.gb[i];
                        for (int k = 0; k < 3; k++) g.pos[k] = h00 * A[k] + h10 * L * tA[k] + h01 * B[k] + h11 * L * tB[k], g.nrm[k] = (1 - t) * nA[k] + t * nB[k];
                        g.mode &= ~16u; /* opaque */
                        g.wgt[0] = g.wgt[1] = g.wgt[2] = 0;
                        col[i].push_back(g);
                    }
                }
                for (size_t i = 0; i < n; i++) {
                    size_t j = (i + 1) % n;
                    if (!col[i].size()) continue;
                    for (int s = 0; s < SEG; s++) {
                        out.push_back(col[i][s]), out.push_back(col[j][s]), out.push_back(col[i][s + 1]);
                        out.push_back(col[j][s]), out.push_back(col[j][s + 1]), out.push_back(col[i][s + 1]);
                    }
                }
            }
            if (!out.empty()) {
                if (!sc.bvao) GL(glGenVertexArrays)(1, &sc.bvao), GL(glGenBuffers)(1, &sc.bvbo);
                GL(glBindVertexArray)(sc.bvao);
                GL(glBindBuffer)(GL_ARRAY_BUFFER, sc.bvbo);
                GL(glBufferData)(GL_ARRAY_BUFFER, out.size() * sizeof(GVert), out.data(), GL_STREAM_DRAW);
                gvert_attribs();
                M4 I = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}}, mvp = mul(vp, PSX_TO_GL);
                float on = 0, fade[3] = {1, 1, 1};
                GL(glUniformMatrix4fv)(loc_mvp, 1, GL_FALSE, mvp.m);
                GL(glUniformMatrix4fv)(loc_world, 1, GL_FALSE, I.m);
                GL(glUniform1f)(loc_blend, on);
                GL(glUniform3fv)(loc_fade, 1, fade);
                GL(glUniform4f)(loc_tint, 0, 0, 0, 0);
                GL(glDisable)(GL_CULL_FACE);
                GL(glDrawArrays)(GL_TRIANGLES, 0, (GLsizei)out.size());
                if (v.cull) GL(glEnable)(GL_CULL_FACE);
                GL(glBindVertexArray)(sc.vao);
            }
        }
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

