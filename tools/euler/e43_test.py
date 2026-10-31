"""E4.2a static test of patch_script_slot on Agumon (0499): new motion with a one-shot ending (-1) in the first free slot, script 5's last motion command pointed at it.
Checks: only the intended s16 differs, the script re-reads with the new slot, the motion archive/keys are untouched by the patch, every refusal leaves the input unchanged
(wrong expected slot, slot +-1 around the known one, empty slot, loop ending on a waiting script, face slot, wrong nth). Writes kb/e43-test-output.txt and viewer/data/e14/0499-e43.bin"""
import os, sys, hashlib
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..'))
import survey_keys as sk
from e7_compare import load_raw, pose_track
from e41_writer import add_motion
from e42_select import fighters, script_motions, u32, s16
from e43_script_patch import patch_script_slot, commands, ending

lines = []
def log(s): print(s); lines.append(s)
f = next(x for x in fighters() if x['model'] >> 16 == 499); ME, EE = f['motions'] & 0xFFFF, f['effects'] & 0xFFFF
path = os.path.join(HERE, '..', '..', 'data', 'files', '0499.bin'); raw = open(path, 'rb').read(); tr, mo = load_raw(path); nb = len(tr)
N = 30; lo = mo[0][0][2]; maxf = max(k[3] for b in tr for t in b for k in t if k[3] < 30000); F0 = (maxf // 10 + 2) * 10
def tracks_with(end_step):
    tk = {}
    for b in range(1, nb + 1):
        tk[b] = []
        for t in range(3):
            v = [pose_track(tr[b - 1][t], lo + i) for i in range(N)]
            tk[b].append([(*v[0], F0), (*v[0], F0 + N - 1)] if all(x == v[0] for x in v) else [(*x, F0 + i) for i, x in enumerate(v)])
    return tk
once = add_motion(raw, tracks_with(0), [(0, N, F0, F0 + N - 1), (N, 0, -1, 0)]); slot = add_motion.slot
loop = add_motion(raw, tracks_with(0), [(0, N, F0, F0 + N - 1), (N, 0, -32768, 0)])
log(f'new motion in slot {slot} (motion number {slot + 1}); script 5 commands: {[m for i, m, w, ok in commands(once, ME, EE) if i == 5]}')
h = hashlib.sha1(once).hexdigest()
out, info = patch_script_slot(once, ME, EE, 5, 4, 18, slot); log(f'patched: {info}')
diff = [i for i in range(len(once)) if once[i] != out[i]]
log(f'changed bytes {diff} (expected exactly the low byte of the s16 at {info["offset"]}): {diff == [info["offset"]]}; length equal {len(out) == len(once)}; input unchanged {hashlib.sha1(once).hexdigest() == h}')
mo2, eo2 = u32(out, ME * 4), u32(out, EE * 4); m, ok = script_motions(out, eo2 + u32(out, eo2 + 5 * 4), len(out))
log(f'script 5 re-read: slots {m} parses {ok}; ending of the target {ending(out, mo2, slot)}')
ok_all = diff == [info['offset']] and ok and m[-1] == slot and m[:-1] == [14, 15, 16, 17]
def refuse(name, fn):
    global ok_all
    try: fn(); r = False
    except AssertionError as e: r = True; name += f' ({e})'
    log(f'  refused: {r}  {name}'); ok_all &= r
refuse('expected slot 17 (known is 18)', lambda: patch_script_slot(once, ME, EE, 5, 4, 17, slot))
refuse('expected slot 19', lambda: patch_script_slot(once, ME, EE, 5, 4, 19, slot))
refuse('wrong nth (3 holds 17)', lambda: patch_script_slot(once, ME, EE, 5, 3, 18, slot))
refuse('nth beyond the script', lambda: patch_script_slot(once, ME, EE, 5, 5, 18, slot))
refuse('new slot = first free after the new one (no motion)', lambda: patch_script_slot(once, ME, EE, 5, 4, 18, slot + 1))
refuse('new slot 7 (free, no motion)', lambda: patch_script_slot(once, ME, EE, 5, 4, 18, 7))
refuse('new slot 1 (back-to-idle code)', lambda: patch_script_slot(once, ME, EE, 5, 4, 18, 1))
refuse('face slot 5 on a script with no waits', lambda: patch_script_slot(once, ME, EE, 10, 1, 45, 5))
refuse('loop motion on a waiting script', lambda: patch_script_slot(loop, ME, EE, 5, 4, 18, slot))
refuse('absent script 9', lambda: patch_script_slot(once, ME, EE, 9, 0, 18, slot))
log(f'loop motion on script 10 (no waits) accepted: {patch_script_slot(loop, ME, EE, 10, 1, 45, slot)[1]["ending"] == "loop"}')
open(os.path.join(HERE, '..', '..', 'data', 'e14', '0499-e43.bin'), 'wb').write(out)
log('E43 static test: ' + ('PASS' if ok_all else 'FAIL'))
open(os.path.join(HERE, '..', '..', '..', 'kb', 'e43-test-output.txt'), 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
