    /* ===== Model ===== */
    auto model_browser = [&]() {
        if (stage_on()) hint("Stage %d's model: its bones are the parts' pieces.", stageId);
        ImGui::BeginChild("bones");
        int before = scene.selected;
        bone_tree(scene, 0, hoverBone, scrollToSel, [&](int b) { bone_menu(b); });
        if (stage_on() && scene.selected != before) select_bone(scene.selected);
        ImGui::EndChild();
    };
    auto model_inspector = [&]() {
        Model &m = *scene.model;
        heading("Selected bone");
        int b = scene.selected;
        if (stage_on()) {
            if (stagePart >= 0) {
                ImGui::PushFont(g_fontBold);
                ImGui::TextColored(C_SELECT, "%s", part_name(stagePart).c_str());
                ImGui::PopFont();
            }
            hint("Stage parts are edited in Stages.");
            if (ImGui::Button(IC_STAGE "  Go to Stages")) section = STAGES;
        } else if (b > 0 && b < (int)m.bones.size()) {
            const Bone &bone = m.bones[b];
            const Mesh &me = m.meshes[bone.mesh];
            const Pose &p = m.pose[b];
            ImGui::PushFont(g_fontBold);
            ImGui::TextColored(C_SELECT, "bone %d", b);
            ImGui::PopFont();
            int kids = 0;
            for (const Bone &o : m.bones) kids += &o != &bone && o.parent == b;
            if (bone.parent > 0) {
                char l[32];
                snprintf(l, sizeof l, "parent: bone %d", bone.parent);
                same_line_if_fits(l);
                if (ImGui::SmallButton(l)) scene.selected = bone.parent;
                tip("Select it");
            }
            field("Polygons", "%d", (int)me.polys.size());
            field("Vertices", "%d", (int)me.verts.size());
            field("Children", "%d", kids);
            field("Position", "%d %d %d", p.pos.x, p.pos.y, p.pos.z);
            field("Rotation", "%d %d %d", p.rot.x, p.rot.y, p.rot.z);
            field("Scale", "%d %d %d", p.scale.x, p.scale.y, p.scale.z);
            field("Entries", "mesh %d, keys %d", me.entry, bone.keyEntry);
            const char *hide = scene.hidden[b] ? IC_EYE "  Show" : IC_HIDE "  Hide";
            if (ImGui::Button(IC_FIT "  Frame")) frame_sel();
            tip("The camera on it (F)");
            same_line_if_fits(hide);
            if (ImGui::Button(hide)) scene.hidden[b] = !scene.hidden[b];
            tip("H");
            same_line_if_fits("Show only this");
            if (ImGui::Button("Show only this")) show_only(b);
            if (char_on()) {
                heading("Adjust this piece");
                hint("Moves, turns or scales its mesh in the file, in the bone's own axes: the animations still move it. "
                     "Drag the arrows or the box in the view.");
                PieceEdit pe = charEdits.count(b) ? charEdits[b] : PieceEdit();
                bool changed = false;
                auto field_done = [&]() {
                    if (ImGui::IsItemActivated()) char_mark(), charEditing = true;
                    if (ImGui::IsItemDeactivated() && charEditing && !vpMoving) {
                        if (!ImGui::IsItemDeactivatedAfterEdit() && !charUndo.empty()) charUndo.pop_back();
                        charEditing = false;
                    }
                };
                const ImGuiStyle &sty = ImGui::GetStyle();
                float bw = ImGui::GetFrameHeight();
                static const ImU32 AXC[3] = {IM_COL32(236, 84, 84, 255), IM_COL32(118, 208, 96, 255), IM_COL32(84, 142, 245, 255)};
                static const char *AXN[3] = {"X", "Y", "Z"};
                if (ImGui::BeginTable("##piece", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) {
                    ImGui::TableSetupColumn("##l", ImGuiTableColumnFlags_WidthFixed, std::max(bw, ImGui::CalcTextSize("Length").x));
                    ImGui::TableSetupColumn("##f", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("##r", ImGuiTableColumnFlags_WidthFixed, bw);
                    auto row = [&](const char *id, const std::function<void()> &label, const std::function<void()> &field,
                                   const std::function<void()> &reset) {
                        ImGui::PushID(id);
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        label();
                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        field();
                        field_done();
                        ImGui::TableNextColumn();
                        if (ImGui::Button(IC_UNDO, ImVec2(bw, bw))) char_mark(), reset(), changed = true;
                        tip("Back to the start");
                        ImGui::PopID();
                    };
                    for (int a = 0; a < 3; a++)
                        row(AXN[a],
                            [&]() {
                                ImVec2 p0 = ImGui::GetCursorScreenPos();
                                ImDrawList *adl = ImGui::GetWindowDrawList();
                                adl->AddRectFilled(p0, p0 + ImVec2(bw, bw), AXC[a], sty.FrameRounding);
                                adl->AddText(p0 + (ImVec2(bw, bw) - ImGui::CalcTextSize(AXN[a])) * 0.5f, IM_COL32(20, 22, 26, 255), AXN[a]);
                                ImGui::Dummy(ImVec2(bw, bw));
                                tip("In the bone's own axes, in the game's units%s.", a == 1 ? " (up is positive)" : "");
                            },
                            [&]() {
                                float v = a == 1 ? 0.0f - pe.move[1] : pe.move[a];
                                if (ImGui::DragFloat("##v", &v, 2, -4000, 4000, "%.0f")) pe.move[a] = a == 1 ? -v : v, changed = true;
                            },
                            [&]() { pe.move[a] = 0; });
                    row("turn",
                        [&]() {
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextUnformatted("Turn");
                            tip("About the bone's vertical axis.");
                        },
                        [&]() { changed |= ImGui::DragFloat("##v", &pe.turn, 0.5f, -180, 180, "%.1f deg"); }, [&]() { pe.turn = 0; });
                    row("scale",
                        [&]() {
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextUnformatted("Scale");
                            tip("In the view: drag the box.");
                        },
                        [&]() {
                            if (ImGui::DragFloat("##v", &pe.scale, 0.01f, 0.05f, 20, "%.2f")) changed = true, pe.reach = pe.follow ? pe.scale : pe.reach;
                        },
                        [&]() { pe.scale = 1, pe.reach = pe.follow ? 1 : pe.reach; });
                    static const char *SN[3] = {"Width", "Length", "Depth"}, *ST[3] = {"Along the bone's X: how wide the mesh is.", "Along the bone's Y: how long the mesh is.", "Along the bone's Z: how deep the mesh is."};
                    for (int a = 0; a < 3; a++)
                        row(SN[a],
                            [&]() {
                                ImGui::AlignTextToFramePadding();
                                ImGui::TextUnformatted(SN[a]);
                                tip("%s Proportion, not position: the joint stays where it is and so do the bones under it.", ST[a]);
                            },
                            [&]() { changed |= ImGui::DragFloat("##v", &pe.stretch[a], 0.01f, 0.05f, 20, "x %.2f"); }, [&]() { pe.stretch[a] = 1; });
                    row("reach",
                        [&]() {
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextUnformatted("Reach");
                            tip("Where the bones under this one sit from its joint: the limb's length. The mesh stays as it is.");
                        },
                        [&]() { changed |= ImGui::DragFloat("##v", &pe.reach, 0.01f, 0.05f, 20, "%.2f"); }, [&]() { pe.reach = 1; });
                    ImGui::EndTable();
                }
                bool joint = pe.joint, follow = pe.follow;
                if (check_box("Scale about the joint", &joint)) char_mark(), pe.joint = joint, changed = true;
                tip("The mesh grows from the bone's joint, so a limb's segments stay attached. Off: from the mesh's own centre.");
                if (check_box("Reach follows Scale", &follow)) char_mark(), pe.follow = follow, changed = true;
                tip("Scale changes Reach too: the bones under this one move with the mesh. Off: Reach is its own.");
                bool gone = pe.gone;
                if (check_box("Not drawn in the game", &gone)) char_mark(), pe.gone = gone, changed = true;
                tip("Collapses the piece to a point, the way to remove it without touching the animations");
                if (changed) {
                    if (pe.any()) charEdits[b] = pe;
                    else charEdits.erase(b);
                    char_apply();
                    charDirty = true;
                }
            }
        } else {
            hint("Click the model in the view, or a bone on the left, to see it here.");
        }
        if (m.dw2) dw2_size();
        if (char_on()) {
            heading("In the game");
            ImGui::BeginDisabled(!charDirty);
            if (ImGui::Button(IC_SAVE "  Save to game")) char_save();
            ImGui::EndDisabled();
            tip("Writes %s to the port's mods folder (Ctrl+S): it reads it instead of the disc's when it starts", m.name.c_str());
            same_line_if_fits(IC_RESTART "  Revert to the disc's");
            ImGui::BeginDisabled(!char_mod() && !charDirty);
            if (ImGui::Button(IC_RESTART "  Revert to the disc's")) ask_revert_char();
            ImGui::EndDisabled();
            if (charDirty) ImGui::TextColored(C_SELECT, "Unsaved changes");
            else if (char_mod()) ImGui::TextDisabled("This is the mod's file.");
            heading("Blender and the like (OBJ)");
            if (ImGui::Button(IC_EXPORT "  Export OBJ")) char_export();
            tip("One object per bone, named part_N, at rest (y up, 1 unit = 100 of the game's), with the model's texture pages as PNGs. "
                "Move vertices, edit faces and UVs; keep the names, the materials and the PNGs as they are.");
            same_line_if_fits(IC_IMPORT "  Import OBJ...");
            if (ImGui::Button(IC_IMPORT "  Import OBJ...")) {
                static const SDL_DialogFileFilter OBJ = {"OBJ models", "obj"};
                SDL_ShowOpenFileDialog(
                    [](void *u, const char *const *l, int) {
                        if (!l || !*l) return;
                        Picked *p = (Picked *)u;
                        std::lock_guard<std::mutex> lock(p->m);
                        p->obj = *l;
                    },
                    &picked, win, &OBJ, 1, charDir.empty() ? nullptr : charDir.c_str(), false);
            }
            tip("The bones whose faces changed get a new mesh (up to 256 vertices each, textured with the model's own pages); "
                "the others stay. Then Save to game.");
            if (!charDir.empty()) {
                ImGui::SameLine();
                if (ImGui::Button(IC_FOLDER "##chardir")) open_folder(charDir);
                tip("Open %s", charDir.c_str());
            }
        }
        heading("Display");
        display_options();
        if (details("Technical details")) {
            size_t polys = 0, verts = 0;
            for (const Mesh &me : m.meshes) polys += me.polys.size(), verts += me.verts.size();
            field("File", "%s", m.name.c_str());
            field("Entries", "%d", m.arc.count());
            field("Bones", "%d", (int)m.bones.size() - 1);
            field("Polygons", "%d", (int)polys);
            field("Vertices", "%d", (int)verts);
            field("Bone list", "entry %d", m.boneEntry);
            field("Textures", "entry %d", m.texEntry);
            field("Motions", "entry %d", m.motionEntry);
        }
    };

