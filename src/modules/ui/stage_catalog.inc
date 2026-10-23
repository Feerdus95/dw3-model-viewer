    /* --- the catalog: every part of the 57 stages as a picture, looks that repeat listed once ---
       Read all at once when it first opens; each stage's pictures are drawn the first time one of its parts
       shows (one stage a frame), by the scene's own renderer into a small texture. */
    struct CatPart {
        int stage = 0, part = 0, polys = 0;
        std::string name;
        std::vector<int> also; /* the other stages with the same look */
        GLuint tex = 0;
    };
    std::vector<CatPart> cat;
    bool catOpen = false, catBuilt = false;
    char catFind[64] = "";
    int catStage = -1, catPick = -1; /* the stage the list is limited to; the picked part */
    std::set<int> catDone;           /* the stages whose pictures are made */
    GLuint catFbo = 0, catDepth = 0;
    const int CAT_T = 128;
    auto cat_free = [&]() {
        for (CatPart &c : cat)
            if (c.tex) GL(glDeleteTextures)(1, &c.tex);
        cat.clear(), catDone.clear();
        catBuilt = false, catPick = -1;
    };
    auto cat_build = [&]() {
        cat_free();
        catBuilt = true;
        auto fnv = [](uint64_t h, const void *p, size_t n) {
            const uint8_t *b = (const uint8_t *)p;
            for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ull;
            return h;
        };
        const uint64_t BASIS = 14695981039346656037ull;
        std::map<uint64_t, int> seen;
        for (const StageInfo &o : stageInfos) {
            StageEdit e;
            StageInfo oi;
            std::vector<uint8_t> ob;
            if (!stage_load(o.id, e, oi, ob)) continue;
            std::vector<uint64_t> ih;
            for (const StageImage &im : e.images) ih.push_back(fnv(BASIS, im.px.data(), im.px.size() * 4));
            for (int k = 0; k < (int)e.parts.size(); k++) {
                const StagePart &p = e.parts[k];
                if (p.faces.empty() || p.verts.empty()) continue;
                /* the shape (wherever it stands), what its faces use of it, and the pictures they read */
                float lo[3] = {1e30f, 1e30f, 1e30f};
                for (const auto &v : p.verts)
                    for (int i = 0; i < 3; i++) lo[i] = std::min(lo[i], v[i]);
                uint64_t h = BASIS;
                for (const auto &v : p.verts)
                    for (int i = 0; i < 3; i++) {
                        int q = (int)lroundf(v[i] - lo[i]);
                        h = fnv(h, &q, 4);
                    }
                std::set<int> imgs;
                for (const StageFace &f : p.faces) {
                    h = fnv(h, f.v, sizeof f.v), h = fnv(h, &f.abr, 4);
                    imgs.insert(f.image);
                }
                for (int i : imgs)
                    if (i >= 0 && i < (int)ih.size()) h = fnv(h, &ih[i], 8);
                auto it = seen.find(h);
                if (it != seen.end()) {
                    cat[it->second].also.push_back(o.id);
                    continue;
                }
                seen[h] = (int)cat.size();
                CatPart c;
                c.stage = o.id, c.part = k, c.polys = (int)p.faces.size(), c.name = p.name;
                cat.push_back(c);
            }
        }
    };
    /* all the pictures of stage ID: its model on the card with one part showing at a time */
    auto cat_pictures = [&](int id) {
        catDone.insert(id);
        StageEdit e;
        StageInfo oi;
        std::vector<uint8_t> bytes;
        if (!stage_load(id, e, oi, bytes)) return;
        Scene sc;
        sc.model = std::make_unique<Model>();
        if (!sc.model->load(fid_name(oi.fid), bytes, nullptr, oi.boneEntry)) return;
        upload(sc);
        if (!catFbo) {
            GL(glGenFramebuffers)(1, &catFbo);
            GL(glGenRenderbuffers)(1, &catDepth);
            GL(glBindRenderbuffer)(GL_RENDERBUFFER, catDepth);
            GL(glRenderbufferStorage)(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, CAT_T, CAT_T);
            GL(glBindFramebuffer)(GL_FRAMEBUFFER, catFbo);
            GL(glFramebufferRenderbuffer)(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, catDepth);
        }
        View v;
        v.grid = false, v.lighting = 2, v.stage = id;
        for (CatPart &c : cat) {
            int b = c.part + 1;
            if (c.stage != id || b >= (int)sc.model->bones.size()) continue;
            std::fill(sc.hidden.begin(), sc.hidden.end(), true);
            sc.hidden[b] = false;
            if (!frame_bones(sc, v, {b})) continue;
            v.cam.yaw = 0.6f, v.cam.pitch = 0.35f;
            GL(glGenTextures)(1, &c.tex);
            GL(glBindTexture)(GL_TEXTURE_2D, c.tex);
            GL(glTexImage2D)(GL_TEXTURE_2D, 0, GL_RGBA8, CAT_T, CAT_T, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            GL(glBindFramebuffer)(GL_FRAMEBUFFER, catFbo);
            GL(glFramebufferTexture2D)(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c.tex, 0);
            render(sc, v, 0, 0, CAT_T, CAT_T);
        }
        GL(glBindFramebuffer)(GL_FRAMEBUFFER, 0);
        GL(glDeleteBuffers)(1, &sc.vbo);
        GL(glDeleteVertexArrays)(1, &sc.vao);
        GL(glDeleteTextures)(1, &sc.vram);
    };
    auto cat_add = [&](int i) {
        if (!stage_on() || i < 0 || i >= (int)cat.size()) return;
        StageInfo oi;
        std::vector<uint8_t> ob;
        if (!stage_load(cat[i].stage, stageFrom, oi, ob)) return;
        char pre[8];
        snprintf(pre, sizeof pre, "s%02d_", cat[i].stage);
        stage_mark();
        stageEdit.append(stageFrom, cat[i].part, pre);
        stagePart = (int)stageEdit.parts.size() - 1;
        if (stage_rebuild()) note(NOTE_OK, "Added " + part_name(stagePart));
        scrollToSel = true;
    };
    auto catalog_window = [&]() {
        if (!catOpen) return;
        ImVec2 ds = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowSize(ImVec2(std::min(ds.x - 40 * dpi, 900 * dpi), std::min(ds.y - 60 * dpi, 680 * dpi)), ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (!ImGui::Begin(IC_STAGE "  Parts of the stages###catalog", &catOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::End();
            return;
        }
        if (!catBuilt) cat_build();
        auto lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            return s;
        };
        /* the search and the stage it is limited to */
        float cw = ImGui::GetContentRegionAvail().x;
        ImGui::SetNextItemWidth(cw * 0.55f);
        ImGui::InputTextWithHint("##catfind", IC_SEARCH " Search a part, a stage or what it shows", catFind, sizeof catFind);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        std::string lim = catStage < 0 ? "All stages" : std::to_string(catStage) + "  " + stage_look(catStage);
        if (ImGui::BeginCombo("##catstage", lim.c_str())) {
            if (ImGui::Selectable("All stages", catStage < 0)) catStage = -1;
            for (const StageInfo &o : stageInfos) {
                std::string l = std::to_string(o.id) + "  " + stage_look(o.id);
                if (ImGui::Selectable(l.c_str(), catStage == o.id)) catStage = o.id;
            }
            ImGui::EndCombo();
        }
        std::vector<int> shown;
        std::string q = lower(catFind);
        for (int i = 0; i < (int)cat.size(); i++) {
            const CatPart &c = cat[i];
            if (catStage >= 0 && c.stage != catStage && std::find(c.also.begin(), c.also.end(), catStage) == c.also.end()) continue;
            if (!q.empty() && lower(c.name + " " + std::to_string(c.stage) + " " + stage_look(c.stage)).find(q) == std::string::npos) continue;
            shown.push_back(i);
        }
        /* the pictures */
        const ImGuiStyle &sty = ImGui::GetStyle();
        float pic = 104 * dpi, lineH = ImGui::GetTextLineHeight();
        ImVec2 cell(pic + 12 * dpi, pic + lineH * 2 + 14 * dpi);
        float foot = ImGui::GetFrameHeightWithSpacing() * 2 + lineH * 2 + sty.ItemSpacing.y * 3;
        ImGui::BeginChild("##catgrid", ImVec2(0, -foot), ImGuiChildFlags_Borders);
        int cols = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (cell.x + sty.ItemSpacing.x)));
        int rows = ((int)shown.size() + cols - 1) / cols, want = -1, addNow = -1;
        /* the free width is split to both sides, so the grid sits in the middle */
        float rowOff = std::max(0.0f, (ImGui::GetContentRegionAvail().x - (cols * (cell.x + sty.ItemSpacing.x) - sty.ItemSpacing.x)) / 2);
        ImGuiListClipper clip;
        clip.Begin(rows, cell.y + sty.ItemSpacing.y);
        while (clip.Step()) {
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++) {
                for (int c = 0; c < cols; c++) {
                    int n = r * cols + c;
                    if (n >= (int)shown.size()) break;
                    int i = shown[n];
                    const CatPart &cp = cat[i];
                    ImGui::PushID(i);
                    if (c) ImGui::SameLine();
                    else ImGui::SetCursorPosX(ImGui::GetCursorPosX() + rowOff);
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    if (ImGui::Selectable("##c", catPick == i, 0, cell)) catPick = i;
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) addNow = i;
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    ImVec2 a = p + ImVec2(6 * dpi, 4 * dpi), b = a + ImVec2(pic, pic);
                    if (cp.tex) {
                        dl->AddImage((ImTextureID)(intptr_t)cp.tex, a, b, ImVec2(0, 1), ImVec2(1, 0));
                    } else {
                        dl->AddRectFilled(a, b, IM_COL32(40, 44, 52, 255));
                        if (!catDone.count(cp.stage)) want = want < 0 ? cp.stage : want;
                        else dl->AddText(a + ImVec2(8 * dpi, pic / 2 - lineH / 2), IM_COL32(130, 135, 145, 255), "no picture");
                    }
                    ImGui::PushClipRect(a - ImVec2(0, 0), p + ImVec2(cell.x - 2 * dpi, cell.y), true);
                    dl->AddText(ImVec2(a.x, b.y + 3 * dpi), ImGui::GetColorU32(ImGuiCol_Text), cp.name.c_str());
                    char sub[48];
                    snprintf(sub, sizeof sub, "stage %d, %d polys", cp.stage, cp.polys);
                    dl->AddText(ImVec2(a.x, b.y + 3 * dpi + lineH), ImGui::GetColorU32(ImGuiCol_TextDisabled), sub);
                    ImGui::PopClipRect();
                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::Text("%s", cp.name.c_str());
                        ImGui::TextDisabled("Stage %d: %s", cp.stage, stage_look(cp.stage));
                        ImGui::TextDisabled("%d polygons. Double-click adds it.", cp.polys);
                        ImGui::EndTooltip();
                    }
                    ImGui::PopID();
                }
            }
        }
        if (shown.empty()) hint("%s", cat.empty() ? "No stage could be read." : "No part matches.");
        ImGui::EndChild();
        if (want >= 0) cat_pictures(want); /* a stage a frame */
        /* what's picked */
        if (catPick >= 0 && catPick < (int)cat.size()) {
            const CatPart &c = cat[catPick];
            std::string also;
            for (size_t k = 0; k < c.also.size() && k < 12; k++) also += (k ? ", " : "") + std::to_string(c.also[k]);
            if (c.also.size() > 12) also += ", ...";
            ImGui::PushFont(g_fontBold);
            ImGui::TextColored(C_SELECT, "%s", c.name.c_str());
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::TextDisabled("%d polygons, stage %d: %s", c.polys, c.stage, stage_look(c.stage));
            if (!also.empty()) hint("The same look is in stage %s too.", also.c_str());
            else hint("Only in this stage.");
        } else {
            hint("Pick a part: a click shows where it is from, a double-click adds it to the open stage.");
            ImGui::Dummy(ImVec2(0, lineH));
        }
        ImGui::BeginDisabled(catPick < 0 || !stage_on());
        if (button(IC_ADD "  Add to the stage", 1)) addNow = catPick;
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (stage_on()) ImGui::TextDisabled("It joins stage %d where it stood in its own; move it with the gizmo.", stageId);
        else ImGui::TextDisabled("Open a stage first.");
        if (addNow >= 0) cat_add(addNow);
        ImGui::End();
    };
