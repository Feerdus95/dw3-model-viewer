/* FIGHTSTG's 2D effects, read and drawn as the battle does (dw3_decomp:
   src/fightstg/sprite_effect.c, sprite_anim.c; src/main/gfx/sprite_drawer.c,
   wfightmn/wfightmn.c). No OpenGL here. */
#pragma once
#include <cstdint>
#include <string>
#include <vector>

/* One of an effect's sprite animations (SpriteAnim): its flags pick which
   of its 9 values (frame, CLUT row, x, y, scale x, y, rotation x, y, z)
   each step of its data sets; a step is one field (1/60 s) */
struct EffectAnim {
    int flags = 0, stride = 0, duration = 0;
    int16_t init[9] = {};
    std::vector<int16_t> data;
    void at(int time, int16_t out[9]) const; /* the values at TIME */
};

/* A sprite effect: its animations, its sheet and its images in VRAM */
struct Effect {
    int id = -1, sheetIndex = -1;
    int fid = 0, entry = 0;       /* the animations' archive: file, entry */
    int sheetFid = 0, sheetEntry = 0;
    int texX = 0, texY = 0;       /* the sheet's texture and CLUTs */
    std::vector<EffectAnim> anims;
    std::vector<uint8_t> sheet;
    std::vector<uint16_t> vram;   /* 1024x512 */
    int duration = 0;
    std::string error;

    /* reads effect ID from FILES (data/files) */
    bool load(int id, const std::string &files);
    /* the effect at TIME into RGBA pixels (W x H, already holding the
       background), its origin at OX, OY: the parts blend as the PSX blends */
    void draw(int time, uint32_t *rgba, int w, int h, int ox, int oy) const;
};

/* Every effect's id, in the game's table's order */
std::vector<int> effect_ids();
