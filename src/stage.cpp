/* A fight stage, edited (stage.h): read from the game's model, an OBJ or
   other stages, and built back in the game's format (FIGHTSTG_createModel,
   createMesh, drawMesh and timLoaderLoadArchive's reading; dw3_decomp, MIT,
   see CREDITS.md) */
#include "stage.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <tuple>

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static void put16(std::vector<uint8_t> &b, int v) { b.push_back((uint8_t)v), b.push_back((uint8_t)(v >> 8)); }
static void put32(std::vector<uint8_t> &b, uint32_t v) { for (int i = 0; i < 4; i++) b.push_back((uint8_t)(v >> i * 8)); }
static void set32(std::vector<uint8_t> &b, size_t at, uint32_t v) { for (int i = 0; i < 4; i++) b[at + i] = (uint8_t)(v >> i * 8); }
static void pad4(std::vector<uint8_t> &b) { while (b.size() & 3) b.push_back(0); }

std::vector<StageInfo> read_stage_infos(const std::vector<uint8_t> &t) {
    std::vector<StageInfo> out;
    for (size_t o = 0; o + 0x58 <= t.size() && rd32(&t[o]) != 0; o += 0x58) {
        StageInfo s;
        s.id = (int)out.size();
        s.fid = rd32(&t[o]) >> 16, s.boneEntry = rd32(&t[o]) & 0xFFFF;
        memcpy(s.bg, &t[o + 0x0C], 3);
        memcpy(s.noBounds, &t[o + 0x10], 8);
        out.push_back(s);
    }
    return out;
}

void write_stage_info(std::vector<uint8_t> &t, const StageInfo &s) {
    size_t o = (size_t)s.id * 0x58;
    if (s.id < 0 || o + 0x58 > t.size()) return;
    memcpy(&t[o + 0x0C], s.bg, 3);
    memcpy(&t[o + 0x10], s.noBounds, 8);
}

const char *stage_look(int id) {
    static const char *LOOK[] = {
        "grass plateaus, clouds", "grass plain, mesas, clouds", "beach, sea, rock pillars", "sandy ruins, hazy sky",
        "rock ledge on a mountain", "desert dunes", "dark stone shrine", "brick sewer, ruins hall",
        "forest clearing, pines", "graveyard, full moon", "circuit board", "seabed, coral",
        "factory, catwalks", "grass, tree line, clouds", "snow field, icy cliffs", "bright lobby hall",
        "undersea base hangar", "Earth, stars, tiles", "purple holographic rings", "giant swords, purple flames",
        "arena, three crests", "space station, stars", "ship far off, stars", "\"Digimon Online\" sign",
        "lava floor, fire walls", "ice floor, crystal spikes", "dark purple chamber", "underground lake cave",
        "\"DIGI\" band grid", "light burst (digivolving)", "sky over a ring of teeth", "\"EVOLUTION\" band grid",
        "Fire backdrop", "Fire backdrop, band", "Fire backdrop, flames", "Water backdrop",
        "Water backdrop, band", "Water backdrop, more", "Ice backdrop", "Ice backdrop, band",
        "Ice backdrop, more", "Wind backdrop", "Wind backdrop, band", "Wind backdrop, more",
        "Thunder backdrop", "Thunder backdrop, band", "Thunder backdrop, more", "arena, city signs",
        "Machine backdrop, band", "Machine backdrop, gears", "one-polygon placeholder", "Dark backdrop, band",
        "Dark backdrop, more", "ice field, iceberg", "desert dunes (as 5)", "rows of Digimon icons",
        "Fire backdrop (as 34)",
    };
    return id >= 0 && id < (int)(sizeof LOOK / sizeof *LOOK) ? LOOK[id] : "";
}

/* --- matrices: column-major 4x4, affine --- */

static void identity(float m[16]) {
    for (int k = 0; k < 16; k++) m[k] = k % 5 == 0;
}

static void mul(const float a[16], const float b[16], float out[16]) {
    float w[16];
    for (int c = 0; c < 4; c++)
        for (int i = 0; i < 4; i++)
            w[c * 4 + i] = a[i] * b[c * 4] + a[4 + i] * b[c * 4 + 1] + a[8 + i] * b[c * 4 + 2] + a[12 + i] * b[c * 4 + 3];
    memcpy(out, w, sizeof w);
}

