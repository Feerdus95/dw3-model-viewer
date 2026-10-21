/* A Digimon World 2 model (the .dw2m dw2-extractor/todw2m.py writes from the disc's files) read into the viewer's
   Model, so its bones, textures and motions go through the same card as a DW3 one. Formats: dw2_decomp's
   src/main/{model,anim,texslot}.c. Each DW2 motion is laid end to end on the bones' key tracks (one key a frame), and
   played as a plain MotionStep run: holding on its last frame or looping to the frame the game's 0xFE names. */
#include "model.h"
#include <cstring>

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
    file = std::move(bytes);
    Rd r{file.data() + 4, file.data() + file.size()};
    int ver = r.u16(), nb = r.u16(), imgW = r.u16(), imgH = r.u16();
    if (ver != 1 || !r.ok || nb <= 0 || imgW % 4 || imgH <= 0 || imgW > 256 || imgH > 256) {
        error = "not a DW2 model file";
        return false;
    }
    int hw = imgW / 4;
    if (!r.need((size_t)hw * imgH * 2)) { error = "DW2 model: cut short"; return false; }
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
                    k.x = (int16_t)r.s16(), k.y = (int16_t)r.s16(), k.z = (int16_t)r.s16(), k.frame = (int16_t)(base + f);
                    bones[b].keys[t].push_back(k);
                }
        std::vector<MotionStep> steps;
        steps.push_back({0, (int16_t)m.frames, (int16_t)base, (int16_t)(base + m.frames)}); /* endFrame != 0: plain frames */
        if (m.loop) steps.push_back({1, 0, (int16_t)0x8000, (int16_t)std::min(m.loopFrame, m.frames - 1)});
        else steps.push_back({1, 0, (int16_t)0xFFFF, 0});
        steps.push_back({0x7FFF, 0, 0, 0});
        motions.push_back(std::move(steps));
        base += m.frames;
        if (base >= 29000) break;
    }
    if (!r.ok) { error = "DW2 model: cut short"; return false; }
    for (size_t b = 1; b < bones.size(); b++)
        for (int t = 0; t < 3; t++) {
            if (bones[b].keys[t].empty()) bones[b].keys[t].push_back({t == 2 ? (int16_t)4096 : (int16_t)0, t == 2 ? (int16_t)4096 : (int16_t)0, t == 2 ? (int16_t)4096 : (int16_t)0, 0});
            Key e = bones[b].keys[t].back();
            e.frame = 30000;
            bones[b].keys[t].push_back(e);
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
    if (motions.size() >= 2) idleFrames[0] = motions[0][0].frame, idleFrames[1] = motions[1][0].frame;
    if (!motions.empty()) setMotion(1);
    else computeWorld();
    return true;
}
