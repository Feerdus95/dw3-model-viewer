"""E9 (synthetic, in memory only): winding at clip boundaries and DW3's blend. Run: python e9_winding.py   (writes nothing)

What the engine does at a boundary (model.cpp, see the handoff section 18): a blend step lerps, per component and per track,
   prev + (int16)((int16)(cur - prev) * t >> 12)
from PREV = the stored triple of the source pose (the carried pose, or pose_track(source frame)) to CUR = the stored triple of the
FIRST KEY WITH frame >= target frame (not an interpolated pose), with t = ((i+1)<<12)/(n+1). Only integer frames are ever posed and
only those two triples enter the blend, so with one key per frame the stored triple of a boundary key can be replaced by any
equivalent triple (same rotation) at no cost to normal playback: the discontinuity sits between two consecutive frames, where no
played frame lies.

Variants (the converter, the format, the models and the player are untouched; edits are in memory, on OUR clip's boundary key only):
  V0   clip as the existing converter leaves it: continuous winding from the clip's seed (plus the winding offset under test)
  L1   boundary key replaced by the equivalent triple (2 Euler families x +-1 turn per angle) nearest to its counterpart (min sum|d|)
  DEV  the equivalent triple whose DW3 blend path stays closest to the ideal spherical blend (min of the max geodesic deviation)
Ideal (reference only, never played): slerp(source rotation, target rotation, t/4096) per bone, same t schedule.
Chain of 6 bones (parent = previous), 300 units apart; tip = last bone's world position (straight-chain length 1500)."""
import itertools, math, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, ".."))
from euler_zyx import compose, geodesic, slerp, candidates, TAU, GIMBAL_EPS
from dw3_euler import UNITS, key_matrix, rotations_to_keys, dw3_lerp, wrap16, to_units, from_units
from e7_compare import Player, pose_track

D = 180 / math.pi
NB = 6
PAR = list(range(NB))          # viewer parent numbers: bone b+1 hangs from bone b (0 = control block)
OFF = [(0, 0, 0)] + [(0, 300, 0)] * (NB - 1)
LEN = 1500.0
NT = 10                        # blend ticks


# ---------------------------------------------------------------- synthetic clips
def smooth(u): return u * u * (3 - 2 * u)


def clip_angles(kind, b, u):
    """radians (x, y, z) of bone b at progress u in [0,1]"""
    k = 0.5 + 0.1 * b
    if kind == 'idle':
        a = 0.08 * (1 + 0.1 * b); ph = 2 * math.pi * u + b
        return (a * math.sin(ph), 0.5 * a * math.cos(ph), 0.7 * a * math.sin(ph + 1))
    if kind == 'attack':   s = smooth(u); return tuple(p + s * (q - p) for p, q in zip((0.4 * k, -0.3 * k, 0.5 * k), (1.0 * k, -0.9 * k, 1.2 * k)))
    if kind == 'attack2':  s = smooth(u); return tuple(p + s * (q - p) for p, q in zip((1.0 * k, -0.9 * k, 1.2 * k), (-0.5 * k, 0.6 * k, -0.8 * k)))
    if kind == 'spin':     s = smooth(u); return (-TAU * s * 0.5 * (b % 2) + 0.2 * k * s, 0.1 * k * s, TAU * s + 0.3 * k * s)   # z net +1 turn, odd bones x net -0.5 turn
    if kind == 'gout':     s = smooth(u); return (0.6, s * (math.pi / 2 - 0.01), 0.9)                         # ends 0.57 deg short of the pole
    if kind == 'gin':      s = smooth(1 - u); return (0.6, s * (math.pi / 2 - 0.01), 0.9)                     # starts there
    if kind == 'gexact':   s = smooth(u); return (0.6, s * (math.pi / 2), 0.9)                               # ends exactly on the pole
    raise KeyError(kind)


FRAMES = {'idle': (10, 30), 'attack': (100, 40), 'attack2': (200, 40), 'spin': (300, 40), 'gout': (400, 40), 'gin': (500, 40), 'gexact': (600, 40)}


def make_keys(kind, b, off=(0, 0, 0)):
    f0, n = FRAMES[kind]
    Rs = [compose(*clip_angles(kind, b, i / (n - 1))) for i in range(n)]
    c0 = candidates(Rs[0])[0][1]
    start = tuple(to_units(a) + o for a, o in zip(c0, off))
    ks = rotations_to_keys(Rs, start)
    return [(k[0], k[1], k[2], f0 + i) for i, k in enumerate(ks)]


