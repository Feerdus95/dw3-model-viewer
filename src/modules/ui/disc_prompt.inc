    /* ===== the disc prompt of the DW2 and DCB tabs, and their bulk export ===== */
    static struct DiscUd { Picked *p; int game; } discUd[2];
    auto pick_disc = [&](int game) {
        static const SDL_DialogFileFilter DISC = {"Disc images (.cue, .bin, .iso)", "cue;bin;iso"};
        discUd[game] = {&picked, game};
        SDL_ShowOpenFileDialog(
            [](void *u, const char *const *l, int) {
                if (!l || !*l) return;
                DiscUd *d = (DiscUd *)u;
                std::lock_guard<std::mutex> lock(d->p->m);
                (d->game ? d->p->dcbDisc : d->p->dw2Disc) = *l;
            },
            &discUd[game], win, &DISC, 1, nullptr, false);
    };
    /* the reading's progress, or the bulk export's; true while one runs (the tab shows only that) */
    auto disc_progress = [&](int game) -> bool {
        DiscRead &r = g_read[game];
        BulkExport &bx = g_bulk[game];
        {
            std::string img;
            {
                std::lock_guard<std::mutex> lock(picked.m);
                std::string &c = game ? picked.dcbDisc : picked.dw2Disc;
                img = c, c.clear();
            }
            if (!img.empty()) disc_read_start(game, img);
        }
        if (r.running) {
            hint("Reading the disc: %d%%", (int)r.prog.percent);
            ImGui::ProgressBar(r.prog.percent / 100.0f, ImVec2(-FLT_MIN, 0));
            if (button("Cancel")) r.prog.cancel = true;
            return true;
        }
        if (bx.running) {
            hint("Exporting every model (glTF and OBJ): %d%%", (int)bx.percent);
            ImGui::ProgressBar(bx.percent / 100.0f, ImVec2(-FLT_MIN, 0));
            if (button("Cancel##bulk")) bx.cancel = true;
            return true;
        }
        if (bx.finished) {
            if (bx.th.joinable()) bx.th.join();
            bx.finished = false;
            note(NOTE_OK, bx.note);
        }
        return false;
    };
    /* the button that asks for the disc, and the last reading's error */
    auto disc_button = [&](int game, bool first) {
        DiscRead &r = g_read[game];
        if (!r.err.empty()) hint("%s", r.err.c_str());
        if (first) {
            hint(game ? "Choose your own Digital Card Battle disc (a .cue, .bin or .iso, or drop it on the window): its Digimon are read into %s."
                      : "Choose your own Digimon World 2 disc (USA, SLUS-01193; a .cue, .bin or .iso, or drop it on the window): its models are read into %s.",
                 (game ? g_dcb : g_dw2).empty() ? (game ? "data/dcb" : "data/dw2") : (game ? g_dcb : g_dw2).c_str());
        }
        if (button((std::string(IC_FOLDER) + (first ? "  Choose the disc..." : "  Read a disc...")).c_str(), first ? 1 : 0)) pick_disc(game);
    };
    auto bulk_button = [&](int game, const std::vector<Dw2Entry> &list, int only) {
        if (button(IC_EXPORT "  Export all models...", 0)) {
            std::vector<std::string> paths;
            for (const Dw2Entry &en : list)
                if (only < 0 || (int)en.fx == only) paths.push_back(en.path);
            bulk_export_start(game, paths);
        }
        tip("Every model of this list as a .glb (skeleton and motions) and an .obj, with their textures, in %s/models/%s",
            g_exe_dir.c_str(), game ? "dcb" : "dw2");
    };
    /* the open DW2 / DCB model's export buttons: the same files as Model's, in models/<dw2|dcb>/<name>/ */
    auto viewer_export = [&](int game) {
        heading("Blender and the like");
        std::string name = std::filesystem::u8path(scene.path).parent_path().filename().u8string();
        std::string dir = g_exe_dir + "models/" + (game ? "dcb/" : "dw2/") + name;
        if (ImGui::Button(IC_EXPORT "  Export OBJ")) {
            SDL_CreateDirectory((g_exe_dir + "models").c_str());
            SDL_CreateDirectory((g_exe_dir + "models/" + (game ? "dcb" : "dw2")).c_str());
            SDL_CreateDirectory(dir.c_str());
            StageEdit e;
            StageInfo info;
            e.fromModel(*scene.model, info);
            if (e.exportObj(dir, name)) note(NOTE_OK, "Exported " + name + " to " + dir);
            else note(NOTE_ERROR, "Can't write " + dir);
        }
        tip("One object per bone, named part_N, at rest (y up, 1 unit = 100 of the game's), with the model's texture pages as PNGs");
        same_line_if_fits(IC_EXPORT "  Export glTF");
        if (ImGui::Button(IC_EXPORT "  Export glTF")) {
            SDL_CreateDirectory((g_exe_dir + "models").c_str());
            SDL_CreateDirectory((g_exe_dir + "models/" + (game ? "dcb" : "dw2")).c_str());
            SDL_CreateDirectory(dir.c_str());
            std::string err;
            if (export_gltf(*scene.model, dir, name, err)) note(NOTE_OK, "Exported " + name + " to " + dir + "/" + name + ".glb");
            else note(NOTE_ERROR, err);
        }
        tip("The skeleton, the faces with their texture pages as PNGs, and every motion as an animation: a .glb for Blender's File > Import > glTF");
        if (ImGui::Button(IC_FOLDER "##viewerdir")) open_folder(dir);
        tip("Open %s", dir.c_str());
    };
