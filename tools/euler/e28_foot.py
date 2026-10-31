"""E2.8: foot/toe geometry of the Agumon legs (world, side view) in a bin at given idle frames. python e28_foot.py BIN [frames]"""
import sys, numpy as np, math
from e7_compare import load_raw, Player, pose_track
from e8_resample import parents_of, World
D = 180 / math.pi
bin_ = sys.argv[1]; frames = [int(x) for x in sys.argv[2].split(',')] if len(sys.argv) > 2 else [10]
tr, mo = load_raw(bin_); par = parents_of(bin_); n = len(tr)
for f in frames:
    ref = [[pose_track(tr[b][t], f) for t in range(3)] for b in range(n)]
    W = World(par, n).update(ref); p = lambda b: W[b][:3, 3]
    print(f'--- frame {f}')
    for nm, seg in (('L', [11, 12, 13, 14]), ('R', [15, 16, 17, 18])):
        P = [p(b) for b in seg]; up = lambda v: np.array([v[0], -v[1], -v[2]])   # x left, y up, z back-> forward = -z
        d = [up(P[i + 1] - P[i]) for i in range(3)]
        pitch = lambda v: math.degrees(math.atan2(v[1], math.hypot(v[0], v[2])))
        ang = lambda a, b: math.degrees(math.acos(np.dot(a, b) / np.linalg.norm(a) / np.linalg.norm(b)))
        print(f'{nm}: lens thigh/shank/foot {[round(float(np.linalg.norm(x))) for x in d]}; pitch thigh/shank/foot(17->18) {[round(pitch(x)) for x in d]}; bend at knee {ang(d[0], d[1]):.0f}; angle shank vs foot {ang(d[1], d[2]):.0f}; heights ankle {-P[2][1]:.0f} toe {-P[3][1]:.0f}')
