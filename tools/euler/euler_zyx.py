"""Rotation matrix -> Euler ZYX with temporal branch tracking. Pure math: radians, float64, no DW3 specifics.

Convention: R = Rz(z) @ Ry(y) @ Rx(x), column vectors, right-handed matrices, rotations positive counter-clockwise about
their axis. Elements: R[2,0] = -sin y, R[2,1] = cos y sin x, R[2,2] = cos y cos x, R[1,0] = sin z cos y, R[0,0] = cos z cos y.

A rotation has two Euler triples per turn-shift: (x, y, z) and (x+pi, pi-y, z+pi), plus any 2*pi shift of each angle.
At cos y = 0 (pitch +-90 deg) only x-z (pitch +90) or x+z (pitch -90) is determined: gimbal lock.
`track` picks, per frame, the triple nearest the previous one (Viterbi over the two families), so the sequence is continuous
and the game's per-component linear interpolation does not take the long way round.
"""
import math
import numpy as np

TAU = 2 * math.pi
GIMBAL_EPS = 1e-6  # cos(y) at or below this is treated as singular (~6e-5 deg, far below DW3's 0.088 deg step)


def compose(x, y, z):
    cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
    return np.array([[cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx],
                     [sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx],
                     [-sy, cy * sx, cy * cx]])


def geodesic(a, b):
    """Angle in radians of the rotation taking a to b (atan2 form: exact near 0, unlike acos)."""
    r = a.T @ b
    v = np.array([r[2, 1] - r[1, 2], r[0, 2] - r[2, 0], r[1, 0] - r[0, 1]]) / 2
    return math.atan2(float(np.linalg.norm(v)), (float(np.trace(r)) - 1) / 2)


def _near(a, ref):
    return a + TAU * round((ref - a) / TAU)


def candidates(R, prev=None):
    """[(family tag, (x, y, z))] for R; with PREV each angle is shifted by 2*pi multiples to lie nearest PREV.
    Generic: two families (tag 0: |y| <= pi/2, tag 1: its alternative). Gimbal: one triple, x kept from PREV (0 without)."""
    cy = math.hypot(R[0, 0], R[1, 0])
    if cy > GIMBAL_EPS:
        x, y, z = math.atan2(R[2, 1], R[2, 2]), math.atan2(-R[2, 0], cy), math.atan2(R[1, 0], R[0, 0])
        fams = [(0, (x, y, z)), (1, (x + math.pi, math.pi - y, z + math.pi))]
    else:
        up = R[2, 0] < 0  # sin y = +1
        x = prev[0] if prev else 0.0
        if up:
            z = x - math.atan2(R[0, 1], R[1, 1])    # x - z = atan2(R01, R11)
        else:
            z = math.atan2(-R[0, 1], R[1, 1]) - x   # x + z = atan2(-R01, R11)
        fams = [(0, (x, math.pi / 2 if up else -math.pi / 2, z))]
    if prev is None:
        return fams[:1]
    return [(t, tuple(_near(a, p) for a, p in zip(ang, prev))) for t, ang in fams]


def track(Rs, start=None):
    """Continuous Euler triples (radians) for a sequence of rotation matrices.
    START: the triple the sequence continues from (not returned); without it the first frame is the principal solution
    (|y| <= pi/2, x and z in (-pi, pi]). Cost = sum of squared angle steps; two survivor paths (one per family) are kept."""
    surv = [(0.0, (tuple(start), None))] if start is not None else []
    for R in Rs:
        if not surv:
            surv = [(0.0, (candidates(R)[0][1], None))]
            continue
        new = {}
        for cost, node in surv:
            prev = node[0]
            for tag, ang in candidates(R, prev):
                c = cost + sum((a - b) ** 2 for a, b in zip(ang, prev))
                if tag not in new or c < new[tag][0]:
                    new[tag] = (c, (ang, node))
        surv = list(new.values())
    node = min(surv, key=lambda s: s[0])[1] if surv else None
    out = []
    while node:
        out.append(node[0])
        node = node[1]
    out.reverse()
    return out[1:] if start is not None else out


def quat(R):
    t = np.trace(R)
    if t > 0:
        s = math.sqrt(t + 1) * 2
        q = [(R[2, 1] - R[1, 2]) / s, (R[0, 2] - R[2, 0]) / s, (R[1, 0] - R[0, 1]) / s, s / 4]
    else:
        i = int(np.argmax(np.diag(R))); j, k = (i + 1) % 3, (i + 2) % 3
        s = math.sqrt(R[i, i] - R[j, j] - R[k, k] + 1) * 2
        q = [0.0] * 4; q[i] = s / 4; q[j] = (R[j, i] + R[i, j]) / s; q[k] = (R[k, i] + R[i, k]) / s; q[3] = (R[k, j] - R[j, k]) / s
    return np.array(q)


def unquat(q):
    x, y, z, w = q / np.linalg.norm(q)
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def slerp(a, b, t):
    qa, qb = quat(a), quat(b)
    if qa @ qb < 0: qb = -qb
    d = min(1.0, qa @ qb)
    th = math.acos(d)
    if th < 1e-9: return a
    return unquat((math.sin((1 - t) * th) * qa + math.sin(t * th) * qb) / math.sin(th))
