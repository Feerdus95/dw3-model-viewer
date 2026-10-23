/* --dump-compendium: the battle's models, fighters, cameras and action scripts as data/compendium/compendium.json, and
   what could and couldn't be read as audit.md. Read-only over the extracted data; no window, no GL. The same data gives
   the same bytes: ids come from the disc's own keys (a fid, a fighter id, a camera's index, a script's archive and
   number), never from an order of listing, and nothing in the files is a time or a path. */

namespace comp {

static std::string esc(const std::string &s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') o += '\\', o += (char)c;
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o + "\"";
}

/* one JSON object on one line, keys in the order they are added */
struct Obj {
    std::string s;
    Obj &raw(const char *k, const std::string &v) {
        s += (s.empty() ? "{" : ",") + esc(k) + ":" + v;
        return *this;
    }
    Obj &str(const char *k, const std::string &v) { return raw(k, esc(v)); }
    Obj &num(const char *k, long long v) { return raw(k, std::to_string(v)); }
    Obj &flt(const char *k, double v) {
        char b[32];
        snprintf(b, sizeof b, "%.4f", v);
        return raw(k, b);
    }
    Obj &b(const char *k, bool v) { return raw(k, v ? "true" : "false"); }
    std::string done() const { return s.empty() ? "{}" : s + "}"; }
};

template <class T, class F> static std::string arr(const std::vector<T> &v, F f) {
    std::string o = "[";
    for (size_t i = 0; i < v.size(); i++) o += (i ? "," : "") + f(v[i]);
    return o + "]";
}
static std::string ints(const int *v, int n) {
    std::vector<int> a(v, v + n);
    return arr(a, [](int x) { return std::to_string(x); });
}
static std::string ints3(int a, int b, int c) {
    int v[3] = {a, b, c};
    return ints(v, 3);
}
static std::string strs(const std::vector<std::string> &v) {
    return arr(v, [](const std::string &x) { return esc(x); });
}
static std::string lines(const std::vector<std::string> &recs) { /* one record a line, so a diff is a diff of records */
    std::string o = "[\n";
    for (size_t i = 0; i < recs.size(); i++) o += "  " + recs[i] + (i + 1 < recs.size() ? ",\n" : "\n");
    return o + "]";
}
static uint32_t fnv(const void *p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ ((const uint8_t *)p)[i]) * 16777619u;
    return h;
}
static std::string hex8(uint32_t v) {
    char b[16];
    snprintf(b, sizeof b, "%08x", v);
    return b;
}

static std::string cam_id(int fighter, bool enemy, int i) { return "cam:" + std::to_string(fighter) + ":" + (enemy ? "enemy" : "partner") + ":" + std::to_string(i); }
static std::string script_id(int fid, int entry, int idx) { return "script:" + std::to_string(fid) + "." + std::to_string(entry) + ":" + std::to_string(idx); }

/* the slots the viewer's Fighter section names for a script's number (script_role); the rest are only known by the techniques that run them */
static const char *slot_name(int i) {
    switch (i) {
    case 2: return "hit reaction";
    case 3: return "KO reaction";
    case 4: return "evade reaction";
    case 5: return "basic attack (Fight)";
    default: return "";
    }
}

struct Scr {
    int fid, entry, idx;
    std::vector<ScriptLine> lines;
    std::set<int> users; /* fighter ids */
    bool present = true; /* its slot's offset is not 0: the archive's table has slots with no script (offset 0) */
};

} // namespace comp

