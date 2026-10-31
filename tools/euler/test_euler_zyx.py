"""Synthetic validation of euler_zyx / dw3_euler. Run: python viewer/tools/euler/test_euler_zyx.py  (numpy lives in the user site, so not -I)
No game data. Thresholds were fixed before the first run:
  float round trip  <= 1e-9 rad              (every case)
  quantised round trip <= 0.14 deg           (3 * half a step: each angle moves <= 0.044 deg and each angle's Jacobian has norm 1)
  tracked == generating path (mod a constant 2*pi per angle) to <= 1e-9 rad, on cases whose truth is continuous, given the start
  lerp vs slerp <= 0.25 deg, path/geodesic <= 1.1 (steps >= 1 deg) and excess path <= 0.25 deg (both on the exact Euler lerp), integer lerp within 1 unit of it, on the smooth cases.
  (revision after run 2: the spin/excess metric was first measured on the integer lerp; its per-sample rounding is not geometry)
  no component step >= 180 deg on any tracked case; the naive per-key baseline is reported for contrast"""
import math, os, sys, random
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from euler_zyx import TAU, compose, geodesic, track, candidates, quat, unquat, slerp
from dw3_euler import UNITS, rotations_to_keys, key_matrix, dw3_lerp, from_units

D = 180 / math.pi


def interp_metrics(keys):
    """Per pair of consecutive keys, sampled at t = 0..4096 step 256:
    dev: worst deviation (deg) of the game's integer lerp from the slerp of the two keys;
    spin, excess: path/geodesic and degrees travelled beyond the straight way, on the exact (float) linear Euler lerp, which is
        what the keys intend; the integer lerp rounds every sample, a noise of up to a step that is not geometry;
    unit_err: worst per-component gap (units) between the integer lerp and the exact one; the game's arithmetic must stay within 1."""
    dev, spin, excess, unit_err = 0.0, 1.0, 0.0, 0.0
    for k0, k1 in zip(keys, keys[1:]):
        R0, R1 = key_matrix(k0), key_matrix(k1)
        g = geodesic(R0, R1)
        path, last = 0.0, R0
        for t in range(0, 4097, 256):
            ki = dw3_lerp(k0, k1, t)
            kf = [a + (b - a) * t / 4096 for a, b in zip(k0, k1)]
            unit_err = max(unit_err, max(abs(i - f) for i, f in zip(ki, kf)))
            dev = max(dev, geodesic(key_matrix(ki), slerp(R0, R1, t / 4096)))
            R = compose(*(from_units(f) for f in kf))
            path += geodesic(last, R); last = R
        excess = max(excess, path - g)
        if g * D >= 1.0: spin = max(spin, path / g)  # a ratio means nothing below the 0.088 deg quantum
    return dev * D, spin, excess * D, unit_err


def naive(Rs):
    return [candidates(R)[0][1] for R in Rs]


def max_step(angles):
    return max((abs(a - b) for p, c in zip(angles, angles[1:]) for a, b in zip(p, c)), default=0.0) * D


results, failures = [], []


def check(cond, msg):
    if not cond: failures.append(msg)


def run(name, truth=None, Rs=None, smooth=False, continuous_truth=False, hint=True, lerp_check=True, quant=True):
    """TRUTH: generating Euler path (radians, may exceed +-pi). Rs from it unless given."""
    Rs = Rs if Rs is not None else [compose(*t) for t in truth]
    start = truth[0] if (truth is not None and hint) else None
    seq = track(Rs[1:], start) if start is not None else track(Rs[1:], None)
    seq = [start] + seq if start is not None else seq
    # the first rotation is consumed as START when a hint is given; the loop below covers Rs[1:]
    if start is None: Rs_used = Rs[1:]
    else: Rs_used = Rs[1:]
    ferr = max(geodesic(R, compose(*a)) for R, a in zip(Rs_used, seq[1:] if start is not None else seq))
    row = dict(case=name, n=len(Rs), float_err=ferr, quant_err_deg=float("nan"), step_deg=max_step(seq), naive_step_deg=max_step(naive(Rs)))
    check(ferr <= 1e-9, f"{name}: float round trip {ferr}")
    keys = []
    if quant:  # G (independent rotations) drifts out of int16 by design, see test I2
        keys = rotations_to_keys(Rs_used, tuple(round(a * UNITS / TAU) for a in start) if start is not None else None)
        qerr = max(geodesic(R, key_matrix(k)) for R, k in zip(Rs_used, keys)) * D
        row["quant_err_deg"] = qerr
        check(qerr <= 0.14, f"{name}: quantised round trip {qerr} deg")
    check(row["step_deg"] < 180, f"{name}: tracked step {row['step_deg']} deg")
    if continuous_truth and start is not None:
        off = max(abs(a - b) for s, t in zip(seq[1:], truth[1:]) for a, b in zip(s, t))
        row["vs_truth_rad"] = off
        check(off <= 1e-9, f"{name}: tracked differs from truth by {off} rad")
    if lerp_check and quant:
        dev, spin, excess, uerr = interp_metrics(keys)
        row.update(lerp_dev_deg=dev, lerp_spin=spin, lerp_excess_deg=excess, lerp_unit_err=uerr)
        check(uerr <= 1.0, f"{name}: integer lerp is {uerr} units from the exact one")
        if smooth:
            check(dev <= 0.25, f"{name}: lerp deviates {dev} deg from slerp")
            check(spin <= 1.1, f"{name}: lerp path/geodesic {spin}")
            check(excess <= 0.25, f"{name}: lerp travels {excess} deg beyond the straight way")
    results.append(row)


