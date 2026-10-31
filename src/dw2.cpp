/* A Digimon World 2 model (the .dw2m tools/extract_dw2.py writes from the disc's files) read into the viewer's
   Model, so its bones, textures and motions go through the same card as a DW3 one. Formats: dw2_decomp's
   src/main/{model,anim,texslot}.c. Each DW2 motion is laid end to end on the bones' key tracks (one key a frame), and
   played as a plain MotionStep run: holding on its last frame or looping to the frame the game's 0xFE names. */
#include "model.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace {
struct Rd {
    const uint8_t *p, *end;
    bool ok = true;
    bool need(size_t n) { if ((size_t)(end - p) < n) ok = false; return ok; }
    int u8() { if (!need(1)) return 0; return *p++; }
    int u16() { if (!need(2)) return 0; int v = p[0] | p[1] << 8; p += 2; return v; }
    int s16() { return (int16_t)u16(); }
};
} // namespace

bool Model::load_dw2(const std::string &n, std::vector<uint8_t> bytes) {
    name = n;
    dw2 = true;
    file = std::move(bytes);
    dcb = file[0] == 'D' && file[1] == 'C';
    Rd r{file.data() + 4, file.data() + file.size()};
    int ver = r.u16(), nb = r.u16(), imgW = r.u16(), imgH = r.u16();
    if (ver != 1 || !r.ok || nb <= 0 || imgW % 4 || imgH <= 0 || imgW > 256 || imgH > 256) {
        error = "not a DW2 model file";
        return false;
    }
    int hw = imgW / 4;
    if (!r.need((size_t)hw * imgH * 2)) { error = "DW2/DCB model: cut short"; return false; }
    vram.assign(1024 * 512, 0);
    for (int y = 0; y < imgH; y++)
        for (int x = 0; x < hw; x++) vram[(TEX_Y + y) * 1024 + TEX_X + x] = (uint16_t)r.u16();
    struct Mot { int group, index, frames, loop, loopFrame; };
    std::vector<Mot> mots(r.u16());
    for (Mot &m : mots) {
        m.group = r.u16(), m.index = r.u16(), m.frames = r.u16();
        m.loop = r.u8(), r.u8(), m.loopFrame = r.u16();
    }
    bones.assign(1, Bone{0, -1, -1, {}});
    for (int b = 0; b < nb && r.ok; b++) {
        Bone bone{};
        int parent = r.s16(), nv = r.u16(), nf = r.u16();
        bone.parent = parent < 0 ? 0 : parent + 1;
        Mesh mesh;
        mesh.entry = -1;
        for (int i = 0; i < nv; i++) {
            Vec3s v;
            v.x = (int16_t)r.s16(), v.y = (int16_t)r.s16(), v.z = (int16_t)r.s16();
            mesh.verts.push_back(v);
        }
        for (int i = 0; i < nf; i++) {
            Poly p = {};
            if (dcb) { /* flags (1 quad, 2 textured, 4 semi-transparent), four 16-bit vertices, the UVs, the CLUT, the flat color */
                int flags = r.u8(), v[4], uv[8];
                for (int k = 0; k < 4; k++) v[k] = r.u16();
                for (int k = 0; k < 8; k++) uv[k] = r.u8();
                int clut = r.u16(), rgb[3] = {r.u8(), r.u8(), r.u8()};
                if (v[0] >= nv || v[1] >= nv || v[2] >= nv || ((flags & 1) && v[3] >= nv)) { error = "DCB model: bad vertex"; return false; }
                p.quad = flags & 1, p.textured = flags & 2, p.abr = flags & 4 ? 1 : 0;
                for (int k = 0; k < 4; k++) p.idx[k] = v[k], p.uv[k][0] = (uint8_t)uv[2 * k], p.uv[k][1] = (uint8_t)uv[2 * k + 1];
                p.tpage = (uint16_t)(TEX_X / 64 | (TEX_Y >= 256 ? 16 : 0));
                p.clut = (uint16_t)(((TEX_Y + (clut >> 6)) << 6) | ((TEX_X >> 4) + (clut & 63)));
                for (int k = 0; k < 3; k++) p.color[k] = (uint8_t)(p.textured ? 128 : rgb[k]);
                mesh.polys.push_back(p);
                continue;
            }
            int cnt = r.u8(), semi = r.u8();
            int v[4], uv[8];
            for (int k = 0; k < 4; k++) v[k] = r.u8();
            for (int k = 0; k < 8; k++) uv[k] = r.u8();
            int clut = r.u16();
            if (v[0] >= nv || v[1] >= nv || v[2] >= nv || (cnt == 4 && v[3] >= nv)) { error = "DW2 model: bad vertex"; return false; }
            p.quad = cnt == 4, p.textured = true, p.abr = semi ? 1 : 0;
            for (int k = 0; k < 4; k++) p.idx[k] = v[k], p.uv[k][0] = (uint8_t)uv[2 * k], p.uv[k][1] = (uint8_t)uv[2 * k + 1];
            p.tpage = (uint16_t)(TEX_X / 64 | (TEX_Y >= 256 ? 16 : 0));
            p.clut = (uint16_t)(((TEX_Y + (clut >> 6)) << 6) | ((TEX_X >> 4) + (clut & 63)));
            p.color[0] = p.color[1] = p.color[2] = 128;
            mesh.polys.push_back(p);
        }
        bone.mesh = (int)meshes.size();
        meshes.push_back(std::move(mesh));
        bones.push_back(std::move(bone));
    }
    int base = 0;
    motions.clear();
    for (const Mot &m : mots) {
        for (int f = 0; f < m.frames; f++)
            for (int b = 1; b <= nb; b++)
                for (int t = 0; t < 3; t++) {
                    Key k;
                    k.x = (int16_t)r.s16(), k.y = (int16_t)r.s16(), k.z = (int16_t)r.s16(), k.frame = (int16_t)(2 * (base + f));
                    bones[b].keys[t].push_back(k);
                    k.frame++; /* the game steps a frame every 2 vsyncs (Anim_StepModelAnim), the viewer's tick is one */
                    bones[b].keys[t].push_back(k);
                }
        std::vector<MotionStep> steps;
        steps.push_back({0, (int16_t)(2 * m.frames), (int16_t)(2 * base), (int16_t)(2 * (base + m.frames))}); /* endFrame != 0: plain frames */
        if (m.loop) steps.push_back({1, 0, (int16_t)0x8000, (int16_t)(2 * std::min(m.loopFrame, m.frames - 1))});
        else steps.push_back({1, 0, (int16_t)0xFFFF, 0});
        steps.push_back({0x7FFF, 0, 0, 0});
        motions.push_back(std::move(steps));
        base += m.frames;
        if (base >= 14000) break;
    }
    if (dcb) { /* the hidden flags, a byte a bone a frame: the mesh is not drawn, the bone still moves its children */
        int hbase = 0;
        for (const Mot &m : mots) {
            for (int f = 0; f < m.frames; f++)
                for (int b = 1; b <= nb; b++) {
                    Key k;
                    k.x = (int16_t)r.u8(), k.y = k.z = 0, k.frame = (int16_t)(2 * (hbase + f));
                    bones[b].hide.push_back(k);
                }
            hbase += m.frames;
            if (hbase >= 14000) break;
        }
    }
    if (!r.ok) { error = "DW2/DCB model: cut short"; return false; }
    for (size_t b = 1; b < bones.size(); b++)
        for (int t = 0; t < 3; t++) {
            if (bones[b].keys[t].empty()) bones[b].keys[t].push_back({t == 2 ? (int16_t)4096 : (int16_t)0, t == 2 ? (int16_t)4096 : (int16_t)0, t == 2 ? (int16_t)4096 : (int16_t)0, 0});
            Key e = bones[b].keys[t].back();
            e.frame = 30000;
            bones[b].keys[t].push_back(e);
        }
    tune.mesh.assign(bones.size(), 1.0f), tune.reach.assign(bones.size(), 1.0f), tune.place.assign(bones.size(), 1.0f), tune.shape.assign(bones.size() * 3, 1.0f);
    for (size_t b = 1; b < bones.size(); b++) dw2BaseVerts.push_back(meshes[bones[b].mesh].verts), dw2BasePos.push_back(bones[b].keys[0]);
    dw2BaseVerts.insert(dw2BaseVerts.begin(), std::vector<Vec3s>()), dw2BasePos.insert(dw2BasePos.begin(), std::vector<Key>());
    pose.assign(bones.size(), Pose{});
    for (Pose &p : pose) {
        p.scale = {4096, 4096, 4096};
        p.visible = true;
        for (int k = 0; k < 16; k++) p.world[k] = k % 5 == 0;
    }
    keyframes.assign(0x640, 0);
    blendTargets.assign(0x640, 0);
    blendSources.assign(0x640, 0);
    if (motions.size() >= 2) idleFrames[0] = motions[0][0].frame, idleFrames[1] = motions[1][0].frame;
    if (!motions.empty()) setMotion(1);
    else computeWorld();
    return true;
}

