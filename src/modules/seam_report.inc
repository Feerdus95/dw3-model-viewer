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

/* One point of a joint's neighbourhood at rest, for the overlap diagnostic: role 0 is a child vertex against the parent's
   nearest vertex (and its normal), role 1 a parent vertex against the child's. rel0 is the pair's offset in the other bone's
   frame at rest, s0 the point's signed distance along the other surface's normal (negative: behind that surface) */
struct SeamReg {
    int role, a, o;
    float rel0[3], s0;
};

/* A mesh's vertex normals: its polygons name normals by corner (Poly::nrm), not by vertex, so a vertex's is the sum of those
   its corners use (zero for a vertex no lit polygon uses) */
static std::vector<SeamPt> seam_vnormals(const Mesh &m) {
    std::vector<SeamPt> v(m.verts.size(), SeamPt{0, 0, 0});
    for (const Poly &p : m.polys) {
        if (!p.lit) continue;
        for (int k = 0; k < (p.quad ? 4 : 3); k++) {
            const Vec3s &n = m.normals[p.nrm[k]];
            v[p.idx[k]].x += n.x, v[p.idx[k]].y += n.y, v[p.idx[k]].z += n.z;
        }
    }
    return v;
}

/* P against Q (a vertex of the other piece, with its normal): their offset in Q's bone frame, and P's signed distance along
   Q's normal. The offset moves only as the two bones do */
static void seam_eval(const float *wp, const Vec3s &pv, const float *wq, const Vec3s &qv, const SeamPt *qn, float rel[3], float *s, float *dist = nullptr) {
    SeamPt a = seam_tf(wp, pv), b = seam_tf(wq, qv);
    float d[3] = {a.x - b.x, a.y - b.y, a.z - b.z};
    for (int j = 0; j < 3; j++) rel[j] = wq[4 * j] * d[0] + wq[4 * j + 1] * d[1] + wq[4 * j + 2] * d[2];
    *s = 0;
    if (dist) *dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (qn) {
        SeamPt n = {wq[0] * qn->x + wq[4] * qn->y + wq[8] * qn->z, wq[1] * qn->x + wq[5] * qn->y + wq[9] * qn->z, wq[2] * qn->x + wq[6] * qn->y + wq[10] * qn->z};
        float l = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
        if (l > 1e-3f) *s = (d[0] * n.x + d[1] * n.y + d[2] * n.z) / l;
    }
}

/* The distance to the other piece's real surface (the overlap diagnostic's second version). A triangle of a mesh, as the game
   draws a polygon (a quad is (0,1,2) and (1,3,2)); flip is the winding that makes its normal point out, judged by the vertex
   normals at rest (0: unknown, so no side can be told) */
struct SeamTri {
    int a, b, c;
    float flip;
};

static std::vector<SeamTri> seam_tris(const Mesh &m, const std::vector<SeamPt> &vn) {
    std::vector<SeamTri> t;
    for (const Poly &p : m.polys) {
        static const int Q[2][3] = {{0, 1, 2}, {1, 3, 2}};
        for (int h = 0; h < (p.quad ? 2 : 1); h++) {
            SeamTri r = {p.idx[Q[h][0]], p.idx[Q[h][1]], p.idx[Q[h][2]], 0};
            const Vec3s &a = m.verts[r.a], &b = m.verts[r.b], &c = m.verts[r.c];
            float u[3] = {(float)(b.x - a.x), (float)(b.y - a.y), (float)(b.z - a.z)}, v[3] = {(float)(c.x - a.x), (float)(c.y - a.y), (float)(c.z - a.z)};
            float n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
            if (n[0] * n[0] + n[1] * n[1] + n[2] * n[2] < 1e-3f) continue; /* degenerate */
            float vs[3] = {vn[r.a].x + vn[r.b].x + vn[r.c].x, vn[r.a].y + vn[r.b].y + vn[r.c].y, vn[r.a].z + vn[r.b].z + vn[r.c].z};
            float d = n[0] * vs[0] + n[1] * vs[1] + n[2] * vs[2];
            r.flip = d > 1e-3f ? 1.0f : d < -1e-3f ? -1.0f : 0.0f;
            t.push_back(r);
        }
    }
    return t;
}

/* The distance from P to triangle abc (Ericson's closest point); *face is 1 when the closest point is inside the face, where
   the triangle's side of P can be trusted, and *side the sign of P along the winding normal */
