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

struct SeamJoint {
    int fid = 0, bone = 0, parent = 0;
    std::string model, who; /* who: the fighter's name, empty for a model no fighter uses (an effect) */
    bool enemy = false;
    const char *cls = "";
    int pairs = 0, verts = 0;
    float cover = 0, minGap = 0, drift = 0, normalDeg = 0, maxSep = 0;
    int poses = 0, apart = 0, sepMotion = 0, sepKey = 0; /* poses seen, and those where the two pieces' boxes are 1% of the model apart */
    std::vector<std::pair<int, int>> pv; /* child vertex, parent vertex */
    /* the drift's spread within one motion (its sampled keyframes): the worst motion's p95, and the pair that opened the most */
    float p95 = 0, worstD = 0;
    int p95Motion = 0, worstMotion = 0, worstKey = 0;
    std::pair<int, int> worstPair = {-1, -1};
    std::vector<float> cur; /* this motion's per-keyframe drift (the pairs' farthest) */
};

static int check_seams() {
    std::vector<SeamJoint> all;
    int models = 0, skipped = 0;
    for (const Entry &e : scan_models()) {
        std::vector<uint8_t> bytes;
        read_file(fid_path(e.fid), bytes);
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
        float eps = D * 0.01f, closeD = D * 0.06f;
        std::vector<SeamJoint> js;
        for (size_t b = 1; b < nb; b++) {
            int p = m.bones[b].parent;
            if (p <= 0 || rest[b].empty() || rest[p].empty()) continue;
            SeamJoint j;
            j.fid = e.fid, j.model = e.name, j.who = fs.empty() ? "" : fs[0]->name, j.enemy = !fs.empty() && fs[0]->enemy, j.bone = (int)b, j.parent = p, j.verts = (int)rest[b].size();
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
            j.minGap /= D;
            js.push_back(std::move(j));
        }
        /* every motion's poses (at most ~30 keyframes of each) */
        for (size_t mo = 1; mo <= m.motions.size(); mo++) {
            m.setMotion((int)mo);
            for (SeamJoint &j : js) j.cur.clear();
            int n = m.keyframeCount, step = std::max(1, n / 30);
            for (int k = 0; k < std::max(n, 1); k += step) {
                m.keyframe = k;
                m.evaluate();
                std::vector<std::array<float, 6>> pb(nb);
                std::vector<char> have(nb, 0);
                auto boxOf = [&](int b) -> const std::array<float, 6> & {
                    if (have[b]) return pb[b];
                    have[b] = 1;
                    std::array<float, 6> bx = {1e9f, 1e9f, 1e9f, -1e9f, -1e9f, -1e9f};
                    for (const Vec3s &v : m.meshes[m.bones[b].mesh].verts) {
                        SeamPt q = seam_tf(m.pose[b].world, v);
                        float c[3] = {q.x, q.y, q.z};
                        for (int i = 0; i < 3; i++) bx[i] = std::min(bx[i], c[i]), bx[3 + i] = std::max(bx[3 + i], c[i]);
                    }
                    return pb[b] = bx;
                };
                for (SeamJoint &j : js) {
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
                    const Mesh &cm = m.meshes[m.bones[j.bone].mesh], &pm = m.meshes[m.bones[j.parent].mesh];
                    float fm = 0;
                    for (auto &pr : j.pv) {
                        float d = seam_dist(seam_tf(m.pose[j.bone].world, cm.verts[pr.first]), seam_tf(m.pose[j.parent].world, pm.verts[pr.second])) / D;
                        j.drift = std::max(j.drift, d);
                        fm = std::max(fm, d);
                        if (d > j.worstD) j.worstD = d, j.worstPair = pr, j.worstMotion = (int)mo, j.worstKey = k;
                    }
                    j.cur.push_back(fm);
                }
            }
            for (SeamJoint &j : js) {
                if (j.cur.empty()) continue;
                std::sort(j.cur.begin(), j.cur.end());
                float p = j.cur[(size_t)(0.95f * (j.cur.size() - 1))];
                if (p > j.p95) j.p95 = p, j.p95Motion = (int)mo;
            }
        }
        for (SeamJoint &j : js) all.push_back(std::move(j));
    }
    for (const SeamJoint &j : all)
        printf("%04d %-13s b%-3d <- b%-3d %-4s pairs %3d/%-3d cover %3.0f%% gap %5.2f%% drift %6.2f%% p95 %6.2f%% (motion %d) worst pair c%d/p%d (motion %d key %d) normals %5.1f deg  apart %3d%% of poses, up to %5.1f%% (motion %d key %d)\n", j.fid,
               j.model.c_str(), j.bone, j.parent, j.cls, j.pairs, j.verts, j.cover * 100, j.minGap * 100, j.drift * 100, j.p95 * 100, j.p95Motion, j.worstPair.first,
               j.worstPair.second, j.worstMotion, j.worstKey, j.normalDeg,
               j.poses ? j.apart * 100 / j.poses : 0, j.maxSep * 100, j.sepMotion, j.sepKey);
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
        std::vector<float> p;
        for (const SeamJoint &j : all)
            if (!strcmp(j.cls, c)) p.push_back(j.p95);
        std::sort(p.begin(), p.end());
        printf("%s: per-motion p95 of the drift (worst motion of each joint): median %.2f%% p90 %.2f%% max %.2f%%\n", c, p[p.size() / 2] * 100,
               p[(size_t)(0.9f * (p.size() - 1))] * 100, p.back() * 100);
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
    /* by the p95 within a motion: a joint that stays open, against one that opens for a frame */
    for (const char *c : {"COIN", "OVER"}) {
        worst.clear();
        for (const SeamJoint &j : all)
            if (!strcmp(j.cls, c)) worst.push_back(&j);
        std::sort(worst.begin(), worst.end(), [](const SeamJoint *a, const SeamJoint *b) { return a->p95 > b->p95; });
        printf("\nthe 25 %s joints with the highest per-motion p95:\n", c);
        for (size_t i = 0; i < worst.size() && i < 25; i++)
            printf("  %04d %-13s b%d <- b%d p95 %.2f%% (motion %d) max %.2f%% (motion %d key %d, pair c%d/p%d)\n", worst[i]->fid, worst[i]->model.c_str(),
                   worst[i]->bone, worst[i]->parent, worst[i]->p95 * 100, worst[i]->p95Motion, worst[i]->worstD * 100, worst[i]->worstMotion,
                   worst[i]->worstKey, worst[i]->worstPair.first, worst[i]->worstPair.second);
    }
    /* the fighters alone (partners and enemies; not effects or props): a model's joints against the other models', so the
       ones with the most joints do not decide the whole */
    for (const char *c : {"COIN", "OVER"}) {
        std::map<int, std::vector<const SeamJoint *>> by;
        for (const SeamJoint &j : all)
            if (!j.who.empty() && !strcmp(j.cls, c)) by[j.fid].push_back(&j);
        std::vector<float> med, hi5;
        int joints = 0, over5 = 0;
        printf("\nFIGHTERS, %s joints, per model: n / median p95 / p90 / max / joints over 5%%\n", c);
        for (auto &kv : by) {
            std::vector<float> p;
            for (const SeamJoint *j : kv.second) p.push_back(j->p95);
            std::sort(p.begin(), p.end());
            int o5 = 0;
            for (float x : p) o5 += x > 0.05f;
            const SeamJoint *w = *std::max_element(kv.second.begin(), kv.second.end(), [](const SeamJoint *a, const SeamJoint *b) { return a->p95 < b->p95; });
            printf("  %04d %-13s %-12s %-7s n %3d  median %5.2f%%  p90 %5.2f%%  max %6.2f%%  over5%% %3d  worst b%d<-b%d motion %d key %d pair c%d/p%d\n", kv.first,
                   w->model.c_str(), w->who.c_str(), w->enemy ? "enemy" : "partner", (int)p.size(), p[p.size() / 2] * 100,
                   p[(size_t)(0.9f * (p.size() - 1))] * 100, p.back() * 100, o5, w->bone, w->parent, w->worstMotion, w->worstKey, w->worstPair.first,
                   w->worstPair.second);
            med.push_back(p[p.size() / 2]), hi5.push_back((float)o5 / p.size());
            joints += (int)p.size(), over5 += o5;
        }
        if (med.empty()) continue;
        std::sort(med.begin(), med.end());
        std::sort(hi5.begin(), hi5.end());
        printf("FIGHTERS %s: %d models, %d joints; the models' median p95: median %.2f%% p90 %.2f%% max %.2f%%; share of a model's joints over 5%%: median %.0f%% p90 %.0f%%; overall %d/%d joints\n",
               c, (int)med.size(), joints, med[med.size() / 2] * 100, med[(size_t)(0.9f * (med.size() - 1))] * 100, med.back() * 100,
               hi5[hi5.size() / 2] * 100, hi5[(size_t)(0.9f * (hi5.size() - 1))] * 100, over5, joints);
    }
    return 0;
}
