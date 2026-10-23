/* What a UI section that lives in its own source file gets from main(): references to the state it uses, nothing more. A section adds a
   field only for something it really reads or calls. */
#pragma once
#include "viewer_types.h"
#include <functional>
#include <utility>
#include <vector>

/* the texture page Textures shows: main's loop decodes it and draws it in the view */
struct TexPage {
    std::vector<uint32_t> pixels;
    int w = 0, h = 0;
    bool fitPending = true; /* the view fits the page on the next frame */
};

struct App {
    Scene &scene;
    View &view;
    std::function<void()> display_options; /* the Display block shared by sections (it also frames and shows the bones: main's selection) */
    TexPage &tex;
    std::function<std::string(int)> thing_name;              /* what a bone is called (a stage's part on a stage) */
    std::function<int(int)> thing_polys;                     /* its polygons */
    std::function<void(uint16_t, uint16_t)> save_page;       /* Textures: the page as a BMP next to the exe */
    std::function<void(uint16_t, uint16_t)> palette;         /* Textures: the palette editor (the model editor's undo is main's) */
};

/* Motions (motions.cpp) */
void draw_motions_browser(App &app);
void draw_motions_inspector(App &app);

/* Textures (textures.cpp) */
void draw_textures_browser(App &app);
void draw_textures_inspector(App &app);
std::vector<std::pair<uint16_t, uint16_t>> tex_combos(const Scene &scene); /* the page and CLUT pairs the model's polygons read (the view draws the first ones) */
