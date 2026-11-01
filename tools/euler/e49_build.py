"""E4.2d (writes ONE throw-away viewer fixture): Rambley 'mixamo.com' (twerk dance, 30 fps) retargeted onto ExVeemon (0575) and installed as its IDLE for the viewer.
Method = the E1.x ABS pipeline with the moving body frame (carrier): legs 2-5 / 6-9 (e17), arms 14-16 / 22-24 (e13), pelvis bone 1 and torso bone 13 rotations (e20), and ONE hips translation
written identically to the sibling bones 1 and 13 (body-frame displacement from the clip mean, scaled by the leg-length ratio). Everything else (tail 10-12, head 17-19, wings 20-21 / 25-26) stays at the reference (idle frame 10).
The keys go to NEW frames above the file's last key (e41_writer.add_motion, new slot), and the idle motion's two steps are re-pointed at them: (0,n,F0,F0+n-1), (n,0,0x8000,0). Original 0575.bin untouched.
Env (set by this script): E49_SRC=rambley E49_MODEL=0575 E49_DEDUP=1. Output viewer/data/e14/0575-e49-dance.bin, kb/e49-build-output.txt"""
import os, sys, struct, hashlib
os.environ.update(E49_SRC='rambley', E49_MODEL='0575', E49_DEDUP='1')
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..'))
import numpy as np, contextlib, io
import survey_keys as sk
from survey_real import read_motions
from e7_compare import load_raw, pose_track, read_glb
from e13_arm import Mix, U, main as arm_main
from e17_leg import main as leg_main, mix_pos
from e20_body import body_keys
from e41_writer import add_motion
import e49_src
SG = os.environ.get('E49_GLTF', 'C:/Users/ferve/Desktop/km_rambley_twerk/scene.gltf'); ANIM = 'mixamo.com'
D = os.path.join(HERE, '..', '..', 'data'); AB = os.path.join(D, 'files', '0575.bin'); OUT = os.path.join(D, 'e14', '0575-e49-dance.bin')
lines = []
def log(s): print(s); lines.append(s)
def quiet(fn, *a):
    with contextlib.redirect_stdout(io.StringIO()): return fn(*a)
raw = open(AB, 'rb').read(); h0 = hashlib.sha1(raw).hexdigest(); tr, mo = load_raw(AB); nb = len(tr)
keys = {}                                  # bone -> list of (x,y,z) per frame (rotation track)
for side in ('Left', 'Right'):
    L = quiet(leg_main, SG, AB, ANIM, True, side, True)['ABS']; A = quiet(arm_main, SG, AB, ANIM, True, side, True)['ABS']
    for b, k in zip((range(2, 6) if side == 'Left' else range(6, 10)), L): keys[b] = k
    for b, k in zip(((14, 15, 16) if side == 'Left' else (22, 23, 24)), A): keys[b] = k
keys[1] = body_keys(SG, AB, ANIM, True, 'pelvis')[0][0]; keys[13] = body_keys(SG, AB, ANIM, True, 'torso')[0][0]
n = len(keys[1]); assert all(len(v) == n for v in keys.values()), {b: len(v) for b, v in keys.items()}
# hips translation -> bones 1 and 13 (same value, once)
M = Mix(SG, ANIM); I = M.idx; js, bb = read_glb(SG); a = next(x for x in js['animations'] if x['name'] == ANIM); M.tra = e49_src.channels(js, bb, a, 'translation'); M.nf = n
P = lambda nm, f=None: mix_pos(M, nm, f) * 100
left = U(P('LeftUpLeg') - P('RightUpLeg')); up0 = P('Neck') - P('Hips'); up = U(up0 - np.dot(up0, left) * left); fwd = np.cross(left, up); BM = np.column_stack([left, up, fwd])
hp = np.array([P('Hips', f) for f in range(n)]); d = (hp - hp.mean(0)) @ BM          # body-frame displacement (left, up, fwd) about the clip mean, Rambley units
ref = {b: np.array(pose_track(tr[b - 1][0], 10), float) for b in (1, 13)}; assert (ref[1] == ref[13]).all()
aleft = np.array([1.0, 0, 0]); aup = np.array([0, -1.0, 0]); afwd = np.cross(aleft, aup); BA = np.column_stack([aleft, aup, afwd])   # DW3 body frame at rest: left +x, up -y, forward = left x up (= -z)
legA = np.linalg.norm(pose_track(tr[2][0], 10)) + np.linalg.norm(pose_track(tr[3][0], 10)); legM = np.linalg.norm(M.off(I['LeftLeg'])) + np.linalg.norm(M.off(I['LeftFoot'])); s = legA / legM
bob = [tuple(int(round(v)) for v in ref[1] + BA @ (s * d[f])) for f in range(n)]
log(f'frames {n} (30 fps, {n / 30:.2f} s); scale dest/source leg length {s:.0f}; hips displacement about the mean (source units) left {np.ptp(d[:, 0]):.3f} up {np.ptp(d[:, 1]):.3f} fwd {np.ptp(d[:, 2]):.3f} (leg {legM:.3f}) -> DW3 range x {np.ptp([b[0] for b in bob])} y {np.ptp([b[1] for b in bob])} z {np.ptp([b[2] for b in bob])} (leg {legA:.0f})')
maxf = max(k[3] for b in tr for t in b for k in t if k[3] < 30000); F0 = (maxf // 10 + 2) * 10
tracks = {}
for b in range(1, nb + 1):
    tt = []
    for t in range(3):
        if t == 1 and b in keys: v = [tuple(int(x) for x in keys[b][f]) for f in range(n)]
        elif t == 0 and b in (1, 13): v = bob
        else: v = [tuple(int(x) for x in pose_track(tr[b - 1][t], 10))] * n
        tt.append([(*v[0], F0), (*v[0], F0 + n - 1)] if all(x == v[0] for x in v) else [(*x, F0 + i) for i, x in enumerate(v)])
    tracks[b] = tt
steps = [(0, n, F0, F0 + n - 1), (n, 0, -32768, 0)]
blob = bytearray(add_motion(raw, tracks, steps)); slot = add_motion.slot; log(f'new motion slot {slot} (number {slot + 1}) steps {steps}; bridged tracks {len(add_motion.bridged)}')
a_ = sk.Arc(bytes(blob)); bi = sk.find_bone(a_); tex = sk.u32(bytes(blob), a_.off(bi))
mi = next(i for i in range(a_.n()) if i not in (bi, tex) and a_.off(i) and read_motions(a_.sub(i)))
p = a_.off(mi) + sk.Arc(a_.sub(mi).b).off(0); assert struct.unpack_from('<hhhh', blob, p) == (0, 100, 10, 109) and struct.unpack_from('<hhhh', blob, p + 8) == (100, 0, -32768, 0), 'unexpected idle steps'
struct.pack_into('<hhhh', blob, p, *steps[0]); struct.pack_into('<hhhh', blob, p + 8, *steps[1]); out = bytes(blob)
open(OUT, 'wb').write(out); t1, m1 = load_raw(OUT)
log(f'idle steps now {m1[0][:2]}; new slot steps {m1[slot][:2]}; wrote {OUT} {len(out)} bytes ({len(out) / 2048:.1f} sectors; original {len(raw)})')
changed = [i for i in range(len(mo)) if mo[i] != m1[i]]; log(f'motion table entries that differ from the original: {len(changed)} (idle + motions that run through its steps + the new slot)')
log(f'original untouched: {hashlib.sha1(open(AB, "rb").read()).hexdigest() == h0}')
open(os.path.join(HERE, '..', '..', '..', 'kb', 'e49-build-output.txt'), 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
