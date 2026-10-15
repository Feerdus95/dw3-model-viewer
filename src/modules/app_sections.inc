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
    float browserW = 0, inspectorW = 0; /* the first frame makes them a share of the window */
    bool showBrowser = true, showInspector = true, showHints = true, helpOpen = false, logOpen = false;
    int hoverBone = -1;       /* under the mouse, in the view or a list */
    bool scrollToSel = false; /* the selection was made in the view: the lists bring it into sight */
    int ownFrame = -1;        /* the own images' frame picked in their table */
    std::set<std::string> stageHidden; /* the stage's parts hidden in the view, by name */
    bool partEditing = false, vpMoving = false; /* a part being moved, turned or scaled; by Ctrl+drag in the view */
    float partStart[5] = {};                    /* its move, turn and scale when that began */
    std::string partVerb;
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
    auto leave_stage = [&](std::function<void()> then) {
        ask_first(stageDirty && stageId >= 0,
                  "Stage " + std::to_string(stageId) + " has changes you haven't saved to the game. Leave it and lose them?", std::move(then));
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
    auto can_save = [&]() { return section == EFFECTS ? fx.id >= 0 && !own.frames.empty() : stage_on(); };
    auto do_save = [&]() {
        if (!can_save()) return;
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
            ImGui::DragFloat3("Move", pt.move, 10, -40000, 40000, "%.0f");
            part_field("Moving");
            tip("x, y (down), z in the game's units. In the view: Ctrl+drag the part, Shift+Ctrl+drag lifts it.");
            ImGui::DragFloat("Turn", &pt.turn, 0.5f, -180, 180, "%.1f deg");
            part_field("Turning");
            ImGui::DragFloat("Scale", &pt.scale, 0.01f, 0.05f, 20, "%.2f");
            part_field("Scaling");
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
            hint("Click a part in the view or in the Parts list. Right-click one for its actions; Ctrl+drag one in the view to move it.");
        }

        heading("Add parts from another stage");
        std::string from = stageFromId >= 0 ? std::to_string(stageFromId) + "  " + stage_look(stageFromId) : "Pick a stage...";
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##from", from.c_str())) {
            for (const StageInfo &o : stageInfos) {
                std::string l = std::to_string(o.id) + "  " + stage_look(o.id);
                StageInfo oi;
                std::vector<uint8_t> ob;
                if (ImGui::Selectable(l.c_str(), o.id == stageFromId) && stage_load(o.id, stageFrom, oi, ob)) stageFromId = o.id;
            }
            ImGui::EndCombo();
        }
        if (stageFromId >= 0) {
            ImGui::BeginChild("from", ImVec2(0, ImGui::GetFrameHeightWithSpacing() * 6), ImGuiChildFlags_Borders);
            for (int k = 0; k < (int)stageFrom.parts.size(); k++) {
                ImGui::PushID(k);
                if (ImGui::SmallButton(IC_ADD " Add")) {
                    char pre[8];
                    snprintf(pre, sizeof pre, "s%02d_", stageFromId);
                    stage_mark();
                    stageEdit.append(stageFrom, k, pre);
                    stagePart = (int)stageEdit.parts.size() - 1;
                    if (stage_rebuild()) note(NOTE_OK, "Added " + part_name(stagePart));
                    scrollToSel = true;
                }
                ImGui::SameLine();
                ImGui::Text("%s", stageFrom.parts[k].name.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("%d polys", (int)stageFrom.parts[k].faces.size());
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
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

        if (ImGui::CollapsingHeader("Technical details")) {
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
        } else {
            hint("Click the model in the view, or a bone on the left, to see it here.");
        }
        heading("Display");
        display_options();
        if (ImGui::CollapsingHeader("Technical details")) {
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

    /* ===== Fighter ===== */
    auto fighter_browser = [&]() {
        Model &mm = *scene.model;
        if (scene.fighters.empty()) return hint("No fighter uses this file: it has no action scripts.");
        const Fighter &f = *scene.fighters[scene.fighter];
        hint("Click a motion to play it, a camera to look through it, an effect to see it.");
        ImGui::BeginChild("scripts");
        for (int i = 0; i < (int)mm.scripts.size(); i++) {
            const std::vector<ScriptLine> &sc = mm.scripts[i];
            char h[48];
            snprintf(h, sizeof h, "script %d  (%d commands)", i, (int)sc.size());
            if (!ImGui::CollapsingHeader(h)) continue;
            ImGui::PushID(i);
            for (int k = 0; k < (int)sc.size(); k++) {
                const ScriptLine &ln = sc[k];
                ImGui::PushID(k);
                bool active = ln.motion > 0 || ln.camera >= 0 || ln.effect >= 0;
                if (!active) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                if (ImGui::Selectable(ln.text.c_str(), false) && active) {
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
        heading("Cameras");
        if (button(IC_CAMERA "  Free camera", view.gameCamera < 0 ? 1 : 0)) frame_model(scene, view);
        tip("Orbit freely: dragging in the view leaves the game's camera too");
        static const char *HINT[12] = {"", "", "", "", "", "", "portrait", "portrait, 2nd idle", "battle", "battle", "battle", "battle"};
        if (ImGui::BeginTable("cams", 2)) {
            for (int i = 0; i < (int)cf.cameras.size(); i++) {
                ImGui::TableNextColumn();
                char l[48];
                snprintf(l, sizeof l, "%s %d  %s", IC_CAMERA, i, !cf.enemy && i < 12 ? HINT[i] : "");
                if (ImGui::Selectable(l, view.gameCamera == i)) apply_camera(view, cf, i);
            }
            ImGui::EndTable();
        }
        if (ImGui::CollapsingHeader("Technical details"))
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
        if (ImGui::CollapsingHeader("Technical details")) {
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
