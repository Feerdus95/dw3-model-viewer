"""E4.2a (separate from add_motion): patch ONE s16 of a battle script so an op 2 / cmd 0 command plays another motion SLOT.
Naming: SLOT = index in the motion table = motion number - 1 = the script's `arg` (the game plays motion arg+1). Only slots are accepted here.
Refuses (nothing written) unless: the word is an op-2/cmd-0 command's arg, it is the n-th such command of the named script, its current value is the expected slot,
the new slot has a motion, the motion ends in a way the script can use (a script that waits needs a motion that ends with -1 or blends to idle; a loop would hang it),
and the slot does not change the face (slots 2 and 4..11, unless allow_face). Run: python e43_script_patch.py FID [list]"""
import os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from e42_select import script_motions, fighters, u32, s16

FACE_SLOTS = {2} | set(range(4, 12))    # fightstg step_face: eyes closed for slots 4..11, half for slot 2


def locate(b, fid_entries):
    me, ee = fid_entries
    return u32(b, me * 4), u32(b, ee * 4)


def ending(b, mo, slot):
    """'loop' | 'end' | 'idle' | 'other' for the motion at the slot (steps end at index 0x7FFF)"""
    p = mo + u32(b, mo + slot * 4); steps = []
    while p + 8 <= len(b):
        s = struct.unpack_from('<hhhh', b, p); steps.append(s); p += 8
        if s[0] == 0x7FFF: break
    steps = steps[:-1]
    if not steps: return 'other'
    i, n, fr, en = steps[-1]
    if n == 0 and fr == -32768: return 'loop'
    if n == 0 and fr == -1: return 'end'
    return 'other'


def patch_script_slot(b, motion_entry, effects_entry, script, nth, expect_slot, new_slot, allow_face=False):
    """returns (new bytes, info); raises AssertionError without having changed anything"""
    mo, eo = locate(b, (motion_entry, effects_entry)); n = u32(b, mo) // 4
    assert 0 < mo < len(b) and 0 < eo < len(b), 'entries out of the file'
    so = u32(b, eo + script * 4); assert so, 'script is absent'
    named, ok = script_motions(b, eo + so, len(b)); pos = list(script_motions.pos); waits = script_motions.waits
    assert ok, 'script does not parse'
    assert 0 <= nth < len(pos), f'script has {len(pos)} motion commands, asked for #{nth}'
    q = pos[nth]; assert q + 2 <= len(b)
    assert s16(b, q - 6) == 2 and s16(b, q - 4) == 0, 'not an op 2 / cmd 0 command'
    assert s16(b, q) == expect_slot, f'current slot is {s16(b, q)}, expected {expect_slot}'
    assert 2 <= new_slot < n and u32(b, mo + new_slot * 4), 'the new slot has no motion (or is an idle/wait/back-to-idle code)'
    end = ending(b, mo, new_slot)
    assert not (waits and end == 'loop'), 'the script waits for the motion to end and the new motion loops'
    assert allow_face or new_slot not in FACE_SLOTS, 'this slot changes the face (eyes); pass allow_face to accept'
    out = bytearray(b); struct.pack_into('<h', out, q, new_slot)
    return bytes(out), dict(offset=q, script=script, nth=nth, old=expect_slot, new=new_slot, ending=end, waits=waits)


def commands(b, motion_entry, effects_entry):
    mo, eo = locate(b, (motion_entry, effects_entry)); res = []
    for i in range(u32(b, eo) // 4):
        so = u32(b, eo + i * 4)
        if so:
            m, ok = script_motions(b, eo + so, len(b)); res.append((i, m, script_motions.waits, ok))
    return res


if __name__ == '__main__':
    fid = int(sys.argv[1]); f = next(x for x in fighters() if x['model'] >> 16 == fid)
    b = open(os.path.join(HERE, '..', '..', 'data', 'files', f'{fid:04d}.bin'), 'rb').read()
    for i, m, w, ok in commands(b, f['motions'] & 0xFFFF, f['effects'] & 0xFFFF): print(f'script {i}: slots {m} waits {w} parses {ok}')
