"""E4.2d (writes ONE throw-away viewer fixture): Rambley 'mixamo.com' (twerk dance, 30 fps) retargeted onto ExVeemon (0575) and installed as its IDLE.
Method = the E1.x ABS pipeline with the moving body frame (carrier): legs 2-5 / 6-9 (e17), arms 14-16 / 22-24 (e13), pelvis bone 1 and torso bone 13 rotations (e20), and ONE hips translation
written identically to the sibling bones 1 and 13 (body-frame displacement from the clip mean x leg-length ratio). Tail 10-12, head 17-19 and wings 20-21 / 25-26 stay at the reference (idle frame 10).
v2 (after the in-game test: too fast, feet off the ground, hands through the face):
 - TIME: the game plays one integer frame per tick (60 Hz), so the 30 fps clip is resampled to E49_UPS=2 keys per source frame (60 fps) -> 2x longer, same motion.
 - GROUND: per frame the pelvis (bones 1 and 13 together) is moved so the LOWEST foot vertex sits on the ground level of the reference idle (vertical), and horizontally so the feet that are in contact keep
   their mean position (least squares over the contacting feet): the contact/slide metrics are measured on the posed vertex clouds (rigid per-bone skinning, as the viewer shows it).
 - HANDS: the whole arm is rotated outward about the torso's forward axis by the smallest angle (0..50 deg, step 5) that keeps the hand clouds at least E49_CLEAR units from the head clouds in every frame.
Keys go to NEW frames above the last key (e41_writer.add_motion, new slot) and the idle's two steps are re-pointed at them: (0,n,F0,F0+n-1), (n,0,0x8000,0). Original 0575.bin untouched.
Output viewer/data/e14/0575-e49-dance.bin, kb/e49-build-output.txt"""
import os, sys, struct, hashlib, math
os.environ.update(E49_SRC='rambley', E49_MODEL='0575', E49_DEDUP='1'); os.environ.setdefault('E49_UPS', '2')
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..'))
import numpy as np, contextlib, io, glob, tempfile, subprocess
import survey_keys as sk
from survey_real import read_motions
from e7_compare import load_raw, pose_track, read_glb, accessor
from e8_resample import parents_of, World
from e13_arm import Mix, U, nrot, main as arm_main
from e17_leg import main as leg_main, mix_pos
from e20_body import body_keys
from e41_writer import add_motion
from dw3_euler import key_matrix, rotations_to_keys
import e49_src
SG = os.environ.get('E49_GLTF', 'C:/Users/ferve/Desktop/km_rambley_twerk/scene.gltf'); ANIM = 'mixamo.com'; CLEAR = float(os.environ.get('E49_CLEAR', 25))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..')); D = os.path.join(ROOT, 'viewer', 'data'); AB = os.path.join(D, 'files', '0575.bin'); OUT = os.path.join(D, 'e14', '0575-e49-dance.bin')
lines = []
def log(s): print(s); lines.append(s)
def quiet(fn, *a):
    with contextlib.redirect_stdout(io.StringIO()): return fn(*a)