def make_tracks(kinds, offs):
    """tr[b] = [pos, rot, scale] track key lists; OFFS maps a kind to a per-bone list of winding offsets (units)."""
    tr = []
    for b in range(NB):
        rot = []
        for kind in kinds:
            o = offs.get(kind, [(0, 0, 0)] * NB)[b]
            rot += make_keys(kind, b, o)
        rot.sort(key=lambda k: k[3]); rot.append((0, 0, 0, 30000))
        tr.append([[(*OFF[b], 0), (*OFF[b], 30000)], rot, [(4096, 4096, 4096, 0), (4096, 4096, 4096, 30000)]])
    return tr


def script(*segs):
    """('play', kind) | ('blend',) | ('playn', kind, n) -> a motion in the viewer's step format (idx, count, frame, end)"""
    steps, idx = [], 0
    for s in segs:
        if s[0] == 'blend': steps.append((idx, NT, 0, 0)); idx += NT
        else:
            f0, n = FRAMES[s[1]]; n = s[2] if len(s) > 2 else n
            steps.append((idx, n, f0, f0 + n - 1)); idx += n
    steps.append((idx, 0, -1, 0)); steps.append((0x7FFF, 0, 0, 0))
    return steps


# ---------------------------------------------------------------- running and measuring
def run_motion(pl, mi):
    pl.set_motion(mi); snaps = []
    for _ in range(1700):
        snaps.append(dict(pose=list(pl.pose), frame=pl.frame, bl=pl.blending, blend=pl.blend if pl.blending else None,
                          prev=list(pl.prev) if pl.blending else None))
        if pl.done or pl.keyframe >= pl.count - 1: break
        pl.advance(); pl.evaluate()
    return snaps


def tip(mats, poss):
    W = np.eye(4)
    for R, p in zip(mats, poss):
        L = np.eye(4); L[:3, :3] = R; L[:3, 3] = p; W = W @ L
    return W[:3, 3]


def measure(tr, motions):
    """Play all motions in order (pose carried over); measure the LAST motion's blend ticks against the ideal spherical blend."""
    pl = Player(tr, motions);
    for mi in range(1, len(motions) + 1): snaps = run_motion(pl, mi)
    dev, turns, tipe, n = 0.0, 0.0, 0.0, 0
    for s in snaps:
        if not s['bl']: continue
        n += 1; t = s['blend']
        idl, act, poss = [], [], []
        for b in range(NB):
            f = s['frame']; cur = next(k for k in tr[b][1] if k[3] >= f)
            Rs, Rt = key_matrix(s['prev'][b][1]), key_matrix(cur[:3])
            ideal = slerp(Rs, Rt, t / 4096); a = key_matrix(s['pose'][b][1])
            dev = max(dev, geodesic(a, ideal) * D); idl.append(ideal); act.append(a); poss.append(s['pose'][b][0])
            turns = max(turns, max(abs(wrap16(c - p)) for c, p in zip(cur[:3], s['prev'][b][1])) / 4096)
        tipe = max(tipe, float(np.linalg.norm(tip(act, poss) - tip(idl, poss))))
    return dict(dev=dev, turns=turns, tip=tipe, ticks=n, snaps=snaps)


# ---------------------------------------------------------------- equivalent triples
def equivalents(u, near):
    """Every triple with the rotation of U: both Euler families, +-1 turn per angle around the one nearest NEAR (units)."""
    R = key_matrix(u); prev = tuple(from_units(v) for v in near)
    out = set()
    cy = math.hypot(R[0, 0], R[1, 0])
    bases = []
    if cy > GIMBAL_EPS: bases = [prev]
    else: bases = [(from_units(near[0] + g), prev[1], prev[2]) for g in range(-2048, 2049, 128)]   # gimbal: x is free (z follows)
    for pv in bases:
        for _, ang in candidates(R, pv):
            c = [to_units(a) for a in ang]
            for d in itertools.product((-4096, 0, 4096), repeat=3):
                v = tuple(ci + di for ci, di in zip(c, d))
                if all(-32768 <= x <= 32767 for x in v): out.add(v)
    return out


