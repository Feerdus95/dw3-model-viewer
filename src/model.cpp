/* The battle model's reading and posing: a port of what FIGHTSTG does with
   its files (dw3_decomp src/fightstg/fightstg.c, src/main/gfx/tim_loader.c;
   MIT, see CREDITS.md) */
#include "model.h"
#include <cmath>
#include <cstring>

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static int16_t rd16(const uint8_t *p) { int16_t v; memcpy(&v, p, 2); return v; }

int Archive::count() const {
    return size < 4 ? 0 : (int)(rd32(data) / 4);
}

const uint8_t *Archive::entry(int i) const {
    if (i < 0 || (size_t)(i + 1) * 4 > size) return nullptr;
    uint32_t off = rd32(data + i * 4);
    return off < size ? data + off : nullptr;
}

size_t Archive::entry_size(int i) const {
    const uint8_t *e = entry(i);
    if (!e) return 0;
    size_t end = size;
    for (int j = 0; j < count(); j++) { /* the nearest offset past it: entries need not be in order */
        uint32_t off = rd32(data + j * 4);
        if (off > (size_t)(e - data) && off < end) end = off;
    }
    return end - (e - data);
}

/* An archive inside an entry */
static Archive sub(const Archive &a, int i) {
    Archive s;
    s.data = a.entry(i);
    s.size = s.data ? a.entry_size(i) : 0;
    return s;
}

int find_bone_entry(const Archive &a) {
    int n = a.count();
    for (int i = 0; i < n; i++) {
        const uint8_t *e = a.entry(i);
        size_t size = a.entry_size(i);
        if (!e || size < 8) continue;
        uint32_t tex = rd32(e), cnt = rd32(e + 4);
        if (tex >= (uint32_t)n || cnt == 0 || cnt > 255 || 8 + 12 * cnt > size) continue;
        bool ok = true;
        for (uint32_t b = 0; b < cnt && ok; b++) {
            uint32_t parent = rd32(e + 8 + b * 12), mesh = rd32(e + 12 + b * 12), keys = rd32(e + 16 + b * 12);
            const uint8_t *me = a.entry(mesh), *ke = a.entry(keys);
            ok = parent <= b && mesh < (uint32_t)n && keys < (uint32_t)n && me && ke && rd32(me) == 0x18 &&
                 (rd32(ke) == 0x10 || rd32(ke) == 0x0C);
        }
        if (ok) return i;
    }
    return -1;
}

/* The count in [0].x, then the vectors (FIGHTSTG_projectMesh, lightMesh) */
static bool read_vecs(const Archive &a, int i, std::vector<Vec3s> &out) {
    const uint8_t *p = a.entry(i);
    size_t size = a.entry_size(i);
    if (!p || size < 6) return false;
    int n = rd16(p);
    if (n < 0 || (size_t)(n + 1) * 6 > size) return false;
    out.resize(n);
    for (int k = 0; k < n; k++) out[k] = {rd16(p + 6 + k * 6), rd16(p + 8 + k * 6), rd16(p + 10 + k * 6)};
    return true;
}

