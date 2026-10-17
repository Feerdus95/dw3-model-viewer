"""Activation policy of joint blending (selection only, no geometry):
  python tools/test_blend_policy.py [--data DIR]   (default: ../native/data)

  - default: exactly the approved pairs are enabled
  - an excluded or rejected pair is never enabled, whatever --weld / --weld-all / --weld-unreviewed ask
  - --weld-unreviewed enables pairs that have no decision, and no pending or excluded one
  - a pending pair is enabled only when asked for by name (--weld)
Reads the `blend N state` column of `dw3mv --seams`. Exit 1 on any failure."""
import os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, 'build', 'dw3mv.exe')
DATA = os.path.join(os.path.dirname(ROOT), 'native', 'data')
if '--data' in sys.argv:
    DATA = sys.argv[sys.argv.index('--data') + 1]
FILES = os.path.join(DATA, 'files')

src = open(os.path.join(ROOT, 'src', 'modules', 'blend_decisions.inc')).read()
DEC = {}  # (model, a, b) -> approved | excluded | pending
for m in re.finditer(r'\{"(\w+\.BIN)", (\d+), (\d+), BLEND_(\w+),', src):
    DEC[(m[1], min(int(m[2]), int(m[3])), max(int(m[2]), int(m[3])))] = m[4].lower()
jb = open(os.path.join(ROOT, 'src', 'modules', 'joint_blend.inc')).read()
ALWAYS = {}  # excluded for good, outside the table
for m in re.finditer(r'\{"(\w+\.BIN)", (\d+), (\d+), "', jb):
    ALWAYS[(m[1], int(m[2]), int(m[3]))] = True

ROW = re.compile(r'^(\d+) (\S+)\s+b(\d+)\s+<- b(\d+) .* blend (\d+) (\S+)\s*$')


def enabled(flags, only_file=None):
    """{(model, a, b)} of pairs the viewer would draw with FLAGS"""
    args = [EXE, '--seams'] + flags + ([os.path.join(FILES, only_file)] if only_file else ['--data', DATA])
    out = subprocess.run(args, capture_output=True, text=True, errors='replace').stdout
    got = set()
    for l in out.splitlines():
        m = ROW.match(l)
        if m and m[6] == 'enabled':
            a, b = int(m[3]), int(m[4])
            got.add((m[2], min(a, b), max(a, b)))
    return got


fails = []


def check(ok, what):
    print(('ok   ' if ok else 'FAIL ') + what)
    if not ok:
        fails.append(what)


approved = {k for k, v in DEC.items() if v == 'approved'}
check(len(approved) == 5, 'the table has 5 approved pairs (%d)' % len(approved))
dflt = enabled([])
check(dflt == approved, 'default enables exactly the approved pairs (%d: %s)' % (len(dflt), sorted(dflt ^ approved)))

excluded = {k for k, v in DEC.items() if v == 'excluded'} | set(ALWAYS)
excluded.add(('M122HAGU.BIN', 1, 2))  # rejected as confirmed destructive (BLEND_REJECT)
files = {'M378BVND.BIN': '1766.bin', 'M122HAGU.BIN': '0638.bin', 'M366GIRU.BIN': '0356.bin', 'M126BRAK.BIN': '0502.bin'}
for model, a, b in sorted(excluded):
    f = files.get(model)
    if not f:
        continue
    combos = [['--weld', '%d,%d' % (a, b)], ['--weld', '%d,%d' % (b, a)], ['--weld-all'], ['--weld-unreviewed'], ['--weld-unreviewed', '--weld', '%d,%d' % (a, b)]]
    if model != 'M122HAGU.BIN':  # a REJECTED pair (confirmed destructive) can be drawn by --weld-inspect, a diagnostic, to look at why; an EXCLUDED one never
        combos.append(['--weld-all', '--weld', '%d,%d' % (a, b), '--weld-inspect'])
    for flags in combos:
        got = enabled(flags, f)
        check((model, a, b) not in got, '%s %d,%d never enabled with %s' % (model, a, b, ' '.join(flags)))

un = enabled(['--weld-unreviewed'])
decided = set(DEC) | set(ALWAYS) | {('M122HAGU.BIN', 1, 2)}
extra = un - approved
check(len(extra) > 0, '--weld-unreviewed enables pairs beyond the approved (%d)' % len(extra))
check(not (extra & decided), '--weld-unreviewed enables no pending, excluded or rejected pair (%s)' % sorted(extra & decided)[:5])
check(approved <= un, '--weld-unreviewed keeps the approved pairs')

pending = sorted(k for k, v in DEC.items() if v == 'pending')
m, a, b = next(k for k in pending if k[0] == 'M241HNGY.BIN' and k[1:] == (18, 19))
check((m, a, b) not in enabled(['--weld-all'], '1798.bin'), 'a pending pair is not enabled by --weld-all')
check((m, a, b) in enabled(['--weld', '18,19'], '1798.bin'), 'a pending pair is enabled by name (--weld)')

print('\n%d failure(s)' % len(fails))
sys.exit(1 if fails else 0)
