    /* ===== Cameras: the Library (the presets and applications kept) on the left, the Workbench on the right ===== */
    int camTabGo = -1; /* the workbench's tab to show next: 0 Camera, 1 Sequence, 2 Details; -1 as it is */
    /* the two files, read when first wanted */
    auto libs_load = [&]() {
        if (!pre.loaded) {
            pre.loaded = true;
            std::string text = preset_text(preset_path());
            std::vector<std::string> skipped;
            if (!text.empty() && !preset_parse(text, pre.list, pre.error, skipped)) pre.blocked = true, pre.list.clear(), pre.error = "presets.json: " + pre.error + ". It is left as it is; nothing is saved over it.";
            for (const std::string &s : skipped) pre.msg += s + "\n";
        }
        if (!choreo.loaded) {
            choreo.loaded = true;
            std::string text = preset_text(app_path());
            std::vector<std::string> skipped;
            if (!text.empty() && !app_parse(text, choreo.list, choreo.error, skipped)) choreo.blocked = true, choreo.list.clear(), choreo.error = "applications.json: " + choreo.error + ". It is left as it is; nothing is saved over it.";
            for (const std::string &s : skipped) choreo.msg += s + "\n";
        }
    };
    /* the fighter the sections work on, or why there is none */
    auto cams_fighter = [&]() -> const Fighter * {
        if (!scene.model) return nullptr;
        if (scene.model->dw2) {
            hint("A DW2 model has no cameras or scripts of its own: the DW2003 version does.");
            if (button(IC_PLAY "  Open the DW2003 version", 1)) dw2_open_converted(scene.path);
            return nullptr;
        }
        if (scene.fighters.empty()) return hint("No fighter uses this file: it has no cameras."), nullptr;
        const Fighter &f = *scene.fighters[scene.fighter];
        if (!cand_ensure(f)) return hint("This fighter is not in the fighters' list."), nullptr;
        return &f;
    };
    auto cameras_browser = [&]() {
        const Fighter *fp = cams_fighter();
        if (!fp) return;
        const Fighter &f = *fp;
        libs_load();
        hint("What was kept for this fighter. Click one to load it and play its script; the workbench shows it.");
        const bool modded = g_context != "base_game";
        auto persist = [&]() { preset_persist(pre.list, pre.error); }; /* (the modals' callbacks outlive this frame: they call preset_persist on PRE, not this) */
        heading("Presets");
        if (g_context != "base_game") hint("Context: %s", g_context.c_str());
        if (!pre.error.empty()) ImGui::TextColored(C_WARN, "%s", pre.error.c_str());
        if (!pre.msg.empty()) ImGui::TextWrapped("%s", pre.msg.c_str());
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
                    pre.status = status_index(p.status);
                    pre.active = p.id();
                    run_script(p.scriptIdx);
                    camTabGo = 0;
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
                    ask_delete(ask, pre.list, pre.active, p.id(), "Delete the preset", "Delete \"" + p.name + "\"? It can't be undone.", [&]() { preset_persist(pre.list, pre.error); });
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (!mine) hint("No preset for this fighter yet.");
        else hint("Right-click a preset to delete it.");

        json_buttons<&Picked::presetIn, &Picked::presetOut>(picked, win, IC_IMPORT "  Import...", IC_SAVE "  Export...", "Presets", "presets.json");
        tip("Export writes every preset; import reads a file of presets and shows what it would add before it changes anything");
        std::string inPath = take_picked(picked, &Picked::presetIn), outPath = take_picked(picked, &Picked::presetOut);
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

        if (!choreo.error.empty()) ImGui::TextColored(C_WARN, "%s", choreo.error.c_str());
        if (!choreo.msg.empty()) ImGui::TextWrapped("%s", choreo.msg.c_str());

        heading("Applications");
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
                choreo.status = status_index(a.status);
                choreo_refresh();
                run_script(a.scriptIdx);
                    camTabGo = 1;
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
                    ask_delete(ask, choreo.list, choreo.active, a.id(), "Delete the application",
                               "Delete \"" + a.name + "\"? It can't be undone (the previous file is kept as applications.json.bak).", [&]() { app_persist(choreo.list, choreo.error); });
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        hint(mineA ? "Right-click an application to duplicate or delete it." : "No application for this fighter yet.");
        json_buttons<&Picked::appIn, &Picked::appOut>(picked, win, IC_IMPORT "  Import##apps", IC_SAVE "  Export##apps", "Applications", "applications.json");
        tip("Export writes every application. Import adds the ones in a file that you don't have; one with the same id is kept as yours and said");
        std::string aIn = take_picked(picked, &Picked::appIn), aOut = take_picked(picked, &Picked::appOut);
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
    };
