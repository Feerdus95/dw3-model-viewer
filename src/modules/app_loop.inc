    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) running = false;
            if (e.type == SDL_EVENT_DROP_FILE) open_entry(e.drop.data, 0);
            if (!io.WantCaptureMouse && fxActive) { /* the effect: dragged and zoomed */
                if (e.type == SDL_EVENT_MOUSE_MOTION && e.motion.state)
                    fxPan[0] -= e.motion.xrel / fxZoom, fxPan[1] -= e.motion.yrel / fxZoom;
                else if (e.type == SDL_EVENT_MOUSE_WHEEL)
                    fxZoom = std::clamp(fxZoom * powf(1.15f, e.wheel.y), 0.25f, 16.0f);
            } else if (!io.WantCaptureMouse) {
                if (e.type == SDL_EVENT_MOUSE_MOTION && (e.motion.state & SDL_BUTTON_LMASK)) {
                    view.cam.yaw -= e.motion.xrel * 0.008f;
                    view.cam.pitch = std::clamp(view.cam.pitch + e.motion.yrel * 0.008f, -1.5f, 1.5f);
                } else if (e.type == SDL_EVENT_MOUSE_MOTION && (e.motion.state & (SDL_BUTTON_RMASK | SDL_BUTTON_MMASK))) {
                    /* pan in the view's plane */
                    float k = view.cam.dist * 0.0015f, cy = cosf(view.cam.yaw), sy = sinf(view.cam.yaw);
                    view.cam.target[0] -= e.motion.xrel * k * cy;
                    view.cam.target[2] += e.motion.xrel * k * sy;
                    view.cam.target[1] += e.motion.yrel * k;
                } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                    view.cam.dist *= powf(0.88f, e.wheel.y);
                }
            }
            if (e.type == SDL_EVENT_KEY_DOWN && !io.WantCaptureKeyboard && fxActive && e.key.key == SDLK_F) fxFitPending = true;
            else if (e.type == SDL_EVENT_KEY_DOWN && !io.WantCaptureKeyboard && scene.model) {
                if (e.key.key == SDLK_SPACE) view.playing = !view.playing;
                if (e.key.key == SDLK_F) view.stageView ? frame_stage(view) : frame_model(scene, view);
            }
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
                m.advance(1);
                if (m.motionDone && view.loop) m.setMotion(m.motion);
                m.evaluate();
                if (view.face) faced |= m.stepFace();
            }
            if (faced) upload_vram(scene);
        }

        if (fx.id >= 0 && fxPlaying) { /* in ticks, 60 a second, as FIGHTSTG_battle.frames counts */
            fxTime += dt * 60 * view.speed;
            if (fxTime >= fx_length()) fxTime = fxLoop ? 0 : (float)fx_length();
        }
        {
            std::lock_guard<std::mutex> lock(picked.m);
            if (!picked.files.empty()) {
                int n = own.import(picked.files);
                ownIds = own_effects(ownRoot);
                status = own.error.empty() ? "imported " + std::to_string(n) + " PNGs to " + own.dir : own.error;
                ownShow = ownShow || n > 0, ownDirty = ownDirty || n > 0, fxDrawn = -1;
                picked.files.clear();
            }
            if (!picked.folder.empty()) {
                ownRoot = picked.folder;
                std::ofstream(g_exe_dir + "effects_dir.txt") << ownRoot << "\n";
                own_load();
                stage_tables();
                picked.folder.clear();
            }
            if (!picked.obj.empty()) {
                if (stageId >= 0 && stageEdit.importObj(picked.obj)) {
                    std::string warning = stageEdit.error;
                    stagePart = -1;
                    if (stage_rebuild() && !warning.empty()) status = warning;
                } else if (stageId >= 0) {
                    status = stageEdit.error;
                }
                picked.obj.clear();
            }
            if (!picked.data.empty()) { /* another data folder: everything read again from it */
                if (std::ifstream(picked.data + "/names.txt")) {
                    std::ofstream(g_exe_dir + "data_dir.txt") << picked.data << "\n";
                    find_data(picked.data);
                    scene.model.reset(), scene.fighters.clear(), scene.fid = -1;
                    fx = Effect(), listed = false;
                    stage_tables(), stageId = -1, stageFromId = -1, view.stageView = false;
                    fxGrouped = false, fxCommon.clear(), fxByDigimon.clear();
                    status.clear();
                } else {
                    status = "no names.txt in " + picked.data + ": pick the data folder tools/setup_disc.py wrote";
                }
                picked.data.clear();
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        int ww, wh;
        SDL_GetWindowSizeInPixels(win, &ww, &wh);
        float panel = 420 * (dpi > 0 ? dpi : 1);
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - panel, 0));
        ImGui::SetNextWindowSize(ImVec2(panel, io.DisplaySize.y));
        ImGui::Begin("##panel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
        fxActive = false;
        if (scene.model && !scene.fighters.empty() && !scene.fighters[scene.fighter]->name.empty())
            ImGui::Text("%s  (%s)", scene.fighters[scene.fighter]->name.c_str(), scene.model->name.c_str());
        else if (scene.model)
            ImGui::Text("%s", scene.model->name.c_str());
        else
            ImGui::TextWrapped("Pick a model, or drop a .BIN on the window.");
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize("About").x - ImGui::GetStyle().FramePadding.x * 2);
        if (ImGui::SmallButton("About")) aboutOpen = true;
        if (!status.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", status.c_str());
        /* SameLine only when the next widget, w wide, fits the panel */
        auto same_line = [](float w) {
            float x = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x;
            if (x + w <= ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) ImGui::SameLine();
        };
        auto bw = [](const char *l) {
            return ImGui::CalcTextSize(l, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2;
        };
        if (ImGui::BeginTabBar("tabs")) {
            if (ImGui::BeginTabItem("Files")) {
                if (!listed) {
                    list = scan_models();
                    listed = true;
                }
                if (ImGui::Button("Data folder...")) {
                    SDL_ShowOpenFolderDialog(
                        [](void *u, const char *const *l, int) {
                            if (!l || !*l) return;
                            Picked *p = (Picked *)u;
                            std::lock_guard<std::mutex> lock(p->m);
                            p->data = *l;
                        },
                        &picked, win, nullptr, false);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("The data folder tools/setup_disc.py wrote (with names.txt). Now: %s",
                                      g_data.substr(0, g_data.size() - 6).c_str());
                if (list.empty()) ImGui::TextWrapped("No models found in %s: run tools/setup_disc.py with your disc, or pick its data folder.", g_data.c_str());
                ImGui::InputTextWithHint("##filter", "filter", filter, sizeof filter);
                ImGui::BeginChild("list");
                for (const Entry &en : list) {
                    if (stageFids.count(en.fid)) continue; /* in the Stages tab */
                    std::string label = (en.label.empty() ? "" : en.label + "  ") + en.name + "  #" + std::to_string(en.fid);
                    std::string lo = label, f = filter;
                    std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);
                    std::transform(f.begin(), f.end(), f.begin(), ::tolower);
                    if (!f.empty() && lo.find(f) == std::string::npos) continue;
                    bool sel = scene.model && !view.stageView && scene.model->name == en.name;
                    if (ImGui::Selectable(label.c_str(), sel) && open_model(scene, view, en.name, fid_path(en.fid), status, en.fid))
                        view.stageView = false, stageId = -1;
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Stages")) {
                if (stageId >= 0 && view.stageView) {
                    const StageInfo &si = stageInfos[stageId];
                    std::string shared;
                    for (const StageInfo &o : stageInfos)
                        if (o.fid == si.fid && o.id != si.id) shared += (shared.empty() ? "" : ", ") + std::to_string(o.id);
                    ImGui::Text("Stage %d: %s", stageId, stage_look(stageId));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    ImGui::TextWrapped("%s #%d, bone list entry %d%s%s", fid_name(si.fid).c_str(), si.fid, si.boneEntry,
                                       shared.empty() ? "" : "; its model is stage ", shared.c_str());
                    ImGui::TextWrapped("%s", stageMods.count(si.fid) ? ("mod: " + mod_path(si.fid)).c_str() : "the disc's (no mod)");
                    ImGui::PopStyleColor();
                    float bgc[3] = {stageEdit.bg[0] / 255.0f, stageEdit.bg[1] / 255.0f, stageEdit.bg[2] / 255.0f};
                    if (ImGui::ColorEdit3("background", bgc)) {
                        for (int k = 0; k < 3; k++) stageEdit.bg[k] = stageInfo.bg[k] = (uint8_t)lroundf(bgc[k] * 255), view.stageBg[k] = bgc[k];
                        stageDirty = true;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("What shows through the panorama's gaps (the stage's bgColor)");
                    if (ImGui::Button("Export OBJ")) {
                        char nm[16];
                        snprintf(nm, sizeof nm, "stage_%02d", stageId);
                        stageDir = g_exe_dir + "stages";
                        SDL_CreateDirectory(stageDir.c_str());
                        stageDir += std::string("/") + nm;
                        status = stageEdit.exportObj(stageDir, nm) ? "exported to " + stageDir : "can't write " + stageDir;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("An OBJ with its MTL and PNGs (y up, 1 unit = 100 of the game's), for Blender and the like.\n"
                                          "Keep the objects' names to keep their parts; a material's _abrN is its semi-transparency.");
                    if (!stageDir.empty()) {
                        same_line(bw("Open folder"));
                        if (ImGui::Button("Open folder##stage")) open_folder(stageDir);
                    }
                    same_line(bw("Import OBJ..."));
                    if (ImGui::Button("Import OBJ...")) {
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
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Its objects become the stage's parts: textures repacked into the stage's\n"
                                          "three pages, 16 colors each piece, shrunk when they don't fit");
                    if (ImGui::Button(stageDirty ? "Save to game*" : "Save to game")) stage_save();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Writes %s and the stages' table, 0459.bin,\nwhich the native port reads instead of the disc's",
                                          mod_path(si.fid).c_str());
                    if (stageMods.count(si.fid) || stageDirty) {
                        same_line(bw("Revert to the disc's"));
                        if (ImGui::Button("Revert to the disc's")) stage_revert();
                    }
                    int polys = 0;
                    for (const StagePart &pt : stageEdit.parts) polys += (int)pt.faces.size();
                    ImGui::Text("%d parts, %d polygons", (int)stageEdit.parts.size(), polys);
                    if (stageMade.islands) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("textures: %d pieces%s", stageMade.islands, stageMade.shrink < 1 ? ", shrunk to fit" : "");
                    }
                    ImGui::SeparatorText("Parts");
                    ImGui::BeginChild("parts", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 8), ImGuiChildFlags_Borders);
                    int drop = -1;
                    bool rebuild = false;
                    for (int k = 0; k < (int)stageEdit.parts.size(); k++) {
                        StagePart &pt = stageEdit.parts[k];
                        ImGui::PushID(k);
                        rebuild |= ImGui::Checkbox("##nb", &pt.noBounds);
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("No bounds check: drawn even when its box seems off screen\n(the ground and the panorama; 8 bones at most)");
                        ImGui::SameLine();
                        char l[96];
                        snprintf(l, sizeof l, "%s  (%d polys)", pt.name.c_str(), (int)pt.faces.size());
                        if (ImGui::Selectable(l, stagePart == k, 0, ImVec2(ImGui::GetContentRegionAvail().x - bw("x") - 8, 0))) {
                            stagePart = stagePart == k ? -1 : k;
                            scene.selected = stagePart >= 0 && stagePart < (int)stageMade.firstBone.size() ? stageMade.firstBone[stagePart] : -1;
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("x")) drop = k;
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove");
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                    if (drop >= 0) {
                        stageEdit.remove(drop);
                        stagePart = -1;
                        rebuild = true;
                    }
                    if (stagePart >= 0 && stagePart < (int)stageEdit.parts.size()) {
                        StagePart &pt = stageEdit.parts[stagePart];
                        ImGui::DragFloat3("move", pt.move, 10, -40000, 40000, "%.0f");
                        rebuild |= ImGui::IsItemDeactivatedAfterEdit();
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("In the game's units: x, y (down), z");
                        ImGui::DragFloat("turn", &pt.turn, 0.5f, -180, 180, "%.1f deg");
                        rebuild |= ImGui::IsItemDeactivatedAfterEdit();
                        ImGui::DragFloat("scale", &pt.scale, 0.01f, 0.05f, 20, "%.2f");
                        rebuild |= ImGui::IsItemDeactivatedAfterEdit();
                    }
                    if (rebuild) stage_rebuild();
                    ImGui::SeparatorText("Add from a stage");
                    std::string from = stageFromId >= 0 ? std::to_string(stageFromId) + "  " + stage_look(stageFromId) : "pick a stage";
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
                        ImGui::BeginChild("from", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 6), ImGuiChildFlags_Borders);
                        for (int k = 0; k < (int)stageFrom.parts.size(); k++) {
                            ImGui::PushID(k);
                            if (ImGui::SmallButton("add")) {
                                char pre[8];
                                snprintf(pre, sizeof pre, "s%02d_", stageFromId);
                                stageEdit.append(stageFrom, k, pre);
                                stagePart = (int)stageEdit.parts.size() - 1;
                                stage_rebuild();
                            }
                            ImGui::SameLine();
                            ImGui::Text("%s  (%d polys)", stageFrom.parts[k].name.c_str(), (int)stageFrom.parts[k].faces.size());
                            ImGui::PopID();
                        }
                        ImGui::EndChild();
                    }
                }
                ImGui::SeparatorText("Stages");
                ImGui::BeginChild("stagelist");
                for (const StageInfo &si : stageInfos) {
                    char l[96];
                    snprintf(l, sizeof l, "%2d  %s%s", si.id, stage_look(si.id), stageMods.count(si.fid) ? "  (mod)" : "");
                    if (ImGui::Selectable(l, si.id == stageId && view.stageView)) stage_open(si.id);
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (scene.model && ImGui::BeginTabItem("Model")) {
                Model &m = *scene.model;
                size_t polys = 0, verts = 0;
                for (const Mesh &me : m.meshes) polys += me.polys.size(), verts += me.verts.size();
                ImGui::Text("%d entries, %d bones, %d polygons, %d vertices", m.arc.count(), (int)m.bones.size() - 1,
                            (int)polys, (int)verts);
                ImGui::Text("bone list: entry %d, textures: %d, motions: %d", m.boneEntry, m.texEntry, m.motionEntry);
                ImGui::Separator();
                ImGui::Text("Textures");
                ImGui::SameLine();
                ImGui::RadioButton("off", &view.textures, 0);
                ImGui::SameLine();
                ImGui::RadioButton("PSX", &view.textures, 1);
                ImGui::SameLine();
                ImGui::RadioButton("bilinear", &view.textures, 2);
                ImGui::Text("Lights  ");
                ImGui::SameLine();
                ImGui::RadioButton("none", &view.lighting, 0);
                ImGui::SameLine();
                ImGui::RadioButton("viewer", &view.lighting, 1);
                ImGui::SameLine();
                ImGui::RadioButton("stage", &view.lighting, 2);
                if (view.lighting == 2 && !g_game.stages.empty()) {
                    ImGui::SliderInt("fight stage", &view.stage, 0, (int)g_game.stages.size() - 1);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A fight stage's three lights and ambient color (SFSTDATA.PRO)");
                }
                ImGui::Checkbox("Face", &view.face);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("The blinking eyes and the other animated face parts");
                ImGui::SameLine();
                ImGui::Checkbox("Wireframe", &view.wireframe);
                ImGui::SameLine();
                ImGui::Checkbox("Skeleton", &view.skeleton);
                ImGui::Checkbox("Back-face culling", &view.cull);
                ImGui::SameLine();
                ImGui::Checkbox("Grid", &view.grid);
                if (ImGui::Button("Show all")) std::fill(scene.hidden.begin(), scene.hidden.end(), false);
                ImGui::SameLine();
                if (ImGui::Button("Frame (F)")) frame_model(scene, view);
                ImGui::Separator();
                ImGui::BeginChild("bones");
                bone_tree(scene, 0);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (scene.model && ImGui::BeginTabItem("Motions")) {
                Model &m = *scene.model;
                if (m.motions.empty()) {
                    ImGui::TextWrapped("No motions in this file: it shows its first keys.");
                } else {
                    if (ImGui::Button(view.playing ? "Pause (space)" : "Play (space)")) view.playing = !view.playing;
                    ImGui::SameLine();
                    if (ImGui::Button("Step")) {
                        view.playing = false;
                        m.advance(1);
                        m.evaluate();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Restart")) m.setMotion(m.motion);
                    ImGui::SameLine();
                    ImGui::Checkbox("Loop", &view.loop);
                    ImGui::SliderFloat("speed", &view.speed, 0.05f, 4, "%.2fx");
                    int key = m.keyframe;
                    if (ImGui::SliderInt("keyframe", &key, 0, std::max(m.keyframeCount - 1, 0))) {
                        view.playing = false;
                        m.keyframe = key;
                        m.motionDone = false;
                        m.evaluate();
                    }
                    ImGui::Text("frame %d%s%s", m.frame, m.blending ? "  (blending)" : "", m.motionDone ? "  done" : "");
                    ImGui::Separator();
                    ImGui::BeginChild("motions");
                    for (int i = 1; i <= (int)m.motions.size(); i++) {
                        const std::vector<MotionStep> &st = m.motions[i - 1];
                        int frames = 0;
                        for (const MotionStep &x : st)
                            if (x.index != 0x7FFF) frames += std::max<int>(x.count, 0);
                        char label[64];
                        snprintf(label, sizeof label, "motion %2d   %d steps, %d frames", i, (int)st.size() - 1, frames);
                        if (ImGui::Selectable(label, m.motion == i)) {
                            m.setMotion(i);
                            view.playing = true;
                        }
                    }
                    ImGui::EndChild();
                }
                ImGui::EndTabItem();
            }
            if (scene.model && ImGui::BeginTabItem("Fighter")) {
                Model &m = *scene.model;
                if (scene.fighters.empty()) {
                    ImGui::TextWrapped("No fighter uses this file in the fighters' file (SMDLDATA.PRO): it has no "
                                       "name, face, cameras or action scripts there.");
                } else {
                    /* the fighters that use this file: each may name other entries */
                    if (scene.fighters.size() > 1) {
                        std::string cur = std::to_string(scene.fighters[scene.fighter]->id) + " " + scene.fighters[scene.fighter]->name;
                        if (ImGui::BeginCombo("fighter", cur.c_str())) {
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
                    }
                    const Fighter &f = *scene.fighters[scene.fighter];
                    Model &mm = *scene.model;
                    ImGui::Text("fighter %d, %s (kind 0x%X)", f.id, f.enemy ? "an enemy" : "a partner", f.kind);
                    ImGui::Text("entries: bones %d, motions %d, scripts %d", f.modelEntry, f.motionsEntry, f.effectsEntry);
                    int parts = 0;
                    for (bool u : f.faceUsed) parts += u;
                    ImGui::Text("face: %d parts", parts);
                    ImGui::SeparatorText("Cameras");
                    if (ImGui::Button("Free")) frame_model(scene, view);
                    for (int i = 0; i < (int)f.cameras.size(); i++) {
                        static const char *HINT[12] = {"", "", "", "", "", "", "portrait", "portrait, 2nd idle",
                                                       "battle", "battle", "battle", "battle"};
                        char l[48];
                        snprintf(l, sizeof l, "%d%s%s", i, !f.enemy && *HINT[i] ? " " : "", !f.enemy ? HINT[i] : "");
                        if (i % 4) ImGui::SameLine();
                        if (ImGui::RadioButton(l, view.gameCamera == i)) apply_camera(view, f, i);
                    }
                    ImGui::SeparatorText("Action scripts");
                    ImGui::TextDisabled("Click a motion to play it, a camera to look through it, an effect to see it.");
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
                                if (ln.motion > 0 && ln.motion <= (int)mm.motions.size()) {
                                    mm.setMotion(ln.motion);
                                    view.playing = true;
                                }
                                if (ln.effect >= 0) open_effect(ln.effect, true);
                                int c = ln.camera >= 100 ? ln.camera - 100 : ln.camera;
                                if (c >= 0 && c < (int)f.cameras.size() && (ln.camera >= 100) == f.enemy)
                                    apply_camera(view, f, c);
                            }
                            if (!active) ImGui::PopStyleColor();
                            ImGui::PopID();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Effects", nullptr, fxSelect ? ImGuiTabItemFlags_SetSelected : 0)) {
                fxSelect = false;
                fxActive = true;
                if (fx.id >= 0) {
                    if (fxFitPending) fx_fit(io.DisplaySize.x - panel, io.DisplaySize.y), fxFitPending = false;
                    int t = (int)fxTime;
                    if (t != fxDrawn) {
                        fx_render(t, FX_BG);
                        fxTex = rgba_texture(fxTex, fxPx, FXC_W, FXC_H);
                        fxDrawn = t;
                    }
                    /* in the view, where the model is: the canvas, the 320x240 screen
                       around the effect's point (what Export PNGs and own images
                       span) and the point */
                    ImDrawList *dl = ImGui::GetBackgroundDrawList();
                    float cx = (io.DisplaySize.x - panel) / 2 - fxPan[0] * fxZoom, cy = io.DisplaySize.y / 2 - fxPan[1] * fxZoom;
                    dl->AddImage((ImTextureID)(intptr_t)fxTex, ImVec2(cx - FXC_W / 2 * fxZoom, cy - FXC_H / 2 * fxZoom),
                                 ImVec2(cx + FXC_W / 2 * fxZoom, cy + FXC_H / 2 * fxZoom));
                    dl->AddRect(ImVec2(cx - FX_W / 2 * fxZoom, cy - FX_H / 2 * fxZoom), ImVec2(cx + FX_W / 2 * fxZoom, cy + FX_H / 2 * fxZoom),
                                IM_COL32(255, 255, 255, 60));
                    dl->AddLine(ImVec2(cx - 8, cy), ImVec2(cx + 8, cy), IM_COL32(255, 220, 80, 160));
                    dl->AddLine(ImVec2(cx, cy - 8), ImVec2(cx, cy + 8), IM_COL32(255, 220, 80, 160));
                    if (ImGui::Button("Fit (F)")) fxFitPending = true;
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Drag to move, wheel to zoom. The box is the 320x240 screen around the effect's point (the cross).");
                    ImGui::TextWrapped("effect %d: sheet %d, %d sprite animations, %d fields", fx.id, fx.sheetIndex,
                                       (int)fx.anims.size(), fx.duration);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    ImGui::TextWrapped("animations %s #%d entry %d, sheet %s #%d entry %d", fid_name(fx.fid).c_str(), fx.fid,
                                       fx.entry, fid_name(fx.sheetFid).c_str(), fx.sheetFid, fx.sheetEntry);
                    ImGui::PopStyleColor();
                    if (ImGui::Button(fxPlaying ? "Pause" : "Play")) fxPlaying = !fxPlaying;
                    same_line(bw("Restart"));
                    if (ImGui::Button("Restart")) fxTime = 0;
                    same_line(bw("Loop") + ImGui::GetFrameHeight());
                    ImGui::Checkbox("Loop##fx", &fxLoop);
                    same_line(bw("Export PNGs"));
                    if (ImGui::Button("Export PNGs")) {
                        int n = export_effect(fx, fxDir);
                        status = "saved " + std::to_string(n) + " frames to effects/effect_" + std::to_string(fx.id);
                    }
                    if (!fxDir.empty()) {
                        same_line(bw("Open folder"));
                        if (ImGui::Button("Open folder")) open_folder(fxDir);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", fxDir.c_str());
                    }
                    ImGui::SliderFloat("speed##fx", &view.speed, 0.05f, 4, "%.2fx");
                    float ft = fxTime;
                    if (ImGui::SliderFloat("tick", &ft, 0, (float)std::max(fx_length() - 1, 0), "%.0f")) fxTime = ft, fxPlaying = false;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("1 tick = 1/60 s, as the battle counts time");
                    if (ImGui::CollapsingHeader("Own images (native port)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        bool changed = false;
                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                        ImGui::TextWrapped("%s", own.dir.c_str());
                        ImGui::PopStyleColor();
                        if (ImGui::Button("Mods folder...")) {
                            SDL_ShowOpenFolderDialog(
                                [](void *u, const char *const *list, int) {
                                    if (!list || !*list) return;
                                    Picked *p = (Picked *)u;
                                    std::lock_guard<std::mutex> lock(p->m);
                                    p->folder = *list;
                                },
                                &picked, win, ownRoot.c_str(), false);
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The native port's data/mods/effects folder");
                        same_line(bw("Open"));
                        if (ImGui::Button("Open##own")) {
                            SDL_CreateDirectory(own.dir.c_str());
                            open_folder(own.dir);
                        }
                        same_line(bw("Hit") + ImGui::GetFrameHeight());
                        if (ImGui::RadioButton("Hit", !ownCritical)) ownCritical = false, own_load();
                        same_line(bw("Critical hit") + ImGui::GetFrameHeight());
                        if (ImGui::RadioButton("Critical hit", ownCritical)) ownCritical = true, own_load();
                        if (ImGui::Checkbox("Show them", &ownShow)) fxTime = 0, fxDrawn = -1;
                        same_line(bw("Import PNGs..."));
                        if (ImGui::Button("Import PNGs...")) {
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
                        same_line(bw("Save*"));
                        if (ImGui::Button(ownDirty ? "Save*" : "Save")) {
                            if (own.save()) ownDirty = false, ownIds = own_effects(ownRoot), status = "saved " + own.dir + "/effect.json";
                            else status = own.error;
                        }
                        same_line(bw("Reload"));
                        if (ImGui::Button("Reload")) own_load();
                        ImGui::TextWrapped("%d frames, %d ticks = %.2f s (the original: %d ticks = %.2f s)", (int)own.frames.size(),
                                    own.duration(), own.duration() / 60.0f, fx.duration, fx.duration / 60.0f);
                        /* two to a row, each half the panel with its label */
                        float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2 - bw("scale");
                        ImGui::PushItemWidth(half);
                        changed |= ImGui::DragFloat("x", &own.x, 0.5f, -320, 320, "%.1f");
                        ImGui::SameLine();
                        changed |= ImGui::DragFloat("y", &own.y, 0.5f, -240, 240, "%.1f");
                        changed |= ImGui::DragFloat("scale", &own.scale, 0.01f, 0.05f, 8, "%.2f");
                        ImGui::SameLine();
                        int blend = own.add;
                        if (ImGui::Combo("blend", &blend, "alpha\0add\0")) own.add = blend != 0, changed = true;
                        ImGui::PopItemWidth();
                        int playing = ownShow ? own.frameAt((int)fxTime) : -1, move = -1, dir = 0, drop = -1, dup = -1;
                        if (!own.frames.empty() && ImGui::BeginTable("ownframes", 4, ImGuiTableFlags_RowBg)) {
                            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed);
                            ImGui::TableSetupColumn("image", ImGuiTableColumnFlags_WidthStretch);
                            ImGui::TableSetupColumn("ticks", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 6);
                            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
                            ImGui::TableHeadersRow();
                            for (int i = 0; i < (int)own.frames.size(); i++) {
                                CustomFrame &fr = own.frames[i];
                                ImGui::PushID(i);
                                ImGui::TableNextRow();
                                if (i == playing) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(70, 90, 140, 160));
                                ImGui::TableNextColumn();
                                ImGui::Text("%d", i);
                                ImGui::TableNextColumn();
                                ImGui::SetNextItemWidth(-FLT_MIN);
                                if (ImGui::BeginCombo("##file", fr.file.c_str())) {
                                    for (const auto &it : own.images)
                                        if (ImGui::Selectable(it.first.c_str(), it.first == fr.file)) fr.file = it.first, changed = true;
                                    ImGui::EndCombo();
                                }
                                ImGui::TableNextColumn();
                                ImGui::SetNextItemWidth(-FLT_MIN);
                                if (ImGui::InputInt("##ticks", &fr.ticks)) fr.ticks = std::clamp(fr.ticks, 1, 600), changed = true;
                                ImGui::TableNextColumn();
                                if (ImGui::SmallButton("^")) move = i, dir = -1;
                                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Up");
                                ImGui::SameLine();
                                if (ImGui::SmallButton("v")) move = i, dir = 1;
                                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Down");
                                ImGui::SameLine();
                                if (ImGui::SmallButton("+")) dup = i;
                                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy");
                                ImGui::SameLine();
                                if (ImGui::SmallButton("x")) drop = i;
                                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove");
                                ImGui::PopID();
                            }
                            ImGui::EndTable();
                        }
                        if (move >= 0 && move + dir >= 0 && move + dir < (int)own.frames.size())
                            std::swap(own.frames[move], own.frames[move + dir]), changed = true;
                        if (dup >= 0) {
                            CustomFrame copy = own.frames[dup];
                            own.frames.insert(own.frames.begin() + dup + 1, copy), changed = true;
                        }
                        if (drop >= 0) own.frames.erase(own.frames.begin() + drop), changed = true;
                        if (changed) ownDirty = true, fxDrawn = -1;
                        if (!own.error.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", own.error.c_str());
                    }
                    ImGui::Separator();
                } else {
                    ImGui::TextWrapped("Pick an effect: the battle's 2D effects, its sparks, hits and techniques' sprites.");
                }
                if (!fxGrouped) group_effects(), ownIds = own_effects(ownRoot);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputTextWithHint("##fxfind", "Search: Digimon, id or technique", fxFind, sizeof fxFind);
                if (ImGui::BeginTabBar("fxlists")) {
                    if (ImGui::BeginTabItem("Original")) {
                        ImGui::BeginChild("fxlist");
                        if (*fxFind) {
                            for (int id : fxIds)
                                if (effect_found(id)) effect_item(id);
                        } else if (ImGui::CollapsingHeader("Common", ImGuiTreeNodeFlags_DefaultOpen)) {
                            ImGui::TextDisabled("The battle's own (hits, digivolving, items) and those several Digimon start");
                            for (int id : fxCommon) effect_item(id);
                        }
                        if (!*fxFind && ImGui::CollapsingHeader("By Digimon", ImGuiTreeNodeFlags_DefaultOpen)) {
                            for (const auto &it : fxByDigimon) {
                                bool open = std::find(it.second.begin(), it.second.end(), fx.id) != it.second.end();
                                ImGui::SetNextItemOpen(open, ImGuiCond_Once);
                                if (ImGui::TreeNode(it.first.c_str())) {
                                    for (int id : it.second) effect_item(id);
                                    ImGui::TreePop();
                                }
                            }
                        }
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }
                    char modTab[32];
                    snprintf(modTab, sizeof modTab, "Mod (%d)###mod", (int)ownIds.size());
                    if (ImGui::BeginTabItem(modTab)) {
                        ImGui::BeginChild("fxmods");
                        if (ownIds.empty()) ImGui::TextWrapped("No effect has own images in %s yet.", ownRoot.c_str());
                        for (int id : std::set<int>(ownIds)) /* a copy: opening one rescans ownIds */
                            if (effect_found(id)) effect_item(id);
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
                ImGui::EndTabItem();
            }
            if (scene.model && ImGui::BeginTabItem("Textures")) {
                Model &m = *scene.model;
                /* the texture page and CLUT pairs its polygons read */
                std::vector<std::pair<uint16_t, uint16_t>> combos;
                std::set<std::pair<uint16_t, uint16_t>> seen;
                for (const Mesh &me : m.meshes)
                    for (const Poly &p : me.polys)
                        if (p.textured && seen.insert({(uint16_t)(p.tpage & 0x19F), p.clut}).second)
                            combos.push_back({(uint16_t)(p.tpage & 0x19F), p.clut});
                for (const Tim &t : m.tims)
                    ImGui::Text("TIM %dx%d halfwords at %d,%d%s", t.w, t.h, t.x, t.y, t.rlen ? " (RLEN)" : "");
                if (combos.empty()) {
                    ImGui::Text("No textured polygons.");
                } else {
                    view.texCombo = std::min(view.texCombo, (int)combos.size() - 1);
                    for (int i = 0; i < (int)combos.size(); i++) {
                        char label[64];
                        static const char *DEPTH[] = {"4-bit", "8-bit", "16-bit", "?"};
                        snprintf(label, sizeof label, "%s page %d,%d CLUT %d,%d", DEPTH[tpage_depth(combos[i].first)],
                                 tpage_x(combos[i].first), tpage_y(combos[i].first), clut_x(combos[i].second),
                                 clut_y(combos[i].second));
                        ImGui::RadioButton(label, &view.texCombo, i);
                    }
                    auto [tp, cl] = combos[view.texCombo];
                    int w = 256, h = 256;
                    std::string key = m.name + std::to_string(tp) + "/" + std::to_string(cl);
                    if (key != texKey) {
                        texPixels = decode_page(m.vram, tp, cl, w, h);
                        texView = rgba_texture(texView, texPixels, w, h);
                        texKey = key, texW = w, texH = h;
                    }
                    ImGui::Checkbox("UVs", &view.uvs);
                    ImGui::SameLine();
                    ImGui::SliderFloat("zoom", &view.texZoom, 1, 4, "%.1fx");
                    if (ImGui::Button("Save BMP")) {
                        char name[96];
                        snprintf(name, sizeof name, "%s_%04x_%04x.bmp", m.name.substr(0, m.name.find('.')).c_str(), tp, cl);
                        save_bmp(g_exe_dir + name, texPixels, texW, texH);
                        status = std::string("saved ") + name;
                    }
                    ImGui::BeginChild("tex", ImVec2(0, 0), 0, ImGuiWindowFlags_HorizontalScrollbar);
                    ImVec2 o = ImGui::GetCursorScreenPos();
                    float z = view.texZoom;
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    dl->AddRectFilled(o, ImVec2(o.x + texW * z, o.y + texH * z), IM_COL32(40, 40, 48, 255));
                    ImGui::Image((ImTextureID)(intptr_t)texView, ImVec2(texW * z, texH * z));
                    if (view.uvs) {
                        for (size_t b = 1; b < m.bones.size(); b++) {
                            for (const Poly &p : m.meshes[m.bones[b].mesh].polys) {
                                if (!p.textured || (p.tpage & 0x19F) != tp || p.clut != cl) continue;
                                static const int ORDER[2][4] = {{0, 1, 2, -1}, {0, 1, 3, 2}};
                                const int *ord = ORDER[p.quad];
                                int n = p.quad ? 4 : 3;
                                ImU32 c = (int)b == scene.selected ? IM_COL32(255, 220, 60, 255) : IM_COL32(80, 255, 120, 160);
                                for (int k = 0; k < n; k++) {
                                    const uint8_t *a = p.uv[ord[k]], *bb = p.uv[ord[(k + 1) % n]];
                                    dl->AddLine(ImVec2(o.x + (a[0] + 0.5f) * z, o.y + (a[1] + 0.5f) * z),
                                                ImVec2(o.x + (bb[0] + 0.5f) * z, o.y + (bb[1] + 0.5f) * z), c);
                                }
                            }
                        }
                    }
                    ImGui::EndChild();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
        about_window(aboutOpen, aboutIcon);
        ImGui::Render();

        if (fxActive) { /* the effect, drawn by ImGui over the cleared view */
            GL(glViewport)(0, 0, ww, wh);
            GL(glClearColor)((FX_BG & 255) / 255.0f, (FX_BG >> 8 & 255) / 255.0f, (FX_BG >> 16 & 255) / 255.0f, 1);
            GL(glClear)(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        } else {
            render(scene, view, ww - (int)(panel * ww / std::max(io.DisplaySize.x, 1.0f)), wh);
        }
        GL(glViewport)(0, 0, ww, wh);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(win);
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
