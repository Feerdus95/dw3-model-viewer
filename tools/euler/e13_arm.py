"""E1.3 (READ-ONLY, in memory): retarget ONE arm, Mixamo Soldier/Idle LeftArm,LeftForeArm,LeftHand -> Agumon bones 4,5,6.
Run: python e13_arm.py [SOLDIER.glb] [AGUMON.bin]      Writes nothing; no viewer/format/converter change.
Method (world-direction retarget, independent of the bones' local axes):
 1. Body frame B = columns (left, up, forward) from each skeleton's own reference geometry; carried by the chest bone
    (Mixamo Spine2 / Agumon bone 1) through a constant offset: B(t) = Rchest(t) * offset.
 2. Segment frame F_s = Rworld_s * C_s, C_s constant, built at the reference pose: y = bone direction (towards the child;
    Agumon bone 6 has no child -> forearm direction), z = body forward made orthogonal to y, x = y cross z.
 3. Q_s(t) = B_M(t)^-1 F_s,M(t): segment orientation in body coordinates.
    ABS:   F_s,A(t) = B_A Q_s(t)                                  (Agumon takes Mixamo's body-frame orientation)
    DELTA: F_s,A(t) = B_A [Q_s(t) Q_s,M,ref^-1] Q_s,A,ref         (Mixamo's change from its T-pose applied on Agumon's idle)
 4. Agumon world rotation = F_s,A C_s,A^-1; local rotations from the parent chain; Euler keys by the existing rotations_to_keys.
Mixamo reference = node rest (validated bind pose). Agumon reference = the idle frame of motion 1; chest (bone 1) held at it."""
import math, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, ".."))
from e7_compare import read_glb, accessor, load_raw, Player, pose_track
from e8_resample import parents_of, World
from euler_zyx import geodesic, unquat
from dw3_euler import key_matrix, rotations_to_keys

D = 180 / math.pi
U = lambda v: v / np.linalg.norm(v)
ang = lambda a, b: math.acos(max(-1, min(1, float(np.dot(U(a), U(b)))))) * D


def frame_from(y, fwd):
    y = U(y); z = U(fwd - np.dot(fwd, y) * y); return np.column_stack([np.cross(y, z), y, z])


def stats(v):
    v = np.asarray(v, float); return f'n={len(v)} mean={v.mean():.2f} p50={np.percentile(v, 50):.2f} p95={np.percentile(v, 95):.2f} max={v.max():.2f}'


def nrot(W): return W[:3, :3] / np.linalg.norm(W[:3, :3], axis=0)


class Mix:
    def __init__(self, path, anim='Idle'):
        js, b = read_glb(path); self.N = js['nodes']
        self.par = {c: i for i, n in enumerate(self.N) for c in n.get('children', [])}
        self.idx = {n['name'].replace('mixamorig:', ''): i for i, n in enumerate(self.N)}
        a = next(x for x in js['animations'] if x['name'] == anim); self.rot = {}
        for c in a['channels']:
            if c['target']['path'] == 'rotation':
                self.rot[c['target']['node']] = accessor(js, b, a['samplers'][c['sampler']]['output']).astype(float)
        self.nf = len(next(iter(self.rot.values())))
        self.fps = 1 / float(np.median(np.diff(accessor(js, b, a['samplers'][0]['input'])[:, 0])))

    def Rl(self, i, f):
        if f is not None and i in self.rot: return unquat(self.rot[i][f])
        return unquat(np.array(self.N[i].get('rotation', [0, 0, 0, 1]), float))

    def Rw(self, i, f=None):
        R = self.Rl(i, f)
        while i in self.par: i = self.par[i]; R = self.Rl(i, f) @ R
        return R

    def off(self, i): return np.array(self.N[i].get('translation', [0, 0, 0]), float)   # offset in the parent's frame

    def pos_rest(self, i):
        p = self.off(i)
        while i in self.par:
            j = self.par[i]; p = self.Rl(j, None) @ (np.array(self.N[j].get('scale', [1, 1, 1]), float) * p) + self.off(j); i = j
        return p


