"""E4.2 source-side helpers for glTF clips whose channels have different key times (Rambley mixamo.com: 78 channels, 325..482 keys, 0..15.2 s).
channels(js, b, a, path) -> {node: array resampled to ONE uniform grid (nf = the most common key count, t = linspace(0, tmax))}; rotations are blended component-wise on the same hemisphere and renormalised (dense 30 fps keys).
Channels that already sit on the grid are returned untouched (Soldier: byte-identical behaviour). ALIAS maps the Mixamo names the retargeters use to the Rambley node names (E49_SRC=rambley)."""
import os
import numpy as np
from collections import Counter
from e7_compare import accessor

ALIAS = {'Hips': 'hips_02', 'Spine2': 'chest_038', 'Neck': 'Neck_080', 'Head': 'Head_081'}
for s_, S in (('L', 'Left'), ('R', 'Right')):
    n = {'L': ('thigh.L_03', 'shin.L_04', 'foot.L_05', 'toe.L_06', 'upper_arm.L_040', 'forearm.L_041', 'hand_L_042', 'f_middle.01.L_049'),
         'R': ('thigh.R_013', 'shin.R_014', 'foot.R_015', 'toe.R_016', 'upper_arm.R_059', 'forearm.R_060', 'hand_R_061', 'f_middle.01.R_068')}[s_]
    for k, v in zip(('UpLeg', 'Leg', 'Foot', 'ToeBase', 'Arm', 'ForeArm', 'Hand', 'HandMiddle1'), n): ALIAS[S + k] = v
USE_ALIAS = os.environ.get('E49_SRC') == 'rambley'


def channels(js, b, a, path):
    cs = [c for c in a['channels'] if c['target']['path'] == path]
    nf = Counter(js['accessors'][a['samplers'][c['sampler']]['input']]['count'] for c in a['channels']).most_common(1)[0][0]
    tmax = max(js['accessors'][a['samplers'][c['sampler']]['input']]['max'][0] for c in a['channels']); T = np.linspace(0, tmax, nf)
    out = {}
    for c in cs:
        s = a['samplers'][c['sampler']]; t = accessor(js, b, s['input'])[:, 0].astype(float); v = accessor(js, b, s['output']).astype(float)
        if len(t) == nf and np.allclose(t, T, atol=1e-4): out[c['target']['node']] = v; continue
        if path == 'rotation':
            v = v.copy()
            for i in range(1, len(v)):
                if np.dot(v[i], v[i - 1]) < 0: v[i] = -v[i]
        r = np.stack([np.interp(T, t, v[:, k]) for k in range(v.shape[1])], 1)
        if path == 'rotation': r /= np.linalg.norm(r, axis=1, keepdims=True)
        out[c['target']['node']] = r
    if path == 'translation':      # RambleyRig: raw values are cm in its parent's frame; offsets are 'U' = 1/100 of a cm... with the armature scale normalised to 0.01 positions are U/100, so 1 U = 100 raw cm -> raw x 1e-4
        for k in out:
            if js['nodes'][k].get('name') == 'RambleyRig': out[k] = out[k] * 1e-4
    return out
