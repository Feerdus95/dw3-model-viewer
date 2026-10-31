import struct, sys
from pak import chunks

class Bits:
    def __init__(s, b, o=0): s.b, s.o, s.left, s.cur = b, o, 0, 0
    def bit(s):
        s.left -= 1
        if s.left >= 0: return (s.cur >> s.left) & 1
        s.left = 7; s.cur = s.b[s.o]; s.o += 1
        return s.cur >> 7
    def bits(s, n):
        v = 0
        for _ in range(n): v = (v << 1) | s.bit()
        return v

def decompress(b, off=0):
    """LZ + Huffman as decompressLzHuffman (src/main/system/decompress.c)."""
    r = Bits(b, off)
    size = (r.bits(16) << 16) | r.bits(16)
    out = bytearray(); win = bytearray(0x1000); wp = 0xFEE
    decoded = 0x1000; root = 0
    L, R = {}, {}
    nxt = [0x110]
    def tree():
        if r.bit():
            n = nxt[0]; nxt[0] += 1
            L[n] = tree(); R[n] = tree(); return n
        return r.bits(9)
    while len(out) < size:
        if decoded == 0x1000:
            nxt[0] = 0x110; root = tree(); decoded = 0
        s = root
        while s >= 0x110: s = R[s] if r.bit() else L[s]
        decoded += 1
        if s < 0x100:
            out.append(s); win[wp] = s; wp = (wp + 1) & 0xFFF
        else:
            n = s - 0xFD; off2 = r.bits(12)
            for i in range(n):
                c = win[(off2 + i) & 0xFFF]; out.append(c); win[wp] = c; wp = (wp + 1) & 0xFFF
    return bytes(out[:size])

def anim(b):
    """-> (loopKey, [(duration, sound, [32 x (rx,ry,rz,attr,tx,ty,tz,sx,sy,sz)])])"""
    n = struct.unpack_from('<i', b, 0)[0]; keys = []
    for k in range(n):
        o = 4 + k * 772
        dur, snd = struct.unpack_from('<hh', b, o)
        bones = []
        for i in range(32):
            v = struct.unpack_from('<12h', b, o + 4 + i * 24)
            bones.append((v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[8], v[9], v[10]))
        keys.append((dur, snd, bones))
    loop = struct.unpack_from('<h', b, 4 + 4 + 22)[0]
    return loop, keys
