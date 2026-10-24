    /* ===== a DW2 model's size: the whole model, and the selected bone's mesh and reach, live, kept in tuning.txt (and shape.txt) beside the model ===== */
    auto dw2_size = [&]() {
        Model &m = *scene.model;
        dw2_sync();
        heading("Size");
        {
                Model::Dw2Tuning &t = m.tune;
                bool changed = false;
                auto slider_done = [&]() { /* a drag is a step of undo, none if it changed nothing */
                    if (ImGui::IsItemActivated()) dw2_mark(), dw2Editing = true;
                    if (ImGui::IsItemDeactivated() && dw2Editing) {
                        if (!ImGui::IsItemDeactivatedAfterEdit() && !dw2Undo.empty()) dw2Undo.pop_back();
                        dw2Editing = false;
                    }
                };
                float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
                for (size_t b = 1; b < m.bones.size(); b++) {
                    if (!m.pose[b].visible) continue;
                    const float *w = m.pose[b].world;
                    for (const Vec3s &pv : m.meshes[m.bones[b].mesh].verts)
                        for (int i = 0; i < 3; i++) {
                            float p = w[i] * pv.x + w[4 + i] * pv.y + w[8 + i] * pv.z + w[12 + i];
                            lo[i] = std::min(lo[i], p), hi[i] = std::max(hi[i], p);
                        }
                }
                float height = hi[1] - lo[1];
                static float target = 2771; /* ExVeemon's mean height over its idle, in model units (see the plan) */
                ImGui::SetNextItemWidth(-FLT_MIN);
                changed |= ImGui::SliderFloat("##dw2scale", &t.scale, 0.2f, 3.0f, "Whole model  x %.3f", ImGuiSliderFlags_Logarithmic);
                slider_done();
                tip("Everything: the meshes and the distances between joints (Ctrl+click to type a value)");
                field("Height now", "%.0f", height);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
                ImGui::InputFloat("##dw2target", &target, 0, 0, "%.0f");
                ImGui::SameLine();
                if (ImGui::Button("Set height") && height > 1 && target > 1) dw2_mark(), t.scale *= target / height, changed = true;
                tip("Scales the whole model so it is this tall in the pose shown. 2771 is ExVeemon's average over its idle");
                int sb = scene.selected;
                if (sb > 0 && sb < (int)m.bones.size()) {
                    ImGui::PushFont(g_fontBold);
                    ImGui::TextColored(C_SELECT, "%s", thing_name(sb).c_str());
                    ImGui::PopFont();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    changed |= ImGui::SliderFloat("##dw2mesh", &t.mesh[sb], 0.2f, 3.0f, "Its mesh  x %.3f", ImGuiSliderFlags_Logarithmic);
                    slider_done();
                    tip("This bone's mesh alone, about its joint; the bones under it keep their place");
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    changed |= ImGui::SliderFloat("##dw2reach", &t.reach[sb], 0.2f, 3.0f, "Its reach  x %.3f", ImGuiSliderFlags_Logarithmic);
                    slider_done();
                    tip("How far the bones under it sit from its joint: longer or shorter limbs, without growing the mesh");
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    changed |= ImGui::SliderFloat("##dw2place", &t.place[sb], 0.2f, 3.0f, "Its place  x %.3f", ImGuiSliderFlags_Logarithmic);
                    slider_done();
                    tip("Where this bone's own joint sits from its parent's, and nothing else: the parent's reach moves every bone under it, this only this one (and what hangs from it)");
                    static const char *AXIS[3] = {"##dw2sx", "##dw2sy", "##dw2sz"}, *AXIS_FMT[3] = {"Its width  x %.3f", "Its length  y %.3f", "Its depth  z %.3f"};
                    for (int a = 0; a < 3; a++) {
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        changed |= ImGui::SliderFloat(AXIS[a], &t.shape[sb * 3 + a], 0.2f, 3.0f, AXIS_FMT[a], ImGuiSliderFlags_Logarithmic);
                        slider_done();
                    }
                    tip("The mesh stretched along one axis of the bone, about its joint (shape.txt): slimmer without shorter");
                    if (ImGui::Button("Reset this bone")) dw2_mark(), t.mesh[sb] = t.reach[sb] = t.place[sb] = 1, t.shape[sb * 3] = t.shape[sb * 3 + 1] = t.shape[sb * 3 + 2] = 1, changed = true;
                    ImGui::SameLine();
                } else {
                    hint("Click a bone in the view or the Model tab to tune it alone.");
                }
                if (ImGui::Button("Reset all")) dw2_mark(), t.scale = 1, std::fill(t.mesh.begin(), t.mesh.end(), 1.0f), std::fill(t.reach.begin(), t.reach.end(), 1.0f), std::fill(t.place.begin(), t.place.end(), 1.0f), std::fill(t.shape.begin(), t.shape.end(), 1.0f), changed = true;
                if (button(IC_SAVE "  Save", 0)) {
                    if (m.dw2_save_tuning(dw2_tuning_path(scene.path))) note(NOTE_OK, "Saved the size to " + dw2_tuning_path(scene.path));
                    else note(NOTE_ERROR, "Can't write " + dw2_tuning_path(scene.path));
                }
                tip("Writes tuning.txt (and shape.txt) beside the model: the converter reads the same files");
                if (changed) m.dw2_retune(), upload(scene);
        }
    };

