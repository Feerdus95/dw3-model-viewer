/* ===== Flamedramon's hand adjustment: dw2-extractor/flame_adjust.txt, and a live preview of a change over it =====
   The file's lines (flame_adjust.py): "bone CLIP BONE RX RY RZ" turns a DW2 bone and what hangs from it in one attack, "fx ID X Y Z RX RY RZ SCALE"
   moves an effect in its hand's frame (456, 457) or the fighter's. What the file holds is baked into 7120-7127 by flame_dcb.py; the panel's sliders
   are a change over that, shown live (Model::adj, and flame_pre for the effects) and written back composed with it. */

struct FlameFx {
    float pos[3] = {0, 0, 0}, rot[3] = {0, 0, 0}, scale = 1; /* rot in degrees */
    bool zero() const { return !pos[0] && !pos[1] && !pos[2] && !rot[0] && !rot[1] && !rot[2] && scale == 1; }
};
struct FlameAdj {
    std::map<int, std::map<int, std::array<float, 3>>> bones; /* clip -> DW2 bone -> degrees */
    std::map<int, FlameFx> fx;
};
static FlameAdj g_flBase, g_flLive; /* what the file has (baked into the model shown), and the change over it */

static std::string flame_dir() { return g_exe_dir + "../../dw2-extractor/"; }
static int flame_motion(int clip) { return clip == 1 ? 51 : clip == 2 ? 63 : 64; } /* the viewer's motion numbers of the three attacks */
static int flame_clip(int motion) { return motion == 51 ? 1 : motion == 63 ? 2 : motion == 64 ? 3 : 0; }
static bool flame_model(const Model &m) { return m.name.compare(0, 14, "Flamedramon DC") == 0 && m.pose.size() >= 19; }

struct Mat3 {
    float m[3][3];
};
static Mat3 fl_rot(const float d[3]) { /* Rz*Ry*Rx, degrees */
    Mat3 r;
    const float k = 3.14159265f / 180;
    float cx = cosf(d[0] * k), sx = sinf(d[0] * k), cy = cosf(d[1] * k), sy = sinf(d[1] * k), cz = cosf(d[2] * k), sz = sinf(d[2] * k);
    float t[3][3] = {{cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx}, {sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx}, {-sy, cy * sx, cy * cx}};
    memcpy(r.m, t, sizeof t);
    return r;
}
static Mat3 fl_mul(const Mat3 &a, const Mat3 &b) {
    Mat3 r;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
    return r;
}
static void fl_euler(const Mat3 &r, float d[3]) {
    const float k = 180 / 3.14159265f;
    d[1] = asinf(std::max(-1.0f, std::min(1.0f, -r.m[2][0]))) * k;
    if (fabsf(r.m[2][0]) < 0.99999f) d[0] = atan2f(r.m[2][1], r.m[2][2]) * k, d[2] = atan2f(r.m[1][0], r.m[0][0]) * k;
    else d[0] = atan2f(-r.m[1][2], r.m[1][1]) * k, d[2] = 0;
}

