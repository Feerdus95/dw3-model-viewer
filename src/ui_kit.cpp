/* The look: theme, fonts, messages and small widgets. Declared in ui_kit.h. */
#include "ui_kit.h"
#include <SDL3/SDL.h>
#include <cstdarg>
#include <filesystem>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

static const char *ICONS_ALL = IC_PLAY IC_PAUSE IC_RESTART IC_STEP IC_SAVE IC_DELETE IC_COPY IC_PASTE IC_UNDO IC_REDO
    IC_ADD IC_CLOSE IC_UP IC_DOWN IC_SEARCH IC_INFO IC_HELP IC_FOLDER IC_OPEN IC_WARN IC_ERROR IC_OK IC_EYE IC_HIDE
    IC_FIT IC_IMPORT IC_EXPORT IC_LOOP IC_FILES IC_STAGE IC_MODEL IC_MOTION IC_FIGHTER IC_EFFECT IC_TEXTURE IC_CAMERA;


ImFont *g_fontBold, *g_fontTitle, *g_fontMono;

void ui_fonts(float dpi) {
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

void ui_theme(float dpi) {
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

/* --- messages --- */

std::vector<Note> g_notes;

void note(int kind, const std::string &text) {
    if (!text.empty()) g_notes.push_back({kind, text, SDL_GetTicks() / 1000.0});
}
ImVec4 note_color(int kind) {
    return kind == NOTE_OK ? C_OK : kind == NOTE_WARN ? C_WARN : kind == NOTE_ERROR ? C_ERROR : ImGui::GetStyleColorVec4(ImGuiCol_Text);
}
const char *note_icon(int kind) {
    return kind == NOTE_OK ? IC_OK : kind == NOTE_WARN ? IC_WARN : kind == NOTE_ERROR ? IC_ERROR : IC_INFO;
}

/* --- small widgets --- */

void heading(const char *text) {
    ImGui::Spacing();
    ImGui::PushFont(g_fontBold);
    ImGui::SeparatorText(text);
    ImGui::PopFont();
}
void title(const char *fmt, ...) {
    va_list a;
    va_start(a, fmt);
    ImGui::PushFont(g_fontTitle);
    ImGui::TextWrappedV(fmt, a);
    ImGui::PopFont();
    va_end(a);
}
void hint(const char *fmt, ...) {
    va_list a;
    va_start(a, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrappedV(fmt, a);
    ImGui::PopStyleColor();
    va_end(a);
}
void tip(const char *fmt, ...) {
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
void chip(const char *text, ImVec4 col) {
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
bool details(const char *label) {
    ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
    bool open = ImGui::CollapsingHeader(label);
    ImGui::PopStyleColor(3);
    return open;
}
/* a button in the accent (KIND 1) or the danger color (2) */
bool button(const char *label, int kind, ImVec2 size) {
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
bool segmented(const char *id, int *v, std::initializer_list<const char *> labels) {
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
void same_line_if_fits(const char *label) {
    float w = ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2;
    float x = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x;
    if (x + w <= ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) ImGui::SameLine();
}
/* a path as people read it: no "..", Windows' slashes */
std::string nice_path(const std::string &p) {
    std::error_code ec;
    std::filesystem::path c = std::filesystem::weakly_canonical(std::filesystem::u8path(p), ec);
    if (ec) return p;
    c.make_preferred();
    return c.u8string();
}
/* a label and its value on one line, the value in the technical font */
void field(const char *label, const char *fmt, ...) {
    va_list a;
    va_start(a, fmt);
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(ImGui::GetFontSize() * 7.5f);
    ImGui::PushFont(g_fontMono);
    ImGui::TextV(fmt, a);
    ImGui::PopFont();
    va_end(a);
}

