/* What the viewer's draw code and its UI sections share: the matrix and orbit camera, the model on the card (Scene) and how it is
   shown (View). No drawing here: render(), upload() and the like are in main.cpp's modules. */
#pragma once
#include "game.h"
#include "model.h"
#include <SDL3/SDL_opengl.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

struct M4 {
    float m[16];
};

/* The camera, orbiting its target; the PSX's y is down and its z away from
   the camera, so the model goes through diag(1, -1, -1) */
struct Camera {
    float yaw = 0.6f, pitch = 0.25f, dist = 1500, target[3] = {0, 300, 0};
    M4 view() const {
        float e[3] = {target[0] + dist * cosf(pitch) * sinf(yaw), target[1] + dist * sinf(pitch),
                      target[2] + dist * cosf(pitch) * cosf(yaw)};
        float f[3] = {target[0] - e[0], target[1] - e[1], target[2] - e[2]};
        float fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
        for (float &x : f) x /= fl;
        float s[3] = {f[1] * 0 - f[2] * 1, f[2] * 0 - f[0] * 0, f[0] * 1 - f[1] * 0}; /* f x up */
        float sl = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
        for (float &x : s) x /= sl;
        float u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};
        M4 r = {{s[0], u[0], -f[0], 0, s[1], u[1], -f[1], 0, s[2], u[2], -f[2], 0, 0, 0, 0, 1}};
        r.m[12] = -(s[0] * e[0] + s[1] * e[1] + s[2] * e[2]);
        r.m[13] = -(u[0] * e[0] + u[1] * e[1] + u[2] * e[2]);
        r.m[14] = f[0] * e[0] + f[1] * e[1] + f[2] * e[2];
        return r;
    }
};

static const M4 PSX_TO_GL = {{1, 0, 0, 0, 0, -1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1}};

/* --- the model on the card --- */

struct Scene {
    std::unique_ptr<Model> model;
    std::vector<int> first, count; /* each bone's vertices */
    std::vector<bool> hidden;
    int selected = -1;
    std::vector<uint8_t> mark; /* each bone's highlight: 1 under the mouse, 2 selected */
    /* an edit in progress, drawn before it's built: bones previewFirst up
       to previewEnd moved by preview (in the model's space) */
    float preview[16] = {};
    int previewFirst = 0, previewEnd = 0;
    GLuint vao = 0, vbo = 0, vram = 0;
    float radius = 500, center[3] = {0, 0, 0};
    int fid = -1;
    std::string path;
    std::vector<const Fighter *> fighters; /* who uses the file */
    int fighter = 0;                       /* the one shown */
};

/* --- the viewer --- */

struct View {
    Camera cam;
    int textures = 1; /* 0 off, 1 as the PSX, 2 bilinear */
    int lighting = 2; /* 0 off, 1 the viewer's light, 2 a stage's */
    int stage = 0;
    float fov = 0.8f; /* vertical, radians; a game camera's from its projection */
    int gameCamera = -1;
    bool face = true;
    bool wireframe = false, skeleton = false, cull = false, grid = true;
    bool marks = false; /* a cross where each effect of a script is */
    bool dummy = false; /* a stand-in enemy at its battle home, and the ground out to it */
    bool playing = true, loop = true;
    float speed = 1, ticks = 0;
    int texCombo = 0;
    float texZoom = 2;
    bool uvs = true;
    bool stageView = false;          /* a fight stage: its background, no grid */
    float stageBg[3] = {0, 0, 0};
    float pfog[4] = {}, pfogz[2] = {}; /* the props' own fog when set (alpha 0: none): colour and amount; where it starts and ends */
};