static int16_t clamp16(float v) { return (int16_t)std::clamp((int)std::lround(v), -32768, 32767); }

bool Model::dw2_tuned() const {
    if (tune.scale != 1) return true;
    for (float v : tune.mesh)
        if (v != 1) return true;
    for (float v : tune.reach)
        if (v != 1) return true;
    return false;
}

/* A bone's mesh is its base times the whole scale and its own mesh factor; its position keys (where its joint sits under its
   parent) are the base times the whole scale and its parent's reach */
void Model::dw2_retune() {
    for (size_t b = 1; b < bones.size() && b < dw2BaseVerts.size(); b++) {
        std::vector<Vec3s> &v = meshes[bones[b].mesh].verts;
        float f = tune.scale * tune.mesh[b];
        float sx = 1, sy = 1, sz = 1;
        if (tune.shape.size() >= (b + 1) * 3) sx = tune.shape[b * 3], sy = tune.shape[b * 3 + 1], sz = tune.shape[b * 3 + 2];
        for (size_t i = 0; i < v.size(); i++)
            v[i] = {clamp16(dw2BaseVerts[b][i].x * f * sx), clamp16(dw2BaseVerts[b][i].y * f * sy), clamp16(dw2BaseVerts[b][i].z * f * sz)};
        float g = tune.scale * tune.reach[bones[b].parent] * (b < tune.place.size() ? tune.place[b] : 1.0f);
        for (size_t k = 0; k < bones[b].keys[0].size(); k++) {
            const Key &o = dw2BasePos[b][k];
            bones[b].keys[0][k] = {clamp16(o.x * g), clamp16(o.y * g), clamp16(o.z * g), o.frame};
        }
    }
    evaluate();
}

