    /* ===== Stages ===== */
    auto stages_browser = [&]() {
        float h = stage_on() ? ImGui::GetContentRegionAvail().y * 0.42f : 0;
        if (ImGui::BeginTable("stages", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit, ImVec2(0, h))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("#");
            ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("State");
            ImGui::TableHeadersRow();
            for (const StageInfo &si : stageInfos) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(si.id);
                char l[8];
                snprintf(l, sizeof l, "%2d", si.id);
                bool sel = si.id == stageId && view.stageView;
                if (ImGui::Selectable(l, sel, ImGuiSelectableFlags_SpanAllColumns) && !sel) open_stage(si.id);
                if (ImGui::BeginPopupContextItem("##menu")) {
                    menu_title("Stage " + std::to_string(si.id), "%s", stage_look(si.id));
                    if (ImGui::MenuItem(IC_OPEN "  Open")) open_stage(si.id);
                    if (ImGui::MenuItem(IC_COPY "  Copy its name"))
                        ImGui::SetClipboardText(("stage " + std::to_string(si.id) + " " + stage_look(si.id)).c_str());
                    ImGui::EndPopup();
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(stage_look(si.id));
                ImGui::TableNextColumn();
                if (sel && stageDirty) ImGui::TextColored(C_WARN, "unsaved");
                else if (stageMods.count(si.fid)) ImGui::TextColored(C_ACCENT, "mod");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (!stage_on()) return;
        char head[48];
        snprintf(head, sizeof head, "Parts (%d)", (int)stageEdit.parts.size());
        heading(head);
        if (ImGui::BeginTable("parts", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("##eye");
            ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##always");
            ImGui::TableSetupColumn("##polys");
            for (int k = 0; k < (int)stageEdit.parts.size(); k++) {
                const StagePart &pt = stageEdit.parts[k];
                ImGui::TableNextRow();
                ImGui::PushID(k);
                ImGui::TableNextColumn();
                bool shown = !stageHidden.count(pt.name);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(shown ? ImGuiCol_Text : ImGuiCol_TextDisabled));
                if (ImGui::SmallButton(shown ? IC_EYE : IC_HIDE)) part_hide(k);
                ImGui::PopStyleColor(2);
                tip(shown ? "Shown in the view: click to hide it there (the game still draws it)" : "Hidden in the view: click to show it");
                ImGui::TableNextColumn();
                if (ImGui::Selectable(pt.name.c_str(), stagePart == k, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
                    select_part(stagePart == k ? -1 : k);
                if (ImGui::IsItemHovered() && k < (int)stageMade.firstBone.size()) hoverBone = stageMade.firstBone[k];
                if (stagePart == k && scrollToSel) ImGui::SetScrollHereY();
                size_t count = stageEdit.parts.size();
                if (ImGui::BeginPopupContextItem("##menu")) {
                    part_menu(k);
                    ImGui::EndPopup();
                }
                if (stageEdit.parts.size() != count) { /* the menu added or removed one: pt is gone */
                    ImGui::PopID();
                    break;
                }
                ImGui::TableNextColumn();
                if (pt.noBounds) {
                    ImGui::TextColored(C_ACCENT, IC_OK);
                    tip("Always drawn (no bounds check)");
                }
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%d polys", (int)pt.faces.size());
                ImGui::PopID();
                if (k >= (int)stageEdit.parts.size()) break; /* a menu removed it */
            }
            ImGui::EndTable();
        }
    };
