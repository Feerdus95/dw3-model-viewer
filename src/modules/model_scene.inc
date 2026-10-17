/* --- the model on the card --- */

struct GVert {
    float pos[3], nrm[3], uv[2];
    uint8_t color[4];
    uint16_t tex[4], limits[4];
    uint32_t mode;
    float wgt[3]; /* how much it follows each of its bone's partners (BoneBlend) */
};

struct Scene {
    std::unique_ptr<Model> model;
    std::vector<int> first, count; /* each bone's vertices */
    std::vector<bool> hidden;
    BlendSet blend;                 /* the joints that can be drawn blended */
    int selected = -1;
    std::vector<uint8_t> mark; /* each bone's highlight: 1 under the mouse, 2 selected */
    /* an edit in progress, drawn before it's built: bones previewFirst up
       to previewEnd moved by preview (in the model's space) */
    float preview[16] = {};
    int previewFirst = 0, previewEnd = 0;
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
    blend_build(m, s.blend);
    for (size_t b = 1; b < m.bones.size(); b++) {
        const Mesh &mesh = m.meshes[m.bones[b].mesh];
        s.first[b] = (int)v.size();
        const BoneBlend *bw = s.blend.bone[b].n ? &s.blend.bone[b] : nullptr; /* this bone's vertices' blend weights, if it is in a pair */
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
                    if (bw)
                        for (int j = 0; j < 3; j++) g.wgt[j] = bw->w[p.idx[k]][j];
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
    for (int i = 0; i < 8; i++) GL(glEnableVertexAttribArray)(i);
    GL(glVertexAttribPointer)(0, 3, GL_FLOAT, GL_FALSE, sizeof(GVert), (void *)offsetof(GVert, pos));
    GL(glVertexAttribPointer)(1, 3, GL_FLOAT, GL_FALSE, sizeof(GVert), (void *)offsetof(GVert, nrm));
    GL(glVertexAttribPointer)(2, 2, GL_FLOAT, GL_FALSE, sizeof(GVert), (void *)offsetof(GVert, uv));
    GL(glVertexAttribPointer)(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GVert), (void *)offsetof(GVert, color));
    GL(glVertexAttribIPointer)(4, 4, GL_UNSIGNED_SHORT, sizeof(GVert), (void *)offsetof(GVert, tex));
    GL(glVertexAttribIPointer)(5, 4, GL_UNSIGNED_SHORT, sizeof(GVert), (void *)offsetof(GVert, limits));
    GL(glVertexAttribIPointer)(6, 1, GL_UNSIGNED_INT, sizeof(GVert), (void *)offsetof(GVert, mode));
    GL(glVertexAttribPointer)(7, 3, GL_FLOAT, GL_FALSE, sizeof(GVert), (void *)offsetof(GVert, wgt));
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

