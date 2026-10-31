"""Reads the Digimon and effect models of your own Digimon World 2 disc (USA, SLUS-01193) for the viewer's DW2 tab.

    python tools/extract_dw2.py "path/to/Digimon World 2 (USA).cue" OUT_DIR

writes OUT_DIR/digimon/NNN_Name/NNN_Name.dw2m and OUT_DIR/fx/CODE/CODE.dw2m; choose OUT_DIR in the DW2 tab.
Formats: the game's model/anim/text code (dw2_decomp src/main/{model,anim,texslot,text}.c). Standard library only.
"""
import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from discfs import Disc  # noqa: E402


def glyphs(b):
    """The game's glyph codes: 0-9 digits, 10.. A-Z, 0x24.. a-z, 0x44 '?', 0x49 '-'."""
    return "".join(str(c) if c < 10 else chr(55 + c) if c < 0x24 else chr(61 + c) if c < 0x3E else "?" if c == 0x44 else "-" if c == 0x49 else "_" for c in b)


def tex(d):
    t = struct.unpack("<I", d[:4])[0]
    flags = struct.unpack("<I", d[t + 4:t + 8])[0]
    assert flags & 8 and (flags & 7) == 0, "only 4bpp+clut handled"
    p = t + 8 + struct.unpack("<I", d[t + 8:t + 12])[0]
    _, _, w, h = struct.unpack("<4H", d[p + 4:p + 12])
    return w * 4, h, d[p + 12:p + 12 + w * h * 2]  # width in 4bpp pixels; the last 4 rows hold the palettes


def parse_model(d):
    n = struct.unpack("<I", d[8:12])[0]
    tab = struct.unpack("<%dI" % (4 * n), d[12:12 + 16 * n])
    vo, po, dp = tab[:n], tab[2 * n:3 * n], tab[3 * n:]
    parents, last, bones = [], {}, []
    for j in range(n):
        parents.append(last.get(dp[j] - 1, -1) if dp[j] else -1)
        last[dp[j]] = j
    for j in range(n):
        nv = struct.unpack("<H", d[vo[j]:vo[j] + 2])[0]
        verts = [struct.unpack("<3h", d[vo[j] + 6 + 6 * i:vo[j] + 12 + 6 * i]) for i in range(nv)]
        q, faces = po[j], []
        for sec, (sz, nvx) in enumerate([(0x14, 4), (0x14, 4), (0x10, 3), (0x10, 3)]):
            cnt = struct.unpack("<I", d[q:q + 4])[0]
            for i in range(cnt):
                e = d[q + 4 + i * sz:q + 4 + (i + 1) * sz]
                uv = list(e[2 * nvx:4 * nvx])
                faces.append((nvx, 1 if sec & 1 == 0 else 0, list(e[:nvx]) + [0] * (4 - nvx), uv + [0] * (8 - len(uv)), struct.unpack("<H", e[sz - 4:sz - 2])[0]))
            q += 4 + cnt * sz
        bones.append((parents[j], verts, faces))
    return bones


