    /* --- opening things (asking first when that loses changes) --- */
    auto after_open = [&]() {
        view.stageView = false, stageId = -1, stageDirty = false;
        reset_selection();
        texPage.fitPending = true;
    };
    struct StageSnap {
        std::vector<StagePart> parts;
        std::vector<StageProp> props;
        PropsMeta meta;
        std::shared_ptr<const std::vector<StageImage>> images; /* shared by the steps that didn't change them */
        uint8_t bg[3];
        StageInfo info;
        int part;
    };
    History<StageSnap> stageHist;
    struct OwnSnap {
        std::vector<CustomFrame> frames;
        float x, y, scale;
        bool add;
    };
    History<OwnSnap> ownHist;
    auto open_file = [&](std::string name, std::string path, int fid) {
        leave_stage([&, name, path, fid]() {
            if (!open_model(scene, view, name, path, status, fid)) return;
            after_open();
            stageHist.clear();
        });
    };
    /* a DW2 model's size: the steps of undo are the whole tuning as it was */
    std::vector<Model::Dw2Tuning> dw2Undo, dw2Redo;
    const Model *dw2Of = nullptr;
    std::string dw2OfPath;
    bool dw2Editing = false;
    auto dw2_sync = [&]() {
        if (scene.model.get() == dw2Of && scene.path == dw2OfPath) return;
        dw2Of = scene.model.get(), dw2OfPath = scene.path, dw2Editing = false, dw2Undo.clear(), dw2Redo.clear();
    };
    auto dw2_mark = [&]() {
        dw2Undo.push_back(scene.model->tune);
        if (dw2Undo.size() > 60) dw2Undo.erase(dw2Undo.begin());
        dw2Redo.clear();
    };
    /* what a DW2 model becomes in DW2003 (dw2003.txt beside it): converted by its own converter, then opened in the same sections as any
       fighter, with the scripts and portraits the game will give it */
    auto dw2_convert = [&](const std::string &model) {
        Dw2Link l = dw2_link(model);
        if (!l.ok || l.converter.empty() || l.tmpl.empty()) return note(NOTE_ERROR, "dw2003.txt needs converter, template, out, fid and like");
        std::string dir = std::filesystem::u8path(model).parent_path().u8string();
        const char *args[] = {l.python.c_str(), l.converter.c_str(), dir.c_str(), l.tmpl.c_str(), l.out.c_str(), nullptr};
        SDL_Process *pr = SDL_CreateProcess(args, false);
        if (!pr) return note(NOTE_ERROR, "Can't run " + l.python + ": " + SDL_GetError());
        int code = -1;
        SDL_WaitProcess(pr, true, &code);
        SDL_DestroyProcess(pr);
        if (code == 0) note(NOTE_OK, "Converted to " + l.out);
        else note(NOTE_ERROR, "The converter failed (exit " + std::to_string(code) + "): run it by hand to see why");
    };
    auto dw2_open_converted = [&](const std::string &model) {
        Dw2Link l = dw2_link(model);
        std::vector<uint8_t> bytes;
        if (!l.ok || !read_file(l.out, bytes)) return note(NOTE_ERROR, l.ok ? "Convert it first: " + l.out + " isn't there" : "No dw2003.txt beside this model");
        if (!dw2_register(l)) return note(NOTE_ERROR, "No partner named " + l.like + " in the fighters' file to be like");
        open_file((l.name.empty() ? l.like : l.name) + " (DW2003)", l.out, l.fid);
    };
    auto open_dropped = [&](std::string path) {
        leave_stage([&, path]() {
            open_entry(path, 0);
            if (scene.model && !view.stageView) after_open(), stageHist.clear();
        });
    };
    auto open_stage = [&](int id) {
        if (id == stageId && view.stageView) return;
        leave_stage([&, id]() {
            stageHidden.clear(), stageHist.clear();
            reset_selection();
            stage_open(id);
        });
    };
    auto pick_effect = [&](int id, bool select) {
        if (id == fx.id) {
            fxSelect = fxSelect || select;
            return;
        }
        leave_own([&, id, select]() {
            ownHist.clear(), ownFrame = -1;
            open_effect(id, select);
        });
    };
    auto reload_own = [&](bool critical) {
        leave_own([&, critical]() {
            ownCritical = critical, ownHist.clear(), ownFrame = -1;
            own_load();
        });
    };
    auto pick_data_folder = [&]() {
        SDL_ShowOpenFolderDialog(
            [](void *u, const char *const *l, int) {
                if (!l || !*l) return;
                Picked *p = (Picked *)u;
                std::lock_guard<std::mutex> lock(p->m);
                p->data = *l;
            },
            &picked, win, nullptr, false);
    };
    auto pick_mods_folder = [&]() {
        SDL_ShowOpenFolderDialog(
            [](void *u, const char *const *list, int) {
                if (!list || !*list) return;
                Picked *p = (Picked *)u;
                std::lock_guard<std::mutex> lock(p->m);
                p->folder = *list;
            },
            &picked, win, ownRoot.c_str(), false);
    };
    auto pick_bin = [&]() {
        static const SDL_DialogFileFilter BIN = {"The game's model files (.BIN, DW2: .DW2M)", "bin;dw2m"};
        SDL_ShowOpenFileDialog(
            [](void *u, const char *const *l, int) {
                if (!l || !*l) return;
                Picked *p = (Picked *)u;
                std::lock_guard<std::mutex> lock(p->m);
                p->bin = *l;
            },
            &picked, win, &BIN, 1, g_data.c_str(), false);
    };

