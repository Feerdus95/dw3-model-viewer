"""E1 (READ-ONLY): inspect a Mixamo-style GLB. Run: python e1_inspect.py FILE.glb [AnimName]"""
import math, sys, os
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from e7_compare import read_glb, accessor
from euler_zyx import geodesic, unquat


def main(path, anim=None):
    js, bin_ = read_glb(path); N = js['nodes']
    par = {c: i for i, n in enumerate(N) for c in n.get('children', [])}
    print(f'asset {js["asset"]}; nodes {len(N)}, skins {len(js.get("skins", []))}, animations {[a["name"] for a in js.get("animations", [])]}')
    sk = js['skins'][0]; J = sk['joints']; jset = set(J)
    print(f'skin 0: {len(J)} joints, skeleton root node {sk.get("skeleton")}, IBM accessor {sk.get("inverseBindMatrices")}')
    def depth(i):
        d = 0
        while i in par: i = par[i]; d += 1
        return d
    print('\n## non-joint ancestors of the skeleton (scale/rotation here changes units)')
    r = J[0]
    while r in par:
        r = par[r]; n = N[r]; print(f'  node {r} {n.get("name")}: T={n.get("translation")} R={n.get("rotation")} S={n.get("scale")}')
    print('\n## joint hierarchy, rest TRS (local)')
    for j in J:
        n = N[j]; t = n.get('translation', [0, 0, 0]); q = n.get('rotation', [0, 0, 0, 1]); s = n.get('scale', [1, 1, 1])
        print(f'{"  " * (depth(j) - depth(J[0]))}{n["name"].replace("mixamorig:", "")}  T=({t[0]:.2f},{t[1]:.2f},{t[2]:.2f}) |q-id|={math.degrees(2 * math.acos(min(1, abs(q[3])))):.1f}deg S={s}')
    anims = js['animations']
    a = next((x for x in anims if x['name'] == anim), anims[0]); print(f'\n## animation {a["name"]}')
    chans = {}
    for c in a['channels']:
        s = a['samplers'][c['sampler']]
        chans[(c['target']['node'], c['target']['path'])] = (accessor(js, bin_, s['input'])[:, 0], accessor(js, bin_, s['output']), s.get('interpolation', 'LINEAR'))
    paths = {}
    for (nd, p) in chans: paths[p] = paths.get(p, 0) + 1
    print('channels per path', paths, '; interpolations', {v[2] for v in chans.values()}, '; targets outside skin', sorted({N[n]['name'] for n, _ in chans if n not in jset}))
    ts = next(iter(chans.values()))[0]; dt = np.diff(ts)
    print(f'time {ts[0]:.4f}..{ts[-1]:.4f}s, {len(ts)} keys on the first channel; dt min/median/max {dt.min():.5f}/{np.median(dt):.5f}/{dt.max():.5f} -> effective {1 / np.median(dt):.3f} FPS')
    same = all(len(v[0]) == len(ts) and np.allclose(v[0], ts) for v in chans.values())
    print('all channels share one time base:', same, '; key counts', sorted({len(v[0]) for v in chans.values()}))
    print('\n## rest pose vs first/last animated pose (rotation geodesic, deg; translation delta)')
    rows = []
    for j in J:
        n = N[j]; q0 = np.array(n.get('rotation', [0, 0, 0, 1]), float)
        rq = chans.get((j, 'rotation'))
        if rq is None: rows.append((j, None, None, None)); continue
        R0 = unquat(q0); d0 = geodesic(R0, unquat(rq[1][0].astype(float))) * 180 / math.pi
        dmax = max(geodesic(R0, unquat(q.astype(float))) for q in rq[1]) * 180 / math.pi
        rows.append((j, d0, dmax, None))
    for j, d0, dm, _ in rows:
        if d0 is None: print(f'  {N[j]["name"]}: no rotation channel'); continue
        tr = chans.get((j, 'translation'))
        tx = '' if tr is None else f'  T key0-rest={np.linalg.norm(tr[1][0] - np.array(N[j].get("translation", [0, 0, 0]))):.3f}'
        print(f'  {N[j]["name"].replace("mixamorig:", "")}: first-vs-rest {d0:.2f}  max-vs-rest {dm:.2f}{tx}')
    print('\n## which joints have translation animation:', [N[n]['name'].replace('mixamorig:', '') for (n, p) in chans if p == 'translation' and n in jset])
    q = chans[(J[0], 'rotation')][1]
    print('quaternion sign flips between consecutive keys (hips):', int(sum(np.dot(q[i], q[i + 1]) < 0 for i in range(len(q) - 1))))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
