"""Reads files out of a PlayStation disc image (.cue/.bin raw 2352, or a plain 2048 .iso), no extraction tool needed."""
import re
import struct
from pathlib import Path


class Disc:
    def __init__(self, path):
        p = Path(path)
        if p.suffix.lower() == ".cue":
            m = re.search(r'FILE\s+"([^"]+)"', p.read_text(errors="replace"))
            p = p.parent / m.group(1)
        self.f = open(p, "rb")
        size = p.stat().st_size
        # raw sectors (2352, user data after a 24-byte header) or cooked ones (2048)
        self.stride, self.skip = (2352, 24) if size % 2352 == 0 else (2048, 0)
        self._files = None

    def sector(self, n):
        self.f.seek(n * self.stride + self.skip)
        return self.f.read(2048)

    def read(self, lba, size):
        return b"".join(self.sector(lba + i) for i in range((size + 2047) // 2048))[:size]

    def files(self):
        """{first sector: (path, size)} for every file, from the ISO9660 path table."""
        if self._files is None:
            self._files = {}
            pvd = self.sector(16)
            lba, size = struct.unpack("<I", pvd[140:144])[0], struct.unpack("<I", pvd[132:136])[0]
            pt, p, dirs = self.sector(lba)[:size], 0, []
            while p < size:
                nl = pt[p]
                dirs.append((struct.unpack("<I", pt[p + 2:p + 6])[0], struct.unpack("<H", pt[p + 6:p + 8])[0], pt[p + 8:p + 8 + nl].decode("latin1")))
                p += 8 + nl + (nl & 1)

            def full(i):
                _, par, n = dirs[i]
                return "" if i == 0 else full(par - 1) + n + "/"

            for i, (e, _, _) in enumerate(dirs):
                dsize = struct.unpack("<I", self.sector(e)[10:14])[0]
                d, q, base = self.read(e, dsize), 0, full(i)
                while q < dsize:
                    n = d[q]
                    if n == 0:
                        q = (q // 2048 + 1) * 2048
                        continue
                    flags, nl = d[q + 25], d[q + 32]
                    name = d[q + 33:q + 33 + nl].decode("latin1")
                    if not flags & 2:
                        self._files[struct.unpack("<I", d[q + 2:q + 6])[0]] = (base + name.split(";")[0], struct.unpack("<I", d[q + 10:q + 14])[0])
                    q += n
        return self._files

    def find(self, path):
        """(first sector, size) of a file by its path, or None."""
        return next(((l, s) for l, (p, s) in self.files().items() if p == path), None)