/* FIGHTSTG_drawMesh's walk through the command bytes, keeping every polygon */
static bool read_commands(const uint8_t *cmd, const uint8_t *end, int texX, int texY, Mesh &mesh, std::string &err) {
    bool quad = false, textured = false, lit = false, gouraud = false;
    int abr = 0, u = 0, v = 0;
    uint16_t tpage = 0, clut = 0;
    uint8_t color[4][3] = {};

    while (cmd < end && *cmd != 0xFF) {
        int hi = *cmd >> 4, lo = *cmd & 15;
        if (hi != 0) {
            switch (hi) {
            case 8: quad = lo; break;
            case 9: textured = lo; break;
            case 12: lit = lo; break;
            case 13: gouraud = lo; break;
            case 14: abr = lo; break;
            }
            cmd++;
        } else if (lo == 1) { /* setMeshTexture */
            if (end - cmd < 7) break;
            u = cmd[1] + (cmd[2] << 8);
            v = cmd[3];
            int cx = cmd[4] + texX, cy = cmd[5] + texY, tp = cmd[6];
            int px = texX + (cmd[2] << 6), py = texY, a = abr ? abr - 1 : 0;
            clut = (uint16_t)((cy << 6) | ((cx >> 4) & 0x3F));
            tpage = (uint16_t)(((tp & 3) << 7) | ((a & 3) << 5) | ((py & 0x100) >> 4) | ((px & 0x3FF) >> 6));
            cmd += 7;
        } else if (lo >= 2 && lo <= 5) {
            if (end - cmd < 4) break;
            memcpy(color[lo - 2], cmd + 1, 3);
            cmd += 4;
        } else if (lo == 0) { /* drawMeshPolygons */
            do {
                cmd++;
                int n = quad ? 4 : 3;
                int len = n + (lit ? n : 0) + (textured ? n * 2 : 0);
                if (end - cmd < len + 1) {
                    err = "commands run past their entry";
                    return false;
                }
                Poly p = {};
                p.quad = quad, p.textured = textured, p.lit = lit, p.gouraud = gouraud, p.abr = abr;
                p.tpage = tpage, p.clut = clut;
                memcpy(p.color, color[0], 3);
                for (int k = 0; k < n; k++) {
                    p.idx[k] = cmd[k];
                    if (lit) p.nrm[k] = cmd[n + k];
                    if (textured) {
                        const uint8_t *q = cmd + (lit ? 2 * n : n);
                        p.uv[k][0] = (uint8_t)(q[k * 2] + u);
                        p.uv[k][1] = (uint8_t)(q[k * 2 + 1] + v);
                    }
                }
                for (int k = 0; k < n; k++) {
                    if (p.idx[k] >= (int)mesh.verts.size() || (lit && p.nrm[k] >= (int)mesh.normals.size())) {
                        err = "a polygon's index is past its mesh";
                        return false;
                    }
                }
                mesh.polys.push_back(p);
                cmd += len;
            } while (cmd < end && *cmd == 0);
        } else {
            err = "unknown command byte";
            return false;
        }
    }
    return true;
}

/* The TIMs of the texture archive, side by side 0x40 halfwords apart
   (timLoaderLoadArchive); the image FIGHTSTG gives the model starts at
   texX, texY */
void read_tims(const Archive &a, int texX, int texY, std::vector<Tim> &out) {
    for (int i = 0; i < a.count(); i++) {
        if (rd32(a.data + i * 4) == 0) break; /* the table ends with an offset of 0 */
        const uint8_t *src = a.entry(i);
        if (!src) break;
        const uint8_t *end = a.data + a.size;
        std::vector<uint8_t> buf;
        bool rlen = end - src >= 8 && memcmp(src, "RLEN", 4) == 0;
        if (rlen) {
            const uint8_t *s = src + 8;
            while (s < end && *s) {
                int c = *s;
                if (c & 0x80) {
                    if (s + 1 >= end) break;
                    buf.insert(buf.end(), c & 0x7F, s[1]);
                    s += 2;
                } else {
                    if (s + 1 + c > end) break;
                    buf.insert(buf.end(), s + 1, s + 1 + c);
                    s += 1 + c;
                }
            }
        } else {
            buf.assign(src, end);
        }
        /* the TIM: id, flag, then the CLUT's block (when it has one) and the image's */
        if (buf.size() < 20 || rd32(buf.data()) != 0x10) continue;
        size_t at = 8;
        if (rd32(buf.data() + 4) & 8) at += rd32(buf.data() + 8);
        if (at + 12 > buf.size()) continue;
        Tim t;
        t.x = texX + i * 0x40, t.y = texY, t.rlen = rlen;
        t.w = (uint16_t)rd16(buf.data() + at + 8);
        t.h = (uint16_t)rd16(buf.data() + at + 10);
        size_t px = (size_t)t.w * t.h;
        if (at + 12 + px * 2 > buf.size()) continue;
        t.pixels.resize(px);
        memcpy(t.pixels.data(), buf.data() + at + 12, px * 2);
        out.push_back(t);
    }
}

