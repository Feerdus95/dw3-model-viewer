"""DCB Digimon PAK -> .dw2m (the format viewer/src/dw2.cpp reads), so the viewer's DW2 tab shows DCB models.
usage: todcbm.py OUTDIR DRV/F/187 [...]   -> OUTDIR/digimon/<id>_<name>/<id>_<name>.dw2m
Animations replay the game's own key easing (model_anim.c: setupRotationCurve / accelerateModelBones)."""
import sys, os, json, math, struct
from pak import chunks
from dcblib import decompress, anim
import omd

def cdiv(a, b):  # C integer division
    q = abs(a) // abs(b); return q if (a < 0) == (b < 0) else -q

def mul(A, B): return [[sum(A[i][k]*B[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
def rot_yxz(rx, ry, rz):
    f = 2*math.pi/4096
    cx, sx, cy, sy, cz, sz = math.cos(rx*f), math.sin(rx*f), math.cos(ry*f), math.sin(ry*f), math.cos(rz*f), math.sin(rz*f)
    return mul(mul([[cy,0,sy],[0,1,0],[-sy,0,cy]], [[1,0,0],[0,cx,-sx],[0,sx,cx]]), [[cz,-sz,0],[sz,cz,0],[0,0,1]])
import functools
@functools.lru_cache(maxsize=None)
def rot_euler(rx, ry, rz): return euler_zyx(rot_yxz(rx, ry, rz))
def euler_zyx(R):  # R = Rz*Ry*Rx -> (rx, ry, rz) in 4096 units
    k = 4096/(2*math.pi)
    ry = math.asin(max(-1, min(1, -R[2][0])))
    if abs(R[2][0]) < 0.99999: rx = math.atan2(R[2][1], R[2][2]); rz = math.atan2(R[1][0], R[0][0])
    else: rx = math.atan2(-R[1][2], R[1][1]); rz = 0
    return tuple(int(round(a*k)) for a in (rx, ry, rz))

class Chan:
    __slots__ = ('v', 'vel', 'a0', 'a1')
    def __init__(s, v=0): s.v, s.vel, s.a0, s.a1 = v, 0, 0, 0

def sim(keys, loop):
    """frames (one per 2 ticks) of [(pos, euler, scale)] per bone, starting posed at key 0"""
    nb = 32
    ch = [[Chan(k) for k in (0,0,0,0,0,0,4096<<16,4096<<16,4096<<16)] for _ in range(nb)]  # rot*3 (<<20), pos*3, scale*3 (<<16)
    attr = [keys[0][2][b][3] for b in range(nb)]   # obj->attribute: set from each key as it is approached (model_anim.c)
    for b in range(nb):
        kb = keys[0][2][b]
        for i in range(3): ch[b][i].v = kb[i] << 20
        for i in range(3): ch[b][3+i].v = kb[4+i] << 16
        for i in range(3): ch[b][6+i].v = kb[7+i] << 16
    n = len(keys); idx = 0; out = []; mode = 0; timer = 0; half = 0; tick = 0; wraps = []   # wraps: the ticks at which a looping clip goes back to its loop key
    def load(m):
        nonlocal idx, timer, half
        if idx < 0: timer = -1; return
        if not (m == 0 and idx + 1 < n) and loop >= 0: wraps.append(tick)
        dur = keys[idx][0]; half = dur; length = half*2; timer = length
        to = idx+1 if (m == 0 and idx+1 < n) else loop
        nl = 1; nk = keys[idx][2]
        if to >= 0: nl = keys[to][0]*2; nk = keys[to][2]
        cur = idx; idx = to if to >= 0 else -1
        for b in range(nb):
            k = keys[cur][2][b]; nx = nk[b]; attr[b] = k[3]
            tgt = (k[0], k[1], k[2], k[4], k[5], k[6], k[7], k[8], k[9]); nxt = (nx[0], nx[1], nx[2], nx[4], nx[5], nx[6], nx[7], nx[8], nx[9])
            for i, c in enumerate(ch[b]):
                sh = 20 if i < 3 else 16
                frm = cdiv(c.v, 0x100000) if i < 3 else (((c.v >> 16) + 0x8000) & 0xFFFF) - 0x8000
                start = frm << sh; c.v = start
                mid = (tgt[i] << sh) - start; slope = cdiv(mid, length)
                ev = cdiv(cdiv((nxt[i]-tgt[i]) << sh, nl) + slope, 2) if True else 0
                mv = slope*2 - cdiv(ev + c.vel, 2)
                c.a0 = cdiv(mv - c.vel, half) if half else 0; c.a1 = cdiv(ev - mv, half) if half else 0
    load(-1) if False else None
    # key 0 is "arrived" already: start easing towards key 1
    idx = 1 if n > 1 else -1
    if n == 1: return [[((keys[0][2][b][4], keys[0][2][b][5], keys[0][2][b][6]), tuple(keys[0][2][b][0:3]), tuple(keys[0][2][b][7:10]), keys[0][2][b][3]) for b in range(nb)]], False, 0
    load(0)  # eases into key 1 (idx was 1 -> `cur`=1)
    hp = half; ended = False
    # per tick: --timer<=0 -> next load; update matrices; accelerate
    while True:
        timer -= 1
        if timer <= 0:
            load(0)
            if idx < 0 and timer < 0: ended = True
            hp = half
        if ended or len(wraps) >= 3: break    # a looping clip: its first turn, and one whole period of the loop (steady state) after it
        if tick % 2 == 0:
            out.append([((int(c[3].v >> 16 if False else _i16(c[3].v)), _i16(c[4].v), _i16(c[5].v)),
                         (cdiv(c[0].v, 0x100000), cdiv(c[1].v, 0x100000), cdiv(c[2].v, 0x100000)),
                         (_i16(c[6].v), _i16(c[7].v), _i16(c[8].v)), attr[b]) for b, c in enumerate(ch)])
        for c in ch:
            for x in c:
                x.v += x.vel
        hp -= 1
        for c in ch:
            for x in c: x.vel += x.a0 if hp >= 0 else x.a1
        tick += 1
        if tick > 6000: break
    return out, loop >= 0, (wraps[1] // 2 if len(wraps) >= 3 else 0)

def _i16(v): return (((v >> 16) + 0x8000) & 0xFFFF) - 0x8000

def convert(b, outdir, name):
    cs = chunks(b)
    mc = [c for c in cs if c[0] == 0][0]; model = omd.parse(b[mc[2]:mc[2]+mc[3]])
    tc = [c for c in cs if c[0] == 5][0]; t = b[tc[2]:tc[2]+tc[3]]
    bl, cx, cy, cw, ch_ = struct.unpack_from('<IHHHH', t, 8)
    pal = [struct.unpack_from('<%dH' % cw, t, 20 + 2*cw*r) for r in range(ch_)]
    o = 8 + bl; bl2, ix, iy, iw, ih = struct.unpack_from('<IHHHH', t, o)
    img = t[o+12:o+12+iw*2*ih]
    W = 256; H = ih; ox = (ix % 64) * 2     # image placed where the tpage puts it, rows padded to the page
    rows = bytearray()
    for y in range(ih):
        row = bytearray(128); row[ox:ox+iw*2] = img[y*iw*2:(y+1)*iw*2][:128-ox]; rows += row
    pr = [bytearray(128) for _ in range(4)]
    for r, p in enumerate(pal):
        if r >= 16: break
        for i, v in enumerate(p[:16]): struct.pack_into('<H', pr[r//4], (r % 4)*32 + 2*i, v)
    for r in pr: rows += r
    B = model['bones']; nb = len(B)
    clips = []
    for i, s_, o2, sz in cs:
        if i != 1: continue
        if sz < 16: continue    # a 4-byte stub: the clip lives in another model's PAK
        raw = b[o2:o2+sz]; n0 = struct.unpack_from('<i', raw, 0)[0]
        d = raw if 0 < n0 < 5000 and sz == 4 + n0*772 else decompress(b, o2)   # some PAKs keep the clips uncompressed
        loop, keys = anim(d)
        fr, looped, ls = sim(keys, loop)
        clips.append((s_, fr, looped, ls))
    clips.sort(key=lambda c: c[0])
    # which bones ever lose their mesh: its own attribute, or itself/an ancestor collapsed to scale 0 (the children go with it)
    par = [x['parent'] for x in B]
    def collapsed(f):
        z = [max(abs(v) for v in f[i][2]) <= 48 for i in range(nb)]
        out = [None] * nb
        def up(i):
            if out[i] is None: out[i] = z[i] or (par[i] >= 0 and up(par[i]))
            return out[i]
        for i in range(nb): up(i)
        return out
    hides = set(); frames2 = []
    for s_, fr, looped, ls in clips:
        fr2 = []
        for f in fr:
            col = collapsed(f)
            hid = [bool(f[i][3]) or col[i] for i in range(nb)]
            hides.update(i for i in range(nb) if hid[i])
            fr2.append((f, hid, col))
        frames2.append(fr2)
    tb = nb
    hdr = bytearray(b'DCBM' + struct.pack('<HHHH', 1, tb, W, H + 4)) + rows
    hdr += struct.pack('<H', len(clips))
    for s_, fr, looped, ls in clips: hdr += struct.pack('<HHHBBH', 0, s_, len(fr), 1 if looped else 0, 0, ls)
    skipped = badclut = 0
    def bone_record(parent, verts, faces):
        r = struct.pack('<hHH', parent, len(verts), len(faces))
        for v in verts: r += struct.pack('<3h', *v)
        for fl, vs, uv, cl, rgb in faces: r += struct.pack('<B4H8BH3B', fl, *vs, *[c for p in uv for c in p], cl, *rgb)
        return r
    recs = []
    for bi, bn in enumerate(B):
        verts = []; faces = []
        for pt in bn['parts']:
            vo = len(verts); verts += pt['verts']
            for tri, (cl, tp, tex, code, col) in zip(pt['tris'], pt['attr']):
                r = (cl >> 6) - cy
                if tex and not (0 <= r < len(pal) and r < 16): badclut += 1; r = 0
                clut = ((H + (r // 4)) << 6) | (r % 4)
                vs = [x[0] + vo for x in tri] + [0]; uv = [((x[1] & 0xFF), (x[1] >> 8) & 0xFF) for x in tri] + [(0, 0)]
                fl = (2 if tex else 0) | (4 if code & 2 else 0)
                faces.append((fl, vs, uv, clut if tex else 0, (col & 0xFF, (col >> 8) & 0xFF, (col >> 16) & 0xFF)))
        recs.append(bone_record(bn['parent'], verts, faces))
    for r in recs: hdr += r
    for (s_, fr, looped, ls), fr2 in zip(clips, frames2):
        for f, hid, col in fr2:
            for bi in range(nb):
                pos, rot, sc, at = f[bi]
                bp = B[bi]['pos']
                if max(abs(v) for v in sc) <= 48: sc = (4096, 4096, 4096)
                e = rot_euler(*rot)
                hdr += struct.pack('<9h', bp[0]+pos[0], bp[1]+pos[1], bp[2]+pos[2], *e, *sc)
    for fr2 in frames2:   # then, a byte a bone a frame: 1 while the mesh is not drawn
        for f, hid, col in fr2:
            hdr += bytes(1 if hid[bi] else 0 for bi in range(nb))
    d = os.path.join(outdir, 'digimon', name); os.makedirs(d, exist_ok=True)
    open(os.path.join(d, name + '.dcbm'), 'wb').write(hdr)
    return tb, len(clips), sum(1 for x in hides), badclut
