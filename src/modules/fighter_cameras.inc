/* ===== Fighter: the battle camera of a played script, A and B =====
   The state of the script player, the candidate camera and the choreography, and the logic that keeps A (the camera the script asks
   for) and B (the candidate, or the application) on the same frame. Plain functions with their inputs explicit: the Fighter section's
   lambdas (ui/script_player.inc) only bind them to main's locals. */

/* The game's camera the script has set (or the battle's default view), kept while it plays; the battle camera twice, on the same
   script: A with the cameras the script asks for, B with the candidate (or the application) in their place */
struct ScriptRun {
    bool on = false;
    int script = 0, pc = 0;
    float wait = 0;
    bool waitMotion = false;
    bool camSet = false;
    Camera cam;
    float fov = 0.8f;
    int camIdx = -1;
    CamTrack a, b;
    float camAcc = 0;
    bool rollNoted = false;
    int camOrd = 0; /* the camera commands met so far */
};

/* A candidate camera (Camera workbench): another fighter's camera, adjusted, in the place of the camera the script asks for, at the
   same moments. FIGHTER is its donor in g_game.fighters (same side as the model); EVERY: also at the camera commands the
   player doesn't act on (explicit ones, the other side's shots). ORIG: the game's camera the script last asked for (-1 the default view) */
struct Candidate {
    bool on = false, every = false;
    int fighter = -1, cam = 0;
    GameCamera c = {};
    int orig = -1;
    int script = -1; /* the script last played, of OF: what a preset is made for */
    const Fighter *of = nullptr;
    GameCamera before = {}; /* the camera before a fit, for undoing it */
    double fitK = 0;        /* the scale of the fit the camera started from; 0 none */
};

/* The choreography (templates.inc): APP, an application being worked on, its EFF effective cameras on the target's script, and the
   saved ones. ON: B is the application instead of the candidate camera */
struct ChoreoUi {
    bool on = false, has = false, fit = true;
    Application app;
    std::vector<EffShot> eff;
    std::vector<std::string> notes;
    FitFrame ff;
    std::pair<int, int> ffKey = {-1, -1};
    int donor = -1, donorScript = -1, targetScript = -1; /* donor: index in g_game.fighters */
    int donorOf = -2;                                      /* the donor DOPT was made for */
    std::vector<std::pair<int, int>> dopt;                 /* the donor's scripts with camera commands: number, count */
    bool loaded = false, blocked = false;
    std::string error, msg, active;
    std::vector<Application> list;
    char name[64] = "", notes_[256] = "", variant[24] = "";
    int status = 0;
};

/* B's source is one choice: the sequence (an application on screen, and "B shows" on it), else the candidate camera */
static bool b_is_sequence(const ChoreoUi &choreo) { return choreo.on && choreo.has; }
static const char *cams_b_name(const ChoreoUi &choreo) { return b_is_sequence(choreo) ? "sequence" : "candidate camera"; }
static std::string cams_showing(bool b, const ChoreoUi &choreo) { return b ? std::string("Showing B: ") + cams_b_name(choreo) : "Showing A: the script's camera"; }

/* the candidate's default donor for fighter F: F itself (its index in g_game.fighters, -1 if it isn't there) and its battle view (a partner's
   camera 9, an enemy's first), clamped to its cameras */
static int cams_default_donor(const Fighter &f, int &cam) {
    for (int i = 0; i < (int)g_game.fighters.size(); i++)
        if (&g_game.fighters[i] == &f) return cam = std::min<int>(f.enemy ? 0 : 8, std::max(0, (int)f.cameras.size() - 1)), i;
    return -1;
}

static const Fighter *scene_fighter(const Scene &scene) { return scene.fighters.empty() ? nullptr : scene.fighters[scene.fighter]; }

/* the view takes the game's camera the run has set, as the player's own */
static void run_snap_cam(ScriptRun &run, const View &view) { run.cam = view.cam, run.fov = view.fov, run.camIdx = view.gameCamera, run.camSet = true; }

