"""E4.2 read-only rotation test: rotate ONE bone of the 0526 reference pose by a small angle (+-DELTA int16 units on each local axis, 4096 = 360 deg),
re-chain the hierarchy and list which bones' mesh centroids move (and by how much). Only the description kb/e4/models/<id>.json is used; no binary is read or written.
Run: python e47_rotbone.py 0526 1 13 -> kb/e47-rotbone-output.txt"""
import json, math, os, sys
HERE = os.path.dirname(os.path.abspath(__file__)); KB = os.path.join(HERE, '..', '..', '..', 'kb')
mid = sys.argv[1]; tests = [int(x) for x in sys.argv[2:]]; DELTA = 114   # ~10 degrees
B = {b['id']: b for b in json.load(open(os.path.join(KB, 'e4', 'models', f'{mid}.json')))['bones']}


def rot(a):
    x, y, z = [v * 2 * math.pi / 4096 for v in a]
    Rx = [[1, 0, 0], [0, math.cos(x), -math.sin(x)], [0, math.sin(x), math.cos(x)]]
    Ry = [[math.cos(y), 0, math.sin(y)], [0, 1, 0], [-math.sin(y), 0, math.cos(y)]]
    Rz = [[math.cos(z), -math.sin(z), 0], [math.sin(z), math.cos(z), 0], [0, 0, 1]]
    mm = lambda A, C: [[sum(A[i][k] * C[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
    return mm(Rz, mm(Ry, Rx))


mm = lambda A, C: [[sum(A[i][k] * C[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
mv = lambda A, v: [sum(A[i][k] * v[k] for k in range(3)) for i in range(3)]


def centroids(over=None):
    """world centroid of every bone's geometry; over = (bone, extra euler added to its local rotation)"""
    Rw, Pw, out = {}, {}, {}
    def go(i):
        if i in Rw: return
        p = B[i]['parent']; e = list(B[i]['ref_local_rot_int16'])
        if over and over[0] == i: e = [e[k] + over[1][k] for k in range(3)]
        L = rot(e)
        if p in B:
            go(p); Rw[i] = mm(Rw[p], L)
            lp = [B[i]['ref_world_pos'][k] - B[p]['ref_world_pos'][k] for k in range(3)]       # reference offset in the parent's reference frame is not recoverable without the parent's reference rotation,
            Pw[i] = None                                                                         # so positions are rebuilt below from the reference world offsets rotated by the parent's change
        else: Rw[i] = L
    for i in B: go(i)
    return Rw


# World position of bone i after rotating `over`: pos stays for bones outside the subtree; inside it, p' = P_over + dR * (p - P_over), where dR = R'_over * R_over^-1 is the change of the bone's world rotation.
def inv(R): return [[R[j][i] for j in range(3)] for i in range(3)]
def subtree(r): return [i for i in B if (lambda f: f(f, i))(lambda f, j: j == r or (B[j]['parent'] in B and f(f, B[j]['parent'])))]
R0 = centroids()
lines = []
def log(s): print(s); lines.append(s)
for r in tests:
    sub = set(subtree(r)); log(f'== rotating bone {r} (parent {B[r]["parent"]}, ref pos {B[r]["ref_world_pos"]}, subtree {sorted(sub)}, geometry vertices of the subtree {sum(B[i]["geometry"]["vertices"] for i in sub if B[i].get("geometry"))} of {sum(b["geometry"]["vertices"] for b in B.values() if b.get("geometry"))})')
    for ax, name in enumerate('XYZ'):
        for sgn in (1, -1):
            d = [0, 0, 0]; d[ax] = sgn * DELTA
            # local rotation change: R'_local = rot(e + d); world delta in the parent frame
            R1 = centroids((r, d)); dR = mm(R1[r], inv(R0[r])); P = B[r]['ref_world_pos']
            moved = []
            for i in B:
                g = B[i].get('geometry')
                if not g: continue
                c0 = [B[i]['ref_world_pos'][k] + mv(R0[i], g['centroid'])[k] for k in range(3)]
                if i in sub:
                    q = [c0[k] - P[k] for k in range(3)]; c1 = [P[k] + mv(dR, q)[k] for k in range(3)]
                else: c1 = c0
                disp = math.dist(c0, c1)
                if disp > 0.5: moved.append((i, round(disp), [round(c1[k] - c0[k]) for k in range(3)]))
            if sgn == 1: log(f'  {name}{"+" if sgn > 0 else "-"}{DELTA}: bones whose geometry moves {[m[0] for m in moved]}; largest displacements {sorted(moved, key=lambda m: -m[1])[:4]}')
    out_ids = sorted(i for i in B if i not in sub and B[i].get('geometry')); log(f'  geometry that does NOT move: bones {out_ids}')
open(os.path.join(KB, 'e47-rotbone-output.txt'), 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
