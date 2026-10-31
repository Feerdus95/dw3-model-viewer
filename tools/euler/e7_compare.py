"""E7 (READ-ONLY): does the Python reimplementation of the motion player (survey_real.py's assumptions) match the viewer?
The viewer's own evaluation is read from `dw3mv --export-gltf FILE.bin OUTDIR` (headless; model.cpp setMotion/advance/evaluate,
sampled every 2nd tick, gltf.cpp:265-297). This script replays the same ticks in Python from the raw keys with the bit-exact
integer lerp (dw3_euler.dw3_lerp), takes each bone's rotation, converts it to the glTF frame (conjugation by diag(1,-1,-1)) and
compares it with the GLB's rotation samples as a geodesic angle.
Run: python e7_compare.py FILE.bin FILE.glb [label]      (writes nothing)
Scope: every motion the GLB holds, blends included; motions are replayed in order (the pose carries over)."""
import json, math, os, struct, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, ".."))
from euler_zyx import geodesic, unquat
from dw3_euler import key_matrix, dw3_lerp
import survey_keys as sk
from survey_real import read_motions

D = 180 / math.pi
CONJ = np.diag([1.0, -1.0, -1.0])


def read_glb(path):
    b = open(path, 'rb').read()
    jl, _ = struct.unpack_from('<II', b, 12)
    js = json.loads(b[20:20 + jl])
    off = 20 + jl + 8
    return js, b[off:]


def accessor(js, bin_, i):
    a = js['accessors'][i]; bv = js['bufferViews'][a['bufferView']]
    n = {'SCALAR': 1, 'VEC3': 3, 'VEC4': 4}[a['type']]
    o = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
    return np.frombuffer(bin_, '<f4', a['count'] * n, o).reshape(a['count'], n)


def load_raw(path):
    """tracks[b][t] = every key incl. the end sentinel, as the viewer's Bone.keys; motions."""
    a = sk.Arc(open(path, 'rb').read())
    bi = sk.find_bone(a); e = a.off(bi); cnt = sk.u32(a.b, e + 4); tex = sk.u32(a.b, e)
    motions = None
    for i in range(a.n()):
        if i in (bi, tex) or a.off(i) == 0: continue
        m = read_motions(a.sub(i))
        if m: motions = m; break
    tr = [sk.tracks(a.sub(sk.u32(a.b, e + 16 + b * 12))) for b in range(cnt)]
    return tr, motions


def pose_track(keys, frame):
    """model.cpp pose_bone for one track. keys: (x,y,z,frame) incl. the sentinel."""
    k = 0; found = False
    while True:
        if k + 1 >= len(keys): break
        if keys[k][3] == frame: found = True; break
        if keys[k][3] >= frame: break
        k += 1
    cur = keys[k]
    if found or k == 0 or cur[3] < frame: return tuple(cur[:3])
    prev = keys[k - 1]; span = cur[3] - prev[3]
    dp = int(((frame - prev[3]) << 12) / span) if span else 0   # C int division truncates
    return dw3_lerp(tuple(prev[:3]), tuple(cur[:3]), dp)


