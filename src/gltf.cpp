/* A battle model as a glTF 2.0 binary (gltf.h): PSX y-down, left-handed -> glTF by diag(1, -1, -1), as the OBJ's export;
   the skeleton is the model's bones at the first frame of their keys (what stage.cpp calls the bind), each vertex
   weighted whole to its bone */
#include "gltf.h"
#include "stage.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace {

const float UNIT = 0.01f; /* the game's units in one of glTF's */
const float PI = 3.14159265f;

struct Mat {
    float m[16]; /* column-major */
};

Mat identity() {
    Mat r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1;
    return r;
}

Mat mul(const Mat &a, const Mat &b) {
    Mat r{};
    for (int c = 0; c < 4; c++)
        for (int i = 0; i < 4; i++)
            for (int k = 0; k < 4; k++) r.m[c * 4 + i] += a.m[k * 4 + i] * b.m[c * 4 + k];
    return r;
}

/* the inverse of an affine matrix */
Mat inverse(const Mat &a) {
    const float *m = a.m;
    float r[3][3] = {{m[0], m[4], m[8]}, {m[1], m[5], m[9]}, {m[2], m[6], m[10]}}, o[3][3];
    float det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
                r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
    if (std::fabs(det) < 1e-12f) det = 1e-12f;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            int i1 = (i + 1) % 3, i2 = (i + 2) % 3, j1 = (j + 1) % 3, j2 = (j + 2) % 3;
            o[j][i] = (r[i1][j1] * r[i2][j2] - r[i1][j2] * r[i2][j1]) / det; /* the adjugate's transpose */
        }
    Mat out{};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) out.m[j * 4 + i] = o[i][j];
    for (int i = 0; i < 3; i++) out.m[12 + i] = -(out.m[i] * m[12] + out.m[4 + i] * m[13] + out.m[8 + i] * m[14]);
    out.m[15] = 1;
    return out;
}

/* a bone's matrix in the game's axes -> glTF's: C M C, its translation scaled */
Mat to_gltf(const Mat &a) {
    Mat r = a;
    static const float c[3] = {1, -1, -1};
    for (int col = 0; col < 3; col++)
        for (int i = 0; i < 3; i++) r.m[col * 4 + i] = a.m[col * 4 + i] * c[col] * c[i];
    for (int i = 0; i < 3; i++) r.m[12 + i] = a.m[12 + i] * c[i] * UNIT;
    return r;
}

struct Trs {
    float t[3], q[4], s[3];
};

/* translation, rotation (a quaternion x y z w) and scale of a glTF-space matrix */
Trs decompose(const Mat &a) {
    Trs o{};
    float r[3][3];
    for (int c = 0; c < 3; c++) {
        float n = std::sqrt(a.m[c * 4] * a.m[c * 4] + a.m[c * 4 + 1] * a.m[c * 4 + 1] + a.m[c * 4 + 2] * a.m[c * 4 + 2]);
        o.s[c] = n;
        if (n == 0) n = 1;
        for (int i = 0; i < 3; i++) r[i][c] = a.m[c * 4 + i] / n;
    }
    float det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
                r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
    if (det < 0) {
        o.s[0] = -o.s[0];
        for (int i = 0; i < 3; i++) r[i][0] = -r[i][0];
    }
    float tr = r[0][0] + r[1][1] + r[2][2];
    if (tr > 0) {
        float w = std::sqrt(1 + tr) / 2;
        o.q[0] = (r[2][1] - r[1][2]) / (4 * w), o.q[1] = (r[0][2] - r[2][0]) / (4 * w), o.q[2] = (r[1][0] - r[0][1]) / (4 * w), o.q[3] = w;
    } else {
        int i = 0;
        if (r[1][1] > r[0][0]) i = 1;
        if (r[2][2] > r[i][i]) i = 2;
        int j = (i + 1) % 3, k = (i + 2) % 3;
        float x = std::sqrt(std::max(0.0f, 1 + r[i][i] - r[j][j] - r[k][k])) / 2;
        if (x == 0) x = 1e-6f;
        o.q[i] = x, o.q[j] = (r[j][i] + r[i][j]) / (4 * x), o.q[k] = (r[k][i] + r[i][k]) / (4 * x), o.q[3] = (r[k][j] - r[j][k]) / (4 * x);
    }
    for (int i = 0; i < 3; i++) o.t[i] = a.m[12 + i];
    return o;
}

