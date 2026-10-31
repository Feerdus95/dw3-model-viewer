"""E1.4 (writes ONE throw-away file): Agumon 0499.bin with the rotation tracks of bones 4,5,6 replaced, in the native layout.
Run: python e14_fixture.py [OUT.bin]      default OUT = viewer/data/e14/0499-e14-abs.bin (data/ is git-ignored). The original is never touched.
The idle (motion 1) plays frames 10..69; in those tracks the keys with frame 10..69 are replaced by the E1.3 ABS keys (one per frame),
keys outside that range (other motions) stay byte for byte. Only the key archives of bones 4,5,6 are rebuilt (the rotation array is
swapped, the three other offsets shift); every other entry is copied verbatim and the entry table is re-based.
Then reads it back with the existing reader and checks: wanted rotations present, nothing else changed."""
import os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, ".."))
import survey_keys as sk
from e7_compare import load_raw
from survey_real import read_motions
from e13_arm import main as retarget

SRC = os.environ.get('E14_SRC', os.path.join(HERE, '..', '..', 'data', 'files', '0499.bin'))
REF = os.environ.get('E14_REF', os.path.join(HERE, '..', '..', 'data', 'files', '0499.bin'))      # reference pose for the retargeters (the original, even when chaining fixtures)
LO, HI = 10, 69


def rebuild_keyarc(ka_bytes, new_rot, idx=1):
    """key archive: u32 offsets (n of them), then arrays; track 1 = rotation. Replace its array, keep every other byte."""
    ka = sk.Arc(ka_bytes); n = ka.n(); offs = [ka.off(i) for i in range(n)]
    assert n >= 3 and offs == sorted(offs) and offs[0] == 4 * n
    old_len = offs[idx + 1] - offs[idx]
    delta = len(new_rot) - old_len
    hdr = struct.pack('<%dI' % n, *[o + (delta if i >= idx + 1 else 0) for i, o in enumerate(offs)])
    return hdr + ka_bytes[offs[0]:offs[idx]] + new_rot + ka_bytes[offs[idx + 1]:]


