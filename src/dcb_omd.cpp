/* DCB model parser (OMD0 blocks and standard TMD ones): tools/dcb/omd.py and pak.py. */
#include "dcb.h"

static long fdiv(long a, long b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); } /* Python's // */

std::vector<DcbChunk> dcb_chunks(Span b) {
    std::vector<DcbChunk> res;
    size_t o = 0;
    while (o + 8 <= b.n) {
        int id = b.s16(o), sub = b.s16(o + 2);
        long sz = b.s32(o + 4);
        if (id < 0 || sz < 0) break;
        res.push_back({id, sub, o + 8, sz});
        o += 8 + sz;
    }
    return res;
}

static void parse_part(Span d, size_t o, std::vector<DcbPart> &parts, size_t &end) {
    Span tag = d.sub(o, 4);
    if (tag.n < 4 || memcmp(tag.p, "OMD0", 4) != 0) throw std::runtime_error("no OMD0");
    size_t ptr = o + d.s32(o + 12 + 20);
    uint32_t nparts = d.u32(ptr);
    size_t p = ptr + 4;
    for (uint32_t pi = 0; pi < nparts; pi++) {
        DcbPart part;
        uint32_t nv = d.u32(p);
        p += 4;
        for (uint32_t i = 0; i < nv; i++) part.verts.push_back({d.s16(p + 8 * i), d.s16(p + 8 * i + 2), d.s16(p + 8 * i + 4)});
        p += 8 * (size_t)nv;
        uint32_t ng = d.u32(p);
        p += 4;
        std::vector<int> gcol;
        for (uint32_t g = 0; g < ng; g++) {
            uint32_t w = d.u32(p);
            p += 4;
            uint32_t c = w & 0xFF;
            for (uint32_t i = 0; i < (c ? c : 1); i++) gcol.push_back(w >> 8);
            p += 8 * (size_t)c; /* the lit normals: only the positions are exported */
        }
        for (;;) {
            uint32_t h = d.u32(p);
            p += 4;
            if (h == 0) break;
            uint32_t fans = h >> 20, strips = (h >> 8) & 0xFFF, code = h & 0xFF;
            bool quad = code & 8, tex = code & 4;
            for (int pas = 0; pas < 2; pas++) {
                uint32_t count = pas ? strips : fans;
                for (uint32_t c = 0; c < count; c++) {
                    uint32_t run = d.u32(p);
                    p += 4;
                    uint32_t L = run >> 16, S = run & 0xFFFF;
                    for (uint32_t s = 0; s < S; s++) {
                        auto vi = [](uint32_t w) { return (int)((w >> 16) / 8); };
                        size_t t0 = part.tris.size();
                        int sa0 = 0, sa1 = 0;
                        uint32_t vw0;
                        if (!quad) {
                            int n0 = tex ? 6 : 3;
                            uint32_t vw[3], raw[3] = {0, 0, 0};
                            for (int i = 0; i < 3; i++) vw[i] = d.u32(p + 4 * i);
                            if (tex)
                                for (int i = 0; i < 3; i++) raw[i] = d.u32(p + 12 + 4 * i);
                            sa0 = raw[0] >> 16, sa1 = raw[1] >> 16, vw0 = vw[0];
                            p += 4 * n0;
                            DcbRef R[3];
                            for (int i = 0; i < 3; i++) R[i] = {vi(vw[i]), (int)(raw[i] & 0xFFFF)};
                            part.tris.push_back({R[0], R[1], R[2]});
                            for (uint32_t k = 1; k < L; k++) {
                                uint32_t nw = d.u32(p), nuv = tex ? d.u32(p + 4) & 0xFFFF : 0;
                                p += tex ? 8 : 4;
                                static const int FAN[3] = {2, 0, 1};
                                int slot = pas ? FAN[(k - 1) % 3] : (k % 2 ? 2 : 1);
                                R[slot] = {vi(nw), (int)nuv};
                                if (k % 2 == 0) part.tris.push_back({R[0], R[1], R[2]});
                                else part.tris.push_back({R[0], R[2], R[1]});
                            }
                        } else {
                            int n0 = tex ? 8 : 4;
                            uint32_t vw[4], raw[4] = {0, 0, 0, 0};
                            for (int i = 0; i < 4; i++) vw[i] = d.u32(p + 4 * i);
                            if (tex)
                                for (int i = 0; i < 4; i++) raw[i] = d.u32(p + 16 + 4 * i);
                            sa0 = raw[0] >> 16, sa1 = raw[1] >> 16, vw0 = vw[0];
                            p += 4 * n0;
                            DcbRef R[4];
                            for (int i = 0; i < 4; i++) R[i] = {vi(vw[i]), (int)(raw[i] & 0xFFFF)};
                            auto emit = [&](bool flip) {
                                DcbRef a = R[0], b = R[1], c2 = R[2], e = R[3];
                                std::array<DcbRef, 3> t1 = {a, b, c2}, t2 = {b, e, c2};
                                part.tris.push_back(flip ? std::array<DcbRef, 3>{t1[0], t1[2], t1[1]} : t1);
                                part.tris.push_back(flip ? std::array<DcbRef, 3>{t2[0], t2[2], t2[1]} : t2);
                            };
                            emit(false);
                            for (uint32_t k = 1; k < L; k++) {
                                uint32_t w1 = d.u32(p), w2 = d.u32(p + 4);
                                uint32_t u1 = 0, u2 = 0;
                                if (tex) u1 = d.u32(p + 8) & 0xFFFF, u2 = d.u32(p + 12) & 0xFFFF;
                                p += tex ? 16 : 8;
                                int a = pas ? (k % 2 ? 2 : 0) : (k % 2 ? 2 : 1), b = pas ? (k % 2 ? 3 : 1) : 3;
                                R[a] = {vi(w1), (int)u1}, R[b] = {vi(w2), (int)u2};
                                emit(k % 2 == 1);
                            }
                        }
                        long gi = fdiv((long)(vw0 & 0xFFFF) - 8 * (long)nv, 4);
                        int col = 0x808080;
                        if (gi < (long)gcol.size()) {
                            if (gcol.empty() || (gi < 0 ? 0 : gi) >= (long)gcol.size()) throw std::out_of_range("gcol");
                            col = gcol[gi < 0 ? 0 : gi];
                        }
                        for (size_t i = t0; i < part.tris.size(); i++) part.attr.push_back({sa0, sa1, tex, (int)code, col});
                    }
                }
            }
        }
        parts.push_back(std::move(part));
    }
    end = p;
}