static FlameAdj flame_read() {
    FlameAdj a;
    std::ifstream f(flame_dir() + "flame_adjust.txt");
    std::string line;
    while (std::getline(f, line)) {
        line = line.substr(0, line.find('#'));
        std::istringstream s(line);
        std::string w;
        if (!(s >> w)) continue;
        if (w == "bone") {
            int c, b;
            float x, y, z;
            if (s >> c >> b >> x >> y >> z) a.bones[c][b] = {x, y, z};
        } else if (w == "fx") {
            int id;
            FlameFx e;
            if (!(s >> id >> e.pos[0] >> e.pos[1] >> e.pos[2])) continue;
            s >> e.rot[0] >> e.rot[1] >> e.rot[2];
            float sc;
            if (s >> sc) e.scale = sc;
            a.fx[id] = e;
        }
    }
    return a;
}
/* the file as BASE then LIVE make it */
static bool flame_write(const FlameAdj &base, const FlameAdj &live) {
    std::ofstream f(flame_dir() + "flame_adjust.txt");
    if (!f) return false;
    f << "# Hand corrections to Flamedramon's attacks (flame_adjust.py); written by the viewer's Fighter panel. Regenerate with:  python flame_dcb.py\n";
    std::set<std::pair<int, int>> keys;
    for (auto &c : base.bones)
        for (auto &b : c.second) keys.insert({c.first, b.first});
    for (auto &c : live.bones)
        for (auto &b : c.second) keys.insert({c.first, b.first});
    for (auto [c, b] : keys) {
        std::array<float, 3> zb = {0, 0, 0}, zl = {0, 0, 0};
        if (base.bones.count(c) && base.bones.at(c).count(b)) zb = base.bones.at(c).at(b);
        if (live.bones.count(c) && live.bones.at(c).count(b)) zl = live.bones.at(c).at(b);
        float d[3];
        fl_euler(fl_mul(fl_rot(zl.data()), fl_rot(zb.data())), d);
        if (fabsf(d[0]) + fabsf(d[1]) + fabsf(d[2]) > 0.005f) f << "bone " << c << " " << b << " " << d[0] << " " << d[1] << " " << d[2] << "\n";
    }
    std::set<int> ids;
    for (auto &e : base.fx) ids.insert(e.first);
    for (auto &e : live.fx) ids.insert(e.first);
    for (int id : ids) {
        FlameFx b, l;
        if (base.fx.count(id)) b = base.fx.at(id);
        if (live.fx.count(id)) l = live.fx.at(id);
        Mat3 rl = fl_rot(l.rot), rb = fl_rot(b.rot);
        bool hand = id == 456 || id == 457;
        FlameFx t; /* the change's turn after the baked one: in the hand's frame it carries the baked move too */
        for (int i = 0; i < 3; i++) t.pos[i] = l.pos[i] + (hand ? rl.m[i][0] * b.pos[0] + rl.m[i][1] * b.pos[1] + rl.m[i][2] * b.pos[2] : b.pos[i]);
        fl_euler(fl_mul(rl, rb), t.rot);
        t.scale = b.scale * l.scale;
        if (!t.zero()) f << "fx " << id << " " << t.pos[0] << " " << t.pos[1] << " " << t.pos[2] << " " << t.rot[0] << " " << t.rot[1] << " " << t.rot[2] << " " << t.scale << "\n";
    }
    return (bool)f;
}
/* the live turns into the model's: DW2 bone N is the viewer's N+1 */
static void flame_sync(Model &m) {
    m.adj.clear();
    for (auto &c : g_flLive.bones)
        for (auto &b : c.second)
            if (b.second[0] || b.second[1] || b.second[2]) m.adj[flame_motion(c.first)][b.first < 0 ? 0 : b.first + 1] = b.second;
    m.evaluate();
}

/* an affine matrix's inverse (column-major; the 3x3 may carry a scale) */
static M4 fl_inv(const M4 &a) {
    const float *m = a.m;
    float c[3][3] = {{m[0], m[4], m[8]}, {m[1], m[5], m[9]}, {m[2], m[6], m[10]}}; /* c[row][col] */
    float det = c[0][0] * (c[1][1] * c[2][2] - c[1][2] * c[2][1]) - c[0][1] * (c[1][0] * c[2][2] - c[1][2] * c[2][0]) + c[0][2] * (c[1][0] * c[2][1] - c[1][1] * c[2][0]);
    float id = det ? 1 / det : 0, v[3][3];
    v[0][0] = (c[1][1] * c[2][2] - c[1][2] * c[2][1]) * id, v[0][1] = (c[0][2] * c[2][1] - c[0][1] * c[2][2]) * id, v[0][2] = (c[0][1] * c[1][2] - c[0][2] * c[1][1]) * id;
    v[1][0] = (c[1][2] * c[2][0] - c[1][0] * c[2][2]) * id, v[1][1] = (c[0][0] * c[2][2] - c[0][2] * c[2][0]) * id, v[1][2] = (c[0][2] * c[1][0] - c[0][0] * c[1][2]) * id;
    v[2][0] = (c[1][0] * c[2][1] - c[1][1] * c[2][0]) * id, v[2][1] = (c[0][1] * c[2][0] - c[0][0] * c[2][1]) * id, v[2][2] = (c[0][0] * c[1][1] - c[0][1] * c[1][0]) * id;
    M4 r = {{v[0][0], v[1][0], v[2][0], 0, v[0][1], v[1][1], v[2][1], 0, v[0][2], v[1][2], v[2][2], 0, 0, 0, 0, 1}};
    for (int i = 0; i < 3; i++) r.m[12 + i] = -(v[i][0] * m[12] + v[i][1] * m[13] + v[i][2] * m[14]);
    return r;
}
static M4 fl_translate(const float p[3]) { return {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, p[0], p[1], p[2], 1}}; }

