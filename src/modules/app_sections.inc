/* ===== the UI: sections, selection, editing, and each section's two panels (the browser on the left, the inspector on the right) =====
   The code is in the files of modules/ui/, included here in the order it was written: each part uses what the ones above it define (the lambdas
   capture main's locals), so the order is the file. */
#include "ui/sections.inc" /* the sections: enum, names, tips */
#include "ui/state_selection.inc" /* the UI's state, the selection, asking before losing work */
#include "ui/open_load.inc" /* opening things: files, stages, DW2, the folder pickers */
#include "ui/model_edit.inc" /* a model's pieces: editing, palette, import and export */
#include "ui/undo.inc" /* undo: the stage's edits and the own images' */
#include "ui/stage_parts.inc" /* the stage's parts, visibility, framing */
#include "ui/menus.inc" /* the right-click menus */
#include "ui/edit_actions.inc" /* frame, prop and part actions; copy, paste, delete, save */
#include "ui/panel_parts.inc" /* the panels' parts shared by sections */
#include "ui/files.inc" /* Files */
#include "ui/stages_browser.inc" /* Stages: the browser */
#include "ui/stage_catalog.inc" /* Stages: the parts catalog window */
#include "ui/stage_props.inc" /* Stages: props, the brush */
#include "ui/props_catalog.inc" /* Stages: the props catalog window */
#include "ui/props_panel.inc" /* Stages: the props panel */
#include "ui/stages_inspector.inc" /* Stages: the inspector */
#include "ui/dw2_size.inc" /* a DW2 model's size */
#include "ui/model.inc" /* Model */
#include "ui/motions.inc" /* Motions */
#include "ui/fighter_state.inc" /* Fighter: script roles and the state of the player, the workbench and the choreography */
#include "ui/script_player.inc" /* Fighter: the script player */
#include "ui/ab_controls.inc" /* A/B under the view */
#include "ui/workbench_ops.inc" /* the workbench's history */
#include "ui/fighter_browser.inc" /* Fighter: the browser (the scripts and the player's options) */
#include "ui/fighter_inspector.inc" /* Fighter: the inspector */
#include "ui/cameras_library.inc" /* Cameras: the Library */
#include "ui/workbench_save.inc" /* saving what the workbench holds, and the undo and save hooks */
#include "ui/cameras_workbench.inc" /* Cameras: the Workbench */
#include "ui/effects.inc" /* Effects */
#include "ui/textures.inc" /* Textures */
#include "ui/dw2.inc" /* DW2 */
#include "ui/layout.inc" /* the sections' browser and inspector */
#include "ui/transport.inc" /* the playback bar */
