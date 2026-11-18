"""Builds the viewer and packs a release: build/dw3-model-viewer-<version>-win64.zip,
then unpacks it to a temporary folder and checks that it starts and reads the
models (with the data setup_disc.py made in data/).

    python tools/package.py v0.1.0
"""
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
BUILD = HERE / "build"
ROOT = "dw3-model-viewer"


def main():
    if len(sys.argv) != 2 or not sys.argv[1].startswith("v"):
        sys.exit(__doc__)
    version = sys.argv[1]
    if subprocess.run([sys.executable, HERE / "build.py"]).returncode:
        sys.exit(1)
    sdl = next(p for p in sorted((HERE / "third_party").glob("SDL3-*"), reverse=True) if (p / "LICENSE.txt").exists())
    files = {
        "dw3mv.exe": BUILD / "dw3mv.exe",
        "SDL3.dll": BUILD / "SDL3.dll",
        "tools/setup_disc.py": HERE / "tools/setup_disc.py",
        "README.md": HERE / "README.md",
        "LICENSE": HERE / "LICENSE",
        "CREDITS.md": HERE / "CREDITS.md",
        "licenses/imgui.txt": HERE / "third_party/imgui/LICENSE.txt",
        "licenses/SDL3.txt": sdl / "LICENSE.txt",
    }
    out = BUILD / f"{ROOT}-{version}-win64.zip"
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for name, src in files.items():
            z.write(src, f"{ROOT}/{name}")
    print("packed", out, f"{out.stat().st_size // 1024} KB")

    # the zip as a user gets it, with the data setup_disc.py would write next to the exe
    with tempfile.TemporaryDirectory() as tmp:
        zipfile.ZipFile(out).extractall(tmp)
        unpacked = Path(tmp) / ROOT
        shutil.copytree(HERE / "data", unpacked / "data")
        r = subprocess.run([unpacked / "dw3mv.exe", "--check"], capture_output=True, text=True)
        last = r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr.strip()
        print("check:", last)
        if r.returncode or not last.endswith(", 0 unread"):
            sys.exit("the packed viewer failed its check")


if __name__ == "__main__":
    main()
