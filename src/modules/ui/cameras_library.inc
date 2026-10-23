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
    /* one filter row for both lists: context, origin (a mod's context only) and status */
    auto lib_filters = [&](bool modded) {
        std::vector<std::string> ctxs;
        for (const std::string &c : [&] {
                 std::vector<std::string> all;
                 for (const Preset &p : pre.list) all.push_back(p.context);
                 for (const Application &a : choreo.list) all.push_back(a.context);
                 return all;
             }())
            if (std::find(ctxs.begin(), ctxs.end(), c) == ctxs.end()) ctxs.push_back(c);
        if (ctxs.size() > 1) {
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##lctx", libf.ctx.empty() ? "All contexts" : ("Context: " + libf.ctx).c_str())) {
                if (ImGui::Selectable("All contexts", libf.ctx.empty())) libf.ctx.clear();
                for (const std::string &c : ctxs)
                    if (ImGui::Selectable(c.c_str(), libf.ctx == c)) libf.ctx = c;
                ImGui::EndCombo();
            }
            tip("A mod's fighter and the original it replaces are different entries: each belongs to the context it was made in");
        }
        if (modded) {
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##oflt", libf.origin.empty() ? "Origin: all" : ("Origin: " + libf.origin).c_str())) {
                if (ImGui::Selectable("Origin: all", libf.origin.empty())) libf.origin.clear();
                for (const char *o : {"base_game", "modified_base", "mod", "unknown"})
                    if (ImGui::Selectable(o, libf.origin == o)) libf.origin = o;
                ImGui::EndCombo();
            }
            tip("Where a fighter comes from, as this context's compendium dump says (needs --dump-compendium --context %s --base DIR); unknown without it. The Camera tab's donors follow it", g_context.c_str());
        }
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##lstat", libf.status.empty() ? "Status: all" : ("Status: " + libf.status).c_str())) {
            if (ImGui::Selectable("Status: all", libf.status.empty())) libf.status.clear();
            for (const char *s : PRESET_STATUS)
                if (ImGui::Selectable(s, libf.status == s)) libf.status = s;
            ImGui::EndCombo();
        }
    };
    auto cameras_browser = [&]() {
        const Fighter *fp = cams_fighter();
        if (!fp) return;
        const Fighter &f = *fp;
        libs_load();
        hint("What was kept for this fighter. Click one to load it and play its script; the workbench shows it.");
        const bool modded = g_context != "base_game";
        if (modded) hint("Context: %s", g_context.c_str());
        lib_filters(modded);
        auto origin_of = [&](int fighter) { return modded && !libf.origin.empty() ? fighter_origin(fighter) : std::string(); };

        /* --- presets: one camera --- */
        heading("Presets");
        if (!pre.error.empty()) ImGui::TextColored(C_WARN, "%s", pre.error.c_str());
        if (!pre.msg.empty()) ImGui::TextWrapped("%s", pre.msg.c_str());
        int mine = library_rows<Preset>(
            pre.list, pre.active, 0, [&](const Preset &p) { return p.fighter == f.id && lib_pass(libf, modded, p.context, p.status, origin_of(p.fighter)); },
            [](const Preset &p) {
                char l[256];
                snprintf(l, sizeof l, "%s%s  [%s]  script %d, %s%s%s%s", p.warn.empty() ? "" : IC_WARN " ", p.name.empty() ? "(no name)" : p.name.c_str(), p.status.c_str(), p.scriptIdx,
                         p.donor_ref().c_str() + 4, p.every ? ", cuts" : "", p.context == "base_game" ? "" : ", context ", p.context == "base_game" ? "" : p.context.c_str());
                return std::string(l);
            },
            [](const Preset &p) { return p.warn.empty(); },
            [](const Preset &p) {
                if (p.warn.empty()) return std::string("Click to load it and play its script");
                std::string t = "Can't be used here:\n";
                for (const std::string &w : p.warn) t += w + "\n";
                return t;
            },
            [&](Preset &p) {
                int di = -1;
                for (int k = 0; k < (int)g_game.fighters.size(); k++)
                    if (&g_game.fighters[k] == fighter_by_id(p.donor, p.donorEnemy, false)) di = k;
                if (di < 0) return;
                wb_begin();
                cand.fighter = di, cand.cam = p.donorIdx, cand.c = p.c, cand.every = p.every, cand.fitK = p.fitK, cand.on = true; /* the snapshot, not the donor's */
                wb_end();
                wbh.set_clean_cand(wb_now().c);
                form.load(p);
                pre.active = p.id();
                run_script(p.scriptIdx);
                camTabGo = 0;
            },
            [&](Preset p) { menu_delete<Preset>(ask, pre.list, pre.active, p, "preset", "", [&]() { preset_persist(pre.list, pre.error); }); });
        hint(mine ? "Right-click a preset to delete it." : "No preset for this fighter yet.");
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
                if (!pre.blocked) preset_persist(pre.list, pre.error);
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) pre.in.clear();
        }

        /* --- applications: a sequence of cameras --- */
        heading("Applications");
        if (!choreo.error.empty()) ImGui::TextColored(C_WARN, "%s", choreo.error.c_str());
        if (!choreo.msg.empty()) ImGui::TextWrapped("%s", choreo.msg.c_str());
        auto soft = [](const Application &a) { return std::all_of(a.warn.begin(), a.warn.end(), [](const std::string &w) { return w.find("snapshot") != std::string::npos; }); };
        int mineA = library_rows<Application>(
            choreo.list, choreo.active, 5000, [&](const Application &a) { return a.fighter == f.id && lib_pass(libf, modded, a.context, a.status, origin_of(a.fighter)); },
            [](const Application &a) {
                char l[256];
                snprintf(l, sizeof l, "%s%s%s  [%s]  script %d <- %d.%d:%d%s%s", a.warn.empty() ? "" : IC_WARN " ", a.name.empty() ? "(no name)" : a.name.c_str(),
                         a.variant.empty() ? "" : (" (" + a.variant + ")").c_str(), a.status.c_str(), a.scriptIdx, a.donorFid, a.donorEntry, a.donorIdx,
                         a.context == "base_game" ? "" : ", context ", a.context == "base_game" ? "" : a.context.c_str());
                return std::string(l);
            },
            soft,
            [&](const Application &a) {
                if (a.warn.empty()) return std::string("Click to load it and play its script");
                std::string t = soft(a) ? "Can be used (its snapshot is):\n" : "Can't be used here:\n";
                for (const std::string &w : a.warn) t += w + "\n";
                return t;
            },
            [&](Application &a) {
                wb_begin();
                choreo.app = a;
                choreo.has = choreo.on = true;
                choreo.active = a.id();
                wb_end();
                wbh.set_clean_seq(wb_now().s);
                form.load(a);
                choreo_refresh();
                run_script(a.scriptIdx);
                camTabGo = 1;
                set_ab(true);
            },
            [&](Application a) {
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
                menu_delete<Application>(ask, choreo.list, choreo.active, a, "application", " (the previous file is kept as applications.json.bak)", [&]() { app_persist(choreo.list, choreo.error); });
            });
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