bool Model::dw2_load_tuning(const std::string &path) {
    tune.shape.assign(bones.size() * 3, 1.0f), tune.place.assign(bones.size(), 1.0f);
    {   /* shape.txt, "bone N sx sy sz" (N from 1, as tuning.txt's): DCB's per-axis stretch of a bone's mesh */
        std::ifstream sf(std::filesystem::u8path(path).parent_path() / "shape.txt");
        std::string line;
        while (std::getline(sf, line)) {
            int n;
            float a, b, c;
            if (sscanf(line.c_str(), "bone %d %f %f %f", &n, &a, &b, &c) == 4 && n >= 0 && n < (int)bones.size()) {
                tune.shape[n * 3] = a, tune.shape[n * 3 + 1] = b, tune.shape[n * 3 + 2] = c;
            }
        }
    }
    std::ifstream f(path);
    if (!f) return false;
    tune.scale = 1;
    tune.mesh.assign(bones.size(), 1.0f), tune.reach.assign(bones.size(), 1.0f);
    std::string w;
    while (f >> w) {
        if (w == "scale") f >> tune.scale;
        else if (w == "bone") {
            int b;
            std::string what;
            float v;
            f >> b >> what >> v;
            if (b >= 0 && b < (int)bones.size()) (what == "mesh" ? tune.mesh : what == "place" ? tune.place : tune.reach)[b] = v;
        }
    }
    dw2_retune();
    return true;
}

bool Model::dw2_save_tuning(const std::string &path) const {
    std::ofstream f(path);
    if (!f) return false;
    f << "scale " << tune.scale << '\n';
    for (size_t b = 0; b < bones.size(); b++) {
        if (tune.mesh[b] != 1) f << "bone " << b << " mesh " << tune.mesh[b] << '\n';
        if (tune.reach[b] != 1) f << "bone " << b << " reach " << tune.reach[b] << '\n';
        if (b < tune.place.size() && tune.place[b] != 1) f << "bone " << b << " place " << tune.place[b] << '\n';
    }
    bool shaped = false; /* shape.txt beside it: a bone's mesh stretched per axis (kept only when something is) */
    for (float s : tune.shape) shaped |= s != 1;
    std::filesystem::path sp = std::filesystem::u8path(path).parent_path() / "shape.txt";
    std::error_code ec;
    if (shaped) {
        std::ofstream sf(sp);
        for (size_t b = 0; b < bones.size() && (b + 1) * 3 <= tune.shape.size(); b++)
            if (tune.shape[b * 3] != 1 || tune.shape[b * 3 + 1] != 1 || tune.shape[b * 3 + 2] != 1)
                sf << "bone " << b << ' ' << tune.shape[b * 3] << ' ' << tune.shape[b * 3 + 1] << ' ' << tune.shape[b * 3 + 2] << '\n';
    } else std::filesystem::remove(sp, ec);
    return (bool)f;
}
