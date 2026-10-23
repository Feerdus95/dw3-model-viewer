    /* ===== Textures: what the section calls back to main for (App.save_page, App.palette) ===== */
    auto texture_save = [&](uint16_t tp, uint16_t cl) {
        Model &m = *scene.model;
        char name[96];
        snprintf(name, sizeof name, "%s_%04x_%04x.bmp", m.name.substr(0, m.name.find('.')).c_str(), tp, cl);
        save_bmp(g_exe_dir + name, texPage.pixels, texPage.w, texPage.h);
        note(NOTE_OK, std::string("Saved ") + g_exe_dir + name);
    };
    /* the palette of the page's CLUT, for a model that can be edited */
    auto texture_palette = [&](uint16_t tp, uint16_t cl) {
        Model &m = *scene.model;
        if (char_on() && tpage_depth(tp) < 2) {
            heading("Palette");
            int n = tpage_depth(tp) == 0 ? 16 : 256, base = clut_y(cl) * 1024 + clut_x(cl);
            static int pick = -1;
            static float rgb[3];
            bool openPal = false;
            float sw = 20 * dpi, gap = 2 * dpi; /* the swatches in rows as wide as the panel (a 256-color page too) */
            int per = std::max(1, (int)((ImGui::GetContentRegionAvail().x + gap) / (sw + gap)));
            for (int i = 0; i < n; i++) {
                uint16_t c = m.vram[base + i];
                ImGui::PushID(i);
                if (i % per) ImGui::SameLine(0, gap);
                ImVec4 col((c & 31) / 31.0f, (c >> 5 & 31) / 31.0f, (c >> 10 & 31) / 31.0f, 1);
                if (ImGui::ColorButton("##c", col, ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoTooltip, ImVec2(sw, sw))) {
                    pick = base + i;
                    rgb[0] = col.x, rgb[1] = col.y, rgb[2] = col.z;
                    char_mark(), palSession = true, palEdited = false, openPal = true;
                }
                if (c == 0) tip("Transparent (0): coloring it makes it opaque");
                ImGui::PopID();
            }
            if (openPal) ImGui::OpenPopup("##pal");
            if (ImGui::BeginPopup("##pal")) {
                if (ImGui::ColorPicker3("##rgb", rgb, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoAlpha)) {
                    uint16_t old = m.vram[pick];
                    uint16_t v = (uint16_t)((int)lroundf(rgb[0] * 31) | (int)lroundf(rgb[1] * 31) << 5 | (int)lroundf(rgb[2] * 31) << 10 | (old & 0x8000));
                    if (!v) v = 0x0421; /* black, not the transparent 0 */
                    charPal[pick] = v;
                    palEdited = charDirty = true;
                    char_pal_apply();
                }
                if (ImGui::Button(IC_UNDO "  Back to the start")) {
                    palEdited = charDirty = true;
                    charPal.erase(pick);
                    char_pal_apply();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            } else if (palSession) {
                palSession = false;
                if (!palEdited && !charUndo.empty()) charUndo.pop_back();
            }
            hint("Click a color to change it; Save to game writes the model's file with it. The palette is the model's own.");
        }
    };
