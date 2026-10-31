"""E2.1 (READ-ONLY measurement, writes nothing): what Soldier Walk's Hips translation is, and how it compares with Agumon.
Mixamo: Hips world position (node tree units; the Character node scales 0.01 -> shown in cm), in the body frame of the pelvis-neck line (left, up, forward) used elsewhere,
relative to the clip mean; phase against the ankles; amplitudes as % of leg length (hip-ankle) so they are unit-free. Agumon: the pelvis/torso position tracks of its authored motions
(range of bones 10 and 1 per motion) and its leg length (221 units, hip joint to ankle). Run: python e22_hips.py"""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, ".."))
from e7_compare import load_raw, Player, pose_track, read_glb, accessor
from euler_zyx import geodesic, unquat
from e13_arm import Mix, U, D
from e17_leg import mix_pos
SG = os.path.join(HERE, '..', '..', '..', 'kb', 'mixamo', 'Soldier.glb'); AB = os.path.join(HERE, '..', '..', 'data', 'files', '0499.bin')
M = Mix(SG, 'Walk'); I = M.idx; M.tra = {}
js, b = read_glb(SG); a = next(x for x in js['animations'] if x['name'] == 'Walk')
for c in a['channels']:
    if c['target']['path'] == 'translation': M.tra[c['target']['node']] = accessor(js, b, a['samplers'][c['sampler']]['output']).astype(float)
if all(geodesic(unquat(q[-1]), unquat(q[0])) * D < 0.05 for q in M.rot.values()): M.nf -= 1
nf = M.nf; P = lambda n, f=None: mix_pos(M, n, f)
left = U(P('LeftUpLeg') - P('RightUpLeg')); up0 = P('Neck') - P('Hips'); up = U(up0 - np.dot(up0, left) * left); fwd = np.cross(left, up); BM = np.column_stack([left, up, fwd])
cm = 100.0
legM = np.linalg.norm(M.off(I['LeftLeg'])) + np.linalg.norm(M.off(I['LeftFoot']))      # rig units = cm (raw node translations); P() below is in metres (Character node x0.01)
print(f'Soldier Walk: {nf} frames at {M.fps:.1f} fps; Hips has a translation track: {I["Hips"] in M.tra}; tracks with translation: {len(M.tra)} (only Hips: {list(M.tra) == [I["Hips"]]})')
hp = np.array([P('Hips', f) * cm for f in range(nf)]); h0 = P('Hips') * cm
print(f'Hips rest height {h0[1]:.1f} cm above the floor plane of the rig; clip mean {hp[:, 1].mean():.1f}; leg length (knee+ankle segments) {legM:.1f} cm; hip joint height at rest / leg length {h0[1] / legM:.2f}')
loc = np.array([M.tra[I['Hips']][f] for f in range(nf)])      # raw rig units = cm
print(f'Hips local translation track range (x,y,z) cm: {np.round(np.ptp(loc, axis=0), 2)}; first->last difference {np.round(loc[-1] - loc[0], 2)}; mean {np.round(loc.mean(0), 1)}')
bc = np.array([BM.T @ (h - hp.mean(0)) for h in hp])      # (left, up, forward) relative to the mean, cm
print('\n## Hips in body axes (left, up, forward), relative to the clip mean')
for i, nm in enumerate(('left (lateral)', 'up (vertical)', 'forward')):
    print(f'  {nm}: range {bc[:, i].min():+.2f}..{bc[:, i].max():+.2f} cm (peak-to-peak {np.ptp(bc[:, i]):.2f} cm = {np.ptp(bc[:, i]) / legM * 100:.1f} % of leg length); at frames: min {int(np.argmin(bc[:, i]))}, max {int(np.argmax(bc[:, i]))}')
