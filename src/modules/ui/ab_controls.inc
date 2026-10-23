    /* ===== A/B under the view: a view control, like play and pause ===== */
    auto ab_bar = [&]() {
        const Fighter *f = scene_fighter(scene);
        if (!f) return;
        ImGui::AlignTextToFramePadding();
        /* the names in full when both fit the bar, then the letters and a word, then the letters */
        std::string bName = b_is_sequence(choreo) ? "The sequence" : "The candidate", aL = "A  The script's camera", bL = "B  " + bName;
        float need = ImGui::CalcTextSize(aL.c_str()).x + ImGui::CalcTextSize(bL.c_str()).x + (ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x) * 2;
        if (need > ImGui::GetContentRegionAvail().x) aL = "A  Script", bL = "B  " + std::string(b_is_sequence(choreo) ? "Sequence" : "Candidate");
        need = ImGui::CalcTextSize(aL.c_str()).x + ImGui::CalcTextSize(bL.c_str()).x + (ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x) * 2;
        if (need > ImGui::GetContentRegionAvail().x) aL = "A", bL = "B";
        if (button(aL.c_str(), cand.on ? 0 : 1)) set_ab(false);
        tip("The camera the script asks for, as the game runs it. Switching changes only the view, on the frame that is showing (key B toggles)");
        ImGui::SameLine();
        if (button(bL.c_str(), cand.on ? 1 : 0)) set_ab(true);
        tip("%s, at the same moments. Choose what B shows in the workbench", b_is_sequence(choreo) ? "The choreography's sequence" : "The candidate camera");
        std::string showing = cams_showing(cand.on, choreo);
        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize(showing.c_str()).x > ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) return; /* the buttons say it already */
        ImGui::SameLine();
        ImGui::TextColored(cand.on ? C_WARN : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "%s", cams_showing(cand.on, choreo).c_str());
    };
