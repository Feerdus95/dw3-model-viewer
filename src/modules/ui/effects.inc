    /* ===== Effects ===== */
    /* the port's own partners (its native.h): no fighter of the disc's has their ids */
    static const std::pair<int, std::string> PORT_DIGIMON[] = {{471, "BladeGarurumon"}, {472, "Flamedramon"}};
    auto effect_row = [&](int id) {
        const std::set<std::string> &u = fxUsers[id], &t = fxTechs[id];
        std::string l = "effect " + std::to_string(id);
        if (!t.empty()) l += "  " + joined(t);
        else if (u.size() > 1) l += "  (" + std::to_string(u.size()) + " Digimon)";
        else if (u.empty()) l += "  (the battle's own)";
        ImGui::PushID(id);
        float ownW = ownIds.count(id) ? ImGui::CalcTextSize("mod").x + ImGui::GetStyle().ItemSpacing.x : 0; /* its "mod" mark has the row's end to itself */
        if (ImGui::Selectable(l.c_str(), fx.id == id, ImGuiSelectableFlags_AllowOverlap, ImVec2(ImGui::GetContentRegionAvail().x - ownW, 0)) && fx.id != id) pick_effect(id, false);
        if (u.size() > 1 || (!t.empty() && !u.empty())) tip("%s", joined(u).c_str());
        if (ImGui::BeginPopupContextItem("##menu")) {
            menu_title("effect " + std::to_string(id), "%s", t.empty() ? joined(u).c_str() : joined(t).c_str());
            if (ImGui::MenuItem(IC_OPEN "  Open")) pick_effect(id, false);
            if (ImGui::MenuItem(IC_COPY "  Copy \"effect N\"")) ImGui::SetClipboardText(("effect " + std::to_string(id)).c_str());
            ImGui::EndPopup();
        }
        if (ownIds.count(id)) {
            ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize("mod").x);
            ImGui::TextColored(C_ACCENT, "mod");
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
            snprintf(modTab, sizeof modTab, "Mods (%d)###mod", (int)ownIds.size());
            if (ImGui::BeginTabItem(modTab)) {
                ImGui::BeginChild("fxmods");
                if (ownIds.empty()) hint("No effect has a mod in %s yet. Open an effect and drop its PNGs on the window to make one.", ownRoot.c_str());
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
        if (ImGui::CollapsingHeader("Mod (native port)", ImGuiTreeNodeFlags_DefaultOpen)) {
            hint("PNGs the native port draws instead of this effect. Pick who it's for, then drop the PNGs (or a folder) on the window.");
            int level = ownLevel, crit = ownCritical;
            if (segmented("level", &level, {"Everyone", "Rookie", "Champion", "Ultimate", "Mega"})) reload_own(ownCritical, level);
            if (ownLevel == 0) {
                hint("Plays for every enemy, and for any partner with no mod for its level.");
            } else {
                std::set<std::string> who;
                for (const Fighter &f : g_game.fighters) {
                    auto l = ownLevels.find(f.id);
                    if (!f.enemy && !f.converted && l != ownLevels.end() && l->second == ownLevel) who.insert(f.name);
                }
                for (const auto &it : PORT_DIGIMON) {
                    auto l = ownLevels.find(it.first);
                    if (l != ownLevels.end() && l->second == ownLevel) who.insert(it.second);
                }
                if (who.empty()) ImGui::TextColored(C_WARN, IC_WARN "  No Digimon has this level yet: set them in Digimon levels, below.");
                else hint("Plays for %s, and for the levels above with no mod of their own.", some(who).c_str()), tip("%s", joined(who).c_str());
            }
            if (segmented("kind", &crit, {"Hit", "Critical hit"})) reload_own(crit != 0, ownLevel);
            same_line_if_width(ImGui::CalcTextSize(IC_WARN " Unsaved").x + 16 * dpi);
            if (ownDirty) chip(IC_WARN " Unsaved", C_WARN);
            else if (own.frames.empty() && own.sound.empty()) chip("None yet", C_DIM);
            else if (!own.active) chip("Off", C_DIM);
            else chip(own.hasJson ? "Saved" : "PNGs only", C_ACCENT);
            if (own.frames.empty()) hint("Nothing here yet: these Digimon play %s, which the view shows.", ownFallback.c_str());
            if (check_box("Show the mod in the view", &ownShow)) fxTime = 0, fxDrawn = -1;
            tip("Off, the view shows the original effect");
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
            tip("Copies them into the folder below and adds them as frames, in their names' order (2 before 10), and saves. "
                "An image is the 320x240 screen around the effect's point, at any size of that shape.");
            same_line_if_fits(IC_FOLDER "  Import a folder...");
            if (ImGui::Button(IC_FOLDER "  Import a folder...")) {
                SDL_ShowOpenFolderDialog(
                    [](void *u, const char *const *list, int) {
                        if (!list || !*list) return;
                        Picked *p = (Picked *)u;
                        std::vector<std::string> pngs = pngs_at(*list);
                        std::lock_guard<std::mutex> lock(p->m);
                        p->files.insert(p->files.end(), pngs.begin(), pngs.end());
                    },
                    &picked, win, nullptr, false);
            }
            tip("All the PNGs in a folder, as Import PNGs does. Dropping the folder on the window does the same");
            same_line_if_fits(IC_SAVE "  Save");
            ImGui::BeginDisabled(own.frames.empty() && own.sound.empty());
            if (button(IC_SAVE "  Save", ownDirty ? 1 : 0)) own_save();
            ImGui::EndDisabled();
            tip("Writes effect.json: the frames, their ticks, the placement and the sound (Ctrl+S)");
            same_line_if_fits(IC_RESTART "  Reload");
            if (ImGui::Button(IC_RESTART "  Reload")) reload_own(ownCritical, ownLevel);
            tip("Reads the folder again");
            hint("Folder: %s", nice_path(own.dir).c_str());
            if (ImGui::SmallButton(IC_FOLDER " Open it")) SDL_CreateDirectory(own.dir.c_str()), open_folder(own.dir);
            same_line_if_fits("Change the mods folder...");
            if (ImGui::SmallButton("Change the mods folder...")) pick_mods_folder();
            tip("The native port's data/mods/effects folder");
            if (!own.images.empty() || !own.sound.empty()) {
                same_line_if_fits(IC_DELETE " Delete this mod");
                if (ImGui::SmallButton(IC_DELETE " Delete this mod")) own_delete();
                tip("Deletes this folder's PNGs, sound and effect.json");
            }
            {
                std::string wav = take_picked(picked, &Picked::wav);
                if (!wav.empty()) {
                    if (own.import_sound(wav)) changed = true;
                }
                ImGui::SeparatorText("Hit sound");
                if (own.sound.empty()) hint("No sound: the game's hit sound plays. A WAV here plays in its place.");
                else ImGui::TextUnformatted(own.sound.c_str());
                {
                    /* the game's own hit sounds of the techniques that start this effect, to hear against the mod's */
                    std::map<int, std::string> orig;
                    for (const TechRow &t : g_techs)
                        if (t.effect == fx.id && t.sound > 0 && t.sound < 0x5A) {
                            std::string &who = orig[t.sound];
                            if (who.size() < 120) who += (who.empty() ? "" : ", ") + t.name;
                        }
                    static std::future<std::string> job;
                    static int jobIndex = -1;
                    std::string exe = ownRoot + "/../../../dw2003.exe", cache = g_exe_dir + "original_sounds";
                    if (!SDL_GetPathInfo(exe.c_str(), nullptr)) exe = ownRoot + "/../../../build/dw2003.exe"; /* the port built from its sources */
                    if (job.valid() && job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                        std::string wav = job.get();
                        if (wav.empty()) note(NOTE_ERROR, "Couldn't render the game's sound " + std::to_string(jobIndex) + " (is dw2003.exe at " + nice_path(exe) + "?)");
                        else sound_preview(wav, 1);
                        jobIndex = -1;
                    }
                    if (orig.empty()) hint("No technique starts this effect with a hit sound, so there is no original to play.");
                    for (const auto &o : orig) {
                        char label[96];
                        snprintf(label, sizeof label, IC_PLAY "  Original %d##orig%d", o.first, o.first);
                        ImGui::BeginDisabled(job.valid());
                        if (ImGui::Button(label)) job = std::async(std::launch::async, original_sound, exe, cache, jobIndex = o.first);
                        ImGui::EndDisabled();
                        tip("The game's own hit sound %d, from %s. The first time it is rendered by the port (a couple of seconds)", o.first, o.second.c_str());
                        same_line_if_fits(label);
                    }
                    ImGui::NewLine();
                    if (job.valid()) hint("Rendering the original...");
                }
                if (ImGui::Button(IC_IMPORT "  Import WAV...")) {
                    static const SDL_DialogFileFilter WAV = {"WAV sounds", "wav"};
                    SDL_ShowOpenFileDialog(picked_cb<&Picked::wav>, &picked, win, &WAV, 1, nullptr, false);
                }
                tip("Copies the WAV into the folder below as this mod's hit sound. The game plays it when the effect starts, "
                    "and leaves its own hit sound out (switch it off in game from Options > Mods > Custom sounds). Save to apply");
                if (!own.sound.empty()) {
                    same_line_if_fits(IC_PLAY "  Play");
                    if (ImGui::Button(IC_PLAY "  Play")) sound_preview(own.dir + "/" + own.sound, own.soundVolume);
                    same_line_if_fits(IC_DELETE "  Remove");
                    if (ImGui::Button(IC_DELETE "  Remove")) own.sound.clear(), changed = true;
                    tip("Takes the sound out of effect.json; the WAV stays in the folder");
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
                    changed |= ImGui::DragFloat("volume", &own.soundVolume, 0.01f, 0, 4, "%.2f");
                    tip("1 is the file as it is, up to 4");
                }
            }
            if (!own.frames.empty()) {
                if (check_box("Active", &own.active)) changed = true;
                tip("Off, the game leaves this mod out, as if it weren't there, and keeps its files. Save to apply");
                hint("%d frames, %d ticks = %.2f s (the original: %.2f s)", (int)own.frames.size(), own.duration(), own.duration() / 60.0f,
                     fx.duration / 60.0f);
                static char ticksText[256];
                bool applyTicks = ImGui::InputTextWithHint("##ticksall", "All the ticks, e.g. 2 2 3 3 4 4 4", ticksText, sizeof ticksText, ImGuiInputTextFlags_EnterReturnsTrue);
                tip("The ticks of every frame in order, as the artist gives them. Enter applies them");
                same_line_if_fits("Apply");
                applyTicks |= ImGui::Button("Apply");
                if (applyTicks && *ticksText) {
                    std::vector<int> ticks;
                    char *p = ticksText, *end;
                    for (p += strspn(p, " ,;"); *p; p = end + strspn(end, " ,;")) {
                        long v = strtol(p, &end, 10);
                        if (end == p) break;
                        ticks.push_back(std::clamp((int)v, 1, 600));
                    }
                    if (*p) note(NOTE_ERROR, "Only numbers, please: 2 2 3 3 4 4 4");
                    else if (ticks.size() != own.frames.size()) note(NOTE_ERROR, "That's " + std::to_string(ticks.size()) + " ticks for " + std::to_string(own.frames.size()) + " frames");
                    else {
                        for (size_t i = 0; i < ticks.size(); i++) own.frames[i].ticks = ticks[i];
                        changed = true, *ticksText = 0;
                    }
                }
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
            if (uiFx >= 0) ImGui::SetNextItemOpen(true, ImGuiCond_Once); /* --fx-open, for a --ui-shot */
            if (ImGui::TreeNode("Digimon levels")) {
                hint("Which mods each partner plays. Saved at once to stages.json; the game reads it when it starts.");
                struct LevelRow {
                    int id, level;
                    std::string name;
                };
                std::vector<LevelRow> all;
                int counts[5] = {};
                auto add = [&](int id, const std::string &name) {
                    auto l = ownLevels.find(id);
                    int v = l == ownLevels.end() ? 0 : l->second;
                    all.push_back({id, v, name}), counts[v]++;
                };
                for (const Fighter &f : g_game.fighters)
                    if (!f.enemy && !f.converted && f.name.size() > 1) add(f.id, f.name);
                for (const auto &it : PORT_DIGIMON) add(it.first, it.second + " (the port's)");
                static char levelFind[64] = "";
                static int levelShow = -1; /* -1 all of them, else that level (0 none yet) */
                static const char *const LEVEL_NAME[5] = {"No level", "Rookie", "Champion", "Ultimate", "Mega"};
                float comboW = ImGui::GetFontSize() * 9;
                float findW = ImGui::GetContentRegionAvail().x - comboW - ImGui::GetStyle().ItemSpacing.x;
                bool oneRow = findW >= comboW; /* else the two, each on its own row, as wide as the panel */
                ImGui::SetNextItemWidth(oneRow ? findW : -FLT_MIN);
                ImGui::InputTextWithHint("##levelfind", IC_SEARCH "  Search a Digimon", levelFind, sizeof levelFind);
                if (oneRow) ImGui::SameLine();
                ImGui::SetNextItemWidth(oneRow ? comboW : -FLT_MIN);
                auto showLabel = [&](int s) {
                    return s < 0 ? "All (" + std::to_string(all.size()) + ")" : std::string(LEVEL_NAME[s]) + " (" + std::to_string(counts[s]) + ")";
                };
                if (ImGui::BeginCombo("##levelshow", showLabel(levelShow).c_str())) {
                    for (int s = -1; s < 5; s++)
                        if (ImGui::Selectable(showLabel(s).c_str(), levelShow == s)) levelShow = s;
                    ImGui::EndCombo();
                }
                tip("Only the Digimon of a level. No level: those that still play the Everyone mod");
                auto lower = [](std::string s) {
                    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
                    return s;
                };
                std::string find = lower(levelFind);
                std::vector<LevelRow> rows;
                for (const LevelRow &r : all)
                    if ((levelShow < 0 || r.level == levelShow) && lower(r.name).find(find) != std::string::npos) rows.push_back(r);
                float lines = (float)std::min((int)rows.size() + 1, 14);
                ImVec2 size(0, lines * ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().CellPadding.y * 2);
                if (rows.empty()) hint("No Digimon matches.");
                else if (ImGui::BeginTable("levels", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY, size)) {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("Digimon", ImGuiTableColumnFlags_DefaultSort);
                    ImGui::TableSetupColumn("Level");
                    ImGui::TableHeadersRow();
                    const ImGuiTableColumnSortSpecs *sort = ImGui::TableGetSortSpecs()->Specs;
                    bool byLevel = sort && sort->ColumnIndex == 1, down = sort && sort->SortDirection == ImGuiSortDirection_Descending;
                    std::stable_sort(rows.begin(), rows.end(), [&](const LevelRow &a, const LevelRow &b) {
                        if (byLevel && a.level != b.level) return down ? a.level > b.level : a.level < b.level;
                        int c = a.name.compare(b.name);
                        return !byLevel && down ? c > 0 : c < 0;
                    });
                    for (const LevelRow &r : rows) {
                        ImGui::PushID(r.id);
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextUnformatted(r.name.c_str());
                        ImGui::TableNextColumn();
                        int v = r.level;
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        if (ImGui::Combo("##level", &v, "None\0Rookie\0Champion\0Ultimate\0Mega\0")) {
                            if (v) ownLevels[r.id] = v;
                            else ownLevels.erase(r.id);
                            if (!levels_save(ownRoot, ownLevels)) note(NOTE_ERROR, "Can't write " + ownRoot + "/stages.json");
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                ImGui::TreePop();
            }
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

