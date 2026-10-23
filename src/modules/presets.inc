/* Camera presets: data/compendium/presets.json, apart from the compendium the dump regenerates. A preset is a candidate camera made
   for one combination: a fighter, one of its scripts and a donor camera. Its status (candidate, approved, rejected) belongs to that
   combination, so the id is made of the three. The camera is a snapshot of the native values, edited or not: loading a preset never
   rebuilds it from the donor. References are the compendium's ids (fighter:N, script:FID.ENTRY:N, cam:N:KIND:N) and are checked
   against the disc's data when read; one that doesn't resolve is reported, never replaced by a guess. */

static const int PRESET_SCHEMA = 1;

struct Preset {
    std::string name, status = "candidate", notes;
    int fighter = 0;                                   /* the target: a fighter id */
    int scriptFid = 0, scriptEntry = 0, scriptIdx = 0; /* its script: the effects archive and the number */
    int donor = 0, donorIdx = 0;                       /* the donor camera: a fighter id and its camera's index */
    bool donorEnemy = false;
    GameCamera c = {};
    bool every = false;                                /* also at the cuts the player skips */
    std::string context = "base_game", donorContext = "base_game"; /* the data sets the target and the donor are in (--context) */
    double fitK = 0;                                   /* 0, or the scale of the "Fit to this model" it started from (derived) */
    std::vector<std::string> warn;                     /* what doesn't resolve here (not written) */

    std::string fighter_ref() const { return "fighter:" + std::to_string(fighter); }
    std::string script_ref() const { return comp::script_id(scriptFid, scriptEntry, scriptIdx); }
    std::string donor_ref() const { return comp::cam_id(donor, donorEnemy, donorIdx); }
    std::string id() const {
        std::string c = (context == "base_game" ? "" : context + "|") + (donorContext == "base_game" ? "" : "donor@" + donorContext + "|");
        return "preset:" + c + fighter_ref() + "|" + script_ref() + "|" + donor_ref();
    }
};

static const char *const PRESET_STATUS[3] = {"candidate", "approved", "rejected"};

static std::filesystem::path preset_path() { return std::filesystem::path(g_data).parent_path() / "compendium" / "presets.json"; }

static const Fighter *fighter_by_id(int id, bool enemy, bool anySide = true) {
    for (const Fighter &f : g_game.fighters)
        if (f.id == id && (anySide || f.enemy == enemy)) return &f;
    return nullptr;
}

/* What of P doesn't resolve against the disc's data here */
static void preset_check(Preset &p) {
    p.warn.clear();
    /* a preset of a mod's fighter says so, instead of looking like a fighter that doesn't exist */
    for (int i = 0; i < 2; i++) {
        const std::string &c = i ? p.donorContext : p.context;
        if (c != "base_game" && c != g_context) p.warn.push_back(std::string(i ? "its donor" : "its target") + " is in the context " + c + ", and the one loaded is " + g_context);
    }
    if (!p.warn.empty()) return;
    const Fighter *t = fighter_by_id(p.fighter, false);
    if (!t) p.warn.push_back(p.fighter_ref() + " is not a fighter of this disc");
    const Fighter *d = fighter_by_id(p.donor, p.donorEnemy, false);
    if (!d) p.warn.push_back("donor " + std::to_string(p.donor) + " is not a " + (p.donorEnemy ? "enemy" : "partner") + " of this disc");
    else if (p.donorIdx < 0 || p.donorIdx >= (int)d->cameras.size()) p.warn.push_back(p.donor_ref() + " does not exist");
    if (t && d && t->enemy != d->enemy) p.warn.push_back("donor and target are not on the same side");
    if (t) {
        if (t->effectsFid != p.scriptFid || t->effectsEntry != p.scriptEntry) p.warn.push_back(p.script_ref() + " is not in " + t->name + "'s scripts");
        else {
            std::vector<uint8_t> file;
            int n = 0;
            if (read_file(fid_path(t->effectsFid), file)) {
                Archive a{file.data(), file.size()};
                if (p.scriptEntry < a.count() && a.entry(p.scriptEntry)) n = (int)read_scripts(a.entry(p.scriptEntry), a.entry_size(p.scriptEntry)).size();
            }
            if (p.scriptIdx < 0 || p.scriptIdx >= n) p.warn.push_back(p.script_ref() + " does not exist");
        }
    }
}

