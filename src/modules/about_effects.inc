static const char *VERSION = "0.7.0";

/* The exe's icon (res/dw3mv.rc's icon 1) as a texture, for the About window; 0 without one */
static GLuint icon_texture(int size) {
#ifdef _WIN32
    HICON icon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, size, size, 0);
    if (!icon) return 0;
    ICONINFO info;
    GLuint tex = 0;
    if (GetIconInfo(icon, &info)) {
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = size, bi.bmiHeader.biHeight = -size; /* top-down */
        bi.bmiHeader.biPlanes = 1, bi.bmiHeader.biBitCount = 32, bi.bmiHeader.biCompression = BI_RGB;
        std::vector<uint32_t> px(size * size);
        HDC dc = GetDC(nullptr);
        if (GetDIBits(dc, info.hbmColor, 0, size, px.data(), &bi, DIB_RGB_COLORS)) {
            for (uint32_t &c : px) c = (c & 0xFF00FF00) | (c >> 16 & 0xFF) | (c & 0xFF) << 16; /* BGRA to RGBA */
            tex = rgba_texture(0, px, size, size);
            GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
        ReleaseDC(nullptr, dc);
        DeleteObject(info.hbmColor);
        DeleteObject(info.hbmMask);
    }
    DestroyIcon(icon);
    return tex;
#else
    return 0;
#endif
}

/* The About window: what the viewer is, its version, license and credits */
static void about_window(bool &open, GLuint &icon) {
    if (!open) return;
    if (!icon) icon = icon_texture(96);
    ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(1);
    if (!ImGui::Begin("About DW3 Model Viewer", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                                     ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    if (icon) {
        ImGui::Image((ImTextureID)(intptr_t)icon, ImVec2(96, 96));
        ImGui::SameLine();
    }
    ImGui::BeginGroup();
    ImGui::Text("DW3 Model Viewer");
    ImGui::TextDisabled("Version %s", VERSION);
    ImGui::Spacing();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 320);
    ImGui::TextWrapped("A viewer for Digimon World 2003's battle models: their meshes, textures, bones, "
                       "motions, faces, cameras, action scripts and 2D effects.");
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::Separator();
    ImGui::Text("(c) 2026 Feerdus - MIT License");
    auto link = [](const char *label, const char *url) {
        ImGui::TextColored(ImVec4(0.45f, 0.7f, 1, 1), "%s", label);
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemClicked()) SDL_OpenURL(url);
    };
    link("github.com/Feerdus95/dw3-model-viewer", "https://github.com/Feerdus95/dw3-model-viewer");
    ImGui::Spacing();
    ImGui::Text("Built on:");
    link("  dw3_decomp (MIT, juandav)", "https://github.com/ReGame-Labs/dw3_decomp");
    link("  Dear ImGui (MIT)", "https://github.com/ocornut/imgui");
    link("  SDL3 (zlib)", "https://libsdl.org");
    ImGui::Spacing();
    ImGui::TextDisabled("Not affiliated with Bandai. No game data is included:");
    ImGui::TextDisabled("it reads your own copy of the game.");
    ImGui::Spacing();
    if (ImGui::Button("Close", ImVec2(120, 0))) open = false;
    ImGui::End();
}

static void save_bmp(const std::string &path, const std::vector<uint32_t> &px, int w, int h) {
    SDL_Surface *s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ABGR8888, (void *)px.data(), w * 4);
    if (s) {
        SDL_SaveBMP(s, path.c_str());
        SDL_DestroySurface(s);
    }
}

/* An effect's frames as PNGs, every other field (the game draws 30 frames a
   second) on a 320x240 screen, its origin in the middle, with a transparent
   background: <exe>/effects/effect_<id>/frame_NNN.png. Returns how many.
   Each frame is drawn over black and over white and its alpha is what the
   two differ by: opaque texels stay opaque, averaged ones are half, and the
   added ones (no alpha of their own) take their brightness as alpha, which
   over black is exactly the game's. */
static const int FX_W = 320, FX_H = 240;
/* the Effects tab's canvas: three screens across, the effect's point in its middle */
static const int FXC_W = 960, FXC_H = 720;
static const uint32_t FX_BG = 0xFF302824;

/* An effect's frame F as straight-alpha RGBA (FX_W x FX_H, its point in the
   middle): drawn over black and over white, the difference is each pixel's cover */
static void alpha_frame(const std::function<void(uint32_t *)> &draw, std::vector<uint32_t> &px, std::vector<uint32_t> &white) {
    px.assign(FX_W * FX_H, 0xFF000000u);
    white.assign(FX_W * FX_H, 0xFFFFFFFFu);
    draw(px.data());
    draw(white.data());
    for (int i = 0; i < FX_W * FX_H; i++) {
        int a = 0;
        for (int c = 0; c < 24; c += 8) a = std::max(a, 255 - (int)(white[i] >> c & 255) + (int)(px[i] >> c & 255));
        a = std::clamp(a, 0, 255);
        uint32_t out = 0;
        for (int c = 0; c < 24; c += 8)
            if (a) out |= (uint32_t)std::min(255, (int)(px[i] >> c & 255) * 255 / a) << c;
        px[i] = out | (uint32_t)a << 24;
    }
}

