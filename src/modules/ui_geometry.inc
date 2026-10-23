/* --- the view's undo list and its geometry: where bones are on screen, and which is under a point --- */

/* A list of states to go back to (undo) and forth (redo) */
template <class T> struct History {
    std::vector<T> undo, redo;
    void push(T t) {
        undo.push_back(std::move(t));
        if (undo.size() > 40) undo.erase(undo.begin());
        redo.clear();
    }
    void clear() { undo.clear(), redo.clear(); }
};

static M4 view_proj(const View &v, int w, int h) {
    return mul(perspective(v.fov, (float)w / std::max(h, 1), v.cam.dist * 0.01f, v.cam.dist * 50), v.cam.view());
}

/* Where the mouse (MX, MY on the W x H view) meets the ground, y 0; false in the sky */
static bool ground_hit(const View &v, float w, float h, float mx, float my, float out[3]) {
    const Camera &c = v.cam;
    float e[3] = {c.target[0] + c.dist * cosf(c.pitch) * sinf(c.yaw), c.target[1] + c.dist * sinf(c.pitch), c.target[2] + c.dist * cosf(c.pitch) * cosf(c.yaw)};
    float f[3] = {c.target[0] - e[0], c.target[1] - e[1], c.target[2] - e[2]};
    float fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (float &x : f) x /= fl;
    float s[3] = {-f[2], 0, f[0]}, sl = sqrtf(s[0] * s[0] + s[2] * s[2]);
    for (float &x : s) x /= sl;
    float u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};
    float k = tanf(v.fov / 2), nx = (mx / w * 2 - 1) * k * (w / h), ny = (1 - my / h * 2) * k, d[3];
    for (int i = 0; i < 3; i++) d[i] = f[i] + s[i] * nx + u[i] * ny;
    if (d[1] > -1e-4f) return false;
    float t = -e[1] / d[1];
    if (t <= 0) return false;
    for (int i = 0; i < 3; i++) out[i] = e[i] + d[i] * t;
    return true;
}

/* A point of the world on the view (W x H, from its top left); false behind the camera */
static bool to_screen(const M4 &vp, const float p[3], float w, float h, ImVec2 &out) {
    const float *m = vp.m;
    float cw = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
    if (cw < 1e-3f) return false;
    float x = (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]) / cw, y = (m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]) / cw;
    out = ImVec2((x + 1) / 2 * w, (1 - y) / 2 * h);
    return true;
}

/* --- the stage editor's gizmo: an arrow a axis (x red, y green and up, z blue), a ring on the ground
   to turn and a box to scale; all on the view (from its top left), a constant size on screen --- */

struct Gizmo {
    bool on = false;
    float o[3] = {}, len = 0; /* its centre in the world, and an arrow's length there */
    float up = -1;            /* the y arrow's direction: -1 in a stage (y down), 1 in a model's view */
    bool rings = true;
    ImVec2 c, tip[3], ring[48];
};
static const float GZ_DIR[3][3] = {{1, 0, 0}, {0, -1, 0}, {0, 0, 1}}; /* the game's y goes down: the arrow up is -y */

static bool gizmo_make(Gizmo &g, const M4 &vp, const float o[3], float w, float h, float fov, float dpi) {
    const float *m = vp.m;
    float depth = m[3] * o[0] + m[7] * o[1] + m[11] * o[2] + m[15];
    g.on = false;
    if (depth < 1e-3f || !to_screen(vp, o, w, h, g.c)) return false;
    memcpy(g.o, o, sizeof g.o);
    g.len = 90 * dpi * 2 * depth * tanf(fov / 2) / std::max(h, 1.0f);
    for (int a = 0; a < 3; a++) {
        float p[3] = {o[0] + GZ_DIR[a][0] * g.len, o[1] + (a == 1 ? g.up : GZ_DIR[a][1]) * g.len, o[2] + GZ_DIR[a][2] * g.len};
        if (!to_screen(vp, p, w, h, g.tip[a])) return false;
    }
    for (int i = 0; i < 48; i++) {
        float t = i * 6.2831853f / 48, r = g.len * 0.7f, p[3] = {o[0] + r * cosf(t), o[1], o[2] + r * sinf(t)};
        if (!to_screen(vp, p, w, h, g.ring[i])) return false;
    }
    return g.on = true;
}

