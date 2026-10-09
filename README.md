# DW3 Model Viewer

A viewer for Digimon World 2003's battle models (FIGHT/MODEL, FIGHT/EFFECT), in the
spirit of Half-Life's model viewer: the model with its textures, its bones, and each of
its motions. Separate from the native port; it reads the same `native/data/files`.

The formats are read the way the game reads them, ported from dw3_decomp
(`src/fightstg/fightstg.c`, `src/main/gfx/tim_loader.c`).

## Build and run

    python viewer/build.py
    python viewer/tools/disc_names.py "path/to/Digimon World 2003 (Europe).cue"
    viewer/build/dw3mv.exe

`disc_names.py` reads the file names from your own disc into `build/names.txt`; the
lists show the models by them (once, after the first build).

Pick a model in **Files**, or drop a `.BIN` on the window. Left drag orbits, right drag
pans, the wheel zooms, F frames the model, space plays and pauses.

    dw3mv --check                   reads every model and prints what it found
    dw3mv --shot out.bmp [--motion N] [--key K] file.bin

## Third party

See CREDITS.md: the model reading is ported from dw3_decomp (MIT); Dear ImGui 1.91.8
(MIT, `third_party/imgui`); SDL3 (zlib, from the port's third_party).
