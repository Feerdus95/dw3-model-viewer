#pragma once
/* Script mods: a fighter's battle script as a list of commands that can be edited, and the native port's file for it
   (data/mods/scripts/<fighter>/<script>.json, its platform/scriptmod.c; the format is in its docs/script-and-sound-mods.md).
   A command is the 16-bit words the game stores, as the viewer's decoder reads them, so editing a script never changes what a
   command means; the file names each command (an "op" and its fields), and what has no name is written as a "raw" command. */
#include <string>
#include <vector>
#include "custom.h"
#include "game.h"

struct ScriptCmd {
    std::vector<int> w;
    std::string sound; /* a library sound's name, for a sound command that plays one (w[1] is then 0, or 0x4000 when it is hit-aware) */
};
typedef std::vector<ScriptCmd> ScriptWords;

/* How many words the command at W (N words are there) has, as the game reads it; 0 for one it doesn't know */
int script_cmd_len(const int *w, int n);
/* The game's script at P (up to END) split into its commands, as far as the game would run it */
ScriptWords script_split(const uint8_t *p, const uint8_t *end);
/* What the viewer shows of each command: the lines the Fighter section lists and the script player runs */
std::vector<ScriptLine> script_lines(const ScriptWords &s);
/* The file for S, and S from a file's text (false with ERR: the port would reject it). A sound's name is not looked up here */
std::string script_to_json(const ScriptWords &s);
bool script_from_json(const std::string &text, ScriptWords &out, std::string &err);
/* A new command of kind KIND (script_kinds' index), with values that run */
const std::vector<std::string> &script_kinds();
ScriptCmd script_new(int kind);
/* The word names of command C, from word 1, with the ones a player can change (not the ones that say how long the command is) */
struct ScriptField {
    std::string name;
    int index, lo, hi;
};
std::vector<ScriptField> script_fields(const ScriptCmd &c);