static float seam_pt_tri(const SeamPt &p, const SeamPt &a, const SeamPt &b, const SeamPt &c, int *face, float *side) {
    auto sub = [](const SeamPt &x, const SeamPt &y) { return SeamPt{x.x - y.x, x.y - y.y, x.z - y.z}; };
    auto dot = [](const SeamPt &x, const SeamPt &y) { return x.x * y.x + x.y * y.y + x.z * y.z; };
    SeamPt ab = sub(b, a), ac = sub(c, a), ap = sub(p, a), q;
    float d1 = dot(ab, ap), d2 = dot(ac, ap), d3, d4, d5, d6, vc, vb, va;
    *face = 0;
    SeamPt n = {ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x};
    auto done = [&](const SeamPt &qq) {
        SeamPt r = sub(p, qq);
        *side = dot(r, n) >= 0 ? 1.0f : -1.0f;
        return sqrtf(dot(r, r));
    };
    if (d1 <= 0 && d2 <= 0) return done(a);
    SeamPt bp = sub(p, b);
    d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return done(b);
    vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) {
        float v = d1 / (d1 - d3);
        return done({a.x + v * ab.x, a.y + v * ab.y, a.z + v * ab.z});
    }
    SeamPt cp = sub(p, c);
    d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return done(c);
    vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
        float w = d2 / (d2 - d6);
        return done({a.x + w * ac.x, a.y + w * ac.y, a.z + w * ac.z});
    }
    va = d3 * d6 - d5 * d4;
    if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return done({b.x + w * (c.x - b.x), b.y + w * (c.y - b.y), b.z + w * (c.z - b.z)});
    }
    float den = 1.0f / (va + vb + vc), v = vb * den, w = vc * den;
    *face = 1;
    return done({a.x + ab.x * v + ac.x * w, a.y + ab.y * v + ac.y * w, a.z + ab.z * v + ac.z * w});
}

/* One side of a joint: points of one piece against a chosen few triangles of the other, at rest and per keyframe */
struct SeamSide {
    std::vector<int> pts, tris; /* vertices of the point piece, triangles of the other */
    std::vector<float> d0, s0;  /* at rest: distance, signed distance (negative: behind the surface) */
    std::vector<char> ok0;      /* whether that side is trusted */
};

/* The points' distance to the triangles' surface now: d the distance, sd the signed one, ok whether its sign is trusted
   (the closest point is inside a face with a known winding) */
