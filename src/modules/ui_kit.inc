/* --- the look: theme, fonts, icons, messages, small widgets, and finding
   what the view shows under the mouse --- */

/* Segoe Fluent Icons / Segoe MDL2 Assets glyphs: Windows' own fonts, read
   from its Fonts folder (nothing shipped) */
#define IC_PLAY     "\xee\x9d\xa8"
#define IC_PAUSE    "\xee\x9d\xa9"
#define IC_RESTART  "\xee\x9c\xac"
#define IC_STEP     "\xee\xa2\x93"
#define IC_SAVE     "\xee\x9d\x8e"
#define IC_DELETE   "\xee\x9d\x8d"
#define IC_COPY     "\xee\xa3\x88"
#define IC_PASTE    "\xee\x9d\xbf"
#define IC_UNDO     "\xee\x9e\xa7"
#define IC_REDO     "\xee\x9e\xa6"
#define IC_ADD      "\xee\x9c\x90"
#define IC_CLOSE    "\xee\x9c\x91"
#define IC_UP       "\xee\x9c\x8e"
#define IC_DOWN     "\xee\x9c\x8d"
#define IC_SEARCH   "\xee\x9c\xa1"
#define IC_INFO     "\xee\xa5\x86"
#define IC_HELP     "\xee\xa2\x97"
#define IC_FOLDER   "\xee\xa2\xb7"
#define IC_OPEN     "\xee\xa3\xa5"
#define IC_WARN     "\xee\x9e\xba"
#define IC_ERROR    "\xee\x9e\x83"
#define IC_OK       "\xee\x9c\xbe"
#define IC_EYE      "\xee\xa2\x90"
#define IC_HIDE     "\xee\xb4\x9a"
#define IC_FIT      "\xee\xa6\xa6"
#define IC_IMPORT   "\xee\xa2\xb5"
#define IC_EXPORT   "\xee\xb7\xa1"
#define IC_LOOP     "\xee\xa3\xae"
#define IC_FILES    "\xee\xa3\xb1"
#define IC_STAGE    "\xee\x9d\xb4"
#define IC_MODEL    "\xee\x9d\xbb"
#define IC_MOTION   "\xee\xa0\x85"
#define IC_FIGHTER  "\xee\x9f\x81"
#define IC_EFFECT   "\xee\x9e\x90"
#define IC_TEXTURE  "\xee\xa2\xb9"
#define IC_CAMERA   "\xee\x9c\xa2"
static const char *ICONS_ALL = IC_PLAY IC_PAUSE IC_RESTART IC_STEP IC_SAVE IC_DELETE IC_COPY IC_PASTE IC_UNDO IC_REDO
    IC_ADD IC_CLOSE IC_UP IC_DOWN IC_SEARCH IC_INFO IC_HELP IC_FOLDER IC_OPEN IC_WARN IC_ERROR IC_OK IC_EYE IC_HIDE
    IC_FIT IC_IMPORT IC_EXPORT IC_LOOP IC_FILES IC_STAGE IC_MODEL IC_MOTION IC_FIGHTER IC_EFFECT IC_TEXTURE IC_CAMERA;

/* the palette: the UI's accent is blue, so the view's yellow stays the selection's own */
static const ImVec4 C_ACCENT = ImVec4(0.30f, 0.55f, 1.00f, 1), C_OK = ImVec4(0.35f, 0.80f, 0.45f, 1),
                    C_WARN = ImVec4(1.00f, 0.72f, 0.25f, 1), C_ERROR = ImVec4(1.00f, 0.42f, 0.40f, 1),
                    C_SELECT = ImVec4(1.00f, 0.85f, 0.20f, 1), C_DIM = ImVec4(0.55f, 0.58f, 0.63f, 1);

static ImFont *g_fontBold, *g_fontTitle, *g_fontMono;