def clouds(binpath, n):
    """per-bone vertex cloud in the bone's own frame (DW3 axes/units), zero-rotation bind pose of the viewer export (same method as e40_describe.geometry, vertices kept)"""
    tmp = tempfile.mkdtemp(prefix='e49_'); subprocess.run([os.path.join(ROOT, 'viewer', 'build', 'dw3mv.exe'), '--export-gltf', binpath, tmp.replace('\\', '/')], capture_output=True, timeout=120)
    js, b = read_glb(glob.glob(os.path.join(tmp, '*.glb'))[0]); N = js['nodes']; par = {c: i for i, nd in enumerate(N) for c in nd.get('children', [])}
    bn = {i: int(nd['name'].split('_')[1]) for i, nd in enumerate(N) if nd['name'].startswith('bone_')}
    def wpos(i):
        p = np.zeros(3)
        while i is not None: p = p + np.array(N[i].get('translation', [0, 0, 0])); i = par.get(i)
        return p
    to_dw = lambda v: np.array([v[0], -v[1], -v[2]]) * 100
    joints = js['skins'][0]['joints']; verts = {}
    for m in js['meshes']:
        for pr in m['primitives']:
            P = accessor(js, b, pr['attributes']['POSITION']).astype(float); ja = js['accessors'][pr['attributes']['JOINTS_0']]; jv_ = js['bufferViews'][ja['bufferView']]; jo = jv_.get('byteOffset', 0) + ja.get('byteOffset', 0)
            J = np.frombuffer(b, {5121: np.uint8, 5123: np.uint16}[ja['componentType']], ja['count'] * 4, jo).reshape(ja['count'], 4)[:, 0].astype(int)
            for jv in set(J.tolist()): verts.setdefault(bn[joints[int(jv)]], []).append(P[J == jv])
    out = {}
    for k in range(1, n + 1):
        if k in verts:
            node = next(i for i, v in bn.items() if v == k); c = wpos(node); V = np.vstack(verts[k]); out[k] = np.array([to_dw(v - c) for v in V])[::max(1, len(V) // 120)]
    return out


raw = open(AB, 'rb').read(); h0 = hashlib.sha1(raw).hexdigest(); tr, mo = load_raw(AB); nb = len(tr); par = parents_of(AB); CL = clouds(AB, nb)
keys = {}
armmat = {}
for side in ('Left', 'Right'):
    L = quiet(leg_main, SG, AB, ANIM, True, side, True)['ABS']; A = quiet(arm_main, SG, AB, ANIM, True, side, True)['ABS']
    for b, k in zip((range(2, 6) if side == 'Left' else range(6, 10)), L): keys[b] = k
    for b, k in zip(((14, 15, 16) if side == 'Left' else (22, 23, 24)), A): keys[b] = k
keys[1] = body_keys(SG, AB, ANIM, True, 'pelvis')[0][0]; keys[13] = body_keys(SG, AB, ANIM, True, 'torso')[0][0]
n = len(keys[1]); assert all(len(v) == n for v in keys.values()), {b: len(v) for b, v in keys.items()}
M = Mix(SG, ANIM); I = M.idx; js, bb = read_glb(SG); a = next(x for x in js['animations'] if x['name'] == ANIM); M.tra = e49_src.channels(js, bb, a, 'translation'); M.nf = n
P = lambda nm, f=None: mix_pos(M, nm, f) * 100
left = U(P('LeftUpLeg') - P('RightUpLeg')); up0 = P('Neck') - P('Hips'); up = U(up0 - np.dot(up0, left) * left); fwd = np.cross(left, up); BM = np.column_stack([left, up, fwd])
hp = np.array([P('Hips', f) for f in range(n)]); d = (hp - hp.mean(0)) @ BM
ref = {b: np.array(pose_track(tr[b - 1][0], 10), float) for b in (1, 13)}; assert (ref[1] == ref[13]).all()
BA = np.column_stack([[1.0, 0, 0], [0, -1.0, 0], [0, 0, -1.0]])      # DW3 body frame at rest: left +x, up -y, forward -z
refpose = [[tuple(int(x) for x in pose_track(tr[b][t], 10)) for t in range(3)] for b in range(nb)]
Wref = World(par, nb).update(refpose)
# FEET (v3): ExVeemon's leg is digitigrade: bone 4 = hock, 4->5 points DOWN (the claws hang from bone 5), so the E1.x ABS rule (foot direction = Rambley's horizontal ankle->toe) lifts the paws off the
# ground. Foot (4/8) and toe (5/9) are instead the CHANGE of Rambley's own foot/toe world rotation from its clip-mean (stance) orientation, applied to ExVeemon's own reference world rotation.
Tm = BA @ BM.T                                                  # Rambley world -> DW3 world (both body frames: left/up/forward)
Rp_ = [key_matrix(k) for k in keys[1]]
cmean = lambda A: (lambda u, _, vt: u @ vt)(*np.linalg.svd(A.sum(0)))
for side, legb, fb in (('Left', (2, 3), (4, 5)), ('Right', (6, 7), (8, 9))):
    Rs = [np.array([M.Rw(I[side + nm], f) for f in range(n)]) for nm in ('Foot', 'ToeBase')]
    Wf = [Tm @ (Rs[i][f] @ cmean(Rs[i]).T) @ Tm.T @ nrot(Wref[fb[i]]) for i in (0, 1) for f in range(n)]; Wf = [Wf[:n], Wf[n:]]
    L4, L5 = [], []
    for f in range(n):
        W3 = Rp_[f] @ key_matrix(keys[legb[0]][f]) @ key_matrix(keys[legb[1]][f]); L4.append(W3.T @ Wf[0][f]); L5.append(Wf[0][f].T @ Wf[1][f])
    keys[fb[0]] = rotations_to_keys(L4, tuple(refpose[fb[0] - 1][1])); keys[fb[1]] = rotations_to_keys(L5, tuple(refpose[fb[1] - 1][1]))
legA = np.linalg.norm(pose_track(tr[2][0], 10)) + np.linalg.norm(pose_track(tr[3][0], 10)); legM = np.linalg.norm(M.off(I['LeftLeg'])) + np.linalg.norm(M.off(I['LeftFoot'])); s = legA / legM
bob0 = np.array([ref[1] + BA @ (s * d[f]) for f in range(n)])
log(f'frames {n} ({n / 60:.2f} s at 60 ticks/s; source 30 fps x{os.environ["E49_UPS"]}); scale dest/source leg length {s:.0f}; hips displacement (source units) left {np.ptp(d[:, 0]):.3f} up {np.ptp(d[:, 1]):.3f} fwd {np.ptp(d[:, 2]):.3f} (leg {legM:.3f})')
R13 = nrot(Wref[13]); a_loc = R13.T @ np.array([0, 0, -1.0])             # torso-local forward axis (the arms' parent is the torso, constant local frame)
FEET, HAND, HEAD = (4, 5, 8, 9), {'L': (15, 16), 'R': (23, 24)}, (17, 18, 19)
def rod(ax, t): K = np.array([[0, -ax[2], ax[1]], [ax[2], 0, -ax[0]], [-ax[1], ax[0], 0]]); return np.eye(3) + math.sin(t) * K + (1 - math.cos(t)) * K @ K


def arm_keys(theta):
    """keys of the arm bones 14,22 with the whole arm turned outward by theta (rad, per frame) about the torso-local forward axis (left arm +, right arm -)"""
    out = dict(keys); th = np.broadcast_to(np.asarray(theta, float), (n,))
    for b, sg in ((14, 1), (22, -1)):
        out[b] = rotations_to_keys([rod(a_loc, sg * th[f] * SIGN) @ key_matrix(k) for f, k in enumerate(keys[b])], tuple(refpose[b - 1][1]))
    return out


def posed(kk, bob, frames=None):
    """per frame: world pos / rot of every bone (FK with the viewer's World)"""
    res = []
    for f in (range(n) if frames is None else frames):
        ps = [[tuple(int(round(x)) for x in bob[f]) if (t == 0 and b in (0, 12)) else (kk[b + 1][f] if (t == 1 and b + 1 in kk) else refpose[b][t]) for t in range(3)] for b in range(nb)]
        W = World(par, nb).update(ps); res.append(([W[b][:3, 3].copy() for b in range(1, nb + 1)], [nrot(W[b]) for b in range(1, nb + 1)]))
    return res


H = lambda pp, R, b: pp[b - 1] + (R[b - 1] @ CL[b].T).T


def metrics(kk, bob, G=None):
    st = posed(kk, bob)
    foot = np.array([[min(-H(pp, R, b)[:, 1].max() for b in FEET[i * 2:i * 2 + 2]) if False else -max(H(pp, R, b)[:, 1].max() for b in FEET[i * 2:i * 2 + 2]) for i in (0, 1)] for pp, R in st])   # height of the lowest vertex of each foot
    fxz = np.array([[np.mean([H(pp, R, b)[:, [0, 2]].mean(0) for b in FEET[i * 2:i * 2 + 2]], axis=0) for i in (0, 1)] for pp, R in st])       # (n, 2 feet, 2) horizontal centre of the foot clouds
    hd = np.array([min(np.linalg.norm(H(pp, R, hb)[:, None] - H(pp, R, hh)[None], axis=2).min() for hb in HAND[sd] for hh in HEAD) for pp, R in st for sd in ('L', 'R')]).reshape(n, 2)
    return foot, fxz, hd, st


# reference ground level = lowest foot vertex of the ORIGINAL idle at frame 10
st0 = posed({}, np.tile(ref[1], (1, 1)).repeat(1, 0), [0]) if False else None
refst = World(par, nb).update(refpose); Gv = max((refst[b][:3, 3] + (nrot(refst[b]) @ CL[b].T).T)[:, 1].max() for b in FEET); G = -Gv
log(f'ground level (lowest foot vertex of the reference idle) height {G:.0f}')
SIGN = 1.0
foot, fxz, hd, _ = metrics(keys, bob0)
hand_x = lambda kk: None
log(f'BEFORE: lowest foot height over the clip {foot.min():.0f}..{foot.max():.0f} (ground {G:.0f}); frames with a foot below ground {int((foot.min(1) < G - 5).sum())}, lowest foot above ground by >30 u: {int((foot.min(1) > G + 30).sum())}; hand-head min distance {hd.min():.0f} (per side L/R {hd.min(0).round()}), frames under {CLEAR:.0f}: {int((hd.min(1) < CLEAR).sum())}')
# direction of 'outward': test +theta on the left arm hand x
t_kk = arm_keys(math.radians(20)); _, _, hd20, st20 = metrics(t_kk, bob0)
refst_f = posed(keys, bob0, [0])[0]; x0 = np.mean([refst_f[0][15][0] for _ in (0,)]); x1 = st20[0][0][15][0]
SIGN = 1.0 if x1 > x0 else -1.0; log(f'abduction sign for +theta: {SIGN:+.0f} (left hand x {x0:.0f} -> {x1:.0f} at frame 0 with +20 deg)')
_, _, _, st_ = metrics(keys, bob0); need = np.zeros((n, 2))
for f in range(n):
    pp, R = st_[f]; head = np.vstack([H(pp, R, b) for b in HEAD]); w = R[12] @ a_loc
    for si, (hb, sh, sg) in enumerate((((15, 16), 14, 1), ((23, 24), 22, -1))):
        S = pp[sh - 1]; Pt = np.vstack([H(pp, R, b) for b in hb]); need[f, si] = 90
        for th in range(0, 95, 5):
            Pr = S + (rod(w, sg * SIGN * math.radians(th)) @ (Pt - S).T).T
            if np.linalg.norm(Pr[:, None] - head[None], axis=2).min() >= CLEAR: need[f, si] = th; break
mx = lambda x, r: np.array([x[max(0, i - r):i + r + 1].max(0) for i in range(len(x))])
av = lambda x, r: np.array([x[max(0, i - r):i + r + 1].mean(0) for i in range(len(x))])
thetas = av(mx(need, 12), 6)                     # running max (radius 12 frames) then mean (radius 6): smooth and never below the needed angle
log(f'abduction needed per frame (deg): arms L/R: frames needing >0: {(need > 0).sum(0)}, mean {need.mean(0).round(1)}, max {need.max(0)}; used (smoothed): mean {thetas.mean(0).round(1)}, max {thetas.max(0).round(1)}')
keys2 = arm_keys(np.radians(thetas[:, 0]))
if True:      # the right arm gets its own angle: rebuild with the right-arm column
    th = np.radians(thetas); out = dict(keys)
    for si, (b, sg) in enumerate(((14, 1), (22, -1))):
        out[b] = rotations_to_keys([rod(a_loc, sg * th[f, si] * SIGN) @ key_matrix(k) for f, k in enumerate(keys[b])], tuple(refpose[b - 1][1]))
    keys2 = out
foot, fxz, hd, _ = metrics(keys2, bob0)
hd_b = metrics(keys, bob0)[2]
log(f'hand-head min distance (posed vertex samples): without abduction {hd_b.min():.0f}, frames under 15/{CLEAR:.0f}: {int((hd_b.min(1) < 15).sum())}/{int((hd_b.min(1) < CLEAR).sum())}; with the abduction {hd.min():.0f}, frames under 15/{CLEAR:.0f}: {int((hd.min(1) < 15).sum())}/{int((hd.min(1) < CLEAR).sum())} of {n}')
# ground: vertical shift so the lowest foot sits on the ground; horizontal: keep the contacting feet at their mean position
low = foot.min(1); dy = -(low - G)                    # height to add (y is down: y -= dh)
contact = foot < (low[:, None] + 25)                 # feet within 25 u of the lowest one
home = np.array([fxz[contact[:, i], i].mean(0) for i in (0, 1)])
shift = np.array([np.mean([home[i] - fxz[f, i] for i in (0, 1) if contact[f, i]], axis=0) for f in range(n)])
bob1 = bob0.copy(); bob1[:, 1] -= dy      # vertical only: the horizontal lock made the slide worse (measured), so it is reported, not applied
SM = lambda x: np.convolve(np.pad(x, ((2, 2), (0, 0)), mode='edge'), np.ones(5) / 5, mode='valid') if False else x
foot2, fxz2, hd2, _ = metrics(keys2, bob1)
cx = lambda fx, c: np.mean([np.abs(np.diff(fx[:, i], axis=0))[(c[:-1, i] & c[1:, i])].sum(axis=1).mean() for i in (0, 1)])
log(f'AFTER ground fix: lowest foot {foot2.min(1).min():.0f}..{foot2.min(1).max():.0f} (ground {G:.0f}); contact frames L {int(contact[:, 0].sum())} R {int(contact[:, 1].sum())} of {n}; foot travel per frame while in contact: before {cx(fxz, contact):.1f} u, after {cx(fxz2, contact):.1f} u; pelvis vertical shift max {np.abs(dy).max():.0f} u; hand-head min {hd2.min():.0f}')
bob = [tuple(int(round(v)) for v in b) for b in bob1]
maxf = max(k[3] for b in tr for t in b for k in t if k[3] < 30000); F0 = (maxf // 10 + 2) * 10
tracks = {}
for b in range(1, nb + 1):
    tt = []
    for t in range(3):
        if t == 1 and b in keys2: v = [tuple(int(x) for x in keys2[b][f]) for f in range(n)]
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
log(f'idle steps now {m1[0][:2]}; wrote {OUT} {len(out)} bytes ({len(out) / 2048:.1f} sectors; original {len(raw)}); original untouched: {hashlib.sha1(open(AB, "rb").read()).hexdigest() == h0}')
open(os.path.join(ROOT, 'kb', 'e49-build-output.txt'), 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
