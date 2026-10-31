"""E4.1: write a NEW motion into a DW3 model archive without touching the existing ones, and prove it.
add_motion(raw, tracks, steps) -> bytes
  tracks[b][t] = new keys (x, y, z, frame) for bone b (viewer number, 1..n) and track t (0 pos, 1 rot, 2 scale); every bone and track must be given (a track with no keys in the new frame
  range would interpolate towards its sentinel key, see e41_limits.py); frames must lie above every existing real key of that track and below 30000, ascending.
  steps = MotionStep tuples (index, count, frame, endFrame) WITHOUT the 0x7FFF terminator (added here).
The writer appends the keys of each track after its last real key (before the sentinel), appends the steps as a new entry at the end of the motion archive (table grows by one u32, the existing steps are byte-identical,
their table offsets move by 4; when the table has an unused slot (offset 0) that slot is reused instead and nothing moves), re-lays the file keeping the entries' original order and the file-table order (not always ascending), pads to a multiple of 2048 (the disc sector). Returns the new file; the new motion number is
motions+1. The time contract is the caller's (frames and steps are written as given); no fps conversion here.
Run as a script for the E2.8 regression: python e41_writer.py [OUT.bin] -> takes the Agumon E2.8 fixture's idle motion data (frames 10..40, all bones/tracks) and writes it as a new motion into the ORIGINAL 0499, then proves
(1) the original entries are byte-identical, (2) the new motion plays, tick for tick, exactly as the fixture's motion 1, (3) every other motion plays as before, (4) the report of limits."""
import os, sys, struct
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, ".."))
import survey_keys as sk
from e7_compare import load_raw, Player, pose_track
from e8_resample import run_ticks
from survey_real import read_motions, played_frames

NUL = bytes(1); SECTOR = 2048; MAX_TICKS = 0x640


def ticks(steps):
    t = 0
    for s in steps:
        if s[0] == 0x7FFF: break
        t += s[1] if s[1] else 1
        if s[1] == 0: break
    return t


def pack_keys(keys): return b''.join(struct.pack('<hhhh', *[int(v) for v in k]) for k in keys)


