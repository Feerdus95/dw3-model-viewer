/* --- the model on the card --- */

struct GVert {
    float pos[3], nrm[3], uv[2];
    uint8_t color[4];
    uint16_t tex[4], limits[4];
    uint32_t mode;
};

static void upload_vram(Scene &s) {
    GL(glBindTexture)(GL_TEXTURE_2D, s.vram);
    GL(glPixelStorei)(GL_UNPACK_ALIGNMENT, 2);
    GL(glTexImage2D)(GL_TEXTURE_2D, 0, GL_R16UI, 1024, 512, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, s.model->vram.data());
    GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}


/* --- smooth skin (a prototype; DW3MV_SMOOTH=1) ---
   The game moves each bone's mesh as one rigid piece, so the pieces show where they meet. This gives every vertex up to 3 bone
   influences, by its distance to the bones' segments (a bone's joint to its children's, or to its mesh's far end), taken in the
   pose the model had when it was uploaded (its bind), and skins it by CPU each frame. The game's skeleton and motions are as they are. */
struct SmV {
    int b[3];
    float w[3], l[3][3]; /* the bone, its weight, and the vertex in that bone's bind frame */
    int bone;
    float nrm0[3];
};
struct SmoothData {
    std::vector<SmV> v;
    std::vector<GVert> out;
};
static std::map<const Scene *, SmoothData> g_sm;
static bool g_smooth = SDL_getenv("DW3MV_SMOOTH") != nullptr;

