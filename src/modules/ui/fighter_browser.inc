    /* ===== Fighter ===== */
    auto fighter_browser = [&]() {
        Model &mm = *scene.model;
        if (mm.dw2) {
            hint("A DW2 model has no action scripts or portraits of its own: the DW2003 version does.");
            if (button(IC_PLAY "  Open the DW2003 version", 1)) dw2_open_converted(scene.path);
            return;
        }
        if (scene.fighters.empty()) return hint("No fighter uses this file: it has no action scripts.");
        const Fighter &f = *scene.fighters[scene.fighter];
        hint("Play a script to see it as the battle runs it. Click a line to play its motion, look through its camera or see its effect.");
        if (!ownIds.empty()) {
            ImGui::Checkbox("Show the own images (mod)", &shotMods);
            tip("A script's sprite effects play the effect's own images (%d effects have them in the mods folder) instead of the original's; off, the original's", (int)ownIds.size());
        } else {
            hint("No effect has own images in the mods folder, so a script plays the original sprites.");
        }
        ImGui::Checkbox("Keep the game's camera", &lockCam);
        tip("While a script plays the view stays where the battle puts the camera (its default view, then the script's cameras). Off, you can move around it: a sprite effect then shows from where you look, not as the game shows it");
        b_source();
#include "fighter_candidate.inc"
#include "fighter_choreography.inc"
        if (run.on && button(IC_PAUSE "  Stop the script")) run.on = false, free_fx_models();
        ImGui::BeginChild("scripts");
        for (int g = 0; g < 4; g++) {
            bool titled = false;
            for (int i = 0; i < (int)mm.scripts.size(); i++) {
                const std::vector<ScriptLine> &sc = mm.scripts[i];
                ScriptRole role = script_role(i);
                if (role.group != g) continue;
                if (!titled) heading(SCRIPT_GROUPS[g]), titled = true;
                char h[96];
                if (*role.name) snprintf(h, sizeof h, "%s  #%d###script%d", role.name, i, i);
                else snprintf(h, sizeof h, "Script %d###script%d", i, i);
                bool open = ImGui::CollapsingHeader(h);
                tip("%s%s%d commands", role.examples, *role.examples ? ".  " : "", (int)sc.size());
                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem(IC_PLAY "  Play the script")) run_script(i);
                    ImGui::EndPopup();
                }
                if (!open) continue;
                ImGui::PushID(i);
                if (button(run.on && run.script == i ? IC_RESTART "  Play again" : IC_PLAY "  Play the script")) run_script(i);
                tip("Runs its commands in order: motions, waits, cameras and sprite effects");
                if (std::any_of(sc.begin(), sc.end(), [](const ScriptLine &l) { return l.fxOwn; })) {
                    /* its "own" sprite effect is the technique's: TechData.scriptEffect */
                    const TechRow *cur = own_tech(i);
                    char prev[160];
                    snprintf(prev, sizeof prev, "%s  (effect %d)", cur ? cur->name.c_str() : "Fight command", own_effect_id(i));
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::BeginCombo("##owntech", prev)) {
                        if (i == 5 && ImGui::Selectable("Fight command  (effect 2)", !cur)) ownTech = 0;
                        for (const TechRow &t : g_techs) {
                            if (t.script != i || t.name.size() < 2) continue;
                            char l[160];
                            snprintf(l, sizeof l, "%s  (effect %d)##t%d", t.name.c_str(), t.effect, t.id);
                            if (ImGui::Selectable(l, cur == &t)) ownTech = t.id;
                        }
                        ImGui::EndCombo();
                    }
                    tip("The technique whose look this script plays: its \"own\" sprite effect is the one the technique names");
                }
                for (int k = 0; k < (int)sc.size(); k++) {
                    const ScriptLine &ln = sc[k];
                    ImGui::PushID(k);
                    bool active = ln.motion > 0 || ln.camera >= 0 || ln.effect >= 0 || ln.camMode == 2;
                    bool now = run.on && run.script == i && k == run.pc - 1;
                    if (!active) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    if (ImGui::Selectable(ln.text.c_str(), now) && active) {
                        if (ln.motion > 0 && ln.motion <= (int)mm.motions.size()) mm.setMotion(ln.motion), view.playing = true;
                        if (ln.effect >= 0) pick_effect(ln.effect, true);
                        int c = ln.camera >= 100 ? ln.camera - 100 : ln.camera;
                        if (c >= 0 && c < (int)f.cameras.size() && (ln.camera >= 100) == f.enemy) {
                            if (cand.on) apply_game_camera(view, f, cand.c, -1);
                            else apply_camera(view, f, c);
                        } else if (ln.camMode == 2) {
                            apply_cam_words(view, f, ln.camRaw, -1); /* an explicit camera, as it is once its fade is over */
                        }
                    }
                    if (!active) ImGui::PopStyleColor();
                    if (active) tip("%s", ln.motion > 0 ? "Plays this motion" : ln.effect >= 0 ? "Shows this effect (in Effects)" : "Looks through this camera");
                    if (ImGui::BeginPopupContextItem()) {
                        if (ImGui::MenuItem(IC_COPY "  Copy the command")) ImGui::SetClipboardText(ln.text.c_str());
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    };
