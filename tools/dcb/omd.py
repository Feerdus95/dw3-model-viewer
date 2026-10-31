"""DCB OMD model parser, from tmd_sort.c (sortModelObject / sortModelPrimitives)."""
import struct, sys

TMDID = bytes([0x41, 0, 0, 0, 0, 0, 0, 0])

def parse_part(d, o):
    """one OMD0 block at o -> dict(verts, normals, tris[(a,b,c,uvs)], end)"""
    assert d[o:o+4] == b'OMD0', d[o:o+4]
    ptr = o + 12 + 4          # bbox is words 0..3, then offset word (unk14) -> data
    ptr = o + struct.unpack_from('<i', d, o + 12 + 20)[0]
    nparts = struct.unpack_from('<I', d, ptr)[0]; p = ptr + 4
    parts = []
    for _ in range(nparts):
        nv = struct.unpack_from('<I', d, p)[0]; p += 4
        verts = [struct.unpack_from('<3h', d, p + 8*i) for i in range(nv)]; p += 8*nv
        ng = struct.unpack_from('<I', d, p)[0]; p += 4
        nn = 0; gcol = []; norms = []; cslot = []   # colour of each lit normal (one entry per colour slot); the normals; slot -> normal
        for _ in range(ng):
            w = struct.unpack_from('<I', d, p)[0]; p += 4
            c = w & 0xFF
            gcol += [w >> 8] * (c if c else 1)
            cslot += list(range(len(norms), len(norms) + c)) if c else [-1]
            norms += [struct.unpack_from('<3h', d, p + 8*i) for i in range(c)]
            nn += c; p += 8*c      # normals (lit); position only is exported
        tris = []; attr = []
        W = lambda i: struct.unpack_from('<I', d, i)[0]
        while True:
            h = W(p); p += 4
            if h == 0: break
            fans, strips, code = h >> 20, (h >> 8) & 0xFFF, h & 0xFF
            quad, tex = bool(code & 8), bool(code & 4)
            for pas, count in ((0, fans), (1, strips)):
                for _ in range(count):
                    run = W(p); p += 4
                    L, S = run >> 16, run & 0xFFFF
                    for _ in range(S):
                        vi = lambda w: (w >> 16) // 8
                        ci = lambda w: ((w & 0xFFFF) - 8 * nv) // 4
                        t0 = len(tris); sa = (0, 0)
                        if not quad:
                            n0 = 6 if tex else 3
                            vw = [W(p+4*i) for i in range(3)]
                            raw = [W(p+12+4*i) for i in range(3)] if tex else [0]*3; sa = (raw[0] >> 16, raw[1] >> 16); uv = [x & 0xFFFF for x in raw]
                            p += 4*n0
                            R = [[vi(vw[i]), uv[i], ci(vw[i])] for i in range(3)]
                            tris.append((R[0], R[1], R[2]))
                            for k in range(1, L):
                                nw = W(p); nuv = W(p+4) & 0xFFFF if tex else 0
                                p += 8 if tex else 4
                                nv_ = [vi(nw), nuv, ci(nw)]
                                if pas: slot = (2, 0, 1)[(k-1) % 3]
                                else: slot = 2 if k % 2 else 1
                                R[slot] = nv_
                                t = (R[0], R[1], R[2])
                                tris.append(t if k % 2 == 0 else (t[0], t[2], t[1]))
                        else:
                            n0 = 8 if tex else 4
                            vw = [W(p+4*i) for i in range(4)]
                            raw = [W(p+16+4*i) for i in range(4)] if tex else [0]*4; sa = (raw[0] >> 16, raw[1] >> 16); uv = [x & 0xFFFF for x in raw]
                            p += 4*n0
                            R = [[vi(vw[i]), uv[i], ci(vw[i])] for i in range(4)]
                            def emit(flip):
                                a, b, c, e = R
                                ts = [(a, b, c), (b, e, c)]
                                for t in ts: tris.append((t[0], t[2], t[1]) if flip else t)
                            emit(False)
                            for k in range(1, L):
                                w1, w2 = W(p), W(p+4)
                                u1, u2 = (W(p+8) & 0xFFFF, W(p+12) & 0xFFFF) if tex else (0, 0)
                                p += 16 if tex else 8
                                if pas:
                                    sl = (2, 3) if k % 2 else (0, 1)
                                else:
                                    sl = (2, 3) if k % 2 else (1, 3)
                                R[sl[0]] = [vi(w1), u1, ci(w1)]; R[sl[1]] = [vi(w2), u2, ci(w2)]
                                emit(k % 2 == 1)
                        attr += [(sa[0], sa[1], tex, code, gcol[max(0, ((vw[0] & 0xFFFF) - 8*nv) // 4)] if ((vw[0] & 0xFFFF) - 8*nv) // 4 < len(gcol) else 0x808080)] * (len(tris) - t0)
        parts.append(dict(verts=verts, tris=tris, attr=attr, nnorm=nn, gcol=gcol, norms=norms, slot=cslot))
    return parts, p

def parse_tmd(d, o):
    """one standard TMD block (id 0x41) at o: objects are the bone's parts. Packet layouts as the PsyQ TMD format."""
    nobj = struct.unpack_from('<I', d, o + 8)[0]; base = o + 12; parts = []; end = base + 28*nobj
    U = lambda i: struct.unpack_from('<H', d, i)[0]
    for k in range(nobj):
        vt, nv, nt, nn, pt, npr, sc = struct.unpack_from('<7i', d, base + 28*k)
        verts = [struct.unpack_from('<3h', d, base + vt + 8*i) for i in range(nv)]
        tris = []; attr = []; q = base + pt
        for _ in range(npr):
            olen, ilen, fl, mode = struct.unpack_from('<4B', d, q); w = q + 4; q += 4 + 4*ilen
            tex, quad, semi, lit = mode & 4, mode & 8, mode & 2, not (fl & 1)
            nv_ = 4 if quad else 3
            if mode & 0x10 and not lit: raise ValueError('unlit gouraud TMD packet')
            rgb = 0x808080; uv = None
            if tex:
                if not lit: rgb = struct.unpack_from('<I', d, w)[0] & 0xFFFFFF; w += 4
                uv = [struct.unpack_from('<BBH', d, w + 4*i) for i in range(nv_)]; w += 4*nv_
            else:
                rgb = struct.unpack_from('<I', d, w)[0] & 0xFFFFFF; w += 4
            if mode & 0x10:   # gouraud, lit: (normal, vertex) a corner
                vi = [U(w + 4*i + 2) for i in range(nv_)]
            elif lit:         # flat, lit: normal, then the vertices
                vi = [U(w + 2 + 2*i) for i in range(nv_)]
            else:             # flat, unlit: the vertices
                vi = [U(w + 2*i) for i in range(nv_)]
            R = [[vi[i], (uv[i][0] | uv[i][1] << 8) if uv else 0] for i in range(nv_)]
            ts = [(R[0], R[1], R[2])] + ([(R[1], R[3], R[2])] if quad else [])
            cl, tp = (uv[0][2], uv[1][2]) if uv else (0, 0)
            for t in ts: tris.append(t); attr.append((cl, tp, bool(tex), mode, rgb))
        parts.append(dict(verts=verts, tris=tris, attr=attr, nnorm=nn, gcol=[]))
        end = max(end, q)
    return parts, end

def parse(d):
    nobj = struct.unpack_from('<H', d, 16)[0]
    parent = [struct.unpack_from('<b', d, 20 + i)[0] for i in range(nobj)]
    pos = [struct.unpack_from('<3h', d, 52 + 12*i + 4) for i in range(nobj)]
    bones = []; o = 52 + 12*nobj
    for i in range(nobj):
        while d[o:o+4] != b'OMD0' and d[o:o+8] != TMDID:
            o += 4
            if o >= len(d): raise ValueError('no OMD0 or TMD block')
        parts, end = parse_part(d, o) if d[o:o+4] == b'OMD0' else parse_tmd(d, o)
        bones.append(dict(parent=parent[i], pos=pos[i], parts=parts, off=o, end=end)); o = end
    return dict(tim=d[:16].split(b'\0')[0].decode(), bones=bones)

if __name__ == '__main__':
    from pak import chunks
    b = open(sys.argv[1], 'rb').read()
    c = [c for c in chunks(b) if c[0] == 0][0]; d = b[c[2]:c[2]+c[3]]
    m = parse(d)
    for i, bn in enumerate(m['bones']):
        pt = bn['parts'][0]
        print(i, 'parent', bn['parent'], 'pos', bn['pos'], 'v', len(pt['verts']), 'tris', len(pt['tris']), 'parsed to', bn['end'], 'next OMD0', d.find(b'OMD0', bn['end']))
