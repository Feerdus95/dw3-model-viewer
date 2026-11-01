"""E4.2 check of the 0526 legs-only fixture (viewer/data/e14/0526-e48-legs.bin) against the original 0526.bin, read-only.
(1) only rotation tracks of bones 5-12 differ, only on keys with frame 10..40; (2) other motions untouched (key-level); (3) ground contact / penetration / slide / loop seam of the walk (Player idle, world positions of ankle and toe)."""
import os, sys, math, numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..'))
from e7_compare import load_raw, Player, pose_track
from e8_resample import parents_of, World
O = os.path.join(HERE, '..', '..', 'data', 'files', '0526.bin'); F = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', 'data', 'e14', '0526-e48-frozen.bin')
t0, m0 = load_raw(O); t1, m1 = load_raw(F); n = len(t0); legs = set(range(1, 22)) if 'frozen' in F else set(range(5, 13)); tt = (0, 1) if 'frozen' in F else (1,)
bad = []; ch = 0
for b in range(n):
    for t in range(3):
        if t0[b][t] == t1[b][t]: continue
        ok = b + 1 in legs and t in tt and all(k0 == k1 for k0, k1 in zip(t0[b][t], t1[b][t]) if not (10 <= k0[3] <= 40))
        o = {k[3]: k for k in t0[b][t]}; keep = [k for k in t1[b][t] if k[3] >= 30000 or k[3] < 10 or k[3] > 40]
        ok = b + 1 in legs and t in tt and keep == [k for k in t0[b][t] if k[3] >= 30000 or k[3] < 10 or k[3] > 40]
        ch += 1
        if not ok: bad.append((b + 1, t))
print(f'tracks that differ: {ch} (legs file 8 = rotation of bones 5-12; frozen file 34 = 8 leg rotations + position and rotation of the 13 frozen bones); outside the allowed set: {bad}')
print('motion table differences: ' + str([i for i in range(len(m0)) if m0[i] != m1[i]][:30]) + f' ({sum(a != b for a, b in zip(m0, m1))} motions; the ones running through the idle steps)')
print('idle steps now:', m1[0][:2])
# walk analysis
par = parents_of(F); tr, mo = t1, m1
lo, hi = mo[0][0][2], mo[0][0][3]; nf = hi - lo + 1
Wf = []
for f in range(lo, hi + 1):
    ref = [[pose_track(tr[b][t], f) for t in range(3)] for b in range(n)]
    Wf.append(World(par, n).update(ref))
Wo = World(par, n).update([[pose_track(t0[b][t], 10) for t in range(3)] for b in range(n)])
p = lambda W, b: W[b][:3, 3]
rep = lambda *a: print(*a)
for nm, ank, toe in (('left', 7, 8), ('right', 11, 12)):
    A = np.array([-p(W, ank)[1] for W in Wf]); T = np.array([-p(W, toe)[1] for W in Wf]); Ao = -p(Wo, ank)[1]; To = -p(Wo, toe)[1]
    z = np.array([p(W, toe)[2] for W in Wf]); x = np.array([p(W, toe)[0] for W in Wf])
    rep(f'{nm}: idle-reference ankle height {Ao:.0f}, toe {To:.0f}; walk ankle {A.min():.0f}..{A.max():.0f}, toe {T.min():.0f}..{T.max():.0f}')
    # ground = the lower of the two idle toes (as e17); penetration = below that
    G = min(-p(Wo, 8)[1], -p(Wo, 12)[1])
    low = T < G + 15; rep(f'  toe below ground (idle level {G:.0f}): {int((T < G).sum())} of {nf} frames, deepest {min(T.min() - G, 0):.0f} u; frames with toe within 15 u of the ground (contact) {int(low.sum())}')
    # slide: toe z speed during contact frames (pelvis static -> ground-fixed foot should move 0 per frame in the world)
    dz = np.abs(np.diff(z)); c = [i for i in range(nf - 1) if low[i] and low[i + 1]]
    rep(f'  toe travel per frame in contact frames: mean {np.mean(dz[c]) if c else float("nan"):.1f} u, max {np.max(dz[c]) if c else float("nan"):.1f} u (0 = no slide); total fore-aft range {np.ptp(z):.0f} u, lateral range {np.ptp(x):.0f} u')
    rep(f'  loop seam last->first: ankle dz {z[-1] - z[0]:.1f} (toe), height diff ankle {A[-1] - A[0]:.1f}, toe {T[-1] - T[0]:.1f}; largest per-frame toe step {np.max(np.linalg.norm(np.diff([p(W, toe) for W in Wf], axis=0), axis=1)):.0f} u')
# knee check: no hyperextension: angle between thigh and shank directions
for nm, seg in (('left', [5, 6, 7]), ('right', [9, 10, 11])):
    ang = []
    for W in Wf:
        d1 = p(W, seg[1]) - p(W, seg[0]); d2 = p(W, seg[2]) - p(W, seg[1])
        ang.append(math.degrees(math.acos(np.dot(d1, d2) / np.linalg.norm(d1) / np.linalg.norm(d2))))
    d1 = p(Wo, seg[1]) - p(Wo, seg[0]); d2 = p(Wo, seg[2]) - p(Wo, seg[1]); a0 = math.degrees(math.acos(np.dot(d1, d2) / np.linalg.norm(d1) / np.linalg.norm(d2)))
    rep(f'{nm} knee bend (thigh-shank angle) idle ref {a0:.1f}, walk {min(ang):.1f}..{max(ang):.1f}')
