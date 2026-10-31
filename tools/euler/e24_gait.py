"""E2.3 (READ-ONLY): look for a locomotion cycle with alternating legs in every model of viewer/data/files, from the kinematics (not the names).
Per model: leg pairs = two mirrored chains (offsets x -> -x, same length) hanging from one joint P (the leaf of each chain far below P); gait = the non-blend ticks of a motion (a motion ends at its end marker and the game restarts it, so a cycle is one whole motion), >= 8 own ticks, closing (first and last leaf z within 40 % of the swing), with the leaf of the two chains moving fore/aft (z) in counter-phase
(corr < -0.5) and each swinging > 25 % of the chain length. Per candidate: pelvis (P) range x/y/z in units and as % of the chain length, cycles per loop of the leaf z (strides) and of P's y (bob),
phase between P's vertical extreme and the left leaf's forward extreme. Run: python e24_gait.py [DIR] ; 4 worker processes ; writes nothing (stdout)."""
import os, sys, glob, time
import numpy as np
from multiprocessing import Pool
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, ".."))
from e7_compare import load_raw, Player, pose_track
from e8_resample import parents_of, World


def analyse(path):
    try:
        tr, mo = load_raw(path); par = parents_of(path); n = len(tr)
        if n < 6 or not mo: return path, None
        pl = Player(tr, mo); fr = pl.idle
        ref = [[pose_track(tr[b][t], fr) for t in range(3)] for b in range(n)]; Wd = World(par, n)
        W0 = [w.copy() for w in Wd.update(ref)]
        ch = {}
        for b in range(1, n + 1): ch.setdefault(par[b - 1], []).append(b)
        off = lambda b: np.array(ref[b - 1][0], float)
        def chain(c):
            out = [c]
            while len(ch.get(out[-1], [])) == 1: out.append(ch[out[-1]][0])
            return out
        pairs = []
        for p, kids in ch.items():
            for i in range(len(kids)):
                for j in range(i + 1, len(kids)):
                    a, b = kids[i], kids[j]; oa, ob = off(a), off(b)
                    if np.linalg.norm(oa) < 20 or abs(oa[0]) < 10: continue
                    if np.linalg.norm(oa - np.array([-ob[0], ob[1], ob[2]])) > 0.15 * np.linalg.norm(oa) + 5: continue
                    ca, cb = chain(a), chain(b)
                    if len(ca) < 2 or len(ca) != len(cb): continue
                    Lc = sum(np.linalg.norm(off(x)) for x in ca)
                    drop = W0[ca[-1]][1, 3] - W0[p][1, 3] if p else 0       # y is down
                    if drop > 0.5 * Lc: pairs.append((p, ca, cb, Lc))
        if not pairs: return path, {'n': n, 'pairs': 0, 'cand': []}
        cands, seen_sig, best, nloops = [], set(), None, 0
        for m in range(1, len(mo) + 1):
            pl.set_motion(m)
            if pl.count <= 0: continue
            rec, seenk = [], {}
            for t in range(600):
                if pl.keyframe in seenk: i0 = 0; break      # reached the end marker (the motion ends there; the game restarts a cycle by calling the motion again) or a jump back: whole sequence
                seenk[pl.keyframe] = len(rec)
                if not pl.blending: rec.append([list(x) for x in pl.pose])
                else: rec.append(None)
                if pl.done or pl.keyframe >= pl.count - 1: i0 = 0; break
                pl.advance(); pl.evaluate()
            else: i0 = None
            if i0 is None: continue
            loop = [r for r in rec[i0:] if r is not None]
            if len(loop) < 8: continue
            Ws = [[w.copy() for w in Wd.update(r)] for r in loop]; nloops += 1
            for (p, ca, cb, Lc) in pairs:
                zl = np.array([w[ca[-1]][2, 3] - w[p][2, 3] for w in Ws]); zr = np.array([w[cb[-1]][2, 3] - w[p][2, 3] for w in Ws])
                if zl.std() < 1e-6 or zr.std() < 1e-6: continue
                clo = max(abs(zl[0] - zl[-1]), abs(zr[0] - zr[-1])) / max(np.ptp(zl), np.ptp(zr))
                c = np.corrcoef(zl, zr)[0, 1]; sw = min(np.ptp(zl), np.ptp(zr)) / Lc
                if sw > 0.1 and (best is None or c < best[0]): best = (float(c), float(sw), m, len(loop))
                if c < -0.5 and sw > 0.25 and clo < 0.4:
                    P = np.array([w[p][:3, 3] for w in Ws]); sig = (round(float(zl[0])), round(float(zl[len(zl) // 2])), len(loop))
                    if sig in seen_sig: continue
                    seen_sig.add(sig)
                    cz = int(np.argmax(np.abs(np.fft.rfft(zl - zl.mean()))[1:]) + 1); cy = int(np.argmax(np.abs(np.fft.rfft(P[:, 1] - P[:, 1].mean()))[1:]) + 1) if np.ptp(P[:, 1]) > 0 else 0
                    ph = (int(np.argmin(P[:, 1])) - int(np.argmin(zl))) % len(loop)
                    cands.append(dict(clo=float(clo), m=m, period=len(loop), corr=float(c), swing=float(sw), rng=np.ptp(P, axis=0).tolist(), Lc=float(Lc), cz=cz, cy=cy, phase=ph, p=p, leaf=(ca[-1], cb[-1]), depth=len(ca)))
        return path, {'n': n, 'pairs': len(pairs), 'cand': cands, 'best': best, 'loops': nloops}
    except Exception as ex:
        return path, {'err': repr(ex)[:80]}


if __name__ == '__main__':
    d = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', 'data', 'files')
    files = sorted(glob.glob(os.path.join(d, '*.bin'))); t0 = time.time(); res = []
    with Pool(4) as pool:
        for i, r in enumerate(pool.imap_unordered(analyse, files, chunksize=2)):
            res.append(r)
            if (i + 1) % 50 == 0: print(f'  {i + 1}/{len(files)} files, {time.time() - t0:.0f} s', file=sys.stderr, flush=True)
    res.sort()
    ok = [r for r in res if r[1] and 'err' not in r[1]]; err = [r for r in res if r[1] and 'err' in r[1]]; none = [r for r in res if r[1] is None]
    print(f'{len(files)} files: analysed {len(ok)}, errors {len(err)}, not a model/no motions {len(none)}; with leg pairs {sum(1 for r in ok if r[1]["pairs"])}; with a gait candidate {sum(1 for r in ok if r[1]["cand"])}')
    bl = sorted((r['best'][0], os.path.basename(p), r['best'][1:], r['n']) for p, r in ok if r.get('best'))
    print(f'loops found in {sum(1 for p, r in ok if r.get("loops"))} models; most anti-correlated leg pair (swing > 10 %) per model, best 12: ' + '; '.join(f'{b} corr {c:+.2f} swing {s_[0] * 100:.0f}% motion {s_[1]} period {s_[2]}' for c, b, s_, nn in bl[:12]))
    print('  distribution of the per-model best corr: ' + str(np.histogram([x[0] for x in bl], bins=[-1.01, -0.5, -0.2, 0.2, 0.5, 1.01])[0]) + ' (bins -1..-.5, -.5..-.2, -.2...2, .2..-.5, .5..1)')
    for p, r in err[:5]: print('  error', os.path.basename(p), r['err'])
    for p, r in ok:
        for c in r['cand']:
            print(f'{os.path.basename(p)} bones {r["n"]} pairs {r["pairs"]} | motion {c["m"]:2d} period {c["period"]:3d} | corr {c["corr"]:+.2f} swing {c["swing"] * 100:3.0f}% | chain {c["Lc"]:4.0f} depth {c["depth"]} P={c["p"]} | pelvis range x {c["rng"][0]:4.0f} y {c["rng"][1]:4.0f} z {c["rng"][2]:4.0f} = {c["rng"][0] / c["Lc"] * 100:3.0f}/{c["rng"][1] / c["Lc"] * 100:3.0f}/{c["rng"][2] / c["Lc"] * 100:3.0f} % | z cycles {c["cz"]} y cycles {c["cy"]} phase {c["phase"]}')
