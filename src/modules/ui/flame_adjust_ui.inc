    /* ===== Fighter: Flamedramon's hand adjustment (flame_adjust.inc), in the inspector ===== */
    auto flame_panel = [&]() {
        if (!scene.model || !flame_model(*scene.model) || !std::filesystem::exists(flame_dir())) return;
        static bool loaded = false;
        static int bone = -1, fxId = 456;
        static std::atomic<int> job{0}; /* the rebuild: 0 none, 1 running, 2 done, 3 failed */
        Model &m = *scene.model;
        if (!loaded) g_flBase = flame_read(), loaded = true;
        heading("Hand adjust");
        hint("What you change here shows live over what flame_adjust.txt has already baked in. Save writes the file; Rebuild bakes it into 7120-7127 (about 10 s) and reloads.");
        int clip = flame_clip(m.motion);
        if (!clip) {
            hint("Play motion 51 (Fire Rocket), 63 (Knuckle Fire) or 64 (Express Claw) to turn its bones.");
        } else {
            static const char *BONES[20] = {"whole body (about the pelvis)", "0 (leg)", "1 (leg)", "2 (leg)", "3 (leg)", "4 (foot)", "5 (leg)", "6 (leg)", "7 (leg)", "8 (foot)", "9 (tail)",
                                            "10 (tail)", "11 (tail)", "12 (body)", "13 (chest)", "14 (head)", "15 (right arm)", "16 (right hand)", "17 (left arm)", "18 (left hand)"};
            hint("Body: this attack's %s. A bone turns with everything under it, in every frame", clip == 1 ? "Fire Rocket" : clip == 2 ? "Knuckle Fire" : "Express Claw");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##flbone", BONES[bone + 1])) {
                for (int i = -1; i < 19; i++)
                    if (ImGui::Selectable(BONES[i + 1], i == bone)) bone = i;
                ImGui::EndCombo();
            }
            std::array<float, 3> &a = g_flLive.bones[clip][bone];
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat3("##flrot", a.data(), 0.25f, -180, 180, "%.1f")) flame_sync(m);
            tip("Degrees about the model's x, y and z (applied as Rz*Ry*Rx), about the bone's own joint. Ctrl+click to type");
        }
        static const int FX[7] = {452, 453, 454, 455, 456, 457, 458};
        static const char *FXN[7] = {"452  Fire Rocket: dome", "453  Fire Rocket: spiral", "454  Fire Rocket: spiral", "455  Fire Rocket: big dome",
                                     "456  Knuckle Fire: fist flame (hand)", "457  Knuckle Fire: glow (hand)", "458  Knuckle Fire: ball"};
        hint("Effect: its place and size (the hand's two also turn with the hand)");
        int sel = fxId - 452;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##flfx", FXN[sel])) {
            for (int i = 0; i < 7; i++)
                if (ImGui::Selectable(FXN[i], i == sel)) fxId = FX[i];
            ImGui::EndCombo();
        }
        FlameFx &f = g_flLive.fx[fxId];
        bool hand = fxId == 456 || fxId == 457;
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::DragFloat3("##flpos", f.pos, 5, -4000, 4000, "%.0f");
        tip(hand ? "Move, in model units, along the hand's own axes" : "Move, in model units, in the fighter's space (PSX: y down)");
        if (hand) {
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::DragFloat3("##flfrot", f.rot, 0.25f, -180, 180, "%.1f");
            tip("Turn, in degrees, in the hand's frame, about the hand's joint");
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::DragFloat("##flsc", &f.scale, 0.01f, 0.05f, 6, "size x%.2f");
        tip("Size about the effect's own centre");
        int j = job.load();
        if (j == 1) hint("Rebuilding...");
        if (button("Reset", 0)) g_flLive = FlameAdj(), flame_sync(m);
        tip("Drop what is changed here (not what the file has)");
        ImGui::SameLine();
        if (button("Save", 0)) {
            bool ok = flame_write(g_flBase, g_flLive);
            note(ok ? NOTE_OK : NOTE_INFO, ok ? "Saved dw2-extractor/flame_adjust.txt" : "Couldn't write dw2-extractor/flame_adjust.txt");
        }
        tip("Writes the file: what it had, with this change on top");
        ImGui::SameLine();
        if (j != 1 && button("Save and rebuild", 1)) {
            if (flame_write(g_flBase, g_flLive)) {
                job = 1;
                std::string cmd = "\"cd /d \"" + std::filesystem::absolute(flame_dir()).make_preferred().string() + "\" && python flame_dcb.py > flame_build.log 2>&1\"";
                std::thread([cmd]() { job = std::system(cmd.c_str()) == 0 ? 2 : 3; }).detach();
            } else
                note(NOTE_INFO, "Couldn't write dw2-extractor/flame_adjust.txt");
        }
        tip("Saves, runs python flame_dcb.py (needs python on the PATH), and reloads the model with the change baked in");
        if (j >= 2) {
            job = 0;
            if (j == 3) note(NOTE_INFO, "The rebuild failed: see dw2-extractor/flame_build.log");
            else {
                g_flLive = FlameAdj(), g_flBase = flame_read();
                scene.model->adj.clear();
                std::string name = m.name;
                open_model(scene, view, name, scene.path, status, scene.fid, scene.fighter);
                note(NOTE_OK, "Rebuilt: 7120-7127 have the change");
            }
        }
    };
