/* The fighters' file, the stages' lights, the names, the action scripts and
   the faces, read as FIGHTSTG reads them (dw3_decomp src/fightstg/; MIT, see
   CREDITS.md) */
#include "game.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static int16_t rd16(const uint8_t *p) { int16_t v; memcpy(&v, p, 2); return v; }

static std::vector<uint8_t> load_fid(const std::string &files, int fid) {
    char b[16];
    snprintf(b, sizeof b, "/%04d.bin", fid);
    std::ifstream f(files + b, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

/* A text file: a count, then each string's offset; a string is the font's
   glyph numbers up to a 0 (digits from 4, capitals from 14, small letters
   from 40, 1 a narrow space) */
static std::vector<std::string> read_strings(const std::vector<uint8_t> &d) {
    std::vector<std::string> out;
    if (d.size() < 4) return out;
    uint32_t n = rd32(d.data());
    for (uint32_t i = 0; i < n && 8 + i * 4 <= d.size(); i++) {
        uint32_t o = rd32(d.data() + 4 + i * 4);
        std::string s;
        for (; o < d.size() && d[o]; o++) {
            int c = d[o];
            if (c >= 4 && c <= 13) s += (char)('0' + c - 4);
            else if (c >= 14 && c <= 39) s += (char)('A' + c - 14);
            else if (c >= 40 && c <= 65) s += (char)('a' + c - 40);
            else if (c == 1) { if (s.empty() || s.back() != ' ') s += ' '; }
            else s += '?';
        }
        out.push_back(s);
    }
    return out;
}

/* FILE_FIGHTERS: {base, entries, partners, enemies}; an entry {id, index,
   kind}, up to an id of 0; a partner's FighterInfo is 0xC4 bytes, an enemy's
   0x48 */
void GameData::load(const std::string &files) {
    digimonNames = read_strings(load_fid(files, 79));  /* USDIGNAM: TEXT_DIGIMON_NAMES */
    skillNames = read_strings(load_fid(files, 163));   /* USSKLNAM: TEXT_SKILL_NAMES */

    /* the enemies' names: the battle table (BattleTableEntry, 0x46 bytes), by id */
    std::vector<uint8_t> table = load_fid(files, 463);
    auto enemyName = [&](int id) -> std::string {
        for (size_t o = 0; o + 0x46 <= table.size() && rd16(&table[o]) != 0; o += 0x46)
            if (rd16(&table[o]) == id) {
                int n = rd16(&table[o + 6]);
                return n >= 0 && n < (int)digimonNames.size() ? digimonNames[n] : "";
            }
        return "";
    };

    std::vector<uint8_t> d = load_fid(files, 460);
    if (d.size() >= 16) {
        uint32_t base = rd32(&d[0]), entries = rd32(&d[4]), partners = rd32(&d[8]), enemies = rd32(&d[12]);
        for (uint32_t e = entries; e + 4 <= d.size() && rd16(&d[e]) != 0; e += 4) {
            Fighter f = {};
            f.id = rd16(&d[e]);
            int index = d[e + 2];
            f.kind = d[e + 3];
            f.enemy = f.kind >= 0x3A;
            size_t o = f.enemy ? enemies + index * 0x48 : partners + index * 0xC4;
            if (o + (f.enemy ? 0x46 : 0xC2) > d.size()) continue;
            uint32_t model = rd32(&d[o]), motions = rd32(&d[o + 4]), effects = rd32(&d[o + 8]);
            f.modelFid = model >> 16, f.modelEntry = model & 0xFFFF;
            f.motionsFid = motions >> 16, f.motionsEntry = motions & 0xFFFF;
            f.effectsFid = effects >> 16, f.effectsEntry = effects & 0xFFFF;
            f.distance = rd16(&d[o + 0x10]), f.height = rd16(&d[o + 0x18]);
            /* the face: up to 16 FaceRects, up to an x of 0xFF, at an address
               that assumes the file is at base */
            uint32_t face = rd32(&d[o + 12]) - base;
            for (int i = 0; i < 16 && face + i * 10 + 10 <= d.size() && d[face + i * 10] != 0xFF; i++) {
                memcpy(&f.face[i], &d[face + i * 10], sizeof(FaceRect));
                f.faceUsed[i] = f.face[i].w != 0;
            }
            if (f.enemy) f.cameraCount = rd16(&d[o + 0x44]);
            int cams = f.enemy ? std::min<int>(rd16(&d[o + 0x44]), 3) : 12;
            size_t pos = o + 0x1A, ref = pos + (f.enemy ? 3 : 12) * 6, proj = ref + (f.enemy ? 3 : 12) * 6;
            for (int i = 0; i < cams; i++) {
                GameCamera c;
                for (int k = 0; k < 3; k++) c.pos[k] = rd16(&d[pos + i * 6 + k * 2]), c.ref[k] = rd16(&d[ref + i * 6 + k * 2]);
                c.proj = rd16(&d[proj + i * 2]);
                f.cameras.push_back(c);
            }
            /* a partner's name is its Digimon's (DigimonData.nameId, the Digimon's
               number); an enemy's, its battle table entry's */
            f.name = f.enemy ? enemyName(f.id) : f.kind < (int)digimonNames.size() ? digimonNames[f.kind] : "";
            fighters.push_back(f);
        }
    }

    /* FILE_FIGHT_STAGES: FightStageInfo, 0x58 bytes each, its LightSet at 0x18 */
    std::vector<uint8_t> s = load_fid(files, 459);
    for (size_t o = 0; o + 0x58 <= s.size() && rd32(&s[o]) != 0; o += 0x58) {
        StageLights l = {};
        for (int k = 0; k < 3; k++) l.bgColor[k] = s[o + 12 + k];
        for (int i = 0; i < 3; i++) {
            const uint8_t *g = &s[o + 0x18 + i * 16]; /* GsF_LIGHT: vx, vy, vz, r, g, b */
            float v[3] = {(float)(int32_t)rd32(g), (float)(int32_t)rd32(g + 4), (float)(int32_t)rd32(g + 8)};
            float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            for (int k = 0; k < 3; k++) {
                l.dir[i][k] = len > 0 ? -v[k] / len : 0; /* GsSetFlatLight: the matrix row is -direction */
                l.color[i][k] = len > 0 ? g[12 + k] : 0;
            }
        }
        for (int k = 0; k < 3; k++) l.ambient[k] = (float)(int32_t)rd32(&s[o + 0x48 + k * 4]);
        stages.push_back(l);
    }
}

std::vector<const Fighter *> GameData::forFile(int fid) const {
    std::vector<const Fighter *> out;
    for (const Fighter &f : fighters)
        if (f.modelFid == fid) out.push_back(&f);
    return out;
}

std::string GameData::fileLabel(int fid, const std::string &fileName) const {
    std::set<std::string> seen;
    std::string label;
    for (const Fighter *f : forFile(fid)) {
        if (f->name.empty() || !seen.insert(f->name).second) continue;
        label += (label.empty() ? "" : " / ") + f->name;
    }
    /* an effect model, MEFT<technique>: the technique's name */
    if (label.empty() && fileName.size() >= 8 && fileName.compare(0, 4, "MEFT") == 0) {
        int tech = atoi(fileName.substr(4, 4).c_str());
        if (tech > 0 && tech < (int)skillNames.size() && skillNames[tech].size() > 1) label = skillNames[tech];
    }
    return label;
}

/* --- action scripts (FIGHTSTG_updateBattleScript and its commands) --- */

static std::string fmt(const char *f, ...) {
    char b[256];
    va_list a;
    va_start(a, f);
    vsnprintf(b, sizeof b, f, a);
    va_end(a);
    return b;
}

/* FIGHTSTG_getScriptModel's types */
static const char *who(int t) {
    static const char *W[] = {"own", "partner 1", "partner 2", "partner 3", "enemy 1", "enemy 2", "enemy 3"};
    return t >= 0 && t <= 6 ? W[t] : "own";
}

static std::vector<ScriptLine> read_script(const uint8_t *p, const uint8_t *end) {
    std::vector<ScriptLine> out;
    auto left = [&]() { return (end - p) / 2; };
    auto next = [&]() -> int { int v = left() > 0 ? rd16(p) : 0; p += 2; return v; };
    while (left() > 0 && out.size() < 500) {
        ScriptLine l;
        int op = next();
        switch (op) {
        case 0:
        case 0xFF:
            l.text = "end";
            out.push_back(l);
            return out;
        case 1: {
            int h = next();
            l.text = h == 0 ? "hit: the other side reacts to the last hit" : h == 5 ? "hit: the other side reacts to the next hit"
                                                                             : fmt("hit %d (does nothing)", h);
            break;
        }
        case 2: {
            int cmd = next(), type = next(), arg = 0;
            if (cmd != 1 && cmd != 2 && cmd != 3 && cmd != 4 && cmd != 7 && cmd != 8 && cmd != 9) arg = next();
            const char *m = who(type);
            switch (cmd) {
            case 0:
                if (arg == 0) l.text = fmt("model %s: wait for its motion", m), l.waitMotion = type == 0;
                else if (arg == 1) l.text = fmt("model %s: back to idle", m), l.motion = type == 0 ? 1 : 0;
                else l.text = fmt("model %s: motion %d", m, arg + 1), l.motion = type == 0 ? arg + 1 : 0;
                break;
            case 1: l.text = fmt("model %s: show", m); break;
            case 2: l.text = fmt("model %s: hide", m); break;
            case 3: {
                int x = next(), y = next(), z = next(), t = next();
                l.text = fmt("model %s: move to %d, %d, %d over %d", m, x, y, z, t);
                break;
            }
            case 4: {
                int x = next(), y = next(), z = next();
                next();
                l.text = fmt("model %s: turn to %d, %d, %d", m, x, y, z);
                break;
            }
            case 5:
                if (arg == 0) l.text = fmt("model %s: wait for its jump", m);
                else if (arg == 4) l.text = fmt("model %s: jump 4 forwards %d", m, next());
                else l.text = fmt("model %s: jump %d", m, arg);
                break;
            case 6: l.text = fmt("add fighter %d as %s", arg, m); break;
            case 7: l.text = fmt("model %s: home over %d", m, next()); break;
            case 8: l.text = fmt("model %s: 0x2800 from home over %d", m, next()); break;
            case 9: l.text = fmt("remove %s", m); break;
            default: l.text = fmt("model %s: command %d %d", m, cmd, arg); break;
            }
            break;
        }
        case 3: {
            int mode = next(), e = next();
            std::string name = e == 9999 ? "own" : std::to_string(e);
            if (mode == 1) {
                l.text = "load sprite effect " + name;
            } else {
                int x = next(), y = next(), z = next();
                l.text = fmt("sprite effect %s at %d, %d, %d", name.c_str(), x, y, z);
                l.fxStart = true, l.fxOwn = e == 9999, l.pos[0] = x, l.pos[1] = y, l.pos[2] = z;
                if (e != 9999) l.effect = e;
            }
            break;
        }
        case 4: {
            int mode = next(), id = next();
            std::string name = id == 0x38 ? "own" : id == 0 ? "the battle's" : std::to_string(id);
            if (id == -1) l.text = "stage: none";
            else if (mode == 1) l.text = "wait for stage " + name + "'s motions";
            else {
                int out_ = next(), in = next();
                l.text = fmt("stage %s (fade out %d, in %d)", name.c_str(), out_, in);
            }
            break;
        }
        case 5: {
            int time = next(), mode = next();
            std::string when = time ? fmt(" over %d", time) : "";
            if (mode == 1) {
                int v[14];
                for (int &x : v) x = next();
                l.text = fmt("camera: at %d, %d, %d looking at %d, %d, %d, projection %d", v[0], v[1], v[2], v[3], v[4], v[5], v[13]) + when;
            } else {
                int id = next();
                if (id >= 1 && id <= 4) l.text = fmt("camera: partner's shot %d", id), l.camera = id + 7;
                else if (id >= 5 && id <= 7) l.text = fmt("camera: enemy's shot %d", id - 4), l.camera = 100 + id - 5;
                else l.text = "camera: the enemy's view";
                l.text += when;
            }
            break;
        }
        case 6: {
            int mode = next(), id = next();
            if (mode == 1) l.text = fmt("wait for effect model %d", id);
            else {
                int v[6];
                for (int &x : v) x = next();
                l.text = fmt("effect model %d at %d, %d, %d turned %d, %d, %d", id, v[0], v[1], v[2], v[3], v[4], v[5]);
            }
            break;
        }
        case 7: {
            int o = next(), f = next();
            l.text = o == 0 ? fmt("white flash over %d", f) : fmt("white flash ends over %d", f);
            break;
        }
        case 8:
        case 9: l.text = fmt("command %d (does nothing)", op); break;
        case 10: {
            int s = next(), t = next();
            l.text = s == 0x62 || s == 0x63 ? fmt("hit sound for %d", t) : fmt("sound %d for %d", s, t);
            break;
        }
        case 11: l.waitTicks = next(), l.text = fmt("wait %d", l.waitTicks); break;
        default:
            l.text = fmt("unknown command %d: the game would stop here", op);
            out.push_back(l);
            return out;
        }
        out.push_back(l);
    }
    return out;
}

std::vector<std::vector<ScriptLine>> read_scripts(const uint8_t *a, size_t size) {
    std::vector<std::vector<ScriptLine>> out;
    if (!a || size < 4) return out;
    uint32_t n = rd32(a) / 4;
    for (uint32_t i = 0; i < n && (i + 1) * 4 <= size; i++) {
        uint32_t o = rd32(a + i * 4);
        out.push_back(o < size ? read_script(a + o, a + size) : std::vector<ScriptLine>{});
    }
    return out;
}

/* --- the face (FIGHTSTG_updateFace, getEyesFrame, moveFacePart) --- */

bool step_face(const Fighter &f, FaceState &s, int motion, int texX, int texY, std::vector<uint16_t> &vram) {
    int eyes = motion >= 4 && motion <= 11 ? 2 : motion == 2 ? 1 : 0;
    if (eyes == 0) {
        switch (s.blinkTimer >> 1) {
        case 0: s.blinkTimer = (rand() & 0x7F) + 60; eyes = 0; break;
        case 1: case 2: case 5: case 6: eyes = 1; break;
        case 3: case 4: eyes = 2; break;
        default: eyes = 0; break;
        }
        if (--s.blinkTimer < 0) s.blinkTimer = 0;
    } else {
        s.blinkTimer = 0;
    }
    s.time++;
    int cycle = s.time % 18 / 6;
    bool changed = false;
    for (int i = 0; i < 16; i++) {
        int want = i < 2 ? eyes : cycle;
        if (!f.faceUsed[i] || s.frame[i] == want) continue;
        s.frame[i] = want;
        const FaceRect &r = f.face[i];
        /* a DR_MOVE: the frame's rectangle over the part's */
        std::vector<uint16_t> tmp(r.w * r.h);
        for (int y = 0; y < r.h; y++)
            for (int x = 0; x < r.w; x++)
                tmp[y * r.w + x] = vram[((texY + r.frames[want][1] + y) & 511) * 1024 + ((texX + r.frames[want][0] + x) & 1023)];
        for (int y = 0; y < r.h; y++)
            for (int x = 0; x < r.w; x++)
                vram[((texY + r.y + y) & 511) * 1024 + ((texX + r.x + x) & 1023)] = tmp[y * r.w + x];
        changed = true;
    }
    return changed;
}
