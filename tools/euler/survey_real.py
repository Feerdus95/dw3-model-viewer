"""READ-ONLY characterisation of DW3's real rotation tracks against the Euler converter (euler_zyx / dw3_euler, unchanged).
Run: python viewer/tools/euler/survey_real.py [--top N] file.bin[:label] ...   (writes nothing; opens files 'rb')

Population: for every bone, the rotation track (track 1) is the keys {x,y,z,frame} in frame order, frame < 30000 (the 30000 end
sentinel is not a key here). A PAIR is two consecutive keys. The game interpolates a pair only for a played frame strictly between
their frames (model.cpp:338-358), so:
  A = all pairs;  B = pairs with at least one played frame strictly inside ("interpolated in play"), where the played frames come
  from the model's motion steps (model.cpp:381-420: step.frame .. frame+count-1 for plain steps, plus a blend step's source frame).
A pair is "long way" when one Euler component moves 180 deg or more (>= 2048 units): the game's lerp then turns that way as authored,
while the slerp reference takes the short way, so its deviation is a different thing and is reported apart.
Realised deviation of a B pair = the game's integer lerp (dw3_lerp) at exactly the played frames' t, against the slerp of the keys.
Potential deviation = the same at t = 1024, 2048, 3072."""
import bisect, math, os, sys, struct
from collections import Counter
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, ".."))
from euler_zyx import TAU, geodesic, slerp
from dw3_euler import UNITS, key_matrix, dw3_lerp, rotations_to_keys, wrap16
import survey_keys as sk

D = 180 / math.pi
U2D = 360 / UNITS


def read_motions(arc):
    n = arc.n()
    if n <= 0 or n > 512 or len(arc.b) < n * 4: return None
    out = []
    for i in range(n):
        p, steps = arc.off(i), []
        while p + 8 <= len(arc.b):
            s = struct.unpack_from('<hhhh', arc.b, p); steps.append(s); p += 8
            if s[0] == 0x7FFF: break
        if not steps or steps[-1][0] != 0x7FFF: return None
        out.append(steps)
    return out


def played_frames(motions):
    S = set()
    for m in motions:
        for s, (idx, cnt, frame, end) in enumerate(m):
            if idx == 0x7FFF: break
            if cnt == 0: continue
            if end == 0:
                if idx != 0 and s > 0 and (m[s - 1][3] & 0xFFFF) != 0xFFFF: S.add(m[s - 1][3] & 0xFFFF)
            else:
                S.update(frame + i for i in range(cnt))
    return sorted(f for f in S if 0 <= f < 30000)


def load(path):
    a = sk.Arc(open(path, 'rb').read())
    bi = sk.find_bone(a)
    if bi < 0: raise ValueError("no bone list")
    e = a.off(bi); cnt = sk.u32(a.b, e + 4); tex = sk.u32(a.b, e)
    motions = None
    for i in range(a.n()):
        if i in (bi, tex) or a.off(i) == 0: continue
        m = read_motions(a.sub(i))
        if m: motions = m; break
    tracks = []
    for b in range(cnt):
        ks = sk.tracks(a.sub(sk.u32(a.b, e + 16 + b * 12)))[1]
        tracks.append([k for k in ks if k[3] < 30000])
    return tracks, motions or []


def pole_deg(R):
    """Distance from the gimbal pole in degrees: 0 = pitch +-90 (singular), 90 = pitch 0."""
    return math.degrees(math.atan2(math.hypot(R[0, 0], R[1, 0]), abs(R[2, 0])))


def stats(v):
    if not len(v): return (0,) + (float("nan"),) * 5
    return (len(v), float(np.mean(v)), float(np.percentile(v, 50)), float(np.percentile(v, 95)), float(np.percentile(v, 99)), float(max(v)))


def pair_dev(k0, k1, R0, R1, ts):
    """Worst deviation (deg) of the game's integer lerp from the slerp of the two keys, over the interpolation fractions TS."""
    return max(geodesic(key_matrix(dw3_lerp(k0, k1, t)), slerp(R0, R1, t / 4096)) for t in ts) * D


