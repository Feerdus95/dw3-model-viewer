"""E8 (READ-ONLY on models and the animation format): per-frame resampling of the rotation tracks against the validated player.
Nothing is written; the variants exist only in memory. Run: python e8_resample.py FILE.bin Label [--top N]

Reference A = the model's own rotation tracks, played by e7_compare.Player (validated against the viewer in E7), every tick.
B = each rotation track rebuilt with one key per PLAYED frame (frames the player visits, plus blend sources/targets, plus the
    first and last real key): the desired orientation at that frame is A's own evaluated rotation; it is turned into Euler
    triples by euler_zyx/dw3_euler.rotations_to_keys (unchanged), and played back with the same player (DW3's integer lerp).
C = same density, but the desired orientation is the SLERP between the two bracketing ORIGINAL keys (control only).
Positions and scales are left as authored in B and C. Runs of consecutive played frames are converted continuously, starting from
A's own Euler triple at the run's first frame (a winding hint, not an answer): winding is therefore carried over from the original.
A conversion that overflows int16 or jumps >= 16384 units raises in the converter; that track then keeps its original data in the
variant and is counted as a limitation. No converter threshold is changed.
Samples: every tick of every motion, every bone. Categories (priority order): blend-key / blend-snap (blending tick; the bone has an original key exactly at the blend's target frame, or the engine takes the NEXT key, model.cpp:371), longway (the bracketing
original pair has an Euler component step >= 2048 units), pole (reference local rotation < 10 deg from the gimbal pole), normal.
Errors: local rotation (geodesic), world rotation, world joint position (model units; leaf bones reported apart)."""
import bisect, math, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, ".."))
from euler_zyx import geodesic, slerp
from dw3_euler import key_matrix, rotations_to_keys
from e7_compare import load_raw, Player, pose_track
from survey_real import pole_deg
import survey_keys as sk

D = 180 / math.pi


def parents_of(path):
    a = sk.Arc(open(path, 'rb').read()); e = a.off(sk.find_bone(a))
    return [sk.u32(a.b, e + 8 + b * 12) for b in range(sk.u32(a.b, e + 4))]   # viewer bone numbers, 0 = control block


def run_ticks(pl, motions):
    """snapshots per motion: (pose list, frame, blending, blend source)"""
    out = {}
    for m in range(1, len(motions) + 1):
        pl.set_motion(m)
        if pl.count <= 0: continue
        snaps = []
        for tick in range(1700):
            snaps.append((list(pl.pose), pl.frame, pl.blending, pl.bs[pl.keyframe] if pl.blending else None, list(pl.prev) if pl.blending else None))
            if pl.done or pl.keyframe >= pl.count - 1: break
            pl.advance(); pl.evaluate()
        out[m] = snaps
    return out


def build_variant(tr, F, mode):
    """New track list (rotation tracks resampled). mode 'B' or 'C'. Returns tracks, stats."""
    new, st = [list(t) for t in tr], dict(keys=0, full=0, failed=0, resampled=0, notes=[])
    for b, trk in enumerate(tr):
        ka = trk[1]; real = [k for k in ka if k[3] < 30000]
        if len(real) < 2:
            st['keys'] += len(ka); continue
        lo, hi = real[0][3], real[-1][3]
        fs = sorted({f for f in F if lo <= f <= hi} | {lo, hi})
        rf = [k[3] for k in real]; Rk = [key_matrix(k[:3]) for k in real]
        def want(f):
            if mode == 'B': return key_matrix(pose_track(ka, f))
            i = bisect.bisect_right(rf, f) - 1
            if i >= len(real) - 1 or rf[i] == f: return Rk[min(i, len(real) - 1)]
            return slerp(Rk[i], Rk[i + 1], (f - rf[i]) / (rf[i + 1] - rf[i]))
        runs, cur = [], [fs[0]]
        for f in fs[1:]:
            if f == cur[-1] + 1: cur.append(f)
            else: runs.append(cur); cur = [f]
        runs.append(cur)
        keys, ok = [], True
        for r in runs:
            try:
                ks = rotations_to_keys([want(f) for f in r], tuple(pose_track(ka, r[0])))
            except OverflowError as ex:
                ok = False; st['notes'].append(f'track {b} frames {r[0]}..{r[-1]}: {str(ex)[:60]}'); break
            keys += [(k[0], k[1], k[2], f) for k, f in zip(ks, r)]
        if not ok:
            st['failed'] += 1; st['keys'] += len(ka); continue
        new[b][1] = keys + [ka[-1]]
        st['keys'] += len(keys) + 1; st['resampled'] += 1; st['full'] += hi - lo + 2
    return new, st


