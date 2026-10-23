"""The refactor's safety net: the same checks before and after every behaviour-preserving step.

    python tools/refactor/check.py record [--exe SLES_039.36]   writes tools/refactor/baseline/
    python tools/refactor/check.py verify [--exe SLES_039.36]   runs the checks again and compares with the baseline

What is compared:
  build        build.py's result, and the warnings a full compile of main.cpp gives (messages, not their places)
  preprocessed main.cpp preprocessed with build.py's own flags (-E -P) and the macros it defines (-dM): both as streams of tokens,
               so only a difference in what the compiler is given shows, not in how the lines are cut
  tests        the exact output of --test-presets, --test-templates and --test-cameras (the last one from R2 on)
  dump         sha256 of compendium.json and audit.md from --dump-compendium (given --exe)
  cli          the exact output of --apply-template and --check-framing for WarGreymon script 11 -> MetalGarurumon script 11
Exit status 0 when everything matches. The window is never opened: every one of these flags returns before SDL_Init.
"""
import hashlib
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT))
import build  # noqa: E402  (build.py only builds under __main__)

BASE = HERE / "baseline"
EXE = ROOT / "build" / "dw3mv.exe"
DEFAULT_SLES = ROOT.parent / "native" / "data" / "SLES_039.36"
PAIR = ["WarGreymon", "11", "MetalGarurumon", "11"]


def run(cmd, timeout=900):
    p = subprocess.run([str(c) for c in cmd], capture_output=True, timeout=timeout, cwd=ROOT)
    return p.returncode, (p.stdout + p.stderr).decode("utf-8", "replace").replace("\r\n", "\n")


def run_out(cmd, timeout=900):
    """stdout alone: the preprocessed text must not carry the compiler's warnings"""
    p = subprocess.run([str(c) for c in cmd], capture_output=True, timeout=timeout, cwd=ROOT)
    return p.returncode, p.stdout.decode("utf-8", "replace").replace("\r\n", "\n")


def sha(data):
    return hashlib.sha256(data if isinstance(data, bytes) else data.encode()).hexdigest()


def tokens(text):
    return " ".join(text.split())


def flags():
    cxx, sdl = build.find_cxx(), build.find_sdl()
    return cxx, ["-std=c++17", "-O2", "-Wall", f"-I{build.IMGUI}", f"-I{sdl / 'include'}"]


def collect(sles):
    out = {}
    cxx, cf = flags()
    out["compiler.txt"] = run([cxx, "--version"])[1].splitlines()[0] + "\n"

    rc, text = run([sys.executable, "build.py"])
    made = any(l.startswith("ok ") for l in text.splitlines())  # (the last line may be a compiler's "1 warning generated.")
    out["build.txt"] = f"build.py exit {rc}, ends with its ok line: {'yes' if made else 'no'}\n"
    with tempfile.TemporaryDirectory() as tmp:
        rc, text = run([cxx, *cf, "-c", ROOT / "src/main.cpp", "-o", Path(tmp) / "m.o"])
    msgs = sorted(re.sub(r"^.*?:\d+:\d+: ", "", l) for l in text.splitlines() if re.search(r":\d+:\d+: (warning|error):", l))
    out["build.txt"] += f"main.cpp full compile exit {rc}, {len(msgs)} diagnostics\n" + "".join(m + "\n" for m in msgs)

    rc, pp = run_out([cxx, *cf, "-E", "-P", ROOT / "src/main.cpp"])
    rc2, macros = run_out([cxx, *cf, "-dM", "-E", ROOT / "src/main.cpp"])
    tk = tokens(pp)
    out["preprocessed.txt"] = f"exit {rc}/{rc2}\ntokens {len(tk.split())}\nsha256 {sha(tk)}\nmacros {len(macros.splitlines())}\nmacros sha256 {sha(chr(10).join(sorted(macros.splitlines())))}\n"
    out["_preprocessed_full.txt"] = tk  # kept aside by the caller, not committed

    for name, flag in (("test-presets.txt", "--test-presets"), ("test-templates.txt", "--test-templates"), ("test-cameras.txt", "--test-cameras")):
        rc, text = run([EXE, flag])
        out[name] = f"exit {rc}\n{text}"
    rc, text = run([EXE, "--apply-template", *PAIR])
    out["apply-template.txt"] = f"exit {rc}\n{text}"
    rc, text = run([EXE, "--check-framing", *PAIR])
    out["check-framing.txt"] = f"exit {rc}\n{text}"

    if sles and Path(sles).exists():
        with tempfile.TemporaryDirectory() as tmp:
            rc, text = run([EXE, "--dump-compendium", "--out", tmp, "--exe", sles])
            hs = {f: sha((Path(tmp) / f).read_bytes()) for f in ("compendium.json", "audit.md") if (Path(tmp) / f).exists()}
        summary = text.strip().splitlines()[-1].split("->")[0] if text.strip() else ""
        out["dump.txt"] = f"exit {rc}\n{summary}\n" + json.dumps(hs, indent=1) + "\n"
    else:
        out["dump.txt"] = "no --exe: the dump was not checked\n"
    return out


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if len(sys.argv) < 2 or sys.argv[1] not in ("record", "verify"):
        sys.exit(__doc__)
    sles = str(DEFAULT_SLES)
    if "--exe" in sys.argv:
        sles = sys.argv[sys.argv.index("--exe") + 1]
    got = collect(sles)
    full = got.pop("_preprocessed_full.txt")
    keep = Path(tempfile.gettempdir()) / f"dw3mv_pp_{sys.argv[1]}.txt"
    keep.write_text(full, encoding="utf-8")
    if sys.argv[1] == "record":
        BASE.mkdir(parents=True, exist_ok=True)
        for name, text in got.items():
            (BASE / name).write_text(text, encoding="utf-8", newline="\n")
        print(f"recorded {len(got)} files in {BASE}; the full preprocessed stream is in {keep}")
        return
    bad = 0
    for name, text in got.items():
        want = (BASE / name).read_text(encoding="utf-8") if (BASE / name).exists() else None
        same = want == text
        print(("same  " if same else "DIFF  ") + name)
        if not same:
            bad += 1
            (Path(tempfile.gettempdir()) / f"dw3mv_check_fail_{name}").write_text(text, encoding="utf-8")  # what was captured, for looking into it
            import difflib
            for l in list(difflib.unified_diff((want or "").splitlines(), text.splitlines(), "baseline", "now", lineterm=""))[:40]:
                print("   " + l)
    print("everything matches" if not bad else f"{bad} differ")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
