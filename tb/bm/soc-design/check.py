#!/usr/bin/env python3
# check.py -- checks the output of the bare-metal soc-design / core-integration probes against an expected.json
# (tb/bm/soc-design/mkexpected.py, tb/bm/core-integration/mkexpected.py).  Owner: WI-CORE-A.
#   usage: check.py RUN_OUTPUT [RUN_OUTPUT ...] [--expected FILE] [--level core|fetch|soc|cache|all] [--translate 0|1]
#                   [--prog P[,P]] [-v]
#          check.py --self [--expected FILE]
#     RUN_OUTPUT   the text a run printed (the sim-control putchar stream; several programs may be in one file)
#     --level      which token classes are compared: core (make v1-exc: v1_core on v1_ideal_mem with the Iclk P4
#                  stub), fetch (+ WI-CORE-B's fetch schedule), soc (+ WI-CORE-F's FPU kill / traps, the real P4
#                  bridge, the LSU / fetch events, interrupts, the MMU: M1a / M2), cache (+ the IC / OC / BSC: M3),
#                  all (= cache).  'open' and 'env' tokens are never compared.
#     --translate  0: the run's v1_soc has TRANSLATE = 0 (M1a, M3: SOC.md 3.6): the 'mmu' class (the UTLB-miss rows and
#                  what they leave behind, e.g. the stale EXPEVT the next interrupt handler records) is not compared.
#                  Default 1
#     --kos        (M5, WI-SOC; SOC.md 8.3 M5) the console's own ELF unmodified under KOS on the board with the dcload
#                  stand-in (soc_tb --dcload): the line keys of KOS_DOC (below) -- timing records whose value depends on
#                  the instruction-cache state the chip's IC-miss timing does not reproduce (SOC.md 9.23) -- are reported
#                  "documented" when they miss their rule (they must still be printed, with the right token count);
#                  every other line is checked as usual
#     --self       the console logs checked against themselves (every compared token must pass), and a negative
#                  test: each compared number of the console, changed by one, must be rejected
# Lines are paired by key: the first two tokens of a line (an indented line: its parent's key plus its own first two),
# with an occurrence count for repeats.  Token modes:
#   w   a word, equal                      x   a string, equal (hex compared case-insensitively)
#   i   a number compared as an integer (both within 0.03 of one; the console's 1/256-cycle loop noise)
#   s   not compared
#   hm:MASK        hex (the part after '='), equal under the mask
#   r:LINE:TOK     hex, relative: the value minus the value of token TOK of line LINE of the same output must be the
#                  console's (code addresses against the test's own recorded address, data against the scratch buffer:
#                  the bare-metal link layout differs from KOS's)
#   pt:TOK         hex: PTEH = the value of token TOK of the same line (TEA) & ~0x3FF
#   iset:W[;C]     a number (or the value of a name=value word) compared as an integer, which must be one of the
#                  comma-separated set W, or of C at a level
#                  that includes the 'cache' class.  For a value that depends on a Bclk / Pclk phase the bare-metal build
#                  cannot reproduce (its code between the last synchronising access and the measured point is not
#                  KOS's): W is every value the structure gives over the phases (e.g. an isolated Bclk read 8 or 9 by
#                  phase); C a console value that includes an instruction-cache miss (IDEAL_MEM = 1 has none: M3)
#   rset:LINE:TOK:W[;C]  the relative mode r:LINE:TOK, with the offset (in bytes) one of W, or of C at a cache level
#   f:TOL          a number within TOL of the console's (M3, WI-SOC: a loop period that carries the refresh share,
#                  core-integration ci4's P2 / miss loops -- mkexpected.py there says why TOL)
#     --refresh-slack S   (M3, WI-SOC: the runs on rm, IDEAL_MEM = 0) an 'i' number printed with a fraction (a loop
#                  period, an E difference: (min of 5 x N2 - min of 5 x N1) / 256) is accepted within S of the
#                  console's integer instead of 0.03.  On the real memory every timed run that takes a cache miss
#                  meets the SDRAM refresh at the phase of the free-running refresh timer; the runs of one row sample
#                  a nearly fixed sequence of phases from a start the preceding printf sets -- dcload's on the console,
#                  the model's own -- so a minimum or maximum over five runs may carry one refresh collision (at most
#                  24 Icyc [HW bsc-memmap 6.2]) the console's did not, or not carry one it did.  v1-soc-m3 passes
#                  S = 24 / 256 = 0.094 (M3-fix, M3-verify F8: one collision; the grafted runs' residuals are <= 0.07,
#                  while the 2 x 24 / 256 = 0.1875 used before accepted the bare-metal layout artifact c1 'L nop'
#                  3.172 against 3.000).  Integers printed without a fraction (brackets, counts) stay exact.
#                  --self --refresh-slack S also checks the slack itself: the layout artifact (3.172 for 3) must be
#                  rejected, the grafted runs' largest residual (27.965 for 28, 30.070 for 30) accepted
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
LEVELS = {'core': {'core'}, 'fetch': {'core', 'fetch'},
          'soc': {'core', 'fetch', 'soc', 'fpu', 'irq', 'mmu'},
          'cache': {'core', 'fetch', 'soc', 'fpu', 'irq', 'mmu', 'cache'}}