def main(out, anim='Idle', n_frames=60, dedup=False, patch_motion=False, side='Left', limb='arm', variant='ABS'):
    bones = {('arm', 'Left'): (4, 5, 6), ('arm', 'Right'): (7, 8, 9), ('leg', 'Left'): (11, 12, 13, 14), ('leg', 'Right'): (15, 16, 17, 18), ('torso', side): (1,), ('pelvis', side): (10,), ('bob', side): (1, 10), ('still', side): (1, 10)}[(limb, side)]
    HI = LO + n_frames - 1; T = 0 if limb in ('bob', 'still') else 1      # track edited: 0 = position, 1 = rotation
    import contextlib, io
    with contextlib.redirect_stdout(io.StringIO()):
        if limb == 'leg':
            from e17_leg import main as retarget
        else:
            from e13_arm import main as retarget
        sg = os.path.join(HERE, '..', '..', '..', 'kb', 'mixamo', 'Soldier.glb')
        if limb in ('bob', 'still'):
            from e25_bob import bob_keys; kk = bob_keys(sg, REF, anim, dedup, limb == 'still'); res = {variant: [kk, kk]}
        elif limb in ('torso', 'pelvis'):
            from e20_body import body_keys; res = {variant: body_keys(sg, REF, anim, dedup, limb, variant)[0]}
        else: res = retarget(sg, REF, anim, dedup, side)
    new = res[variant]                                    # [bone4, bone5, bone6] -> 60 triples
    assert len(new[0]) == HI - LO + 1
    raw = open(SRC, 'rb').read(); a = sk.Arc(raw); bi = sk.find_bone(a); e = a.off(bi)
    entries = [raw[a.off(i):a.end(i)] for i in range(a.n())]; changed = {}
    for k, b in enumerate(bones):
        ei = sk.u32(raw, e + 16 + (b - 1) * 12)
        ka = a.sub(ei); rot = sk.tracks(ka)[T]
        keep = [t for t in rot if t[3] < 30000 and not (LO <= t[3] <= HI)]
        mixed = sorted(keep + [(*new[k][f], LO + f) for f in range(HI - LO + 1)], key=lambda t: t[3])
        sentinel = [t for t in rot if t[3] >= 30000]
        arr = b''.join(struct.pack('<hhhh', *[int(v) for v in t]) for t in mixed + sentinel)
        changed[ei] = rebuild_keyarc(ka.b, arr, T)
        assert len({t[3] for t in mixed}) == len(mixed), 'duplicate frames'
    # re-lay the file: same order, same table, shifted offsets
    n = a.n(); head = a.off(0); assert head == 4 * n or head > 4 * n
    pieces, newoffs, pos = [], [], head
    for i in range(n):
        data = changed.get(i, entries[i]); newoffs.append(pos); pieces.append(data); pos += len(data)
    if patch_motion:     # the idle (motion 1) shortened to n_frames: step 0 = (0, n, LO, HI), loop step = (n, 0, 0x8000, 0)
        mi = next(i for i in range(n) if i not in (bi, sk.u32(raw, e)) and a.off(i) and read_motions(a.sub(i)))
        buf = bytearray(pieces[mi]); p = sk.Arc(bytes(buf)).off(0)
        assert struct.unpack_from('<hhhh', buf, p) == (0, 60, 10, 69) and struct.unpack_from('<hhhh', buf, p + 8) == (60, 0, -32768, 0), 'unexpected motion 1 steps'
        struct.pack_into('<hhhh', buf, p, 0, n_frames, LO, HI); struct.pack_into('<h', buf, p + 8, n_frames); pieces[mi] = bytes(buf)
    table = struct.pack('<%dI' % n, *newoffs) + raw[4 * n:head]        # keep any header bytes after the table
    out_b = table + b''.join(pieces)
    os.makedirs(os.path.dirname(out), exist_ok=True); open(out, 'wb').write(out_b)
    print(f'wrote {out}: {len(out_b)} bytes (original {len(raw)}); rebuilt key archives of entries {sorted(changed)}')
    # round trip with the existing reader
    t0, m0 = load_raw(SRC); t1, m1 = load_raw(out)
    if patch_motion:     # blend-script motions run on into the idle's steps (shared tail), so they change by exactly the patched steps
        fix = {(0, 60, 10, 69): (0, n_frames, LO, HI), (60, 0, -32768, 0): (n_frames, 0, -32768, 0)}
        assert len(m0) == len(m1) and all([fix.get(s, s) for s in x] == y for x, y in zip(m0, m1)), 'motion table changed beyond the idle steps'
        print(f'motion table: only the idle steps changed ({sum(m0[i] != m1[i] for i in range(len(m0)))} motions run through them)')
    else: assert m0 == m1, 'motion table changed'
    bad = 0; other = 0
    for b in range(len(t0)):
        for t in range(3):
            same = t0[b][t] == t1[b][t]
            if b + 1 in bones and t == T:
                got = {k[3]: k[:3] for k in t1[b][t] if k[3] < 30000}
                for f in range(HI - LO + 1):
                    if got.get(LO + f) != tuple(int(v) for v in new[bones.index(b + 1)][f]): bad += 1
                outside = [k for k in t0[b][t] if k[3] < 30000 and not (LO <= k[3] <= HI)]
                assert all(got.get(k[3]) == k[:3] for k in outside), 'keys outside the idle range changed'
            elif not same: other += 1
    print(f'round trip: wanted rotation keys wrong {bad} of {3 * (HI - LO + 1)}; other tracks changed {other}; keys outside frames {LO}-{HI} on bones 4,5,6 unchanged')
    a1 = sk.Arc(out_b)
    same_entries = sum(1 for i in range(n) if a1.sub(i).b == entries[i]); print(f'entries identical to the original: {same_entries} of {n} (expected {n - len(changed)})')


if __name__ == '__main__':
    a = sys.argv[1:]      # OUT [ANIM N DEDUP PATCH]; default = the E1.4 idle fixture
    main(a[0] if a else os.path.join(HERE, '..', '..', 'data', 'e14', '0499-e14-abs.bin'), *( [a[1], int(a[2]), a[3] == '1', a[4] == '1'] + a[5:8] if len(a) > 4 else [] ))
