#!/usr/bin/env python3
"""check.py -- checks the output of the timers-clock bare-metal ports (tb/bm/timers-clock: tc1 .. tc4, tck) against
expected.json (mkexpected.py, from the console logs tools/hw/timers-clock/*.hwlog).  Owner: WI-CLK.

usage: check.py RUN_OUTPUT.txt [--profile iss|soc] [--timing] [--irq] [--board] [--progs tc1,tc2,...]
       check.py --self
       check.py --mutants
  RUN_OUTPUT.txt   the text the programs printed (the sim-control putchar stream; several programs may share a file)
  --profile iss    the ISS runner (tb/v1_clk_bmrun.cpp, make v1-clk-bm): the _noirq builds, nominal timing, no board.
                   Checks exact / match lines and the functional (`func`) part of the timing lines.  Default.
  --profile soc    the v1 SoC (M1b): adds --timing and --irq (tc1 / tc2 built with interrupts)
  --timing         also the `timing` expressions of the timing / irq lines (tolerances around the console's numbers)
  --irq            the interrupt tests (J, S) ran; without it the skip lines of the -DBM_NO_IRQ builds are expected
  --board          also tck (test K: the AICA RTC; M4 and later)
  --progs          only these programs (default: tc1 tc2 tc3 tc4, plus tck with --board)
  --kos            (M5, WI-SOC; SOC.md 8.3 M5) the console's own ELFs (tools/hw/timers-clock/tc*.elf) unmodified under
                   KOS on the board with the dcload stand-in (soc_tb --dcload): each line is compared in the console's
                   form -- an entry's `sub` (the port's shortened window: "over 200k cycles" for the console's "over 4M
                   cycles") is applied to the run's line before its rule, so an exact line must equal the console's --,
                   test K is tc.elf's "K AICA RTC" line (tck), and tc1 is tc.elf's output without it; implies --profile
                   soc --board
  --self           check the console logs themselves (every rule against its own console line, with --timing --irq
                   --board): a sanity test of the rules; exits 0 when the console satisfies every rule
  --mutants        planted errors in console lines (what a wrong wake cost or WDT window would print) that the rules
                   must reject; exits 0 when every one is rejected and every unmutated line passes
Lines are paired in order, program by program (a program's output runs from its first expected id to its done line).
Exit code 1 on any failure.
"""
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
LOGS = os.path.join(HERE, '..', '..', '..', 'tools', 'hw', 'timers-clock')
NUM = re.compile(r'[rv]?-?[0-9a-f]+$')


# ---- helpers the expressions use -------------------------------------------------------------------------------------
def h(x):
    return int(x, 16)


def d(x):
    return int(x)


def cspread(vals, per):
    """smallest arc of a circle of circumference `per` that holds every value"""
    v = sorted(x % per for x in vals)
    if len(v) < 2:
        return 0
    gaps = [v[i + 1] - v[i] for i in range(len(v) - 1)] + [v[0] + per - v[-1]]
    return per - max(gaps)


def cdist(a, b, per):
    x = (a - b) % per
    return min(x, per - x)


def nonincr(vals):
    return all(a >= b for a, b in zip(vals, vals[1:]))


def dseq(T):
    """D lines: the TCNT values in change order (a record for a TCR-only change repeats the TCNT)"""
    out = []
    for t in T:
        v = d(t[1])
        if not out or out[-1] != v:
            out.append(v)
    return out


def dunf(T, tcor):
    """D lines: TCR reads 0004 before the first reload and 0104 from it on; a 0104 before the reload (the UNF read in
    the same pass as the tick, before TCNT shows the reload) only on a TCNT = 0 record"""
    prev, r = None, None
    for i, t in enumerate(T):
        v = d(t[1])
        if prev == 0 and v == tcor and r is None:
            r = i
        prev = v
    if r is None:
        return False
    seen = False
    for i, t in enumerate(T):
        u = t[2] == '0104'
        if i >= r:
            if not u:
                return False
        else:
            if seen and not u:
                return False
            if u and d(t[1]) != 0:
                return False
            seen |= u
    return True


