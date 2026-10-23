/* Choreography templates: data/compendium/applications.json. A template is a script's camera commands in order; it is never stored on
   its own, it is read from the donor script. What is stored is an application: a template put on a target fighter's script, with
   the donor's commands as they were (a snapshot, so a donor that is gone still loads), the map from each of them to a command of the
   target's script, a Fit and per-shot overrides on top of it. Three things are kept apart: the donor's original (SNAP), the adapted
   sequence (Fit, then overrides) and the effective camera of each command of the target's script (app_effective). Nothing here writes
   a script or a game file. References are the compendium's ids; one that doesn't resolve is reported, never replaced by a guess. */

static const int APP_SCHEMA = 1;

/* One camera command of a script. KIND: 1 a fighter's shot by number (CAMID), 2 explicit (W, the 14 words), 3 the battle's default view */
struct TplShot {
    int line = 0, kind = 0, camId = 0, time = 0, w[14] = {};
};

/* A donor's command, and what the application does with it */
struct AppShot {
    TplShot s;           /* the donor's command, as it was */
    int to = -1;         /* the command of the target's script it goes to (0-based among its camera commands), or -1 */
    bool enabled = true; /* off: the target's command keeps its own camera */
    bool has = false;    /* an override: explicit camera OW, over OTIME frames (-1: the donor's) */
    int ow[14] = {}, otime = -1;
};

struct Application {
    std::string name, status = "candidate", notes;
    std::string variant; /* empty, or a word that keeps several applications of one donor script on one target script apart */
    int fighter = 0, scriptFid = 0, scriptEntry = 0, scriptIdx = 0;     /* the target */
    int donor = 0, donorFid = 0, donorEntry = 0, donorIdx = 0;          /* the donor fighter and script */
    std::string context = "base_game", donorContext = "base_game";
    double fitK = 0; /* 0 no Fit; else the scale it had when saved (the Fit itself is computed from the models) */
    std::vector<AppShot> shots;
    std::vector<std::string> warn; /* what doesn't resolve here (not written) */

    std::string fighter_ref() const { return "fighter:" + std::to_string(fighter); }
    std::string script_ref() const { return comp::script_id(scriptFid, scriptEntry, scriptIdx); }
    std::string donor_ref() const { return comp::script_id(donorFid, donorEntry, donorIdx); }
    std::string id() const {
        std::string c = (context == "base_game" ? "" : context + "|") + (donorContext == "base_game" ? "" : "donor@" + donorContext + "|");
        return "application:" + c + fighter_ref() + "|" + script_ref() + "|" + donor_ref() + (variant.empty() ? "" : "|v:" + variant);
    }
};

static std::filesystem::path app_path() { return std::filesystem::path(g_data).parent_path() / "compendium" / "applications.json"; }

/* A fighter's scripts (its effects archive's table), read from the data */
static std::vector<std::vector<ScriptLine>> fighter_scripts(const Fighter &f) {
    std::vector<uint8_t> file;
    if (!read_file(fid_path(f.effectsFid), file)) return {};
    Archive a{file.data(), file.size()};
    if (f.effectsEntry >= a.count() || !a.entry(f.effectsEntry)) return {};
    return read_scripts(a.entry(f.effectsEntry), a.entry_size(f.effectsEntry));
}

/* The camera commands of a script in order: the template */
static std::vector<TplShot> script_cams(const std::vector<ScriptLine> &sc) {
    std::vector<TplShot> out;
    for (size_t n = 0; n < sc.size(); n++) {
        const ScriptLine &l = sc[n];
        if (!l.camMode) continue;
        TplShot s;
        s.line = (int)n, s.kind = l.camMode, s.camId = l.camId, s.time = l.camTime;
        if (l.camMode == 2) memcpy(s.w, l.camRaw, sizeof s.w);
        out.push_back(s);
    }
    return out;
}

/* Fit for a sequence: the same similarity as "Fit to this model" (centers match, distances scale by K, the ratio of the boxes'
   diagonals), as an affine map of the battle's words, x -> K x + O: for two fighters of one side the flips of battle_to_view and
   view_to_battle cancel. A word triple (eye, target or translation) P of a camera in the world is R p + t; the map moves the world
   point, so p -> K p, t -> K t + O (R, the rotation, and the roll are carried as they are). With no rotation the offset goes into
   the eye and the target instead, so the translation words stay a scale of the donor's. */
