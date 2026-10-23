    /* --- a model's pieces: the edits applied to the meshes in the view, and written into the model's file in place
       (it keeps its size, so the animations and scripts still fit) as the port's mod file --- */
    auto char_apply = [&]() {
        Model &m = *scene.model;
        auto fit16 = [](float x) { return (int16_t)std::clamp((int)lroundf(x), -32768, 32767); };
        for (size_t b = 1; b < m.bones.size() && b < charK0.size(); b++) { /* each bone's joint: where its parent's Reach puts it */
            m.bones[b].keys[0] = charK0[b];
            auto pe = charEdits.find(m.bones[b].parent);
            if (pe == charEdits.end() || pe->second.reach == 1) continue;
            for (Key &k : m.bones[b].keys[0]) k.x = fit16(k.x * pe->second.reach), k.y = fit16(k.y * pe->second.reach), k.z = fit16(k.z * pe->second.reach);
        }
        m.evaluate();
        for (size_t b = 1; b < m.bones.size() && b < charV0.size(); b++) {
            Mesh &me = m.meshes[m.bones[b].mesh];
            me.verts = charV0[b], me.normals = charN0[b];
            auto it = charEdits.find((int)b);
            if (it == charEdits.end() || charV0[b].empty()) continue;
            const PieceEdit &e = it->second;
            float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
            for (const Vec3s &v : charV0[b]) {
                float p[3] = {(float)v.x, (float)v.y, (float)v.z};
                for (int i = 0; i < 3; i++) lo[i] = std::min(lo[i], p[i]), hi[i] = std::max(hi[i], p[i]);
            }
            float c[3] = {(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2};
            if (e.joint) c[0] = c[1] = c[2] = 0;
            float s = e.gone ? 0.0f : e.scale, cs = cosf(e.turn * 0.017453293f), sn = sinf(e.turn * 0.017453293f);
            auto fit = [](float x) { return (int16_t)std::clamp((int)lroundf(x), -32768, 32767); };
            for (Vec3s &v : me.verts) {
                float dx = (v.x - c[0]) * s, dy = (v.y - c[1]) * s, dz = (v.z - c[2]) * s;
                v = {fit(c[0] + e.move[0] + dx * cs + dz * sn), fit(c[1] + e.move[1] + dy), fit(c[2] + e.move[2] - dx * sn + dz * cs)};
            }
            for (Vec3s &n : me.normals) {
                float x = n.x, z = n.z;
                n.x = fit(x * cs + z * sn), n.z = fit(-x * sn + z * cs);
            }
        }
        /* the view keeps its camera and what's hidden */
        std::vector<bool> hidden = scene.hidden;
        float center[3] = {scene.center[0], scene.center[1], scene.center[2]}, radius = scene.radius;
        upload(scene);
        scene.hidden = hidden;
        memcpy(scene.center, center, sizeof center);
        scene.radius = radius;
    };
    /* a VRAM halfword as the file's TIMs have it */
    auto vram_orig = [&](int idx) -> uint16_t {
        int x = idx % 1024, y = idx / 1024;
        for (const Tim &t : scene.model->tims)
            if (x >= t.x && x < t.x + t.w && y >= t.y && y < t.y + t.h) return t.pixels[(y - t.y) * t.w + x - t.x];
        return scene.model->vram[idx];
    };
    /* the view's VRAM gets the changed colors (and the ones changed back their own) */
    auto char_pal_apply = [&]() {
        for (int idx : charPalShown)
            if (!charPal.count(idx)) scene.model->vram[idx] = vram_orig(idx);
        charPalShown.clear();
        for (const auto &kv : charPal) scene.model->vram[kv.first] = kv.second, charPalShown.insert(kv.first);
        upload_vram(scene);
    };
    auto char_mark = [&]() { /* before an edit */
        charUndo.push_back({charEdits, charPal});
        if (charUndo.size() > 40) charUndo.erase(charUndo.begin());
        charRedo.clear();
    };
    /* a change of the view's world, in the model's space, to the bone's own axes */
    auto char_local = [&](int b, const float dw[3], float out[3]) {
        const float *w = scene.model->pose[b].world;
        float d[3] = {dw[0], -dw[1], -dw[2]};
        float a = w[0], bb = w[4], c = w[8], dd = w[1], e = w[5], f = w[9], g = w[2], h = w[6], i = w[10];
        float det = a * (e * i - f * h) - bb * (dd * i - f * g) + c * (dd * h - e * g);
        out[0] = out[1] = out[2] = 0;
        if (fabsf(det) < 1e-6f) return;
        float inv[9] = {(e * i - f * h), -(bb * i - c * h), (bb * f - c * e), -(dd * i - f * g), (a * i - c * g),
                        -(a * f - c * dd), (dd * h - e * g), -(a * h - bb * g), (a * e - bb * dd)};
        for (int r = 0; r < 3; r++) out[r] = (inv[r * 3] * d[0] + inv[r * 3 + 1] * d[1] + inv[r * 3 + 2] * d[2]) / det;
    };
    auto char_bytes = [&](std::vector<uint8_t> &bytes) {
        bytes = scene.model->file;
        for (const auto &kv : charEdits) {
            const Mesh &me = scene.model->meshes[scene.model->bones[kv.first].mesh];
            patch_vecs(bytes, me.entry, 0, me.verts);
            patch_vecs(bytes, me.entry, 1, me.normals);
        }
        for (size_t b = 1; b < scene.model->bones.size() && b < charK0.size(); b++) /* the joints Reach moved */
            if (scene.model->bones[b].keys[0].size() == charK0[b].size() &&
                memcmp(scene.model->bones[b].keys[0].data(), charK0[b].data(), charK0[b].size() * sizeof(Key)) != 0)
                patch_keys(bytes, scene.model->bones[b].keyEntry, scene.model->bones[b].keys[0]);
        std::map<int, Tim> tims; /* the TIMs with a changed color */
        for (const auto &kv : charPal)
            for (const Tim &t : scene.model->tims) {
                int x = kv.first % 1024, y = kv.first / 1024;
                if (x < t.x || x >= t.x + t.w || y < t.y || y >= t.y + t.h) continue;
                Tim &c = tims.emplace(t.entry, t).first->second;
                c.pixels[(y - t.y) * t.w + x - t.x] = kv.second;
            }
        if (!tims.empty()) {
            std::vector<Tim> list;
            for (const auto &kv : tims) list.push_back(kv.second);
            std::string err;
            if (!patch_tims(bytes, scene.model->texEntry, list, err)) {
                note(NOTE_ERROR, err);
                return false;
            }
        }
        return true;
    };
    auto char_save = [&]() {
        std::vector<uint8_t> bytes;
        if (!char_bytes(bytes)) return;
        SDL_CreateDirectory(mods_root().c_str());
        std::ofstream(mod_path(scene.fid), std::ios::binary).write((const char *)bytes.data(), (std::streamsize)bytes.size());
        charDirty = false;
        charClean = {charEdits, charPal}, charCleanGen = charGen;
        note(NOTE_OK, "Saved " + scene.model->name + " to " + mod_path(scene.fid) + ": the game reads it when it starts");
    };
    /* the model's bones as parts: the OBJ's objects */
    auto char_parts = [&](StageEdit &e) {
        StageInfo info;
        info.fid = scene.fid;
        e.fromModel(*scene.model, info);
    };
    auto char_export = [&]() {
        StageEdit e;
        char_parts(e);
        std::string nm = lower(scene.model->name.substr(0, scene.model->name.find('.')));
        charDir = g_exe_dir + "models";
        SDL_CreateDirectory(charDir.c_str());
        charDir += "/" + nm;
        if (e.exportObj(charDir, nm)) note(NOTE_OK, "Exported " + scene.model->name + " to " + charDir);
        else note(NOTE_ERROR, "Can't write " + charDir);
    };
    /* the model as bytes, shown again (the camera and the motion kept) */
    auto char_reload = [&](std::vector<uint8_t> bytes) {
        const Fighter *f = scene.fighters.empty() ? nullptr : scene.fighters[scene.fighter];
        auto m = std::make_unique<Model>();
        if (!m->load(scene.model->name, std::move(bytes), f)) {
            note(NOTE_ERROR, scene.model->name + ": " + m->error);
            return false;
        }
        int motion = scene.model->motion;
        float center[3] = {scene.center[0], scene.center[1], scene.center[2]}, radius = scene.radius;
        scene.model = std::move(m);
        upload(scene);
        memcpy(scene.center, center, sizeof center);
        scene.radius = radius;
        if (motion > 0) scene.model->setMotion(motion);
        return true;
    };
    /* a different file for the same model (an import, or undoing one): the undo steps and what's saved stay */
    auto char_swap_file = [&](std::vector<uint8_t> bytes) {
        auto undo = charUndo, redo = charRedo;
        CharSnap clean = charClean;
        int cleanGen = charCleanGen;
        if (!char_reload(std::move(bytes))) return false;
        char_sync();
        charUndo = std::move(undo), charRedo = std::move(redo), charClean = std::move(clean), charCleanGen = cleanGen;
        return true;
    };
    /* the edits changed or went back: unsaved only if they differ from what was saved */
    auto char_same_edits = [&](const std::map<int, PieceEdit> &a, const std::map<int, PieceEdit> &b) {
        auto v = [](const std::map<int, PieceEdit> &m) {
            std::vector<std::array<float, 10>> out;
            for (const auto &kv : m)
                if (kv.second.any())
                    out.push_back({(float)kv.first, kv.second.move[0], kv.second.move[1], kv.second.move[2], kv.second.turn, kv.second.scale,
                                   (float)kv.second.gone, kv.second.reach, (float)kv.second.joint, (float)kv.second.follow});
            return out;
        };
        return v(a) == v(b);
    };
    auto char_unsaved = [&]() { return charGen != charCleanGen || !char_same_edits(charEdits, charClean.e) || charPal != charClean.pal; };
    /* an OBJ back: the bones whose faces changed get new meshes (the others stay), the file's size may change */
    auto char_import = [&](const std::string &path) {
        char_sync();
        StageEdit cur, imp;
        char_parts(cur);
        imp = cur;
        if (!imp.importObj(path)) return note(NOTE_ERROR, imp.error);
        std::string warning = imp.error;
        std::vector<uint8_t> bytes;
        /* the pages the PNGs changed: their texels through the page's own palette */
        std::map<int, uint16_t> texChanges = charPal;
        bool texEdited = false;
        for (StageImage &im : imp.images) {
            if (im.from < 0 || im.from >= (int)cur.images.size()) continue;
            const StageImage &was = cur.images[im.from];
            if (was.tpage < 0) return note(NOTE_ERROR, "tex" + std::to_string(im.from) + ".png: the polygons with no texture have none to edit");
            std::string err;
            if (!texture_edits(scene.model->vram, (uint16_t)was.tpage, (uint16_t)was.pageClut, was.px, im.px, texChanges, err))
                return note(NOTE_ERROR, "tex" + std::to_string(im.from) + ".png: " + err);
            im.tpage = was.tpage, im.pageClut = was.pageClut, texEdited = true;
        }
        std::map<int, std::vector<uint8_t>> repl;
        std::set<int> seen;
        int changed = 0, skipped = 0, nbones = (int)scene.model->bones.size() - 1;
        struct NewBone {
            int parent;
            std::vector<uint8_t> mesh, keys;
        };
        std::vector<NewBone> fresh; /* the objects named new_P_...: bones after the model's, on bone P (0 the root) */
        for (int k = 0; k < (int)imp.parts.size(); k++) {
            int b = 0;
            const std::string &nm = imp.parts[k].name;
            if (nm.rfind("new", 0) == 0) {
                int pp = 0;
                if (nm.size() > 3 && nm[3] == '_' && isdigit((unsigned char)nm[4])) pp = atoi(nm.c_str() + 4);
                if (pp > nbones) return note(NOTE_ERROR, nm + ": there is no bone " + std::to_string(pp) + " to hang it on");
                if (nbones + (int)fresh.size() + 1 > 255) return note(NOTE_ERROR, "A model holds 255 bones at most");
                NewBone nb;
                nb.parent = pp;
                if (!imp.newBone(k, pp ? &cur.parts[pp - 1] : nullptr, nb.mesh, nb.keys)) return note(NOTE_ERROR, imp.error);
                fresh.push_back(std::move(nb));
                continue;
            }
            if (sscanf(nm.c_str(), "part_%d", &b) != 1 || b < 1 || b >= (int)scene.model->bones.size()) {
                skipped++;
                continue;
            }
            seen.insert(b);
            if (imp.signature(k) == cur.signature(b - 1)) continue;
            std::vector<uint8_t> mesh;
            if (!imp.boneMesh(k, mesh)) return note(NOTE_ERROR, imp.error);
            repl[scene.model->meshes[scene.model->bones[b].mesh].entry] = std::move(mesh);
            changed++;
        }
        if (!fresh.empty()) { /* the bone list and the new entries (a mesh and its keys each) */
            Archive ar;
            ar.data = scene.model->file.data(), ar.size = scene.model->file.size();
            int at = ar.count();
            const uint8_t *le = ar.entry(scene.model->boneEntry);
            std::vector<uint8_t> list(le, le + ar.entry_size(scene.model->boneEntry));
            list.resize(8 + 12 * nbones); /* the entry may be padded */
            uint32_t total = (uint32_t)(nbones + fresh.size());
            memcpy(&list[4], &total, 4);
            for (const NewBone &nb : fresh) {
                uint32_t trip[3] = {(uint32_t)nb.parent, (uint32_t)at, (uint32_t)at + 1};
                list.insert(list.end(), (const uint8_t *)trip, (const uint8_t *)trip + 12);
                repl[at++] = nb.mesh;
                repl[at++] = nb.keys;
            }
            repl[scene.model->boneEntry] = list;
            changed += (int)fresh.size();
        }
        if (!changed && !texEdited) return note(NOTE_INFO, "No bone's faces or pages changed in " + path);
        if (!changed) { /* only pages: the palette's colors and texels, as edits */
            char_mark();
            charPal = texChanges;
            char_pal_apply();
            charDirty = true;
            return note(NOTE_OK, "Imported the pages' changes from " + path + " (Ctrl+Z undoes; the texels take the page's palette colors)");
        }
        std::map<int, uint16_t> palWas = charPal;
        charPal = texChanges;
        CharSnap pre{charEdits, palWas, std::make_shared<const std::vector<uint8_t>>(scene.model->file), charGen};
        if (!char_bytes(bytes) || !char_swap_file(replace_entries(bytes, repl))) {
            charPal = palWas;
            return;
        }
        charUndo.push_back(std::move(pre)), charRedo.clear();
        if (charUndo.size() > 40) charUndo.erase(charUndo.begin());
        charGen = ++charGenSeq;
        charDirty = true;
        std::string extra;
        if (skipped) extra += "; " + std::to_string(skipped) + " objects not named part_N skipped";
        if (seen.size() + 1 < scene.model->bones.size()) extra += "; bones with no object kept";
        note(warning.empty() ? NOTE_OK : NOTE_WARN,
             "Imported " + std::to_string(changed) + " bones from " + path + extra + " (Ctrl+Z undoes it)");
    };
    auto char_mod = [&]() { return char_on() && SDL_GetPathInfo(mod_path(scene.fid).c_str(), nullptr); };
    auto ask_revert_char = [&]() {
        ask.title = "Revert to the disc's";
        ask.text = scene.model->name + " goes back to the disc's: this deletes " + mod_path(scene.fid) + ". It can't be undone.";
        ask.ok = "Revert", ask.danger = true, ask.open = true;
        ask.then = [&]() {
            int fid = scene.fid, fighter = scene.fighter;
            std::string name = scene.model->name, path = scene.path;
            SDL_RemovePath(mod_path(fid).c_str());
            charOf = nullptr;
            if (open_model(scene, view, name, path, status, fid, fighter)) after_open();
            note(NOTE_OK, name + " is the disc's again");
        };
    };

