/* --seams: a read-only report of how a model's pieces meet at each joint (child bone against its parent's mesh), at
   rest and over every motion. Joints are classed by the rest pose: COIN (3+ child vertices on a parent vertex),
   OVER (the pieces' boxes overlap), GAP (close but apart), FAR (floating). Drift is the farthest those rest pairs get. */
struct SeamPt {
    float x, y, z;
};
static SeamPt seam_tf(const float *w, const Vec3s &v) {
    return {w[0] * v.x + w[4] * v.y + w[8] * v.z + w[12], w[1] * v.x + w[5] * v.y + w[9] * v.z + w[13],
            w[2] * v.x + w[6] * v.y + w[10] * v.z + w[14]};
}
static float seam_dist(const SeamPt &a, const SeamPt &b) {
    return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

static SeamPt seam_sub(const SeamPt &a, const SeamPt &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static float seam_dot(const SeamPt &a, const SeamPt &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static SeamPt seam_mad(const SeamPt &a, const SeamPt &d, float s) { return {a.x + d.x * s, a.y + d.y * s, a.z + d.z * s}; }

/* the closest point of the triangle ABC to P (Ericson, Real-Time Collision Detection 5.1.5) */
static SeamPt seam_closest(const SeamPt &p, const SeamPt &a, const SeamPt &b, const SeamPt &c) {
    SeamPt ab = seam_sub(b, a), ac = seam_sub(c, a), ap = seam_sub(p, a);
    float d1 = seam_dot(ab, ap), d2 = seam_dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return a;
    SeamPt bp = seam_sub(p, b);
    float d3 = seam_dot(ab, bp), d4 = seam_dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return b;
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return seam_mad(a, ab, d1 / (d1 - d3));
    SeamPt cp = seam_sub(p, c);
    float d5 = seam_dot(ab, cp), d6 = seam_dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return c;
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return seam_mad(a, ac, d2 / (d2 - d6));
    float va = d3 * d6 - d5 * d4;
    if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) return seam_mad(b, seam_sub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6)));
    float den = 1 / (va + vb + vc);
    return seam_mad(seam_mad(a, ab, vb * den), ac, vc * den);
}

/* how many of three rays from P (almost along x, y and z) cross the triangles an odd number of times: 3 inside the
   volume they close, 0 outside it, between them the mesh isn't closed there or P is on its skin (Moller-Trumbore) */
static int seam_inside(const SeamPt &p, const std::vector<std::array<int, 3>> &tris, const std::vector<SeamPt> &w) {
    static const SeamPt dirs[3] = {{1, 0.0137f, 0.0071f}, {0.0093f, 1, 0.0151f}, {0.0121f, 0.0057f, 1}};
    int votes = 0;
    for (const SeamPt &d : dirs) {
        int hits = 0;
        for (const auto &t : tris) {
            const SeamPt &a = w[t[0]];
            SeamPt e1 = seam_sub(w[t[1]], a), e2 = seam_sub(w[t[2]], a);
            SeamPt h{d.y * e2.z - d.z * e2.y, d.z * e2.x - d.x * e2.z, d.x * e2.y - d.y * e2.x};
            float det = seam_dot(e1, h);
            if (fabsf(det) < 1e-9f) continue;
            SeamPt s = seam_sub(p, a);
            float u = seam_dot(s, h) / det;
            if (u < 0 || u > 1) continue;
            SeamPt q{s.y * e1.z - s.z * e1.y, s.z * e1.x - s.x * e1.z, s.x * e1.y - s.y * e1.x};
            float v = seam_dot(d, q) / det;
            if (v < 0 || u + v > 1) continue;
            hits += seam_dot(e2, q) / det > 0;
        }
        votes += hits & 1;
    }
    return votes;
}

/* a mesh's triangles, and which of its vertices lie on an open rim (an edge only one polygon uses; vertices at the same
   place count as one, so a split along a UV seam isn't a rim) */
