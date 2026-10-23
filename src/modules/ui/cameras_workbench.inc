    auto cameras_inspector = [&]() {
        const Fighter *fp = cams_fighter();
        if (!fp) return;
        const Fighter &f = *fp;
        Model &mm = *scene.model;
        libs_load();
        Application &app = choreo.app;
        if (choreo.has && app.fighter != f.id) choreo.has = choreo.on = false, choreo.eff.clear();
        const Fighter &donor = g_game.fighters[cand.fighter];
        /* the script on screen, and the way to run it again */
        if (cand.script >= 0 && cand.of == &f) {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Script %d", cand.script);
            ImGui::SameLine();
            if (button(run.on && run.script == cand.script ? IC_RESTART "  Play again" : IC_PLAY "  Play the script")) run_script(cand.script);
            if (run.on) {
                ImGui::SameLine();
                if (button(IC_PAUSE "  Stop")) run.on = false, free_fx_models();
            }
        } else {
            hint("Play a script in Fighter, or load an entry from the Library.");
        }
        b_source();
        auto tab_camera = [&]() {
            auto pick = cand_pick;
            if (!cand_ensure(f)) return hint("This fighter is not in the fighters' list.");
            hint("A and B are under the view (key B): B replaces the camera the script asks for with the candidate, at the same moments.");
            ImGui::Checkbox("Also at the cuts the player skips", &cand.every);
            tip("The script's explicit cameras and the other side's shots: the player doesn't move the view for them, the candidate does");
            const Fighter &donor = g_game.fighters[cand.fighter];
            /* origin (where a fighter comes from) is not the context (the data set loaded): shown only with a mod's context loaded; the Library's filter row sets it */
            const bool modded = g_context != "base_game";
            auto origin_tag = [&](const Fighter &d) { return modded ? std::string(", origin: ") + fighter_origin(d.id) : std::string(); };
            if (modded && !libf.origin.empty()) hint("Donors of origin %s (set in the Library)", libf.origin.c_str());
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##donor", (donor.name + " (fighter " + std::to_string(donor.id) + origin_tag(donor) + ")").c_str())) {
                for (int i = 0; i < (int)g_game.fighters.size(); i++) {
                    const Fighter &d = g_game.fighters[i];
                    if (d.enemy != f.enemy || d.cameras.empty()) continue;
                    if (modded && !libf.origin.empty() && libf.origin != fighter_origin(d.id) && i != cand.fighter) continue;
                    ImGui::PushID(i);
                    if (ImGui::Selectable((d.name + " (fighter " + std::to_string(d.id) + origin_tag(d) + ")").c_str(), i == cand.fighter)) pick(i, cand.cam);
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##donorcam", camera_role(donor.enemy, cand.cam).name)) {
                for (int i = 0; i < (int)donor.cameras.size(); i++)
                    if (ImGui::Selectable(camera_role(donor.enemy, i).name, i == cand.cam)) pick(cand.fighter, i);
                ImGui::EndCombo();
            }
            /* the native values, as the game stores them (battle space) */
            int pos[3] = {cand.c.pos[0], cand.c.pos[1], cand.c.pos[2]}, ref[3] = {cand.c.ref[0], cand.c.ref[1], cand.c.ref[2]}, proj = cand.c.proj;
            bool ch = ImGui::DragInt3("Eye", pos, 4);
            ch |= ImGui::DragInt3("Target", ref, 4);
            ch |= ImGui::DragInt("Projection", &proj, 1, 1, 32767);
            if (ch) {
                for (int k = 0; k < 3; k++) cand.c.pos[k] = (int16_t)pos[k], cand.c.ref[k] = (int16_t)ref[k];
                cand.c.proj = (int16_t)proj;
                cand_edited();
            }
            if (button(IC_CAMERA "  Look through it")) apply_game_camera(view, f, cand.c, -1);
            ImGui::SameLine();
            if (button("Reset to the donor's")) pick(cand.fighter, cand.cam);
            if (button(IC_FIT "  Fit to this model")) {
                GameCamera fitted;
                double k;
                if (fit_camera(donor, cand.c, f, fitted, k)) {
                    cand.before = cand.c, cand.c = fitted, cand.fitK = k;
                    cand_edited();
                    note(NOTE_INFO, "Fitted: the donor's framing, scaled by " + std::to_string(k).substr(0, 5) + " to this model. Check it in the script.");
                } else {
                    note(NOTE_WARN, "A model's box couldn't be read, so there is nothing to fit by.");
                }
            }
            tip("Moves the eye and the target so this model is framed as the donor's was: the boxes' centers match and distances scale with their sizes. The projection stays. It is a proposal: proportions and pose are not in a box");
            if (cand.fitK > 0) {
                ImGui::SameLine();
                if (button("Undo the fit")) {
                    if (cand.before.proj) cand.c = cand.before;
                    cand.fitK = 0;
                    cand_edited();
                }
                ImGui::TextColored(C_WARN, "Fitted (x%.2f): a proposal, not checked", cand.fitK);
            }

        };
        auto tab_sequence = [&]() {
            /* the donor: a fighter of this one's side with scripts, and one of its scripts with camera commands */
            if (choreo.donor < 0 || choreo.donor >= (int)g_game.fighters.size() || g_game.fighters[choreo.donor].enemy != f.enemy) {
                choreo.donor = -1;
                for (int i = 0; i < (int)g_game.fighters.size(); i++)
                    if (&g_game.fighters[i] == &f) choreo.donor = i;
            }
            auto load_dopt = [&]() {
                choreo.dopt.clear();
                std::vector<std::vector<ScriptLine>> sc = fighter_scripts(g_game.fighters[choreo.donor]);
                for (size_t i = 0; i < sc.size(); i++)
                    if (size_t n = script_cams(sc[i]).size()) choreo.dopt.push_back({(int)i, (int)n});
                choreo.donorOf = choreo.donor;
                if (std::none_of(choreo.dopt.begin(), choreo.dopt.end(), [&](auto &o) { return o.first == choreo.donorScript; }))
                    choreo.donorScript = choreo.dopt.empty() ? -1 : choreo.dopt[0].first;
        };
            if (choreo.donor >= 0) {
                if (choreo.donorOf != choreo.donor) load_dopt();
                const Fighter &dn = g_game.fighters[choreo.donor];
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##cdonor", (dn.name + " (fighter " + std::to_string(dn.id) + ")").c_str())) {
                    for (int i = 0; i < (int)g_game.fighters.size(); i++) {
                        const Fighter &d = g_game.fighters[i];
                        if (d.enemy != f.enemy || d.effectsFid <= 0) continue;
                        ImGui::PushID(i);
                        if (ImGui::Selectable((d.name + " (fighter " + std::to_string(d.id) + ")").c_str(), i == choreo.donor)) choreo.donor = i;
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                if (choreo.donorOf != choreo.donor) load_dopt();
                char dl[80] = "(no script with a camera command)";
                for (auto &o : choreo.dopt)
                    if (o.first == choreo.donorScript) snprintf(dl, sizeof dl, "Donor script %d: %d camera commands", o.first, o.second);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##cdscript", dl)) {
                    for (auto &o : choreo.dopt) {
                        char l[80];
                        snprintf(l, sizeof l, "Script %d: %d camera commands", o.first, o.second);
                        if (ImGui::Selectable(l, o.first == choreo.donorScript)) choreo.donorScript = o.first;
                    }
                    ImGui::EndCombo();
                }
                /* the target: one of this fighter's scripts with camera commands */
                std::vector<std::pair<int, int>> topt;
                for (int i = 0; i < (int)mm.scripts.size(); i++)
                    if (size_t n = script_cams(mm.scripts[i]).size()) topt.push_back({i, (int)n});
                if (std::none_of(topt.begin(), topt.end(), [&](auto &o) { return o.first == choreo.targetScript; }))
                    choreo.targetScript = topt.empty() ? -1 : (cand.script >= 0 && std::any_of(topt.begin(), topt.end(), [&](auto &o) { return o.first == cand.script; }) ? cand.script : topt[0].first);
                char tl[80] = "(no script with a camera command)";
                for (auto &o : topt)
                    if (o.first == choreo.targetScript) snprintf(tl, sizeof tl, "On this script %d: %d camera commands", o.first, o.second);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##ctscript", tl)) {
                    for (auto &o : topt) {
                        char l[80];
                        snprintf(l, sizeof l, "Script %d: %d camera commands", o.first, o.second);
                        if (ImGui::Selectable(l, o.first == choreo.targetScript)) choreo.targetScript = o.first;
                    }
                    ImGui::EndCombo();
                }
                ImGui::Checkbox("Fit to this model", &choreo.fit);
                tip("Scales the donor's cameras to this model as one sequence (the same similarity as the candidate's Fit); rotation, roll and projection are carried as they are");
                ImGui::BeginDisabled(choreo.donorScript < 0 || choreo.targetScript < 0);
                if (button(IC_PLAY "  Build and play", 1)) {
                    choreo.app = app_new(dn, choreo.donorScript, f, choreo.targetScript, choreo.fit);
                    choreo.has = choreo.on = true;
                    choreo.active.clear();
                    choreo_refresh();
                    run_script(choreo.targetScript);
                    set_ab(true);
                }
                ImGui::EndDisabled();
                tip("Maps the donor's camera commands to this script's by order, then plays the script with B on the sequence. Nothing is saved until you save it");
            }

            if (choreo.has) {
                heading("The sequence");
                for (const std::string &w : app.warn) ImGui::TextColored(C_WARN, "%s", w.c_str());
                bool changed = false;
                bool fitted = app.fitK > 0;
                if (ImGui::Checkbox("Fitted to this model", &fitted)) {
                    const Fighter *d = fighter_by_id(app.donor, false);
                    FitFrame ff = d ? fit_frame(*d, f) : FitFrame();
                    app.fitK = fitted && ff.ok ? ff.k : 0;
                    changed = true;
                }
                if (app.fitK > 0) {
                    ImGui::SameLine();
                    ImGui::TextColored(C_WARN, "x%.2f: a proposal, not checked", app.fitK);
                }
                std::vector<TplShot> tcmd;
                if (app.scriptIdx >= 0 && app.scriptIdx < (int)mm.scripts.size()) tcmd = script_cams(mm.scripts[app.scriptIdx]);
                for (int k = 0; k < (int)app.shots.size(); k++) {
                    AppShot &s = app.shots[k];
                    ImGui::PushID(k);
                    char l[160];
                    snprintf(l, sizeof l, "%s%d  %s, line %d, over %d  ->  %s###shot", s.enabled ? "" : "(off) ", k + 1, APP_KIND[s.s.kind], s.s.line, s.s.time,
                             s.to < 0 ? "nowhere" : ("command " + std::to_string(s.to + 1)).c_str());
                    if (ImGui::TreeNode(l)) {
                        changed |= ImGui::Checkbox("On", &s.enabled);
                        tip("Off: the script's own camera stays at that command");
                        char gl[64] = "none";
                        if (s.to >= 0) snprintf(gl, sizeof gl, "command %d", s.to + 1);
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::BeginCombo("Goes to", gl)) {
                            if (ImGui::Selectable("none", s.to < 0)) s.to = -1, changed = true;
                            for (int j = 0; j < (int)tcmd.size(); j++) {
                                char o[96];
                                snprintf(o, sizeof o, "command %d (line %d, %s)", j + 1, tcmd[j].line, APP_KIND[tcmd[j].kind]);
                                if (ImGui::Selectable(o, s.to == j)) s.to = j, changed = true;
                            }
                            ImGui::EndCombo();
                        }
                        if (s.s.kind == 2) hint("Donor: %s", words_text(s.s.w).c_str());
                        const EffShot *e = s.to >= 0 && s.to < (int)choreo.eff.size() ? &choreo.eff[s.to] : nullptr;
                        if (e) {
                            hint("Here: %s, over %d%s%s", e->source, e->time, e->kind == 2 ? ", " : "", e->kind == 2 ? words_text(e->w).c_str() : "");
                            if (e->kind == 2 && button(IC_CAMERA "  Look through it")) apply_cam_words(view, f, e->w, -1);
                        }
                        bool ov = s.has;
                        if (ImGui::Checkbox("Override this shot", &ov)) {
                            s.has = ov, changed = true;
                            if (ov) {
                                memcpy(s.ow, e && e->kind == 2 ? e->w : s.s.w, sizeof s.ow);
                                s.otime = -1;
                            }
                        }
                        if (s.has) {
                            bool c2 = ImGui::DragInt3("Eye", s.ow, 4, -32768, 32767);
                            c2 |= ImGui::DragInt3("Target", s.ow + 3, 4, -32768, 32767);
                            c2 |= ImGui::DragInt3("Translation", s.ow + 6, 4, -32768, 32767);
                            c2 |= ImGui::DragInt("Projection", &s.ow[13], 1, 1, 32767);
                            c2 |= ImGui::DragInt("Over (-1: the donor's)", &s.otime, 1, -1, 4096);
                            if (s.ow[9] || s.ow[10] || s.ow[11] || s.ow[12]) hint("Rotation %d, %d, %d and roll %d are carried as they are", s.ow[9], s.ow[10], s.ow[11], s.ow[12]);
                            if (button("Reset the shot")) s.has = false, c2 = true;
                            changed |= c2;
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                for (const std::string &n : choreo.notes) hint("%s", n.c_str());
                if (changed) choreo_refresh();

            }
        };
        auto tab_details = [&]() {
            hint("One form for both: the name, notes and status are kept with the entry; the variant belongs to applications.");
            lib_form(form);
            tip("The status is of this combination only: this fighter, this script and this donor camera");
            heading("Save the candidate camera as a preset");
            auto make = [&]() {
                Preset p;
                p.name = form.name, p.notes = form.notes, p.status = form.status_name();
                p.fighter = f.id;
                p.scriptFid = f.effectsFid, p.scriptEntry = f.effectsEntry, p.scriptIdx = cand.script;
                p.donor = donor.id, p.donorEnemy = donor.enemy, p.donorIdx = cand.cam;
                p.c = cand.c, p.every = cand.every, p.fitK = cand.fitK;
                p.context = p.donorContext = g_context;
                preset_check(p);
                return p;
            };
            bool canSave = !pre.blocked && cand.script >= 0 && cand.of == &f;
            ImGui::BeginDisabled(!canSave);
            if (button(IC_SAVE "  Save as preset", 1)) {
                keep_or_ask<Preset>(
                    ask, pre.list, make(), "Replace the preset",
                    [](const Preset &o) { return "\"" + o.name + "\" is already saved for this fighter, script and donor camera. Replace it with the one on screen?"; },
                    [&](const Preset &p) {
                        pre.active = p.id();
                        preset_persist(pre.list, pre.error);
                    });
            }
            ImGui::EndDisabled();
            if (!canSave && !pre.blocked) hint("Play one of this fighter's scripts first: a preset is made for the script it was seen with.");
            if (canSave) hint("For script %d, donor %s", cand.script, comp::cam_id(donor.id, donor.enemy, cand.cam).c_str());

            heading("Save the sequence as an application");
            if (choreo.has) {
                ImGui::BeginDisabled(choreo.blocked);
                if (button(IC_SAVE "  Save as application", 1)) {
                    Application p = app;
                    p.name = form.name, p.variant = form.variant, p.notes = form.notes, p.status = form.status_name();
                    p.context = p.donorContext = g_context;
                    app_check(p);
                    keep_or_ask<Application>(
                        ask, choreo.list, p, "Replace the application",
                        [](const Application &o) { return "\"" + o.name + "\" is already saved for this donor script and this script (variant \"" + o.variant + "\"). Replace it with the one on screen?"; },
                        [&](const Application &q) {
                            choreo.active = q.id();
                            choreo.app.variant = q.variant;
                            app_persist(choreo.list, choreo.error);
                        });
                }
                ImGui::EndDisabled();
            } else {
                hint("Build a sequence in the Sequence tab to keep it as an application.");
            }
        };
        if (ImGui::BeginTabBar("##camtabs")) {
            static const char *const NAMES[3] = {"Camera", "Sequence", "Details"};
            for (int k = 0; k < 3; k++) {
                if (ImGui::BeginTabItem(NAMES[k], nullptr, camTabGo == k ? ImGuiTabItemFlags_SetSelected : 0)) {
                    ImGui::BeginChild("##camtab");
                    k == 0 ? tab_camera() : k == 1 ? tab_sequence() : tab_details();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
        camTabGo = -1;
    };
