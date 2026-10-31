"""E1.9 (READ-ONLY, in memory; fixtures through e14_fixture.py): pelvis and torso rotations, Mixamo Soldier Walk Hips / Spine2 -> Agumon bones 10 / 1.
ABS method: the carrier's orientation in body coords, Qc(t) = BM^T Rw(carrier,t) offM, is applied to Agumon's static body frame: R(t) = BA Qc(t) BA^T R_ref (R_ref = world rotation at the idle).
Bones 1 and 10 are siblings under the control block 0 (identity rotation), so local = world; neither is applied as parent of the other.
Limbs: e13_arm.main / e17_leg.main with carrier=True use the same B(t) = BA Qc(t) as the moving body frame (their Rt output). body_keys() returns the two bones' keys.
Run: python e20_body.py [SOLDIER.glb] [AGUMON.bin]"""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, ".."))
from e7_compare import load_raw, Player, pose_track
from euler_zyx import geodesic, unquat
from dw3_euler import key_matrix, rotations_to_keys
from e13_arm import stats, D, Mix, ang
from e13_arm import main as arm_main
from e17_leg import main as leg_main
import contextlib, io


def quiet(fn, *a, **k):
    with contextlib.redirect_stdout(io.StringIO()): return fn(*a, **k)


def body_keys(sg, ab, anim='Walk', dedup=True, which='torso', variant='ABS'):
    """keys [[(x,y,z)] per frame] of bone 1 (torso) or 10 (pelvis) -> returned as [keys] (one bone)"""
    if which == 'torso': Rt = quiet(arm_main, sg, ab, anim, dedup, 'Left', True)['_Rt']; b = 1
    else: Rt = quiet(leg_main, sg, ab, anim, dedup, 'Left', True)['_Rt']; b = 10
    tr, mo = load_raw(ab); fr = Player(tr, mo).idle
    rt = pose_track(tr[b - 1][1], fr)
    if variant == 'REL':      # E2.0: the change from the clip's own mean posture applied to Agumon's idle orientation (the carrier's Qc, mean taken in the SVD / chordal sense)
        R0 = key_matrix(tuple(rt)); D_ = [r @ R0.T for r in Rt]; u, _, vt = np.linalg.svd(sum(D_)); Dm = u @ vt
        Rt = [d @ Dm.T @ R0 for d in D_]
    return [rotations_to_keys(list(Rt), tuple(rt))], Rt


def main(sg, ab, anim='Walk', dedup=True):
    tr, mo = load_raw(ab); fr = Player(tr, mo).idle
    M = Mix(sg, anim); I = M.idx
    if dedup and all(geodesic(unquat(q[-1]), unquat(q[0])) * D < 0.05 for q in M.rot.values()): M.nf -= 1
    for which, b, car in (('pelvis', 10, 'Hips'), ('torso', 1, 'Spine2')):
        keys, Rt = body_keys(sg, ab, anim, dedup, which)
        k = keys[0]; ref = pose_track(tr[b - 1][1], fr); R0 = key_matrix(tuple(ref))
        rec = [key_matrix(k[f]) for f in range(M.nf)]
        print(f'\n## {which} (Mixamo {car} -> Agumon bone {b}): {M.nf} frames; keys {len(k)}')
        print(f'  quantisation error of the world rotation, deg: {stats([geodesic(Rt[f], rec[f]) * D for f in range(M.nf)])}')
        print(f'  rotation away from the reference: Agumon max {max(geodesic(Rt[f], R0) * D for f in range(M.nf)):.1f} deg; Mixamo (from frame 0) max {max(geodesic(M.Rw(I[car], f), M.Rw(I[car], 0)) * D for f in range(M.nf)):.1f} deg')
        mx = max(abs(int(k[f + 1][c]) - int(k[f][c])) for f in range(M.nf - 1) for c in range(3))
        print(f'  largest key step {mx} units ({mx / 4096 * 360:.1f} deg); loop closure last->first {geodesic(rec[-1], rec[0]) * D:.2f} deg (Mixamo {geodesic(M.Rw(I[car], M.nf - 1), M.Rw(I[car], 0)) * D:.2f}); largest frame step {max(geodesic(rec[f], rec[f + 1]) * D for f in range(M.nf - 1)):.2f} deg (Mixamo {max(geodesic(M.Rw(I[car], f), M.Rw(I[car], f + 1)) * D for f in range(M.nf - 1)):.2f})')
        # rotation relative to the idle reference as a rotation vector in world axes of Agumon (x left, y down, z back): range per axis, deg
        rv = []
        for f in range(M.nf):
            d = Rt[f] @ R0.T; a = np.arccos(np.clip((np.trace(d) - 1) / 2, -1, 1)); v = np.array([d[2, 1] - d[1, 2], d[0, 2] - d[2, 0], d[1, 0] - d[0, 1]])
            rv.append(np.degrees(a) * v / (np.linalg.norm(v) + 1e-12))
        rv = np.array(rv); print(f'  Agumon rotation vector from the reference, range per world axis (x,y,z) deg: {np.round(np.ptp(rv, axis=0), 1)}')
    # same body motion seen through the Mixamo hierarchy: torso relative to pelvis (Spine2 vs Hips) bend, deg
    sp = [geodesic(M.Rw(I['Hips'], f).T @ M.Rw(I['Spine2'], f), M.Rw(I['Hips']).T @ M.Rw(I['Spine2'])) * D for f in range(M.nf)]
    _, Rp = body_keys(sg, ab, anim, dedup, 'pelvis'); _, Rc = body_keys(sg, ab, anim, dedup, 'torso')
    sa = [geodesic(Rp[f].T @ Rc[f], Rp[0].T @ Rc[0]) * D for f in range(M.nf)]
    print(f'\n## torso relative to pelvis (change from frame 0) deg: Mixamo max {max(geodesic(M.Rw(I["Hips"], f).T @ M.Rw(I["Spine2"], f), M.Rw(I["Hips"], 0).T @ M.Rw(I["Spine2"], 0)) * D for f in range(M.nf)):.1f}; Agumon max {max(sa):.1f}')
    for nm, fn, sd in (('left arm', arm_main, 'Left'), ('right arm', arm_main, 'Right'), ('left leg', leg_main, 'Left'), ('right leg', leg_main, 'Right')):
        for car in (False, True):
            o = quiet(fn, sg, ab, anim, dedup, sd, car)
            ks = o['ABS']; print(f'  {nm} ABS carrier={car}: keys {sum(len(x) for x in ks)}; largest key step {max(abs(int(x[f + 1][c]) - int(x[f][c])) for x in ks for f in range(M.nf - 1) for c in range(3))}')


if __name__ == '__main__':
    a = sys.argv[1:]
    main(a[0] if a else os.path.join(HERE, '..', '..', '..', 'kb', 'mixamo', 'Soldier.glb'), a[1] if len(a) > 1 else os.path.join(HERE, '..', '..', 'data', 'files', '0499.bin'))
