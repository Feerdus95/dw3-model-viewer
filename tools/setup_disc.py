"""Reads the viewer's data from your own disc of Digimon World 2003 (Europe,
SLES-03936): the battle models' files, by file number, and every file's name.

    python tools/setup_disc.py "path/to/Digimon World 2003 (Europe).cue"

writes
    data/files/NNNN.bin   each battle model's file (M###XXXX, MEFT####), and
                          the battle's data on its fighters
    data/names.txt        the disc's file names, line N for file N

The game finds its files by number through a table in its executable (each
file's first sector and its length in sectors). Their names are in directory
records the disc's root doesn't list, so every sector is searched for them.
"""
import hashlib
import re
import struct
import sys
from pathlib import Path

EXE_NAME = "SLES_039.36"
EXE_SHA1 = "d1b7e4d646e3a9c2b88fdb25d20b5f7116bbb06d"
EXE_VRAM = 0x80010000
FILE_SECTORS = 0x80044F6C  # s32[2382]: first sector
FILE_SECTOR_COUNTS = 0x800474A4  # u16[2382]: sectors
FILE_COUNT = 2382
# the battle's data on its fighters: USDIGNAM (the Digimon's names), USSKLNAM
# (the techniques'), SFSTDATA.PRO (the stages' lights), SMDLDATA.PRO (the
# fighters: models, faces, cameras, scripts), SDIGIEDT.PRO (the enemies' table)
BATTLE_DATA = {79, 163, 459, 460, 463}
OUT = Path(__file__).resolve().parents[1] / "data"


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    path = Path(sys.argv[1])
    if path.suffix.lower() == ".cue":
        path = path.with_suffix(".bin")
    if not path.exists():
        sys.exit(f"{path} not found")
    f = open(path, "rb")
    size = path.stat().st_size
    raw = size % 2352 == 0  # raw 2352-byte sectors (.bin), or plain 2048-byte ones (.iso)
    sectors = size // (2352 if raw else 2048)

    def read(lba, n=1):
        """The 2048 data bytes of each sector."""
        out = bytearray()
        for s in range(lba, lba + n):
            f.seek(s * 2352 + 24 if raw else s * 2048)
            out += f.read(2048)
        return bytes(out)

    # every directory record: its extent is stored little and big-endian,
    # and a file's name ends ";1"
    by_lba = {}
    exe = None
    for lba in range(sectors):
        data = read(lba)
        i = 0
        while i + 34 <= len(data):
            n, ln = data[i], data[i + 32]
            if n < 34 or i + n > len(data) or 33 + ln > n or data[i + 2:i + 6] != data[i + 6:i + 10][::-1]:
                break
            name = data[i + 33:i + 33 + ln]
            if name.endswith(b";1"):
                start, length = struct.unpack_from("<I", data, i + 2)[0], struct.unpack_from("<I", data, i + 10)[0]
                by_lba[start] = name[:-2].decode("ascii", "replace")
                if name == EXE_NAME.encode() + b";1":
                    exe = read(start, (length + 2047) // 2048)[:length]
            i += n
    if exe is None or hashlib.sha1(exe).hexdigest() != EXE_SHA1:
        sys.exit("this isn't Digimon World 2003 (Europe), SLES-03936")

    def table(addr, fmt):
        return struct.unpack_from(f"<{FILE_COUNT}{fmt}", exe, addr - EXE_VRAM + 0x800)

    starts, counts = table(FILE_SECTORS, "i"), table(FILE_SECTOR_COUNTS, "H")
    names = [by_lba.get(s, "") for s in starts]
    (OUT / "files").mkdir(parents=True, exist_ok=True)
    (OUT / "names.txt").write_text("".join(n + "\n" for n in names), encoding="ascii")
    models = 0
    for fid, (start, count, name) in enumerate(zip(starts, counts, names)):
        if (re.match(r"M(\d|EFT)", name) or fid in BATTLE_DATA) and 0 < start and count and start + count <= sectors:
            (OUT / "files" / f"{fid:04d}.bin").write_bytes(read(start, count))
            models += 1
    print(f"{sum(1 for n in names if n)} names, {models} files -> {OUT}")


if __name__ == "__main__":
    main()
