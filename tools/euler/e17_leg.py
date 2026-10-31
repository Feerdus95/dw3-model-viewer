"""E1.7 (READ-ONLY, in memory): ONE leg, Mixamo Soldier Walk LeftUpLeg,LeftLeg,LeftFoot,LeftToeBase -> Agumon bones 11,12,13,14.
Run: python e17_leg.py [SOLDIER.glb] [AGUMON.bin] [ANIM] ; main() returns the ABS keys. Same ABS/DELTA method as e13_arm.py with:
 - the carrier of the body frame is the PELVIS (Mixamo Hips / Agumon bone 10), held at its reference rotation in Agumon (pelvis, torso, root, other leg untouched);
 - 4 segments; ToeBase and Agumon 14 have no child: their direction is the previous segment's (assumed straight toes at the reference);
 - the frame's second reference axis is body FORWARD for thigh/shank and body UP for foot/toe (their bone direction is almost horizontal);
 - extra metrics: ankle/toe trajectories in body coordinates and heights (world up), compared with Mixamo's own, ground penetration w.r.t. the idle level."""
import math, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, ".."))
from e7_compare import load_raw, Player, pose_track
from e8_resample import parents_of, World
from euler_zyx import geodesic, unquat
from dw3_euler import key_matrix, rotations_to_keys
from e13_arm import Mix, U, ang, frame_from, stats, nrot, D

NAMES = ['UpLeg', 'Leg', 'Foot', 'ToeBase']


def mix_pos(M, name, f, side='Left'):
    """world position of a Mixamo joint with animated Hips translation (units of the node tree: x0.01 from the Character node)"""
    i = M.idx[name]; tr = M.tra
    p = tr[i][f] if i in tr and f is not None else M.off(i)
    while i in M.par:
        j = M.par[i]; pj = tr[j][f] if j in tr and f is not None else M.off(j)
        p = M.Rl(j, f) @ (np.array(M.N[j].get('scale', [1, 1, 1]), float) * p) + pj; i = j
    return p