def parse_anim(d, nb):
    tbl, i = [], 0
    while True:
        a = struct.unpack("<I", d[4 * (1 + nb + i):4 * (2 + nb + i)])[0]
        if a == 1:
            break
        tbl.append(a)
        i += 1
    offs = struct.unpack("<%dI" % nb, d[4:4 + 4 * nb])
    keys = []
    for b in range(nb):
        end = offs[b + 1] if b + 1 < nb else len(d)
        keys.append([struct.unpack("<12h", d[offs[b] + 24 * j:offs[b] + 24 * j + 24]) for j in range((end - offs[b]) // 24)])
    anims = []
    for i, a in enumerate(tbl):
        if not a:
            continue
        frames, p = [], a
        while not d[p] & 0x80:
            frames.append(d[p:p + nb])
            p += nb
        anims.append((i, frames, 0 if d[p] == 0xFF else 1, (struct.unpack("<H", d[p + 1:p + 3])[0] // nb if d[p] == 0xFE else 0)))
    return keys, anims


def euler(m):
    """A key's 3x3 matrix (4.12) -> rotation (4096 = 360 deg, Rz*Ry*Rx) and scale (4096 = 1)."""
    M = [[m[3 * r + c] / 4096.0 for c in range(3)] for r in range(3)]
    s = [math.sqrt(sum(M[r][c] ** 2 for r in range(3))) for c in range(3)]
    if 0 in s:
        return (0, 0, 0), (0, 0, 0)
    R = [[M[r][c] / s[c] for c in range(3)] for r in range(3)]
    det = (R[0][0] * (R[1][1] * R[2][2] - R[1][2] * R[2][1]) - R[0][1] * (R[1][0] * R[2][2] - R[1][2] * R[2][0]) + R[0][2] * (R[1][0] * R[2][1] - R[1][1] * R[2][0]))
    if det < 0:
        s[0] = -s[0]
        for r in range(3):
            R[r][0] = -R[r][0]
    ry = math.asin(max(-1, min(1, -R[2][0])))
    if abs(R[2][0]) < 0.99999:
        rx, rz = math.atan2(R[2][1], R[2][2]), math.atan2(R[1][0], R[0][0])
    else:
        rx, rz = math.atan2(-R[1][2], R[1][1]), 0
    k = 4096 / (2 * math.pi)
    return tuple(int(round(a * k)) for a in (rx, ry, rz)), tuple(int(round(max(-7.99, min(7.99, x)) * 4096)) for x in s)


def dw2m(model, motions):
    """model: the model file's bytes; motions: [(group, anim file's bytes)] -> the .dw2m bytes."""
    bones = parse_model(model)
    nb = len(bones)
    w, h, px = tex(model)
    mot = []
    for g, ad in motions:
        keys, anims = parse_anim(ad, nb)
        mot += [(g, i, keys, fr, end, loop) for i, fr, end, loop in anims]
    mot.sort(key=lambda m: (m[0], m[1]))
    o = bytearray(b"DW2M" + struct.pack("<HHHH", 1, nb, w, h) + px + struct.pack("<H", len(mot)))
    for g, i, _, fr, end, loop in mot:
        o += struct.pack("<HHHBBH", g, i, len(fr), end, 0, loop)
    for parent, verts, faces in bones:
        o += struct.pack("<hHH", parent, len(verts), len(faces))
        for v in verts:
            o += struct.pack("<3h", *v)
        for n, blend, v, uv, clut in faces:
            o += struct.pack("<BB4B8BH", n, blend, *v, *uv, clut)
    cache = {}
    for g, i, keys, fr, _, _ in mot:
        for f in fr:
            for bi in range(nb):
                t = keys[bi]
                k = f[bi] if f[bi] < len(t) else len(t) - 1
                if (id(keys), bi, k) not in cache:
                    if not t:
                        cache[(id(keys), bi, k)] = ((0, 0, 0), (0, 0, 0), (4096, 4096, 4096))
                    else:
                        e, s = euler(t[k][:9])
                        cache[(id(keys), bi, k)] = (tuple(t[k][9:12]), e, s)
                p, e, s = cache[(id(keys), bi, k)]
                o += struct.pack("<9h", *p, *e, *s)
    return bytes(o)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    disc, out = Disc(sys.argv[1]), Path(sys.argv[2])
    files = disc.files()
    slus = disc.find("SLUS_011.93")
    if not slus:
        sys.exit("This is not a Digimon World 2 (USA, SLUS-01193) disc.")
    ids = struct.unpack("<%dI" % 0xE5B, disc.read(*slus)[0x33F94:0x33F94 + 0x396C])  # file number -> first sector

    def fil(i):
        return disc.read(ids[i], files[ids[i]][1])

    ok = bad = 0
    for fid in (0xCB9, 0xCBA, 0xCBB):  # the Digimon table: 0x28-byte records
        d = fil(fid)
        o = struct.unpack("<I", d[:4])[0]
        while True:
            e = d[o:o + 0x28]
            k = (struct.unpack("<I", e[4:8])[0] >> 1) & 0x7FFF
            if k == 0:
                break
            name = glyphs(d[struct.unpack("<I", e[:4])[0]:].split(b"\xff")[0])
            model = struct.unpack("<h", e[6:8])[0]
            code = files[ids[model]][0].split("/")[-1][1:-4]
            if not name or "?" in name:
                name = code.lstrip("0123456789") or code  # unnamed (NPCs, bosses): the model's code
            label = "%03d_%s" % (k, name)
            try:
                afs = {}  # one motion file per group (a file shared by two groups keeps the last)
                for g, af in enumerate(struct.unpack("<11h", e[8:30])):
                    if af and ids[af] in files:
                        afs[ids[af]] = (g, fil(af))
                dst = out / "digimon" / label
                dst.mkdir(parents=True, exist_ok=True)
                (dst / (label + ".dw2m")).write_bytes(dw2m(fil(model), list(afs.values())))
                ok += 1
            except Exception as ex:
                bad += 1
                print("skipped", label, repr(ex))
            o += 0x28
    for lba, (path, size) in sorted(files.items()):  # the effects: EFFECT/<dir>/M<code>.BIN + A<code>.BIN, motion 0
        d, _, m = path.rpartition("/")
        if "/EFFECT/" not in "/" + d + "/" or not (m.startswith("M") and m.endswith(".BIN")):
            continue
        try:
            code = m[1:-4]
            dst = out / "fx" / code
            dst.mkdir(parents=True, exist_ok=True)
            (dst / (code + ".dw2m")).write_bytes(dw2m(disc.read(lba, size), [(0, disc.read(*disc.find(d + "/A" + m[1:])))]))
            ok += 1
        except Exception as ex:
            bad += 1
            print("skipped", path, repr(ex))
    print("%d models written, %d skipped -> %s" % (ok, bad, out))


if __name__ == "__main__":
    main()
