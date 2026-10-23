    /* --- the props' catalog: every picture and model on offer as a thumbnail; pick one and click in the stage --- */
    auto pcat_free = [&]() {
        for (PCat &c : pcat)
            if (c.gl) GL(glDeleteTextures)(1, &c.gl);
        pcat.clear(), pcatArm = -1;
    };
    auto pcat_root = [&]() { return mods_root() + "/prop-catalog"; };
    auto pcat_build = [&]() {
        pcat_free();
        pcatStage = stageId;
        auto lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            return s;
        };
        auto scan = [&](const std::string &dir, const char *pattern, const std::string &fixed) {
            int n = 0;
            char **names = SDL_GlobDirectory(dir.c_str(), pattern, SDL_GLOB_CASEINSENSITIVE, &n);
            std::vector<std::string> sorted(names, names + (names ? n : 0));
            SDL_free(names);
            std::sort(sorted.begin(), sorted.end());
            std::set<std::string> used; /* a model's picture is its own, not an item */
            for (const std::string &rel : sorted)
                if (lower(rel).size() > 4 && lower(rel).substr(lower(rel).size() - 4) == ".obj") {
                    std::string t = obj_texture(dir + "/" + rel);
                    if (!t.empty()) used.insert(lower(rel.substr(0, rel.find_last_of("/") + 1) + t));
                }
            for (const std::string &rel : sorted) {
                std::string l = lower(rel), ext = l.size() > 4 ? l.substr(l.size() - 4) : "";
                if (ext != ".png" && ext != ".obj") continue;
                if (used.count(l)) continue;
                PCat c;
                size_t sl = rel.find_last_of("/");
                c.name = sl == std::string::npos ? rel : rel.substr(sl + 1);
                c.cat = !fixed.empty() ? fixed : sl == std::string::npos ? "Other" : rel.substr(0, sl);
                c.path = dir + "/" + rel;
                std::string lc = lower(c.cat);
                c.kind = ext == ".obj" ? 2 : lc == "falling" || lc == "particles" ? 1 : 0;
                if (c.kind == 2) {
                    std::string t = obj_texture(c.path);
                    if (!t.empty()) c.tex = c.path.substr(0, c.path.find_last_of("/\\") + 1) + t;
                } else {
                    c.tex = c.path;
                    if (!fixed.empty()) /* what this stage already uses as particles */
                        for (const StageProp &p : props)
                            if (p.emitter && p.image == c.name) c.kind = 1;
                }
                pcat.push_back(c);
            }
        };
        scan(pcat_root(), "*", "");
        scan(pcat_root(), "*/*", "");
        if (stage_on()) scan(props_dir(stageId), "*", "This stage");
    };
    auto pcat_thumb = [&](PCat &c) { /* its picture as a texture, read the first time it is shown */
        c.tried = true;
        if (c.tex.empty()) return;
        SDL_Surface *png = SDL_LoadPNG(c.tex.c_str());
        SDL_Surface *rgba = png ? SDL_ConvertSurface(png, SDL_PIXELFORMAT_RGBA32) : nullptr;
        if (png) SDL_DestroySurface(png);
        if (!rgba) return;
        GL(glGenTextures)(1, &c.gl);
        GL(glBindTexture)(GL_TEXTURE_2D, c.gl);
        GL(glPixelStorei)(GL_UNPACK_ALIGNMENT, 4);
        GL(glPixelStorei)(0x0CF2 /* GL_UNPACK_ROW_LENGTH */, rgba->pitch / 4);
        GL(glTexImage2D)(GL_TEXTURE_2D, 0, GL_RGBA8, rgba->w, rgba->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
        GL(glPixelStorei)(0x0CF2, 0);
        GL(glGenerateMipmap)(GL_TEXTURE_2D);
        GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        c.w = (float)rgba->w, c.h = (float)rgba->h;
        SDL_DestroySurface(rgba);
    };
    auto pcat_window = [&]() {
        if (!pcatOpen) return;
        if (pcatStage != stageId) pcat_build();
        ImVec2 ds = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowSize(ImVec2(std::min(ds.x - 40 * dpi, 640 * dpi), std::min(ds.y - 60 * dpi, 560 * dpi)), ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (!ImGui::Begin(IC_STAGE "  Props catalog###pcat", &pcatOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::End();
            return;
        }
        if (!pcatOpen) pcatArm = -1;
        auto lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            return s;
        };
        std::vector<std::string> tabs;
        for (const PCat &c : pcat)
            if (std::find(tabs.begin(), tabs.end(), c.cat) == tabs.end()) tabs.push_back(c.cat);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.4f);
        ImGui::InputTextWithHint("##pcfind", IC_SEARCH " Search", pcatFind, sizeof pcatFind);
        ImGui::SameLine();
        if (ImGui::Button(IC_LOOP "  Refresh")) pcat_build();
        tip("Read the folders again");
        ImGui::SameLine();
        if (ImGui::Button(IC_FOLDER "  Open folder")) {
            SDL_CreateDirectory(pcat_root().c_str());
            open_folder(pcat_root());
        }
        tip("The catalog's folder: a PNG in a subfolder is a billboard, in Falling a particle, an OBJ (with its PNG) a model. Add your own and press Refresh.");
        if (ImGui::BeginTabBar("##pctabs")) {
            if (ImGui::BeginTabItem("All")) pcatTab = "", ImGui::EndTabItem();
            for (const std::string &t : tabs)
                if (ImGui::BeginTabItem(t.c_str())) pcatTab = t, ImGui::EndTabItem();
            ImGui::EndTabBar();
        }
        float pic = 96 * dpi, lineH = ImGui::GetTextLineHeight();
        const ImGuiStyle &sty = ImGui::GetStyle();
        ImVec2 cell(pic + 12 * dpi, pic + lineH + 14 * dpi);
        float foot = ImGui::GetTextLineHeightWithSpacing() * 2 + sty.ItemSpacing.y * 2 + (stage_on() ? ImGui::GetFrameHeightWithSpacing() * ((brushOn ? 3 : 1) + (brushOn && brushMixOn ? 1 : 0)) : 0);
        ImGui::BeginChild("##pcgrid", ImVec2(0, -foot), ImGuiChildFlags_Borders);
        int cols = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (cell.x + sty.ItemSpacing.x))), n = 0, loaded = 0;
        std::string q = lower(pcatFind);
        double now = ImGui::GetTime();
        for (int i = 0; i < (int)pcat.size(); i++) {
            PCat &c = pcat[i];
            if (!pcatTab.empty() && c.cat != pcatTab) continue;
            if (!q.empty() && lower(c.name + " " + c.cat).find(q) == std::string::npos) continue;
            if (n++ % cols) ImGui::SameLine();
            if (!c.tried && loaded < 6) pcat_thumb(c), loaded++;
            ImGui::PushID(i);
            ImVec2 p = ImGui::GetCursorScreenPos();
            bool hov;
            bool mixing = brushOn && brushMixOn && c.kind == 0, ticked = mixing && brushMix.count(c.path);
            if (ImGui::Selectable("##i", mixing ? ticked : pcatArm == i, 0, cell)) {
                if (mixing) { /* ticks it in or out of the mix; the last one ticked is the one whose weight is edited */
                    if (brushMix.erase(c.path) == 0) brushMix[c.path] = 1, pcatArm = i, propSel = -1, select_part(-1);
                } else {
                    pcatArm = pcatArm == i ? -1 : i, propSel = -1;
                    if (pcatArm >= 0) select_part(-1);
                }
            }
            hov = ImGui::IsItemHovered();
            ImDrawList *dl = ImGui::GetWindowDrawList();
            ImVec2 a = p + ImVec2(6 * dpi, 4 * dpi), b = a + ImVec2(pic, pic);
            dl->AddRectFilled(a, b, IM_COL32(40, 44, 52, 255));
            dl->PushClipRect(a, b, true);
            if (c.gl) {
                float k = std::min((pic - 8 * dpi) / c.w, (pic - 8 * dpi) / c.h);
                ImVec2 mid = (a + b) * 0.5f, hs(c.w * k * 0.5f, c.h * k * 0.5f);
                ImTextureID tx = (ImTextureID)(intptr_t)c.gl;
                if (hov && c.kind == 1) { /* it falls: a handful of them turning as they go down */
                    for (int s = 0; s < 8; s++) {
                        float ph = (float)fmod(now * 0.45 + s * 0.173, 1.0), x = a.x + (0.1f + 0.8f * (s * 0.37f - floorf(s * 0.37f))) * pic + sinf((float)now * 3 + s) * 7 * dpi;
                        float y = a.y + ph * (pic + 20 * dpi) - 10 * dpi, ang = (float)now * 2 + s, r = pic * 0.12f, ca = cosf(ang) * r, sa = sinf(ang) * r;
                        float fl = 0.35f + 0.65f * fabsf(cosf((float)now * 3.6f + s * 2));
                        ImVec2 m(x, y);
                        dl->AddImageQuad(tx, m + ImVec2(-ca * fl + sa, -sa * fl - ca), m + ImVec2(ca * fl + sa, sa * fl - ca), m + ImVec2(ca * fl - sa, sa * fl + ca),
                                         m + ImVec2(-ca * fl - sa, -sa * fl + ca));
                    }
                } else if (hov && c.kind == 0) { /* it sways: the top goes with the wind, the foot stays */
                    float sw = sinf((float)now * 3) * hs.x * 0.35f;
                    dl->AddImageQuad(tx, ImVec2(mid.x - hs.x + sw, mid.y - hs.y), ImVec2(mid.x + hs.x + sw, mid.y - hs.y), mid + hs, ImVec2(mid.x - hs.x, mid.y + hs.y));
                } else {
                    dl->AddImage(tx, mid - hs, mid + hs);
                }
            } else {
                dl->AddText(a + ImVec2(8 * dpi, pic / 2 - lineH / 2), IM_COL32(130, 135, 145, 255), c.kind == 2 ? "no texture" : "can't read");
            }
            dl->PopClipRect();
            const char *badge = c.kind == 2 ? "3D" : c.kind == 1 ? "falls" : "";
            if (*badge) dl->AddText(a + ImVec2(4 * dpi, 2 * dpi), IM_COL32(255, 217, 51, 255), badge);
            if (ticked) {
                dl->AddRect(a, b, IM_COL32(110, 220, 120, 255), 0, 0, 2 * dpi);
                char wt[16];
                snprintf(wt, sizeof wt, "x%.1f", brushMix[c.path]);
                dl->AddText(ImVec2(b.x - ImGui::CalcTextSize(wt).x - 4 * dpi, a.y + 2 * dpi), IM_COL32(110, 220, 120, 255), wt);
            } else if (!mixing && pcatArm == i) {
                dl->AddRect(a, b, IM_COL32(255, 217, 51, 255), 0, 0, 2 * dpi);
            }
            ImGui::PushClipRect(a, p + ImVec2(cell.x - 2 * dpi, cell.y), true);
            dl->AddText(ImVec2(a.x, b.y + 3 * dpi), ImGui::GetColorU32(ImGuiCol_Text), c.name.c_str());
            ImGui::PopClipRect();
            if (hov) {
                ImGui::BeginTooltip();
                ImGui::Text("%s", c.name.c_str());
                ImGui::TextDisabled("%s: %s", c.cat.c_str(), c.kind == 2 ? "a 3D model" : c.kind == 1 ? "particles falling through an area" : "a picture standing in the stage");
                ImGui::EndTooltip();
            }
            ImGui::PopID();
        }
        if (!n) hint("%s", pcat.empty() ? "Nothing here yet: Open folder, add PNGs or OBJs in subfolders, and Refresh." : "Nothing matches.");
        ImGui::EndChild();
        if (stage_on()) {
            ImGui::Checkbox("Brush", &brushOn);
            tip("Drag on the ground to scatter pictures: grass fields, flower beds. Ctrl+drag erases them.");
            if (brushOn) {
                ImGui::SameLine();
                ImGui::Checkbox("Mix", &brushMixOn);
                tip("Paint with several pictures at once for variety: tick the ones to use in the grid. Off: only the picked one.");
                float w = ImGui::GetContentRegionAvail().x / 3 - 8 * dpi;
                ImGui::SetNextItemWidth(w);
                ImGui::DragFloat("##br", &brushR, 10, 50, 5000, "Radius %.0f");
                tip("How far around the mouse, in the game's units");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(w);
                ImGui::DragInt("##bn", &brushN, 0.1f, 1, 30, "%d a dab");
                tip("How many it drops each time; a dab comes every half radius you drag");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(w);
                ImGui::DragFloat("##bs", &brushSway, 0.01f, 0, 3, "Sway %.2f");
                ImGui::SetNextItemWidth(w * 2 + 8 * dpi);
                ImGui::DragFloatRange2("##bz", &brushScale[0], &brushScale[1], 0.01f, 0.1f, 5, "Size %.2f", "to %.2f");
                tip("Each gets a size between these, of the 300-tall picture");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(w);
                ImGui::DragFloat("##bv", &brushVar, 0.01f, 0, 0.9f, "Shade %.2f");
                tip("How much darker some come out, so the field isn't flat");
                if (brushMixOn) {
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Mixing %d", mix_pool());
                    ImGui::SameLine();
                    if (pcatArm >= 0 && pcatArm < (int)pcat.size() && brushMix.count(pcat[pcatArm].path)) {
                        ImGui::SetNextItemWidth(90 * dpi);
                        ImGui::DragFloat("##bw", &brushMix[pcat[pcatArm].path], 0.05f, 0.1f, 20, "Weight %.1f");
                        tip("How often this one comes up next to the others: 2 is twice as often as 1");
                        ImGui::SameLine();
                    }
                    ImGui::Checkbox("No repeats", &brushNoRepeat);
                    tip("A picture never comes right after itself, so there are no patches of one kind");
                    ImGui::SameLine();
                    if (ImGui::Button("Tick this tab")) {
                        for (const PCat &c : pcat)
                            if (c.kind == 0 && (pcatTab.empty() || c.cat == pcatTab) && !brushMix.count(c.path)) brushMix[c.path] = 1;
                    }
                    tip("Adds every picture shown under this tab (not the particles or models)");
                    ImGui::SameLine();
                    if (ImGui::Button("None")) brushMix.clear();
                } else {
                    ImGui::TextDisabled("Drag on the ground. Ctrl+drag erases. One Ctrl+Z takes back a stroke.");
                }
            }
        }
        if (!stage_on()) ImGui::TextDisabled("Open a stage first.");
        else if (brushOn && !brush_ready()) ImGui::TextDisabled(brushMixOn ? "Tick the pictures to mix in the grid above." : "Pick a picture to paint with.");
        else if (brushOn) ImGui::TextDisabled("Drag on the ground. Ctrl+drag erases. One Ctrl+Z takes back a stroke.");
        else if (pcatArm >= 0) ImGui::TextColored(C_SELECT, "Click in the stage to place it. Shift keeps placing; Esc or right-click stops.");
        else ImGui::TextDisabled("Pick one, then click in the stage where it goes.");
        ImGui::End();
    };