static void parse_tmd(Span d, size_t o, std::vector<DcbPart> &parts, size_t &end) {
    uint32_t nobj = d.u32(o + 8);
    size_t base = o + 12;
    end = base + 28 * (size_t)nobj;
    for (uint32_t k = 0; k < nobj; k++) {
        int vt = d.s32(base + 28 * k), nv = d.s32(base + 28 * k + 4), pt = d.s32(base + 28 * k + 16), npr = d.s32(base + 28 * k + 20);
        DcbPart part;
        for (int i = 0; i < nv; i++) part.verts.push_back({d.s16(base + vt + 8 * i), d.s16(base + vt + 8 * i + 2), d.s16(base + vt + 8 * i + 4)});
        size_t q = base + pt;
        for (int pk = 0; pk < npr; pk++) {
            int ilen = d.u8(q + 1), fl = d.u8(q + 2), mode = d.u8(q + 3);
            size_t w = q + 4;
            q += 4 + 4 * (size_t)ilen;
            bool tex = mode & 4, quad = mode & 8, lit = !(fl & 1);
            int nvx = quad ? 4 : 3;
            if ((mode & 0x10) && !lit) throw std::runtime_error("unlit gouraud TMD packet");
            int rgb = 0x808080;
            int uvs[4][3] = {};
            if (tex) {
                if (!lit) rgb = d.u32(w) & 0xFFFFFF, w += 4;
                for (int i = 0; i < nvx; i++) uvs[i][0] = d.u8(w + 4 * i), uvs[i][1] = d.u8(w + 4 * i + 1), uvs[i][2] = d.u16(w + 4 * i + 2);
                w += 4 * nvx;
            } else rgb = d.u32(w) & 0xFFFFFF, w += 4;
            int vi[4];
            for (int i = 0; i < nvx; i++) vi[i] = (mode & 0x10) ? d.u16(w + 4 * i + 2) : lit ? d.u16(w + 2 + 2 * i) : d.u16(w + 2 * i);
            DcbRef R[4];
            for (int i = 0; i < nvx; i++) R[i] = {vi[i], tex ? (uvs[i][0] | uvs[i][1] << 8) : 0};
            int cl = tex ? uvs[0][2] : 0, tp = tex ? uvs[1][2] : 0;
            part.tris.push_back({R[0], R[1], R[2]});
            part.attr.push_back({cl, tp, tex, mode, rgb});
            if (quad) part.tris.push_back({R[1], R[3], R[2]}), part.attr.push_back({cl, tp, tex, mode, rgb});
        }
        end = std::max(end, q);
        parts.push_back(std::move(part));
    }
}

std::vector<DcbBone> dcb_omd_parse(Span d) {
    int nobj = d.u16(16);
    std::vector<DcbBone> bones(nobj);
    size_t o = 52 + 12 * (size_t)nobj;
    static const uint8_t TMDID[8] = {0x41, 0, 0, 0, 0, 0, 0, 0};
    for (int i = 0; i < nobj; i++) {
        DcbBone &b = bones[i];
        b.parent = d.s8(20 + i);
        for (int c = 0; c < 3; c++) b.pos[c] = d.s16(52 + 12 * i + 4 + 2 * c);
        for (;;) {
            Span a = d.sub(o, 4), t = d.sub(o, 8);
            if ((a.n == 4 && !memcmp(a.p, "OMD0", 4)) || (t.n == 8 && !memcmp(t.p, TMDID, 8))) break;
            o += 4;
            if (o >= d.n) throw std::runtime_error("no OMD0 or TMD block");
        }
        size_t end;
        if (!memcmp(d.p + o, "OMD0", 4)) parse_part(d, o, b.parts, end);
        else parse_tmd(d, o, b.parts, end);
        o = end;
    }
    return bones;
}