LEVELS['all'] = LEVELS['cache']


def toks(line):
    return re.findall(r'[^\s/()]+', line)


def keys(lines):
    """the pairing key of every line"""
    out, seen, parent = [], {}, ''
    for l in lines:
        t = toks(l)
        # the line's name: its first token and a second one that is a name (no '=', not a number: not a value)
        base = t[0] if t else ''
        if len(t) > 1 and '=' not in t[1] and not re.match(r'^[-+]?([0-9][0-9.]*|0x[0-9a-fA-F]+)$', t[1]):
            base += ' ' + t[1]
        if l[:1].isspace():
            k = parent + ' / ' + base
        else:
            parent = base
            k = base
        n = seen.get(k, 0)
        seen[k] = n + 1
        out.append(k + ('#%d' % n if n else ''))
    return out


def block(text, prog, start):
    """a run's lines of the program: from its first line (its header, or `start`) to its '<prog>: done' line"""
    ls = [l.rstrip() for l in text.splitlines()]
    st = start or (prog + ': ')
    s = next((k for k, l in enumerate(ls) if l.startswith(st) and not l.startswith(prog + ': done')), None)
    if s is None:
        return None
    e = next((k for k in range(s, len(ls)) if ls[k].startswith(prog + ': done')), None)
    return [l for l in ls[s:e + 1] if l.strip()] if e is not None else None


def near_int(v):
    return abs(v - round(v)) <= 0.03


def hexv(tok):
    try:
        return int(tok.split('=')[-1], 16)
    except ValueError:
        return None


def set_of(spec, cold_ok):
    """the allowed values of an iset / rset spec 'W[;C]'"""
    w, _, c = spec.partition(';')
    vals = {int(x, 0) for x in w.split(',') if x}
    if cold_ok and c:
        vals |= {int(x, 0) for x in c.split(',') if x}
    return vals


REFRESH_SLACK = 0.0


