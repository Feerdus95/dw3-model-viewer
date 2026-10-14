/* What the battle knows about its fighters, besides their models: their
   names, faces, cameras and action scripts, and the stages' lights
   (dw3_decomp: include/fightstg/model.h, stage.h, battle.h;
   src/fightstg/fighter_info.c, face.c, battle_script.c). No OpenGL here. */
#pragma once
#include <cstdint>
#include <string>
#include <vector>

/* A part of a fighter's face and where its three frames are in its texture,
   in VRAM halfwords from the model's texture position (FaceRect) */
struct FaceRect {
    uint8_t x, y, w, h;
    uint8_t frames[3][2];
};

/* A camera that looks at a fighter (FighterInfo.camPos/camRef/camProj): its
   y and z as the stored ones, so up and towards the viewer, and its
   projection distance on a 320x240 screen */
struct GameCamera {
    int16_t pos[3], ref[3], proj;
};

/* An entry of the fighters' file (SMDLDATA.PRO) */
struct Fighter {
    int id;    /* the fighter, as the battle names it */
    int kind;  /* a partner's Digimon (1 on), or 0x3A on for an enemy */
    bool enemy;
    int modelFid, modelEntry, motionsFid, motionsEntry, effectsFid, effectsEntry;
    std::string name;
    int distance, height; /* where it stands in battle (FighterInfo) */
    bool faceUsed[16];
    FaceRect face[16];
    std::vector<GameCamera> cameras; /* 12 for a partner, an enemy's own count */
};

/* A fight stage's lights (LightSet): three flat lights and the ambient color */
struct StageLights {
    int16_t bgColor[3];
    float dir[3][3];   /* where each light comes from, normalized: -its direction */
    float color[3][3]; /* 0-255 */
    float ambient[3];  /* 0-255 */
};

/* A line of an action script: the command as text, and what the viewer can
   show of it */
struct ScriptLine {
    std::string text;
    int motion = 0;  /* a motion it plays on its own side's model, 1-based */
    int camera = -1; /* a fighter camera: 0-11 the partner's, 100-102 the enemy's */
    int effect = -1; /* a sprite effect it starts */
};

struct GameData {
    std::vector<Fighter> fighters;
    std::vector<StageLights> stages;
    std::vector<std::string> digimonNames, skillNames;

    /* from the data folder's files: SMDLDATA.PRO (460), SFSTDATA.PRO (459),
       the battle table (463) and the English names (79, 163) */
    void load(const std::string &files);
    std::vector<const Fighter *> forFile(int fid) const;
    std::string fileLabel(int fid, const std::string &fileName) const; /* who uses a file */
};

/* A model's action scripts (FighterInfo.effects): its archive's entries,
   each command decoded as FIGHTSTG_updateBattleScript reads it */
std::vector<std::vector<ScriptLine>> read_scripts(const uint8_t *archive, size_t size);

/* The face's step (FIGHTSTG_updateFace) by one battle frame: the eyes blink
   or follow the motion, the other parts cycle; the parts that change are
   copied in VRAM. Returns whether VRAM changed. */
struct FaceState {
    int frame[16] = {};
    int blinkTimer = 0, time = 0;
};
bool step_face(const Fighter &f, FaceState &s, int motion, int texX, int texY, std::vector<uint16_t> &vram);
