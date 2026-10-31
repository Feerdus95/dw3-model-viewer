"""E1.7.1 (READ-ONLY, in memory): what would it take for the toe of the E1.7 leg variants to stay above the ground, WITHOUT changing the
transferred orientations? Uses the keys of e17_leg.main (ABS, ABSW, HYB), pelvis fixed at its reference like E1.7. Ground = idle level of the lowest toe.
Reports per variant: penetration, the per-frame root lift that would cancel it (a root/pelvis translation, orientations untouched),
and the foot length that would keep the toe on the ground for each frame's ankle height and foot pitch."""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, ".."))
from e7_compare import load_raw, Player, pose_track
from e8_resample import parents_of, World
from dw3_euler import key_matrix
from e13_arm import nrot, stats
import e17_leg, contextlib, io

SG = os.path.join(HERE, '..', '..', '..', 'kb', 'mixamo', 'Soldier.glb'); AB = os.path.join(HERE, '..', '..', 'data', 'files', '0499.bin')
with contextlib.redirect_stdout(io.StringIO()): res = e17_leg.main(SG, AB, 'Walk', True, 'Left')
tr, mo = load_raw(AB); par = parents_of(AB); n = len(tr); fr = Player(tr, mo).idle
ref = [[pose_track(tr[b][t], fr) for t in range(3)] for b in range(n)]
W = World(par, n).update(ref); Rp = nrot(W[10]); P10 = W[10][:3, 3]
off = {b: np.array(ref[b - 1][0], float) for b in (11, 12, 13, 14)}
ground = min(-W[14][1, 3], -W[18][1, 3]); ank0 = -W[13][1, 3] - ground; leg = np.linalg.norm(off[12]) + np.linalg.norm(off[13])
print(f'Agumon reference: idle ankle height {ank0:.0f}, toe {-W[14][1, 3] - ground:.0f} units over the ground; leg hip-ankle {leg:.0f}; foot bone {np.linalg.norm(off[14]):.0f}; pelvis held at its reference')
for v in ('ABS', 'ABSW', 'HYB'):
    keys = res[v]; nf = len(keys[0]); A, T, L = [], [], []
    for f in range(nf):
        r = [Rp @ key_matrix(keys[0][f])]
        for k in range(1, 4): r.append(r[-1] @ key_matrix(keys[k][f]))
        p11 = P10 + Rp @ off[11]; p12 = p11 + r[0] @ off[12]; p13 = p12 + r[1] @ off[13]; p14 = p13 + r[2] @ off[14]
        A.append(-p13[1] - ground); T.append(-p14[1] - ground)
    A, T = np.array(A), np.array(T); low = np.minimum(A, T)
    lift = np.maximum(0, -low)                       # root lift that brings the lowest of ankle/toe to the ground, per frame
    ankle_restore = np.maximum(0, ank0 - A)          # lift that restores the idle ankle height
    pitch = np.arcsin(np.clip((A - T) / np.linalg.norm(off[14]), -1, 1))   # + = toe down
    need_len = np.where((A > 0) & (np.sin(pitch) > 1e-3), A / np.maximum(np.sin(pitch), 1e-3), np.inf)
    print(f'\n## {v}: ankle {A.min():.0f}..{A.max():.0f}, toe {T.min():.0f}..{T.max():.0f}; frames toe below ground {int((T < 0).sum())} of {nf}')
    print(f'  root lift that would cancel the penetration: {stats(lift)} units (max {lift.max() / leg * 100:.0f} % of the leg); lift that would restore the idle ankle height: max {ankle_restore.max():.0f}, toe still below ground afterwards in {int(((T + ankle_restore) < 0).sum())} frames (deepest {(T + ankle_restore).min():.0f})')
    fin = need_len[np.isfinite(need_len)]
    print(f'  foot-bone length that would keep the toe on the ground (ankle height / sin(pitch)): {stats(fin) if len(fin) else "n/a"} vs actual {np.linalg.norm(off[14]):.0f}; frames where no length works (foot pitched up or ankle on the ground): {int((~np.isfinite(need_len)).sum())}')
    print(f'  vertical change of the lowest point over the cycle (lift needed varies by): {lift.max() - lift.min():.0f} units = {(lift.max() - lift.min()) / leg * 100:.0f} % of the leg (Mixamo hips translation range 6-9 cm = 7-10 % of its 88 cm leg)')
