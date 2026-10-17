/* An effect's own images for the native port (its platform/effects.c): the
   PNGs of a folder, data/mods/effects/<id>/ (critical/ in it for a critical
   hit), and its effect.json, which says how they play:
     {"frames": [["00.png", 2], ...], "x": 0, "y": 0, "scale": 1, "blend": "alpha"}
   each frame a PNG and its ticks (1/60 s, as the battle counts time), the
   images moved by (x, y) screen pixels and scaled, blended by their alpha or
   added ("add"). Without it, every PNG in their names' order, 2 ticks each.
   An image is the 320x240 screen around the effect's point, at any size of
   that shape. No OpenGL here. */
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct CustomFrame {
    std::string file;
    int ticks = 2;
};

struct CustomImage {
    int w = 0, h = 0;
    std::vector<uint32_t> px; /* RGBA, R in the low byte */
};

struct CustomEffect {
    std::string dir;
    std::vector<CustomFrame> frames;
    float x = 0, y = 0, scale = 1;
    bool add = false;
    bool hasJson = false;
    std::map<std::string, CustomImage> images; /* the folder's PNGs, by name */
    std::string error;

    /* reads DIR's PNGs and effect.json; false when it has no PNGs */
    bool load(const std::string &dir);
    /* writes effect.json */
    bool save();
    /* copies PATHS (PNGs) into the folder, made if missing, and adds them
       as frames, in their names' order */
    int import(const std::vector<std::string> &paths);
    int duration() const; /* in ticks */
    int frameAt(int tick) const; /* the frame playing at TICK, -1: none */
    /* the frame at TICK over RGBA pixels (W x H), the effect's point at OX, OY */
    void draw(int tick, uint32_t *rgba, int w, int h, int ox, int oy) const;
};

/* A fight stage's props for the native port (its platform/stagefx.c): the
   billboards (an upright PNG at a point) and emitters (a PNG falling through
   an area, like the leaves) of data/mods/fightstages/<id>/props.json, with
   their PNGs beside it. Positions are the port's world: y up, z as the game's. */
struct StageProp {
    bool emitter = false;
    std::string image; /* a PNG's name in the folder */
    float pos[3] = {0, 0, 0}; /* a billboard's base; an emitter's area centre */
    float size[2] = {100, 100}; /* a billboard's width, height; an emitter's smallest, biggest */
    float tint[3] = {255, 255, 255};
    float scale = 1, sway = 0, rot = 0; /* billboards: size factor, wind sway, turn when fixed */
    bool fixed = false;                 /* flat in the world instead of facing the camera */
    int count = 20;                     /* emitters: particles */
    float radius = 3000, top = 4000, fall = 1, wind = 1;
};

/* DIR's props.json; false (and PROPS empty) when there is none */
bool props_load(const std::string &dir, std::vector<StageProp> &props);
/* writes it, or deletes it when PROPS is empty */
bool props_save(const std::string &dir, const std::vector<StageProp> &props);
