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
static void apply_camera(View &v, const Fighter &f, int index) {
    const GameCamera &c = f.cameras[index];
    float home = (float)(0x1400 + f.distance) * (f.enemy ? -1 : 1);
    auto local = [&](const int16_t *p, float *out) {
        float x = p[0], y = p[1] - (float)f.height, z = p[2] - home;
        out[0] = f.enemy ? x : -x, out[1] = y, out[2] = f.enemy ? z : -z; /* a partner's turn: 0x800 about y */
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

static bool open_model(Scene &s, View &v, const std::string &name, const std::string &path, std::string &status,
                       int fid = -1, int fighter = 0) {
    std::vector<uint8_t> bytes;
    if (!read_file(path, bytes)) {
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
    s.selected = -1;
    upload(s);
    frame_model(s, v);
    v.texCombo = 0;
    v.ticks = 0;
    status = "";
    return true;
}

static void bone_tree(Scene &s, int b) {
    Model &m = *s.model;
    std::vector<int> kids;
    for (size_t c = 1; c < m.bones.size(); c++)
        if (m.bones[c].parent == b && (int)c != b) kids.push_back((int)c);
    char label[64];
    const Mesh *mesh = b ? &m.meshes[m.bones[b].mesh] : nullptr;
    if (b == 0)
        snprintf(label, sizeof label, "root");
    else
        snprintf(label, sizeof label, "bone %d  (%d polys)", b, (int)mesh->polys.size());
    ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (kids.empty()) fl |= ImGuiTreeNodeFlags_Leaf;
    if (b == s.selected) fl |= ImGuiTreeNodeFlags_Selected;
    ImGui::PushID(b);
    if (b) {
        bool shown = !s.hidden[b];
        if (ImGui::Checkbox("##v", &shown)) s.hidden[b] = !shown;
        ImGui::SameLine();
    }
    bool open = ImGui::TreeNodeEx(label, fl);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) s.selected = s.selected == b ? -1 : b;
    if (b && ImGui::IsItemHovered()) {
        const Bone &bone = m.bones[b];
        const Pose &p = m.pose[b];
        ImGui::SetTooltip("mesh entry %d, keys entry %d\n%d vertices, %d normals\npos %d %d %d\nrot %d %d %d\nscale %d %d %d",
                          mesh->entry, bone.keyEntry, (int)mesh->verts.size(), (int)mesh->normals.size(), p.pos.x, p.pos.y,
                          p.pos.z, p.rot.x, p.rot.y, p.rot.z, p.scale.x, p.scale.y, p.scale.z);
    }
    if (open) {
        for (int c : kids) bone_tree(s, c);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

