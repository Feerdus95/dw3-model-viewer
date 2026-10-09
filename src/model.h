/* A battle model of Digimon World 2003, read the way FIGHTSTG reads it
   (dw3_decomp: src/fightstg/fightstg.c, include/fightstg/model.h). Nothing
   here knows about OpenGL. */
#pragma once
#include "game.h"
#include <cstdint>
#include <string>
#include <vector>

/* A file of the disc: an archive, a table of offsets from its start */
struct Archive {
    const uint8_t *data = nullptr;
    size_t size = 0;
    int count() const;                 /* entries: the first offset / 4 */
    const uint8_t *entry(int i) const; /* nullptr when out of the file */
    size_t entry_size(int i) const;    /* up to the next entry (or the end) */
};

struct Vec3s {
    int16_t x, y, z;
};

/* A key of a bone's track: x, y, z at a frame (an SVECTOR, its pad the frame) */
struct Key {
    int16_t x, y, z, frame;
};

/* A polygon of a mesh, as FIGHTSTG_drawMesh's state has it when it adds it */
struct Poly {
    bool quad, textured, lit, gouraud;
    int abr;          /* semi-transparency mode + 1, 0 opaque */
    int idx[4];       /* vertices */
    int nrm[4];       /* normals, when lit */
    uint8_t uv[4][2]; /* with the command's offset */
    uint16_t tpage, clut;
    uint8_t color[3]; /* the flat color, when not lit */
};

struct Mesh {
    int entry;                /* in the file */
    std::vector<Vec3s> verts; /* model units */
    std::vector<Vec3s> normals;
    std::vector<Poly> polys;
};

struct Bone {
    int parent;
    int mesh;     /* index in Model.meshes, -1 for the root */
    int keyEntry; /* its keys' archive, an entry of the file */
    std::vector<Key> keys[3]; /* positions, rotations (4096 = 360°), scales (4096 = 1) */
};

/* A motion: its steps (MotionStep), as in the motions' archive */
struct MotionStep {
    int16_t index, count, frame, endFrame;
};

/* A TIM of the model's textures, unpacked */
struct Tim {
    int x, y, w, h; /* where FIGHTSTG puts it in VRAM, in halfwords */
    bool rlen;
    std::vector<uint16_t> pixels;
};

/* The pose state of FIGHTSTG's Model (keyframes, blending), ported as it is */
struct Pose {
    Vec3s pos, rot, scale;
    Vec3s prevPos, prevRot, prevScale;
    bool visible;
    float world[16]; /* column-major, model units */
};

/* Where the viewer puts a model's textures in VRAM (FIGHTSTG uses
   FIGHTSTG_fighterTexPos; any 64-halfword column works) */
const int TEX_X = 512, TEX_Y = 0;

struct Model {
    std::string name;
    const Fighter *fighter = nullptr; /* who uses it, when the fighters' file says */
    std::vector<std::vector<ScriptLine>> scripts;
    std::vector<uint16_t> vram; /* its textures in place, and the face's changes */
    FaceState face;
    std::vector<uint8_t> file;
    Archive arc;
    int boneEntry = -1, motionEntry = -1, texEntry = -1;
    std::vector<Bone> bones; /* [0] the root */
    std::vector<Mesh> meshes;
    std::vector<Tim> tims;
    std::vector<std::vector<MotionStep>> motions; /* [i] is motion i + 1 */
    std::string error;

    /* playback (FIGHTSTG_setMotion / stepMotion / poseBones) */
    std::vector<Pose> pose;
    int motion = 0, keyframe = 0, keyframeCount = 0, frame = 0;
    bool motionDone = false, blending = false;
    int blend = 0, blendTarget = 0, idleFrames[2] = {0, 0};
    std::vector<uint16_t> keyframes, blendTargets, blendSources;

    /* FIGHTER, when given, names its bone list, motions and scripts */
    bool load(const std::string &name, std::vector<uint8_t> bytes, const Fighter *fighter = nullptr);
    bool stepFace(); /* one battle frame of the face; true when VRAM changed */
    void setMotion(int m);  /* 1-based, as the game's */
    void evaluate();        /* the pose at the current keyframe */
    void advance(int ticks); /* steps the keyframe on, as FIGHTSTG_stepMotion */
    void computeWorld();
};

/* Finds the bone list in a file: the entry whose words fit
   {texFile, count, (parent, mesh, keys) * count}; -1 if none */
int find_bone_entry(const Archive &a);

/* The texture page and CLUT a polygon reads, in VRAM halfwords */
inline int tpage_x(uint16_t tp) { return (tp & 15) * 64; }
inline int tpage_y(uint16_t tp) { return (tp >> 4 & 1) * 256; }
inline int tpage_depth(uint16_t tp) { return tp >> 7 & 3; } /* 0 4-bit, 1 8-bit, 2 16-bit */
inline int clut_x(uint16_t c) { return (c & 63) * 16; }
inline int clut_y(uint16_t c) { return c >> 6 & 511; }