struct FitFrame {
    double k = 1;
    float o[3] = {};
    bool ok = false;
};
static FitFrame fit_frame(const Fighter &donor, const Fighter &target) {
    FitFrame ff;
    float dl[3], dh[3], tl[3], th[3];
    if (donor.enemy != target.enemy || !fighter_box(donor, dl, dh) || !fighter_box(target, tl, th)) return ff;
    auto diag = [](const float *l, const float *h) { return sqrtf((h[0] - l[0]) * (h[0] - l[0]) + (h[1] - l[1]) * (h[1] - l[1]) + (h[2] - l[2]) * (h[2] - l[2])); };
    float dd = diag(dl, dh), td = diag(tl, th);
    if (dd < 1 || td < 1) return ff;
    ff.k = td / dd;
    float cd[3] = {(dl[0] + dh[0]) / 2, -(dl[1] + dh[1]) / 2, -(dl[2] + dh[2]) / 2}, ct[3] = {(tl[0] + th[0]) / 2, -(tl[1] + th[1]) / 2, -(tl[2] + th[2]) / 2};
    float zero[3] = {0, 0, 0}, v[3];
    battle_to_view(donor, zero, v);
    for (int i = 0; i < 3; i++) v[i] = ct[i] + (float)ff.k * (v[i] - cd[i]);
    view_to_battle(target, v, ff.o);
    ff.ok = true;
    return ff;
}
static int clamp16(double x) { return (int)std::clamp(lround(x), -32768L, 32767L); }
static void fit_words(const FitFrame &ff, const int in[14], int out[14]) {
    memcpy(out, in, 14 * sizeof(int));
    bool rotated = in[9] || in[10] || in[11];
    for (int i = 0; i < 3; i++) {
        double off = rotated ? 0 : ff.o[i];
        out[i] = clamp16(ff.k * in[i] + off), out[3 + i] = clamp16(ff.k * in[3 + i] + off);
        out[6 + i] = clamp16(ff.k * in[6 + i] + (rotated ? ff.o[i] : 0));
    }
}

/* What the target's script does at each of its camera commands with the application on. SOURCE: "script" (the target's own),
   "template" (the donor's, as it was), "fit" (the donor's, fitted) or "override" */
struct EffShot {
    int ordinal = 0, line = 0, kind = 0, camId = 0, time = 0, w[14] = {};
    const char *source = "script";
};

/* The effective sequence of A on TARGET (the fighter A names), from the target's script as the data has it now; NOTES say what was left
   as the script has it and why. FF is the Fit between the two models, used when A has one */
static std::vector<EffShot> app_effective(const Application &a, const Fighter &target, const FitFrame &ff, std::vector<std::string> &notes) {
    std::vector<EffShot> out;
    std::vector<std::vector<ScriptLine>> scripts = fighter_scripts(target);
    if (a.scriptIdx < 0 || a.scriptIdx >= (int)scripts.size()) return notes.push_back(a.script_ref() + " does not exist"), out;
    std::vector<TplShot> cmds = script_cams(scripts[a.scriptIdx]);
    for (size_t j = 0; j < cmds.size(); j++) {
        EffShot e;
        e.ordinal = (int)j, e.line = cmds[j].line, e.kind = cmds[j].kind, e.camId = cmds[j].camId, e.time = cmds[j].time;
        memcpy(e.w, cmds[j].w, sizeof e.w);
        out.push_back(e);
    }
    for (size_t k = 0; k < a.shots.size(); k++) {
        const AppShot &s = a.shots[k];
        std::string who = "donor shot " + std::to_string(k + 1);
        if (s.to < 0 || s.to >= (int)out.size()) {
            notes.push_back(who + " goes nowhere: the script has " + std::to_string(out.size()) + " camera commands");
            continue;
        }
        if (!s.enabled) {
            notes.push_back(who + " is off: command " + std::to_string(s.to + 1) + " keeps the script's camera");
            continue;
        }
        EffShot &e = out[s.to];
        e.time = s.has && s.otime >= 0 ? s.otime : s.s.time;
        if (s.has) {
            e.kind = 2, e.source = "override";
            memcpy(e.w, s.ow, sizeof e.w);
        } else if (s.s.kind == 2) {
            e.kind = 2, e.source = a.fitK > 0 && ff.ok ? "fit" : "template";
            if (a.fitK > 0 && ff.ok) fit_words(ff, s.s.w, e.w);
            else memcpy(e.w, s.s.w, sizeof e.w);
        } else if (s.s.kind == 1) {
            int idx = s.s.camId <= 4 ? (target.enemy ? -1 : s.s.camId + 7) : (target.enemy ? s.s.camId - 5 : -1);
            if (idx < 0 || idx >= (int)target.cameras.size()) {
                notes.push_back(who + " is a shot of the other side: command " + std::to_string(s.to + 1) + " keeps the script's camera");
                e.time = cmds[s.to].time;
                continue;
            }
            e.kind = 1, e.camId = s.s.camId, e.source = "template";
            cam_words(target.cameras[idx], e.w);
        } else {
            e.kind = 3, e.camId = 0, e.source = "template";
        }
    }
    return out;
}

