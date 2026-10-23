    /* ===== Motions ===== */
    auto motion_frames = [](const std::vector<MotionStep> &st) {
        int frames = 0;
        for (const MotionStep &x : st)
            if (x.index != 0x7FFF) frames += std::max<int>(x.count, 0);
        return frames;
    };
    auto motions_browser = [&]() {
        Model &m = *scene.model;
        if (m.motions.empty()) return hint("No motions in this file: it shows its first keys.");
        if (ImGui::BeginTable("motions", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Motion", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Steps");
            ImGui::TableSetupColumn("Frames");
            ImGui::TableHeadersRow();
            for (int i = 1; i <= (int)m.motions.size(); i++) {
                const std::vector<MotionStep> &st = m.motions[i - 1];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                char l[24];
                snprintf(l, sizeof l, "%s motion %d", m.motion == i && view.playing ? IC_PLAY : "    ", i);
                if (ImGui::Selectable(l, m.motion == i, ImGuiSelectableFlags_SpanAllColumns)) m.setMotion(i), view.playing = true;
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem(IC_PLAY "  Play")) m.setMotion(i), view.playing = true;
                    if (ImGui::MenuItem(IC_COPY "  Copy \"motion N\"")) ImGui::SetClipboardText(("motion " + std::to_string(i)).c_str());
                    ImGui::EndPopup();
                }
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%d", (int)st.size() - 1);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%d", motion_frames(st));
            }
            ImGui::EndTable();
        }
    };
    auto motions_inspector = [&]() {
        Model &m = *scene.model;
        if (!m.motions.empty()) {
            heading("Now playing");
            field("Motion", "%d of %d", m.motion, (int)m.motions.size());
            field("Keyframe", "%d of %d", m.keyframe, m.keyframeCount);
            field("Frame", "%d", m.frame);
            field("State", "%s%s%s", view.playing ? "playing" : "paused", m.blending ? ", blending" : "", m.motionDone ? ", done" : "");
            heading("Playback");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderFloat("##speed", &view.speed, 0.05f, 4, "speed %.2fx");
            ImGui::Checkbox("Loop", &view.loop);
            hint("Space plays or pauses, the arrow keys step a keyframe. The bar under the view has the same controls.");
        }
        heading("Display");
        display_options();
    };