static bool invert(const float m[16], float out[16]) {
    float a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
    float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (fabsf(det) < 1e-9f) return false;
    float r[9] = {(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det,
                  (f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det,
                  (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det};
    identity(out);
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 3; col++) out[col * 4 + row] = r[row * 3 + col];
    for (int row = 0; row < 3; row++) out[12 + row] = -(out[row] * m[12] + out[4 + row] * m[13] + out[8 + row] * m[14]);
    return true;
}

static std::array<float, 3> apply(const float m[16], float x, float y, float z) {
    return {m[0] * x + m[4] * y + m[8] * z + m[12], m[1] * x + m[5] * y + m[9] * z + m[13], m[2] * x + m[6] * y + m[10] * z + m[14]};
}

/* Where a bone's keys' first frame puts it, under its parents; a part
   hidden there (scaled to nothing, shown by its motion) at full size */
static void bind_world(const Model &m, int b, float out[16]) {
    if (b <= 0) return identity(out);
    const Bone &bone = m.bones[b];
    Vec3s v[3];
    for (int t = 0; t < 3; t++) v[t] = {bone.keys[t][0].x, bone.keys[t][0].y, bone.keys[t][0].z};
    if (std::abs(v[2].x) <= 48 || std::abs(v[2].y) <= 48 || std::abs(v[2].z) <= 48) v[2] = {4096, 4096, 4096};
    float local[16], parent[16];
    bone_matrix(v[0], v[1], v[2], local);
    bind_world(m, bone.parent < b ? bone.parent : 0, parent);
    mul(parent, local, out);
}

/* A part that doesn't move: its keys at 0 from frame 0 on (as the stages' own) */
static std::vector<uint8_t> static_keys() {
    std::vector<uint8_t> k;
    for (uint32_t o : {0x10u, 0x20u, 0x30u, 0x40u}) put32(k, o);
    for (int t = 0; t < 3; t++)
        for (int frame : {0, 30000}) {
            int s = t == 2 ? 4096 : 0;
            put16(k, s), put16(k, s), put16(k, s), put16(k, frame);
        }
    put16(k, 0), put16(k, 0), put16(k, 30000), put16(k, 0);
    return k;
}

/* --- from the game's model --- */

void StageEdit::fromModel(const Model &m, const StageInfo &info) {
    parts.clear(), images.clear(), error.clear();
    memcpy(bg, info.bg, 3);
    /* an image for each texture page and CLUT the polygons read; the ones
       semi-transparent polygons read with their semi-transparent texels half
       opaque */
    std::map<std::pair<int, int>, int> byCombo;
    std::set<std::pair<int, int>> semi;
    for (const Mesh &me : m.meshes)
        for (const Poly &p : me.polys)
            if (p.textured && p.abr) semi.insert({p.tpage & 0x19F, p.clut});
    int white = -1;
    for (size_t b = 1; b < m.bones.size(); b++) {
        const Bone &bone = m.bones[b];
        const Mesh &mesh = m.meshes[bone.mesh];
        StagePart part;
        char name[32];
        snprintf(name, sizeof name, "part_%02d", (int)b);
        part.name = name;
        part.parent = bone.parent < (int)b ? bone.parent : 0;
        const uint8_t *k = m.arc.entry(bone.keyEntry);
        part.keys.assign(k, k + m.arc.entry_size(bone.keyEntry));
        bind_world(m, (int)b, part.bind);
        for (uint8_t nb : info.noBounds) {
            if (!nb) break;
            if (nb == b) part.noBounds = true;
        }
        if (!mesh.polys.empty()) memcpy(part.color, mesh.polys[0].color, 3);
        for (const Vec3s &v : mesh.verts) part.verts.push_back(apply(part.bind, v.x, v.y, v.z));
        for (const Poly &p : mesh.polys) {
            StageFace f;
            f.n = p.quad ? 4 : 3;
            f.abr = p.abr;
            /* the PSX's front is clockwise on screen: tri 0 1 2, quad 0 1 3 2 */
            static const int CCW[2][4] = {{0, 2, 1, 0}, {0, 2, 3, 1}};
            for (int c = 0; c < f.n; c++) {
                int k2 = CCW[p.quad][c];
                f.v[c] = p.idx[k2];
                f.uv[c][0] = p.uv[k2][0], f.uv[c][1] = p.uv[k2][1];
            }
            if (p.textured) {
                std::pair<int, int> key = {p.tpage & 0x19F, p.clut};
                auto it = byCombo.find(key);
                if (it == byCombo.end()) {
                    StageImage img;
                    img.tpage = key.first, img.pageClut = key.second;
                    img.w = img.h = 256;
                    img.px = decode_page(m.vram, (uint16_t)key.first, (uint16_t)key.second, 256, 256, semi.count(key) > 0);
                    if (tpage_depth((uint16_t)key.first) == 0) {
                        int px = tpage_x((uint16_t)key.first), py = tpage_y((uint16_t)key.first);
                        img.source = info.fid * 64 + (key.first & 0x1F);
                        img.index.resize(256 * 256);
                        for (int v = 0; v < 256; v++)
                            for (int u = 0; u < 256; u++)
                                img.index[v * 256 + u] = m.vram[(py + v) * 1024 + px + u / 4] >> (u & 3) * 4 & 15;
                        img.clutX = clut_x((uint16_t)key.second), img.clutY = clut_y((uint16_t)key.second);
                        for (int c = 0; c < 16; c++) img.clut[c] = m.vram[img.clutY * 1024 + img.clutX + c];
                    }
                    it = byCombo.emplace(key, (int)images.size()).first;
                    images.push_back(std::move(img));
                }
                f.image = it->second;
            } else {
                if (white < 0) {
                    white = (int)images.size();
                    images.push_back({4, 4, std::vector<uint32_t>(16, 0xFFFFFFFFu)});
                    images.back().flat = true;
                }
                f.image = white;
                for (auto &uv : f.uv) uv[0] = uv[1] = 1;
            }
            part.faces.push_back(f);
        }
        parts.push_back(std::move(part));
    }
}

/* --- editing --- */

std::vector<std::array<float, 3>> StageEdit::placed(const StagePart &p) const {
    if (p.move[0] == 0 && p.move[1] == 0 && p.move[2] == 0 && p.turn == 0 && p.scale == 1) return p.verts;
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const auto &v : p.verts)
        for (int i = 0; i < 3; i++) lo[i] = std::min(lo[i], v[i]), hi[i] = std::max(hi[i], v[i]);
    float c[3] = {(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2};
    float cs = cosf(p.turn * 0.017453293f), sn = sinf(p.turn * 0.017453293f);
    std::vector<std::array<float, 3>> out;
    for (const auto &v : p.verts) {
        float d[3] = {(v[0] - c[0]) * p.scale, (v[1] - c[1]) * p.scale, (v[2] - c[2]) * p.scale};
        out.push_back({c[0] + p.move[0] + d[0] * cs + d[2] * sn, c[1] + p.move[1] + d[1], c[2] + p.move[2] - d[0] * sn + d[2] * cs});
    }
    return out;
}

/* A part fixed where it stands now: no parent, keys that don't move */
static void fix(StagePart &p, std::vector<std::array<float, 3>> verts) {
    p.verts = std::move(verts);
    p.parent = 0;
    p.keys = static_keys();
    identity(p.bind);
    p.move[0] = p.move[1] = p.move[2] = p.turn = 0, p.scale = 1;
}

void StageEdit::append(const StageEdit &from, int k, const std::string &prefix) {
    if (k < 0 || k >= (int)from.parts.size()) return;
    StagePart p = from.parts[k];
    fix(p, from.placed(from.parts[k]));
    std::map<int, int> remap;
    for (StageFace &f : p.faces) {
        auto it = remap.find(f.image);
        if (it == remap.end()) {
            it = remap.emplace(f.image, (int)images.size()).first;
            images.push_back(from.images[f.image]);
        }
        f.image = it->second;
    }
    std::string name = prefix + p.name;
    for (int n = 2; std::any_of(parts.begin(), parts.end(), [&](const StagePart &q) { return q.name == name; }); n++)
        name = prefix + p.name + "_" + std::to_string(n);
    p.name = name;
    parts.push_back(std::move(p));
}

void StageEdit::remove(int k) {
    if (k < 0 || k >= (int)parts.size()) return;
    for (StagePart &p : parts) {
        if (p.parent == k + 1) fix(p, placed(p));
        else if (p.parent > k + 1) p.parent--;
    }
    parts.erase(parts.begin() + k);
}

/* --- OBJ --- */

static const float OBJ_UNIT = 100; /* the game's units in one of the OBJ's */

bool StageEdit::exportObj(const std::string &dir, const std::string &name) const {
    SDL_CreateDirectory(dir.c_str());
    std::ofstream obj(dir + "/" + name + ".obj"), mtl(dir + "/" + name + ".mtl");
    if (!obj || !mtl) return false;
    obj << "# a Digimon World 2003 model, from DW3 Model Viewer: y up, 1 unit = " << OBJ_UNIT << " of the game's\n";
    obj << "# keep each object's name to keep its part (its keys, its place); a material's _abrN is its semi-transparency\n";
    obj << "mtllib " << name << ".mtl\n";
    std::set<std::pair<int, int>> mats;
    int base = 1, uvBase = 1;
    char line[160];
    for (const StagePart &p : parts) {
        obj << "o " << p.name << "\n";
        for (const auto &v : placed(p)) {
            snprintf(line, sizeof line, "v %.4f %.4f %.4f\n", v[0] / OBJ_UNIT, -v[1] / OBJ_UNIT, -v[2] / OBJ_UNIT);
            obj << line;
        }
        int lastMat = -1;
        for (const StageFace &f : p.faces) {
            const StageImage &img = images[f.image];
            for (int c = 0; c < f.n; c++) {
                snprintf(line, sizeof line, "vt %.6f %.6f\n", (f.uv[c][0] + 0.5f) / img.w, 1 - (f.uv[c][1] + 0.5f) / img.h);
                obj << line;
            }
            int mat = f.image * 8 + f.abr;
            if (mat != lastMat) {
                obj << "usemtl tex" << f.image << (f.abr ? "_abr" + std::to_string(f.abr) : "") << "\n";
                mats.insert({f.image, f.abr});
                lastMat = mat;
            }
            obj << "f";
            for (int c = 0; c < f.n; c++) obj << " " << base + f.v[c] << "/" << uvBase + c;
            obj << "\n";
            uvBase += f.n;
        }
        base += (int)p.verts.size();
    }
    std::set<int> used;
    for (auto [image, abr] : mats) {
        mtl << "newmtl tex" << image << (abr ? "_abr" + std::to_string(abr) : "") << "\nKd 1 1 1\nmap_Kd tex" << image << ".png\n\n";
        used.insert(image);
    }
    for (int i : used) {
        const StageImage &img = images[i];
        SDL_Surface *s = SDL_CreateSurfaceFrom(img.w, img.h, SDL_PIXELFORMAT_RGBA32, (void *)img.px.data(), img.w * 4);
        if (s) SDL_SavePNG(s, (dir + "/tex" + std::to_string(i) + ".png").c_str()), SDL_DestroySurface(s);
    }
    return true;
}

static bool load_png(const std::string &path, StageImage &out) {
    SDL_Surface *png = SDL_LoadPNG(path.c_str());
    SDL_Surface *s = png ? SDL_ConvertSurface(png, SDL_PIXELFORMAT_RGBA32) : nullptr;
    if (png) SDL_DestroySurface(png);
    if (!s) return false;
    out.w = s->w, out.h = s->h;
    out.px.resize((size_t)s->w * s->h);
    for (int y = 0; y < s->h; y++) memcpy(&out.px[(size_t)y * s->w], (uint8_t *)s->pixels + y * s->pitch, s->w * 4);
    SDL_DestroySurface(s);
    return true;
}

static std::string trim(std::string s) {
    while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
    size_t a = 0;
    while (a < s.size() && isspace((unsigned char)s[a])) a++;
    return s.substr(a);
}

bool StageEdit::importObj(const std::string &path) {
    error.clear();
    std::ifstream in(path);
    if (!in) return error = "can't read " + path, false;
    std::string dir = path.substr(0, path.find_last_of("/\\") + 1);
    struct Mat {
        std::string map;
        float kd[3] = {1, 1, 1};
    };
    std::map<std::string, Mat> mats;
    std::vector<std::array<float, 3>> pos;
    std::vector<std::array<float, 2>> uvs;
    struct Corner { int v, t; };
    struct Face { std::vector<Corner> c; std::string mat; };
    struct Obj { std::string name; std::vector<Face> faces; };
    std::vector<Obj> objs;
    std::string mat, line;
    bool named = false; /* objects by "o"; by "g" in a file without any */
    auto object = [&](const std::string &name) {
        if (!objs.empty() && objs.back().faces.empty()) objs.back().name = name;
        else objs.push_back({name, {}});
    };
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string op;
        ls >> op;
        if (op == "v") {
            std::array<float, 3> v{};
            ls >> v[0] >> v[1] >> v[2];
            pos.push_back({v[0] * OBJ_UNIT, -v[1] * OBJ_UNIT, -v[2] * OBJ_UNIT});
        } else if (op == "vt") {
            std::array<float, 2> t{};
            ls >> t[0] >> t[1];
            uvs.push_back(t);
        } else if (op == "o") {
            object(trim(line.substr(1))), named = true;
        } else if (op == "g" && !named) {
            object(trim(line.substr(1)));
        } else if (op == "usemtl") {
            mat = trim(line.substr(6));
        } else if (op == "mtllib") {
            std::ifstream ml(dir + trim(line.substr(6)));
            std::string cur, l2;
            while (std::getline(ml, l2)) {
                std::istringstream ms(l2);
                std::string mop;
                ms >> mop;
                if (mop == "newmtl") cur = trim(l2.substr(6)), mats[cur];
                else if (mop == "Kd") ms >> mats[cur].kd[0] >> mats[cur].kd[1] >> mats[cur].kd[2];
                else if (mop == "map_Kd") {
                    std::string m = trim(l2.substr(6));
                    if (!m.empty() && m[0] == '-') m = m.substr(m.find_last_of(" \t") + 1); /* options (-s 1 1 1 ...), then the file */
                    mats[cur].map = m;
                }
            }
        } else if (op == "f") {
            if (objs.empty()) objs.push_back({"part", {}});
            Face f;
            f.mat = mat;
            std::string c;
            while (ls >> c) {
                int v = 0, t = 0;
                sscanf(c.c_str(), "%d/%d", &v, &t);
                if (v < 0) v += (int)pos.size() + 1;
                if (t < 0) t += (int)uvs.size() + 1;
                f.c.push_back({v - 1, t - 1});
            }
            if (f.c.size() >= 3) objs.back().faces.push_back(f);
        }
    }
    if (objs.empty()) return error = path + ": no faces", false;

    /* the images: each material's PNG, or its color */
    std::vector<StageImage> imgs;
    std::map<std::string, int> byMat, byFile;
    /* texN.png as exported, unchanged: the image it was (its page's indices kept) */
    auto as_was = [&](const std::string &file, StageImage &img) {
        size_t s = file.find_last_of("/\\") + 1;
        int n = -1;
        char tail[8] = "";
        if (sscanf(file.c_str() + s, "tex%d.%4s", &n, tail) == 2 && n >= 0 && n < (int)images.size() && images[n].w == img.w &&
            images[n].h == img.h && images[n].px == img.px)
            img = images[n];
    };
    auto image_of = [&](const std::string &m) {
        auto it = byMat.find(m);
        if (it != byMat.end()) return it->second;
        const Mat &mt = mats[m];
        int i = -1;
        if (!mt.map.empty()) {
            std::string file = mt.map.size() > 1 && (mt.map[1] == ':' || mt.map[0] == '/') ? mt.map : dir + mt.map;
            auto f = byFile.find(file);
            if (f != byFile.end()) i = f->second;
            else {
                StageImage img;
                if (load_png(file, img)) as_was(file, img), i = byFile[file] = (int)imgs.size(), imgs.push_back(std::move(img));
                else error = "can't read " + file + " (a PNG): its faces take the material's color";
            }
        }
        if (i < 0) {
            uint32_t c = 0xFF000000u;
            for (int k = 0; k < 3; k++) c |= (uint32_t)std::clamp((int)(mt.kd[k] * 255 + 0.5f), 0, 255) << k * 8;
            i = (int)imgs.size();
            imgs.push_back({4, 4, std::vector<uint32_t>(16, c)});
        }
        return byMat[m] = i;
    };

    std::vector<StagePart> made;
    std::map<std::string, int> oldIndex;
    for (int k = 0; k < (int)parts.size(); k++) oldIndex[parts[k].name] = k;
    std::vector<int> oldOf;
    for (const Obj &o : objs) {
        if (o.faces.empty()) continue;
        StagePart p;
        auto it = oldIndex.find(o.name);
        if (it != oldIndex.end()) {
            const StagePart &old = parts[it->second];
            p.parent = old.parent, p.keys = old.keys, p.noBounds = old.noBounds;
            memcpy(p.bind, old.bind, sizeof p.bind);
            memcpy(p.color, old.color, 3);
            oldOf.push_back(it->second);
        } else {
            fix(p, {});
            oldOf.push_back(-1);
        }
        p.name = o.name;
        std::map<int, int> local;
        for (const Face &f : o.faces) {
            int img = image_of(f.mat);
            const StageImage &im = imgs[img];
            /* a polygon of more than 4 corners: a fan of triangles */
            for (size_t a = 1; a + 1 < f.c.size(); a++) {
                StageFace sf;
                sf.image = img;
                size_t corners[4] = {0, a, a + 1, a + 2};
                sf.n = f.c.size() == 4 ? 4 : 3;
                for (int c = 0; c < sf.n; c++) {
                    const Corner &cn = f.c[corners[c]];
                    if (cn.v < 0 || cn.v >= (int)pos.size()) return error = path + ": a face's vertex isn't there", false;
                    auto li = local.find(cn.v);
                    if (li == local.end()) li = local.emplace(cn.v, (int)p.verts.size()).first, p.verts.push_back(pos[cn.v]);
                    sf.v[c] = li->second;
                    if (cn.t >= 0 && cn.t < (int)uvs.size())
                        sf.uv[c][0] = uvs[cn.t][0] * im.w - 0.5f, sf.uv[c][1] = (1 - uvs[cn.t][1]) * im.h - 0.5f;
                    else
                        sf.uv[c][0] = sf.uv[c][1] = 1;
                }
                size_t abr = f.mat.rfind("_abr");
                sf.abr = abr != std::string::npos ? std::clamp(atoi(f.mat.c_str() + abr + 4), 0, 4) : 0;
                p.faces.push_back(sf);
                if (sf.n == 4) break;
            }
        }
        made.push_back(std::move(p));
    }
    /* the parents, by name; a part whose parent is gone stays where it stands */
    for (size_t k = 0; k < made.size(); k++) {
        StagePart &p = made[k];
        if (!p.parent) continue;
        int to = 0;
        for (size_t j = 0; j < k; j++)
            if (oldOf[j] == p.parent - 1) to = (int)j + 1;
        if (to) p.parent = to;
        else fix(p, p.verts);
    }
    parts = std::move(made);
    images = std::move(imgs);
    return true;
}

/* --- building the game's model --- */

/* 15 or 16 colors (the 0 a texel of none, when any) for a piece's texels:
   its own when they're that few, else a median cut. 0x8000: the
   semi-transparency bit */
static std::vector<uint16_t> palette(const std::vector<uint16_t> &texels, bool clear) {
    std::map<uint16_t, int> count;
    for (uint16_t t : texels)
        if (t) count[t]++;
    int room = clear ? 15 : 16;
    std::vector<uint16_t> out;
    if (clear) out.push_back(0);
    if ((int)count.size() <= room) {
        for (auto &it : count) out.push_back(it.first);
        return out;
    }
    struct C { int c[3], stp, n; };
    std::vector<C> cols;
    for (auto &it : count) cols.push_back({{it.first & 31, it.first >> 5 & 31, it.first >> 10 & 31}, it.first >> 15, it.second});
    std::vector<std::pair<int, int>> boxes = {{0, (int)cols.size()}};
    while ((int)boxes.size() < room) {
        int best = -1, axis = 0, wide = 0;
        for (int b = 0; b < (int)boxes.size(); b++) {
            if (boxes[b].second - boxes[b].first < 2) continue;
            for (int a = 0; a < 3; a++) {
                int lo = 31, hi = 0;
                for (int i = boxes[b].first; i < boxes[b].second; i++) lo = std::min(lo, cols[i].c[a]), hi = std::max(hi, cols[i].c[a]);
                if (hi - lo > wide) wide = hi - lo, best = b, axis = a;
            }
        }
        if (best < 0) break;
        auto [from, to] = boxes[best];
        std::sort(cols.begin() + from, cols.begin() + to, [&](const C &x, const C &y) { return x.c[axis] < y.c[axis]; });
        int total = 0, half = 0, cut = from + 1;
        for (int i = from; i < to; i++) total += cols[i].n;
        for (int i = from; i < to - 1; i++) {
            half += cols[i].n;
            cut = i + 1;
            if (half * 2 >= total) break;
        }
        boxes[best] = {from, cut};
        boxes.push_back({cut, to});
    }
    for (auto [from, to] : boxes) {
        long sum[3] = {0, 0, 0}, n = 0, stp = 0;
        for (int i = from; i < to; i++) {
            for (int a = 0; a < 3; a++) sum[a] += (long)cols[i].c[a] * cols[i].n;
            n += cols[i].n, stp += (long)cols[i].stp * cols[i].n;
        }
        uint16_t c = (uint16_t)((sum[0] + n / 2) / n | (sum[1] + n / 2) / n << 5 | (sum[2] + n / 2) / n << 10 | (stp * 2 >= n ? 0x8000 : 0));
        out.push_back(c ? c : 0x8000);
    }
    return out;
}

static int nearest(const std::vector<uint16_t> &pal, uint16_t t) {
    if (!t) return 0; /* the transparent one: a piece with any keeps it first */
    int best = 0, bd = 1 << 30;
    for (int i = 0; i < (int)pal.size(); i++) {
        if (!pal[i]) continue;
        int d = (int)((pal[i] ^ t) >> 15) * 64;
        for (int s = 0; s < 15; s += 5) {
            int e = (pal[i] >> s & 31) - (t >> s & 31);
            d += e * e;
        }
        if (d < bd) bd = d, best = i;
    }
    return best;
}

/* An RGBA texel as the GPU's halfword: alpha under 64 none (0), under 192
   semi-transparent (the bit) when SEMI, black kept from being none */
static uint16_t texel(uint32_t c, bool semi) {
    int a = c >> 24;
    if (a < 64) return 0;
    uint16_t p = (uint16_t)(((c & 255) * 31 + 127) / 255 | ((c >> 8 & 255) * 31 + 127) / 255 << 5 | ((c >> 16 & 255) * 31 + 127) / 255 << 10);
    if (semi && a < 192) return p | 0x8000;
    if (!p) return semi ? 0x0421 : 0x8000;
    return p;
}

bool StageEdit::build(const std::vector<uint8_t> &disc, int boneEntry, std::vector<uint8_t> &out, StageInfo &info, StageBuild &made) {
    error.clear(), made = StageBuild();
    Archive base{disc.data(), disc.size()};
    int count = base.count();
    if (count <= 0 || (size_t)count * 4 > disc.size() || boneEntry >= count || !base.entry(boneEntry))
        return error = "the stage's file isn't an archive", false;
    int texEntry = (int)rd32(base.entry(boneEntry));
    if (parts.empty()) return error = "a stage needs a part", false;

    /* the pieces of texture: the faces' boxes in their images, those of an
       image (and its semi-transparency) that overlap as one; of a game's
       page, those of its source (any CLUT) */
    struct Ref { int part, face; };
    std::vector<Ref> refs;
    std::vector<std::array<int, 4>> box;
    for (int p = 0; p < (int)parts.size(); p++)
        for (int f = 0; f < (int)parts[p].faces.size(); f++) {
            const StageFace &fc = parts[p].faces[f];
            const StageImage &im = images[fc.image];
            std::array<int, 4> b = {1 << 30, 1 << 30, -(1 << 30), -(1 << 30)};
            for (int c = 0; c < fc.n; c++) {
                b[0] = std::min(b[0], (int)floorf(fc.uv[c][0] + 0.001f)), b[1] = std::min(b[1], (int)floorf(fc.uv[c][1] + 0.001f));
                b[2] = std::max(b[2], (int)ceilf(fc.uv[c][0] - 0.001f)), b[3] = std::max(b[3], (int)ceilf(fc.uv[c][1] - 0.001f));
            }
            b[0] = std::clamp(b[0], 0, im.w - 1), b[2] = std::clamp(b[2], b[0], im.w - 1);
            b[1] = std::clamp(b[1], 0, im.h - 1), b[3] = std::clamp(b[3], b[1], im.h - 1);
            refs.push_back({p, f});
            box.push_back(b);
        }
    auto key = [&](int i) {
        const StageFace &f = parts[refs[i].part].faces[refs[i].face];
        int src = images[f.image].source;
        return src >= 0 ? -1 - src : f.image * 2 + (f.abr != 0);
    };
    std::vector<int> root(refs.size());
    std::iota(root.begin(), root.end(), 0);
    std::function<int(int)> find = [&](int i) { return root[i] == i ? i : root[i] = find(root[i]); };
    for (size_t i = 0; i < refs.size(); i++)
        for (size_t j = i + 1; j < refs.size(); j++)
            if (key((int)i) == key((int)j) && box[i][0] <= box[j][2] && box[j][0] <= box[i][2] && box[i][1] <= box[j][3] &&
                box[j][1] <= box[i][3])
                root[find((int)i)] = find((int)j);
    struct Piece {
        int image = 0;
        bool semi = false;
        bool shared = false;   /* a game's page: its indices, a CLUT for each of its images */
        std::vector<int> imgs; /* the images its faces read */
        int clut0 = 0;         /* its first CLUT */
        int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1; /* in its image, inclusive */
        int w = 0, h = 0, page = 0, px = 0, py = 0;    /* placed */
    };
    std::vector<Piece> pieces;
    std::map<int, int> pieceOf;
    std::vector<int> refPiece(refs.size());
    for (size_t i = 0; i < refs.size(); i++) {
        int r = find((int)i);
        auto it = pieceOf.find(r);
        if (it == pieceOf.end()) {
            it = pieceOf.emplace(r, (int)pieces.size()).first;
            Piece pc;
            const StageFace &f = parts[refs[i].part].faces[refs[i].face];
            pc.image = f.image, pc.semi = f.abr != 0, pc.shared = images[f.image].source >= 0;
            pieces.push_back(pc);
        }
        Piece &pc = pieces[it->second];
        int img = parts[refs[i].part].faces[refs[i].face].image;
        if (std::find(pc.imgs.begin(), pc.imgs.end(), img) == pc.imgs.end()) pc.imgs.push_back(img);
        pc.x0 = std::min(pc.x0, box[i][0]), pc.y0 = std::min(pc.y0, box[i][1]);
        pc.x1 = std::max(pc.x1, box[i][2]), pc.y1 = std::max(pc.y1, box[i][3]);
        refPiece[i] = it->second;
    }
    made.islands = (int)pieces.size();
    /* the CLUTs: a piece's, or one for each image of a game's page */
    const int PAGES = 3;
    int cluts = 0;
    for (Piece &pc : pieces) pc.clut0 = cluts, cluts += pc.shared ? (int)pc.imgs.size() : 1;
    std::vector<std::array<int, 3>> clutAt(cluts); /* page, halfword in it (16 apart), row */
    auto clut_of = [&](const Piece &pc, int image) {
        return pc.clut0 + (pc.shared ? (int)(std::find(pc.imgs.begin(), pc.imgs.end(), image) - pc.imgs.begin()) : 0);
    };
    auto put_clut = [&](std::vector<uint16_t> *pages, int slot, const uint16_t *c, int n) {
        for (int k = 0; k < n; k++) pages[clutAt[slot][0]][clutAt[slot][2] * 64 + clutAt[slot][1] + k] = c[k];
    };
    /* placed: a game's page's pieces and CLUTs where they were (each source
       on its own page, as the game had it, when that page is free), the rest
       first-fit on a grid of 4x4 texels, the tallest first, shrunk alike
       until they fit, and the other CLUTs four to a free 64x4 block; when
       the game's places don't work out, every piece where it fits */
    std::vector<int> order(pieces.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return pieces[a].y1 - pieces[a].y0 > pieces[b].y1 - pieces[b].y0; });
    const int CELL = 4, GRID = 256 / CELL, top = 256;
    auto place_all = [&](bool pin, float g) {
        std::vector<uint8_t> used(PAGES * GRID * GRID, 0);
        std::vector<int> sat(PAGES * (GRID + 1) * (GRID + 1)); /* each page's used cells, summed from its corner */
        auto mark = [&](int pg, int x0, int y0, int x1, int y1) { /* texels, inclusive */
            for (int cy = y0 / CELL; cy <= std::min(y1 / CELL, GRID - 1); cy++)
                for (int cx = x0 / CELL; cx <= std::min(x1 / CELL, GRID - 1); cx++) used[(pg * GRID + cy) * GRID + cx] = 1;
            int *t = &sat[pg * (GRID + 1) * (GRID + 1)];
            for (int cy = 0; cy < GRID; cy++)
                for (int cx = 0; cx < GRID; cx++)
                    t[(cy + 1) * (GRID + 1) + cx + 1] = used[(pg * GRID + cy) * GRID + cx] + t[cy * (GRID + 1) + cx + 1] +
                                                        t[(cy + 1) * (GRID + 1) + cx] - t[cy * (GRID + 1) + cx];
        };
        auto busy = [&](int pg, int cx, int cy, int cw, int ch) {
            const int *t = &sat[pg * (GRID + 1) * (GRID + 1)];
            return t[(cy + ch) * (GRID + 1) + cx + cw] - t[cy * (GRID + 1) + cx + cw] - t[(cy + ch) * (GRID + 1) + cx] + t[cy * (GRID + 1) + cx];
        };
        int owner[PAGES] = {-1, -1, -1}; /* the source pinned on each page */
        std::vector<bool> clutSet(cluts, false);
        std::vector<int> rest;
        for (int i : order) {
            Piece &pc = pieces[i];
            int src = images[pc.image].source, pg = (src & 15) - TEX_X / 64; /* its page, as the model was read */
            if (!pin || !pc.shared || pg < 0 || pg >= PAGES || (owner[pg] >= 0 && owner[pg] != src)) {
                rest.push_back(i);
                continue;
            }
            owner[pg] = src;
            pc.page = pg, pc.px = pc.x0, pc.py = pc.y0, pc.w = pc.x1 - pc.x0 + 1, pc.h = pc.y1 - pc.y0 + 1;
            mark(pc.page, pc.x0, pc.y0, pc.x1, pc.y1);
        }
        /* their CLUTs where they were, when on a page of the same file's */
        for (const Piece &pc : pieces) {
            if (!pc.shared || std::find(rest.begin(), rest.end(), (int)(&pc - pieces.data())) != rest.end()) continue;
            for (int k = 0; k < (int)pc.imgs.size(); k++) {
                const StageImage &im = images[pc.imgs[k]];
                int cp = (im.clutX - TEX_X) / 64;
                if (im.clutX < TEX_X || im.clutX >= TEX_X + PAGES * 64 || im.clutY < 0 || im.clutY > 255 || owner[cp] < 0 ||
                    owner[cp] / 64 != im.source / 64)
                    continue;
                clutAt[pc.clut0 + k] = {cp, im.clutX - TEX_X - cp * 64, im.clutY};
                clutSet[pc.clut0 + k] = true;
                int tx0 = (im.clutX - TEX_X - cp * 64) * 4;
                mark(cp, tx0, im.clutY, tx0 + 63, im.clutY);
            }
        }
        for (int i : rest) {
            Piece &pc = pieces[i];
            int w = pc.x1 - pc.x0 + 1, h = pc.y1 - pc.y0 + 1;
            float sc = std::min({1.0f, 256.0f / w, (float)top / h}) * g;
            pc.w = std::max(1, (int)ceilf(w * sc - 0.001f)), pc.h = std::max(1, (int)ceilf(h * sc - 0.001f));
            int cw = (pc.w + CELL - 1) / CELL, ch = (pc.h + CELL - 1) / CELL;
            bool found = false;
            for (int pg = 0; pg < PAGES && !found; pg++)
                for (int cy = 0; cy + ch <= GRID && !found; cy++)
                    for (int cx = 0; cx + cw <= GRID && !found; cx++)
                        if (!busy(pg, cx, cy, cw, ch)) {
                            pc.page = pg, pc.px = cx * CELL, pc.py = cy * CELL;
                            mark(pg, pc.px, pc.py, pc.px + pc.w - 1, pc.py + pc.h - 1);
                            found = true;
                        }
            if (!found) return false;
        }
        /* the other CLUTs: four to a block of 64x4 texels (a CLUT's x is 16 halfwords apart), from the bottom */
        std::vector<int> loose;
        for (int c = 0; c < cluts; c++)
            if (!clutSet[c]) loose.push_back(c);
        for (size_t at = 0; at < loose.size(); at += 4) {
            bool found = false;
            for (int pg = 0; pg < PAGES && !found; pg++)
                for (int cy = GRID - 1; cy >= 0 && !found; cy--)
                    for (int cx = 0; cx < GRID && !found; cx += 16)
                        if (!busy(pg, cx, cy, 16, 1)) {
                            mark(pg, cx * CELL, cy * CELL, cx * CELL + 63, cy * CELL + 3);
                            for (size_t k = 0; k < 4 && at + k < loose.size(); k++) clutAt[loose[at + k]] = {pg, cx, cy * CELL + (int)k};
                            found = true;
                        }
            if (!found) return false;
        }
        return true;
    };
    bool fits = false;
    for (int pin = 1; pin >= 0 && !fits; pin--)
        for (float g = 1; g > 0.04f && !fits; g *= 0.92f) made.shrink = g, fits = place_all(pin != 0, g);
    if (!fits) return error = "the textures don't fit the stage's three pages", false;

    /* an image with no half-opaque texels (a new one): its semi-transparent
       polygons blend all of it */
    std::vector<int> half(images.size(), -1);
    auto has_half = [&](int i) {
        if (half[i] < 0)
            half[i] = std::any_of(images[i].px.begin(), images[i].px.end(), [](uint32_t c) { return (c >> 24) >= 64 && (c >> 24) < 192; });
        return half[i] == 1;
    };

    /* the pages: each piece's texels, 4 to a halfword, through its CLUT */
    std::vector<uint16_t> page[PAGES];
    for (auto &pg : page) pg.assign(64 * 256, 0);
    for (int i = 0; i < (int)pieces.size(); i++) {
        const Piece &pc = pieces[i];
        const StageImage &im = images[pc.image];
        int w = pc.x1 - pc.x0 + 1, h = pc.y1 - pc.y0 + 1;
        if (pc.shared) { /* the game's indices (the nearest when shrunk), each image's CLUT */
            for (int y = 0; y < pc.h; y++)
                for (int x = 0; x < pc.w; x++) {
                    int u = pc.px + x, sx = pc.x0 + x * w / pc.w, sy = pc.y0 + y * h / pc.h;
                    page[pc.page][(pc.py + y) * 64 + u / 4] |= (uint16_t)(im.index[(size_t)sy * im.w + sx] << (u & 3) * 4);
                }
            for (int k = 0; k < (int)pc.imgs.size(); k++) put_clut(page, pc.clut0 + k, images[pc.imgs[k]].clut, 16);
            continue;
        }
        std::vector<uint16_t> tx(pc.w * pc.h);
        bool clear = false;
        for (int y = 0; y < pc.h; y++)
            for (int x = 0; x < pc.w; x++) {
                /* the source texels it covers, averaged (one when not shrunk) */
                int sx0 = pc.x0 + x * w / pc.w, sx1 = std::max(sx0 + 1, pc.x0 + (x + 1) * w / pc.w);
                int sy0 = pc.y0 + y * h / pc.h, sy1 = std::max(sy0 + 1, pc.y0 + (y + 1) * h / pc.h);
                uint32_t sum[4] = {0, 0, 0, 0}, n = 0;
                for (int sy = sy0; sy < sy1; sy++)
                    for (int sx = sx0; sx < sx1; sx++) {
                        uint32_t c = im.px[(size_t)sy * im.w + sx];
                        for (int k = 0; k < 4; k++) sum[k] += c >> k * 8 & 255;
                        n++;
                    }
                uint32_t c = 0;
                for (int k = 0; k < 4; k++) c |= (sum[k] + n / 2) / n << k * 8;
                tx[y * pc.w + x] = texel(c, pc.semi);
                if (pc.semi && !has_half(pc.image) && tx[y * pc.w + x]) tx[y * pc.w + x] |= 0x8000;
                clear |= !tx[y * pc.w + x];
            }
        std::vector<uint16_t> pal = palette(tx, clear);
        put_clut(page, pc.clut0, pal.data(), (int)pal.size());
        for (int y = 0; y < pc.h; y++)
            for (int x = 0; x < pc.w; x++) {
                int u = pc.px + x;
                page[pc.page][(pc.py + y) * 64 + u / 4] |= (uint16_t)(nearest(pal, tx[y * pc.w + x]) << (u & 3) * 4);
            }
    }

    /* the bones: each part's faces in meshes of up to 256 vertices, in the
       part's own space */
    struct Bonebuild { int parent; std::vector<uint8_t> mesh, keys; bool noBounds; };
    std::vector<Bonebuild> bones;
    for (int p = 0; p < (int)parts.size(); p++) {
        const StagePart &part = parts[p];
        float inv[16];
        if (!invert(part.bind, inv)) return error = part.name + ": its keys scale it to nothing", false;
        std::vector<std::array<float, 3>> world = placed(part);
        std::vector<Vec3s> local;
        for (const auto &w : world) {
            std::array<float, 3> l = apply(inv, w[0], w[1], w[2]);
            for (float c : l)
                if (fabsf(c) > 32767) return error = part.name + " is too big (its points past 32767 of its own space)", false;
            local.push_back({(int16_t)lroundf(l[0]), (int16_t)lroundf(l[1]), (int16_t)lroundf(l[2])});
        }
        made.firstBone.push_back((int)bones.size() + 1);
        /* the faces, sorted by piece and kind, cut where the vertices run past 256 */
        std::vector<int> faces(part.faces.size());
        std::iota(faces.begin(), faces.end(), 0);
        size_t refBase = 0;
        for (int q = 0; q < p; q++) refBase += parts[q].faces.size();
        auto pieceOfFace = [&](int f) { return refPiece[refBase + f]; };
        auto clutOfFace = [&](int f) { return clut_of(pieces[pieceOfFace(f)], part.faces[f].image); };
        std::sort(faces.begin(), faces.end(), [&](int a, int b) {
            const StageFace &x = part.faces[a], &y = part.faces[b];
            return std::make_tuple(clutOfFace(a), x.abr, x.n) < std::make_tuple(clutOfFace(b), y.abr, y.n);
        });
        size_t at = 0;
        do {
            std::map<int, int> vmap;
            std::vector<int> chunk;
            for (; at < faces.size(); at++) {
                const StageFace &f = part.faces[faces[at]];
                int fresh = 0;
                for (int c = 0; c < f.n; c++) fresh += !vmap.count(f.v[c]);
                if ((int)vmap.size() + fresh > 256) break;
                for (int c = 0; c < f.n; c++)
                    if (!vmap.count(f.v[c])) vmap.emplace(f.v[c], (int)vmap.size());
                chunk.push_back(faces[at]);
            }
            std::vector<Vec3s> vs(vmap.size());
            for (auto [from, to] : vmap) vs[to] = local[from];
            /* the commands (FIGHTSTG_drawMesh): unlit, flat, textured, one
               color; each run of a piece, semi-transparency and kind */
            std::vector<uint8_t> cmd = {0xA0, 0xB1, 0xC0, 0xD0, 0x91, 0x02, part.color[0], part.color[1], part.color[2]};
            int last = -1;
            for (int fi : chunk) {
                const StageFace &f = part.faces[fi];
                int pi = pieceOfFace(fi);
                const Piece &pc = pieces[pi];
                int clut = clutOfFace(fi), run = (clut * 8 + f.abr) * 2 + (f.n == 4);
                if (run != last) {
                    cmd.push_back((uint8_t)(0xE0 | f.abr));
                    cmd.push_back((uint8_t)(0x80 | (f.n == 4)));
                    uint8_t tex[7] = {0x01, 0, (uint8_t)pc.page, 0, (uint8_t)(clutAt[clut][0] * 64 + clutAt[clut][1]), (uint8_t)clutAt[clut][2], 0};
                    cmd.insert(cmd.end(), tex, tex + 7);
                    last = run;
                }
                /* the PSX's order: tri 0 2 1, quad 0 3 1 2 of the counter-clockwise corners */
                static const int PSX[2][4] = {{0, 2, 1, 0}, {0, 3, 1, 2}};
                const int *ord = PSX[f.n == 4];
                cmd.push_back(0x00);
                for (int c = 0; c < f.n; c++) cmd.push_back((uint8_t)vmap[f.v[ord[c]]]);
                int w = pc.x1 - pc.x0 + 1, h = pc.y1 - pc.y0 + 1;
                for (int c = 0; c < f.n; c++) {
                    const float *uv = f.uv[ord[c]];
                    int u = std::clamp((int)lroundf((uv[0] - pc.x0) * pc.w / w), 0, pc.w - 1);
                    int v = std::clamp((int)lroundf((uv[1] - pc.y0) * pc.h / h), 0, pc.h - 1);
                    cmd.push_back((uint8_t)(pc.px + u));
                    cmd.push_back((uint8_t)(pc.py + v));
                }
            }
            cmd.push_back(0xFF);
            /* the mesh's archive: vertices (0), normals (1, none), commands
               (2), its box's middle and corners (5), as the stages' own */
            std::vector<uint8_t> m;
            for (int k = 0; k < 6; k++) put32(m, 0);
            set32(m, 0, (uint32_t)m.size());
            put16(m, (int)vs.size()), put16(m, 0), put16(m, 0);
            int lo[3] = {32767, 32767, 32767}, hi[3] = {-32768, -32768, -32768};
            for (const Vec3s &v : vs) {
                put16(m, v.x), put16(m, v.y), put16(m, v.z);
                int c[3] = {v.x, v.y, v.z};
                for (int k = 0; k < 3; k++) lo[k] = std::min(lo[k], c[k]), hi[k] = std::max(hi[k], c[k]);
            }
            if (vs.empty()) lo[0] = lo[1] = lo[2] = hi[0] = hi[1] = hi[2] = 0;
            pad4(m);
            set32(m, 20, (uint32_t)m.size());
            put16(m, 9), put16(m, 0), put16(m, 0);
            put16(m, (lo[0] + hi[0]) / 2), put16(m, (lo[1] + hi[1]) / 2), put16(m, (lo[2] + hi[2]) / 2);
            for (int z : {hi[2], lo[2]})
                for (int y : {hi[1], lo[1]})
                    for (int x : {hi[0], lo[0]}) put16(m, x), put16(m, y), put16(m, z);
            set32(m, 4, (uint32_t)m.size());
            put16(m, 0), put16(m, 0), put16(m, 0), put16(m, 0);
            set32(m, 8, (uint32_t)m.size());
            m.insert(m.end(), cmd.begin(), cmd.end());
            pad4(m);
            int parent = part.parent ? made.firstBone[part.parent - 1] : 0;
            bones.push_back({parent, std::move(m), part.keys, part.noBounds});
        } while (at < faces.size());
    }
    if (bones.size() > 255) return error = "too many bones (" + std::to_string(bones.size()) + "; 255 at most)", false;

    /* the textures' archive: three TIMs of 64x256 halfwords, 16-bit, unpacked */
    std::vector<uint8_t> tim;
    for (int k = 0; k < PAGES; k++) put32(tim, 0);
    put32(tim, 0);
    for (int k = 0; k < PAGES; k++) {
        set32(tim, k * 4, (uint32_t)tim.size());
        put32(tim, 0x10), put32(tim, 2), put32(tim, 12 + 64 * 256 * 2);
        put16(tim, 0x280 + k * 64), put16(tim, 0), put16(tim, 64), put16(tim, 256);
        for (uint16_t h : page[k]) put16(tim, h);
    }

    /* the bone list: {textures, bones, (parent, mesh, keys)...}, the meshes and keys after the disc's entries */
    std::vector<std::vector<uint8_t>> entries(count);
    for (int i = 0; i < count; i++) {
        const uint8_t *e = base.entry(i);
        if (e && rd32(disc.data() + i * 4)) entries[i].assign(e, e + base.entry_size(i));
    }
    std::vector<uint8_t> list;
    put32(list, (uint32_t)texEntry), put32(list, (uint32_t)bones.size());
    for (const Bonebuild &b : bones) {
        put32(list, (uint32_t)b.parent);
        put32(list, (uint32_t)entries.size()), entries.push_back(b.mesh);
        put32(list, (uint32_t)entries.size()), entries.push_back(b.keys);
    }
    entries[boneEntry] = list;
    if (texEntry > 0 && texEntry < count) entries[texEntry] = tim;
    out.clear();
    for (size_t i = 0; i < entries.size(); i++) put32(out, 0);
    for (size_t i = 0; i < entries.size(); i++) {
        if (i < (size_t)count && !rd32(disc.data() + i * 4)) continue; /* the disc's 0: kept */
        set32(out, i * 4, (uint32_t)out.size());
        out.insert(out.end(), entries[i].begin(), entries[i].end());
        pad4(out);
    }
    while (out.size() % 2048) out.push_back(0); /* whole sectors, as the disc's */

    /* the bones the bounds check skips */
    memset(info.noBounds, 0, sizeof info.noBounds);
    int n = 0;
    for (size_t b = 0; b < bones.size(); b++) {
        if (!bones[b].noBounds) continue;
        if (n < 8) info.noBounds[n++] = (uint8_t)(b + 1);
        else made.warning = "more than 8 bones skip the bounds check: the rest may vanish near the screen's edges";
    }
    memcpy(info.bg, bg, 3);
    return true;
}