/* What of A doesn't resolve against the disc's data here */
static void app_check(Application &a) {
    a.warn.clear();
    for (int i = 0; i < 2; i++) {
        const std::string &c = i ? a.donorContext : a.context;
        if (c != "base_game" && c != g_context) a.warn.push_back(std::string(i ? "its donor" : "its target") + " is in the context " + c + ", and the one loaded is " + g_context);
    }
    if (!a.warn.empty()) return;
    const Fighter *t = fighter_by_id(a.fighter, false), *d = fighter_by_id(a.donor, false);
    if (!t) a.warn.push_back(a.fighter_ref() + " is not a fighter of this disc");
    if (!d) a.warn.push_back("donor fighter " + std::to_string(a.donor) + " is not a fighter of this disc");
    if (t && d && t->enemy != d->enemy) a.warn.push_back("donor and target are not on the same side");
    if (t) {
        if (t->effectsFid != a.scriptFid || t->effectsEntry != a.scriptEntry) a.warn.push_back(a.script_ref() + " is not in " + t->name + "'s scripts");
        else if (a.scriptIdx < 0 || a.scriptIdx >= (int)fighter_scripts(*t).size()) a.warn.push_back(a.script_ref() + " does not exist");
    }
    if (d) {
        std::vector<std::vector<ScriptLine>> sc = fighter_scripts(*d);
        if (d->effectsFid != a.donorFid || d->effectsEntry != a.donorEntry || a.donorIdx < 0 || a.donorIdx >= (int)sc.size())
            a.warn.push_back("the donor script " + a.donor_ref() + " is not there: the snapshot of its shots is used");
        else {
            std::vector<TplShot> now = script_cams(sc[a.donorIdx]);
            bool same = now.size() == a.shots.size();
            for (size_t k = 0; same && k < now.size(); k++)
                same = now[k].kind == a.shots[k].s.kind && now[k].time == a.shots[k].s.time && now[k].camId == a.shots[k].s.camId && !memcmp(now[k].w, a.shots[k].s.w, sizeof now[k].w);
            if (!same) a.warn.push_back("the donor script has changed since this was saved: the snapshot of its shots is used");
        }
    }
}

/* A new application from the donor's script, mapped by order, with a Fit when asked */
static Application app_new(const Fighter &donor, int donorIdx, const Fighter &target, int targetIdx, bool fit) {
    Application a;
    a.fighter = target.id, a.scriptFid = target.effectsFid, a.scriptEntry = target.effectsEntry, a.scriptIdx = targetIdx;
    a.donor = donor.id, a.donorFid = donor.effectsFid, a.donorEntry = donor.effectsEntry, a.donorIdx = donorIdx;
    a.context = a.donorContext = g_context;
    std::vector<std::vector<ScriptLine>> ds = fighter_scripts(donor);
    if (donorIdx >= 0 && donorIdx < (int)ds.size())
        for (const TplShot &s : script_cams(ds[donorIdx])) {
            AppShot x;
            x.s = s, x.to = (int)a.shots.size();
            a.shots.push_back(x);
        }
    if (fit) {
        FitFrame ff = fit_frame(donor, target);
        if (ff.ok) a.fitK = ff.k;
    }
    return a;
}

static const char *const APP_KIND[4] = {"", "shot", "explicit", "default_view"};

static std::string app_json(const std::vector<Application> &list) {
    std::vector<Application> sorted = list;
    std::sort(sorted.begin(), sorted.end(), [](const Application &a, const Application &b) { return a.id() < b.id(); });
    std::vector<std::string> recs;
    for (const Application &a : sorted) {
        std::vector<std::string> shots;
        for (size_t k = 0; k < a.shots.size(); k++) {
            const AppShot &s = a.shots[k];
            comp::Obj o;
            o.num("index", (long long)k).num("line", s.s.line).str("kind", APP_KIND[s.s.kind]).num("script_camera", s.s.camId).num("over", s.s.time);
            if (s.s.kind == 2) o.raw("words", comp::ints(s.s.w, 14));
            o.num("map_to", s.to).num("enabled", s.enabled ? 1 : 0);
            if (s.has) o.raw("override", comp::Obj().raw("words", comp::ints(s.ow, 14)).num("over", s.otime).done());
            shots.push_back(o.done());
        }
        comp::Obj o;
        o.str("id", a.id()).str("name", a.name).str("variant", a.variant).str("status", a.status).str("notes", a.notes)
            .raw("target", comp::Obj().str("fighter", a.fighter_ref()).str("script", a.script_ref()).str("context", a.context).done())
            .raw("donor", comp::Obj().str("fighter", "fighter:" + std::to_string(a.donor)).str("script", a.donor_ref()).str("context", a.donorContext).done());
        if (a.fitK > 0) o.raw("fit", comp::Obj().str("kind", "fit_to_model").flt("scale", a.fitK).str("provenance", "derived").done());
        o.raw("shots", comp::lines(shots));
        recs.push_back(o.done());
    }
    return "{\n\"schema_version\":" + std::to_string(APP_SCHEMA) + ",\n\"game\":" +
           comp::Obj().str("title", "Digimon World 2003").str("edition", "PAL SLES-03936").done() + ",\n\"applications\":" + comp::lines(recs) + "\n}\n";
}