/* VRAM, 1024x512 halfwords, with the model's TIMs where FIGHTSTG puts them */
static void place_tims(const std::vector<Tim> &tims, std::vector<uint16_t> &vram) {
    vram.assign(1024 * 512, 0);
    for (const Tim &t : tims)
        for (int y = 0; y < t.h; y++)
            for (int x = 0; x < t.w; x++)
                vram[((t.y + y) & 511) * 1024 + ((t.x + x) & 1023)] = t.pixels[y * t.w + x];
}

/* A motions' archive: each entry MotionSteps up to an index of 0x7FFF */
static bool read_motions(const Archive &a, std::vector<std::vector<MotionStep>> &out) {
    int n = a.count();
    if (n <= 0 || n > 512 || a.size < (size_t)n * 4) return false;
    std::vector<std::vector<MotionStep>> list(n);
    for (int i = 0; i < n; i++) {
        const uint8_t *p = a.entry(i);
        if (!p) return false;
        const uint8_t *end = a.data + a.size;
        for (; p + 8 <= end; p += 8) {
            MotionStep s = {rd16(p), rd16(p + 2), rd16(p + 4), rd16(p + 6)};
            list[i].push_back(s);
            if (s.index == 0x7FFF) break;
        }
        if (list[i].empty() || list[i].back().index != 0x7FFF) return false;
    }
    out = std::move(list);
    return true;
}

bool Model::load(const std::string &n, std::vector<uint8_t> bytes, const Fighter *f, int entry) {
    name = n;
    fighter = f;
    file = std::move(bytes);
    arc.data = file.data();
    arc.size = file.size();
    if (arc.count() <= 0 || (size_t)arc.count() * 4 > arc.size) {
        error = "not an archive";
        return false;
    }
    boneEntry = f ? f->modelEntry : entry >= 0 ? entry : find_bone_entry(arc);
    if (boneEntry >= arc.count() || !arc.entry(boneEntry)) boneEntry = -1;
    if (boneEntry < 0) {
        error = "no bone list (not a model?)";
        return false;
    }
    /* FIGHTSTG_createModel: {texture entry, bones - 1, (parent, mesh, keys)...} */
    const uint8_t *e = arc.entry(boneEntry);
    texEntry = (int)rd32(e);
    int count = (int)rd32(e + 4);
    bones.assign(1, Bone{0, -1, -1, {}});
    for (int b = 0; b < count; b++) {
        Bone bone;
        bone.parent = (int)rd32(e + 8 + b * 12);
        int meshEntry = (int)rd32(e + 12 + b * 12);
        bone.keyEntry = (int)rd32(e + 16 + b * 12);
        bone.mesh = (int)meshes.size();
        Mesh mesh;
        mesh.entry = meshEntry;
        Archive ma = sub(arc, meshEntry);
        if (!read_vecs(ma, 0, mesh.verts) || !read_vecs(ma, 1, mesh.normals) || !ma.entry(2)) {
            error = "bone " + std::to_string(b + 1) + ": bad mesh";
            return false;
        }
        if (!read_commands(ma.entry(2), ma.data + ma.size, TEX_X, TEX_Y, mesh, error)) {
            error = "bone " + std::to_string(b + 1) + ": " + error;
            return false;
        }
        meshes.push_back(std::move(mesh));
        Archive ka = sub(arc, bone.keyEntry);
        for (int t = 0; t < 3; t++) {
            const uint8_t *p = ka.entry(t);
            const uint8_t *end = ka.data + ka.size;
            for (; p && p + 8 <= end; p += 8) {
                Key k = {rd16(p), rd16(p + 2), rd16(p + 4), rd16(p + 6)};
                bone.keys[t].push_back(k);
                if (k.frame >= 30000) break; /* the end */
            }
            if (bone.keys[t].empty()) bone.keys[t].push_back({0, 0, 0, 30000});
        }
        bones.push_back(std::move(bone));
    }
    if (texEntry != 0) read_tims(sub(arc, texEntry), TEX_X, TEX_Y, tims);
    place_tims(tims, vram);
    /* the motions: FighterInfo.motions names their archive (the fighters' is
       entry 0 of their model's file); without it, the first entry that reads
       as one */
    if (f && f->motionsFid == f->modelFid && read_motions(sub(arc, f->motionsEntry), motions)) motionEntry = f->motionsEntry;
    if (f && f->effectsFid == f->modelFid) {
        Archive e = sub(arc, f->effectsEntry);
        scripts = read_scripts(e.data, e.size);
    }
    for (int i = 0; i < arc.count() && motionEntry < 0; i++) {
        if (i == boneEntry || i == texEntry || rd32(arc.data + i * 4) == 0) continue;
        if (read_motions(sub(arc, i), motions)) motionEntry = i;
    }
    pose.assign(bones.size(), Pose{});
    for (Pose &p : pose) {
        p.scale = {4096, 4096, 4096};
        p.visible = true;
        for (int k = 0; k < 16; k++) p.world[k] = k % 5 == 0;
    }
    keyframes.assign(0x640, 0);
    blendTargets.assign(0x640, 0);
    blendSources.assign(0x640, 0);
    if (motions.size() >= 2) {
        idleFrames[0] = motions[0][0].frame;
        idleFrames[1] = motions[1][0].frame;
    }
    if (!motions.empty()) {
        setMotion(1);
    } else {
        for (size_t b = 1; b < bones.size(); b++) {
            Pose &p = pose[b];
            p.pos = {bones[b].keys[0][0].x, bones[b].keys[0][0].y, bones[b].keys[0][0].z};
            p.rot = {bones[b].keys[1][0].x, bones[b].keys[1][0].y, bones[b].keys[1][0].z};
            p.scale = {bones[b].keys[2][0].x, bones[b].keys[2][0].y, bones[b].keys[2][0].z};
        }
        computeWorld();
    }
    return true;
}