/* the camera on screen: the words of the track that is selected (B with the candidate on, else A) */
static void cams_show(View &view, const Scene &scene, ScriptRun &run, const Candidate &cand) {
    const Fighter *f = scene_fighter(scene);
    const CamTrack &tr = cand.on ? run.b : run.a;
    if (!f || !tr.set) return;
    apply_cam_words(view, *f, tr.cur, tr.shown());
    run_snap_cam(run, view);
}

/* B's camera at the script's camera command ORD (LN, of fighter F): the application's effective one while it is on, else the
   candidate, at the fighter's own shots and with "also at the cuts" at the rest. False: B stays where it is */
static bool cams_b_target(const Candidate &cand, const ChoreoUi &choreo, const Fighter &f, const ScriptLine &ln, int ord, int w[14], int &time) {
    time = ln.camTime;
    if (choreo.on && choreo.has) {
        if (ord >= (int)choreo.eff.size()) return false;
        const EffShot &e = choreo.eff[ord];
        time = e.time;
        if (e.kind == 2) return memcpy(w, e.w, 14 * sizeof(int)), true;
        if (e.kind == 1) {
            int idx = e.camId <= 4 ? (f.enemy ? -1 : e.camId + 7) : (f.enemy ? e.camId - 5 : -1);
            if (idx < 0 || idx >= (int)f.cameras.size()) return false;
            return cam_words(f.cameras[idx], w), true;
        }
        if (f.enemy || f.cameras.size() < 2) return false;
        return cam_words(f.cameras[1], w), true; /* the battle's default view */
    }
    int c = ln.camera >= 100 ? ln.camera - 100 : ln.camera;
    bool own = ln.camera >= 0 && c < (int)f.cameras.size() && (ln.camera >= 100) == f.enemy;
    if (!(own || cand.every)) return false;
    return cam_words(cand.c, w), true;
}

/* where B starts a script: the battle's default view with an application, else the candidate */
static void cams_b_start(const Candidate &cand, const ChoreoUi &choreo, const Fighter &f, int w[14]) {
    if (choreo.on && choreo.has && !f.enemy && f.cameras.size() > 1) cam_words(f.cameras[1], w);
    else cam_words(cand.c, w);
}

/* B as it would be at the point the script has reached (after an edit of what B is): the last target up to there, as a cut */
static void cams_rebuild_b(View &view, const Scene &scene, ScriptRun &run, const Candidate &cand, const ChoreoUi &choreo) {
    const Fighter *f = scene_fighter(scene);
    if (!f || !run.on || !scene.model || run.script >= (int)scene.model->scripts.size()) return;
    const std::vector<ScriptLine> &sc = scene.model->scripts[run.script];
    int w[14], w2[14], t, ord = 0;
    cams_b_start(cand, choreo, *f, w);
    for (int n = 0; n < run.pc && n < (int)sc.size(); n++) {
        if (!sc[n].camMode) continue;
        if (cams_b_target(cand, choreo, *f, sc[n], ord, w2, t)) memcpy(w, w2, sizeof w);
        ord++;
    }
    run.b.cut(w, -1);
    if (cand.on) cams_show(view, scene, run, cand);
}

/* the application's effective cameras again, after any change to it */
static void cams_choreo_refresh(View &view, const Scene &scene, ScriptRun &run, const Candidate &cand, ChoreoUi &choreo) {
    const Fighter *f = scene_fighter(scene);
    choreo.eff.clear(), choreo.notes.clear();
    if (!f || !choreo.has) return;
    app_check(choreo.app);
    const Fighter *d = fighter_by_id(choreo.app.donor, false);
    if (choreo.app.fitK > 0 && d && (choreo.ffKey != std::make_pair(d->id, f->id) || !choreo.ff.ok)) /* the boxes are read from the models: once for a pair */
        choreo.ff = fit_frame(*d, *f), choreo.ffKey = {d->id, f->id};
    choreo.eff = app_effective(choreo.app, *f, choreo.ff, choreo.notes);
    cams_rebuild_b(view, scene, run, cand, choreo);
}

