    auto props_panel = [&]() {
        char head[48];
        snprintf(head, sizeof head, "Props (%d)", (int)props.size());
        heading(head);
        if (button(IC_STAGE "  Catalog...")) pcatOpen = true;
        same_line_if_fits("Select");
        if (ImGui::Checkbox("Select", &selectMode)) marquee = false;
        tip("Drag a box over the view to pick every prop in it; then move, copy (Ctrl+C, Ctrl+V) or delete them together. Shift adds, Ctrl toggles. Ctrl+A picks all.");
        tip("Grass, flowers, petals and models as thumbnails: pick one, then click in the stage to place it");
        if (button(IC_ADD "  Billboard...")) pick_prop(0);
        tip("A PNG standing upright in the stage (grass, a flower, a rock), facing the camera. Its transparency cuts it out.");
        same_line_if_fits(IC_ADD "  Emitter...");
        if (button(IC_ADD "  Emitter...")) pick_prop(1);
        tip("A PNG falling through an area, like the forest's leaves: petals, snow, embers");
        same_line_if_fits(IC_ADD "  Model...");
        if (button(IC_ADD "  Model...")) pick_prop(2);
        tip("A small 3D model from an OBJ (a rock, a signpost, a tree) with a PNG texture; if the OBJ's MTL names one, it comes along");
        if (ImGui::Checkbox("Outline all", &propOutlines)) {}
        tip("Off: only the picked prop shows its outline, so the ground stays clear. On: every billboard and emitter shows its box.");
        same_line_if_fits("Original stage");
        if (ImGui::Checkbox("Original stage", &showOrig)) {
            if (showOrig) stage_show(stageDisc, false);
            else stage_rebuild();
        }
        tip("Shows the disc's stage instead of yours, to see where its trees (or anything you took out) stood. The props stay. Editing a part brings yours back.");
        if (!props.empty()) {
            float lh = ImGui::GetTextLineHeightWithSpacing();
            if (ImGui::BeginChild("##props", ImVec2(0, std::min((int)props.size(), 6) * lh + 8 * dpi), ImGuiChildFlags_Borders)) {
                ImGuiListClipper clip;
                clip.Begin((int)props.size());
                while (clip.Step())
                    for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
                        const StageProp &p = props[i];
                        ImGui::PushID(i);
                        std::string l = std::string(p.model ? "Model  " : p.emitter ? "Emitter  " : "Billboard  ") + (p.model ? p.obj : p.image);
                        if (ImGui::Selectable(l.c_str(), i == propSel || (propSet.size() > 1 && propSet.count(i)))) {
                            std::set<int> g = ImGui::GetIO().KeyCtrl ? cur_props() : std::set<int>();
                            if (!g.erase(i)) g.insert(i);
                            set_props(g);
                        }
                        ImGui::PopID();
                    }
            }
            ImGui::EndChild();
        }
        /* what all the props share: undo gets the list's settings as they were */
        PropsMeta mbefore = propsMeta;
        auto me = [&](bool changed) {
            if (ImGui::IsItemActivated()) stage_mark(), stageHist.undo.back().meta = mbefore;
            if (changed) stageDirty = true;
        };
        if (ImGui::TreeNode("Wind and fog for all of them")) {
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
            me(ImGui::DragFloat("Wind", &propsMeta.wind, 0.01f, 0, 5, "%.2f"));
            tip("How hard the wind blows on the props, as a factor of the stage's own: 0 none, 1 as the trees, 2 a storm");
            ImGui::Checkbox("Play effects", &propWind);
            tip("Only in the view: the wind bends what sways and the particles fall, as they do in the battle. Off: everything stands still.");
            {
                bool own = propsMeta.leaves >= 0;
                me(ImGui::Checkbox("Own leaves", &own));
                if (own != (propsMeta.leaves >= 0)) propsMeta.leaves = own ? 15 : -1, stageDirty = true;
                tip("Leaves falling through the whole arena, in the stage's greens and browns. Off: the ones stages.ini gives the stage, if any (the view can't show those).");
                if (propsMeta.leaves >= 0) me(ImGui::DragInt("Leaves", &propsMeta.leaves, 0.5f, 0, 5000));
            }
            me(ImGui::Checkbox("Own fog", &propsMeta.fog));
            tip("Off: the props take the stage's fog, as its trees do. On: a fog of their own that you set here.");
            if (propsMeta.fog) {
                float c[3] = {propsMeta.fogColor[0] / 255, propsMeta.fogColor[1] / 255, propsMeta.fogColor[2] / 255};
                bool cc = ImGui::ColorEdit3("Fog color", c, ImGuiColorEditFlags_NoInputs);
                me(cc);
                if (cc)
                    for (int k = 0; k < 3; k++) propsMeta.fogColor[k] = (float)lroundf(c[k] * 255);
                me(ImGui::DragFloat("Fog amount", &propsMeta.fogAmount, 0.005f, 0, 1, "%.2f"));
                tip("How much of the fog's color the farthest props take");
                me(ImGui::DragFloat("Fog starts", &propsMeta.fogFrom, 0.005f, 0, 0.99f, "%.2f"));
                tip("Where it begins, as a part of the way to the farthest thing drawn");
                hint("The view's fog is approximate: the game measures it from its own camera and the stage's farthest point.");
            }
            ImGui::PopItemWidth();
            ImGui::TreePop();
        }
        if (propSet.size() > 1) {
            ImGui::PushFont(g_fontBold);
            ImGui::TextColored(C_SELECT, "%d props picked", (int)propSet.size());
            ImGui::PopFont();
            hint("Drag the arrows to move them all, the box scales them. Ctrl+C then Ctrl+V puts a copy where the mouse is on the ground; Del removes them.");
            auto each = [&](auto f) {
                for (int i : propSet)
                    if (i >= 0 && i < (int)props.size()) f(props[i]);
            };
            if (ImGui::Button("Smaller")) stage_mark(), each([](StageProp &p) { (p.emitter ? p.radius : p.scale) *= 0.9f; }), stageDirty = true;
            ImGui::SameLine();
            if (ImGui::Button("Bigger")) stage_mark(), each([](StageProp &p) { (p.emitter ? p.radius : p.scale) *= 1.1f; }), stageDirty = true;
            tip("Every one 10% smaller or bigger, each about its own foot");
            static bool marked = false;
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
            float sw = props[propSel].sway, sh = props[propSel].tint[0] / 255.0f;
            bool ch = ImGui::DragFloat("Sway", &sw, 0.01f, 0, 3, "%.2f");
            if (ch) {
                if (!marked) stage_mark(), marked = true;
                each([&](StageProp &p) { if (!p.emitter) p.sway = sw; }), stageDirty = true;
            }
            if (ImGui::IsItemDeactivated()) marked = false;
            tip("How far their tips swing in the wind, set on all of them");
            ch = ImGui::DragFloat("Shade", &sh, 0.01f, 0, 1.5f, "%.2f");
            if (ch) {
                if (!marked) stage_mark(), marked = true;
                each([&](StageProp &p) { for (float &c : p.tint) c = std::clamp(roundf(255 * sh), 0.0f, 255.0f); }), stageDirty = true;
            }
            if (ImGui::IsItemDeactivated()) marked = false;
            tip("Their tint, 1 as the picture is");
            ImGui::PopItemWidth();
            if (ImGui::Button(IC_ADD "  Duplicate")) prop_duplicate(propSel);
            tip("Ctrl+D");
            ImGui::SameLine();
            if (button(IC_DELETE "  Delete", 2)) prop_delete(propSel);
            tip("Del; Ctrl+Z brings them back");
            return;
        }
        if (propSel < 0 || propSel >= (int)props.size()) {
            hint("Click one in the view or in the list. Drag its arrows to place it anywhere; the box scales it. "
                 "Save to game writes %s", nice_path(props_dir(stageId) + "/props.json").c_str());
            return;
        }
        StageProp &p = props[propSel];
        StageProp before = p;
        ImGui::PushFont(g_fontBold);
        ImGui::TextColored(C_SELECT, "%s", (p.model ? p.obj : p.image).c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextDisabled(p.model ? "model" : p.emitter ? "emitter" : "billboard");
        if (p.image.empty() || !prop_img(p.image).tex) chip(IC_WARN " texture missing", C_WARN);
        if (p.model && prop_mesh(p.obj).v.empty()) chip(IC_WARN " OBJ unreadable", C_WARN);
        /* a field: undo gets the prop as it was when the drag or typing began */
        auto pe = [&](bool changed) {
            if (ImGui::IsItemActivated()) stage_mark(), stageHist.undo.back().props[propSel] = before;
            if (changed) stageDirty = true;
        };
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
        pe(ImGui::DragFloat("X", &p.pos[0], 10, -40000, 40000, "%.0f"));
        tip("Left and right, in the game's units. In the view: drag the red arrow.");
        pe(ImGui::DragFloat("Y", &p.pos[1], 10, -40000, 40000, "%.0f"));
        tip("Up (0 is the ground). In the view: the green arrow.");
        pe(ImGui::DragFloat("Z", &p.pos[2], 10, -40000, 40000, "%.0f"));
        tip("Depth, as the port reads it (the view's blue arrow goes the other way). The arena's middle is 0, 0, 0.");
        if (p.emitter) {
            pe(ImGui::DragInt("Count", &p.count, 0.5f, 1, 5000));
            pe(ImGui::DragFloat("Radius", &p.radius, 10, 50, 40000, "%.0f"));
            tip("The area's radius around the point. In the view: the box.");
            pe(ImGui::DragFloat("Height", &p.top, 10, 50, 40000, "%.0f"));
            pe(ImGui::DragFloat("Fall", &p.fall, 0.01f, 0, 5, "%.2f"));
            tip("How fast they fall; 0 floats");
            pe(ImGui::DragFloat("Wind", &p.wind, 0.01f, 0, 5, "%.2f"));
            tip("How much the stage's wind carries them");
            pe(ImGui::DragFloat("Smallest", &p.size[0], 1, 1, 2000, "%.0f"));
            pe(ImGui::DragFloat("Biggest", &p.size[1], 1, 1, 2000, "%.0f"));
        } else {
            if (!p.model) {
                pe(ImGui::DragFloat("Width", &p.size[0], 5, 1, 20000, "%.0f"));
                pe(ImGui::DragFloat("Height", &p.size[1], 5, 1, 20000, "%.0f"));
            }
            pe(ImGui::DragFloat("Scale", &p.scale, p.model ? std::max(0.05f, p.scale * 0.005f) : 0.01f, 0.01f, 100000, "%.2f")); /* a model's step follows its size: a stage is thousands of units wide */
            tip("In the view: the box.");
            pe(ImGui::DragFloat("Sway", &p.sway, 0.01f, 0, 3, "%.2f"));
            tip("How much the wind bends it, from its foot to its top (the view shows it with Play effects); 0 stands still");
            if (!p.model) {
                pe(ImGui::Checkbox("Flat in the world", &p.fixed));
                tip("Instead of facing the camera, a plane turned about the vertical: a path, a puddle, a banner");
            }
            if (p.fixed || p.model) pe(ImGui::DragFloat("Turn", &p.rot, 0.5f, -180, 180, "%.1f deg"));
        }
        pe(ImGui::DragFloat("Fog", &p.fog, 0.01f, 0, 1, "%.2f"));
        tip("How much of the fog it takes: 1 as the rest, 0 none (a lantern, a glow)");
        float t[3] = {p.tint[0] / 255, p.tint[1] / 255, p.tint[2] / 255};
        bool tc = ImGui::ColorEdit3("Tint", t, ImGuiColorEditFlags_NoInputs);
        pe(tc);
        if (tc)
            for (int k = 0; k < 3; k++) p.tint[k] = (float)lroundf(t[k] * 255);
        ImGui::PopItemWidth();
        if (ImGui::Button(IC_FIT "  Frame")) {
            float h = p.emitter ? 0 : p.model ? prop_mesh(p.obj).h * p.scale : p.size[1] * p.scale;
            view.cam.target[0] = p.pos[0], view.cam.target[1] = p.pos[1] + h / 2, view.cam.target[2] = -p.pos[2];
            if (!p.emitter) view.cam.dist = std::max(h * 3, 600.0f);
            view.gameCamera = -1;
        }
        same_line_if_fits(IC_IMPORT "  Texture...");
        if (ImGui::Button(IC_IMPORT "  Texture...")) pick_prop_tex();
        tip("Another PNG for it, copied beside props.json");
        same_line_if_fits(IC_ADD "  Duplicate");
        if (ImGui::Button(IC_ADD "  Duplicate")) prop_duplicate(propSel);
        tip("Ctrl+D");
        same_line_if_fits(IC_DELETE "  Delete");
        if (button(IC_DELETE "  Delete", 2)) prop_delete(propSel);
        tip("Del; Ctrl+Z brings it back");
    };

