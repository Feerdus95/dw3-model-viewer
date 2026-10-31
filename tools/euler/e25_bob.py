"""E2.4 (fixtures through e14_fixture.py limbs 'still' / 'bob'): vertical pelvis bob from Soldier Walk Hips, same XYZ position track for Agumon bones 1 and 10.
Base = the E2.0 variant B file. Position = Agumon's ORIGINAL mean over the replaced frames (10..40 of the idle: interpolated keys) with only the vertical Soldier oscillation added
(body-up component relative to the clip mean, x leg-length ratio 221 units / 87.9 cm); x and z keep the original track values. 'still' = the same mean, no oscillation (comparison).
Run: python e25_bob.py [SOLDIER.glb] [AGUMON.bin] -> numbers about the bob, no files."""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, ".."))
from e7_compare import load_raw, Player, pose_track, read_glb, accessor
from euler_zyx import geodesic, unquat
from e13_arm import Mix, U, D
from e17_leg import mix_pos
LO = 10


def bob_keys(sg, ab, anim='Walk', dedup=True, still=False):
    M = Mix(sg, anim); I = M.idx; M.tra = {}
    js, b = read_glb(sg); a = next(x for x in js['animations'] if x['name'] == anim)
    for c in a['channels']:
        if c['target']['path'] == 'translation': M.tra[c['target']['node']] = accessor(js, b, a['samplers'][c['sampler']]['output']).astype(float)
    if dedup and all(geodesic(unquat(q[-1]), unquat(q[0])) * D < 0.05 for q in M.rot.values()): M.nf -= 1
    P = lambda n, f=None: mix_pos(M, n, f)
    left = U(P('LeftUpLeg') - P('RightUpLeg')); up0 = P('Neck') - P('Hips'); up = U(up0 - np.dot(up0, left) * left)
    hp = np.array([P('Hips', f) for f in range(M.nf)]) * 100          # cm
    ud = np.array([up @ (h - hp.mean(0)) for h in hp])                  # body-up component about the mean, cm (+ = up)
    legM = np.linalg.norm(M.off(I['LeftLeg'])) + np.linalg.norm(M.off(I['LeftFoot']))
    tr, mo = load_raw(ab); legA = np.linalg.norm(pose_track(tr[11][0], Player(tr, mo).idle)) + np.linalg.norm(pose_track(tr[12][0], Player(tr, mo).idle))
    s = legA / legM
    orig = np.array([pose_track(tr[9][0], LO + f) for f in range(M.nf)], float)       # bone 10 original position at the replaced frames
    assert (orig == np.array([pose_track(tr[0][0], LO + f) for f in range(M.nf)], float)).all(), 'bones 1 and 10 differ'
    my = orig[0, 1] if os.environ.get('E25_BASE') == 'ref' else orig[:, 1].mean()      # 'ref' = the idle reference frame 10 (-525) instead of the mean of the replaced frames (-571)
    keys = [(int(round(orig[f, 0])), int(round(my - (0 if still else s * ud[f]))), int(round(orig[f, 2]))) for f in range(M.nf)]    # y is down: up = -y
    return keys


def main(sg, ab):
    ks = np.array(bob_keys(sg, ab)); k0 = np.array(bob_keys(sg, ab, still=True))
    tr, mo = load_raw(ab); o = np.array([pose_track(tr[9][0], LO + f) for f in range(len(ks))], float)
    print(f'frames {len(ks)}; original idle pelvis y over the same frames: {o[0, 1]:.0f} -> {o[-1, 1]:.0f} (mean {o[:, 1].mean():.0f}, breathing range {np.ptp(o[:, 1]):.0f}); still y = {k0[0, 1]}')
    print(f'bob y: min {ks[:, 1].min()} max {ks[:, 1].max()} (peak-to-peak {np.ptp(ks[:, 1])} units = {np.ptp(ks[:, 1]) / 221 * 100:.1f} % of the 221-unit leg); x range {np.ptp(ks[:, 0])}, z range {np.ptp(ks[:, 2])}; largest frame step {np.abs(np.diff(ks[:, 1])).max()} units; loop closure |last-first| {abs(ks[-1, 1] - ks[0, 1])} units')
    print(f'vertical extremes: pelvis highest at frame {int(np.argmin(ks[:, 1]))}, lowest at {int(np.argmax(ks[:, 1]))} of {len(ks)} (Soldier: Hips max up frame 2, min 25)')


if __name__ == '__main__':
    a = sys.argv[1:]
    main(a[0] if a else os.path.join(HERE, '..', '..', '..', 'kb', 'mixamo', 'Soldier.glb'), a[1] if len(a) > 1 else os.path.join(HERE, '..', '..', 'data', 'files', '0499.bin'))


def played(path, tag=None):
    """own ticks of motion 1 of a fixture with the E7 Player: world y (up = -y) of pelvis (10), ankles (13, 17), toes (14, 18) and ankle fore/aft"""
    from e8_resample import parents_of, World
    tr, mo = load_raw(path); pl = Player(tr, mo); Wd = World(parents_of(path), len(tr)); pl.set_motion(1); out = []
    for t in range(31):
        W = Wd.update(pl.pose); out.append([-W[b][1, 3] for b in (10, 13, 17, 14, 18)] + [-(W[13][2, 3] - W[11][2, 3]), -(W[17][2, 3] - W[15][2, 3])])
        pl.advance(); pl.evaluate()
    return np.array(out)


if __name__ == '__main__' and len(sys.argv) > 3:
    D_ = os.path.join(HERE, '..', '..', 'data', 'e14')
    for nm in (os.environ.get('E25_NAMES', 'still,bob').split(',')):
        r = played(os.path.join(D_, f'0499-{nm}.bin' if nm.startswith('e') else f'0499-e22-{nm}.bin'))
        print(f'\n## {nm}: pelvis height {r[:, 0].min():.0f}..{r[:, 0].max():.0f} (range {np.ptp(r[:, 0]):.0f}); left ankle {r[:, 1].min():.0f}..{r[:, 1].max():.0f}, right ankle {r[:, 2].min():.0f}..{r[:, 2].max():.0f}; left toe {r[:, 3].min():.0f}..{r[:, 3].max():.0f}, right toe {r[:, 4].min():.0f}..{r[:, 4].max():.0f} (up = +, world units)')
        print(f'  lowest toe over the cycle {min(r[:, 3].min(), r[:, 4].min()):.0f}; frames with a toe below the lowest toe of the idle reference: ' + str(int(((r[:, 3] < r[0, 3] - 0) | (r[:, 4] < r[0, 4] - 0)).sum())))
        print(f'  pelvis vs legs phase: pelvis highest at {int(np.argmax(r[:, 0]))}, lowest {int(np.argmin(r[:, 0]))}; left ankle forward max {int(np.argmax(r[:, 5]))} back {int(np.argmin(r[:, 5]))}; right forward max {int(np.argmax(r[:, 6]))} back {int(np.argmin(r[:, 6]))}; correlation pelvis height with the left/right ankle forward position {np.corrcoef(r[:, 0], r[:, 5])[0, 1]:+.2f} / {np.corrcoef(r[:, 0], r[:, 6])[0, 1]:+.2f}')
        print(f'  largest frame step of the pelvis {np.abs(np.diff(r[:, 0])).max():.0f}; loop closure |last-first| {abs(r[-1, 0] - r[0, 0]):.0f}')