Mat local_of(const Vec3s &pos, const Vec3s &rot, const Vec3s &scale) {
    Mat r;
    bone_matrix(pos, rot, scale, r.m);
    return r;
}

bool hidden(const Vec3s &s) { return ((int)s.x | (int)s.y | (int)s.z) <= 48; } /* FIGHTSTG_updateModel's */

/* The bones' bind: the first frame of their keys, a scale of 48 or less as full size (stage.cpp's bind_world) */
std::vector<Mat> bind_of(const Model &m) {
    std::vector<Mat> local(m.bones.size(), identity()), world(m.bones.size(), identity());
    for (size_t b = 1; b < m.bones.size(); b++) {
        const Bone &bone = m.bones[b];
        Vec3s v[3] = {{0, 0, 0}, {0, 0, 0}, {4096, 4096, 4096}};
        for (int t = 0; t < 3; t++)
            if (!bone.keys[t].empty()) v[t] = {bone.keys[t][0].x, bone.keys[t][0].y, bone.keys[t][0].z};
        if (std::abs(v[2].x) <= 48 || std::abs(v[2].y) <= 48 || std::abs(v[2].z) <= 48) v[2] = {4096, 4096, 4096};
        local[b] = local_of(v[0], v[1], v[2]);
        int parent = bone.parent < (int)b ? bone.parent : 0;
        world[b] = mul(world[parent], local[b]);
    }
    return world;
}

struct Writer {
    std::vector<uint8_t> bin;
    std::ostringstream views, accessors;
    int nviews = 0, naccessors = 0;

    /* DATA as a buffer view and an accessor of TYPE ("SCALAR", "VEC3"...), COMPONENT (5126 float, 5121 byte); returns the accessor */
    int add(const void *data, size_t bytes, int count, int component, const char *type, const float *lo = nullptr, const float *hi = nullptr,
            int nlimits = 0, int target = 0) {
        while (bin.size() & 3) bin.push_back(0);
        size_t at = bin.size();
        bin.insert(bin.end(), (const uint8_t *)data, (const uint8_t *)data + bytes);
        if (nviews++) views << ",";
        views << "{\"buffer\":0,\"byteOffset\":" << at << ",\"byteLength\":" << bytes;
        if (target) views << ",\"target\":" << target;
        views << "}";
        if (naccessors++) accessors << ",";
        accessors << "{\"bufferView\":" << nviews - 1 << ",\"componentType\":" << component << ",\"count\":" << count << ",\"type\":\"" << type << "\"";
        if (nlimits) {
            accessors << ",\"min\":[";
            for (int i = 0; i < nlimits; i++) accessors << (i ? "," : "") << lo[i];
            accessors << "],\"max\":[";
            for (int i = 0; i < nlimits; i++) accessors << (i ? "," : "") << hi[i];
            accessors << "]";
        }
        accessors << "}";
        return naccessors - 1;
    }
};

} // namespace