static void ui_fonts(float dpi) {
    ImGuiIO &io = ImGui::GetIO();
    std::string dir;
#ifdef _WIN32
    char win[MAX_PATH];
    if (GetWindowsDirectoryA(win, MAX_PATH)) dir = std::string(win) + "\\Fonts\\";
#endif
    auto exists = [&](const char *f) { return !dir.empty() && SDL_GetPathInfo((dir + f).c_str(), nullptr); };
    static ImVector<ImWchar> icons; /* the atlas reads it when it builds */
    ImFontGlyphRangesBuilder rb;
    rb.AddText(ICONS_ALL);
    rb.BuildRanges(&icons);
    const char *iconFile = exists("SegoeIcons.ttf") ? "SegoeIcons.ttf" : "segmdl2.ttf";
    auto add = [&](const char *file, float size, bool withIcons) -> ImFont * {
        if (!exists(file)) return nullptr;
        ImFont *f = io.Fonts->AddFontFromFileTTF((dir + file).c_str(), size);
        if (f && withIcons && exists(iconFile)) {
            ImFontConfig c;
            c.MergeMode = true;
            c.GlyphOffset.y = size * 0.14f;
            io.Fonts->AddFontFromFileTTF((dir + iconFile).c_str(), size * 0.92f, &c, icons.Data);
        }
        return f;
    };
    ImFont *regular = add("segoeui.ttf", 15 * dpi, true);
    if (!regular) { /* not Windows: ImGui's own, scaled */
        ImFontConfig c;
        c.SizePixels = 13 * dpi;
        io.Fonts->AddFontDefault(&c);
        return;
    }
    auto either = [](ImFont *a, ImFont *b) { return a ? a : b; };
    g_fontBold = either(add("seguisb.ttf", 15 * dpi, true), regular);
    g_fontTitle = either(add("seguisb.ttf", 20 * dpi, true), g_fontBold);
    g_fontMono = either(add("consola.ttf", 14 * dpi, false), regular);
}

static void ui_theme(float dpi) {
    ImGuiStyle &s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowPadding = ImVec2(12, 10);
    s.FramePadding = ImVec2(8, 5);
    s.ItemSpacing = ImVec2(8, 7);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.CellPadding = ImVec2(6, 4);
    s.IndentSpacing = 18;
    s.ScrollbarSize = 12;
    s.GrabMinSize = 10;
    s.WindowRounding = 0;
    s.ChildRounding = 6;
    s.FrameRounding = 5;
    s.PopupRounding = 6;
    s.ScrollbarRounding = 6;
    s.GrabRounding = 4;
    s.TabRounding = 5;
    s.WindowBorderSize = 1;
    s.PopupBorderSize = 1;
    s.SeparatorTextBorderSize = 1;
    s.SeparatorTextPadding = ImVec2(0, 4);
    s.WindowMenuButtonPosition = ImGuiDir_None;
    ImVec4 *c = s.Colors;
    auto rgb = [](int hex, float a = 1) { return ImVec4((hex >> 16 & 255) / 255.0f, (hex >> 8 & 255) / 255.0f, (hex & 255) / 255.0f, a); };
    c[ImGuiCol_Text] = rgb(0xE4E7EC);
    c[ImGuiCol_TextDisabled] = rgb(0x8C929E);
    c[ImGuiCol_WindowBg] = rgb(0x1B1D22);
    c[ImGuiCol_ChildBg] = rgb(0x16181C, 0);
    c[ImGuiCol_PopupBg] = rgb(0x23262D, 0.98f);
    c[ImGuiCol_Border] = rgb(0x30343C);
    c[ImGuiCol_BorderShadow] = rgb(0, 0);
    c[ImGuiCol_FrameBg] = rgb(0x262A31);
    c[ImGuiCol_FrameBgHovered] = rgb(0x2F343D);
    c[ImGuiCol_FrameBgActive] = rgb(0x363C47);
    c[ImGuiCol_TitleBg] = rgb(0x16181C);
    c[ImGuiCol_TitleBgActive] = rgb(0x1E2128);
    c[ImGuiCol_MenuBarBg] = rgb(0x16181C);
    c[ImGuiCol_ScrollbarBg] = rgb(0, 0);
    c[ImGuiCol_ScrollbarGrab] = rgb(0x3A3F49);
    c[ImGuiCol_ScrollbarGrabHovered] = rgb(0x474D59);
    c[ImGuiCol_ScrollbarGrabActive] = rgb(0x545B68);
    c[ImGuiCol_CheckMark] = C_ACCENT;
    c[ImGuiCol_SliderGrab] = rgb(0x4C8DFF, 0.85f);
    c[ImGuiCol_SliderGrabActive] = C_ACCENT;
    c[ImGuiCol_Button] = rgb(0x2B3039);
    c[ImGuiCol_ButtonHovered] = rgb(0x363D48);
    c[ImGuiCol_ButtonActive] = rgb(0x414A58);
    c[ImGuiCol_Header] = rgb(0x2C4D86, 0.85f);
    c[ImGuiCol_HeaderHovered] = rgb(0x2E3644);
    c[ImGuiCol_HeaderActive] = rgb(0x335BA0);
    c[ImGuiCol_Separator] = rgb(0x30343C);
    c[ImGuiCol_SeparatorHovered] = rgb(0x4C8DFF, 0.6f);
    c[ImGuiCol_SeparatorActive] = C_ACCENT;
    c[ImGuiCol_ResizeGrip] = rgb(0, 0);
    c[ImGuiCol_ResizeGripHovered] = rgb(0x4C8DFF, 0.5f);
    c[ImGuiCol_ResizeGripActive] = C_ACCENT;
    c[ImGuiCol_Tab] = rgb(0x22252B);
    c[ImGuiCol_TabHovered] = rgb(0x363D48);
    c[ImGuiCol_TabSelected] = rgb(0x2B3039);
    c[ImGuiCol_TabSelectedOverline] = C_ACCENT;
    c[ImGuiCol_TabDimmed] = rgb(0x22252B);
    c[ImGuiCol_TabDimmedSelected] = rgb(0x2B3039);
    c[ImGuiCol_TableHeaderBg] = rgb(0x22252B);
    c[ImGuiCol_TableBorderStrong] = rgb(0x30343C);
    c[ImGuiCol_TableBorderLight] = rgb(0x282C33);
    c[ImGuiCol_TableRowBgAlt] = rgb(0xFFFFFF, 0.025f);
    c[ImGuiCol_TextSelectedBg] = rgb(0x4C8DFF, 0.35f);
    c[ImGuiCol_NavHighlight] = C_ACCENT;
    c[ImGuiCol_ModalWindowDimBg] = rgb(0x000000, 0.55f);
    if (dpi > 0) s.ScaleAllSizes(dpi);
}

