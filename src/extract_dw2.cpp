/* Digimon World 2 (USA, SLUS-01193): the models and their motions as .dw2m files, what tools/extract_dw2.py wrote. The
   formats are the game's model/anim/text code (dw2_decomp src/main/{model,anim,texslot,text}.c). */
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>

#include "disc.h"
#include "extract.h"

namespace fs = std::filesystem;
namespace {

struct Face {
    int n, blend, v[4], uv[8], clut;
};
struct Bone {
    int parent;
    std::vector<int16_t> verts; /* x y z */
    std::vector<Face> faces;
};
struct Key {
    int m[12];
};
struct Anim {
    int idx, end, loop;
    std::vector<Bytes> frames;
};
struct Keys {
    std::vector<std::vector<Key>> bone; /* per bone */
    std::vector<Anim> anims;
};

std::string glyphs(const Bytes &b, size_t from) {
    std::string s;
    for (size_t i = from; i < b.size() && b[i] != 0xFF; i++) {
        int c = b[i];
        s += c < 10 ? (char)('0' + c) : c < 0x24 ? (char)(55 + c) : c < 0x3E ? (char)(61 + c) : c == 0x44 ? '?' : c == 0x49 ? '-' : '_';
    }
    return s;
}

/* the model's texture: 4bpp width in pixels, rows (the last 4 hold the palettes), its bytes */
void tex(Span d, int &w, int &h, Bytes &px) {
    uint32_t t = d.u32(0);
    uint32_t flags = d.u32(t + 4);
    if (!(flags & 8) || (flags & 7)) throw std::runtime_error("only 4bpp+clut handled");
    size_t p = t + 8 + d.u32(t + 8);
    int ww = d.u16(p + 8), hh = d.u16(p + 10);
    w = ww * 4, h = hh;
    Span s = d.sub(p + 12, (size_t)ww * hh * 2);
    px.assign(s.p, s.p + s.n);
}

std::vector<Bone> parse_model(Span d) {
    uint32_t n = d.u32(8);
    std::vector<uint32_t> tab(4 * (size_t)n);
    for (size_t i = 0; i < tab.size(); i++) tab[i] = d.u32(12 + 4 * i);
    std::vector<int> parents(n);
    std::map<uint32_t, int> last;
    for (uint32_t j = 0; j < n; j++) {
        uint32_t dp = tab[3 * n + j];
        parents[j] = dp && last.count(dp - 1) ? last[dp - 1] : -1;
        last[dp] = j;
    }
    std::vector<Bone> bones(n);
    static const int SZ[4] = {0x14, 0x14, 0x10, 0x10}, NV[4] = {4, 4, 3, 3};
    for (uint32_t j = 0; j < n; j++) {
        Bone &b = bones[j];
        b.parent = parents[j];
        uint32_t vo = tab[j], po = tab[2 * n + j];
        int nv = d.u16(vo);
        for (int i = 0; i < nv; i++)
            for (int c = 0; c < 3; c++) b.verts.push_back((int16_t)d.s16(vo + 6 + 6 * i + 2 * c));
        size_t q = po;
        for (int sec = 0; sec < 4; sec++) {
            uint32_t cnt = d.u32(q);
            for (uint32_t i = 0; i < cnt; i++) {
                size_t e = q + 4 + (size_t)i * SZ[sec];
                d.need(e, SZ[sec]);
                Face f = {};
                f.n = NV[sec], f.blend = (sec & 1) == 0 ? 1 : 0;
                for (int k = 0; k < NV[sec]; k++) f.v[k] = d.u8(e + k);
                for (int k = 0; k < 2 * NV[sec]; k++) f.uv[k] = d.u8(e + 2 * NV[sec] + k);
                f.clut = d.u16(e + SZ[sec] - 4);
                b.faces.push_back(f);
            }
            q += 4 + (size_t)cnt * SZ[sec];
        }
    }
    return bones;
}

Keys parse_anim(Span d, int nb) {
    Keys k;
    std::vector<uint32_t> tbl;
    for (size_t i = 0;; i++) {
        uint32_t a = d.u32(4 * (1 + nb + i));
        if (a == 1) break;
        tbl.push_back(a);
    }
    std::vector<uint32_t> offs(nb);
    for (int b = 0; b < nb; b++) offs[b] = d.u32(4 + 4 * b);
    k.bone.resize(nb);
    for (int b = 0; b < nb; b++) {
        size_t end = b + 1 < nb ? offs[b + 1] : d.n;
        long cnt = end > offs[b] ? (long)((end - offs[b]) / 24) : 0;
        for (long j = 0; j < cnt; j++) {
            Key key;
            for (int c = 0; c < 12; c++) key.m[c] = d.s16(offs[b] + 24 * j + 2 * c);
            k.bone[b].push_back(key);
        }
    }
    for (size_t i = 0; i < tbl.size(); i++) {
        if (!tbl[i]) continue;
        Anim a;
        a.idx = (int)i;
        size_t p = tbl[i];
        while (!(d.u8(p) & 0x80)) {
            d.need(p, nb);
            a.frames.emplace_back(d.p + p, d.p + p + nb);
            p += nb;
        }
        a.end = d.u8(p) == 0xFF ? 0 : 1;
        a.loop = d.u8(p) == 0xFE ? d.u16(p + 1) / nb : 0;
        k.anims.push_back(std::move(a));
    }
    return k;
}

/* a key's 3x3 matrix (4.12) -> rotation (4096 = 360 deg, Rz*Ry*Rx) and scale (4096 = 1) */
void euler(const int *m, int *rot, int *scale) {
    double M[3][3], R[3][3], s[3];
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) M[r][c] = m[3 * r + c] / 4096.0;
    for (int c = 0; c < 3; c++) s[c] = std::sqrt(0 + M[0][c] * M[0][c] + M[1][c] * M[1][c] + M[2][c] * M[2][c]);
    if (s[0] == 0 || s[1] == 0 || s[2] == 0) {
        for (int i = 0; i < 3; i++) rot[i] = scale[i] = 0;
        return;
    }
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) R[r][c] = M[r][c] / s[c];
    double det = R[0][0] * (R[1][1] * R[2][2] - R[1][2] * R[2][1]) - R[0][1] * (R[1][0] * R[2][2] - R[1][2] * R[2][0]) +
                 R[0][2] * (R[1][0] * R[2][1] - R[1][1] * R[2][0]);
    if (det < 0) {
        s[0] = -s[0];
        for (int r = 0; r < 3; r++) R[r][0] = -R[r][0];
    }
    double ry = std::asin(std::max(-1.0, std::min(1.0, -R[2][0]))), rx, rz;
    if (std::fabs(R[2][0]) < 0.99999) rx = std::atan2(R[2][1], R[2][2]), rz = std::atan2(R[1][0], R[0][0]);
    else rx = std::atan2(-R[1][2], R[1][1]), rz = 0;
    double k = 4096 / (2 * 3.141592653589793);
    rot[0] = (int)std::nearbyint(rx * k), rot[1] = (int)std::nearbyint(ry * k), rot[2] = (int)std::nearbyint(rz * k);
    for (int i = 0; i < 3; i++) scale[i] = (int)std::nearbyint(std::max(-7.99, std::min(7.99, s[i])) * 4096);
}

