"""E4.2c: RETARGETED content in the game. Source = the E2.8 fixture (Soldier walk retargeted onto Agumon, FLAT feet, KNEE2) frames 10..40 of its idle slot. New motion in the first free slot of the ORIGINAL 0499 =
10 blend ticks + 31 frames + blend back to idle, script 5 command #4 (slot 18) -> that slot (same patch as E4.2b). Writes viewer/data/e14/0499-e45-retarget.bin and kb/e45-build-output.txt"""
import os, sys, hashlib
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..'))
from e7_compare import load_raw, pose_track
from e41_writer import add_motion, player_poses
from e42_select import fighters
from e43_script_patch import patch_script_slot

lines = []
def log(s): print(s); lines.append(s)
f = next(x for x in fighters() if x['model'] >> 16 == 499); ME, EE = f['motions'] & 0xFFFF, f['effects'] & 0xFFFF
D = os.path.join(HERE, '..', '..', 'data'); path = os.path.join(D, 'files', '0499.bin'); raw = open(path, 'rb').read(); h0 = hashlib.sha1(raw).hexdigest()
fx = os.path.join(D, 'e14', '0499-e28-flat-both.bin'); ftr, fmo = load_raw(fx); nb = len(ftr); _, _, lo, hi = fmo[0][0]; n = hi - lo + 1
tr, mo = load_raw(path); maxf = max(k[3] for b in tr for t in b for k in t if k[3] < 30000); F0 = (maxf // 10 + 2) * 10
tracks = {}
for b in range(1, nb + 1):
    tracks[b] = []
    for t in range(3):
        v = [pose_track(ftr[b - 1][t], lo + i) for i in range(n)]
        tracks[b].append([(*v[0], F0), (*v[0], F0 + n - 1)] if all(x == v[0] for x in v) else [(*x, F0 + i) for i, x in enumerate(v)])
steps = [(0, 10, 0, 0), (10, n, F0, F0 + n - 1), (10 + n, 10, 0, 0), (20 + n, 0, -1, 0)]
blob = add_motion(raw, tracks, steps); slot = add_motion.slot; log(f'source: fixture idle frames {lo}..{hi} ({n}); new motion slot {slot} (number {slot + 1}) steps {steps}')
out, info = patch_script_slot(blob, ME, EE, 5, 4, 18, slot); log(f'script patch: {info}')
tmp = os.path.join(D, 'e14', '0499-e45-retarget.bin'); open(tmp, 'wb').write(out)
pa, _ = player_poses(fx, 1, 60); pb, _ = player_poses(tmp, slot + 1, 100)
same = pa[10:10 + n - 1] == pb[10:10 + n - 1] if False else all(pb[10 + i] == pa[i] for i in range(n - 2))
log(f'play ticks equal the fixture idle motion (ticks 10..{n + 7} vs 0..{n - 3}): {same}')
bad = [m for m in range(1, len(mo) + 1) if mo[m - 1] and m - 1 != slot and player_poses(path, m, 1700)[0] != player_poses(tmp, m, 1700)[0]]
log(f'originals differing: {bad}; original untouched {hashlib.sha1(open(path, "rb").read()).hexdigest() == h0}; sha1 {hashlib.sha1(out).hexdigest()}, {len(out) // 2048} sectors')
log('E45: ' + ('PASS' if same and not bad else 'FAIL'))
open(os.path.join(HERE, '..', '..', '..', 'kb', 'e45-build-output.txt'), 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
