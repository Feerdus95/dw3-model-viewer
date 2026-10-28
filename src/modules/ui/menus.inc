    /* --- the right-click menus --- */
    auto menu_title = [&](const std::string &name, const char *fmt, ...) {
        ImGui::PushFont(g_fontBold);
        ImGui::TextColored(C_SELECT, "%s", name.c_str());
        ImGui::PopFont();
        va_list a;
        va_start(a, fmt);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextV(fmt, a);
        ImGui::PopStyleColor();
        va_end(a);
        ImGui::Separator();
    };
    auto part_menu = [&](int k) {
        if (k < 0 || k >= (int)stageEdit.parts.size()) return;
        menu_title(part_name(k), "part %d, %d polygons", k, (int)stageEdit.parts[k].faces.size());
        if (ImGui::MenuItem("Select", nullptr, stagePart == k)) return select_part(k);
        if (ImGui::MenuItem(IC_FIT "  Frame it", "F")) return select_part(k), frame_sel();
        bool hidden = stageHidden.count(part_name(k)) > 0;
        if (ImGui::MenuItem(hidden ? IC_EYE "  Show in the view" : IC_HIDE "  Hide in the view", "H")) return part_hide(k);
        if (ImGui::MenuItem("Show only this")) return show_only(stageMade.firstBone[k]);
        if (ImGui::MenuItem("Show all", "Alt+H")) return show_all();
        ImGui::Separator();
        if (ImGui::MenuItem(IC_COPY "  Copy", "Ctrl+C")) return part_copy(k);
        if (ImGui::MenuItem(IC_PASTE "  Paste", "Ctrl+V", false, !partClip.parts.empty())) return part_paste();
        if (ImGui::MenuItem(IC_ADD "  Duplicate", "Ctrl+D")) return part_duplicate(k);
        if (ImGui::MenuItem("Always drawn (no bounds check)", nullptr, stageEdit.parts[k].noBounds)) return part_bounds(k);
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, C_ERROR);
        bool del = ImGui::MenuItem(IC_DELETE "  Delete", "Del");
        ImGui::PopStyleColor();
        if (del) part_delete(k);
    };
    auto bone_menu = [&](int b) {
        if (stage_on()) return part_menu(part_of_bone(b));
        if (!scene.model || b < 1 || b >= (int)scene.model->bones.size()) return;
        menu_title(thing_name(b), "%d polygons", thing_polys(b));
        if (ImGui::MenuItem("Select", nullptr, scene.selected == b)) return void(scene.selected = b);
        if (ImGui::MenuItem(IC_FIT "  Frame it", "F")) return void(scene.selected = b), frame_sel();
        if (ImGui::MenuItem(scene.hidden[b] ? IC_EYE "  Show" : IC_HIDE "  Hide", "H")) return void(scene.hidden[b] = !scene.hidden[b]);
        if (ImGui::MenuItem("Show only this")) return show_only(b);
        if (ImGui::MenuItem("Show all", "Alt+H")) return show_all();
        ImGui::Separator();
        if (ImGui::MenuItem(IC_COPY "  Copy its details", "Ctrl+C")) ImGui::SetClipboardText(bone_info(b).c_str());
    };
    auto view_menu = [&]() {
        if (ImGui::MenuItem(IC_FIT "  Frame all", "F")) frame_all();
        if (scene.model && section != EFFECTS && section != TEXTURES) {
            if (ImGui::MenuItem(IC_EYE "  Show all", "Alt+H")) show_all();
            if (stage_on() && ImGui::MenuItem(IC_PASTE "  Paste part", "Ctrl+V", false, !partClip.parts.empty())) part_paste();
            if (view.gameCamera >= 0 && ImGui::MenuItem(IC_CAMERA "  Free camera")) frame_model(scene, view);
            ImGui::Separator();
            if (!view.stageView) ImGui::MenuItem("Grid", nullptr, &view.grid);
            if (!view.stageView) ImGui::MenuItem("Stand-in enemy", nullptr, &view.dummy);
            ImGui::MenuItem("Aim guide", nullptr, &view.guide);
            ImGui::MenuItem("Skeleton", nullptr, &view.skeleton);
            ImGui::MenuItem("Wireframe", nullptr, &view.wireframe);
            ImGui::MenuItem("Back-face culling", nullptr, &view.cull);
        }
        ImGui::Separator();
        ImGui::MenuItem("Controls hint", nullptr, &showHints);
    };

    /* --- Edit's actions, for the menu and the keys --- */
    std::vector<CustomFrame> frameClip;
    std::string frameClipDir;
