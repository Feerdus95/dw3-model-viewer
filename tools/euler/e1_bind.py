"""E1.1 (READ-ONLY): do the skin's inverseBindMatrices match the nodes' rest pose? Run: python e1_bind.py FILE.glb"""
import sys, numpy as np
from e7_compare import read_glb


def main(path):
    js, b = read_glb(path); N = js['nodes']
    par = {c: i for i, n in enumerate(N) for c in n.get('children', [])}
    def L(n):
        if 'matrix' in n: return np.array(n['matrix'], float).reshape(4, 4).T
        x, y, z, w = n.get('rotation', [0, 0, 0, 1]); s = n.get('scale', [1, 1, 1])
        R = np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)], [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)], [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])
        M = np.eye(4); M[:3, :3] = R * np.array(s); M[:3, 3] = n.get('translation', [0, 0, 0]); return M
    def G(i, stop=None):
        M = L(N[i])
        while i in par and par[i] != stop: i = par[i]; M = L(N[i]) @ M
        return M
    for si, sk in enumerate(js['skins']):
        a = js['accessors'][sk['inverseBindMatrices']]; bv = js['bufferViews'][a['bufferView']]
        ibm = np.frombuffer(b, '<f4', a['count'] * 16, bv.get('byteOffset', 0) + a.get('byteOffset', 0)).reshape(-1, 4, 4).transpose(0, 2, 1)
        J = sk['joints']; meshes = [i for i, n in enumerate(N) if n.get('skin') == si]
        print(f'\nskin {si}: {len(J)} joints; mesh nodes {[N[i]["name"] for i in meshes]}; IBM count {a["count"]}')
        for m in meshes:
            Mm = G(m); print(f'  mesh node {N[m]["name"]} world diag/translation: {np.round(np.diag(Mm)[:3], 4)} {np.round(Mm[:3, 3], 4)}, parent {N[par[m]]["name"] if m in par else None}')
        for label, fn in (('IBM . jointWorld (incl. root node)', lambda j: G(j)),
                          ('IBM . jointWorld excluding the root parent node', lambda j: G(j, stop=par.get(J[0]) if J[0] in par else None))):
            e = np.array([np.abs(ibm[k] @ fn(j) - np.eye(4)).max() for k, j in enumerate(J)])
            print(f'  {label}: max {e.max():.3e}, median {np.median(e):.3e}, joints > 1e-3: {(e > 1e-3).sum()}')
        # also: mesh-relative (inverse mesh world . joint world)
        for m in meshes[:1]:
            e = np.array([np.abs(ibm[k] @ np.linalg.inv(G(m)) @ G(j) - np.eye(4)).max() for k, j in enumerate(J)])
            print(f'  IBM . inv(meshWorld) . jointWorld: max {e.max():.3e}, median {np.median(e):.3e}, > 1e-3: {(e > 1e-3).sum()}')
        gaps = sorted({N[par[j]]['name'] for j in J if j in par and par[j] not in set(J)})
        print('  joints whose parent is not a joint:', gaps)
    s0, s1 = set(js['skins'][0]['joints']), set(js['skins'][1]['joints']) if len(js['skins']) > 1 else set()
    print('\njoints in skin 1 only:', [N[i]['name'] for i in s1 - s0], '; skin 0 joints without bind in skin 1 (count):', len(s0 - s1))
    mnames = [n['name'] for n in N if n.get('mesh') is not None]; print('mesh nodes:', mnames)


if __name__ == '__main__':
    main(sys.argv[1])
