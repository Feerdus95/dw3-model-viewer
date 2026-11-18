"""Release test: a user with the zip and the three discs gets every model of the three games, with nothing of ours.

    python tools/test_release.py DW3.cue DW2.cue DCB.cue [build/dw3-model-viewer-vX-win64.zip] [--dir CLEAN_FOLDER]

--dir unpacks there (emptied first) instead of a temporary folder, and leaves it for a look afterwards.

Unpacks the zip (the newest in build/ if none is given) into a temporary folder and, from the zip's own tools only:
  1. checks the zip holds no model data (no data/, no .bin/.dw2m/.dcbm, no saved edits or mods);
  2. runs setup_disc.py (Digimon World 2003, Europe), then the exe's own --extract-dw2 and --extract-dcb (no Python)
     into the folders the DW2 and DCB tabs read by default, data/dw2 and data/dcb, and checks the zip ships no Python extractor;
  3. runs `dw3mv --check`: every model of
     the three games must load with a mesh and a motion, and none may be unread;
  4. renders one model of each game with --shot and checks the picture is not blank.
"""
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
MIN = {"DW3": 400, "DW2": 700, "DCB": 240}  # models each game must give (the discs have 434, 773 and 248)
BAD_NAME = re.compile(r"(^|/)data/|\.(bin|dw2m|dcbm|obj|png|bmp)$|tuning|shape\.txt|hand\.bin|flame|magnam|/mods/", re.I)


def run(*cmd, **kw):
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, **kw)
    if r.returncode:
        sys.exit("FAILED: %s\n%s\n%s" % (" ".join(str(c) for c in cmd), r.stdout[-1500:], r.stderr[-1500:]))
    return r.stdout


def blank(bmp):
    """True if the picture has fewer than 8 distinct colors (a missing model draws only the background)."""
    b = bmp.read_bytes()
    off, w, h, bpp = int.from_bytes(b[10:14], "little"), int.from_bytes(b[18:22], "little"), abs(int.from_bytes(b[22:26], "little", signed=True)), b[28] // 8
    return len({b[off + i:off + i + bpp] for i in range(0, w * h * bpp, bpp * 7)}) < 8


def main():
    args, keep = sys.argv[1:], None
    if "--dir" in args:
        i = args.index("--dir")
        keep = Path(args[i + 1]).resolve()
        del args[i:i + 2]
    if len(args) not in (3, 4):
        sys.exit(__doc__)
    dw3, dw2, dcb = (str(Path(a).resolve()) for a in args[:3])
    zips = sorted((HERE / "build").glob("dw3-model-viewer-*-win64.zip"), key=lambda p: p.stat().st_mtime)
    zp = Path(args[3]) if len(args) == 4 else zips[-1]
    if keep:
        if keep.exists():
            shutil.rmtree(keep)
        keep.mkdir(parents=True)
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = str(keep or tmpdir)
        zf = zipfile.ZipFile(zp)
        leaks = [n for n in zf.namelist() if BAD_NAME.search(n) or re.search(r"(extract_d|discfs|tools/dcb/)", n)]
        if leaks:
            sys.exit("the zip holds model data or edits: %s" % leaks)
        zf.extractall(tmp)
        root = next(Path(tmp).iterdir())
        print("zip:", zp.name, "->", root)
        py = [sys.executable, "-I"]
        run(*py, root / "tools/setup_disc.py", dw3)
        exe = root / "dw3mv.exe"
        print(run(exe, "--extract-dw2", dw2, root / "data/dw2").strip().splitlines()[-1:])
        print(run(exe, "--extract-dcb", dcb, root / "data/dcb").strip().splitlines()[-1:])
        for tag, sub in (("dw2", "dw2"), ("dcb", "dcb")):  # what the tabs write after their first read
            (root / (tag + "_dir.txt")).write_text(str(root / "data" / sub) + "\n")
        r = subprocess.run([str(exe), "--check"], capture_output=True, text=True)
        if r.returncode:
            print("\n".join(l for l in r.stdout.splitlines() if "ERROR" in l or "unreadable" in l)[-2000:])
            sys.exit("--check failed (exit %d)" % r.returncode)
        got = {}
        for line in r.stdout.splitlines():
            m = re.fullmatch(r"(?:(DW2|DCB): )?(\d+) models, (\d+) unread", line.strip())
            if m:
                got[m.group(1) or "DW3"] = (int(m.group(2)), int(m.group(3)))
        for game, least in MIN.items():
            n, bad = got.get(game, (0, 1))
            print("%s: %d models, %d unread" % (game, n, bad))
            if bad or n < least:
                sys.exit("%s: expected at least %d models and none unread" % (game, least))

        shots = [("DW3", root / "data/files" / "0575.bin"),
                 ("DW2", next((root / "data/dw2/digimon").glob("003_*/*.dw2m"))),
                 ("DCB", next((root / "data/dcb/digimon").glob("*_Imperialdramon/*.dcbm")))]
        for game, f in shots:
            bmp = root / (game + ".bmp")
            run(exe, "--shot", bmp, f)
            if not bmp.exists() or blank(bmp):
                sys.exit("%s: the picture of %s is blank" % (game, f.name))
            print("%s: %s draws" % (game, f.name))
    print("release OK")


if __name__ == "__main__":
    main()