def add_motion(raw, tracks, steps):
    a = sk.Arc(raw); n = a.n(); bi = sk.find_bone(a); e = a.off(bi); tex = sk.u32(raw, e); cnt = sk.u32(raw, e + 4)
    offs = [a.off(i) for i in range(n)]
    assert sorted(tracks) == list(range(1, cnt + 1)) and all(len(tracks[b]) == 3 for b in tracks), 'every bone and track needs keys'
    assert ticks(steps) <= MAX_TICKS, f'motion of {ticks(steps)} ticks exceeds the engine arrays ({MAX_TICKS})'
    assert steps[0][0] == 0 and all(s[0] != 0 for s in steps[1:]), 'first step index 0, the others non-zero'
    played = played_frames(read_motions(a.sub(next(i for i in range(n) if i not in (bi, tex) and offs[i] and read_motions(a.sub(i)))))); bridged = []
    new = {}
    for b in range(1, cnt + 1):
        ei = sk.u32(raw, e + 16 + (b - 1) * 12); ka = a.sub(ei); m = ka.n(); ko = [ka.off(i) for i in range(m)]
        assert m >= 3 and ko == sorted(ko) and ko[0] == 4 * m, 'unexpected key archive layout'
        pieces = [ka.b[ko[i]:(ko[i + 1] if i + 1 < m else len(ka.b))] for i in range(m)]
        for t in range(3):
            ks = tracks[b][t]; fr = [k[3] for k in ks]
            assert ks and fr == sorted(fr) and len(set(fr)) == len(fr) and 0 <= fr[0] and fr[-1] < 30000, f'bone {b} track {t}: bad frames'
            p = pieces[t]; q = 0
            while q + 8 <= len(p) and struct.unpack_from('<h', p, q + 6)[0] < 30000: q += 8
            assert q + 8 <= len(p), f'bone {b} track {t}: no sentinel'
            last = struct.unpack_from('<h', p, q - 2)[0] if q else -1
            assert fr[0] > last, f'bone {b} track {t}: new frame {fr[0]} not above the last key {last}'
            # bridge: frames some existing motion plays above this track's last real key were interpolated towards the sentinel; a new key would change them, so they get explicit keys with the old value
            allk = [struct.unpack_from('<hhhh', p, o) for o in range(0, q + 8, 8)]
            bridge = [(*pose_track(allk, f), f) for f in played if f > last and f < fr[0]]
            bridge = [k for i, k in enumerate(bridge) if i in (0, len(bridge) - 1) or k[:3] != bridge[i - 1][:3] or k[:3] != bridge[i + 1][:3]]      # runs of equal values keep their two ends (interpolation inside a run is a hold)
            if bridge: bridged.append((b, t, len(bridge)))
            pieces[t] = p[:q] + pack_keys(bridge + list(ks)) + p[q:]
        hdr, pos, body = [], 4 * m, b''
        for pc in pieces: hdr.append(pos); pos += len(pc); body += pc
        new[ei] = struct.pack('<%dI' % m, *hdr) + body
    # motion archive: find it as the readers do
    mi = next(i for i in range(n) if i not in (bi, tex) and offs[i] and read_motions(a.sub(i)))
    ma = a.sub(mi); mn = ma.n(); mo = [ma.off(i) for i in range(mn)]; assert mo[0] == 4 * mn, 'unexpected motion archive layout'
    step_bytes = b''.join(struct.pack('<hhhh', *s) for s in list(steps) + [(0x7FFF, 0, 0, 0)])
    free = [i for i, o in enumerate(mo) if o == 0]
    if free:       # reuse the first unused slot (offset 0): the table does not grow, no existing offset moves; the new motion number is slot + 1
        last_used = max((i for i, o in enumerate(mo) if o), default=-1); slot = next((i for i in free if i > last_used), free[0]); tbl = list(mo); tbl[slot] = len(ma.b)
        new[mi] = struct.pack('<%dI' % mn, *tbl) + ma.b[4 * mn:] + step_bytes
    else:          # append: the table grows by one u32, the used offsets move by 4
        slot = mn
        new[mi] = struct.pack('<%dI' % (mn + 1), *([o + 4 if o else 0 for o in mo] + [len(ma.b) + 4])) + ma.b[4 * mn:] + step_bytes
    add_motion.slot = slot; add_motion.bridged = bridged
    # re-lay the file: entries keep their order by original position; sizes from the next greater offset
    order = sorted((i for i in range(n) if offs[i]), key=lambda i: (offs[i], i)); head = min(o for o in offs if o) if offs else 4 * n      # offset 0 = absent entry: stays 0, is not re-laid
    assert head == 4 * n or head > 4 * n
    newoffs, out, pos = [0] * n, [], head
    for i in order:
        data = new.get(i, raw[offs[i]:a.end(i)])
        pad = (-len(data)) % 4; data += b'\0' * pad
        newoffs[i] = pos; out.append(data); pos += len(data)
    blob = struct.pack('<%dI' % n, *newoffs) + raw[4 * n:head] + b''.join(out)
    blob += b'\0' * ((-len(blob)) % SECTOR)
    return blob


def player_poses(path, motion, count):
    tr, mo = load_raw(path); pl = Player(tr, mo); snaps = run_ticks_one(pl, motion, count)
    return snaps, mo


def run_ticks_one(pl, m, count):
    pl.set_motion(m); out = []
    for _ in range(count):
        out.append([list(map(tuple, p)) for p in pl.pose])
        if pl.done: break
        pl.advance(); pl.evaluate()
    return out