/* Reads an applications file. False with ERR set when it is not one this viewer can read at all; an application in it that can't be
   read is skipped and named in SKIPPED */
static bool app_parse(const std::string &text, std::vector<Application> &out, std::string &err, std::vector<std::string> &skipped) {
    const char *p = text.c_str();
    Json root;
    if (!json_parse(p, root) || root.type != '{') return err = "not a JSON object", false;
    const Json *v = root.get("schema_version");
    if (!v || v->type != 'n') return err = "no schema_version: not an applications file", false;
    if ((int)v->num != APP_SCHEMA) return err = "schema_version " + std::to_string((int)v->num) + " is not one this viewer reads (it reads " + std::to_string(APP_SCHEMA) + ")", false;
    const Json *list = root.get("applications");
    if (!list || list->type != '[') return err = "no applications list", false;
    auto str = [&](const Json *o, const char *k) { const Json *x = o ? o->get(k) : nullptr; return x && x->type == 's' ? x->str : std::string(); };
    auto num = [&](const Json *o, const char *k, double def) { const Json *x = o ? o->get(k) : nullptr; return x && x->type == 'n' ? x->num : def; };
    auto words = [&](const Json *o, int w[14]) {
        if (!o || o->type != '[' || o->items.size() != 14) return false;
        for (int i = 0; i < 14; i++) {
            if (o->items[i].second.type != 'n') return false;
            w[i] = (int)o->items[i].second.num;
        }
        return true;
    };
    int n = 0;
    for (const auto &it : list->items) {
        n++;
        const Json &j = it.second;
        Application q;
        const Json *tg = j.get("target"), *dn = j.get("donor"), *sh = j.get("shots");
        int dfighter = 0;
        bool ok = j.type == '{' && sscanf(str(tg, "fighter").c_str(), "fighter:%d", &q.fighter) == 1 &&
                  sscanf(str(tg, "script").c_str(), "script:%d.%d:%d", &q.scriptFid, &q.scriptEntry, &q.scriptIdx) == 3 &&
                  sscanf(str(dn, "fighter").c_str(), "fighter:%d", &dfighter) == 1 &&
                  sscanf(str(dn, "script").c_str(), "script:%d.%d:%d", &q.donorFid, &q.donorEntry, &q.donorIdx) == 3 && sh && sh->type == '[';
        q.donor = dfighter;
        for (size_t k = 0; ok && k < sh->items.size(); k++) {
            const Json &sj = sh->items[k].second;
            AppShot s;
            std::string kind = str(&sj, "kind");
            for (int c = 1; c < 4; c++)
                if (kind == APP_KIND[c]) s.s.kind = c;
            s.s.line = (int)num(&sj, "line", 0), s.s.camId = (int)num(&sj, "script_camera", 0), s.s.time = (int)num(&sj, "over", 0);
            s.to = (int)num(&sj, "map_to", -1), s.enabled = num(&sj, "enabled", 1) != 0;
            if (!s.s.kind || (s.s.kind == 2 && !words(sj.get("words"), s.s.w))) ok = false;
            if (const Json *ov = sj.get("override")) {
                if (!words(ov->get("words"), s.ow)) ok = false;
                s.has = true, s.otime = (int)num(ov, "over", -1);
            }
            q.shots.push_back(s);
        }
        if (!ok) {
            skipped.push_back("application " + std::to_string(n) + " is incomplete or malformed");
            continue;
        }
        q.name = str(&j, "name"), q.notes = str(&j, "notes"), q.status = str(&j, "status"), q.variant = str(&j, "variant");
        if (q.status != PRESET_STATUS[0] && q.status != PRESET_STATUS[1] && q.status != PRESET_STATUS[2]) {
            skipped.push_back("application " + std::to_string(n) + " has status \"" + q.status + "\": read as candidate");
            q.status = PRESET_STATUS[0];
        }
        if (str(tg, "context").size()) q.context = str(tg, "context");
        if (str(dn, "context").size()) q.donorContext = str(dn, "context");
        q.fitK = num(j.get("fit"), "scale", 0);
        app_check(q);
        out.push_back(q);
    }
    return true;
}

static bool app_save(const std::filesystem::path &path, const std::vector<Application> &list, std::string &err) {
    return save_atomic(path, app_json(list), [&](const std::string &back, std::string &perr) {
        std::vector<Application> check;
        std::vector<std::string> skipped;
        return app_parse(back, check, perr, skipped) && check.size() == list.size() && skipped.empty();
    }, err);
}

/* The applications to their file; ERROR holds why not, or is cleared */
static void app_persist(const std::vector<Application> &list, std::string &error) {
    std::string err;
    if (!app_save(app_path(), list, err)) error = err;
    else error.clear();
}

