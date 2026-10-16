    /* ===== the UI: sections, selection, editing, and each section's two
       panels (the browser on the left, the inspector on the right) ===== */

    enum { FILES, STAGES, MODEL, MOTIONS, FIGHTER, EFFECTS, TEXTURES, SECTIONS };
    static const char *SECTION_NAME[SECTIONS] = {"Files", "Stages", "Model", "Motions", "Fighter", "Effects", "Textures"};
    static const char *SECTION_ICON[SECTIONS] = {IC_FILES, IC_STAGE, IC_MODEL, IC_MOTION, IC_FIGHTER, IC_EFFECT, IC_TEXTURE};
    static const char *SECTION_TIP[SECTIONS] = {
        "The game's models: Digimon, enemies and technique models",
        "The 57 fight stages: move, add and remove their parts, save them for the native port",
        "The open model's bones and how it's drawn",
        "The open model's animations",
        "Who uses the open model: cameras and action scripts",
        "The battle's 2D effects, and their own images for the native port",
        "The open model's texture pages, palettes and UVs"};
    auto needs_model = [](int s) { return s == MODEL || s == MOTIONS || s == FIGHTER || s == TEXTURES; };
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
        bool gone = false; /* collapsed to a point: not drawn in the game */
        bool any() const { return move[0] || move[1] || move[2] || turn || scale != 1 || gone; }
    };
    std::map<int, PieceEdit> charEdits;
    std::vector<std::vector<Vec3s>> charV0, charN0; /* each bone's vertices and normals as the file has them */
    std::vector<std::map<int, PieceEdit>> charUndo, charRedo;
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
        charEdits.clear(), charUndo.clear(), charRedo.clear(), charV0.clear(), charN0.clear();
        if (!m) return;
        for (const Bone &b : m->bones) {
            charV0.push_back(b.mesh >= 0 ? m->meshes[b.mesh].verts : std::vector<Vec3s>());
            charN0.push_back(b.mesh >= 0 ? m->meshes[b.mesh].normals : std::vector<Vec3s>());
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

    /* --- opening things (asking first when that loses changes) --- */
    auto after_open = [&]() {
        view.stageView = false, stageId = -1, stageDirty = false;
        reset_selection();
        texFitPending = true;
    };
    struct StageSnap {
        std::vector<StagePart> parts;
        std::shared_ptr<const std::vector<StageImage>> images; /* shared by the steps that didn't change them */
        uint8_t bg[3];
        StageInfo info;
        int part;
    };
    History<StageSnap> stageHist;
    struct OwnSnap {
        std::vector<CustomFrame> frames;
        float x, y, scale;
        bool add;
    };
    History<OwnSnap> ownHist;
    auto open_file = [&](std::string name, std::string path, int fid) {
        leave_stage([&, name, path, fid]() {
            if (!open_model(scene, view, name, path, status, fid)) return;
            after_open();
            stageHist.clear();
        });
    };
    auto open_dropped = [&](std::string path) {
        leave_stage([&, path]() {
            open_entry(path, 0);
            if (scene.model && !view.stageView) after_open(), stageHist.clear();
        });
    };
    auto open_stage = [&](int id) {
        if (id == stageId && view.stageView) return;
        leave_stage([&, id]() {
            stageHidden.clear(), stageHist.clear();
            reset_selection();
            stage_open(id);
        });
    };
    auto pick_effect = [&](int id, bool select) {
        if (id == fx.id) {
            fxSelect = fxSelect || select;
            return;
        }
        leave_own([&, id, select]() {
            ownHist.clear(), ownFrame = -1;
            open_effect(id, select);
        });
    };
    auto reload_own = [&](bool critical) {
        leave_own([&, critical]() {
            ownCritical = critical, ownHist.clear(), ownFrame = -1;
            own_load();
        });
    };
    auto pick_data_folder = [&]() {
        SDL_ShowOpenFolderDialog(
            [](void *u, const char *const *l, int) {
                if (!l || !*l) return;
                Picked *p = (Picked *)u;
                std::lock_guard<std::mutex> lock(p->m);
                p->data = *l;
            },
            &picked, win, nullptr, false);
    };
    auto pick_mods_folder = [&]() {
        SDL_ShowOpenFolderDialog(
            [](void *u, const char *const *list, int) {
                if (!list || !*list) return;
                Picked *p = (Picked *)u;
                std::lock_guard<std::mutex> lock(p->m);
                p->folder = *list;
            },
            &picked, win, ownRoot.c_str(), false);
    };
    auto pick_bin = [&]() {
        static const SDL_DialogFileFilter BIN = {"The game's model files (.BIN)", "bin"};
        SDL_ShowOpenFileDialog(
            [](void *u, const char *const *l, int) {
                if (!l || !*l) return;
                Picked *p = (Picked *)u;
                std::lock_guard<std::mutex> lock(p->m);
                p->bin = *l;
            },
            &picked, win, &BIN, 1, g_data.c_str(), false);
    };

    /* --- a model's pieces: the edits applied to the meshes in the view, and written into the model's file in place
       (it keeps its size, so the animations and scripts still fit) as the port's mod file --- */
    auto char_apply = [&]() {
        Model &m = *scene.model;
        for (size_t b = 1; b < m.bones.size() && b < charV0.size(); b++) {
            Mesh &me = m.meshes[m.bones[b].mesh];
            me.verts = charV0[b], me.normals = charN0[b];
            auto it = charEdits.find((int)b);
            if (it == charEdits.end() || charV0[b].empty()) continue;
            const PieceEdit &e = it->second;
            float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
            for (const Vec3s &v : charV0[b]) {
                float p[3] = {(float)v.x, (float)v.y, (float)v.z};
                for (int i = 0; i < 3; i++) lo[i] = std::min(lo[i], p[i]), hi[i] = std::max(hi[i], p[i]);
            }
            float c[3] = {(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2};
            float s = e.gone ? 0.0f : e.scale, cs = cosf(e.turn * 0.017453293f), sn = sinf(e.turn * 0.017453293f);
            auto fit = [](float x) { return (int16_t)std::clamp((int)lroundf(x), -32768, 32767); };
            for (Vec3s &v : me.verts) {
                float dx = (v.x - c[0]) * s, dy = (v.y - c[1]) * s, dz = (v.z - c[2]) * s;
                v = {fit(c[0] + e.move[0] + dx * cs + dz * sn), fit(c[1] + e.move[1] + dy), fit(c[2] + e.move[2] - dx * sn + dz * cs)};
            }
            for (Vec3s &n : me.normals) {
                float x = n.x, z = n.z;
                n.x = fit(x * cs + z * sn), n.z = fit(-x * sn + z * cs);
            }
        }
        /* the view keeps its camera and what's hidden */
        std::vector<bool> hidden = scene.hidden;
        float center[3] = {scene.center[0], scene.center[1], scene.center[2]}, radius = scene.radius;
        upload(scene);
        scene.hidden = hidden;
        memcpy(scene.center, center, sizeof center);
        scene.radius = radius;
    };
    auto char_mark = [&]() { /* before an edit */
        charUndo.push_back(charEdits);
        if (charUndo.size() > 40) charUndo.erase(charUndo.begin());
        charRedo.clear();
    };
    /* a change of the view's world, in the model's space, to the bone's own axes */
    auto char_local = [&](int b, const float dw[3], float out[3]) {
        const float *w = scene.model->pose[b].world;
        float d[3] = {dw[0], -dw[1], -dw[2]};
        float a = w[0], bb = w[4], c = w[8], dd = w[1], e = w[5], f = w[9], g = w[2], h = w[6], i = w[10];
        float det = a * (e * i - f * h) - bb * (dd * i - f * g) + c * (dd * h - e * g);
        out[0] = out[1] = out[2] = 0;
        if (fabsf(det) < 1e-6f) return;
        float inv[9] = {(e * i - f * h), -(bb * i - c * h), (bb * f - c * e), -(dd * i - f * g), (a * i - c * g),
                        -(a * f - c * dd), (dd * h - e * g), -(a * h - bb * g), (a * e - bb * dd)};
        for (int r = 0; r < 3; r++) out[r] = (inv[r * 3] * d[0] + inv[r * 3 + 1] * d[1] + inv[r * 3 + 2] * d[2]) / det;
    };
    auto char_save = [&]() {
        std::vector<uint8_t> bytes = scene.model->file;
        for (const auto &kv : charEdits) {
            const Mesh &me = scene.model->meshes[scene.model->bones[kv.first].mesh];
            patch_vecs(bytes, me.entry, 0, me.verts);
            patch_vecs(bytes, me.entry, 1, me.normals);
        }
        SDL_CreateDirectory(mods_root().c_str());
        std::ofstream(mod_path(scene.fid), std::ios::binary).write((const char *)bytes.data(), (std::streamsize)bytes.size());
        charDirty = false;
        note(NOTE_OK, "Saved " + scene.model->name + " to " + mod_path(scene.fid) + ": the game reads it when it starts");
    };
    auto char_mod = [&]() { return char_on() && SDL_GetPathInfo(mod_path(scene.fid).c_str(), nullptr); };
    auto ask_revert_char = [&]() {
        ask.title = "Revert to the disc's";
        ask.text = scene.model->name + " goes back to the disc's: this deletes " + mod_path(scene.fid) + ". It can't be undone.";
        ask.ok = "Revert", ask.danger = true, ask.open = true;
        ask.then = [&]() {
            int fid = scene.fid, fighter = scene.fighter;
            std::string name = scene.model->name, path = scene.path;
            SDL_RemovePath(mod_path(fid).c_str());
            charOf = nullptr;
            if (open_model(scene, view, name, path, status, fid, fighter)) after_open();
            note(NOTE_OK, name + " is the disc's again");
        };
    };

    /* --- undo: the stage's edits, the own images' edits --- */
    std::shared_ptr<const std::vector<StageImage>> stageImages;
    auto same_images = [](const std::vector<StageImage> &a, const std::vector<StageImage> &b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); i++)
            if (a[i].w != b[i].w || a[i].h != b[i].h || a[i].source != b[i].source || a[i].clutX != b[i].clutX ||
                a[i].clutY != b[i].clutY || memcmp(a[i].clut, b[i].clut, sizeof a[i].clut) || a[i].px != b[i].px || a[i].index != b[i].index)
                return false;
        return true;
    };
    auto stage_snap = [&]() {
        StageSnap s;
        s.parts = stageEdit.parts;
        if (!stageImages || !same_images(*stageImages, stageEdit.images))
            stageImages = std::make_shared<const std::vector<StageImage>>(stageEdit.images);
        s.images = stageImages;
        memcpy(s.bg, stageEdit.bg, 3);
        s.info = stageInfo, s.part = stagePart;
        return s;
    };
    auto stage_mark = [&]() { stageHist.push(stage_snap()); }; /* before an edit */
    auto stage_restore = [&](const StageSnap &s) {
        stageEdit.parts = s.parts, stageEdit.images = *s.images, stageInfo = s.info;
        memcpy(stageEdit.bg, s.bg, 3);
        for (int k = 0; k < 3; k++) view.stageBg[k] = s.bg[k] / 255.0f;
        stagePart = s.part < (int)stageEdit.parts.size() ? s.part : -1;
        stage_rebuild();
        select_part(stagePart);
    };
    auto own_snap = [&]() { return OwnSnap{own.frames, own.x, own.y, own.scale, own.add}; };
    auto own_restore = [&](const OwnSnap &s) {
        own.frames = s.frames, own.x = s.x, own.y = s.y, own.scale = s.scale, own.add = s.add;
        ownDirty = true, fxDrawn = -1;
        if (ownFrame >= (int)own.frames.size()) ownFrame = -1;
    };
    auto undo_own = [&]() { return section == EFFECTS; };
    auto can_undo = [&](bool redo) {
        if (undo_own()) return fx.id >= 0 && !(redo ? ownHist.redo : ownHist.undo).empty();
        if (section == MODEL && char_on()) return !(redo ? charRedo : charUndo).empty();
        return stage_on() && !(redo ? stageHist.redo : stageHist.undo).empty();
    };
    auto do_undo = [&](bool redo) {
        if (!can_undo(redo)) return;
        if (undo_own()) {
            std::vector<OwnSnap> &from = redo ? ownHist.redo : ownHist.undo, &to = redo ? ownHist.undo : ownHist.redo;
            to.push_back(own_snap());
            OwnSnap s = std::move(from.back());
            from.pop_back();
            own_restore(s);
        } else if (section == MODEL && char_on()) {
            auto &from = redo ? charRedo : charUndo, &to = redo ? charUndo : charRedo;
            to.push_back(charEdits);
            charEdits = std::move(from.back());
            from.pop_back();
            char_apply();
            charDirty = true;
        } else {
            std::vector<StageSnap> &from = redo ? stageHist.redo : stageHist.undo, &to = redo ? stageHist.undo : stageHist.redo;
            to.push_back(stage_snap());
            StageSnap s = std::move(from.back());
            from.pop_back();
            stage_restore(s);
        }
    };

    /* --- the stage's parts --- */
    StageEdit partClip; /* a part copied: partClip.parts[0], fixed where it stood */
    int partClipStage = -1;
    auto part_name = [&](int k) { return k >= 0 && k < (int)stageEdit.parts.size() ? stageEdit.parts[k].name : std::string(); };
    /* an edit of the selected part begins: undo gets where it was (BEFORE: its move, turn and scale) */
    auto part_begin = [&](const char *verb, const float before[5]) {
        if (stagePart < 0) return;
        stage_mark();
        StagePart &old = stageHist.undo.back().parts[stagePart];
        memcpy(old.move, before, 12), old.turn = before[3], old.scale = before[4];
        memcpy(partStart, before, sizeof partStart);
        partVerb = verb, partEditing = true;
    };
    auto part_end = [&](bool edited) {
        partEditing = vpMoving = false;
        if (edited) stage_rebuild();
        else if (!stageHist.undo.empty()) stageHist.undo.pop_back();
    };
    auto part_place = [&](int k, float out[5]) {
        const StagePart &p = stageEdit.parts[k];
        out[0] = p.move[0], out[1] = p.move[1], out[2] = p.move[2], out[3] = p.turn, out[4] = p.scale;
    };
    auto part_delete = [&](int k) {
        if (k < 0 || k >= (int)stageEdit.parts.size()) return;
        std::string n = part_name(k);
        stage_mark();
        stageEdit.remove(k);
        stagePart = -1;
        if (stage_rebuild()) note(NOTE_INFO, "Removed " + n + " (Ctrl+Z brings it back)");
    };
    auto part_copy = [&](int k) {
        if (k < 0 || k >= (int)stageEdit.parts.size()) return;
        partClip = StageEdit();
        partClip.append(stageEdit, k, "");
        partClipStage = stageId;
        ImGui::SetClipboardText(part_name(k).c_str());
        note(NOTE_INFO, "Copied " + part_name(k) + ": Ctrl+V pastes it here or in another stage");
    };
    auto part_paste = [&]() {
        if (partClip.parts.empty() || !stage_on()) return;
        stage_mark();
        char pre[8] = "";
        if (partClipStage != stageId) snprintf(pre, sizeof pre, "s%02d_", partClipStage);
        stageEdit.append(partClip, 0, pre);
        if (partClipStage == stageId) stageEdit.parts.back().move[0] += 300; /* beside the one it copies */
        stagePart = (int)stageEdit.parts.size() - 1;
        if (stage_rebuild()) note(NOTE_OK, "Pasted " + part_name(stagePart));
        scrollToSel = true;
    };
    auto part_duplicate = [&](int k) {
        if (k < 0 || k >= (int)stageEdit.parts.size()) return;
        stage_mark();
        stageEdit.append(stageEdit, k, "");
        stageEdit.parts.back().move[0] += 300;
        stagePart = (int)stageEdit.parts.size() - 1;
        if (stage_rebuild()) note(NOTE_OK, "Duplicated as " + part_name(stagePart));
        scrollToSel = true;
    };
    auto part_hide = [&](int k) {
        std::string n = part_name(k);
        if (!stageHidden.erase(n)) stageHidden.insert(n);
    };
    auto part_bounds = [&](int k) {
        stage_mark();
        stageEdit.parts[k].noBounds = !stageEdit.parts[k].noBounds;
        stage_rebuild();
    };

    /* --- the model's bones, and the view --- */
    auto show_all = [&]() {
        if (stage_on()) stageHidden.clear();
        else if (scene.model) std::fill(scene.hidden.begin(), scene.hidden.end(), false);
    };
    auto show_only = [&](int b) {
        if (stage_on()) {
            int k = part_of_bone(b);
            stageHidden.clear();
            for (int i = 0; i < (int)stageEdit.parts.size(); i++)
                if (i != k) stageHidden.insert(stageEdit.parts[i].name);
        } else if (scene.model) {
            for (size_t i = 1; i < scene.hidden.size(); i++) scene.hidden[i] = (int)i != b;
        }
    };
    auto hide_selected = [&]() {
        if (stage_on() && stagePart >= 0) part_hide(stagePart);
        else if (scene.model && scene.selected > 0) scene.hidden[scene.selected] = !scene.hidden[scene.selected];
    };
    auto frame_all = [&]() {
        if (section == EFFECTS) fxFitPending = true;
        else if (section == TEXTURES) texFitPending = true;
        else if (scene.model) view.stageView ? frame_stage(view) : frame_model(scene, view);
    };
    auto frame_sel = [&]() {
        std::vector<int> bs = section == EFFECTS || section == TEXTURES ? std::vector<int>() : selected_bones();
        if (bs.empty() || !frame_bones(scene, view, bs)) frame_all();
    };

    /* --- the right-click menus --- */
    auto menu_title = [&](const std::string &name, const char *fmt, ...) {
        ImGui::PushFont(g_fontBold);
        ImGui::TextColored(C_SELECT, "%s", name.c_str());
        ImGui::PopFont();
        va_list a;
        va_start(a, fmt);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextV(fmt, a);
        ImGui::PopStyleColor();
        va_end(a);
        ImGui::Separator();
    };
    auto part_menu = [&](int k) {
        if (k < 0 || k >= (int)stageEdit.parts.size()) return;
        menu_title(part_name(k), "part %d, %d polygons", k, (int)stageEdit.parts[k].faces.size());
        if (ImGui::MenuItem("Select", nullptr, stagePart == k)) return select_part(k);
        if (ImGui::MenuItem(IC_FIT "  Frame it", "F")) return select_part(k), frame_sel();
        bool hidden = stageHidden.count(part_name(k)) > 0;
        if (ImGui::MenuItem(hidden ? IC_EYE "  Show in the view" : IC_HIDE "  Hide in the view", "H")) return part_hide(k);
        if (ImGui::MenuItem("Show only this")) return show_only(stageMade.firstBone[k]);
        if (ImGui::MenuItem("Show all", "Alt+H")) return show_all();
        ImGui::Separator();
        if (ImGui::MenuItem(IC_COPY "  Copy", "Ctrl+C")) return part_copy(k);
        if (ImGui::MenuItem(IC_PASTE "  Paste", "Ctrl+V", false, !partClip.parts.empty())) return part_paste();
        if (ImGui::MenuItem(IC_ADD "  Duplicate", "Ctrl+D")) return part_duplicate(k);
        if (ImGui::MenuItem("Always drawn (no bounds check)", nullptr, stageEdit.parts[k].noBounds)) return part_bounds(k);
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, C_ERROR);
        bool del = ImGui::MenuItem(IC_DELETE "  Delete", "Del");
        ImGui::PopStyleColor();
        if (del) part_delete(k);
    };
    auto bone_menu = [&](int b) {
        if (stage_on()) return part_menu(part_of_bone(b));
        if (!scene.model || b < 1 || b >= (int)scene.model->bones.size()) return;
        menu_title(thing_name(b), "%d polygons", thing_polys(b));
        if (ImGui::MenuItem("Select", nullptr, scene.selected == b)) return void(scene.selected = b);
        if (ImGui::MenuItem(IC_FIT "  Frame it", "F")) return void(scene.selected = b), frame_sel();
        if (ImGui::MenuItem(scene.hidden[b] ? IC_EYE "  Show" : IC_HIDE "  Hide", "H")) return void(scene.hidden[b] = !scene.hidden[b]);
        if (ImGui::MenuItem("Show only this")) return show_only(b);
        if (ImGui::MenuItem("Show all", "Alt+H")) return show_all();
        ImGui::Separator();
        if (ImGui::MenuItem(IC_COPY "  Copy its details", "Ctrl+C")) ImGui::SetClipboardText(bone_info(b).c_str());
    };
    auto view_menu = [&]() {
        if (ImGui::MenuItem(IC_FIT "  Frame all", "F")) frame_all();
        if (scene.model && section != EFFECTS && section != TEXTURES) {
            if (ImGui::MenuItem(IC_EYE "  Show all", "Alt+H")) show_all();
            if (stage_on() && ImGui::MenuItem(IC_PASTE "  Paste part", "Ctrl+V", false, !partClip.parts.empty())) part_paste();
            if (view.gameCamera >= 0 && ImGui::MenuItem(IC_CAMERA "  Free camera")) frame_model(scene, view);
            ImGui::Separator();
            if (!view.stageView) ImGui::MenuItem("Grid", nullptr, &view.grid);
            ImGui::MenuItem("Skeleton", nullptr, &view.skeleton);
            ImGui::MenuItem("Wireframe", nullptr, &view.wireframe);
            ImGui::MenuItem("Back-face culling", nullptr, &view.cull);
        }
        ImGui::Separator();
        ImGui::MenuItem("Controls hint", nullptr, &showHints);
    };

    /* --- Edit's actions, for the menu and the keys --- */
    std::vector<CustomFrame> frameClip;
    std::string frameClipDir;
    auto frame_ok = [&]() { return fx.id >= 0 && ownFrame >= 0 && ownFrame < (int)own.frames.size(); };
    auto frame_paste = [&]() {
        if (frameClip.empty() || fx.id < 0) return;
        if (frameClipDir != own.dir) return note(NOTE_WARN, "Frames paste within one effect's images: they name its PNGs");
        ownHist.push(own_snap());
        int at = frame_ok() ? ownFrame + 1 : (int)own.frames.size();
        own.frames.insert(own.frames.begin() + at, frameClip.begin(), frameClip.end());
        ownFrame = at, ownDirty = true, fxDrawn = -1;
    };
    auto frame_edit = [&](int i, int what) { /* 0 delete, 1 duplicate, -2/2 up/down */
        if (i < 0 || i >= (int)own.frames.size()) return;
        int to = i + what / 2;
        if (what && what != 1 && (to < 0 || to >= (int)own.frames.size())) return;
        ownHist.push(own_snap());
        if (what == 0) own.frames.erase(own.frames.begin() + i), ownFrame = -1;
        else if (what == 1) own.frames.insert(own.frames.begin() + i + 1, own.frames[i]), ownFrame = i + 1;
        else std::swap(own.frames[i], own.frames[to]), ownFrame = to;
        ownDirty = true, fxDrawn = -1;
    };
    auto can_paste = [&]() { return section == EFFECTS ? !frameClip.empty() && fx.id >= 0 : stage_on() && !partClip.parts.empty(); };
    auto can_edit_sel = [&]() { return section == EFFECTS ? frame_ok() : stage_on() && stagePart >= 0; };
    auto do_copy = [&]() {
        if (section == EFFECTS) {
            if (frame_ok()) {
                frameClip = {own.frames[ownFrame]}, frameClipDir = own.dir;
                note(NOTE_INFO, "Copied frame " + std::to_string(ownFrame));
            } else if (fx.id >= 0) {
                ImGui::SetClipboardText(("effect " + std::to_string(fx.id)).c_str());
                note(NOTE_INFO, "Copied \"effect " + std::to_string(fx.id) + "\"");
            }
        } else if (stage_on() && stagePart >= 0) {
            part_copy(stagePart);
        } else if (scene.model && !stage_on() && scene.selected > 0) {
            ImGui::SetClipboardText(bone_info(scene.selected).c_str());
            note(NOTE_INFO, "Copied bone " + std::to_string(scene.selected) + "'s details");
        } else if (scene.model) {
            std::string t = scene.model->name + (scene.fid >= 0 ? " #" + std::to_string(scene.fid) : "");
            ImGui::SetClipboardText(t.c_str());
            note(NOTE_INFO, "Copied \"" + t + "\"");
        }
    };
    auto do_paste = [&]() { section == EFFECTS ? frame_paste() : part_paste(); };
    auto do_duplicate = [&]() { section == EFFECTS ? frame_edit(ownFrame, 1) : part_duplicate(stagePart); };
    auto do_delete = [&]() { section == EFFECTS ? frame_edit(ownFrame, 0) : part_delete(stagePart); };
    auto own_save = [&]() {
        if (own.save()) ownDirty = false, ownIds = own_effects(ownRoot), note(NOTE_OK, "Saved " + own.dir + "/effect.json");
        else note(NOTE_ERROR, own.error);
    };
    auto can_save = [&]() { return section == EFFECTS ? fx.id >= 0 && !own.frames.empty() : stage_on() || (char_on() && charDirty); };
    auto do_save = [&]() {
        if (!can_save()) return;
        if (char_on() && section != EFFECTS) return char_save();
        section == EFFECTS ? own_save() : stage_save();
    };
    auto ask_revert = [&]() {
        ask.title = "Revert to the disc's";
        ask.text = "Stage " + std::to_string(stageId) + " goes back to the disc's: this deletes " + mod_path(stageInfo.fid) +
                   " and puts its line of the stages' table back. It can't be undone.";
        ask.ok = "Revert", ask.danger = true, ask.open = true;
        ask.then = [&]() {
            stageHidden.clear(), stageHist.clear();
            reset_selection();
            int id = stageId;
            stage_revert();
            note(NOTE_OK, "Stage " + std::to_string(id) + " is the disc's again");
        };
    };

    /* --- the panels' parts shared by sections --- */
    auto panel_title = [&](const char *icon, const char *title) {
        ImGui::PushFont(g_fontBold);
        ImGui::TextColored(C_ACCENT, "%s", icon);
        ImGui::SameLine();
        ImGui::TextUnformatted(title);
        ImGui::PopFont();
        ImGui::Separator();
    };
    auto title = [&](const char *fmt, ...) {
        va_list a;
        va_start(a, fmt);
        ImGui::PushFont(g_fontTitle);
        ImGui::TextWrappedV(fmt, a);
        ImGui::PopFont();
        va_end(a);
    };
    auto display_options = [&]() {
        float lw = ImGui::GetFontSize() * 5;
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Textures");
        ImGui::SameLine(lw);
        segmented("tex", &view.textures, {"Off", "PSX", "Bilinear"});
        tip("PSX: the console's sharp texels. Bilinear: smoothed.");
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Lights");
        ImGui::SameLine(lw);
        segmented("light", &view.lighting, {"None", "Viewer", "Stage"});
        tip("Viewer: a light over the camera's shoulder. Stage: a fight stage's three lights and ambient color.");
        if (view.lighting == 2 && !g_game.stages.empty()) {
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderInt("##fightstage", &view.stage, 0, (int)g_game.stages.size() - 1, "fight stage %d");
            tip("Whose lights (SFSTDATA.PRO)");
        }
        if (ImGui::BeginTable("display", 2)) {
            ImGui::TableNextColumn();
            ImGui::Checkbox("Face", &view.face);
            tip("The blinking eyes and the other animated face parts");
            ImGui::TableNextColumn();
            ImGui::Checkbox("Wireframe", &view.wireframe);
            ImGui::TableNextColumn();
            ImGui::Checkbox("Skeleton", &view.skeleton);
            ImGui::TableNextColumn();
            ImGui::Checkbox("Grid", &view.grid);
            ImGui::EndTable();
        }
        ImGui::Checkbox("Back-face culling", &view.cull);
        if (ImGui::Button(IC_EYE "  Show all")) show_all();
        tip("Shows the hidden bones again (Alt+H)");
        same_line_if_fits(IC_FIT "  Frame");
        if (ImGui::Button(IC_FIT "  Frame")) frame_sel();
        tip("The camera on the selection, or on everything (F)");
    };

    /* ===== Files ===== */
    auto files_browser = [&]() {
        if (!listed) list = scan_models(), listed = true;
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##filter", IC_SEARCH "  Search: name, file or number", filter, sizeof filter);
        if (list.empty()) {
            ImGui::Spacing();
            hint("No models found in %s.", g_data.c_str());
            hint("Run tools/setup_disc.py with your own disc image, then choose the data folder it made.");
            if (button(IC_FOLDER "  Choose the data folder...", 1)) pick_data_folder();
            return;
        }
        std::string f = lower(filter);
        if (ImGui::BeginTable("files", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.7f);
            ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthStretch, 0.45f);
            ImGui::TableHeadersRow();
            for (const Entry &en : list) {
                if (stageFids.count(en.fid)) continue; /* in Stages */
                if (!f.empty() && lower(en.label + " " + en.name + " " + std::to_string(en.fid)).find(f) == std::string::npos) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(en.fid);
                bool sel = scene.model && !view.stageView && scene.fid == en.fid;
                const std::string &shown = en.label.empty() ? en.name : en.label;
                if (ImGui::Selectable(shown.c_str(), sel, ImGuiSelectableFlags_SpanAllColumns) && !sel)
                    open_file(en.name, fid_path(en.fid), en.fid);
                if (ImGui::BeginPopupContextItem("##menu")) {
                    menu_title(shown, "%s, file %d", en.name.c_str(), en.fid);
                    if (ImGui::MenuItem(IC_OPEN "  Open")) open_file(en.name, fid_path(en.fid), en.fid);
                    if (ImGui::MenuItem(IC_COPY "  Copy the name")) ImGui::SetClipboardText(shown.c_str());
                    if (ImGui::MenuItem(IC_COPY "  Copy the file's name")) ImGui::SetClipboardText(en.name.c_str());
                    if (ImGui::MenuItem(IC_COPY "  Copy the file's number")) ImGui::SetClipboardText(std::to_string(en.fid).c_str());
                    ImGui::EndPopup();
                }
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", en.name.c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%d", en.fid);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    };
    auto files_inspector = [&]() {
        if (scene.model && !view.stageView) {
            Model &m = *scene.model;
            const Fighter *fi = scene.fighters.empty() ? nullptr : scene.fighters[scene.fighter];
            title("%s", fi && !fi->name.empty() ? fi->name.c_str() : m.name.c_str());
            if (fi) {
                chip(fi->enemy ? "enemy" : "partner", fi->enemy ? C_ERROR : C_OK);
                ImGui::SameLine();
            }
            chip(m.name.c_str(), C_DIM);
            size_t polys = 0, verts = 0;
            for (const Mesh &me : m.meshes) polys += me.polys.size(), verts += me.verts.size();
            ImGui::Spacing();
            field("Bones", "%d", (int)m.bones.size() - 1);
            field("Polygons", "%d", (int)polys);
            field("Vertices", "%d", (int)verts);
            field("Motions", "%d", (int)m.motions.size());
            field("Textures", "%d TIMs", (int)m.tims.size());
            if (scene.fid >= 0) field("File number", "%d", scene.fid);
            heading("Look closer");
            float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
            for (int s : {MODEL, MOTIONS, FIGHTER, TEXTURES}) {
                if (s == MOTIONS || s == TEXTURES) ImGui::SameLine();
                std::string l = std::string(SECTION_ICON[s]) + "  " + SECTION_NAME[s];
                if (ImGui::Button(l.c_str(), ImVec2(w, 0))) section = s;
                tip("%s", SECTION_TIP[s]);
            }
        } else {
            title("No model open");
            hint("Pick one on the left, or drop a .BIN file of the game on the window (Ctrl+O opens one).");
        }
        heading("Game data");
        hint("Read from your own copy of the game, as tools/setup_disc.py extracted it:");
        ImGui::PushFont(g_fontMono);
        ImGui::TextWrapped("%s", nice_path(g_data.substr(0, g_data.size() >= 6 ? g_data.size() - 6 : 0)).c_str());
        ImGui::PopFont();
        if (ImGui::Button(IC_FOLDER "  Change the data folder...")) pick_data_folder();
        tip("The folder tools/setup_disc.py made, with names.txt in it");
    };

    /* ===== Stages ===== */
    auto stages_browser = [&]() {
        float h = stage_on() ? ImGui::GetContentRegionAvail().y * 0.42f : 0;
        if (ImGui::BeginTable("stages", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit, ImVec2(0, h))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("#");
            ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("State");
            ImGui::TableHeadersRow();
            for (const StageInfo &si : stageInfos) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(si.id);
                char l[8];
                snprintf(l, sizeof l, "%2d", si.id);
                bool sel = si.id == stageId && view.stageView;
                if (ImGui::Selectable(l, sel, ImGuiSelectableFlags_SpanAllColumns) && !sel) open_stage(si.id);
                if (ImGui::BeginPopupContextItem("##menu")) {
                    menu_title("Stage " + std::to_string(si.id), "%s", stage_look(si.id));
                    if (ImGui::MenuItem(IC_OPEN "  Open")) open_stage(si.id);
                    if (ImGui::MenuItem(IC_COPY "  Copy its name"))
                        ImGui::SetClipboardText(("stage " + std::to_string(si.id) + " " + stage_look(si.id)).c_str());
                    ImGui::EndPopup();
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(stage_look(si.id));
                ImGui::TableNextColumn();
                if (sel && stageDirty) ImGui::TextColored(C_WARN, "unsaved");
                else if (stageMods.count(si.fid)) ImGui::TextColored(C_ACCENT, "mod");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (!stage_on()) return;
        char head[48];
        snprintf(head, sizeof head, "Parts (%d)", (int)stageEdit.parts.size());
        heading(head);
        if (ImGui::BeginTable("parts", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("##eye");
            ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##always");
            ImGui::TableSetupColumn("##polys");
            for (int k = 0; k < (int)stageEdit.parts.size(); k++) {
                const StagePart &pt = stageEdit.parts[k];
                ImGui::TableNextRow();
                ImGui::PushID(k);
                ImGui::TableNextColumn();
                bool shown = !stageHidden.count(pt.name);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(shown ? ImGuiCol_Text : ImGuiCol_TextDisabled));
                if (ImGui::SmallButton(shown ? IC_EYE : IC_HIDE)) part_hide(k);
                ImGui::PopStyleColor(2);
                tip(shown ? "Shown in the view: click to hide it there (the game still draws it)" : "Hidden in the view: click to show it");
                ImGui::TableNextColumn();
                if (ImGui::Selectable(pt.name.c_str(), stagePart == k, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
                    select_part(stagePart == k ? -1 : k);
                if (ImGui::IsItemHovered() && k < (int)stageMade.firstBone.size()) hoverBone = stageMade.firstBone[k];
                if (stagePart == k && scrollToSel) ImGui::SetScrollHereY();
                size_t count = stageEdit.parts.size();
                if (ImGui::BeginPopupContextItem("##menu")) {
                    part_menu(k);
                    ImGui::EndPopup();
                }
                if (stageEdit.parts.size() != count) { /* the menu added or removed one: pt is gone */
                    ImGui::PopID();
                    break;
                }
                ImGui::TableNextColumn();
                if (pt.noBounds) {
                    ImGui::TextColored(C_ACCENT, IC_OK);
                    tip("Always drawn (no bounds check)");
                }
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%d polys", (int)pt.faces.size());
                ImGui::PopID();
                if (k >= (int)stageEdit.parts.size()) break; /* a menu removed it */
            }
            ImGui::EndTable();
        }
    };
    /* --- the catalog: every part of the 57 stages as a picture, looks that repeat listed once ---
       Read all at once when it first opens; each stage's pictures are drawn the first time one of its parts
       shows (one stage a frame), by the scene's own renderer into a small texture. */
    struct CatPart {
        int stage = 0, part = 0, polys = 0;
        std::string name;
        std::vector<int> also; /* the other stages with the same look */
        GLuint tex = 0;
    };
    std::vector<CatPart> cat;
    bool catOpen = false, catBuilt = false;
    char catFind[64] = "";
    int catStage = -1, catPick = -1; /* the stage the list is limited to; the picked part */
    std::set<int> catDone;           /* the stages whose pictures are made */
    GLuint catFbo = 0, catDepth = 0;
    const int CAT_T = 128;
    auto cat_free = [&]() {
        for (CatPart &c : cat)
            if (c.tex) GL(glDeleteTextures)(1, &c.tex);
        cat.clear(), catDone.clear();
        catBuilt = false, catPick = -1;
    };
    auto cat_build = [&]() {
        cat_free();
        catBuilt = true;
        auto fnv = [](uint64_t h, const void *p, size_t n) {
            const uint8_t *b = (const uint8_t *)p;
            for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ull;
            return h;
        };
        const uint64_t BASIS = 14695981039346656037ull;
        std::map<uint64_t, int> seen;
        for (const StageInfo &o : stageInfos) {
            StageEdit e;
            StageInfo oi;
            std::vector<uint8_t> ob;
            if (!stage_load(o.id, e, oi, ob)) continue;
            std::vector<uint64_t> ih;
            for (const StageImage &im : e.images) ih.push_back(fnv(BASIS, im.px.data(), im.px.size() * 4));
            for (int k = 0; k < (int)e.parts.size(); k++) {
                const StagePart &p = e.parts[k];
                if (p.faces.empty() || p.verts.empty()) continue;
                /* the shape (wherever it stands), what its faces use of it, and the pictures they read */
                float lo[3] = {1e30f, 1e30f, 1e30f};
                for (const auto &v : p.verts)
                    for (int i = 0; i < 3; i++) lo[i] = std::min(lo[i], v[i]);
                uint64_t h = BASIS;
                for (const auto &v : p.verts)
                    for (int i = 0; i < 3; i++) {
                        int q = (int)lroundf(v[i] - lo[i]);
                        h = fnv(h, &q, 4);
                    }
                std::set<int> imgs;
                for (const StageFace &f : p.faces) {
                    h = fnv(h, f.v, sizeof f.v), h = fnv(h, &f.abr, 4);
                    imgs.insert(f.image);
                }
                for (int i : imgs)
                    if (i >= 0 && i < (int)ih.size()) h = fnv(h, &ih[i], 8);
                auto it = seen.find(h);
                if (it != seen.end()) {
                    cat[it->second].also.push_back(o.id);
                    continue;
                }
                seen[h] = (int)cat.size();
                CatPart c;
                c.stage = o.id, c.part = k, c.polys = (int)p.faces.size(), c.name = p.name;
                cat.push_back(c);
            }
        }
    };
    /* all the pictures of stage ID: its model on the card with one part showing at a time */
    auto cat_pictures = [&](int id) {
        catDone.insert(id);
        StageEdit e;
        StageInfo oi;
        std::vector<uint8_t> bytes;
        if (!stage_load(id, e, oi, bytes)) return;
        Scene sc;
        sc.model = std::make_unique<Model>();
        if (!sc.model->load(fid_name(oi.fid), bytes, nullptr, oi.boneEntry)) return;
        upload(sc);
        if (!catFbo) {
            GL(glGenFramebuffers)(1, &catFbo);
            GL(glGenRenderbuffers)(1, &catDepth);
            GL(glBindRenderbuffer)(GL_RENDERBUFFER, catDepth);
            GL(glRenderbufferStorage)(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, CAT_T, CAT_T);
            GL(glBindFramebuffer)(GL_FRAMEBUFFER, catFbo);
            GL(glFramebufferRenderbuffer)(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, catDepth);
        }
        View v;
        v.grid = false, v.lighting = 2, v.stage = id;
        for (CatPart &c : cat) {
            int b = c.part + 1;
            if (c.stage != id || b >= (int)sc.model->bones.size()) continue;
            std::fill(sc.hidden.begin(), sc.hidden.end(), true);
            sc.hidden[b] = false;
            if (!frame_bones(sc, v, {b})) continue;
            v.cam.yaw = 0.6f, v.cam.pitch = 0.35f;
            GL(glGenTextures)(1, &c.tex);
            GL(glBindTexture)(GL_TEXTURE_2D, c.tex);
            GL(glTexImage2D)(GL_TEXTURE_2D, 0, GL_RGBA8, CAT_T, CAT_T, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            GL(glBindFramebuffer)(GL_FRAMEBUFFER, catFbo);
            GL(glFramebufferTexture2D)(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c.tex, 0);
            render(sc, v, 0, 0, CAT_T, CAT_T);
        }
        GL(glBindFramebuffer)(GL_FRAMEBUFFER, 0);
        GL(glDeleteBuffers)(1, &sc.vbo);
        GL(glDeleteVertexArrays)(1, &sc.vao);
        GL(glDeleteTextures)(1, &sc.vram);
    };
    auto cat_add = [&](int i) {
        if (!stage_on() || i < 0 || i >= (int)cat.size()) return;
        StageInfo oi;
        std::vector<uint8_t> ob;
        if (!stage_load(cat[i].stage, stageFrom, oi, ob)) return;
        char pre[8];
        snprintf(pre, sizeof pre, "s%02d_", cat[i].stage);
        stage_mark();
        stageEdit.append(stageFrom, cat[i].part, pre);
        stagePart = (int)stageEdit.parts.size() - 1;
        if (stage_rebuild()) note(NOTE_OK, "Added " + part_name(stagePart));
        scrollToSel = true;
    };
    auto catalog_window = [&]() {
        if (!catOpen) return;
        ImVec2 ds = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowSize(ImVec2(std::min(ds.x - 40 * dpi, 900 * dpi), std::min(ds.y - 60 * dpi, 680 * dpi)), ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (!ImGui::Begin(IC_STAGE "  Parts of the stages###catalog", &catOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::End();
            return;
        }
        if (!catBuilt) cat_build();
        auto lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            return s;
        };
        /* the search and the stage it is limited to */
        float cw = ImGui::GetContentRegionAvail().x;
        ImGui::SetNextItemWidth(cw * 0.55f);
        ImGui::InputTextWithHint("##catfind", IC_SEARCH " Search a part, a stage or what it shows", catFind, sizeof catFind);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        std::string lim = catStage < 0 ? "All stages" : std::to_string(catStage) + "  " + stage_look(catStage);
        if (ImGui::BeginCombo("##catstage", lim.c_str())) {
            if (ImGui::Selectable("All stages", catStage < 0)) catStage = -1;
            for (const StageInfo &o : stageInfos) {
                std::string l = std::to_string(o.id) + "  " + stage_look(o.id);
                if (ImGui::Selectable(l.c_str(), catStage == o.id)) catStage = o.id;
            }
            ImGui::EndCombo();
        }
        std::vector<int> shown;
        std::string q = lower(catFind);
        for (int i = 0; i < (int)cat.size(); i++) {
            const CatPart &c = cat[i];
            if (catStage >= 0 && c.stage != catStage && std::find(c.also.begin(), c.also.end(), catStage) == c.also.end()) continue;
            if (!q.empty() && lower(c.name + " " + std::to_string(c.stage) + " " + stage_look(c.stage)).find(q) == std::string::npos) continue;
            shown.push_back(i);
        }
        /* the pictures */
        const ImGuiStyle &sty = ImGui::GetStyle();
        float pic = 104 * dpi, lineH = ImGui::GetTextLineHeight();
        ImVec2 cell(pic + 12 * dpi, pic + lineH * 2 + 14 * dpi);
        float foot = ImGui::GetFrameHeightWithSpacing() * 2 + lineH * 2 + sty.ItemSpacing.y * 3;
        ImGui::BeginChild("##catgrid", ImVec2(0, -foot), ImGuiChildFlags_Borders);
        int cols = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (cell.x + sty.ItemSpacing.x)));
        int rows = ((int)shown.size() + cols - 1) / cols, want = -1, addNow = -1;
        ImGuiListClipper clip;
        clip.Begin(rows, cell.y + sty.ItemSpacing.y);
        while (clip.Step()) {
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++) {
                for (int c = 0; c < cols; c++) {
                    int n = r * cols + c;
                    if (n >= (int)shown.size()) break;
                    int i = shown[n];
                    const CatPart &cp = cat[i];
                    ImGui::PushID(i);
                    if (c) ImGui::SameLine();
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    if (ImGui::Selectable("##c", catPick == i, 0, cell)) catPick = i;
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) addNow = i;
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    ImVec2 a = p + ImVec2(6 * dpi, 4 * dpi), b = a + ImVec2(pic, pic);
                    if (cp.tex) {
                        dl->AddImage((ImTextureID)(intptr_t)cp.tex, a, b, ImVec2(0, 1), ImVec2(1, 0));
                    } else {
                        dl->AddRectFilled(a, b, IM_COL32(40, 44, 52, 255));
                        if (!catDone.count(cp.stage)) want = want < 0 ? cp.stage : want;
                        else dl->AddText(a + ImVec2(8 * dpi, pic / 2 - lineH / 2), IM_COL32(130, 135, 145, 255), "no picture");
                    }
                    ImGui::PushClipRect(a - ImVec2(0, 0), p + ImVec2(cell.x - 2 * dpi, cell.y), true);
                    dl->AddText(ImVec2(a.x, b.y + 3 * dpi), ImGui::GetColorU32(ImGuiCol_Text), cp.name.c_str());
                    char sub[48];
                    snprintf(sub, sizeof sub, "stage %d, %d polys", cp.stage, cp.polys);
                    dl->AddText(ImVec2(a.x, b.y + 3 * dpi + lineH), ImGui::GetColorU32(ImGuiCol_TextDisabled), sub);
                    ImGui::PopClipRect();
                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::Text("%s", cp.name.c_str());
                        ImGui::TextDisabled("Stage %d: %s", cp.stage, stage_look(cp.stage));
                        ImGui::TextDisabled("%d polygons. Double-click adds it.", cp.polys);
                        ImGui::EndTooltip();
                    }
                    ImGui::PopID();
                }
            }
        }
        if (shown.empty()) hint("%s", cat.empty() ? "No stage could be read." : "No part matches.");
        ImGui::EndChild();
        if (want >= 0) cat_pictures(want); /* a stage a frame */
        /* what's picked */
        if (catPick >= 0 && catPick < (int)cat.size()) {
            const CatPart &c = cat[catPick];
            std::string also;
            for (size_t k = 0; k < c.also.size() && k < 12; k++) also += (k ? ", " : "") + std::to_string(c.also[k]);
            if (c.also.size() > 12) also += ", ...";
            ImGui::PushFont(g_fontBold);
            ImGui::TextColored(C_SELECT, "%s", c.name.c_str());
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::TextDisabled("%d polygons, stage %d: %s", c.polys, c.stage, stage_look(c.stage));
            if (!also.empty()) hint("The same look is in stage %s too.", also.c_str());
            else hint("Only in this stage.");
        } else {
            hint("Pick a part: a click shows where it is from, a double-click adds it to the open stage.");
            ImGui::Dummy(ImVec2(0, lineH));
        }
        ImGui::BeginDisabled(catPick < 0 || !stage_on());
        if (button(IC_ADD "  Add to the stage", 1)) addNow = catPick;
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (stage_on()) ImGui::TextDisabled("It joins stage %d where it stood in its own; move it with the gizmo.", stageId);
        else ImGui::TextDisabled("Open a stage first.");
        if (addNow >= 0) cat_add(addNow);
        ImGui::End();
    };
    auto stages_inspector = [&]() {
        if (!stage_on()) {
            title("No stage open");
            hint("Open one of the 57 fight stages on the left. You can move, turn and scale its parts, add parts from "
                 "other stages, change its background, swap it through Blender (OBJ), and save it for the native port.");
            return;
        }
        const StageInfo &si = stageInfos[stageId];
        title("Stage %d", stageId);
        ImGui::TextWrapped("%s", stage_look(stageId));
        if (stageDirty) chip(IC_WARN " Unsaved changes", C_WARN);
        else if (stageMods.count(si.fid)) chip("Modded: saved for the game", C_ACCENT);
        else chip("The disc's, unchanged", C_DIM);
        ImGui::Spacing();
        if (button(IC_SAVE "  Save to game", 1)) stage_save();
        tip("Writes the stage's model and its line of the stages' table (0459.bin) to the mods folder, which the native "
            "port reads instead of the disc's (Ctrl+S)");
        if (stageMods.count(si.fid) || stageDirty) {
            same_line_if_fits(IC_RESTART "  Revert to the disc's");
            if (button(IC_RESTART "  Revert to the disc's", 2)) ask_revert();
            tip("Deletes the mod: the stage is the disc's again");
        }
        hint("Saves to %s", nice_path(mod_path(si.fid)).c_str());
        float bgc[3] = {stageEdit.bg[0] / 255.0f, stageEdit.bg[1] / 255.0f, stageEdit.bg[2] / 255.0f};
        if (ImGui::ColorEdit3("Background", bgc, ImGuiColorEditFlags_NoInputs)) {
            if (ImGui::GetTime() - bgPushTime > 0.75) stage_mark();
            bgPushTime = ImGui::GetTime();
            for (int k = 0; k < 3; k++) stageEdit.bg[k] = stageInfo.bg[k] = (uint8_t)lroundf(bgc[k] * 255), view.stageBg[k] = bgc[k];
            stageDirty = true;
        }
        tip("What shows through the panorama's gaps (the stage's bgColor)");

        heading("Selected part");
        if (stagePart >= 0 && stagePart < (int)stageEdit.parts.size()) {
            StagePart &pt = stageEdit.parts[stagePart];
            ImGui::PushFont(g_fontBold);
            ImGui::TextColored(C_SELECT, "%s", pt.name.c_str());
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::TextDisabled("part %d, %d polygons", stagePart, (int)pt.faces.size());
            float before[5];
            part_place(stagePart, before);
            auto part_field = [&](const char *verb) {
                if (ImGui::IsItemActivated()) part_begin(verb, before);
                if (ImGui::IsItemDeactivated() && partEditing && !vpMoving) part_end(ImGui::IsItemDeactivatedAfterEdit());
            };
            /* label, field and a button that puts it back, a row each, in columns: the field takes what is
               left, so nothing falls off the panel's edge at any width. X, Y, Z are colored; y is up */
            const ImGuiStyle &sty = ImGui::GetStyle();
            float bw = ImGui::GetFrameHeight();
            static const ImU32 AXC[3] = {IM_COL32(236, 84, 84, 255), IM_COL32(118, 208, 96, 255), IM_COL32(84, 142, 245, 255)};
            static const char *AXN[3] = {"X", "Y", "Z"};
            static const char *AXT[3] = {"Left and right.", "Up and down: up is positive (the game's y goes down).", "Depth."};
            if (ImGui::BeginTable("##place", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) {
                ImGui::TableSetupColumn("##l", ImGuiTableColumnFlags_WidthFixed, std::max(bw, ImGui::CalcTextSize("Scale").x));
                ImGui::TableSetupColumn("##f", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("##r", ImGuiTableColumnFlags_WidthFixed, bw);
                auto row = [&](const char *id, const std::function<void()> &label, const std::function<void()> &field, const char *verb,
                               const std::function<void()> &reset) {
                    ImGui::PushID(id);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    label();
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    field();
                    part_field(verb);
                    ImGui::TableNextColumn();
                    if (ImGui::Button(IC_UNDO, ImVec2(bw, bw))) part_begin(verb, before), reset(), part_end(true);
                    tip("Back to the start");
                    ImGui::PopID();
                };
                for (int a = 0; a < 3; a++)
                    row(AXN[a],
                        [&]() {
                            ImVec2 p0 = ImGui::GetCursorScreenPos();
                            ImDrawList *adl = ImGui::GetWindowDrawList();
                            adl->AddRectFilled(p0, p0 + ImVec2(bw, bw), AXC[a], sty.FrameRounding);
                            adl->AddText(p0 + (ImVec2(bw, bw) - ImGui::CalcTextSize(AXN[a])) * 0.5f, IM_COL32(20, 22, 26, 255), AXN[a]);
                            ImGui::Dummy(ImVec2(bw, bw));
                            tip("%s In the game's units. In the view: drag its arrow.", AXT[a]);
                        },
                        [&]() {
                            float v = a == 1 ? 0.0f - pt.move[1] : pt.move[a]; /* 0 - x: never -0 */
                            if (ImGui::DragFloat("##v", &v, 10, -40000, 40000, "%.0f")) pt.move[a] = a == 1 ? -v : v;
                        },
                        "Moving", [&]() { pt.move[a] = 0; });
                row("turn",
                    [&]() {
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextUnformatted("Turn");
                        tip("Around the vertical axis. In the view: drag the ring.");
                    },
                    [&]() { ImGui::DragFloat("##v", &pt.turn, 0.5f, -180, 180, "%.1f deg"); }, "Turning", [&]() { pt.turn = 0; });
                row("scale",
                    [&]() {
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextUnformatted("Scale");
                        tip("In the view: drag the box.");
                    },
                    [&]() { ImGui::DragFloat("##v", &pt.scale, 0.01f, 0.05f, 20, "%.2f"); }, "Scaling", [&]() { pt.scale = 1; });
                ImGui::EndTable();
            }
            bool nb = pt.noBounds;
            if (ImGui::Checkbox("Always drawn", &nb)) part_bounds(stagePart);
            tip("No bounds check: drawn even when its box seems off screen (the ground and the panorama; 8 bones at most)");
            int k = stagePart;
            const char *hide = stageHidden.count(pt.name) ? IC_EYE "  Show" : IC_HIDE "  Hide";
            if (ImGui::Button(IC_FIT "  Frame")) frame_sel();
            tip("The camera on it (F)");
            same_line_if_fits(hide);
            if (ImGui::Button(hide)) part_hide(k);
            tip("Only in the view: the game still draws it (H)");
            same_line_if_fits(IC_ADD "  Duplicate");
            if (ImGui::Button(IC_ADD "  Duplicate")) part_duplicate(k);
            tip("Ctrl+D");
            same_line_if_fits(IC_DELETE "  Delete");
            if (button(IC_DELETE "  Delete", 2)) part_delete(k);
            tip("Del; Ctrl+Z brings it back");
        } else {
            hint("Click a part in the view or in the Parts list. Right-click one for its actions. Then drag its arrows to move it, the ring to turn it and the box to scale it.");
        }

        heading("Add parts from another stage");
        if (button(IC_ADD "  Open the catalog...")) catOpen = true;
        tip("Every part of the 57 stages as a picture, with a search: double-click one to add it here");
        hint("Or copy a part (Ctrl+C) and paste it in another stage (Ctrl+V)."); 

        heading("Blender and the like (OBJ)");
        if (ImGui::Button(IC_EXPORT "  Export OBJ")) {
            char nm[16];
            snprintf(nm, sizeof nm, "stage_%02d", stageId);
            stageDir = g_exe_dir + "stages";
            SDL_CreateDirectory(stageDir.c_str());
            stageDir += std::string("/") + nm;
            if (stageEdit.exportObj(stageDir, nm)) note(NOTE_OK, "Exported stage " + std::to_string(stageId) + " to " + stageDir);
            else note(NOTE_ERROR, "Can't write " + stageDir);
        }
        tip("An OBJ with its MTL and PNGs (y up, 1 unit = 100 of the game's). Keep the objects' names to keep their "
            "parts; a material's _abrN is its semi-transparency.");
        same_line_if_fits(IC_IMPORT "  Import OBJ...");
        if (ImGui::Button(IC_IMPORT "  Import OBJ...")) {
            static const SDL_DialogFileFilter OBJ = {"OBJ models", "obj"};
            SDL_ShowOpenFileDialog(
                [](void *u, const char *const *l, int) {
                    if (!l || !*l) return;
                    Picked *p = (Picked *)u;
                    std::lock_guard<std::mutex> lock(p->m);
                    p->obj = *l;
                },
                &picked, win, &OBJ, 1, stageDir.empty() ? nullptr : stageDir.c_str(), false);
        }
        tip("Its objects become the stage's parts: textures repacked into the stage's three pages, 16 colors each "
            "piece, shrunk when they don't fit");
        if (!stageDir.empty()) {
            ImGui::SameLine();
            if (ImGui::Button(IC_FOLDER "##stagedir")) open_folder(stageDir);
            tip("Open %s", stageDir.c_str());
        }

        if (details("Technical details")) {
            std::string shared;
            for (const StageInfo &o : stageInfos)
                if (o.fid == si.fid && o.id != si.id) shared += (shared.empty() ? "" : ", ") + std::to_string(o.id);
            int polys = 0;
            for (const StagePart &pt : stageEdit.parts) polys += (int)pt.faces.size();
            field("Model file", "%s #%d", fid_name(si.fid).c_str(), si.fid);
            field("Bone list", "entry %d", si.boneEntry);
            if (!shared.empty()) field("Shared with", "stage %s", shared.c_str());
            field("Parts", "%d, %d polygons", (int)stageEdit.parts.size(), polys);
            if (stageMade.islands)
                field("Textures", "%d pieces%s", stageMade.islands, stageMade.shrink < 1 ? ", shrunk to fit" : "");
            field("Source", "%s", stageMods.count(si.fid) ? "the mod" : "the disc");
        }
    };

    /* ===== Model ===== */
    auto model_browser = [&]() {
        if (stage_on()) hint("Stage %d's model: its bones are the parts' pieces.", stageId);
        ImGui::BeginChild("bones");
        int before = scene.selected;
        bone_tree(scene, 0, hoverBone, scrollToSel, [&](int b) { bone_menu(b); });
        if (stage_on() && scene.selected != before) select_bone(scene.selected);
        ImGui::EndChild();
    };
    auto model_inspector = [&]() {
        Model &m = *scene.model;
        heading("Selected bone");
        int b = scene.selected;
        if (stage_on()) {
            if (stagePart >= 0) {
                ImGui::PushFont(g_fontBold);
                ImGui::TextColored(C_SELECT, "%s", part_name(stagePart).c_str());
                ImGui::PopFont();
            }
            hint("Stage parts are edited in Stages.");
            if (ImGui::Button(IC_STAGE "  Go to Stages")) section = STAGES;
        } else if (b > 0 && b < (int)m.bones.size()) {
            const Bone &bone = m.bones[b];
            const Mesh &me = m.meshes[bone.mesh];
            const Pose &p = m.pose[b];
            ImGui::PushFont(g_fontBold);
            ImGui::TextColored(C_SELECT, "bone %d", b);
            ImGui::PopFont();
            int kids = 0;
            for (const Bone &o : m.bones) kids += &o != &bone && o.parent == b;
            if (bone.parent > 0) {
                ImGui::SameLine();
                char l[32];
                snprintf(l, sizeof l, "parent: bone %d", bone.parent);
                if (ImGui::SmallButton(l)) scene.selected = bone.parent;
                tip("Select it");
            }
            field("Polygons", "%d", (int)me.polys.size());
            field("Vertices", "%d", (int)me.verts.size());
            field("Children", "%d", kids);
            field("Position", "%d %d %d", p.pos.x, p.pos.y, p.pos.z);
            field("Rotation", "%d %d %d", p.rot.x, p.rot.y, p.rot.z);
            field("Scale", "%d %d %d", p.scale.x, p.scale.y, p.scale.z);
            field("Entries", "mesh %d, keys %d", me.entry, bone.keyEntry);
            const char *hide = scene.hidden[b] ? IC_EYE "  Show" : IC_HIDE "  Hide";
            if (ImGui::Button(IC_FIT "  Frame")) frame_sel();
            tip("The camera on it (F)");
            same_line_if_fits(hide);
            if (ImGui::Button(hide)) scene.hidden[b] = !scene.hidden[b];
            tip("H");
            same_line_if_fits("Show only this");
            if (ImGui::Button("Show only this")) show_only(b);
            if (char_on()) {
                heading("Adjust this piece");
                hint("Moves, turns or scales its mesh in the file, in the bone's own axes: the animations still move it. "
                     "Drag the arrows or the box in the view.");
                PieceEdit pe = charEdits.count(b) ? charEdits[b] : PieceEdit();
                bool changed = false;
                auto field_done = [&]() {
                    if (ImGui::IsItemActivated()) char_mark(), charEditing = true;
                    if (ImGui::IsItemDeactivated() && charEditing && !vpMoving) {
                        if (!ImGui::IsItemDeactivatedAfterEdit() && !charUndo.empty()) charUndo.pop_back();
                        charEditing = false;
                    }
                };
                const ImGuiStyle &sty = ImGui::GetStyle();
                float bw = ImGui::GetFrameHeight();
                static const ImU32 AXC[3] = {IM_COL32(236, 84, 84, 255), IM_COL32(118, 208, 96, 255), IM_COL32(84, 142, 245, 255)};
                static const char *AXN[3] = {"X", "Y", "Z"};
                if (ImGui::BeginTable("##piece", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) {
                    ImGui::TableSetupColumn("##l", ImGuiTableColumnFlags_WidthFixed, std::max(bw, ImGui::CalcTextSize("Scale").x));
                    ImGui::TableSetupColumn("##f", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("##r", ImGuiTableColumnFlags_WidthFixed, bw);
                    auto row = [&](const char *id, const std::function<void()> &label, const std::function<void()> &field,
                                   const std::function<void()> &reset) {
                        ImGui::PushID(id);
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        label();
                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        field();
                        field_done();
                        ImGui::TableNextColumn();
                        if (ImGui::Button(IC_UNDO, ImVec2(bw, bw))) char_mark(), reset(), changed = true;
                        tip("Back to the start");
                        ImGui::PopID();
                    };
                    for (int a = 0; a < 3; a++)
                        row(AXN[a],
                            [&]() {
                                ImVec2 p0 = ImGui::GetCursorScreenPos();
                                ImDrawList *adl = ImGui::GetWindowDrawList();
                                adl->AddRectFilled(p0, p0 + ImVec2(bw, bw), AXC[a], sty.FrameRounding);
                                adl->AddText(p0 + (ImVec2(bw, bw) - ImGui::CalcTextSize(AXN[a])) * 0.5f, IM_COL32(20, 22, 26, 255), AXN[a]);
                                ImGui::Dummy(ImVec2(bw, bw));
                                tip("In the bone's own axes, in the game's units%s.", a == 1 ? " (up is positive)" : "");
                            },
                            [&]() {
                                float v = a == 1 ? 0.0f - pe.move[1] : pe.move[a];
                                if (ImGui::DragFloat("##v", &v, 2, -4000, 4000, "%.0f")) pe.move[a] = a == 1 ? -v : v, changed = true;
                            },
                            [&]() { pe.move[a] = 0; });
                    row("turn",
                        [&]() {
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextUnformatted("Turn");
                            tip("About the bone's vertical axis.");
                        },
                        [&]() { changed |= ImGui::DragFloat("##v", &pe.turn, 0.5f, -180, 180, "%.1f deg"); }, [&]() { pe.turn = 0; });
                    row("scale",
                        [&]() {
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextUnformatted("Scale");
                            tip("In the view: drag the box.");
                        },
                        [&]() { changed |= ImGui::DragFloat("##v", &pe.scale, 0.01f, 0.05f, 20, "%.2f"); }, [&]() { pe.scale = 1; });
                    ImGui::EndTable();
                }
                bool gone = pe.gone;
                if (ImGui::Checkbox("Not drawn in the game", &gone)) char_mark(), pe.gone = gone, changed = true;
                tip("Collapses the piece to a point, the way to remove it without touching the animations");
                if (changed) {
                    if (pe.any()) charEdits[b] = pe;
                    else charEdits.erase(b);
                    char_apply();
                    charDirty = true;
                }
            }
        } else {
            hint("Click the model in the view, or a bone on the left, to see it here.");
        }
        if (char_on()) {
            heading("In the game");
            ImGui::BeginDisabled(!charDirty);
            if (ImGui::Button(IC_SAVE "  Save to game")) char_save();
            ImGui::EndDisabled();
            tip("Writes %s to the port's mods folder (Ctrl+S): it reads it instead of the disc's when it starts", m.name.c_str());
            same_line_if_fits(IC_RESTART "  Revert to the disc's");
            ImGui::BeginDisabled(!char_mod() && !charDirty);
            if (ImGui::Button(IC_RESTART "  Revert to the disc's")) ask_revert_char();
            ImGui::EndDisabled();
            if (charDirty) ImGui::TextColored(C_SELECT, "Unsaved changes");
            else if (char_mod()) ImGui::TextDisabled("This is the mod's file.");
        }
        heading("Display");
        display_options();
        if (details("Technical details")) {
            size_t polys = 0, verts = 0;
            for (const Mesh &me : m.meshes) polys += me.polys.size(), verts += me.verts.size();
            field("File", "%s", m.name.c_str());
            field("Entries", "%d", m.arc.count());
            field("Bones", "%d", (int)m.bones.size() - 1);
            field("Polygons", "%d", (int)polys);
            field("Vertices", "%d", (int)verts);
            field("Bone list", "entry %d", m.boneEntry);
            field("Textures", "entry %d", m.texEntry);
            field("Motions", "entry %d", m.motionEntry);
        }
    };

    /* ===== Motions ===== */
    auto motion_frames = [](const std::vector<MotionStep> &st) {
        int frames = 0;
        for (const MotionStep &x : st)
            if (x.index != 0x7FFF) frames += std::max<int>(x.count, 0);
        return frames;
    };
    auto motions_browser = [&]() {
        Model &m = *scene.model;
        if (m.motions.empty()) return hint("No motions in this file: it shows its first keys.");
        if (ImGui::BeginTable("motions", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Motion", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Steps");
            ImGui::TableSetupColumn("Frames");
            ImGui::TableHeadersRow();
            for (int i = 1; i <= (int)m.motions.size(); i++) {
                const std::vector<MotionStep> &st = m.motions[i - 1];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                char l[24];
                snprintf(l, sizeof l, "%s motion %d", m.motion == i && view.playing ? IC_PLAY : "    ", i);
                if (ImGui::Selectable(l, m.motion == i, ImGuiSelectableFlags_SpanAllColumns)) m.setMotion(i), view.playing = true;
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem(IC_PLAY "  Play")) m.setMotion(i), view.playing = true;
                    if (ImGui::MenuItem(IC_COPY "  Copy \"motion N\"")) ImGui::SetClipboardText(("motion " + std::to_string(i)).c_str());
                    ImGui::EndPopup();
                }
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%d", (int)st.size() - 1);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%d", motion_frames(st));
            }
            ImGui::EndTable();
        }
    };
    auto motions_inspector = [&]() {
        Model &m = *scene.model;
        if (!m.motions.empty()) {
            heading("Now playing");
            field("Motion", "%d of %d", m.motion, (int)m.motions.size());
            field("Keyframe", "%d of %d", m.keyframe, m.keyframeCount);
            field("Frame", "%d", m.frame);
            field("State", "%s%s%s", view.playing ? "playing" : "paused", m.blending ? ", blending" : "", m.motionDone ? ", done" : "");
            heading("Playback");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderFloat("##speed", &view.speed, 0.05f, 4, "speed %.2fx");
            ImGui::Checkbox("Loop", &view.loop);
            hint("Space plays or pauses, the arrow keys step a keyframe. The bar under the view has the same controls.");
        }
        heading("Display");
        display_options();
    };

    /* ===== Fighter: what a script is, and playing it ===== */
    /* What each script's number is: its technique's script (TechData.script)
       or the reaction the other side runs for a hit's result. The ones with no
       name are listed by the techniques that use them, or not known */
    struct ScriptRole {
        const char *name, *examples;
        int group;
    };
    static const char *SCRIPT_GROUPS[4] = {"Reactions", "Attacks and techniques", "Support and field", "Other"};
    auto script_role = [](int i) -> ScriptRole {
        switch (i) {
        case 2: return {"Hit reaction", "Runs when the other side's attack hits", 0};
        case 3: return {"KO reaction", "Runs when the other side's attack knocks it out", 0};
        case 4: return {"Evade reaction", "Runs when the other side's attack misses", 0};
        case 5: return {"Basic attack (Fight)", "The Fight command, and many enemy attacks", 1};
        case 6: return {"Physical technique", "Heat Cutter, Whirlwind, Spinal Tap...", 1};
        case 8: return {"", "Used by Hammer Rush, Impact Rush", 1};
        case 9: return {"Magic technique", "Flame Ball, Divine Rain, Ice Blow...", 1};
        case 11: return {"Signature technique", "Hot Head, Pepper Breath, Nova Blast...", 1};
        case 12: return {"DNA technique", "Omnimon, Beelzemon, BladeGarurumon...", 1};
        case 13: return {"", "Used by Triple Fire, Giga Fire, Black Thorn...", 1};
        case 10: return {"Support technique", "Small Heal, Antidote, Erase Poison...", 2};
        case 15: return {"Field technique", "Fire Field, Water Field, Dark Field...", 2};
        case 16: return {"", "Used by Armor Break, Slow Down, Mega Break", 2};
        default: return {"", "", 3};
        }
    };

    /* A script played as the battle runs it: motions, waits, cameras and the
       sprite effects it starts, which draw over the view where the game
       would put them. It leaves out what the viewer has no model for: the
       jumps and moves, the other side, the sounds, fades and added models */
    struct FxShot {
        int id;
        float t, pos[3];
        int screen = 0; /* the point is a screen pixel: 1 in front of everything, 2 behind it */
    };
    /* the sprites and models the view draws this frame */
    std::vector<SpriteDraw> spriteDraws;
    std::vector<ExtraModel> extraModels;
    /* the front layer's effects (FIGHTSTG_updateSpriteEffect) draw over the models; the rest sort with them */
    auto fx_front = [](int id) { return (id >= 1000 && id < 1003) || id == 1007; };
    struct ScriptRun {
        bool on = false;
        int script = 0, pc = 0;
        float wait = 0;
        bool waitMotion = false;
        /* the game's camera the script has set (or the battle's default view), kept while it plays */
        bool camSet = false;
        Camera cam;
        float fov = 0.8f;
        int camIdx = -1;
    } run;
    bool lockCam = true;
    /* the technique whose look a script's own (9999) sprite effect takes: TechData.scriptEffect. The Fight
       command is no technique: its effect is 2 */
    int ownTech = 0;
    /* the technique that gives script SCRIPT's own effect: the chosen one when it runs that script, else
       (not for the Fight command's 5) the first that does; nullptr for none */
    auto own_tech = [&](int script) -> const TechRow * {
        for (const TechRow &t : g_techs)
            if (t.id == ownTech && t.script == script) return &t;
        if (script == 5) return nullptr;
        for (const TechRow &t : g_techs)
            if (t.script == script && t.effect) return &t;
        return nullptr;
    };
    auto own_effect_id = [&](int script) {
        const TechRow *t = own_tech(script);
        return t ? t->effect : script == 5 ? 2 : -1;
    };
    auto snap_cam = [&]() { run.cam = view.cam, run.fov = view.fov, run.camIdx = view.gameCamera, run.camSet = true; };
    std::vector<FxShot> shots;
    std::map<int, std::unique_ptr<Effect>> shotFx;
    /* the effect's own images (the mod), when it has them and they are on: they play in the original's place */
    bool shotMods = true;
    std::map<int, std::unique_ptr<CustomEffect>> shotOwnFx;
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
    auto run_script = [&](int i) {
        if (!scene.model || i < 0 || i >= (int)scene.model->scripts.size()) return;
        run = ScriptRun{true, i, 0, 0, false};
        shots.clear();
        shotOwnFx.clear(), ownIds = own_effects(ownRoot); /* the mod's files may have changed since */
        free_fx_models();
        scene.model->setMotion(1);
        view.playing = true;
        /* the battle's default view, which the script's cameras then change: a partner's camera 2, the one
           for an enemy of 2 cameras, the most common */
        const Fighter *f = scene.fighters.empty() ? nullptr : scene.fighters[scene.fighter];
        if (lockCam && f && !f->enemy && f->cameras.size() > 1) apply_camera(view, *f, 1), snap_cam();
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
            if (ln.camera >= 0 && f) {
                int c = ln.camera >= 100 ? ln.camera - 100 : ln.camera;
                if (c < (int)f->cameras.size() && (ln.camera >= 100) == f->enemy) apply_camera(view, *f, c), snap_cam();
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

    /* ===== Fighter ===== */
    auto fighter_browser = [&]() {
        Model &mm = *scene.model;
        if (scene.fighters.empty()) return hint("No fighter uses this file: it has no action scripts.");
        const Fighter &f = *scene.fighters[scene.fighter];
        hint("Play a script to see it as the battle runs it. Click a line to play its motion, look through its camera or see its effect.");
        if (!ownIds.empty()) {
            ImGui::Checkbox("Show the own images (mod)", &shotMods);
            tip("A script's sprite effects play the effect's own images (%d effects have them in the mods folder) instead of the original's; off, the original's", (int)ownIds.size());
        } else {
            hint("No effect has own images in the mods folder, so a script plays the original sprites.");
        }
        ImGui::Checkbox("Keep the game's camera", &lockCam);
        tip("While a script plays the view stays where the battle puts the camera (its default view, then the script's cameras). Off, you can move around it: a sprite effect then shows from where you look, not as the game shows it");
        if (run.on && button(IC_PAUSE "  Stop the script")) run.on = false, free_fx_models();
        ImGui::BeginChild("scripts");
        for (int g = 0; g < 4; g++) {
            bool titled = false;
            for (int i = 0; i < (int)mm.scripts.size(); i++) {
                const std::vector<ScriptLine> &sc = mm.scripts[i];
                ScriptRole role = script_role(i);
                if (role.group != g) continue;
                if (!titled) heading(SCRIPT_GROUPS[g]), titled = true;
                char h[96];
                if (*role.name) snprintf(h, sizeof h, "%s  #%d###script%d", role.name, i, i);
                else snprintf(h, sizeof h, "Script %d###script%d", i, i);
                bool open = ImGui::CollapsingHeader(h);
                tip("%s%s%d commands", role.examples, *role.examples ? ".  " : "", (int)sc.size());
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem(IC_PLAY "  Play the script")) run_script(i);
                    ImGui::EndPopup();
                }
                if (!open) continue;
                ImGui::PushID(i);
                if (button(run.on && run.script == i ? IC_RESTART "  Play again" : IC_PLAY "  Play the script")) run_script(i);
                tip("Runs its commands in order: motions, waits, cameras and sprite effects");
                if (std::any_of(sc.begin(), sc.end(), [](const ScriptLine &l) { return l.fxOwn; })) {
                    /* its "own" sprite effect is the technique's: TechData.scriptEffect */
                    const TechRow *cur = own_tech(i);
                    char prev[160];
                    snprintf(prev, sizeof prev, "%s  (effect %d)", cur ? cur->name.c_str() : "Fight command", own_effect_id(i));
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::BeginCombo("##owntech", prev)) {
                        if (i == 5 && ImGui::Selectable("Fight command  (effect 2)", !cur)) ownTech = 0;
                        for (const TechRow &t : g_techs) {
                            if (t.script != i || t.name.size() < 2) continue;
                            char l[160];
                            snprintf(l, sizeof l, "%s  (effect %d)##t%d", t.name.c_str(), t.effect, t.id);
                            if (ImGui::Selectable(l, cur == &t)) ownTech = t.id;
                        }
                        ImGui::EndCombo();
                    }
                    tip("The technique whose look this script plays: its \"own\" sprite effect is the one the technique names");
                }
                for (int k = 0; k < (int)sc.size(); k++) {
                    const ScriptLine &ln = sc[k];
                    ImGui::PushID(k);
                    bool active = ln.motion > 0 || ln.camera >= 0 || ln.effect >= 0;
                    bool now = run.on && run.script == i && k == run.pc - 1;
                    if (!active) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    if (ImGui::Selectable(ln.text.c_str(), now) && active) {
                        if (ln.motion > 0 && ln.motion <= (int)mm.motions.size()) mm.setMotion(ln.motion), view.playing = true;
                        if (ln.effect >= 0) pick_effect(ln.effect, true);
                        int c = ln.camera >= 100 ? ln.camera - 100 : ln.camera;
                        if (c >= 0 && c < (int)f.cameras.size() && (ln.camera >= 100) == f.enemy) apply_camera(view, f, c);
                    }
                    if (!active) ImGui::PopStyleColor();
                    if (active) tip("%s", ln.motion > 0 ? "Plays this motion" : ln.effect >= 0 ? "Shows this effect (in Effects)" : "Looks through this camera");
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem(IC_COPY "  Copy the command")) ImGui::SetClipboardText(ln.text.c_str());
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    };
    auto fighter_inspector = [&]() {
        if (scene.fighters.empty()) {
            title("No fighter");
            return hint("No fighter uses this file in the fighters' file (SMDLDATA.PRO): it has no name, face, cameras or action scripts there.");
        }
        Model &m = *scene.model;
        const Fighter &f = *scene.fighters[scene.fighter];
        title("%s", f.name.c_str());
        chip(f.enemy ? "enemy" : "partner", f.enemy ? C_ERROR : C_OK);
        if (scene.fighters.size() > 1) {
            std::string cur = std::to_string(f.id) + " " + f.name;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##fighter", cur.c_str())) {
                for (int i = 0; i < (int)scene.fighters.size(); i++) {
                    std::string l = std::to_string(scene.fighters[i]->id) + " " + scene.fighters[i]->name +
                                    (scene.fighters[i]->enemy ? "  (enemy)" : "  (partner)");
                    if (ImGui::Selectable(l.c_str(), i == scene.fighter)) {
                        std::string name = m.name;
                        open_model(scene, view, name, scene.path, status, scene.fid, i);
                    }
                }
                ImGui::EndCombo();
            }
            tip("The fighters that use this file: each may name other entries");
        }
        const Fighter &cf = *scene.fighters[scene.fighter];
        int parts = 0;
        for (bool u : cf.faceUsed) parts += u;
        field("Fighter", "%d (kind 0x%X)", cf.id, cf.kind);
        field("Face", "%d parts", parts);
        if (cf.enemy && cf.cameraCount > 0) field("Battle view", "its partner's %d", cf.cameraCount);
        heading("Cameras");
        if (button(IC_CAMERA "  Free camera", view.gameCamera < 0 ? 1 : 0)) frame_model(scene, view);
        tip("Orbit freely: dragging in the view leaves the game's camera too");
        static const char *CAMERA_GROUPS[3] = {"Battle views (the default view)", "Portraits", "Battle shots"};
        for (int g = 0; g < 3; g++) {
            bool any = false;
            for (int i = 0; i < (int)cf.cameras.size(); i++) any |= camera_role(cf.enemy, i).group == g;
            if (!any) continue;
            hint("%s", CAMERA_GROUPS[g]);
            for (int i = 0; i < (int)cf.cameras.size(); i++) {
                CameraRole r = camera_role(cf.enemy, i);
                if (r.group != g) continue;
                char l[80];
                snprintf(l, sizeof l, "%s  %s###cam%d", IC_CAMERA, r.name, i);
                if (ImGui::Selectable(l, view.gameCamera == i)) apply_camera(view, cf, i);
                tip("Camera %d: %s", i, r.what);
            }
        }
        if (details("Technical details"))
            field("Entries", "bones %d, motions %d, scripts %d", cf.modelEntry, cf.motionsEntry, cf.effectsEntry);
    };

    /* ===== Effects ===== */
    auto effect_row = [&](int id) {
        const std::set<std::string> &u = fxUsers[id], &t = fxTechs[id];
        std::string l = "effect " + std::to_string(id);
        if (!t.empty()) l += "  " + joined(t);
        else if (u.size() > 1) l += "  (" + std::to_string(u.size()) + " Digimon)";
        else if (u.empty()) l += "  (the battle's own)";
        ImGui::PushID(id);
        if (ImGui::Selectable(l.c_str(), fx.id == id, ImGuiSelectableFlags_AllowOverlap) && fx.id != id) pick_effect(id, false);
        if (u.size() > 1 || (!t.empty() && !u.empty())) tip("%s", joined(u).c_str());
        if (ImGui::BeginPopupContextItem("##menu")) {
            menu_title("effect " + std::to_string(id), "%s", t.empty() ? joined(u).c_str() : joined(t).c_str());
            if (ImGui::MenuItem(IC_OPEN "  Open")) pick_effect(id, false);
            if (ImGui::MenuItem(IC_COPY "  Copy \"effect N\"")) ImGui::SetClipboardText(("effect " + std::to_string(id)).c_str());
            ImGui::EndPopup();
        }
        if (ownIds.count(id)) {
            ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize("own").x);
            ImGui::TextColored(C_ACCENT, "own");
        }
        ImGui::PopID();
    };
    auto effects_browser = [&]() {
        if (!fxGrouped) group_effects(), ownIds = own_effects(ownRoot);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##fxfind", IC_SEARCH "  Search: Digimon, id or technique", fxFind, sizeof fxFind);
        if (ImGui::BeginTabBar("fxlists")) {
            if (ImGui::BeginTabItem("Original")) {
                ImGui::BeginChild("fxlist");
                if (*fxFind) {
                    for (int id : fxIds)
                        if (effect_found(id)) effect_row(id);
                } else {
                    if (ImGui::CollapsingHeader("Common", ImGuiTreeNodeFlags_DefaultOpen)) {
                        hint("The battle's own (hits, digivolving, items) and those several Digimon start");
                        for (int id : fxCommon) effect_row(id);
                    }
                    if (ImGui::CollapsingHeader("By Digimon", ImGuiTreeNodeFlags_DefaultOpen)) {
                        for (const auto &it : fxByDigimon) {
                            bool open = std::find(it.second.begin(), it.second.end(), fx.id) != it.second.end();
                            ImGui::SetNextItemOpen(open, ImGuiCond_Once);
                            if (ImGui::TreeNode(it.first.c_str())) {
                                for (int id : it.second) effect_row(id);
                                ImGui::TreePop();
                            }
                        }
                    }
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            char modTab[32];
            snprintf(modTab, sizeof modTab, "With own images (%d)###mod", (int)ownIds.size());
            if (ImGui::BeginTabItem(modTab)) {
                ImGui::BeginChild("fxmods");
                if (ownIds.empty()) hint("No effect has own images in %s yet.", ownRoot.c_str());
                for (int id : std::set<int>(ownIds)) /* a copy: opening one rescans ownIds */
                    if (effect_found(id)) effect_row(id);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    };
    auto effects_inspector = [&]() {
        if (fx.id < 0) {
            title("No effect open");
            return hint("Pick an effect on the left: the battle's 2D effects, its sparks, hits and techniques' sprites.");
        }
        title("Effect %d", fx.id);
        const std::set<std::string> &t = fxTechs[fx.id], &u = fxUsers[fx.id];
        auto some = [&](const std::set<std::string> &s) { /* the first few; all of them in the tooltip */
            std::string out;
            int n = 0;
            for (const std::string &x : s)
                if (n++ < 6) out += (out.empty() ? "" : ", ") + x;
            if (n > 6) out += " and " + std::to_string(n - 6) + " more";
            return out;
        };
        if (!t.empty()) ImGui::TextWrapped("%s", some(t).c_str()), tip("%s", joined(t).c_str());
        if (!u.empty()) hint("Started by %s", some(u).c_str()), tip("%s", joined(u).c_str());
        field("Length", "%d ticks, %.2f s", fx.duration, fx.duration / 60.0f);
        field("Animations", "%d, sheet %d", (int)fx.anims.size(), fx.sheetIndex);
        if (ImGui::Button(IC_FIT "  Fit")) fxFitPending = true;
        tip("The whole effect in the view (F)");
        same_line_if_fits(IC_EXPORT "  Export PNGs");
        if (ImGui::Button(IC_EXPORT "  Export PNGs")) {
            int n = export_effect(fx, fxDir);
            note(NOTE_OK, "Saved " + std::to_string(n) + " frames to " + fxDir);
        }
        tip("Every other field as a 320x240 PNG with its transparency, in effects/effect_%d", fx.id);
        if (!fxDir.empty()) {
            ImGui::SameLine();
            if (ImGui::Button(IC_FOLDER "##fxdir")) open_folder(fxDir);
            tip("Open %s", fxDir.c_str());
        }

        OwnSnap before = own_snap();
        bool changed = false;
        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Own images (native port)", ImGuiTreeNodeFlags_DefaultOpen)) {
            hint("PNGs the native port draws instead of this effect.");
            int crit = ownCritical;
            if (segmented("kind", &crit, {"Hit", "Critical hit"})) reload_own(crit != 0);
            ImGui::SameLine();
            if (ownDirty) chip(IC_WARN " Unsaved", C_WARN);
            else if (own.frames.empty()) chip("None yet", C_DIM);
            else chip(own.hasJson ? "Saved" : "PNGs only", C_ACCENT);
            if (ImGui::Checkbox("Show them in the view", &ownShow)) fxTime = 0, fxDrawn = -1;
            tip("Instead of the original effect");
            if (ImGui::Button(IC_IMPORT "  Import PNGs...")) {
                static const SDL_DialogFileFilter PNG = {"PNG images", "png"};
                SDL_ShowOpenFileDialog(
                    [](void *u, const char *const *list, int) {
                        if (!list) return;
                        Picked *p = (Picked *)u;
                        std::lock_guard<std::mutex> lock(p->m);
                        for (; *list; list++) p->files.push_back(*list);
                    },
                    &picked, win, &PNG, 1, nullptr, true);
            }
            tip("Copies them into the folder below and adds them as frames, in their names' order. "
                "An image is the 320x240 screen around the effect's point, at any size of that shape.");
            same_line_if_fits(IC_SAVE "  Save");
            ImGui::BeginDisabled(own.frames.empty());
            if (button(IC_SAVE "  Save", ownDirty ? 1 : 0)) own_save();
            ImGui::EndDisabled();
            tip("Writes effect.json: the frames, their ticks and the placement (Ctrl+S)");
            same_line_if_fits(IC_RESTART "  Reload");
            if (ImGui::Button(IC_RESTART "  Reload")) reload_own(ownCritical);
            tip("Reads the folder again");
            hint("Folder: %s", nice_path(own.dir).c_str());
            if (ImGui::SmallButton(IC_FOLDER " Open it")) SDL_CreateDirectory(own.dir.c_str()), open_folder(own.dir);
            same_line_if_fits("Change the mods folder...");
            if (ImGui::SmallButton("Change the mods folder...")) pick_mods_folder();
            tip("The native port's data/mods/effects folder");
            if (!own.frames.empty()) {
                hint("%d frames, %d ticks = %.2f s (the original: %.2f s)", (int)own.frames.size(), own.duration(), own.duration() / 60.0f,
                     fx.duration / 60.0f);
                float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2 - ImGui::CalcTextSize("scale").x -
                             ImGui::GetStyle().ItemInnerSpacing.x;
                ImGui::PushItemWidth(half);
                changed |= ImGui::DragFloat("x", &own.x, 0.5f, -320, 320, "%.1f");
                ImGui::SameLine();
                changed |= ImGui::DragFloat("y", &own.y, 0.5f, -240, 240, "%.1f");
                changed |= ImGui::DragFloat("scale", &own.scale, 0.01f, 0.05f, 8, "%.2f");
                ImGui::SameLine();
                int blend = own.add;
                if (ImGui::Combo("blend", &blend, "alpha\0add\0")) own.add = blend != 0, changed = true;
                ImGui::PopItemWidth();
                int playing = ownShow ? own.frameAt((int)fxTime) : -1, act = -1, what = 0;
                if (ImGui::BeginTable("ownframes", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
                    ImGui::TableSetupColumn("#");
                    ImGui::TableSetupColumn("Image", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Ticks", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 5);
                    ImGui::TableSetupColumn("##do");
                    ImGui::TableHeadersRow();
                    for (int i = 0; i < (int)own.frames.size(); i++) {
                        CustomFrame &fr = own.frames[i];
                        ImGui::PushID(i);
                        ImGui::TableNextRow();
                        if (i == playing) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(76, 141, 255, 50));
                        ImGui::TableNextColumn();
                        char n[8];
                        snprintf(n, sizeof n, "%d", i);
                        if (ImGui::Selectable(n, ownFrame == i, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
                            ownFrame = ownFrame == i ? -1 : i;
                        if (ImGui::BeginPopupContextItem("##menu")) {
                            ownFrame = i;
                            menu_title("frame " + std::to_string(i), "%s, %d ticks", fr.file.c_str(), fr.ticks);
                            if (ImGui::MenuItem(IC_COPY "  Copy", "Ctrl+C")) do_copy();
                            if (ImGui::MenuItem(IC_PASTE "  Paste after it", "Ctrl+V", false, !frameClip.empty())) act = i, what = 3;
                            if (ImGui::MenuItem(IC_ADD "  Duplicate", "Ctrl+D")) act = i, what = 1;
                            if (ImGui::MenuItem(IC_UP "  Move up", nullptr, false, i > 0)) act = i, what = -2;
                            if (ImGui::MenuItem(IC_DOWN "  Move down", nullptr, false, i + 1 < (int)own.frames.size())) act = i, what = 2;
                            ImGui::Separator();
                            if (ImGui::MenuItem(IC_DELETE "  Delete", "Del")) act = i, what = 0;
                            ImGui::EndPopup();
                        }
                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::BeginCombo("##file", fr.file.c_str())) {
                            for (const auto &it : own.images)
                                if (ImGui::Selectable(it.first.c_str(), it.first == fr.file)) fr.file = it.first, changed = true;
                            ImGui::EndCombo();
                        }
                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::InputInt("##ticks", &fr.ticks, 0)) fr.ticks = std::clamp(fr.ticks, 1, 600), changed = true;
                        tip("How long it shows, in ticks (1/60 s)");
                        ImGui::TableNextColumn();
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                        if (ImGui::SmallButton(IC_UP)) act = i, what = -2;
                        tip("Move up");
                        ImGui::SameLine();
                        if (ImGui::SmallButton(IC_DOWN)) act = i, what = 2;
                        tip("Move down");
                        ImGui::SameLine();
                        if (ImGui::SmallButton(IC_ADD)) act = i, what = 1;
                        tip("Duplicate (Ctrl+D)");
                        ImGui::SameLine();
                        if (ImGui::SmallButton(IC_DELETE)) act = i, what = 0;
                        tip("Delete (Del)");
                        ImGui::PopStyleColor();
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                if (act >= 0 && what == 3) ownFrame = act, frame_paste();
                else if (act >= 0) frame_edit(act, what);
                hint("Click a frame to pick it: Ctrl+C, Ctrl+V, Ctrl+D and Del work on it, and Ctrl+Z undoes.");
            }
            if (!own.error.empty()) ImGui::TextColored(C_ERROR, IC_ERROR "  %s", own.error.c_str());
        }
        if (changed) {
            if (ImGui::GetTime() - ownPushTime > 0.6) ownHist.push(before);
            ownPushTime = ImGui::GetTime();
            ownDirty = true, fxDrawn = -1;
        }
        if (details("Technical details")) {
            field("Animations", "%s #%d entry %d", fid_name(fx.fid).c_str(), fx.fid, fx.entry);
            field("Sheet", "%s #%d entry %d", fid_name(fx.sheetFid).c_str(), fx.sheetFid, fx.sheetEntry);
            field("Fields", "%d", fx.duration);
        }
    };

    /* ===== Textures ===== */
    /* the texture page and CLUT pairs the model's polygons read */
    auto tex_combos = [&]() {
        std::vector<std::pair<uint16_t, uint16_t>> combos;
        std::set<std::pair<uint16_t, uint16_t>> seen;
        if (!scene.model) return combos;
        for (const Mesh &me : scene.model->meshes)
            for (const Poly &p : me.polys)
                if (p.textured && seen.insert({(uint16_t)(p.tpage & 0x19F), p.clut}).second)
                    combos.push_back({(uint16_t)(p.tpage & 0x19F), p.clut});
        return combos;
    };
    auto combo_label = [](std::pair<uint16_t, uint16_t> c) {
        static const char *DEPTH[] = {"4-bit", "8-bit", "16-bit", "?"};
        char l[64];
        snprintf(l, sizeof l, "%s page %d,%d  CLUT %d,%d", DEPTH[tpage_depth(c.first)], tpage_x(c.first), tpage_y(c.first),
                 clut_x(c.second), clut_y(c.second));
        return std::string(l);
    };
    auto textures_browser = [&]() {
        std::vector<std::pair<uint16_t, uint16_t>> combos = tex_combos();
        if (combos.empty()) return hint("No textured polygons.");
        hint("Each page with a palette (CLUT) the polygons read:");
        for (int i = 0; i < (int)combos.size(); i++) {
            std::string l = combo_label(combos[i]);
            ImGui::PushID(i);
            if (ImGui::Selectable(l.c_str(), view.texCombo == i)) view.texCombo = i, texFitPending = true;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem(IC_COPY "  Copy")) ImGui::SetClipboardText(l.c_str());
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        heading("TIMs");
        for (const Tim &t : scene.model->tims)
            hint("%dx%d halfwords at %d,%d%s", t.w, t.h, t.x, t.y, t.rlen ? " (RLEN)" : "");
    };
    auto textures_inspector = [&]() {
        Model &m = *scene.model;
        std::vector<std::pair<uint16_t, uint16_t>> combos = tex_combos();
        if (combos.empty()) return hint("No textured polygons.");
        auto [tp, cl] = combos[std::min(view.texCombo, (int)combos.size() - 1)];
        title("Texture page");
        ImGui::PushFont(g_fontMono);
        ImGui::TextWrapped("%s", combo_label({tp, cl}).c_str());
        ImGui::PopFont();
        ImGui::Checkbox("UVs", &view.uvs);
        tip("Where the polygons take their texels from");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderFloat("##zoom", &view.texZoom, 0.5f, 12, "zoom %.1fx");
        if (ImGui::Button(IC_FIT "  Fit")) texFitPending = true;
        tip("The whole page in the view (F)");
        same_line_if_fits(IC_SAVE "  Save BMP");
        if (ImGui::Button(IC_SAVE "  Save BMP")) {
            char name[96];
            snprintf(name, sizeof name, "%s_%04x_%04x.bmp", m.name.substr(0, m.name.find('.')).c_str(), tp, cl);
            save_bmp(g_exe_dir + name, texPixels, texW, texH);
            note(NOTE_OK, std::string("Saved ") + g_exe_dir + name);
        }
        heading("Legend");
        auto swatch = [](ImU32 c, const char *text) {
            ImVec2 p = ImGui::GetCursorScreenPos();
            float y = p.y + ImGui::GetTextLineHeight() / 2, w = ImGui::GetFontSize() * 2;
            ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, y), ImVec2(p.x + w, y), c, 2);
            ImGui::Dummy(ImVec2(w, ImGui::GetTextLineHeight()));
            ImGui::SameLine();
            ImGui::TextUnformatted(text);
        };
        swatch(IM_COL32(80, 255, 120, 200), "a polygon's UVs");
        swatch(IM_COL32(255, 220, 60, 255), "the selected bone's");
        swatch(IM_COL32(140, 205, 255, 255), "under the mouse");
        hint("Click a polygon in the view to select its bone; the Model section shows it.");
        heading("Selected bone");
        if (scene.selected > 0) {
            ImGui::PushFont(g_fontBold);
            ImGui::TextColored(C_SELECT, "%s", thing_name(scene.selected).c_str());
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::TextDisabled("%d polygons", thing_polys(scene.selected));
        } else {
            hint("None");
        }
    };

    auto browser = [&]() {
        static const char *TITLE[SECTIONS] = {"Models", "Stages", "Bones", "Motions", "Action scripts", "Effects", "Texture pages"};
        panel_title(SECTION_ICON[section], TITLE[section]);
        switch (section) {
        case FILES: files_browser(); break;
        case STAGES: stages_browser(); break;
        case MODEL: model_browser(); break;
        case MOTIONS: motions_browser(); break;
        case FIGHTER: fighter_browser(); break;
        case EFFECTS: effects_browser(); break;
        case TEXTURES: textures_browser(); break;
        }
    };
    auto inspector = [&]() {
        static const char *TITLE[SECTIONS] = {"Details", "Stage editor", "Model", "Playback", "Fighter", "Effect", "Texture"};
        panel_title(IC_INFO, TITLE[section]);
        switch (section) {
        case FILES: files_inspector(); break;
        case STAGES: stages_inspector(); break;
        case MODEL: model_inspector(); break;
        case MOTIONS: motions_inspector(); break;
        case FIGHTER: fighter_inspector(); break;
        case EFFECTS: effects_inspector(); break;
        case TEXTURES: textures_inspector(); break;
        }
    };

    /* ===== the playback bar under the view: a motion's or an effect's ===== */
    auto transport = [&]() {
        bool fxMode = section == EFFECTS;
        Model *m = scene.model.get();
        bool &playing = fxMode ? fxPlaying : view.playing;
        bool &loop = fxMode ? fxLoop : view.loop;
        float b = ImGui::GetFrameHeight() * 1.4f, F = ImGui::GetFontSize();
        /* what fits: the speed and the motion's list go first when the view is narrow */
        float spare = ImGui::GetContentRegionAvail().x - b * 4.2f - ImGui::GetStyle().ItemSpacing.x * 6 - F * 11;
        bool speed = spare > F * 6, list = !fxMode && spare > F * 13.5f;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::Button(IC_RESTART, ImVec2(b, 0))) fxMode ? void(fxTime = 0) : m->setMotion(m->motion);
        tip("Restart");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (button(playing ? IC_PAUSE : IC_PLAY, 1, ImVec2(b * 1.2f, 0))) playing = !playing;
        tip(playing ? "Pause (Space)" : "Play (Space)");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::Button(IC_STEP, ImVec2(b, 0))) {
            playing = false;
            if (fxMode) fxTime = std::min(fxTime + 1, (float)std::max(fx_length() - 1, 0));
            else m->advance(1), m->evaluate();
        }
        tip("Step (Right arrow; Left steps back)");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (loop) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
        else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::Button(IC_LOOP, ImVec2(b, 0))) loop = !loop;
        ImGui::PopStyleColor();
        tip(loop ? "Loop: on" : "Loop: off");
        if (speed) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
            ImGui::SliderFloat("##speed", &view.speed, 0.05f, 4, "%.2fx");
            tip("Speed");
        }
        if (list) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7);
            std::string cur = "motion " + std::to_string(m->motion);
            if (ImGui::BeginCombo("##motion", cur.c_str())) {
                for (int i = 1; i <= (int)m->motions.size(); i++)
                    if (ImGui::Selectable(("motion " + std::to_string(i)).c_str(), m->motion == i)) m->setMotion(i), playing = true;
                ImGui::EndCombo();
            }
        }
        ImGui::SameLine();
        char pos[32];
        if (fxMode) snprintf(pos, sizeof pos, "tick %d / %d", (int)fxTime, std::max(fx_length() - 1, 0));
        else snprintf(pos, sizeof pos, "key %d / %d", m->keyframe, std::max(m->keyframeCount - 1, 0));
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (fxMode) {
            float ft = fxTime;
            if (ImGui::SliderFloat("##tick", &ft, 0, (float)std::max(fx_length() - 1, 0), pos)) fxTime = ft, playing = false;
            tip("1 tick = 1/60 s, as the battle counts time");
        } else {
            int key = m->keyframe;
            if (ImGui::SliderInt("##key", &key, 0, std::max(m->keyframeCount - 1, 0), pos)) {
                playing = false;
                m->keyframe = key, m->motionDone = false;
                m->evaluate();
            }
            tip("frame %d%s%s", m->frame, m->blending ? " (blending)" : "", m->motionDone ? ", done" : "");
        }
    };
    auto step = [&](int dir) {
        if (section == EFFECTS && fx.id >= 0) {
            fxPlaying = false;
            fxTime = std::clamp(fxTime + dir, 0.0f, (float)std::max(fx_length() - 1, 0));
        } else if (scene.model && scene.model->keyframeCount > 0) {
            Model &m = *scene.model;
            view.playing = false;
            if (dir > 0) m.advance(1);
            else m.keyframe = std::max(m.keyframe - 1, 0), m.motionDone = false;
            m.evaluate();
        }
    };
