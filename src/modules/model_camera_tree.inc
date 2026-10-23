static void frame_model(Scene &s, View &v) {
    v.cam.target[0] = s.center[0], v.cam.target[1] = s.center[1], v.cam.target[2] = s.center[2];
    v.cam.dist = s.radius * 3.4f;
    v.fov = 0.8f;
    v.gameCamera = -1;
}

/* A fighter's camera: at camPos looking at camRef, in the battle's space.
   The game negates their y and z into the PSX's (FIGHTSTG_getFighterView),
   which the viewer's own flip takes back. There the fighter stands at its
   side's home (FIGHTSTG_faceModel): a partner 0x1400 + distance towards the
   viewer's +z, turned round to face the enemy, an enemy as far the other way;
   both raised by height. The viewer's model stands at the origin, so the
   camera comes into its space. Its projection is on a 240-line screen. */
static void battle_to_view(const Fighter &f, const float b[3], float out[3]) {
    float home = (float)(0x1400 + f.distance) * (f.enemy ? -1 : 1);
    float x = b[0], y = b[1] - (float)f.height, z = b[2] - home;
    out[0] = f.enemy ? x : -x, out[1] = y, out[2] = f.enemy ? z : -z; /* a partner's turn: 0x800 about y */
}

/* battle_to_view the other way: a point in the viewer's space, in fighter F's battle space */
static void view_to_battle(const Fighter &f, const float v[3], float out[3]) {
    float home = (float)(0x1400 + f.distance) * (f.enemy ? -1 : 1);
    out[0] = f.enemy ? v[0] : -v[0], out[1] = v[1] + (float)f.height, out[2] = f.enemy ? v[2] + home : -v[2] + home;
}

/* What a fighter's camera is for, by where the game looks through it: the
   battle's own view (FIGHTSTG_getEnemyView, which the scripts' camera 0, the
   idle between actions and the ends of the entrances and Digimon changes go
   back to) is the PARTNER's camera numbered the enemy's camera count - 1, so
   its first six are that view for enemies of 1 to 6 cameras; then the
   scripts' camera command (a partner's shots 1-4 are its cameras 8-11, an
   enemy's 1-3 its cameras 0-2), the portrait (6, or 7 while it is weak), a
   Digimon change (9) and an entrance (a partner's 10, an enemy's 2) */
struct CameraRole {
    const char *name, *what;
    int group; /* 0 battle views, 1 portraits, 2 battle shots */
};
static CameraRole camera_role(bool enemy, int i) {
    if (enemy) {
        switch (i) {
        case 0: return {"Shot 1", "Script camera 5, and a view the battle's own shots use", 2};
        case 1: return {"Shot 2", "Script camera 6", 2};
        case 2: return {"Shot 3, entrance", "Script camera 7; also the camera on it as it enters", 2};
        default: return {"Unused", "Nothing in the game looks through it", 2};
        }
    }
    static const char *VIEW_NAME[6] = {"Battle view 1", "Battle view 2", "Battle view 3", "Battle view 4", "Battle view 5", "Battle view 6"};
    static const char *VIEW_WHAT[6] = {
        "The battle's default view when the enemy has 1 camera: what the game looks through while it waits for the next action",
        "The battle's default view when the enemy has 2 cameras (the most common)",
        "The battle's default view when the enemy has 3 cameras",
        "The battle's default view when the enemy has 4 cameras",
        "The battle's default view when the enemy has 5 cameras",
        "The battle's default view when the enemy has 6 cameras"};
    switch (i) {
    case 0: case 1: case 2: case 3: case 4: case 5: return {VIEW_NAME[i], VIEW_WHAT[i], 0};
    case 6: return {"Portrait", "The 3D portrait in the battle menu", 1};
    case 7: return {"Portrait, low HP", "The portrait while it is in its weak idle (a quarter of its HP or less)", 1};
    case 8: return {"Shot 1", "Script camera 1; also a view the battle's own shots use", 2};
    case 9: return {"Shot 2, Digimon change", "Script camera 2; also the camera of a Digimon change", 2};
    case 10: return {"Shot 3, entrance", "Script camera 3; also the camera on it as it enters", 2};
    case 11: return {"Shot 4", "Script camera 4", 2};
    default: return {"Unused", "Nothing in the game looks through it", 0};
    }
}

