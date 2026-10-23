/* ===== The camera workbench's history and its unsaved state =====
   Undo, redo and "is this what was saved?" for the candidate camera and the sequence (Cameras section). Plain structs with no ImGui:
   the section's lambdas take a snapshot before an operation, tell the history, and restore a snapshot on Undo / Redo.
   What a snapshot holds is the semantic state: the candidate (donor, donor camera, the camera values, "also at the cuts", the Fit it
   started from) and the sequence (whether there is one, and the application's donor, scripts, shots and Fit). Not in it: A/B showing, what
   B shows, the script's progress, the save form's name / notes / status, the Library's lists and files. */

struct WbCand {
    int fighter = -1, cam = 0; /* the donor: index in g_game.fighters, and its camera */
    GameCamera c = {}, before = {}; /* the camera; the one before a Fit (what "Undo the fit" goes back to) */
    bool every = false;
    double fitK = 0;
};
struct WbSeq {
    bool has = false;
    Application app;
    std::string key; /* what makes two sequences the same one: see wb_seq_key */
};
struct WbSnap {
    WbCand c;
    WbSeq s;
};

/* A sequence's identity for "unsaved": its target and donor, its Fit, and each shot's map, switch and override. The name, notes and status
   are the save form's: they are not part of what is being edited. */
static std::string wb_seq_key(const Application &a) {
    std::string k = a.id() + "|fit:" + std::to_string(a.fitK);
    char b[32];
    for (const AppShot &s : a.shots) {
        snprintf(b, sizeof b, "|%d,%d,%d,%d", s.s.line, s.s.kind, s.s.camId, s.s.time);
        k += b;
        for (int i = 0; i < 14; i++) k += "," + std::to_string(s.s.w[i]);
        k += ";" + std::to_string(s.to) + (s.enabled ? "+" : "-") + (s.has ? "o" : "n");
        if (s.has) {
            k += std::to_string(s.otime);
            for (int i = 0; i < 14; i++) k += "," + std::to_string(s.ow[i]);
        }
    }
    return k;
}

static bool wb_same(const WbCand &a, const WbCand &b) {
    return a.fighter == b.fighter && a.cam == b.cam && a.every == b.every && a.fitK == b.fitK && !memcmp(&a.c, &b.c, sizeof a.c) &&
           (a.fitK <= 0 || !memcmp(&a.before, &b.before, sizeof a.before)); /* the camera before a Fit only matters while there is a Fit to undo */
}
static bool wb_same(const WbSeq &a, const WbSeq &b) { return a.has == b.has && (!a.has || a.key == b.key); }
static bool wb_same(const WbSnap &a, const WbSnap &b) { return wb_same(a.c, b.c) && wb_same(a.s, b.s); }

/* The workbench as it is now */
static WbSnap wb_take(const Candidate &cand, const ChoreoUi &choreo) {
    WbSnap s;
    s.c.fighter = cand.fighter, s.c.cam = cand.cam, s.c.c = cand.c, s.c.before = cand.before, s.c.every = cand.every, s.c.fitK = cand.fitK;
    s.s.has = choreo.has;
    if (choreo.has) s.s.app = choreo.app, s.s.key = wb_seq_key(choreo.app);
    return s;
}

/* The workbench back to S. A/B showing, the script's progress and the donor's other settings are not touched; B showing the sequence
   without one is not a thing, so it goes back to the candidate. The caller then resyncs B (cand_edited, choreo_refresh). */
static void wb_put(Candidate &cand, ChoreoUi &choreo, const WbSnap &s) {
    cand.fighter = s.c.fighter, cand.cam = s.c.cam, cand.c = s.c.c, cand.before = s.c.before, cand.every = s.c.every, cand.fitK = s.c.fitK;
    choreo.has = s.s.has;
    if (s.s.has) choreo.app = s.s.app;
    else choreo.on = false;
}

/* Which parts differ from the last clean (saved or loaded) state: 1 the candidate, 2 the sequence */
enum { WB_CAND = 1, WB_SEQ = 2 };

struct WbHist {
    static const size_t LIMIT = 100; /* the oldest entries go first */
    std::vector<WbSnap> undo, redo;   /* the states before each operation; the states Undo left */
    bool pending = false;             /* an operation has begun and is not finished */
    bool dragging = false;            /* ... a drag: the frames after the first belong to it */
    WbCand cleanC;
    WbSeq cleanS;
    bool hasClean = false;

    void clear() { *this = WbHist(); }
    bool can_undo() const { return !undo.empty(); }
    bool can_redo() const { return !redo.empty(); }

    /* An operation is about to change the state: BEFORE is what it was. CONTINUOUS: a drag, whose later frames call this too and make no
       new entry. A new operation drops what Redo had. */
    void edit(const WbSnap &before, bool continuous) {
        if (dragging) return;
        undo.push_back(before);
        if (undo.size() > LIMIT) undo.erase(undo.begin(), undo.begin() + (undo.size() - LIMIT));
        redo.clear();
        pending = true, dragging = continuous;
    }
    /* The operation is over and the state is NOW: when it changed nothing, it leaves no entry */
    void finish(const WbSnap &now) {
        dragging = false;
        if (!pending) return;
        pending = false;
        if (!undo.empty() && wb_same(undo.back(), now)) undo.pop_back();
    }
    /* Undo (or Redo): TO is the state to go to, NOW the one being left, which goes onto the other stack */
    bool step(bool redoing, const WbSnap &now, WbSnap &to) {
        std::vector<WbSnap> &from = redoing ? redo : undo, &other = redoing ? undo : redo;
        if (from.empty()) return false;
        to = from.back();
        from.pop_back();
        other.push_back(now);
        if (other.size() > LIMIT) other.erase(other.begin(), other.begin() + (other.size() - LIMIT));
        pending = dragging = false;
        return true;
    }

    /* the clean points: what was last loaded or saved, for each part */
    void set_clean_cand(const WbCand &c) { cleanC = c, hasClean = true; }
    void set_clean_seq(const WbSeq &s) { cleanS = s, hasClean = true; }
    int dirty(const WbSnap &now) const {
        if (!hasClean) return 0;
        return (wb_same(now.c, cleanC) ? 0 : WB_CAND) | (wb_same(now.s, cleanS) ? 0 : WB_SEQ);
    }
};

/* Undo / Redo to TO: the workbench goes back to it and B follows: the sequence's cameras again (or the candidate's when there is none now),
   the candidate's edit. The script is not touched: its progress, its waits and A stay where they are. */
static void cams_restore(View &view, const Scene &scene, ScriptRun &run, Candidate &cand, ChoreoUi &choreo, const WbSnap &to) {
    bool hadSeq = choreo.has;
    wb_put(cand, choreo, to);
    cams_choreo_refresh(view, scene, run, cand, choreo); /* with a sequence: its cameras again, B rebuilt */
    if (hadSeq && !choreo.has) cams_rebuild_b(view, scene, run, cand, choreo); /* the sequence is gone: B was showing it, now it is the candidate */
    cams_cand_edited(view, scene, run, cand, choreo);
}