tot = np.array([hp[f] - hp[0] for f in range(nf)]); print(f'  net displacement over the loop (last frame vs first, body axes) {np.round(BM.T @ (hp[-1] - hp[0]), 2)} cm; step to the first frame closing the loop {np.round(BM.T @ (hp[0] - hp[-1]), 2)}')
# phase: ankles in body coords relative to hips; heel strike ~ the foot furthest forward
fl = np.array([BM.T @ (P('LeftFoot', f) * cm - P('Hips', f) * cm) for f in range(nf)]); fr = np.array([BM.T @ (P('RightFoot', f) * cm - P('Hips', f) * cm) for f in range(nf)])
print(f'\n## Phase (frames of {nf}): left ankle furthest forward {int(np.argmax(fl[:, 2]))}, furthest back {int(np.argmin(fl[:, 2]))}; right forward {int(np.argmax(fr[:, 2]))}, back {int(np.argmin(fr[:, 2]))}; Hips vertical max {int(np.argmax(bc[:, 1]))}, min {int(np.argmin(bc[:, 1]))}; lateral max (left) {int(np.argmax(bc[:, 0]))}')
print(f'  vertical cycles per clip (FFT peak, excluding DC): {int(np.argmax(np.abs(np.fft.rfft(bc[:, 1]))[1:]) + 1)}; lateral {int(np.argmax(np.abs(np.fft.rfft(bc[:, 0]))[1:]) + 1)}; forward {int(np.argmax(np.abs(np.fft.rfft(bc[:, 2]))[1:]) + 1)}  (1 = once per clip = the stride of both legs)')
# ankle height with and without the hips bob (Mixamo world, up axis of the glb)
ah = np.array([P('LeftFoot', f)[1] * cm for f in range(nf)]); print(f'  left ankle world height range {np.ptp(ah):.1f} cm ({np.ptp(ah) / legM * 100:.0f} % of leg); with the Hips vertical bob removed: {np.ptp(ah - (hp[:, 1] - hp[:, 1].mean())):.1f} cm; correlation of Hips vertical with left ankle height {np.corrcoef(bc[:, 1], ah)[0, 1]:+.2f}')
# Agumon
tr, mo = load_raw(AB); pl = Player(tr, mo); fr_ = pl.idle
ref = [[pose_track(tr[b][t], fr_) for t in range(3)] for b in range(len(tr))]
print(f'\n## Agumon: pelvis (10) position at the idle {ref[9][0]}, torso (1) {ref[0][0]}; leg length hip-ankle 221 units; unit ratio if legs are matched: {221 / legM:.2f} units/cm')
rows = []
for mi, m in enumerate(mo):
    fs = set()
    for st in m:
        i, n, f, e = st[:4]
        if n > 0 and f >= 0 and e >= f: fs |= set(range(f, e + 1))
    if not fs: continue
    r = []
    for b_ in (10, 1):
        ks = [k[:3] for k in tr[b_ - 1][0] if k[3] < 30000 and k[3] in fs]
        r.append(np.ptp(np.array(ks, float), axis=0) if len(ks) > 1 else np.zeros(3))
    rows.append((mi, len(fs), r[0], r[1]))
cyc = []
for mi, n_, p10, p1 in rows:
    fs = set()
    for st in mo[mi]:
        i_, nn, f_, e_ = st[:4]
        if nn > 0 and f_ >= 0 and e_ >= f_: fs |= set(range(f_, e_ + 1))
    ks = sorted((k for k in tr[9][0] if k[3] < 30000 and k[3] in fs), key=lambda k: k[3])
    close = np.abs(np.array(ks[0][:3], float) - np.array(ks[-1][:3], float)).max() if len(ks) > 1 else 0
    cyc.append((mi, n_, p10, close))
sm = [c for c in cyc if 0 < np.abs(c[2]).max() <= 150]
print(f'  motions where the pelvis moves at most 150 units (68 % of the leg): {len(sm)}; first/last key equal to within 5 units (loops): {sum(1 for c in sm if c[3] <= 5)}')
for mi, n_, p10, close in sorted(sm, key=lambda c: -c[1])[:10]: print(f'    motion {mi} ({n_} frames): pelvis range x,y,z {p10}, first-last difference {close:.0f}')
idle = [c for c in cyc if c[0] == 1]; print(f'  idle (motion 1): pelvis range {idle[0][2] if idle else None}')
print(f'  Hips amplitudes of Soldier converted at {221 / legM:.2f} units/cm (hip-ankle leg lengths matched): lateral {np.ptp(bc[:, 0]) * 221 / legM:.0f}, vertical {np.ptp(bc[:, 1]) * 221 / legM:.0f}, forward {np.ptp(bc[:, 2]) * 221 / legM:.0f} Agumon units')
