# DW3 Model Viewer

A viewer for the battle models of **Digimon World 2003** (PAL, SLES-03936), in the
spirit of Half-Life's model viewer: open a model and look at its mesh, its textures and
their UVs, its skeleton, and each of its motions, frame by frame.

It is a separate tool from the native PC port and never changes the game's files: it
reads the same `native/data/files` the port extracts from your disc. No game data is
included; you need your own copy of the game.

## What it shows

- **Models**: every battle model on the disc. That's 434 files, from FIGHT/MODEL (the
  Digimon) and FIGHT/EFFECT (DNA forms and technique models).
- **Mesh**:
  - textures off, as the PSX draws them (nearest texels), or bilinear-filtered (a
    preview of a smoother look);
  - lighting, wireframe, back-face culling and a floor grid.
- **Skeleton**:
  - the bone tree, with each part's mesh;
  - show or hide any part, and highlight one;
  - hover a bone for its entries in the file and its current position, rotation and
    scale.
- **Motions**:
  - each of a fighter's motions (62 for the Digimon);
  - play, pause, step one keyframe, restart, loop, speed from 0.05x to 4x, and a
    keyframe slider;
  - the blends between motions play as the game plays them.
- **Textures**:
  - every texture page and palette pair the polygons use, decoded;
  - the UVs drawn over it (the selected part's in yellow);
  - save one as a BMP: the starting point for redrawing a texture.

## Requirements

- Windows 10 or 11, 64-bit, and a graphics card with OpenGL 3.3.
- Python 3, for the build and the setup scripts.
- The native port's toolchain (llvm-mingw, in `%LOCALAPPDATA%\retcomm\toolchains`) and
  its SDL3 (`native/third_party/SDL3-3.4.16`).
- Your disc as a `.bin`/`.cue` image, already extracted for the port:

      python native/tools/extract_iso.py "path/to/Digimon World 2003 (Europe).cue"

## Build and setup

    python viewer/build.py
    python viewer/tools/disc_names.py "path/to/Digimon World 2003 (Europe).cue"

- `build.py` makes `viewer/build/dw3mv.exe` and copies `SDL3.dll` next to it.
- `disc_names.py` reads the file names (such as `M384KOET.BIN`) from your disc into
  `viewer/build/names.txt`. You only need to run it once. Without it, the model list is
  empty; a dropped `.BIN` still opens.

## Use

Run `viewer/build/dw3mv.exe`. Pick a model in **Files** (the filter takes part of a name
or a file number), or drop a `.BIN` on the window.

| Control | Action |
|---|---|
| Left drag | orbit |
| Right or middle drag | pan |
| Wheel | zoom |
| F | frame the model |
| Space | play / pause the motion |

### Command line

    dw3mv [file.bin]                      opens the viewer, and the model
    dw3mv --data DIR                      reads the disc's files from DIR
    dw3mv --check                         reads every model, prints a line for each
    dw3mv --shot out.bmp [--motion N] [--key K] file.bin
                                          draws one frame to a BMP and exits

By default the files are looked for in `native/data/files`, from the viewer's
`build` folder.

## How it reads a model

The reading is a port of what the game itself does, from the dw3_decomp decompilation
(`src/fightstg/fightstg.c`, `src/main/gfx/tim_loader.c`), not a guess at the format.

- **The file** is an archive, a table of offsets. Its parts:
  - **bone list**: the texture entry, then each bone's parent, mesh and keys;
  - **meshes**: vertices, normals, and command bytes that set the texture, flags and
    colors, then list the polygons (triangles and quads, flat or gouraud, lit or not,
    semi-transparent);
  - **keys**: a bone's positions, rotations (4096 = 360°) and scales, each at a frame
    of one timeline every motion shares;
  - **motions**: each a list of steps that play ranges of that timeline or blend into
    the next;
  - **textures**: RLEN-compressed TIMs, palettes included.
- **Textures** are placed in an emulated 1024×512 VRAM. The shader looks each texel up
  through its page and palette, as the PSX does.
- **Bones**: each one's matrix is built as the game builds it (Z·Y·X rotation, then
  scale), and a bone scaled under 48/4096 is hidden.

`src/model.cpp` reads and poses a model with no OpenGL in it. `src/main.cpp` is the
window, the renderer and the interface.

## Not there yet

- **Names**: models go by their file names, not by Digimon names (from SMDLDATA.PRO).
- **Faces**: the blinking eyes and other animated face parts.
- **Battle data**: the cameras of each fighter (SMDLDATA.PRO) and the action scripts.
- **Lighting**: an approximation, not the PSX's light matrices.
- **Export and replacement** of models and textures, beyond saving a texture as a BMP.

## Credits

See [CREDITS.md](CREDITS.md):
- the model reading is ported from **dw3_decomp** (MIT);
- **Dear ImGui** 1.91.8 (MIT, `third_party/imgui`);
- **SDL3** (zlib), from the native port.
