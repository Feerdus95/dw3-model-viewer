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

/* A bone's place in the model's space: its pose, after the edit previewed on it */
static M4 bone_pose(const Scene &s, int b) {
    M4 w = from(s.model->pose[b].world);
    return b >= s.previewFirst && b < s.previewEnd ? mul(from(s.preview), w) : w;
}
/* ... and in the view's world */
static M4 bone_world(const Scene &s, int b) { return mul(PSX_TO_GL, bone_pose(s, b)); }

/* The scene in the view at X, Y (from the window's bottom left), W x H pixels */
static void render(Scene &s, View &v, int x, int y, int w, int h) {
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
            M4 pose = bone_pose(s, (int)b), mvp = mul(vp, mul(PSX_TO_GL, pose));
            GL(glUniformMatrix4fv)(loc_mvp, 1, GL_FALSE, mvp.m);
            GL(glUniformMatrix4fv)(loc_world, 1, GL_FALSE, pose.m);
            int mark = b < s.mark.size() ? s.mark[b] : 0;
            if (mark == 2) GL(glUniform4f)(loc_tint, 1, 0.85f, 0.2f, 0.35f);
            else GL(glUniform4f)(loc_tint, 0.55f, 0.8f, 1, mark == 1 ? 0.22f : 0);
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
            bool sel = b < s.mark.size() ? s.mark[b] == 2 : (int)b == s.selected;
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

