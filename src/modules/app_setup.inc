int main(int argc, char **argv) {
    std::string shot, open_path, data;
    std::string start_obj; /* an OBJ to import once the model is open */
    int shot_motion = 1, shot_key = 0, shot_cam = -1, shot_stage = 0, shot_fighter = 0;
    std::vector<int> shot_frame; /* bones the picture is framed on */
    std::vector<int> shot_only;  /* the only bones drawn */
    float shot_zoom = 1;
    bool shot_wire = false;
    float shot_yaw = 1e9f;       /* degrees, around the model */
    bool check = false, users = false, stages = false, seams = false;
    int effect = -1;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--shot" && i + 1 < argc) shot = argv[++i];
        else if (a == "--motion" && i + 1 < argc) shot_motion = atoi(argv[++i]);
        else if (a == "--key" && i + 1 < argc) shot_key = atoi(argv[++i]);
        else if (a == "--cam" && i + 1 < argc) shot_cam = atoi(argv[++i]);
        else if (a == "--frame" && i + 1 < argc)
            for (const char *c = argv[++i]; *c; c += *c == ',') shot_frame.push_back(atoi(c)), c += strspn(c, "0123456789");
        else if (a == "--only" && i + 1 < argc)
            for (const char *c = argv[++i]; *c; c += *c == ',') shot_only.push_back(atoi(c)), c += strspn(c, "0123456789");
        else if (a == "--wire") shot_wire = true;
        else if (a == "--zoom" && i + 1 < argc) shot_zoom = (float)atof(argv[++i]);
        else if (a == "--yaw" && i + 1 < argc) shot_yaw = (float)atof(argv[++i]);
        else if (a == "--stage" && i + 1 < argc) shot_stage = atoi(argv[++i]);
        else if (a == "--fighter" && i + 1 < argc) shot_fighter = atoi(argv[++i]);
        else if (a == "--import-obj" && i + 1 < argc) start_obj = argv[++i];
        else if (a == "--data" && i + 1 < argc) data = argv[++i];
        else if (a == "--check") check = true;
        else if (a == "--stages") stages = true;
        else if (a == "--seams") seams = true;
        else if (a == "--effect" && i + 1 < argc) effect = atoi(argv[++i]);
        else if (a == "--effect-users") users = true;
        else open_path = a;
    }
    const char *base = SDL_GetBasePath();
    g_exe_dir = base ? base : "";
    find_data(data);

    if (check || users || stages || seams || !shot.empty() || effect >= 0) attach_console();
    if (stages) return check_stages();
    if (seams) return check_seams(open_path);
    if (users) {
        std::map<int, std::set<std::string>> by = effect_users();
        for (int id : effect_ids()) {
            printf("%4d %2d", id, (int)by[id].size());
            for (const std::string &n : by[id]) printf(" %s", n.c_str());
            printf("\n");
        }
        return 0;
    }
    if (effect >= 0) {
        Effect fx;
        if (!fx.load(effect, g_data)) {
            fprintf(stderr, "%s\n", fx.error.c_str());
            return 1;
        }
        std::string dir;
        int n = export_effect(fx, dir);
        printf("effect %d: %d animations, %d fields -> %d frames in %s\n", effect, (int)fx.anims.size(), fx.duration, n, dir.c_str());
        return 0;
    }
    if (check) {
        std::vector<Entry> list = scan_models();
        int bad = 0, emStarts = 0, emBad = 0, screenFx = 0, ownFx = 0;
        std::set<int> emSeen;
        for (const Entry &e : list) {
            std::vector<uint8_t> bytes;
            read_file(fid_path(e.fid), bytes);
            std::vector<const Fighter *> fs = g_game.forFile(e.fid);
            Model m;
            if (!m.load(e.name, std::move(bytes), fs.empty() ? nullptr : fs[0])) {
                printf("%04d %-13s ERROR %s\n", e.fid, e.name.c_str(), m.error.c_str());
                bad++;
                continue;
            }
            for (const std::vector<ScriptLine> &sc : m.scripts)
                for (const ScriptLine &ln : sc) {
                    int16_t z = (int16_t)ln.pos[2];
                    screenFx += ln.fxStart && (z == 0x7FFF || z == -1), ownFx += ln.fxStart && ln.fxOwn;
                    if (!ln.emStart) continue;
                    emStarts++;
                    if (!emSeen.insert(ln.emId).second) continue;
                    const EffectModelRow *row = nullptr;
                    for (const EffectModelRow &r : EFFECT_MODELS)
                        if (r.id == ln.emId) row = &r;
                    Model em;
                    std::vector<uint8_t> eb;
                    if (!row) { /* the battle ends such a task at once */
                        printf("%04d effect model %d: not in the table, the game skips it\n", e.fid, ln.emId);
                    } else if (!read_file(fid_path((int)(row->file >> 16)), eb) ||
                               !em.load("em", std::move(eb), nullptr, (int)(row->file & 0xFFFF), (int)(row->motions & 0xFFFF)) || em.motions.empty()) {
                        printf("%04d effect model %d: unreadable or no motions\n", e.fid, ln.emId);
                        emBad++;
                    }
                }
            size_t polys = 0;
            for (const Mesh &me : m.meshes) polys += me.polys.size();
            printf("%04d %-13s bones %3d polys %5d tims %d motions %2d scripts %2d  %s\n", e.fid, e.name.c_str(),
                   (int)m.bones.size() - 1, (int)polys, (int)m.tims.size(), (int)m.motions.size(), (int)m.scripts.size(),
                   e.label.c_str());
        }
        printf("scripts: %d effect model starts (%d distinct, %d unreadable), %d sprite effects at a screen position, %d own\n", emStarts,
               (int)emSeen.size(), emBad, screenFx, ownFx);
        printf("%d models, %d unread\n", (int)list.size(), bad);
        return bad != 0 || emBad != 0;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return fatal(SDL_GetError());
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    cfg_load();
    SDL_Window *win = SDL_CreateWindow("DW3 Model Viewer", g_cfg.w, g_cfg.h,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                           (shot.empty() ? 0 : SDL_WINDOW_HIDDEN));
    if (!win) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        win = SDL_CreateWindow("DW3 Model Viewer", 1440, 900, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    }
    if (win && shot.empty()) { /* back where it was, if that is still on a screen */
        if (g_cfg.hasPos) {
            SDL_Point p = {g_cfg.x, g_cfg.y};
            if (SDL_GetDisplayForPoint(&p)) SDL_SetWindowPosition(win, g_cfg.x, g_cfg.y);
        }
        if (g_cfg.max) SDL_MaximizeWindow(win);
    }
    SDL_GLContext ctx = win ? SDL_GL_CreateContext(win) : nullptr;
    if (!ctx || !gl_load()) {
        return fatal((std::string("This needs a graphics card with OpenGL 3.3.\n") + SDL_GetError()).c_str());
    }
    SDL_GL_SetSwapInterval(1);
    g_mesh = compile(MESH_VS, MESH_FS);
    g_line = compile(LINE_VS, LINE_FS);
    g_sprite = compile(SPRITE_VS, SPRITE_FS);
    if (!g_mesh || !g_line || !g_sprite) return 1;
    GL(glGenVertexArrays)(1, &g_lineVao);
    GL(glGenBuffers)(1, &g_lineVbo);
    GL(glBindVertexArray)(g_lineVao);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, g_lineVbo);
    GL(glEnableVertexAttribArray)(0);
    GL(glEnableVertexAttribArray)(1);
    GL(glVertexAttribPointer)(0, 3, GL_FLOAT, GL_FALSE, 28, (void *)0);
    GL(glVertexAttribPointer)(1, 4, GL_FLOAT, GL_FALSE, 28, (void *)12);

    Scene scene;
    View view;
    std::string status;
    bool aboutOpen = false;
    GLuint aboutIcon = 0;
    std::vector<Entry> list;
    bool listed = false;
    auto open_entry = [&](const std::string &path, int fighter) {
        std::string name = path.substr(path.find_last_of("/\\") + 1);
        int fid = -1;
        /* a fid's file goes by its disc name */
        if (name.size() == 8 && name.substr(4) == ".bin" && atoi(name.c_str()) > 0) {
            fid = atoi(name.c_str());
            name = fid_name(fid);
        }
        if (open_model(scene, view, name, path, status, fid, fighter)) view.stageView = false;
    };
    if (!open_path.empty()) open_entry(open_path, shot_fighter);

    /* the Effects tab: a sprite effect on a 320x240 screen, its origin in the middle */
    Effect fx;
    std::vector<int> fxIds = effect_ids();
    std::vector<uint32_t> fxPx(FXC_W * FXC_H);
    bool fxFitPending = false; /* fit the view to the effect when next drawn */
    float fxZoom = 2, fxPan[2] = {0, 0}; /* screen pixels per effect pixel; the view's middle from the point */
    GLuint fxTex = 0;
    float fxTime = 0;
    int fxDrawn = -1;
    bool fxPlaying = true, fxLoop = true, fxSelect = false;
    std::string fxDir; /* the last export's folder */
    /* its own images for the native port (custom.h), in <root>/<id>/ (the
       port's data/mods/effects, kept in effects_dir.txt by the exe) */
    CustomEffect own;
    bool ownShow = false, ownCritical = false, ownDirty = false;
    std::string ownRoot = g_data + "/../mods/effects";
    {
        std::ifstream f(g_exe_dir + "effects_dir.txt");
        std::string line;
        if (f && std::getline(f, line) && !line.empty()) ownRoot = line;
        else /* the port's, beside the viewer's folder (both built from their sources) */
            for (const char *dir : {"../../native/data/mods/effects", "../native/data/mods/effects"})
                if (SDL_GetPathInfo((g_exe_dir + dir).c_str(), nullptr)) {
                    ownRoot = g_exe_dir + dir;
                    break;
                }
    }
    {
        size_t e = ownRoot.find_last_of("/\\");
        g_mods = e == std::string::npos ? ownRoot : ownRoot.substr(0, e);
    }
    struct Picked { /* what the file dialogs gave, maybe from another thread */
        std::mutex m;
        std::vector<std::string> files;
        std::string folder, data, obj, bin;
    } picked;
    picked.obj = start_obj;
    std::set<int> ownIds = own_effects(ownRoot); /* the effects with own images in ownRoot */
    read_techs({g_data + "/../SLES_039.36", ownRoot + "/../../SLES_039.36"});

    /* the Stages tab: a fight stage, edited (stage.h), saved for the native
       port in its data/mods (ownRoot's parent): <fid>.bin and the stages'
       table, 0459.bin */
    std::vector<StageInfo> stageInfos; /* the disc's table */
    std::set<int> stageFids, stageMods; /* the stages' model files; those with a mod */
    int stageId = -1, stageFromId = -1, stagePart = -1;
    StageEdit stageEdit, stageFrom;
    StageInfo stageInfo; /* the stage's entry, as edited */
    StageBuild stageMade;
    std::vector<uint8_t> stageDisc; /* its file on the disc, what builds start from */
    bool stageDirty = false;
    std::string stageDir; /* the last export's folder */
    auto mods_root = [&]() {
        size_t e = ownRoot.find_last_of("/\\");
        return e == std::string::npos ? ownRoot : ownRoot.substr(0, e);
    };
    auto mod_path = [&](int fid) {
        char b[16];
        snprintf(b, sizeof b, "/%04d.bin", fid);
        return mods_root() + b;
    };
    auto stage_tables = [&]() {
        std::vector<uint8_t> t;
        read_file(fid_path(459), t);
        stageInfos = read_stage_infos(t);
        stageFids.clear(), stageMods.clear();
        for (const StageInfo &si : stageInfos) {
            stageFids.insert(si.fid);
            if (SDL_GetPathInfo(mod_path(si.fid).c_str(), nullptr)) stageMods.insert(si.fid);
        }
    };
    stage_tables();
    /* a stage as the port has it: its mod's model and table entry, else the disc's */
    auto stage_load = [&](int id, StageEdit &edit, StageInfo &info, std::vector<uint8_t> &bytes) {
        info = stageInfos[id];
        std::vector<uint8_t> table;
        if (read_file(mod_path(459), table)) {
            std::vector<StageInfo> mod = read_stage_infos(table);
            if (id < (int)mod.size()) memcpy(info.bg, mod[id].bg, 3), memcpy(info.noBounds, mod[id].noBounds, 8);
        }
        if (!read_file(mod_path(info.fid), bytes) && !read_file(fid_path(info.fid), bytes)) {
            status = "can't read " + fid_path(info.fid);
            return false;
        }
        Model m;
        if (!m.load(fid_name(info.fid), bytes, nullptr, info.boneEntry)) {
            status = "stage " + std::to_string(id) + ": " + m.error;
            return false;
        }
        edit.fromModel(m, info);
        return true;
    };
    auto own_load = [&]() {
        own.load(ownRoot + "/" + std::to_string(fx.id) + (ownCritical ? "/critical" : ""));
        ownShow = !own.frames.empty(), ownDirty = false, fxDrawn = -1;
        ownIds = own_effects(ownRoot);
    };
    /* the list's groups: the effects no script starts, several Digimon's do
       or a technique's does, and each Digimon's own (its Black King, MP and
       MF forms with it) */
    bool fxGrouped = false;
    std::map<int, std::set<std::string>> fxUsers, fxTechs;
    std::vector<int> fxCommon;
    std::map<std::string, std::vector<int>> fxByDigimon;
    char fxFind[64] = "";
    auto group_effects = [&]() {
        fxUsers = effect_users();
        fxTechs = effect_techs();
        for (int id : fxIds) {
            std::set<std::string> base;
            for (std::string n : fxUsers[id]) {
                if (n.rfind("BK ", 0) == 0) n = n.substr(3);
                if (n.size() > 3 && (n.compare(n.size() - 3, 3, " MP") == 0 || n.compare(n.size() - 3, 3, " MF") == 0)) n.resize(n.size() - 3);
                base.insert(n);
            }
            if (base.size() == 1 && fxTechs[id].empty()) fxByDigimon[*base.begin()].push_back(id);
            else fxCommon.push_back(id);
        }
        fxGrouped = true;
    };
    auto fx_length = [&]() { return ownShow ? own.duration() : fx.duration; };
    auto open_effect = [&](int id, bool select) {
        if (!fx.load(id, g_data)) status = fx.error;
        fxTime = 0, fxDrawn = -1, fxPlaying = true, fxSelect = select;
        fxDir.clear();
        if (fx.id >= 0) own_load();
        fxFitPending = true;
    };
    auto joined = [](const std::set<std::string> &s) {
        std::string all;
        for (const std::string &n : s) all += (all.empty() ? "" : ", ") + n;
        return all;
    };
    /* the search: an id, or part of a Digimon's or technique's name */
    auto effect_found = [&](int id) {
        std::string f = fxFind;
        if (f.empty()) return true;
        if (std::all_of(f.begin(), f.end(), ::isdigit)) return std::to_string(id) == f;
        std::string all = joined(fxUsers[id]) + "," + joined(fxTechs[id]);
        auto lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            return s;
        };
        return lower(all).find(lower(f)) != std::string::npos;
    };
    auto fx_render = [&](int t, uint32_t bg) {
        std::fill(fxPx.begin(), fxPx.end(), bg);
        if (fx.id >= 0 && ownShow) own.draw(t, fxPx.data(), FXC_W, FXC_H, FXC_W / 2, FXC_H / 2);
        else if (fx.id >= 0) fx.draw(t, fxPx.data(), FXC_W, FXC_H, FXC_W / 2, FXC_H / 2);
    };
    /* the view on all the effect draws over its length, in the view's W x H */
    auto fx_fit = [&](float w, float h) {
        int box[4] = {FXC_W, FXC_H, -1, -1};
        for (int t = 0; t < std::max(fx_length(), 1); t += 2) {
            fx_render(t, 0);
            for (int y = 0; y < FXC_H; y++)
                for (int x = 0; x < FXC_W; x++)
                    if (fxPx[y * FXC_W + x]) {
                        box[0] = std::min(box[0], x), box[1] = std::min(box[1], y);
                        box[2] = std::max(box[2], x), box[3] = std::max(box[3], y);
                    }
        }
        if (box[2] < 0) box[0] = (FXC_W - FX_W) / 2, box[1] = (FXC_H - FX_H) / 2, box[2] = box[0] + FX_W, box[3] = box[1] + FX_H;
        fxPan[0] = (box[0] + box[2]) / 2.0f - FXC_W / 2, fxPan[1] = (box[1] + box[3]) / 2.0f - FXC_H / 2;
        fxZoom = std::clamp(0.8f * std::min(w / (box[2] - box[0] + 1), h / (box[3] - box[1] + 1)), 0.25f, 8.0f);
        fxDrawn = -1;
    };

    /* the stage's model in the view, as the game will draw it */
    auto stage_show = [&](std::vector<uint8_t> bytes, bool reframe) {
        auto m = std::make_unique<Model>();
        if (!m->load(fid_name(stageInfo.fid), std::move(bytes), nullptr, stageInfo.boneEntry)) {
            status = m->error;
            return;
        }
        scene.fid = -1, scene.path.clear(), scene.fighters.clear(), scene.fighter = 0;
        scene.model = std::move(m);
        scene.selected = -1;
        upload(scene);
        view.stageView = true;
        for (int k = 0; k < 3; k++) view.stageBg[k] = stageEdit.bg[k] / 255.0f;
        if (reframe) frame_stage(view);
        view.texCombo = 0;
    };
    auto stage_open = [&](int id) {
        std::vector<uint8_t> bytes;
        if (!stage_load(id, stageEdit, stageInfo, bytes)) return;
        read_file(fid_path(stageInfo.fid), stageDisc);
        stageId = id, stagePart = -1, stageDirty = false, stageMade = StageBuild();
        for (int k = 0; k < (int)stageEdit.parts.size(); k++) stageMade.firstBone.push_back(k + 1);
        status.clear();
        stage_show(std::move(bytes), true);
    };
    /* after an edit: built, and shown as built */
    auto stage_rebuild = [&]() {
        std::vector<uint8_t> bytes;
        if (!stageEdit.build(stageDisc, stageInfo.boneEntry, bytes, stageInfo, stageMade)) {
            status = stageEdit.error;
            return false;
        }
        stageDirty = true;
        note(NOTE_WARN, stageMade.warning);
        stage_show(std::move(bytes), false);
        if (stagePart >= 0 && stagePart < (int)stageMade.firstBone.size()) scene.selected = stageMade.firstBone[stagePart];
        return true;
    };
    /* the stage's model and its entry of the table to the mods folder; the
       stages that share its model (0 and 1, 34 and 56) take its bones'
       bounds check too */
    auto stage_save = [&]() {
        std::vector<uint8_t> bytes, table;
        if (!stageEdit.build(stageDisc, stageInfo.boneEntry, bytes, stageInfo, stageMade)) {
            status = stageEdit.error;
            return;
        }
        SDL_CreateDirectory(mods_root().c_str());
        if (!read_file(mod_path(459), table)) read_file(fid_path(459), table);
        std::vector<StageInfo> now = read_stage_infos(table);
        for (StageInfo si : now) {
            if (si.fid != stageInfo.fid) continue;
            memcpy(si.noBounds, stageInfo.noBounds, 8);
            if (si.id == stageId) memcpy(si.bg, stageInfo.bg, 3);
            write_stage_info(table, si);
        }
        std::ofstream(mod_path(stageInfo.fid), std::ios::binary).write((const char *)bytes.data(), (std::streamsize)bytes.size());
        std::ofstream(mod_path(459), std::ios::binary).write((const char *)table.data(), (std::streamsize)table.size());
        stageDirty = false;
        stageMods.insert(stageInfo.fid);
        note(NOTE_OK, "Saved stage " + std::to_string(stageId) + " to " + mod_path(stageInfo.fid) + ": the game reads it when it starts");
    };
    /* back to the disc's: the mod's model gone, its table entries the disc's */
    auto stage_revert = [&]() {
        SDL_RemovePath(mod_path(stageInfo.fid).c_str());
        stageMods.erase(stageInfo.fid);
        std::vector<uint8_t> table, disc;
        if (read_file(mod_path(459), table) && read_file(fid_path(459), disc)) {
            for (const StageInfo &si : stageInfos)
                if (si.fid == stageInfo.fid) write_stage_info(table, si);
            if (table == disc) SDL_RemovePath(mod_path(459).c_str());
            else std::ofstream(mod_path(459), std::ios::binary).write((const char *)table.data(), (std::streamsize)table.size());
        }
        stage_open(stageId);
    };

    if (!shot.empty()) { /* one frame, drawn off screen */
        if (!scene.model) {
            fprintf(stderr, "%s\n", status.c_str());
            return 1;
        }
        scene.model->setMotion(shot_motion);
        for (int i = 0; i < shot_key; i++) {
            scene.model->evaluate();
            scene.model->advance(1);
            scene.model->stepFace();
        }
        scene.model->evaluate();
        upload_vram(scene);
        const int W = 960, H = 720;
        GLuint fbo, rb[2];
        GL(glGenFramebuffers)(1, &fbo);
        GL(glBindFramebuffer)(GL_FRAMEBUFFER, fbo);
        GL(glGenRenderbuffers)(2, rb);
        GL(glBindRenderbuffer)(GL_RENDERBUFFER, rb[0]);
        GL(glRenderbufferStorage)(GL_RENDERBUFFER, GL_RGBA8, W, H);
        GL(glFramebufferRenderbuffer)(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb[0]);
        GL(glBindRenderbuffer)(GL_RENDERBUFFER, rb[1]);
        GL(glRenderbufferStorage)(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, W, H);
        GL(glFramebufferRenderbuffer)(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb[1]);
        view.skeleton = false;
        view.stage = shot_stage;
        if (shot_cam >= 0 && !scene.fighters.empty() && shot_cam < (int)scene.fighters[scene.fighter]->cameras.size())
            apply_camera(view, *scene.fighters[scene.fighter], shot_cam);
        if (!shot_only.empty()) {
            for (size_t b = 0; b < scene.hidden.size(); b++) scene.hidden[b] = std::find(shot_only.begin(), shot_only.end(), (int)b) == shot_only.end();
            if (shot_frame.empty()) shot_frame = shot_only;
        }
        if (!shot_frame.empty()) frame_bones(scene, view, shot_frame);
        view.cam.dist /= shot_zoom;
        view.wireframe = shot_wire;
        if (shot_yaw < 1e8f) view.cam.yaw = shot_yaw * 3.14159265f / 180;
        render(scene, view, 0, 0, W, H);
        std::vector<uint32_t> px(W * H), flipped(W * H);
        GL(glReadPixels)(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        for (int y = 0; y < H; y++) memcpy(&flipped[y * W], &px[(H - 1 - y) * W], W * 4);
        save_bmp(shot, flipped, W, H);
        printf("%s: motion %d key %d -> %s\n", scene.model->name.c_str(), shot_motion, shot_key, shot.c_str());
        return 0;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    float dpi = SDL_GetWindowDisplayScale(win);
    if (dpi <= 0) dpi = 1;
    ui_theme(dpi);
    ui_fonts(dpi);
    ImGui_ImplSDL3_InitForOpenGL(win, ctx);
    ImGui_ImplOpenGL3_Init("#version 330");

    GLuint texView = 0;
    std::string texKey;
    std::vector<uint32_t> texPixels;
    int texW = 0, texH = 0;
    char filter[64] = "";
    uint64_t last = SDL_GetTicksNS();
    bool running = true;
