/* FIGHTSTG's 2D effects: a port of what the battle does with them
   (dw3_decomp src/fightstg/sprite_effect.c, sprite_anim.c,
   src/main/gfx/sprite_drawer.c; MIT, see CREDITS.md) */
#include "effect.h"
#include "model.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static int16_t rd16(const uint8_t *p) { int16_t v; memcpy(&v, p, 2); return v; }

/* FIGHTSTG_effectSheets (European): the images' archive (file << 16 |
   entry, 0 for none), the sheet, and the texture's VRAM position */
struct EffectSheet {
    uint32_t images, sheet;
    int x, y;
};

/* FIGHTSTG_spriteEffects (European): an effect's id, sheet and animations */
struct EffectEntry {
    int id, sheet;
    uint32_t file;
};

static const EffectSheet SHEETS[] = {
    {0, 0x79D0026, 320, 256}, {0, 0x7A30000, 448, 256}, {0, 0x7E00000, 512, 256}, {0, 0x7E20000, 576, 256},
    {0, 0, 576, 256}, {0x7F50006, 0x7F50000, 768, 256}, {0x8A20005, 0x8A20000, 768, 256}, {0x80F0002, 0x80F0000, 896, 256},
    {0x78B0002, 0x78B0000, 896, 256}, {0x80E0003, 0x80E0000, 896, 256}, {0x7890003, 0x7890000, 896, 256},
    {0x81C0002, 0x81C0001, 896, 256}, {0x7E50003, 0x7E50000, 896, 256}, {0x6D90002, 0x6D90000, 896, 256},
    {0x7E60003, 0x7E60000, 896, 256}, {0x8110002, 0x8110000, 896, 256}, {0x81D0002, 0x81D0001, 896, 256},
    {0x78C0002, 0x78C0000, 896, 256}, {0x7DB0002, 0x7DB0000, 896, 256}, {0x7E70002, 0x7E70000, 896, 256},
    {0x8120002, 0x8120000, 896, 256}, {0x7A50002, 0x7A50000, 896, 256}, {0x8190002, 0x8190000, 896, 256},
    {0x78D0003, 0x78D0000, 896, 256}, {0x78E0005, 0x78E0000, 896, 256}, {0x89D0002, 0x89D0000, 896, 256},
    {0x78F0004, 0x78F0000, 896, 256}, {0x7DC0002, 0x7DC0000, 896, 256}, {0x8130003, 0x8130000, 896, 256},
    {0x8180002, 0x8180000, 896, 256}, {0x81B0002, 0x81B0000, 896, 256}, {0x78A0003, 0x78A0000, 896, 256},
    {0x7730002, 0x7730000, 896, 256}, {0x83D0002, 0x83D0000, 896, 256}, {0x7E80003, 0x7E80000, 896, 256},
    {0x7DD0003, 0x7DD0000, 896, 256}, {0x7DE0002, 0x7DE0000, 896, 256}, {0x7E90004, 0x7E90000, 896, 256},
    {0x8010002, 0x8010000, 896, 256}, {0x7DF0002, 0x7DF0000, 896, 256}, {0x8030002, 0x8030000, 896, 256},
    {0x7900001, 0x7900000, 896, 256}, {0x8050002, 0x8050000, 896, 256}, {0x8020003, 0x8020000, 896, 256},
    {0x8900002, 0x8900000, 896, 256}, {0x8890002, 0x8890000, 896, 256}, {0x88A0004, 0x88A0000, 896, 256},
    {0x8880002, 0x8880000, 896, 256}, {0x88B0004, 0x88B0000, 896, 256}, {0x8100002, 0x8100000, 896, 256},
    {0x81A0002, 0x81A0000, 896, 256},
};
static const EffectEntry ENTRIES[] = {
    {0, 0, 0x79D0000}, {1, 0, 0x79D0001}, {2, 0, 0x79D0002}, {19, 0, 0x79D0003}, {20, 0, 0x79D0028}, {21, 0, 0x79D0004},
    {22, 0, 0x79D0005}, {23, 0, 0x79D0006}, {24, 0, 0x79D0007}, {25, 0, 0x79D0027}, {26, 0, 0x79D0008}, {27, 0, 0x79D0009},
    {28, 0, 0x79D000A}, {29, 0, 0x79D000B}, {31, 0, 0x79D000C}, {32, 0, 0x79D000D}, {42, 0, 0x79D0017}, {43, 0, 0x79D0018},
    {44, 0, 0x79D0019}, {45, 0, 0x79D001A}, {46, 0, 0x79D001B}, {47, 0, 0x79D001C}, {57, 0, 0x79D0024}, {58, 0, 0x79D0025},
    {33, 0, 0x79D000E}, {34, 0, 0x79D000F}, {35, 0, 0x79D0010}, {36, 0, 0x79D0011}, {37, 0, 0x79D0012}, {38, 0, 0x79D0013},
    {39, 0, 0x79D0014}, {40, 0, 0x79D0015}, {41, 0, 0x79D0016}, {50, 0, 0x79D001D}, {51, 0, 0x79D001E}, {52, 0, 0x79D001F},
    {53, 0, 0x79D0020}, {54, 0, 0x79D0021}, {55, 0, 0x79D0022}, {56, 0, 0x79D0023}, {13, 1, 0x7A30001}, {14, 1, 0x7A30002},
    {15, 1, 0x7A30003}, {16, 1, 0x7A30004}, {17, 1, 0x7A30005}, {18, 1, 0x7A30006}, {64, 1, 0x7A30008}, {65, 1, 0x7A30009},
    {3, 2, 0x7E00001}, {4, 2, 0x7E00002}, {5, 2, 0x7E00003}, {6, 2, 0x7E00004}, {7, 2, 0x7E00005}, {8, 2, 0x7E00006},
    {9, 2, 0x7E00007}, {10, 2, 0x7E00008}, {11, 2, 0x7E00009}, {12, 2, 0x7E0000A}, {63, 2, 0x7E0000B}, {62, 3, 0x7E2000B},
    {1009, 3, 0x7E20009}, {1012, 3, 0x7E20003}, {1013, 3, 0x7E20004}, {1014, 3, 0x7E20005}, {1017, 3, 0x7E20008},
    {1020, 3, 0x7E2000A}, {1004, 5, 0x7F50001}, {1005, 5, 0x7F50002}, {1006, 5, 0x7F50003}, {1000, 6, 0x8A20001},
    {1001, 6, 0x8A20002}, {1002, 6, 0x8A20003}, {1003, 6, 0x8A20004}, {1007, 6, 0x8A20006}, {1008, 6, 0x8A20007},
    {1010, 7, 0x80F0001}, {59, 9, 0x80E0001}, {60, 9, 0x80E0002}, {1018, 9, 0x80E0006}, {1019, 9, 0x80E0007},
    {2040, 8, 0x78B0001}, {2036, 10, 0x7890001}, {2037, 10, 0x7890002}, {2058, 11, 0x81C0000}, {1021, 12, 0x7E50004},
    {2027, 12, 0x7E50001}, {2028, 12, 0x7E50002}, {2043, 13, 0x6D90001}, {2011, 14, 0x7E60001}, {2012, 14, 0x7E60002},
    {1015, 15, 0x8110001}, {2059, 15, 0x8110003}, {2060, 16, 0x81D0000}, {2056, 17, 0x78C0003}, {2031, 17, 0x78C0001},
    {2014, 18, 0x7DB0001}, {2026, 19, 0x7E70001}, {1016, 20, 0x8120001}, {2044, 21, 0x7A50001}, {1022, 22, 0x8190001},
    {2019, 23, 0x78D0001}, {2020, 23, 0x78D0002}, {2008, 24, 0x78E0002}, {2009, 24, 0x78E0003}, {2010, 24, 0x78E0004},
    {2062, 25, 0x89D0001}, {2000, 26, 0x78F0001}, {2001, 26, 0x78F0002}, {2002, 26, 0x78F0003}, {2004, 27, 0x7DC0001},
    {2046, 28, 0x8130001}, {2047, 28, 0x8130002}, {2061, 29, 0x8180001}, {2057, 30, 0x81B0001}, {2029, 31, 0x78A0001},
    {2030, 31, 0x78A0002}, {2032, 32, 0x7730001}, {2007, 33, 0x83D0001}, {2041, 34, 0x7E80001}, {2042, 34, 0x7E80002},
    {2024, 35, 0x7DD0001}, {2025, 35, 0x7DD0002}, {2005, 36, 0x7DE0001}, {2006, 36, 0x7DE0003}, {2016, 37, 0x7E90001},
    {2017, 37, 0x7E90002}, {2018, 37, 0x7E90003}, {2038, 38, 0x8010003}, {2039, 38, 0x8010004}, {2003, 39, 0x7DF0001},
    {2015, 40, 0x8030001}, {2021, 41, 0x7900002}, {2022, 41, 0x7900003}, {2023, 41, 0x7900004}, {2013, 42, 0x8050001},
    {2033, 43, 0x8020001}, {2034, 43, 0x8020002}, {2035, 44, 0x8900001}, {2049, 45, 0x8890001}, {2050, 46, 0x88A0001},
    {2051, 46, 0x88A0002}, {2052, 46, 0x88A0003}, {2048, 47, 0x8880001}, {2053, 48, 0x88B0001}, {2054, 48, 0x88B0002},
    {2055, 48, 0x88B0003}, {1011, 49, 0x8100001}, {2045, 50, 0x81A0001},
};

