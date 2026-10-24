    /* ===== Digital Card Battle ===== */
    std::vector<Dw2Entry> dcbList;
    bool dcbListed = false;
    char dcbFilter[64] = "";
    auto pick_dcb_folder = [&]() {
        SDL_ShowOpenFolderDialog(
            [](void *u, const char *const *l, int) {
                if (!l || !*l) return;
                Picked *p = (Picked *)u;
                std::lock_guard<std::mutex> lock(p->m);
                p->dcb = *l;
            },
            &picked, win, nullptr, false);
    };
    auto dcb_open = [&]() { return scene.model && scene.model->dw2 && !view.stageView && !g_dcb.empty() && scene.path.compare(0, g_dcb.size(), g_dcb) == 0; };
    auto dcb_browser = [&]() {
        if (!dcbListed) dcb_load_dir(), dcbList = g_dcb.empty() ? std::vector<Dw2Entry>() : dcb_scan(), dcbListed = true;
        if (!picked.dcb.empty()) {
            g_dcb = picked.dcb, picked.dcb.clear();
            dcb_save_dir(), dcbList = dcb_scan();
        }
        if (dcbList.empty()) {
            hint("No Digital Card Battle models found%s.", g_dcb.empty() ? "" : (" in " + g_dcb).c_str());
            hint("Run dcb-extract (ex.py, drv.py, then todcbm.py) on your own copy of the disc, and choose the folder it made: digimon/ in it.");
            if (button(IC_FOLDER "  Choose the folder...", 1)) pick_dcb_folder();
            return;
        }
        bool open = dcb_open();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##dcbfilter", IC_SEARCH "  Search: number or name", dcbFilter, sizeof dcbFilter);
        std::string f = lower(dcbFilter);
        ImGui::BeginChild("##dcblist", ImVec2(0, open ? ImGui::GetContentRegionAvail().y * 0.4f : 0));
        if (ImGui::BeginTable("dcbfiles", 1, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            for (const Dw2Entry &en : dcbList) {
                if (!f.empty() && lower(en.label).find(f) == std::string::npos) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                bool sel = open && scene.path == en.path;
                if (ImGui::Selectable(en.label.c_str(), sel, ImGuiSelectableFlags_SpanAllColumns) && !sel) open_file("DCB " + en.label, en.path, -1);
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
        if (open) { /* the model's bones, as in Model */
            heading("Bones");
            model_browser();
        }
    };
    auto dcb_inspector = [&]() {
        if (dcb_open()) {
            Model &m = *scene.model;
            title("%s", m.name.c_str());
            chip("Digital Card Battle", C_DIM);
            size_t polys = 0, verts = 0;
            for (const Mesh &me : m.meshes) polys += me.polys.size(), verts += me.verts.size();
            ImGui::Spacing();
            field("Bones", "%d", (int)m.bones.size() - 1);
            field("Polygons", "%d", (int)polys);
            field("Vertices", "%d", (int)verts);
            field("Motions", "%d", (int)m.motions.size());
            model_inspector(); /* the selected bone, and the size: as in Model */
        } else {
            title("No Digital Card Battle model open");
            hint("Pick one on the left.");
        }
        heading("Folder");
        hint("%s", g_dcb.empty() ? "None chosen" : g_dcb.c_str());
        if (button(IC_FOLDER "  Choose the folder...", 0)) pick_dcb_folder();
    };