bool Model::stepFace() {
    return fighter && step_face(*fighter, face, motion, TEX_X, TEX_Y, vram);
}

/* --- FIGHTSTG's posing --- */

static int16_t &comp(Vec3s &v, int i) { return i == 0 ? v.x : i == 1 ? v.y : v.z; }

/* out = prev + (cur - prev) * t / 4096, as the GTE's gpf12 with the halfword difference */
static Vec3s lerp(const Vec3s &prev, const Vec3s &cur, int t) {
    Vec3s out = prev;
    for (int i = 0; i < 3; i++) {
        int16_t d = (int16_t)(comp(const_cast<Vec3s &>(cur), i) - comp(out, i));
        comp(out, i) += (int16_t)((d * t) >> 12);
    }
    return out;
}

/* FIGHTSTG_poseBone */
static void pose_bone(const Bone &bone, Pose &p, int frame) {
    Vec3s *outs[3] = {&p.pos, &p.rot, &p.scale};
    for (int t = 0; t < 3; t++) {
        const std::vector<Key> &keys = bone.keys[t];
        size_t k = 0;
        bool found = false;
        for (;; k++) {
            if (k + 1 >= keys.size()) break; /* the viewer's guard: the game trusts the end key */
            if (keys[k].frame == frame) { found = true; break; }
            if (keys[k].frame >= frame) break;
        }
        const Key &cur = keys[k];
        if (found || k == 0 || keys[k].frame < frame) {
            *outs[t] = {cur.x, cur.y, cur.z};
            continue;
        }
        const Key &prev = keys[k - 1];
        int span = cur.frame - prev.frame;
        int dp = span ? ((frame - prev.frame) << 12) / span : 0;
        *outs[t] = lerp({prev.x, prev.y, prev.z}, {cur.x, cur.y, cur.z}, dp);
    }
}

