"""E4.1 corpus test (no retargeting): for each model write a synthetic NEW motion (a copy of the model's own idle frames, as new keys at new frames) with e41_writer.add_motion and check the format-level criteria:
roundtrip, untouched entries byte-identical, original keys a prefix, every used original motion plays identically (full length), the new motion plays like the source frames, free slots / unsorted file table handled,
determinism (same input twice -> same bytes), the original file untouched, explicit failures (1601 ticks, frames out of range, no capacity). Run: python e41_corpus.py [IDs...] ; writes kb/e41-corpus-output.txt"""
import os, sys, hashlib
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, ".."))
import survey_keys as sk
from e7_compare import load_raw, pose_track
from survey_real import read_motions, played_frames
from e41_writer import add_motion, player_poses, ticks, SECTOR


def one(mid, out_dir, log):
    path = os.path.join(HERE, '..', '..', 'data', 'files', f'{mid}.bin'); raw = open(path, 'rb').read(); h0 = hashlib.sha1(raw).hexdigest()
    tr, mo = load_raw(path); nb = len(tr); a = sk.Arc(raw); bi = sk.find_bone(a); e = a.off(bi); tex = sk.u32(raw, e)
    mi = next(i for i in range(a.n()) if i not in (bi, tex) and a.off(i) and read_motions(a.sub(i)))
    ma = a.sub(mi); used = [i for i in range(len(mo)) if ma.off(i)]
    s0 = mo[0][0]; lo = s0[2] if s0[1] > 0 else 0; N = 30; fl = [f for f in range(lo, lo + N)]
    maxf = max([k[3] for b in tr for t in b for k in t if k[3] < 30000] or [0]); F0 = (maxf // 10 + 2) * 10
    tracks = {}
    for b in range(1, nb + 1):
        tracks[b] = []
        for t in range(3):
            vals = [pose_track(tr[b - 1][t], f) for f in fl]
            tracks[b].append([(*vals[0], F0), (*vals[0], F0 + N - 1)] if all(v == vals[0] for v in vals) else [(*v, F0 + i) for i, v in enumerate(vals)])
    steps = [(0, N, F0, F0 + N - 1), (N, 0, -32768, 0)]
    res = {}
    try:
        blob = add_motion(raw, tracks, steps); slot = add_motion.slot; bridged = len(add_motion.bridged)
    except Exception as ex:
        log(f'{mid}: writer refused: {ex}'); return False
    out = os.path.join(out_dir, f'{mid}-e41.bin'); open(out, 'wb').write(blob)
    a1 = sk.Arc(blob); tr1, mo1 = load_raw(out)
    changed = {sk.u32(raw, e + 16 + b * 12) for b in range(nb)} | {mi}
    res['entries identical'] = all(a.sub(i).b.rstrip(bytes(1)) == a1.sub(i).b.rstrip(bytes(1)) for i in range(a.n()) if i not in changed and a.off(i))
    res['key prefix'] = all([k for k in tr1[b][t] if k[3] < 30000][:len([k for k in tr[b][t] if k[3] < 30000])] == [k for k in tr[b][t] if k[3] < 30000] for b in range(nb) for t in range(3))
    res['table steps'] = all(mo1[i] == mo[i] for i in used if i != slot)
    bad = 0; skipped = []
    for m in [i + 1 for i in used if i != slot]:
        try: pa, _ = player_poses(path, m, 1700)
        except Exception: skipped.append(m); continue          # the E7 Player cannot play this ORIGINAL motion: not a writer matter, recorded
        pb, _ = player_poses(out, m, 1700); bad += pa != pb
    res['originals identical'] = bad == 0
    sn, _ = player_poses(out, slot + 1, N); res['new motion = source frames'] = len(sn) == N and all(sn[i][b][t] == pose_track(tr[b][t], fl[i] + 1) for i in range(N - 1) for b in range(nb) for t in range(3))      # tick i shows frame first+i+1 (the engine starts at keyframe 1)
    res['deterministic'] = add_motion(raw, tracks, steps) == blob
    res['original untouched'] = hashlib.sha1(open(path, 'rb').read()).hexdigest() == h0
    # limits and refusals
    def refuses(st, tk=tracks):
        try: add_motion(raw, tk, st); return False
        except AssertionError: return True
    ok1599 = False
    try:
        t1599 = {b: [[(*k[:3], F0 + i) for i, k in enumerate([tracks[b][t][0]] + [tracks[b][t][-1]])] if False else [(*tracks[b][t][0][:3], F0), (*tracks[b][t][-1][:3], F0 + 1598)] for t in range(3)] for b in tracks}
        add_motion(raw, t1599, [(0, 1599, F0, F0 + 1598), (1599, 0, -32768, 0)]); ok1599 = True       # 1599 play ticks + the loop step = 1600
    except AssertionError: pass
    res['1600 ticks accepted'] = ok1599
    res['1601 ticks refused'] = refuses([(0, 1600, F0, F0 + 1599), (1600, 0, -32768, 0)], {b: [[(*tracks[b][t][0][:3], F0), (*tracks[b][t][-1][:3], F0 + 1599)] for t in range(3)] for b in tracks})
    res['frame >= 30000 refused'] = refuses(steps, {b: [[(*k[:3], 30000 + i) for i, k in enumerate(tracks[b][t])] for t in range(3)] for b in tracks})
    res['frame below last key refused'] = refuses(steps, {b: [[(*k[:3], 0 + i) for i, k in enumerate(tracks[b][t])] for t in range(3)] for b in tracks})
    res['missing bone refused'] = refuses(steps, {b: tracks[b] for b in list(tracks)[:-1]})
    unsorted = a.n() and [a.off(i) for i in range(a.n())] != sorted(a.off(i) for i in range(a.n()))
    log(f"{mid}: {'PASS' if all(res.values()) else 'FAIL'} | bones {nb} entries {a.n()} unsorted table {unsorted} used motions {len(used)} slot {slot + 1} bridged tracks {bridged} sectors {len(raw) // SECTOR}->{len(blob) // SECTOR} originals unplayable in the E7 Player {len(skipped)} | " + ', '.join(f'{k}={v}' for k, v in res.items() if not v))
    return all(res.values())


if __name__ == '__main__':
    ids = sys.argv[1:] or ['0499', '0526', '0581', '1547', '0879', '0873']
    out_dir = os.path.join(HERE, '..', '..', 'data', 'e14'); lines = []
    def log(s): print(s); lines.append(s)
    ok = [one(m, out_dir, log) for m in ids]
    log(f'corpus: {sum(ok)} of {len(ok)} pass')
    open(os.path.join(HERE, '..', '..', '..', 'kb', 'e41-corpus-output.txt'), 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
