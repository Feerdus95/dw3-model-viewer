    /* --- the panels' parts shared by sections --- */
    auto panel_title = [&](const char *icon, const char *title) {
        ImGui::PushFont(g_fontBold);
        ImGui::TextColored(C_ACCENT, "%s", icon);
        ImGui::SameLine();
        ImGui::TextUnformatted(title);
        ImGui::PopFont();
        ImGui::Separator();
    };
    auto display_options = [&]() {
        float lw = ImGui::GetFontSize() * 5;
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Textures");
        ImGui::SameLine(lw);
        segmented("tex", &view.textures, {"Off", "PSX", "Bilinear"});
        tip("PSX: the console's sharp texels. Bilinear: smoothed.");
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Lights");
        ImGui::SameLine(lw);
        segmented("light", &view.lighting, {"None", "Viewer", "Stage"});
        tip("Viewer: a light over the camera's shoulder. Stage: a fight stage's three lights and ambient color.");
        if (view.lighting == 2 && !g_game.stages.empty()) {
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderInt("##fightstage", &view.stage, 0, (int)g_game.stages.size() - 1, "fight stage %d");
            tip("Whose lights (SFSTDATA.PRO)");
        }
        if (ImGui::BeginTable("display", 2)) {
            ImGui::TableNextColumn();
            check_box("Face", &view.face);
            tip("The blinking eyes and the other animated face parts");
            ImGui::TableNextColumn();
            check_box("Wireframe", &view.wireframe);
            ImGui::TableNextColumn();
            check_box("Skeleton", &view.skeleton);
            ImGui::TableNextColumn();
            check_box("Grid", &view.grid);
            ImGui::TableNextColumn();
            check_box("Effect points", &view.marks);
            tip("A cross where each effect of a script is (pink: a sprite, blue: an effect model), to see where they land on the model");
            ImGui::EndTable();
        }
        check_box("Back-face culling", &view.cull);
        if (ImGui::Button(IC_EYE "  Show all")) show_all();
        tip("Shows the hidden bones again (Alt+H)");
        same_line_if_fits(IC_FIT "  Frame");
        if (ImGui::Button(IC_FIT "  Frame")) frame_sel();
        tip("The camera on the selection, or on everything (F)");
    };

