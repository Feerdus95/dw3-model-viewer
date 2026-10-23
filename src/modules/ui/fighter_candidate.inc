        if (ImGui::CollapsingHeader("Candidate camera")) {
            /* the donor: a fighter of this one's side and one of its cameras; its values are the start, then adjustable */
            auto pick = [&](int fi, int ci) {
                const Fighter &d = g_game.fighters[fi];
                cand.fighter = fi, cand.cam = std::clamp(ci, 0, std::max(0, (int)d.cameras.size() - 1));
                if (!d.cameras.empty()) cand.c = d.cameras[cand.cam];
                cand.fitK = 0;
            };
            if (cand.fighter < 0 || cand.fighter >= (int)g_game.fighters.size() || g_game.fighters[cand.fighter].enemy != f.enemy) {
                for (int i = 0; i < (int)g_game.fighters.size(); i++)
                    if (&g_game.fighters[i] == &f) pick(i, std::min<int>(f.enemy ? 0 : 8, std::max(0, (int)f.cameras.size() - 1)));
                cand.on = false;
            }
            if (cand.fighter < 0) return hint("This fighter is not in the fighters' list.");
            if (ImGui::RadioButton("A  The script's camera", !cand.on)) set_ab(false);
            ImGui::SameLine();
            if (ImGui::RadioButton("B  The candidate", cand.on)) set_ab(true);
            tip("B replaces the camera the script asks for with the candidate, at the same moments; A is the game's own. Switching changes only the view, on the frame that is showing: the animation, its timing and the candidate's edits stay (key B toggles)");
            ImGui::TextColored(cand.on ? C_WARN : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "Showing %s", cand.on ? "B, the candidate" : "A, the script's camera");
            ImGui::Checkbox("Also at the cuts the player skips", &cand.every);
            tip("The script's explicit cameras and the other side's shots: the player doesn't move the view for them, the candidate does");
            const Fighter &donor = g_game.fighters[cand.fighter];
            ImGui::SetNextItemWidth(-1);
            /* origin (where a fighter comes from) is not the context (the data set loaded): shown only with a mod's context loaded */
            const bool modded = g_context != "base_game";
            auto origin_tag = [&](const Fighter &d) { return modded ? std::string(", origin: ") + fighter_origin(d.id) : std::string(); };
            if (modded) {
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##oflt", pre.origin.empty() ? "Origin: all" : ("Origin: " + pre.origin).c_str())) {
                    if (ImGui::Selectable("Origin: all", pre.origin.empty())) pre.origin.clear();
                    for (const char *o : {"base_game", "modified_base", "mod", "unknown"})
                        if (ImGui::Selectable(o, pre.origin == o)) pre.origin = o;
                    ImGui::EndCombo();
                }
                tip("Where a fighter comes from, as this context's compendium dump says (needs --dump-compendium --context %s --base DIR); unknown without it", g_context.c_str());
            }
            if (ImGui::BeginCombo("##donor", (donor.name + " (fighter " + std::to_string(donor.id) + origin_tag(donor) + ")").c_str())) {
                for (int i = 0; i < (int)g_game.fighters.size(); i++) {
                    const Fighter &d = g_game.fighters[i];
                    if (d.enemy != f.enemy || d.cameras.empty()) continue;
                    if (modded && !pre.origin.empty() && pre.origin != fighter_origin(d.id) && i != cand.fighter) continue;
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

            /* --- presets: the candidate kept for this fighter, this script and this donor camera --- */
            heading("Presets");
            if (g_context != "base_game") hint("Context: %s", g_context.c_str());
            if (!pre.loaded) {
                pre.loaded = true;
                std::string text = preset_text(preset_path());
                std::vector<std::string> skipped;
                if (!text.empty() && !preset_parse(text, pre.list, pre.error, skipped)) pre.blocked = true, pre.list.clear(), pre.error = "presets.json: " + pre.error + ". It is left as it is; nothing is saved over it.";
                for (const std::string &s : skipped) pre.msg += s + "\n";
            }
            if (!pre.error.empty()) ImGui::TextColored(C_WARN, "%s", pre.error.c_str());
            if (!pre.msg.empty()) ImGui::TextWrapped("%s", pre.msg.c_str());
            auto persist = [&]() { preset_persist(pre.list, pre.error); }; /* (the modals' callbacks outlive this frame: they call preset_persist on PRE, not this) */
            auto make = [&]() {
                Preset p;
                p.name = pre.name, p.notes = pre.notes, p.status = PRESET_STATUS[std::clamp(pre.status, 0, 2)];
                p.fighter = f.id;
                p.scriptFid = f.effectsFid, p.scriptEntry = f.effectsEntry, p.scriptIdx = cand.script;
                p.donor = donor.id, p.donorEnemy = donor.enemy, p.donorIdx = cand.cam;
                p.c = cand.c, p.every = cand.every, p.fitK = cand.fitK;
                p.context = p.donorContext = g_context;
                preset_check(p);
                return p;
            };
            bool canSave = !pre.blocked && cand.script >= 0 && cand.of == &f;
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##pname", "Name", pre.name, sizeof pre.name);
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##pnotes", "Notes", pre.notes, sizeof pre.notes);
            ImGui::SetNextItemWidth(-1);
            ImGui::Combo("##pstatus", &pre.status, "Candidate\0Approved\0Rejected\0");
            tip("The status is of this combination only: this fighter, this script and this donor camera");
            ImGui::BeginDisabled(!canSave);
            if (button(IC_SAVE "  Save as preset", 1)) {
                Preset p = make();
                auto it = std::find_if(pre.list.begin(), pre.list.end(), [&](const Preset &o) { return o.id() == p.id(); });
                auto put = [&, p, idx = (int)(it - pre.list.begin()), has = it != pre.list.end()]() {
                    if (has && idx < (int)pre.list.size()) pre.list[idx] = p;
                    else pre.list.push_back(p);
                    pre.active = p.id();
                    preset_persist(pre.list, pre.error);
                };
                if (it == pre.list.end()) put();
                else {
                    ask.title = "Replace the preset", ask.text = "\"" + it->name + "\" is already saved for this fighter, script and donor camera. Replace it with the one on screen?";
                    ask.ok = "Replace", ask.danger = true, ask.open = true, ask.then = put;
                }
            }
            ImGui::EndDisabled();
            if (!canSave && !pre.blocked) hint("Play one of this fighter's scripts first: a preset is made for the script it was seen with.");
            if (canSave) hint("For script %d, donor %s", cand.script, comp::cam_id(donor.id, donor.enemy, cand.cam).c_str());

            std::vector<std::string> ctxs;
            for (const Preset &p : pre.list)
                if (std::find(ctxs.begin(), ctxs.end(), p.context) == ctxs.end()) ctxs.push_back(p.context);
            if (ctxs.size() > 1) {
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##pctx", pre.ctx.empty() ? "All contexts" : ("Context: " + pre.ctx).c_str())) {
                    if (ImGui::Selectable("All contexts", pre.ctx.empty())) pre.ctx.clear();
                    for (const std::string &c : ctxs)
                        if (ImGui::Selectable(c.c_str(), pre.ctx == c)) pre.ctx = c;
                    ImGui::EndCombo();
                }
                tip("A mod's fighter and the original it replaces are different presets: each belongs to the context it was made in");
            }
            int mine = 0;
            for (int i = 0; i < (int)pre.list.size(); i++) {
                Preset &p = pre.list[i];
                if (p.fighter != f.id || (!pre.ctx.empty() && p.context != pre.ctx)) continue;
                if (modded && !pre.origin.empty() && pre.origin != fighter_origin(p.fighter)) continue;
                mine++;
                ImGui::PushID(i);
                char l[256];
                snprintf(l, sizeof l, "%s%s  [%s]  script %d, %s%s%s%s###p", p.warn.empty() ? "" : IC_WARN " ", p.name.empty() ? "(no name)" : p.name.c_str(), p.status.c_str(), p.scriptIdx,
                         p.donor_ref().c_str() + 4, p.every ? ", cuts" : "", p.context == "base_game" ? "" : ", context ", p.context == "base_game" ? "" : p.context.c_str());
                if (ImGui::Selectable(l, pre.active == p.id()) && p.warn.empty()) {
                    int di = -1;
                    for (int k = 0; k < (int)g_game.fighters.size(); k++)
                        if (&g_game.fighters[k] == fighter_by_id(p.donor, p.donorEnemy, false)) di = k;
                    if (di >= 0) {
                        cand.fighter = di, cand.cam = p.donorIdx, cand.c = p.c, cand.every = p.every, cand.fitK = p.fitK, cand.on = true; /* the snapshot, not the donor's */
                        snprintf(pre.name, sizeof pre.name, "%s", p.name.c_str());
                        snprintf(pre.notes, sizeof pre.notes, "%s", p.notes.c_str());
                        pre.status = p.status == PRESET_STATUS[1] ? 1 : p.status == PRESET_STATUS[2] ? 2 : 0;
                        pre.active = p.id();
                        run_script(p.scriptIdx);
                    }
                }
                if (!p.warn.empty()) {
                    std::string t;
                    for (const std::string &w : p.warn) t += w + "\n";
                    tip("Can't be used here:\n%s", t.c_str());
                } else {
                    tip("Click to load it and play its script");
                }
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem(IC_DELETE "  Delete the preset")) {
                        std::string id = p.id();
                        ask.title = "Delete the preset", ask.text = "Delete \"" + p.name + "\"? It can't be undone.", ask.ok = "Delete", ask.danger = true, ask.open = true;
                        ask.then = [&, id]() {
                            pre.list.erase(std::remove_if(pre.list.begin(), pre.list.end(), [&](const Preset &o) { return o.id() == id; }), pre.list.end());
                            if (pre.active == id) pre.active.clear();
                            preset_persist(pre.list, pre.error);
                        };
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            if (!mine) hint("No preset for this fighter yet.");
            else hint("Right-click a preset to delete it.");

            if (button(IC_IMPORT "  Import...")) {
                static const SDL_DialogFileFilter JSONF = {"Presets", "json"};
                SDL_ShowOpenFileDialog(
                    [](void *u, const char *const *list, int) {
                        if (!list || !*list) return;
                        Picked *p = (Picked *)u;
                        std::lock_guard<std::mutex> lock(p->m);
                        p->presetIn = *list;
                    },
                    &picked, win, &JSONF, 1, nullptr, false);
            }
            ImGui::SameLine();
            if (button(IC_SAVE "  Export...")) {
                static const SDL_DialogFileFilter JSONF = {"Presets", "json"};
                SDL_ShowSaveFileDialog(
                    [](void *u, const char *const *list, int) {
                        if (!list || !*list) return;
                        Picked *p = (Picked *)u;
                        std::lock_guard<std::mutex> lock(p->m);
                        p->presetOut = *list;
                    },
                    &picked, win, &JSONF, 1, "presets.json");
            }
            tip("Export writes every preset; import reads a file of presets and shows what it would add before it changes anything");
            std::string inPath, outPath;
            {
                std::lock_guard<std::mutex> lock(picked.m);
                inPath.swap(picked.presetIn), outPath.swap(picked.presetOut);
            }
            if (!outPath.empty()) {
                std::string err;
                pre.msg = preset_save(std::filesystem::u8path(outPath), pre.list, err) ? "Exported " + std::to_string(pre.list.size()) + " presets to " + outPath : err;
            }
            if (!inPath.empty()) {
                std::vector<Preset> got;
                std::vector<std::string> skipped;
                std::string err;
                pre.in.clear(), pre.msg.clear();
                if (!preset_parse(preset_text(std::filesystem::u8path(inPath)), got, err, skipped)) pre.msg = "Not imported: " + err;
                else {
                    for (const Preset &g : got) {
                        bool dup = std::any_of(pre.list.begin(), pre.list.end(), [&](const Preset &o) { return o.id() == g.id(); });
                        pre.in.push_back({g, dup, !dup});
                    }
                    pre.inFile = inPath;
                    for (const std::string &s : skipped) pre.msg += s + "\n";
                    if (pre.in.empty()) pre.msg += "The file has no presets.";
                }
            }
            if (!pre.in.empty()) {
                ImGui::TextWrapped("Import from %s", pre.inFile.c_str());
                for (int i = 0; i < (int)pre.in.size(); i++) {
                    auto &e = pre.in[i];
                    ImGui::PushID(1000 + i);
                    std::string label = (e.p.name.empty() ? "(no name)" : e.p.name) + "  " + e.p.fighter_ref() + ", " + e.p.script_ref() + (e.dup ? "  - already yours: replace it" : "");
                    ImGui::Checkbox(label.c_str(), &e.take);
                    for (const std::string &w : e.p.warn) ImGui::TextColored(C_WARN, "  %s", w.c_str());
                    ImGui::PopID();
                }
                if (button("Import the ticked", 1)) {
                    for (auto &e : pre.in) {
                        if (!e.take) continue;
                        auto it = std::find_if(pre.list.begin(), pre.list.end(), [&](const Preset &o) { return o.id() == e.p.id(); });
                        if (it != pre.list.end()) *it = e.p;
                        else pre.list.push_back(e.p);
                    }
                    pre.in.clear();
                    if (!pre.blocked) persist();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) pre.in.clear();
            }
        }