struct SeamMesh {
    std::vector<std::array<int, 3>> tris;
    std::vector<char> rim;
};
static void seam_build(const Mesh &me, SeamMesh &out) {
    std::map<std::tuple<int, int, int>, int> at;
    std::vector<int> canon(me.verts.size());
    for (size_t i = 0; i < me.verts.size(); i++)
        canon[i] = at.emplace(std::make_tuple((int)me.verts[i].x, (int)me.verts[i].y, (int)me.verts[i].z), (int)i).first->second;
    std::map<std::pair<int, int>, int> edges;
    for (const Poly &p : me.polys) {
        int n = p.quad ? 4 : 3;
        static const int tri[3] = {0, 2, 1}, quad[4] = {0, 2, 3, 1}; /* the PSX vertex order walked around the polygon */
        const int *o = p.quad ? quad : tri;
        for (int k = 0; k < n; k++) {
            int a = canon[p.idx[o[k]]], b = canon[p.idx[o[(k + 1) % n]]];
            if (a != b) edges[{std::min(a, b), std::max(a, b)}]++;
        }
        out.tris.push_back({p.idx[o[0]], p.idx[o[1]], p.idx[o[2]]});
        if (p.quad) out.tris.push_back({p.idx[o[0]], p.idx[o[2]], p.idx[o[3]]});
    }
    std::vector<char> open(me.verts.size(), 0);
    for (const auto &e : edges)
        if (e.second == 1) open[e.first.first] = open[e.first.second] = 1;
    out.rim.assign(me.verts.size(), 0);
    for (size_t i = 0; i < me.verts.size(); i++) out.rim[i] = open[canon[i]];
}

struct SeamJoint {
    int fid = 0, bone = 0, parent = 0;
    /* open-rim vertices of one piece (0 the child's, 1 the parent's) near the other's surface at rest: how far outside that
       surface they get over the poses, as part of the model's size */
    std::vector<int> rimV[2];
    float rimExp[2] = {0, 0}, rimAmb[2] = {0, 0}; /* outside the other piece / unsure (its mesh isn't closed there) */
    int rimMotion = 0, rimKey = 0;
    std::string model;
    const char *cls = "";
    int pairs = 0, verts = 0;
    float cover = 0, minGap = 0, drift = 0, normalDeg = 0, maxSep = 0;
    int blendMeet = 0; /* vertices on one another at the bind pose: the joint can be blended with 3 or more */
    int poses = 0, apart = 0, sepMotion = 0, sepKey = 0; /* poses seen, and those where the two pieces' boxes are 1% of the model apart */
    std::vector<std::pair<int, int>> pv; /* child vertex, parent vertex */
};

