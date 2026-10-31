"""E4.1 step 1 (READ-ONLY): the real limits of the DW3 model archive format, from the engine (dw3_decomp fightstg.c, file_cache.c) and measured over every parsed model.
Engine facts [F] (fightstg.c / model.h / file_cache.c):
  - archive = u32 offset table from the start of the entry, entry i at table[i]; the engine never reads an entry's size (getArchiveEntry masks the index with 0xFFFF, no bounds check).
  - a motion = MotionStep {s16 index, count, frame, endFrame} list ending with index 0x7FFF; model->keyframes/blendTargets/blendSources are u16[0x640] each: a motion may hold at most 1600 ticks
    (setMotion has no bound check: more overruns into the next arrays and finally into the function pointers). Each step adds: count ticks (a play step: frames frame..frame+count-1; a blend step: count blend ticks) or 1 (a count-0 step).
  - keyframe values are u16: a tick value with bit 0x8000 is a blend tick (so a played frame must be < 0x8000); a count-0 step with frame 0x8000 jumps to keyframe index endFrame (LOOP); with frame -1 (0xFFFF) ends the motion (motionDone; the game returns to idle
    when the previous blend went to -1); a blend step with endFrame 0 blends from step[-1].endFrame (or the live pose if it is the first step, index 0) to step[1].frame (-1: the idle's frame).
  - poseBone searches each track for the first key with frame >= wanted: a wanted frame below the first key reads key[-1] (before the array); above the last real key it interpolates towards the sentinel key. So every used frame must lie inside [first key, last key] of every track.
  - the file is read whole into the heap: the size on disc is the file table's sector count (CD_SECTOR_SIZE 2048); the format itself only needs u32 offsets.
Measured here: per model the motion archive, ticks per motion, key counts per track, frames, alignment of offsets and sizes, file sizes. Run: python e41_limits.py [DIR] ; 4 workers ; writes kb/e41-limits-output.txt"""
import os, sys, glob, struct, collections
from multiprocessing import Pool
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, ".."))
import survey_keys as sk
from survey_real import read_motions


def ticks(steps):
    t = 0
    for s in steps:
        if s[0] == 0x7FFF: break
        t += s[1] if s[1] else 1
        if s[1] == 0: break            # a count-0 step ends the layout (loop or end)
    return t


def analyse(path):
    try:
        raw = open(path, 'rb').read(); a = sk.Arc(raw); n = a.n()
        if n <= 0 or n * 4 > len(raw): return path, None
        bi = sk.find_bone(a)
        if bi < 0: return path, None
        e = a.off(bi); tex = sk.u32(raw, e); cnt = sk.u32(raw, e + 4)
        offs = [a.off(i) for i in range(n)]
        mi = next((i for i in range(n) if i not in (bi, tex) and offs[i] and read_motions(a.sub(i))), -1)
        if mi < 0: return path, None
        mot = read_motions(a.sub(mi)); mt = [ticks(m) for m in mot[:40]]
        keyentries = [sk.u32(raw, e + 16 + b * 12) for b in range(cnt)]
        kc = []; fr = []; kn = collections.Counter()
        for ei in keyentries:
            ka = a.sub(ei); kn[ka.n()] += 1
            for tck in sk.tracks(ka):
                real = [k for k in tck if k[3] < 30000]; kc.append(len(real))
                fr += [k[3] for k in real]
        ends = collections.Counter()
        for m in mot:
            for s in m:
                if s[0] == 0x7FFF: break
                if s[1] == 0: ends[('loop' if s[2] == -32768 else 'end' if s[2] == -1 else 'other', s[3] if s[2] == -32768 else 0)] += 1
        return path, dict(bytes=len(raw), entries=n, mi=mi, bi=bi, motions=len(mot), maxticks=max(mt), key_arc_n=dict(kn), maxkeys=max(kc), maxframe=max(fr) if fr else 0,
                          align4=all(o % 4 == 0 for o in offs), align8=all(o % 8 == 0 for o in offs), sorted=offs == sorted(offs), ends=dict(ends),
                          gaps=sum(1 for i in range(n - 1) if a.end(i) != offs[i + 1] if False))
    except Exception as ex:
        return path, 'ERR ' + str(ex)[:60]


if __name__ == '__main__':
    d = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', 'data', 'files')
    files = sorted(glob.glob(os.path.join(d, '*.bin')))
    with Pool(4) as p: res = p.map(analyse, files, chunksize=8)
    ok = [(os.path.basename(p), r) for p, r in res if isinstance(r, dict)]
    out = [f'models analysed {len(ok)} of {len(files)}']
    col = lambda k: [r[k] for _, r in ok]
    for k in ('bytes', 'entries', 'motions', 'maxticks', 'maxkeys', 'maxframe'): out.append(f'{k}: min {min(col(k))} max {max(col(k))} median {sorted(col(k))[len(ok) // 2]}')
    out.append(f"motion entry index: {dict(collections.Counter(col('mi')))}; bone entry index: {dict(collections.Counter(col('bi')))}")
    out.append(f"key archive entry counts: {dict(sum((collections.Counter(r['key_arc_n']) for _, r in ok), collections.Counter()))}")
    out.append(f"offsets 4-aligned in {sum(col('align4'))}, 8-aligned in {sum(col('align8'))}, ascending in {sum(col('sorted'))} of {len(ok)}")
    ev = collections.Counter()
    for _, r in ok:
        for k, v in r['ends'].items(): ev[k] += v
    out.append(f'count-0 steps over all motions: {dict(ev)}')
    big = sorted(ok, key=lambda x: -x[1]['maxticks'])[:5]
    out.append('largest ticks per motion (of the first 40 motions): ' + ', '.join(f"{n} {r['maxticks']}" for n, r in big) + ' (engine limit 1600)')
    out.append('largest files: ' + ', '.join(f"{n} {r['bytes']}" for n, r in sorted(ok, key=lambda x: -x[1]['bytes'])[:5]))
    txt = '\n'.join(out); print(txt)
    open(os.path.join(HERE, '..', '..', '..', 'kb', 'e41-limits-output.txt'), 'w', encoding='utf-8').write(txt + '\n')
