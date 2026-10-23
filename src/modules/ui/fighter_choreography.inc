        if (ImGui::CollapsingHeader("Choreography")) {
            Application &app = choreo.app;
            if (choreo.has && app.fighter != f.id) choreo.has = choreo.on = false, choreo.eff.clear();
            hint("A donor script's camera sequence on one of this fighter's scripts. B shows it in place of the candidate camera; the scripts and the game's files are not changed.");
            if (!choreo.loaded) {
                choreo.loaded = true;
                std::string text = preset_text(app_path());
                std::vector<std::string> skipped;
                if (!text.empty() && !app_parse(text, choreo.list, choreo.error, skipped)) choreo.blocked = true, choreo.list.clear(), choreo.error = "applications.json: " + choreo.error + ". It is left as it is; nothing is saved over it.";
                for (const std::string &s : skipped) choreo.msg += s + "\n";
            }
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
                if (ImGui::RadioButton("A  The script's cameras##ch", !cand.on)) set_ab(false);
                ImGui::SameLine();
                if (ImGui::RadioButton("B  The sequence##ch", cand.on)) set_ab(true);
                tip("Switches only the view, on the frame that is showing (key B)");
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
                if (ImGui::Checkbox("B uses the sequence", &choreo.on)) choreo_refresh();

                /* --- keep it --- */
                heading("Save");
                ImGui::SetNextItemWidth(-1);
                ImGui::InputTextWithHint("##aname", "Name", choreo.name, sizeof choreo.name);
                ImGui::SetNextItemWidth(-1);
                ImGui::InputTextWithHint("##avar", "Variant (to keep several for this script)", choreo.variant, sizeof choreo.variant);
                ImGui::SetNextItemWidth(-1);
                ImGui::InputTextWithHint("##anotes", "Notes", choreo.notes_, sizeof choreo.notes_);
                ImGui::SetNextItemWidth(-1);
                ImGui::Combo("##astatus", &choreo.status, "Candidate\0Approved\0Rejected\0");
                ImGui::BeginDisabled(choreo.blocked);
                if (button(IC_SAVE "  Save as application", 1)) {
                    Application p = app;
                    p.name = choreo.name, p.variant = choreo.variant, p.notes = choreo.notes_, p.status = PRESET_STATUS[std::clamp(choreo.status, 0, 2)];
                    p.context = p.donorContext = g_context;
                    app_check(p);
                    auto it = std::find_if(choreo.list.begin(), choreo.list.end(), [&](const Application &o) { return o.id() == p.id(); });
                    auto put = [&, p, idx = (int)(it - choreo.list.begin()), has = it != choreo.list.end()]() {
                        if (has && idx < (int)choreo.list.size()) choreo.list[idx] = p;
                        else choreo.list.push_back(p);
                        choreo.active = p.id();
                        choreo.app.variant = p.variant;
                        app_persist(choreo.list, choreo.error);
                    };
                    if (it == choreo.list.end()) put();
                    else {
                        ask.title = "Replace the application", ask.text = "\"" + it->name + "\" is already saved for this donor script and this script (variant \"" + it->variant + "\"). Replace it with the one on screen?";
                        ask.ok = "Replace", ask.danger = true, ask.open = true, ask.then = put;
                    }
                }
                ImGui::EndDisabled();
            }
            if (!choreo.error.empty()) ImGui::TextColored(C_WARN, "%s", choreo.error.c_str());
            if (!choreo.msg.empty()) ImGui::TextWrapped("%s", choreo.msg.c_str());

            heading("Saved");
            int mineA = 0;
            for (int i = 0; i < (int)choreo.list.size(); i++) {
                Application &a = choreo.list[i];
                if (a.fighter != f.id) continue;
                mineA++;
                ImGui::PushID(5000 + i);
                char l[256];
                snprintf(l, sizeof l, "%s%s%s  [%s]  script %d <- %d.%d:%d%s%s###a", a.warn.empty() ? "" : IC_WARN " ", a.name.empty() ? "(no name)" : a.name.c_str(),
                         a.variant.empty() ? "" : (" (" + a.variant + ")").c_str(), a.status.c_str(), a.scriptIdx, a.donorFid, a.donorEntry, a.donorIdx,
                         a.context == "base_game" ? "" : ", context ", a.context == "base_game" ? "" : a.context.c_str());
                bool soft = std::all_of(a.warn.begin(), a.warn.end(), [](const std::string &w) { return w.find("snapshot") != std::string::npos; });
                if (ImGui::Selectable(l, choreo.active == a.id()) && soft) {
                    choreo.app = a;
                    choreo.has = choreo.on = true;
                    choreo.active = a.id();
                    snprintf(choreo.name, sizeof choreo.name, "%s", a.name.c_str());
                    snprintf(choreo.variant, sizeof choreo.variant, "%s", a.variant.c_str());
                    snprintf(choreo.notes_, sizeof choreo.notes_, "%s", a.notes.c_str());
                    choreo.status = a.status == PRESET_STATUS[1] ? 1 : a.status == PRESET_STATUS[2] ? 2 : 0;
                    choreo_refresh();
                    run_script(a.scriptIdx);
                    set_ab(true);
                }
                if (!a.warn.empty()) {
                    std::string t;
                    for (const std::string &w : a.warn) t += w + "\n";
                    tip("%s%s", soft ? "Can be used (its snapshot is):\n" : "Can't be used here:\n", t.c_str());
                } else {
                    tip("Click to load it and play its script");
                }
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem(IC_COPY "  Duplicate")) {
                        Application c = a;
                        for (int n = 1; n < 100; n++) {
                            c.variant = n == 1 ? "copy" : "copy" + std::to_string(n);
                            if (std::none_of(choreo.list.begin(), choreo.list.end(), [&](const Application &o) { return o.id() == c.id(); })) break;
                        }
                        c.name = (a.name.empty() ? "(no name)" : a.name) + " (copy)";
                        choreo.list.push_back(c);
                        if (!choreo.blocked) app_persist(choreo.list, choreo.error);
                    }
                    if (ImGui::MenuItem(IC_DELETE "  Delete the application")) {
                        std::string id = a.id();
                        ask.title = "Delete the application", ask.text = "Delete \"" + a.name + "\"? It can't be undone (the previous file is kept as applications.json.bak).", ask.ok = "Delete", ask.danger = true, ask.open = true;
                        ask.then = [&, id]() {
                            choreo.list.erase(std::remove_if(choreo.list.begin(), choreo.list.end(), [&](const Application &o) { return o.id() == id; }), choreo.list.end());
                            if (choreo.active == id) choreo.active.clear();
                            app_persist(choreo.list, choreo.error);
                        };
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            hint(mineA ? "Right-click an application to duplicate or delete it." : "No application for this fighter yet.");
            if (button(IC_IMPORT "  Import##apps")) {
                static const SDL_DialogFileFilter JSONF = {"Applications", "json"};
                SDL_ShowOpenFileDialog(
                    [](void *u, const char *const *list, int) {
                        if (!list || !*list) return;
                        Picked *p = (Picked *)u;
                        std::lock_guard<std::mutex> lock(p->m);
                        p->appIn = *list;
                    },
                    &picked, win, &JSONF, 1, nullptr, false);
            }
            ImGui::SameLine();
            if (button(IC_SAVE "  Export##apps")) {
                static const SDL_DialogFileFilter JSONF = {"Applications", "json"};
                SDL_ShowSaveFileDialog(
                    [](void *u, const char *const *list, int) {
                        if (!list || !*list) return;
                        Picked *p = (Picked *)u;
                        std::lock_guard<std::mutex> lock(p->m);
                        p->appOut = *list;
                    },
                    &picked, win, &JSONF, 1, "applications.json");
            }
            tip("Export writes every application. Import adds the ones in a file that you don't have; one with the same id is kept as yours and said");
            std::string aIn, aOut;
            {
                std::lock_guard<std::mutex> lock(picked.m);
                aIn.swap(picked.appIn), aOut.swap(picked.appOut);
            }
            if (!aOut.empty()) {
                std::string err;
                choreo.msg = app_save(std::filesystem::u8path(aOut), choreo.list, err) ? "Exported " + std::to_string(choreo.list.size()) + " applications to " + aOut : err;
            }
            if (!aIn.empty()) {
                std::vector<Application> got;
                std::vector<std::string> skipped;
                std::string err;
                choreo.msg.clear();
                if (!app_parse(preset_text(std::filesystem::u8path(aIn)), got, err, skipped)) choreo.msg = "Not imported: " + err;
                else {
                    int added = 0, kept = 0;
                    for (const Application &g : got) {
                        if (std::any_of(choreo.list.begin(), choreo.list.end(), [&](const Application &o) { return o.id() == g.id(); })) kept++;
                        else choreo.list.push_back(g), added++;
                    }
                    for (const std::string &s : skipped) choreo.msg += s + "\n";
                    choreo.msg += "Imported " + std::to_string(added) + " applications; " + std::to_string(kept) + " you already had were kept as yours.";
                    if (added && !choreo.blocked) app_persist(choreo.list, choreo.error);
                }
            }
        }
