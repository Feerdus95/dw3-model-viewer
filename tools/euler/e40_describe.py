"""E4.0 (READ-ONLY): structural description of one DW3 model as JSON (schema dw3-model-description/1, see kb/e4/schema-description-v1.md).
Facts only: every field says where it comes from (parser / derived / inferred). It records hierarchy, reference transforms, per-bone geometry, motions, file limits and the TRAPS/AMBIGUITIES
found (geometric facts, never anatomy). Run: python e40_describe.py ID [ID ...]  (ids like 0499; needs build/dw3mv.exe for the geometry; writes kb/e4/models/<id>.json and <id>-report.md)"""
import os, sys, json, math, subprocess, tempfile, glob
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, ".."))
import survey_keys as sk
from e7_compare import load_raw, Player, pose_track, read_glb, accessor
from e8_resample import parents_of, World
from e13_arm import nrot
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..')); VIEWER = os.path.join(ROOT, 'viewer'); OUT = os.path.join(ROOT, 'kb', 'e4', 'models')
D = 180 / math.pi
r1 = lambda x: round(float(x), 1)


def geometry(binpath, n):
    """per-bone vertex count, centroid and bbox in the bone's own frame, DW3 axes/units (x left, y down, z back), measured in the export's zero-rotation bind pose (not the idle)."""
    tmp = tempfile.mkdtemp(prefix='e40_')
    subprocess.run([os.path.join(VIEWER, 'build', 'dw3mv.exe'), '--export-gltf', binpath, tmp.replace('\\', '/')], capture_output=True, timeout=120)
    g = glob.glob(os.path.join(tmp, '*.glb'))
    if not g: return None, 'export failed'
    js, b = read_glb(g[0]); N = js['nodes']; par = {c: i for i, nd in enumerate(N) for c in nd.get('children', [])}
    bn = {i: int(nd['name'].split('_')[1]) for i, nd in enumerate(N) if nd['name'].startswith('bone_')}
    def wpos(i):
        p = np.zeros(3)
        while i is not None: p = p + np.array(N[i].get('translation', [0, 0, 0])); i = par.get(i)
        return p
    to_dw = lambda v: np.array([v[0], -v[1], -v[2]]) * 100          # glTF y-up, z flipped, /100 -> DW3 units
    joints = js['skins'][0]['joints']; verts = {}
    for m in js['meshes']:
        for pr in m['primitives']:
            P = accessor(js, b, pr['attributes']['POSITION']).astype(float); ja = js['accessors'][pr['attributes']['JOINTS_0']]; jv_ = js['bufferViews'][ja['bufferView']]; jo = jv_.get('byteOffset', 0) + ja.get('byteOffset', 0)
            J = np.frombuffer(b, {5121: np.uint8, 5123: np.uint16}[ja['componentType']], ja['count'] * 4, jo).reshape(ja['count'], 4)[:, 0].astype(int)      # the shared accessor() reads everything as float32; joints are integers
            for jv in set(J.tolist()):
                verts.setdefault(bn[joints[int(jv)]], []).append(P[J == jv])
    out = {}
    for k in range(1, n + 1):
        node = next(i for i, v in bn.items() if v == k); c = wpos(node)
        if k in verts:
            V = np.vstack(verts[k]); L = np.array([to_dw(v - c) for v in V])
            out[k] = dict(vertices=len(V), centroid=[round(float(x)) for x in L.mean(0)], bbox_min=[round(float(x)) for x in L.min(0)], bbox_max=[round(float(x)) for x in L.max(0)])
        else: out[k] = None
    return out, 'ok'


