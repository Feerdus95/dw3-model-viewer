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
            for (int s : {MODEL, MOTIONS, FIGHTER, CAMERAS, TEXTURES}) {
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

