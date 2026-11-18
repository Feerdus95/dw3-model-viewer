/* A DCB Digimon PAK -> .dcbm (tools/dcb/dcblib.py and todcbm.py): its mesh, its texture page with the palettes below, and
   every motion replayed through the game's own key easing (model_anim.c: setupRotationCurve / accelerateModelBones). */
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <unordered_map>

#include "dcb.h"
#include "extract.h"

namespace {

const double PI = 3.141592653589793;

int64_t cdiv(int64_t a, int64_t b) { /* C integer division */
    if (b == 0) throw std::runtime_error("division by zero");
    return a / b; /* C++ truncates toward zero too */
}

struct Bits {
    Span b;
    size_t o;
    int left = 0, cur = 0;
    int bit() {
        if (--left >= 0) return (cur >> left) & 1;
        left = 7, cur = b.u8(o++);
        return cur >> 7;
    }
    uint32_t bits(int n) {
        uint32_t v = 0;
        while (n--) v = v << 1 | bit();
        return v;
    }
};

struct KeyBone { int v[10]; }; /* rx ry rz attr tx ty tz sx sy sz */
struct AnimKey { int dur, snd; KeyBone bone[32]; };

void read_anim(Span b, int &loop, std::vector<AnimKey> &keys) {
    int n = b.s32(0);
    for (int k = 0; k < n; k++) {
        size_t o = 4 + (size_t)k * 772;
        AnimKey a;
        a.dur = b.s16(o), a.snd = b.s16(o + 2);
        for (int i = 0; i < 32; i++) {
            int v[12];
            for (int c = 0; c < 12; c++) v[c] = b.s16(o + 4 + i * 24 + 2 * c);
            int pick[10] = {0, 1, 2, 3, 4, 5, 6, 8, 9, 10};
            for (int c = 0; c < 10; c++) a.bone[i].v[c] = v[pick[c]];
        }
        keys.push_back(a);
    }
    loop = b.s16(4 + 4 + 22);
}

struct Chan { int64_t v = 0, vel = 0, a0 = 0, a1 = 0; };
struct Pose { int pos[3]; int64_t rot[3]; int sc[3]; int attr; };
struct Clip {
    int sub;
    std::vector<std::array<Pose, 32>> frames;
    bool looped;
    int loopStart;
};

int i16(int64_t v) { return (int)((((v >> 16) + 0x8000) & 0xFFFF) - 0x8000); }

Clip sim(const std::vector<AnimKey> &keys, int loop) {
    const int nb = 32;
    Clip clip;
    std::vector<std::array<Chan, 9>> ch(nb);
    int attr[32];
    for (int b = 0; b < nb; b++) {
        const KeyBone &kb = keys.at(0).bone[b];
        for (int i = 0; i < 3; i++) ch[b][i].v = (int64_t)kb.v[i] * ((int64_t)1 << 20), ch[b][3 + i].v = (int64_t)kb.v[4 + i] * ((int64_t)1 << 16), ch[b][6 + i].v = (int64_t)kb.v[7 + i] * ((int64_t)1 << 16);
        attr[b] = kb.v[3];
    }
    int n = (int)keys.size();
    clip.looped = loop >= 0, clip.loopStart = 0;
    if (n == 1) {
        std::array<Pose, 32> f;
        for (int b = 0; b < nb; b++) {
            const KeyBone &k = keys[0].bone[b];
            f[b] = {{k.v[4], k.v[5], k.v[6]}, {k.v[0], k.v[1], k.v[2]}, {k.v[7], k.v[8], k.v[9]}, k.v[3]};
        }
        clip.frames.push_back(f);
        clip.looped = false;
        return clip;
    }
    int idx = 1, tick = 0;
    int64_t timer = 0, half = 0;
    std::vector<int> wraps;
    auto load = [&](int m) {
        if (idx < 0) return void(timer = -1);
        if (!(m == 0 && idx + 1 < n) && loop >= 0) wraps.push_back(tick);
        half = keys.at(idx).dur;
        int64_t length = half * 2;
        timer = length;
        int to = (m == 0 && idx + 1 < n) ? idx + 1 : loop;
        int64_t nl = 1;
        const AnimKey *nk = &keys[idx];
        if (to >= 0) nl = keys.at(to).dur * (int64_t)2, nk = &keys[to];
        int cur = idx;
        idx = to >= 0 ? to : -1;
        for (int b = 0; b < nb; b++) {
            const KeyBone &k = keys[cur].bone[b], &nx = nk->bone[b];
            attr[b] = k.v[3];
            const int ix[9] = {0, 1, 2, 4, 5, 6, 7, 8, 9};
            for (int i = 0; i < 9; i++) {
                Chan &c = ch[b][i];
                int sh = i < 3 ? 20 : 16;
                int64_t frm = i < 3 ? cdiv(c.v, 0x100000) : ((((c.v >> 16) + 0x8000) & 0xFFFF) - 0x8000);
                int64_t start = frm * ((int64_t)1 << sh);
                c.v = start;
                int64_t tgt = k.v[ix[i]], nxt = nx.v[ix[i]];
                int64_t mid = tgt * ((int64_t)1 << sh) - start, slope = cdiv(mid, length);
                int64_t ev = cdiv(cdiv((nxt - tgt) * ((int64_t)1 << sh), nl) + slope, 2);
                int64_t mv = slope * 2 - cdiv(ev + c.vel, 2);
                c.a0 = half ? cdiv(mv - c.vel, half) : 0;
                c.a1 = half ? cdiv(ev - mv, half) : 0;
            }
        }
    };
    load(0);
    int64_t hp = half;
    bool ended = false;
    for (;;) {
        timer--;
        if (timer <= 0) {
            load(0);
            if (idx < 0 && timer < 0) ended = true;
            hp = half;
        }
        if (ended || wraps.size() >= 3) break;
        if (tick % 2 == 0) {
            std::array<Pose, 32> f;
            for (int b = 0; b < nb; b++) {
                const auto &c = ch[b];
                f[b] = {{i16(c[3].v), i16(c[4].v), i16(c[5].v)}, {cdiv(c[0].v, 0x100000), cdiv(c[1].v, 0x100000), cdiv(c[2].v, 0x100000)}, {i16(c[6].v), i16(c[7].v), i16(c[8].v)}, attr[b]};
            }
            clip.frames.push_back(f);
        }
        for (auto &c : ch)
            for (Chan &x : c) x.v += x.vel;
        hp--;
        for (auto &c : ch)
            for (Chan &x : c) x.vel += hp >= 0 ? x.a0 : x.a1;
        tick++;
        if (tick > 6000) break;
    }
    clip.loopStart = wraps.size() >= 3 ? wraps[1] / 2 : 0;
    return clip;
}

/* a rotation in the game's order (Y X Z) -> the viewer's (Rz*Ry*Rx), in 4096 units */
struct E3 { int v[3]; };
E3 rot_euler(int64_t rx, int64_t ry, int64_t rz) {
    typedef double M3[3][3];
    auto mul = [](const M3 &A, const M3 &B, M3 &C) {
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) C[i][j] = 0.0 + A[i][0] * B[0][j] + A[i][1] * B[1][j] + A[i][2] * B[2][j]; /* Python's sum() starts from 0 (and 0 + -0.0 is 0.0) */
    };
    double f = 2 * PI / 4096;
    double cx = std::cos(rx * f), sx = std::sin(rx * f), cy = std::cos(ry * f), sy = std::sin(ry * f), cz = std::cos(rz * f), sz = std::sin(rz * f);
    M3 Y = {{cy, 0, sy}, {0, 1, 0}, {-sy, 0, cy}}, X = {{1, 0, 0}, {0, cx, -sx}, {0, sx, cx}}, Z = {{cz, -sz, 0}, {sz, cz, 0}, {0, 0, 1}}, T, R;
    mul(Y, X, T), mul(T, Z, R);
    double k = 4096 / (2 * PI), ry2 = std::asin(std::max(-1.0, std::min(1.0, -R[2][0]))), rx2, rz2;
    if (std::fabs(R[2][0]) < 0.99999) rx2 = std::atan2(R[2][1], R[2][2]), rz2 = std::atan2(R[1][0], R[0][0]);
    else rx2 = std::atan2(-R[1][2], R[1][1]), rz2 = 0;
    return {{(int)std::nearbyint(rx2 * k), (int)std::nearbyint(ry2 * k), (int)std::nearbyint(rz2 * k)}};
}

