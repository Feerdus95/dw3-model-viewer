/* A battle model as a glTF 2.0 binary for Blender and the like: its bones as a skeleton (one joint each), its faces
   weighted whole to their bone (the game's meshes are rigid), its texture pages as PNGs beside it and every motion as an
   animation. No OpenGL here. */
#pragma once
#include "model.h"

/* DIR/NAME.glb and DIR/texN.png; false and ERR when it can't be written. Units: y up, 1 unit = 100 of the game's, as the
   OBJ; the animations are sampled 30 times a second, from the model's own motion player (the game's blends included). A bone
   the game hides (scaled to 48 or less) keeps its last matrix, as the game does, but its mesh stays drawn */
bool export_gltf(const Model &model, const std::string &dir, const std::string &name, std::string &err);