/* --- messages: what happened, kept for the log --- */

enum { NOTE_INFO, NOTE_OK, NOTE_WARN, NOTE_ERROR };
struct Note {
    int kind;
    std::string text;
    double time;
};
static std::vector<Note> g_notes;

static void note(int kind, const std::string &text) {
    if (!text.empty()) g_notes.push_back({kind, text, SDL_GetTicks() / 1000.0});
}
static ImVec4 note_color(int kind) {
    return kind == NOTE_OK ? C_OK : kind == NOTE_WARN ? C_WARN : kind == NOTE_ERROR ? C_ERROR : ImGui::GetStyleColorVec4(ImGuiCol_Text);
}
static const char *note_icon(int kind) {
    return kind == NOTE_OK ? IC_OK : kind == NOTE_WARN ? IC_WARN : kind == NOTE_ERROR ? IC_ERROR : IC_INFO;
}

/* --- small widgets --- */

static void heading(const char *text) {
    ImGui::Spacing();
    ImGui::PushFont(g_fontBold);
    ImGui::SeparatorText(text);
    ImGui::PopFont();
}
static void hint(const char *fmt, ...) IM_FMTARGS(1);
static void hint(const char *fmt, ...) {
    va_list a;
    va_start(a, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrappedV(fmt, a);
    ImGui::PopStyleColor();
    va_end(a);
}
static void tip(const char *fmt, ...) IM_FMTARGS(1);
static void tip(const char *fmt, ...) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) return;
    va_list a;
    va_start(a, fmt);
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
    ImGui::TextV(fmt, a);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
    va_end(a);
}
/* a rounded tag: a state or a kind */
static void chip(const char *text, ImVec4 col) {
    ImVec2 sz = ImGui::CalcTextSize(text), pad(ImGui::GetFontSize() * 0.45f, 1);
    ImVec2 p = ImGui::GetCursorScreenPos();
    p.y += (ImGui::GetFrameHeight() - sz.y - pad.y * 2) / 2;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + sz.x + pad.x * 2, p.y + sz.y + pad.y * 2), ImGui::GetColorU32(ImVec4(col.x, col.y, col.z, 0.18f)),
                      sz.y);
    dl->AddText(ImVec2(p.x + pad.x, p.y + pad.y), ImGui::GetColorU32(col), text);
    ImGui::Dummy(ImVec2(sz.x + pad.x * 2, ImGui::GetFrameHeight()));
}
/* a section that opens, quiet: the theme's header color is for what is selected */
static bool details(const char *label) {
    ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
    bool open = ImGui::CollapsingHeader(label);
    ImGui::PopStyleColor(3);
    return open;
}
/* a button in the accent (KIND 1) or the danger color (2) */
static bool button(const char *label, int kind = 0, ImVec2 size = ImVec2(0, 0)) {
    static const ImVec4 FILL[3][3] = {{}, {{0.22f, 0.43f, 0.85f, 1}, {0.28f, 0.51f, 0.95f, 1}, {0.20f, 0.38f, 0.75f, 1}},
                                      {{0.45f, 0.17f, 0.18f, 1}, {0.58f, 0.21f, 0.22f, 1}, {0.40f, 0.14f, 0.15f, 1}}};
    if (kind) {
        ImGui::PushStyleColor(ImGuiCol_Button, FILL[kind][0]);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, FILL[kind][1]);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, FILL[kind][2]);
    }
    bool hit = ImGui::Button(label, size);
    if (kind) ImGui::PopStyleColor(3);
    return hit;
}
/* buttons side by side, the chosen one lit: *V the index */
static bool segmented(const char *id, int *v, std::initializer_list<const char *> labels) {
    bool changed = false;
    int i = 0;
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, ImGui::GetStyle().ItemSpacing.y));
    for (const char *l : labels) {
        if (i) ImGui::SameLine();
        bool on = *v == i;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
        if (ImGui::Button(l)) *v = i, changed = true;
        if (on) ImGui::PopStyleColor();
        i++;
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}
/* SameLine only when a button labelled LABEL still fits the line: rows of buttons wrap */
static void same_line_if_fits(const char *label) {
    float w = ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2;
    float x = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x;
    if (x + w <= ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) ImGui::SameLine();
}
/* a path as people read it: no "..", Windows' slashes */
static std::string nice_path(const std::string &p) {
    std::error_code ec;
    std::filesystem::path c = std::filesystem::weakly_canonical(std::filesystem::u8path(p), ec);
    if (ec) return p;
    c.make_preferred();
    return c.u8string();
}
/* a label and its value on one line, the value in the technical font */
static void field(const char *label, const char *fmt, ...) IM_FMTARGS(2);
static void field(const char *label, const char *fmt, ...) {
    va_list a;
    va_start(a, fmt);
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(ImGui::GetFontSize() * 7.5f);
    ImGui::PushFont(g_fontMono);
    ImGui::TextV(fmt, a);
    ImGui::PopFont();
    va_end(a);
}