static std::string preset_json(const std::vector<Preset> &list) {
    std::vector<Preset> sorted = list;
    std::sort(sorted.begin(), sorted.end(), [](const Preset &a, const Preset &b) { return a.id() < b.id(); });
    std::vector<std::string> recs;
    for (const Preset &p : sorted) {
        comp::Obj cam;
        cam.str("space", "native: the battle's, int16 as FighterInfo stores them").str("provenance", "snapshot, edited or not")
            .raw("pos", comp::ints3(p.c.pos[0], p.c.pos[1], p.c.pos[2])).raw("ref", comp::ints3(p.c.ref[0], p.c.ref[1], p.c.ref[2])).num("proj", p.c.proj);
        comp::Obj o;
        o.str("id", p.id()).str("name", p.name).str("status", p.status).str("notes", p.notes)
            .raw("target", comp::Obj().str("fighter", p.fighter_ref()).str("script", p.script_ref()).str("context", p.context).done())
            .str("donor_camera_id", p.donor_ref()).str("donor_context", p.donorContext).raw("camera", cam.done()).num("also_at_cuts", p.every ? 1 : 0);
        if (p.fitK > 0) o.raw("transform", comp::Obj().str("kind", "fit_to_model").flt("scale", p.fitK).str("provenance", "derived").done());
        recs.push_back(o.done());
    }
    return "{\n\"schema_version\":" + std::to_string(PRESET_SCHEMA) + ",\n\"game\":" +
           comp::Obj().str("title", "Digimon World 2003").str("edition", "PAL SLES-03936").done() + ",\n\"presets\":" + comp::lines(recs) + "\n}\n";
}

/* Reads a presets file. False with ERR set when it is not one this viewer can read at all (not JSON, another schema version);
   a preset in it that can't be read is skipped and named in SKIPPED */
static bool preset_parse(const std::string &text, std::vector<Preset> &out, std::string &err, std::vector<std::string> &skipped) {
    const char *p = text.c_str();
    Json root;
    if (!json_parse(p, root) || root.type != '{') return err = "not a JSON object", false;
    const Json *v = root.get("schema_version");
    if (!v || v->type != 'n') return err = "no schema_version: not a presets file", false;
    if ((int)v->num != PRESET_SCHEMA) return err = "schema_version " + std::to_string((int)v->num) + " is not one this viewer reads (it reads " + std::to_string(PRESET_SCHEMA) + ")", false;
    const Json *list = root.get("presets");
    if (!list || list->type != '[') return err = "no presets list", false;
    int n = 0;
    for (const auto &it : list->items) {
        n++;
        const Json &j = it.second;
        Preset q;
        auto str = [&](const Json *o, const char *k) { const Json *x = o ? o->get(k) : nullptr; return x && x->type == 's' ? x->str : std::string(); };
        const Json *tg = j.get("target"), *cam = j.get("camera"), *pos = cam ? cam->get("pos") : nullptr, *ref = cam ? cam->get("ref") : nullptr, *pj = cam ? cam->get("proj") : nullptr;
        char kind[16] = {};
        std::string f = str(tg, "fighter"), s = str(tg, "script"), d = str(&j, "donor_camera_id");
        bool ok = j.type == '{' && sscanf(f.c_str(), "fighter:%d", &q.fighter) == 1 &&
                  sscanf(s.c_str(), "script:%d.%d:%d", &q.scriptFid, &q.scriptEntry, &q.scriptIdx) == 3 &&
                  sscanf(d.c_str(), "cam:%d:%15[a-z]:%d", &q.donor, kind, &q.donorIdx) == 3 && pos && ref && pj && pos->items.size() == 3 &&
                  ref->items.size() == 3 && pj->type == 'n';
        if (ok) {
            q.donorEnemy = !strcmp(kind, "enemy");
            ok = q.donorEnemy || !strcmp(kind, "partner");
        }
        for (int k = 0; ok && k < 3; k++) {
            if (pos->items[k].second.type != 'n' || ref->items[k].second.type != 'n') ok = false;
            else q.c.pos[k] = (int16_t)pos->items[k].second.num, q.c.ref[k] = (int16_t)ref->items[k].second.num;
        }
        if (!ok) {
            skipped.push_back("preset " + std::to_string(n) + " is incomplete or malformed");
            continue;
        }
        q.c.proj = (int16_t)pj->num;
        q.name = str(&j, "name"), q.notes = str(&j, "notes"), q.status = str(&j, "status");
        if (q.status != PRESET_STATUS[0] && q.status != PRESET_STATUS[1] && q.status != PRESET_STATUS[2]) {
            skipped.push_back("preset " + std::to_string(n) + " has status \"" + q.status + "\": read as candidate");
            q.status = PRESET_STATUS[0];
        }
        const Json *ev = j.get("also_at_cuts");
        q.every = ev && ev->type == 'n' && ev->num != 0; /* a number: the reader has no booleans */
        if (str(tg, "context").size()) q.context = str(tg, "context");
        if (str(&j, "donor_context").size()) q.donorContext = str(&j, "donor_context");
        const Json *tf = j.get("transform"), *sc = tf ? tf->get("scale") : nullptr;
        if (sc && sc->type == 'n') q.fitK = sc->num;
        preset_check(q);
        out.push_back(q);
    }
    return true;
}