def cmp_token(want, got, mode, wline_toks, gline_toks, wlines, glines, cold_ok=True):
    """wlines / glines: {line index: tokens} of the console / the run (for the relative modes); cold_ok: the level
    includes the 'cache' class (the C part of an iset / rset spec is accepted)"""
    if mode in ('w', 'x'):
        return want.lower() == got.lower()
    if mode.startswith('iset:'):
        if '=' in want:                                          # a name=value word: the name equal, the value in the set
            if want.split('=')[0] != got.split('=')[0]:
                return False
            got = got.split('=')[-1]
        try:
            g = float(got)
        except ValueError:
            return False
        return near_int(g) and round(g) in set_of(mode[5:], cold_ok)
    if mode == 'i':
        try:
            w, g = float(want), float(got)
        except ValueError:
            return False
        if REFRESH_SLACK and '.' in want:
            return abs(g - round(w)) <= max(REFRESH_SLACK, 0.03)
        return near_int(g) and round(w) == round(g)
    if mode.startswith('f:'):
        try:
            w, g = float(want), float(got)
        except ValueError:
            return False
        return abs(w - g) <= float(mode[2:])
    wv, gv = hexv(want), hexv(got)
    if wv is None or gv is None:
        return False
    if '=' in want and want.split('=')[0] != got.split('=')[0]:
        return False
    if mode.startswith('hm:'):
        m = int(mode[3:], 16)
        return (wv & m) == (gv & m)
    if mode.startswith('r:'):
        _, L, K = mode.split(':')
        L, K = int(L), int(K)
        if L not in wlines or L not in glines or K >= len(wlines[L]) or K >= len(glines[L]):
            return False
        wa, ga = hexv(wlines[L][K]), hexv(glines[L][K])
        if wa is None or ga is None:
            return False
        return ((wv - wa) & 0xFFFFFFFF) == ((gv - ga) & 0xFFFFFFFF)
    if mode.startswith('rset:'):
        _, L, K, spec = mode.split(':', 3)
        L, K = int(L), int(K)
        if L not in glines or K >= len(glines[L]):
            return False
        ga = hexv(glines[L][K])
        if ga is None:
            return False
        off = (gv - ga) & 0xFFFFFFFF
        off = off - (1 << 32) if off & 0x80000000 else off
        return off in set_of(spec, cold_ok)
    if mode.startswith('pt:'):
        K = int(mode[3:])
        wt, gt = hexv(wline_toks[K]), hexv(gline_toks[K])
        return wt is not None and gt is not None and wv == (wt & ~0x3FF) and gv == (gt & ~0x3FF)
    return False


# (M5, --kos) (suite, program, line key): why the line's value on the unmodified image is cache-dependent, phase-
# dependent, or M3's open row -- (token indices, (lo, hi), why) (M5-fix, M5-verify F2: token-precise, bounded): only
# those tokens are free, any other failing token of the line still fails, and a freed token must lie within (lo, hi)
# of the console's -- for a number (i / f / iset) the difference run - console; for a relative address (r / rset: a
# landing point) the difference of the two offsets from their reference tokens, in bytes
_COLD = ('cache: the cold IRQ record -- the first take of each I row lands in a line the fetch has not seen; the '
         'landing point during that IC miss is SOC.md 9.23 (M3: "the IRQ landing points of ci\'s cold records differ by '
         '3-8 bytes"; M5: 6-16) -- spc only, within one 32-byte IC line of the console\'s')
KOS_DOC = {
    ('core-integration', 'ci', k): ((3,), (-32, 32), _COLD)
    for k in ('I irq_ldcl / R0', 'I irq_rte / R0#1', 'I irq_rte / R0#2', 'I irq_br0 / R0', 'I irq_br2 / R0',
              'I irq_br4 / R0', 'I irq_br6 / R0', 'I irq_br7 / R0')}
_MAXRUN = ('cache / phase: the maximum of five runs (its min equals the console\'s): under KOS the timer and vblank '
           'interrupts run between the repetitions at a phase the print path sets, so one run may start with an IC '
           'refill or meet a refresh collision (~28 Icyc over 256 iterations, 0.109) the console\'s worst run did not '
           '(SOC.md 9.27 / 9.28) -- the max column only, up to two such events (64 / 256 = 0.25) above the console\'s')
KOS_DOC.update({('core-integration', 'ci3', 'T ram_use'): ((4,), (-0.03, 0.25), _MAXRUN),
                ('core-integration', 'ci3', 'T ram_st_nop8'): ((4,), (-0.03, 0.25), _MAXRUN),
                ('core-integration', 'ci4', 'T ram_fdm'): ((4,), (-0.03, 0.25), _MAXRUN),
                ('core-integration', 'ci4', 'T p2_fdd'): ((2, 4), (-0.25, 2.25),
                                                          'open (M3): SOC.md 9.26, FDIV DR before an uncached load -- '
                                                          'the chip gives 33.0 against 31.1 (M3\'s failing row, gated by '
                                                          'v1-soc-m3; not a cache or phase effect) -- both columns, up to '
                                                          '2.25 above the console\'s (9.26\'s 1.9-2.0 and the slack)')})


