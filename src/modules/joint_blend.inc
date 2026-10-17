/* --- joints blended (experimental, opt in) ---
   Two bones' meshes that meet at rest (their vertices on each other's) can follow both bones near the joint, as a
   skinned model does: a vertex is drawn as (1 - sum of w) * (its bone's matrix * v) + sum of w_k * (partner k's matrix *
   partner k's rest inverse * its bone's rest * v), a pair's w 0.5 where the pieces meet and fading to 0 over a band
   around it. Both pieces' vertices that meet get the same w and the same two matrices, so they stay on one another in
   every pose. The model's data isn't changed.

   A bone can be in up to 3 pairs: each vertex keeps one weight per partner, partners in bone order (never the order the
   pairs were asked for), the weights added and, if they come to more than 1, scaled to 1. Where one pair's meeting
   vertices carry weight from another pair too (the bands overlap there) the pieces no longer stay exactly on one
   another, and that is reported as a conflict, never settled by which pair came last.

   Four steps, kept apart: DETECTION (a pair is ELIGIBLE when 3 or more of one bone's vertices are on the other's at
   rest), ACTIVATION (only the pairs asked for: --weld, --weld-all, a model's built-in ones), EXCLUSION (pairs
   left out: --weld-skip, BLEND_SKIP), and the blending itself. "Rest" is the bind pose if the pair meets there,
   else the model's idle pose (its first motion's first keyframe), where the pieces are modelled to fit. */

static float g_blendEps = 0;                          /* how near two vertices are to be "the same point" at rest; 0: 1% of the model's size */
static float g_blendBand = 60;                       /* game units over which the blending fades out */
static std::vector<std::pair<int, int>> g_blendExtra; /* pairs asked for on the command line, on any model */
static std::vector<std::pair<int, int>> g_blendSkip;  /* pairs left out on the command line */
static bool g_blendAll = false;                       /* --weld-all: every eligible pair (but the excluded) */

/* Pairs left out for good, by model: hinges and the like that are meant to show their gap. Bones are the file's numbers. */
struct BlendSkip {
    const char *model;
    int a, b;
    const char *why;
};
static const BlendSkip BLEND_SKIP[] = {
    {"M126BRAK.BIN", 16, 17, "Brachiomon's jaw: a hinge, its opening is meant to show"},
};

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

/* the rest a pair meets in: the one with more of its vertices on one another (the bind pose if they tie) */
static const std::vector<std::array<float, 16>> &blend_pick(const Model &m, const BlendRest &r, int a, int b, float &eps, int &count) {
    int nb = blend_count(m, r.bind, r.epsBind, a, b), ni = blend_count(m, r.idle, r.epsIdle, a, b);
    if (nb >= ni) return count = nb, eps = r.epsBind, r.bind;
    return count = ni, eps = r.epsIdle, r.idle;
}

/* DETECTION: how many of A's vertices are on one of B's at rest; a pair needs 3 to be eligible */
static int blend_meets(const Model &m, const BlendRest &r, int a, int b) {
    float eps;
    int n;
    blend_pick(m, r, a, b, eps, n);
    return n;
}

/* A bone's blending: up to 3 partners, and for each vertex of its mesh the weight of each */
struct BoneBlend {
    int n = 0;
    int partner[3] = {0, 0, 0};
    float rel[3][16];                   /* partner's rest inverse * this bone's rest: this bone's space to the partner's */
    std::vector<std::array<float, 3>> w; /* by mesh vertex */
};

struct BlendPair {
    int a = 0, b = 0, meets = 0;        /* a < b */
    const char *state = "eligible";     /* eligible (found, not asked for), enabled, excluded */
};

struct BlendSet {
    std::vector<BoneBlend> bone;        /* by bone; n == 0: drawn rigid */
    std::vector<BlendPair> pairs;       /* every eligible pair, with what became of it */
    std::vector<std::string> notes;     /* conflicts between pairs and pairs that couldn't be used */
    bool any() const {
        for (const BoneBlend &b : bone)
            if (b.n) return true;
        return false;
    }
    const BlendPair *find(int x, int y) const {
        for (const BlendPair &p : pairs)
            if (p.a == std::min(x, y) && p.b == std::max(x, y)) return &p;
        return nullptr;
    }
};

static std::pair<int, int> blend_key(int a, int b) { return {std::min(a, b), std::max(a, b)}; }

