/* Textures: the texture pages (page + CLUT pairs) the model reads, and the page's legend and selected bone. The palette editor and the BMP save
   are main's (they use the model editor's undo): App.palette and App.save_page. */
#include "app.h"
#include "ui_kit.h"
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <set>

std::vector<std::pair<uint16_t, uint16_t>> tex_combos(const Scene &scene) {
    std::vector<std::pair<uint16_t, uint16_t>> combos;
    std::set<std::pair<uint16_t, uint16_t>> seen;
    if (!scene.model) return combos;
    for (const Mesh &me : scene.model->meshes)
        for (const Poly &p : me.polys)
            if (p.textured && seen.insert({(uint16_t)(p.tpage & 0x19F), p.clut}).second)
                combos.push_back({(uint16_t)(p.tpage & 0x19F), p.clut});
    return combos;
}
static std::string combo_label(std::pair<uint16_t, uint16_t> c) {
    static const char *DEPTH[] = {"4-bit", "8-bit", "16-bit", "?"};
    char l[64];
    snprintf(l, sizeof l, "%s page %d,%d  CLUT %d,%d", DEPTH[tpage_depth(c.first)], tpage_x(c.first), tpage_y(c.first),
             clut_x(c.second), clut_y(c.second));
    return std::string(l);
}
void draw_textures_browser(App &app) {
    Scene &scene = app.scene;
    View &view = app.view;
    std::vector<std::pair<uint16_t, uint16_t>> combos = tex_combos(scene);
    if (combos.empty()) return hint("No textured polygons.");
    hint("Each page with a palette (CLUT) the polygons read:");
    for (int i = 0; i < (int)combos.size(); i++) {
        std::string l = combo_label(combos[i]);
        ImGui::PushID(i);
        if (ImGui::Selectable(l.c_str(), view.texCombo == i)) view.texCombo = i, app.tex.fitPending = true;
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem(IC_COPY "  Copy")) ImGui::SetClipboardText(l.c_str());
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    heading("TIMs");
    for (const Tim &t : scene.model->tims)
        hint("%dx%d halfwords at %d,%d%s", t.w, t.h, t.x, t.y, t.rlen ? " (RLEN)" : "");
}
void draw_textures_inspector(App &app) {
    Scene &scene = app.scene;
    View &view = app.view;
    std::vector<std::pair<uint16_t, uint16_t>> combos = tex_combos(scene);
    if (combos.empty()) return hint("No textured polygons.");
    auto [tp, cl] = combos[std::min(view.texCombo, (int)combos.size() - 1)];
    title("Texture page");
    ImGui::PushFont(g_fontMono);
    ImGui::TextWrapped("%s", combo_label({tp, cl}).c_str());
    ImGui::PopFont();
    check_box("UVs", &view.uvs);
    tip("Where the polygons take their texels from");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderFloat("##zoom", &view.texZoom, 0.5f, 12, "zoom %.1fx");
    if (ImGui::Button(IC_FIT "  Fit")) app.tex.fitPending = true;
    tip("The whole page in the view (F)");
    same_line_if_fits(IC_SAVE "  Save BMP");
    if (ImGui::Button(IC_SAVE "  Save BMP")) app.save_page(tp, cl);
    app.palette(tp, cl);
    heading("Legend");
    auto swatch = [](ImU32 c, const char *text) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float y = p.y + ImGui::GetTextLineHeight() / 2, w = ImGui::GetFontSize() * 2;
        ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, y), ImVec2(p.x + w, y), c, 2);
        ImGui::Dummy(ImVec2(w, ImGui::GetTextLineHeight()));
        ImGui::SameLine();
        ImGui::TextUnformatted(text);
    };
    swatch(IM_COL32(80, 255, 120, 200), "a polygon's UVs");
    swatch(IM_COL32(255, 220, 60, 255), "the selected bone's");
    swatch(IM_COL32(140, 205, 255, 255), "under the mouse");
    hint("Click a polygon in the view to select its bone; the Model section shows it.");
    heading("Selected bone");
    if (scene.selected > 0) {
        ImGui::PushFont(g_fontBold);
        ImGui::TextColored(C_SELECT, "%s", app.thing_name(scene.selected).c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextDisabled("%d polygons", app.thing_polys(scene.selected));
    } else {
        hint("None");
    }
}