/* --- a battle model's bones --- */

static bool local_verts(const StagePart &part, const std::vector<std::array<float, 3>> &world, std::vector<std::array<int, 3>> &out) {
    float inv[16];
    if (!invert(part.bind, inv)) return false;
    out.clear();
    for (const auto &w : world) {
        std::array<float, 3> l = apply(inv, w[0], w[1], w[2]);
        out.push_back({(int)lroundf(l[0]), (int)lroundf(l[1]), (int)lroundf(l[2])});
    }
    return true;
}

std::string StageEdit::signature(int p) const {
    const StagePart &part = parts[p];
    std::vector<std::array<int, 3>> loc;
    if (!local_verts(part, placed(part), loc)) return "?";
    std::vector<std::string> faces;
    for (const StageFace &f : part.faces) {
        std::vector<std::string> cs;
        char b[96];
        for (int c = 0; c < f.n; c++) {
            const auto &v = loc[f.v[c]];
            snprintf(b, sizeof b, "%d,%d,%d,%d,%d", v[0], v[1], v[2], (int)lroundf(f.uv[c][0]), (int)lroundf(f.uv[c][1]));
            cs.push_back(b);
        }
        std::sort(cs.begin(), cs.end());
        const StageImage &im = images[f.image];
        snprintf(b, sizeof b, "|%d,%d,%d,%d", f.n, f.abr, im.tpage, im.pageClut);
        std::string s;
        for (const std::string &c : cs) s += c + ";";
        faces.push_back(s + b);
    }
    std::sort(faces.begin(), faces.end());
    std::string all;
    for (const std::string &f : faces) all += f + "\n";
    return all;
}