/* The dump; the exit code is 1 only when it couldn't read or write what it needs, a thing it couldn't resolve is in audit.md */
static int dump_compendium(const std::string &outArg, const std::string &exe) {
    using namespace comp;
    if (g_game.fighters.empty()) {
        fprintf(stderr, "dump-compendium: no fighters (SMDLDATA.PRO, file 460) in %s\n", g_data.c_str());
        return 1;
    }
    std::vector<std::string> problems; /* anything unresolved, in the order it was met */
    std::vector<std::string> noScripts; /* fighters whose effects entry has no script table (first offset 0): not an error in itself */
    auto problem = [&](const std::string &s) { problems.push_back(s); };
    if (!exe.empty()) read_techs({exe});
    else read_techs({g_data + "/../SLES_039.36"});
    bool haveTechs = !g_techs.empty();
    if (!haveTechs) problem("techniques: the game's exe (SLES_039.36) was not found, so no script is tied to a technique (pass --exe PATH)");

    /* ---- fighters ---- */
    std::vector<const Fighter *> fighters;
    for (const Fighter &f : g_game.fighters) fighters.push_back(&f);
    std::stable_sort(fighters.begin(), fighters.end(), [](const Fighter *a, const Fighter *b) { return a->id < b->id; });
    std::map<int, int> idCount;
    for (const Fighter *f : fighters) idCount[f->id]++;
    for (auto &kv : idCount)
        if (kv.second > 1) problem("fighter id " + std::to_string(kv.first) + " appears " + std::to_string(kv.second) + " times: its ids are not unique");

    /* ---- script archives: one record per (effects fid, entry, number), shared by the fighters that use the archive ---- */
    std::map<std::tuple<int, int, int>, Scr> scripts;
    std::map<std::pair<int, int>, int> archiveUsers; /* archive -> fighters */
    std::map<int, std::vector<uint8_t>> fileCache;
    auto file = [&](int fid) -> const std::vector<uint8_t> * {
        auto it = fileCache.find(fid);
        if (it == fileCache.end()) {
            std::vector<uint8_t> b;
            read_file(fid_path(fid), b);
            it = fileCache.emplace(fid, std::move(b)).first;
        }
        return it->second.empty() ? nullptr : &it->second;
    };
    for (const Fighter *f : fighters) {
        std::string who = "fighter:" + std::to_string(f->id);
        for (int w = 0; w < 3; w++) { /* model, motions and effects files must exist */
            int fid = w == 0 ? f->modelFid : w == 1 ? f->motionsFid : f->effectsFid;
            if (fid <= 0 || !file(fid)) problem(who + ": " + (w == 0 ? "model" : w == 1 ? "motions" : "effects") + " file " + std::to_string(fid) + " is missing");
        }
        const std::vector<uint8_t> *fb = f->effectsFid > 0 ? file(f->effectsFid) : nullptr;
        if (!fb) continue;
        Archive a{fb->data(), fb->size()};
        if (f->effectsEntry >= a.count() || !a.entry(f->effectsEntry)) {
            problem(who + ": effects entry " + std::to_string(f->effectsEntry) + " is not in file " + std::to_string(f->effectsFid));
            continue;
        }
        archiveUsers[{f->effectsFid, f->effectsEntry}]++;
        std::vector<std::vector<ScriptLine>> sc = read_scripts(a.entry(f->effectsEntry), a.entry_size(f->effectsEntry));
        if (sc.empty()) noScripts.push_back("fighter:" + std::to_string(f->id) + " (" + f->name + "), effects entry " + std::to_string(f->effectsFid) + "." + std::to_string(f->effectsEntry));
        for (size_t i = 0; i < sc.size(); i++) {
            Scr &s = scripts[{f->effectsFid, f->effectsEntry, (int)i}];
            if (s.lines.empty()) {
                uint32_t off;
                memcpy(&off, a.entry(f->effectsEntry) + i * 4, 4);
                s.fid = f->effectsFid, s.entry = f->effectsEntry, s.idx = (int)i;
                s.present = off != 0;
                if (s.present) s.lines = sc[i];
            }
            s.users.insert(f->id);
        }
    }

    /* ---- models: the disc's, as the viewer lists them ---- */
    std::vector<Entry> list = scan_models();
    std::map<int, std::vector<const Fighter *>> byModel;
    for (const Fighter *f : fighters) byModel[f->modelFid].push_back(f);
    std::set<int> listed;
    for (const Entry &e : list) listed.insert(e.fid);
    for (auto &kv : byModel)
        if (!listed.count(kv.first)) problem("model file " + std::to_string(kv.first) + " is used by fighter:" + std::to_string(kv.second[0]->id) + " but is not a model the viewer lists");

    std::vector<std::string> modelRecs;
    int mDigimon = 0, mEffect = 0, mOther = 0, mBad = 0, mNoFighter = 0;
    for (const Entry &e : list) {
        std::vector<uint8_t> bytes;
        read_file(fid_path(e.fid), bytes);
        const std::vector<const Fighter *> &fs = byModel.count(e.fid) ? byModel[e.fid] : std::vector<const Fighter *>{};
        const char *cls = !fs.empty() ? "digimon_model" : e.name.compare(0, 4, "MEFT") == 0 ? "effect_model" : "unclassified";
        (fs.empty() ? (cls[0] == 'e' ? mEffect : mOther) : mDigimon)++;
        if (fs.empty() && cls[0] != 'e') problem("model " + std::to_string(e.fid) + " " + e.name + ": no fighter and not a MEFT effect model, left unclassified");
        if (fs.empty()) mNoFighter++;
        Obj o;
        o.str("id", "model:" + std::to_string(e.fid)).num("fid", e.fid).str("file_name", e.name).str("class", cls).str("label", e.label);
        std::vector<std::string> fids;
        for (const Fighter *f : fs) fids.push_back("fighter:" + std::to_string(f->id));
        o.raw("fighters", strs(fids));
        Model m;
        if (!m.load(e.name, std::move(bytes), fs.empty() ? nullptr : fs[0])) {
            mBad++;
            problem("model " + std::to_string(e.fid) + " " + e.name + ": " + m.error);
            o.b("loaded", false).str("error", m.error);
        } else {
            m.setMotion(1);
            m.evaluate();
            float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
            bool any = false;
            for (size_t k = 1; k < m.bones.size(); k++) {
                if (!m.pose[k].visible || m.bones[k].mesh < 0) continue;
                const float *w = m.pose[k].world;
                for (const Vec3s &pv : m.meshes[m.bones[k].mesh].verts) {
                    any = true;
                    for (int i = 0; i < 3; i++) {
                        float p = w[i] * pv.x + w[4 + i] * pv.y + w[8 + i] * pv.z + w[12 + i];
                        lo[i] = std::min(lo[i], p), hi[i] = std::max(hi[i], p);
                    }
                }
            }
            size_t polys = 0;
            for (const Mesh &me : m.meshes) polys += me.polys.size();
            o.b("loaded", true).num("bones", (long long)m.bones.size() - 1).num("polys", (long long)polys).num("motions", (long long)m.motions.size());
            if (any)
                o.raw("bbox", Obj().str("pose", "motion 1, key 0").str("space", "model units, y down").str("provenance", "derived")
                                  .raw("min", ints3((int)lo[0], (int)lo[1], (int)lo[2])).raw("max", ints3((int)hi[0], (int)hi[1], (int)hi[2])).done());
        }
        modelRecs.push_back(o.done());
    }

    /* ---- cameras and fighters ---- */
    std::vector<std::string> camRecs, fighterRecs;
    std::map<std::string, std::vector<std::string>> camUsers; /* camera id -> scripts (own side) */
    std::map<uint32_t, int> fpCount;
    int camTotal = 0, camNeverUsed = 0;
    std::vector<std::string> unusedCams;
    for (auto &kv : scripts) { /* own-side camera refs: a partner's shots 1-4 are its cameras 8-11, an enemy's 1-3 its 0-2 */
        const Scr &s = kv.second;
        for (int fi : s.users) {
            const Fighter *f = nullptr;
            for (const Fighter *g : fighters)
                if (g->id == fi) f = g;
            if (!f) continue;
            for (const ScriptLine &l : s.lines) {
                if (l.camMode != 1) continue;
                int idx = l.camId <= 4 ? (f->enemy ? -1 : l.camId + 7) : (f->enemy ? l.camId - 5 : -1);
                if (idx >= 0 && idx < (int)f->cameras.size()) camUsers[cam_id(f->id, f->enemy, idx)].push_back(script_id(s.fid, s.entry, s.idx));
            }
        }
    }
    for (auto &kv : camUsers) {
        std::sort(kv.second.begin(), kv.second.end());
        kv.second.erase(std::unique(kv.second.begin(), kv.second.end()), kv.second.end());
    }
    for (const Fighter *f : fighters) {
        std::vector<std::string> cams, scr;
        for (int i = 0; i < (int)f->cameras.size(); i++) {
            const GameCamera &c = f->cameras[i];
            std::string id = cam_id(f->id, f->enemy, i);
            cams.push_back(id);
            camTotal++;
            int16_t raw[7] = {c.pos[0], c.pos[1], c.pos[2], c.ref[0], c.ref[1], c.ref[2], c.proj};
            uint32_t fp = fnv(raw, sizeof raw);
            fpCount[fp]++;
            float eye[3], ref[3];
            float bp[3] = {(float)c.pos[0], (float)c.pos[1], (float)c.pos[2]}, br[3] = {(float)c.ref[0], (float)c.ref[1], (float)c.ref[2]};
            battle_to_view(*f, bp, eye);
            battle_to_view(*f, br, ref);
            float d[3] = {eye[0] - ref[0], eye[1] - ref[1], eye[2] - ref[2]};
            float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            CameraRole r = camera_role(f->enemy, i);
            Obj o;
            o.str("id", id).str("fighter", "fighter:" + std::to_string(f->id)).str("fighter_kind", f->enemy ? "enemy" : "partner").num("index", i)
                .str("role", r.name).str("role_what", r.what).num("role_group", r.group)
                .raw("native", Obj().str("provenance", "extracted").raw("pos", ints3(c.pos[0], c.pos[1], c.pos[2]))
                                    .raw("ref", ints3(c.ref[0], c.ref[1], c.ref[2])).num("proj", c.proj).done());
            Obj dv;
            dv.str("provenance", "derived").str("space", "the fighter's own, as apply_camera sets it: model units at the origin, y up as the PSX flip leaves it")
                .raw("eye", arr(std::vector<float>{eye[0], eye[1], eye[2]}, [](float x) { char b[32]; snprintf(b, sizeof b, "%.2f", x); return std::string(b); }))
                .raw("target", arr(std::vector<float>{ref[0], ref[1], ref[2]}, [](float x) { char b[32]; snprintf(b, sizeof b, "%.2f", x); return std::string(b); }));
            if (len >= 1) dv.flt("distance", len).flt("yaw_deg", atan2f(d[0], d[2]) * 57.29578f).flt("pitch_deg", asinf(d[1] / len) * 57.29578f);
            if (c.proj > 0) dv.flt("fov_deg", 2 * atanf(120.0f / c.proj) * 57.29578f);
            o.raw("derived", dv.done()).str("fingerprint", hex8(fp));
            auto u = camUsers.find(id);
            o.raw("used_by", strs(u == camUsers.end() ? std::vector<std::string>{} : u->second));
            if (u == camUsers.end() && r.group == 2) { /* a shot nobody on its own side calls */
                camNeverUsed++;
                unusedCams.push_back(id);
            }
            camRecs.push_back(o.done());
        }
        for (const auto &kv : scripts)
            if (kv.second.users.count(f->id)) scr.push_back(script_id(kv.second.fid, kv.second.entry, kv.second.idx));
        Obj o;
        o.str("id", "fighter:" + std::to_string(f->id)).num("native_id", f->id).num("native_kind", f->kind).b("enemy", f->enemy).str("name", f->name)
            .str("model", "model:" + std::to_string(f->modelFid)).num("model_entry", f->modelEntry)
            .num("motions_fid", f->motionsFid).num("motions_entry", f->motionsEntry)
            .str("scripts_archive", std::to_string(f->effectsFid) + "." + std::to_string(f->effectsEntry))
            .num("height", f->height).num("distance", f->distance);
        if (f->enemy) o.num("camera_count_native", f->cameraCount);
        o.raw("cameras", strs(cams)).raw("scripts", strs(scr)).str("provenance", "extracted");
        fighterRecs.push_back(o.done());
    }

    /* ---- scripts ---- */
    std::map<int, std::vector<const TechRow *>> techOf;
    for (const TechRow &t : g_techs) techOf[t.script].push_back(&t);
    std::vector<std::string> scriptRecs;
    long long cmdTotal = 0, camShotCmd = 0, camExplCmd = 0, camDefCmd = 0, camCross = 0, unknownCmd = 0;
    int sCam = 0, sShot = 0, sExpl = 0, sDef = 0, sBoth = 0, sNoTech = 0, sNamed = 0, sEmpty = 0, sUnknown = 0, sAbsent = 0;
    std::vector<std::string> noTech;
    for (const auto &kv : scripts) {
        const Scr &s = kv.second;
        std::vector<std::string> txt, users, motions, techs;
        std::vector<std::string> camCmds;
        std::set<int> mo, fx, em;
        bool shot = false, expl = false, def = false, unknown = false;
        int n = 0;
        if (!s.present) sAbsent++;
        for (const ScriptLine &l : s.lines) {
            n++;
            txt.push_back(l.text);
            if (l.motion) mo.insert(l.motion);
            if (l.effect >= 0) fx.insert(l.effect);
            if (l.emStart) em.insert(l.emId);
            if (l.text.compare(0, 7, "unknown") == 0) unknown = true, unknownCmd++;
            if (l.camMode) {
                Obj c;
                c.num("line", n - 1).num("time", l.camTime);
                if (l.camMode == 1) {
                    shot = true, camShotCmd++;
                    c.str("kind", "shot").num("script_camera", l.camId)
                        .str("side", l.camId <= 4 ? "partner" : "enemy").num("native_index", l.camId <= 4 ? l.camId + 7 : l.camId - 5);
                } else if (l.camMode == 2) {
                    expl = true, camExplCmd++;
                    c.str("kind", "explicit").raw("words", ints(l.camRaw, 14)).str("words_provenance", "decoded")
                        .raw("pos", ints(l.camRaw, 3)).raw("ref", ints(l.camRaw + 3, 3)).num("proj", l.camRaw[13]);
                } else {
                    def = true, camDefCmd++;
                    c.str("kind", "default_view").num("raw_id", l.camId);
                }
                camCmds.push_back(c.done());
            }
        }
        cmdTotal += n;
        sCam += !camCmds.empty(), sShot += shot, sExpl += expl, sDef += def, sBoth += shot && expl, sUnknown += unknown, sEmpty += s.present && s.lines.empty();
        for (int u : s.users) users.push_back("fighter:" + std::to_string(u));
        /* a shot that names the other side's camera can't be pinned to a camera of this fighter */
        for (int u : s.users) {
            const Fighter *f = nullptr;
            for (const Fighter *g : fighters)
                if (g->id == u) f = g;
            if (!f) continue;
            for (const ScriptLine &l : s.lines)
                if (l.camMode == 1 && (l.camId <= 4) == f->enemy) camCross++;
        }
        auto tit = techOf.find(s.idx);
        std::vector<std::string> tl;
        if (tit != techOf.end())
            for (const TechRow *t : tit->second) tl.push_back(Obj().num("id", t->id).str("name", t->name).done());
        bool named = *slot_name(s.idx) != 0;
        sNamed += named;
        if (tl.empty() && !named && haveTechs && s.present) sNoTech++, noTech.push_back(script_id(s.fid, s.entry, s.idx));
        Obj o;
        o.str("id", script_id(s.fid, s.entry, s.idx)).str("archive", std::to_string(s.fid) + "." + std::to_string(s.entry)).num("index", s.idx)
            .raw("fighters", strs(users)).str("slot", slot_name(s.idx)).raw("techniques", "[" + [&] { std::string j; for (size_t i = 0; i < tl.size(); i++) j += (i ? "," : "") + tl[i]; return j; }() + "]")
            .str("techniques_provenance", haveTechs ? "decoded (TechData.script)" : "unavailable")
            .b("present", s.present).str("coverage", !s.present ? "absent" : unknown ? "partial" : "decoded")
            .raw("camera_commands", "[" + [&] { std::string j; for (size_t i = 0; i < camCmds.size(); i++) j += (i ? "," : "") + camCmds[i]; return j; }() + "]")
            .raw("motions", arr(std::vector<int>(mo.begin(), mo.end()), [](int x) { return std::to_string(x); }))
            .raw("sprite_effects", arr(std::vector<int>(fx.begin(), fx.end()), [](int x) { return std::to_string(x); }))
            .raw("effect_models", arr(std::vector<int>(em.begin(), em.end()), [](int x) { return std::to_string(x); }))
            .raw("commands", strs(txt));
        scriptRecs.push_back(o.done());
    }

    /* ---- compendium.json ---- */
    std::string json = "{\n\"schema_version\":1,\n";
    json += "\"game\":" + Obj().str("title", "Digimon World 2003").str("edition", "PAL SLES-03936").done() + ",\n";
    json += "\"viewer_version\":" + esc(VERSION) + ",\n";
    json += "\"coordinates\":" + Obj()
        .str("native", "pos and ref are the battle's space, int16, y and z as FighterInfo stores them; proj is the projection distance on a 320x240 screen")
        .str("derived_rule", "derived blocks are computed by the viewer (battle_to_view, apply_camera) and are never read back as native values")
        .str("fov", "2*atan(120/proj)").done() + ",\n";
    json += "\"provenance_values\":" + strs({"extracted", "decoded", "derived", "inferred", "visually_verified"}) + ",\n";
    json += "\"playback_limitations\":" + strs({"the script player leaves out jumps and moves, the other side of the fight, sounds, fades, stages and models a script adds"}) + ",\n";
    json += "\"models\":" + lines(modelRecs) + ",\n\"fighters\":" + lines(fighterRecs) + ",\n\"cameras\":" + lines(camRecs) + ",\n\"scripts\":" + lines(scriptRecs) + "\n}\n";

    /* ---- audit.md ---- */
    int partners = 0, enemies = 0;
    for (const Fighter *f : fighters) (f->enemy ? enemies : partners)++;
    int shared = 0;
    for (auto &kv : byModel) shared += kv.second.size() > 1;
    long long archRefs = 0;
    for (auto &kv : archiveUsers) archRefs += kv.second;
    int dupCam = 0;
    for (auto &kv : fpCount) dupCam += kv.second - 1;
    std::string md = "# Compendium audit\n\nGenerated by `dw3mv --dump-compendium`. The numbers under \"Counts\" are `key: value`, one a line, for diffing; \"unique\" counts records, \"references\" counts uses.\n\n## Counts\n\n```\n";
    auto kvp = [&](const char *k, long long v) { md += std::string(k) + ": " + std::to_string(v) + "\n"; };
    kvp("models.unique", (long long)list.size());
    kvp("models.digimon", mDigimon);
    kvp("models.effect", mEffect);
    kvp("models.unclassified", mOther);
    kvp("models.without_fighter", mNoFighter);
    kvp("models.unreadable", mBad);
    kvp("models.shared_by_several_fighters", shared);
    kvp("fighters.unique", (long long)fighters.size());
    kvp("fighters.partners", partners);
    kvp("fighters.enemies", enemies);
    kvp("cameras.unique_records", camTotal);
    kvp("cameras.distinct_values", (long long)fpCount.size());
    kvp("cameras.repeated_values", dupCam);
    kvp("cameras.shots_never_called_by_own_side", camNeverUsed);
    kvp("script_archives.unique", (long long)archiveUsers.size());
    kvp("script_archives.fighter_references", archRefs);
    kvp("scripts.unique", (long long)scripts.size());
    kvp("scripts.present", (long long)scripts.size() - sAbsent);
    kvp("scripts.absent_slot", sAbsent);
    kvp("scripts.empty", sEmpty);
    kvp("scripts.with_unknown_command", sUnknown);
    kvp("scripts.with_camera_command", sCam);
    kvp("scripts.with_shot_by_id", sShot);
    kvp("scripts.with_explicit_camera", sExpl);
    kvp("scripts.with_default_view", sDef);
    kvp("scripts.with_shot_and_explicit", sBoth);
    kvp("scripts.named_slot", sNamed);
    kvp("scripts.without_technique", sNoTech);
    kvp("commands.total", cmdTotal);
    kvp("commands.camera_shot_by_id", camShotCmd);
    kvp("commands.camera_explicit", camExplCmd);
    kvp("commands.camera_default_view", camDefCmd);
    kvp("commands.camera_shot_other_side", camCross);
    kvp("commands.unknown", unknownCmd);
    kvp("techniques.read", (long long)g_techs.size());
    kvp("fighters.without_script_table", (long long)noScripts.size());
    kvp("problems", (long long)problems.size());
    md += "```\n\n## Reading these\n\n- A shot by id is the script asking for a fighter's camera: a partner's shots 1-4 are its cameras 8-11, an enemy's 1-3 its cameras 0-2. A shot of the other side (`camera_shot_other_side`) is the opposing fighter's camera in the real fight, and that fighter is not known here, so it is counted but not tied to any camera record.\n";
    md += "- An explicit camera carries its own eye, target and projection (words 0-5 and 13 are read; the other words are kept as the script has them).\n";
    md += "- `used_by` in a camera record is only its own side's scripts. A shot nobody calls may still be a battle view or portrait: only the shots (role group 2) are counted as never called.\n";
    md += "- A script slot with offset 0 in its archive's table has no script (`absent`): the viewer's `read_scripts` still reads the table itself as a command there, so those show as \"unknown command 72\" in the Fighter section; the dump does not count them as scripts.\n";
    md += "- A script is tied to a technique only through TechData.script, and the slot names 2-5 are the viewer's own. Scripts with neither are listed below, not guessed.\n\n";
    md += "## Scripts without a technique or a named slot\n\n";
    if (!haveTechs) md += "Techniques were not available.\n";
    else if (noTech.empty()) md += "None.\n";
    else {
        for (size_t i = 0; i < noTech.size() && i < 200; i++) md += "- " + noTech[i] + "\n";
        if (noTech.size() > 200) md += "- ... " + std::to_string(noTech.size() - 200) + " more\n";
    }
    md += "\n## Shots (role group 2) no script of their own side calls\n\n";
    if (unusedCams.empty()) md += "None.\n";
    for (size_t i = 0; i < unusedCams.size() && i < 200; i++) md += "- " + unusedCams[i] + "\n";
    md += "\n## Problems\n\n";
    if (problems.empty()) md += "None.\n";
    for (const std::string &p : problems) md += "- " + p + "\n";

    /* ---- write: beside the data, never over a file of it ---- */
    std::filesystem::path dir = outArg.empty() ? std::filesystem::path(g_data).parent_path() / "compendium" : std::filesystem::path(outArg);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    auto put = [&](const char *name, const std::string &text) {
        std::filesystem::path p = dir / name, t = dir / (std::string(name) + ".tmp");
        {
            std::ofstream f(t, std::ios::binary);
            f.write(text.data(), (std::streamsize)text.size());
            if (!f) return false;
        }
        std::filesystem::rename(t, p, ec);
        return !ec;
    };
    if (!put("compendium.json", json) || !put("audit.md", md)) {
        fprintf(stderr, "dump-compendium: can't write to %s\n", dir.string().c_str());
        return 1;
    }
    printf("compendium: %d models, %d fighters, %d cameras, %d scripts, %d problems -> %s\n", (int)list.size(), (int)fighters.size(), camTotal,
           (int)scripts.size(), (int)problems.size(), dir.string().c_str());
    return 0;
}