struct Motion {
    int group, idx;
    const Keys *keys;
    const Anim *anim;
};

/* the model file's bytes and its motions [(group, anim file's bytes)] -> the .dw2m */
Bytes dw2m(const Bytes &model, const std::vector<std::pair<int, Bytes>> &motions) {
    std::vector<Bone> bones = parse_model(model);
    int nb = (int)bones.size(), w, h;
    Bytes px;
    tex(model, w, h, px);
    std::vector<Keys> keys(motions.size());
    std::vector<Motion> mot;
    for (size_t m = 0; m < motions.size(); m++) {
        keys[m] = parse_anim(motions[m].second, nb);
    }
    for (size_t m = 0; m < motions.size(); m++)
        for (const Anim &a : keys[m].anims) mot.push_back({motions[m].first, a.idx, &keys[m], &a});
    std::stable_sort(mot.begin(), mot.end(), [](const Motion &a, const Motion &b) { return a.group != b.group ? a.group < b.group : a.idx < b.idx; });
    Out o;
    o.raw("DW2M");
    o.u16(1), o.u16(nb), o.u16(w), o.u16(h);
    o.raw(px);
    o.u16(mot.size());
    for (const Motion &m : mot) o.u16(m.group), o.u16(m.idx), o.u16(m.anim->frames.size()), o.u8(m.anim->end), o.u8(0), o.u16(m.anim->loop);
    for (const Bone &b : bones) {
        o.s16(b.parent), o.u16(b.verts.size() / 3), o.u16(b.faces.size());
        for (int16_t v : b.verts) o.s16(v);
        for (const Face &f : b.faces) {
            o.u8(f.n), o.u8(f.blend);
            for (int k = 0; k < 4; k++) o.u8(f.v[k]);
            for (int k = 0; k < 8; k++) o.u8(f.uv[k]);
            o.u16(f.clut);
        }
    }
    struct Cached { bool done = false; int pos[3], rot[3], sc[3]; };
    std::map<const Keys *, std::vector<std::vector<Cached>>> cache;
    for (const Motion &m : mot) {
        auto &c = cache[m.keys];
        if (c.empty()) c.resize(nb);
        for (const Bytes &f : m.anim->frames)
            for (int bi = 0; bi < nb; bi++) {
                const std::vector<Key> &t = m.keys->bone[bi];
                size_t k = f[bi] < t.size() ? f[bi] : t.size() - 1; /* an empty track: no key at all */
                if (c[bi].size() <= (t.empty() ? 0 : k)) c[bi].resize(t.empty() ? 1 : t.size());
                Cached &e = c[bi][t.empty() ? 0 : k];
                if (!e.done) {
                    e.done = true;
                    if (t.empty()) e.pos[0] = e.pos[1] = e.pos[2] = e.rot[0] = e.rot[1] = e.rot[2] = 0, e.sc[0] = e.sc[1] = e.sc[2] = 4096;
                    else {
                        euler(t[k].m, e.rot, e.sc);
                        for (int i = 0; i < 3; i++) e.pos[i] = t[k].m[9 + i];
                    }
                }
                for (int i = 0; i < 3; i++) o.s16(e.pos[i]);
                for (int i = 0; i < 3; i++) o.s16(e.rot[i]);
                for (int i = 0; i < 3; i++) o.s16(e.sc[i]);
            }
    }
    return o.b;
}

