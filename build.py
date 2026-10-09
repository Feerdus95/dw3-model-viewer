"""Builds the model viewer: viewer/build/dw3mv.exe (64-bit), with the native
port's toolchain (llvm-mingw) and SDL3, and Dear ImGui from third_party.

    python viewer/build.py
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE / "build"
OBJ = OUT / "obj"
TC = Path(os.environ["LOCALAPPDATA"]) / "retcomm/toolchains/cmake-clang-v1/1.0.14/bin"
CXX = TC / "x86_64-w64-mingw32-clang++.exe"
SDL = HERE.parent / "native/third_party/SDL3-3.4.16/x86_64-w64-mingw32"
IMGUI = HERE / "third_party/imgui"

SOURCES = [HERE / "src/main.cpp", HERE / "src/model.cpp"] + [
    IMGUI / f for f in ("imgui.cpp", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp",
                        "backends/imgui_impl_sdl3.cpp", "backends/imgui_impl_opengl3.cpp")]
CFLAGS = ["-std=c++17", "-O2", "-Wall", f"-I{IMGUI}", f"-I{SDL / 'include'}"]


def main():
    OBJ.mkdir(parents=True, exist_ok=True)
    objs = []
    for src in SOURCES:
        obj = OBJ / (src.stem + ".o")
        objs.append(obj)
        deps = [src, *HERE.glob("src/*.h")]
        if obj.exists() and obj.stat().st_mtime > max(d.stat().st_mtime for d in deps):
            continue
        print("compile", src.name)
        if subprocess.run([CXX, *CFLAGS, "-c", src, "-o", obj]).returncode:
            sys.exit(1)
    exe = OUT / "dw3mv.exe"
    if subprocess.run([CXX, *objs, "-o", exe, f"-L{SDL / 'lib'}", "-l:libSDL3.dll.a", "-mwindows", "-static"]).returncode:
        sys.exit(1)
    for f in (SDL / "bin/SDL3.dll",):
        dst = OUT / f.name
        if not dst.exists() or dst.read_bytes() != f.read_bytes():
            shutil.copy2(f, dst)
    print("ok", exe)


if __name__ == "__main__":
    main()