static std::string words_text(const int w[14]) {
    char b[160];
    snprintf(b, sizeof b, "eye %d,%d,%d target %d,%d,%d", w[0], w[1], w[2], w[3], w[4], w[5]);
    std::string s = b;
    if (w[6] || w[7] || w[8]) s += " translation " + std::to_string(w[6]) + "," + std::to_string(w[7]) + "," + std::to_string(w[8]);
    if (w[9] || w[10] || w[11]) s += " rotation " + std::to_string(w[9]) + "," + std::to_string(w[10]) + "," + std::to_string(w[11]);
    if (w[12]) s += " roll " + std::to_string(w[12]);
    return s + " proj " + std::to_string(w[13]);
}

static const Fighter *partner_named(const std::string &name) {
    for (const Fighter &f : g_game.fighters)
        if (f.name == name && !f.enemy) return &f;
    return nullptr;
}

/* Where a fighter's box at rest (LO, HI from fighter_box) falls on the screen through the camera of words W, in the 240-line screen's
   pixels from its center: the box's center, its size, and the part of its corners inside 320 x 240. The roll is not drawn (as in the
   viewer). OK is false when a corner is behind the camera */
struct Framing {
    bool ok = false;
    float cx = 0, cy = 0, w = 0, h = 0, inside = 0;
    float mx = 0, my = 0; /* where the box's own center falls */
};
static Framing frame_of(const Fighter &f, const int w[14], const float lo[3], const float hi[3]) {
    Framing r;
    float eB[3], rB[3], eye[3], ref[3];
    cam_eff(w, eB, rB);
    battle_to_view(f, eB, eye), battle_to_view(f, rB, ref);
    auto sub = [](const float *a, const float *b, float *o) { for (int i = 0; i < 3; i++) o[i] = a[i] - b[i]; };
    auto dot = [](const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    auto cross = [](const float *a, const float *b, float *o) { o[0] = a[1] * b[2] - a[2] * b[1], o[1] = a[2] * b[0] - a[0] * b[2], o[2] = a[0] * b[1] - a[1] * b[0]; };
    auto norm = [&](float *a) { float l = sqrtf(dot(a, a)); for (int i = 0; i < 3; i++) a[i] /= l > 0 ? l : 1; };
    float fwd[3], up[3] = {0, 1, 0}, right[3], up2[3];
    sub(ref, eye, fwd), norm(fwd), cross(fwd, up, right), norm(right), cross(right, fwd, up2);
    float proj = (float)w[13], xmin = 1e9f, xmax = -1e9f, ymin = 1e9f, ymax = -1e9f;
    int in = 0;
    for (int c = 0; c < 8; c++) {
        /* a model's y and z are the PSX's: the viewer's space has them flipped */
        float p[3] = {c & 1 ? hi[0] : lo[0], -(c & 2 ? hi[1] : lo[1]), -(c & 4 ? hi[2] : lo[2])}, d[3];
        sub(p, eye, d);
        float z = dot(d, fwd);
        if (z <= 1) return r;
        float x = proj * dot(d, right) / z, y = proj * dot(d, up2) / z;
        xmin = std::min(xmin, x), xmax = std::max(xmax, x), ymin = std::min(ymin, y), ymax = std::max(ymax, y);
        in += fabsf(x) <= 160 && fabsf(y) <= 120;
    }
    r.ok = true, r.cx = (xmin + xmax) / 2, r.cy = (ymin + ymax) / 2, r.w = xmax - xmin, r.h = ymax - ymin, r.inside = in / 8.0f;
    float m[3] = {(lo[0] + hi[0]) / 2, -(lo[1] + hi[1]) / 2, -(lo[2] + hi[2]) / 2}, d[3];
    sub(m, eye, d);
    float z = dot(d, fwd);
    if (z > 1) r.mx = proj * dot(d, right) / z, r.my = proj * dot(d, up2) / z;
    else r.ok = false;
    return r;
}

/* --check-framing DONOR DSCRIPT TARGET TSCRIPT: for each explicit shot the application puts on the target, where the donor's model
   falls on the screen through the donor's camera and where the target's falls through the effective one. A Fit is a similarity, so
   the center must agree; the size agrees as far as the two boxes have the same proportions. */
static int check_framing_cli(const std::string &donor, int dIdx, const std::string &target, int tIdx) {
    const Fighter *d = partner_named(donor), *t = partner_named(target);
    if (!d || !t) return printf("--check-framing: a partner of that name doesn't exist\n"), 1;
    float dl[3], dh[3], tl[3], th[3];
    if (!fighter_box(*d, dl, dh) || !fighter_box(*t, tl, th)) return printf("--check-framing: a model's box couldn't be read\n"), 1;
    Application a = app_new(*d, dIdx, *t, tIdx, true);
    FitFrame ff = fit_frame(*d, *t);
    std::vector<std::string> notes;
    std::vector<EffShot> eff = app_effective(a, *t, ff, notes);
    printf("%s box %.0f x %.0f x %.0f, %s box %.0f x %.0f x %.0f, scale %.3f. Pixels from the center of a 320 x 240 screen.\n", d->name.c_str(), dh[0] - dl[0], dh[1] - dl[1],
           dh[2] - dl[2], t->name.c_str(), th[0] - tl[0], th[1] - tl[1], th[2] - tl[2], ff.k);
    for (size_t k = 0; k < a.shots.size() && k < eff.size(); k++) {
        if (a.shots[k].s.kind != 2 || a.shots[k].to < 0 || a.shots[k].to >= (int)eff.size()) continue;
        const EffShot &e = eff[a.shots[k].to];
        Framing fd = frame_of(*d, a.shots[k].s.w, dl, dh), ft = frame_of(*t, e.w, tl, th);
        printf("  shot %zu (command %d, %s): donor  centre %6.1f,%6.1f  size %6.1f x %6.1f  inside %.0f%%%s\n", k + 1, a.shots[k].to + 1, e.source, fd.cx, fd.cy, fd.w, fd.h, fd.inside * 100, fd.ok ? "" : "  (box behind the camera)");
        printf("%*s target centre %6.1f,%6.1f  size %6.1f x %6.1f  inside %.0f%%%s\n", 40, "", ft.cx, ft.cy, ft.w, ft.h, ft.inside * 100, ft.ok ? "" : "  (box behind the camera)");
    }
    return 0;
}

/* --apply-template DONOR DSCRIPT TARGET TSCRIPT|?: DONOR's script's camera sequence on TARGET's script, printed; "?" lists the target's
   scripts that have camera commands */
static int apply_template_cli(const std::string &donor, int dIdx, const std::string &target, const std::string &tArg) {
    const Fighter *d = partner_named(donor), *t = partner_named(target);
    if (!d || !t) return printf("--apply-template: a partner of that name doesn't exist\n"), 1;
    if (tArg == "?") {
        std::vector<std::vector<ScriptLine>> sc = fighter_scripts(*t);
        for (size_t i = 0; i < sc.size(); i++) {
            std::vector<TplShot> c = script_cams(sc[i]);
            int ex = 0;
            for (const TplShot &s : c) ex += s.kind == 2;
            if (!c.empty()) printf("%s script %zu: %zu camera commands (%d explicit)\n", t->name.c_str(), i, c.size(), ex);
        }
        return 0;
    }
    int tIdx = atoi(tArg.c_str());
    Application a = app_new(*d, dIdx, *t, tIdx, true);
    app_check(a);
    printf("%s script %d -> %s script %d (%s): %zu donor shots, scale %.3f\n", d->name.c_str(), dIdx, t->name.c_str(), tIdx, a.script_ref().c_str(), a.shots.size(), a.fitK);
    for (const std::string &w : a.warn) printf("  warning: %s\n", w.c_str());
    for (size_t k = 0; k < a.shots.size(); k++) {
        const AppShot &s = a.shots[k];
        printf("  donor %zu (line %d, %s, over %d)%s -> command %d\n", k + 1, s.s.line, APP_KIND[s.s.kind], s.s.time, s.s.kind == 2 ? (" " + words_text(s.s.w)).c_str() : "", s.to + 1);
    }
    std::vector<std::string> notes;
    FitFrame ff = fit_frame(*d, *t);
    std::vector<EffShot> eff = app_effective(a, *t, ff, notes);
    for (const EffShot &e : eff) {
        printf("  command %d (line %d): %s, over %d, %s%s\n", e.ordinal + 1, e.line, e.source, e.time, APP_KIND[e.kind], e.kind == 2 ? (" " + words_text(e.w)).c_str() : "");
    }
    for (const std::string &n : notes) printf("  note: %s\n", n.c_str());
    return 0;
}

/* --test-templates: the application's rules on the disc's own fighters. 0 when all hold */
static int templates_selftest() {
    int bad = 0;
    auto ok = [&](bool c, const char *what) { printf("%s  %s\n", c ? "ok  " : "FAIL", what), bad += !c; };
    /* two partners, each with a script of 3 or more camera commands, one of them explicit */
    const Fighter *d = nullptr, *t = nullptr;
    int di = -1, ti = -1;
    for (const Fighter &f : g_game.fighters) {
        if (f.enemy || (d && t)) continue;
        std::vector<std::vector<ScriptLine>> sc = fighter_scripts(f);
        for (size_t i = 0; i < sc.size(); i++) {
            std::vector<TplShot> c = script_cams(sc[i]);
            int ex = 0;
            for (const TplShot &s : c) ex += s.kind == 2 && (s.w[6] || s.w[7] || s.w[8]);
            if (!d && c.size() >= 4 && ex >= 1) { d = &f, di = (int)i; break; }
            if (d && !t && &f != d && c.size() >= 3) { t = &f, ti = (int)i; break; }
        }
    }
    if (!d || !t) return printf("no two partners to test with\n"), 1;
    Application a = app_new(*d, di, *t, ti, true);
    app_check(a);
    ok(a.shots.size() >= 4 && a.warn.empty() && a.fitK > 0, "an application from a donor script resolves and has a Fit");
    bool order = true;
    for (size_t k = 0; k < a.shots.size(); k++) order = order && a.shots[k].to == (int)k;
    ok(order, "the map is by order");
    std::vector<Application> got;
    std::vector<std::string> skipped;
    std::string err;
    a.name = "a \"quoted\"\nname", a.notes = "back\\slash", a.status = "approved";
    a.shots[1].enabled = false;
    a.shots[2].has = true, a.shots[2].otime = 17;
    for (int i = 0; i < 14; i++) a.shots[2].ow[i] = 100 * i - 300;
    ok(app_parse(app_json({a}), got, err, skipped) && got.size() == 1 && skipped.empty() && got[0].warn.empty(), "writes and reads back, and resolves");
    ok(got.size() == 1 && app_json(got) == app_json({a}), "the file is the same bytes when read and written again");
    ok(got.size() == 1 && got[0].name == a.name && got[0].notes == a.notes && got[0].status == "approved" && !got[0].shots[1].enabled && got[0].shots[2].has &&
           got[0].shots[2].otime == 17 && !memcmp(got[0].shots[2].ow, a.shots[2].ow, sizeof a.shots[2].ow) && got[0].id() == a.id(),
       "name, notes, status, an off shot and an override are kept");
    /* the effective sequence */
    std::vector<std::vector<ScriptLine>> tsBefore = fighter_scripts(*t);
    FitFrame ff = fit_frame(*d, *t);
    std::vector<std::string> notes;
    std::vector<EffShot> eff = app_effective(a, *t, ff, notes);
    std::vector<TplShot> tc = script_cams(tsBefore[ti]);
    ok(eff.size() == tc.size() && ff.ok, "one effective camera for each command of the target's script");
    bool offOk = eff.size() > 2 && !strcmp(eff[1].source, "script") && eff[1].kind == tc[1].kind && !memcmp(eff[1].w, tc[1].w, sizeof tc[1].w);
    ok(offOk, "an off shot leaves the target's own command as it is");
    ok(eff.size() > 2 && !strcmp(eff[2].source, "override") && eff[2].time == 17 && !memcmp(eff[2].w, a.shots[2].ow, sizeof a.shots[2].ow), "an override replaces the shot, with its own time");
    bool fitOk = true;
    for (size_t j = 0; j < eff.size() && j < a.shots.size(); j++)
        if (a.shots[j].enabled && !a.shots[j].has && a.shots[j].s.kind == 2) fitOk = fitOk && !strcmp(eff[j].source, "fit") && eff[j].time == a.shots[j].s.time;
    ok(fitOk, "a fitted shot keeps the donor's over");
    /* Fit moves the world as the similarity does, and carries rotation and roll as they are */
    bool world = true, carried = true, anyRot = false;
    auto M = [&](const float *x, float *o) { for (int i = 0; i < 3; i++) o[i] = (float)ff.k * x[i] + ff.o[i]; };
    for (const AppShot &s : a.shots) {
        if (s.s.kind != 2) continue;
        int w2[14];
        fit_words(ff, s.s.w, w2);
        float e1[3], r1[3], e2[3], r2[3], me[3], mr[3];
        cam_eff(s.s.w, e1, r1), cam_eff(w2, e2, r2);
        M(e1, me), M(r1, mr);
        for (int i = 0; i < 3; i++) world = world && fabsf(e2[i] - me[i]) < 3 && fabsf(r2[i] - mr[i]) < 3;
        carried = carried && w2[9] == s.s.w[9] && w2[10] == s.s.w[10] && w2[11] == s.s.w[11] && w2[12] == s.s.w[12] && w2[13] == s.s.w[13];
        anyRot |= s.s.w[9] || s.s.w[10] || s.s.w[11];
    }
    ok(world, "the fitted camera sees the world where the similarity puts the donor's");
    {
        /* on the screen: the model's center falls where the donor's did, and the size follows the boxes' proportions */
        float dl[3], dh[3], tl[3], th[3];
        bool same = fighter_box(*d, dl, dh) && fighter_box(*t, tl, th), any = false;
        for (size_t k = 0; same && k < a.shots.size() && k < eff.size(); k++) {
            if (a.shots[k].s.kind != 2 || !a.shots[k].enabled || a.shots[k].has) continue;
            Framing fd = frame_of(*d, a.shots[k].s.w, dl, dh), ft = frame_of(*t, eff[a.shots[k].to].w, tl, th);
            if (!fd.ok || !ft.ok) continue; /* a shot that doesn't see the model (a cut to somewhere else) */
            any = true;
            same = same && fabsf(fd.mx - ft.mx) < 4 && fabsf(fd.my - ft.my) < 4;
        }
        ok(same && any, "on the screen the model's center falls where the donor's did");
    }
    ok(carried, "rotation, roll and projection are carried as they are");
    /* a synthetic rotated camera, since few real ones are */
    {
        int w[14] = {-4000, 300, -9000, 0, 0, 100, 500, 2000, 6000, 0, -182, 0, 7, 280}, w2[14];
        fit_words(ff, w, w2);
        float e1[3], r1[3], e2[3], r2[3], me[3], mr[3];
        cam_eff(w, e1, r1), cam_eff(w2, e2, r2);
        M(e1, me), M(r1, mr);
        bool g = true;
        for (int i = 0; i < 3; i++) g = g && fabsf(e2[i] - me[i]) < 3 && fabsf(r2[i] - mr[i]) < 3;
        ok(g && w2[10] == -182 && w2[12] == 7, "the same with a rotation: the offset goes into the translation");
        (void)anyRot;
    }
    Application nofit = a;
    nofit.fitK = 0;
    notes.clear();
    std::vector<EffShot> e0 = app_effective(nofit, *t, ff, notes);
    bool raw = true;
    for (size_t j = 0; j < e0.size() && j < a.shots.size(); j++)
        if (a.shots[j].enabled && !a.shots[j].has && a.shots[j].s.kind == 2) raw = raw && !strcmp(e0[j].source, "template") && !memcmp(e0[j].w, a.shots[j].s.w, sizeof e0[j].w);
    ok(raw, "without a Fit the donor's words are used as they were");
    Application self = app_new(*d, di, *d, di, true);
    FitFrame fs = fit_frame(*d, *d);
    std::vector<EffShot> es = app_effective(self, *d, fs, notes);
    bool same = fs.ok && fabs(fs.k - 1) < 1e-6 && es.size() == a.shots.size();
    for (size_t j = 0; same && j < es.size(); j++)
        if (a.shots[j].s.kind == 2)
            for (int i = 0; i < 14; i++) same = same && abs(es[j].w[i] - a.shots[j].s.w[i]) <= 1;
    ok(same, "a Fit of a model to itself changes nothing");
    /* fewer commands in the target than shots in the donor */
    Application big = a;
    big.shots.push_back(big.shots.back());
    big.shots.back().to = (int)tc.size() + 5;
    notes.clear();
    app_effective(big, *t, ff, notes);
    ok(!notes.empty() && notes.back().find("goes nowhere") != std::string::npos, "a donor shot with no command to go to is said, not forced");
    std::vector<TplShot> tcAfter = script_cams(fighter_scripts(*t)[ti]);
    bool untouched = tcAfter.size() == tc.size();
    for (size_t j = 0; untouched && j < tc.size(); j++) untouched = !memcmp(&tc[j], &tcAfter[j], sizeof(TplShot));
    ok(untouched, "the target's script is as the data has it");
    /* what doesn't resolve */
    Application gone = a;
    gone.donor = 99999;
    got.clear();
    app_parse(app_json({gone}), got, err, skipped);
    ok(got.size() == 1 && !got[0].warn.empty() && got[0].shots.size() == a.shots.size(), "a missing donor is reported and the snapshot kept");
    Application mod = a;
    mod.context = "dw2003.bladegarurumon";
    got.clear();
    app_parse(app_json({mod}), got, err, skipped);
    ok(got.size() == 1 && got[0].id() != a.id() && got[0].warn.size() && got[0].warn[0].find("context") != std::string::npos, "a mod's context is another application and says what it needs");
    Application changed = a;
    changed.shots[0].s.w[0] += 1;
    changed.shots[0].s.kind = 2;
    got.clear();
    app_parse(app_json({changed}), got, err, skipped);
    ok(got.size() == 1 && !got[0].warn.empty(), "a donor script that changed since the save is said");
    std::string v2 = app_json({a});
    v2.replace(v2.find("\"schema_version\":1"), 18, "\"schema_version\":2");
    got.clear();
    ok(!app_parse(v2, got, err, skipped) && err.find("schema_version 2") != std::string::npos, "an unknown schema version is refused, naming it");
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "dw3mv_app_test", file = dir / "applications.json";
    remove_tree(dir);
    ok(app_save(file, {a}, err), "saves");
    std::string first = preset_text(file);
    Application twin = a;
    twin.variant = "copy";
    got.clear();
    ok(a.id() != twin.id() && app_parse(app_json({a, twin}), got, err, skipped) && got.size() == 2 && (got[0].variant == "copy" || got[1].variant == "copy"), "a variant is another application and is kept");
    Application a2 = a;
    a2.status = "rejected";
    ok(app_save(file, {a2}, err) && preset_text(file) == app_json({a2}) && !std::filesystem::exists(dir / "applications.json.tmp") && preset_text(file.string() + ".bak") == first,
       "replaces the file byte for byte, leaves no temp and keeps the previous one as .bak");
    remove_tree(dir);
    printf("%s\n", bad ? "FAILED" : "all hold");
    return bad != 0;
}