E3 rot_cached(int64_t rx, int64_t ry, int64_t rz) {
    thread_local std::unordered_map<unsigned long long, E3> cache;
    unsigned long long key = ((unsigned long long)(rx & 0xFFFFF) << 40) ^ ((unsigned long long)(ry & 0xFFFFF) << 20) ^ (unsigned long long)(rz & 0xFFFFF);
    bool small = rx >= -0x80000 && rx < 0x80000 && ry >= -0x80000 && ry < 0x80000 && rz >= -0x80000 && rz < 0x80000;
    if (!small) return rot_euler(rx, ry, rz);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    return cache[key] = rot_euler(rx, ry, rz);
}

} // namespace

Bytes dcb_decompress(Span b, size_t off) {
    Bits r{b, off};
    uint32_t size = r.bits(16) << 16;
    size |= r.bits(16);
    if (size > (256u << 20)) throw std::runtime_error("bad size");
    Bytes out;
    uint8_t win[0x1000] = {};
    size_t wp = 0xFEE;
    int64_t decoded = 0x1000;
    int root = 0, nxt = 0x110;
    std::vector<int> L(0x400), R(0x400);
    std::function<int()> tree = [&]() -> int {
        if (r.bit()) {
            int n = nxt++;
            if (n - 0x110 >= (int)L.size()) throw std::runtime_error("bad tree");
            L[n - 0x110] = tree();
            R[n - 0x110] = tree();
            return n;
        }
        return (int)r.bits(9);
    };
    while (out.size() < size) {
        if (decoded == 0x1000) nxt = 0x110, root = tree(), decoded = 0;
        int s = root;
        while (s >= 0x110) s = r.bit() ? R.at(s - 0x110) : L.at(s - 0x110);
        decoded++;
        if (s < 0x100) out.push_back(s), win[wp] = s, wp = (wp + 1) & 0xFFF;
        else {
            int n = s - 0xFD;
            size_t o2 = r.bits(12);
            for (int i = 0; i < n; i++) {
                uint8_t c = win[(o2 + i) & 0xFFF];
                out.push_back(c), win[wp] = c, wp = (wp + 1) & 0xFFF;
            }
        }
    }
    out.resize(size);
    return out;
}

