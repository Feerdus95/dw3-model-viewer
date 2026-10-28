/* A battle model of Digimon World 2003, read the way FIGHTSTG reads it
   (dw3_decomp: src/fightstg/fightstg.c, include/fightstg/model.h). Nothing
   here knows about OpenGL. */
#pragma once
#include "game.h"
#include <array>
#include <cstdint>
#include <map>
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
    std::vector<Key> hide;    /* DCB: x = 1 while the mesh is not drawn (the key's attribute, or a collapsed scale); the matrix still stands for the children */
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
    int entry = 0;               /* in the textures' archive */
    std::vector<uint8_t> buf;    /* the TIM as the file has it (RLEN unpacked) */
    size_t pxOff = 0;            /* where its pixels start in BUF */
};

/* The pose state of FIGHTSTG's Model (keyframes, blending), ported as it is */
struct Pose {
    Vec3s pos, rot, scale;
    Vec3s prevPos, prevRot, prevScale;
    bool visible;
    bool hide = false; /* the mesh is hidden by the animation, the matrix is not */
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
    bool dw2 = false; /* a Digimon World 2 or Digital Card Battle model (load_dw2) */
    bool dcb = false; /* a Digital Card Battle one (.dcbm: 16-bit vertex indices, flat-colored faces) */
    /* its size, adjustable (dw2.cpp): the whole model by SCALE, each bone's own mesh by MESH[bone] (about its joint), and
       where its children's joints sit by REACH[bone] (their offsets from it). All 1 is the model as the disc has it; the
       bones' lists are indexed as bones, [0] the root */
    struct Dw2Tuning {
        float scale = 1;
        std::vector<float> mesh, reach;
        std::vector<float> place; /* where a bone's own joint sits from its parent's, alone (the parent's REACH moves every bone under it) */
        std::vector<float> shape; /* shape.txt beside tuning.txt (DCB): three factors a bone, a mesh stretched per axis about its joint; not edited here */
    } tune;
    std::vector<std::vector<Vec3s>> dw2BaseVerts; /* each bone's mesh as loaded */
    std::vector<std::vector<Key>> dw2BasePos;     /* each bone's position keys as loaded */
    void dw2_retune();                            /* the meshes and positions from the base and the tuning */
    bool dw2_load_tuning(const std::string &path); /* tuning.txt: "scale F", "bone N mesh F", "bone N reach F"; false when none */
    bool dw2_save_tuning(const std::string &path) const;
    bool dw2_tuned() const;

    /* playback (FIGHTSTG_setMotion / stepMotion / poseBones) */
    std::vector<Pose> pose;
    int motion = 0, keyframe = 0, keyframeCount = 0, frame = 0;
    bool motionDone = false, blending = false;
    int blend = 0, blendTarget = 0, idleFrames[2] = {0, 0};
    std::vector<uint16_t> keyframes, blendTargets, blendSources;
    /* a live turn, in degrees about the model's axes (x y z, as Rz*Ry*Rx), of a bone and all under it, while MOTION plays: adj[motion][bone]. A
       hand adjustment (flame_adjust.inc) to look at before it is baked; rawWorld then holds the bones' worlds without it */
    std::map<int, std::map<int, std::array<float, 3>>> adj;
    std::vector<std::array<float, 16>> rawWorld;

    /* FIGHTER, when given, names its bone list, motions and scripts; else
       ENTRY (a stage's), else the first entry that reads as one; MOTIONS
       (an effect model's) the entry its motions are in */
    bool load(const std::string &name, std::vector<uint8_t> bytes, const Fighter *fighter = nullptr, int entry = -1,
              int motions = -1);
    /* a Digimon World 2 model (dw2.cpp), as dw2-extractor writes it */
    bool load_dw2(const std::string &name, std::vector<uint8_t> bytes);
    bool stepFace(); /* one battle frame of the face; true when VRAM changed */
    void setMotion(int m);  /* 1-based, as the game's */
    void evaluate();        /* the pose at the current keyframe */
    bool tpose = false;     /* the rotations replaced by a T-pose (apply_tpose); the keys' positions and scales stay */
    void apply_tpose();
    /* steps the keyframe on, as FIGHTSTG_stepMotion. ONCE: a jump back (the motion's own loop) ends it as the terminator does, holding the last keyframe */
    void advance(int ticks, bool once = false);
    void computeWorld();
};

/* Finds the bone list in a file: the entry whose words fit
   {texFile, count, (parent, mesh, keys) * count}; -1 if none */
int find_bone_entry(const Archive &a);

/* The TIMs of an archive of images, side by side 0x40 halfwords apart from
   texX, texY, unpacking the RLEN ones (timLoaderLoadArchive) */
void read_tims(const Archive &a, int texX, int texY, std::vector<Tim> &out);

/* The texture page and CLUT a polygon reads, in VRAM halfwords */
inline int tpage_x(uint16_t tp) { return (tp & 15) * 64; }
inline int tpage_y(uint16_t tp) { return (tp >> 4 & 1) * 256; }
inline int tpage_depth(uint16_t tp) { return tp >> 7 & 3; } /* 0 4-bit, 1 8-bit, 2 16-bit */
inline int clut_x(uint16_t c) { return (c & 63) * 16; }
inline int clut_y(uint16_t c) { return c >> 6 & 511; }

/* A texture page as its polygons see it, W x H texels of RGBA (R in the low
   byte), a texel of 0 transparent; STP: the texels with the semi-transparency
   bit half opaque (the ones a semi-transparent polygon blends) */
std::vector<uint32_t> decode_page(const std::vector<uint16_t> &vram, uint16_t tpage, uint16_t clut, int w, int h,
                                  bool stp = false);

/* Writes VECS over entry WHICH (0 vertices, 1 normals) of the mesh archive at MESHENTRY, in place: the count stays */
void patch_vecs(std::vector<uint8_t> &file, int meshEntry, int which, const std::vector<Vec3s> &vecs);
void patch_keys(std::vector<uint8_t> &file, int keyEntry, const std::vector<Key> &keys); /* a bone's position keys' x y z, in place */

/* Writes the TIMS (their pixels changed) over their entries of the textures' archive at TEXENTRY: an RLEN one is
   packed again, and the archive and file grow as they need (replace_entries) */
bool patch_tims(std::vector<uint8_t> &file, int texEntry, const std::vector<Tim> &tims, std::string &err);

/* The texels of a 256x256 page (as decode_page gives them, WAS) that NOW has otherwise, written into CHANGES (VRAM
   halfword -> value, over VRAM) as the nearest color of the page's CLUT; 4 and 8-bit pages only */
bool texture_edits(const std::vector<uint16_t> &vram, uint16_t tpage, uint16_t clut, const std::vector<uint32_t> &was,
                   const std::vector<uint32_t> &now, std::map<int, uint16_t> &changes, std::string &err);

/* FILE with its entries REPL (index -> bytes) replaced, as a disc's file again (whole sectors) */
std::vector<uint8_t> replace_entries(const std::vector<uint8_t> &file, const std::map<int, std::vector<uint8_t>> &repl);

/* A bone's matrix (column-major) from its position, rotation and scale: Rz *
   Ry * Rx (RotMatrixZYX), its columns scaled, at its position */
void bone_matrix(const Vec3s &pos, const Vec3s &rot, const Vec3s &scale, float out[16]);