/* A list of states to go back to (undo) and forth (redo) */
template <class T> struct History {
    std::vector<T> undo, redo;
    void push(T t) {
        undo.push_back(std::move(t));
        if (undo.size() > 40) undo.erase(undo.begin());
        redo.clear();
    }
    void clear() { undo.clear(), redo.clear(); }
};

/* --- the view's geometry: where bones are on screen, and which is under a point --- */

static M4 view_proj(const View &v, int w, int h) {
    return mul(perspective(v.fov, (float)w / std::max(h, 1), v.cam.dist * 0.01f, v.cam.dist * 50), v.cam.view());
}

/* Where the mouse (MX, MY on the W x H view) meets the ground, y 0; false in the sky */
static bool ground_hit(const View &v, float w, float h, float mx, float my, float out[3]) {
    const Camera &c = v.cam;
    float e[3] = {c.target[0] + c.dist * cosf(c.pitch) * sinf(c.yaw), c.target[1] + c.dist * sinf(c.pitch), c.target[2] + c.dist * cosf(c.pitch) * cosf(c.yaw)};
    float f[3] = {c.target[0] - e[0], c.target[1] - e[1], c.target[2] - e[2]};
    float fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (float &x : f) x /= fl;
    float s[3] = {-f[2], 0, f[0]}, sl = sqrtf(s[0] * s[0] + s[2] * s[2]);
    for (float &x : s) x /= sl;
    float u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};
    float k = tanf(v.fov / 2), nx = (mx / w * 2 - 1) * k * (w / h), ny = (1 - my / h * 2) * k, d[3];
    for (int i = 0; i < 3; i++) d[i] = f[i] + s[i] * nx + u[i] * ny;
    if (d[1] > -1e-4f) return false;
    float t = -e[1] / d[1];
    if (t <= 0) return false;
    for (int i = 0; i < 3; i++) out[i] = e[i] + d[i] * t;
    return true;
}