bool export_gltf(const Model &model, const std::string &dir, const std::string &name, std::string &err) {
    if (model.bones.size() < 2 || model.pose.size() != model.bones.size()) return err = "no bones", false;
    Model m = model; /* its player is stepped through every motion */
    StageEdit e;
    StageInfo info;
    e.fromModel(m, info); /* the faces and the texture pages, as the OBJ's */
    SDL_CreateDirectory(dir.c_str());
    std::ofstream file(dir + "/" + name + ".glb", std::ios::binary);
    if (!file) return err = "can't write " + dir, false;

    const int nb = (int)m.bones.size();
    std::vector<Mat> bind = bind_of(m), gl(nb), ibm(nb);
    for (int b = 0; b < nb; b++) gl[b] = to_gltf(bind[b]), ibm[b] = inverse(gl[b]);

    Writer w;
    /* --- the mesh: a primitive for each material, every bone's faces in it, each vertex whole on its bone --- */
    struct Prim {
        std::vector<float> pos, uv;
        std::vector<uint8_t> joint;
    };
    std::map<std::pair<int, int>, Prim> prims;
    std::set<int> usedImages;
    for (int b = 1; b < nb; b++) {
        const StagePart &part = e.parts[b - 1];
        for (const StageFace &f : part.faces) {
            const StageImage &img = e.images[f.image];
            Prim &p = prims[{f.image, f.abr}];
            usedImages.insert(f.image);
            static const int TRI[2][3] = {{0, 1, 2}, {0, 2, 3}};
            for (int t = 0; t < (f.n == 4 ? 2 : 1); t++)
                for (int k = 0; k < 3; k++) {
                    int c = TRI[t][k];
                    const auto &v = part.verts[f.v[c]];
                    p.pos.push_back(v[0] * UNIT), p.pos.push_back(-v[1] * UNIT), p.pos.push_back(-v[2] * UNIT);
                    p.uv.push_back((f.uv[c][0] + 0.5f) / img.w), p.uv.push_back((f.uv[c][1] + 0.5f) / img.h);
                    p.joint.push_back((uint8_t)b), p.joint.push_back(0), p.joint.push_back(0), p.joint.push_back(0);
                }
        }
    }
    std::vector<int> imageIds(usedImages.begin(), usedImages.end());
    std::map<std::pair<int, int>, int> materialOf;
    std::ostringstream prim, mats, texs, imgs;
    std::vector<float> weights;
    for (auto &kv : prims) {
        int count = (int)kv.second.pos.size() / 3;
        if (weights.size() < (size_t)count * 4) {
            weights.assign((size_t)count * 4, 0);
            for (int i = 0; i < count; i++) weights[i * 4] = 1;
        }
    }
    int n = 0;
    for (auto &kv : prims) {
        Prim &p = kv.second;
        int count = (int)p.pos.size() / 3;
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (int i = 0; i < count; i++)
            for (int a = 0; a < 3; a++) lo[a] = std::min(lo[a], p.pos[i * 3 + a]), hi[a] = std::max(hi[a], p.pos[i * 3 + a]);
        int aPos = w.add(p.pos.data(), p.pos.size() * 4, count, 5126, "VEC3", lo, hi, 3, 34962);
        int aUv = w.add(p.uv.data(), p.uv.size() * 4, count, 5126, "VEC2", nullptr, nullptr, 0, 34962);
        int aJoint = w.add(p.joint.data(), p.joint.size(), count, 5121, "VEC4", nullptr, nullptr, 0, 34962);
        int aWeight = w.add(weights.data(), (size_t)count * 16, count, 5126, "VEC4", nullptr, nullptr, 0, 34962);
        materialOf[kv.first] = n;
        prim << (n ? "," : "") << "{\"attributes\":{\"POSITION\":" << aPos << ",\"TEXCOORD_0\":" << aUv << ",\"JOINTS_0\":" << aJoint
             << ",\"WEIGHTS_0\":" << aWeight << "},\"material\":" << n << ",\"mode\":4}";
        int img = (int)(std::find(imageIds.begin(), imageIds.end(), kv.first.first) - imageIds.begin());
        mats << (n ? "," : "") << "{\"name\":\"tex" << kv.first.first << (kv.first.second ? "_abr" + std::to_string(kv.first.second) : "")
             << "\",\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":" << img << "},\"metallicFactor\":0,\"roughnessFactor\":1},"
             << "\"alphaMode\":\"" << (kv.first.second ? "BLEND" : "MASK") << "\",\"alphaCutoff\":0.5,\"doubleSided\":true,"
             << "\"extensions\":{\"KHR_materials_unlit\":{}}}";
        n++;
    }
    for (size_t i = 0; i < imageIds.size(); i++) {
        texs << (i ? "," : "") << "{\"source\":" << i << ",\"sampler\":0}";
        imgs << (i ? "," : "") << "{\"uri\":\"tex" << imageIds[i] << ".png\"}";
        const StageImage &img = e.images[imageIds[i]];
        SDL_Surface *s = SDL_CreateSurfaceFrom(img.w, img.h, SDL_PIXELFORMAT_RGBA32, (void *)img.px.data(), img.w * 4);
        if (s) SDL_SavePNG(s, (dir + "/tex" + std::to_string(imageIds[i]) + ".png").c_str()), SDL_DestroySurface(s);
    }

    /* --- the skeleton: a node a bone, at its bind; the inverse bind matrices --- */
    std::vector<float> ibmData;
    for (int b = 0; b < nb; b++) ibmData.insert(ibmData.end(), ibm[b].m, ibm[b].m + 16);
    int aIbm = w.add(ibmData.data(), ibmData.size() * 4, nb, 5126, "MAT4");
    std::vector<std::vector<int>> kids(nb);
    for (int b = 1; b < nb; b++) kids[m.bones[b].parent < b ? m.bones[b].parent : 0].push_back(b);
    std::ostringstream nodes, joints;
    for (int b = 0; b < nb; b++) {
        /* the node's local matrix at the bind: its world under its parent's */
        int parent = b == 0 ? 0 : (m.bones[b].parent < b ? m.bones[b].parent : 0);
        Mat local = b == 0 ? gl[0] : mul(inverse(gl[parent]), gl[b]);
        Trs t = decompose(local);
        char buf[96];
        nodes << (b ? "," : "") << "{\"name\":\"bone_" << b << "\"";
        if (!kids[b].empty()) {
            nodes << ",\"children\":[";
            for (size_t i = 0; i < kids[b].size(); i++) nodes << (i ? "," : "") << kids[b][i];
            nodes << "]";
        }
        snprintf(buf, sizeof buf, ",\"translation\":[%.6g,%.6g,%.6g]", t.t[0], t.t[1], t.t[2]), nodes << buf;
        snprintf(buf, sizeof buf, ",\"rotation\":[%.6g,%.6g,%.6g,%.6g]", t.q[0], t.q[1], t.q[2], t.q[3]), nodes << buf;
        snprintf(buf, sizeof buf, ",\"scale\":[%.6g,%.6g,%.6g]}", t.s[0], t.s[1], t.s[2]), nodes << buf;
        joints << (b ? "," : "") << b;
    }
    nodes << ",{\"name\":\"" << name << "\",\"mesh\":0,\"skin\":0}"; /* node nb */

    /* --- the motions: the game's player stepped a tick at a time, sampled every other one --- */
    std::ostringstream anims;
    int nAnim = 0;
    for (int mo = 1; mo <= (int)m.motions.size(); mo++) {
        m.setMotion(mo);
        if (m.keyframeCount <= 0) continue;
        std::vector<std::vector<Trs>> frames; /* [sample][bone] */
        std::vector<Trs> last(nb);
        for (int b = 0; b < nb; b++) {
            int parent = b == 0 ? 0 : (m.bones[b].parent < b ? m.bones[b].parent : 0);
            last[b] = decompose(b == 0 ? gl[0] : mul(inverse(gl[parent]), gl[b]));
        }
        for (int tick = 0; tick < 1700; tick++) {
            bool lastTick = m.motionDone || m.keyframe >= m.keyframeCount - 1;
            if (tick % 2 == 0 || lastTick) {
                std::vector<Trs> row = last;
                for (int b = 1; b < nb; b++) {
                    const Pose &p = m.pose[b];
                    if (hidden(p.scale)) continue; /* it keeps its last matrix */
                    row[b] = decompose(to_gltf(local_of(p.pos, p.rot, p.scale)));
                }
                for (int b = 1; b < nb; b++) { /* the shorter way round from the last sample */
                    float *q = row[b].q, *o = last[b].q;
                    if (!frames.empty() && q[0] * o[0] + q[1] * o[1] + q[2] * o[2] + q[3] * o[3] < 0)
                        for (int i = 0; i < 4; i++) q[i] = -q[i];
                }
                frames.push_back(row);
                last = row;
            }
            if (lastTick) break;
            m.advance(1, true);
            m.evaluate();
        }
        if (frames.size() < 2) continue;
        int ns = (int)frames.size();
        std::vector<float> times(ns);
        for (int i = 0; i < ns; i++) times[i] = i / 30.0f;
        float tlo = 0, thi = times.back();
        int aTime = w.add(times.data(), times.size() * 4, ns, 5126, "SCALAR", &tlo, &thi, 1);
        std::ostringstream chans, samps;
        int ci = 0;
        for (int b = 0; b < nb; b++) {
            std::vector<float> tv, qv, sv;
            for (int i = 0; i < ns; i++) {
                const Trs &t = frames[i][b];
                tv.insert(tv.end(), t.t, t.t + 3), qv.insert(qv.end(), t.q, t.q + 4), sv.insert(sv.end(), t.s, t.s + 3);
            }
            struct Ch { const char *path; std::vector<float> *v; int comps; const char *type; } chs[3] = {
                {"translation", &tv, 3, "VEC3"}, {"rotation", &qv, 4, "VEC4"}, {"scale", &sv, 3, "VEC3"}};
            for (auto &c : chs) {
                int out = w.add(c.v->data(), c.v->size() * 4, ns, 5126, c.type);
                samps << (ci ? "," : "") << "{\"input\":" << aTime << ",\"output\":" << out << ",\"interpolation\":\"LINEAR\"}";
                chans << (ci ? "," : "") << "{\"sampler\":" << ci << ",\"target\":{\"node\":" << b << ",\"path\":\"" << c.path << "\"}}";
                ci++;
            }
        }
        anims << (nAnim++ ? "," : "") << "{\"name\":\"motion_" << mo << "\",\"channels\":[" << chans.str() << "],\"samplers\":[" << samps.str() << "]}";
    }

    std::ostringstream js;
    js << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"DW3 Model Viewer\"},\"extensionsUsed\":[\"KHR_materials_unlit\"],"
       << "\"scene\":0,\"scenes\":[{\"nodes\":[0," << nb << "]}],\"nodes\":[" << nodes.str() << "],"
       << "\"skins\":[{\"joints\":[" << joints.str() << "],\"inverseBindMatrices\":" << aIbm << ",\"skeleton\":0}],"
       << "\"meshes\":[{\"primitives\":[" << prim.str() << "]}],\"materials\":[" << mats.str() << "],"
       << "\"textures\":[" << texs.str() << "],\"images\":[" << imgs.str() << "],"
       << "\"samplers\":[{\"magFilter\":9728,\"minFilter\":9728,\"wrapS\":33071,\"wrapT\":33071}],"
       << "\"buffers\":[{\"byteLength\":" << w.bin.size() << "}],\"bufferViews\":[" << w.views.str() << "],\"accessors\":[" << w.accessors.str() << "]";
    if (nAnim) js << ",\"animations\":[" << anims.str() << "]";
    js << "}";
    std::string json = js.str();
    while (json.size() & 3) json += ' ';
    while (w.bin.size() & 3) w.bin.push_back(0);
    auto u32 = [&](uint32_t v) { file.write((const char *)&v, 4); };
    u32(0x46546C67), u32(2), u32((uint32_t)(12 + 8 + json.size() + 8 + w.bin.size()));
    u32((uint32_t)json.size()), u32(0x4E4F534A), file.write(json.data(), (std::streamsize)json.size());
    u32((uint32_t)w.bin.size()), u32(0x004E4942), file.write((const char *)w.bin.data(), (std::streamsize)w.bin.size());
    return file.good() ? true : (err = "can't write " + dir, false);
}