N = 400
u = [i / (N - 1) for i in range(N)]
run("A single axis, 3 turns of z", [(0, 0, 3 * TAU * s) for s in u], smooth=True, continuous_truth=True)
run("B combined, 2 turns of z", [(math.sin(3 * s), 1.0 * math.sin(2 * s), 2 * TAU * s) for s in u], smooth=True, continuous_truth=True)
run("C cross 360 (x 350->370 deg)", [(math.radians(350 + 20 * s), 0.3, -0.2) for s in u], smooth=True, continuous_truth=True)
run("C2 cross -360 backwards, y negative", [(-math.radians(350 + 20 * s), -0.5, 3.0 + s) for s in u], smooth=True, continuous_truth=True)
run("D through the pole (y 80->100 deg)", [(0.3 + s, math.radians(80 + 20 * s), -0.2 + 0.5 * s) for s in u], smooth=True, continuous_truth=True)
run("D2 through the south pole (y -80->-100)", [(0.3 + s, math.radians(-80 - 20 * s), -0.2 + 0.5 * s) for s in u], smooth=True, continuous_truth=True)
# E: dwell exactly at pitch +90: only x-z is determined
E = [(0.5 * s, math.pi / 2, 1.0 - 2 * s) for s in u]
run("E dwell at y=+90 exactly", E, hint=True)
run("E2 dwell at y=-90 exactly", [(0.5 * s, -math.pi / 2, 1.0 - 2 * s) for s in u], hint=True)
# F: hovering 0.01 deg short of the pole with small noise
random.seed(1)
F = []
x, z = 0.0, 0.0
for i in range(N):
    x += random.uniform(-0.02, 0.02); z += random.uniform(-0.02, 0.02)
    F.append((x, math.radians(89.99) + random.uniform(-1e-4, 1e-4), z))
run("F hover at y=89.99 deg with noise", F, hint=True, continuous_truth=False)
# G: independent random rotations (worst case for continuity, fine for round trip)
G = [unquat(np.array([random.gauss(0, 1) for _ in range(4)])) for _ in range(1000)]
run("G random independent rotations (float only)", Rs=G, hint=False, quant=False)
# I2: ... and quantised, the winding of unrelated rotations random-walks until int16 overflows: refused, not wrapped
try:
    rotations_to_keys(G); results.append(dict(case="I2 1000 unrelated rotations -> keys", note="fit in int16"))
except OverflowError as ex:
    results.append(dict(case="I2 1000 unrelated rotations -> keys", note="refused: " + str(ex)[:50]))
# I3: a quantised prefix of them is exact to 0.14 deg wherever it fits (200 frames)
Gp = G[:200]
kp = rotations_to_keys(Gp)
check(max(geodesic(R, key_matrix(k)) for R, k in zip(Gp, kp)) * D <= 0.14, "I3: 200 unrelated rotations quantised")
# H: no start hint on a continuous path: equals truth up to a constant 2*pi multiple per angle
Ht = [(4.0 + 2 * s, 0.7, -5.0 + 3 * s) for s in u]
run("H no start hint (offset by 2*pi*k)", Ht, hint=False, smooth=True)
Rs_h = [compose(*t) for t in Ht]
sq = track(Rs_h[1:], None)
for c in range(3):
    offs = {round((s[c] - t[c]) / TAU) for s, t in zip(sq, Ht[1:])}
    check(len(offs) == 1, f"H: offset of angle {c} changes along the path: {offs}")
