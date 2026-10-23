    auto stages_inspector = [&]() {
        if (!stage_on()) {
            title("No stage open");
            hint("Open one of the 57 fight stages on the left. You can move, turn and scale its parts, add parts from "
                 "other stages, change its background, swap it through Blender (OBJ), and save it for the native port.");
            return;
        }
        const StageInfo &si = stageInfos[stageId];
        title("Stage %d", stageId);
        ImGui::TextWrapped("%s", stage_look(stageId));
        if (stageDirty) chip(IC_WARN " Unsaved changes", C_WARN);
        else if (stageMods.count(si.fid)) chip("Modded: saved for the game", C_ACCENT);
        else chip("The disc's, unchanged", C_DIM);
        ImGui::Spacing();
        if (button(IC_SAVE "  Save to game", 1)) stage_save();
        tip("Writes the stage's model and its line of the stages' table (0459.bin) to the mods folder, which the native "
            "port reads instead of the disc's (Ctrl+S)");
        if (stageMods.count(si.fid) || stageDirty) {
            same_line_if_fits(IC_RESTART "  Revert to the disc's");
            if (button(IC_RESTART "  Revert to the disc's", 2)) ask_revert();
            tip("Deletes the mod: the stage is the disc's again");
        }
        hint("Saves to %s", nice_path(mod_path(si.fid)).c_str());
        float bgc[3] = {stageEdit.bg[0] / 255.0f, stageEdit.bg[1] / 255.0f, stageEdit.bg[2] / 255.0f};
        if (ImGui::ColorEdit3("Background", bgc, ImGuiColorEditFlags_NoInputs)) {
            if (ImGui::GetTime() - bgPushTime > 0.75) stage_mark();
            bgPushTime = ImGui::GetTime();
            for (int k = 0; k < 3; k++) stageEdit.bg[k] = stageInfo.bg[k] = (uint8_t)lroundf(bgc[k] * 255), view.stageBg[k] = bgc[k];
            stageDirty = true;
        }
        tip("What shows through the panorama's gaps (the stage's bgColor)");

        heading("Selected part");
        if (stagePart >= 0 && stagePart < (int)stageEdit.parts.size()) {
            StagePart &pt = stageEdit.parts[stagePart];
            ImGui::PushFont(g_fontBold);
            ImGui::TextColored(C_SELECT, "%s", pt.name.c_str());
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::TextDisabled("part %d, %d polygons", stagePart, (int)pt.faces.size());
            float before[5];
            part_place(stagePart, before);
            auto part_field = [&](const char *verb) {
                if (ImGui::IsItemActivated()) part_begin(verb, before);
                if (ImGui::IsItemDeactivated() && partEditing && !vpMoving) part_end(ImGui::IsItemDeactivatedAfterEdit());
            };
            /* label, field and a button that puts it back, a row each, in columns: the field takes what is
               left, so nothing falls off the panel's edge at any width. X, Y, Z are colored; y is up */
            const ImGuiStyle &sty = ImGui::GetStyle();
            float bw = ImGui::GetFrameHeight();
            static const ImU32 AXC[3] = {IM_COL32(236, 84, 84, 255), IM_COL32(118, 208, 96, 255), IM_COL32(84, 142, 245, 255)};
            static const char *AXN[3] = {"X", "Y", "Z"};
            static const char *AXT[3] = {"Left and right.", "Up and down: up is positive (the game's y goes down).", "Depth."};
            if (ImGui::BeginTable("##place", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) {
                ImGui::TableSetupColumn("##l", ImGuiTableColumnFlags_WidthFixed, std::max(bw, ImGui::CalcTextSize("Scale").x));
                ImGui::TableSetupColumn("##f", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("##r", ImGuiTableColumnFlags_WidthFixed, bw);
                auto row = [&](const char *id, const std::function<void()> &label, const std::function<void()> &field, const char *verb,
                               const std::function<void()> &reset) {
                    ImGui::PushID(id);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    label();
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    field();
                    part_field(verb);
                    ImGui::TableNextColumn();
                    if (ImGui::Button(IC_UNDO, ImVec2(bw, bw))) part_begin(verb, before), reset(), part_end(true);
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
                            tip("%s In the game's units. In the view: drag its arrow.", AXT[a]);
                        },
                        [&]() {
                            float v = a == 1 ? 0.0f - pt.move[1] : pt.move[a]; /* 0 - x: never -0 */
                            if (ImGui::DragFloat("##v", &v, 10, -40000, 40000, "%.0f")) pt.move[a] = a == 1 ? -v : v;
                        },
                        "Moving", [&]() { pt.move[a] = 0; });
                row("turn",
                    [&]() {
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextUnformatted("Turn");
                        tip("Around the vertical axis. In the view: drag the ring.");
                    },
                    [&]() { ImGui::DragFloat("##v", &pt.turn, 0.5f, -180, 180, "%.1f deg"); }, "Turning", [&]() { pt.turn = 0; });
                row("scale",
                    [&]() {
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextUnformatted("Scale");
                        tip("In the view: drag the box.");
                    },
                    [&]() { ImGui::DragFloat("##v", &pt.scale, 0.01f, 0.05f, 20, "%.2f"); }, "Scaling", [&]() { pt.scale = 1; });
                ImGui::EndTable();
            }
            bool nb = pt.noBounds;
            if (ImGui::Checkbox("Always drawn", &nb)) part_bounds(stagePart);
            tip("No bounds check: drawn even when its box seems off screen (the ground and the panorama; 8 bones at most)");
            int k = stagePart;
            const char *hide = stageHidden.count(pt.name) ? IC_EYE "  Show" : IC_HIDE "  Hide";
            if (ImGui::Button(IC_FIT "  Frame")) frame_sel();
            tip("The camera on it (F)");
            same_line_if_fits(hide);
            if (ImGui::Button(hide)) part_hide(k);
            tip("Only in the view: the game still draws it (H)");
            same_line_if_fits(IC_ADD "  Duplicate");
            if (ImGui::Button(IC_ADD "  Duplicate")) part_duplicate(k);
            tip("Ctrl+D");
            same_line_if_fits(IC_DELETE "  Delete");
            if (button(IC_DELETE "  Delete", 2)) part_delete(k);
            tip("Del; Ctrl+Z brings it back");
        } else {
            hint("Click a part in the view or in the Parts list. Right-click one for its actions. Then drag its arrows to move it, the ring to turn it and the box to scale it.");
        }

        props_panel();

        heading("Add parts from another stage");
        if (button(IC_ADD "  Open the catalog...")) catOpen = true;
        tip("Every part of the 57 stages as a picture, with a search: double-click one to add it here");
        hint("Or copy a part (Ctrl+C) and paste it in another stage (Ctrl+V)."); 

        heading("Blender and the like (OBJ)");
        hint("Export to edit outside, import to bring it back. Keep the objects' names to keep their parts.");
        auto obj_export = [&](int only) {
            char nm[16];
            std::string name = only < 0 ? std::string("stage_") + (stageId < 10 ? "0" : "") + std::to_string(stageId) : part_name(only);
            for (char &c : name)
                if (!isalnum((unsigned char)c) && c != '_' && c != '-' && c != '.') c = '_';
            snprintf(nm, sizeof nm, "stage_%02d", stageId);
            stageDir = g_exe_dir + "stages";
            SDL_CreateDirectory(stageDir.c_str());
            stageDir += std::string("/") + nm;
            if (stageEdit.exportObj(stageDir, name, only)) note(NOTE_OK, "Exported " + (only < 0 ? "stage " + std::to_string(stageId) : "part " + name) + " to " + stageDir);
            else note(NOTE_ERROR, "Can't write " + stageDir);
        };
        if (ImGui::Button(IC_EXPORT "  Export stage")) obj_export(-1);
        tip("All the parts in one OBJ with its MTL and PNGs (y up, 1 unit = 100 of the game's). A material's _abrN is its "
            "semi-transparency.");
        same_line_if_fits(IC_EXPORT "  Export part");
        ImGui::BeginDisabled(stagePart < 0);
        if (ImGui::Button(IC_EXPORT "  Export part")) obj_export(stagePart);
        ImGui::EndDisabled();
        tip(stagePart < 0 ? "Select a part first (in the view or the Parts list)" : "Only the selected part, in its own OBJ named after it");
        if (ImGui::Button(IC_IMPORT "  Import OBJ...")) {
            static const SDL_DialogFileFilter OBJ = {"OBJ models", "obj"};
            SDL_ShowOpenFileDialog(
                [](void *u, const char *const *l, int) {
                    if (!l || !*l) return;
                    Picked *p = (Picked *)u;
                    std::lock_guard<std::mutex> lock(p->m);
                    p->obj = *l;
                },
                &picked, win, &OBJ, 1, stageDir.empty() ? nullptr : stageDir.c_str(), false);
        }
        tip("Its objects become the stage's parts: textures repacked into the stage's three pages, 16 colors each "
            "piece, shrunk when they don't fit");
        if (!stageDir.empty()) {
            ImGui::SameLine();
            if (ImGui::Button(IC_FOLDER "##stagedir")) open_folder(stageDir);
            tip("Open %s", stageDir.c_str());
        }

        if (details("Technical details")) {
            std::string shared;
            for (const StageInfo &o : stageInfos)
                if (o.fid == si.fid && o.id != si.id) shared += (shared.empty() ? "" : ", ") + std::to_string(o.id);
            int polys = 0;
            for (const StagePart &pt : stageEdit.parts) polys += (int)pt.faces.size();
            field("Model file", "%s #%d", fid_name(si.fid).c_str(), si.fid);
            field("Bone list", "entry %d", si.boneEntry);
            if (!shared.empty()) field("Shared with", "stage %s", shared.c_str());
            field("Parts", "%d, %d polygons", (int)stageEdit.parts.size(), polys);
            if (stageMade.islands)
                field("Textures", "%d pieces%s", stageMade.islands, stageMade.shrink < 1 ? ", shrunk to fit" : "");
            field("Source", "%s", stageMods.count(si.fid) ? "the mod" : "the disc");
        }
    };

