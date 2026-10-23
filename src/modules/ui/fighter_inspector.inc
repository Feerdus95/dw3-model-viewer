    auto fighter_inspector = [&]() {
        if (scene.model && scene.model->dw2) {
            title("Digimon World 2");
            return hint("Its fighter (scripts, cameras, portraits) is the DW2003 version's: open it from the Digimon World 2 tab or from the list on the left.");
        }
        if (scene.fighters.empty()) {
            title("No fighter");
            return hint("No fighter uses this file in the fighters' file (SMDLDATA.PRO): it has no name, face, cameras or action scripts there.");
        }
        Model &m = *scene.model;
        const Fighter &f = *scene.fighters[scene.fighter];
        title("%s", f.name.c_str());
        chip(f.enemy ? "enemy" : "partner", f.enemy ? C_ERROR : C_OK);
        if (scene.fighters.size() > 1) {
            std::string cur = std::to_string(f.id) + " " + f.name;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##fighter", cur.c_str())) {
                for (int i = 0; i < (int)scene.fighters.size(); i++) {
                    std::string l = std::to_string(scene.fighters[i]->id) + " " + scene.fighters[i]->name +
                                    (scene.fighters[i]->enemy ? "  (enemy)" : "  (partner)");
                    if (ImGui::Selectable(l.c_str(), i == scene.fighter)) {
                        std::string name = m.name;
                        open_model(scene, view, name, scene.path, status, scene.fid, i);
                    }
                }
                ImGui::EndCombo();
            }
            tip("The fighters that use this file: each may name other entries");
        }
        const Fighter &cf = *scene.fighters[scene.fighter];
        int parts = 0;
        for (bool u : cf.faceUsed) parts += u;
        field("Fighter", "%d (kind 0x%X)", cf.id, cf.kind);
        field("Face", "%d parts", parts);
        if (cf.enemy && cf.cameraCount > 0) field("Battle view", "its partner's %d", cf.cameraCount);
        ImGui::Checkbox("Effect points", &view.marks);
        tip("A cross where each effect of a played script is (pink: a sprite, blue: an effect model), to see where they land on the model");
        heading("Cameras");
        if (button(IC_CAMERA "  Free camera", view.gameCamera < 0 ? 1 : 0)) frame_model(scene, view);
        tip("Orbit freely: dragging in the view leaves the game's camera too");
        static const char *CAMERA_GROUPS[3] = {"Battle views (the default view)", "Portraits", "Battle shots"};
        for (int g = 0; g < 3; g++) {
            bool any = false;
            for (int i = 0; i < (int)cf.cameras.size(); i++) any |= camera_role(cf.enemy, i).group == g;
            if (!any) continue;
            hint("%s", CAMERA_GROUPS[g]);
            for (int i = 0; i < (int)cf.cameras.size(); i++) {
                CameraRole r = camera_role(cf.enemy, i);
                if (r.group != g) continue;
                char l[80];
                snprintf(l, sizeof l, "%s  %s###cam%d", IC_CAMERA, r.name, i);
                if (ImGui::Selectable(l, view.gameCamera == i)) {
                    apply_camera(view, cf, i);
                    /* a partner's portraits show the pose the game gives them: its idle (the menu's), its weak idle (low HP) */
                    if (!cf.enemy && (i == 6 || i == 7) && m.motions.size() > 1) m.setMotion(i == 7 ? 2 : 1), view.playing = true;
                }
                tip("Camera %d: %s%s", i, r.what, !cf.enemy && i == 7 ? ". Plays the weak idle (motion 2)" : "");
            }
        }
        if (details("Technical details"))
            field("Entries", "bones %d, motions %d, scripts %d", cf.modelEntry, cf.motionsEntry, cf.effectsEntry);
    };