static void seam_surface(const float *wp, const Mesh &mp, const float *wq, const Mesh &mq, const std::vector<SeamTri> &tq, const SeamSide &sd, std::vector<float> &d,
                         std::vector<float> &sg, std::vector<char> &ok) {
    std::vector<SeamPt> tv(sd.tris.size() * 3);
    float det = wq[0] * (wq[5] * wq[10] - wq[6] * wq[9]) - wq[4] * (wq[1] * wq[10] - wq[2] * wq[9]) + wq[8] * (wq[1] * wq[6] - wq[2] * wq[5]);
    for (size_t i = 0; i < sd.tris.size(); i++) {
        const SeamTri &t = tq[sd.tris[i]];
        tv[i * 3] = seam_tf(wq, mq.verts[t.a]), tv[i * 3 + 1] = seam_tf(wq, mq.verts[t.b]), tv[i * 3 + 2] = seam_tf(wq, mq.verts[t.c]);
    }
    d.assign(sd.pts.size(), 1e30f), sg.assign(sd.pts.size(), 1e30f), ok.assign(sd.pts.size(), 0);
    for (size_t k = 0; k < sd.pts.size(); k++) {
        SeamPt P = seam_tf(wp, mp.verts[sd.pts[k]]);
        for (size_t i = 0; i < sd.tris.size(); i++) {
            int face;
            float side, dist = seam_pt_tri(P, tv[i * 3], tv[i * 3 + 1], tv[i * 3 + 2], &face, &side);
            if (dist >= d[k]) continue;
            float f = tq[sd.tris[i]].flip * (det < 0 ? -1.0f : 1.0f);
            d[k] = dist, ok[k] = face && f != 0, sg[k] = side * f * dist;
        }
    }
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
    /* the overlap diagnostic, over the neighbourhood's points and every sampled keyframe, as shares of the model's size: how far
       a pair moves from its rest offset (rel), how much deeper than at rest a point sinks behind the other surface (pen; penAbs:
       in all), how far a point that was behind the other surface at rest comes out in front of it (exp); pen and exp count only the pairs still within the neighbourhood's radius (6% of the model) at that keyframe */
    std::vector<SeamReg> reg;
    bool hasN = false;
    float relMax = 0, penMax = 0, penAbs = 0, expMax = 0;
    int relM = 0, relK = 0, penM = 0, penK = 0, expM = 0, expK = 0, covered = 0;
    /* the same against the real surface (OVER joints of fighters): how much deeper a point whose side is trusted sinks behind
       the other piece's surface than at rest (tpen), how far from the surface a point that touched it at rest gets (tgap) */
    SeamSide side[2];
    bool surf = false;
    float tpenMax = 0, tpenAbs = 0, tgapMax = 0;
    int tpenM = 0, tpenK = 0, tgapM = 0, tgapK = 0, touching = 0, samples = 0, trusted = 0;
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
        std::vector<std::vector<SeamPt>> rest(nb), vn(nb);
        std::vector<std::vector<SeamTri>> tr(nb);
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
            vn[b] = seam_vnormals(m.meshes[m.bones[b].mesh]);
            tr[b] = seam_tris(m.meshes[m.bones[b].mesh], vn[b]);
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
            /* the neighbourhood: child points near the parent and parent points near the child, 64 of each at most */
            std::vector<std::tuple<float, int, int>> back; /* distance, parent vertex, its nearest child vertex */
            {
                j.hasN = !vn[b].empty() && !vn[p].empty();
                for (size_t k = 0; k < rest[p].size(); k++) {
                    float best = 1e30f;
                    int bi = 0;
                    for (size_t i = 0; i < rest[b].size(); i++) {
                        float d = seam_dist(rest[p][k], rest[b][i]);
                        if (d < best) best = d, bi = (int)i;
                    }
                    back.emplace_back(best, (int)k, bi);
                }
                std::sort(back.begin(), back.end());
                auto add = [&](int role, int a, int o) {
                    SeamReg r;
                    r.role = role, r.a = a, r.o = o;
                    if (role == 0) seam_eval(m.pose[b].world, cm.verts[a], m.pose[p].world, pm.verts[o], j.hasN ? &vn[p][o] : nullptr, r.rel0, &r.s0);
                    else seam_eval(m.pose[p].world, pm.verts[a], m.pose[b].world, cm.verts[o], j.hasN ? &vn[b][o] : nullptr, r.rel0, &r.s0);
                    j.covered += r.s0 < 0;
                    j.reg.push_back(r);
                };
                int n0 = 0, n1 = 0;
                for (auto &t : near_pairs)
                    if (std::get<0>(t) <= closeD && n0++ < 64) add(0, std::get<1>(t), std::get<2>(t));
                for (auto &t : back)
                    if (std::get<0>(t) <= closeD && n1++ < 64) add(1, std::get<1>(t), std::get<2>(t));
            }
            /* the surface diagnostic: 16 points a side, the 32 nearest triangles of the other piece (as they do not change, found once) */
            if (!j.who.empty() && !strcmp(j.cls, "OVER") && j.hasN) {
                j.surf = true;
                for (int sd = 0; sd < 2; sd++) {
                    SeamSide &S = j.side[sd];
                    int po = sd ? p : b, qo = sd ? b : p; /* the point piece's bone, the other's */
                    if (sd == 0) {
                        for (auto &t : near_pairs)
                            if (std::get<0>(t) <= closeD && S.pts.size() < 16) S.pts.push_back(std::get<1>(t));
                    } else {
                        for (auto &t : back)
                            if (std::get<0>(t) <= closeD && S.pts.size() < 16) S.pts.push_back(std::get<1>(t));
                    }
                    std::vector<std::pair<float, int>> td;
                    for (size_t i = 0; i < tr[qo].size(); i++) {
                        float best = 1e30f;
                        for (int pt : S.pts) {
                            int face;
                            float side, d = seam_pt_tri(rest[po][pt], rest[qo][tr[qo][i].a], rest[qo][tr[qo][i].b], rest[qo][tr[qo][i].c], &face, &side);
                            best = std::min(best, d);
                        }
                        td.emplace_back(best, (int)i);
                    }
                    std::sort(td.begin(), td.end());
                    for (size_t i = 0; i < td.size() && i < 32; i++) S.tris.push_back(td[i].second);
                    const Mesh &mp = m.meshes[m.bones[po].mesh], &mq = m.meshes[m.bones[qo].mesh];
                    if (!S.pts.empty() && !S.tris.empty()) seam_surface(m.pose[po].world, mp, m.pose[qo].world, mq, tr[qo], S, S.d0, S.s0, S.ok0);
                    for (size_t i = 0; i < S.d0.size(); i++) j.touching += S.d0[i] <= eps;
                }
            }
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
                    if (j.surf && (k / step) % 3 == 0) { /* every third sampled keyframe: the triangles make it dear */
                        for (int sd = 0; sd < 2; sd++) {
                            const SeamSide &S = j.side[sd];
                            int po = sd ? j.parent : j.bone, qo = sd ? j.bone : j.parent;
                            if (S.pts.empty() || S.tris.empty()) continue;
                            std::vector<float> d, sg;
                            std::vector<char> ok;
                            seam_surface(m.pose[po].world, m.meshes[m.bones[po].mesh], m.pose[qo].world, m.meshes[m.bones[qo].mesh], tr[qo], S, d, sg, ok);
                            for (size_t i = 0; i < d.size(); i++) {
                                j.samples++;
                                if (S.d0[i] <= eps) {
                                    float gp = (d[i] - S.d0[i]) / D;
                                    if (gp > j.tgapMax) j.tgapMax = gp, j.tgapM = (int)mo, j.tgapK = k;
                                }
                                if (!ok[i] || !S.ok0[i]) continue;
                                j.trusted++;
                                float dep = std::max(0.0f, -sg[i]) / D, dep0 = std::max(0.0f, -S.s0[i]) / D;
                                j.tpenAbs = std::max(j.tpenAbs, dep);
                                if (dep - dep0 > j.tpenMax) j.tpenMax = dep - dep0, j.tpenM = (int)mo, j.tpenK = k;
                            }
                        }
                    }
                    for (const SeamReg &r : j.reg) {
                        float rel[3], sv, dist;
                        if (r.role == 0) seam_eval(m.pose[j.bone].world, cm.verts[r.a], m.pose[j.parent].world, pm.verts[r.o], j.hasN ? &vn[j.parent][r.o] : nullptr, rel, &sv, &dist);
                        else seam_eval(m.pose[j.parent].world, pm.verts[r.a], m.pose[j.bone].world, cm.verts[r.o], j.hasN ? &vn[j.bone][r.o] : nullptr, rel, &sv, &dist);
                        float dr = sqrtf((rel[0] - r.rel0[0]) * (rel[0] - r.rel0[0]) + (rel[1] - r.rel0[1]) * (rel[1] - r.rel0[1]) + (rel[2] - r.rel0[2]) * (rel[2] - r.rel0[2])) / D;
                        if (dr > j.relMax) j.relMax = dr, j.relM = (int)mo, j.relK = k;
                        if (!j.hasN || dist > closeD) continue; /* the surface test holds only while the two points are still near */
                        float pen = std::max(0.0f, -sv), pen0 = std::max(0.0f, -r.s0), dp = std::max(0.0f, pen - pen0) / D;
                        j.penAbs = std::max(j.penAbs, pen / D);
                        if (dp > j.penMax) j.penMax = dp, j.penM = (int)mo, j.penK = k;
                        float e = r.s0 < 0 ? std::max(0.0f, sv) / D : 0;
                        if (e > j.expMax) j.expMax = e, j.expM = (int)mo, j.expK = k;
                    }
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
    /* the overlap diagnostic, fighters only, per model (each model counts once, whatever its joints): how far the pieces
       around a joint slide (rel), sink deeper into each other than at rest (pen), and uncover what was covered (exp).
       Not the opening above: other magnitudes. A joint's worst is over its neighbourhood's points and sampled keyframes */
    for (const char *c : {"COIN", "OVER"}) {
        std::map<int, std::vector<const SeamJoint *>> by;
        std::vector<const SeamJoint *> flat;
        for (const SeamJoint &j : all)
            if (!j.who.empty() && !strcmp(j.cls, c) && j.hasN && !j.reg.empty()) by[j.fid].push_back(&j), flat.push_back(&j);
        auto med = [](std::vector<float> v) {
            std::sort(v.begin(), v.end());
            return v.empty() ? 0.0f : v[v.size() / 2];
        };
        printf("\nOVERLAP, FIGHTERS, %s joints, per model: n / median over joints of rel, pen, exp (worst per joint) / the worst joint of each\n", c);
        std::vector<float> mr, mp, me;
        for (auto &kv : by) {
            std::vector<float> r, pe, ex;
            const SeamJoint *wp = kv.second[0], *we = kv.second[0];
            for (const SeamJoint *j : kv.second) {
                r.push_back(j->relMax), pe.push_back(j->penMax), ex.push_back(j->expMax);
                if (j->penMax > wp->penMax) wp = j;
                if (j->expMax > we->expMax) we = j;
            }
            mr.push_back(med(r)), mp.push_back(med(pe)), me.push_back(med(ex));
            printf("  %04d %-13s %-12s n %3d  rel %5.2f%%  pen %5.2f%%  exp %5.2f%%  worst pen %5.2f%% b%d<-b%d m%d k%d  worst exp %5.2f%% b%d<-b%d m%d k%d\n", kv.first,
                   kv.second[0]->model.c_str(), kv.second[0]->who.c_str(), (int)kv.second.size(), mr.back() * 100, mp.back() * 100, me.back() * 100, wp->penMax * 100,
                   wp->bone, wp->parent, wp->penM, wp->penK, we->expMax * 100, we->bone, we->parent, we->expM, we->expK);
        }
        if (mr.empty()) continue;
        auto q = [](std::vector<float> v, float f) {
            std::sort(v.begin(), v.end());
            return v[(size_t)(f * (v.size() - 1))] * 100;
        };
        printf("OVERLAP FIGHTERS %s: %d models, %d joints; the models' median rel %.2f%% (p90 %.2f%%), pen %.2f%% (p90 %.2f%%), exp %.2f%% (p90 %.2f%%)\n", c,
               (int)mr.size(), (int)flat.size(), q(mr, 0.5f), q(mr, 0.9f), q(mp, 0.5f), q(mp, 0.9f), q(me, 0.5f), q(me, 0.9f));
        for (int what = 0; what < 2; what++) {
            std::sort(flat.begin(), flat.end(), [&](const SeamJoint *a, const SeamJoint *b) { return what ? a->expMax > b->expMax : a->penMax > b->penMax; });
            printf("the 10 %s fighter joints with the highest %s:\n", c, what ? "exposure" : "added penetration");
            for (size_t i = 0; i < flat.size() && i < 10; i++)
                printf("  %04d %-13s %-12s b%d <- b%d %s %.2f%% (motion %d key %d)\n", flat[i]->fid, flat[i]->model.c_str(), flat[i]->who.c_str(), flat[i]->bone,
                       flat[i]->parent, what ? "exp" : "pen", (what ? flat[i]->expMax : flat[i]->penMax) * 100, what ? flat[i]->expM : flat[i]->penM,
                       what ? flat[i]->expK : flat[i]->penK);
        }
    }
    /* the same, against the real surface: OVER joints of fighters, per model. tpen: added depth of points whose side is trusted;
       tgap: how far a point that touched the other surface at rest gets from it. A point's trusted side needs its closest point
       inside a face with a known winding; the rest only has a distance. Rank agreement with the plane test above is printed too */
    {
        std::vector<const SeamJoint *> flat;
        std::map<int, std::vector<const SeamJoint *>> by;
        for (const SeamJoint &j : all)
            if (j.surf && j.samples) flat.push_back(&j), by[j.fid].push_back(&j);
        auto med = [](std::vector<float> v) {
            std::sort(v.begin(), v.end());
            return v.empty() ? 0.0f : v[v.size() / 2];
        };
        printf("\nSURFACE, FIGHTERS, OVER joints, per model: n / median over joints of tpen, tgap / the worst joint of each / share of samples with a trusted side\n");
        std::vector<float> mp, mg;
        long smp = 0, tru = 0, ok0 = 0, in0 = 0, tot0 = 0;
        for (auto &kv : by) {
            std::vector<float> a, g;
            const SeamJoint *wp = kv.second[0], *wg = kv.second[0];
            for (const SeamJoint *j : kv.second) {
                a.push_back(j->tpenMax), g.push_back(j->tgapMax);
                if (j->tpenMax > wp->tpenMax) wp = j;
                if (j->tgapMax > wg->tgapMax) wg = j;
                smp += j->samples, tru += j->trusted;
                for (const SeamSide &S : j->side)
                    for (size_t i = 0; i < S.d0.size(); i++) tot0++, ok0 += S.ok0[i], in0 += S.ok0[i] && S.s0[i] < 0;
            }
            mp.push_back(med(a)), mg.push_back(med(g));
            printf("  %04d %-13s %-12s n %3d  tpen %5.2f%%  tgap %5.2f%%  worst tpen %5.2f%% b%d<-b%d m%d k%d  worst tgap %5.2f%% b%d<-b%d m%d k%d\n", kv.first,
                   kv.second[0]->model.c_str(), kv.second[0]->who.c_str(), (int)kv.second.size(), mp.back() * 100, mg.back() * 100, wp->tpenMax * 100, wp->bone,
                   wp->parent, wp->tpenM, wp->tpenK, wg->tgapMax * 100, wg->bone, wg->parent, wg->tgapM, wg->tgapK);
        }
        if (!mp.empty()) {
            auto q = [](std::vector<float> v, float f) {
                std::sort(v.begin(), v.end());
                return v[(size_t)(f * (v.size() - 1))] * 100;
            };
            printf("SURFACE FIGHTERS OVER: %d models, %d joints; the models' median tpen %.2f%% (p90 %.2f%%), tgap %.2f%% (p90 %.2f%%); trusted side in %.0f%% of %ld samples; at rest %.0f%% of %ld points have a trusted side, %.0f%% of those behind the surface\n",
                   (int)mp.size(), (int)flat.size(), q(mp, 0.5f), q(mp, 0.9f), q(mg, 0.5f), q(mg, 0.9f), smp ? tru * 100.0 / smp : 0.0, smp, tot0 ? ok0 * 100.0 / tot0 : 0.0, tot0, ok0 ? in0 * 100.0 / ok0 : 0.0);
            /* the rank agreement (Spearman) of the new tpen with the plane test's pen and exp, over the joints */
            auto ranks = [&](auto get) {
                std::vector<int> o(flat.size());
                for (size_t i = 0; i < o.size(); i++) o[i] = (int)i;
                std::sort(o.begin(), o.end(), [&](int a, int b) { return get(flat[a]) < get(flat[b]); });
                std::vector<double> r(flat.size());
                for (size_t i = 0; i < o.size(); i++) r[o[i]] = (double)i;
                return r;
            };
            auto sp = [&](const std::vector<double> &a, const std::vector<double> &b) {
                double n = (double)a.size(), ma = (n - 1) / 2, sab = 0, saa = 0, sbb = 0;
                for (size_t i = 0; i < a.size(); i++) sab += (a[i] - ma) * (b[i] - ma), saa += (a[i] - ma) * (a[i] - ma), sbb += (b[i] - ma) * (b[i] - ma);
                return sab / sqrt(saa * sbb);
            };
            auto rt = ranks([](const SeamJoint *j) { return j->tpenMax; }), rp = ranks([](const SeamJoint *j) { return j->penMax; }),
                 re = ranks([](const SeamJoint *j) { return j->expMax; }), rg = ranks([](const SeamJoint *j) { return j->tgapMax; });
            printf("rank agreement (Spearman) over %d OVER fighter joints: tpen vs plane pen %.2f, tpen vs plane exp %.2f, tgap vs plane exp %.2f, tpen vs tgap %.2f\n",
                   (int)flat.size(), sp(rt, rp), sp(rt, re), sp(rg, re), sp(rt, rg));
        }
        for (int what = 0; what < 2; what++) {
            std::sort(flat.begin(), flat.end(), [&](const SeamJoint *a, const SeamJoint *b) { return what ? a->tgapMax > b->tgapMax : a->tpenMax > b->tpenMax; });
            printf("the 10 OVER fighter joints with the highest %s:\n", what ? "tgap" : "tpen");
            for (size_t i = 0; i < flat.size() && i < 10; i++)
                printf("  %04d %-13s %-12s b%d <- b%d %s %.2f%% (motion %d key %d)\n", flat[i]->fid, flat[i]->model.c_str(), flat[i]->who.c_str(), flat[i]->bone,
                       flat[i]->parent, what ? "tgap" : "tpen", (what ? flat[i]->tgapMax : flat[i]->tpenMax) * 100, what ? flat[i]->tgapM : flat[i]->tpenM,
                       what ? flat[i]->tgapK : flat[i]->tpenK);
        }
    }
    return 0;
}
