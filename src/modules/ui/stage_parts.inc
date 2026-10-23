    /* --- the stage's parts --- */
    StageEdit partClip; /* a part copied: partClip.parts[0], fixed where it stood */
    int partClipStage = -1;
    auto part_name = [&](int k) { return k >= 0 && k < (int)stageEdit.parts.size() ? stageEdit.parts[k].name : std::string(); };
    /* an edit of the selected part begins: undo gets where it was (BEFORE: its move, turn and scale) */
    auto part_begin = [&](const char *verb, const float before[5]) {
        if (stagePart < 0) return;
        stage_mark();
        StagePart &old = stageHist.undo.back().parts[stagePart];
        memcpy(old.move, before, 12), old.turn = before[3], old.scale = before[4];
        memcpy(partStart, before, sizeof partStart);
        partVerb = verb, partEditing = true;
    };
    auto part_end = [&](bool edited) {
        partEditing = vpMoving = false;
        if (edited) stage_rebuild();
        else if (!stageHist.undo.empty()) stageHist.undo.pop_back();
    };
    auto part_place = [&](int k, float out[5]) {
        const StagePart &p = stageEdit.parts[k];
        out[0] = p.move[0], out[1] = p.move[1], out[2] = p.move[2], out[3] = p.turn, out[4] = p.scale;
    };
    auto part_delete = [&](int k) {
        if (k < 0 || k >= (int)stageEdit.parts.size()) return;
        std::string n = part_name(k);
        stage_mark();
        stageEdit.remove(k);
        stagePart = -1;
        if (stage_rebuild()) note(NOTE_INFO, "Removed " + n + " (Ctrl+Z brings it back)");
    };
    auto part_copy = [&](int k) {
        if (k < 0 || k >= (int)stageEdit.parts.size()) return;
        partClip = StageEdit();
        partClip.append(stageEdit, k, "");
        partClipStage = stageId;
        ImGui::SetClipboardText(part_name(k).c_str());
        note(NOTE_INFO, "Copied " + part_name(k) + ": Ctrl+V pastes it here or in another stage");
    };
    auto part_paste = [&]() {
        if (partClip.parts.empty() || !stage_on()) return;
        stage_mark();
        char pre[8] = "";
        if (partClipStage != stageId) snprintf(pre, sizeof pre, "s%02d_", partClipStage);
        stageEdit.append(partClip, 0, pre);
        if (partClipStage == stageId) stageEdit.parts.back().move[0] += 300; /* beside the one it copies */
        stagePart = (int)stageEdit.parts.size() - 1;
        if (stage_rebuild()) note(NOTE_OK, "Pasted " + part_name(stagePart));
        scrollToSel = true;
    };
    auto part_duplicate = [&](int k) {
        if (k < 0 || k >= (int)stageEdit.parts.size()) return;
        stage_mark();
        stageEdit.append(stageEdit, k, "");
        stageEdit.parts.back().move[0] += 300;
        stagePart = (int)stageEdit.parts.size() - 1;
        if (stage_rebuild()) note(NOTE_OK, "Duplicated as " + part_name(stagePart));
        scrollToSel = true;
    };
    auto part_hide = [&](int k) {
        std::string n = part_name(k);
        if (!stageHidden.erase(n)) stageHidden.insert(n);
    };
    auto part_bounds = [&](int k) {
        stage_mark();
        stageEdit.parts[k].noBounds = !stageEdit.parts[k].noBounds;
        stage_rebuild();
    };

    /* --- the model's bones, and the view --- */
    auto show_all = [&]() {
        if (stage_on()) stageHidden.clear();
        else if (scene.model) std::fill(scene.hidden.begin(), scene.hidden.end(), false);
    };
    auto show_only = [&](int b) {
        if (stage_on()) {
            int k = part_of_bone(b);
            stageHidden.clear();
            for (int i = 0; i < (int)stageEdit.parts.size(); i++)
                if (i != k) stageHidden.insert(stageEdit.parts[i].name);
        } else if (scene.model) {
            for (size_t i = 1; i < scene.hidden.size(); i++) scene.hidden[i] = (int)i != b;
        }
    };
    auto hide_selected = [&]() {
        if (stage_on() && stagePart >= 0) part_hide(stagePart);
        else if (scene.model && scene.selected > 0) scene.hidden[scene.selected] = !scene.hidden[scene.selected];
    };
    auto frame_all = [&]() {
        if (section == EFFECTS) fxFitPending = true;
        else if (section == TEXTURES) texFitPending = true;
        else if (scene.model) view.stageView ? frame_stage(view) : frame_model(scene, view);
    };
    auto frame_sel = [&]() {
        std::vector<int> bs = section == EFFECTS || section == TEXTURES ? std::vector<int>() : selected_bones();
        if (bs.empty() || !frame_bones(scene, view, bs)) frame_all();
    };