/* A (the script's camera) or B (the candidate) now, on the frame that is showing: while a script plays, the two cameras
   go on side by side (also through a fade), so the switch lands where each one is */
static void cams_set_ab(View &view, const Scene &scene, ScriptRun &run, Candidate &cand, const ChoreoUi &choreo, bool b) {
    const Fighter *f = scene_fighter(scene);
    bool app = choreo.on && choreo.has; /* B is the application: no candidate is needed */
    if (!f || (!app && (cand.fighter < 0 || cand.fighter >= (int)g_game.fighters.size() || g_game.fighters[cand.fighter].enemy != f->enemy))) return;
    cand.on = b;
    if (run.on) cams_show(view, scene, run, cand);
    else if (!app) ab_view(view, *f, b, cand.c, cand.orig);
    else if (!b && cand.orig >= 0) apply_camera(view, *f, cand.orig);
}

/* the candidate was edited: B follows it (a fade in progress ends where the edit is) */
static void cams_cand_edited(View &view, const Scene &scene, ScriptRun &run, const Candidate &cand, const ChoreoUi &choreo) {
    const Fighter *f = scene_fighter(scene);
    if (!f || choreo.on) return; /* B is the application */
    if (run.on) {
        int w[14];
        cam_words(cand.c, w);
        if (run.b.t == 0x1000) run.b.cut(w, -1);
        else memcpy(run.b.to, w, sizeof w);
        if (cand.on) cams_show(view, scene, run, cand);
    } else if (cand.on) {
        apply_game_camera(view, *f, cand.c, -1);
    }
}

/* The script's camera command LN (the ORD-th of the run) reaches A and B: A gets what the game does with the command (a fade of
   camTime frames, or a cut); B the candidate in its place, at the fighter's own shots and, with "also at the cuts", at the rest.
   NOTE is called once, the first time a rotation or a roll is met. */
static void cams_command(View &view, const Scene &scene, ScriptRun &run, Candidate &cand, const ChoreoUi &choreo, const Fighter &f, const ScriptLine &ln,
                         const std::function<void()> &note_rotation) {
    int c = ln.camera >= 100 ? ln.camera - 100 : ln.camera;
    bool own = ln.camera >= 0 && c < (int)f.cameras.size() && (ln.camera >= 100) == f.enemy;
    int w[14], idx = -1, ord = run.camOrd++;
    bool act = true;
    if (ln.camMode == 2) {
        memcpy(w, ln.camRaw, sizeof w);
        if ((w[12] || w[9] || w[10] || w[11]) && !run.rollNoted) {
            run.rollNoted = true;
            note_rotation();
        }
    } else if (own) {
        cam_words(f.cameras[c], w), idx = c, cand.orig = c;
    } else if (ln.camMode == 3 && !f.enemy && f.cameras.size() > 1) { /* the battle's default view */
        cam_words(f.cameras[1], w), idx = 1, cand.orig = 1;
    } else {
        act = false; /* the other side's shot: its fighter is not here */
    }
    if (act) run.a.fade(w, ln.camTime, idx);
    int bw[14], bt;
    if (cams_b_target(cand, choreo, f, ln, ord, bw, bt)) run.b.fade(bw, bt, -1);
    cams_show(view, scene, run, cand);
}

