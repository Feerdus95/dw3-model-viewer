"""Reads the Digimon models of your own Digimon Digital Card Battle disc for the viewer's DCB tab.

    python tools/extract_dcb.py "path/to/Digimon Digital Card Battle.cue" OUT_DIR

writes OUT_DIR/digimon/ID_Name/ID_Name.dcbm (G_ ones are the battle models G.DRV holds); choose OUT_DIR in the DCB tab.
The game's files sit in A..P.DRV archives (32-byte records: key, sector, size, name; key 0x80 is a folder).
Takes a few minutes: it replays the game's own motion easing. Standard library only, 4 worker processes.
"""
import multiprocessing as mp
import os
import struct
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE / "dcb"))
from discfs import Disc  # noqa: E402
from pak import chunks  # noqa: E402
import todcbm  # noqa: E402


def drv_files(data, sector=0, path="", seen=None):
    """{path: bytes} of a .DRV archive."""
    seen = set() if seen is None else seen
    out = {}
    if sector in seen:
        return out
    seen.add(sector)
    o = sector * 2048
    while o + 32 <= len(data):
        key, sec, size = struct.unpack_from("<iii", data, o)
        if key == 0:
            break
        name = data[o + 16:o + 32].split(b"\0")[0].decode("cp932", "replace").replace("/", "_")
        if key == 0x80:
            out.update(drv_files(data, sec, path + name + "/", seen))
        else:
            out[path + name] = data[sec * 2048:sec * 2048 + size]
        o += 32
    return out


def card_names(card2):
    """{model id: card name} from CARD2: a header, then the Digimon cards (0x13C bytes: name at +3, model id at +0xE5)."""
    n = struct.unpack_from("<H", card2, 4)[0]
    return {card2[8 + i * 0x13C + 0xE5]: card2[8 + i * 0x13C + 3:8 + i * 0x13C + 0x19].split(b"\0")[0].decode("latin1") for i in range(n)}


def job(a):
    fname, b, names, out = a
    try:
        mid = next(c for c in chunks(b) if c[0] == 0)[1]  # the model chunk's id
        name = "%d_%s" % (mid, names.get(mid, "model").replace(" ", "_").replace("*", "")) + ("_G" + fname[2:] if fname.startswith("G/") else "")
        todcbm.convert(b, out, name)
        return name, "ok"
    except Exception as e:
        return fname, "skipped " + repr(e)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    disc, out = Disc(sys.argv[1]), sys.argv[2]

    def drv(n):
        f = disc.find(n + ".DRV")
        if not f:
            sys.exit("This is not a Digimon Digital Card Battle disc (no %s.DRV)." % n)
        return drv_files(disc.read(*f))

    names = card_names(drv("B")["CARD2"])
    jobs = [(d + "/" + k, v, names, out) for d in "FG" for k, v in drv(d).items()]
    t0, done, ok = time.time(), 0, 0
    with mp.Pool(min(4, os.cpu_count() or 1)) as pool:
        for name, res in pool.imap_unordered(job, jobs):
            done += 1
            ok += res == "ok"
            el = time.time() - t0
            print("[%d/%d %d%%] %s %s  %ds, ~%ds left" % (done, len(jobs), 100 * done // len(jobs), name, res, el, el / done * (len(jobs) - done)), flush=True)
    print("%d models written -> %s (the rest of F/G are not Digimon models)" % (ok, out))


if __name__ == "__main__":
    main()