/* Where effect ID's model goes in the view with the live change: PRE, the placement the script gave it, moved by the change. The hand's two (456, 457)
   ride the hand as it is turned now over as the file bakes it (H' T H^-1, T the sliders' move and turn in the hand's frame); the rest move in the fighter's
   space; each is sized about its own centre. FXM: the effect's model, whose first bone's world is where it stands. */
static M4 flame_pre(const Model &fighter, int id, const Model &fxm, const M4 &pre) {
    if (id < 452 || id > 458 || !flame_model(fighter) || fxm.pose.size() < 2) return pre;
    auto it = g_flLive.fx.find(id);
    if (it == g_flLive.fx.end() || it->second.zero()) return pre;
    const FlameFx &f = it->second;
    M4 c = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    if (id == 456 || id == 457) {
        Mat3 r = fl_rot(f.rot);
        M4 t = {{r.m[0][0], r.m[1][0], r.m[2][0], 0, r.m[0][1], r.m[1][1], r.m[2][1], 0, r.m[0][2], r.m[1][2], r.m[2][2], 0, f.pos[0], f.pos[1], f.pos[2], 1}};
        M4 now;
        memcpy(now.m, fighter.pose[17].world, sizeof now.m);
        M4 was = now;
        if (fighter.rawWorld.size() > 17) memcpy(was.m, fighter.rawWorld[17].data(), sizeof was.m);
        c = mul(now, mul(t, fl_inv(was)));
    } else
        c = fl_translate(f.pos);
    M4 w = mul(c, pre);
    const float *o = fxm.pose[1].world; /* the effect's centre, in the view */
    float p[3];
    for (int i = 0; i < 3; i++) p[i] = w.m[i] * o[12] + w.m[4 + i] * o[13] + w.m[8 + i] * o[14] + w.m[12 + i];
    float q[3] = {-p[0], -p[1], -p[2]};
    M4 s = {{f.scale, 0, 0, 0, 0, f.scale, 0, 0, 0, 0, f.scale, 0, 0, 0, 0, 1}};
    return mul(fl_translate(p), mul(s, mul(fl_translate(q), w)));
}

/* ===== The aim guide: a dotted ray, like the Laser Gun's, from Flamedramon's fist and from the ball to what each hits =====
   The stand-in enemy's joints, padded, make its box; a ray that misses runs on dimmed. What it measures goes to g_guideInfo (the panel shows it). */
static std::string g_guideInfo;
static int g_guideAxis = 0; /* 0 the wrist to the hand's middle, 1 shoulder to wrist, 2.. the hand bone's +X -X +Y -Y +Z -Z */

