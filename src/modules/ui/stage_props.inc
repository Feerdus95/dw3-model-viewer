    /* ===== the stage's props: PNGs and OBJs for the native port, placed anywhere in it ===== */
    auto copy_to_props = [&](const std::string &path, std::string &name) {
        size_t sl = path.find_last_of("/\\");
        name = sl == std::string::npos ? path : path.substr(sl + 1);
        std::string dir = props_dir(stageId), to = dir + "/" + name;
        if (name.find("..") != std::string::npos) return note(NOTE_ERROR, "The port can't read a name with \"..\" in it: rename " + name), false;
        SDL_CreateDirectory((mods_root() + "/fightstages").c_str());
        SDL_CreateDirectory(dir.c_str());
        if (SDL_strcasecmp(path.c_str(), to.c_str()) != 0 && !SDL_CopyFile(path.c_str(), to.c_str()))
            return note(NOTE_ERROR, to + ": " + SDL_GetError()), false;
        auto old = propImgs.find(to); /* a new file under an old name */
        if (old != propImgs.end()) GL(glDeleteTextures)(1, &old->second.tex), propImgs.erase(old);
        propMeshes.erase(to), ++propGen;
        return true;
    };
    auto prop_add = [&](const std::string &path, int kind, const float *at = nullptr) {
        if (!stage_on()) return;
        std::string name, tex;
        if (!copy_to_props(path, name)) return;
        StageProp s;
        s.emitter = kind == 1, s.model = kind == 2;
        if (kind == 2) {
            s.obj = name;
            tex = obj_texture(path);
            std::string from = path.substr(0, path.find_last_of("/\\") + 1) + tex;
            if (!tex.empty() && std::ifstream(from)) {
                std::string tn;
                if (copy_to_props(from, tn)) s.image = tn;
            }
            const ObjMesh &m = prop_mesh(name);
            if (m.v.empty()) return note(NOTE_ERROR, "No triangles in " + name);
            float reach = 0; /* the stage's half width: a model starts a fifth of it tall (a man, in a small stage) */
            for (const StagePart &sp : stageEdit.parts)
                for (const auto &pv : sp.verts) reach = std::max({reach, fabsf(pv[0]), fabsf(pv[2])});
            s.scale = std::max(400.0f, reach * 0.2f) / m.h;
        } else {
            s.image = name;
            const PropImg &pi = prop_img(name);
            if (!pi.tex) return note(NOTE_ERROR, "Can't read " + props_dir(stageId) + "/" + name);
            if (kind == 1) s.size[0] = 60, s.size[1] = 110, s.count = 40;
            else s.size[1] = 300, s.size[0] = std::max(10.0f, 300 * pi.w / pi.h);
        }
        stage_mark();
        s.pos[0] = at ? at[0] : view.cam.target[0], s.pos[2] = -(at ? at[2] : view.cam.target[2]); /* on the ground, where asked or the camera looks */
        props.push_back(s);
        propSel = (int)props.size() - 1;
        select_part(-1);
        stageDirty = true;
        note(s.image.empty() ? NOTE_WARN : NOTE_OK, s.image.empty() ? "Added " + name + ": it has no texture, pick a PNG for it" : "Added " + name + " to the stage's props");
    };
    /* the brush: the picture's file (or the ticked ones' with Mix) is copied once, each dab scatters a few around the mouse */
    auto mix_pool = [&]() {
        int n = 0;
        for (const PCat &c : pcat) n += c.kind == 0 && brush.mix.count(c.path);
        return n;
    };
    auto brush_ready = [&]() {
        bool pool = brush.mixOn ? mix_pool() > 0 : pcatArm >= 0 && pcatArm < (int)pcat.size() && pcat[pcatArm].kind == 0;
        return brush.on && pool && section == STAGES && stage_on();
    };
    auto brush_dab = [&](const float g[3], bool erase) {
        DabResult r = brush_dab_props(props, brush.pool, brush.lastPick, brush.cfg, brush.maxBB, g, erase);
        if (r.changed) brush.added = stageDirty = true;
        if (erase && r.changed) propSel = -1;
        if (r.capped) note(NOTE_WARN, "That is as many pictures as the brush places (" + std::to_string(brush.maxBB) + "): erase some, or use bigger ones");
    };
    auto brush_begin = [&](const float g[3]) {
        brush.pool.clear(), brush.lastPick = -1;
        float tall = 1;
        auto add = [&](const PCat &c, float weight) {
            std::string nm;
            if (!copy_to_props(c.path, nm)) return;
            const PropImg &pi = prop_img(nm);
            if (!pi.tex) return note(NOTE_ERROR, "Can't read " + nm);
            brush.pool.push_back({nm, pi.w, pi.h, weight});
            tall = std::max(tall, pi.h);
        };
        if (brush.mixOn) {
            for (const PCat &c : pcat)
                if (c.kind == 0 && brush.mix.count(c.path)) add(c, std::max(brush.mix[c.path], 0.01f));
        } else {
            add(pcat[pcatArm], 1);
        }
        if (brush.pool.empty()) return false;
        /* the tallest is 300 and the others keep their proportion to it: reeds taller than a tuft */
        brush_normalize(brush.pool, brush.mixOn, tall);
        stage_mark();
        brush.stroke = true, brush.added = false;
        for (int k = 0; k < 3; k++) brush.last[k] = g[k];
        brush_dab(g, ImGui::GetIO().KeyCtrl);
        return true;
    };
    auto pick_prop = [&](int kind) {
        static const SDL_DialogFileFilter PNG = {"PNG images", "png"}, OBJ = {"OBJ models", "obj"};
        propKind = kind;
        SDL_ShowOpenFileDialog(
            [](void *u, const char *const *l, int) {
                if (!l || !*l) return;
                Picked *p = (Picked *)u;
                std::lock_guard<std::mutex> lock(p->m);
                p->prop = *l;
            },
            &picked, win, kind == 2 ? &OBJ : &PNG, 1, nullptr, false);
    };
    auto pick_prop_tex = [&]() { /* a PNG for the picked model (or another for a billboard) */
        static const SDL_DialogFileFilter PNG = {"PNG images", "png"};
        SDL_ShowOpenFileDialog(
            [](void *u, const char *const *l, int) {
                if (!l || !*l) return;
                Picked *p = (Picked *)u;
                std::lock_guard<std::mutex> lock(p->m);
                p->propTex = *l;
            },
            &picked, win, &PNG, 1, nullptr, false);
    };
    auto prop_set_tex = [&](const std::string &path) {
        std::string name;
        if (propSel < 0 || propSel >= (int)props.size() || !copy_to_props(path, name)) return;
        stage_mark();
        props[propSel].image = name;
        stageDirty = true;
    };
    /* the points of a prop that fence it on screen (to pick it): a billboard's corners, a model's box, an emitter's axis */
    auto prop_points = [&](const StageProp &p, float pts[8][3]) -> int {
        if (p.emitter) {
            pts[0][0] = pts[1][0] = p.pos[0], pts[0][2] = pts[1][2] = -p.pos[2], pts[0][1] = p.pos[1], pts[1][1] = p.pos[1] + p.top;
            return 2;
        }
        if (!p.model) {
            prop_corners(p, view.cam.yaw, (float(*)[3])pts);
            return 4;
        }
        const ObjMesh &m = prop_mesh(p.obj);
        if (m.v.empty()) return 0;
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f}, none[2] = {0, 0};
        for (size_t i = 0; i < m.v.size(); i += 8)
            for (int k = 0; k < 3; k++) lo[k] = std::min(lo[k], m.v[i + k]), hi[k] = std::max(hi[k], m.v[i + k]);
        for (int c = 0; c < 8; c++) prop_model_point(p, m, c & 1 ? hi[0] : lo[0], c & 2 ? hi[1] : lo[1], c & 4 ? hi[2] : lo[2], none, 0, pts[c]);
        return 8;
    };
    /* the props on the view: billboards and models drawn, emitters as their volume and a few of their particles, and
       the outlines (the picked one yellow) */
    auto pg_mesh = [&](const std::string &name) -> int {
        const ObjMesh &m = prop_mesh(name);
        if (m.v.empty()) return -1;
        std::string key = props_dir(stageId) + "/" + name;
        int at = -1;
        for (int i = 0; i < (int)propGpu.meshes.size(); i++)
            if (propGpu.meshes[i].key == key) at = i;
        if (at < 0) propGpu.meshes.push_back({}), at = (int)propGpu.meshes.size() - 1, propGpu.meshes[at].key = key;
        PropMeshGpu &g = propGpu.meshes[at];
        if (g.gen != propGen || g.ptr != m.v.data() || g.size != m.v.size()) { /* new, or its file was replaced: up to the card */
            if (!g.vao) GL(glGenVertexArrays)(1, &g.vao), GL(glGenBuffers)(1, &g.vbo);
            GL(glBindVertexArray)(g.vao);
            GL(glBindBuffer)(GL_ARRAY_BUFFER, g.vbo);
            GL(glBufferData)(GL_ARRAY_BUFFER, (GLsizeiptr)(m.v.size() * 4), m.v.data(), GL_STATIC_DRAW);
            const int size[3] = {3, 2, 3}, loc[3] = {0, 1, 4}, off[3] = {0, 12, 20};
            for (int a = 0; a < 3; a++) {
                GL(glEnableVertexAttribArray)(loc[a]);
                GL(glVertexAttribPointer)(loc[a], size[a], GL_FLOAT, GL_FALSE, 32, (void *)(size_t)off[a]);
            }
            g.gen = propGen, g.ptr = m.v.data(), g.size = m.v.size(), g.n = (int)(m.v.size() / 8), g.miny = m.miny, g.h = m.h;
        }
        return at;
    };
    /* the tables of the props: rebuilt only when the hash of what they come from changes (a prop, its picture or model) */
    auto pg_sync = [&]() {
        uint64_t h = props_signature(stageId, propGen, propsMeta, props);
        if (propGpu.valid && h == propGpu.sig) return;
        propGpu.valid = true, propGpu.sig = h;
        for (PropBatch &b : propGpu.batches)
            if (b.buf) GL(glDeleteBuffers)(1, &b.buf);
        propGpu.batches.clear();
        std::map<GLuint, std::vector<float>> bb;
        std::map<std::pair<int, GLuint>, std::vector<float>> md;
        for (const StageProp &p : props) {
            GLuint tex = p.image.empty() ? 0 : prop_img(p.image).tex;
            if (!tex) continue;
            if (p.emitter) {
                PropBatch b;
                b.mode = 1, b.tex = tex, b.n = std::min(p.count, 5000);
                float c[4] = {p.pos[0], p.pos[1], p.pos[2], p.radius}, a[4] = {p.top, p.fall, p.wind, 300}, s[4] = {p.size[0], p.size[1], 20, (float)b.n},
                      t4[4] = {p.tint[0] / 255, p.tint[1] / 255, p.tint[2] / 255, p.fog};
                memcpy(b.pc, c, 16), memcpy(b.pa, a, 16), memcpy(b.pb, s, 16), memcpy(b.tint, t4, 16);
                propGpu.batches.push_back(b);
            } else if (p.model) {
                int mesh = pg_mesh(p.obj);
                if (mesh < 0) continue;
                std::vector<float> &v = md[{mesh, tex}];
                const float row[12] = {p.pos[0], p.pos[1], p.pos[2], p.scale, p.rot, p.sway, p.fog, 0, p.tint[0] / 255, p.tint[1] / 255, p.tint[2] / 255, 1};
                v.insert(v.end(), row, row + 12);
            } else {
                std::vector<float> &v = bb[tex];
                const float row[12] = {p.pos[0], p.pos[1], p.pos[2], p.size[0] * p.scale, p.size[1] * p.scale, p.sway, p.rot,
                                       p.tint[0] / 255, p.tint[1] / 255, p.tint[2] / 255, p.fog, p.fixed ? 1.0f : 0.0f};
                v.insert(v.end(), row, row + 12);
            }
        }
        auto table = [&](PropBatch &b, const std::vector<float> &v) {
            b.n = (int)(v.size() / 12);
            GL(glGenBuffers)(1, &b.buf);
            GL(glBindBuffer)(GL_ARRAY_BUFFER, b.buf);
            GL(glBufferData)(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * 4), v.data(), GL_STATIC_DRAW);
            propGpu.batches.push_back(b);
        };
        for (auto &it : bb) {
            PropBatch b;
            b.mode = 0, b.tex = it.first;
            table(b, it.second);
        }
        for (auto &it : md) {
            PropBatch b;
            b.mode = 3, b.mesh = it.first.first, b.tex = it.first.second;
            table(b, it.second);
        }
        if (propsMeta.leaves > 0) { /* the stage's leaves, the list's count of them: a diamond in greens and browns */
            if (!propWhite) {
                const unsigned char px[4] = {255, 255, 255, 255};
                GL(glGenTextures)(1, &propWhite);
                GL(glBindTexture)(GL_TEXTURE_2D, propWhite);
                GL(glTexImage2D)(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
                GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            }
            PropBatch b;
            b.mode = 2, b.tex = propWhite, b.n = std::min(propsMeta.leaves, 5000);
            const float c[4] = {0, 0, 0, 9000}, a[4] = {7500, 1, 1, 1500}, s[4] = {70, 140, 40, (float)b.n}, t4[4] = {1, 1, 1, 1};
            memcpy(b.pc, c, 16), memcpy(b.pa, a, 16), memcpy(b.pb, s, 16), memcpy(b.tint, t4, 16);
            propGpu.batches.push_back(b);
        }
    };
    /* the props on the view: drawn on the card (propGpu); here the outlines (the picked one yellow), the catalog's ghost and
       the list's fog */
    auto props_build = [&](std::vector<PropQuad> &quads, std::vector<float> &lines) {
        float yaw = view.cam.yaw, rt[3] = {cosf(yaw), 0, -sinf(yaw)};
        float t = propWind ? (float)ImGui::GetTime() * 60.0f : 0, ww[2] = {0, 0};
        if (propWind) prop_wind(t, propsMeta.wind, ww);
        if (propWind) propClock += ImGui::GetIO().DeltaTime * 60.0f; /* the particles' clock; paused, they stay where they are */
        pg_sync();
        propGpu.t = t, propGpu.clock = propClock, propGpu.ww[0] = ww[0], propGpu.ww[1] = ww[1];
        for (int i = 0; i < (int)props.size(); i++) {
            const StageProp &p = props[i];
            bool sel = i == propSel || (propSet.size() > 1 && propSet.count(i));
            if (!sel && (p.model || !propOutlines)) continue; /* a model shows its box only when picked */
            float col[3] = {1.0f, 0.85f, 0.2f};
            if (!sel) col[0] = p.emitter ? 0.3f : 0.6f, col[1] = p.emitter ? 0.8f : 0.6f, col[2] = p.emitter ? 1.0f : 0.6f;
            if (p.model) { /* its box */
                float bx[8][3];
                if (!prop_points(p, bx)) continue;
                static const int E[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
                for (const auto &e : E) line(lines, bx[e[0]], bx[e[1]], col[0], col[1], col[2]);
            } else if (!p.emitter) {
                float c[4][3], h = p.size[1] * p.scale, bend = propWind ? prop_bend(p, t, h) : 0;
                prop_corners(p, yaw, c, ww[0] * bend, ww[1] * bend);
                static const int E[4][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0}};
                for (const auto &e : E) line(lines, c[e[0]], c[e[1]], col[0], col[1], col[2]);
            } else {
                float cx = p.pos[0], cy = p.pos[1], cz = -p.pos[2];
                for (int s = 0; s < 48; s++) {
                    float a = s * 6.2831853f / 48, b = (s + 1) * 6.2831853f / 48;
                    for (float y : {cy, cy + p.top}) {
                        float u[3] = {cx + cosf(a) * p.radius, y, cz + sinf(a) * p.radius}, v[3] = {cx + cosf(b) * p.radius, y, cz + sinf(b) * p.radius};
                        line(lines, u, v, col[0], col[1], col[2]);
                    }
                }
                for (int s = 0; s < 4; s++) {
                    float a = s * 1.5707963f, lo[3] = {cx + cosf(a) * p.radius, cy, cz + sinf(a) * p.radius}, hi[3] = {lo[0], cy + p.top, lo[2]};
                    line(lines, lo, hi, col[0], col[1], col[2]);
                }
            }
        }
        /* where a catalog pick would go: a ring on the ground and, for a picture, the picture itself */
        if (ghostOn && pcatArm >= 0 && pcatArm < (int)pcat.size()) {
            const PCat &g = pcat[pcatArm];
            float r = brush.on && (brush.mixOn || g.kind == 0) ? brush.cfg.radius : view.cam.dist * 0.03f, gy[3] = {ghostAt[0], 0, ghostAt[2]};
            for (int s = 0; s < 24; s++) {
                float a = s * 6.2831853f / 24, b = (s + 1) * 6.2831853f / 24;
                float u[3] = {gy[0] + cosf(a) * r, 0, gy[2] + sinf(a) * r}, v[3] = {gy[0] + cosf(b) * r, 0, gy[2] + sinf(b) * r};
                line(lines, u, v, 1.0f, 0.85f, 0.2f);
            }
            float top[3] = {gy[0], g.kind == 1 ? 4000.0f : 300.0f, gy[2]};
            line(lines, gy, top, 1.0f, 0.85f, 0.2f);
            if (g.kind == 1) {
                for (int s = 0; s < 48; s++) {
                    float a = s * 6.2831853f / 48, b = (s + 1) * 6.2831853f / 48;
                    for (float y : {0.0f, 4000.0f}) {
                        float u[3] = {gy[0] + cosf(a) * 3000, y, gy[2] + sinf(a) * 3000}, v[3] = {gy[0] + cosf(b) * 3000, y, gy[2] + sinf(b) * 3000};
                        line(lines, u, v, 1.0f, 0.85f, 0.2f);
                    }
                }
            }
            if (g.kind == 0 && g.gl && !brush.on) {
                PropQuad gq;
                float h = 300, w = std::max(10.0f, 300 * g.w / g.h), c[4][3], one[3] = {1, 1, 1};
                for (int k = 0; k < 4; k++) {
                    float side = k & 1 ? 0.5f : -0.5f;
                    for (int j = 0; j < 3; j++) c[k][j] = gy[j] + rt[j] * w * side + (j == 1 ? (k < 2 ? h : 0) : 0);
                }
                gq.tex = g.gl;
                pq_quad(gq, c, one, 0);
                quads.push_back(gq);
            }
        }
        /* the fog of the list's own, for the props: the stage's isn't known here, so its far side is about the arena's */
        if (propsMeta.fog) {
            float zf = view.cam.dist + 9000;
            for (int k = 0; k < 3; k++) view.pfog[k] = propsMeta.fogColor[k] / 255.0f;
            view.pfog[3] = propsMeta.fogAmount;
            view.pfogz[0] = zf * propsMeta.fogFrom, view.pfogz[1] = zf;
        } else {
            view.pfog[3] = 0;
        }
    };