static void sm_tf(const float *w, const float *p, float *o) {
    for (int i = 0; i < 3; i++) o[i] = w[i] * p[0] + w[4 + i] * p[1] + w[8 + i] * p[2] + w[12 + i];
}
static void sm_inv(const float *w, float *o) {
    float m[3][3], c[3][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) m[i][j] = w[j * 4 + i];
    c[0][0] = m[1][1] * m[2][2] - m[1][2] * m[2][1], c[0][1] = m[1][2] * m[2][0] - m[1][0] * m[2][2], c[0][2] = m[1][0] * m[2][1] - m[1][1] * m[2][0];
    c[1][0] = m[0][2] * m[2][1] - m[0][1] * m[2][2], c[1][1] = m[0][0] * m[2][2] - m[0][2] * m[2][0], c[1][2] = m[0][1] * m[2][0] - m[0][0] * m[2][1];
    c[2][0] = m[0][1] * m[1][2] - m[0][2] * m[1][1], c[2][1] = m[0][2] * m[1][0] - m[0][0] * m[1][2], c[2][2] = m[0][0] * m[1][1] - m[0][1] * m[1][0];
    float det = m[0][0] * c[0][0] + m[0][1] * c[0][1] + m[0][2] * c[0][2];
    if (fabsf(det) < 1e-12f) det = 1e-12f;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) o[j * 4 + i] = c[j][i] / det;
    for (int i = 0; i < 3; i++) o[12 + i] = -(o[i] * w[12] + o[4 + i] * w[13] + o[8 + i] * w[14]);
    o[3] = o[7] = o[11] = 0, o[15] = 1;
}
static float sm_seg(const float *p, const float *a, const float *b) {
    float ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, ap[3] = {p[0] - a[0], p[1] - a[1], p[2] - a[2]};
    float l2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
    float t = l2 > 1e-6f ? (ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / l2 : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float d[3] = {ap[0] - ab[0] * t, ap[1] - ab[1] * t, ap[2] - ab[2] * t};
    return sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

static void smooth_build(const Scene &s, const std::vector<GVert> &v) {
    Model &m = *s.model;
    size_t nb = m.bones.size();
    std::vector<std::array<float, 16>> W0(nb), I0(nb);
    std::vector<std::array<float, 3>> A(nb), B(nb);
    std::vector<std::vector<int>> kids(nb);
    for (size_t b = 0; b < nb; b++) {
        memcpy(W0[b].data(), m.pose[b].world, 64), sm_inv(W0[b].data(), I0[b].data());
        A[b] = {W0[b][12], W0[b][13], W0[b][14]};
        if (b >= 1 && m.bones[b].parent >= 1) kids[m.bones[b].parent].push_back((int)b);
    }
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    std::vector<std::array<float, 3>> X(v.size());
    for (size_t b = 1; b < nb; b++)
        for (int k = s.first[b]; k < s.first[b] + s.count[b]; k++) {
            float p[3] = {v[k].pos[0], v[k].pos[1], v[k].pos[2]};
            sm_tf(W0[b].data(), p, X[k].data());
            for (int i = 0; i < 3; i++) lo[i] = std::min(lo[i], X[k][i]), hi[i] = std::max(hi[i], X[k][i]);
        }
    float D = sqrtf((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) + (hi[2] - lo[2]) * (hi[2] - lo[2]));
    const char *ee = SDL_getenv("DW3MV_SMOOTH_E"), *bl = SDL_getenv("DW3MV_SMOOTH_BONES"); /* the blend's width as a share of the model; the bones skinned (all if none) */
    float e2 = (ee ? (float)atof(ee) : 0.03f) * D, only[256] = {};
    e2 *= e2;
    for (const char *q = bl; q && *q;) {
        char *end;
        long bn = strtol(q, &end, 10);
        if (end == q) break;
        if (bn >= 0 && bn < 256) only[bn] = 1;
        q = *end ? end + 1 : end;
    }
    for (size_t b = 1; b < nb; b++) { /* each bone's segment: to its children's joints, or to its mesh's farthest point */
        if (!kids[b].empty()) {
            B[b] = {0, 0, 0};
            for (int c : kids[b])
                for (int i = 0; i < 3; i++) B[b][i] += A[c][i] / kids[b].size();
        } else {
            float best = -1;
            B[b] = A[b];
            for (int k = s.first[b]; k < s.first[b] + s.count[b]; k++) {
                float d = sm_seg(X[k].data(), A[b].data(), A[b].data());
                if (d > best) best = d, B[b] = X[k];
            }
        }
    }
    SmoothData &sd = g_sm[&s];
    sd.v.assign(v.size(), SmV{});
    sd.out = v;
    for (size_t b = 1; b < nb; b++)
        for (int k = s.first[b]; k < s.first[b] + s.count[b]; k++) {
            std::vector<int> cand = {(int)b};
            if (m.bones[b].parent >= 1) cand.push_back(m.bones[b].parent);
            for (int c : kids[b]) cand.push_back(c);
            std::vector<std::pair<float, int>> ws;
            if (bl && !only[b]) cand.resize(1); /* a bone not listed stays rigid */
            for (int j : cand) {
                float d = sm_seg(X[k].data(), A[j].data(), B[j].data());
                ws.emplace_back(1.0f / (d * d + e2), j);
            }
            std::sort(ws.begin(), ws.end(), [](auto &a, auto &c) { return a.first > c.first; });
            float sum = 0;
            int n = (int)std::min<size_t>(3, ws.size());
            for (int i = 0; i < n; i++) sum += ws[i].first;
            SmV &o = sd.v[k];
            o.bone = (int)b, o.nrm0[0] = v[k].nrm[0], o.nrm0[1] = v[k].nrm[1], o.nrm0[2] = v[k].nrm[2];
            for (int i = 0; i < 3; i++) {
                o.b[i] = i < n ? ws[i].second : (int)b;
                o.w[i] = i < n ? ws[i].first / sum : 0;
                sm_tf(I0[o.b[i]].data(), X[k].data(), o.l[i]);
            }
        }
}

static void smooth_update(Scene &s) {
    auto it = g_sm.find(&s);
    if (it == g_sm.end() || it->second.v.size() != it->second.out.size()) return;
    Model &m = *s.model;
    SmoothData &sd = it->second;
    for (size_t k = 0; k < sd.v.size(); k++) {
        const SmV &o = sd.v[k];
        if (!o.bone) continue;
        float acc[3] = {0, 0, 0};
        for (int i = 0; i < 3; i++) {
            if (o.w[i] <= 0) continue;
            float p[3];
            sm_tf(m.pose[o.b[i]].world, o.l[i], p);
            for (int c = 0; c < 3; c++) acc[c] += o.w[i] * p[c];
        }
        const float *w = m.pose[o.bone].world;
        for (int c = 0; c < 3; c++) {
            sd.out[k].pos[c] = acc[c];
            sd.out[k].nrm[c] = w[c] * o.nrm0[0] + w[4 + c] * o.nrm0[1] + w[8 + c] * o.nrm0[2];
        }
    }
    GL(glBindVertexArray)(s.vao);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, s.vbo);
    GL(glBufferData)(GL_ARRAY_BUFFER, sd.out.size() * sizeof(GVert), sd.out.data(), GL_STREAM_DRAW);
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
    if (g_smooth) smooth_build(s, v);
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