void dcb_convert(Span b, const std::string &outdir, const std::string &name) {
    namespace fs = std::filesystem;
    std::vector<DcbChunk> cs = dcb_chunks(b);
    auto first = [&](int id) {
        for (const DcbChunk &c : cs)
            if (c.id == id) return c;
        throw std::runtime_error("no chunk");
    };
    DcbChunk mc = first(0), tc = first(5);
    std::vector<DcbBone> B = dcb_omd_parse(b.sub(mc.off, mc.size));
    Span t = b.sub(tc.off, tc.size);
    uint32_t bl = t.u32(8);
    int cy = t.u16(14), cw = t.u16(16), chh = t.u16(18);
    t.need(20, 2 * (size_t)cw * chh); /* a bad header (a background, not a Digimon) must fail before it allocates */
    std::vector<std::vector<int>> pal(chh, std::vector<int>(cw));
    for (int r = 0; r < chh; r++)
        for (int i = 0; i < cw; i++) pal[r][i] = t.u16(20 + 2 * (size_t)cw * r + 2 * i);
    size_t o = 8 + bl;
    int ix = t.u16(o + 4), iw = t.u16(o + 8), ih = t.u16(o + 10);
    Span img = t.sub(o + 12, (size_t)iw * 2 * ih);
    const int W = 256, H = ih, ox = (ix % 64) * 2;
    Out hdr;
    hdr.raw("DCBM");
    hdr.u16(1), hdr.u16(B.size()), hdr.u16(W), hdr.u16(H + 4);
    for (int y = 0; y < ih; y++) { /* the image where the tpage puts it, rows padded to the page */
        uint8_t row[128] = {};
        Span piece = img.sub((size_t)y * iw * 2, (size_t)iw * 2).sub(0, ox <= 128 ? 128 - ox : 0);
        for (size_t i = 0; i < piece.n && ox + i < 128; i++) row[ox + i] = piece.p[i];
        hdr.raw(row, 128);
    }
    uint8_t pr[4][128] = {};
    for (int r = 0; r < chh && r < 16; r++)
        for (int i = 0; i < 16 && i < cw; i++) pr[r / 4][(r % 4) * 32 + 2 * i] = pal[r][i] & 255, pr[r / 4][(r % 4) * 32 + 2 * i + 1] = pal[r][i] >> 8;
    for (auto &r : pr) hdr.raw(r, 128);
    const int nb = (int)B.size();
    if (nb > 32) throw std::runtime_error("more than 32 bones");
    std::vector<Clip> clips;
    for (const DcbChunk &c : cs) {
        if (c.id != 1 || c.size < 16) continue; /* a 4-byte stub: the clip lives in another model's PAK */
        Span raw = b.sub(c.off, c.size);
        int n0 = raw.s32(0), loop;
        Bytes dec;
        bool plain = 0 < n0 && n0 < 5000 && c.size == 4 + (int64_t)n0 * 772; /* some PAKs keep the clips uncompressed */
        if (!plain) dec = dcb_decompress(b, c.off);
        std::vector<AnimKey> keys;
        read_anim(plain ? raw : Span(dec), loop, keys);
        Clip cl = sim(keys, loop);
        cl.sub = c.sub;
        clips.push_back(std::move(cl));
    }
    std::stable_sort(clips.begin(), clips.end(), [](const Clip &a, const Clip &b2) { return a.sub < b2.sub; });
    /* which bones lose their mesh: its own attribute, or itself or an ancestor collapsed to scale 0 (the children go with it) */
    auto collapsed = [&](const std::array<Pose, 32> &f) {
        std::vector<int> out(nb, -1);
        std::function<bool(int, int)> up = [&](int i, int depth) -> bool {
            if (depth > nb) throw std::runtime_error("parent cycle");
            if (out[i] < 0) {
                bool z = std::max({std::abs(f[i].sc[0]), std::abs(f[i].sc[1]), std::abs(f[i].sc[2])}) <= 48;
                int p = B[i].parent;
                if (p >= nb) throw std::out_of_range("parent");
                out[i] = z || (p >= 0 && up(p, depth + 1));
            }
            return out[i];
        };
        for (int i = 0; i < nb; i++) up(i, 0);
        return out;
    };
    std::vector<std::vector<std::vector<int>>> hid(clips.size());
    hdr.u16(clips.size());
    for (size_t c = 0; c < clips.size(); c++) {
        hdr.u16(0), hdr.u16(clips[c].sub), hdr.u16(clips[c].frames.size()), hdr.u8(clips[c].looped ? 1 : 0), hdr.u8(0), hdr.u16(clips[c].loopStart);
        for (const auto &f : clips[c].frames) {
            std::vector<int> col = collapsed(f), h(nb);
            for (int i = 0; i < nb; i++) h[i] = (f[i].attr != 0) || col[i];
            hid[c].push_back(h);
        }
    }
    for (const DcbBone &bn : B) {
        std::vector<std::array<int, 3>> verts;
        Out faces;
        size_t nf = 0;
        for (const DcbPart &pt : bn.parts) {
            size_t vo = verts.size();
            verts.insert(verts.end(), pt.verts.begin(), pt.verts.end());
            for (size_t i = 0; i < pt.tris.size(); i++) {
                const DcbAttr &a = pt.attr.at(i);
                int r = (a.cl >> 6) - cy;
                if (a.tex && !(0 <= r && r < (int)pal.size() && r < 16)) r = 0;
                int clut = ((H + (r >= 0 ? r / 4 : -((3 - r) / 4))) << 6) | (((r % 4) + 4) % 4);
                faces.u8((a.tex ? 2 : 0) | ((a.code & 2) ? 4 : 0));
                for (int k = 0; k < 3; k++) faces.u16(pt.tris[i][k].vi + vo);
                faces.u16(0);
                for (int k = 0; k < 3; k++) faces.u8(pt.tris[i][k].uv & 0xFF), faces.u8((pt.tris[i][k].uv >> 8) & 0xFF);
                faces.u8(0), faces.u8(0);
                faces.u16(a.tex ? clut : 0);
                faces.u8(a.col & 0xFF), faces.u8((a.col >> 8) & 0xFF), faces.u8((a.col >> 16) & 0xFF);
                nf++;
            }
        }
        hdr.s16(bn.parent), hdr.u16(verts.size()), hdr.u16(nf);
        for (auto &v : verts) hdr.s16(v[0]), hdr.s16(v[1]), hdr.s16(v[2]);
        hdr.raw(faces.b);
    }
    for (const Clip &cl : clips)
        for (const auto &f : cl.frames)
            for (int bi = 0; bi < nb; bi++) {
                const Pose &p = f[bi];
                const int *bp = B[bi].pos;
                int sc[3] = {p.sc[0], p.sc[1], p.sc[2]};
                if (std::max({std::abs(sc[0]), std::abs(sc[1]), std::abs(sc[2])}) <= 48) sc[0] = sc[1] = sc[2] = 4096;
                E3 e = rot_cached(p.rot[0], p.rot[1], p.rot[2]);
                for (int i = 0; i < 3; i++) hdr.s16(bp[i] + p.pos[i]);
                for (int i = 0; i < 3; i++) hdr.s16(e.v[i]);
                for (int i = 0; i < 3; i++) hdr.s16(sc[i]);
            }
    for (const auto &c : hid) /* then, a byte a bone a frame: 1 while the mesh is not drawn */
        for (const auto &h : c)
            for (int bi = 0; bi < nb; bi++) hdr.u8(h[bi] ? 1 : 0);
    fs::path d = fs::u8path(outdir) / "digimon" / fs::u8path(name);
    std::error_code ec;
    fs::create_directories(d, ec);
    if (!extract_write((d / fs::u8path(name + ".dcbm")).u8string(), hdr.b)) throw std::runtime_error("can't write " + name);
}