def main(out):
    base = os.path.join(HERE, '..', '..', 'data'); orig = os.path.join(base, 'files', '0499.bin'); fix = os.path.join(base, 'e14', '0499-e28-flat-both.bin')
    raw = open(orig, 'rb').read(); tr_o, mo_o = load_raw(orig); tr_f, mo_f = load_raw(fix); LO, HI = 10, 40; nb = len(tr_o)
    maxf = max(k[3] for b in tr_o for t in b for k in t if k[3] < 30000); F0 = (maxf // 10 + 2) * 10; N = HI - LO + 1
    tracks = {}; full = hold = 0
    for b in range(1, nb + 1):
        tracks[b] = []
        for t in range(3):
            vals = [pose_track(tr_f[b - 1][t], f) for f in range(LO, HI + 1)]
            if all(v == vals[0] for v in vals): ks = [(*vals[0], F0), (*vals[0], F0 + N - 1)]; hold += 1
            else: ks = [(*v, F0 + i) for i, v in enumerate(vals)]; full += 1
            tracks[b].append(ks)
    steps = [(0, N, F0, F0 + N - 1), (N, 0, -32768, 0)]
    blob = add_motion(raw, tracks, steps); open(out, 'wb').write(blob)
    a0, a1 = sk.Arc(raw), sk.Arc(blob); print(f'wrote {out}: {len(blob)} bytes (original {len(raw)}), new frames {F0}..{F0 + N - 1}, tracks full {full} hold {hold}, new motion number {add_motion.slot + 1}')
    # (1) entries: untouched ones byte-identical, edited ones differ only by the appended data
    e0 = a0.off(sk.find_bone(a0)); changed = {sk.u32(raw, e0 + 16 + b * 12) for b in range(nb)}
    mi = next(i for i in range(a0.n()) if i not in (sk.find_bone(a0), sk.u32(raw, e0)) and a0.off(i) and read_motions(a0.sub(i))); changed.add(mi)
    same = sum(1 for i in range(a0.n()) if i not in changed and a0.sub(i).b.rstrip(NUL) == a1.sub(i).b.rstrip(NUL))
    print(f'(1) entries {a0.n()}: untouched and byte-identical {same} of {a0.n() - len(changed)}; edited {len(changed)} (key archives + motion archive)')
    # key archives: every original real key still there, bytewise, in the same order
    ok = True
    for b in range(nb):
        for t in range(3):
            k0 = [k for k in tr_o[b][t] if k[3] < 30000]; k1 = load_raw(out)[0][b][t] if False else None
    tr_n, mo_n = load_raw(out); maxplayed = max(played_frames(mo_o))
    for b in range(nb):
        for t in range(3):
            k0 = [k for k in tr_o[b][t] if k[3] < 30000]; k1 = [k for k in tr_n[b][t] if k[3] < 30000]
            ok &= k1[:len(k0)] == k0 and all(k[3] > k0[-1][3] and (k[3] >= F0 or k[3] <= maxplayed) for k in k1[len(k0):])
    print(f'(1b) original keys preserved as a prefix of every track, new keys only bridge keys (<= last played frame, old values) or the new motion (>= {F0}): {ok}')
    used = [i for i in range(len(mo_o)) if a0.sub(mi).off(i) and i != add_motion.slot]
    print(f'(1c) motion table: {len(mo_o)} -> {len(mo_n)} slots; used motions {len(used)}, their steps identical: {all(mo_n[i] == mo_o[i] for i in used)}; new motion {add_motion.slot + 1} steps {mo_n[add_motion.slot]}')
    # (2) the new motion plays as the fixture's motion 1; (3) the others as before
    sn, _ = player_poses(out, add_motion.slot + 1, N + 1); sf, _ = player_poses(fix, 1, N + 1)
    print(f'(2) new motion vs fixture motion 1, {N} ticks, all bones and tracks identical: {sn[:N] == sf[:N]}; tick {N} (after the loop) equals tick 0: {sn[N] == sn[0]}' if len(sn) > N else f'(2) ticks played {len(sn)}: {sn[:N] == sf[:N]}')
    bad = 0
    for m in [i + 1 for i in used]:
        pa, _ = player_poses(orig, m, 1700); pb, _ = player_poses(out, m, 1700)
        if pa != pb: bad += 1; print('   differs: motion', m, [(t, sum(1 for x, y in zip(pa[t], pb[t]) if x != y)) for t in range(min(len(pa), len(pb))) if pa[t] != pb[t]][:3], len(pa), len(pb))
    print(f'(3) the {len(used)} original used motions play identically (full length): differing {bad}; tracks that needed bridge keys: {add_motion.bridged}')
    print(f'(4) limits: ticks of the new motion {ticks(steps)} (engine {MAX_TICKS}); file {len(blob)} bytes = {len(blob) // SECTOR} sectors (original {len(raw) // SECTOR}); frames < 30000 and below 0x8000: {F0 + N - 1}')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', 'data', 'e14', '0499-e41-newmotion.bin'))