/* A point of the world on the view (W x H, from its top left); false behind the camera */
static bool to_screen(const M4 &vp, const float p[3], float w, float h, ImVec2 &out) {
    const float *m = vp.m;
    float cw = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
    if (cw < 1e-3f) return false;
    float x = (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]) / cw, y = (m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]) / cw;
    out = ImVec2((x + 1) / 2 * w, (1 - y) / 2 * h);
    return true;
}

/* --- the stage editor's gizmo: an arrow a axis (x red, y green and up, z blue), a ring on the ground
   to turn and a box to scale; all on the view (from its top left), a constant size on screen --- */

struct Gizmo {
    bool on = false;
    float o[3] = {}, len = 0; /* its centre in the world, and an arrow's length there */
    float up = -1;            /* the y arrow's direction: -1 in a stage (y down), 1 in a model's view */
    bool rings = true;
    ImVec2 c, tip[3], ring[48];
};
static const float GZ_DIR[3][3] = {{1, 0, 0}, {0, -1, 0}, {0, 0, 1}}; /* the game's y goes down: the arrow up is -y */

static bool gizmo_make(Gizmo &g, const M4 &vp, const float o[3], float w, float h, float fov, float dpi) {
    const float *m = vp.m;
    float depth = m[3] * o[0] + m[7] * o[1] + m[11] * o[2] + m[15];
    g.on = false;
    if (depth < 1e-3f || !to_screen(vp, o, w, h, g.c)) return false;
    memcpy(g.o, o, sizeof g.o);
    g.len = 90 * dpi * 2 * depth * tanf(fov / 2) / std::max(h, 1.0f);
    for (int a = 0; a < 3; a++) {
        float p[3] = {o[0] + GZ_DIR[a][0] * g.len, o[1] + (a == 1 ? g.up : GZ_DIR[a][1]) * g.len, o[2] + GZ_DIR[a][2] * g.len};
        if (!to_screen(vp, p, w, h, g.tip[a])) return false;
    }
    for (int i = 0; i < 48; i++) {
        float t = i * 6.2831853f / 48, r = g.len * 0.7f, p[3] = {o[0] + r * cosf(t), o[1], o[2] + r * sinf(t)};
        if (!to_screen(vp, p, w, h, g.ring[i])) return false;
    }
    return g.on = true;
}

static float seg_dist(ImVec2 p, ImVec2 a, ImVec2 b) {
    ImVec2 ab = b - a;
    float l = ab.x * ab.x + ab.y * ab.y, t = l > 0 ? std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / l, 0.0f, 1.0f) : 0;
    float dx = p.x - (a.x + ab.x * t), dy = p.y - (a.y + ab.y * t);
    return sqrtf(dx * dx + dy * dy);
}