static float seg_dist(ImVec2 p, ImVec2 a, ImVec2 b) {
    ImVec2 ab = b - a;
    float l = ab.x * ab.x + ab.y * ab.y, t = l > 0 ? std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / l, 0.0f, 1.0f) : 0;
    float dx = p.x - (a.x + ab.x * t), dy = p.y - (a.y + ab.y * t);
    return sqrtf(dx * dx + dy * dy);
}

/* What is under P: 0-2 an arrow, 3 the ring, 4 the box; -1 nothing */
static int gizmo_hit(const Gizmo &g, ImVec2 p, float dpi) {
    if (!g.on) return -1;
    if (seg_dist(p, g.c, g.c) < 8 * dpi) return 4;
    float best = 7 * dpi;
    int hit = -1;
    for (int a = 0; a < 3; a++) {
        float d = seg_dist(p, g.c, g.tip[a]);
        if (d < best) best = d, hit = a;
    }
    if (hit >= 0 || !g.rings) return hit;
    for (int i = 0; i < 48; i++) {
        float d = seg_dist(p, g.ring[i], g.ring[(i + 1) % 48]);
        if (d < best) best = d, hit = 3;
    }
    return hit;
}

static void gizmo_draw(ImDrawList *dl, const Gizmo &g, ImVec2 at, int hot, float dpi) {
    static const ImU32 COL[3] = {IM_COL32(236, 84, 84, 255), IM_COL32(118, 208, 96, 255), IM_COL32(84, 142, 245, 255)};
    static const char *NAME[3] = {"X", "Y", "Z"};
    const ImU32 yel = IM_COL32(255, 217, 51, 255), shade = IM_COL32(0, 0, 0, 120);
    for (int i = 0; g.rings && i < 48; i++) {
        ImVec2 a = at + g.ring[i], b = at + g.ring[(i + 1) % 48];
        dl->AddLine(a, b, shade, 4.5f * dpi);
        dl->AddLine(a, b, hot == 3 ? yel : IM_COL32(255, 170, 60, 235), (hot == 3 ? 3.0f : 2.0f) * dpi);
    }
    for (int a = 0; a < 3; a++) {
        ImU32 c = hot == a ? yel : COL[a];
        ImVec2 s = at + g.c, t = at + g.tip[a], d = t - s;
        float l = sqrtf(d.x * d.x + d.y * d.y);
        if (l < 1) continue;
        d = d * (1 / l);
        ImVec2 n(-d.y, d.x);
        dl->AddLine(s, t, shade, 5 * dpi);
        dl->AddLine(s, t, c, 2.5f * dpi);
        dl->AddTriangleFilled(t + d * (11 * dpi), t - d * (2 * dpi) + n * (5.5f * dpi), t - d * (2 * dpi) - n * (5.5f * dpi), c);
        ImVec2 ts = ImGui::CalcTextSize(NAME[a]);
        ImVec2 lp = t + d * (22 * dpi) - ts * 0.5f;
        dl->AddText(lp + ImVec2(1, 1), IM_COL32(0, 0, 0, 200), NAME[a]);
        dl->AddText(lp, c, NAME[a]);
    }
    ImVec2 c = at + g.c;
    dl->AddRectFilled(c - ImVec2(5 * dpi, 5 * dpi), c + ImVec2(5 * dpi, 5 * dpi), hot == 4 ? yel : IM_COL32(236, 238, 242, 255));
    dl->AddRect(c - ImVec2(5 * dpi, 5 * dpi), c + ImVec2(5 * dpi, 5 * dpi), IM_COL32(0, 0, 0, 200));
}

/* The box (in the view's world) around BONES' vertices; false when none shows */
static bool bones_box(const Scene &s, const std::vector<int> &bones, float lo[3], float hi[3]) {
    if (!s.model) return false;
    const Model &m = *s.model;
    bool any = false;
    for (int i = 0; i < 3; i++) lo[i] = 1e30f, hi[i] = -1e30f;
    for (int b : bones) {
        if (b < 1 || b >= (int)m.bones.size() || !m.pose[b].visible) continue;
        M4 w = bone_world(s, b);
        for (const Vec3s &v : m.meshes[m.bones[b].mesh].verts) {
            for (int i = 0; i < 3; i++) {
                float c = w.m[i] * v.x + w.m[4 + i] * v.y + w.m[8 + i] * v.z + w.m[12 + i];
                lo[i] = std::min(lo[i], c), hi[i] = std::max(hi[i], c);
            }
            any = true;
        }
    }
    return any;
}