def doc_bound(want, got, mode, lo, hi, glines_i, wlines, glines):
    """(M5-fix) a freed token's value against the console's: within (lo, hi) -- see KOS_DOC"""
    if mode.startswith('r:') or mode.startswith('rset:'):
        L, K = int(mode.split(':')[1]), int(mode.split(':')[2])
        if L not in wlines or L not in glines or K >= len(wlines[L]) or K >= len(glines[L]):
            return False
        wa, ga, wv, gv = hexv(wlines[L][K]), hexv(glines[L][K]), hexv(want), hexv(got)
        if None in (wa, ga, wv, gv):
            return False
        d = ((gv - ga) - (wv - wa)) & 0xFFFFFFFF
        d = d - (1 << 32) if d & 0x80000000 else d
        return lo <= d <= hi
    try:
        d = float(got.split('=')[-1]) - float(want.split('=')[-1])
    except ValueError:
        return False
    return lo <= d <= hi


KOS = {'on': False, 'suite': '', 'documented': []}


def check_prog(P, lines, level, verbose, translate=True):
    classes = set(LEVELS[level]) - (set() if translate else {'mmu'})
    cold_ok = 'cache' in classes
    fails, n_cmp, n_open = [], 0, 0
    exp = P['lines']
    if lines is None:
        return ['%s: no output block' % P['program']], 0, 0
    gk = keys(lines)
    gidx = {k: i for i, k in enumerate(gk)}
    # the lines by the console's index, for the relative modes: the run's line with the same key
    wl = {i: toks(e['text']) for i, e in enumerate(exp)}
    gl = {}
    for i, e in enumerate(exp):
        j = gidx.get(e['key'])
        if j is not None:
            gl[i] = toks(lines[j])
    for i, e in enumerate(exp):
        t = e['tokens']
        live = [x for x in t if x[1] != 's' and x[2] in classes]
        n_open += sum(1 for x in t if x[2] == 'open' and x[1] == 's')
        if e['class'] == 'env' or not live:
            continue
        j = gidx.get(e['key'])
        if j is None:
            fails.append('%s: no line "%s"\n    want: %s' % (P['program'], e['key'], e['text']))
            continue
        g = toks(lines[j])
        if len(g) != len(t):
            fails.append('%s line "%s": %d tokens, %d expected\n    got:  %s\n    want: %s' %
                         (P['program'], e['key'], len(g), len(t), lines[j], e['text']))
            continue
        bad, badk = [], []
        for k, (w, m, c) in enumerate(t):
            if m == 's' or c not in classes:
                continue
            if m != 'w':
                n_cmp += 1
            if not cmp_token(w, g[k], m, wl[i], g, wl, gl, cold_ok):
                bad.append('%s (want %s, %s)' % (g[k], w, c))
                badk.append(k)
        doc = KOS_DOC.get((KOS['suite'], P['program'], e['key'])) if KOS['on'] else None
        if doc is not None and not set(badk) <= set(doc[0]):
            doc = None                                     # a failing token the entry does not name
        if doc is not None:                                # a freed token outside its bound
            out = [k for k in badk if not doc_bound(t[k][0], g[k], t[k][1], doc[1][0], doc[1][1], None, wl, gl)]
            if out:
                bad = [b + ' [outside the KOS_DOC bound %s..%s]' % doc[1] if k in out else b for b, k in zip(bad, badk)]
                doc = None
        if bad and doc is not None:
            KOS['documented'].append('%s line "%s": %s -- %s' % (P['program'], e['key'], ', '.join(bad), doc[2]))
        elif bad:
            fails.append('%s line "%s" [%s: %s]: %s\n    got:  %s\n    want: %s' %
                         (P['program'], e['key'], e['class'], e['why'], ', '.join(bad), lines[j], e['text']))
        elif verbose:
            print('ok   %s: %s' % (P['program'], lines[j]))
    return fails, n_cmp, n_open


def perturb(tok, mode):
    """the console token changed so that its mode must reject it"""
    if mode == 'i':
        return str(int(round(float(tok))) + 1)
    if mode.startswith('f:'):
        return '%.3f' % (float(tok) + float(mode[2:]) + 0.01)
    if mode.startswith('iset:'):
        pre, eq, _ = tok.rpartition('=')
        return (pre + eq if eq else '') + str(max(set_of(mode[5:], True)) + 3)
    if mode.startswith('rset:'):
        pre, eq, val = tok.rpartition('=')
        return (pre + eq if eq else '') + ('%0*x' % (len(val), (int(val, 16) + 0x100) & 0xFFFFFFFF))
    pre, eq, val = tok.rpartition('=')
    v = int(val, 16)
    if mode.startswith('hm:'):
        m = int(mode[3:], 16)
        v ^= m & (-m)                              # the lowest compared bit
    else:
        v ^= 4
    return (pre + eq if eq else '') + ('%0*x' % (len(val), v & 0xFFFFFFFF))


