    /* ===== Files ===== */
    /* The Digimon made for the port (Digimon World 2's and Digital Card Battle's, converted: a dw2003.txt beside the model names the file and the fighter), opened straight, converted
       already or not: no need to find them in the DW2 tab and convert each time */
    struct Modded {
        std::string name, like, out, dw2, src; /* src: the game it comes from */
        int fid;
        bool made;
    };
    std::vector<Modded> modded;
    uint64_t moddedAt = 0;
    int filesTab = 0, filesTabGo = -1; /* 0 the game's, 1 the modded; the tab to show next */
    auto modded_scan = [&]() {
        if (moddedAt && SDL_GetTicks() - moddedAt < 3000) return; /* a conversion made outside shows within moments */
        moddedAt = SDL_GetTicks() | 1;
        modded.clear();
        if (g_dw2.empty()) dw2_load_dir();
        if (g_dcb.empty()) dcb_load_dir();
        std::error_code ec;
        for (int game = 0; game < 2; game++) {
            const std::string &dir = game ? g_dcb : g_dw2;
            for (const Dw2Entry &en : dir.empty() ? std::vector<Dw2Entry>() : (game ? dcb_scan() : dw2_scan())) {
                if (en.fx) continue;
                Dw2Link l = dw2_link(en.path);
                if (!l.ok) continue;
                modded.push_back({l.name.empty() ? l.like : l.name, l.like, l.out, en.path, game ? "Digital Card Battle" : "Digimon World 2", l.fid, std::filesystem::exists(std::filesystem::u8path(l.out), ec)});
            }
        }
    };
    auto modded_open = [&](const Modded &m) {
        if (!m.made) dw2_convert(m.dw2);
        dw2_open_converted(m.dw2);
        moddedAt = 0;
    };
    auto modded_browser = [&]() {
        modded_scan();
        if (modded.empty()) {
            hint("No Digimon made for the port yet.");
            hint("A Digimon World 2 or Digital Card Battle model with a dw2003.txt beside it (the folders of the DW2 and DCB tabs) is listed here.");
            return;
        }
        hint("Made for Digimon World 2003 from other games' models. Click one to open its converted file with its fighter.");
        if (ImGui::BeginTable("modded", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.4f);
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            for (const Modded &m : modded) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(m.fid);
                bool sel = scene.model && !view.stageView && scene.fid == m.fid;
                if (ImGui::Selectable(m.name.c_str(), sel, ImGuiSelectableFlags_SpanAllColumns) && !sel) modded_open(m);
                tip("%s, from %s, like %s: file %d (%s)", m.name.c_str(), m.src.c_str(), m.like.c_str(), m.fid, nice_path(m.out).c_str());
                if (ImGui::BeginPopupContextItem("##menu")) {
                    menu_title(m.name, "%s, like %s, file %d", m.src.c_str(), m.like.c_str(), m.fid);
                    if (ImGui::MenuItem(IC_OPEN "  Open")) modded_open(m);
                    if (ImGui::MenuItem(IC_RESTART "  Convert again, and open")) {
                        dw2_convert(m.dw2);
                        dw2_open_converted(m.dw2);
                        moddedAt = 0;
                    }
                    if (ImGui::MenuItem(IC_COPY "  Copy the name")) ImGui::SetClipboardText(m.name.c_str());
                    ImGui::EndPopup();
                }
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", m.made ? "converted" : "not converted yet");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    };
    auto files_browser = [&]() {
        if (ImGui::BeginTabBar("##filetabs")) {
            if (ImGui::BeginTabItem("Game", nullptr, filesTabGo == 0 ? ImGuiTabItemFlags_SetSelected : 0)) filesTab = 0, ImGui::EndTabItem();
            if (ImGui::BeginTabItem("Modded", nullptr, filesTabGo == 1 ? ImGuiTabItemFlags_SetSelected : 0)) filesTab = 1, ImGui::EndTabItem();
            ImGui::EndTabBar();
            filesTabGo = -1;
        }
        if (filesTab == 1) return modded_browser();
        if (!listed) {
            if (!scanner.running) scanner.start();
            listed = scanner.poll(list);
        }
        if (!listed) return hint("Listing the models...");
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
                same_line_if_width(ImGui::CalcTextSize(m.name.c_str()).x + 16 * dpi);
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
                if (s == MOTIONS || s == CAMERAS) ImGui::SameLine();
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

