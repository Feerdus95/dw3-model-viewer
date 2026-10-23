    auto frame_ok = [&]() { return fx.id >= 0 && ownFrame >= 0 && ownFrame < (int)own.frames.size(); };
    auto frame_paste = [&]() {
        if (frameClip.empty() || fx.id < 0) return;
        if (frameClipDir != own.dir) return note(NOTE_WARN, "Frames paste within one effect's images: they name its PNGs");
        ownHist.push(own_snap());
        int at = frame_ok() ? ownFrame + 1 : (int)own.frames.size();
        own.frames.insert(own.frames.begin() + at, frameClip.begin(), frameClip.end());
        ownFrame = at, ownDirty = true, fxDrawn = -1;
    };
    auto frame_edit = [&](int i, int what) { /* 0 delete, 1 duplicate, -2/2 up/down */
        if (i < 0 || i >= (int)own.frames.size()) return;
        int to = i + what / 2;
        if (what && what != 1 && (to < 0 || to >= (int)own.frames.size())) return;
        ownHist.push(own_snap());
        if (what == 0) own.frames.erase(own.frames.begin() + i), ownFrame = -1;
        else if (what == 1) own.frames.insert(own.frames.begin() + i + 1, own.frames[i]), ownFrame = i + 1;
        else std::swap(own.frames[i], own.frames[to]), ownFrame = to;
        ownDirty = true, fxDrawn = -1;
    };
    auto set_props = [&](const std::set<int> &g) { /* the selection: none, one, or a group whose primary is its last */
        if (!g.empty()) select_part(-1);
        propSet.clear();
        if (g.size() > 1) propSet = g, propSetPrim = *g.rbegin(), propSetN = props.size(), propSel = propSetPrim;
        else propSel = g.empty() ? -1 : *g.begin();
    };
    auto cur_props = [&]() { return props_current(propSet, propSel, (int)props.size()); };
    auto prop_delete = [&](int i) {
        if (propSet.size() > 1) {
            stage_mark();
            note(NOTE_INFO, "Removed " + std::to_string(props_remove(props, propSet)) + " props (Ctrl+Z brings them back)");
            stageDirty = true;
            set_props({});
            return;
        }
        if (i < 0 || i >= (int)props.size()) return;
        stage_mark();
        props.erase(props.begin() + i);
        propSel = -1, stageDirty = true;
        note(NOTE_INFO, "Removed the prop (Ctrl+Z brings it back)");
    };
    auto prop_duplicate = [&](int i) {
        if (propSet.size() > 1) {
            stage_mark();
            set_props(props_duplicate(props, std::set<int>(propSet))), stageDirty = true;
            return;
        }
        if (i < 0 || i >= (int)props.size()) return;
        stage_mark();
        StageProp c = props[i];
        c.pos[0] += 300;
        props.push_back(c);
        propSel = (int)props.size() - 1, stageDirty = true;
    };
    auto prop_copy = [&]() {
        std::set<int> g = cur_props();
        if (g.empty()) return;
        props_copy(propClip, props, g);
        clipIsProps = true;
        note(NOTE_INFO, "Copied " + std::to_string(g.size()) + (g.size() == 1 ? " prop" : " props") + " (Ctrl+V puts them where the mouse is)");
    };
    auto prop_paste = [&]() { /* the copied group's middle goes to the mouse on the ground, else a step to the side */
        if (propClip.items.empty() || !stage_on()) return;
        ImVec2 mp = ImGui::GetIO().MousePos;
        float g[3], at[3] = {propClip.center[0] + 300, propClip.center[1], propClip.center[2]};
        if (mp.x >= vpRect[0] && mp.x < vpRect[0] + vpRect[2] && mp.y >= vpRect[1] && mp.y < vpRect[1] + vpRect[3] &&
            ground_hit(view, vpRect[2], vpRect[3], mp.x - vpRect[0], mp.y - vpRect[1], g))
            at[0] = g[0], at[1] = 0, at[2] = -g[2];
        stage_mark();
        set_props(props_paste(props, propClip, at)), stageDirty = true;
    };
    auto can_paste = [&]() { return section == EFFECTS ? !frameClip.empty() && fx.id >= 0 : stage_on() && (clipIsProps ? !propClip.items.empty() : !partClip.parts.empty()); };
    auto can_edit_sel = [&]() { return section == EFFECTS ? frame_ok() : stage_on() && (stagePart >= 0 || propSel >= 0); };
    auto do_copy = [&]() {
        if (section == EFFECTS) {
            if (frame_ok()) {
                frameClip = {own.frames[ownFrame]}, frameClipDir = own.dir;
                note(NOTE_INFO, "Copied frame " + std::to_string(ownFrame));
            } else if (fx.id >= 0) {
                ImGui::SetClipboardText(("effect " + std::to_string(fx.id)).c_str());
                note(NOTE_INFO, "Copied \"effect " + std::to_string(fx.id) + "\"");
            }
        } else if (stage_on() && stagePart < 0 && propSel >= 0) {
            prop_copy();
        } else if (stage_on() && stagePart >= 0) {
            clipIsProps = false;
            part_copy(stagePart);
        } else if (scene.model && !stage_on() && scene.selected > 0) {
            ImGui::SetClipboardText(bone_info(scene.selected).c_str());
            note(NOTE_INFO, "Copied bone " + std::to_string(scene.selected) + "'s details");
        } else if (scene.model) {
            std::string t = scene.model->name + (scene.fid >= 0 ? " #" + std::to_string(scene.fid) : "");
            ImGui::SetClipboardText(t.c_str());
            note(NOTE_INFO, "Copied \"" + t + "\"");
        }
    };
    auto do_paste = [&]() { section == EFFECTS ? frame_paste() : clipIsProps ? prop_paste() : part_paste(); };
    auto do_duplicate = [&]() { section == EFFECTS ? frame_edit(ownFrame, 1) : propSel >= 0 ? prop_duplicate(propSel) : part_duplicate(stagePart); };
    auto do_delete = [&]() { section == EFFECTS ? frame_edit(ownFrame, 0) : propSel >= 0 ? prop_delete(propSel) : part_delete(stagePart); };
    auto own_save = [&]() {
        if (own.save()) ownDirty = false, ownIds = own_effects(ownRoot), note(NOTE_OK, "Saved " + own.dir + "/effect.json");
        else note(NOTE_ERROR, own.error);
    };
    auto can_save = [&]() { return section == CAMERAS ? wb_undo_on() : section == EFFECTS ? fx.id >= 0 && !own.frames.empty() : stage_on() || (char_on() && charDirty); };
    auto do_save = [&]() {
        if (!can_save()) return;
        if (section == CAMERAS) return wb_save_do();
        if (char_on() && section != EFFECTS) return char_save();
        section == EFFECTS ? own_save() : stage_save();
    };
    auto ask_revert = [&]() {
        ask.title = "Revert to the disc's";
        ask.text = "Stage " + std::to_string(stageId) + " goes back to the disc's: this deletes " + mod_path(stageInfo.fid) +
                   " and puts its line of the stages' table back. It can't be undone.";
        ask.ok = "Revert", ask.danger = true, ask.open = true;
        ask.then = [&]() {
            stageHidden.clear(), stageHist.clear();
            reset_selection();
            int id = stageId;
            stage_revert();
            note(NOTE_OK, "Stage " + std::to_string(id) + " is the disc's again");
        };
    };