def describe(mid):
    path = os.path.join(VIEWER, 'data', 'files', f'{mid}.bin'); raw = open(path, 'rb').read(); a = sk.Arc(raw)
    tr, mo = load_raw(path); par = parents_of(path); n = len(tr); pl = Player(tr, mo); fr = pl.idle
    ref = [[pose_track(tr[b][t], fr) for t in range(3)] for b in range(n)]; W = World(par, n).update(ref)
    ch = {}
    for b in range(1, n + 1): ch.setdefault(par[b - 1], []).append(b)
    depth = {}
    for b in range(1, n + 1):
        d, x = 0, b
        while par[x - 1]: x = par[x - 1]; d += 1
        depth[b] = d
    geo, gstat = geometry(path, n)
    ang_of = lambda R: math.acos(max(-1, min(1, (np.trace(R) - 1) / 2))) * D
    bones = []
    for b in range(1, n + 1):
        keys = [[k for k in tr[b - 1][t] if k[3] < 30000] for t in range(3)]
        R = nrot(W[b])
        bones.append(dict(id=b, parent=par[b - 1], children=ch.get(b, []), depth=depth[b], ref_offset=list(ref[b - 1][0]), ref_local_rot_int16=list(ref[b - 1][1]), ref_scale_4096=list(ref[b - 1][2]),
                          ref_world_pos=[r1(x) for x in W[b][:3, 3]], ref_world_rot_from_identity_deg=r1(ang_of(R)),
                          keys=dict(pos=len(keys[0]), rot=len(keys[1]), scale=len(keys[2])), animated=dict(pos=len({k[:3] for k in keys[0]}) > 1, rot=len({k[:3] for k in keys[1]}) > 1, scale=len({k[:3] for k in keys[2]}) > 1),
                          geometry=geo.get(b) if geo else None))
    from survey_real import played_frames
    used = played_frames(mo); maxk = max([k[3] for b in range(n) for t in range(3) for k in tr[b][t] if k[3] < 30000] or [0])
    off = lambda b: np.array(ref[b - 1][0], float)
    def chain(c):
        out = [c]
        while len(ch.get(out[-1], [])) == 1: out.append(ch[out[-1]][0])
        return out
    groups = []
    for p, kids in ch.items():
        for i in range(len(kids)):
            for j in range(i + 1, len(kids)):
                x, y = kids[i], kids[j]; ox, oy = off(x), off(y)
                if np.linalg.norm(ox) < 20 or abs(ox[0]) < 10: continue
                if np.linalg.norm(ox - np.array([-oy[0], oy[1], oy[2]])) > 0.15 * np.linalg.norm(ox) + 5: continue
                cx, cy = chain(x), chain(y)
                if len(cx) != len(cy): continue
                d = W[cx[-1]][:3, 3] - (W[p][:3, 3] if p else 0); dn = np.linalg.norm(d)
                groups.append(dict(parent=p, chain_a=cx, chain_b=cy, length=len(cx), direction='down' if d[1] > 0.5 * dn else ('up' if d[1] < -0.3 * dn else 'side'), span_units=round(float(dn))))
    traps, amb = [], []
    for B in bones:
        if tuple(B['ref_scale_4096']) != (4096, 4096, 4096): traps.append(dict(id='non_unit_scale', bones=[B['id']], fact=f"reference scale {B['ref_scale_4096']}"))
    for g in groups:
        ca, cb = g['chain_a'], g['chain_b']
        ra, rb = [ang_of(nrot(W[b])) for b in ca], [ang_of(nrot(W[b])) for b in cb]
        if max(abs(x - y) for x, y in zip(ra, rb)) > 15:
            traps.append(dict(id='asymmetric_reference_pair', bones=ca + cb, fact=f'mirrored chains {ca} / {cb}: reference world rotation from identity {[round(x) for x in ra]} vs {[round(x) for x in rb]} deg', consequence="the two sides do not share a reference: do not copy one side's reference/definition to the other"))
        for c in (ca, cb):
            if len(c) >= 3:
                dirs = [off(c[i + 1]) / (np.linalg.norm(off(c[i + 1])) + 1e-9) for i in range(len(c) - 1)]
                seg = lambda k: nrot(W[c[k]]) @ dirs[k]
                bend = [r1(math.degrees(math.acos(max(-1.0, min(1.0, float(np.dot(seg(k), seg(k + 1)))))))) for k in range(len(c) - 2)]
                if any(x > 60 for x in bend): traps.append(dict(id='bent_reference_chain', bones=c, fact=f'angle between consecutive segment directions in the reference pose {bend} deg', consequence='the reference is not a straight/neutral pose; knee-like hinges are not at 0'))
    terminals = sorted({c[-1] for g in groups for c in (g['chain_a'], g['chain_b'])} | {c[-2] for g in groups for c in (g['chain_a'], g['chain_b']) if len(c) >= 3})
    for t in terminals:
        wa = bones[t - 1]['ref_world_rot_from_identity_deg']
        if wa > 15: traps.append(dict(id='terminal_not_world_aligned', bones=[t], fact=f'reference world rotation {wa} deg from identity', consequence='a flat/neutral terminal reference cannot be assumed to be the idle'))
    if not groups: traps.append(dict(id='no_mirrored_groups', bones=[], fact='no pair of sibling chains with mirrored offsets', consequence='left/right cannot be derived from the hierarchy'))
    kids0 = ch.get(0, [])
    if len(kids0) > 1: amb.append(dict(id='multiple_root_children', bones=kids0, fact=f'control block 0 has {len(kids0)} children {kids0}; any of them can be the pelvis/torso'))
    dirs = [g['direction'] for g in groups]
    if dirs.count('down') >= 2 and len({g['parent'] for g in groups if g['direction'] == 'down'}) > 1: amb.append(dict(id='several_down_pairs', bones=[g['chain_a'][0] for g in groups if g['direction'] == 'down'], fact='two or more mirrored pairs hang downward from different parents (leg pairs vs a quadruped front/back)'))
    if any(g['direction'] in ('up', 'side') and g['length'] >= 2 for g in groups): amb.append(dict(id='non_down_pairs', bones=[g['chain_a'][0] for g in groups if g['direction'] != 'down'], fact='mirrored pairs that do not hang downward: arms, wings or other appendages cannot be told apart from geometry alone'))
    if geo:
        nog = [B['id'] for B in bones if not B['geometry']]
        if nog: traps.append(dict(id='bones_without_geometry', bones=nog, fact='no vertex is skinned to these bones', consequence='pure joints; their mesh centroid cannot be used for matching'))
    if gstat != 'ok': traps.append(dict(id='geometry_unavailable', bones=[], fact=gstat))
    desc = dict(schema='dw3-model-description/1', model=mid, source=f'viewer/data/files/{mid}.bin', file=dict(bytes=len(raw), entries=a.n(), bone_entry=sk.find_bone(a), motions=len(mo)),
        provenance=dict(parser=['bones.parent', 'bones.ref_*', 'bones.keys', 'motions', 'file'], derived=['ref_world_pos', 'ref_world_rot_from_identity_deg', 'geometry (per-bone vertices from the viewer glTF export, bone-local, zero-rotation bind pose)', 'mirrored_groups', 'frames'], inferred=['coordinate_convention']),
        coordinate_convention=dict(up='-y', forward='-z', left='+x', status='observed on Agumon (E1.x); same engine for all models but not verified per model'),
        reference_pose=dict(source='idle motion first frame (Player.idle)', frame=fr, note='the file has no bind pose; the idle pose is not guaranteed neutral'),
        frames=dict(max_key_frame=maxk, played_frames=len(used), free_from=maxk + 1, limit=30000, note='limits of key archives / motion table not yet verified (E4.1 precondition)'),
        structure=dict(bones=n, max_depth=max(depth.values()), leaves=[b for b in range(1, n + 1) if b not in ch], root_children=kids0, mirrored_groups=groups, bbox_ref_world=[[r1(x) for x in np.min([B['ref_world_pos'] for B in bones], 0)], [r1(x) for x in np.max([B['ref_world_pos'] for B in bones], 0)]]),
        bones=bones, motions=[dict(id=i + 1, steps=[list(s) for s in m]) for i, m in enumerate(mo)], traps=traps, ambiguities=amb)
    os.makedirs(OUT, exist_ok=True); json.dump(desc, open(os.path.join(OUT, f'{mid}.json'), 'w'), separators=(',', ':'))
    rep = [f'# {mid}: description report', f'bones {n}, depth {max(depth.values())}, leaves {len(desc["structure"]["leaves"])}, motions {len(mo)}, max key frame {maxk}, geometry: {gstat}', '', '## mirrored groups (geometric)'] + [f"- parent {g['parent']}: {g['chain_a']} / {g['chain_b']} {g['direction']} span {g['span_units']}" for g in groups] + ['', '## traps'] + [f"- **{t['id']}** bones {t['bones']}: {t['fact']}" + (f" -> {t['consequence']}" if 'consequence' in t else '') for t in traps] + ['', '## ambiguities'] + [f"- **{t['id']}** bones {t['bones']}: {t['fact']}" for t in amb]
    open(os.path.join(OUT, f'{mid}-report.md'), 'w', encoding='utf-8').write('\n'.join(rep) + '\n'); print(rep[1]); return desc


if __name__ == '__main__':
    for m in sys.argv[1:]: describe(m)