def main(sg, ab, anim='Walk', dedup=True, side='Left', carrier=False):
    M = Mix(sg, anim); I = M.idx; M.tra = {}
    import e13_arm; from e7_compare import read_glb, accessor
    js, b = read_glb(sg); a = next(x for x in js['animations'] if x['name'] == anim)
    for c in a['channels']:
        if c['target']['path'] == 'translation': M.tra[c['target']['node']] = accessor(js, b, a['samplers'][c['sampler']]['output']).astype(float)
    if dedup and all(geodesic(unquat(q[-1]), unquat(q[0])) * D < 0.05 for q in M.rot.values()): M.nf -= 1
    seg = [11, 12, 13, 14] if side == 'Left' else [15, 16, 17, 18]
    ms = [side + n for n in NAMES]; pelvis = I['Hips']
    P = lambda n: mix_pos(M, n, None)
    left = U(P('LeftUpLeg') - P('RightUpLeg')); up0 = P('Neck') - P('Hips'); up = U(up0 - np.dot(up0, left) * left); fwd = np.cross(left, up)
    BM = np.column_stack([left, up, fwd]); offM = M.Rw(pelvis).T @ BM
    refv = lambda k, u, fw: fw if k < 2 else u
    dM = []
    for k, n in enumerate(ms):
        dM.append(M.Rw(I[n]) @ U(M.off(I[ms[k + 1]])) if k < 3 else dM[-1])
    CM = [M.Rw(I[n]).T @ frame_from(dM[k], refv(k, up, fwd)) for k, n in enumerate(ms)]
    tr, mo = load_raw(ab); par = parents_of(ab); n = len(tr); fr = Player(tr, mo).idle
    ref = [[pose_track(tr[b][t], fr) for t in range(3)] for b in range(n)]
    assert all(ref[b - 1][2] == (4096, 4096, 4096) for b in [10] + seg), 'non-unit scale on the leg chain'
    W = World(par, n).update(ref); rot = lambda b: nrot(W[b]); pos = lambda b: W[b][:3, 3]
    aleft = U(pos(11) - pos(15)); aup0 = pos(2) - pos(10); aup = U(aup0 - np.dot(aup0, aleft) * aleft); afwd = np.cross(aleft, aup)
    BA = np.column_stack([aleft, aup, afwd]); assert abs(np.linalg.det(BA) - 1) < 1e-6
    off = {b: np.array(ref[b - 1][0], float) for b in seg}            # each leg bone's position in its parent's frame (11: in the pelvis)
    dA = [U(rot(seg[k]) @ off[seg[k + 1]]) if k < 3 else None for k in range(4)]; dA[3] = dA[2]
    CA = [rot(b).T @ frame_from(dA[k], refv(k, aup, afwd)) for k, b in enumerate(seg)]
    print(f'Agumon body frame: left={np.round(aleft, 2)} up={np.round(aup, 2)} fwd={np.round(afwd, 2)}; Mixamo: left={np.round(left, 2)} up={np.round(up, 2)} fwd={np.round(fwd, 2)}; clip {M.nf} frames at {M.fps:.1f} fps')
    print('Agumon reference offsets (bone: position in parent frame, length): ' + '; '.join(f'{b}: {np.round(off[b])} {np.linalg.norm(off[b]):.0f}' for b in seg))
    Q = [[(M.Rw(pelvis, f) @ offM).T @ M.Rw(I[nm], f) @ CM[k] for k, nm in enumerate(ms)] for f in range(M.nf)]
    QMr = [BM.T @ M.Rw(I[nm]) @ CM[k] for k, nm in enumerate(ms)]; QAr = [BA.T @ rot(b) @ CA[k] for k, b in enumerate(seg)]
    for k, nm in enumerate(ms):
        print(f'reference frames in body coords, Mixamo T-pose vs Agumon idle, {nm}->{seg[k]}: frames differ by {geodesic(QMr[k], QAr[k]) * D:.1f} deg (bone direction by {ang(QMr[k][:, 1], QAr[k][:, 1]):.1f} deg)')
    Rp = rot(10); out = {}
    Qc = [BM.T @ M.Rw(pelvis, f) @ offM for f in range(M.nf)]      # carrier (pelvis) orientation in body coords; used only when carrier=True and mode ABS (E1.9)
    legA = np.linalg.norm(off[seg[1]]) + np.linalg.norm(off[seg[2]]); legM = np.linalg.norm(M.off(I[ms[1]])) + np.linalg.norm(M.off(I[ms[2]]))
    # Mixamo own trajectories (body coords relative to the hip joint, and world heights in cm, minimum of the clip as ground)
    def mixtraj(nm):
        out_, h = [], []
        for f in range(M.nf):
            Bt = M.Rw(pelvis, f) @ offM; hip = mix_pos(M, ms[0], f) * 100; p = mix_pos(M, nm, f) * 100
            out_.append(Bt.T @ (p - hip)); h.append(p[1])
        return np.array(out_), np.array(h)
    mt = {k: mixtraj(ms[k]) for k in (2, 3)}
    # gait check of the source clip
    ank, hk = mt[2]; toe, ht = mt[3]
    hmin = min(hk.min(), ht.min()); lo = hmin + 0.15 * legM
    stance = [(hk[f] < lo or ht[f] < lo) for f in range(M.nf)]
    rf = lambda nm: [mix_pos(M, nm, f)[1] * 100 for f in range(M.nf)]
    other = np.array(rf('RightFoot')); ph = int(np.argmin(hk)) - int(np.argmin(other))
    print(f'\n## Mixamo Walk gait check: left ankle height {hk.min() - hmin:.1f}..{hk.max() - hmin:.1f} cm, toe {ht.min() - hmin:.1f}..{ht.max() - hmin:.1f} cm (leg length hip-ankle {legM:.1f} cm); frames with the foot within 15 % of the leg length of the ground: {sum(stance)} of {M.nf}; '
          f'forward travel of the ankle relative to the hip over the clip {np.ptp(ank[:, 2]) / legM * 100:.0f} % of leg length (body forward axis); phase offset between left and right ankle minima {ph % M.nf} of {M.nf} frames; hips translation range {np.round([np.ptp(M.tra[pelvis][:, c]) for c in range(3)], 3)}')
    # variants: ABS = body frame leaning like Agumon's pelvis-neck line; ABSW = gravity-aligned body frame (up = world up); HYB = ABSW for thigh/shank,
    # foot and toe as the CHANGE from the flat-foot reference (T-pose / idle both stand flat); DELTA = every segment as a change from the reference
    BW = np.column_stack([aleft, [0, -1.0, 0], np.cross(aleft, [0, -1.0, 0])])
    VAR = {'ABS': (BA, aup, afwd), 'ABSW': (BW, BW[:, 1], BW[:, 2]), 'HYB': (BW, BW[:, 1], BW[:, 2]), 'DELTA': (BA, aup, afwd), 'KNEE': (BA, aup, afwd), 'KNEE2': (BA, aup, afwd), 'FLAT': (BA, aup, afwd)}
    for mode in ('ABS', 'ABSW', 'HYB', 'DELTA', 'KNEE', 'KNEE2', 'FLAT'):
        BAx, upx, fwx = VAR[mode]; car = carrier and mode == 'ABS'
        Bf = (lambda f: BAx @ Qc[f]) if car else (lambda f: BAx)
        Rpt = [Bf(f) @ (rot(10).T @ BAx).T if car else Rp for f in range(M.nf)]
        CAx = [rot(b).T @ frame_from(dA[k], refv(k, upx, fwx)) for k, b in enumerate(seg)]; QArx = [BAx.T @ rot(b) @ CAx[k] for k, b in enumerate(seg)]
        useabs = lambda k: mode in ('ABS', 'ABSW', 'KNEE', 'KNEE2', 'FLAT') or (mode == 'HYB' and k < 2)
        Wr = []
        if mode == 'KNEE':      # E2.6: thigh/foot/toe ABS; knee = Agumon's idle knee rotation plus the CHANGE of Soldier's thigh->shank rotation from its own clip mean (canonical segment frames)
            Lm = [Q[f][0].T @ Q[f][1] for f in range(M.nf)]; u_, _, vt_ = np.linalg.svd(sum(Lm)); Lmm = u_ @ vt_; LAref = QArx[0].T @ QArx[1]
            if os.environ.get('E27_AXES'):
                def rv(R_):
                    c = (np.trace(R_) - 1) / 2; t = math.acos(max(-1, min(1, c)))
                    w = np.array([R_[2, 1] - R_[1, 2], R_[0, 2] - R_[2, 0], R_[1, 0] - R_[0, 1]])
                    return np.zeros(3) if t < 1e-9 else (w / np.linalg.norm(w) * t if t < 3.1 else w * 0)
                def ax(R_): v = rv(R_); return np.round(v / (np.linalg.norm(v) + 1e-12), 2), round(float(np.linalg.norm(v)) * D, 1)
                rvs = np.array([rv(Lmm.T @ L) for L in Lm]); u2, s2, v2 = np.linalg.svd(rvs, full_matrices=False)
                print(f'  [E27 {side}] Agumon ref knee (thigh->shank, canonical frames): axis/angle {ax(LAref)}; Soldier mean knee {ax(Lmm)}; Soldier change-from-mean: principal axis {np.round(v2[0], 2)} (sv {np.round(s2, 2)}), range along it {np.ptp(rvs @ v2[0]) * D:.1f} deg')
                print(f'  [E27 {side}] Soldier knee angle (thigh->shank) over the clip {min(np.linalg.norm(rv(L)) for L in Lm) * D:.1f}..{max(np.linalg.norm(rv(L)) for L in Lm) * D:.1f} deg; hinge axis in Agumon ref knee frame vs Soldier principal axis: angle {np.degrees(np.arccos(abs(np.dot(ax(LAref)[0], v2[0])))):.1f} deg')
        if mode in ('KNEE2', 'FLAT'):     # E2.7: hinge-only knee. Agumon's own reference knee (bend plane and twist kept) + the CHANGE of Soldier's bend angle (angle between thigh and shank bone directions) about Agumon's own hinge axis
            LAref = QArx[0].T @ QArx[1]; ey = np.array([0, 1.0, 0]); bM = np.array([math.acos(max(-1, min(1, (Q[f][0].T @ Q[f][1])[1, 1]))) for f in range(M.nf)]); dth = bM - bM.mean()
            hA = np.cross(ey, LAref[:, 1]); hA /= np.linalg.norm(hA)
            def rod(h, t): K = np.array([[0, -h[2], h[1]], [h[2], 0, -h[0]], [-h[1], h[0], 0]]); return np.eye(3) + math.sin(t) * K + (1 - math.cos(t)) * K @ K
            print(f'  [E27 {side}] KNEE2: Agumon ref bend {math.degrees(math.acos(LAref[1, 1])):.1f} deg about hinge {np.round(hA, 2)}; Soldier bend {np.degrees(bM.min()):.1f}..{np.degrees(bM.max()):.1f} (mean {np.degrees(bM.mean()):.1f}), change {np.degrees(dth.min()):.1f}..{np.degrees(dth.max()):.1f}')
        for f in range(M.nf):
            Fs = [Bf(f) @ (Q[f][k] if useabs(k) else Q[f][k] @ QMr[k].T @ QArx[k]) for k in range(4)]
            if mode == 'KNEE': Fs[1] = Bf(f) @ (Q[f][0] @ LAref @ Lmm.T @ Lm[f])
            if mode in ('KNEE2', 'FLAT'): Fs[1] = Bf(f) @ (Q[f][0] @ rod(hA, dth[f]) @ LAref)
            Wr.append([Fs[k] @ CAx[k].T for k in range(4)])
            if mode == 'FLAT':      # E2.8: foot = Soldier's change from its flat T-pose, applied to a FLAT Agumon foot (world identity, as the left idle foot; the right idle one is a folded stride stance); toe straight (local identity, as bone 14)
                Wr[-1][2] = BW @ Q[f][2] @ QMr[2].T @ BW.T; Wr[-1][3] = Wr[-1][2]
        if mode in ('ABS', 'KNEE2', 'FLAT') and os.environ.get('E28_CLOSE'):
            print(f'  [E28 {side} {mode}] WORLD closure last->first deg thigh/shank/foot: ' + '/'.join(f'{geodesic(Wr[-1][k], Wr[0][k]) * D:.2f}' for k in range(3)) + '; world foot step per frame max/median ' + f'{max(geodesic(Wr[f + 1][2], Wr[f][2]) * D for f in range(M.nf - 1)):.1f}/{np.median([geodesic(Wr[f + 1][2], Wr[f][2]) * D for f in range(M.nf - 1)]):.1f}' + f'; foot steps near the seam (f27-28,28-29,29-30,[30->0]) '+'/'.join(f'{geodesic(Wr[(f + 1) % M.nf][2], Wr[f][2]) * D:.1f}' for f in range(M.nf - 4, M.nf))+f' (nf={M.nf}); Soldier knee bend last vs first {math.degrees(abs(bM[-1] - bM[0])) if mode != "ABS" else 0:.2f}' if mode != 'ABS' else '')
        loc = [[Rpt[f].T @ w[0]] + [w[k - 1].T @ w[k] for k in range(1, 4)] for f, w in enumerate(Wr)]
        try: keys = [rotations_to_keys([loc[f][k] for f in range(M.nf)], tuple(ref[seg[k] - 1][1])) for k in range(4)]
        except Exception as ex: print(mode, 'converter refused:', str(ex)[:100]); continue
        out[mode] = keys
        if mode == 'ABS': out['_Rt'] = Rpt
        rec = []
        for f in range(M.nf):
            r = [Rpt[f] @ key_matrix(keys[0][f])]
            for k in range(1, 4): r.append(r[-1] @ key_matrix(keys[k][f]))
            rec.append(r)
        print(f'\n## {mode}: converter keys {sum(len(k) for k in keys)}; world rotation quantisation error deg {stats([geodesic(Wr[f][k], rec[f][k]) * D for f in range(M.nf) for k in range(4)])}')
        mx = max(abs(int(keys[k][f + 1][c]) - int(keys[k][f][c])) for k in range(4) for f in range(M.nf - 1) for c in range(3))
        print(f'  largest key step {mx} units ({mx / 4096 * 360:.1f} deg); loop closure last->first local rotation deg (bones {seg}): ' + '/'.join(f'{geodesic(key_matrix(keys[k][-1]), key_matrix(keys[k][0])) * D:.2f}' for k in range(4)) +
              ' ; Mixamo ' + '/'.join(f'{geodesic(unquat(M.rot[I[nm]][M.nf - 1]), unquat(M.rot[I[nm]][0])) * D:.2f}' for nm in ms))
        step = lambda seq: max(geodesic(seq[f], seq[f + 1]) * D for f in range(len(seq) - 1))
        print('  largest frame step deg, Agumon: ' + '/'.join(f'{step([key_matrix(keys[k][f]) for f in range(M.nf)]):.2f}' for k in range(4)) + ' ; Mixamo: ' + '/'.join(f'{step([unquat(M.rot[I[nm]][f]) for f in range(M.nf)]):.2f}' for nm in ms))
        # positions (Agumon, world): pelvis static at its reference
        P10 = pos(10); Pw = []
        for f in range(M.nf):
            p11 = P10 + Rpt[f] @ off[seg[0]]; p12 = p11 + rec[f][0] @ off[seg[1]]; p13 = p12 + rec[f][1] @ off[seg[2]]; p14 = p13 + rec[f][2] @ off[seg[3]]
            Pw.append((p11, p12, p13, p14))
        dmix = [(Bt.T @ M.Rw(I[ms[0]], f) @ U(M.off(I[ms[1]])), Bt.T @ M.Rw(I[ms[1]], f) @ U(M.off(I[ms[2]])), Bt.T @ M.Rw(I[ms[2]], f) @ U(M.off(I[ms[3]]))) for f in range(M.nf) for Bt in [M.Rw(pelvis, f) @ offM]]
        drec = [tuple(Bf(f).T @ U(rec[f][k] @ off[seg[k + 1]]) for k in range(3)) for f in range(M.nf)]
        derr = np.array([[ang(dmix[f][k], drec[f][k]) for k in range(3)] for f in range(M.nf)])
        if mode in ('ABS', 'ABSW', 'HYB'): print(f'  segment direction vs Mixamo (body coords; thigh/shank are the ones imposed in HYB), deg: thigh max {derr[:, 0].max():.2f}, shank max {derr[:, 1].max():.2f}, foot max {derr[:, 2].max():.2f}')
        kn = lambda d: [180 - ang(-x[0], x[1]) for x in d]; fm, fa = kn(dmix), kn(drec)
        print(f'  knee flexion deg: Mixamo {min(fm):.1f}..{max(fm):.1f}, Agumon {min(fa):.1f}..{max(fa):.1f} (max diff {max(abs(x - y) for x, y in zip(fm, fa)):.2f}); hip swing from frame 0 max: Mixamo {max(ang(d[0], dmix[0][0]) for d in dmix):.1f}, Agumon {max(ang(d[0], drec[0][0]) for d in drec):.1f}')
        # ankle flexion: angle between shank and foot directions
        af = lambda d: [ang(x[1], x[2]) for x in d]; am, aa = af(dmix), af(drec)
        print(f'  ankle (shank-foot direction angle) deg: Mixamo {min(am):.1f}..{max(am):.1f}, Agumon {min(aa):.1f}..{max(aa):.1f}')
        # foot pitch w.r.t. world horizontal (Agumon world up = -y; Mixamo world up = +y); stance frames of Mixamo
        pitch_a = [math.degrees(math.asin(max(-1, min(1, float(-(Pw[f][3][1] - Pw[f][2][1]) / np.linalg.norm(Pw[f][3] - Pw[f][2])))))) for f in range(M.nf)]
        pitch_m = []
        for f in range(M.nf):
            v = mix_pos(M, ms[3], f) - mix_pos(M, ms[2], f); pitch_m.append(math.degrees(math.asin(float(v[1] / np.linalg.norm(v)))))
        st = [f for f in range(M.nf) if stance[f]]
        print(f'  foot pitch (ankle->toe vs horizontal, + = toe up) deg: Mixamo {min(pitch_m):.1f}..{max(pitch_m):.1f}, Agumon {min(pitch_a):.1f}..{max(pitch_a):.1f}; during the Mixamo stance frames {len(st)}: Mixamo {np.mean([pitch_m[f] for f in st]):.1f} (sd {np.std([pitch_m[f] for f in st]):.1f}), Agumon {np.mean([pitch_a[f] for f in st]):.1f} (sd {np.std([pitch_a[f] for f in st]):.1f})')
        # trajectories in body coords normalised by leg length
        ba = lambda k: np.array([Bf(f).T @ (Pw[f][k] - Pw[f][0]) for f in range(M.nf)]) / legA
        for k, nm in ((2, 'ankle'), (3, 'toe')):
            ta = ba(k); tm = mt[k][0] / legM
            print(f'  {nm} relative to the hip, body coords (left, up, fwd), range as % of leg length: Agumon {np.round(np.ptp(ta, axis=0) * 100)}, Mixamo {np.round(np.ptp(tm, axis=0) * 100)}; mean position Agumon {np.round(ta.mean(0) * 100)} Mixamo {np.round(tm.mean(0) * 100)}; difference per frame mean {np.linalg.norm(ta - tm, axis=1).mean() * 100:.1f} % max {np.linalg.norm(ta - tm, axis=1).max() * 100:.1f} %')
        # heights (Agumon: -y), ground = idle level of the lowest of the two feet's toes/ankles at the reference
        h = lambda k: np.array([-Pw[f][k][1] for f in range(M.nf)])
        ground = min(-pos(14)[1], -pos(18)[1]); ha, ht_ = h(2) - ground, h(3) - ground
        print(f'  Agumon heights over the idle ground level (units; idle ankle {-pos(seg[2])[1] - ground:.0f}, toe {-pos(seg[3])[1] - ground:.0f}): ankle {ha.min():.0f}..{ha.max():.0f}, toe {ht_.min():.0f}..{ht_.max():.0f}; frames below the ground level: ankle {int((ha < 0).sum())}, toe {int((ht_ < 0).sum())}; deepest {min(ha.min(), ht_.min()):.0f} units ({min(ha.min(), ht_.min()) / legA * 100:.0f} % of the leg)')
        print(f'  Mixamo heights above the lowest point of the clip (cm): ankle {hk.min() - hmin:.1f}..{hk.max() - hmin:.1f}, toe {ht.min() - hmin:.1f}..{ht.max() - hmin:.1f}; as % of leg length ankle {(hk.max() - hk.min()) / legM * 100:.0f}, toe {(ht.max() - ht.min()) / legM * 100:.0f}; Agumon ankle range {(ha.max() - ha.min()) / legA * 100:.0f} %, toe {(ht_.max() - ht_.min()) / legA * 100:.0f} %')
        print(f'  leg length hip-knee-ankle: Agumon {legA:.0f} units (thigh {np.linalg.norm(off[seg[1]]):.0f}, shank {np.linalg.norm(off[seg[2]]):.0f}, foot {np.linalg.norm(off[seg[3]]):.0f}); Mixamo {legM:.1f} cm; ratio thigh/shank Agumon {np.linalg.norm(off[seg[1]]) / np.linalg.norm(off[seg[2]]):.2f}, Mixamo {np.linalg.norm(M.off(I[ms[1]])) / np.linalg.norm(M.off(I[ms[2]])):.2f}')
    return out


if __name__ == '__main__':
    a = sys.argv[1:]
    main(a[0] if a else os.path.join(HERE, '..', '..', '..', 'kb', 'mixamo', 'Soldier.glb'), a[1] if len(a) > 1 else os.path.join(HERE, '..', '..', 'data', 'files', '0499.bin'), a[2] if len(a) > 2 else 'Walk', True, a[3] if len(a) > 3 else 'Left')
