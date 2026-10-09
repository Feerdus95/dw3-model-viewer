"""The disc's file names by fid, for the viewer's lists: build/names.txt, one
name a line (line N is fid N). Made from your own disc: each file of the
disc by its first sector (from its directory records), and the exe's table of each fid's first
sector (FILE_SECTORS).

    python viewer/tools/disc_names.py "path/to/Digimon World 2003 (Europe).cue"
"""
import struct
import sys
from pathlib import Path

EXE_VRAM = 0x80010000
FILE_SECTORS = 0x80044F6C  # s32[2382], SLES_039.36
FILE_COUNT = 2382
OUT = Path(__file__).resolve().parents[1] / "build/names.txt"


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    path = Path(sys.argv[1])
    if path.suffix.lower() == ".cue":
        path = path.with_suffix(".bin")
    f = open(path, "rb")
    raw = path.stat().st_size % 2352 == 0  # raw 2352-byte sectors, or a plain .iso

    def read(lba, n=1):
        out = bytearray()
        for s in range(lba, lba + n):
            f.seek(s * 2352 + 24 if raw else s * 2048)
            out += f.read(2048)
        return bytes(out)

    # The game's files are in directories the root doesn't list: every
    # sector is searched for directory records instead (a record's extent is
    # stored both little and big-endian, a file's name ends ";1")
    by_lba = {}
    for lba in range(path.stat().st_size // (2352 if raw else 2048)):
        data = read(lba)
        i = 0
        while i + 34 <= len(data):
            n, ln = data[i], data[i + 32]
            if n < 34 or i + n > len(data) or 33 + ln > n:
                break
            name = data[i + 33:i + 33 + ln]
            if data[i + 2:i + 6] != data[i + 6:i + 10][::-1]:
                break
            if name.endswith(b";1"):
                by_lba[struct.unpack_from("<I", data, i + 2)[0]] = name[:-2].decode("ascii", "replace")
            i += n
    exe_lba = next(l for l, n in by_lba.items() if n == "SLES_039.36")
    exe = read(exe_lba, (FILE_SECTORS - EXE_VRAM + 0x800 + FILE_COUNT * 4) // 2048 + 1)
    starts = struct.unpack_from(f"<{FILE_COUNT}i", exe, FILE_SECTORS - EXE_VRAM + 0x800)
    names = [by_lba.get(s, "") for s in starts]
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("".join(n + "\n" for n in names), encoding="ascii")
    print(f"{sum(1 for n in names if n)} of {FILE_COUNT} named -> {OUT}")


if __name__ == "__main__":
    main()