/* Looks through camera C as fighter F's frame has it; INDEX is the fighter's camera it is, or -1 (a candidate) */
static void apply_game_camera(View &v, const Fighter &f, const GameCamera &c, int index) {
    auto local = [&](const int16_t *p, float *out) {
        float b[3] = {(float)p[0], (float)p[1], (float)p[2]};
        battle_to_view(f, b, out);
    };
    float eye[3], ref[3];
    local(c.pos, eye);
    local(c.ref, ref);
    float d[3] = {eye[0] - ref[0], eye[1] - ref[1], eye[2] - ref[2]};
    float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len < 1) return;
    for (int i = 0; i < 3; i++) v.cam.target[i] = ref[i];
    v.cam.dist = len;
    v.cam.pitch = asinf(d[1] / len);
    v.cam.yaw = atan2f(d[0], d[2]);
    if (c.proj > 0) v.fov = 2 * atanf(120.0f / c.proj);
    v.gameCamera = index;
}
static void apply_camera(View &v, const Fighter &f, int index) { apply_game_camera(v, f, f.cameras[index], index); }

static std::string g_mods; /* the port's mods folder: a model saved there is the one opened */

static bool open_model(Scene &s, View &v, const std::string &name, const std::string &path, std::string &status,
                       int fid = -1, int fighter = 0) {
    std::vector<uint8_t> bytes;
    std::string use = path;
    if (fid > 0 && !g_mods.empty()) {
        char b[16];
        snprintf(b, sizeof b, "/%04d.bin", fid);
        if (SDL_GetPathInfo((g_mods + b).c_str(), nullptr)) use = g_mods + b;
    }
    if (!read_file(use, bytes)) {
        status = "can't read " + path;
        return false;
    }
    std::vector<const Fighter *> fighters = g_game.forFile(fid);
    fighter = fighters.empty() ? 0 : std::clamp(fighter, 0, (int)fighters.size() - 1);
    auto m = std::make_unique<Model>();
    if (!m->load(name, bytes, fighters.empty() ? nullptr : fighters[fighter])) {
        /* a fighter whose entries don't read: the file on its own */
        m = std::make_unique<Model>();
        fighters.clear();
        fighter = 0;
        if (!m->load(name, std::move(bytes))) {
            status = name + ": " + m->error;
            return false;
        }
    }
    s.fid = fid, s.path = path, s.fighters = fighters, s.fighter = fighter;
    s.model = std::move(m);
    if (s.model->dw2) s.model->dw2_load_tuning(dw2_tuning_path(path));
    s.selected = -1;
    upload(s);
    frame_model(s, v);
    v.texCombo = 0;
    v.ticks = 0;
    status = "";
    return true;
}

/* The bones as a tree: an eye to show or hide each, a click to select it,
   HOVER the one under the mouse, MENU its right-click menu's items; SCROLL
   brings the selected one into sight */
static void bone_tree(Scene &s, int b, int &hover, bool scroll, const std::function<void(int)> &menu) {
    Model &m = *s.model;
    std::vector<int> kids;
    for (size_t c = 1; c < m.bones.size(); c++)
        if (m.bones[c].parent == b && (int)c != b) kids.push_back((int)c);
    const Mesh *mesh = b ? &m.meshes[m.bones[b].mesh] : nullptr;
    ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (kids.empty()) fl |= ImGuiTreeNodeFlags_Leaf;
    if (b && b == s.selected) fl |= ImGuiTreeNodeFlags_Selected;
    ImGui::PushID(b);
    if (b) {
        bool shown = !s.hidden[b];
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(shown ? ImGuiCol_Text : ImGuiCol_TextDisabled));
        if (ImGui::SmallButton(shown ? IC_EYE : IC_HIDE)) s.hidden[b] = shown;
        ImGui::PopStyleColor(2);
        tip(shown ? "Shown: click to hide it (H)" : "Hidden: click to show it");
        ImGui::SameLine();
    }
    bool open = ImGui::TreeNodeEx(b ? "##bone" : "root", fl, b ? "bone %d" : "root", b);
    if (b && b == s.selected && scroll) ImGui::SetScrollHereY();
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && b) s.selected = s.selected == b ? -1 : b;
    if (b && ImGui::IsItemHovered()) {
        hover = b;
        const Bone &bone = m.bones[b];
        const Pose &p = m.pose[b];
        ImGui::SetItemTooltip("bone %d\nmesh entry %d, keys entry %d\n%d vertices, %d normals\npos %d %d %d\nrot %d %d %d\nscale %d %d %d",
                              b, mesh->entry, bone.keyEntry, (int)mesh->verts.size(), (int)mesh->normals.size(), p.pos.x,
                              p.pos.y, p.pos.z, p.rot.x, p.rot.y, p.rot.z, p.scale.x, p.scale.y, p.scale.z);
    }
    if (b && ImGui::BeginPopupContextItem("##menu")) {
        menu(b);
        ImGui::EndPopup();
    }
    if (b) {
        ImGui::SameLine();
        ImGui::TextDisabled("%d polys", (int)mesh->polys.size());
    }
    if (open) {
        for (int c : kids) bone_tree(s, c, hover, scroll, menu);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