def blend_dev(s, t):
    Rs, Rt = key_matrix(s), key_matrix(t)
    ts = [((i + 1) << 12) // (NT + 1) for i in range(NT)]
    return max(geodesic(key_matrix(dw3_lerp(s, t, x)), slerp(Rs, Rt, x / 4096)) for x in ts) * D


def recondition(tr, role, ours, others, mode):
    """Replace OUR boundary key (frame OURS) in every bone by an equivalent triple. role 'in': ours is the blend TARGET and the
    counterparts are the source frames OTHERS (their triples are taken from the tracks); role 'out': ours is the blend SOURCE and
    OTHERS are target key frames. mode 'L1' or 'DEV'; with several counterparts DEV minimises the worst one."""
    new = [[t[0], list(t[1]), t[2]] for t in tr]
    for b in range(NB):
        keys = new[b][1]; i = next(j for j, k in enumerate(keys) if k[3] == ours); u = keys[i][:3]
        cps = [tuple(pose_track(tr[b][1], f)) for f in others] if role == 'in' else [tuple(next(k for k in tr[b][1] if k[3] >= f)[:3]) for f in others]
        best, bc = u, None
        for cand in equivalents(u, cps[0]):
            l1 = sum(abs(wrap16(cand[c] - cp[c])) for cp in cps for c in range(3))
            if mode == 'L1': cost = (l1,)
            else: cost = (round(max(blend_dev(cp, cand) if role == 'in' else blend_dev(cand, cp) for cp in cps), 6), l1)
            if bc is None or cost < bc: best, bc = cand, cost
        keys[i] = (*best, ours)
    return new


def same_playback(tr_a, tr_b, motions):
    """max geodesic (deg) between two track sets over every NON-blend tick of every motion"""
    pa, pb, w = Player(tr_a, motions), Player(tr_b, motions), 0.0
    for mi in range(1, len(motions) + 1):
        sa, sb = run_motion(pa, mi), run_motion(pb, mi)
        for x, y in zip(sa, sb):
            if x['bl']: continue
            for b in range(NB): w = max(w, geodesic(key_matrix(x['pose'][b][1]), key_matrix(y['pose'][b][1])) * D)
    return w


def row(label, m, extra=''):
    return f'| {label} | {m["dev"]:.2f} | {m["turns"]:.2f} | {m["tip"]:.0f} ({m["tip"] / LEN * 100:.0f} %) | {m["ticks"]} |{extra}'


def table(title, tr, motions, role, ours, others, kinds_note=''):
    print(f'\n#### {title}')
    print('| variant | max dev vs ideal (deg) | max lerp span (turns) | tip err (units) | blend ticks |')
    print('|---|---|---|---|---|')
    r0 = measure(tr, motions); print(row('V0 as converted', r0))
    for mode in ('L1', 'DEV'):
        tr2 = recondition(tr, role, ours, others, mode)
        m = measure(tr2, motions)
        print(row(mode, m, f' playback change {same_playback(tr, tr2, motions):.1e} deg'))
    return r0


# ---------------------------------------------------------------- scenarios
def main():
    turn = lambda x=0, y=0, z=0: (x * 4096, y * 4096, z * 4096)
    kinds = ['idle', 'attack', 'attack2', 'spin', 'gout', 'gin', 'gexact']
    I = lambda tr: None
    print('### E9 synthetic results (max over all 6 bones and all blend ticks; ideal = slerp with the same t schedule)')

    # 1 attack -> idle, 2 idle -> attack (inside one motion and carried from the previous motion), 3 attack -> attack
    for w_name, w in (('no offset', turn()), ('attack stored +1 turn in z', turn(0, 0, 1)), ('attack stored -1 turn in x', turn(-1)),
                      ('attack stored +2 turns z, -1 turn x', turn(-1, 0, 2))):
        tr = make_tracks(kinds, {'attack': [w] * NB, 'attack2': [turn()] * NB})
        table(f'attack -> idle, {w_name}  (ours = attack end key 139; counterpart = idle key 10)', tr, [script(('play', 'attack'), ('blend',), ('play', 'idle'))], 'out', 139, [10])
        table(f'idle -> attack inside one motion, {w_name}  (ours = attack start key 100; source = idle frame 39)', tr, [script(('play', 'idle'), ('blend',), ('play', 'attack'))], 'in', 100, [39])
        table(f'idle -> attack CARRIED (blend from the previous motion\'s last pose), {w_name}', tr, [script(('play', 'idle')), script(('blend',), ('play', 'attack'))], 'in', 100, [39])
    tr = make_tracks(kinds, {'attack': [turn(0, 0, 1)] * NB, 'attack2': [turn(0, 0, -1)] * NB})
    table('attack(+1 turn z) -> attack2(-1 turn z): both stored a turn from each other. Condition attack2 start (ours) relative to attack end',
          tr, [script(('play', 'attack'), ('blend',), ('play', 'attack2'))], 'in', 200, [139])

    # 4 intentional spin -> idle
    tr = make_tracks(kinds, {})
    r = table('intentional spin (z +1 turn, odd bones x -0.5 turn) -> idle; ours = spin end key 339', tr, [script(('play', 'spin'), ('blend',), ('play', 'idle'))], 'out', 339, [10])
    tr2 = recondition(tr, 'out', 339, [10], 'DEV')
    zs = [k[2] for k in tr2[0][1] if 300 <= k[3] <= 339]
    print(f'   spin inside the clip after conditioning: z of bone 1 goes {zs[0]} -> {zs[-2]} (frame 338) -> {zs[-1]} (frame 339, the one-frame wrap): the full turn is still played, '
          f'max per-frame step {max(abs(a - b) for a, b in zip(zs[1:-1], zs)):.0f} units; playback change {same_playback(tr, tr2, [script(("play", "spin"), ("blend",), ("play", "idle"))]):.1e} deg')

    # 5 near gimbal lock
    tr = make_tracks(kinds, {})
    table('attack ending 0.57 deg short of the pole (gout) -> idle; ours = key 439', tr, [script(('play', 'gout'), ('blend',), ('play', 'idle'))], 'out', 439, [10])
    table('idle -> clip starting 0.57 deg short of the pole (gin); ours = key 500', tr, [script(('play', 'idle'), ('blend',), ('play', 'gin'))], 'in', 500, [39])
    table('attack ending EXACTLY on the pole (gexact, x free) -> idle; ours = key 639', tr, [script(('play', 'gexact'), ('blend',), ('play', 'idle'))], 'out', 639, [10])

    # 6 several joints, each bone stored a different number of turns away (hierarchy)
    mixed = [turn(0, 0, 1), turn(-1), turn(), turn(0, 0, 2), turn(1, 0, -1), turn(0, 0, -1)]
    tr = make_tracks(kinds, {'attack': mixed})
    table('attack -> idle, every joint stored a different number of turns away (z+1, x-1, 0, z+2, x+1/z-1, z-1)', tr, [script(('play', 'attack'), ('blend',), ('play', 'idle'))], 'out', 139, [10])
    table('idle -> attack, same mixed offsets', tr, [script(('play', 'idle'), ('blend',), ('play', 'attack'))], 'in', 100, [39])

    # 7 one key, several sources (requires knowing the source; no general solution)
    print('\n#### one attack start key (100), several possible source states: conditioned for ONE source, then blended from the others')
    print('| key conditioned for | source played | max dev (deg) | max span (turns) | tip err (units) |')
    print('|---|---|---|---|---|')
    src = {'idle end (frame 39)': (script(('playn', 'idle', 30)), 39),
           'spin end (frame 339, +1 turn z)': (script(('play', 'spin')), 339),
           'attack2 interrupted at frame 215': (script(('playn', 'attack2', 16)), 215),
           'spin interrupted at frame 330 (z about +0.85 turn)': (script(('playn', 'spin', 31)), 330)}
    tr = make_tracks(kinds, {})
    def eval_key(trv):
        out = []
        for name, (sc, f) in src.items():
            m = measure(trv, [sc, script(('blend',), ('play', 'attack'))]); out.append((name, m))
        return out
    for cname, others in (('nothing (V0)', None), ('idle end', [39]), ('spin end', [339]), ('attack2@215', [215]), ('minimax over the four', [39, 339, 215, 330])):
        trv = tr if others is None else recondition(tr, 'in', 100, others, 'DEV')
        for name, m in eval_key(trv): print(f'| {cname} | {name} | {m["dev"]:.2f} | {m["turns"]:.2f} | {m["tip"]:.0f} |')

    # 7b the engine's blend-to-frame-0 behaviour (same test in the decomp: FIGHTSTG_stepMotion)
    print('\n#### a blend whose target frame is 0 does not snapshot PREV (blendTarget starts at 0 and the test is `!=`)')
    old = FRAMES['idle']; FRAMES['idle'] = (0, 30)
    try:
        tr = make_tracks(['idle', 'attack'], {}); m = measure(tr, [script(('play', 'attack'), ('blend',), ('play', 'idle'))])
        first = next(s for s in m['snaps'] if s['bl'])
        print(f'   attack -> idle at frame 0: first blend tick PREV of bone 1 = {first["prev"][0][1]} (the attack end pose is {pose_track(tr[0][1], 139)}); '
              f'dev vs ideal {m["dev"]:.2f} deg is measured from the wrong source: the blend starts from a zero pose')
    finally: FRAMES['idle'] = old

    # 8 what sampling loses: more than half a turn per frame
    print('\n#### what a one-key-per-frame clip cannot say: a spin of 210 deg per frame (z)')
    Rs = [compose(0, 0, math.radians(210) * i) for i in range(18)]
    ks = rotations_to_keys(Rs, (0, 0, 0))
    steps = {ks[i + 1][2] - ks[i][2] for i in range(len(ks) - 1)}
    err = max(geodesic(key_matrix(k), R) for k, R in zip(ks, Rs)) * D
    print(f'   converter output: per-frame z step {sorted(steps)} units (= {[round(s * 360 / 4096) for s in sorted(steps)]} deg): the short way, i.e. -150 deg per frame; '
          f'orientation at every integer frame matches the input to {err:.1e} deg. A game that only poses integer frames shows the same picture, '
          f'but the direction/turn count of the original is gone.')


if __name__ == '__main__':
    main()
