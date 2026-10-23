    /* ===== the playback bar under the view: a motion's or an effect's ===== */
    auto transport = [&]() {
        bool fxMode = section == EFFECTS;
        Model *m = scene.model.get();
        bool &playing = fxMode ? fxPlaying : view.playing;
        bool &loop = fxMode ? fxLoop : view.loop;
        float b = ImGui::GetFrameHeight() * 1.4f, F = ImGui::GetFontSize();
        /* what fits: the speed and the motion's list go first when the view is narrow */
        float spare = ImGui::GetContentRegionAvail().x - b * 4.2f - ImGui::GetStyle().ItemSpacing.x * 6 - F * 11;
        bool speed = spare > F * 6, list = !fxMode && spare > F * 13.5f;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::Button(IC_RESTART, ImVec2(b, 0))) fxMode ? void(fxTime = 0) : m->setMotion(m->motion);
        tip("Restart");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (button(playing ? IC_PAUSE : IC_PLAY, 1, ImVec2(b * 1.2f, 0))) playing = !playing;
        tip(playing ? "Pause (Space)" : "Play (Space)");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::Button(IC_STEP, ImVec2(b, 0))) {
            playing = false;
            if (fxMode) fxTime = std::min(fxTime + 1, (float)std::max(fx_length() - 1, 0));
            else m->advance(1), m->evaluate();
        }
        tip("Step (Right arrow; Left steps back)");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (loop) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
        else ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::Button(IC_LOOP, ImVec2(b, 0))) loop = !loop;
        ImGui::PopStyleColor();
        tip(loop ? "Loop: on" : "Loop: off");
        if (speed) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
            ImGui::SliderFloat("##speed", &view.speed, 0.05f, 4, "%.2fx");
            tip("Speed");
        }
        if (list) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7);
            std::string cur = "motion " + std::to_string(m->motion);
            if (ImGui::BeginCombo("##motion", cur.c_str())) {
                for (int i = 1; i <= (int)m->motions.size(); i++)
                    if (ImGui::Selectable(("motion " + std::to_string(i)).c_str(), m->motion == i)) m->setMotion(i), playing = true;
                ImGui::EndCombo();
            }
        }
        ImGui::SameLine();
        char pos[32];
        if (fxMode) snprintf(pos, sizeof pos, "tick %d / %d", (int)fxTime, std::max(fx_length() - 1, 0));
        else snprintf(pos, sizeof pos, "key %d / %d", m->keyframe, std::max(m->keyframeCount - 1, 0));
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (fxMode) {
            float ft = fxTime;
            if (ImGui::SliderFloat("##tick", &ft, 0, (float)std::max(fx_length() - 1, 0), pos)) fxTime = ft, playing = false;
            tip("1 tick = 1/60 s, as the battle counts time");
        } else {
            int key = m->keyframe;
            if (ImGui::SliderInt("##key", &key, 0, std::max(m->keyframeCount - 1, 0), pos)) {
                playing = false;
                m->keyframe = key, m->motionDone = false;
                m->evaluate();
            }
            tip("frame %d%s%s", m->frame, m->blending ? " (blending)" : "", m->motionDone ? ", done" : "");
        }
        if (!fxMode && (section == FIGHTER || section == CAMERAS) && !scene.fighters.empty() && !m->dw2) ab_bar();
    };
    auto step = [&](int dir) {
        if (section == EFFECTS && fx.id >= 0) {
            fxPlaying = false;
            fxTime = std::clamp(fxTime + dir, 0.0f, (float)std::max(fx_length() - 1, 0));
        } else if (scene.model && scene.model->keyframeCount > 0) {
            Model &m = *scene.model;
            view.playing = false;
            if (dir > 0) m.advance(1);
            else m.keyframe = std::max(m.keyframe - 1, 0), m.motionDone = false;
            m.evaluate();
        }
    };
