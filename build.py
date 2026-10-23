"""Builds the model viewer: build/dw3mv.exe (Windows, 64-bit).

    python build.py

Needs a MinGW-w64 C++ compiler for x86_64 (llvm-mingw or MSYS2's), found as
$CXX or on PATH, and SDL3's MinGW development files: $SDL3_DIR, or
third_party/SDL3-<version> as unpacked from SDL3-devel-<version>-mingw.
Dear ImGui is in third_party/imgui.
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE / "build"
OBJ = OUT / "obj"
IMGUI = HERE / "third_party/imgui"
SOURCES = [HERE / "src/main.cpp", HERE / "src/model.cpp", HERE / "src/dw2.cpp", HERE / "src/game.cpp", HERE / "src/effect.cpp", HERE / "src/custom.cpp", HERE / "src/stage.cpp", HERE / "src/ui_kit.cpp", HERE / "src/motions.cpp", HERE / "src/textures.cpp"] + [
    IMGUI / f for f in ("imgui.cpp", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp",
                        "backends/imgui_impl_sdl3.cpp", "backends/imgui_impl_opengl3.cpp")]


def find_cxx():
    for c in (os.environ.get("CXX"), "x86_64-w64-mingw32-clang++", "x86_64-w64-mingw32-g++", "clang++", "g++"):
        if c and shutil.which(c):
            return shutil.which(c)
    sys.exit("no C++ compiler: install llvm-mingw (or MSYS2's mingw-w64 g++) and put it on PATH, or set CXX")


def find_sdl():
    """The x86_64-w64-mingw32 folder of SDL3's MinGW development package."""
    roots = [Path(os.environ["SDL3_DIR"])] if os.environ.get("SDL3_DIR") else []
    roots += sorted((HERE / "third_party").glob("SDL3-*"), reverse=True)
    for root in roots:
        for d in (root / "x86_64-w64-mingw32", root):
            if (d / "include/SDL3/SDL.h").exists() and (d / "bin/SDL3.dll").exists():
                return d
    sys.exit("no SDL3: unpack SDL3-devel-<version>-mingw into third_party, or set SDL3_DIR")


def icon(cxx):
    """res/dw3mv.ico as the exe's icon, through windres (next to the compiler);
    SDL gives the window the exe's first icon"""
    rc = HERE / "res/dw3mv.rc"
    obj = OBJ / "dw3mv_res.o"
    bin_dir = Path(cxx).parent
    windres = next((str(w) for w in (bin_dir / "x86_64-w64-mingw32-windres.exe", bin_dir / "windres.exe") if w.exists()),
                   shutil.which("windres"))
    if not (HERE / "res/dw3mv.ico").exists() or not windres:
        return []
    if not obj.exists() or obj.stat().st_mtime < max(rc.stat().st_mtime, (HERE / "res/dw3mv.ico").stat().st_mtime):
        print("compile", rc.name)
        if subprocess.run([windres, "-O", "coff", "-i", rc, "-o", obj]).returncode:
            sys.exit(1)
    return [obj]


def main():
    cxx, sdl = find_cxx(), find_sdl()
    cflags = ["-std=c++17", "-O2", "-Wall", f"-I{IMGUI}", f"-I{sdl / 'include'}"]
    OBJ.mkdir(parents=True, exist_ok=True)
    objs = []
    for src in SOURCES:
        obj = OBJ / (src.stem + ".o")
        objs.append(obj)
        deps = [src, *HERE.glob("src/*.h")]
        if src.name == "main.cpp":  # only main.cpp includes the modules: touching one must not rebuild the other sources
            deps += [*HERE.glob("src/modules/*.inc"), *HERE.glob("src/modules/ui/*.inc")]
        if obj.exists() and obj.stat().st_mtime > max(d.stat().st_mtime for d in deps):
            continue
        print("compile", src.name)
        if subprocess.run([cxx, *cflags, "-c", src, "-o", obj]).returncode:
            sys.exit(1)
    objs += icon(cxx)
    exe = OUT / "dw3mv.exe"
    # SDL3 as a DLL (through its import library), the C++ runtime linked in
    if subprocess.run([cxx, *objs, "-o", exe, f"-L{sdl / 'lib'}", "-l:libSDL3.dll.a", "-mwindows", "-static"]).returncode:
        sys.exit(1)
    dll = OUT / "SDL3.dll"
    if not dll.exists() or dll.read_bytes() != (sdl / "bin/SDL3.dll").read_bytes():
        shutil.copy2(sdl / "bin/SDL3.dll", dll)
    print("ok", exe)


if __name__ == "__main__":
    main()
