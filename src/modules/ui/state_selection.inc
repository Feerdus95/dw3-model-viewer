    int section = FILES;
    float browserW = g_cfg.bw * dpi, inspectorW = g_cfg.iw * dpi; /* 0: the first frame makes them a share of the window */
    bool showBrowser = g_cfg.browser, showInspector = g_cfg.inspector, showHints = g_cfg.hints, helpOpen = false, logOpen = false;
    int splitDrag = 0; /* the panel edge being dragged: 1 browser's, 2 inspector's */
    int hoverBone = -1;       /* under the mouse, in the view or a list */
    bool scrollToSel = false; /* the selection was made in the view: the lists bring it into sight */
    int ownFrame = -1;        /* the own images' frame picked in their table */
    std::set<std::string> stageHidden; /* the stage's parts hidden in the view, by name */
    bool partEditing = false, vpMoving = false; /* a part being moved, turned or scaled; by Ctrl+drag in the view */
    Gizmo gz;                                   /* the selected part's arrows, ring and box */
    int gzHot = -1, gzDrag = -1;                /* what is under the mouse, and what is being dragged */
    float gzAng = 0;
    bool gzAngSet = false;
    float partStart[5] = {};                    /* its move, turn and scale when that began */
    std::string partVerb;
    /* a model's pieces (bones) adjusted: each bone's mesh moved, turned or scaled about its middle, in the bone's own axes */
    struct PieceEdit {
        float move[3] = {0, 0, 0}, turn = 0, scale = 1;
        float reach = 1;     /* where the bones under this one sit from its joint (the limb's length), the mesh unchanged */
        bool joint = true;   /* the mesh scales about the bone's joint (a limb stays attached), not its own centre */
        bool follow = true;  /* a change of Scale carries Reach with it */
        bool gone = false; /* collapsed to a point: not drawn in the game */
        bool any() const { return move[0] || move[1] || move[2] || turn || scale != 1 || reach != 1 || gone; }
    };
    std::map<int, PieceEdit> charEdits;
    std::vector<std::vector<Vec3s>> charV0, charN0; /* each bone's vertices and normals as the file has them */
    std::vector<std::vector<Key>> charK0;           /* ... and its position keys */
    /* a step of undo: the edits and palette, and (an OBJ import's) the model's file as it was, with its number */
    struct CharSnap {
        std::map<int, PieceEdit> e;
        std::map<int, uint16_t> pal;
        std::shared_ptr<const std::vector<uint8_t>> file;
        int gen = 0;
    };
    std::vector<CharSnap> charUndo, charRedo;
    CharSnap charClean; /* the edits as they were when the file was last saved (or opened) ... */
    int charGen = 0, charCleanGen = 0, charGenSeq = 0; /* ... and the file's number then and now */
    std::map<int, uint16_t> charPal; /* palette colors changed, by VRAM halfword */
    std::set<int> charPalShown;      /* ... and which of them the view's VRAM has */
    bool palSession = false, palEdited = false;
    std::string charDir; /* the last OBJ export's folder */
    const Model *charOf = nullptr;
    std::string charPath;
    bool charDirty = false, charEditing = false, charMoved = false;
    float texPan[2] = {0, 0};
    bool texFitPending = true;
    double ownPushTime = -10, bgPushTime = -10; /* a drag's changes make one step of undo */
    bool dropping = false;
    uint64_t dropSeen = 0;
    bool quitAsked = false;
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s;
    };
    auto stage_on = [&]() { return view.stageView && stageId >= 0 && scene.model; };

    /* --- asking before losing work --- */
    struct Ask {
        std::string title, text, ok;
        std::function<void()> then;
        bool open = false, danger = true;
    } ask;
    auto ask_first = [&](bool dirty, const std::string &text, std::function<void()> then) {
        if (!dirty) return then();
        ask.title = "Unsaved changes", ask.text = text, ask.ok = "Discard them", ask.then = std::move(then), ask.open = true, ask.danger = true;
    };
    auto char_on = [&]() { return scene.model && !stage_on() && scene.fid > 0; };
    /* a model that isn't the one the edits are of: they're forgotten */
    auto char_sync = [&]() {
        const Model *m = scene.model.get();
        if (m == charOf && scene.path == charPath) return;
        charOf = m, charPath = scene.path, charDirty = charEditing = false;
        charEdits.clear(), charUndo.clear(), charRedo.clear(), charV0.clear(), charN0.clear(), charK0.clear(), charPal.clear(), charPalShown.clear();
        charClean = CharSnap(), charGen = charCleanGen = 0;
        palSession = false;
        if (!m) return;
        for (const Bone &b : m->bones) {
            charV0.push_back(b.mesh >= 0 ? m->meshes[b.mesh].verts : std::vector<Vec3s>());
            charN0.push_back(b.mesh >= 0 ? m->meshes[b.mesh].normals : std::vector<Vec3s>());
            charK0.push_back(b.keys[0]);
        }
    };
    auto leave_stage = [&](std::function<void()> then) {
        bool cd = charDirty && !stage_on();
        ask_first((stageDirty && stageId >= 0) || cd,
                  cd ? "This model has piece changes you haven't saved to the game. Leave it and lose them?"
                     : "Stage " + std::to_string(stageId) + " has changes you haven't saved to the game. Leave it and lose them?",
                  std::move(then));
    };
    auto leave_own = [&](std::function<void()> then) {
        ask_first(ownDirty, "Effect " + std::to_string(fx.id) + "'s own images have changes you haven't saved. Leave them and lose them?",
                  std::move(then));
    };

    /* --- what's selected: a bone of a model, or a part of a stage (its bones) --- */
    auto part_of_bone = [&](int b) -> int {
        if (!stage_on() || b < 1) return -1;
        const std::vector<int> &fb = stageMade.firstBone;
        int k = (int)(std::upper_bound(fb.begin(), fb.end(), b) - fb.begin()) - 1;
        return k >= 0 && k < (int)stageEdit.parts.size() ? k : -1;
    };
    auto part_bones = [&](int k) {
        std::vector<int> out;
        const std::vector<int> &fb = stageMade.firstBone;
        if (!stage_on() || k < 0 || k >= (int)fb.size()) return out;
        int end = k + 1 < (int)fb.size() ? fb[k + 1] : (int)scene.model->bones.size();
        for (int b = fb[k]; b < end; b++) out.push_back(b);
        return out;
    };
    auto select_part = [&](int k) {
        stagePart = k >= 0 && k < (int)stageEdit.parts.size() ? k : -1;
        if (stagePart >= 0) propSel = -1;
        scene.selected = stagePart >= 0 && stagePart < (int)stageMade.firstBone.size() ? stageMade.firstBone[stagePart] : -1;
    };
    auto select_bone = [&](int b) {
        if (stage_on()) select_part(part_of_bone(b));
        else scene.selected = b;
    };
    auto selected_bones = [&]() {
        if (stage_on()) return part_bones(stagePart);
        std::vector<int> out;
        if (scene.model && scene.selected > 0) out.push_back(scene.selected);
        return out;
    };
    /* what the lists call a bone: its part's name on a stage */
    auto thing_name = [&](int b) -> std::string {
        if (!scene.model || b < 1) return "";
        if (stage_on()) {
            int k = part_of_bone(b);
            return k >= 0 ? stageEdit.parts[k].name : "";
        }
        return "bone " + std::to_string(b);
    };
    auto thing_polys = [&](int b) -> int {
        if (!scene.model || b < 1 || b >= (int)scene.model->bones.size()) return 0;
        int k = part_of_bone(b);
        if (k >= 0) return (int)stageEdit.parts[k].faces.size();
        return (int)scene.model->meshes[scene.model->bones[b].mesh].polys.size();
    };
    auto bone_info = [&](int b) -> std::string {
        const Model &m = *scene.model;
        const Bone &bone = m.bones[b];
        const Mesh &me = m.meshes[bone.mesh];
        const Pose &p = m.pose[b];
        char t[320];
        snprintf(t, sizeof t, "%s bone %d: parent %d, mesh entry %d, keys entry %d, %d polygons, %d vertices, pos %d %d %d, rot %d %d %d, scale %d %d %d",
                 m.name.c_str(), b, bone.parent, me.entry, bone.keyEntry, (int)me.polys.size(), (int)me.verts.size(), p.pos.x, p.pos.y,
                 p.pos.z, p.rot.x, p.rot.y, p.rot.z, p.scale.x, p.scale.y, p.scale.z);
        return t;
    };
    auto reset_selection = [&]() {
        hoverBone = -1, stagePart = -1, ownFrame = -1, partEditing = vpMoving = false;
        scene.previewEnd = 0;
    };
    /* each frame: the highlights, the hidden parts and the edit's preview, for the view */
    auto mark_scene = [&]() {
        char_sync();
        if (!scene.model) return;
        int n = (int)scene.model->bones.size();
        scene.mark.assign(n, 0);
        scene.previewEnd = 0;
        bool st = stage_on();
        int hoverPart = st ? part_of_bone(hoverBone) : -1;
        for (int b = 1; b < n; b++) {
            if (st) {
                int k = part_of_bone(b);
                if (k < 0) continue;
                scene.hidden[b] = stageHidden.count(stageEdit.parts[k].name) > 0;
                scene.mark[b] = k == stagePart ? 2 : k == hoverPart ? 1 : 0;
            } else {
                scene.mark[b] = b == scene.selected ? 2 : b == hoverBone ? 1 : 0;
            }
        }
        if (st && partEditing && stagePart >= 0 && stagePart < (int)stageEdit.parts.size()) {
            std::vector<int> bs = part_bones(stagePart);
            if (!bs.empty()) {
                part_preview(stageEdit.parts[stagePart], partStart, scene.preview);
                scene.previewFirst = bs.front(), scene.previewEnd = bs.back() + 1;            }
        }
    };

