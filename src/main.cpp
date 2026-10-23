/* DW3 Model Viewer: Digimon World 2003's battle models, their textures,
   bones and motions, in the spirit of Half-Life's model viewer.

   dw3mv [file.bin]            opens the viewer (and a model)
   dw3mv --shot out.bmp [--motion N] [--key K] file.bin
                               draws one frame of the model to a BMP and exits
   dw3mv --check               reads every model of the disc and prints what it found
   dw3mv --seams               reports how each bone's mesh meets its parent's, at rest and over every motion (a dev diagnostic)
   dw3mv --effect ID           saves 2D effect ID's frames as PNGs and exits
   dw3mv --effect-users        prints which Digimon's action scripts start each 2D effect
   dw3mv --dump-compendium [--out DIR] [--exe SLES_039.36]
                               writes compendium.json and audit.md (models, fighters, cameras, scripts) and exits

   dw3mv --data DIR            reads the data from DIR instead

   The data is what tools/setup_disc.py reads from your disc: the models'
   files (files/<fid>.bin) and the disc's names (names.txt). */
#define GL_GLEXT_PROTOTYPES
#define IMGUI_DEFINE_MATH_OPERATORS
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_main.h>
#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl3.h"
#include "custom.h"
#include "effect.h"
#include "model.h"
#include "stage.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#include <shellapi.h>
#endif


/* Implementation is split by responsibility; included in one translation unit
   to preserve the original internal linkage and initialization behavior. */
#include "modules/platform_gl.inc"
#include "modules/shaders.inc"
#include "modules/math_camera.inc"
#include "modules/disc_data.inc"
#include "modules/model_scene.inc"
#include "modules/about_effects.inc"
#include "modules/effect_models.inc"
#include "modules/viewer_render.inc"
#include "modules/ui_kit.inc"
#include "modules/stage_validation.inc"
#include "modules/seam_report.inc"
#include "modules/model_camera_tree.inc"
#include "modules/compendium.inc"
#include "modules/presets.inc"
#include "modules/app_setup.inc"
#include "modules/app_sections.inc"
#include "modules/app_loop.inc"