/* FIGHTSTG_blendBone */
static void blend_bone(const Bone &bone, Pose &p, int frame, int blend) {
    Vec3s *outs[3] = {&p.pos, &p.rot, &p.scale};
    Vec3s prevs[3] = {p.prevPos, p.prevRot, p.prevScale};
    for (int t = 0; t < 3; t++) {
        const std::vector<Key> &keys = bone.keys[t];
        size_t k = 0;
        while (k + 1 < keys.size() && keys[k].frame < frame) k++;
        Vec3s cur = {keys[k].x, keys[k].y, keys[k].z}, prev = prevs[t];
        if (t == 2 && (!prev.x || !prev.y || !prev.z || !cur.x || !cur.y || !cur.z)) {
            *outs[t] = cur;
            return;
        }
        *outs[t] = lerp(prev, cur, blend);
    }
}

void Model::setMotion(int m) {
    motion = m;
    keyframe = 1;
    keyframeCount = 0;
    motionDone = false;
    blending = false;
    blendTarget = 0;
    if (m < 1 || m > (int)motions.size()) return;
    const std::vector<MotionStep> &steps = motions[m - 1];
    int count = 0;
    for (size_t s = 0; s < steps.size() && steps[s].index != 0x7FFF && count < 0x640; s++) {
        const MotionStep &step = steps[s];
        int n = step.count;
        if (n == 0) {
            keyframes[count] = (uint16_t)step.frame;
            blendTargets[count] = (uint16_t)step.endFrame;
            count++;
            break;
        }
        if (step.endFrame == 0) {
            int to = s + 1 < steps.size() ? steps[s + 1].frame : -1;
            if (to == -1) to = idleFrames[0]; /* the idle motion the viewer's model has: the first */
            int from = step.index != 0 && s > 0 ? (uint16_t)steps[s - 1].endFrame : 0xFFFF;
            for (int i = 0; i < n && count < 0x640; i++) {
                blendSources[count] = (uint16_t)from;
                blendTargets[count] = (uint16_t)to;
                keyframes[count] = (uint16_t)((((i + 1) << 12) / (n + 1)) | 0x8000);
                count++;
            }
        } else {
            for (int i = 0; i < n && count < 0x640; i++) {
                keyframes[count] = (uint16_t)(step.frame + i);
                blendTargets[count] = 0;
                count++;
            }
        }
    }
    keyframeCount = count;
    evaluate();
}

/* The first half of FIGHTSTG_stepMotion (the keyframe's frame or blend), then
   FIGHTSTG_poseBones */
void Model::evaluate() {
    if (keyframeCount <= 0) return;
    if (keyframe >= keyframeCount) keyframe = keyframeCount - 1;
    int key = keyframe;
    if (!motionDone) {
        if (keyframes[key] & 0x8000) {
            if (blendTarget != blendTargets[key]) {
                blendTarget = blendTargets[key];
                if (blendSources[key] != 0xFFFF)
                    for (size_t b = 1; b < bones.size(); b++) pose_bone(bones[b], pose[b], blendSources[key]);
                for (Pose &p : pose) p.prevPos = p.pos, p.prevRot = p.rot, p.prevScale = p.scale;
            }
            frame = blendTargets[key];
            blend = keyframes[key] & 0x7FFF;
            blending = true;
        } else {
            blendTarget = 0;
            frame = keyframes[key];
            blending = false;
        }
    } else {
        blendTarget = 0;
        blending = false;
    }
    for (size_t b = 1; b < bones.size(); b++) {
        if (blending)
            blend_bone(bones[b], pose[b], frame, blend);
        else
            pose_bone(bones[b], pose[b], frame);
    }
    computeWorld();
}

