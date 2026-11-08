    /* ===== Fighter: editing a script (the native port's data/mods/scripts/<fighter>/<script>.json) ===== */
    /* A script is edited as its commands' words (script_mod.h); the Fighter section lists and plays what is edited, and Save writes the file the port reads. */
    struct ScriptEdit {
        ScriptWords w;
        bool dirty = false, mod = false; /* mod: it has a file */
    };
    std::map<std::pair<int, int>, ScriptEdit> scrEd; /* by fighter, script */
    std::string scrKey;                              /* the model and fighter the edits were last put into */
    int scrFi = -1, scrSc = -1, scrCmd = -1;         /* the command picked: its fighter, script and place */
    auto scripts_dir = [&]() { return ownRoot + "/../scripts"; };
    auto script_file = [&](int fid, int i) { return scripts_dir() + "/" + std::to_string(fid) + "/" + std::to_string(i) + ".json"; };
    /* the game's script I of fighter F, as commands */
    auto script_game_words = [&](const Fighter &f, int i) -> ScriptWords {
        std::vector<uint8_t> file;
        if (!read_file(fid_path(f.effectsFid), file)) return {};
        Archive a{file.data(), file.size()};
        if (f.effectsEntry >= a.count() || !a.entry(f.effectsEntry) || (size_t)i * 4 + 4 > a.entry_size(f.effectsEntry)) return {};
        uint32_t off;
        memcpy(&off, a.entry(f.effectsEntry) + i * 4, 4);
        if (off == 0 || off >= a.entry_size(f.effectsEntry)) return {};
        return script_split(a.entry(f.effectsEntry) + off, a.entry(f.effectsEntry) + a.entry_size(f.effectsEntry));
    };
    auto script_apply = [&](Model &mm, int fid, int i) {
        auto it = scrEd.find({fid, i});
        if (it != scrEd.end() && i < (int)mm.scripts.size()) mm.scripts[i] = script_lines(it->second.w);
    };
    auto script_changed = [&](Model &mm, int fid, int i) {
        scrEd[{fid, i}].dirty = true;
        scrAnyDirty = true;
        script_apply(mm, fid, i);
    };
    /* the fighter's files are read and the edits put into the model whenever it is another model or fighter */
    auto scripts_sync = [&](Model &mm, const Fighter &f) {
        std::string key = scene.path + "#" + std::to_string(f.id) + "#" + std::to_string((uintptr_t)&mm) + "#" + std::to_string(mm.scripts.size());
        if (key == scrKey) return;
        scrKey = key;
        for (int i = 0; i < (int)mm.scripts.size(); i++) {
            if (!scrEd.count({f.id, i})) {
                std::ifstream in(script_file(f.id, i), std::ios::binary);
                if (!in) continue;
                std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()), err;
                ScriptEdit e;
                if (!script_from_json(text, e.w, err)) {
                    note(NOTE_ERROR, "Script " + std::to_string(f.id) + "." + std::to_string(i) + ": " + err + ". The port would not use it");
                    continue;
                }
                e.mod = true;
                scrEd[{f.id, i}] = e;
            }
            script_apply(mm, f.id, i);
        }
    };
    auto script_customize = [&](Model &mm, const Fighter &f, int i) {
        ScriptWords w = script_game_words(f, i);
        if (w.empty()) return note(NOTE_ERROR, "That script isn't in the game's files");
        scrEd[{f.id, i}] = ScriptEdit{w, false, false};
        script_changed(mm, f.id, i);
        scrFi = f.id, scrSc = i, scrCmd = 0;
    };
    auto scripts_save = [&]() {
        if (!sndLoaded) sounds_load();
        int saved = 0;
        for (auto &kv : scrEd) {
            if (!kv.second.dirty) continue;
            std::string missing;
            for (const ScriptCmd &c : kv.second.w)
                if (!c.sound.empty() && sndLib.find(c.sound) < 0) missing = c.sound;
            std::string tag = std::to_string(kv.first.first) + "." + std::to_string(kv.first.second);
            if (!missing.empty()) {
                note(NOTE_ERROR, "Script " + tag + " names \"" + missing + "\", which the sound library doesn't have: the port would reject it");
                continue;
            }
            std::string json = script_to_json(kv.second.w), err;
            ScriptWords back;
            if (!script_from_json(json, back, err)) {
                note(NOTE_ERROR, "Script " + tag + " would not read back: " + err);
                continue;
            }
            std::string dir = scripts_dir() + "/" + std::to_string(kv.first.first);
            SDL_CreateDirectory((scripts_dir()).c_str());
            SDL_CreateDirectory(dir.c_str());
            std::ofstream out(script_file(kv.first.first, kv.first.second), std::ios::binary);
            if (!out) {
                note(NOTE_ERROR, "Can't write " + script_file(kv.first.first, kv.first.second));
                continue;
            }
            out << json;
            kv.second.dirty = false, kv.second.mod = true, saved++;
        }
        scrAnyDirty = false;
        for (auto &kv : scrEd) scrAnyDirty |= kv.second.dirty;
        if (saved) note(NOTE_OK, "Saved " + std::to_string(saved) + (saved == 1 ? " script" : " scripts") + " in " + nice_path(scripts_dir()));
    };
    scriptsSave = scripts_save;
    auto script_revert = [&](Model &mm, const Fighter &f, int i) {
        SDL_RemovePath(script_file(f.id, i).c_str());
        scrEd.erase({f.id, i});
        std::vector<std::vector<ScriptLine>> orig = fighter_scripts(f);
        if (i < (int)orig.size() && i < (int)mm.scripts.size()) mm.scripts[i] = orig[i];
        if (scrFi == f.id && scrSc == i) scrCmd = -1;
        scrAnyDirty = false;
        for (auto &kv : scrEd) scrAnyDirty |= kv.second.dirty;
    };
    /* the picked command's fields, in the inspector */
    auto script_command_editor = [&](Model &mm, const Fighter &f) {
        auto it = scrEd.find({scrFi, scrSc});
        if (scrFi != f.id || it == scrEd.end() || scrCmd < 0 || scrCmd >= (int)it->second.w.size()) return false;
        ScriptWords &sw = it->second.w;
        ScriptCmd &c = sw[scrCmd];
        /* a number: typed, stepped with the arrows (Ctrl for 10) and, for a short range, dragged on a bar that shows where it is */
        auto int_field = [&](const char *name, int &v, int lo, int hi) {
            bool ch = false, bar = hi - lo <= 1000;
            ImGui::PushID(name);
            ImGui::PushTextWrapPos(0); /* the name goes above, wrapped: it never runs past the panel */
            ImGui::TextUnformatted(name);
            ImGui::PopTextWrapPos();
            ImGui::SetNextItemWidth(bar ? ImGui::GetContentRegionAvail().x * 0.5f : -FLT_MIN);
            int n = v;
            if (ImGui::InputInt("##n", &n, 1, 10)) v = std::clamp(n, lo, hi), ch = true;
            tip(("From " + std::to_string(lo) + " to " + std::to_string(hi) + ". Type a value, or use the arrows").c_str());
            if (bar) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-FLT_MIN);
                n = v;
                if (ImGui::SliderInt("##s", &n, lo, hi, "")) v = n, ch = true;
            }
            ImGui::PopID();
            return ch;
        };
        bool changed = false;
        title("Script %d, command %d", scrSc, scrCmd + 1);
        if (scrCmd < (int)mm.scripts[scrSc].size()) hint("%s", mm.scripts[scrSc][scrCmd].text.c_str());
        if (c.w[0] == 10) {
            /* a sound: one of the library's, one of the game's, or the technique's hit sound */
            int snd = c.w[1] & ~0x4000, aware = c.w[1] & 0x4000;
            int kind = !c.sound.empty() ? 0 : snd == 0x62 ? 2 : snd == 0x63 ? 3 : 1;
            static const char *KINDS[] = {"A sound of the library", "One of the game's sounds", "The hit's sound", "The last hit's sound"};
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##sndkind", KINDS[kind])) {
                for (int k = 0; k < 4; k++)
                    if (ImGui::Selectable(KINDS[k], k == kind) && k != kind) {
                        if (!sndLoaded) sounds_load();
                        c.sound = k == 0 && !sndLib.sounds.empty() ? sndLib.sounds[0].name : "";
                        c.w[1] = k == 2 ? 0x62 : k == 3 ? 0x63 : k == 1 ? 57 : 0;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            if (kind == 0) {
                if (!sndLoaded) sounds_load();
                bool known = sndLib.find(c.sound) >= 0;
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo("##sndname", c.sound.empty() ? "(the library is empty)" : (c.sound + (known ? "" : "  (not in the library)")).c_str())) {
                    for (const SoundEntry &e : sndLib.sounds)
                        if (ImGui::Selectable(e.name.c_str(), e.name == c.sound)) c.sound = e.name, changed = true;
                    ImGui::EndCombo();
                }
                if (sndLib.sounds.empty()) hint("Import a WAV in the Sounds section first.");
                tip("The port plays this sound of data/mods/sounds");
            } else if (kind == 1) {
                if (int_field("game sound", snd, 1, 89)) c.w[1] = snd | aware, changed = true;
            }
            if (kind <= 1) {
                bool a = aware != 0;
                if (ImGui::Checkbox("##aware", &a)) c.w[1] = snd | (a ? 0x4000 : 0), changed = true;
                ImGui::SameLine();
                ImGui::PushTextWrapPos(0);
                ImGui::TextUnformatted("The game's own sound on a result 3 hit");
                ImGui::PopTextWrapPos();
                tip("A hit of result 3 (a hit that lands for 0) plays the game's own sound 56 instead of this one, as the hit's sound does");
            }
        }
        for (const ScriptField &fl : script_fields(c)) {
            int v = c.w[fl.index];
            if (int_field(fl.name.c_str(), v, fl.lo, fl.hi)) c.w[fl.index] = v, changed = true;
        }
        if (script_fields(c).empty() && c.w[0] != 10) hint("This command has nothing to change; insert or delete commands around it.");
        ImGui::SeparatorText("The script");
        bool last = scrCmd == (int)sw.size() - 1;
        ImGui::BeginDisabled(last);
        if (ImGui::Button(IC_DELETE "  Delete")) {
            sw.erase(sw.begin() + scrCmd);
            changed = true;
        }
        ImGui::EndDisabled();
        tip(last ? "The script's last command is its end" : "Takes this command out of the script");
        same_line_if_fits("Up");
        ImGui::BeginDisabled(scrCmd == 0 || last);
        if (ImGui::Button("Up")) std::swap(sw[scrCmd], sw[scrCmd - 1]), scrCmd--, changed = true;
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(scrCmd >= (int)sw.size() - 2);
        if (ImGui::Button("Down")) std::swap(sw[scrCmd], sw[scrCmd + 1]), scrCmd++, changed = true;
        ImGui::EndDisabled();
        static int addKind = 3;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##addkind", ("Insert after: " + script_kinds()[addKind]).c_str())) {
            for (int k = 0; k < (int)script_kinds().size(); k++)
                if (ImGui::Selectable(script_kinds()[k].c_str(), k == addKind)) addKind = k;
            ImGui::EndCombo();
        }
        if (ImGui::Button(IC_ADD "  Insert it")) {
            ScriptCmd n = script_new(addKind);
            if (addKind == 0) {
                if (!sndLoaded) sounds_load();
                if (!sndLib.sounds.empty()) n.sound = sndLib.sounds[0].name;
            }
            int at = last ? scrCmd : scrCmd + 1; /* after the end can't be: the new command goes before it */
            sw.insert(sw.begin() + at, n);
            scrCmd = at;
            changed = true;
        }
        if (changed) script_changed(mm, f.id, scrSc);
        return true;
    };