/* the common sheets' images, which the battle menu loads (WFIGHTMN): file,
   VRAM x, at y 256 */
static const int COMMON_IMAGES[4][2] = {{0x79E, 0x140}, {0x7AE, 0x1C0}, {0x7E1, 0x200}, {0x7E3, 0x240}};

std::vector<int> effect_ids() {
    std::vector<int> out;
    for (const EffectEntry &e : ENTRIES) out.push_back(e.id);
    return out;
}

static std::vector<uint8_t> load_fid(const std::string &files, int fid) {
    char b[16];
    snprintf(b, sizeof b, "/%04d.bin", fid);
    std::ifstream f(files + b, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

static Archive entry_of(const std::vector<uint8_t> &file, int i) {
    Archive a{file.data(), file.size()}, s;
    s.data = a.entry(i);
    s.size = s.data ? a.entry_size(i) : 0;
    return s;
}

void EffectAnim::at(int time, int16_t out[9]) const {
    memcpy(out, init, sizeof init);
    size_t o = (size_t)time * stride;
    for (int bit = 0; bit < 9; bit++) {
        if (!(flags >> bit & 1)) continue;
        if (o < data.size()) out[bit] = data[o];
        o++;
    }
}

bool Effect::load(int want, const std::string &files) {
    *this = Effect();
    const EffectEntry *e = nullptr;
    for (const EffectEntry &x : ENTRIES)
        if (x.id == want) e = &x;
    if (!e) {
        error = "no effect " + std::to_string(want);
        return false;
    }
    const EffectSheet &s = SHEETS[e->sheet];
    id = want, sheetIndex = e->sheet;
    fid = e->file >> 16, entry = e->file & 0xFFFF;
    sheetFid = s.sheet >> 16, sheetEntry = s.sheet & 0xFFFF;
    texX = s.x, texY = s.y;
    vram.assign(1024 * 512, 0);

    /* the images: the battle's own for the common sheets, else the effect's
       archive (FIGHTSTG_runScriptEffect) */
    std::vector<uint8_t> imgFile;
    Archive images;
    int imgX = texX;
    if (e->sheet < 4) {
        imgFile = load_fid(files, COMMON_IMAGES[e->sheet][0]);
        images = Archive{imgFile.data(), imgFile.size()};
        imgX = COMMON_IMAGES[e->sheet][1];
    } else if (s.images) {
        imgFile = load_fid(files, s.images >> 16);
        images = entry_of(imgFile, s.images & 0xFFFF);
    }
    std::vector<Tim> tims;
    if (images.data) read_tims(images, imgX, texY, tims);
    for (const Tim &t : tims)
        for (int y = 0; y < t.h; y++)
            for (int x = 0; x < t.w; x++) vram[((t.y + y) & 511) * 1024 + ((t.x + x) & 1023)] = t.pixels[y * t.w + x];

    std::vector<uint8_t> sf = load_fid(files, sheetFid);
    Archive sh = entry_of(sf, sheetEntry);
    if (!sh.data || sh.size < 8) {
        error = "no sheet in file " + std::to_string(sheetFid);
        return false;
    }
    sheet.assign(sh.data, sh.data + sh.size);

    std::vector<uint8_t> af = load_fid(files, fid);
    Archive arc = entry_of(af, entry);
    if (!arc.data) {
        error = "no animations in file " + std::to_string(fid);
        return false;
    }
    /* up to 30, up to an offset of 0 (FIGHTSTG_updateSpriteEffect) */
    for (int i = 0; i < 30 && i < arc.count() && rd32(arc.data + i * 4); i++) {
        const uint8_t *p = arc.entry(i);
        size_t n = arc.entry_size(i) / 2;
        if (!p || n < 12) break;
        EffectAnim a;
        a.flags = rd16(p), a.stride = rd16(p + 2), a.duration = rd16(p + 4);
        for (int k = 0; k < 9; k++) a.init[k] = rd16(p + 6 + k * 2);
        for (size_t k = 12; k < n; k++) a.data.push_back(rd16(p + k * 2));
        duration = std::max(duration, a.duration);
        anims.push_back(std::move(a));
    }
    if (anims.empty()) {
        error = "effect " + std::to_string(want) + " has no animations";
        return false;
    }
    return true;
}

/* A sheet's frame (spriteDrawerDraw): its parts, each a rectangle of the
   texture at an offset from x, y, scaled and turned about x, y */
static void draw_frame(const Effect &fx, int frame, int x, int y, int clutRow, const int16_t *scale, const int16_t *rot,
                       uint32_t *rgba, int w, int h) {
    const uint8_t *s = fx.sheet.data();
    size_t size = fx.sheet.size();
    uint32_t partsOff = rd32(s), idsOff = rd32(s + 4);
    if (idsOff >= size || partsOff >= size) return;
    size_t i = 0;
    while (idsOff + i < size && s[idsOff + i] != (uint8_t)frame) i++;
    if (idsOff + i >= size || (i + 3) * 4 > size) return;
    uint32_t fo = rd32(s + (i + 2) * 4);
    if (fo + 6 > size) return;
    int count = rd16(s + fo), frameRow = rd16(s + fo + 2), abr = rd16(s + fo + 4);

    /* RotMatrixYXZ, then ScaleMatrix's columns; only x and y reach the screen */
    auto ang = [](int a) { return a * 6.28318530718f / 4096; };
    float cx = cosf(ang(rot[0])), sx = sinf(ang(rot[0])), cy = cosf(ang(rot[1])), sy = sinf(ang(rot[1]));
    float cz = cosf(ang(rot[2])), sz = sinf(ang(rot[2]));
    float m00 = cy * cz + sy * sx * sz, m01 = -cy * sz + sy * sx * cz;
    float m10 = cx * sz, m11 = cx * cz;
    float a = m00 * scale[0] / 4096, b = m01 * scale[1] / 4096, c = m10 * scale[0] / 4096, d = m11 * scale[1] / 4096;
    float det = a * d - b * c;
    if (fabsf(det) < 1e-6f) return;

    for (int k = 0; k < count; k++) {
        size_t t = fo + 6 + k * 6;
        if (t + 6 > size) return;
        int idx = rd16(s + t), px = rd16(s + t + 2), py = rd16(s + t + 4);
        size_t po = partsOff + idx * 14;
        if (po + 14 > size) continue;
        int u = rd16(s + po), v = rd16(s + po + 2), pw = rd16(s + po + 4), ph = rd16(s + po + 6);
        int pcx = rd16(s + po + 8), pcy = rd16(s + po + 10), mode = rd16(s + po + 12);
        /* an 8-bit part reads the drawer's other CLUT, which FIGHTSTG leaves at 0, 0 */
        int clx = (mode ? 0 : fx.texX) + pcx, cly = (mode ? 0 : fx.texY) + pcy + frameRow + clutRow;
        float xs[4], ys[4];
        for (int q = 0; q < 4; q++) {
            float lx = (float)(px + (q & 1) * pw), ly = (float)(py + (q >> 1) * ph);
            xs[q] = a * lx + b * ly, ys[q] = c * lx + d * ly;
        }
        int x0 = (int)floorf(*std::min_element(xs, xs + 4)), x1 = (int)ceilf(*std::max_element(xs, xs + 4));
        int y0 = (int)floorf(*std::min_element(ys, ys + 4)), y1 = (int)ceilf(*std::max_element(ys, ys + 4));
        for (int Y = y0; Y < y1; Y++) {
            int sy_ = y + Y;
            if (sy_ < 0 || sy_ >= h) continue;
            for (int X = x0; X < x1; X++) {
                int sx_ = x + X;
                if (sx_ < 0 || sx_ >= w) continue;
                float ox = X + 0.5f, oy = Y + 0.5f;
                float lx = (d * ox - b * oy) / det - px, ly = (-c * ox + a * oy) / det - py;
                if (lx < 0 || ly < 0 || lx >= pw || ly >= ph) continue;
                int uu = u + (int)lx, vv = (v + (int)ly) & 255;
                uint16_t word, col;
                int ci;
                if (mode == 0) {
                    word = fx.vram[((fx.texY + vv) & 511) * 1024 + ((fx.texX + uu / 4) & 1023)];
                    ci = word >> (uu & 3) * 4 & 15;
                } else {
                    word = fx.vram[((fx.texY + vv) & 511) * 1024 + ((fx.texX + uu / 2) & 1023)];
                    ci = word >> (uu & 1) * 8 & 255;
                }
                col = fx.vram[(cly & 511) * 1024 + ((clx + ci) & 1023)];
                if (col == 0) continue;
                int r = (col & 31) << 3, g = (col >> 5 & 31) << 3, bl = (col >> 10 & 31) << 3;
                uint32_t &o = rgba[sy_ * w + sx_];
                int orr = o & 255, og = o >> 8 & 255, ob = o >> 16 & 255;
                if (abr != -1 && (col & 0x8000)) {
                    switch (abr & 3) {
                    case 0: r = (orr + r) / 2, g = (og + g) / 2, bl = (ob + bl) / 2; break;
                    case 1: r += orr, g += og, bl += ob; break;
                    case 2: r = orr - r, g = og - g, bl = ob - bl; break;
                    default: r = orr + r / 4, g = og + g / 4, bl = ob + bl / 4; break;
                    }
                    r = std::clamp(r, 0, 255), g = std::clamp(g, 0, 255), bl = std::clamp(bl, 0, 255);
                }
                o = (uint32_t)r | (uint32_t)g << 8 | (uint32_t)bl << 16 | 0xFF000000u;
            }
        }
    }
}

void Effect::draw(int time, uint32_t *rgba, int w, int h, int ox, int oy) const {
    /* each animation adds its sprites at the front of the same ordering
       table entry: the last one's are drawn first */
    for (int i = (int)anims.size() - 1; i >= 0; i--) {
        const EffectAnim &an = anims[i];
        if (time >= an.duration) continue;
        int16_t v[9];
        an.at(time, v);
        if (v[4] == 0 || v[5] == 0) continue; /* scaled to nothing: not drawn */
        draw_frame(*this, v[0], ox + v[2], oy + v[3], v[1], v + 4, v + 6, rgba, w, h);
    }
}