/* --test-cameras: A and B on a synthetic script, on a real partner's cameras. Headless: no window, no GL. */
static int cams_selftest() {
    int bad = 0;
    auto ok = [&](bool c, const char *what) { printf("%s  %s\n", c ? "ok  " : "FAIL", what), bad += !c; };
    const Fighter *f = nullptr;
    for (const Fighter &x : g_game.fighters)
        if (!f && !x.enemy && x.cameras.size() >= 3) f = &x;
    if (!f) return printf("no partner with cameras to test with\n"), 1;
    Scene scene;
    scene.model = std::make_unique<Model>();
    scene.fighters = {f};
    auto line = [](int camera, int mode, int time) {
        ScriptLine l;
        l.camera = camera, l.camMode = mode, l.camTime = time;
        return l;
    };
    ScriptLine raw = line(-1, 2, 10);
    int rawW[14] = {-4000, 300, -9000, 0, 0, 100, 500, 2000, 6000, 0, -182, 0, 7, 280};
    memcpy(raw.camRaw, rawW, sizeof rawW);
    scene.model->scripts = {{line(0, 1, 0), raw, line(1, 1, 0)}};
    const std::vector<ScriptLine> &sc = scene.model->scripts[0];
    int w0[14], w1[14], wc[14];
    cam_words(f->cameras[0], w0), cam_words(f->cameras[1], w1);

    View view;
    ScriptRun run;
    Candidate cand;
    ChoreoUi choreo;
    cand.c = f->cameras[2], cam_words(cand.c, wc);
    cand.fighter = (int)(f - &g_game.fighters[0]);
    run.on = true;
    int notes = 0;
    auto noted = [&] { notes++; };

    /* the fighter's own shot: A takes the camera, B the candidate */
    cams_command(view, scene, run, cand, choreo, *f, sc[0], noted);
    ok(!memcmp(run.a.cur, w0, sizeof w0) && run.a.idx == 0 && cand.orig == 0, "A takes the fighter's own camera, and it is the one B is compared with");
    ok(!memcmp(run.b.cur, wc, sizeof wc) && run.b.t == 0x1000, "B takes the candidate, as a cut when the command has no time");
    /* an explicit shot: A fades; B stays unless "also at the cuts" */
    cams_command(view, scene, run, cand, choreo, *f, sc[1], noted);
    ok(run.a.t == 0 && !memcmp(run.a.to, rawW, sizeof rawW) && run.b.t == 0x1000, "an explicit shot fades A over its time, and B stays where it is");
    ok(notes == 1, "a rotation or a roll is said once");
    run.a.t = 0x1000, run.camOrd = 1;
    cand.every = true;
    cams_command(view, scene, run, cand, choreo, *f, sc[1], noted);
    ok(run.b.t == 0 && !memcmp(run.b.to, wc, sizeof wc) && notes == 1, "with \"also at the cuts\" B fades to the candidate there too (and is not said again)");
    int ticks = 0;
    while (run.a.t != 0x1000 && ticks < 100) run.a.tick(), run.b.tick(), ticks++;
    ok(ticks == 11 && !memcmp(run.a.cur, rawW, sizeof rawW) && !memcmp(run.b.cur, wc, sizeof wc), "the fade of 10 frames ends in 11 ticks, on the words");
    cand.every = false;

    /* the other side's shot: nothing for A */
    int before[14];
    memcpy(before, run.a.cur, sizeof before);
    ScriptLine other = line(100, 1, 0);
    cams_command(view, scene, run, cand, choreo, *f, other, noted);
    ok(!memcmp(run.a.cur, before, sizeof before), "the other side's shot leaves A as it was");

    /* the view follows the selected track */
    run.a.cut(w0, 0), run.b.cut(wc, -1);
    cand.on = false;
    cams_show(view, scene, run, cand);
    Camera atA = view.cam;
    cand.on = true;
    cams_show(view, scene, run, cand);
    ok(memcmp(&atA, &view.cam, sizeof atA) != 0 && run.camSet, "the view shows A or B as selected");
    cand.on = false;
    cams_show(view, scene, run, cand);
    ok(!memcmp(&atA, &view.cam, sizeof atA), "and back to A is the same view");

    /* B rebuilt at the point the script has reached */
    cand.every = true, cand.on = false;
    run.pc = 3, run.script = 0, run.a.cut(w0, 0), run.b.cut(w0, -1);
    cams_rebuild_b(view, scene, run, cand, choreo);
    ok(!memcmp(run.b.cur, wc, sizeof wc) && run.b.t == 0x1000, "B rebuilt is the last target up to the line the script is at, as a cut");
    cand.every = false;
    run.pc = 0, cams_rebuild_b(view, scene, run, cand, choreo);
    ok(!memcmp(run.b.cur, wc, sizeof wc), "at the start B is the candidate");

    /* an edit of the candidate: B follows, a fade in progress ends where the edit is */
    cand.c = f->cameras[1], cam_words(cand.c, wc);
    cams_cand_edited(view, scene, run, cand, choreo);
    ok(!memcmp(run.b.cur, wc, sizeof wc), "B follows an edit of the candidate");
    run.b.cut(w0, -1), run.b.fade(w1, 20, -1);
    cams_cand_edited(view, scene, run, cand, choreo);
    ok(run.b.t == 0 && !memcmp(run.b.to, wc, sizeof wc), "a fade in progress goes on, to the edit");

    /* A/B: the candidate must be of the model's side */
    run.on = false, cand.on = false;
    cand.fighter = -1;
    cams_set_ab(view, scene, run, cand, choreo, true);
    ok(!cand.on, "without a candidate there is no B");
    cand.fighter = (int)(f - &g_game.fighters[0]);
    cams_set_ab(view, scene, run, cand, choreo, true);
    ok(cand.on, "with one, B is on");

    /* the application as B */
    choreo.on = choreo.has = true;
    choreo.eff.assign(3, EffShot());
    choreo.eff[0].kind = 2, choreo.eff[0].time = 7, memcpy(choreo.eff[0].w, rawW, sizeof rawW);
    choreo.eff[1].kind = 0, choreo.eff[1].time = 3;
    choreo.eff[2].kind = 1, choreo.eff[2].camId = 1, choreo.eff[2].time = 5;
    int w[14], t;
    ok(cams_b_target(cand, choreo, *f, sc[0], 0, w, t) && t == 7 && !memcmp(w, rawW, sizeof rawW), "an explicit effective shot gives its words and its own time");
    ok(cams_b_target(cand, choreo, *f, sc[0], 1, w, t) && t == 3 && !memcmp(w, w1, sizeof w1), "a shot of the default view is the partner's camera 2");
    ok(cams_b_target(cand, choreo, *f, sc[0], 2, w, t) && t == 5 && !memcmp(w, ([&] { int x[14]; cam_words(f->cameras[8], x); return std::vector<int>(x, x + 14); }()).data(), sizeof w), "a shot of a fighter's camera is that one");
    ok(!cams_b_target(cand, choreo, *f, sc[0], 3, w, t), "a command past the application's shots leaves B as it is");
    cams_b_start(cand, choreo, *f, w);
    ok(!memcmp(w, w1, sizeof w1), "B starts at the battle's default view with an application");
    choreo.on = false;
    cams_b_start(cand, choreo, *f, w);
    ok(!memcmp(w, wc, sizeof wc), "and at the candidate without");
    /* what B is called and what the bar says */
    ChoreoUi none, seq;
    seq.on = seq.has = true;
    ok(!b_is_sequence(none) && b_is_sequence(seq) && !strcmp(cams_b_name(none), "candidate camera") && !strcmp(cams_b_name(seq), "sequence"), "B is the sequence only with one on screen and chosen");
    seq.has = false;
    ok(!b_is_sequence(seq), "chosen but nothing built: B is the candidate");
    seq.has = true, seq.on = false;
    ok(!b_is_sequence(seq), "built but not chosen: B is the candidate");
    ok(cams_showing(false, none) == "Showing A: the script's camera" && cams_showing(true, none) == "Showing B: candidate camera" && cams_showing(true, ([&] { ChoreoUi c; c.on = c.has = true; return c; })()) == "Showing B: sequence",
       "the bar says which one is showing, and what B is");
    /* the default donor */
    int cam = -1, di = cams_default_donor(*f, cam);
    ok(di == (int)(f - &g_game.fighters[0]) && cam == std::min<int>(f->enemy ? 0 : 8, (int)f->cameras.size() - 1), "the default donor is the fighter itself, at its battle view");
    Fighter stranger = *f;
    ok(cams_default_donor(stranger, cam) == -1, "a fighter that isn't in the list has none");
    printf("%s\n", bad ? "FAILED" : "all hold");
    return bad != 0;
}