/* The camera on BONES, as F does the whole model */
static bool frame_bones(const Scene &s, View &v, const std::vector<int> &bones) {
    float lo[3], hi[3];
    if (!bones_box(s, bones, lo, hi)) return false;
    float r = 0;
    for (int i = 0; i < 3; i++) v.cam.target[i] = (lo[i] + hi[i]) / 2, r = std::max(r, hi[i] - lo[i]);
    v.cam.dist = std::max(r / 2, 50.0f) * 3.4f;
    v.gameCamera = -1;
    return true;
}

/* The bone whose polygon is nearest the camera at (PX, PY) of the view
   (W x H, from its top left); -1 none */
static int pick_bone(const Scene &s, const View &v, float w, float h, float px, float py) {
    if (!s.model) return -1;
    const Model &m = *s.model;
    M4 vp = view_proj(v, (int)w, (int)h);
    float best = 1e30f;
    int hit = -1;
    std::vector<float> sx, sy, sz;
    std::vector<char> ok;
    for (size_t b = 1; b < m.bones.size(); b++) {
        if (!m.pose[b].visible || s.hidden[b] || !s.count[b]) continue;
        M4 mvp = mul(vp, bone_world(s, (int)b));
        const Mesh &me = m.meshes[m.bones[b].mesh];
        size_t n = me.verts.size();
        sx.resize(n), sy.resize(n), sz.resize(n), ok.resize(n);
        for (size_t i = 0; i < n; i++) {
            const float *q = mvp.m;
            float x = me.verts[i].x, y = me.verts[i].y, z = me.verts[i].z;
            float cw = q[3] * x + q[7] * y + q[11] * z + q[15];
            ok[i] = cw > 1e-3f;
            if (!ok[i]) continue;
            sx[i] = ((q[0] * x + q[4] * y + q[8] * z + q[12]) / cw + 1) / 2 * w;
            sy[i] = (1 - (q[1] * x + q[5] * y + q[9] * z + q[13]) / cw) / 2 * h;
            sz[i] = (q[2] * x + q[6] * y + q[10] * z + q[14]) / cw;
        }
        auto tri = [&](int a, int c, int d) {
            if (!ok[a] || !ok[c] || !ok[d]) return;
            float den = (sy[c] - sy[d]) * (sx[a] - sx[d]) + (sx[d] - sx[c]) * (sy[a] - sy[d]);
            if (fabsf(den) < 1e-6f) return;
            float l1 = ((sy[c] - sy[d]) * (px - sx[d]) + (sx[d] - sx[c]) * (py - sy[d])) / den;
            float l2 = ((sy[d] - sy[a]) * (px - sx[d]) + (sx[a] - sx[d]) * (py - sy[d])) / den, l3 = 1 - l1 - l2;
            if (l1 < 0 || l2 < 0 || l3 < 0) return;
            float z = l1 * sz[a] + l2 * sz[c] + l3 * sz[d];
            if (z > -1 && z < best) best = z, hit = (int)b;
        };
        for (const Poly &p : me.polys) {
            tri(p.idx[0], p.idx[1], p.idx[2]);
            if (p.quad) tri(p.idx[1], p.idx[3], p.idx[2]);
        }
    }
    return hit;
}

/* A stage's part as an edit in progress would place it, from where it was
   built (START: move, turn, scale): the move from its old middle to its new
   one, turned and scaled about it, as StageEdit::placed does */
static void part_preview(const StagePart &p, const float start[5], float out[16]) {
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const auto &v : p.verts)
        for (int i = 0; i < 3; i++) lo[i] = std::min(lo[i], v[i]), hi[i] = std::max(hi[i], v[i]);
    float o[3], n[3];
    for (int i = 0; i < 3; i++) {
        float c = p.verts.empty() ? 0 : (lo[i] + hi[i]) / 2;
        o[i] = c + start[i], n[i] = c + p.move[i];
    }
    float k = start[4] != 0 ? p.scale / start[4] : 1, t = (p.turn - start[3]) * 0.017453293f;
    float cs = cosf(t) * k, sn = sinf(t) * k;
    float m[16] = {cs, 0, -sn, 0, 0, k, 0, 0, sn, 0, cs, 0,
                   n[0] - (cs * o[0] + sn * o[2]), n[1] - k * o[1], n[2] - (-sn * o[0] + cs * o[2]), 1};
    memcpy(out, m, sizeof m);
}
