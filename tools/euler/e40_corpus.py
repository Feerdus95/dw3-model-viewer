"""E4.0 (READ-ONLY): structural survey of every model in viewer/data/files, for choosing structurally diverse description subjects.
Purely geometric facts at the idle pose (no anatomy): bones, depth, mirrored sibling-chain groups (children of one parent whose offsets mirror in x) with their chain length and
vertical/horizontal direction, skeleton bounding box (units; DW3 y is down, forward = -z). Run: python e40_corpus.py [DIR] ; 4 workers ; prints a table, writes kb/e40-corpus-output.txt"""
import os, sys, glob
import numpy as np
from multiprocessing import Pool
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, ".."))
from e7_compare import load_raw, Player, pose_track
from e8_resample import parents_of, World


def analyse(path):
    try:
        tr, mo = load_raw(path); par = parents_of(path); n = len(tr)
        if n < 3 or not mo: return path, None
        fr = Player(tr, mo).idle
        ref = [[pose_track(tr[b][t], fr) for t in range(3)] for b in range(n)]
        W = World(par, n).update(ref); P = np.array([W[b][:3, 3] for b in range(1, n + 1)])
        ch = {}
        for b in range(1, n + 1): ch.setdefault(par[b - 1], []).append(b)
        off = lambda b: np.array(ref[b - 1][0], float)
        depth = {}
        for b in range(1, n + 1):
            d, x = 0, b
            while par[x - 1]: x = par[x - 1]; d += 1
            depth[b] = d
        def chain(c):
            out = [c]
            while len(ch.get(out[-1], [])) == 1: out.append(ch[out[-1]][0])
            return out
        groups = []
        for p, kids in ch.items():
            for i in range(len(kids)):
                for j in range(i + 1, len(kids)):
                    a, b = kids[i], kids[j]; oa, ob = off(a), off(b)
                    if np.linalg.norm(oa) < 20 or abs(oa[0]) < 10: continue
                    if np.linalg.norm(oa - np.array([-ob[0], ob[1], ob[2]])) > 0.15 * np.linalg.norm(oa) + 5: continue
                    ca, cb = chain(a), chain(b)
                    if len(ca) != len(cb): continue
                    d = W[ca[-1]][:3, 3] - W[p][:3, 3] if p else W[ca[-1]][:3, 3]
                    groups.append((p, len(ca), 'down' if d[1] > 0.5 * np.linalg.norm(d) else ('up' if d[1] < -0.3 * np.linalg.norm(d) else 'side'), round(float(np.linalg.norm(d)))))
        ext = P.max(0) - P.min(0)
        leaves = [b for b in range(1, n + 1) if b not in ch]
        return path, dict(n=n, maxdepth=max(depth.values()), leaves=len(leaves), groups=groups, ext=[round(float(x)) for x in ext], rootkids=len(ch.get(0, [])))
    except Exception as ex:
        return path, 'ERR ' + str(ex)[:60]


if __name__ == '__main__':
    d = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', 'data', 'files')
    files = sorted(glob.glob(os.path.join(d, '*.bin')))
    with Pool(4) as p: res = p.map(analyse, files, chunksize=8)
    lines = []
    for path, r in res:
        nm = os.path.basename(path)
        if r is None or isinstance(r, str): lines.append(f'{nm} {r}'); continue
        g = ' '.join(f'{a}:{k}x{c}{dr[0]}{ln}' for a, k, c, ln in [(x[0], 2, x[1], x[2]) for x in []]) or ''
        g = ' '.join(f'p{x[0]}/len{x[1]}/{x[2]}/{x[3]}' for x in r['groups'])
        lines.append(f"{nm} bones {r['n']} depth {r['maxdepth']} leaves {r['leaves']} rootkids {r['rootkids']} ext(x,y,z) {r['ext']} mirrored-groups {len(r['groups'])}: {g}")
    out = '\n'.join(lines); print(out)
    open(os.path.join(HERE, '..', '..', '..', 'kb', 'e40-corpus-output.txt'), 'w', encoding='utf-8').write(out + '\n')
