"""E4.2 inspection of a model's proposed mapping: four orthographic views (front, side, back, top) of the reference skeleton from kb/e4/models/<id>.json, chains coloured, bone numbers, pivots, local axes (X red, Y green, Z blue)
of the bones named in the mapping, the mesh centroid of every bone (square). World positions come from the description; the local axes from ref_local_rot_int16 (R = Rz*Ry*Rx, 4096 = 360 deg), chained down the hierarchy.
Run: python e46_inspect.py 0526 -> kb/e4/models/<id>-inspect.png"""
import json, math, os, sys
from PIL import Image, ImageDraw
HERE = os.path.dirname(os.path.abspath(__file__)); KB = os.path.join(HERE, '..', '..', '..', 'kb', 'e4')
mid = sys.argv[1] if len(sys.argv) > 1 else '0526'
d = json.load(open(os.path.join(KB, 'models', f'{mid}.json'))); mp = json.load(open(os.path.join(KB, 'examples', f'{mid}.mapping.json')))
B = {b['id']: b for b in d['bones']}


def rot(a):
    x, y, z = [v * 2 * math.pi / 4096 for v in a]
    Rx = [[1, 0, 0], [0, math.cos(x), -math.sin(x)], [0, math.sin(x), math.cos(x)]]
    Ry = [[math.cos(y), 0, math.sin(y)], [0, 1, 0], [-math.sin(y), 0, math.cos(y)]]
    Rz = [[math.cos(z), -math.sin(z), 0], [math.sin(z), math.cos(z), 0], [0, 0, 1]]
    mm = lambda A, C: [[sum(A[i][k] * C[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
    return mm(Rz, mm(Ry, Rx))


W = {}
def world(i):
    if i in W: return W[i]
    p = B[i]['parent']; L = rot(B[i]['ref_local_rot_int16'])
    W[i] = L if p == 0 or p not in B else [[sum(world(p)[r][k] * L[k][c] for k in range(3)) for c in range(3)] for r in range(3)]
    return W[i]


chains = {}; col = {}
palette = {'leg_L': (230, 60, 60), 'leg_R': (60, 90, 230), 'arm_L': (240, 150, 40), 'arm_R': (60, 180, 200)}
for l in mp['limbs']:
    seg = l['segments']; ids = [seg[k]['bone'] for k in ('upper', 'lower', 'terminal') if 'bone' in seg[k]] + [x['bone'] for x in seg['digits'] if 'bone' in x]
    chains[l['id']] = ids
    for i in ids: col[i] = palette[l['id']]
for k, i in (('pelvis', mp['central']['pelvis'].get('bone')), ('torso', mp['central']['torso'].get('bone')), ('head', mp['central']['head'].get('bone'))):
    if i: col[i] = {'pelvis': (170, 60, 200), 'torso': (40, 150, 60), 'head': (120, 120, 120)}[k]
for i in mp['central'].get('other', {}).get('tail', []): col[i] = (150, 110, 60)
axes_for = set(col)
pos = {i: B[i]['ref_world_pos'] for i in B}
cen = {}
for i in B:
    g = B[i].get('geometry')
    if g:
        R = world(i); c = g['centroid']; cen[i] = [pos[i][r] + sum(R[r][k] * c[k] for k in range(3)) for r in range(3)]
views = {'front (model faces the viewer; its +x is on the viewer\'s right)': lambda p: (p[0], p[1]), 'side (model faces right)': lambda p: (-p[2], p[1]),
         'back': lambda p: (-p[0], p[1]), 'top (forward = up)': lambda p: (p[0], p[2])}
PW, PH = 800, 760; img = Image.new('RGB', (PW * 2, PH * 2), (250, 250, 250)); dr = ImageDraw.Draw(img)
xs = [v for p in pos.values() for v in (p[0], p[2])]; ys = [p[1] for p in pos.values()]
S = 0.62
for n, (title, f) in enumerate(views.items()):
    ox, oy = (n % 2) * PW + PW // 2, (n // 2) * PH + PH // 2 + (60 if n < 2 else 0)
    T = lambda p: (ox + f(p)[0] * S, oy + (f(p)[1] + (500 if n < 2 else 0)) * S - (150 if n < 2 else 0))
    dr.text(((n % 2) * PW + 10, (n // 2) * PH + 8), title, fill=(0, 0, 0))
    for i in B:
        p = B[i]['parent']
        if p in B: dr.line([T(pos[p]), T(pos[i])], fill=col.get(i, (170, 170, 170)), width=4 if i in col else 2)
    for i in B:
        if i in cen:
            c = T(cen[i]); dr.rectangle([c[0] - 3, c[1] - 3, c[0] + 3, c[1] + 3], outline=(90, 90, 90))
    for i in B:
        a = T(pos[i]); dr.ellipse([a[0] - 5, a[1] - 5, a[0] + 5, a[1] + 5], fill=col.get(i, (170, 170, 170)), outline=(0, 0, 0))
        dr.text((a[0] + 7, a[1] - 12), str(i), fill=(0, 0, 0))
        if i in axes_for:
            R = world(i)
            for k, cc in enumerate(((220, 0, 0), (0, 170, 0), (0, 0, 220))):
                e = T([pos[i][r] + 70 * R[r][k] for r in range(3)]); dr.line([a, e], fill=cc, width=1)
dr.text((10, PH * 2 - 30), 'violet pelvis, green torso, grey head, brown tail, red/orange left (+x) limbs, blue/cyan right limbs; X red, Y green, Z blue local axes; squares = mesh centroids (bone-local centroid rotated by the reference world rotation)', fill=(0, 0, 0))
out = os.path.join(KB, 'models', f'{mid}-inspect.png'); img.save(out); print(out)
