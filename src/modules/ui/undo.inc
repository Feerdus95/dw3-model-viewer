    /* --- undo: the stage's edits, the own images' edits --- */
    std::shared_ptr<const std::vector<StageImage>> stageImages;
    auto same_images = [](const std::vector<StageImage> &a, const std::vector<StageImage> &b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); i++)
            if (a[i].w != b[i].w || a[i].h != b[i].h || a[i].source != b[i].source || a[i].clutX != b[i].clutX ||
                a[i].clutY != b[i].clutY || memcmp(a[i].clut, b[i].clut, sizeof a[i].clut) || a[i].px != b[i].px || a[i].index != b[i].index)
                return false;
        return true;
    };
    auto stage_snap = [&]() {
        StageSnap s;
        s.parts = stageEdit.parts;
        s.props = props, s.meta = propsMeta;
        if (!stageImages || !same_images(*stageImages, stageEdit.images))
            stageImages = std::make_shared<const std::vector<StageImage>>(stageEdit.images);
        s.images = stageImages;
        memcpy(s.bg, stageEdit.bg, 3);
        s.info = stageInfo, s.part = stagePart;
        return s;
    };
    auto stage_mark = [&]() { stageHist.push(stage_snap()); }; /* before an edit */
    auto stage_restore = [&](const StageSnap &s) {
        stageEdit.parts = s.parts, stageEdit.images = *s.images, stageInfo = s.info, props = s.props, propsMeta = s.meta;
        if (propSel >= (int)props.size()) propSel = -1;
        memcpy(stageEdit.bg, s.bg, 3);
        for (int k = 0; k < 3; k++) view.stageBg[k] = s.bg[k] / 255.0f;
        stagePart = s.part < (int)stageEdit.parts.size() ? s.part : -1;
        stage_rebuild();
        select_part(stagePart);
    };
    auto own_snap = [&]() { return OwnSnap{own.frames, own.x, own.y, own.scale, own.add}; };
    auto own_restore = [&](const OwnSnap &s) {
        own.frames = s.frames, own.x = s.x, own.y = s.y, own.scale = s.scale, own.add = s.add;
        ownDirty = true, fxDrawn = -1;
        if (ownFrame >= (int)own.frames.size()) ownFrame = -1;
    };
    /* the camera workbench's own undo and save (workbench_ops.inc, workbench_save.inc fill these in): Undo / Redo in Cameras and Fighter, Ctrl+S in Cameras */
    std::function<bool()> wb_undo_on;
    std::function<bool(bool)> wb_undo_can;
    std::function<void(bool)> wb_undo_do;
    std::function<void()> wb_save_do;
    auto undo_own = [&]() { return section == EFFECTS; };
    auto can_undo = [&](bool redo) {
        if (wb_undo_on()) return wb_undo_can(redo);
        if (undo_own()) return fx.id >= 0 && !(redo ? ownHist.redo : ownHist.undo).empty();
        if (scene.model && scene.model->dw2 && (section == DW2 || section == DCB || section == MODEL)) return !(redo ? dw2Redo : dw2Undo).empty();
        if (section == MODEL && char_on()) return !(redo ? charRedo : charUndo).empty();
        return stage_on() && !(redo ? stageHist.redo : stageHist.undo).empty();
    };
    auto do_undo = [&](bool redo) {
        if (!can_undo(redo)) return;
        if (wb_undo_on()) return wb_undo_do(redo);
        if (undo_own()) {
            std::vector<OwnSnap> &from = redo ? ownHist.redo : ownHist.undo, &to = redo ? ownHist.undo : ownHist.redo;
            to.push_back(own_snap());
            OwnSnap s = std::move(from.back());
            from.pop_back();
            own_restore(s);
        } else if (scene.model && scene.model->dw2 && (section == DW2 || section == DCB || section == MODEL)) {
            auto &from = redo ? dw2Redo : dw2Undo, &to = redo ? dw2Undo : dw2Redo;
            to.push_back(scene.model->tune);
            scene.model->tune = std::move(from.back());
            from.pop_back();
            scene.model->dw2_retune();
            upload(scene);
        } else if (section == MODEL && char_on()) {
            auto &from = redo ? charRedo : charUndo, &to = redo ? charUndo : charRedo;
            CharSnap now{charEdits, charPal};
            CharSnap s = std::move(from.back());
            from.pop_back();
            if (s.file) { /* across an OBJ import: the file goes back (or on) too */
                now.file = std::make_shared<const std::vector<uint8_t>>(scene.model->file), now.gen = charGen;
                to.push_back(std::move(now));
                if (!char_swap_file(*s.file)) {
                    to.pop_back(), from.push_back(std::move(s));
                    return;
                }
                charGen = s.gen;
            } else {
                to.push_back(std::move(now));
            }
            charEdits = std::move(s.e), charPal = std::move(s.pal);
            char_apply();
            char_pal_apply();
            charDirty = char_unsaved();
        } else {
            std::vector<StageSnap> &from = redo ? stageHist.redo : stageHist.undo, &to = redo ? stageHist.undo : stageHist.redo;
            to.push_back(stage_snap());
            StageSnap s = std::move(from.back());
            from.pop_back();
            stage_restore(s);
        }
    };

