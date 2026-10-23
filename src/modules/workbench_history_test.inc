/* ===== --test-workbench: workbench_history.inc, headless ===== */
static int workbench_selftest() {
    int bad = 0;
    auto ok = [&](bool c, const char *what) { printf("%s  %s\n", c ? "ok  " : "FAIL", what), bad += !c; };
    /* a state with a number in it: V is its candidate's eye x, so the tests can tell states apart */
    auto state = [](int v) {
        WbSnap s;
        s.c.fighter = 3, s.c.cam = 1;
        s.c.c.pos[0] = (int16_t)v;
        return s;
    };
    auto seq = [](int shots, int variant = 0) {
        WbSeq s;
        s.has = true;
        s.app.variant = variant ? "v" + std::to_string(variant) : "";
        s.app.fighter = 7, s.app.scriptIdx = 11, s.app.donorIdx = 11;
        for (int i = 0; i < shots; i++) {
            AppShot a;
            a.s.line = i, a.s.kind = 2, a.s.time = 10, a.to = i;
            s.app.shots.push_back(a);
        }
        s.key = wb_seq_key(s.app);
        return s;
    };

    /* one operation, one entry */
    WbHist h;
    WbSnap cur = state(0);
    for (int frame = 1; frame <= 30; frame++) { /* a drag: 30 frames */
        h.edit(cur, true);
        cur = state(frame);
    }
    h.finish(cur);
    ok(h.undo.size() == 1 && h.undo[0].c.c.pos[0] == 0, "a drag of many frames is one entry, with the state before it");
    WbSnap to;
    ok(h.step(false, cur, to) && to.c.c.pos[0] == 0 && h.redo.size() == 1 && h.redo[0].c.c.pos[0] == 30, "undo goes to the state before the drag and keeps the one it left for redo");
    cur = to;
    ok(h.step(true, cur, to) && to.c.c.pos[0] == 30 && h.undo.size() == 1, "redo comes back to the end of the drag");
    cur = to;
    h.edit(cur, false);
    cur = state(31);
    h.finish(cur);
    ok(h.undo.size() == 2 && h.redo.empty(), "a click is an entry too, and a new operation drops what redo had");
    ok(!h.step(true, cur, to), "nothing to redo");

    /* an operation that changes nothing leaves no entry */
    h = WbHist();
    cur = state(5);
    for (int frame = 0; frame < 10; frame++) h.edit(cur, true); /* a drag that ends where it began */
    h.finish(cur);
    ok(h.undo.empty() && !h.dragging && !h.pending, "a drag put back where it started leaves no entry");
    h.edit(cur, false);
    h.finish(cur);
    ok(h.undo.empty(), "a click that changes nothing neither");
    h.finish(cur);
    ok(h.undo.empty(), "finishing with nothing begun does nothing");
    WbSnap a = state(1), b = state(2);
    h.edit(a, false);
    h.finish(b);
    h.edit(b, false);
    h.finish(b); /* no change: the one before it stays */
    ok(h.undo.size() == 1 && h.undo[0].c.c.pos[0] == 1, "a no-op after a real one does not remove the real one");

    /* the limit: the oldest go */
    h = WbHist();
    for (int i = 0; i < 130; i++) {
        h.edit(state(i), false);
        h.finish(state(i + 1));
    }
    ok(h.undo.size() == WbHist::LIMIT && h.undo.front().c.c.pos[0] == 30 && h.undo.back().c.c.pos[0] == 129, "at 100 entries the oldest go and the recent stay");
    cur = state(130);
    int steps = 0;
    while (h.step(false, cur, to)) cur = to, steps++;
    ok(steps == 100 && cur.c.c.pos[0] == 30 && h.redo.size() == 100, "undoing all of them goes back to the oldest one kept");
    steps = 0;
    while (h.step(true, cur, to)) cur = to, steps++;
    ok(steps == 100 && cur.c.c.pos[0] == 130 && h.undo.size() == 100, "and redo back to the end; neither stack outgrows the limit");

    /* the unsaved state is compared, not flagged */
    h = WbHist();
    WbSnap clean = state(0);
    h.set_clean_cand(clean.c), h.set_clean_seq(clean.s);
    ok(h.dirty(clean) == 0, "right after the clean point nothing is unsaved");
    cur = state(9);
    ok(h.dirty(cur) == WB_CAND, "a changed candidate is unsaved");
    ok(h.dirty(state(0)) == 0, "put back exactly, it is clean again");
    WbSnap withSeq = clean;
    withSeq.s = seq(3);
    ok(h.dirty(withSeq) == WB_SEQ, "a sequence built is an unsaved sequence");
    WbSnap both = cur;
    both.s = seq(3);
    ok(h.dirty(both) == (WB_CAND | WB_SEQ), "both can be unsaved");
    h.set_clean_cand(both.c); /* a preset saved */
    ok(h.dirty(both) == WB_SEQ, "saving the candidate does not make the sequence clean");
    h.set_clean_seq(both.s); /* the application saved */
    ok(h.dirty(both) == 0, "nor the other way round: each part has its own clean point");
    WbSnap edited = both;
    edited.s = seq(3);
    edited.s.app.shots[1].enabled = false;
    edited.s.key = wb_seq_key(edited.s.app);
    ok(h.dirty(edited) == WB_SEQ, "a shot switched off is unsaved");
    edited.s.app.shots[1].enabled = true;
    edited.s.key = wb_seq_key(edited.s.app);
    ok(h.dirty(edited) == 0, "and clean again when switched back");
    /* the history walks back to clean: built, edited, undone */
    h = WbHist();
    h.set_clean_cand(clean.c), h.set_clean_seq(clean.s);
    cur = clean;
    h.edit(cur, false);
    cur.s = seq(3);
    h.finish(cur);
    WbSnap built = cur;
    h.edit(cur, false);
    cur.s.app.shots[0].to = -1;
    cur.s.key = wb_seq_key(cur.s.app);
    h.finish(cur);
    ok(h.dirty(cur) == WB_SEQ && h.undo.size() == 2, "a sequence built and then edited is unsaved, in two entries");
    h.step(false, cur, to), cur = to;
    ok(wb_same(cur, built) && h.dirty(cur) == WB_SEQ, "undone once it is the built one, still unsaved");
    h.step(false, cur, to), cur = to;
    ok(!cur.s.has && h.dirty(cur) == 0, "undone back to no sequence it is clean: running operations is not what makes it unsaved");
    h.step(true, cur, to), cur = to;
    ok(h.dirty(cur) == WB_SEQ, "redone it is unsaved again");

    /* what the identity of a sequence is */
    WbSeq base = seq(3);
    ok(wb_same(base, seq(3)) && !wb_same(base, seq(4)) && !wb_same(base, seq(3, 1)), "the same shots are the same sequence; another count or variant is another");
    WbSeq t = seq(3);
    t.app.name = "other name", t.app.notes = "notes", t.app.status = "approved";
    t.key = wb_seq_key(t.app);
    ok(wb_same(base, t), "the name, notes and status are the form's, not the sequence's");
    auto differs = [&](const std::function<void(Application &)> &change) {
        WbSeq u = seq(3);
        change(u.app);
        u.key = wb_seq_key(u.app);
        return !wb_same(base, u);
    };
    ok(differs([](Application &a) { a.fitK = 1.2; }) && differs([](Application &a) { a.shots[1].to = 2; }) && differs([](Application &a) { a.shots[1].enabled = false; }) &&
           differs([](Application &a) { a.shots[1].has = true; }) && differs([](Application &a) { a.shots[1].has = true, a.shots[1].ow[3] = 5; }) &&
           differs([](Application &a) { a.shots[1].has = true, a.shots[1].otime = 20; }) && differs([](Application &a) { a.scriptIdx = 12; }) &&
           differs([](Application &a) { a.donorIdx = 5; }),
       "the Fit, a shot's map, switch and override (words and time), the target and the donor script are the sequence's identity");
    WbSeq none1, none2;
    none1.key = "x";
    ok(wb_same(none1, none2), "without a sequence there is nothing to compare");
    WbCand c1, c2;
    c1.fitK = c2.fitK = 0, c1.before.pos[0] = 5;
    ok(wb_same(c1, c2), "the camera before a Fit does not count while there is no Fit");
    c1.fitK = c2.fitK = 1.1;
    ok(!wb_same(c1, c2), "and does while there is one");

    /* taking and putting back */
    Candidate cand;
    ChoreoUi choreo;
    cand.fighter = 4, cand.cam = 2, cand.c.pos[1] = 77, cand.every = true, cand.fitK = 1.5, cand.before.ref[0] = 9;
    cand.on = true, cand.orig = 3, cand.script = 11;
    choreo.has = choreo.on = true;
    choreo.app = seq(2).app;
    WbSnap taken = wb_take(cand, choreo);
    Candidate cand2;
    ChoreoUi choreo2;
    cand2.on = false, cand2.orig = -1, cand2.script = 5;
    choreo2.on = true;
    wb_put(cand2, choreo2, taken);
    ok(cand2.fighter == 4 && cand2.cam == 2 && cand2.c.pos[1] == 77 && cand2.every && cand2.fitK == 1.5 && cand2.before.ref[0] == 9 && choreo2.has && wb_same(wb_take(cand2, choreo2), taken),
       "put back is what was taken, both parts");
    ok(!cand2.on && cand2.orig == -1 && cand2.script == 5 && choreo2.on, "A/B showing, the script's last camera and script are not touched");
    WbSnap noSeq = taken;
    noSeq.s = WbSeq();
    wb_put(cand2, choreo2, noSeq);
    ok(!choreo2.has && !choreo2.on, "without a sequence B cannot be showing one");

    /* Undo / Redo while a script plays: B follows, the script does not move */
    {
        const Fighter *f = nullptr;
        for (const Fighter &x : g_game.fighters)
            if (!f && !x.enemy && x.cameras.size() >= 3) f = &x;
        ok(f != nullptr, "a partner with cameras to test with");
        if (f) {
            Scene scene;
            scene.model = std::make_unique<Model>();
            scene.fighters = {f};
            ScriptLine l0;
            l0.camera = 0, l0.camMode = 1, l0.camTime = 0;
            scene.model->scripts = {{l0, l0, l0}};
            View view;
            ScriptRun run;
            Candidate cd;
            ChoreoUi ch;
            int fi = (int)(f - &g_game.fighters[0]);
            cd.fighter = fi, cd.cam = 1, cd.c = f->cameras[1];
            run.on = true, run.script = 0, run.pc = 2, run.wait = 5, run.camOrd = 1;
            int wa[14], wcand0[14], wcand1[14];
            cam_words(f->cameras[0], wa), cam_words(f->cameras[1], wcand0), cam_words(f->cameras[2], wcand1);
            run.a.cut(wa, 0);
            run.b.cut(wcand0, -1);
            WbSnap s0 = wb_take(cd, ch);
            cd.c = f->cameras[2];
            cams_cand_edited(view, scene, run, cd, ch);
            WbSnap s1 = wb_take(cd, ch);
            ok(!memcmp(run.b.cur, wcand1, sizeof wcand1), "(setup) an edit of the candidate moved B");
            cams_restore(view, scene, run, cd, ch, s0);
            ok(!memcmp(&cd.c, &s0.c.c, sizeof cd.c) && !memcmp(run.b.cur, wcand0, sizeof wcand0) && run.b.t == 0x1000, "undoing an edit puts B back on the candidate as it was");
            ok(run.on && run.pc == 2 && run.wait == 5 && run.camOrd == 1 && !memcmp(run.a.cur, wa, sizeof wa) && run.a.idx == 0, "and the script, its waits and A are where they were");
            cams_restore(view, scene, run, cd, ch, s1);
            ok(!memcmp(run.b.cur, wcand1, sizeof wcand1) && run.pc == 2 && run.wait == 5, "redo follows it the same way");
            /* in the middle of a fade of B: it goes on, to the restored camera */
            run.b.cut(wcand1, -1);
            run.b.fade(wcand0, 20, -1);
            run.b.tick();
            int t = run.b.t;
            cams_restore(view, scene, run, cd, ch, s0);
            ok(run.b.t == t && !memcmp(run.b.to, wcand0, sizeof wcand0), "a fade in progress goes on, towards the restored camera");
            /* B was showing a sequence that undo takes away */
            ch.has = ch.on = true;
            ch.app = seq(2).app;
            WbSnap withSeq = wb_take(cd, ch);
            run.b.cut(wcand1, -1);
            cams_restore(view, scene, run, cd, ch, s0); /* s0 has no sequence */
            ok(!ch.has && !ch.on && !memcmp(run.b.cur, wcand0, sizeof wcand0), "undoing a sequence away makes B the candidate again");
            cams_restore(view, scene, run, cd, ch, withSeq);
            ok(ch.has && wb_same(wb_take(cd, ch), withSeq) && run.pc == 2, "and redoing it brings the sequence back, the script untouched");
            /* not running: with B showing, the view follows the restore */
            run.on = false, cd.on = true, ch.has = ch.on = false;
            cams_restore(view, scene, run, cd, ch, s1);
            Camera at1 = view.cam;
            cams_restore(view, scene, run, cd, ch, s0);
            ok(memcmp(&at1, &view.cam, sizeof at1) != 0, "with no script and B showing, the view takes the restored candidate");
        }
    }

    /* clear */
    h = WbHist();
    h.edit(state(1), false);
    h.finish(state(2));
    h.set_clean_cand(state(2).c);
    h.clear();
    ok(h.undo.empty() && h.redo.empty() && !h.hasClean && h.dirty(state(99)) == 0 && !h.pending && !h.dragging, "clear forgets the history and the clean points");
    printf("%s\n", bad ? "FAILED" : "all hold");
    return bad != 0;
}
