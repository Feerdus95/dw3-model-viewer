    /* the effect models a script has started (3 at most, as the battle has): each its own model, played once
       from its first motion until it ends */
    struct FxModel {
        int id = 0;
        std::unique_ptr<Scene> scene;
        float acc = 0;
        M4 pre;
    };
    std::vector<FxModel> fxModels;
    auto free_fx_models = [&]() {
        for (FxModel &e : fxModels) {
            GL(glDeleteBuffers)(1, &e.scene->vbo);
            GL(glDeleteVertexArrays)(1, &e.scene->vao);
            GL(glDeleteTextures)(1, &e.scene->vram);
        }
        fxModels.clear();
    };
    /* Starts the effect model of LN (FIGHTSTG_runScriptEffectModel) where the script puts it: its position and
       rotation are the battle's (y and z negated into the model's PSX space), the fighter standing at its home */
    auto start_fx_model = [&](const ScriptLine &ln, const Fighter &f) {
        const EffectModelRow *row = nullptr;
        for (const EffectModelRow &r : EFFECT_MODELS)
            if (r.id == ln.emId) row = &r;
        if (!row || fxModels.size() >= 3) return;
        char b[32];
        snprintf(b, sizeof b, "/%04d.bin", (int)(row->file >> 16));
        std::vector<uint8_t> bytes;
        auto sc = std::make_unique<Scene>();
        sc->model = std::make_unique<Model>();
        if (!read_file(g_data + b, bytes) ||
            !sc->model->load("effect model " + std::to_string(ln.emId), std::move(bytes), nullptr, (int)(row->file & 0xFFFF), (int)(row->motions & 0xFFFF))) {
            note(NOTE_INFO, "Effect model " + std::to_string(ln.emId) + " couldn't be read: " + sc->model->error);
            return;
        }
        upload(*sc);
        /* the model's root in its PSX space, turned and put where the script says, then into the fighter's view */
        Vec3s pos = {(int16_t)ln.emPos[0], (int16_t)-ln.emPos[1], (int16_t)-ln.emPos[2]};
        Vec3s rot = {(int16_t)ln.emRot[0], (int16_t)-ln.emRot[1], (int16_t)-ln.emRot[2]};
        M4 w;
        bone_matrix(pos, rot, {4096, 4096, 4096}, w.m);
        float bp[3] = {(float)ln.emPos[0], (float)ln.emPos[1], (float)ln.emPos[2]}, v[3];
        battle_to_view(f, bp, v);
        float a = f.enemy ? 1.0f : -1.0f; /* a partner's turn about y: x and z flip */
        for (int c = 0; c < 3; c++) w.m[c * 4] *= a, w.m[c * 4 + 2] *= a;
        w.m[12] = v[0], w.m[13] = -v[1], w.m[14] = -v[2];
        FxModel e;
        e.id = ln.emId, e.scene = std::move(sc), e.pre = w;
        fxModels.push_back(std::move(e));
    };
    GLuint shotTex[8] = {};
    std::vector<uint32_t> shotPx, shotWhite;
    auto shot_effect = [&](int id) -> Effect * {
        auto it = shotFx.find(id);
        if (it == shotFx.end()) {
            auto e = std::make_unique<Effect>();
            if (!e->load(id, g_data)) e.reset();
            it = shotFx.emplace(id, std::move(e)).first;
        }
        return it->second.get();
    };
    auto shot_own = [&](int id) -> CustomEffect * {
        if (!shotMods) return nullptr;
        auto it = shotOwnFx.find(id);
        if (it == shotOwnFx.end()) {
            auto e = std::make_unique<CustomEffect>();
            if (!e->load(ownRoot + "/" + std::to_string(id))) e.reset();
            it = shotOwnFx.emplace(id, std::move(e)).first;
        }
        return it->second.get();
    };
    /* the length, in ticks, of what plays for effect ID: its own images' or the original's */
    auto shot_len = [&](int id) {
        if (CustomEffect *o = shot_own(id)) return o->duration();
        Effect *e = shot_effect(id);
        return e ? e->duration : 0;
    };
    /* the camera on screen: the words of the track that is selected (B with the candidate on, else A) */
    auto show_cam = [&]() {
        const Fighter *f = scene.fighters.empty() ? nullptr : scene.fighters[scene.fighter];
        const CamTrack &tr = cand.on ? run.b : run.a;
        if (!f || !tr.set) return;
        apply_cam_words(view, *f, tr.cur, tr.shown());
        snap_cam();
    };
    /* B's camera at the script's camera command ORD (LN, of fighter F): the application's effective one while it is on, else the
       candidate, at the fighter's own shots and with "also at the cuts" at the rest. False: B stays where it is */
    auto b_target = [&](const Fighter &f, const ScriptLine &ln, int ord, int w[14], int &time) -> bool {
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
    };
    /* where B starts a script: the battle's default view with an application, else the candidate */
    auto b_start = [&](const Fighter &f, int w[14]) {
        if (choreo.on && choreo.has && !f.enemy && f.cameras.size() > 1) cam_words(f.cameras[1], w);
        else cam_words(cand.c, w);
    };
    /* B as it would be at the point the script has reached (after an edit of what B is): the last target up to there, as a cut */
    auto rebuild_b = [&]() {
        const Fighter *f = scene.fighters.empty() ? nullptr : scene.fighters[scene.fighter];
        if (!f || !run.on || !scene.model || run.script >= (int)scene.model->scripts.size()) return;
        const std::vector<ScriptLine> &sc = scene.model->scripts[run.script];
        int w[14], w2[14], t, ord = 0;
        b_start(*f, w);
        for (int n = 0; n < run.pc && n < (int)sc.size(); n++) {
            if (!sc[n].camMode) continue;
            if (b_target(*f, sc[n], ord, w2, t)) memcpy(w, w2, sizeof w);
            ord++;
        }
        run.b.cut(w, -1);
        if (cand.on) show_cam();
    };
    /* the application's effective cameras again, after any change to it */
    auto choreo_refresh = [&]() {
        const Fighter *f = scene.fighters.empty() ? nullptr : scene.fighters[scene.fighter];
        choreo.eff.clear(), choreo.notes.clear();
        if (!f || !choreo.has) return;
        app_check(choreo.app);
        const Fighter *d = fighter_by_id(choreo.app.donor, false);
        if (choreo.app.fitK > 0 && d && (choreo.ffKey != std::make_pair(d->id, f->id) || !choreo.ff.ok)) /* the boxes are read from the models: once for a pair */
            choreo.ff = fit_frame(*d, *f), choreo.ffKey = {d->id, f->id};
        choreo.eff = app_effective(choreo.app, *f, choreo.ff, choreo.notes);
        rebuild_b();
    };
    auto run_script = [&](int i) {
        if (!scene.model || i < 0 || i >= (int)scene.model->scripts.size()) return;
        run = ScriptRun{true, i, 0, 0, false};
        cand.script = i, cand.of = scene.fighters.empty() ? nullptr : scene.fighters[scene.fighter];
        shots.clear();
        shotOwnFx.clear(), ownIds = own_effects(ownRoot); /* the mod's files may have changed since */
        free_fx_models();
        scene.model->setMotion(1);
        view.playing = true;
        /* the battle's default view, which the script's cameras then change: a partner's camera 2, the one
           for an enemy of 2 cameras, the most common */
        const Fighter *f = scene.fighters.empty() ? nullptr : scene.fighters[scene.fighter];
        cand.orig = f && !f->enemy && f->cameras.size() > 1 ? 1 : -1;
        if (f && cand.orig >= 0) {
            int w[14];
            cam_words(f->cameras[cand.orig], w), run.a.cut(w, cand.orig);
            b_start(*f, w), run.b.cut(w, -1);
            if (lockCam) show_cam();
        }
    };
    /* A (the script's camera) or B (the candidate) now, on the frame that is showing: while a script plays, the two cameras
       go on side by side (also through a fade), so the switch lands where each one is */
    auto set_ab = [&](bool b) {
        const Fighter *f = scene.fighters.empty() ? nullptr : scene.fighters[scene.fighter];
        bool app = choreo.on && choreo.has; /* B is the application: no candidate is needed */
        if (!f || (!app && (cand.fighter < 0 || cand.fighter >= (int)g_game.fighters.size() || g_game.fighters[cand.fighter].enemy != f->enemy))) return;
        cand.on = b;
        if (run.on) show_cam();
        else if (!app) ab_view(view, *f, b, cand.c, cand.orig);
        else if (!b && cand.orig >= 0) apply_camera(view, *f, cand.orig);
    };
    /* the candidate was edited: B follows it (a fade in progress ends where the edit is) */
    auto cand_edited = [&]() {
        const Fighter *f = scene.fighters.empty() ? nullptr : scene.fighters[scene.fighter];
        if (!f || choreo.on) return; /* B is the application */
        if (run.on) {
            int w[14];
            cam_words(cand.c, w);
            if (run.b.t == 0x1000) run.b.cut(w, -1);
            else memcpy(run.b.to, w, sizeof w);
            if (cand.on) show_cam();
        } else if (cand.on) {
            apply_game_camera(view, *f, cand.c, -1);
        }
    };
    auto run_step = [&](float dt) {
        float ticks = view.playing ? dt * 60 * view.speed : 0;
        spriteDraws.clear();
        extraModels.clear();
        for (FxModel &e : fxModels) { /* one keyframe a field, until its motion is over */
            Model &em = *e.scene->model;
            e.acc += ticks;
            int n = (int)e.acc;
            e.acc -= n;
            for (int i = 0; i < std::min(n, 8); i++) em.advance(1), em.evaluate();
        }
        fxModels.erase(std::remove_if(fxModels.begin(), fxModels.end(), [&](FxModel &e) {
                           if (!e.scene->model->motionDone) return false;
                           GL(glDeleteBuffers)(1, &e.scene->vbo);
                           GL(glDeleteVertexArrays)(1, &e.scene->vao);
                           GL(glDeleteTextures)(1, &e.scene->vram);
                           return true;
                       }),
                       fxModels.end());
        for (FxModel &e : fxModels) extraModels.push_back({e.scene.get(), e.pre});
        for (FxShot &s : shots) s.t += ticks;
        shots.erase(std::remove_if(shots.begin(), shots.end(),
                                   [&](const FxShot &s) { return !shot_effect(s.id) || s.t >= shot_len(s.id); }),
                    shots.end());
        if (!run.on || !scene.model || run.script >= (int)scene.model->scripts.size()) return void(run.on = false);
        Model &m = *scene.model;
        const std::vector<ScriptLine> &sc = m.scripts[run.script];
        const Fighter *f = scene.fighters.empty() ? nullptr : scene.fighters[scene.fighter];
        float budget = ticks;
        /* the battle camera's fades, a frame at a time */
        run.camAcc += ticks;
        int frames = (int)run.camAcc;
        run.camAcc -= frames;
        bool fading = run.a.t != 0x1000 || run.b.t != 0x1000;
        for (int i = 0; i < std::min(frames, 4096); i++) run.a.tick(), run.b.tick();
        if (fading && lockCam) show_cam();
        for (int guard = 0; guard < 200; guard++) {
            if (run.wait > 0) {
                float use = std::min(run.wait, budget);
                run.wait -= use, budget -= use;
                if (run.wait > 0) return;
            }
            if (run.waitMotion) {
                if (!m.motionDone) return;
                run.waitMotion = false;
            }
            if (run.pc >= (int)sc.size()) return void(run.on = false);
            const ScriptLine &ln = sc[run.pc++];
            if (ln.text == "end") {
                run.on = false;
                m.setMotion(1);
                return;
            }
            if (ln.motion > 0 && ln.motion <= (int)m.motions.size()) m.setMotion(ln.motion);
            if (f && ln.camMode) {
                /* A: what the game does with the command (a fade of camTime frames, or a cut); B: the candidate in its place, at the
                   fighter's own shots and, with "also at the cuts", at the rest */
                int c = ln.camera >= 100 ? ln.camera - 100 : ln.camera;
                bool own = ln.camera >= 0 && c < (int)f->cameras.size() && (ln.camera >= 100) == f->enemy;
                int w[14], idx = -1, ord = run.camOrd++;
                bool act = true;
                if (ln.camMode == 2) {
                    memcpy(w, ln.camRaw, sizeof w);
                    if ((w[12] || w[9] || w[10] || w[11]) && !run.rollNoted) {
                        run.rollNoted = true;
                        note(NOTE_INFO, "This script has a camera with a rotation or a roll: they are applied as the game's library does, unchecked, and a roll is not drawn.");
                    }
                } else if (own) {
                    cam_words(f->cameras[c], w), idx = c, cand.orig = c;
                } else if (ln.camMode == 3 && !f->enemy && f->cameras.size() > 1) { /* the battle's default view */
                    cam_words(f->cameras[1], w), idx = 1, cand.orig = 1;
                } else {
                    act = false; /* the other side's shot: its fighter is not here */
                }
                if (act) run.a.fade(w, ln.camTime, idx);
                int bw[14], bt;
                if (b_target(*f, ln, ord, bw, bt)) run.b.fade(bw, bt, -1);
                show_cam();
            }
            if (ln.emStart && f) start_fx_model(ln, *f);
            if (ln.fxStart) {
                /* 9999 is the technique's own: the Fight command's, 2, is the only one every script of its number shares */
                int id = ln.fxOwn ? own_effect_id(run.script) : ln.effect;
                if (id < 0) note(NOTE_INFO, "This script's own sprite effect is its technique's, and no technique uses this script.");
                else if (shots.size() < 8 && shot_effect(id)) {
                    /* the point is in the PSX's space; the view's is the battle's (y and z negated) about the fighter */
                    float b[3] = {(float)ln.pos[0], -(float)ln.pos[1], -(float)ln.pos[2]}, p[3] = {b[0], b[1], b[2]};
                    int16_t z = (int16_t)ln.pos[2]; /* 0x7FFF and -1 are not a depth: x, y are screen pixels */
                    if (z == 0x7FFF || z == -1) {
                        shots.push_back({id, 0, {(float)ln.pos[0], (float)ln.pos[1], 0}, z == 0x7FFF ? 1 : 2});
                    } else {
                        if (f) battle_to_view(*f, b, p);
                        shots.push_back({id, 0, {p[0], p[1], p[2]}});
                    }
                }
            }
            if (ln.waitTicks >= 0) run.wait = (float)ln.waitTicks;
            if (ln.waitMotion) run.waitMotion = true;
        }
    };