# I: 9 turns must be refused, not wrapped
try:
    rotations_to_keys([compose(0, 0, 9 * TAU * s) for s in u[1:]], (0, 0, 0)); failures.append("I: 9 turns accepted")
except OverflowError:
    pass
# J: a 200 deg jump of one angle between keys is refused (the game's int16 difference would wrap)
try:
    rotations_to_keys([compose(0, 0, math.radians(190))], (0, 0, 0)); results.append(dict(case="J 190 deg in one step", note="accepted (shorter way: -170)"))
except OverflowError:
    results.append(dict(case="J 190 deg in one step", note="refused"))
# K: DW3 lerp edge: int16 wrap model
check(dw3_lerp((0, 0, 0), (-4000, 4000, 8192), 2048) == (-2000, 2000, 4096), "K: lerp midpoint")
check(dw3_lerp((100, 0, 0), (100, 0, 0), 1234) == (100, 0, 0), "K: lerp equal keys")
check(dw3_lerp((3800, 0, 0), (100, 0, 0), 2048) == (1950, 0, 0), "K: lerp takes the arithmetic (long) way, as the game does")

# L: sparsity sweep (informational, no assertions): random slerps between two rotations, K keys each. Shows where the game's
# Euler lerp stops tracking the intended rotation (large steps, paths near the pole). Pairs the converter refuses are counted.
def sweep():
    rnd = random.Random(7)
    rows = []
    for K in (65, 17, 9, 5):
        devs, exc, refused, steps, near = [], [], 0, [], 0
        for _ in range(300):
            a = unquat(np.array([rnd.gauss(0, 1) for _ in range(4)])); b = unquat(np.array([rnd.gauss(0, 1) for _ in range(4)]))
            Rs = [slerp(a, b, i / (K - 1)) for i in range(K)]
            steps.append(geodesic(a, b) / (K - 1) * D)
            try:
                keys = rotations_to_keys(Rs[1:], tuple(round(v * UNITS / TAU) for v in track(Rs[:1])[0]))
            except OverflowError:
                refused += 1; continue
            d, sp, ex, ue = interp_metrics(keys)
            devs.append(d); exc.append(ex)
            near += min(math.hypot(R[0, 0], R[1, 0]) for R in Rs) < 0.1
        q = lambda v, p: sorted(v)[min(len(v) - 1, int(p * len(v)))] if v else float("nan")
        rows.append((K, sum(steps) / len(steps), refused, near, q(devs, .5), q(devs, .99), max(devs, default=float("nan")), q(exc, .5), q(exc, .99)))
    return rows


sweep_rows = sweep()

hdr = f"{'case':44} {'n':>5} {'float err':>10} {'quant deg':>9} {'step deg':>8} {'naive step':>10} {'lerp dev':>8} {'spin':>6} {'excess':>7} {'int-lerp':>8}"
print(hdr)
for r in results:
    if "note" in r: print(f"{r['case']:44} {r['note']}"); continue
    print(f"{r['case']:44} {r['n']:5d} {r['float_err']:10.2e} {r['quant_err_deg']:9.4f} {r['step_deg']:8.2f} {r['naive_step_deg']:10.1f} "
          + (f"{r['lerp_dev_deg']:8.3f} {r['lerp_spin']:6.2f} {r['lerp_excess_deg']:7.3f} {r['lerp_unit_err']:8.3f}" if 'lerp_dev_deg' in r else ""))
print("\nL sparsity sweep: 300 random slerps per row (informational)")
print(f"{'keys':>5} {'avg step deg':>12} {'refused':>8} {'near pole':>9} {'dev p50':>8} {'dev p99':>8} {'dev max':>8} {'exc p50':>8} {'exc p99':>8}")
for r in sweep_rows: print(f"{r[0]:5d} {r[1]:12.2f} {r[2]:8d} {r[3]:9d} " + " ".join(f"{v:8.3f}" for v in r[4:]))
print("FAILURES:" if failures else "ALL PASSED", *failures, sep="\n  " if failures else " ")
sys.exit(1 if failures else 0)
