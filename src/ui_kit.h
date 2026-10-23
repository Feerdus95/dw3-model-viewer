#pragma once
#include "imgui.h"
#include <initializer_list>
#include <string>
#include <vector>

/* ===== The look: theme, fonts, icons, messages and small widgets (ui_kit.cpp) ===== */

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

/* the palette: the UI's accent is blue, so the view's yellow stays the selection's own */
inline const ImVec4 C_ACCENT = ImVec4(0.30f, 0.55f, 1.00f, 1), C_OK = ImVec4(0.35f, 0.80f, 0.45f, 1),
                    C_WARN = ImVec4(1.00f, 0.72f, 0.25f, 1), C_ERROR = ImVec4(1.00f, 0.42f, 0.40f, 1),
                    C_SELECT = ImVec4(1.00f, 0.85f, 0.20f, 1), C_DIM = ImVec4(0.55f, 0.58f, 0.63f, 1);

extern ImFont *g_fontBold, *g_fontTitle, *g_fontMono;
void ui_fonts(float dpi);
void ui_theme(float dpi);

/* messages: what happened, kept for the log */
enum { NOTE_INFO, NOTE_OK, NOTE_WARN, NOTE_ERROR };
struct Note {
    int kind;
    std::string text;
    double time;
};
extern std::vector<Note> g_notes;
void note(int kind, const std::string &text);
ImVec4 note_color(int kind);
const char *note_icon(int kind);

/* small widgets */
void heading(const char *text);
void title(const char *fmt, ...) IM_FMTARGS(1); /* wrapped, in the title font */
void hint(const char *fmt, ...) IM_FMTARGS(1);
void tip(const char *fmt, ...) IM_FMTARGS(1);
void chip(const char *text, ImVec4 col);
bool details(const char *label);
bool button(const char *label, int kind = 0, ImVec2 size = ImVec2(0, 0));
bool segmented(const char *id, int *v, std::initializer_list<const char *> labels);
void same_line_if_fits(const char *label);
void same_line_if_width(float w);
bool check_box(const char *label, bool *v); /* ImGui::Checkbox, its label wrapping when the line is short */
float check_width(const char *label); /* a checkbox's or radio button's width */
std::string nice_path(const std::string &p);
void field(const char *label, const char *fmt, ...) IM_FMTARGS(2);