/* What is under P: 0-2 an arrow, 3 the ring, 4 the box; -1 nothing */
static int gizmo_hit(const Gizmo &g, ImVec2 p, float dpi) {
    if (!g.on) return -1;
    if (seg_dist(p, g.c, g.c) < 8 * dpi) return 4;
    float best = 7 * dpi;
    int hit = -1;
    for (int a = 0; a < 3; a++) {
        float d = seg_dist(p, g.c, g.tip[a]);
        if (d < best) best = d, hit = a;
    }
    if (hit >= 0 || !g.rings) return hit;
    for (int i = 0; i < 48; i++) {
        float d = seg_dist(p, g.ring[i], g.ring[(i + 1) % 48]);
        if (d < best) best = d, hit = 3;
    }
    return hit;
}

static void gizmo_draw(ImDrawList *dl, const Gizmo &g, ImVec2 at, int hot, float dpi) {
    static const ImU32 COL[3] = {IM_COL32(236, 84, 84, 255), IM_COL32(118, 208, 96, 255), IM_COL32(84, 142, 245, 255)};
    static const char *NAME[3] = {"X", "Y", "Z"};
    const ImU32 yel = IM_COL32(255, 217, 51, 255), shade = IM_COL32(0, 0, 0, 120);
    for (int i = 0; g.rings && i < 48; i++) {
        ImVec2 a = at + g.ring[i], b = at + g.ring[(i + 1) % 48];
        dl->AddLine(a, b, shade, 4.5f * dpi);
        dl->AddLine(a, b, hot == 3 ? yel : IM_COL32(255, 170, 60, 235), (hot == 3 ? 3.0f : 2.0f) * dpi);
    }
    for (int a = 0; a < 3; a++) {
        ImU32 c = hot == a ? yel : COL[a];
        ImVec2 s = at + g.c, t = at + g.tip[a], d = t - s;
        float l = sqrtf(d.x * d.x + d.y * d.y);
        if (l < 1) continue;
        d = d * (1 / l);
        ImVec2 n(-d.y, d.x);
        dl->AddLine(s, t, shade, 5 * dpi);
        dl->AddLine(s, t, c, 2.5f * dpi);
        dl->AddTriangleFilled(t + d * (11 * dpi), t - d * (2 * dpi) + n * (5.5f * dpi), t - d * (2 * dpi) - n * (5.5f * dpi), c);
        ImVec2 ts = ImGui::CalcTextSize(NAME[a]);
        ImVec2 lp = t + d * (22 * dpi) - ts * 0.5f;
        dl->AddText(lp + ImVec2(1, 1), IM_COL32(0, 0, 0, 200), NAME[a]);
        dl->AddText(lp, c, NAME[a]);
    }
    ImVec2 c = at + g.c;
    dl->AddRectFilled(c - ImVec2(5 * dpi, 5 * dpi), c + ImVec2(5 * dpi, 5 * dpi), hot == 4 ? yel : IM_COL32(236, 238, 242, 255));
    dl->AddRect(c - ImVec2(5 * dpi, 5 * dpi), c + ImVec2(5 * dpi, 5 * dpi), IM_COL32(0, 0, 0, 200));
}

/* The box (in the view's world) around BONES' vertices; false when none shows */
static bool bones_box(const Scene &s, const std::vector<int> &bones, float lo[3], float hi[3]) {
    if (!s.model) return false;
    const Model &m = *s.model;
    bool any = false;
    for (int i = 0; i < 3; i++) lo[i] = 1e30f, hi[i] = -1e30f;
    for (int b : bones) {
        if (b < 1 || b >= (int)m.bones.size() || !m.pose[b].visible) continue;
        M4 w = bone_world(s, b);
        for (const Vec3s &v : m.meshes[m.bones[b].mesh].verts) {
            for (int i = 0; i < 3; i++) {
                float c = w.m[i] * v.x + w.m[4 + i] * v.y + w.m[8 + i] * v.z + w.m[12 + i];
                lo[i] = std::min(lo[i], c), hi[i] = std::max(hi[i], c);
            }
            any = true;
        }
    }
    return any;
}

/* The camera on BONES, as F does the whole model */
static bool frame_bones(const Scene &s, View &v, const std::vector<int> &bones) {
    float lo[3], hi[3];
    if (!bones_box(s, bones, lo, hi)) return false;
    float r = 0;
    for (int i = 0; i < 3; i++) v.cam.target[i] = (lo[i] + hi[i]) / 2, r = std::max(r, hi[i] - lo[i]);
    v.cam.dist = std::max(r / 2, 50.0f) * 3.4f;
    v.gameCamera = -1;
    return true;
}

