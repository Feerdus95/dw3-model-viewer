    /* ===== Sounds: the game's battle sounds, and the library that replaces them (the native port's data/mods/sounds) ===== */
    /* the game's own sounds are the WAVs the port's launcher renders into data/sounds; the library is data/mods/sounds */
    /* the techniques that play each game sound (their scriptSound), by name, once */
    auto sound_users = [&]() -> const std::map<int, std::vector<std::string>> & {
        static std::map<int, std::vector<std::string>> users;
        static bool built = false;
        if (!built) {
            built = true;
            for (const TechRow &t : g_techs) {
                if (t.sound <= 0 || t.sound >= 0x5A) continue;
                std::string name = t.name == "a" ? "a (basic attack)" : t.name;
                std::vector<std::string> &v = users[t.sound];
                if (!name.empty() && std::find(v.begin(), v.end(), name) == v.end()) v.push_back(name);
            }
        }
        return users;
    };
    /* what a sound is for, from the game's scripts (the compendium: which slots play it) and its code: the techniques' own hit
       sounds are named by the techniques instead */
    #include "sounds_data.inc"
    auto sound_use = [&](int index) -> const SoundUse * {
        for (const SoundUse &u : SOUND_USES)
            if (u.index == index) return &u;
        return nullptr;
    };
    /* what a sound is for: 1-17 are the game's sequences, named in its sound test (soundtst.c), and 1, 2, 8 and 9 are what the
       game's players say they are for; from 18 up the scripts only say what surrounds the sound, never what it is (sounds_data.inc) */
    auto sound_role = [&](int index) -> std::string {
        static const char *SEQ[] = {"", "Confirm (SYSTEM07)", "Digimental activation (DIGIMENT)", "Jingle (JINGLE01)", "Jingle (JINGLE02)",
                                    "Jingle (JINGLE03)", "Jingle (JINGLE04)", "Recovery (RECOVERY)", "Text bubble (SYSTEM04)",
                                    "Text bubble (SYSTEM03)", "Footstep (SHT_STEP)", "Footstep (MID_STEP)", "Footstep (LRG_STEP)",
                                    "Footstep (WNG_STEP)", "Fall (SHT_DOWN)", "Fall (MID_DOWN)", "Fall (LRG_DOWN)", "Fall (MTL_DOWN)"};
        if (index >= 1 && index <= 17) return SEQ[index];
        if (index == 56) return "Result 3 hit (the game's own)";
        if (const SoundUse *u = sound_use(index)) return u->kind;
        return "Not played by any fighter script";
    };
    /* the fighters whose scripts play it */
    auto sound_fighters = [&](int index) -> std::string {
        const SoundUse *u = sound_use(index);
        if (!u) return "";
        std::string out;
        if (*u->basic) out += std::string("Basic attack of: ") + u->basic + ". ";
        if (*u->tech) out += std::string("Techniques of: ") + u->tech + ".";
        return out;
    };
    /* the code's own uses (items, counterattacks) */
    auto sound_extra = [&](int index) -> std::string {
        return index == 27 || index == 30 || index == 31 || index == 39 || index == 49 ? "Items and counterattacks also play it." : "";
    };
    auto sound_found = [&](int index) {
        if (!*sndFind) return true;
        if (atoi(sndFind) == index) return true;
        if (SDL_strcasestr(sound_role(index).c_str(), sndFind) || SDL_strcasestr(sound_fighters(index).c_str(), sndFind)) return true;
        auto it = sound_users().find(index);
        if (it != sound_users().end())
            for (const std::string &n : it->second)
                if (SDL_strcasestr(n.c_str(), sndFind)) return true;
        return false;
    };
    auto sound_techs = [&](int index, size_t most) {
        std::string out;
        auto it = sound_users().find(index);
        if (it == sound_users().end()) return sound_role(index);
        for (size_t i = 0; i < it->second.size() && i < most; i++) out += (i ? ", " : "") + it->second[i];
        if (it->second.size() > most) out += " +" + std::to_string(it->second.size() - most);
        return out;
    };
    auto import_sound_pick = [&]() {
        static const SDL_DialogFileFilter WAV = {"WAV sounds", "wav"};
        SDL_ShowOpenFileDialog(picked_cb<&Picked::wav>, &picked, win, &WAV, 1, nullptr, false);
    };
    auto sounds_browser = [&]() {
        if (!fxGrouped) group_effects(), ownIds = own_effects(ownRoot); /* reads the techniques too */
        if (!sndLoaded) sounds_load();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##sndfind", IC_SEARCH "  Search: number, technique or Digimon", sndFind, sizeof sndFind);
        if (ImGui::BeginTabBar("sndlists")) {
            if (ImGui::BeginTabItem("Original")) {
                ImGui::BeginChild("sndorig");
                for (int i = 1; i < 0x5A; i++) {
                    if (!sound_found(i)) continue;
                    auto rep = sndLib.replace.find(i);
                    std::string l = std::to_string(i) + "  " + sound_techs(i, 2);
                    ImGui::PushID(i);
                    float markW = rep != sndLib.replace.end() ? ImGui::CalcTextSize("mod").x + ImGui::GetStyle().ItemSpacing.x : 0;
                    if (ImGui::Selectable(l.c_str(), sndSel == i, ImGuiSelectableFlags_AllowOverlap, ImVec2(ImGui::GetContentRegionAvail().x - markW, 0)))
                        sndSel = i, sndLibSel.clear();
                    tip("%s", sound_techs(i, 40).c_str());
                    if (rep != sndLib.replace.end()) {
                        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize("mod").x);
                        ImGui::TextColored(C_ACCENT, "mod");
                    }
                    ImGui::PopID();
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            char libTab[32];
            snprintf(libTab, sizeof libTab, "Library (%d)###sndlib", (int)sndLib.sounds.size());
            if (ImGui::BeginTabItem(libTab)) {
                if (ImGui::Button(IC_IMPORT "  Import WAV...")) import_sound_pick();
                tip("Copies a WAV into %s and adds it to the library under a name", nice_path(sounds_dir()).c_str());
                ImGui::BeginChild("sndlibl");
                if (sndLib.sounds.empty()) hint("No sounds yet. Import a WAV, then use it to replace one of the game's.");
                for (const SoundEntry &e : sndLib.sounds) {
                    if (*sndFind && !SDL_strcasestr(e.name.c_str(), sndFind)) continue;
                    if (ImGui::Selectable(e.name.c_str(), sndLibSel == e.name)) sndLibSel = e.name, sndSel = -1;
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    };
    auto sounds_inspector = [&]() {
        if (!fxGrouped) group_effects(), ownIds = own_effects(ownRoot);
        if (!sndLoaded) sounds_load();
        std::string wav = take_picked(picked, &Picked::wav);
        if (!wav.empty()) {
            std::string name = sndLib.import(wav);
            if (name.empty()) note(NOTE_ERROR, sndLib.error);
            else sndLibSel = name, sndSel = -1, sndDirty = true, note(NOTE_OK, "Added " + name + " to the library; Save to apply");
        }
        auto play_lib = [&](const SoundEntry &e) { sound_preview(sounds_dir() + "/" + e.file, e.volume); };
        if (sndSel > 0) {
            title("Game sound %d", sndSel);
            ImGui::TextWrapped("%s", sound_role(sndSel).c_str());
            if (sound_users().count(sndSel)) hint("The hit sound of: %s", sound_techs(sndSel, 8).c_str()), tip("%s", sound_techs(sndSel, 60).c_str());
            if (!sound_fighters(sndSel).empty()) hint("%s", sound_fighters(sndSel).c_str());
            if (!sound_extra(sndSel).empty()) hint("%s", sound_extra(sndSel).c_str());
            std::string orig = originals_dir() + "/" + std::to_string(sndSel) + ".wav";
            bool have = SDL_GetPathInfo(orig.c_str(), nullptr);
            ImGui::BeginDisabled(!have);
            if (ImGui::Button(IC_PLAY "  Play the original")) sound_preview(orig, 1);
            ImGui::EndDisabled();
            if (!have) tip("Not rendered yet: open the port's launcher, which renders the game's sounds into %s", nice_path(originals_dir()).c_str());
            ImGui::SeparatorText("Replaced by");
            auto rep = sndLib.replace.find(sndSel);
            int cur = rep == sndLib.replace.end() ? -1 : sndLib.find(rep->second);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##sndrep", cur >= 0 ? sndLib.sounds[cur].name.c_str() : "the game's own")) {
                if (ImGui::Selectable("the game's own", cur < 0) && rep != sndLib.replace.end()) sndLib.replace.erase(sndSel), sndDirty = true;
                for (const SoundEntry &e : sndLib.sounds)
                    if (ImGui::Selectable(e.name.c_str(), cur >= 0 && sndLib.sounds[cur].name == e.name) && !(cur >= 0 && sndLib.sounds[cur].name == e.name))
                        sndLib.replace[sndSel] = e.name, sndDirty = true;
                ImGui::EndCombo();
            }
            if (sndLib.sounds.empty()) {
                hint("The library is empty: import a WAV first.");
                if (ImGui::Button(IC_IMPORT "  Import WAV...")) import_sound_pick();
            }
            if (cur >= 0) {
                if (ImGui::Button(IC_PLAY "  Play the replacement")) play_lib(sndLib.sounds[cur]);
                hint("Every place the game plays this sound plays %s instead (Options > Mods > Custom sounds switches it off in game).", sndLib.sounds[cur].name.c_str());
            }
        } else if (!sndLibSel.empty() && sndLib.find(sndLibSel) >= 0) {
            SoundEntry &e = sndLib.sounds[sndLib.find(sndLibSel)];
            title("%s", e.name.c_str());
            field("File", "%s", e.file.c_str());
            if (ImGui::Button(IC_PLAY "  Play")) play_lib(e);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
            if (ImGui::DragFloat("volume", &e.volume, 0.01f, 0, 4, "%.2f")) sndDirty = true;
            tip("1 is the file as it is, up to 4");
            std::string uses;
            for (const auto &r : sndLib.replace)
                if (r.second == e.name) uses += (uses.empty() ? "" : ", ") + std::to_string(r.first);
            hint(uses.empty() ? "It replaces no game sound yet: pick one in Original." : "It replaces the game's sounds %s.", uses.c_str());
            if (button(IC_DELETE "  Remove from the library")) {
                std::string name = e.name;
                ask.title = "Remove " + name;
                ask.text = "This deletes " + e.file + " from " + nice_path(sndLib.dir) + " and the replacements that use it. It can't be undone.";
                ask.ok = "Remove", ask.danger = true, ask.open = true;
                ask.then = [&, name]() {
                    sndLib.remove(name);
                    sndLibSel.clear();
                    sounds_save();
                };
            }
        } else {
            title("No sound open");
            hint("Pick one of the game's sounds on the left to replace it with one of yours, or a sound of the library to edit it.");
            if (ImGui::Button(IC_IMPORT "  Import WAV...")) import_sound_pick();
        }
        ImGui::Spacing();
        ImGui::Separator();
        if (button(IC_SAVE "  Save", sndDirty ? 1 : 0)) sounds_save();
        tip("Writes sounds.json in %s (Ctrl+S)", nice_path(sounds_dir()).c_str());
        same_line_if_fits(IC_RESTART "  Reload");
        if (ImGui::Button(IC_RESTART "  Reload")) sounds_load();
        tip("Reads the folder again");
        hint("Folder: %s", nice_path(sounds_dir()).c_str());
        if (ImGui::SmallButton(IC_FOLDER " Open it")) SDL_CreateDirectory(sounds_dir().c_str()), open_folder(sounds_dir());
    };
