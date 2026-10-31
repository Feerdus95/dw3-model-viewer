"""E2.0 (READ-ONLY): torso/pelvis reference variants. A = pelvis ABS + torso ABS (E1.9); B = pelvis ABS + torso REL; C = pelvis REL + torso REL.
REL = change from the clip's mean posture (chordal mean of the carrier's world delta) applied to Agumon's idle orientation. Run: python e21_trunk.py"""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, ".."))
from e7_compare import load_raw, Player, pose_track
from e8_resample import parents_of, World
from euler_zyx import geodesic
from dw3_euler import key_matrix
from e13_arm import stats, D, U, nrot, ang
from e20_body import body_keys
SG = os.path.join(HERE, '..', '..', '..', 'kb', 'mixamo', 'Soldier.glb'); AB = os.path.join(HERE, '..', '..', 'data', 'files', '0499.bin')
tr, mo = load_raw(AB); par = parents_of(AB); n = len(tr); fr = Player(tr, mo).idle
ref = [[pose_track(tr[b][t], fr) for t in range(3)] for b in range(n)]; W = World(par, n).update(ref); pos = lambda b: W[b][:3, 3]
aleft = U(pos(11) - pos(15)); aup0 = pos(2) - pos(10); aup = U(aup0 - np.dot(aup0, aleft) * aleft); afwd = np.cross(aleft, aup); BA = np.column_stack([aleft, aup, afwd])
R10, R1 = [key_matrix(tuple(ref[b - 1][1])) for b in (10, 1)]
UP = np.array([0, -1.0, 0])      # Agumon world up (y is down)
def lean(R, R0):     # body-up carried by R (R0 = reference): tilt from vertical and forward pitch (+ = leaning forward), deg
    u = (R @ R0.T) @ aup; return ang(u, UP), np.degrees(np.arcsin(np.clip(-u @ afwd, -1, 1)))
print(f'Agumon idle: pelvis tilt/pitch {lean(R10, R10)[0]:.1f}/{lean(R10, R10)[1]:.1f} deg (body frame lean of the reference); torso relative to pelvis at the idle: {geodesic(R10, R1) * D:.1f} deg')
V = {'A': ('ABS', 'ABS'), 'B': ('ABS', 'REL'), 'C': ('REL', 'REL')}
for nm, (vp, vt) in V.items():
    _, Rp = body_keys(SG, AB, 'Walk', True, 'pelvis', vp); _, Rc = body_keys(SG, AB, 'Walk', True, 'torso', vt); nf = len(Rp)
    lp = np.array([lean(r, R10) for r in Rp]); lt = np.array([lean(r, R1) for r in Rc])
    rel = [geodesic(Rp[f].T @ Rc[f], R10.T @ R1) * D for f in range(nf)]
    print(f'\n## {nm}: pelvis {vp}, torso {vt}')
    print(f'  pelvis away from the idle orientation: mean {np.mean([geodesic(r, R10) * D for r in Rp]):.1f} max {max(geodesic(r, R10) * D for r in Rp):.1f} deg; torso: mean {np.mean([geodesic(r, R1) * D for r in Rc]):.1f} max {max(geodesic(r, R1) * D for r in Rc):.1f} deg')
    print(f'  pelvis tilt {lp[:, 0].min():.1f}..{lp[:, 0].max():.1f} deg, pitch {lp[:, 1].min():.1f}..{lp[:, 1].max():.1f} (idle {lean(R10, R10)[1]:.1f}); torso tilt {lt[:, 0].min():.1f}..{lt[:, 0].max():.1f}, pitch {lt[:, 1].min():.1f}..{lt[:, 1].max():.1f}')
    print(f'  torso-vs-pelvis angle change from the idle value: mean {np.mean(rel):.1f}, max {max(rel):.1f} deg; torso-pelvis relative orientation range over the clip {max(geodesic(Rp[f].T @ Rc[f], Rp[0].T @ Rc[0]) * D for f in range(nf)):.1f} deg')
    print(f'  loop closure last->first: pelvis {geodesic(Rp[-1], Rp[0]) * D:.2f}, torso {geodesic(Rc[-1], Rc[0]) * D:.2f} deg; largest frame step: pelvis {max(geodesic(Rp[f], Rp[f + 1]) * D for f in range(nf - 1)):.2f}, torso {max(geodesic(Rc[f], Rc[f + 1]) * D for f in range(nf - 1)):.2f} deg')
