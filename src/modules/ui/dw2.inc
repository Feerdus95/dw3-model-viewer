    /* ===== Digimon World 2 ===== */
    std::vector<Dw2Entry> dw2List;
    bool dw2Listed = false;
    char dw2Filter[64] = "";
    int dw2Tab = 0; /* 0 Digimon, 1 effects */
    auto pick_dw2_folder = [&]() {
        SDL_ShowOpenFolderDialog(
            [](void *u, const char *const *l, int) {
                if (!l || !*l) return;
                Picked *p = (Picked *)u;
                std::lock_guard<std::mutex> lock(p->m);
                p->dw2 = *l;
            },
            &picked, win, nullptr, false);
    };
    auto dw2_browser = [&]() {
        if (!dw2Listed) dw2_load_dir(), dw2List = g_dw2.empty() ? std::vector<Dw2Entry>() : dw2_scan(), dw2Listed = true;
        if (!picked.dw2.empty()) {
            g_dw2 = picked.dw2, picked.dw2.clear();
            dw2_save_dir(), dw2List = dw2_scan();
        }
        if (dw2List.empty()) {
            hint("No Digimon World 2 models found%s.", g_dw2.empty() ? "" : (" in " + g_dw2).c_str());
            hint("Run tools/extract_dw2.py on your own SLUS-01193 disc (python tools/extract_dw2.py DISC.cue OUT), and choose OUT.");
            if (button(IC_FOLDER "  Choose the folder...", 1)) pick_dw2_folder();
            return;
        }
        if (ImGui::BeginTabBar("##dw2tabs")) {
            if (ImGui::BeginTabItem("Digimon")) dw2Tab = 0, ImGui::EndTabItem();
            if (ImGui::BeginTabItem("Effects")) dw2Tab = 1, ImGui::EndTabItem();
            ImGui::EndTabBar();
        }
        bool open = scene.model && scene.model->dw2 && !view.stageView;
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##dw2filter", IC_SEARCH "  Search: number or file", dw2Filter, sizeof dw2Filter);
        std::string f = lower(dw2Filter);
        ImGui::BeginChild("##dw2list", ImVec2(0, open ? ImGui::GetContentRegionAvail().y * 0.4f : 0));
        if (ImGui::BeginTable("dw2files", 1, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            for (const Dw2Entry &en : dw2List) {
                if ((int)en.fx != dw2Tab || (!f.empty() && lower(en.label).find(f) == std::string::npos)) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                bool sel = scene.model && scene.model->dw2 && scene.path == en.path && !view.stageView;
                if (ImGui::Selectable(en.label.c_str(), sel, ImGuiSelectableFlags_SpanAllColumns) && !sel) open_file("DW2 " + en.label, en.path, -1);
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
        if (open) { /* the model's bones, as in Model */
            heading("Bones");
            model_browser();
        }
    };
    auto dw2_inspector = [&]() {
        if (scene.model && scene.model->dw2 && !view.stageView) {
            Model &m = *scene.model;
            title("%s", m.name.c_str());
            chip("Digimon World 2", C_DIM);
            size_t polys = 0, verts = 0;
            for (const Mesh &me : m.meshes) polys += me.polys.size(), verts += me.verts.size();
            ImGui::Spacing();
            field("Bones", "%d", (int)m.bones.size() - 1);
            field("Polygons", "%d", (int)polys);
            field("Vertices", "%d", (int)verts);
            field("Motions", "%d", (int)m.motions.size());
            model_inspector(); /* the selected bone, and the size: as in Model */
            heading("In Digimon World 2003");
            Dw2Link l = dw2_link(scene.path);
            if (!l.ok) {
                hint("No dw2003.txt beside this model, so it has no converter, no file or fighter in DW2003 yet.");
            } else {
                field("File", "%s", nice_path(l.out).c_str());
                field("Fighter", "%s, like %s", l.name.c_str(), l.like.c_str());
                if (button(IC_RESTART "  Convert", 0)) dw2_convert(scene.path);
                tip("Runs the converter on this model and its tuning.txt: it writes the file above");
                same_line_if_fits(IC_PLAY "  Open the DW2003 version");
                if (button(IC_PLAY "  Open the DW2003 version", 1)) dw2_open_converted(scene.path);
                tip("The converted file with its fighter, in the same sections: its action scripts, its portraits and cameras, its motions");
            }
        } else {
            title("No Digimon World 2 model open");
            hint("Pick one on the left, or drop a .DW2M file on the window.");
        }
        heading("Folder");
        hint("%s", g_dw2.empty() ? "None chosen" : g_dw2.c_str());
        if (button(IC_FOLDER "  Choose the folder...", 0)) pick_dw2_folder();
    };

