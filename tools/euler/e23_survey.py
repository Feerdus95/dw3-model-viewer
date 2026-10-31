"""E2.2 (READ-ONLY): survey of Agumon's 62 motions with the E7-validated Player, own (non-blend) ticks only.
For each motion: play from set_motion until it ends, or until a key position repeats (one loop period), recording the pose of bones 10/1/11/15 on every non-blend tick.
Columns: own ticks, loop?, blend ticks skipped, pelvis position range (x,y,z) and % of the 221-unit leg, torso==pelvis position, leg alternation = correlation of the left/right ankle fore/aft (Euler x of bones 11/15), thigh pitch range,
dominant vertical cycles per loop, correlation of pelvis height with |thigh pitch| (a bob). Run: python e23_survey.py [AGUMON.bin]"""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, ".."))
from e7_compare import load_raw, Player
from e8_resample import parents_of, World
AB = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', 'data', 'files', '0499.bin')
tr, mo = load_raw(AB); pl = Player(tr, mo); LEG = 221.0; Wd = World(parents_of(AB), len(tr))
seqs = {}
for m in range(1, len(mo) + 1):
    pl.set_motion(m)
    if pl.count <= 0: continue
    rec, seen, skipped, loop = [], set(), 0, False
    for t in range(1700):
        if pl.keyframe in seen: loop = True; break
        seen.add(pl.keyframe)
        if pl.blending: skipped += 1
        else:
            Wt = Wd.update(pl.pose); hp = Wt[10][:3, 3]; hr = Wt[15][:3, 3]
            rec.append((pl.frame, np.array(pl.pose[9][0], float), np.array(pl.pose[0][0], float), (Wt[13][:3, 3] - Wt[11][:3, 3]), (Wt[17][:3, 3] - Wt[15][:3, 3])))
        if pl.done or pl.keyframe >= pl.count - 1: break
        pl.advance(); pl.evaluate()
    seqs[m] = (rec, loop, skipped, pl.done)
print(f'{len(seqs)} motions with steps. columns: m | own ticks | loop | blend ticks | pelvis y range (% leg) | x | z | torso==pelvis | ankle fore/aft L/R corr | ankle swing % leg | bob cycles | corr(pelvis y, ankle swing)')
cand = []
for m, (rec, loop, sk, done) in seqs.items():
    if len(rec) < 3: print(f'{m:3d} | {len(rec):3d} ticks, too short'); continue
    p10 = np.array([r[1] for r in rec]); p1 = np.array([r[2] for r in rec]); AL = np.array([r[3] for r in rec]); AR = np.array([r[4] for r in rec]); tl = AL[:, 2]; trr = AR[:, 2]      # ankle relative to its hip joint: z = fore/aft (forward is -z)
    rg = np.ptp(p10, axis=0); same = bool((p10 == p1).all())
    corr = np.corrcoef(tl, trr)[0, 1] if tl.std() > 0 and trr.std() > 0 else float('nan')
    pr = max(np.ptp(tl), np.ptp(trr)) / LEG * 100
    y = p10[:, 1] - p10[:, 1].mean(); cyc = int(np.argmax(np.abs(np.fft.rfft(y))[1:]) + 1) if y.std() > 0 and len(y) > 3 else 0
    thmag = np.abs(tl - tl.mean()) + np.abs(trr - trr.mean()); cb = np.corrcoef(y, thmag)[0, 1] if y.std() > 0 and thmag.std() > 0 else float('nan')
    print(f'{m:3d} | {len(rec):4d} | {"loop" if loop else ("end" if done else "cap ")} | {sk:3d} | {rg[1]:5.0f} ({rg[1] / LEG * 100:3.0f}%) | {rg[0]:4.0f} | {rg[2]:4.0f} | {"same" if same else "DIFF"} | {corr:+.2f} | {pr:5.0f} | {cyc} | {cb:+.2f}')
    if loop and not np.isnan(corr) and corr < -0.4 and pr > 30: cand.append(m)
print(f'\nlocomotion-like candidates (looping, left/right ankle fore/aft anti-correlated < -0.4, ankle swing > 30 % of the leg): {cand}')

# detail of the candidates and of the other 17-tick loop (per tick: frame, pelvis y/z, left and right ankle fore/aft and height relative to the hip joint, units; up = -y)
for m in [int(x) for x in os.environ.get('E23_DETAIL', '16,18').split(',')]:
    rec = seqs[m][0]; print(f'\n## motion {m}: {len(rec)} own ticks')
    print('  tick frame | pelvis y z | L ankle fore(-z)/down(y) | R ankle fore/down')
    for i, r in enumerate(rec): print(f'  {i:3d} {r[0]:4d} | {r[1][1]:6.0f} {r[1][2]:5.0f} | {-r[3][2]:6.0f} {r[3][1]:6.0f} | {-r[4][2]:6.0f} {r[4][1]:6.0f}')