/* ONLY, when given, is one model file (a mod's, say): just the disc model with its number is analysed, from that file */
static bool g_seamsBlend = false; /* --blend: the joints with a JointBlend placed blended */
static int check_seams(const std::string &only) {
    int onlyFid = only.empty() ? -1 : atoi(std::filesystem::path(only).stem().string().c_str());
    std::vector<SeamJoint> all;
    int models = 0, skipped = 0;
    for (const Entry &e : scan_models()) {
        if (onlyFid >= 0 && e.fid != onlyFid) continue;
        std::vector<uint8_t> bytes;
        read_file(only.empty() ? fid_path(e.fid) : only, bytes);
        std::vector<const Fighter *> fs = g_game.forFile(e.fid);
        Model m;
        if (!m.load(e.name, std::move(bytes), fs.empty() ? nullptr : fs[0]) || m.motions.empty()) {
            skipped++;
            continue;
        }
        models++;
        m.setMotion(1);
        size_t nb = m.bones.size();
        std::vector<std::vector<SeamPt>> rest(nb);
        float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
        std::vector<std::array<float, 6>> box(nb);
        for (size_t b = 1; b < nb; b++) {
            if (m.bones[b].mesh < 0 || !m.pose[b].visible) continue;
            std::array<float, 6> bx = {1e9f, 1e9f, 1e9f, -1e9f, -1e9f, -1e9f};
            for (const Vec3s &v : m.meshes[m.bones[b].mesh].verts) {
                SeamPt p = seam_tf(m.pose[b].world, v);
                rest[b].push_back(p);
                float c[3] = {p.x, p.y, p.z};
                for (int i = 0; i < 3; i++) {
                    lo[i] = std::min(lo[i], c[i]), hi[i] = std::max(hi[i], c[i]);
                    bx[i] = std::min(bx[i], c[i]), bx[3 + i] = std::max(bx[3 + i], c[i]);
                }
            }
            box[b] = bx;
        }
        float D = sqrtf((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) + (hi[2] - lo[2]) * (hi[2] - lo[2]));
        if (D < 1) continue;
        float eps = D * 0.01f, closeD = D * 0.06f, rimNear = D * 0.03f;
        std::vector<SeamMesh> sm(m.meshes.size());
        std::vector<char> smOk(m.meshes.size(), 0);
        auto seam_mesh = [&](int k) -> const SeamMesh & {
            if (!smOk[k]) smOk[k] = 1, seam_build(m.meshes[k], sm[k]);
            return sm[k];
        };
        std::vector<SeamJoint> js;
        BlendRest blendRest = blend_rest(m);
        std::vector<JointBlend> blends;
        if (g_seamsBlend) blend_build(m, blends);
        int curMo = 0, curKey = 0, moveMo = 0, moveKey = 0; /* the pose being placed, and where the blended vertices moved most */
        float blendMove = 0; /* the farthest a blended vertex is from where its bone alone puts it, over the poses */
        /* where a bone's vertex I is in the pose now: its bone's, or blended with the other of its pair */
        std::vector<std::array<float, 16>> m2(nb);
        std::vector<char> m2Ok(nb, 0);
        auto placed = [&](int b, int i) -> SeamPt {
            const Vec3s &v = m.meshes[m.bones[b].mesh].verts[i];
            SeamPt p = seam_tf(m.pose[b].world, v);
            for (const JointBlend &j : blends) {
                bool isA = j.a == b;
                if (!isA && j.b != b) continue;
                int other = isA ? j.b : j.a;
                float w = (isA ? j.wa : j.wb)[i];
                if (w <= 0 || !m.pose[other].visible) continue;
                if (!m2Ok[b]) {
                    m2Ok[b] = 1;
                    M4 mm = mul(from(m.pose[other].world), from(isA ? j.ab : j.ba));
                    memcpy(m2[b].data(), mm.m, sizeof(float) * 16);
                }
                SeamPt q = seam_tf(m2[b].data(), v);
                SeamPt r{p.x + (q.x - p.x) * w, p.y + (q.y - p.y) * w, p.z + (q.z - p.z) * w};
                if (seam_dist(p, r) > blendMove) blendMove = seam_dist(p, r), moveMo = curMo, moveKey = curKey;
                return r;
            }
            return p;
        };
        for (size_t b = 1; b < nb; b++) {
            int p = m.bones[b].parent;
            if (p <= 0 || rest[b].empty() || rest[p].empty()) continue;
            SeamJoint j;
            j.fid = e.fid, j.model = e.name, j.bone = (int)b, j.parent = p, j.verts = (int)rest[b].size();
            std::vector<std::tuple<float, int, int>> near_pairs; /* distance, child vertex, its nearest parent vertex */
            for (size_t i = 0; i < rest[b].size(); i++) {
                float best = 1e30f;
                int bj = 0;
                for (size_t k = 0; k < rest[p].size(); k++) {
                    float d = seam_dist(rest[b][i], rest[p][k]);
                    if (d < best) best = d, bj = (int)k;
                }
                near_pairs.emplace_back(best, (int)i, bj);
            }
            std::sort(near_pairs.begin(), near_pairs.end());
            j.minGap = std::get<0>(near_pairs[0]);
            int close = 0;
            for (auto &t : near_pairs) close += std::get<0>(t) <= eps;
            j.cover = (float)close / j.verts;
            size_t take = close >= 3 ? std::min(close, 64) : std::min<size_t>(8, near_pairs.size());
            for (size_t i = 0; i < take; i++) j.pv.emplace_back(std::get<1>(near_pairs[i]), std::get<2>(near_pairs[i]));
            j.pairs = close;
            bool overlap = true;
            for (int i = 0; i < 3; i++) overlap &= box[b][i] <= box[p][3 + i] && box[p][i] <= box[b][3 + i];
            j.cls = close >= 3 ? "COIN" : j.minGap > closeD ? "FAR" : overlap ? "OVER" : "GAP";
            /* the mean angle between the pairs' normals at rest, in the world */
            const Mesh &cm = m.meshes[m.bones[b].mesh], &pm = m.meshes[m.bones[p].mesh];
            if (cm.normals.size() == cm.verts.size() && pm.normals.size() == pm.verts.size()) {
                double sum = 0;
                int n = 0;
                for (auto &pr : j.pv) {
                    SeamPt a = seam_tf(m.pose[b].world, cm.normals[pr.first]), c = seam_tf(m.pose[p].world, pm.normals[pr.second]);
                    a.x -= m.pose[b].world[12], a.y -= m.pose[b].world[13], a.z -= m.pose[b].world[14];
                    c.x -= m.pose[p].world[12], c.y -= m.pose[p].world[13], c.z -= m.pose[p].world[14];
                    float la = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z), lc = sqrtf(c.x * c.x + c.y * c.y + c.z * c.z);
                    if (la < 1e-3f || lc < 1e-3f) continue;
                    float d = (a.x * c.x + a.y * c.y + a.z * c.z) / (la * lc);
                    sum += acosf(std::max(-1.0f, std::min(1.0f, d))) * 57.29578f, n++;
                }
                j.normalDeg = n ? (float)(sum / n) : 0;
            }
            /* each piece's open-rim vertices that sit near the other's surface at rest (the 16 closest) */
            for (int s = 0; s < 2; s++) {
                int A = s ? (int)p : (int)b, B = s ? (int)b : (int)p;
                const SeamMesh &ma = seam_mesh(m.bones[A].mesh), &mb = seam_mesh(m.bones[B].mesh);
                std::vector<std::pair<float, int>> cand;
                for (size_t i = 0; i < rest[A].size(); i++) {
                    if (!ma.rim[i]) continue;
                    float best = 1e30f;
                    for (const auto &t : mb.tris)
                        best = std::min(best, seam_dist(rest[A][i], seam_closest(rest[A][i], rest[B][t[0]], rest[B][t[1]], rest[B][t[2]])));
                    if (best <= rimNear) cand.emplace_back(best, (int)i);
                }
                std::sort(cand.begin(), cand.end());
                for (size_t i = 0; i < cand.size() && i < 16; i++) j.rimV[s].push_back(cand[i].second);
            }
            j.blendMeet = blend_meets(m, blendRest, (int)b, p);
            j.minGap /= D;
            js.push_back(std::move(j));
        }
        int poseCount = 0;
        /* every motion's poses (at most ~30 keyframes of each) */
        for (size_t mo = 1; mo <= m.motions.size(); mo++) {
            m.setMotion((int)mo);
            int n = m.keyframeCount, step = std::max(1, n / 30);
            for (int k = 0; k < std::max(n, 1); k += step) {
                m.keyframe = k;
                curMo = (int)mo, curKey = k;
                m.evaluate();
                std::fill(m2Ok.begin(), m2Ok.end(), 0);
                std::vector<std::array<float, 6>> pb(nb);
                std::vector<char> have(nb, 0);
                auto boxOf = [&](int b) -> const std::array<float, 6> & {
                    if (have[b]) return pb[b];
                    have[b] = 1;
                    std::array<float, 6> bx = {1e9f, 1e9f, 1e9f, -1e9f, -1e9f, -1e9f};
                    for (size_t vi = 0; vi < m.meshes[m.bones[b].mesh].verts.size(); vi++) {
                        SeamPt q = placed(b, (int)vi);
                        float c[3] = {q.x, q.y, q.z};
                        for (int i = 0; i < 3; i++) bx[i] = std::min(bx[i], c[i]), bx[3 + i] = std::max(bx[3 + i], c[i]);
                    }
                    return pb[b] = bx;
                };
                std::vector<std::vector<SeamPt>> wv(nb);
                std::vector<char> wvOk(nb, 0);
                auto wvOf = [&](int b) -> const std::vector<SeamPt> & {
                    if (!wvOk[b]) {
                        wvOk[b] = 1;
                        for (size_t vi = 0; vi < m.meshes[m.bones[b].mesh].verts.size(); vi++) wv[b].push_back(placed(b, (int)vi));
                    }
                    return wv[b];
                };
                bool rimPose = (poseCount++ % 4) == 0; /* the rim costs more: every 4th pose */
                for (SeamJoint &j : js) {
                    for (int s = 0; s < 2 && rimPose; s++) {
                        int A = s ? j.parent : j.bone, B = s ? j.bone : j.parent;
                        if (j.rimV[s].empty() || !m.pose[A].visible || !m.pose[B].visible) continue;
                        const std::vector<SeamPt> &wa = wvOf(A), &wb = wvOf(B);
                        const SeamMesh &mb = seam_mesh(m.bones[B].mesh);
                        for (int i : j.rimV[s]) {
                            float best = 1e30f;
                            for (size_t t = 0; t < mb.tris.size(); t++) {
                                SeamPt c = seam_closest(wa[i], wb[mb.tris[t][0]], wb[mb.tris[t][1]], wb[mb.tris[t][2]]);
                                float d = seam_dist(wa[i], c);
                                if (d < best) best = d;
                            }
                            int in = seam_inside(wa[i], mb.tris, wb); /* hidden inside the other piece: no exposure */
                            if (in == 3) continue;
                            if (in == 0 && best / D > j.rimExp[s]) j.rimExp[s] = best / D, j.rimMotion = (int)mo, j.rimKey = k;
                            if (in != 0) j.rimAmb[s] = std::max(j.rimAmb[s], best / D);
                        }
                    }
                    if (m.pose[j.bone].visible && m.pose[j.parent].visible) {
                        const auto &a = boxOf(j.bone), &c = boxOf(j.parent);
                        float s2 = 0;
                        for (int i = 0; i < 3; i++) {
                            float g = std::max(0.0f, std::max(a[i] - c[3 + i], c[i] - a[3 + i]));
                            s2 += g * g;
                        }
                        float s = sqrtf(s2) / D;
                        j.poses++, j.apart += s > 0.01f;
                        if (s > j.maxSep) j.maxSep = s, j.sepMotion = (int)mo, j.sepKey = k;
                    }
                    if (!m.pose[j.bone].visible || !m.pose[j.parent].visible) continue;
                    for (auto &pr : j.pv) {
                        float d = seam_dist(placed(j.bone, pr.first), placed(j.parent, pr.second));
                        j.drift = std::max(j.drift, d / D);
                    }
                }
            }
        }
        if (!blends.empty()) printf("%04d %-13s blended: %d joint(s); a vertex moves at most %.2f%% of the model's size from where its bone alone puts it (motion %d key %d)\n", e.fid,
                                    e.name.c_str(), (int)blends.size(), blendMove / D * 100, moveMo, moveKey);
        for (SeamJoint &j : js) all.push_back(std::move(j));
    }
    for (const SeamJoint &j : all)
        printf("%04d %-13s b%-3d <- b%-3d %-4s pairs %3d/%-3d cover %3.0f%% gap %5.2f%% drift %6.2f%% normals %5.1f deg  apart %3d%% of poses, up to %5.1f%% (motion %d key %d)  rim child %2d: %5.2f%% parent %2d: %5.2f%% (motion %d key %d) unsure %4.2f%% %4.2f%% blend %d\n", j.fid,
               j.model.c_str(), j.bone, j.parent, j.cls, j.pairs, j.verts, j.cover * 100, j.minGap * 100, j.drift * 100, j.normalDeg,
               j.poses ? j.apart * 100 / j.poses : 0, j.maxSep * 100, j.sepMotion, j.sepKey, (int)j.rimV[0].size(), j.rimExp[0] * 100,
               (int)j.rimV[1].size(), j.rimExp[1] * 100, j.rimMotion, j.rimKey, j.rimAmb[0] * 100, j.rimAmb[1] * 100, j.blendMeet);
    /* the summary */
    const char *names[] = {"COIN", "OVER", "GAP", "FAR"};
    printf("\n%d models (%d skipped), %d joints\n", models, skipped, (int)all.size());
    for (const char *c : names) {
        std::vector<float> d;
        double ang = 0;
        for (const SeamJoint &j : all)
            if (!strcmp(j.cls, c)) d.push_back(j.drift), ang += j.normalDeg;
        if (d.empty()) continue;
        std::sort(d.begin(), d.end());
        auto q = [&](float f) { return d[(size_t)(f * (d.size() - 1))] * 100; };
        int o2 = 0, o5 = 0;
        int ap = 0, ap25 = 0;
        for (float x : d) o2 += x > 0.02f, o5 += x > 0.05f;
        for (const SeamJoint &j : all)
            if (!strcmp(j.cls, c)) ap += j.apart > 0, ap25 += j.poses && j.apart * 4 > j.poses;
        printf("%s: %d joints; drift median %.2f%% p90 %.2f%% max %.2f%% of the model's size; over 2%%: %d, over 5%%: %d; normals apart %.1f deg; "
               "boxes ever apart: %d, apart in over a quarter of the poses: %d\n",
               c, (int)d.size(), q(0.5f), q(0.9f), q(1), o2, o5, ang / d.size(), ap, ap25);
    }
    std::vector<const SeamJoint *> worst;
    for (const SeamJoint &j : all)
        if (!strcmp(j.cls, "COIN")) worst.push_back(&j);
    std::sort(worst.begin(), worst.end(), [](const SeamJoint *a, const SeamJoint *b) { return a->drift > b->drift; });
    printf("\nthe 25 coincident joints that open the most:\n");
    for (size_t i = 0; i < worst.size() && i < 25; i++)
        printf("  %04d %-13s b%d <- b%d drift %.2f%% cover %.0f%%\n", worst[i]->fid, worst[i]->model.c_str(), worst[i]->bone, worst[i]->parent,
               worst[i]->drift * 100, worst[i]->cover * 100);
    /* the open rims that end up outside the other piece's surface */
    std::vector<const SeamJoint *> rims;
    int r1 = 0, r3 = 0, r6 = 0, withRim = 0;
    for (const SeamJoint &j : all) {
        float e = std::max(j.rimExp[0], j.rimExp[1]);
        withRim += !j.rimV[0].empty() || !j.rimV[1].empty();
        r1 += e > 0.01f, r3 += e > 0.03f, r6 += e > 0.06f;
        if (e > 0.01f) rims.push_back(&j);
    }
    std::sort(rims.begin(), rims.end(), [](const SeamJoint *a, const SeamJoint *b) {
        return std::max(a->rimExp[0], a->rimExp[1]) > std::max(b->rimExp[0], b->rimExp[1]);
    });
    printf("\nopen rims near the other piece at rest: %d joints; outside its surface by over 1%% of the model: %d, over 3%%: %d, over 6%%: %d\n",
           withRim, r1, r3, r6);
    for (size_t i = 0; i < rims.size() && i < 40; i++)
        printf("  %04d %-13s b%d <- b%d  child's rim %.2f%%  parent's rim %.2f%%  (motion %d key %d)\n", rims[i]->fid, rims[i]->model.c_str(),
               rims[i]->bone, rims[i]->parent, rims[i]->rimExp[0] * 100, rims[i]->rimExp[1] * 100, rims[i]->rimMotion, rims[i]->rimKey);
    return 0;
}
