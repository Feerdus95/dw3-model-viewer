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
   billboards (an upright PNG at a point), emitters (a PNG falling through an
   area, like the leaves) and 3D models (an OBJ with a PNG texture) of
   data/mods/fightstages/<id>/props.json, with their files beside it. Positions
   are the port's world: y up, z as the game's. */
struct StageProp {
    bool emitter = false, model = false;
    std::string image; /* a PNG's name in the folder */
    std::string obj;   /* a model's OBJ */
    float pos[3] = {0, 0, 0}; /* a billboard's base, a model's foot; an emitter's area centre */
    float size[2] = {100, 100}; /* a billboard's width, height; an emitter's smallest, biggest */
    float tint[3] = {255, 255, 255};
    float scale = 1, sway = 0, rot = 0; /* size factor, wind sway, turn (models, and billboards when fixed) */
    bool fixed = false;                 /* a billboard flat in the world instead of facing the camera */
    float fog = 1;                      /* how much of the fog it takes: 0 none */
    int count = 20;                     /* emitters: particles */
    float radius = 3000, top = 4000, fall = 1, wind = 1;
};

/* What the whole list shares: the wind's strength for its props (a factor of
   the stage's) and, if set, a fog of its own instead of the battle's */
struct PropsMeta {
    float wind = 1;
    bool fog = false;
    float fogColor[3] = {140, 150, 190}; /* 0-255 */
    float fogAmount = 0.6f, fogFrom = 0.3f;
    int leaves = -1; /* falling leaves for the stage, 0-5000; -1: the stage's own (stages.ini) */
};

/* DIR's props.json; false (and PROPS empty) when there is none */
bool props_load(const std::string &dir, std::vector<StageProp> &props, PropsMeta &meta);
/* writes it, or deletes it when PROPS is empty */
bool props_save(const std::string &dir, const std::vector<StageProp> &props, const PropsMeta &meta);

/* An OBJ's triangles, 8 floats a vertex (x, y, z, u, v, nx, ny, nz; v down), and its height */
struct ObjMesh {
    std::vector<float> v;
    float miny = 0, h = 1;
};
bool obj_load(const std::string &path, ObjMesh &mesh);
/* the PNG its MTL gives as the diffuse map, as a name beside the OBJ; "" none */
std::string obj_texture(const std::string &path);