def analyse(name, path, top=5):
    tracks, motions = load(path)
    played = played_frames(motions)
    R = dict(name=name, bones=len(tracks), keys=0, tracks=0, motions=len(motions), played=len(played))
    geoA, cmpA, geoB, cmpB, gaps, ratios = [], [], [], [], [], []
    real_short, real_long, pot_short, conv_short = [], [], [], []
    cmp_cnt, half, pole, cross, unwrap, conv = Counter(), Counter(), Counter(), Counter(), Counter(), Counter()
    worst, perbone = [], []
    maxorig = maxout = 0
    for b, ks in enumerate(tracks):
        if len(ks) < 2: continue
        R['tracks'] += 1; R['keys'] += len(ks)
        Rs = [key_matrix(k[:3]) for k in ks]
        maxorig = max(maxorig, max(abs(v) for k in ks for v in k[:3]))
        for R_ in Rs:
            d = pole_deg(R_)
            for lim in (1, 5, 10):
                if d < lim: pole[f'keys <{lim} deg from the pole'] += 1
        # the converter on the authored matrices, continuing from the first key (code unchanged)
        outk = None
        try:
            out = rotations_to_keys(Rs[1:], tuple(ks[0][:3]))
            outk = [tuple(ks[0][:3])] + out
            maxout = max(maxout, max(abs(v) for k in out for v in k))
            same = sum(tuple(o) == tuple(k[:3]) for o, k in zip(out, ks[1:]))
            wrapd = sum(tuple(o) != tuple(k[:3]) and all((o[c] - k[c]) % UNITS == 0 for c in range(3)) for o, k in zip(out, ks[1:]))
            conv['tracks reproduced exactly' if same == len(out) else 'tracks changed'] += 1
            conv['keys identical'] += same
            conv['keys differing by 4096 multiples (unwrap)'] += wrapd
            conv['keys on another Euler triple of the same rotation'] += len(out) - same - wrapd
            if max(abs(v) for k in out for v in k) > 24576: conv['tracks above 75% of int16 after tracking'] += 1
        except OverflowError as ex:
            conv['tracks REFUSED by the converter'] += 1
            conv['  refusal: ' + str(ex)[:70]] += 1
        had_long, bdev = False, 0.0
        for i in range(len(ks) - 1):
            k0, k1 = ks[i], ks[i + 1]
            g = geodesic(Rs[i], Rs[i + 1]) * D
            dcu = [abs(wrap16(c - p)) for p, c in zip(k0[:3], k1[:3])]
            dc = [u * U2D for u in dcu]
            geoA.append(g); cmpA.append(max(dc))
            if g >= 1.0: ratios.append(sum(dc) / g)
            long_way = max(dcu) >= 2048
            if long_way:
                unwrap['pairs with a component step >= 180 deg'] += 1; had_long = True
            crossing = math.cos(k0[1] * TAU / UNITS) * math.cos(k1[1] * TAU / UNITS) < 0
            if crossing: cross['pairs whose authored y passes +-90 deg'] += 1
            span = k1[3] - k0[3]
            gaps.append(span)
            inside = played[bisect.bisect_right(played, k0[3]):bisect.bisect_left(played, k1[3])]
            if not long_way:
                pot_short.append(pair_dev(k0[:3], k1[:3], Rs[i], Rs[i + 1], (1024, 2048, 3072)))
            if not inside: continue
            geoB.append(g); cmpB.append(max(dc))
            if crossing: cross['  ... of them interpolated in play'] += 1
            ts = [((f - k0[3]) << 12) // span for f in inside]
            dev = pair_dev(k0[:3], k1[:3], Rs[i], Rs[i + 1], ts)
            if long_way:
                real_long.append(dev); unwrap['  ... interpolated in play'] += 1
                continue
            real_short.append(dev); bdev = max(bdev, dev)
            worst.append((dev, b, k0[3], k1[3], g, max(dc), pole_deg(Rs[i]), pole_deg(Rs[i + 1]), crossing, len(inside)))
            if outk is not None:  # the converter's triples for this pair: same rotations, maybe another Euler triple
                dv = pair_dev(outk[i], outk[i + 1], Rs[i], Rs[i + 1], ts)
                conv_short.append(dv)
                cmp_cnt['converter lerp better by > 0.5 deg' if dv < dev - .5 else 'converter lerp worse by > 0.5 deg' if dv > dev + .5 else 'within 0.5 deg'] += 1
            if dev > 1.0 and span >= 2:  # one extra key at the middle frame = the slerp midpoint (a density test, not a retargeter)
                fm = (k0[3] + k1[3]) // 2
                tm = ((fm - k0[3]) << 12) // span
                Rm = slerp(Rs[i], Rs[i + 1], tm / 4096)
                try:
                    km, ke = rotations_to_keys([Rm, Rs[i + 1]], tuple(k0[:3]))
                    left = [((f - k0[3]) << 12) // (fm - k0[3]) for f in inside if k0[3] < f < fm]
                    right = [((f - fm) << 12) // (k1[3] - fm) for f in inside if fm < f < k1[3]]
                    d2 = max(pair_dev(k0[:3], km, Rs[i], Rm, left) if left else 0, pair_dev(km, ke, Rm, Rs[i + 1], right) if right else 0)
                    half['pairs >1 deg, one extra key tried'] += 1
                    half['  ... now <= 1 deg'] += d2 <= 1.0
                    half['  ... improved'] += d2 < dev
                except OverflowError:
                    half['  ... extra key refused'] += 1
            elif dev > 1.0:
                half['pairs >1 deg with a gap of 1 frame (no frame for a key)'] += 1
        if had_long: unwrap['tracks with such a pair'] += 1
        perbone.append((bdev, b, len(ks)))
    R.update(maxorig=maxorig, maxout=maxout, geoA=stats(geoA), cmpA=stats(cmpA), geoB=stats(geoB), cmpB=stats(cmpB), gaps=stats(gaps),
             ratio=stats(ratios), real=stats(real_short), reallong=stats(real_long), pot=stats(pot_short), convdev=stats(conv_short),
             cmp=cmp_cnt, half=half, pole=pole, cross=cross, unwrap=unwrap, conv=conv, A=len(geoA), B=len(geoB))
    worst.sort(reverse=True); R['worst'] = worst[:top]
    perbone.sort(reverse=True); R['bones_worst'] = perbone[:top]
    return R


def fmt(s):
    return f"n={s[0]} mean={s[1]:.2f} p50={s[2]:.2f} p95={s[3]:.2f} p99={s[4]:.2f} max={s[5]:.2f}"


def report(R):
    print(f"\n### {R['name']}: {R['bones']} bones, {R['tracks']} rotation tracks, {R['keys']} keys, {R['motions']} motions, {R['played']} distinct played frames; pairs A={R['A']} B(interpolated in play)={R['B']}")
    print(f"- key gap (frames), A: {fmt(R['gaps'])}")
    print(f"- step, geodesic deg, A: {fmt(R['geoA'])}")
    print(f"- step, geodesic deg, B: {fmt(R['geoB'])}")
    print(f"- step, largest Euler component deg, A: {fmt(R['cmpA'])}")
    print(f"- step, largest Euler component deg, B: {fmt(R['cmpB'])}")
    print(f"- sum|Euler deltas| / geodesic (pairs >= 1 deg, A): {fmt(R['ratio'])}")
    print(f"- lerp vs slerp, REALISED, B excluding long-way pairs, deg: {fmt(R['real'])}")
    print(f"- lerp vs slerp, realised, B long-way pairs only (reference = short way; the game goes the long way as authored), deg: {fmt(R['reallong'])}")
    print(f"- lerp vs slerp, potential at t=.25/.5/.75, A excluding long-way pairs, deg: {fmt(R['pot'])}")
    print(f"- same B pairs through the converter's triples: {fmt(R['convdev'])}; versus authored: {dict(R['cmp'])}")
    print(f"- one extra key at the middle frame (slerp midpoint), realised: {dict(R['half'])}")
    print(f"- {dict(R['pole'])}; {dict(R['cross'])}")
    print(f"- unwrapping: {dict(R['unwrap'])}")
    print(f"- int16: max |value| authored {R['maxorig']}, after the converter {R['maxout']} (limit 32767)")
    print(f"- converter on the authored matrices: {dict(R['conv'])}")
    print("- worst realised short-way pairs: dev deg | bone | frames | geodesic deg | max Euler comp deg | distance from the pole deg at both ends (0 = gimbal, 90 = pitch 0) | crosses pole | played frames inside")
    for w in R['worst']:
        print(f"  {w[0]:7.2f} | b{w[1]} | {w[2]}->{w[3]} | {w[4]:6.2f} | {w[5]:6.2f} | {w[6]:5.1f}/{w[7]:5.1f} | {w[8]} | {w[9]}")
    print("- worst bones by realised dev: " + ", ".join(f"b{b}({d:.2f} deg, {n} keys)" for d, b, n in R['bones_worst']))


if __name__ == '__main__':
    args = sys.argv[1:]
    top = 5
    if args[:1] == ['--top']:
        top = int(args[1]); args = args[2:]
    for a in args:
        p, _, label = a.partition(':')
        report(analyse(label or os.path.basename(p), p, top))