def main():
    a = sys.argv[1:]
    expf, level, only, verbose, selfm, runs = os.path.join(HERE, 'expected.json'), 'core', None, False, False, []
    translate = True
    while a:
        x = a.pop(0)
        if x == '--expected': expf = a.pop(0)
        elif x == '--level': level = a.pop(0)
        elif x == '--refresh-slack':
            global REFRESH_SLACK
            REFRESH_SLACK = float(a.pop(0))
        elif x == '--translate': translate = a.pop(0) != '0'
        elif x == '--prog': only = set(a.pop(0).split(','))
        elif x == '-v': verbose = True
        elif x == '--self': selfm = True
        elif x == '--kos': KOS['on'] = True
        else: runs.append(x)
    E = json.load(open(expf))
    KOS['suite'] = E['suite']
    progs = [P for P in E['programs'] if not only or P['program'] in only]
    if selfm:
        bad = 0
        for P in progs:
            lines = [e['text'] for e in P['lines']]
            f, n, _ = check_prog(P, lines, 'all', False)
            bad += len(f)
            for x in f: print('SELF FAIL', x)
            caught = tot = 0
            for i, e in enumerate(P['lines']):
                for k, (w, m, c) in enumerate(e['tokens']):
                    if m in ('s', 'w', 'x') or c in ('open', 'env'):
                        continue
                    tt = toks(e['text'])
                    tt[k] = perturb(w, m)
                    L2 = list(lines)
                    L2[i] = (' ' if e['text'][:1].isspace() else '') + ' '.join(tt)
                    f2, _, _ = check_prog(P, L2, 'all', False)
                    tot += 1
                    caught += bool(f2)
            print('self %-5s %3d lines, %4d compared tokens, %4d / %4d perturbations caught' %
                  (P['program'], len(lines), n, caught, tot))
            bad += tot - caught
        if REFRESH_SLACK:                                  # the slack itself (M3-fix, M3-verify F8)
            for want, got, ok, why in (('3.000', '3.172', False, 'the bare-metal layout artifact (c1 L nop)'),
                                       ('28.000', '27.965', True, 'c1 L dae, the grafted run'),
                                       ('30.000', '30.070', True, 'c1e M4 ill, the grafted run'),
                                       ('26.000', '26.100', False, 'one collision and a bit')):
                r = cmp_token(want, got, 'i', [], [], {}, {})
                print('self slack %.4f: %s for %s: %s (%s)' % (REFRESH_SLACK, got, want, 'accepted' if r else 'rejected',
                                                              why))
                bad += r != ok
        print('check.py --self: %s' % ('PASS' if not bad else 'FAIL'))
        return 1 if bad else 0
    text = ''.join(open(r, errors='replace').read() for r in runs)
    allf, tot, topen = [], 0, 0
    for P in progs:
        f, n, o = check_prog(P, block(text, P['program'], P.get('start')), level, verbose, translate)
        allf += f
        tot += n
        topen += o
        print('%-5s %s  %4d tokens compared at level %s%s' % (P['program'], 'PASS' if not f else 'FAIL', n, level,
                                                            '' if translate else ' (TRANSLATE = 0)'))
    for x in allf:
        print('FAIL', x)
    for x in KOS['documented']:
        print('documented (--kos, KOS_DOC):', x)
    if KOS['on']:
        print('check.py (%s): %d lines documented (KOS_DOC: cache- or phase-dependent, or M3\'s open row)' %
              (E['suite'], len(KOS['documented'])))
    print('check.py (%s, level %s%s): %d tokens compared, %d open (not compared): %s' %
          (E['suite'], level, '' if translate else ', TRANSLATE = 0', tot, topen, 'PASS' if not allf else 'FAIL'))
    return 1 if allf else 0


if __name__ == '__main__':
    sys.exit(main())