def main(sg, ab, anim='Idle', dedup=False, side='Left', carrier=False):
    seg = [4, 5, 6] if side == 'Left' else [7, 8, 9]
    M = Mix(sg, anim); I = M.idx
    if dedup and all(geodesic(unquat(q[-1]), unquat(q[0])) * D < 0.05 for q in M.rot.values()): M.nf -= 1   # last key repeats the first: drop it for a clean loop
    chain_m = [(side + 'Arm', side + 'ForeArm'), (side + 'ForeArm', side + 'Hand'), (side + 'Hand', side + 'HandMiddle1')]
    P = lambda n: M.pos_rest(I[n])
    left = U(P('LeftUpLeg') - P('RightUpLeg')); up0 = P('Neck') - P('Hips'); up = U(up0 - np.dot(up0, left) * left); fwd = np.cross(left, up)
    BM = np.column_stack([left, up, fwd]); chest = I['Spine2']; offM = M.Rw(chest).T @ BM
    CM = [M.Rw(I[s]).T @ frame_from(M.Rw(I[s]) @ U(M.off(I[c])), fwd) for s, c in chain_m]
    # Agumon reference
    tr, mo = load_raw(ab); par = parents_of(ab); n = len(tr); fr = Player(tr, mo).idle
    ref = [[pose_track(tr[b][t], fr) for t in range(3)] for b in range(n)]
    assert all(ref[b][2] == (4096, 4096, 4096) for b in [0] + [x - 1 for x in seg]), 'non-unit scale on the arm chain'
    W = World(par, n).update(ref); rot = lambda b: nrot(W[b]); pos = lambda b: W[b][:3, 3]
    aleft = U(pos(11) - pos(15)); aup0 = pos(2) - pos(10); aup = U(aup0 - np.dot(aup0, aleft) * aleft); afwd = np.cross(aleft, aup)
    BA = np.column_stack([aleft, aup, afwd]); offA = rot(1).T @ BA
    print(f'Agumon body frame (world): left={np.round(aleft, 2)} up={np.round(aup, 2)} forward={np.round(afwd, 2)} det={np.linalg.det(BA):.3f}  (expect left=+x as verified, forward ~ -z)')
    print(f'Mixamo body frame (world): left={np.round(left, 2)} up={np.round(up, 2)} forward={np.round(fwd, 2)} det={np.linalg.det(BM):.3f}; clip {M.nf} frames at {M.fps:.3f} fps')
    offs = {}
    for k, b in enumerate(seg[:2]):
        c = seg[k + 1]; v = {tuple(x[:3]) for x in tr[c - 1][0] if x[3] < 30000}; offs[b] = np.array(ref[c - 1][0], float)
        print(f'Agumon bone {c} position track: {len(v)} distinct value(s); reference offset in its parent {offs[b]}')
    dA = [U(rot(seg[0]) @ offs[seg[0]]), U(rot(seg[1]) @ offs[seg[1]])]; dA.append(dA[1])      # bone 6 has no child: forearm direction
    CA = [rot(b).T @ frame_from(d, afwd) for b, d in zip(seg, dA)]
    Q = []
    for f in range(M.nf):
        Bt = M.Rw(chest, f) @ offM
        Q.append([Bt.T @ M.Rw(I[s], f) @ CM[k] for k, (s, _) in enumerate(chain_m)])
    QMr = [BM.T @ M.Rw(I[s]) @ CM[k] for k, (s, _) in enumerate(chain_m)]
    QAr = [BA.T @ rot(b) @ CA[k] for k, b in enumerate(seg)]
    for k, nm in enumerate([f'{side}Arm->{seg[0]}', f'{side}ForeArm->{seg[1]}', f'{side}Hand->{seg[2]}']):
        print(f'reference frames in body coords, Mixamo T-pose vs Agumon idle, {nm}: differ by {geodesic(QMr[k], QAr[k]) * D:.1f} deg (bone direction by {ang(QMr[k][:, 1], QAr[k][:, 1]):.1f} deg)')
    Rch = rot(1)
    Qc = [BM.T @ M.Rw(chest, f) @ offM for f in range(M.nf)]      # carrier (chest) orientation in body coords; used only when carrier=True (E1.9)
    BAt = (lambda f: BA @ Qc[f]) if carrier else (lambda f: BA)
    Rt = [BAt(f) @ (rot(1).T @ BA).T if carrier else Rch for f in range(M.nf)]
    lu, lf = np.linalg.norm(M.off(I[side + 'ForeArm'])), np.linalg.norm(M.off(I[side + 'Hand'])); lm = lu + lf
    la = np.linalg.norm(offs[seg[0]]) + np.linalg.norm(offs[seg[1]])
    dirs_mix = []
    for f in range(M.nf):
        Bt = M.Rw(chest, f) @ offM
        dirs_mix.append((Bt.T @ M.Rw(I[side + 'Arm'], f) @ U(M.off(I[side + 'ForeArm'])), Bt.T @ M.Rw(I[side + 'ForeArm'], f) @ U(M.off(I[side + 'Hand']))))
    wm = [d[0] * lu + d[1] * lf for d in dirs_mix]
    out = {}
    for mode in ('ABS', 'DELTA'):
        Wr = []
        for f in range(M.nf):
            Fs = [BAt(f) @ (Q[f][k] if mode == 'ABS' else Q[f][k] @ QMr[k].T @ QAr[k]) for k in range(3)]
            Wr.append([Fs[k] @ CA[k].T for k in range(3)])
        loc = [[Rt[f].T @ w[0], w[0].T @ w[1], w[1].T @ w[2]] for f, w in enumerate(Wr)]
        keys = []
        try:
            for k in range(3): keys.append(rotations_to_keys([loc[f][k] for f in range(M.nf)], tuple(ref[seg[k] - 1][1])))
        except Exception as ex:
            print(mode, 'converter refused:', str(ex)[:100]); continue
        out[mode] = keys
        rec = []
        for f in range(M.nf):
            r4 = Rt[f] @ key_matrix(keys[0][f]); r5 = r4 @ key_matrix(keys[1][f]); r6 = r5 @ key_matrix(keys[2][f]); rec.append((r4, r5, r6))
        qerr = [geodesic(Wr[f][k], rec[f][k]) * D for f in range(M.nf) for k in range(3)]
        print(f'\n## {mode}: converter keys {sum(len(k) for k in keys)}; quantisation error of the world rotations, deg: {stats(qerr)}')
        mx = max(abs(int(keys[k][f + 1][c]) - int(keys[k][f][c])) for k in range(3) for f in range(M.nf - 1) for c in range(3))
        print(f'  largest key step between consecutive frames: {mx} units ({mx / 4096 * 360:.1f} deg); loop closure last->first, local rotation deg (bones 4/5/6): ' +
              '/'.join(f'{geodesic(key_matrix(keys[k][-1]), key_matrix(keys[k][0])) * D:.2f}' for k in range(3)) +
              ' ; Mixamo ' + '/'.join(f'{geodesic(unquat(M.rot[I[s]][M.nf - 1]), unquat(M.rot[I[s]][0])) * D:.2f}' for s, _ in chain_m))
        d_rec = [(BAt(f).T @ U(rec[f][0] @ offs[seg[0]]), BAt(f).T @ U(rec[f][1] @ offs[seg[1]])) for f in range(M.nf)]
        derr = [(ang(dirs_mix[f][0], d_rec[f][0]), ang(dirs_mix[f][1], d_rec[f][1])) for f in range(M.nf)]
        if mode == 'ABS': print(f'  segment direction vs Mixamo (body coords), deg: upper arm {stats([d[0] for d in derr])}; forearm {stats([d[1] for d in derr])}')
        else: print(f'  (not an absolute match by design) direction difference to Mixamo, deg: upper arm mean {np.mean([d[0] for d in derr]):.1f} max {max(d[0] for d in derr):.1f}; forearm mean {np.mean([d[1] for d in derr]):.1f}')
        flex = lambda d: [180 - ang(-x[0], x[1]) for x in d]
        fm, fa = flex(dirs_mix), flex(d_rec)
        print(f'  elbow flexion (0 = straight) deg over the clip: Mixamo {min(fm):.1f}..{max(fm):.1f}, Agumon {min(fa):.1f}..{max(fa):.1f}; max |difference| {max(abs(a - b) for a, b in zip(fm, fa)):.2f}')
        sm = max(ang(d[0], dirs_mix[0][0]) for d in dirs_mix); sa = max(ang(d[0], d_rec[0][0]) for d in d_rec)
        wr = lambda WW, g: max(geodesic(g(WW[0]), g(WW[f])) * D for f in range(M.nf))
        print(f'  shoulder swing from frame 0, max deg: Mixamo {sm:.1f}, Agumon {sa:.1f}; wrist (hand vs forearm) change from frame 0, max deg: Mixamo {max(geodesic(Q[0][1].T @ Q[0][2], Q[f][1].T @ Q[f][2]) * D for f in range(M.nf)):.1f}, Agumon {max(geodesic(Wr[0][1].T @ Wr[0][2], Wr[f][1].T @ Wr[f][2]) * D for f in range(M.nf)):.1f}')
        step = lambda seq: max(geodesic(seq[f], seq[f + 1]) * D for f in range(len(seq) - 1))
        print('  largest frame-to-frame local rotation step, deg, Agumon 4/5/6: ' + '/'.join(f'{step([key_matrix(keys[k][f]) for f in range(M.nf)]):.2f}' for k in range(3)) +
              ' ; Mixamo LeftArm/ForeArm/Hand: ' + '/'.join(f'{step([unquat(M.rot[I[s]][f]) for f in range(M.nf)]):.2f}' for s, _ in chain_m))
        wa = [BAt(f).T @ (rec[f][0] @ offs[seg[0]] + rec[f][1] @ offs[seg[1]]) for f in range(M.nf)]
        print(f'  chain length: Agumon {la:.0f} units (4->5 {np.linalg.norm(offs[seg[0]]):.0f}, 5->6 {np.linalg.norm(offs[seg[1]]):.0f}); Mixamo {lm:.1f} cm (upper {lu:.1f}, fore {lf:.1f})')
        print(f'  wrist displacement from frame 0, max, % of chain length: Agumon {max(np.linalg.norm(w - wa[0]) for w in wa) / la * 100:.1f}, Mixamo {max(np.linalg.norm(w - wm[0]) for w in wm) / lm * 100:.1f}; '
              f'wrist position difference (each normalised by its chain), %: {stats([np.linalg.norm(wa[f] / la - wm[f] / lm) * 100 for f in range(M.nf)])}')
        print(f'  wrist at frame 0 in body coords (left, up, forward) as % of chain: Agumon {np.round(wa[0] / la * 100)}, Mixamo {np.round(wm[0] / lm * 100)}')
    # Agumon's own authored left arm over all motions (non-blend ticks)
    Wd = World(par, n); pl = Player(tr, mo); ud, el = [], []
    for m in range(1, len(mo) + 1):
        pl.set_motion(m)
        if pl.count <= 0: continue
        for t in range(1700):
            if not pl.blending:
                Wt = Wd.update(pl.pose); Bt = nrot(Wt[1]) @ offA
                d1 = Bt.T @ (nrot(Wt[seg[0]]) @ offs[seg[0]]); d2 = Bt.T @ (nrot(Wt[seg[1]]) @ offs[seg[1]])
                ud.append(ang(d1, BA.T @ dA[0])); el.append(180 - ang(-d1, d2))
            if pl.done or pl.keyframe >= pl.count - 1: break
            pl.advance(); pl.evaluate()
    print(f"\n## Agumon's own authored {side.lower()} arm (bones {seg}): {len(ud)} non-blend ticks over {len(mo)} motions")
    print(f'  elbow flexion (0 = straight), deg: {stats(el)}')
    print(f"  upper-arm direction angle from Agumon's idle direction (body coords), deg: {stats(ud)}")
    d1m = dirs_mix[0][0]
    print(f'  Agumon idle upper-arm direction (left, up, forward): {np.round(BA.T @ dA[0], 2)}; Mixamo Idle frame 0: {np.round(d1m, 2)}; angle between: {ang(BA.T @ dA[0], d1m):.1f} deg')
    out['_Rt'] = Rt
    return out


if __name__ == '__main__':
    a = sys.argv[1:]
    main(a[0] if a else os.path.join(HERE, '..', '..', '..', 'kb', 'mixamo', 'Soldier.glb'), a[1] if len(a) > 1 else os.path.join(HERE, '..', '..', 'data', 'files', '0499.bin'))