/* The second half of FIGHTSTG_stepMotion: on by the frames gone by */
void Model::advance(int ticks) {
    if (motionDone || keyframeCount <= 0) return;
    keyframe += ticks;
    if (keyframe >= keyframeCount) keyframe = keyframeCount - 1;
    switch (keyframes[keyframe]) {
    case 0x8000:
        keyframe = blendTargets[keyframe];
        if (keyframe >= keyframeCount) keyframe = keyframeCount - 1;
        break;
    case 0xFFFF:
        motionDone = true;
        break;
    }
}

/* Each bone's matrix: Rz * Ry * Rx (RotMatrixZYX), its columns scaled, at its
   position, under its parent's; a bone scaled to 48 or less is hidden and
   keeps its last matrix (FIGHTSTG_updateModel) */
void Model::computeWorld() {
    for (size_t b = 0; b < bones.size(); b++) {
        Pose &p = pose[b];
        if (b == 0) {
            p.pos = {0, 0, 0}, p.rot = {0, 0, 0}, p.scale = {4096, 4096, 4096};
        }
        if (((int)p.scale.x | (int)p.scale.y | (int)p.scale.z) <= 48) {
            p.visible = false;
            continue;
        }
        p.visible = true;
        float local[16];
        bone_matrix(p.pos, p.rot, p.scale, local);
        if (b == 0) {
            memcpy(p.world, local, sizeof local);
            continue;
        }
        const float *a = pose[bones[b].parent].world;
        float w[16];
        for (int c = 0; c < 4; c++)
            for (int i = 0; i < 4; i++)
                w[c * 4 + i] = a[i] * local[c * 4] + a[4 + i] * local[c * 4 + 1] + a[8 + i] * local[c * 4 + 2] + a[12 + i] * local[c * 4 + 3];
        memcpy(p.world, w, sizeof w);
    }
}

void bone_matrix(const Vec3s &pos, const Vec3s &rot, const Vec3s &scale, float out[16]) {
    const float k = 6.2831853f / 4096;
    float cx = cosf(rot.x * k), sx = sinf(rot.x * k);
    float cy = cosf(rot.y * k), sy = sinf(rot.y * k);
    float cz = cosf(rot.z * k), sz = sinf(rot.z * k);
    float r[3][3] = { /* Rz * Ry * Rx */
        {cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx},
        {sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx},
        {-sy, cy * sx, cy * cx},
    };
    float s[3] = {scale.x / 4096.0f, scale.y / 4096.0f, scale.z / 4096.0f};
    memset(out, 0, 16 * sizeof(float));
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) out[j * 4 + i] = r[i][j] * s[j];
    out[12] = pos.x, out[13] = pos.y, out[14] = pos.z, out[15] = 1;
}

/* --- the textures, decoded on the CPU --- */

static uint32_t rgba(uint16_t p, bool stp) {
    uint32_t r = (p & 31) * 255 / 31, g = (p >> 5 & 31) * 255 / 31, b = (p >> 10 & 31) * 255 / 31;
    return r | g << 8 | b << 16 | (!p ? 0 : stp && (p & 0x8000) ? 0x80000000u : 0xFF000000u);
}

std::vector<uint32_t> decode_page(const std::vector<uint16_t> &vram, uint16_t tpage, uint16_t clut, int w, int h, bool stp) {
    std::vector<uint32_t> out(w * h);
    int px = tpage_x(tpage), py = tpage_y(tpage), cx = clut_x(clut), cy = clut_y(clut), depth = tpage_depth(tpage);
    auto at = [&](int x, int y) { return vram[(y & 511) * 1024 + (x & 1023)]; };
    for (int v = 0; v < h; v++)
        for (int u = 0; u < w; u++) {
            uint16_t p;
            if (depth == 0)
                p = at(cx + (at(px + u / 4, py + v) >> (u & 3) * 4 & 15), cy);
            else if (depth == 1)
                p = at(cx + (at(px + u / 2, py + v) >> (u & 1) * 8 & 255), cy);
            else
                p = at(px + u, py + v);
            out[v * w + u] = rgba(p, stp);
        }
    return out;
}