std::string lstrip_digits(const std::string &s) {
    size_t i = 0;
    while (i < s.size() && isdigit((unsigned char)s[i])) i++;
    return s.substr(i);
}

} // namespace

std::string extract_dw2(const std::string &image, const std::string &out, ExtractProgress &prog) {
    Disc disc;
    std::string err;
    if (!disc.open(image, err)) return err;
    const auto &files = disc.files();
    uint32_t slus, slusSize;
    if (!disc.find("SLUS_011.93", slus, slusSize)) return "This is not a Digimon World 2 (USA, SLUS-01193) disc.";
    Bytes exe = disc.read(slus, slusSize);
    const int IDS = 0xE5B;
    std::vector<uint32_t> ids(IDS);
    try {
        for (int i = 0; i < IDS; i++) ids[i] = Span(exe).u32(0x33F94 + 4 * i);
    } catch (...) {
        return "This is not a Digimon World 2 (USA, SLUS-01193) disc.";
    }
    auto id_at = [&](int i) { return ids.at(i < 0 ? IDS + i : i); }; /* Python indexed from the end for a negative one */
    auto fil = [&](int i) {
        auto it = files.find(id_at(i));
        if (it == files.end()) throw std::out_of_range("no such file");
        return disc.read(it->first, it->second.second);
    };
    std::error_code ec;
    fs::path tmp = fs::u8path(out) / ".extract-tmp";
    fs::remove_all(tmp, ec);
    int ok = 0, bad = 0;
    std::string log;
    auto skipped = [&](const std::string &what) { bad++, log += (log.empty() ? "" : "\n") + what; };
    /* the Digimon table (0x28-byte records), then the effects: the progress is a share of each */
    for (int fid : {0xCB9, 0xCBA, 0xCBB}) {
        Bytes d;
        try {
            d = fil(fid);
        } catch (...) {
            return "This is not a Digimon World 2 (USA, SLUS-01193) disc.";
        }
        Span s(d);
        try {
            for (size_t o = s.u32(0);; o += 0x28) {
                if (prog.cancel) return fs::remove_all(tmp, ec), "Cancelled.";
                int k = (s.u32(o + 4) >> 1) & 0x7FFF;
                if (k == 0) break;
                Bytes tail(d.begin() + std::min<size_t>(s.u32(o), d.size()), d.end());
                std::string name = glyphs(tail, 0);
                int model = s.s16(o + 6);
                std::string label;
                try {
                    std::string path = files.at(id_at(model)).first;
                    std::string fn = path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1);
                    std::string code = fn.size() >= 5 ? fn.substr(1, fn.size() - 5) : ""; /* fn[1:-4] */
                    if (name.empty() || name.find('?') != std::string::npos) name = lstrip_digits(code).empty() ? code : lstrip_digits(code);
                    char num[16];
                    snprintf(num, sizeof num, "%03d_", k);
                    label = num + name;
                    std::vector<std::pair<uint32_t, std::pair<int, Bytes>>> afs;
                    for (int g = 0; g < 11; g++) {
                        int af = s.s16(o + 8 + 2 * g);
                        if (!af || !files.count(id_at(af))) continue;
                        uint32_t key = id_at(af);
                        auto it = std::find_if(afs.begin(), afs.end(), [&](const auto &p) { return p.first == key; });
                        if (it == afs.end()) afs.push_back({key, {g, fil(af)}});
                        else it->second = {g, fil(af)};
                    }
                    std::vector<std::pair<int, Bytes>> motions;
                    for (auto &p : afs) motions.push_back(p.second);
                    Bytes data = dw2m(fil(model), motions);
                    fs::path dst = tmp / "digimon" / label;
                    fs::create_directories(dst, ec);
                    if (!extract_write((dst / (label + ".dw2m")).u8string(), data)) throw std::runtime_error("can't write");
                    ok++;
                } catch (const std::exception &e) {
                    skipped("skipped " + label + " " + e.what());
                }
                prog.percent = 40 * (fid - 0xCB9) / 3 + 10;
            }
        } catch (const std::exception &) {
        }
    }
    /* the effects: EFFECT/<dir>/M<code>.BIN + A<code>.BIN, motion 0 */
    std::vector<std::pair<uint32_t, std::pair<std::string, uint32_t>>> fx;
    for (const auto &kv : files) {
        const std::string &path = kv.second.first;
        size_t sl = path.rfind('/');
        std::string d = sl == std::string::npos ? "" : path.substr(0, sl), m = sl == std::string::npos ? path : path.substr(sl + 1);
        if (("/" + d + "/").find("/EFFECT/") == std::string::npos || m.size() < 5 || m[0] != 'M' || m.substr(m.size() - 4) != ".BIN") continue;
        fx.push_back({kv.first, {path, kv.second.second}});
    }
    for (size_t i = 0; i < fx.size(); i++) {
        if (prog.cancel) return fs::remove_all(tmp, ec), "Cancelled.";
        prog.percent = 50 + (int)(50 * i / fx.size());
        const std::string &path = fx[i].second.first;
        size_t sl = path.rfind('/');
        std::string d = sl == std::string::npos ? "" : path.substr(0, sl), m = sl == std::string::npos ? path : path.substr(sl + 1);
        try {
            std::string code = m.substr(1, m.size() - 5);
            uint32_t al, as;
            if (!disc.find(d + "/A" + m.substr(1), al, as)) throw std::runtime_error("no motion file");
            Bytes data = dw2m(disc.read(fx[i].first, fx[i].second.second), {{0, disc.read(al, as)}});
            fs::path dst = tmp / "fx" / code;
            fs::create_directories(dst, ec);
            if (!extract_write((dst / (code + ".dw2m")).u8string(), data)) throw std::runtime_error("can't write");
            ok++;
        } catch (const std::exception &e) {
            skipped("skipped " + path + " " + e.what());
        }
    }
    static const char *const SUB[] = {"digimon", "fx"};
    if (ok == 0) return fs::remove_all(tmp, ec), "No model could be read from this disc." + (log.empty() ? "" : "\n" + log);
    if (!extract_publish(out, SUB, 2, err)) return err;
    prog.percent = 100;
    prog.set_note(std::to_string(ok) + " models written, " + std::to_string(bad) + " skipped");
    return "";
}
