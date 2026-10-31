"""read-only survey of DW3 model key tracks (mirrors viewer/src/model.cpp:44-60,239-272)"""
import struct, sys, glob, os
from collections import Counter

def u32(b, o): return struct.unpack_from('<I', b, o)[0]

class Arc:
    def __init__(s, b): s.b = b
    def n(s): return len(s.b) // 4 and u32(s.b, 0) // 4 if len(s.b) >= 4 else 0
    def off(s, i): return u32(s.b, i * 4)
    def end(s, i):
        o = s.off(i); e = len(s.b)
        for j in range(s.n()):
            x = s.off(j)
            if o < x < e: e = x
        return e
    def sub(s, i): return Arc(s.b[s.off(i):s.end(i)])

def find_bone(a):
    n = a.n()
    for i in range(n):
        o = a.off(i)
        if o >= len(a.b) or a.end(i) - o < 8: continue
        tex, cnt = u32(a.b, o), u32(a.b, o + 4)
        if tex >= n or cnt == 0 or cnt > 255 or 8 + 12 * cnt > a.end(i) - o: continue
        ok = all(u32(a.b, o + 8 + 12 * k) <= k and u32(a.b, o + 12 + 12 * k) < n and u32(a.b, o + 16 + 12 * k) < n for k in range(cnt))
        if ok: return i
    return -1

def tracks(ka):
    out = []
    for t in range(3):
        keys = []
        if t < ka.n():
            p = ka.off(t)
            while p + 8 <= len(ka.b):
                k = struct.unpack_from('<hhhh', ka.b, p); keys.append(k); p += 8
                if k[3] >= 30000: break
        out.append(keys)
    return out

def survey(path):
    a = Arc(open(path, 'rb').read())
    if a.n() <= 0 or a.n() * 4 > len(a.b): return None
    bi = find_bone(a)
    if bi < 0: return None
    e = a.off(bi); cnt = u32(a.b, e + 4)
    r = dict(bones=cnt, rot0_nonzero=0, pos_anim=0, rot_anim=0, scl_anim=0, scl0_not_full=0, rotkeys=0)
    for b in range(cnt):
        ka = a.sub(u32(a.b, e + 16 + b * 12))
        pos, rot, scl = tracks(ka)
        pos, rot, scl = [[k for k in t if k[3] < 30000] for t in (pos, rot, scl)]
        if rot and any(rot[0][:3]): r['rot0_nonzero'] += 1
        if len(set(k[:3] for k in pos)) > 1: r['pos_anim'] += 1
        if len(set(k[:3] for k in rot)) > 1: r['rot_anim'] += 1
        if len(set(k[:3] for k in scl)) > 1: r['scl_anim'] += 1
        if scl and scl[0][:3] != (4096, 4096, 4096): r['scl0_not_full'] += 1
        r['rotkeys'] += len(rot)
    return r

if __name__ == '__main__':
    tot = Counter(); n = 0; rows = []
    for f in sorted(glob.glob(sys.argv[1] + '/*.bin')):
        try: r = survey(f)
        except Exception as ex: r = None
        if not r: continue
        n += 1; rows.append((os.path.basename(f), r))
        for k, v in r.items(): tot[k] += v
    print('models', n, dict(tot))
    for name, r in rows[:int(sys.argv[2]) if len(sys.argv) > 2 else 12]: print(name, r)
