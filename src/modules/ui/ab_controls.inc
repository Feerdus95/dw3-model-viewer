    /* ===== A/B under the view: a view control, like play and pause ===== */
    auto ab_bar = [&]() {
        const Fighter *f = scene_fighter(scene);
        if (!f) return;
        ImGui::AlignTextToFramePadding();
        if (button("A  The script's camera", cand.on ? 0 : 1)) set_ab(false);
        tip("The camera the script asks for, as the game runs it. Switching changes only the view, on the frame that is showing (key B toggles)");
        ImGui::SameLine();
        if (button((std::string("B  ") + (b_is_sequence(choreo) ? "The sequence" : "The candidate")).c_str(), cand.on ? 1 : 0)) set_ab(true);
        tip("%s, at the same moments. Choose what B shows in the workbench", b_is_sequence(choreo) ? "The choreography's sequence" : "The candidate camera");
        ImGui::SameLine();
        ImGui::TextColored(cand.on ? C_WARN : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "%s", cams_showing(cand.on, choreo).c_str());
    };
