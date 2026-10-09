# DW3 Model Viewer

A viewer for the battle models of **Digimon World 2003** (PAL, SLES-03936), in the
spirit of Half-Life's model viewer: open a model and look at its mesh, its textures and
their UVs, its skeleton, and each of its motions, frame by frame.

It only reads: it never changes the game's files. **No game data is included**; it reads
the models from your own disc image.

This is a fan project, not affiliated with or endorsed by Bandai Namco. Digimon is a
trademark of its owners.

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

- **Windows 10 or 11**, 64-bit, with a graphics card that supports **OpenGL 3.3** (any
  card or integrated graphics from the last ten years). Only Windows for now.
- **Your own copy of the game**: Digimon World 2003 (Europe), SLES-03936, as a disc
  image, either a `.bin`/`.cue` pair or a `.iso`. Other versions (Digimon World 3,
  USA/Japan) aren't supported: the file tables are read from the European executable.
- **Python 3.8 or later** ([python.org](https://www.python.org/downloads/)), to read the
  models from your disc image (and to build from source).

## Download

1. Download `dw3-model-viewer-<version>-win64.zip` from
   [Releases](https://github.com/Feerdus95/dw3-model-viewer/releases) and unpack it.
2. In the unpacked `dw3-model-viewer` folder, read the models from your disc (once):

       python tools/setup_disc.py "path/to/Digimon World 2003 (Europe).cue"

   This takes a few seconds and writes `data/` next to the exe: the 434 battle model
   files (about 17 MB) and the disc's file names. Given a `.cue`, it reads the `.bin`
   with the same name next to it; if yours is named differently, give the `.bin`
   itself. It checks the disc first: if it says "this isn't Digimon World 2003
   (Europe), SLES-03936", the image is another version or is damaged.
3. Run `dw3mv.exe`.

## Building from source

On top of the requirements above:

- **A MinGW-w64 C++ compiler for x86_64.** Either one works:
  - **llvm-mingw**: download `llvm-mingw-<date>-ucrt-x86_64.zip` from
    [its releases](https://github.com/mstorsjo/llvm-mingw/releases), unpack it, and add
    its `bin` folder to `PATH`;
  - **MSYS2**: from [msys2.org](https://www.msys2.org/), run
    `pacman -S mingw-w64-ucrt-x86_64-gcc`, and add `C:\msys64\ucrt64\bin` to `PATH`.
- **SDL3's development files for MinGW**: download `SDL3-devel-<version>-mingw.zip`
  (3.2 or later) from [SDL's releases](https://github.com/libsdl-org/SDL/releases) and
  unpack it into `third_party/`, so that `third_party/SDL3-<version>/` exists.
- **Dear ImGui** is already in `third_party/imgui`.

### 1. Get the source

    git clone https://github.com/Feerdus95/dw3-model-viewer.git
    cd dw3-model-viewer

Or download it as a ZIP from GitHub's **Code** button and unpack it. Every command
below runs from this folder.

### 2. Read the models from your disc

    python tools/setup_disc.py "path/to/Digimon World 2003 (Europe).cue"

As in [Download](#download): it writes `data/` in this folder, once.

### 3. Build

    python build.py

This makes `build/dw3mv.exe` and copies `SDL3.dll` next to it. The compiler is the
first of these found: `$CXX`, `x86_64-w64-mingw32-clang++`, `x86_64-w64-mingw32-g++`,
`clang++`, `g++`. To use SDL3 from elsewhere, set `SDL3_DIR` to its folder.

If it fails:
- **"no C++ compiler"**: the compiler's `bin` folder isn't on `PATH`. Open a new
  terminal after changing `PATH`.
- **"no SDL3"**: `third_party/SDL3-<version>/` is missing, or it isn't the MinGW
  package (it needs `x86_64-w64-mingw32/include/SDL3/SDL.h`).

### 4. Run

    build\dw3mv.exe

Or double-click `dw3mv.exe` in the `build` folder.

### Making a release

    python tools/package.py v0.1.0

It builds, packs `build/dw3-model-viewer-v0.1.0-win64.zip` (the exe, `SDL3.dll`,
`setup_disc.py`, this README and the licenses), then unpacks it to a temporary folder
and checks that it reads every model.

## Troubleshooting

To check a setup, `dw3mv.exe --check | more` reads every model and should end with
`434 models, 0 unread`. (It's a window app: in cmd or PowerShell, pipe its output, as
here, so that the prompt waits for it.)

- **"SDL3.dll was not found"**: `SDL3.dll` must be next to `dw3mv.exe`.
- **"This needs a graphics card with OpenGL 3.3"**: update the graphics driver. On a
  remote desktop or a virtual machine there may be no OpenGL 3.3.
- **The list of models is empty**: `data/` is missing; run `setup_disc.py` again. The
  viewer looks for it next to the exe, or next to its `build` folder.
- **"python" isn't recognized**: install Python from python.org with **Add python.exe
  to PATH** ticked, then open a new terminal.

## Use

The window is the 3D view, with a panel on the right:

- **Files**: the disc's models by file name, and their file number. The filter takes
  part of a name (`M384`, `KOET`) or a number. You can also drop a model's `.BIN` on the
  window.
- **Model**: what the file holds, the display options, and the bone tree. Click a bone
  to highlight it, untick it to hide it.
- **Motions**: the motion list and the playback controls. Motions play at 60 keyframes
  a second at 1x, the rate the game steps them.
- **Textures**: the texture pages, with the UVs over them. **Save BMP** writes the page
  shown next to `dw3mv.exe`, named after the model, the page and the palette.

| Control | Action |
|---|---|
| Left drag | orbit |
| Right or middle drag | pan |
| Wheel | zoom |
| F | frame the model |
| Space | play / pause the motion |

### Command line

    dw3mv [file.bin]                      opens the viewer, and the model
    dw3mv --data DIR                      reads data/ (files and names.txt) from DIR
    dw3mv --check                         reads every model, prints a line for each
    dw3mv --shot out.bmp [--motion N] [--key K] file.bin
                                          draws one frame to a BMP and exits

By default `data/` is looked for next to the exe, or next to its `build` folder.

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

## License

MIT, see [LICENSE](LICENSE). The game and its data are not covered: they belong to their owners.

## Credits

See [CREDITS.md](CREDITS.md):
- the model reading is ported from **dw3_decomp** (MIT);
- **Dear ImGui** 1.91.8 (MIT, `third_party/imgui`);
- **SDL3** (zlib), linked as `SDL3.dll`.