bool StageEdit::boneMesh(int p, std::vector<uint8_t> &m) {
    error.clear();
    const StagePart &part = parts[p];
    std::vector<std::array<int, 3>> loc;
    if (!local_verts(part, placed(part), loc)) return error = part.name + ": its keys scale it to nothing", false;
    std::map<int, int> vmap; /* the vertices the faces use, in order */
    std::vector<int> used;
    for (const StageFace &f : part.faces)
        for (int c = 0; c < f.n; c++)
            if (!vmap.count(f.v[c])) vmap.emplace(f.v[c], (int)used.size()), used.push_back(f.v[c]);
    if (used.empty()) return error = part.name + " has no faces", false;
    if (used.size() > 256) return error = part.name + " has " + std::to_string(used.size()) + " vertices: a bone's mesh holds 256", false;
    for (int v : used)
        for (int k = 0; k < 3; k++)
            if (abs(loc[v][k]) > 32767) return error = part.name + " is too big (its points past 32767 of its own space)", false;
    for (const StageFace &f : part.faces) {
        const StageImage &im = images[f.image];
        if (im.tpage < 0 && !im.flat)
            return error = part.name + ": a face's texture isn't one of the model's pages (changing the pages isn't supported yet)", false;
    }
    /* a normal for each vertex: its faces' (the PSX's winding), smoothed */
    static const int PSX[2][4] = {{0, 2, 1, 0}, {0, 3, 1, 2}};
    std::vector<std::array<float, 3>> acc(used.size(), {0, 0, 0});
    for (const StageFace &f : part.faces) {
        const int *ord = PSX[f.n == 4];
        int tri[2][3] = {{0, 1, 2}, {1, 3, 2}};
        for (int t = 0; t < (f.n == 4 ? 2 : 1); t++) {
            int i[3];
            for (int k = 0; k < 3; k++) i[k] = vmap[f.v[ord[tri[t][k]]]];
            const auto &a = loc[used[i[0]]], &b = loc[used[i[1]]], &c = loc[used[i[2]]];
            float u[3], w[3];
            for (int k = 0; k < 3; k++) u[k] = (float)(b[k] - a[k]), w[k] = (float)(c[k] - a[k]);
            float n[3] = {-(u[1] * w[2] - u[2] * w[1]), -(u[2] * w[0] - u[0] * w[2]), -(u[0] * w[1] - u[1] * w[0])};
            for (int k = 0; k < 3; k++)
                for (int q : i) acc[q][k] += n[k];
        }
    }
    std::vector<Vec3s> normals;
    for (const auto &n : acc) {
        float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (l < 1e-6f) normals.push_back({0, -4096, 0});
        else normals.push_back({(int16_t)lroundf(n[0] / l * 4096), (int16_t)lroundf(n[1] / l * 4096), (int16_t)lroundf(n[2] / l * 4096)});
    }
    /* the commands (FIGHTSTG_drawMesh): lit, gouraud; a run of each image, semi-transparency and kind */
    std::vector<int> faces(part.faces.size());
    std::iota(faces.begin(), faces.end(), 0);
    std::stable_sort(faces.begin(), faces.end(), [&](int a, int b) {
        const StageFace &x = part.faces[a], &y = part.faces[b];
        return std::make_tuple(x.image, x.abr, x.n) < std::make_tuple(y.image, y.abr, y.n);
    });
    std::vector<uint8_t> cmd = {0xA0, 0xB1, 0xC1, 0xD1};
    int last = -1;
    for (int fi : faces) {
        const StageFace &f = part.faces[fi];
        const StageImage &im = images[f.image];
        int run = (f.image * 8 + f.abr) * 2 + (f.n == 4);
        if (run != last) {
            last = run;
            cmd.push_back((uint8_t)(0xE0 | f.abr));
            cmd.push_back((uint8_t)(0x80 | (f.n == 4)));
            if (im.flat) {
                cmd.push_back(0x90);
                cmd.push_back(0x02), cmd.push_back(part.color[0]), cmd.push_back(part.color[1]), cmd.push_back(part.color[2]);
            } else {
                int page = (im.tpage & 15) - TEX_X / 64, cx = (im.pageClut & 63) * 16 - TEX_X, cy = (im.pageClut >> 6 & 511) - TEX_Y;
                if (page < 0 || cx < 0 || cx > 255 || cy < 0 || cy > 255)
                    return error = part.name + ": a texture page the mesh can't point to", false;
                cmd.push_back(0x91);
                uint8_t tex[7] = {0x01, 0, (uint8_t)page, 0, (uint8_t)cx, (uint8_t)cy, (uint8_t)(im.tpage >> 7 & 3)};
                cmd.insert(cmd.end(), tex, tex + 7);
            }
        }
        const int *ord = PSX[f.n == 4];
        cmd.push_back(0x00);
        for (int c = 0; c < f.n; c++) cmd.push_back((uint8_t)vmap[f.v[ord[c]]]);
        for (int c = 0; c < f.n; c++) cmd.push_back((uint8_t)vmap[f.v[ord[c]]]); /* its normals: the vertices' */
        if (!im.flat)
            for (int c = 0; c < f.n; c++) {
                const float *uv = f.uv[ord[c]];
                cmd.push_back((uint8_t)std::clamp((int)lroundf(uv[0]), 0, 255));
                cmd.push_back((uint8_t)std::clamp((int)lroundf(uv[1]), 0, 255));
            }
    }
    cmd.push_back(0xFF);
    /* the mesh's archive: vertices (0), normals (1), commands (2), its box's middle and corners (5) */
    m.clear();
    for (int k = 0; k < 6; k++) put32(m, 0);
    set32(m, 0, (uint32_t)m.size());
    put16(m, (int)used.size()), put16(m, 0), put16(m, 0);
    int lo[3] = {32767, 32767, 32767}, hi[3] = {-32768, -32768, -32768};
    for (int v : used) {
        for (int k = 0; k < 3; k++) {
            put16(m, loc[v][k]);
            lo[k] = std::min(lo[k], loc[v][k]), hi[k] = std::max(hi[k], loc[v][k]);
        }
    }
    pad4(m);
    set32(m, 20, (uint32_t)m.size());
    put16(m, 9), put16(m, 0), put16(m, 0);
    put16(m, (lo[0] + hi[0]) / 2), put16(m, (lo[1] + hi[1]) / 2), put16(m, (lo[2] + hi[2]) / 2);
    for (int z : {hi[2], lo[2]})
        for (int y : {hi[1], lo[1]})
            for (int x : {hi[0], lo[0]}) put16(m, x), put16(m, y), put16(m, z);
    set32(m, 4, (uint32_t)m.size());
    put16(m, (int)normals.size()), put16(m, 0), put16(m, 0);
    for (const Vec3s &n : normals) put16(m, n.x), put16(m, n.y), put16(m, n.z);
    pad4(m);
    set32(m, 8, (uint32_t)m.size());
    m.insert(m.end(), cmd.begin(), cmd.end());
    pad4(m);
    return true;
}
