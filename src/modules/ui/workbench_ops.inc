    /* ===== The camera workbench's history (workbench_history.inc): the operations on the candidate and the sequence, and Undo / Redo ===== */
    WbHist wbh;
    auto wb_now = [&]() { return wb_take(cand, choreo); };
    /* an operation on the workbench: begin it (the history keeps the state it starts from), change the state, end it (an operation that
       changed nothing leaves no entry) */
    auto wb_begin = [&]() { wbh.edit(wb_now(), false); };
    auto wb_end = [&]() { wbh.finish(wb_now()); };
    /* an edit made by a widget that may go on over frames (a drag): BEFORE is the state taken before the widget changed it; the entry
       is closed when the mouse is let go (wb_settle, every frame) */
    auto wb_widget = [&](const WbSnap &before) { wbh.edit(before, true); };
    auto wb_settle = [&]() {
        if (wbh.dragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) wbh.finish(wb_now());
    };
    /* Undo (or Redo): the workbench goes to the state the history has, and B is made to follow it: the sequence's cameras again, the
       candidate's. The script is not touched: it goes on where it is. */
    auto wb_step = [&](bool redoing) {
        WbSnap to;
        if (!wbh.step(redoing, wb_now(), to)) return false;
        cams_restore(view, scene, run, cand, choreo, to);
        return true;
    };
    /* A different fighter: the workbench starts over for it (no history, nothing unsaved): the donor is the fighter itself, the sequence is none. */
    const Fighter *wbFor = nullptr;
    auto wb_sync = [&]() {
        const Fighter *f = scene.model && !scene.model->dw2 ? scene_fighter(scene) : nullptr;
        if (f == wbFor) return;
        wbFor = f;
        wbh.clear();
        if (!f) return;
        cand_ensure(*f);
        if (choreo.has && choreo.app.fighter != f->id) choreo.has = choreo.on = false, choreo.eff.clear();
        WbSnap s = wb_now();
        wbh.set_clean_cand(s.c), wbh.set_clean_seq(s.s);
    };
    /* what is unsaved: WB_CAND, WB_SEQ, both or 0 */
    auto wb_dirty = [&]() { return wbFor ? wbh.dirty(wb_now()) : 0; };