/* The bone whose polygon is nearest the camera at (PX, PY) of the view
   (W x H, from its top left); -1 none */
static int pick_bone(const Scene &s, const View &v, float w, float h, float px, float py) {
    if (!s.model) return -1;
    const Model &m = *s.model;
    M4 vp = view_proj(v, (int)w, (int)h);
    float best = 1e30f;
    int hit = -1;
    std::vector<float> sx, sy, sz;
    std::vector<char> ok;
    for (size_t b = 1; b < m.bones.size(); b++) {
        if (!m.pose[b].visible || s.hidden[b] || !s.count[b]) continue;
        M4 mvp = mul(vp, bone_world(s, (int)b));
        const Mesh &me = m.meshes[m.bones[b].mesh];
        size_t n = me.verts.size();
        sx.resize(n), sy.resize(n), sz.resize(n), ok.resize(n);
        for (size_t i = 0; i < n; i++) {
            const float *q = mvp.m;
            float x = me.verts[i].x, y = me.verts[i].y, z = me.verts[i].z;
            float cw = q[3] * x + q[7] * y + q[11] * z + q[15];
            ok[i] = cw > 1e-3f;
            if (!ok[i]) continue;
            sx[i] = ((q[0] * x + q[4] * y + q[8] * z + q[12]) / cw + 1) / 2 * w;
            sy[i] = (1 - (q[1] * x + q[5] * y + q[9] * z + q[13]) / cw) / 2 * h;
            sz[i] = (q[2] * x + q[6] * y + q[10] * z + q[14]) / cw;
        }
        auto tri = [&](int a, int c, int d) {
            if (!ok[a] || !ok[c] || !ok[d]) return;
            float den = (sy[c] - sy[d]) * (sx[a] - sx[d]) + (sx[d] - sx[c]) * (sy[a] - sy[d]);
            if (fabsf(den) < 1e-6f) return;
            float l1 = ((sy[c] - sy[d]) * (px - sx[d]) + (sx[d] - sx[c]) * (py - sy[d])) / den;
            float l2 = ((sy[d] - sy[a]) * (px - sx[d]) + (sx[a] - sx[d]) * (py - sy[d])) / den, l3 = 1 - l1 - l2;
            if (l1 < 0 || l2 < 0 || l3 < 0) return;
            float z = l1 * sz[a] + l2 * sz[c] + l3 * sz[d];
            if (z > -1 && z < best) best = z, hit = (int)b;
        };
        for (const Poly &p : me.polys) {
            tri(p.idx[0], p.idx[1], p.idx[2]);
            if (p.quad) tri(p.idx[1], p.idx[3], p.idx[2]);
        }
    }
    return hit;
}

/* A stage's part as an edit in progress would place it, from where it was
   built (START: move, turn, scale): the move from its old middle to its new
   one, turned and scaled about it, as StageEdit::placed does */
static void part_preview(const StagePart &p, const float start[5], float out[16]) {
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const auto &v : p.verts)
        for (int i = 0; i < 3; i++) lo[i] = std::min(lo[i], v[i]), hi[i] = std::max(hi[i], v[i]);
    float o[3], n[3];
    for (int i = 0; i < 3; i++) {
        float c = p.verts.empty() ? 0 : (lo[i] + hi[i]) / 2;
        o[i] = c + start[i], n[i] = c + p.move[i];
    }
    float k = start[4] != 0 ? p.scale / start[4] : 1, t = (p.turn - start[3]) * 0.017453293f;
    float cs = cosf(t) * k, sn = sinf(t) * k;
    float m[16] = {cs, 0, -sn, 0, 0, k, 0, 0, sn, 0, cs, 0,
                   n[0] - (cs * o[0] + sn * o[2]), n[1] - k * o[1], n[2] - (-sn * o[0] + cs * o[2]), 1};
    memcpy(out, m, sizeof m);
}
