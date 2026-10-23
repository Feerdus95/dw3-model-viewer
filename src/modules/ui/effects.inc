    /* ===== Effects ===== */
    auto effect_row = [&](int id) {
        const std::set<std::string> &u = fxUsers[id], &t = fxTechs[id];
        std::string l = "effect " + std::to_string(id);
        if (!t.empty()) l += "  " + joined(t);
        else if (u.size() > 1) l += "  (" + std::to_string(u.size()) + " Digimon)";
        else if (u.empty()) l += "  (the battle's own)";
        ImGui::PushID(id);
        if (ImGui::Selectable(l.c_str(), fx.id == id, ImGuiSelectableFlags_AllowOverlap) && fx.id != id) pick_effect(id, false);
        if (u.size() > 1 || (!t.empty() && !u.empty())) tip("%s", joined(u).c_str());
        if (ImGui::BeginPopupContextItem("##menu")) {
            menu_title("effect " + std::to_string(id), "%s", t.empty() ? joined(u).c_str() : joined(t).c_str());
            if (ImGui::MenuItem(IC_OPEN "  Open")) pick_effect(id, false);
            if (ImGui::MenuItem(IC_COPY "  Copy \"effect N\"")) ImGui::SetClipboardText(("effect " + std::to_string(id)).c_str());
            ImGui::EndPopup();
        }
        if (ownIds.count(id)) {
            ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize("own").x);
            ImGui::TextColored(C_ACCENT, "own");
        }
        ImGui::PopID();
    };
    auto effects_browser = [&]() {
        if (!fxGrouped) group_effects(), ownIds = own_effects(ownRoot);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##fxfind", IC_SEARCH "  Search: Digimon, id or technique", fxFind, sizeof fxFind);
        if (ImGui::BeginTabBar("fxlists")) {
            if (ImGui::BeginTabItem("Original")) {
                ImGui::BeginChild("fxlist");
                if (*fxFind) {
                    for (int id : fxIds)
                        if (effect_found(id)) effect_row(id);
                } else {
                    if (ImGui::CollapsingHeader("Common", ImGuiTreeNodeFlags_DefaultOpen)) {
                        hint("The battle's own (hits, digivolving, items) and those several Digimon start");
                        for (int id : fxCommon) effect_row(id);
                    }
                    if (ImGui::CollapsingHeader("By Digimon", ImGuiTreeNodeFlags_DefaultOpen)) {
                        for (const auto &it : fxByDigimon) {
                            bool open = std::find(it.second.begin(), it.second.end(), fx.id) != it.second.end();
                            ImGui::SetNextItemOpen(open, ImGuiCond_Once);
                            if (ImGui::TreeNode(it.first.c_str())) {
                                for (int id : it.second) effect_row(id);
                                ImGui::TreePop();
                            }
                        }
                    }
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            char modTab[32];
            snprintf(modTab, sizeof modTab, "With own images (%d)###mod", (int)ownIds.size());
            if (ImGui::BeginTabItem(modTab)) {
                ImGui::BeginChild("fxmods");
                if (ownIds.empty()) hint("No effect has own images in %s yet.", ownRoot.c_str());
                for (int id : std::set<int>(ownIds)) /* a copy: opening one rescans ownIds */
                    if (effect_found(id)) effect_row(id);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    };
    auto effects_inspector = [&]() {
        if (fx.id < 0) {
            title("No effect open");
            return hint("Pick an effect on the left: the battle's 2D effects, its sparks, hits and techniques' sprites.");
        }
        title("Effect %d", fx.id);
        const std::set<std::string> &t = fxTechs[fx.id], &u = fxUsers[fx.id];
        auto some = [&](const std::set<std::string> &s) { /* the first few; all of them in the tooltip */
            std::string out;
            int n = 0;
            for (const std::string &x : s)
                if (n++ < 6) out += (out.empty() ? "" : ", ") + x;
            if (n > 6) out += " and " + std::to_string(n - 6) + " more";
            return out;
        };
        if (!t.empty()) ImGui::TextWrapped("%s", some(t).c_str()), tip("%s", joined(t).c_str());
        if (!u.empty()) hint("Started by %s", some(u).c_str()), tip("%s", joined(u).c_str());
        field("Length", "%d ticks, %.2f s", fx.duration, fx.duration / 60.0f);
        field("Animations", "%d, sheet %d", (int)fx.anims.size(), fx.sheetIndex);
        if (ImGui::Button(IC_FIT "  Fit")) fxFitPending = true;
        tip("The whole effect in the view (F)");
        same_line_if_fits(IC_EXPORT "  Export PNGs");
        if (ImGui::Button(IC_EXPORT "  Export PNGs")) {
            int n = export_effect(fx, fxDir);
            note(NOTE_OK, "Saved " + std::to_string(n) + " frames to " + fxDir);
        }
        tip("Every other field as a 320x240 PNG with its transparency, in effects/effect_%d", fx.id);
        if (!fxDir.empty()) {
            ImGui::SameLine();
            if (ImGui::Button(IC_FOLDER "##fxdir")) open_folder(fxDir);
            tip("Open %s", fxDir.c_str());
        }

        OwnSnap before = own_snap();
        bool changed = false;
        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Own images (native port)", ImGuiTreeNodeFlags_DefaultOpen)) {
            hint("PNGs the native port draws instead of this effect.");
            int crit = ownCritical;
            if (segmented("kind", &crit, {"Hit", "Critical hit"})) reload_own(crit != 0);
            ImGui::SameLine();
            if (ownDirty) chip(IC_WARN " Unsaved", C_WARN);
            else if (own.frames.empty()) chip("None yet", C_DIM);
            else chip(own.hasJson ? "Saved" : "PNGs only", C_ACCENT);
            if (ImGui::Checkbox("Show them in the view", &ownShow)) fxTime = 0, fxDrawn = -1;
            tip("Instead of the original effect");
            if (ImGui::Button(IC_IMPORT "  Import PNGs...")) {
                static const SDL_DialogFileFilter PNG = {"PNG images", "png"};
                SDL_ShowOpenFileDialog(
                    [](void *u, const char *const *list, int) {
                        if (!list) return;
                        Picked *p = (Picked *)u;
                        std::lock_guard<std::mutex> lock(p->m);
                        for (; *list; list++) p->files.push_back(*list);
                    },
                    &picked, win, &PNG, 1, nullptr, true);
            }
            tip("Copies them into the folder below and adds them as frames, in their names' order. "
                "An image is the 320x240 screen around the effect's point, at any size of that shape.");
            same_line_if_fits(IC_SAVE "  Save");
            ImGui::BeginDisabled(own.frames.empty());
            if (button(IC_SAVE "  Save", ownDirty ? 1 : 0)) own_save();
            ImGui::EndDisabled();
            tip("Writes effect.json: the frames, their ticks and the placement (Ctrl+S)");
            same_line_if_fits(IC_RESTART "  Reload");
            if (ImGui::Button(IC_RESTART "  Reload")) reload_own(ownCritical);
            tip("Reads the folder again");
            hint("Folder: %s", nice_path(own.dir).c_str());
            if (ImGui::SmallButton(IC_FOLDER " Open it")) SDL_CreateDirectory(own.dir.c_str()), open_folder(own.dir);
            same_line_if_fits("Change the mods folder...");
            if (ImGui::SmallButton("Change the mods folder...")) pick_mods_folder();
            tip("The native port's data/mods/effects folder");
            if (!own.frames.empty()) {
                hint("%d frames, %d ticks = %.2f s (the original: %.2f s)", (int)own.frames.size(), own.duration(), own.duration() / 60.0f,
                     fx.duration / 60.0f);
                float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2 - ImGui::CalcTextSize("scale").x -
                             ImGui::GetStyle().ItemInnerSpacing.x;
                ImGui::PushItemWidth(half);
                changed |= ImGui::DragFloat("x", &own.x, 0.5f, -320, 320, "%.1f");
                ImGui::SameLine();
                changed |= ImGui::DragFloat("y", &own.y, 0.5f, -240, 240, "%.1f");
                changed |= ImGui::DragFloat("scale", &own.scale, 0.01f, 0.05f, 8, "%.2f");
                ImGui::SameLine();
                int blend = own.add;
                if (ImGui::Combo("blend", &blend, "alpha\0add\0")) own.add = blend != 0, changed = true;
                ImGui::PopItemWidth();
                int playing = ownShow ? own.frameAt((int)fxTime) : -1, act = -1, what = 0;
                if (ImGui::BeginTable("ownframes", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
                    ImGui::TableSetupColumn("#");
                    ImGui::TableSetupColumn("Image", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Ticks", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 5);
                    ImGui::TableSetupColumn("##do");
                    ImGui::TableHeadersRow();
                    for (int i = 0; i < (int)own.frames.size(); i++) {
                        CustomFrame &fr = own.frames[i];
                        ImGui::PushID(i);
                        ImGui::TableNextRow();
                        if (i == playing) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(76, 141, 255, 50));
                        ImGui::TableNextColumn();
                        char n[8];
                        snprintf(n, sizeof n, "%d", i);
                        if (ImGui::Selectable(n, ownFrame == i, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
                            ownFrame = ownFrame == i ? -1 : i;
                        if (ImGui::BeginPopupContextItem("##menu")) {
                            ownFrame = i;
                            menu_title("frame " + std::to_string(i), "%s, %d ticks", fr.file.c_str(), fr.ticks);
                            if (ImGui::MenuItem(IC_COPY "  Copy", "Ctrl+C")) do_copy();
                            if (ImGui::MenuItem(IC_PASTE "  Paste after it", "Ctrl+V", false, !frameClip.empty())) act = i, what = 3;
                            if (ImGui::MenuItem(IC_ADD "  Duplicate", "Ctrl+D")) act = i, what = 1;
                            if (ImGui::MenuItem(IC_UP "  Move up", nullptr, false, i > 0)) act = i, what = -2;
                            if (ImGui::MenuItem(IC_DOWN "  Move down", nullptr, false, i + 1 < (int)own.frames.size())) act = i, what = 2;
                            ImGui::Separator();
                            if (ImGui::MenuItem(IC_DELETE "  Delete", "Del")) act = i, what = 0;
                            ImGui::EndPopup();
                        }
                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::BeginCombo("##file", fr.file.c_str())) {
                            for (const auto &it : own.images)
                                if (ImGui::Selectable(it.first.c_str(), it.first == fr.file)) fr.file = it.first, changed = true;
                            ImGui::EndCombo();
                        }
                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::InputInt("##ticks", &fr.ticks, 0)) fr.ticks = std::clamp(fr.ticks, 1, 600), changed = true;
                        tip("How long it shows, in ticks (1/60 s)");
                        ImGui::TableNextColumn();
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                        if (ImGui::SmallButton(IC_UP)) act = i, what = -2;
                        tip("Move up");
                        ImGui::SameLine();
                        if (ImGui::SmallButton(IC_DOWN)) act = i, what = 2;
                        tip("Move down");
                        ImGui::SameLine();
                        if (ImGui::SmallButton(IC_ADD)) act = i, what = 1;
                        tip("Duplicate (Ctrl+D)");
                        ImGui::SameLine();
                        if (ImGui::SmallButton(IC_DELETE)) act = i, what = 0;
                        tip("Delete (Del)");
                        ImGui::PopStyleColor();
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                if (act >= 0 && what == 3) ownFrame = act, frame_paste();
                else if (act >= 0) frame_edit(act, what);
                hint("Click a frame to pick it: Ctrl+C, Ctrl+V, Ctrl+D and Del work on it, and Ctrl+Z undoes.");
            }
            if (!own.error.empty()) ImGui::TextColored(C_ERROR, IC_ERROR "  %s", own.error.c_str());
        }
        if (changed) {
            if (ImGui::GetTime() - ownPushTime > 0.6) ownHist.push(before);
            ownPushTime = ImGui::GetTime();
            ownDirty = true, fxDrawn = -1;
        }
        if (details("Technical details")) {
            field("Animations", "%s #%d entry %d", fid_name(fx.fid).c_str(), fx.fid, fx.entry);
            field("Sheet", "%s #%d entry %d", fid_name(fx.sheetFid).c_str(), fx.sheetFid, fx.sheetEntry);
            field("Fields", "%d", fx.duration);
        }
    };