static void blend_build(const Model &m, BlendSet &out) {
    out = BlendSet();
    out.bone.resize(m.bones.size());
    BlendRest rest = blend_rest(m);
    /* DETECTION: every parent and child that have meshes and meet */
    std::set<std::pair<int, int>> eligible;
    std::map<std::pair<int, int>, int> meets;
    for (size_t b = 1; b < m.bones.size(); b++) {
        int p = m.bones[b].parent;
        if (p < 1) continue;
        int n = blend_meets(m, rest, (int)b, p);
        if (n >= 3) eligible.insert(blend_key((int)b, p)), meets[blend_key((int)b, p)] = n;
    }
    /* ACTIVATION and EXCLUSION */
    std::set<std::pair<int, int>> want, skip;
    for (const auto &p : g_blendExtra) want.insert(blend_key(p.first, p.second));
    if (m.name == "M366GIRU.BIN") want.insert({3, 4}); /* Guilmon's wrist, the prototype's */
    if (g_blendAll) want.insert(eligible.begin(), eligible.end());
    for (const auto &p : g_blendSkip) skip.insert(blend_key(p.first, p.second));
    for (const BlendSkip &s : BLEND_SKIP)
        if (m.name == s.model) skip.insert(blend_key(s.a, s.b));
    for (const auto &p : eligible) {
        BlendPair bp;
        bp.a = p.first, bp.b = p.second, bp.meets = meets[p];
        bp.state = skip.count(p) ? "excluded" : want.count(p) ? "enabled" : "eligible";
        out.pairs.push_back(bp);
    }
    for (const auto &p : want)
        if (!eligible.count(p) && !skip.count(p))
            out.notes.push_back("pair " + std::to_string(p.first) + "," + std::to_string(p.second) + " was asked for but isn't eligible (fewer than 3 vertices meet)");

    /* each enabled pair's weights, its two bones' vertices */
    struct PairW {
        int a, b;
        std::vector<float> wa, wb;
        std::vector<int> contactA, contactB; /* the vertices on the pieces' meeting points */
        float ab[16], ba[16];                /* A's rest space to B's, and back */
    };
    std::vector<PairW> pw;
    for (const BlendPair &bp : out.pairs) {
        if (strcmp(bp.state, "enabled")) continue;
        int a = bp.a, b = bp.b;
        float EPS;
        int count;
        const std::vector<std::array<float, 16>> &world = blend_pick(m, rest, a, b, EPS, count);
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
        PairW w;
        w.a = a, w.b = b;
        std::vector<std::array<float, 3>> meet; /* where the pieces meet */
        for (size_t i = 0; i < pa.size(); i++)
            for (size_t k = 0; k < pb.size(); k++)
                if (dist(pa[i], pb[k]) <= EPS) {
                    meet.push_back(pa[i]);
                    break;
                }
        if (meet.size() < 3) continue;
        /* a vertex's w: 0.5 on the meeting points, a smooth fall to 0 at the band's end */
        auto weights = [&](const std::vector<std::array<float, 3>> &p, std::vector<float> &wt, std::vector<int> &contact) {
            wt.assign(p.size(), 0);
            for (size_t i = 0; i < p.size(); i++) {
                float d = 1e30f;
                for (const auto &q : meet) d = std::min(d, dist(p[i], q));
                if (d <= EPS) {
                    wt[i] = 0.5f;
                    contact.push_back((int)i);
                } else {
                    float s = std::min(d / std::max(g_blendBand, 1.0f), 1.0f);
                    wt[i] = 0.5f * (1 - s * s * (3 - 2 * s));
                }
            }
        };
        weights(pa, w.wa, w.contactA), weights(pb, w.wb, w.contactB);
        M4 ab = mul(from(Rbi), from(Ra)), ba = mul(from(Rai), from(Rb));
        memcpy(w.ab, ab.m, sizeof w.ab), memcpy(w.ba, ba.m, sizeof w.ba);
        pw.push_back(std::move(w));
    }

    /* each bone's partners, in bone order: the same however the pairs were asked for */
    struct Slot {
        int partner, pair;
        bool isA;
    };
    std::vector<std::vector<Slot>> slots(m.bones.size());
    for (size_t k = 0; k < pw.size(); k++) {
        slots[pw[k].a].push_back({pw[k].b, (int)k, true});
        slots[pw[k].b].push_back({pw[k].a, (int)k, false});
    }
    for (size_t b = 1; b < m.bones.size(); b++) {
        std::vector<Slot> &sl = slots[b];
        if (sl.empty()) continue;
        std::sort(sl.begin(), sl.end(), [](const Slot &x, const Slot &y) { return x.partner < y.partner; });
        if (sl.size() > 3) {
            out.notes.push_back("bone " + std::to_string(b) + " is in " + std::to_string(sl.size()) + " pairs; only the 3 with the lowest partner bones are used");
            sl.resize(3);
        }
        BoneBlend &bb = out.bone[b];
        bb.n = (int)sl.size();
        size_t nv = m.meshes[m.bones[b].mesh].verts.size();
        bb.w.assign(nv, {0, 0, 0});
        for (int s = 0; s < bb.n; s++) {
            const PairW &w = pw[sl[s].pair];
            bb.partner[s] = sl[s].partner;
            memcpy(bb.rel[s], sl[s].isA ? w.ab : w.ba, sizeof bb.rel[s]);
            const std::vector<float> &wt = sl[s].isA ? w.wa : w.wb;
            for (size_t i = 0; i < nv; i++) bb.w[i][s] = wt[i];
        }
        for (auto &v : bb.w) { /* weights that come to more than 1 are scaled to 1 */
            float sum = v[0] + v[1] + v[2];
            if (sum > 1) v[0] /= sum, v[1] /= sum, v[2] /= sum;
        }
    }

    /* CONFLICTS: a pair's meeting vertices that also carry another pair's weight */
    for (size_t k = 0; k < pw.size(); k++)
        for (int side = 0; side < 2; side++) {
            int bone = side ? pw[k].b : pw[k].a;
            const std::vector<int> &contact = side ? pw[k].contactB : pw[k].contactA;
            const BoneBlend &bb = out.bone[bone];
            for (int s = 0; s < bb.n; s++) {
                if (bb.partner[s] == (side ? pw[k].a : pw[k].b)) continue; /* this pair's own slot */
                int hit = 0;
                for (int i : contact)
                    if (bb.w[i][s] > 1e-3f) hit++;
                if (hit)
                    out.notes.push_back("conflict: pair " + std::to_string(pw[k].a) + "," + std::to_string(pw[k].b) + " has " + std::to_string(hit) +
                                        " meeting vertices of bone " + std::to_string(bone) + " that also follow bone " + std::to_string(bb.partner[s]) +
                                        " (pair " + std::to_string(std::min(bone, bb.partner[s])) + "," + std::to_string(std::max(bone, bb.partner[s])) +
                                        "): they won't stay exactly on one another");
            }
        }
    std::sort(out.notes.begin(), out.notes.end());
}
