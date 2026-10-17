/* --- joints blended (experimental, opt in) ---
   Two bones' meshes that meet at rest (their vertices on each other's) can follow both bones near the joint, as a
   skinned model does: a vertex of A is drawn as (1 - w) * (A's matrix * v) + w * (B's matrix * B's rest inverse * A's
   rest * v), w 0.5 where the pieces meet and fading to 0 over a band around it. Both pieces' vertices that meet get
   the same w and the same two matrices, so they stay on one another in every pose. The model's data isn't changed.

   Three steps, kept apart: DETECTION (blend_meets: how many of A's vertices are on B's at rest), ELIGIBILITY (3 or
   more), and ACTIVATION (blend_build: only the pairs asked for, never every joint found). "Rest" is the model's idle
   pose (its first motion's first keyframe), where the pieces are modelled to fit. */

struct JointBlend {
    int a = 0, b = 0;           /* the two bones */
    std::vector<float> wa, wb;  /* each one's mesh vertices: how much each follows the other bone */
    float ab[16], ba[16];       /* A's rest space to B's (R_b^-1 R_a), and the other way */
    int contact = 0;            /* A's vertices that are on one of B's at rest */
};

static float g_blendEps = 0;                          /* how near two vertices are to be "the same point" at rest; 0: 1% of the model's size */
static float g_blendBand = 60;                       /* game units over which the blending fades out */
static std::vector<std::pair<int, int>> g_blendExtra; /* pairs asked for on the command line, on any model */

/* the inverse of an affine matrix (column-major) */
static bool blend_inverse(const float m[16], float out[16]) {
    const float a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
    float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (fabsf(det) < 1e-9f) return false;
    float k = 1 / det;
    float r[9] = {(e * i - f * h) * k, (c * h - b * i) * k, (b * f - c * e) * k, (f * g - d * i) * k, (a * i - c * g) * k, (c * d - a * f) * k,
                  (d * h - e * g) * k, (b * g - a * h) * k, (a * e - b * d) * k};
    memset(out, 0, 16 * sizeof(float));
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 3; col++) out[col * 4 + row] = r[row * 3 + col];
    for (int row = 0; row < 3; row++) out[12 + row] = -(out[row] * m[12] + out[4 + row] * m[13] + out[8 + row] * m[14]);
    out[15] = 1;
    return true;
}

static void blend_point(const float *w, const Vec3s &v, float out[3]) {
    for (int i = 0; i < 3; i++) out[i] = w[i] * v.x + w[4 + i] * v.y + w[8 + i] * v.z + w[12 + i];
}

/* the model at rest, two ways: the bind pose (each bone's keys' first frame, as the OBJ has it), where pieces that were
   modelled to meet do so exactly, and its idle pose (its first motion's first keyframe), where they meet near enough */
struct BlendRest {
    std::vector<std::array<float, 16>> bind, idle;
    float epsBind = 1.5f, epsIdle = 1.5f; /* the nearness that counts as the same point in each */
};

static BlendRest blend_rest(const Model &m) {
    BlendRest r;
    Model pose = m; /* the idle pose, without moving the model on show */
    if (!pose.motions.empty()) pose.setMotion(1);
    r.bind.resize(m.bones.size()), r.idle.resize(m.bones.size());
    float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
    for (size_t b = 0; b < m.bones.size(); b++) {
        bind_world(m, (int)b, r.bind[b].data());
        if (pose.motions.empty()) r.idle[b] = r.bind[b];
        else memcpy(r.idle[b].data(), pose.pose[b].world, 16 * sizeof(float));
        if (b == 0 || m.bones[b].mesh < 0) continue;
        for (const Vec3s &v : m.meshes[m.bones[b].mesh].verts) {
            float p[3];
            blend_point(r.idle[b].data(), v, p);
            for (int i = 0; i < 3; i++) lo[i] = std::min(lo[i], p[i]), hi[i] = std::max(hi[i], p[i]);
        }
    }
    float d = sqrtf((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) + (hi[2] - lo[2]) * (hi[2] - lo[2]));
    r.epsIdle = g_blendEps > 0 ? g_blendEps : std::max(1.5f, d * 0.01f);
    r.epsBind = g_blendEps > 0 ? g_blendEps : 1.5f;
    return r;
}

