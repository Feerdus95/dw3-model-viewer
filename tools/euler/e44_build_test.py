"""E4.2b game-test file for 0499: new motion in the first free slot = a COPY of stock slot 10 (motion 11: 10 blend ticks + 71 frames 390..460, ends -1), then script 5's command #4 (slot 18) -> the new slot.
Checks: the new motion plays tick for tick like motion 11 (E7 Player), every original motion plays identically, only one byte differs from the writer's output, original untouched.
Writes viewer/data/e14/0499-e44-attack.bin and kb/e44-build-output.txt"""
import os, sys, hashlib
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..'))
from e7_compare import load_raw, pose_track
from e41_writer import add_motion, player_poses
from e42_select import fighters, u32
from e43_script_patch import patch_script_slot

lines = []
def log(s): print(s); lines.append(s)
f = next(x for x in fighters() if x['model'] >> 16 == 499); ME, EE = f['motions'] & 0xFFFF, f['effects'] & 0xFFFF
path = os.path.join(HERE, '..', '..', 'data', 'files', '0499.bin'); raw = open(path, 'rb').read(); h0 = hashlib.sha1(raw).hexdigest()
tr, mo = load_raw(path); nb = len(tr); SRC = 10
steps_src = mo[SRC]; log(f'source slot {SRC}: {steps_src}')
lo, n = 390, 71
maxf = max(k[3] for b in tr for t in b for k in t if k[3] < 30000); F0 = (maxf // 10 + 2) * 10
tracks = {}
for b in range(1, nb + 1):
    tracks[b] = []
    for t in range(3):
        v = [pose_track(tr[b - 1][t], lo + i) for i in range(n)]
        tracks[b].append([(*v[0], F0), (*v[0], F0 + n - 1)] if all(x == v[0] for x in v) else [(*x, F0 + i) for i, x in enumerate(v)])
steps = [(0, 10, 0, 0), (10, n, F0, F0 + n - 1), (10 + n, 10, 0, 0), (20 + n, 0, -1, 0)]      # the last blend goes back to idle (step[1].frame == -1), as stock motion 19 does
blob = add_motion(raw, tracks, steps); slot = add_motion.slot; log(f'new motion: slot {slot} (motion number {slot + 1}), steps {steps}, bridged tracks {len(add_motion.bridged)}')
out, info = patch_script_slot(blob, ME, EE, 5, 4, 18, slot); log(f'script patch: {info}')
d = [i for i in range(len(blob)) if blob[i] != out[i]]; log(f'bytes changed by the patch: {d}')
open(os.path.join(HERE, '..', '..', 'data', 'e14', '0499-e44-attack.bin'), 'wb').write(out)
tmp = os.path.join(HERE, '..', '..', 'data', 'e14', '0499-e44-attack.bin')
pa, _ = player_poses(path, SRC + 1, 90); pb, _ = player_poses(tmp, slot + 1, 90)
same_new = pa[:9 + n] == pb[:9 + n]; log(f'new motion plays like motion {SRC + 1} over its first 80 ticks (tick 80 is where the source ends and ours starts the blend to idle) (then it blends back to idle): {same_new}')
bad = [m for m in range(1, len(mo) + 1) if mo[m - 1] and m - 1 != slot and player_poses(path, m, 1700)[0] != player_poses(tmp, m, 1700)[0]]
log(f'originals differing: {bad}'); log(f'original untouched: {hashlib.sha1(open(path, "rb").read()).hexdigest() == h0}; output sha1 {hashlib.sha1(out).hexdigest()}, {len(out)} bytes = {len(out) // 2048} sectors')
log('E44: ' + ('PASS' if same_new and not bad and len(d) == 1 else 'FAIL'))
open(os.path.join(HERE, '..', '..', '..', 'kb', 'e44-build-output.txt'), 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
