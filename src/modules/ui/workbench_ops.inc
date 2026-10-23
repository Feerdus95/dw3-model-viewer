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
    [[maybe_unused]] auto wb_step = [&](bool redoing) { /* (bound to Ctrl+Z and Ctrl+Y in the next step) */
        WbSnap to;
        if (!wbh.step(redoing, wb_now(), to)) return false;
        cams_restore(view, scene, run, cand, choreo, to);
        return true;
    };