/* how many of A's vertices are on one of B's with the bones at WORLD, EPS apart at most */
static int blend_count(const Model &m, const std::vector<std::array<float, 16>> &world, float eps, int a, int b) {
    if (a < 1 || b < 1 || a >= (int)m.bones.size() || b >= (int)m.bones.size() || m.bones[a].mesh < 0 || m.bones[b].mesh < 0) return 0;
    const Mesh &ma = m.meshes[m.bones[a].mesh], &mb = m.meshes[m.bones[b].mesh];
    std::vector<std::array<float, 3>> pb(mb.verts.size());
    for (size_t i = 0; i < mb.verts.size(); i++) blend_point(world[b].data(), mb.verts[i], pb[i].data());
    int n = 0;
    for (const Vec3s &va : ma.verts) {
        float p[3];
        blend_point(world[a].data(), va, p);
        for (const auto &q : pb)
            if (sqrtf((p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2])) <= eps) {
                n++;
                break;
            }
    }
    return n;
}

/* the rest a pair meets in: the bind pose if its vertices are on one another there, else the idle pose */
static const std::vector<std::array<float, 16>> &blend_pick(const Model &m, const BlendRest &r, int a, int b, float &eps, int &count) {
    count = blend_count(m, r.bind, r.epsBind, a, b);
    if (count >= 3) return eps = r.epsBind, r.bind;
    count = blend_count(m, r.idle, r.epsIdle, a, b);
    return eps = r.epsIdle, r.idle;
}

/* DETECTION: how many of A's vertices are on one of B's at rest; a pair needs 3 to be blended */
static int blend_meets(const Model &m, const BlendRest &r, int a, int b) {
    float eps;
    int n;
    blend_pick(m, r, a, b, eps, n);
    return n;
}

/* the pairs a model gets: the ones asked for, and Guilmon's wrist (bones 3 and 4) */
static void blend_build(const Model &m, std::vector<JointBlend> &out) {
    std::vector<std::pair<int, int>> pairs = g_blendExtra;
    if (m.name == "M366GIRU.BIN") pairs.push_back({3, 4});
    if (pairs.empty()) return;
    BlendRest rest = blend_rest(m);
    for (const auto &pr : pairs) {
        int a = pr.first, b = pr.second;
        if (a < 1 || b < 1 || a >= (int)m.bones.size() || b >= (int)m.bones.size() || a == b) continue;
        if (m.bones[a].mesh < 0 || m.bones[b].mesh < 0) continue;
        float EPS;
        int meets;
        const std::vector<std::array<float, 16>> &world = blend_pick(m, rest, a, b, EPS, meets);
        const float *Ra = world[a].data(), *Rb = world[b].data();
        float Rai[16], Rbi[16];
        if (!blend_inverse(Ra, Rai) || !blend_inverse(Rb, Rbi)) continue;
        const Mesh &ma = m.meshes[m.bones[a].mesh], &mb = m.meshes[m.bones[b].mesh];
        std::vector<std::array<float, 3>> pa(ma.verts.size()), pb(mb.verts.size());
        for (size_t i = 0; i < ma.verts.size(); i++) blend_point(Ra, ma.verts[i], pa[i].data());
        for (size_t i = 0; i < mb.verts.size(); i++) blend_point(Rb, mb.verts[i], pb[i].data());
        auto dist = [](const std::array<float, 3> &p, const std::array<float, 3> &q) {
            return sqrtf((p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]));
        };
        std::vector<std::array<float, 3>> meet; /* where the pieces meet */
        JointBlend j;
        j.a = a, j.b = b;
        for (size_t i = 0; i < pa.size(); i++)
            for (size_t k = 0; k < pb.size(); k++)
                if (dist(pa[i], pb[k]) <= EPS) {
                    meet.push_back(pa[i]), j.contact++;
                    break;
                }
        if (meet.size() < 3) continue;
        /* a vertex's w: 0.5 on the meeting points, a smooth fall to 0 at the band's end */
        auto weights = [&](const std::vector<std::array<float, 3>> &p, std::vector<float> &w) {
            w.assign(p.size(), 0);
            for (size_t i = 0; i < p.size(); i++) {
                float d = 1e30f;
                for (const auto &q : meet) d = std::min(d, dist(p[i], q));
                if (d <= EPS) {
                    w[i] = 0.5f;
                } else {
                    float s = std::min(d / std::max(g_blendBand, 1.0f), 1.0f);
                    w[i] = 0.5f * (1 - s * s * (3 - 2 * s));
                }
            }
        };
        weights(pa, j.wa), weights(pb, j.wb);
        M4 ab = mul(from(Rbi), from(Ra)), ba = mul(from(Rai), from(Rb));
        memcpy(j.ab, ab.m, sizeof j.ab), memcpy(j.ba, ba.m, sizeof j.ba);
        out.push_back(std::move(j));
    }
}
