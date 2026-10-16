/* A fight stage as the viewer edits it: the stage's model (FIGHTSTG's, from
   FILE_FIGHT_STAGES; dw3_decomp src/fightstg/stage.c, include/fightstg/stage.h)
   as parts (its bones) in the world and their textures as RGBA images. Read
   from the game's model, an OBJ (with its MTL and PNGs) or other stages'
   parts; written back in the game's own format, which the native port reads
   from data/mods/<fid>.bin before the disc's (and the stages' table from
   data/mods/0459.bin). No OpenGL here.

   The game's limits, which build() keeps: a mesh indexes 256 vertices (a part
   with more becomes several bones); the textures are 4-bit, 16 colors each,
   in the stage's three 64x256-halfword pages at VRAM x 0x280-0x33F (the
   fighters' start at 0x340), their CLUTs among them; a part's
   coordinates fit 16 bits; 8 bones skip the bounds check. */
#pragma once
#include "model.h"
#include <array>

/* FILE_FIGHT_STAGES' entry (FightStageInfo, 0x58 bytes) */
struct StageInfo {
    int id = -1, fid = 0, boneEntry = 0;
    uint8_t bg[3] = {};       /* what shows through the panorama's gaps */
    uint8_t noBounds[8] = {}; /* the bones the bounds check skips, up to a 0 */
};
std::vector<StageInfo> read_stage_infos(const std::vector<uint8_t> &table);
void write_stage_info(std::vector<uint8_t> &table, const StageInfo &info); /* its bg and noBounds */
const char *stage_look(int id); /* a few words on what it shows */

struct StageImage {
    int w = 0, h = 0;
    std::vector<uint32_t> px; /* RGBA, R in the low byte; alpha 128 the semi-transparent texels */
    /* a game's 4-bit page as it was: its texels' indices and CLUT; the pages
       of one source (a stage's file and page) share their texels, as the
       game's do, each with its own CLUT. -1: an image of its own */
    int source = -1;
    std::vector<uint8_t> index;
    uint16_t clut[16] = {};
    int clutX = -1, clutY = -1; /* where its CLUT was in VRAM */
    /* a battle model's page as its polygons read it (tpage & 0x19F, CLUT); -1: not one of its pages */
    int tpage = -1, pageClut = 0;
    bool flat = false; /* the polygons with no texture */
    int from = -1;     /* read from a PNG named texN of an export: N (its pixels may be edited) */
};

/* A polygon: its corners counter-clockwise seen from the front (OBJ's), its
   UVs in its image's pixels */
struct StageFace {
    int n = 3, v[4] = {};
    float uv[4][2] = {};
    int image = 0, abr = 0; /* abr: the game's semi-transparency (0 none, else the mode + 1) */
};

struct StagePart {
    std::string name;
    int parent = 0;            /* another part's index + 1, 0 the root */
    std::vector<uint8_t> keys; /* its keys' archive, as in the file (animated parts keep theirs) */
    float bind[16];            /* where its keys' first frame puts it */
    bool noBounds = false;     /* drawn even when its box seems off screen (the ground) */
    uint8_t color[3] = {128, 128, 128};
    std::vector<std::array<float, 3>> verts; /* in the world at bind, the PSX's axes (y down) */
    std::vector<StageFace> faces;
    float move[3] = {0, 0, 0}, turn = 0, scale = 1; /* the viewer's placement: about its middle, turn in degrees */
};

/* What build() made */
struct StageBuild {
    int islands = 0;              /* texture pieces, each with its CLUT */
    float shrink = 1;             /* how far the textures were scaled down to fit */
    std::vector<int> firstBone;   /* each part's first bone */
    std::string warning;
};

struct StageEdit {
    std::vector<StagePart> parts;
    std::vector<StageImage> images;
    uint8_t bg[3] = {};
    std::string error;

    void fromModel(const Model &m, const StageInfo &info);
    /* adds another stage's part, fixed where it stands (its animation not kept) */
    void append(const StageEdit &from, int part, const std::string &prefix);
    void remove(int part); /* its children stay where they stand */
    std::vector<std::array<float, 3>> placed(const StagePart &p) const; /* its vertices, placed */
    /* DIR/NAME.obj (y up, 1 unit = 100 of the game's), NAME.mtl and texN.png */
    bool exportObj(const std::string &dir, const std::string &name) const;
    /* an OBJ: its objects become the parts (by name the ones it had, new ones fixed) */
    bool importObj(const std::string &path);
    /* the stage's model file: DISC's entries, the bone list (BONE_ENTRY) and
       textures made anew, the meshes and keys added; INFO's noBounds set */
    bool build(const std::vector<uint8_t> &disc, int boneEntry, std::vector<uint8_t> &out, StageInfo &info, StageBuild &made);
    /* A battle model's bone (the parts of fromModel), for the OBJ round trip: SIGNATURE is its faces in its own
       space, to tell a bone that was edited; BONEMESH its mesh archive as the file has it (vertices, a normal each,
       commands), the pages and CLUTs the faces' images say; false and ERROR when it can't be */
    std::string signature(int part) const;
    bool boneMesh(int part, std::vector<uint8_t> &mesh);
};
