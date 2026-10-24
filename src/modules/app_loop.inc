    bool vpDown[3] = {}; /* a mouse button pressed in the view, still held */
    int ctxBone = -1;    /* what the view's right-click menu is for */
    int texHover = -1;   /* the bone whose UVs are under the mouse, in Textures */
    int lastHover = -1;
    double stillSince = 0; /* the mouse has rested on lastHover since */
    int vpUses = 0;        /* clicks and drags in the view: after a few, the controls hint gets small */

    if (uiStage >= 0) open_stage(uiStage), section = STAGES;
    if (uiFx >= 0) pick_effect(uiFx, true), section = EFFECTS;
    if (uiSection >= 0 && uiSection < SECTIONS) section = uiSection;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) quitAsked = true;
            if (e.type == SDL_EVENT_DROP_BEGIN || e.type == SDL_EVENT_DROP_POSITION) dropping = true, dropSeen = SDL_GetTicks();
            if (e.type == SDL_EVENT_DROP_COMPLETE) dropping = false;
            if (e.type == SDL_EVENT_DROP_FILE) dropping = false, open_dropped(e.drop.data);
        }
        if (quitAsked) {
            quitAsked = false;
            std::string what;
            if (stageDirty && stageId >= 0) what = "stage " + std::to_string(stageId);
            if (charDirty && !stage_on()) what += (what.empty() ? "" : " and ") + std::string("the model's pieces");
            if (ownDirty) what += (what.empty() ? "" : " and ") + std::string("effect ") + std::to_string(fx.id) + "'s own images";
            ask_first(!what.empty(), "Your changes to " + what + " aren't saved. Quit anyway?", [&]() { running = false; });
            if (!what.empty()) ask.ok = "Quit without saving";
        }
        uint64_t now = SDL_GetTicksNS();
        float dt = (now - last) / 1e9f;
        last = now;

        /* the motion: one keyframe a PSX field (60 a second), as FIGHTSTG steps it */
        if (scene.model && view.playing && scene.model->keyframeCount > 0) {
            Model &m = *scene.model;
            view.ticks += dt * 60 * view.speed;
            int n = (int)view.ticks;
            view.ticks -= n;
            bool faced = false;
            for (int i = 0; i < std::min(n, 8); i++) {
                m.advance(1, !view.loop && !run.on); /* Loop off: one pass, also for a motion that loops inside its keys */
                if (m.motionDone && view.loop && !run.on) m.setMotion(m.motion);
                m.evaluate();
                if (view.face) faced |= m.stepFace();
            }
            if (faced) upload_vram(scene);
        }

        run_step(dt);
        if (fx.id >= 0 && fxPlaying) { /* in ticks, 60 a second, as FIGHTSTG_battle.frames counts */
            fxTime += dt * 60 * view.speed;
            if (fxTime >= fx_length()) fxTime = fxLoop ? 0 : (float)fx_length();
        }
        {
            std::lock_guard<std::mutex> lock(picked.m);
            if (!picked.files.empty()) {
                ownHist.push(own_snap());
                int n = own.import(picked.files);
                ownIds = own_effects(ownRoot);
                if (own.error.empty()) note(NOTE_OK, "Imported " + std::to_string(n) + " PNGs to " + own.dir);
                else note(NOTE_ERROR, own.error);
                ownShow = ownShow || n > 0, ownDirty = ownDirty || n > 0, fxDrawn = -1;
                picked.files.clear();
            }
            if (!picked.folder.empty()) {
                ownRoot = picked.folder;
                g_mods = ownRoot.substr(0, std::max<size_t>(ownRoot.find_last_of("/\\"), 0));
                std::ofstream(g_exe_dir + "effects_dir.txt") << ownRoot << "\n";
                ownHist.clear();
                own_load();
                stage_tables();
                note(NOTE_INFO, "The mods folder is now " + mods_root());
                picked.folder.clear();
            }
            if (!picked.obj.empty()) {
                if (stage_on()) {
                    stage_mark();
                    if (stageEdit.importObj(picked.obj)) {
                        std::string warning = stageEdit.error;
                        stagePart = -1;
                        if (stage_rebuild()) note(warning.empty() ? NOTE_OK : NOTE_WARN, warning.empty() ? "Imported " + picked.obj : warning);
                    } else {
                        stageHist.undo.pop_back();
                        note(NOTE_ERROR, stageEdit.error);
                    }
                } else if (char_on()) {
                    char_import(picked.obj);
                }
                picked.obj.clear();
            }
            if (!picked.prop.empty()) prop_add(picked.prop, propKind), picked.prop.clear();
            if (!picked.propTex.empty()) prop_set_tex(picked.propTex), picked.propTex.clear();
            if (!picked.bin.empty()) open_dropped(picked.bin), picked.bin.clear();
            if (!picked.data.empty()) { /* another data folder: everything read again from it */
                std::string dir = picked.data;
                picked.data.clear();
                if (std::ifstream(dir + "/names.txt")) {
                    ask_first((stageDirty && stageId >= 0) || ownDirty, "Changing the data folder drops the changes you haven't saved. Go on?", [&, dir]() {
                        std::ofstream(g_exe_dir + "data_dir.txt") << dir << "\n";
                        scanner.stop(); /* it reads g_names and g_data */
                        find_data(dir);
                        dw2_register_all();
                        scene.model.reset(), scene.fighters.clear(), scene.fid = -1;
                        fx = Effect(), listed = false, ownDirty = false;
                        stage_tables(), cat_free(), stageId = -1, stageFromId = -1, view.stageView = false, stageDirty = false;
                        fxGrouped = false, fxCommon.clear(), fxByDigimon.clear();
                        stageHist.clear(), ownHist.clear(), stageHidden.clear();
                        reset_selection();
                        section = FILES;
                        note(NOTE_OK, "Reading the game's data from " + dir);
                    });
                } else {
                    note(NOTE_ERROR, "No names.txt in " + dir + ": pick the data folder tools/setup_disc.py made");
                }
            }
        }
        if (!status.empty()) note(NOTE_ERROR, status), status.clear();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        int ww, wh;
        SDL_GetWindowSizeInPixels(win, &ww, &wh);
        ImVec2 ds = io.DisplaySize;
        const ImGuiStyle &st = ImGui::GetStyle();
        if (fxSelect) section = EFFECTS, fxSelect = false;
        if (needs_model(section) && !scene.model) section = FILES;
        bool mode2d = section == EFFECTS || section == TEXTURES;
        hoverBone = -1;
        mark_scene();
        ImFont *bold = g_fontBold ? g_fontBold : ImGui::GetFont();

        /* --- the menu bar --- */
        float menuH = 0;
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem(IC_OPEN "  Open a model file...", "Ctrl+O")) pick_bin();
                if (ImGui::MenuItem(IC_SAVE "  Save", "Ctrl+S", false, can_save())) do_save();
                if (ImGui::MenuItem(IC_RESTART "  Revert the stage to the disc's...", nullptr, false,
                                    stage_on() && (stageMods.count(stageInfo.fid) || stageDirty)))
                    ask_revert();
                if (ImGui::MenuItem(IC_RESTART "  Revert the model to the disc's...", nullptr, false, char_on() && (char_mod() || charDirty)))
                    ask_revert_char();
                ImGui::Separator();
                if (ImGui::MenuItem(IC_FOLDER "  Change the data folder...")) pick_data_folder();
                if (ImGui::MenuItem(IC_FOLDER "  Change the effects' mods folder...")) pick_mods_folder();
                ImGui::Separator();
                if (ImGui::MenuItem("Quit", "Alt+F4")) quitAsked = true;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem(IC_UNDO "  Undo", "Ctrl+Z", false, can_undo(false))) do_undo(false);
                if (ImGui::MenuItem(IC_REDO "  Redo", "Ctrl+Y", false, can_undo(true))) do_undo(true);
                ImGui::Separator();
                if (ImGui::MenuItem(IC_COPY "  Copy", "Ctrl+C", false, section == EFFECTS ? fx.id >= 0 : scene.model != nullptr)) do_copy();
                if (ImGui::MenuItem(IC_PASTE "  Paste", "Ctrl+V", false, can_paste())) do_paste();
                if (ImGui::MenuItem(IC_ADD "  Duplicate", "Ctrl+D", false, can_edit_sel())) do_duplicate();
                if (ImGui::MenuItem(IC_DELETE "  Delete", "Del", false, can_edit_sel())) do_delete();
                ImGui::Separator();
                if (ImGui::MenuItem("Deselect", "Esc")) select_bone(-1), ownFrame = -1;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Browser panel", "Ctrl+B", &showBrowser);
                ImGui::MenuItem("Inspector panel", "Ctrl+I", &showInspector);
                ImGui::MenuItem("Controls hint", nullptr, &showHints);
                ImGui::Separator();
                if (ImGui::MenuItem(IC_FIT "  Frame the selection", "F")) frame_sel();
                if (ImGui::MenuItem("Frame everything")) frame_all();
                if (ImGui::MenuItem(IC_EYE "  Show all", "Alt+H")) show_all();
                ImGui::Separator();
                if (ImGui::BeginMenu("Textures")) {
                    const char *T[] = {"Off", "PSX", "Bilinear"};
                    for (int i = 0; i < 3; i++)
                        if (ImGui::MenuItem(T[i], nullptr, view.textures == i)) view.textures = i;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Lights")) {
                    const char *L[] = {"None", "Viewer", "Stage"};
                    for (int i = 0; i < 3; i++)
                        if (ImGui::MenuItem(L[i], nullptr, view.lighting == i)) view.lighting = i;
                    ImGui::EndMenu();
                }
                ImGui::MenuItem("Face animation", nullptr, &view.face);
                ImGui::MenuItem("Wireframe", nullptr, &view.wireframe);
                ImGui::MenuItem("Skeleton", nullptr, &view.skeleton);
                ImGui::MenuItem("Grid", nullptr, &view.grid);
                ImGui::MenuItem("Back-face culling", nullptr, &view.cull);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                ImGui::MenuItem(IC_HELP "  Controls", "F1", &helpOpen);
                ImGui::MenuItem(IC_INFO "  Activity log", nullptr, &logOpen);
                ImGui::Separator();
                if (ImGui::MenuItem("About DW3 Model Viewer")) aboutOpen = true;
                ImGui::EndMenu();
            }
            menuH = ImGui::GetWindowSize().y;
            ImGui::EndMainMenuBar();
        }
        const ImGuiWindowFlags BAR = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar;

        /* --- the sections, what's open, undo --- */
        float barH = ImGui::GetFrameHeight() + 14 * dpi;
        ImGui::SetNextWindowPos(ImVec2(0, menuH));
        ImGui::SetNextWindowSize(ImVec2(ds.x, barH));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10 * dpi, 7 * dpi));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
        ImGui::Begin("##sections", nullptr, BAR);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        /* the sections' names when they fit (with the undo and redo buttons at the right); the icons alone when not (the name is in each one's tip) */
        float undoW = ImGui::GetFrameHeight() * 1.3f * 2 + st.ItemSpacing.x;
        bool compact;
        {
            float need = 0;
            for (int s = 0; s < SECTIONS; s++) need += ImGui::CalcTextSize((std::string(SECTION_ICON[s]) + "  " + SECTION_NAME[s]).c_str()).x + st.FramePadding.x * 2 + 3 * dpi;
            compact = need > ds.x - st.WindowPadding.x * 2 - undoW - 40 * dpi;
        }
        for (int s = 0; s < SECTIONS; s++) {
            if (s) ImGui::SameLine(0, 3 * dpi);
            bool off = needs_model(s) && !scene.model, on = section == s;
            ImGui::BeginDisabled(off);
            ImGui::PushStyleColor(ImGuiCol_Button, on ? ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive) : ImVec4(0, 0, 0, 0));
            std::string l = compact ? std::string(SECTION_ICON[s]) : std::string(SECTION_ICON[s]) + "  " + SECTION_NAME[s];
            l += "###sec" + std::to_string(s);
            if (ImGui::Button(l.c_str())) section = s;
            ImGui::PopStyleColor();
            ImGui::EndDisabled();
            if (off) tip("%s: %s.\nOpen a model first (in Files).", SECTION_NAME[s], SECTION_TIP[s]);
            else tip("%s: %s", SECTION_NAME[s], SECTION_TIP[s]);
        }
        {
            std::string open = "Nothing open";
            if (stage_on()) open = "Stage " + std::to_string(stageId) + "  " + stage_look(stageId);
            else if (scene.model && !scene.fighters.empty() && !scene.fighters[scene.fighter]->name.empty())
                open = scene.fighters[scene.fighter]->name + "  (" + scene.model->name + ")";
            else if (scene.model) open = scene.model->name;
            int wbd = wb_dirty(); /* the camera workbench: WB_CAND, WB_SEQ */
            bool wbOnly = wbd && !(stageDirty && stageId >= 0) && !ownDirty;
            std::string unsaved = IC_WARN " unsaved";
            if (wbOnly) unsaved += ": " + wb_unsaved_what(wbd);
            bool dirty = (stageDirty && stageId >= 0) || ownDirty || wbd;
            float ub = ImGui::GetFrameHeight() * 1.3f;
            float right = ds.x - st.WindowPadding.x - ub * 2 - st.ItemSpacing.x;
            float room = right - ImGui::GetCursorPosX() - 20 * dpi - st.ItemSpacing.x * 2; /* what the buttons leave for the open thing and the unsaved mark */
            auto width = [&]() { return ImGui::CalcTextSize(open.c_str()).x + (dirty ? ImGui::CalcTextSize((unsaved + "   ").c_str()).x : 0); };
            if (dirty && width() > room) unsaved = IC_WARN; /* the mark alone: its tip says what */
            if (width() > room) open.clear();
            if (width() > room) dirty = false; /* not even that fits */
            float tw = width();
            ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 20 * dpi, right - tw - st.ItemSpacing.x * 2));
            ImGui::AlignTextToFramePadding();
            if (dirty) {
                ImGui::TextColored(C_WARN, "%s", unsaved.c_str());
                tip("%s%s%s%s%s", stageDirty && stageId >= 0 ? "The stage's changes aren't saved to the game. " : "", ownDirty ? "The own images' changes aren't saved. " : "",
                    wbd & WB_CAND ? "The candidate camera isn't saved as a preset. " : "", wbd & WB_SEQ ? "The sequence isn't saved as an application. " : "",
                    "Ctrl+S saves the section's (in Cameras: what B shows).");
                ImGui::SameLine();
            }
            ImGui::TextDisabled("%s", open.c_str());
            ImGui::SameLine(right);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::BeginDisabled(!can_undo(false));
            if (ImGui::Button(IC_UNDO, ImVec2(ub, 0))) do_undo(false);
            ImGui::EndDisabled();
            tip("Undo (Ctrl+Z): the %s", wb_undo_on() ? "camera workbench's edits" : undo_own() ? "own images' edits" : "stage's edits");
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_undo(true));
            if (ImGui::Button(IC_REDO, ImVec2(ub, 0))) do_undo(true);
            ImGui::EndDisabled();
            tip("Redo (Ctrl+Y)");
            ImGui::PopStyleColor();
        }
        ImGui::End();

        /* --- the layout: browser | view (and its playback bar) | inspector, the status bar under all --- */
        float statusH = ImGui::GetFrameHeight() + 6 * dpi;
        float top = menuH + barH, bottom = ds.y - statusH;
        bool motionBar = scene.model && !view.stageView && !scene.model->motions.empty() &&
                         (section == FILES || section == DW2 || section == DCB || section == MODEL || section == MOTIONS || section == FIGHTER || section == CAMERAS);
        bool transportOn = (section == EFFECTS && fx.id >= 0) || motionBar;
        bool abBar = motionBar && (section == FIGHTER || section == CAMERAS) && !scene.fighters.empty() && !scene.model->dw2;
        float transH = transportOn ? ImGui::GetFrameHeight() * (abBar ? 2 : 1) + (abBar ? 16 : 12) * dpi : 0;
        if (browserW <= 0) browserW = ds.x * 0.21f;
        if (inspectorW <= 0) inspectorW = ds.x * 0.25f;
        /* the panels' inner edges drag like splitters, before the layout reads the widths */
        bool splitHot = false;
        {
            float grab = 4 * dpi;
            bool inY = io.MousePos.y >= top && io.MousePos.y < bottom;
            int over = 0;
            if (inY && showBrowser && fabsf(io.MousePos.x - browserW) <= grab) over = 1;
            else if (inY && showInspector && fabsf(io.MousePos.x - (ds.x - inspectorW)) <= grab) over = 2;
            if (!splitDrag && over && !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
                splitHot = true;
                if (ImGui::IsMouseClicked(0)) splitDrag = over;
            }
            if (splitDrag && !ImGui::IsMouseDown(0)) splitDrag = 0;
            if (splitDrag == 1) browserW = io.MousePos.x;
            if (splitDrag == 2) inspectorW = ds.x - io.MousePos.x;
            if (splitDrag || splitHot) {
                splitHot = true;
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            }
        }
        browserW = std::clamp(browserW, 180 * dpi, std::max(180 * dpi, ds.x * 0.45f));
        inspectorW = std::clamp(inspectorW, 180 * dpi, std::max(180 * dpi, ds.x * 0.45f));
        if (showBrowser && showInspector) { /* the view keeps room: the panels give it up (as far as their least), the one being dragged last */
            float over = browserW + inspectorW - (ds.x - std::min(320 * dpi, ds.x * 0.34f));
            float &first = splitDrag == 1 ? inspectorW : browserW, &second = splitDrag == 1 ? browserW : inspectorW;
            if (over > 0) {
                float cut = std::min(over / 2, std::max(first - 180 * dpi, 0.0f));
                first -= cut;
                second -= std::min(over - cut, std::max(second - 180 * dpi, 0.0f));
            }
        }
        float vx = showBrowser ? browserW : 0, vr = showInspector ? ds.x - inspectorW : ds.x;
        float vy = top, vw = std::max(vr - vx, 40.0f), vh = std::max(bottom - transH - top, 40.0f);
        ImVec2 vc(vx + vw / 2, vy + vh / 2);

        /* --- the mouse in the view --- */
        ImVec2 mp = io.MousePos, d = io.MouseDelta;
        bool inView = mp.x >= vx && mp.x < vx + vw && mp.y >= vy && mp.y < vy + vh;
        bool vpHover = inView && !io.WantCaptureMouse && !splitHot;
        bool openViewMenu = false;
        float &zoom2 = section == EFFECTS ? fxZoom : view.texZoom;
        float *pan2 = section == EFFECTS ? fxPan : texPan;
        /* the selected part's gizmo: where it is, and what the mouse is on */
        gzHot = -1;
        gz.on = false;
        vpRect[0] = vx, vpRect[1] = vy, vpRect[2] = vw, vpRect[3] = vh;
        if (propSet.size() > 1 && (propSel != propSetPrim || props.size() != propSetN)) propSet.clear(); /* something else was picked, or the list changed */
        if (section == STAGES && stage_on() && !mode2d && scene.model && stagePart >= 0 && stagePart < (int)stageEdit.parts.size()) {
            std::vector<int> gb = selected_bones();
            float lo[3], hi[3];
            if (!gb.empty() && bones_box(scene, gb, lo, hi)) {
                const StagePart &sp = stageEdit.parts[stagePart];
                float o[3];
                for (int i = 0; i < 3; i++) o[i] = (lo[i] + hi[i]) / 2 + (partEditing ? sp.move[i] - partStart[i] : 0);
                gizmo_make(gz, view_proj(view, (int)vw, (int)vh), o, vw, vh, view.fov, dpi);
            }
        }
        bool propGz = false;
        if (section == STAGES && stage_on() && !mode2d && propSel >= 0 && propSel < (int)props.size()) { /* a prop's arrows: the view's y is up */
            const StageProp &sp = props[propSel];
            float o[3] = {sp.pos[0], sp.pos[1], -sp.pos[2]};
            if (propSet.size() > 1) { /* a group: the arrows are at its middle */
                o[0] = o[1] = o[2] = 0;
                for (int i : propSet) o[0] += props[i].pos[0], o[1] += props[i].pos[1], o[2] -= props[i].pos[2];
                for (float &c : o) c /= propSet.size();
            }
            gz.up = 1, gz.rings = propSet.size() <= 1 && !sp.emitter && (sp.model || sp.fixed); /* the ring turns a model, or a flat billboard */
            propGz = gizmo_make(gz, view_proj(view, (int)vw, (int)vh), o, vw, vh, view.fov, dpi);
        }
        bool charGz = false;
        if (section == MODEL && char_on() && !mode2d && scene.selected > 0) {
            std::vector<int> gb = {scene.selected};
            float lo[3], hi[3];
            if (bones_box(scene, gb, lo, hi)) {
                float o[3] = {(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2};
                gz.up = 1, gz.rings = false;
                gizmo_make(gz, view_proj(view, (int)vw, (int)vh), o, vw, vh, view.fov, dpi);
                charGz = gz.on;
            }
        }
        if (!charGz && !propGz) gz.up = -1, gz.rings = true;
        if (gz.on) {
            gzHot = gzDrag >= 0 ? gzDrag : vpHover ? gizmo_hit(gz, ImVec2(mp.x - vx, mp.y - vy), dpi) : -1;
            if (gzHot >= 0) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        for (int b = 0; b < 3; b++) {
            if (!ImGui::IsMouseClicked(b) || !vpHover) continue;
            vpDown[b] = true, vpUses++;
            float bg[3];
            if (b == 0 && brush_ready() && ground_hit(view, vw, vh, mp.x - vx, mp.y - vy, bg) && brush_begin(bg)) {
                /* a stroke of the brush has begun */
            } else if (b == 0 && gzHot >= 0 && gz.on && propGz) {
                stage_mark();
                propMoving = true, propBefore = props[propSel];
                gzDrag = gzHot, gzAngSet = false;
            } else if (b == 0 && gzHot >= 0 && gz.on && charGz) {
                char_mark();
                vpMoving = charEditing = true, charMoved = false;
                gzDrag = gzHot;
            } else if (b == 0 && gzHot >= 0 && gz.on) {
                float pl[5];
                part_place(stagePart, pl);
                part_begin(gzHot == 3 ? "Turning" : gzHot == 4 ? "Scaling" : "Moving", pl);
                vpMoving = partEditing;
                gzDrag = vpMoving ? gzHot : -1;
                gzAngSet = false;
            } else if (b == 0 && selectMode && !mode2d && section == STAGES && stage_on() && !(pcatArm >= 0 && pcatArm < (int)pcat.size())) {
                marquee = true, marqA = marqB = mp; /* a box over the view picks the props in it */
            } else if (b == 0 && io.KeyCtrl && !mode2d && stage_on()) { /* Ctrl+drag: the part under the mouse (or the selected one) moves */
                int hit = pick_bone(scene, view, vw, vh, mp.x - vx, mp.y - vy);
                int k = hit > 0 ? part_of_bone(hit) : stagePart;
                if (k >= 0) {
                    float pl[5];
                    select_part(k);
                    part_place(k, pl);
                    part_begin("Moving", pl);
                    vpMoving = partEditing;
                }
            }
        }
        bool anyDown = vpDown[0] || vpDown[1] || vpDown[2], moved = d.x != 0 || d.y != 0;
        if (mode2d) {
            if (anyDown && moved) pan2[0] -= d.x / zoom2, pan2[1] -= d.y / zoom2;
            if (vpHover && io.MouseWheel != 0) { /* about the point under the mouse */
                float cx = vc.x - pan2[0] * zoom2, cy = vc.y - pan2[1] * zoom2;
                float px = (mp.x - cx) / zoom2, py = (mp.y - cy) / zoom2;
                zoom2 = std::clamp(zoom2 * powf(1.15f, io.MouseWheel), 0.25f, 16.0f);
                pan2[0] = (vc.x + px * zoom2 - mp.x) / zoom2, pan2[1] = (vc.y + py * zoom2 - mp.y) / zoom2;
            }
        } else if (scene.model) {
            if (vpDown[0] && propMoving && propSel >= 0 && propSel < (int)props.size()) {
                /* a prop: an arrow moves it along that axis of the view (the port's z is the view's -z), the box scales it */
                std::vector<int> tg;
                if (propSet.size() > 1) tg.assign(propSet.begin(), propSet.end());
                else tg.push_back(propSel);
                if (gzDrag < 3) {
                    ImVec2 s = gz.tip[gzDrag] - gz.c;
                    float sl = s.x * s.x + s.y * s.y;
                    if (sl > 1 && moved) {
                        float k = (d.x * s.x + d.y * s.y) / sl * gz.len;
                        for (int i : tg) props[i].pos[gzDrag] += gzDrag == 2 ? -k : k;
                        stageDirty = true;
                    }
                } else if (gzDrag == 3) { /* the ring: the mouse's angle around the prop in the ground's plane, as the part's ring does */
                    ImVec2 a = gz.ring[0] - gz.c, b = gz.ring[12] - gz.c, q = ImVec2(mp.x - vx, mp.y - vy) - gz.c;
                    float det = a.x * b.y - a.y * b.x;
                    if (fabsf(det) > 1) {
                        float ang = atan2f((a.x * q.y - a.y * q.x) / det, (q.x * b.y - q.y * b.x) / det);
                        if (gzAngSet) {
                            float da = ang - gzAng;
                            if (da > 3.1415927f) da -= 6.2831853f;
                            if (da < -3.1415927f) da += 6.2831853f;
                            props[propSel].rot = std::remainder(props[propSel].rot + da * 57.29578f, 360.0f);
                            stageDirty = true;
                        }
                        gzAng = ang, gzAngSet = true;
                    }
                } else if (moved) {
                    float f = powf(1.01f, d.x - d.y);
                    for (int i : tg) {
                        StageProp &p = props[i];
                        if (p.emitter) p.radius = std::clamp(p.radius * f, 50.0f, 40000.0f);
                        else p.scale = std::clamp(p.scale * f, 0.01f, 100000.0f);
                    }
                    stageDirty = true;
                }
            } else if (vpDown[0] && vpMoving && charEditing && gzDrag >= 0 && charGz) {
                /* a model's piece: an arrow moves it along that axis of the view, the box scales it */
                int sb = scene.selected;
                PieceEdit pe = charEdits.count(sb) ? charEdits[sb] : PieceEdit();
                bool did = false;
                if (gzDrag < 3) {
                    ImVec2 s = gz.tip[gzDrag] - gz.c;
                    float sl = s.x * s.x + s.y * s.y;
                    if (sl > 1 && moved) {
                        float k = (d.x * s.x + d.y * s.y) / sl * gz.len, dw[3] = {0, 0, 0}, loc[3];
                        dw[gzDrag] = k;
                        char_local(sb, dw, loc);
                        for (int i = 0; i < 3; i++) pe.move[i] += loc[i];
                        did = true;
                    }
                } else if (gzDrag == 4 && moved) {
                    pe.scale = std::clamp(pe.scale * powf(1.01f, d.x - d.y), 0.05f, 20.0f);
                    if (pe.follow) pe.reach[0] = pe.reach[1] = pe.reach[2] = pe.scale;
                    did = true;
                }
                if (did) {
                    charEdits[sb] = pe;
                    char_apply();
                    charDirty = charMoved = true;
                }
            } else if (vpDown[0] && vpMoving && gzDrag >= 0 && stagePart >= 0) {
                /* the gizmo: an arrow moves along its axis, the ring turns, the box scales */
                StagePart &p = stageEdit.parts[stagePart];
                if (gzDrag < 3) {
                    ImVec2 s = gz.tip[gzDrag] - gz.c;
                    float sl = s.x * s.x + s.y * s.y;
                    if (sl > 1) {
                        float k = (d.x * s.x + d.y * s.y) / sl * gz.len; /* the mouse's part along the arrow, in the world */
                        if (gzDrag == 1) p.move[1] -= k;
                        else p.move[gzDrag] += k;
                    }
                } else if (gzDrag == 3) {
                    /* the mouse in the ring's plane: its two unit vectors are the ring's points at 0 and 90 degrees */
                    ImVec2 a = gz.ring[0] - gz.c, b = gz.ring[12] - gz.c, q = ImVec2(mp.x - vx, mp.y - vy) - gz.c;
                    float det = a.x * b.y - a.y * b.x;
                    if (fabsf(det) > 1) {
                        float ang = atan2f((a.x * q.y - a.y * q.x) / det, (q.x * b.y - q.y * b.x) / det);
                        if (gzAngSet) {
                            float da = ang - gzAng;
                            if (da > 3.1415927f) da -= 6.2831853f;
                            if (da < -3.1415927f) da += 6.2831853f;
                            p.turn = std::remainder(p.turn - da * 57.29578f, 360.0f);
                        }
                        gzAng = ang, gzAngSet = true;
                    }
                } else if (moved) {
                    p.scale = std::clamp(p.scale * powf(1.01f, d.x - d.y), 0.05f, 20.0f);
                }
            } else if (vpDown[0] && vpMoving && moved && stagePart >= 0) {
                /* on the ground, the way the mouse goes on screen; Shift: up and down */
                StagePart &p = stageEdit.parts[stagePart];
                float k = view.cam.dist * 0.0015f, cy = cosf(view.cam.yaw), sy = sinf(view.cam.yaw);
                if (io.KeyShift) p.move[1] += d.y * k;
                else p.move[0] += (d.x * cy + d.y * sy) * k, p.move[2] -= (-d.x * sy + d.y * cy) * k;
            } else if (vpDown[0] && marquee) {
                marqB = mp;
            } else if (vpDown[0] && brush.stroke) {
                float g[3];
                if (moved && ground_hit(view, vw, vh, mp.x - vx, mp.y - vy, g)) {
                    float dx = g[0] - brush.last[0], dz = g[2] - brush.last[2];
                    if (dx * dx + dz * dz >= brush.cfg.radius * brush.cfg.radius * 0.25f) {
                        brush_dab(g, io.KeyCtrl);
                        for (int k = 0; k < 3; k++) brush.last[k] = g[k];
                    }
                }
            } else if (vpDown[0] && moved) {
                view.cam.yaw -= d.x * 0.008f;
                view.cam.pitch = std::clamp(view.cam.pitch + d.y * 0.008f, -1.5f, 1.5f);
                view.gameCamera = -1;
            }
            if ((vpDown[1] || vpDown[2]) && moved) { /* in the view's plane */
                float k = view.cam.dist * 0.0015f, cy = cosf(view.cam.yaw), sy = sinf(view.cam.yaw);
                view.cam.target[0] -= d.x * k * cy;
                view.cam.target[2] += d.x * k * sy;
                view.cam.target[1] += d.y * k;
                view.gameCamera = -1;
            }
            if (vpHover && io.MouseWheel != 0) view.cam.dist *= powf(0.88f, io.MouseWheel), view.gameCamera = -1;
            if (run.on && lockCam && run.camSet) view.cam = run.cam, view.fov = run.fov, view.gameCamera = run.camIdx;
            if (vpHover && !anyDown) hoverBone = pick_bone(scene, view, vw, vh, mp.x - vx, mp.y - vy);
        }
        for (int b = 0; b < 3; b++) {
            if (!vpDown[b] || ImGui::IsMouseDown(b)) continue;
            vpDown[b] = false;
            if (b == 0 && brush.stroke) {
                brush.stroke = false;
                if (!brush.added && !stageHist.undo.empty()) stageHist.undo.pop_back();
                continue;
            }
            if (b == 0 && marquee) {
                marquee = false;
                if (io.MouseDragMaxDistanceSqr[0] > 25 * dpi * dpi) { /* a box: every prop whose foot is inside it */
                    M4 pvp = view_proj(view, (int)vw, (int)vh);
                    float x0 = std::min(marqA.x, marqB.x) - vx, x1 = std::max(marqA.x, marqB.x) - vx, y0 = std::min(marqA.y, marqB.y) - vy, y1 = std::max(marqA.y, marqB.y) - vy;
                    std::set<int> g = io.KeyShift || io.KeyCtrl ? cur_props() : std::set<int>();
                    for (int i = 0; i < (int)props.size(); i++) {
                        float c[3] = {props[i].pos[0], props[i].pos[1], -props[i].pos[2]};
                        ImVec2 sp;
                        if (!to_screen(pvp, c, vw, vh, sp) || sp.x < x0 || sp.x > x1 || sp.y < y0 || sp.y > y1) continue;
                        if (io.KeyCtrl && g.erase(i)) continue;
                        g.insert(i);
                    }
                    set_props(g);
                    continue;
                }
            }
            if (b == 0 && propMoving) {
                gzDrag = -1, propMoving = false;
                const StageProp &n = props[propSel];
                if (!memcmp(propBefore.pos, n.pos, 12) && propBefore.scale == n.scale && propBefore.radius == n.radius && !stageHist.undo.empty())
                    stageHist.undo.pop_back();
                continue;
            }
            if (b == 0 && vpMoving && charEditing) {
                gzDrag = -1, vpMoving = charEditing = false;
                if (!charMoved && !charUndo.empty()) charUndo.pop_back();
                continue;
            }
            if (b == 0 && vpMoving) {
                gzDrag = -1;
                float pl[5];
                part_place(stagePart, pl);
                part_end(memcmp(pl, partStart, sizeof pl) != 0);
                continue;
            }
            if (io.MouseDragMaxDistanceSqr[b] > 25 * dpi * dpi || !inView) continue; /* a drag, not a click */
            if (pcatArm >= 0 && pcatArm < (int)pcat.size() && section == STAGES && stage_on()) { /* a catalog pick: a click places it, the other button stops */
                float g[3];
                if (b == 0 && !brush.on && vpHover && ground_hit(view, vw, vh, mp.x - vx, mp.y - vy, g)) {
                    prop_add(pcat[pcatArm].path, pcat[pcatArm].kind, g);
                    if (!io.KeyShift) pcatArm = -1;
                } else if (b == 1) {
                    pcatArm = -1;
                }
                continue;
            }
            int hit = section == TEXTURES ? texHover : mode2d || !scene.model ? -1 : pick_bone(scene, view, vw, vh, mp.x - vx, mp.y - vy);
            if (b == 0 && (section == TEXTURES || !mode2d)) {
                /* a prop under the mouse wins over the stage's parts: the nearest of the quads it is on */
                int ph = -1;
                if (section == STAGES && stage_on()) {
                    M4 pvp = view_proj(view, (int)vw, (int)vh);
                    float best = 1e30f;
                    for (int i = 0; i < (int)props.size(); i++) {
                        const StageProp &p = props[i];
                        float c[8][3], lo[2] = {1e30f, 1e30f}, hi[2] = {-1e30f, -1e30f};
                        int np = prop_points(p, c);
                        bool all = np > 0;
                        for (int k = 0; k < np; k++) {
                            ImVec2 sp;
                            if (!to_screen(pvp, c[k], vw, vh, sp)) all = false;
                            else lo[0] = std::min(lo[0], sp.x), lo[1] = std::min(lo[1], sp.y), hi[0] = std::max(hi[0], sp.x), hi[1] = std::max(hi[1], sp.y);
                        }
                        float pad = (p.emitter ? 14 : 4) * dpi, mx = mp.x - vx, my = mp.y - vy;
                        float area = (hi[0] - lo[0]) * (hi[1] - lo[1]);
                        if (all && mx >= lo[0] - pad && mx <= hi[0] + pad && my >= lo[1] - pad && my <= hi[1] + pad && area < best) best = area, ph = i;
                    }
                }
                if (ph >= 0 && (io.KeyShift || (io.KeyCtrl && selectMode))) { /* adds to the group, or takes out of it */
                    std::set<int> g = cur_props();
                    if (!g.erase(ph)) g.insert(ph);
                    set_props(g);
                } else {
                    propSet.clear();
                    propSel = ph;
                    if (ph >= 0) select_part(-1), propSel = ph;
                    else select_bone(hit);
                }
                scrollToSel = hit > 0 && ph < 0;
            }
            if (b == 1) ctxBone = hit, openViewMenu = true;
        }
        if (hoverBone != lastHover || moved) lastHover = hoverBone, stillSince = ImGui::GetTime();

        /* --- the keys (not while typing or in a menu) --- */
        bool keys = !io.WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
        auto chord = [&](ImGuiKeyChord c) { return keys && ImGui::IsKeyChordPressed(c); };
        if (chord(ImGuiMod_Ctrl | ImGuiKey_Z)) do_undo(false);
        if (chord(ImGuiMod_Ctrl | ImGuiKey_Y) || chord(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) do_undo(true);
        if (chord(ImGuiMod_Ctrl | ImGuiKey_C)) do_copy();
        if (chord(ImGuiMod_Ctrl | ImGuiKey_V)) do_paste();
        if (chord(ImGuiMod_Ctrl | ImGuiKey_D)) do_duplicate();
        if (chord(ImGuiMod_Ctrl | ImGuiKey_S)) do_save();
        if (chord(ImGuiMod_Ctrl | ImGuiKey_O)) pick_bin();
        wb_sync();
        wb_settle();
        if (chord(ImGuiMod_Ctrl | ImGuiKey_B)) showBrowser = !showBrowser;
        if (chord(ImGuiMod_Ctrl | ImGuiKey_I)) showInspector = !showInspector;
        if (chord(ImGuiKey_Delete)) do_delete();
        if (chord(ImGuiKey_H)) hide_selected();
        if (chord(ImGuiMod_Alt | ImGuiKey_H)) show_all();
        if (chord(ImGuiKey_F)) frame_sel();
        if (chord(ImGuiKey_B) && (section == FIGHTER || section == CAMERAS)) set_ab(!cand.on);
        if (chord(ImGuiKey_Escape)) select_bone(-1), ownFrame = -1, pcatArm = -1, propSet.clear(), marquee = false;
        if (chord(ImGuiMod_Ctrl | ImGuiKey_A) && section == STAGES && stage_on() && !props.empty()) {
            std::set<int> all;
            for (int i = 0; i < (int)props.size(); i++) all.insert(i);
            set_props(all);
        }
        if (chord(ImGuiKey_F1)) helpOpen = !helpOpen;
        if (chord(ImGuiKey_Space)) section == EFFECTS ? void(fxPlaying = !fxPlaying) : void(view.playing = !view.playing);
        if (keys && !io.KeyMods && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) step(1);
        if (keys && !io.KeyMods && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) step(-1);

        /* --- the panels, resizable from their inner edges --- */
        auto panel = [&](const char *id, float &w, bool right, const std::function<void()> &draw) {
            float h = bottom - top;
            ImGui::SetNextWindowPos(ImVec2(right ? ds.x : 0, top), ImGuiCond_Always, ImVec2(right ? 1.0f : 0.0f, 0));
            ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
            ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                          ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_NoBringToFrontOnFocus);
            draw();
            ImGui::End();
        };
        if (showBrowser) panel("##browser", browserW, false, browser);
        if (showInspector) panel("##inspector", inspectorW, true, inspector);
        scrollToSel = false;

        if (transportOn) {
            ImGui::SetNextWindowPos(ImVec2(vx, vy + vh));
            ImGui::SetNextWindowSize(ImVec2(vw, transH));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10 * dpi, 6 * dpi));
            ImGui::Begin("##transport", nullptr, BAR);
            ImGui::PopStyleVar();
            transport();
            ImGui::End();
        }

        /* --- the status bar: the last message; all of them in the log --- */
        ImGui::SetNextWindowPos(ImVec2(0, bottom));
        ImGui::SetNextWindowSize(ImVec2(ds.x, statusH));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10 * dpi, 3 * dpi));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
        ImGui::Begin("##status", nullptr, BAR);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        {
            char logL[32];
            snprintf(logL, sizeof logL, IC_INFO " Log (%d)", (int)g_notes.size());
            float lw = ImGui::CalcTextSize(logL).x + st.FramePadding.x * 2;
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::PushClipRect(p, ImVec2(ds.x - lw - 24 * dpi, p.y + statusH), true);
            ImGui::AlignTextToFramePadding();
            if (!g_notes.empty()) {
                const Note &n = g_notes.back();
                ImVec4 c = note_color(n.kind);
                if (SDL_GetTicks() / 1000.0 - n.time > 8) c.w = 0.6f;
                ImGui::TextColored(c, "%s  %s", note_icon(n.kind), n.text.c_str());
            } else {
                ImGui::TextDisabled("Ready. F1 shows the controls.");
            }
            ImGui::PopClipRect();
            ImGui::SameLine(ds.x - lw - st.WindowPadding.x);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            if (ImGui::Button(logL)) logOpen = !logOpen;
            ImGui::PopStyleColor();
            tip("Every message of this session");
        }
        ImGui::End();

        /* --- the view's overlays: the 2D canvases, the selection's label, help --- */
        ImGui::SetNextWindowPos(ImVec2(vx, vy));
        ImGui::SetNextWindowSize(ImVec2(vw, vh));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        ImGui::Begin("##view", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground |
                                            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
        ImGui::PopStyleVar(2);
        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImVec2 pad(8 * dpi, 5 * dpi);
        auto box_text = [&](ImVec2 p, ImU32 bg, ImU32 border, ImU32 fg, const std::string &text, ImFont *font = nullptr) {
            if (!font) font = ImGui::GetFont();
            ImVec2 ts = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0, text.c_str());
            dl->AddRectFilled(p, p + ts + pad * 2, bg, 6 * dpi);
            if (border) dl->AddRect(p, p + ts + pad * 2, border, 6 * dpi, 0, 1.5f * dpi);
            dl->AddText(font, font->FontSize, p + pad, fg, text.c_str());
            return ts + pad * 2;
        };
        bool quiet = anyDown || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
        if (section == EFFECTS) {
            if (fx.id >= 0) {
                if (fxFitPending) fx_fit(vw, vh), fxFitPending = false;
                int t = (int)fxTime;
                if (t != fxDrawn) {
                    fx_render(t, FX_BG);
                    fxTex = rgba_texture(fxTex, fxPx, FXC_W, FXC_H);
                    fxDrawn = t;
                }
                /* the canvas, the 320x240 screen around the effect's point (what
                   Export PNGs and own images span) and the point */
                float cx = vc.x - fxPan[0] * fxZoom, cy = vc.y - fxPan[1] * fxZoom;
                dl->AddImage((ImTextureID)(intptr_t)fxTex, ImVec2(cx - FXC_W / 2 * fxZoom, cy - FXC_H / 2 * fxZoom),
                             ImVec2(cx + FXC_W / 2 * fxZoom, cy + FXC_H / 2 * fxZoom));
                dl->AddRect(ImVec2(cx - FX_W / 2 * fxZoom, cy - FX_H / 2 * fxZoom), ImVec2(cx + FX_W / 2 * fxZoom, cy + FX_H / 2 * fxZoom),
                            IM_COL32(255, 255, 255, 60));
                dl->AddLine(ImVec2(cx - 8, cy), ImVec2(cx + 8, cy), IM_COL32(255, 220, 80, 160));
                dl->AddLine(ImVec2(cx, cy - 8), ImVec2(cx, cy + 8), IM_COL32(255, 220, 80, 160));
            } else {
                const char *t = "Pick an effect in the list on the left";
                dl->AddText(vc - ImGui::CalcTextSize(t) / 2, ImGui::GetColorU32(ImGuiCol_TextDisabled), t);
            }
        } else if (section == TEXTURES && scene.model) {
            Model &m = *scene.model;
            std::vector<std::pair<uint16_t, uint16_t>> combos = tex_combos(scene);
            texHover = -1;
            if (!combos.empty()) {
                view.texCombo = std::min(view.texCombo, (int)combos.size() - 1);
                auto [tp, cl] = combos[view.texCombo];
                std::string key = m.name + std::to_string(tp) + "/" + std::to_string(cl);
                if (key != texKey) {
                    texPage.pixels = decode_page(m.vram, tp, cl, 256, 256);
                    texView = rgba_texture(texView, texPage.pixels, 256, 256);
                    texKey = key, texPage.w = 256, texPage.h = 256;
                }
                if (texPage.fitPending) {
                    view.texZoom = std::clamp(0.9f * std::min(vw / texPage.w, vh / texPage.h), 0.5f, 12.0f);
                    texPan[0] = texPan[1] = 0, texPage.fitPending = false;
                }
                float z = view.texZoom;
                ImVec2 o(vc.x - texPan[0] * z - texPage.w * z / 2, vc.y - texPan[1] * z - texPage.h * z / 2);
                dl->AddRectFilled(o, o + ImVec2(texPage.w * z, texPage.h * z), IM_COL32(40, 40, 48, 255));
                dl->AddImage((ImTextureID)(intptr_t)texView, o, o + ImVec2(texPage.w * z, texPage.h * z));
                dl->AddRect(o - ImVec2(1, 1), o + ImVec2(texPage.w * z + 1, texPage.h * z + 1), IM_COL32(255, 255, 255, 40));
                if (view.uvs) {
                    auto at = [&](const uint8_t *uv) { return ImVec2(o.x + (uv[0] + 0.5f) * z, o.y + (uv[1] + 0.5f) * z); };
                    auto inside = [](ImVec2 p, ImVec2 a, ImVec2 b, ImVec2 c) {
                        float d1 = (p.x - b.x) * (a.y - b.y) - (a.x - b.x) * (p.y - b.y), d2 = (p.x - c.x) * (b.y - c.y) - (b.x - c.x) * (p.y - c.y),
                              d3 = (p.x - a.x) * (c.y - a.y) - (c.x - a.x) * (p.y - a.y);
                        return !((d1 < 0 || d2 < 0 || d3 < 0) && (d1 > 0 || d2 > 0 || d3 > 0));
                    };
                    static const int ORDER[2][4] = {{0, 1, 2, -1}, {0, 1, 3, 2}};
                    for (int pass = 0; pass < 2; pass++) /* the hovered and selected on top */
                        for (size_t b = 1; b < m.bones.size(); b++) {
                            int mark = (int)b == scene.selected ? 2 : (int)b == texHover ? 1 : 0;
                            if ((pass == 1) != (mark > 0)) continue;
                            for (const Poly &p : m.meshes[m.bones[b].mesh].polys) {
                                if (!p.textured || (p.tpage & 0x19F) != tp || p.clut != cl) continue;
                                if (pass == 0 && vpHover && !quiet &&
                                    (inside(mp, at(p.uv[0]), at(p.uv[1]), at(p.uv[2])) || (p.quad && inside(mp, at(p.uv[1]), at(p.uv[3]), at(p.uv[2])))))
                                    texHover = (int)b;
                                const int *ord = ORDER[p.quad];
                                int n = p.quad ? 4 : 3;
                                ImU32 c = mark == 2 ? IM_COL32(255, 220, 60, 255) : mark == 1 ? IM_COL32(140, 205, 255, 255) : IM_COL32(80, 255, 120, 160);
                                for (int k = 0; k < n; k++) dl->AddLine(at(p.uv[ord[k]]), at(p.uv[ord[(k + 1) % n]]), c, mark ? 1.5f : 1);
                            }
                        }
                }
                if (texHover > 0 && vpHover && !quiet) {
                    ImGui::BeginTooltip();
                    ImGui::PushFont(g_fontBold);
                    ImGui::TextColored(C_SELECT, "%s", thing_name(texHover).c_str());
                    ImGui::PopFont();
                    ImGui::TextDisabled("%d polygons. Click to select it.", thing_polys(texHover));
                    ImGui::EndTooltip();
                }
            }
        } else if (scene.model) {
            M4 vp = view_proj(view, (int)vw, (int)vh);
            /* the sprite effects a script is running, where the battle puts them: at their point in the world
               (or at a screen position), one PSX pixel (the screen is 320x240) the view's height over 240. The
               view draws them with the models, by their depth */
            for (size_t k = 0; k < shots.size() && k < 8; k++) {
                Effect *e = shot_effect(shots[k].id);
                const float *gp = shots[k].pos;
                ImVec2 c;
                if (!e) continue;
                bool seen = shots[k].screen || (to_screen(vp, gp, vw, vh, c) && !(c.x < -FX_W || c.x > vw + FX_W || c.y < -FX_H || c.y > vh + FX_H));
                if (!seen) {
                    char t[96];
                    snprintf(t, sizeof t, "Sprite effect %d plays out of view: it is at the enemy's side", shots[k].id);
                    ImVec2 ts = bold->CalcTextSizeA(bold->FontSize, FLT_MAX, 0, t) + pad * 2;
                    box_text(ImVec2(vc.x - ts.x / 2, vy + 52 * dpi), IM_COL32(20, 22, 26, 225), IM_COL32(255, 217, 51, 230),
                             IM_COL32(255, 232, 140, 255), t, bold);
                    continue;
                }
                int tick = (int)shots[k].t;
                if (CustomEffect *o = shot_own(shots[k].id))
                    alpha_frame([&](uint32_t *p) { o->draw(tick, p, FX_W, FX_H, FX_W / 2, FX_H / 2); }, shotPx, shotWhite);
                else
                    fx_alpha(*e, tick, shotPx, shotWhite);
                shotTex[k] = rgba_texture(shotTex[k], shotPx, FX_W, FX_H);
                SpriteDraw d;
                d.tex = shotTex[k], d.screen = shots[k].screen, d.front = fx_front(shots[k].id);
                memcpy(d.pos, gp, sizeof d.pos);
                spriteDraws.push_back(d);
            }
            /* the selection's label: its name, as the lists have it, over it */
            std::vector<int> bs = selected_bones();
            float lo[3], hi[3];
            if (!bs.empty() && bones_box(scene, bs, lo, hi)) {
                float tp[3] = {(lo[0] + hi[0]) / 2, hi[1], (lo[2] + hi[2]) / 2}, mid[3] = {tp[0], (lo[1] + hi[1]) / 2, tp[2]};
                ImVec2 a, c;
                char extra[64];
                snprintf(extra, sizeof extra, "   %d polys%s", thing_polys(bs[0]),
                         (stage_on() ? stageHidden.count(part_name(stagePart)) > 0 : scene.hidden[bs[0]]) ? ", hidden" : "");
                std::string text = thing_name(bs[0]) + extra;
                bool seen = to_screen(vp, tp, vw, vh, a) && to_screen(vp, mid, vw, vh, c);
                if (seen) a = a + ImVec2(vx, vy), c = c + ImVec2(vx, vy);
                if (!seen || c.x < vx || c.x > vx + vw || c.y < vy || c.y > vy + vh) { /* out of sight: said at the top */
                    text += "   out of view: F frames it";
                    ImVec2 ts = bold->CalcTextSizeA(bold->FontSize, FLT_MAX, 0, text.c_str()) + pad * 2;
                    box_text(ImVec2(vc.x - ts.x / 2, vy + 52 * dpi), IM_COL32(20, 22, 26, 225), IM_COL32(255, 217, 51, 230),
                             IM_COL32(255, 232, 140, 255), text, bold);
                } else {
                    ImVec2 ts = bold->CalcTextSizeA(bold->FontSize, FLT_MAX, 0, text.c_str()) + pad * 2;
                    ImVec2 p(std::clamp(a.x - ts.x / 2, vx + 6, std::max(vx + 6, vx + vw - ts.x - 6)),
                             std::clamp(a.y - ts.y - 24 * dpi, vy + 44 * dpi, std::max(vy + 44 * dpi, vy + vh - ts.y - 6)));
                    if (c.x >= vx && c.x <= vx + vw && c.y >= vy && c.y <= vy + vh) {
                        dl->AddLine(c, ImVec2(std::clamp(c.x, p.x + 6, p.x + ts.x - 6), p.y + ts.y), IM_COL32(255, 217, 51, 170), 1.5f * dpi);
                        dl->AddCircleFilled(c, 3.5f * dpi, IM_COL32(255, 217, 51, 255));
                    }
                    box_text(p, IM_COL32(20, 22, 26, 225), IM_COL32(255, 217, 51, 230), IM_COL32(255, 232, 140, 255), text, bold);
                }
            }
            if (gz.on) gizmo_draw(dl, gz, ImVec2(vx, vy), gzDrag >= 0 ? gzDrag : gzHot, dpi);
            if (marquee) {
                ImVec2 lo(std::min(marqA.x, marqB.x), std::min(marqA.y, marqB.y)), hi(std::max(marqA.x, marqB.x), std::max(marqA.y, marqB.y));
                dl->AddRectFilled(lo, hi, IM_COL32(110, 180, 255, 36));
                dl->AddRect(lo, hi, IM_COL32(110, 180, 255, 220), 0, 0, 1.5f * dpi);
            }
            /* what's being edited, while it is */
            if (partEditing && stagePart >= 0 && stagePart < (int)stageEdit.parts.size()) {
                const StagePart &p = stageEdit.parts[stagePart];
                char t[160];
                if (partVerb == "Turning") snprintf(t, sizeof t, "Turning  %s   %.1f deg", p.name.c_str(), p.turn);
                else if (partVerb == "Scaling") snprintf(t, sizeof t, "Scaling  %s   x%.2f", p.name.c_str(), p.scale);
                else snprintf(t, sizeof t, "Moving  %s   x %.0f   y %.0f   z %.0f", p.name.c_str(), p.move[0], -p.move[1], p.move[2]);
                ImVec2 ts = bold->CalcTextSizeA(bold->FontSize, FLT_MAX, 0, t) + pad * 2;
                box_text(ImVec2(vc.x - ts.x / 2, vy + 10 * dpi), IM_COL32(44, 84, 160, 235), 0, IM_COL32(255, 255, 255, 255), t, bold);
            }
            /* what's under the mouse, once it rests there */
            if (vpHover && !quiet && hoverBone > 0 && ImGui::GetTime() - stillSince > 0.3) {
                ImGui::BeginTooltip();
                ImGui::PushFont(g_fontBold);
                ImGui::TextColored(C_SELECT, "%s", thing_name(hoverBone).c_str());
                ImGui::PopFont();
                ImGui::TextDisabled("%d polygons", thing_polys(hoverBone));
                bool sel = stage_on() ? part_of_bone(hoverBone) == stagePart : hoverBone == scene.selected;
                ImGui::TextDisabled("%s  Right-click: its actions", sel ? "Selected." : "Click: select it.");
                if (stage_on()) ImGui::TextDisabled("Select it, then drag its arrows");
                ImGui::EndTooltip();
            }
        }

        /* the mode, top left */
        {
            char t[160];
            if (section == EFFECTS)
                snprintf(t, sizeof t, IC_EFFECT "  2D effect%s   zoom %.1fx%s", fx.id >= 0 ? (" " + std::to_string(fx.id)).c_str() : "", fxZoom,
                         ownShow && fx.id >= 0 ? "   own images" : "");
            else if (section == TEXTURES) snprintf(t, sizeof t, IC_TEXTURE "  2D texture page   zoom %.1fx", view.texZoom);
            else if (stage_on()) snprintf(t, sizeof t, IC_STAGE "  3D stage %d", stageId);
            else if (scene.model && view.gameCamera >= 0 && !scene.fighters.empty())
                snprintf(t, sizeof t, IC_MODEL "  3D model   " IC_CAMERA " %s", camera_role(scene.fighters[scene.fighter]->enemy, view.gameCamera).name);
            else if (scene.model && view.gameCamera >= 0) snprintf(t, sizeof t, IC_MODEL "  3D model   " IC_CAMERA " game camera %d", view.gameCamera);
            else if (scene.model) snprintf(t, sizeof t, IC_MODEL "  3D model");
            else t[0] = 0;
            if (*t) box_text(ImVec2(vx + 10 * dpi, vy + 10 * dpi), IM_COL32(16, 18, 22, 200), 0, IM_COL32(228, 231, 236, 255), t);
        }
        /* the controls, bottom left */
        if (showHints && (scene.model || mode2d)) {
            std::string h;
            if (mode2d) h = "Drag: pan    Wheel: zoom    F: fit";
            if (section == TEXTURES) h += "\nClick a polygon: select its bone";
            if (!mode2d) {
                h = "Left-drag: orbit    Right-drag: pan\nWheel: zoom    F: frame    H: hide\nClick: select    Right-click: actions";
                if (stage_on()) h += "\nCtrl+drag: move a part (Shift: lift it)";
                else if (!scene.model->motions.empty()) h += "\nSpace: play/pause    Arrows: step";
            }
            h += "\nF1: every control";
            ImVec2 ts = ImGui::CalcTextSize(h.c_str()) + pad * 2;
            if (vpUses >= 6 || ts.x > vw * 0.8f || ts.y > vh * 0.5f) h = "F1: every control", ts = ImGui::CalcTextSize(h.c_str()) + pad * 2;
            box_text(ImVec2(vx + 10 * dpi, vy + vh - ts.y - 10 * dpi), IM_COL32(16, 18, 22, 160), 0, IM_COL32(200, 205, 214, 220), h);
        }
        /* the last message, for a few seconds, bottom right */
        if (!g_notes.empty() && SDL_GetTicks() / 1000.0 - g_notes.back().time < 4) {
            const Note &n = g_notes.back();
            std::string t = std::string(note_icon(n.kind)) + "  " + n.text;
            if (t.size() > 120) t = t.substr(0, 117) + "...";
            ImVec2 ts = ImGui::CalcTextSize(t.c_str()) + pad * 2;
            ImVec4 c = note_color(n.kind);
            box_text(ImVec2(std::max(vx + 10 * dpi, vx + vw - ts.x - 10 * dpi), vy + vh - ts.y - 10 * dpi), IM_COL32(16, 18, 22, 235),
                     ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, 0.7f)), ImGui::GetColorU32(c), t);
        }
        if (dropping && SDL_GetTicks() - dropSeen < 500) {
            dl->AddRectFilled(ImVec2(vx, vy), ImVec2(vx + vw, vy + vh), IM_COL32(76, 141, 255, 40));
            dl->AddRect(ImVec2(vx + 8, vy + 8), ImVec2(vx + vw - 8, vy + vh - 8), IM_COL32(76, 141, 255, 200), 8 * dpi, 0, 3 * dpi);
            const char *t = IC_OPEN "  Drop a .BIN model of the game to open it";
            ImGui::PushFont(g_fontTitle);
            ImVec2 ts = ImGui::CalcTextSize(t);
            dl->AddText(vc - ts / 2, IM_COL32(255, 255, 255, 255), t);
            ImGui::PopFont();
        }
        if (openViewMenu) ImGui::OpenPopup("##viewmenu");
        if (ImGui::BeginPopup("##viewmenu")) {
            if (ctxBone > 0 && section != EFFECTS) bone_menu(ctxBone);
            else view_menu();
            ImGui::EndPopup();
        }
        ImGui::End();

        /* --- the first steps, while nothing is open --- */
        if (!mode2d && !scene.model) {
            ImGui::SetNextWindowPos(vc, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(std::max(std::min(vw - 40 * dpi, 500 * dpi), std::min(ds.x - 40 * dpi, 340 * dpi)), 0)); /* wider than a narrow view, over the panels' edges */
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22 * dpi, 18 * dpi));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10 * dpi);
            ImGui::Begin("##welcome", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            ImGui::PopStyleVar(2);
            ImGui::PushFont(g_fontTitle);
            ImGui::TextUnformatted("DW3 Model Viewer");
            ImGui::PopFont();
            ImGui::TextWrapped("Battle models, fight stages and 2D effects of Digimon World 2003, read from your own copy of the game.");
            ImGui::Spacing();
            if (g_names.empty()) {
                ImGui::TextColored(C_WARN, IC_WARN "  No game data found yet");
                hint("1. Run tools/setup_disc.py with your own disc image (.cue).\n2. Choose the data folder it made.");
                if (button(IC_FOLDER "  Choose the data folder...", 1)) pick_data_folder();
            } else {
                float w = (ImGui::GetContentRegionAvail().x - st.ItemSpacing.x) / 2;
                if (button(IC_FILES "  Browse the models", section == FILES ? 1 : 0, ImVec2(w, 0))) section = FILES, showBrowser = true;
                ImGui::SameLine();
                if (button(IC_STAGE "  Open a fight stage", section == STAGES ? 1 : 0, ImVec2(w, 0))) section = STAGES, showBrowser = true;
                if (ImGui::Button(IC_EFFECT "  See the 2D effects", ImVec2(w, 0))) section = EFFECTS;
                ImGui::SameLine();
                if (ImGui::Button(IC_OPEN "  Open a .BIN file...", ImVec2(w, 0))) pick_bin();
                hint(section == STAGES ? "Pick a stage in the list on the left." : "Pick a model in the list on the left, or drop a .BIN file of the game here.");
            }
            hint("F1 shows every control.");
            ImGui::End();
        }

        /* --- asking first --- */
        if (ask.open) ImGui::OpenPopup("##ask"), ask.open = false;
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(std::min(440 * dpi, ds.x - 40 * dpi), 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20 * dpi, 16 * dpi));
        if (ImGui::BeginPopupModal("##ask", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
            ImGui::PushFont(g_fontTitle);
            ImGui::TextColored(ask.danger ? C_WARN : C_ACCENT, "%s", ask.danger ? IC_WARN : IC_INFO);
            ImGui::SameLine();
            ImGui::TextUnformatted(ask.title.c_str());
            ImGui::PopFont();
            ImGui::Spacing();
            ImGui::TextWrapped("%s", ask.text.c_str());
            ImGui::Spacing();
            float bw = ImGui::GetFontSize() * 7;
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - st.WindowPadding.x - bw * 2.6f - st.ItemSpacing.x);
            if (button(ask.ok.c_str(), ask.danger ? 2 : 1, ImVec2(bw * 1.6f, 0))) {
                std::function<void()> then = std::move(ask.then);
                ask.then = nullptr;
                ImGui::CloseCurrentPopup();
                if (then) then();
            }
            ImGui::SameLine();
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            if (ImGui::Button("Cancel", ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ask.then = nullptr, ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();

        /* --- help: the controls --- */
        catalog_window();
        pcat_window();
        if (helpOpen) {
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(std::min(ds.x - 40 * dpi, 600 * dpi), std::min(ds.y - 60 * dpi, 760 * dpi)), ImGuiCond_Appearing);
            if (ImGui::Begin(IC_HELP "  Controls###help", &helpOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
                auto rows = [&](const char *head, std::initializer_list<std::pair<const char *, const char *>> r) {
                    heading(head);
                    if (ImGui::BeginTable(head, 2)) {
                        ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 10);
                        ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthStretch);
                        for (const auto &it : r) {
                            ImGui::TableNextColumn();
                            ImGui::PushFont(g_fontBold);
                            ImGui::TextColored(C_ACCENT, "%s", it.first);
                            ImGui::PopFont();
                            ImGui::TableNextColumn();
                            ImGui::TextWrapped("%s", it.second);
                        }
                        ImGui::EndTable();
                    }
                };
                rows("The 3D view", {{"Left-drag", "Orbit"},
                                     {"Right / middle-drag", "Pan"},
                                     {"Wheel", "Zoom"},
                                     {"Click", "Select a bone, or a stage's part (its name shows over it)"},
                                     {"Right-click", "Its actions; on nothing, the view's"},
                                     {"Ctrl+drag", "Move a stage's part on the ground"},
                                     {"Shift+Ctrl+drag", "Lift or lower it"},
                                     {"Drop a .BIN", "Open that model"}});
                rows("The 2D views (Effects, Textures)", {{"Drag", "Pan"}, {"Wheel", "Zoom about the mouse"}, {"Click", "Select a polygon's bone (Textures)"}});
                rows("Keys", {{"F", "Frame the selection (or everything)"},
                              {"H  /  Alt+H", "Hide the selection  /  show everything"},
                              {"Esc", "Deselect"},
                              {"Space", "Play or pause"},
                              {"Left  /  Right", "Step back  /  forth"},
                              {"Ctrl+O", "Open a model file"},
                              {"Ctrl+S", "Save the stage, or the own images (Effects)"},
                              {"Ctrl+Z  /  Ctrl+Y", "Undo  /  redo them"},
                              {"Ctrl+C  /  Ctrl+V", "Copy  /  paste a stage's part or an own images' frame;\nelsewhere Ctrl+C copies what's selected"},
                              {"Ctrl+D", "Duplicate it"},
                              {"Del", "Delete it"},
                              {"Ctrl+B  /  Ctrl+I", "Hide or show the side panels (or drag their edges)"},
                              {"F1", "This window"}});
            }
            ImGui::End();
        }
        /* --- the log --- */
        if (logOpen) {
            ImGui::SetNextWindowSize(ImVec2(std::min(640 * dpi, ds.x - 40 * dpi), std::min(320 * dpi, ds.y - 60 * dpi)), ImGuiCond_Appearing);
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            if (ImGui::Begin(IC_INFO "  Activity###log", &logOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
                auto stamp = [](double t) {
                    char s[16];
                    snprintf(s, sizeof s, "%02d:%02d", (int)t / 60, (int)t % 60);
                    return std::string(s);
                };
                if (ImGui::Button(IC_COPY "  Copy all")) {
                    std::string all;
                    for (const Note &n : g_notes) all += stamp(n.time) + "  " + n.text + "\n";
                    ImGui::SetClipboardText(all.c_str());
                }
                ImGui::SameLine();
                if (ImGui::Button(IC_DELETE "  Clear")) g_notes.clear();
                ImGui::Separator();
                ImGui::BeginChild("notes");
                if (g_notes.empty()) hint("Nothing yet: what you save, export or import, and what goes wrong, shows here.");
                for (int i = (int)g_notes.size() - 1; i >= 0; i--) {
                    const Note &n = g_notes[i];
                    ImGui::TextDisabled("%s", stamp(n.time).c_str());
                    ImGui::SameLine();
                    ImGui::TextColored(note_color(n.kind), "%s", note_icon(n.kind));
                    ImGui::SameLine();
                    ImGui::TextWrapped("%s", n.text.c_str());
                }
                ImGui::EndChild();
            }
            ImGui::End();
        }
        about_window(aboutOpen, aboutIcon);
        ImGui::Render();

        mark_scene();
        if (mode2d) {
            GL(glViewport)(0, 0, ww, wh);
            if (section == EFFECTS)
                GL(glClearColor)((FX_BG & 255) / 255.0f, (FX_BG >> 8 & 255) / 255.0f, (FX_BG >> 16 & 255) / 255.0f, 1);
            else
                GL(glClearColor)(0.09f, 0.1f, 0.12f, 1);
            GL(glClear)(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        } else {
            float sx = ww / std::max(ds.x, 1.0f), sy = wh / std::max(ds.y, 1.0f);
            std::vector<PropQuad> propQuads;
            std::vector<float> propLines;
            ghostOn = pcatArm >= 0 && section == STAGES && stage_on() && inView && !ImGui::GetIO().WantCaptureMouse &&
                      ground_hit(view, vw, vh, mp.x - vx, mp.y - vy, ghostAt);
            if (stage_on()) props_build(propQuads, propLines);
            render(scene, view, (int)(vx * sx), (int)((ds.y - vy - vh) * sy), (int)(vw * sx), (int)(vh * sy), spriteDraws, extraModels, propQuads,
                   propLines, stage_on() ? &propGpu : nullptr);
        }
        GL(glViewport)(0, 0, ww, wh);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (!uiShot.empty()) {
            static int uiFrames = 0;
            if (uiFrames == 2) aboutOpen = uiWin == "about", helpOpen = uiWin == "help", logOpen = uiWin == "log", catOpen = uiWin == "catalog", pcatOpen = uiWin == "pcat";
            if (uiTab >= 0 && uiFrames < 3) camTabGo = uiTab;
            if (uiTab >= 0 && section == FILES) filesTabGo = uiTab;
            if (++uiFrames == 8) { /* the window as drawn: ImGui's frame is in the back buffer */
                std::vector<uint32_t> px(ww * wh), flipped(ww * wh);
                GL(glReadPixels)(0, 0, ww, wh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                for (int y = 0; y < wh; y++) memcpy(&flipped[y * ww], &px[(wh - 1 - y) * ww], ww * 4);
                save_bmp(uiShot, flipped, ww, wh);
                printf("%dx%d -> %s\n", ww, wh, uiShot.c_str());
                running = false;
            }
        }
        SDL_GL_SwapWindow(win);
        { static int frames = 0; if (frames < 3 && ++frames) startup_mark(frames == 1 ? "first frame shown" : "frame"); if (frames == 3 && getenv("DW3MV_TIMING_EXIT")) running = false; }
    }
    if (shot.empty() && uiShot.empty()) {
        g_cfg.max = (SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED) != 0;
        if (!g_cfg.max) { /* a maximized window's own size would come back as the restored one */
            SDL_GetWindowSize(win, &g_cfg.w, &g_cfg.h);
            SDL_GetWindowPosition(win, &g_cfg.x, &g_cfg.y);
            g_cfg.hasPos = true;
        }
        g_cfg.browser = showBrowser, g_cfg.inspector = showInspector, g_cfg.hints = showHints;
        g_cfg.bw = browserW / dpi, g_cfg.iw = inspectorW / dpi;
        cfg_save();
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
