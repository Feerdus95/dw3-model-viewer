"""DW3 key format on top of euler_zyx: int16 Euler, 4096 = 360 deg, R = Rz*Ry*Rx (viewer/src/model.cpp:691-706),
and a bit-exact model of the game's per-component linear interpolation (model.cpp:327-335)."""
import math
from euler_zyx import TAU, compose, track

UNITS = 4096
STEP_DEG = 360 / UNITS  # 0.0879 deg: quantisation step; the rounding error is at most half of it per angle


def to_units(a):
    return math.floor(a * UNITS / TAU + 0.5)


def from_units(u):
    return u * TAU / UNITS


def wrap16(v):
    return (v + 32768) % 65536 - 32768


def key_matrix(k):
    return compose(*(from_units(u) for u in k))


def rotations_to_keys(Rs, start_keys=None):
    """Matrices -> int16 (x, y, z) keys in DW3 units, continuous. START_KEYS: the key the run continues from.
    Raises OverflowError when a value leaves int16 (about +-8 turns of unwrapped angle) or two consecutive keys differ by
    16384 units (a half turn of one angle) or more, where the game's int16 difference starts to wrap."""
    start = tuple(from_units(u) for u in start_keys) if start_keys is not None else None
    keys = [tuple(to_units(a) for a in ang) for ang in track(Rs, start)]
    prev = start_keys
    for i, k in enumerate(keys):
        if any(not -32768 <= u <= 32767 for u in k):
            raise OverflowError(f"key {i} {k} leaves int16")
        if prev is not None and any(abs(a - b) >= 16384 for a, b in zip(k, prev)):
            raise OverflowError(f"key {i} {k} jumps {prev} -> {k}: half a turn or more in one angle")
        prev = k
    return keys


def dw3_lerp(prev, cur, t):
    """out = prev + (int16)(((int16)(cur - prev) * t) >> 12) per component, t in 0..4096 (model.cpp:327-335)."""
    return tuple(wrap16(p + wrap16((wrap16(c - p) * t) >> 12)) for p, c in zip(prev, cur))
