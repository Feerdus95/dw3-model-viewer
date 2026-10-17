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

   Five things, kept apart:
   MATCHING  which of a pair's vertices are the same point. Path A: vertices that coincide in the bind pose, within
             BLEND_TOL. Path B, only when path A finds fewer than 3: the idle pose, within BLEND_RING_TOL of the
             ring's own radius (never a share of the whole model). A pair needs 3 matched vertices to be ELIGIBLE.
   VERDICT   the deformation audit, built in: the pair alone is run over the model's motions. It is VALIDATED if its
             matched vertices never end up farther apart blended than they were at rest, and nothing moves farther than
             what a ring turned about its own middle could carry it; else REJECTED, with the reason. It is run for every pair about to be drawn.
             A ring that shrinks (blended against the rigid ring of the same pose) below BLEND_PINCH_WARN is not rejected but flagged REVIEW: the figure
             measures shrinkage, not whether the joint looks right. Only BLEND_REJECT, confirmed by eye, rejects for how a joint looks.
   ACTIVATION only the pairs asked for (--weld, --weld-all, a model's built-in ones) AND validated are drawn; a REVIEW pair only when asked for by name (--weld, not --weld-all).
   EXCLUSION pairs left out whatever their verdict (--weld-skip, BLEND_SKIP), which wins over activation.
   "Rest" is the bind pose for path A and the model's idle pose (first motion, first keyframe) for path B. */

static float g_blendBand = 60;                       /* game units over which the blending fades out */
static std::vector<std::pair<int, int>> g_blendExtra; /* pairs asked for on the command line, on any model */
static std::vector<std::pair<int, int>> g_blendSkip;  /* pairs left out on the command line */
static bool g_blendUnreviewed = false;                /* --weld-unreviewed: EXPERIMENT. Also draws the pairs nobody has looked at, if they validate */
/* --weld-inspect: DRAW asked-for pairs even when rejected, to look at why. A diagnostic only; --weld-all ignores it */
static bool g_blendInspect = false;
static bool g_blendValidateAll = false;               /* --blend-audit: run the verdict for every eligible pair, drawn or not */

/* The tolerances. Units are the game's: a model's vertices are int16, and 100 of them are 1 unit of the OBJ. */
static const float BLEND_TOL = 1.5f;       /* path A: two vertices of the bind pose are the same point within this. The data is integers; the bind
                                              pose is rotations (4096 to the turn) in float, whose rounding moves a vertex by under one unit. */
static const float BLEND_RING_TOL = 0.05f; /* path B: the same point within this share of the ring's radius: a ring modelled to meet is off by a
                                              few percent of its own size at most, and the blend keeps whatever gap it had at rest */
static const float BLEND_RING_MIN = 10;    /* a ring smaller than this (a tenth of an OBJ unit) has no scale to judge by: rounding is a tenth of it */
static const int BLEND_RING_K = 8;         /* path B finds the ring among each piece's K vertices nearest the other piece */
static const float BLEND_SEARCH = 0.25f;   /* ... and only within this share of the smaller piece's size */
/* Pinch (the old, rest-relative figure, kept as a diagnostic): the average of two matrices that differ by a turn of THETA about an axis through the ring shrinks the ring by cos(THETA/2) across the
   axis (all across, for a twist about the ring's own axis, which at 180 degrees collapses it). So the blended ring's radius over its rest radius
   says how much twist or bend a pair carries. BLEND_PINCH_MIN is how much it may shrink: 15%, a twist of about 60 degrees or a bend of about 100;
   past that the joint reads as pinched. (Derived before measuring; it was set aside once on poses that turned out to be artifacts of jumping
   to keyframes, and put back when the motions were played properly.) */
static const float BLEND_PINCH_WARN = 0.85f; /* a WARNING, not a gate: the blended ring is under this share of the rigid ring of the same pose. The rigid ring is the right
                                                reference (a scale animation shrinks both); 85% is the 15% of the derivation above, and where the visual inspection of the 40 pairs
                                                below it found good, neutral and bad joints alike. It says "look", not "bad". */
/* Joints confirmed destructive by eye: rejected whatever the numbers say (never drawn by --weld-all or --weld) */
struct BlendReject {
    const char *model;
    int a, b;
    const char *why;
};
static const BlendReject BLEND_REJECT[] = {
    {"M122HAGU.BIN", 1, 2, "the gear spins inside the body shell; blended, the shell collapses (seen)"},
};
/* FADE (opt-in, --weld-fade): averaging two matrices collapses a ring when one bone turns a lot against the other (an elbow's inside, a twisted tube: a
   turn of THETA shrinks it by cos(THETA/2), 15% at about 60 degrees, half at 120). So a pair's weights fade with the turn of its partner's image from
   where its own bone puts the vertex: whole up to FADE_START, none from FADE_END, smoothstep between, back to the rigid pose (and its crack) at extremes. */
static bool g_blendFade = false;
static const float BLEND_FADE_START = 60.0f, BLEND_FADE_END = 120.0f; /* degrees */
/* the share of the weight kept: A the bone's own world matrix, B its partner's image of it (both column-major, scale allowed, no shear) */
static float blend_fade(const float *A, const float *B) {
    float a[9], b[9];
    for (int c = 0; c < 3; c++) {
        float la = sqrtf(A[c * 4] * A[c * 4] + A[c * 4 + 1] * A[c * 4 + 1] + A[c * 4 + 2] * A[c * 4 + 2]);
        float lb = sqrtf(B[c * 4] * B[c * 4] + B[c * 4 + 1] * B[c * 4 + 1] + B[c * 4 + 2] * B[c * 4 + 2]);
        if (la < 1e-6f || lb < 1e-6f) return 1;
        for (int r = 0; r < 3; r++) a[c * 3 + r] = A[c * 4 + r] / la, b[c * 3 + r] = B[c * 4 + r] / lb;
    }
    float tr = 0; /* trace of a^T b */
    for (int i = 0; i < 9; i++) tr += a[i] * b[i];
    float theta = acosf(std::max(-1.0f, std::min(1.0f, (tr - 1) / 2))) * 57.29578f;
    float t = std::max(0.0f, std::min(1.0f, (theta - BLEND_FADE_START) / (BLEND_FADE_END - BLEND_FADE_START)));
    return 1 - t * t * (3 - 2 * t);
}
/* SUBDIVISION (opt-in, --weld-subdiv): a tube between two rings has no vertex to round an elbow with, so the triangles that reach into a pair's band are
   split into four (midpoints) until their edges are under BLEND_SUBDIV_EDGE, and the new vertices get the weight the same rule gives them. A draw-time
   copy: the model's own polygons are untouched. */
static bool g_blendSubdiv = false;
static const float BLEND_SUBDIV_EDGE = 20; /* game units (a fifth of an OBJ unit) */
static const int BLEND_SUBDIV_DEPTH = 5;
/* the weights of a point (the bone's own mesh space) that isn't a vertex: as blend_build gives a vertex, then scaled if they come to more than 1 */
static void blend_weights_at(const struct BoneBlend &bb, const float *v, float out[3]);
static const float BLEND_SEP_SLACK = 1.0f; /* blended, matched vertices may be this much (rounding) farther apart than at rest */
/* Displacement: a vertex follows its partner bone by w (at most 0.5) of the way, so it moves at most w times the two bones' offset, and for a
   ring turned about its own middle that offset is at most twice the vertex's distance L from the middle: displacement <= L. A weighted vertex is
   within the band of the ring, so L <= (the ring's farthest vertex from its middle) + the band. More than that means the pieces don't turn about
   the ring: they slide or swing about a far point, and blending would drag one across the other. */

/* Pairs left out for good, by model: hinges and the like that are meant to show their gap. Bones are the file's numbers. */
struct BlendSkip {
    const char *model;
    int a, b;
    const char *why;
};
static const BlendSkip BLEND_SKIP[] = {
    {"M126BRAK.BIN", 16, 17, "Brachiomon's jaw: a hinge, its opening is meant to show"},
    {"M366GIRU.BIN", 6, 7, "Guilmon's jaw: a hinge; blended, the jaw and teeth distort"},
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

/* the model at rest, two ways: the bind pose (each bone's keys' first frame, as the OBJ has it), and its idle pose
   (its first motion's first keyframe) */
struct BlendRest {
    std::vector<std::array<float, 16>> bind, idle;
};

static BlendRest blend_rest(const Model &m) {
    BlendRest r;
    Model pose = m; /* the idle pose, without moving the model on show */
    if (!pose.motions.empty()) pose.setMotion(1);
    r.bind.resize(m.bones.size()), r.idle.resize(m.bones.size());
    for (size_t b = 0; b < m.bones.size(); b++) {
        bind_world(m, (int)b, r.bind[b].data());
        if (pose.motions.empty()) r.idle[b] = r.bind[b];
        else memcpy(r.idle[b].data(), pose.pose[b].world, 16 * sizeof(float));
    }
    return r;
}

typedef std::array<float, 3> BlendPt;
static float blend_dist(const BlendPt &p, const BlendPt &q) {
    return sqrtf((p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]));
}
static std::vector<BlendPt> blend_pts(const Mesh &me, const float *world) {
    std::vector<BlendPt> out(me.verts.size());
    for (size_t i = 0; i < me.verts.size(); i++) blend_point(world, me.verts[i], out[i].data());
    return out;
}

/* How two bones' meshes match: which vertices of A (the lower bone) are the same point as which of B's, by what path and
   tolerance, at what ring radius */
struct BlendMatch {
    bool candidate = false;           /* 3 or more vertices matched (or a ring found but with no scale: why says) */
    char path = 0;                    /* 'A' bind pose, exact; 'B' idle pose, by the ring's own scale */
    const std::vector<std::array<float, 16>> *world = nullptr;
    float tol = 0, ring = 0, ringMax = 0; /* the nearness used, the ring's rms radius and its farthest vertex from the middle, in units */
    std::vector<std::pair<int, int>> pairs; /* A's vertex, the B vertex it is on */
    int nearA = 0, nearB = 0;         /* vertices of each within half a ring radius of the other piece: what coverage is of */
    std::string why;                  /* set when the ring has no usable scale */
    std::string miss;                 /* when not a candidate: how near path B came (for the diagnostic of a pair asked for) */
};

static float blend_ring(const std::vector<BlendPt> &p) { /* the radius of a ring of points: their rms distance from their middle */
    if (p.size() < 3) return 0;
    BlendPt c = {0, 0, 0};
    for (const auto &q : p) c[0] += q[0], c[1] += q[1], c[2] += q[2];
    for (float &x : c) x /= (float)p.size();
    double s = 0;
    for (const auto &q : p) s += (double)blend_dist(q, c) * blend_dist(q, c);
    return (float)sqrt(s / p.size());
}

static float blend_ring_max(const std::vector<BlendPt> &p) { /* the farthest of a ring's points from their middle */
    if (p.size() < 3) return 0;
    BlendPt c = {0, 0, 0};
    for (const auto &q : p) c[0] += q[0], c[1] += q[1], c[2] += q[2];
    for (float &x : c) x /= (float)p.size();
    float m = 0;
    for (const auto &q : p) m = std::max(m, blend_dist(q, c));
    return m;
}

static float blend_size(const std::vector<BlendPt> &p) { /* a piece's size: its box's diagonal */
    if (p.empty()) return 0;
    BlendPt lo = p[0], hi = p[0];
    for (const auto &q : p)
        for (int i = 0; i < 3; i++) lo[i] = std::min(lo[i], q[i]), hi[i] = std::max(hi[i], q[i]);
    return blend_dist(lo, hi);
}

static BlendMatch blend_match(const Model &m, const BlendRest &r, int a, int b) {
    BlendMatch mt;
    if (a < 1 || b < 1 || a >= (int)m.bones.size() || b >= (int)m.bones.size() || m.bones[a].mesh < 0 || m.bones[b].mesh < 0) return mt;
    const Mesh &ma = m.meshes[m.bones[a].mesh], &mb = m.meshes[m.bones[b].mesh];
    for (int path = 0; path < 2; path++) {
        const auto &world = path ? r.idle : r.bind;
        std::vector<BlendPt> pa = blend_pts(ma, world[a].data()), pb = blend_pts(mb, world[b].data());
        std::vector<float> d(pa.size(), 1e30f);
        std::vector<int> nearest(pa.size(), -1);
        for (size_t i = 0; i < pa.size(); i++)
            for (size_t k = 0; k < pb.size(); k++) {
                float x = blend_dist(pa[i], pb[k]);
                if (x < d[i]) d[i] = x, nearest[i] = (int)k;
            }
        float tol = BLEND_TOL, ring = 0;
        std::vector<int> pick;
        if (path == 0) {
            for (size_t i = 0; i < pa.size(); i++)
                if (d[i] <= tol) pick.push_back((int)i);
            if (pick.size() < 3) continue;
        } else {
            /* the ring: the K vertices of A nearest B, if near enough; its radius is the scale the tolerance comes from */
            std::vector<int> order(pa.size());
            for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
            std::sort(order.begin(), order.end(), [&](int x, int y) { return d[x] < d[y] || (d[x] == d[y] && x < y); });
            float search = BLEND_SEARCH * std::min(blend_size(pa), blend_size(pb));
            std::vector<BlendPt> cand;
            for (size_t i = 0; i < order.size() && (int)i < BLEND_RING_K; i++)
                if (d[order[i]] <= search) cand.push_back(pa[order[i]]);
            if (cand.size() < 3) continue;
            ring = blend_ring(cand);
            if (ring < BLEND_RING_MIN) {
                mt.candidate = true, mt.path = 'B', mt.world = &world, mt.ring = ring;
                mt.why = "no robust local scale: the ring is " + std::to_string((int)ring) + " units across, under " + std::to_string((int)BLEND_RING_MIN);
                return mt;
            }
            tol = std::max(BLEND_TOL, BLEND_RING_TOL * ring);
            for (size_t i = 0; i < pa.size(); i++)
                if (d[i] <= tol) pick.push_back((int)i);
            if (pick.size() < 3) {
                char buf[200];
                snprintf(buf, sizeof buf, "the 3 nearest vertices are up to %.0f units apart in the idle pose; path B allows %.1f (%.0f%% of the ring's %.0f)", d[order[2]], tol,
                         100 * BLEND_RING_TOL, ring);
                mt.miss = buf;
                continue;
            }
        }
        mt.candidate = true, mt.path = path ? 'B' : 'A', mt.world = &world, mt.tol = tol;
        for (int i : pick) mt.pairs.push_back({i, nearest[i]});
        std::vector<BlendPt> pm;
        for (int i : pick) pm.push_back(pa[i]);
        mt.ring = blend_ring(pm), mt.ringMax = blend_ring_max(pm);
        if (mt.ring < BLEND_RING_MIN) {
            mt.why = "no robust local scale: the ring is " + std::to_string((int)mt.ring) + " units across, under " + std::to_string((int)BLEND_RING_MIN);
            return mt;
        }
        float half = 0.5f * mt.ring;
        for (size_t i = 0; i < pa.size(); i++) mt.nearA += d[i] <= half;
        for (size_t k = 0; k < pb.size(); k++) {
            float x = 1e30f;
            for (const auto &q : pa) x = std::min(x, blend_dist(pb[k], q));
            mt.nearB += x <= half;
        }
        return mt;
    }
    return mt;
}

/* A bone's blending: up to 3 partners, and for each vertex of its mesh the weight of each */
struct BoneBlend {
    int n = 0;
    int partner[3] = {0, 0, 0};
    float rel[3][16];                   /* partner's rest inverse * this bone's rest: this bone's space to the partner's */
    std::vector<std::array<float, 3>> w; /* by mesh vertex */
    /* what the weights were made from, for vertices that don't exist yet (the subdivision): per partner, the meeting points in the pair's rest
       space, this bone's rest matrix to that space, and the tolerance of a point on the meeting points */
    std::vector<std::array<float, 3>> meet[3];
    float restW[3][16];
    float tol[3] = {0, 0, 0};
};

static void blend_weights_at(const BoneBlend &bb, const float *v, float out[3]) {
    out[0] = out[1] = out[2] = 0;
    for (int s = 0; s < bb.n; s++) {
        const float *R = bb.restW[s];
        BlendPt p;
        for (int i = 0; i < 3; i++) p[i] = R[i] * v[0] + R[4 + i] * v[1] + R[8 + i] * v[2] + R[12 + i];
        float d = 1e30f;
        for (const BlendPt &q : bb.meet[s]) d = std::min(d, blend_dist(p, q));
        if (d <= bb.tol[s]) out[s] = 0.5f;
        else {
            float t = std::min(d / std::max(g_blendBand, 1.0f), 1.0f);
            out[s] = 0.5f * (1 - t * t * (3 - 2 * t));
        }
    }
    float sum = out[0] + out[1] + out[2];
    if (sum > 1) out[0] /= sum, out[1] /= sum, out[2] /= sum;
}

struct BlendPair {
    int a = 0, b = 0;                   /* a < b */
    int meets = 0;                      /* matched vertices */
    /* the state: eligible (matched, no verdict yet), validated, rejected (why), enabled (asked for and validated), excluded */
    const char *state = "eligible";
    std::string why;                    /* the reason, for rejected */
    /* matching */
    char path = 0;
    float tol = 0, ring = 0, ringMax = 0;
    std::vector<std::pair<int, int>> matched;
    int nearA = 0, nearB = 0;
    /* what the pair was given, once its weights are built */
    int weightedA = 0, weightedB = 0, contactA = 0, contactB = 0, vertsA = 0, vertsB = 0;
    int sharedMatched = 0;              /* matched pairs whose two vertices both follow the other bone, with the same weight */
    /* the verdict, over the motions, in units */
    bool judged = false;
    float sepRigid = 0, sepBlended = 0, disp = 0, pinch = 1; float pinchVsRigid = 1; /* the smallest blended ring over the rigid ring of the SAME pose (an animated scale shrinks both): the metric the verdict uses */
    int pvMo = 0, pvKey = 0, pvPoses = 0, pvDegen = 0; /* where, and the poses measured and skipped (a rigid ring under BLEND_RING_MIN has no scale) */
    bool review = false;
    float fadeMin = 1;  /* --weld-fade: the least share of the weight kept in any pose (blend_fade) ... */
    int fadePoses = 0, fadeHit = 0; /* ... and the poses measured and those where it was under 1 */
    const BlendDecision *decision = nullptr; /* what was decided by eye (blend_decisions.inc), if anyone looked */
    int pinchMo = 0, pinchKey = 0; /* pinch: the smallest the blended ring gets, as a share of its rest radius */
    int dispMo = 0, dispKey = 0;
};

struct BlendSet {
    std::vector<BoneBlend> bone;        /* by bone; n == 0: drawn rigid */
    std::vector<BlendPair> pairs;       /* every eligible pair, with what became of it */
    std::vector<std::string> notes;     /* conflicts between pairs and pairs that couldn't be used */
    int scaled = 0;                     /* vertices whose weights came to more than 1 and were scaled */
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

static void blend_build(const Model &m, BlendSet &out, const std::set<std::pair<int, int>> *only = nullptr) {
    out = BlendSet();
    out.bone.resize(m.bones.size());
    BlendRest rest = blend_rest(m);
    /* MATCHING: every parent and child that have meshes */
    std::map<std::pair<int, int>, BlendMatch> match;
    std::map<std::pair<int, int>, std::string> missed; /* near misses of path B, for the notes */
    for (size_t b = 1; b < m.bones.size(); b++) {
        int p = m.bones[b].parent;
        if (p < 1) continue;
        auto key = blend_key((int)b, p);
        BlendMatch mt = blend_match(m, rest, key.first, key.second);
        if (mt.candidate) match[key] = std::move(mt);
        else if (!mt.miss.empty()) missed[key] = mt.miss;
    }
    /* ACTIVATION and EXCLUSION */
    std::set<std::pair<int, int>> want, skip, explicitAsk; /* explicitAsk: by name, which also draws a pair flagged for review (--weld-all doesn't) */
    if (only) { /* a diagnostic asking for these pairs and no others */
        want = explicitAsk = *only;
    } else {
        for (const auto &p : g_blendExtra) want.insert(blend_key(p.first, p.second));
        explicitAsk = want;
    }
    /* the per-pair decisions: approved are asked for by name, excluded are left out (over everything), pending are drawn only by name */
    std::map<std::pair<int, int>, const BlendDecision *> decided;
    for (const BlendDecision &d : BLEND_DECISIONS)
        if (m.name == d.model) {
            auto k = blend_key(d.a, d.b);
            decided[k] = &d;
            if (only) continue; /* a diagnostic asking for these pairs: the decisions are shown, not applied */
            if (d.kind == BLEND_APPROVED) want.insert(k), explicitAsk.insert(k);
            else if (d.kind == BLEND_EXCLUDED) skip.insert(k);
            else if (!explicitAsk.count(k)) want.erase(k);
        }
    if (g_blendUnreviewed && !only) { /* the experiment: what nobody has looked at (a decided pair keeps its decision) */
        for (const auto &kv : match)
            if (!decided.count(kv.first)) want.insert(kv.first);
        out.notes.push_back("experimental: pairs nobody has reviewed are drawn (--weld-unreviewed)");
    }
    for (const auto &p : g_blendSkip) skip.insert(blend_key(p.first, p.second));
    for (const BlendSkip &s : BLEND_SKIP)
        if (m.name == s.model) skip.insert(blend_key(s.a, s.b));
    for (const auto &kv : match) {
        const BlendMatch &mt = kv.second;
        BlendPair bp;
        bp.a = kv.first.first, bp.b = kv.first.second, bp.meets = (int)mt.pairs.size();
        bp.path = mt.path, bp.tol = mt.tol, bp.ring = mt.ring, bp.ringMax = mt.ringMax, bp.matched = mt.pairs, bp.nearA = mt.nearA, bp.nearB = mt.nearB;
        bp.why = mt.why;
        if (decided.count(kv.first)) bp.decision = decided[kv.first];
        if (!mt.why.empty()) bp.state = "rejected";
        out.pairs.push_back(std::move(bp));
    }
    for (const auto &p : want)
        if (!match.count(p) && !skip.count(p))
            out.notes.push_back("pair " + std::to_string(p.first) + "," + std::to_string(p.second) + " was asked for but isn't eligible (fewer than 3 vertices match" +
                                (missed.count(p) ? ": " + missed[p] : std::string()) + ")");

    /* each pair's weights, and its verdict: needed for the pairs about to be drawn, and for all with --blend-audit */
    struct PairW {
        int pair; /* index in out.pairs */
        std::vector<float> wa, wb;
        std::vector<int> contactA, contactB; /* the vertices on the pieces' meeting points */
        float ab[16], ba[16];                /* A's rest space to B's, and back */
        std::vector<BlendPt> meet;           /* the meeting points, in the pair's rest space */
        float Ra[16], Rb[16];                /* each bone's rest matrix to it */
        float tol = 0;
    };
    std::vector<PairW> pw;
    std::vector<int> judge; /* indices into pw to give a verdict */
    for (size_t k = 0; k < out.pairs.size(); k++) {
        BlendPair &bp = out.pairs[k];
        auto key = blend_key(bp.a, bp.b);
        bool wanted = want.count(key) && !skip.count(key);
        if (!bp.why.empty()) {
            if (wanted) out.notes.push_back("pair " + std::to_string(bp.a) + "," + std::to_string(bp.b) + " was asked for but is rejected: " + bp.why);
            continue;
        }
        if (!wanted && !g_blendValidateAll && !only) continue;
        const BlendMatch &mt = match[key];
        const std::vector<std::array<float, 16>> &world = *mt.world;
        const float *Ra = world[bp.a].data(), *Rb = world[bp.b].data();
        float Rai[16], Rbi[16];
        if (!blend_inverse(Ra, Rai) || !blend_inverse(Rb, Rbi)) {
            bp.state = "rejected", bp.why = "a bone's matrix can't be inverted";
            continue;
        }
        const Mesh &ma = m.meshes[m.bones[bp.a].mesh], &mb = m.meshes[m.bones[bp.b].mesh];
        std::vector<BlendPt> pa = blend_pts(ma, Ra), pb = blend_pts(mb, Rb);
        PairW w;
        w.pair = (int)k;
        std::vector<BlendPt> meet; /* where the pieces meet */
        for (const auto &pr : bp.matched) meet.push_back(pa[pr.first]);
        /* a vertex's w: 0.5 on the meeting points, a smooth fall to 0 at the band's end */
        auto weights = [&](const std::vector<BlendPt> &p, std::vector<float> &wt, std::vector<int> &contact) {
            wt.assign(p.size(), 0);
            for (size_t i = 0; i < p.size(); i++) {
                float d = 1e30f;
                for (const auto &q : meet) d = std::min(d, blend_dist(p[i], q));
                if (d <= mt.tol) {
                    wt[i] = 0.5f;
                    contact.push_back((int)i);
                } else {
                    float s = std::min(d / std::max(g_blendBand, 1.0f), 1.0f);
                    wt[i] = 0.5f * (1 - s * s * (3 - 2 * s));
                }
            }
        };
        weights(pa, w.wa, w.contactA), weights(pb, w.wb, w.contactB);
        w.meet = meet, w.tol = mt.tol;
        memcpy(w.Ra, Ra, sizeof w.Ra), memcpy(w.Rb, Rb, sizeof w.Rb);
        bp.vertsA = (int)pa.size(), bp.vertsB = (int)pb.size();
        bp.contactA = (int)w.contactA.size(), bp.contactB = (int)w.contactB.size();
        for (float x : w.wa) bp.weightedA += x > 0;
        for (float x : w.wb) bp.weightedB += x > 0;
        for (const auto &pr : bp.matched) bp.sharedMatched += w.wa[pr.first] > 0 && fabsf(w.wa[pr.first] - w.wb[pr.second]) < 1e-4f;
        M4 ab = mul(from(Rbi), from(Ra)), ba = mul(from(Rai), from(Rb));
        memcpy(w.ab, ab.m, sizeof w.ab), memcpy(w.ba, ba.m, sizeof w.ba);
        judge.push_back((int)pw.size());
        pw.push_back(std::move(w));
    }
    /* VERDICT: each of those pairs alone, over every motion, played from its start as the game plays it: a keyframe that blends two frames
       needs the one before it, so poses reached by jumping would be poses the game never shows. Every 4th keyframe is measured. */
    if (!judge.empty()) {
        Model pm = m;
        for (size_t mo = 1; mo <= pm.motions.size(); mo++) {
            pm.setMotion((int)mo);
            int n = pm.keyframeCount;
            for (int key = 0; key < std::max(n, 1) && !pm.motionDone; key++, pm.advance(1)) {
                pm.evaluate();
                if (key % 4) continue;
                for (int ji : judge) {
                    const PairW &w = pw[ji];
                    BlendPair &bp = out.pairs[w.pair];
                    if (!pm.pose[bp.a].visible || !pm.pose[bp.b].visible) continue;
                    const Mesh &ma = m.meshes[m.bones[bp.a].mesh], &mb = m.meshes[m.bones[bp.b].mesh];
                    M4 mab = mul(from(pm.pose[bp.b].world), from(w.ab)), mba = mul(from(pm.pose[bp.a].world), from(w.ba));
                    auto place = [&](const Vec3s &v, const float *own, const M4 &other, float wt, BlendPt &rigid) {
                        BlendPt p, q, r;
                        blend_point(own, v, p.data()), blend_point(other.m, v, q.data());
                        for (int i = 0; i < 3; i++) r[i] = p[i] + (q[i] - p[i]) * wt;
                        rigid = p;
                        return r;
                    };
                    std::vector<BlendPt> ring, rring;
                    for (const auto &pr : bp.matched) {
                        BlendPt ra, rb;
                        BlendPt xa = place(ma.verts[pr.first], pm.pose[bp.a].world, mab, w.wa[pr.first], ra);
                        BlendPt xb = place(mb.verts[pr.second], pm.pose[bp.b].world, mba, w.wb[pr.second], rb);
                        bp.sepRigid = std::max(bp.sepRigid, blend_dist(ra, rb)), bp.sepBlended = std::max(bp.sepBlended, blend_dist(xa, xb));
                        ring.push_back(xa), rring.push_back(ra);
                    }
                    if (bp.ring > 0 && blend_ring(ring) / bp.ring < bp.pinch) bp.pinch = blend_ring(ring) / bp.ring, bp.pinchMo = (int)mo, bp.pinchKey = key;
                    {
                        float f = std::min(blend_fade(pm.pose[bp.a].world, mab.m), blend_fade(pm.pose[bp.b].world, mba.m));
                        bp.fadePoses++, bp.fadeHit += f < 0.999f, bp.fadeMin = std::min(bp.fadeMin, f);
                    }
                    if (float rr = blend_ring(rring); rr < BLEND_RING_MIN) bp.pvDegen++;
                    else {
                        bp.pvPoses++;
                        if (blend_ring(ring) / rr < bp.pinchVsRigid) bp.pinchVsRigid = blend_ring(ring) / rr, bp.pvMo = (int)mo, bp.pvKey = key;
                    }
                    for (int side = 0; side < 2; side++) {
                        const Mesh &me = side ? mb : ma;
                        const std::vector<float> &wt = side ? w.wb : w.wa;
                        for (size_t i = 0; i < wt.size(); i++) {
                            if (wt[i] <= 0) continue;
                            BlendPt rg;
                            BlendPt bl = place(me.verts[i], pm.pose[side ? bp.b : bp.a].world, side ? mba : mab, wt[i], rg);
                            float d = blend_dist(rg, bl);
                            if (d > bp.disp) bp.disp = d, bp.dispMo = (int)mo, bp.dispKey = key;
                        }
                    }
                }
            }
        }
        for (int ji : judge) {
            BlendPair &bp = out.pairs[pw[ji].pair];
            bp.judged = true;
            char buf[200];
            if (bp.sepBlended > bp.tol + BLEND_SEP_SLACK) {
                snprintf(buf, sizeof buf, "matched vertices end up %.0f units apart blended, more than their %.1f at rest (+%.0f rounding)", bp.sepBlended, bp.tol, BLEND_SEP_SLACK);
                bp.state = "rejected", bp.why = buf;
            } else if (bp.disp > bp.ringMax + std::max(g_blendBand, 1.0f) + BLEND_SEP_SLACK) {
                snprintf(buf, sizeof buf, "a vertex moves %.0f units, more than a ring turned about its middle could carry it (%.0f: its radius %.0f plus the band)", bp.disp,
                         bp.ringMax + std::max(g_blendBand, 1.0f), bp.ringMax);
                bp.state = "rejected", bp.why = buf;
            } else if (bp.pvPoses == 0) {
                bp.state = "review", bp.review = true;
                bp.why = "indeterminate: the rigid ring is under the smallest scale in every pose (the piece collapses), so no shrinkage can be measured";
            } else if (bp.pinchVsRigid < BLEND_PINCH_WARN) {
                snprintf(buf, sizeof buf, "review: the blended ring shrinks to %.0f%% of the rigid ring in the same pose (motion %d key %d); the warning is under %.0f%%", 100 * bp.pinchVsRigid,
                         bp.pvMo, bp.pvKey, 100 * BLEND_PINCH_WARN);
                bp.state = "review", bp.review = true, bp.why = buf;
            } else {
                bp.state = "validated";
            }
            for (const BlendReject &r : BLEND_REJECT)
                if (m.name == r.model && blend_key(r.a, r.b) == blend_key(bp.a, bp.b)) bp.state = "rejected", bp.why = std::string("confirmed destructive: ") + r.why;
        }
    }
    /* ACTIVATION again: only what was asked for and validated; EXCLUSION wins */
    for (BlendPair &bp : out.pairs) {
        auto key = blend_key(bp.a, bp.b);
        if (skip.count(key)) bp.state = "excluded";
        else if (!strcmp(bp.state, "validated") && want.count(key)) bp.state = "enabled";
        else if (!strcmp(bp.state, "review") && explicitAsk.count(key)) {
            bp.state = "enabled";
            out.notes.push_back("pair " + std::to_string(bp.a) + "," + std::to_string(bp.b) + " is drawn although flagged for review: " + bp.why);
        }
        else if (!strcmp(bp.state, "rejected") && want.count(key) && bp.judged && g_blendInspect && !g_blendUnreviewed) {
            bp.state = "enabled";
            out.notes.push_back("pair " + std::to_string(bp.a) + "," + std::to_string(bp.b) + " is drawn for inspection although rejected: " + bp.why);
        } else if (!strcmp(bp.state, "rejected") && want.count(key) && bp.judged)
            out.notes.push_back("pair " + std::to_string(bp.a) + "," + std::to_string(bp.b) + " was asked for but is rejected: " + bp.why);
    }

    /* each enabled pair's partners, in bone order: the same however the pairs were asked for */
    struct Slot {
        int partner, pair;
        bool isA;
    };
    std::vector<std::vector<Slot>> slots(m.bones.size());
    for (size_t k = 0; k < pw.size(); k++) {
        const BlendPair &bp = out.pairs[pw[k].pair];
        if (strcmp(bp.state, "enabled")) continue;
        slots[bp.a].push_back({bp.b, (int)k, true});
        slots[bp.b].push_back({bp.a, (int)k, false});
    }
    for (size_t b = 1; b < m.bones.size(); b++) {
        std::vector<Slot> &sl = slots[b];
        if (sl.empty()) continue;
        std::sort(sl.begin(), sl.end(), [](const Slot &x, const Slot &y) { return x.partner < y.partner; });
        if (sl.size() > 3) {
            std::string kept, left;
            for (size_t s = 0; s < sl.size(); s++) {
                const BlendPair *bp = out.find((int)b, sl[s].partner);
                std::string one = std::to_string(sl[s].partner) + "(" + std::to_string(bp ? bp->meets : 0) + " meet)";
                (s < 3 ? kept : left) += (s < 3 ? (kept.empty() ? "" : " ") : (left.empty() ? "" : " ")) + one;
            }
            out.notes.push_back("bone " + std::to_string(b) + " is in " + std::to_string(sl.size()) + " pairs; used (partner bones, lowest first): " + kept +
                                "; left out: " + left);
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
            bb.meet[s].assign(w.meet.begin(), w.meet.end());
            memcpy(bb.restW[s], sl[s].isA ? w.Ra : w.Rb, sizeof bb.restW[s]);
            bb.tol[s] = w.tol;
        }
        for (auto &v : bb.w) { /* weights that come to more than 1 are scaled to 1 */
            float sum = v[0] + v[1] + v[2];
            if (sum > 1) v[0] /= sum, v[1] /= sum, v[2] /= sum, out.scaled++;
        }
    }

    /* CONFLICTS: an enabled pair's meeting vertices that also carry another pair's weight */
    for (size_t k = 0; k < pw.size(); k++) {
        const BlendPair &bp = out.pairs[pw[k].pair];
        if (strcmp(bp.state, "enabled")) continue;
        for (int side = 0; side < 2; side++) {
            int bone = side ? bp.b : bp.a;
            const std::vector<int> &contact = side ? pw[k].contactB : pw[k].contactA;
            const BoneBlend &bb = out.bone[bone];
            for (int s = 0; s < bb.n; s++) {
                if (bb.partner[s] == (side ? bp.a : bp.b)) continue; /* this pair's own slot */
                int hit = 0;
                for (int i : contact)
                    if (bb.w[i][s] > 1e-3f) hit++;
                if (hit)
                    out.notes.push_back("conflict: pair " + std::to_string(bp.a) + "," + std::to_string(bp.b) + " has " + std::to_string(hit) +
                                        " meeting vertices of bone " + std::to_string(bone) + " that also follow bone " + std::to_string(bb.partner[s]) +
                                        " (pair " + std::to_string(std::min(bone, bb.partner[s])) + "," + std::to_string(std::max(bone, bb.partner[s])) +
                                        "): they won't stay exactly on one another");
            }
        }
    }
    if (out.scaled) out.notes.push_back(std::to_string(out.scaled) + " vertices' weights came to more than 1 and were scaled");
    std::sort(out.notes.begin(), out.notes.end());
}