static void fx_alpha(const Effect &fx, int f, std::vector<uint32_t> &px, std::vector<uint32_t> &white) {
    alpha_frame([&](uint32_t *p) { fx.draw(f, p, FX_W, FX_H, FX_W / 2, FX_H / 2); }, px, white);
}

static int export_effect(const Effect &fx, std::string &dir) {
    std::vector<uint32_t> px, white;
    dir = g_exe_dir + "effects";
    SDL_CreateDirectory(dir.c_str());
    dir += "/effect_" + std::to_string(fx.id);
    SDL_CreateDirectory(dir.c_str());
    int n = 0;
    for (int f = 0; f < fx.duration; f += 2, n++) {
        fx_alpha(fx, f, px, white);
        char name[32];
        snprintf(name, sizeof name, "/frame_%03d.png", n);
        SDL_Surface *s = SDL_CreateSurfaceFrom(FX_W, FX_H, SDL_PIXELFORMAT_ABGR8888, px.data(), FX_W * 4);
        if (s) SDL_SavePNG(s, (dir + name).c_str()), SDL_DestroySurface(s);
    }
    return n;
}

/* Who starts each sprite effect: the Digimon (by name) whose action scripts
   have its line. The effects no script names (the hits' own, digivolving,
   items) are the battle's own */
static std::map<int, std::set<std::string>> effect_users() {
    std::map<int, std::set<std::string>> out;
    std::map<int, std::vector<uint8_t>> files;
    for (const Fighter &f : g_game.fighters) {
        if (f.effectsFid <= 0) continue;
        std::vector<uint8_t> &file = files[f.effectsFid];
        if (file.empty() && !read_file(fid_path(f.effectsFid), file)) continue;
        Archive a{file.data(), file.size()};
        if (f.effectsEntry >= a.count() || !a.entry(f.effectsEntry)) continue;
        for (const std::vector<ScriptLine> &sc : read_scripts(a.entry(f.effectsEntry), a.entry_size(f.effectsEntry)))
            for (const ScriptLine &ln : sc)
                if (ln.effect >= 0) out[ln.effect].insert(f.name);
    }
    return out;
}

/* The techniques whose scripts start each effect by its 9999 (TechData.
   scriptEffect, +0xE of the exe's TECHS: 443 of 0x12 bytes), by name. The
   first of EXES that opens is the PAL exe. */
static std::map<int, std::set<std::string>> effect_techs(const std::vector<std::string> &exes) {
    std::map<int, std::set<std::string>> out;
    std::vector<uint8_t> exe;
    for (const std::string &p : exes)
        if (read_file(p, exe)) break;
    if (exe.size() < 0x800) return out;
    uint32_t text = exe[0x18] | exe[0x19] << 8 | exe[0x1A] << 16 | (uint32_t)exe[0x1B] << 24;
    size_t techs = 0x80042BDC - text + 0x800;
    for (int i = 0; i < 443 && techs + (i + 1) * 0x12 <= exe.size(); i++) {
        int id = i + 1, effect = exe[techs + i * 0x12 + 0xE];
        if (effect && id < (int)g_game.skillNames.size() && g_game.skillNames[id].size() > 1)
            out[effect].insert(g_game.skillNames[id]);
    }
    return out;
}

/* Shows a folder in Explorer: ShellExecute takes a plain path, which a
   file:/// URL with .. or \ in it isn't */
static void open_folder(const std::string &dir) {
    std::error_code ec;
    std::filesystem::path p = std::filesystem::weakly_canonical(std::filesystem::u8path(dir), ec);
    if (ec) p = std::filesystem::u8path(dir);
    p.make_preferred();
#ifdef _WIN32
    ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    SDL_OpenURL(("file://" + p.u8string()).c_str());
#endif
}

/* The effects with own images in ROOT (<root>/<id>/, or its critical/) */
static std::set<int> own_effects(const std::string &root) {
    std::set<int> out;
    int n = 0;
    char **dirs = SDL_GlobDirectory(root.c_str(), "*", 0, &n);
    for (int i = 0; i < n; i++) {
        char *end;
        long id = strtol(dirs[i], &end, 10);
        if (*end || end == dirs[i]) continue;
        for (const char *sub : {"", "/critical"}) {
            int k = 0;
            char **pngs = SDL_GlobDirectory((root + "/" + dirs[i] + sub).c_str(), "*.png", SDL_GLOB_CASEINSENSITIVE, &k);
            SDL_free(pngs);
            if (k > 0) out.insert((int)id);
        }
    }
    SDL_free(dirs);
    return out;
}