static std::string preset_text(const std::filesystem::path &path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), {});
}

/* Writes LIST over PATH only once what was written reads back as the same presets: a temp file beside it, then a rename */
static bool preset_save(const std::filesystem::path &path, const std::vector<Preset> &list, std::string &err) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    std::string text = preset_json(list);
    {
        std::ofstream f(tmp, std::ios::binary);
        f.write(text.data(), (std::streamsize)text.size());
        if (!f) return err = "can't write " + tmp.string(), false;
    }
    std::string back = preset_text(tmp);
    std::vector<Preset> check;
    std::vector<std::string> skipped;
    std::string perr;
    if (!preset_parse(back, check, perr, skipped) || check.size() != list.size() || !skipped.empty()) {
        std::filesystem::remove(tmp, ec);
        return err = "what was written did not read back" + (perr.empty() ? "" : ": " + perr) + "; " + path.string() + " is as it was", false;
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) return err = "can't replace " + path.string() + ": " + ec.message(), false;
    return true;
}

/* The presets to their file; ERROR holds why not, or is cleared */
static void preset_persist(const std::vector<Preset> &list, std::string &error) {
    std::string err;
    if (!preset_save(preset_path(), list, err)) error = err;
    else error.clear();
}

/* --test-presets: the file format's rules, on the disc's own fighters, in a temp folder. 0 when all hold */
static int preset_selftest() {
    int bad = 0;
    auto ok = [&](bool c, const char *what) { printf("%s  %s\n", c ? "ok  " : "FAIL", what), bad += !c; };
    const Fighter *t = nullptr, *d = nullptr;
    for (const Fighter &f : g_game.fighters)
        if (!f.enemy && f.cameras.size() >= 12 && !(t && d)) {
            std::vector<uint8_t> file;
            if (!read_file(fid_path(f.effectsFid), file)) continue;
            Archive a{file.data(), file.size()};
            if (f.effectsEntry < a.count() && a.entry(f.effectsEntry) && read_scripts(a.entry(f.effectsEntry), a.entry_size(f.effectsEntry)).size() > 6) (t ? d : t) = &f;
        }
    if (!t || !d) return printf("no two partners to test with\n"), 1;
    Preset p;
    p.name = "a \"quoted\"\nname", p.notes = "back\slash", p.status = "approved";
    p.fighter = t->id, p.scriptFid = t->effectsFid, p.scriptEntry = t->effectsEntry, p.scriptIdx = 5;
    p.donor = d->id, p.donorIdx = 9, p.c = d->cameras[9], p.c.pos[0] += 7, p.every = true;
    std::vector<Preset> got;
    std::vector<std::string> skipped;
    std::string err;
    ok(preset_parse(preset_json({p}), got, err, skipped) && got.size() == 1 && skipped.empty(), "writes and reads back");
    if (got.size() == 1) {
        const Preset &q = got[0];
        ok(!memcmp(q.c.pos, p.c.pos, 6) && !memcmp(q.c.ref, p.c.ref, 6) && q.c.proj == p.c.proj, "native values kept (the edit too, not the donor's)");
        ok(q.name == p.name && q.notes == p.notes && q.status == p.status && q.every && q.id() == p.id(), "name, notes, status, flag, id kept");
        ok(q.warn.empty(), "resolves against the disc");
    }
    got.clear();
    std::string v2 = preset_json({p});
    v2.replace(v2.find("\"schema_version\":1"), 18, "\"schema_version\":2");
    ok(!preset_parse(v2, got, err, skipped) && err.find("schema_version 2") != std::string::npos, "an unknown schema version is refused, naming it");
    Preset gone = p;
    gone.donor = 99999;
    got.clear();
    preset_parse(preset_json({gone}), got, err, skipped);
    ok(got.size() == 1 && !got[0].warn.empty() && got[0].donor == 99999, "a missing donor is reported, not replaced");
    Preset other = p;
    other.donorIdx = 10;
    ok(other.id() != p.id(), "another donor camera is another preset");
    other = p, other.scriptIdx = 6;
    ok(other.id() != p.id(), "another script is another preset");
    Preset mod = p;
    mod.context = "dw2003.bladegarurumon";
    got.clear();
    preset_parse(preset_json({mod}), got, err, skipped);
    ok(got.size() == 1 && got[0].context == mod.context && got[0].id() != p.id(), "a mod's context is kept and makes another preset");
    ok(got.size() == 1 && !got[0].warn.empty() && got[0].warn[0].find("context") != std::string::npos, "a preset of a mod that is not loaded says which context it needs");
    Preset fit = p;
    fit.fitK = 1.25;
    got.clear();
    preset_parse(preset_json({fit}), got, err, skipped);
    ok(got.size() == 1 && fabs(got[0].fitK - 1.25) < 1e-6, "the fit a preset started from is kept");
    {
        GameCamera out;
        double k = 0;
        const GameCamera &dc = d->cameras[9];
        ok(fit_camera(*d, dc, *d, out, k) && fabs(k - 1) < 1e-6 && abs(out.pos[0] - dc.pos[0]) <= 1 && abs(out.pos[1] - dc.pos[1]) <= 1 && abs(out.pos[2] - dc.pos[2]) <= 1 &&
               abs(out.ref[0] - dc.ref[0]) <= 1 && abs(out.ref[1] - dc.ref[1]) <= 1 && abs(out.ref[2] - dc.ref[2]) <= 1,
           "fitting a camera to its own model changes nothing");
        ok(fit_camera(*d, dc, *t, out, k) && k > 0 && out.proj == dc.proj, "fitting to another model keeps the projection");
        /* the box's center is where it was on the screen: x/z and y/z of the center from the eye, in the camera's own axes */
        auto center_on_screen = [](const Fighter &f, const GameCamera &c, float &sx, float &sy) {
            float lo[3], hi[3], e[3], r[3], b1[3] = {(float)c.pos[0], (float)c.pos[1], (float)c.pos[2]}, b2[3] = {(float)c.ref[0], (float)c.ref[1], (float)c.ref[2]};
            fighter_box(f, lo, hi);
            battle_to_view(f, b1, e), battle_to_view(f, b2, r);
            float m[3] = {(lo[0] + hi[0]) / 2, -(lo[1] + hi[1]) / 2, -(lo[2] + hi[2]) / 2};
            float fw[3] = {r[0] - e[0], r[1] - e[1], r[2] - e[2]}, l = sqrtf(fw[0] * fw[0] + fw[1] * fw[1] + fw[2] * fw[2]);
            for (float &x : fw) x /= l;
            float rt[3] = {fw[2], 0, -fw[0]}, rl = sqrtf(rt[0] * rt[0] + rt[2] * rt[2]); /* up x forward, for a world up of +y */
            for (float &x : rt) x /= rl;
            float up[3] = {rt[1] * fw[2] - rt[2] * fw[1], rt[2] * fw[0] - rt[0] * fw[2], rt[0] * fw[1] - rt[1] * fw[0]};
            float v[3] = {m[0] - e[0], m[1] - e[1], m[2] - e[2]};
            float z = v[0] * fw[0] + v[1] * fw[1] + v[2] * fw[2];
            sx = (v[0] * rt[0] + v[1] * rt[1] + v[2] * rt[2]) / z, sy = (v[0] * up[0] + v[1] * up[1] + v[2] * up[2]) / z;
        };
        for (const Fighter &e : g_game.fighters) { /* an enemy's side has its own sign: its own model, too, comes back */
            float lo[3], hi[3];
            if (!e.enemy || e.cameras.empty() || !fighter_box(e, lo, hi)) continue;
            GameCamera eo;
            ok(fit_camera(e, e.cameras[0], e, eo, k) && abs(eo.pos[0] - e.cameras[0].pos[0]) <= 1 && abs(eo.pos[1] - e.cameras[0].pos[1]) <= 1 &&
                   abs(eo.pos[2] - e.cameras[0].pos[2]) <= 1 && abs(eo.ref[2] - e.cameras[0].ref[2]) <= 1,
               "the same for an enemy");
            break;
        }
        float ax, ay, bx, by;
        center_on_screen(*d, dc, ax, ay);
        center_on_screen(*t, out, bx, by);
        ok(fabs(ax - bx) < 0.02f && fabs(ay - by) < 0.02f, "the model's box center lands where the donor's did");
    }
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "dw3mv_preset_test", file = dir / "presets.json";
    std::filesystem::remove_all(dir);
    ok(preset_save(file, {p}, err), "saves");
    std::string first = preset_text(file);
    Preset p2 = p;
    p2.status = "rejected";
    ok(preset_save(file, {p2}, err) && preset_text(file) != first && !std::filesystem::exists(dir / "presets.json.tmp"), "replaces the file and leaves no temp");
    ok(preset_text(file) == preset_json({p2}), "the file is the json, byte for byte");
    std::filesystem::remove_all(dir);
    printf("%s\n", bad ? "FAILED" : "all hold");
    return bad != 0;
}
