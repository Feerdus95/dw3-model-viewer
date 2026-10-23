    /* ===== Textures ===== */
    /* the texture page and CLUT pairs the model's polygons read */
    auto tex_combos = [&]() {
        std::vector<std::pair<uint16_t, uint16_t>> combos;
        std::set<std::pair<uint16_t, uint16_t>> seen;
        if (!scene.model) return combos;
        for (const Mesh &me : scene.model->meshes)
            for (const Poly &p : me.polys)
                if (p.textured && seen.insert({(uint16_t)(p.tpage & 0x19F), p.clut}).second)
                    combos.push_back({(uint16_t)(p.tpage & 0x19F), p.clut});
        return combos;
    };
    auto combo_label = [](std::pair<uint16_t, uint16_t> c) {
        static const char *DEPTH[] = {"4-bit", "8-bit", "16-bit", "?"};
        char l[64];
        snprintf(l, sizeof l, "%s page %d,%d  CLUT %d,%d", DEPTH[tpage_depth(c.first)], tpage_x(c.first), tpage_y(c.first),
                 clut_x(c.second), clut_y(c.second));
        return std::string(l);
    };
    auto textures_browser = [&]() {
        std::vector<std::pair<uint16_t, uint16_t>> combos = tex_combos();
        if (combos.empty()) return hint("No textured polygons.");
        hint("Each page with a palette (CLUT) the polygons read:");
        for (int i = 0; i < (int)combos.size(); i++) {
            std::string l = combo_label(combos[i]);
            ImGui::PushID(i);
            if (ImGui::Selectable(l.c_str(), view.texCombo == i)) view.texCombo = i, texFitPending = true;
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem(IC_COPY "  Copy")) ImGui::SetClipboardText(l.c_str());
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        heading("TIMs");
        for (const Tim &t : scene.model->tims)
            hint("%dx%d halfwords at %d,%d%s", t.w, t.h, t.x, t.y, t.rlen ? " (RLEN)" : "");
    };
    auto textures_inspector = [&]() {
        Model &m = *scene.model;
        std::vector<std::pair<uint16_t, uint16_t>> combos = tex_combos();
        if (combos.empty()) return hint("No textured polygons.");
        auto [tp, cl] = combos[std::min(view.texCombo, (int)combos.size() - 1)];
        title("Texture page");
        ImGui::PushFont(g_fontMono);
        ImGui::TextWrapped("%s", combo_label({tp, cl}).c_str());
        ImGui::PopFont();
        ImGui::Checkbox("UVs", &view.uvs);
        tip("Where the polygons take their texels from");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderFloat("##zoom", &view.texZoom, 0.5f, 12, "zoom %.1fx");
        if (ImGui::Button(IC_FIT "  Fit")) texFitPending = true;
        tip("The whole page in the view (F)");
        same_line_if_fits(IC_SAVE "  Save BMP");
        if (ImGui::Button(IC_SAVE "  Save BMP")) {
            char name[96];
            snprintf(name, sizeof name, "%s_%04x_%04x.bmp", m.name.substr(0, m.name.find('.')).c_str(), tp, cl);
            save_bmp(g_exe_dir + name, texPixels, texW, texH);
            note(NOTE_OK, std::string("Saved ") + g_exe_dir + name);
        }
        if (char_on() && tpage_depth(tp) < 2) {
            heading("Palette");
            int n = tpage_depth(tp) == 0 ? 16 : 256, base = clut_y(cl) * 1024 + clut_x(cl);
            static int pick = -1;
            static float rgb[3];
            bool openPal = false;
            for (int i = 0; i < n; i++) {
                uint16_t c = m.vram[base + i];
                ImGui::PushID(i);
                if (i % 16) ImGui::SameLine(0, 2 * dpi);
                ImVec4 col((c & 31) / 31.0f, (c >> 5 & 31) / 31.0f, (c >> 10 & 31) / 31.0f, 1);
                if (ImGui::ColorButton("##c", col, ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoTooltip, ImVec2(20 * dpi, 20 * dpi))) {
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
        heading("Legend");
        auto swatch = [](ImU32 c, const char *text) {
            ImVec2 p = ImGui::GetCursorScreenPos();
            float y = p.y + ImGui::GetTextLineHeight() / 2, w = ImGui::GetFontSize() * 2;
            ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, y), ImVec2(p.x + w, y), c, 2);
            ImGui::Dummy(ImVec2(w, ImGui::GetTextLineHeight()));
            ImGui::SameLine();
            ImGui::TextUnformatted(text);
        };
        swatch(IM_COL32(80, 255, 120, 200), "a polygon's UVs");
        swatch(IM_COL32(255, 220, 60, 255), "the selected bone's");
        swatch(IM_COL32(140, 205, 255, 255), "under the mouse");
        hint("Click a polygon in the view to select its bone; the Model section shows it.");
        heading("Selected bone");
        if (scene.selected > 0) {
            ImGui::PushFont(g_fontBold);
            ImGui::TextColored(C_SELECT, "%s", thing_name(scene.selected).c_str());
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::TextDisabled("%d polygons", thing_polys(scene.selected));
        } else {
            hint("None");
        }
    };

