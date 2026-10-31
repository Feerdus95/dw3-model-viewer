"""E4.1b research (read-only): how the game reaches a motion number. Fighters file (fid 460) -> FighterInfo {model, motions, effects} -> the model file's
effects archive = battle scripts; script op 2 (model), cmd 0, arg>=2 plays motion arg+1 (table[arg]). This lists, per fighter of the corpus, the motion slots that scripts
name, the slots in use and the free ones. Run: python e42_select.py [fid...]  -> kb/e42-select-output.txt"""
import os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__)); FILES = os.path.join(HERE, '..', '..', 'data', 'files')
u16 = lambda b, o: struct.unpack_from('<H', b, o)[0]; s16 = lambda b, o: struct.unpack_from('<h', b, o)[0]; u32 = lambda b, o: struct.unpack_from('<I', b, o)[0]


def script_motions(b, o, end):
    """Walk one script like game.cpp read_script; return (motion args named by op 2 cmd 0, ok)"""
    out = []; p = o
    def nx():
        nonlocal p
        v = s16(b, p) if p + 2 <= end else 0; p += 2; return v
    for _ in range(500):
        if p >= end: return out, False
        op = nx()
        if op in (0, 0xFF, 255, -1): return out, True
        if op == 1: nx()
        elif op == 2:
            cmd = nx(); nx(); arg = 0
            if cmd not in (1, 2, 3, 4, 7, 8, 9): arg = nx()
            if cmd == 0 and arg >= 2: out.append(arg)
            elif cmd == 3: [nx() for _ in range(4)]
            elif cmd == 4: [nx() for _ in range(4)]
            elif cmd == 5 and arg == 4: nx()
            elif cmd in (7, 8): nx()
        elif op == 3:
            mode = nx(); nx()
            if mode != 1: [nx() for _ in range(3)]
        elif op == 4:
            mode = nx(); i = nx()
            if i != -1 and mode != 1: nx(); nx()
        elif op == 5:
            nx(); mode = nx()
            [nx() for _ in range(14 if mode == 1 else 1)]
        elif op == 6:
            mode = nx(); nx()
            if mode != 1: [nx() for _ in range(6)]
        elif op == 7: nx(); nx()
        elif op in (8, 9): pass
        elif op == 10: nx(); nx()
        elif op == 11: nx()
        else: return out, False
    return out, False


def fighters():
    d = open(os.path.join(FILES, '0460.bin'), 'rb').read()
    base, ent, par, ene = u32(d, 0), u32(d, 4), u32(d, 8), u32(d, 12); res = []; e = ent
    while e + 4 <= len(d) and u16(d, e):
        fid, idx, kind = u16(d, e), d[e + 2], d[e + 3]; enemy = kind >= 0x3A
        o = ene + idx * 0x48 if enemy else par + idx * 0xC4
        res.append(dict(id=fid, enemy=enemy, model=u32(d, o), motions=u32(d, o + 4), effects=u32(d, o + 8))); e += 4
    return res


def main():
    want = set(sys.argv[1:]); lines = []; log = lambda s: (print(s), lines.append(s))
    fs = fighters(); log(f'fighters: {len(fs)}; same file for model/motions/effects: {sum(f["model"] >> 16 == f["motions"] >> 16 == f["effects"] >> 16 for f in fs)}')
    seen = set(); stats = dict(files=0, free=0, named_free=0, scripts=0, bad=0)
    for f in fs:
        fid = f['model'] >> 16
        if fid in seen or (want and f'{fid:04d}' not in want): continue
        seen.add(fid); path = os.path.join(FILES, f'{fid:04d}.bin')
        if not os.path.exists(path) or f['motions'] >> 16 != fid or f['effects'] >> 16 != fid: continue
        b = open(path, 'rb').read(); me, ee = f['motions'] & 0xFFFF, f['effects'] & 0xFFFF
        mo, eo = u32(b, me * 4), u32(b, ee * 4)
        if not (0 < mo < len(b) and 0 < eo < len(b)): continue
        n = u32(b, mo) // 4; used = [i for i in range(n) if u32(b, mo + i * 4)]
        en = u32(b, eo) // 4; eend = min([u32(b, ee * 4 + 4 * k) for k in range(1, 3) if False] + [len(b)])
        named = set(); bad = 0
        for i in range(en):
            so = u32(b, eo + i * 4)
            if not so: continue
            m, ok = script_motions(b, eo + so, len(b)); named |= set(m); bad += not ok
        stats['files'] += 1; stats['scripts'] += en; stats['bad'] += bad
        free = [i for i in range(n) if i not in used]; nf = sorted(a for a in named if a < n and a not in used)
        stats['free'] += len(free); stats['named_free'] += len(nf)
        log(f'{fid:04d} fighter {f["id"]} enemy {f["enemy"]}: motion table {n} slots, used {len(used)}, free {len(free)} (first {free[:3]}); scripts {en}; slots named by scripts {sorted(named)}; named but free {nf}; unparsed scripts {bad}')
    log(str(stats))
    open(os.path.join(HERE, '..', '..', '..', 'kb', 'e42-select-output.txt'), 'w', encoding='utf-8').write('\n'.join(lines) + '\n')


main()