class World:
    def __init__(self, parents, n):
        self.par, self.W = parents, [np.eye(4) for _ in range(n + 1)]

    def update(self, pose):
        for b, (pos, rot, sc) in enumerate(pose):
            if (sc[0] | sc[1] | sc[2]) <= 48: continue          # hidden: keeps its last matrix (model.cpp:648)
            L = np.eye(4); L[:3, :3] = key_matrix(rot) * (np.array(sc, float) / 4096)[None, :]; L[:3, 3] = pos
            self.W[b + 1] = self.W[self.par[b]] @ L
        return self.W


def stats(v):
    v = np.asarray(v, float)
    if not len(v): return 'n=0'
    return f'n={len(v)} mean={v.mean():.3f} p50={np.percentile(v,50):.3f} p95={np.percentile(v,95):.3f} p99={np.percentile(v,99):.3f} max={v.max():.3f}'


def main(path, label, top=5):
    tr, motions = load_raw(path)
    par = parents_of(path); nb = len(tr)
    kids = {p for p in par}; leaf = [b for b in range(nb) if (b + 1) not in kids]
    A = run_ticks(Player(tr, motions), motions)
    F = set()
    for snaps in A.values():
        for _, fr, bl, src, _p in snaps:
            if 0 <= fr < 30000: F.add(fr)
            if src is not None and 0 <= src < 30000: F.add(src)
    vars_ = {}
    for mode in 'BC':
        t2, st = build_variant(tr, F, mode)
        vars_[mode] = (run_ticks(Player(t2, motions), motions), st)
    # sizes
    ka = sum(len(t) for trk in tr for t in trk); ra = sum(len(trk[1]) for trk in tr)
    print(f'\n### {label}: {nb} bones, {len(A)} motions, {sum(len(s) for s in A.values())} ticks, {len(F)} distinct played frames, leaf bones {[b + 1 for b in leaf]}')
    print(f'- size: original rotation keys {ra} ({ra * 8} B) of {ka} keys in all tracks ({ka * 8} B; file {os.path.getsize(path)} B)')
    for mode, (_, st) in vars_.items():
        print(f'- {mode}: rotation keys {st["keys"]} ({st["keys"] * 8} B, played frames only); all-frames-in-range estimate for the {st["resampled"]} resampled tracks: {st["full"]} keys ({st["full"] * 8} B); '
              f'tracks kept original because the converter refused: {st["failed"]}' + (' ' + str(st['notes'][:3]) if st['notes'] else ''))
    # per-track original pair classes for the category of a played frame
    cls = []
    for trk in tr:
        real = [k for k in trk[1] if k[3] < 30000]
        fr = [k[3] for k in real]
        lw = [max(abs(real[i + 1][c] - real[i][c]) for c in range(3)) >= 2048 for i in range(len(real) - 1)]
        cls.append((fr, lw))
    f_set = [set(c[0]) for c in cls]
    rows = {'B': [], 'C': []}   # (cat, motion, tick, bone, localrot, worldrot, worldpos)
    for mode in 'BC':
        wa, wv = World(par, nb), World(par, nb)
        for m, snaps in A.items():
            sv = vars_[mode][0][m]
            assert len(sv) == len(snaps)
            for t, ((pa, fr, bl, _, prA), (pv, _, _, _, prV)) in enumerate(zip(snaps, sv)):
                Wa, Wv = wa.update(pa), wv.update(pv)
                for b in range(nb):
                    Ra, Rv = key_matrix(pa[b][1]), key_matrix(pv[b][1])
                    lr = geodesic(Ra, Rv) * D
                    wr = geodesic(Wa[b + 1][:3, :3] / np.maximum(np.linalg.norm(Wa[b + 1][:3, :3], axis=0), 1e-9), Wv[b + 1][:3, :3] / np.maximum(np.linalg.norm(Wv[b + 1][:3, :3], axis=0), 1e-9)) * D
                    wp = float(np.linalg.norm(Wa[b + 1][:3, 3] - Wv[b + 1][:3, 3]))
                    f_, lw_ = cls[b]
                    i = bisect.bisect_right(f_, fr) - 1
                    longway = 0 <= i < len(lw_) and lw_[i]
                    onkey = fr in f_set[b]
                    cat = ('blend-key' if onkey else 'blend-snap') if bl else 'longway' if longway else 'pole' if pole_deg(Ra) < 10 else 'normal'
                    shifted = False
                    if bl:   # the blend's source pose (prev) is the same rotation as A's but a whole number of turns away
                        da = [x - y for x, y in zip(prA[b][1], prV[b][1])]
                        shifted = any(da) and all(d % 4096 == 0 for d in da)
                    rows[mode].append((cat, m, t, b, lr, wr, wp, shifted))
    # extent for relative sizes: joint spread at the first tick of the first motion
    w0 = World(par, nb); first = A[min(A)][0][0]; Wf = w0.update(first)
    pts = np.array([Wf[b + 1][:3, 3] for b in range(nb)]); ext = float(np.ptp(pts, axis=0).max())
    print(f'- model extent at the first pose (largest joint spread on one axis): {ext:.0f} units; errors below are in units, % of that in brackets for the p99/max')
    for mode in 'BC':
        R = rows[mode]
        print(f'\n#### {label} variant {mode} vs A ({"per-frame resample" if mode == "B" else "SLERP control"})')
        for cat in ('all', 'normal', 'longway', 'pole', 'blend-key', 'blend-snap'):
            sel = [r for r in R if cat == 'all' or r[0] == cat]
            if not sel: print(f'- {cat}: no samples'); continue
            print(f'- {cat}: samples {len(sel)}')
            print(f'    local rot deg: {stats([r[4] for r in sel])}')
            print(f'    world rot deg: {stats([r[5] for r in sel])}')
            print(f'    world pos units (all joints): {stats([r[6] for r in sel])}')
            lf = [r[6] for r in sel if r[3] in leaf]
            if lf: print(f'    world pos units (leaf bones): {stats(lf)}   max = {max(lf) / ext * 100:.2f}% of extent')
        bl_bad = [r for r in R if r[0].startswith('blend') and r[4] > 1]
        print(f'- blend ticks with local rotation error > 1 deg: {len(bl_bad)} of {sum(r[0].startswith("blend") for r in R)}; of those the blend SOURCE pose is the same rotation as A but whole turns away (winding): {sum(r[7] for r in bl_bad)}')
        perb = {}
        for r in R: perb.setdefault(r[3], []).append(r)
        worstb = sorted(perb, key=lambda b: -max(x[6] for x in perb[b]))[:top]
        print('- worst joints by max world position error (viewer bone, leaf?, local rot max/p99 deg, world pos p99/max units): ' +
              '; '.join(f'{b + 1}{"*" if b in leaf else ""}: {max(x[4] for x in perb[b]):.2f}/{np.percentile([x[4] for x in perb[b]], 99):.2f}, {np.percentile([x[6] for x in perb[b]], 99):.2f}/{max(x[6] for x in perb[b]):.2f}' for b in worstb))
        wr = sorted(R, key=lambda r: -r[6])[:top]
        print('- worst samples (category, motion, tick, viewer bone, local rot deg, world pos units): ' + '; '.join(f'{r[0]},{r[1]},{r[2]},{r[3] + 1},{r[4]:.2f},{r[6]:.2f}' for r in wr))


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    main(args[0], args[1] if len(args) > 1 else os.path.basename(args[0]))
