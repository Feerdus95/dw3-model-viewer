/* What a UI section that lives in its own source file gets from main(): references to the state it uses, nothing more. A section adds a
   field only for something it really reads or calls. */
#pragma once
#include "viewer_types.h"
#include <functional>

struct App {
    Scene &scene;
    View &view;
    std::function<void()> display_options; /* the Display block shared by sections (it also frames and shows the bones: main's selection) */
};

/* Motions (motions.cpp) */
void draw_motions_browser(App &app);
void draw_motions_inspector(App &app);