def dgaps(T, per, tol):
    """D lines: the TCNT changes come one tick apart, the first within one tick of the start"""
    st, prev = [], None
    for t in T:
        v = d(t[1])
        if v != prev:
            st.append(d(t[0]))
        prev = v
    return bool(st) and 1 <= st[0] <= per + tol and all(abs(b - a - per) <= tol for a, b in zip(st, st[1:]))


def rtwo(T, per, tol, early):
    """R lines (start mod per, e1-start, e2-e1, e3-e2, e1 mod per): the second count always falls on the regular edge
    (one phase modulo the period); the first is on it too, or early (e1 - start <= `early`)"""
    e2 = [(d(t[4]) + d(t[2])) % per for t in T]
    if cspread(e2, per) > tol:
        return False
    return all(d(t[1]) <= early or cdist(d(t[4]), e2m, per) <= tol for t, e2m in zip(T, e2))


def cmed(vals, per):
    """the median of a tight cluster of values on a circle of circumference `per`"""
    ref = vals[0] % per
    offs = sorted(((x - ref + per // 2) % per) - per // 2 for x in vals)
    return (ref + offs[len(offs) // 2]) % per


def _rsplit(T, per, tol, early):
    """R lines: the regular edge e2m (the median second count mod per) and L, the median e1 - start of the trials whose
    first count is clearly early (at most `early` after the start and more than `tol` off the regular edge)"""
    e2m = cmed([(d(t[4]) + d(t[2])) % per for t in T], per)
    lat = sorted(d(t[1]) for t in T if d(t[1]) <= early and cdist(d(t[4]), e2m, per) > tol)
    return e2m, (lat[len(lat) // 2] if lat else None)


def rlat(T, per, tol, early):
    """R lines: L, the latency from the start sample to an early first count (the TME write landing plus the poll
    read's sample); -1000 when no trial counted early"""
    L = _rsplit(T, per, tol, early)[1]
    return -1000 if L is None else L


def rwin(T, per, tol, early):
    """R lines: which start phases give the early first count.  The WDT counts on the rising edge of (TME & tap), so a
    TME write that lands while the tap is high (the half period after the regular edge e2m) counts at once, and one
    that lands in the low half counts at the next regular edge.  The write lands at start + L (rlat).  Every trial
    whose landing point lies at least `tol` inside the high half must count early (e1 - start within `tol` of L), every
    one at least `tol` inside the low half on the regular edge; trials near the two boundaries may do either.  At least
    one trial counts early (so L exists) and each half holds at least one trial."""
    e2m, L = _rsplit(T, per, tol, early)
    if L is None:
        return False
    nh = nl = 0
    for t in T:
        x = (d(t[0]) + L - e2m) % per
        if tol <= x < per // 2 - tol:
            nh += 1
            if abs(d(t[1]) - L) > tol:
                return False
        elif per // 2 + tol <= x < per - tol:
            nl += 1
            if cdist(d(t[4]), e2m, per) > tol:
                return False
    return nh >= 1 and nl >= 1


def tuples(line):
    out = []
    for m in re.finditer(r'\(([^()]*)\)|\[([^\[\]]*)\]', line):
        toks = (m.group(1) if m.group(1) is not None else m.group(2)).split()
        if toks and all(NUM.match(x) for x in toks):
            out.append(toks)
    return out


# ---- checking ---------------------------------------------------------------------------------------------------------
def extract(text, first, last):
    ls = [l.rstrip() for l in text.splitlines()]
    try:
        i = next(k for k, l in enumerate(ls) if l.startswith(first))
        j = next(k for k in range(i, len(ls)) if ls[k].startswith(last))
    except StopIteration:
        return None
    return [l for l in ls[i:j + 1] if l.strip()]


def port_text(e):
    """the console line as the port prints it (its documented text changes applied)"""
    s = e['console']
    for a, b in e.get('sub', []):
        s = s.replace(a, b)
    return s


def expected_list(exp, irq, board, selfmode, kos=False):
    out = []
    for e in exp:
        m = e['mode']
        if m == 'irq' and not irq or m == 'noirq' and irq or m == 'board' and not board:
            continue
        if (selfmode or kos) and e['console'] is None:                  # a line only the port prints
            continue
        out.append(e)
    return out


def check_line(e, ln, timing):
    """returns a list of failure strings (empty: pass); None: not checked"""
    m = e['mode']
    if m == 'info':
        return None
    if 'regex' not in e:
        want = e['line']
        return [] if ln == want else [f'{ln!r} != {want!r}']
    mt = re.fullmatch(e['regex'], ln)
    if not mt:
        return [f'{ln!r} does not match {e["regex"]!r}']
    env = {'L': ln, 'G': mt.groupdict(), 'T': tuples(ln), 'h': h, 'd': d, 'cspread': cspread, 'nonincr': nonincr,
           'dseq': dseq, 'dunf': dunf, 'dgaps': dgaps, 'rtwo': rtwo, 'rwin': rwin, 'rlat': rlat, 'abs': abs, 'all': all,
           'len': len, 'float': float}
    fails = []
    exprs = list(e.get('func', [])) + (list(e.get('timing', [])) if timing else [])
    for x in exprs:
        try:
            ok = eval(x, dict(env, __builtins__={}))
        except Exception as ex:                                        # a malformed line
            ok, x = False, f'{x} ({type(ex).__name__}: {ex})'
        if not ok:
            fails.append(f'{"timing" if x in e.get("timing", []) else "func"} {x} fails on {ln[:160]!r}')
    return fails


FIRST_LAST = {'tc1': ('0: PMCR0', 'tc: done'), 'tc2': ('tc2: STBCR', 'tc2: done'), 'tc3': ('tc3 as found', 'tc3: done'),
              'tc4': ('tc4: STBCR', 'tc4: done'), 'tck': ('K AICA RTC', 'tck: done'),
              'sleepwarm': ('sleepwarm: start', 'sleepwarm: done')}
SELF_LOGS = {'tc1': 'tc.elf.hwlog', 'tc2': 'tc2.elf.hwlog', 'tc3': 'tc3.elf.hwlog', 'tc4': 'tc4.elf.hwlog',
             'tck': 'tc.elf.hwlog', 'sleepwarm': 'sleepwarm.elf.hwlog'}


def check_prog(prog, exp, text, timing, irq, board, selfmode, kos=False):
    first, last = FIRST_LAST[prog]
    if (selfmode or kos) and prog == 'tc1':
        last = 'tc: done'
    lines = extract(text, first, last)
    if (selfmode or kos) and prog == 'tck':
        lines = [l for l in text.splitlines() if l.startswith('K AICA RTC')]
    if (selfmode or kos) and prog == 'tc1' and lines is not None:
        lines = [l for l in lines if not l.startswith('K AICA')]
    if lines is None:
        return [f'{prog}: no output ("{first}" ... "{last}")'], 0
    want = expected_list(exp, irq, board, selfmode, kos)
    fails, checked = [], 0
    if len(lines) != len(want):
        fails.append(f'{prog}: {len(lines)} lines printed, {len(want)} expected (pairing by position may drift)')
    for n, e in enumerate(want):
        if n >= len(lines):
            fails.append(f'{prog}: missing line for {e["id"]!r}')
            break
        ln = lines[n]
        if selfmode:
            ln = port_text(e)
        elif kos:                                       # the console's form: the port's documented text changes
            for a, b in e.get('sub', []):
                ln = ln.replace(a, b)
        if not ln.startswith(e['id']):
            fails.append(f'{prog} line {n}: {ln[:60]!r} where {e["id"]!r} was expected')
            continue
        r = check_line(e, ln, timing)
        if r is None:
            continue
        checked += 1
        fails += [f'{prog} {e["id"]} ({e["mode"]}): {f}' + (f'  [{e["note"]}]' if 'note' in e else '') for f in r]
    return fails, checked


# ---- --mutants: planted errors the timing rules must reject -----------------------------------------------------------
def _rline(ln, fn):
    """rebuild an R CKS line with every tuple (s, e1-s, e2-e1, e3-e2, e1 mod per) passed through fn(tuple, per, e2m)"""
    per = 32 << int(re.match(r'R CKS (\d)', ln).group(1))
    T = [[d(x) for x in t] for t in tuples(ln)]
    e2m = cmed([(t[4] + t[2]) % per for t in T], per)
    head = ln[:ln.index('per): ') + len('per): ')]
    return head + ' '.join('(%d %d %d %d %d)' % tuple(fn(t, per, e2m)) for t in T)


def _early(t, per, e2m):
    return t[1] <= 64 and cdist(t[4], e2m, per) > (24 if per == 256 else 48)


def _as_regular(t, per, e2m):
    """the trial as a WDT without the early count prints it: the first count on the next regular edge"""
    s = t[0]
    g = (e2m - s) % per or per
    return [s, g, per, t[3], (s + g) % per]


def _as_early(t, per, e2m, L=19):
    """the trial with an early first count L after the start"""
    s = t[0]
    e1 = s + L
    g = (e2m - e1) % per or per
    return [s, L, g, t[3], e1 % per]


def _s_hist(ln, k1, hist=True):
    """an S line with k1 trials at 1 tick and the rest at 2 (the shown tuples: the first k1 at 1); hist = False leaves
    the histogram as it was"""
    n = [0]

    def tup(m):
        f = m.group(1).split()
        if not all(NUM.match(x) for x in f):                      # the header "(N flag k ...)"
            return m.group(0)
        n[0] += 1
        f[2] = '1' if n[0] <= min(k1, 16) else '2'
        return '(' + ' '.join(f) + ')'
    ln = re.sub(r'\(([^()]*)\)', tup, ln)
    if not hist:
        return ln
    return re.sub(r'k histogram 0\.\.7:(?: \d+){8}$', 'k histogram 0..7: 0 %d %d 0 0 0 0 0' % (k1, 32 - k1), ln)


MUTANTS = [
    # (what, program, id, line transform)
    ('S sleep: every trial 1 tick (a zero / too small wake cost)', 'tc2', 'S sleep', lambda l: _s_hist(l, 32)),
    ('S sleep: 17 of 32 at 1 tick (only 15 at 2)', 'tc2', 'S sleep', lambda l: _s_hist(l, 17)),
    ('S sleep: the 16 shown trials at 1 tick, histogram untouched', 'tc2', 'S sleep', lambda l: _s_hist(l, 32, False)),
    ('S spin: one trial at 2 ticks', 'tc2', 'S spin', lambda l: l.replace('(100 1 1 ', '(100 1 2 ', 1)
     .replace('histogram 0..7: 0 32 0', 'histogram 0..7: 0 31 1')),
    ('R CKS 3: no early first count (the WDT edge detector on the tap alone)', 'tc2', 'R CKS 3',
     lambda l: _rline(l, lambda t, per, m: _as_regular(t, per, m))),
    ('R CKS 7: no early first count', 'tc2', 'R CKS 7', lambda l: _rline(l, lambda t, per, m: _as_regular(t, per, m))),
    ('R CKS 3: a start deep in the tap\'s low half counts early', 'tc2', 'R CKS 3',
     lambda l: _rline(l, lambda t, per, m: _as_early(t, per, m) if t[0] == 5 else t)),
    ('R CKS 7: early and regular swapped (early in the low half)', 'tc2', 'R CKS 7',
     lambda l: _rline(l, lambda t, per, m: _as_regular(t, per, m) if _early(t, per, m) else _as_early(t, per, m))),
    ('R CKS 3: the TME write lands 16 Icyc later', 'tc2', 'R CKS 3',
     lambda l: _rline(l, lambda t, per, m: [t[0], t[1] + 16, t[2] - 16, t[3], (t[4] + 16) % per]
                      if _early(t, per, m) else t)),
    ('R CKS 7: the TME write lands 18 Icyc earlier', 'tc2', 'R CKS 7',
     lambda l: _rline(l, lambda t, per, m: [t[0], t[1] - 18, t[2] + 18, t[3], (t[4] - 18) % per]
                      if _early(t, per, m) else t)),
    ('sleepwarm W J: the clock stopped at the request (28 counted, M1a\'s CPG)', 'sleepwarm', 'W J N=101',
     lambda l: re.sub(r'\((\d) \d+ (\d+) (-?\d+) (\d+)\)', lambda m: '(%s 28 %s %s 110)' % (m.group(1), m.group(2),
                      m.group(3)), l)),
    ('sleepwarm W J: a counted resync after the sleep (handler-to-return 90, not 82)', 'sleepwarm', 'W J N=102',
     lambda l: re.sub(r'\((\d) (\d+) (\d+) (-?\d+) (\d+)\)', lambda m: '(%s %s %s %s %d)' % (m.group(1), m.group(2),
                      m.group(3), m.group(4), int(m.group(2)) + 90), l)),
    ('sleepwarm W S: every trial 1 tick (a 2-pce resync)', 'sleepwarm', 'W S N=102',
     lambda l: re.sub(r'\((\d) (\d+) \d+ ', r'(\1 \2 1 ', l)),
    ('sleepwarm P S: SPC at the SLEEP (a wake before the SLEEP)', 'sleepwarm', 'P S K=3',
     lambda l: l.replace(' 2 2 ', ' 2 0 ', 1)),
]


def run_mutants():
    doc = json.load(open(os.path.join(HERE, 'expected.json')))
    bad = 0
    for what, prog, ident_, fn in MUTANTS:
        e = next(x for x in doc['programs'][prog] if x['id'] == ident_)
        ln = port_text(e)
        base = check_line(e, ln, True)
        mut = fn(ln)
        r = check_line(e, mut, True) if mut != ln else []
        ok = base == [] and bool(r)
        bad += not ok
        print(f'{"caught  " if ok else "MISSED  "} {what}' + ('' if ok else f'  (unmutated: {base}; mutated line '
                                                                          f'{"unchanged" if mut == ln else "passed"})'))
        if ok:
            print(f'           {r[0][:150]}')
    print(f'timers-clock bm mutants: {len(MUTANTS) - bad} of {len(MUTANTS)} planted errors rejected')
    print('timers-clock bm mutants: ' + ('PASS' if not bad else 'FAIL'))
    return 1 if bad else 0


def main():
    argv = sys.argv[1:]
    if '--mutants' in argv:
        return run_mutants()
    args = [a for a in argv if not a.startswith('--')]
    opt = lambda k: k in argv
    prof = 'iss'
    progs = None
    for i, a in enumerate(argv):
        if a == '--profile' and i + 1 < len(argv):
            prof = argv[i + 1]
            args = [x for x in args if x != prof]
        if a == '--progs' and i + 1 < len(argv):
            progs = argv[i + 1].split(',')
            args = [x for x in args if x != argv[i + 1]]
    selfmode = opt('--self')
    kos = opt('--kos')
    if kos:                                                             # M5: the unmodified ELFs under KOS
        prof = 'soc'
    timing = opt('--timing') or prof == 'soc' or selfmode
    irq = opt('--irq') or prof == 'soc' or selfmode
    board = opt('--board') or selfmode or kos
    doc = json.load(open(os.path.join(HERE, 'expected.json')))
    if progs is None:                                                   # sleepwarm needs interrupts (M1b)
        progs = ['tc1', 'tc2', 'tc3', 'tc4'] + (['sleepwarm'] if irq else []) + (['tck'] if board else [])
    if not selfmode and not args:
        print(__doc__)
        return 2
    text = None if selfmode else open(args[0], errors='replace').read()
    total_fail, total = 0, 0
    for prog in progs:
        t = open(os.path.join(LOGS, SELF_LOGS[prog]), errors='replace').read() if selfmode else text
        fails, n = check_prog(prog, doc['programs'][prog], t, timing, irq, board, selfmode, kos and not selfmode)
        total += n
        total_fail += len(fails)
        print(f'{prog}: {n} lines checked, {len(fails)} failures')
        for f in fails[:30]:
            print('  ' + f)
    what = 'self' if selfmode else ('kos ' if kos else '') + prof + (' +timing' if timing and prof != 'soc' else '') + \
        (' +irq' if irq and prof != 'soc' else '') + (' +board' if board else '')
    print(f'timers-clock bm ({what}): {total} lines checked, {total_fail} failures')
    print('timers-clock bm: ' + ('PASS' if total_fail == 0 else 'FAIL'))
    return 1 if total_fail else 0


if __name__ == '__main__':
    sys.exit(main())
