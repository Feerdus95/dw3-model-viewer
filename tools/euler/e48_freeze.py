"""E4.2 (writes ONE throw-away viewer fixture): 0526 legs fixture + every non-leg bone held at its REFERENCE (idle frame 10) over the idle range 10..40:
position and rotation tracks of bones 1-4 and 13-21 get one key per frame 10..40 = the original frame-10 value (so pelvis 1 and torso 13, siblings under the root, receive the SAME constant
translation - no Soldier Hips translation is transferred - and tail/head/arms stay in reference). Bones 5-12 keep the retargeted rotation keys of e48_legs. Keys outside 10..40 untouched.
Run: python e48_freeze.py -> viewer/data/e14/0526-e48-frozen.bin"""
import os, sys, struct
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..'))
import survey_keys as sk
from e7_compare import load_raw, pose_track
from e14_fixture import rebuild_keyarc
D = os.path.join(HERE, '..', '..', 'data'); O = os.path.join(D, 'files', '0526.bin'); S = os.path.join(D, 'e14', '0526-e48-legs.bin'); OUT = os.path.join(D, 'e14', '0526-e48-frozen.bin')
LO, HI = 10, 40; FREEZE = [b for b in range(1, 22) if not 5 <= b <= 12]
t0, _ = load_raw(O); raw = open(S, 'rb').read(); a = sk.Arc(raw); bi = sk.find_bone(a); e = a.off(bi); n = a.n()
entries = [raw[a.off(i):a.end(i)] for i in range(n)]; changed = {}
for b in FREEZE:
    ei = sk.u32(raw, e + 16 + (b - 1) * 12); kb = a.sub(ei).b
    for T in (0, 1):
        trk = sk.tracks(sk.Arc(kb))[T]; v = tuple(int(x) for x in pose_track(t0[b - 1][T], LO))
        keep = [k for k in trk if k[3] < 30000 and not LO <= k[3] <= HI]; sen = [k for k in trk if k[3] >= 30000]
        mixed = sorted(keep + [(*v, f) for f in range(LO, HI + 1)], key=lambda k: k[3])
        kb = rebuild_keyarc(kb, b''.join(struct.pack('<hhhh', *[int(x) for x in k]) for k in mixed + sen), T)
    changed[ei] = kb
head = a.off(0); pieces, offs, pos = [], [], head
for i in range(n):
    d = changed.get(i, entries[i]); offs.append(pos); pieces.append(d); pos += len(d)
out = struct.pack('<%dI' % n, *offs) + raw[4 * n:head] + b''.join(pieces); open(OUT, 'wb').write(out)
t1, m1 = load_raw(OUT); _, ms = load_raw(S); assert m1 == ms
bad = 0
for b in range(1, 22):
    for T in range(3):
        same = t1[b - 1][T] == load_raw(S)[0][b - 1][T]
        if b in FREEZE and T < 2: bad += any(pose_track(t1[b - 1][T], f) != pose_track(t0[b - 1][T], LO) for f in range(LO, HI + 1))
        elif not same: bad += 1
print(f'wrote {OUT} {len(out)} bytes; frozen bones {FREEZE}; wrong tracks {bad} (expected 0); the retargeted leg tracks and scale tracks are unchanged')
