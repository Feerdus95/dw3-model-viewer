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
