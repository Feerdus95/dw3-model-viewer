    /* ===== Saving what the workbench holds: as a preset (the candidate) or as an application (the sequence) =====
       The buttons of the Details tab and Ctrl+S in Cameras call these. The clean point moves only once the file was written. */
    auto wb_save_preset = [&]() {
        libs_load();
        const Fighter *fp = scene_fighter(scene);
        if (!fp || !cand_ensure(*fp)) return;
        const Fighter &f = *fp, &donor = g_game.fighters[cand.fighter];
        if (pre.blocked) return note(NOTE_WARN, "presets.json couldn't be read, so nothing is saved over it.");
        if (cand.script < 0 || cand.of != &f) return note(NOTE_WARN, "Play one of this fighter's scripts first: a preset is made for the script it was seen with.");
        Preset p;
        p.name = form.name, p.notes = form.notes, p.status = form.status_name();
        p.fighter = f.id;
        p.scriptFid = f.effectsFid, p.scriptEntry = f.effectsEntry, p.scriptIdx = cand.script;
        p.donor = donor.id, p.donorEnemy = donor.enemy, p.donorIdx = cand.cam;
        p.c = cand.c, p.every = cand.every, p.fitK = cand.fitK;
        p.context = p.donorContext = g_context;
        preset_check(p);
        keep_or_ask<Preset>(
            ask, pre.list, p, "Replace the preset",
            [](const Preset &o) { return "\"" + o.name + "\" is already saved for this fighter, script and donor camera. Replace it with the one on screen?"; },
            [&](const Preset &q) {
                pre.active = q.id();
                preset_persist(pre.list, pre.error);
                if (!pre.error.empty()) return;
                wbh.set_clean_cand(wb_now().c);
                note(NOTE_OK, "Saved the preset \"" + q.name + "\"");
            });
    };
    auto wb_save_app = [&]() {
        libs_load();
        if (!choreo.has) return note(NOTE_WARN, "There is no sequence to save: build one in the Sequence tab.");
        if (choreo.blocked) return note(NOTE_WARN, "applications.json couldn't be read, so nothing is saved over it.");
        Application p = choreo.app;
        p.name = form.name, p.variant = form.variant, p.notes = form.notes, p.status = form.status_name();
        p.context = p.donorContext = g_context;
        app_check(p);
        keep_or_ask<Application>(
            ask, choreo.list, p, "Replace the application",
            [](const Application &o) { return "\"" + o.name + "\" is already saved for this donor script and this script (variant \"" + o.variant + "\"). Replace it with the one on screen?"; },
            [&](const Application &q) {
                choreo.active = q.id();
                choreo.app.variant = q.variant;
                app_persist(choreo.list, choreo.error);
                if (!choreo.error.empty()) return;
                wbh.set_clean_seq(wb_now().s);
                note(NOTE_OK, "Saved the application \"" + q.name + "\"");
            });
    };
    /* the hooks undo.inc and edit_actions.inc call */
    wb_undo_on = [&]() { return (section == CAMERAS || section == FIGHTER) && scene.model && !scene.model->dw2 && scene_fighter(scene); };
    wb_undo_can = [&](bool redo) { return redo ? wbh.can_redo() : wbh.can_undo(); };
    wb_undo_do = [&](bool redo) { wb_step(redo); };
    wb_save_do = [&]() { b_is_sequence(choreo) ? wb_save_app() : wb_save_preset(); }; /* what B shows is what Ctrl+S saves */