class Player:
    """Port of Model::setMotion / advance / evaluate (model.cpp:328-465, 601-619) for pos/rot/scale, track b = viewer bone b+1.
    The pose persists across setMotion calls, as in the exporter's loop (gltf.cpp:268), which matters for blends from 0xFFFF."""
    def __init__(self, tracks, motions):
        self.tr, self.motions, n = tracks, motions, len(tracks)
        self.pose = [[(0, 0, 0)] * 3 for _ in range(n)]
        self.prev = [[(0, 0, 0)] * 3 for _ in range(n)]
        self.idle = motions[0][0][2] if motions else 0
        self.blendTarget = 0
        self.frame = 0

    def set_motion(self, m):
        self.keyframe, self.done, self.blending, self.blendTarget = 1, False, False, 0
        steps = self.motions[m - 1]
        kf, bs, bt, cnt = [0] * 0x640, [0] * 0x640, [0] * 0x640, 0
        s = 0
        while s < len(steps) and steps[s][0] != 0x7FFF and cnt < 0x640:
            idx, n, frame, end = steps[s]
            if n == 0:
                kf[cnt] = frame & 0xFFFF; bt[cnt] = end & 0xFFFF; cnt += 1; break
            if end == 0:
                to = steps[s + 1][2] if s + 1 < len(steps) else -1
                if to == -1: to = self.idle
                frm = (steps[s - 1][3] & 0xFFFF) if idx != 0 and s > 0 else 0xFFFF
                for i in range(n):
                    if cnt >= 0x640: break
                    bs[cnt], bt[cnt] = frm, to & 0xFFFF
                    kf[cnt] = ((((i + 1) << 12) // (n + 1)) | 0x8000) & 0xFFFF; cnt += 1
            else:
                for i in range(n):
                    if cnt >= 0x640: break
                    kf[cnt] = (frame + i) & 0xFFFF; bt[cnt] = 0; cnt += 1
            s += 1
        self.kf, self.bs, self.bt, self.count = kf, bs, bt, cnt
        self.evaluate()

    def evaluate(self):
        if self.count <= 0: return
        if self.keyframe >= self.count: self.keyframe = self.count - 1
        key = self.keyframe
        if not self.done:
            if self.kf[key] & 0x8000:
                if self.blendTarget != self.bt[key]:
                    self.blendTarget = self.bt[key]
                    if self.bs[key] != 0xFFFF:
                        for b in range(len(self.tr)): self.pose[b] = [pose_track(self.tr[b][t], self.bs[key]) for t in range(3)]
                    self.prev = [list(p) for p in self.pose]
                self.frame, self.blend, self.blending = self.bt[key], self.kf[key] & 0x7FFF, True
            else:
                self.blendTarget, self.frame, self.blending = 0, self.kf[key], False
        else:
            self.blendTarget, self.blending = 0, False
        for b in range(len(self.tr)):
            if self.blending: self.blend_bone(b)
            else: self.pose[b] = [pose_track(self.tr[b][t], self.frame) for t in range(3)]

    def blend_bone(self, b):
        out = list(self.pose[b])
        for t in range(3):
            keys = self.tr[b][t]; k = 0
            while k + 1 < len(keys) and keys[k][3] < self.frame: k += 1
            cur, prev = tuple(keys[k][:3]), self.prev[b][t]
            if t == 2 and (not all(prev) or not all(cur)): out[t] = cur; break
            out[t] = dw3_lerp(prev, cur, self.blend)
        self.pose[b] = out

    def advance(self):
        if self.done or self.count <= 0: return
        self.keyframe = min(self.keyframe + 1, self.count - 1)
        v = self.kf[self.keyframe]
        if v == 0x8000:
            if self.bt[self.keyframe] < self.keyframe: self.keyframe -= 1; self.done = True
            else: self.keyframe = min(self.bt[self.keyframe], self.count - 1)
        elif v == 0xFFFF: self.done = True

    def samples(self, m):
        """gltf.cpp:277-297: rotation rows every 2nd tick (and the last); a hidden bone keeps its last row."""
        self.set_motion(m)
        if self.count <= 0: return None
        frames, last = [], {}
        for tick in range(1700):
            lastTick = self.done or self.keyframe >= self.count - 1
            if tick % 2 == 0 or lastTick:
                row = dict(last)
                for b, p in enumerate(self.pose):
                    sx, sy, sz = p[2]
                    if (sx | sy | sz) <= 48: continue
                    row[b] = (p[1], p[0])
                frames.append(row); last = row
            if lastTick: break
            self.advance(); self.evaluate()
        return frames


def main(binp, glbp, label):
    tracks, motions = load_raw(binp)
    js, bin_ = read_glb(glbp)
    anims = {a['name']: a for a in js.get('animations', [])}
    errs, perr, per_motion, skipped, nmot = [], [], [], 0, 0
    worst = []
    pl = Player(tracks, motions)
    for mi, steps in enumerate(motions, 1):
        fr = pl.samples(mi)          # always replayed, in order: the pose carries over, as in the exporter
        an = anims.get(f'motion_{mi}')
        if an is None or not fr: skipped += 1; continue
        nmot += 1
        rot, tra = {}, {}
        for ch in an['channels']:
            pth = ch['target']['path']
            if pth in ('rotation', 'translation'):
                (rot if pth == 'rotation' else tra)[ch['target']['node']] = accessor(js, bin_, an['samplers'][ch['sampler']]['output'])
        ns = len(next(iter(rot.values())))
        if ns != len(fr):
            per_motion.append((mi, f'SAMPLE COUNT {len(fr)} (py) vs {ns} (viewer)')); continue
        mx = 0.0
        for s, row in enumerate(fr):
            for b, (r, pos) in row.items():
                if b + 1 not in rot: continue
                perr.append(float(np.abs(tra[b + 1][s] - np.array([pos[0], -pos[1], -pos[2]]) * 0.01).max()))
                R = CONJ @ key_matrix(r) @ CONJ
                x, y, z, w = rot[b + 1][s].astype(float)
                d = geodesic(R, unquat(np.array([x, y, z, w]))) * D
                errs.append(d)
                if d > mx: mx = d
                worst.append((d, mi, s, b))
        per_motion.append((mi, f'max {mx:.4f} deg over {ns} samples'))
    worst = sorted(worst, reverse=True)[:5]
    e = np.array(errs) if errs else np.array([float('nan')])
    print(f'\n### {label}: motions compared {nmot}, motions with no animation in the GLB {skipped}, rotation samples {len(errs)}')
    print(f'- geodesic python-vs-viewer, deg: mean={e.mean():.5f} p50={np.percentile(e,50):.5f} p99={np.percentile(e,99):.5f} max={e.max():.5f}; '
          f'samples > 0.01 deg: {(e > 0.01).sum()}, > 0.1 deg: {(e > 0.1).sum()}')
    print(f'- local translation, max |python - viewer| (glTF units, 0.01/unit): {max(perr):.2e} over {len(perr)} samples')
    bad = [m for m in per_motion if 'SAMPLE' in m[1]]
    print(f'- motions with a sample-count mismatch: {len(bad)}' + (' e.g. ' + str(bad[:4]) if bad else ''))
    print(f'- worst (deg, motion, sample, bone): {[(round(w[0], 4),) + w[1:] for w in worst]}')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else os.path.basename(sys.argv[1]))
