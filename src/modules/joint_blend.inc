/* --- joints blended (experimental, opt in) ---
   Two bones' meshes that meet at rest (their vertices on each other's) can follow both bones near the joint, as a
   skinned model does: a vertex of A is drawn as (1 - w) * (A's matrix * v) + w * (B's matrix * B's rest inverse * A's
   rest * v), w 0.5 where the pieces meet and fading to 0 over a band around it. Both pieces' vertices that meet get
   the same w and the same two matrices, so they stay on one another in every pose. The model's data isn't changed. */

struct JointBlend {
    int a = 0, b = 0;           /* the two bones */
    std::vector<float> wa, wb;  /* each one's mesh vertices: how much each follows the other bone */
    float ab[16], ba[16];       /* A's rest space to B's (R_b^-1 R_a), and the other way */
    int contact = 0;            /* A's vertices that are on one of B's at rest */
};

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

/* the pairs a model gets: the ones asked for, and Guilmon's wrist (bones 3 and 4) */
static void blend_build(const Model &m, std::vector<JointBlend> &out) {
    std::vector<std::pair<int, int>> pairs = g_blendExtra;
    if (m.name == "M366GIRU.BIN") pairs.push_back({3, 4});
    for (const auto &pr : pairs) {
        int a = pr.first, b = pr.second;
        if (a < 1 || b < 1 || a >= (int)m.bones.size() || b >= (int)m.bones.size() || a == b) continue;
        if (m.bones[a].mesh < 0 || m.bones[b].mesh < 0) continue;
        float Ra[16], Rb[16], Rai[16], Rbi[16];
        bind_world(m, a, Ra), bind_world(m, b, Rb);
        if (!blend_inverse(Ra, Rai) || !blend_inverse(Rb, Rbi)) continue;
        const Mesh &ma = m.meshes[m.bones[a].mesh], &mb = m.meshes[m.bones[b].mesh];
        std::vector<std::array<float, 3>> pa(ma.verts.size()), pb(mb.verts.size());
        for (size_t i = 0; i < ma.verts.size(); i++) blend_point(Ra, ma.verts[i], pa[i].data());
        for (size_t i = 0; i < mb.verts.size(); i++) blend_point(Rb, mb.verts[i], pb[i].data());
        auto dist = [](const std::array<float, 3> &p, const std::array<float, 3> &q) {
            return sqrtf((p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]));
        };
        const float EPS = 1.5f; /* the pieces' vertices that meet are the same integers: the game's units */
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