static void guide_dashed(std::vector<float> &out, const float a[3], const float d[3], float len, float r, float g, float b) {
    for (float t = 0; t < len; t += 150) {
        float p[3], q[3], e = std::min(t + 60, len);
        for (int i = 0; i < 3; i++) p[i] = a[i] + d[i] * t, q[i] = a[i] + d[i] * e;
        line(out, p, q, r, g, b);
    }
}
static void guide_cross(std::vector<float> &out, const float p[3], float k, float r, float g, float b) {
    for (int a = 0; a < 3; a++) {
        float lo[3] = {p[0], p[1], p[2]}, hi[3] = {p[0], p[1], p[2]};
        lo[a] -= k, hi[a] += k;
        line(out, lo, hi, r, g, b);
    }
}
/* one ray from O along D (unit, in the view): to the box's entry point, or dimmed on past it; what it finds goes to g_guideInfo */
static void guide_ray(std::vector<float> &out, const float o[3], const float d[3], const float lo[3], const float hi[3], bool box, const char *name, const float col[3]) {
    float t0 = 0, t1 = 1e9f;
    bool hit = box;
    for (int i = 0; i < 3 && hit; i++) {
        if (fabsf(d[i]) < 1e-6f) {
            if (o[i] < lo[i] || o[i] > hi[i]) hit = false;
            continue;
        }
        float a = (lo[i] - o[i]) / d[i], b = (hi[i] - o[i]) / d[i];
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a), t1 = std::min(t1, b);
        if (t0 > t1) hit = false;
    }
    float len = hit ? t0 : 14000.0f, k = hit ? 1.0f : 0.8f, c[3] = {col[0] * k, col[1] * k, col[2] * k};
    guide_dashed(out, o, d, len, c[0], c[1], c[2]);
    float p[3] = {o[0] + d[0] * len, o[1] + d[1] * len, o[2] + d[2] * len};
    guide_cross(out, p, hit ? 160.0f : 90.0f, c[0], c[1], c[2]);
    char b[160];
    if (box) { /* how far the ray passes from the enemy's middle */
        float m[3] = {(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2}, v[3], s = 0, off = 0;
        for (int i = 0; i < 3; i++) v[i] = m[i] - o[i], s += v[i] * d[i];
        for (int i = 0; i < 3; i++) off += (v[i] - d[i] * s) * (v[i] - d[i] * s);
        if (hit) snprintf(b, sizeof b, "%s: hits the enemy at %.0f, %.0f off its middle\n", name, t0, sqrtf(off));
        else snprintf(b, sizeof b, "%s: misses the enemy, %.0f off its middle at the closest\n", name, sqrtf(off));
    } else
        snprintf(b, sizeof b, "%s: no enemy to hit (View > Stand-in enemy)\n", name);
    g_guideInfo += b;
}
static void guide_lines(const Model &fighter, const std::vector<ExtraModel> &models, std::vector<float> &out) {
    g_guideInfo.clear();
    float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
    bool box = false;
    for (const ExtraModel &e : models) {
        if (!e.scene || e.scene != g_dummy.get() || !e.scene->model) continue;
        const Model &m = *e.scene->model;
        for (size_t b = 1; b < m.pose.size(); b++) {
            M4 w;
            memcpy(w.m, m.pose[b].world, sizeof w.m);
            w = mul(e.pre, w);
            float v[3] = {w.m[12], -w.m[13], -w.m[14]};
            for (int i = 0; i < 3; i++) lo[i] = std::min(lo[i], v[i] - 350), hi[i] = std::max(hi[i], v[i] + 350);
        }
        box = true;
    }
    static const float RED[3] = {1.0f, 0.15f, 0.1f}, GOLD[3] = {1.0f, 0.8f, 0.1f};
    if (flame_model(fighter) && fighter.bones.size() > 17 && fighter.bones[17].mesh >= 0 && !fighter.meshes[fighter.bones[17].mesh].verts.empty()) {
        const auto &vs = fighter.meshes[fighter.bones[17].mesh].verts;
        double mx = 0, my = 0, mz = 0;
        for (const Vec3s &v : vs) mx += v.x, my += v.y, mz += v.z;
        float c[3] = {(float)(mx / vs.size()), (float)(my / vs.size()), (float)(mz / vs.size())}, o[3], wr[3], sh[3], d[3] = {0, 0, 0};
        const float *w = fighter.pose[17].world;
        for (int i = 0; i < 3; i++) o[i] = w[i] * c[0] + w[4 + i] * c[1] + w[8 + i] * c[2] + w[12 + i], wr[i] = w[12 + i], sh[i] = fighter.pose[16].world[12 + i];
        if (g_guideAxis == 0) for (int i = 0; i < 3; i++) d[i] = o[i] - wr[i];
        else if (g_guideAxis == 1) for (int i = 0; i < 3; i++) d[i] = wr[i] - sh[i];
        else for (int i = 0; i < 3; i++) d[i] = w[4 * ((g_guideAxis - 2) / 2) + i] * ((g_guideAxis & 1) ? -1.0f : 1.0f);
        float n = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (n > 1e-3f) {
            float ov[3] = {o[0], -o[1], -o[2]}, dv[3] = {d[0] / n, -d[1] / n, -d[2] / n};
            guide_ray(out, ov, dv, lo, hi, box, "Hand", RED);
        }
    }
    for (const ExtraModel &e : models) {
        if (e.id != 458 || !e.scene || !e.scene->model || e.scene->model->pose.size() < 2) continue;
        M4 w;
        memcpy(w.m, e.scene->model->pose[1].world, sizeof w.m);
        w = mul(e.pre, w);
        float ov[3] = {w.m[12], -w.m[13], -w.m[14]}, dv[3] = {w.m[8], -w.m[9], -w.m[10]};
        float n = sqrtf(dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2]);
        if (n < 1e-3f) continue;
        for (float &x : dv) x /= n;
        if (box) { /* the ball's z runs one way or the other in its model: it goes the way the enemy is */
            float s = 0;
            for (int i = 0; i < 3; i++) s += dv[i] * ((lo[i] + hi[i]) / 2 - ov[i]);
            if (s < 0) for (float &x : dv) x = -x;
        }
        guide_ray(out, ov, dv, lo, hi, box, "Ball", GOLD);
    }
}
