#!/usr/bin/env python3
"""check.py -- tb/bm/soc-adv/m4 (M4, WI-SOC): checks wdtrst's report against SOC.md 2.3's reset columns, and (M4-fix)
how the M4 verification's programs ended.

usage: check.py RUN_OUTPUT [--self]
       check.py RUN_OUTPUT --progs PROG --status STATUS [--scif SCIF] [--self]
  RUN_OUTPUT  the sim-control text (soc_tb --out) of `soc_tb build/bm/soc-adv/m4/wdtrst.bin --resets 2 --no-uart-check`,
              or of the program PROG
  --progs     (M4-fix) one of the M4 verification's programs (tb/bm/soc-adv/Makefile: rstfly rstfly_por rstfly_c8
              rstcols sleeprst irlx uartx_c0 uartx_c1 uartx_c2 uartx_c3 sfres); STATUS is soc_tb's stdout, SCIF its
              --scif file.  Each program judges its own rows (OK / FAIL, the rules in its source's header); PROG_RULES
              below say how its run must end: soc_tb PASS, the lockstep verdict, the done line with 0 failures, the
              number of OK rows and of each reset kind, and for uartx the SCIF transcript (uartx_c3: and soc_tb's count
              of frames a reset cut in their stop bit)
  --self      the rules' own test: every rule must reject a report line changed to violate it, and a report with a
              line missing must fail (exit 0 when all are rejected); with --progs: a FAIL row, a missing done line, a
              soc_tb FAIL, another lockstep verdict, a reset line fewer and (uartx) a changed transcript must be rejected

wdtrst prints "WDTRST boot=<n> kind=<manual|por> <reg> set=<hex> got=<hex>": `set` is what boot n - 1 read back after
giving the register its value, `got` what boot n reads after the reset and the boot stub.  The rules are the columns
"manual (pin, WDT, multiple hit)" and "WDT or H-UDI POR" of SOC.md 2.3, for the registers the boot stub does not
rewrite (SOC.md 7.3); the stub's MSTP0 / MSTP1 are cleared first (SOC.md 2.4: a module stop keeps the RTC's registers
and SCSPTR1, and puts the SCI's others at their initial values, so SCSPTR1's I/O bits are the SCI's reset witness).
A row whose `set` already equals what the rule expects proves nothing; it is reported, not failed.
"""
import re
import sys

HELD = ('held',)                       # got == set
WOVF = ('wovf',)                       # got == set | WOVF: the WDT's own reset keeps WTCSR and sets WOVF
RCR2M = ('rcr2m',)                     # got == set & (RTCEN | START): PES, ADJ, RESET cleared, RTCEN START held


def I(v):                              # the initial value
    return ('init', v)


def M(mask, v):                        # got & mask == v
    return ('mask', mask, v)


def LT(n):                             # got < n (a counter that runs on)
    return ('lt', n)


UBC = ('BARA', 'BAMRA', 'BBRA', 'BASRA', 'BARB', 'BAMRB', 'BBRB', 'BASRB', 'BDRB', 'BDMRB')
# register: (manual rule, power-on rule) -- SOC.md 2.3
RULES = {
    'WTCSR': (WOVF, WOVF),             # held (WOVF set by a WDT reset); v1_wdt: only the RESET pin clears it
    'WTCNT': (LT(0x40), LT(0x40)),     # held, counting on (0x00 after the overflow)
    'EXPEVT': (I(0x020), I(0x000)),
    'TRA': (HELD, I(0)), 'INTEVT': (HELD, I(0)),
    'ICR': (I(0x8000), I(0x8000)),     # NMIL = the pin (the DC ties NMI high), the rest 0
    'IPRA': (I(0), I(0)), 'IPRB': (I(0), I(0)), 'IPRC': (I(0), I(0)),
    'TOCR': (I(0), I(0)), 'TSTR': (I(0), I(0)), 'TCOR0': (I(0xFFFFFFFF), I(0xFFFFFFFF)),
    'TCNT0': (I(0xFFFFFFFF), I(0xFFFFFFFF)), 'TCR0': (I(0), I(0)), 'TCOR1': (I(0xFFFFFFFF), I(0xFFFFFFFF)),
    'TCNT1': (I(0xFFFFFFFF), I(0xFFFFFFFF)), 'TCR1': (I(0), I(0)),
    'RCR1': (M(0x18, 0), M(0x18, 0)),  # CIE AIE 0
    'RCR2': (RCR2M, I(0x09)),
    'RSECAR': (HELD, M(0x80, 0)), 'RMINAR': (HELD, M(0x80, 0)),   # the alarm held / ENB 0
    'RSECCNT': (HELD, HELD), 'RMINCNT': (HELD, HELD),              # never reset (no oscillator: not counting)
    'SCSMR1': (I(0), I(0)), 'SCBRR1': (I(0xFF), I(0xFF)), 'SCSPTR1': (M(0x8A, 0), M(0x8A, 0)),   # SCI init
    'SCFSR2': (I(0x60), I(0x60)), 'SCLSR2': (I(0), I(0)),          # SCIF init: ORER from the overrun cleared
    **{r: (HELD, I(0)) for r in UBC},                              # UBC held / all 0
    'PMCR2': (HELD, I(0)),
    'SAR1': (HELD, I(0)), 'DAR1': (HELD, I(0)), 'DMATCR1': (HELD, I(0)), 'CHCR1': (I(0), I(0)), 'CHCR3': (I(0), I(0)),
    'MMUCR': (I(0), I(0)), 'PTEH': (HELD, I(0)), 'TEA': (HELD, I(0)), 'PTEL': (I(0), I(0)), 'TTB': (I(0), I(0)),
    'PTEA': (I(0), I(0)),
    'STBCR2': (HELD, I(0)),
}
KINDS = (('manual', 2), ('por', 3))
LINE = re.compile(r'^WDTRST boot=(\d+) kind=(manual|por) (\w+) set=([0-9a-f]{8}) got=([0-9a-f]{8})$')


def want(rule, s):
    """the value the rule expects for set s (None: a predicate rule)"""
    k = rule[0]
    return s if k == 'held' else s | 0x10 if k == 'wovf' else s & 0x09 if k == 'rcr2m' else rule[1] if k == 'init' \
        else None


def ok(rule, s, g):
    k = rule[0]
    if k == 'mask':
        return g & rule[1] == rule[2]
    if k == 'lt':
        return g < rule[1]
    return g == want(rule, s)


def informative(rule, s):
    """does the row prove anything: the value set is not what the rule expects anyway"""
    k = rule[0]
    if k == 'mask':
        return s & rule[1] != rule[2]
    if k in ('lt', 'held'):
        return True
    return s != want(rule, s)


def parse(text):
    rows, fails = {}, []
    for ln in text.splitlines():
        m = LINE.match(ln.strip())
        if m:
            rows[(m.group(2), m.group(3))] = (int(m.group(1)), int(m.group(4), 16), int(m.group(5), 16))
        elif ln.startswith('WDTRST') and ('no reset' in ln or 'unexpected' in ln):
            fails.append(ln.strip())
    if 'WDTRST done' not in text:
        fails.append('no "WDTRST done"')
    return rows, fails


def check(text):
    rows, fails = parse(text)
    weak, n = [], 0
    for kind, boot in KINDS:
        for reg, rr in RULES.items():
            rule = rr[0 if kind == 'manual' else 1]
            if (kind, reg) not in rows:
                fails.append(f'{kind} {reg}: no report line')
                continue
            b, s, g = rows[(kind, reg)]
            n += 1
            if b != boot:
                fails.append(f'{kind} {reg}: reported by boot {b}, want {boot}')
            if not ok(rule, s, g):
                w = want(rule, s)
                fails.append(f'{kind} {reg}: got {g:08x} with {s:08x} set, want ' +
                             (f'{w:08x}' if w is not None else str(rule)) + f' (SOC.md 2.3, rule {rule[0]})')
            if not informative(rule, s):
                weak.append(f'{kind} {reg}')
    return fails, n, weak


def perturb(rule, s, g):
    """a got value the rule must reject"""
    k = rule[0]
    if k == 'mask':
        return g ^ rule[1]
    if k == 'lt':
        return rule[1]
    return want(rule, s) ^ (0x10 if k == 'wovf' else 1)


# ---- (M4-fix) the M4 verification's programs: how each run must end (agents/reports/integration/M4-verify.md)
UART_A = 'UARTX line A at 115200\n'
PROG_RULES = {
    # prog: lockstep verdict, done line, OK rows (None: any, at least 1), resets {kind: n}, extra predicates
    'rstfly': dict(lock='reset', done=r'RFLY done scenarios=145 fails=0', rows=r'^RFLY j=', nok=146,
                   resets={'a WDT manual reset': 146}),
    'rstfly_por': dict(lock='reset', done=r'RFLY done scenarios=145 fails=0', rows=r'^RFLY j=', nok=146,
                       resets={'a WDT power-on reset': 146}),
    'rstfly_c8': dict(lock='reset', done=r'RFLY done scenarios=27 fails=0', rows=r'^RFLY j=', nok=28,
                      resets={'a WDT manual reset': 28}),
    # rm: 62 rows over a manual and a WDT power-on reset; rmx (a UTLB answers: "tlb=1") 95 and the 0x140 re-boot
    'rstcols': dict(lock='reset', done=r'RCOL done fails=0', rows=r'^RCOL boot=', nok=None, resets=None),
    'sleeprst': dict(lock='reset', done=r'RSLP done scenarios=107 fails=0', rows=r'^RSLP j=', nok=108,
                     resets={'a WDT manual reset': 106, 'a WDT power-on reset': 2}),
    'irlx': dict(lock='pass', done=r'IRLX done fails=0', rows=r'^IRLX T', nok=21, resets={}),
    'uartx_c0': dict(lock='pass', done=r'^UARTX done', rows=None, nok=None, resets={}, scif=re.escape(UART_A)),
    'uartx_c1': dict(lock='reset', done=r'^UARTX done', rows=None, nok=None, resets={'a WDT manual reset': 1},
                     scif=re.escape(UART_A) + r'(V(W(X(Y(Z)?)?)?)?)?' + re.escape('UARTX line C after the reset\n'),
                     log=r'^UARTX boot=2 expevt=020$'),
    'uartx_c2': dict(lock='pass', done=r'^UARTX done', rows=None, nok=None, resets={},
                     scif=re.escape(UART_A + 'UARTX line B at 1.5625 Mbaud\n')),
    # 19 resets swept across a frame's end after a calibration boot: line A, '#', then the letters whose frames
    # finished (in order), and at least one frame the reset cut in its stop bit's second half (soc_tb's count)
    'uartx_c3': dict(lock='reset', done=r'^UARTX done', rows=None, nok=None, resets={'a WDT manual reset': 20},
                     scif=re.escape('UARTX line A at 115200\n#') + ''.join(f'{chr(97 + k)}?' for k in range(19)),
                     status=r'^soc_tb: UART monitor: \d+ frames decoded whose stop bit a reset cut'),
    'sfres': dict(lock='reset', done=r'SFRES done fails=0', rows=r'^SFRES boot=2 ', nok=5,
                  resets={'the RESET pin (SB_SFRES)': 1}),
}


def check_prog(prog, log, st, scif):
    """the failures of one program run: log (its --out), st (soc_tb's stdout), scif (its --scif or None)"""
    r = PROG_RULES[prog]
    f = []
    if not re.search(r'^soc_tb: PASS$', st, re.M):
        f.append('soc_tb did not PASS (exit code 0, the UART monitor, no monitor or RTL error)')
    m = re.search(r', lockstep (\w+)', st)
    if not m or m.group(1) != r['lock']:
        f.append(f'lockstep {m.group(1) if m else "?"}, want {r["lock"]}')
    if not re.search(r['done'], log, re.M):
        f.append(f'no "{r["done"]}"')
    bad = [l for l in log.splitlines() if re.search(r'\bFAIL\b|unexpected|no reset|no multiple', l)]
    if bad:
        f.append(f'{len(bad)} failing rows (the first: {bad[0][:110]})')
    if r['rows']:
        rows = [l for l in log.splitlines() if re.search(r['rows'], l)]
        nok = sum(1 for l in rows if re.search(r' OK( |$)', l))
        want = r['nok']
        if prog == 'rstcols':
            want = 95 if re.search(r'^RCOL start tlb=1', log, re.M) else 62
        if nok != want:
            f.append(f'{nok} OK rows, want {want}')
    kinds = {}
    for l in st.splitlines():
        m = re.match(r'^soc_tb: reset: (.*) at cycle \d+$', l)
        if m:
            kinds[m.group(1)] = kinds.get(m.group(1), 0) + 1
    want = r['resets']
    if prog == 'rstcols':
        tlb = re.search(r'^RCOL start tlb=1', log, re.M) is not None
        want = {'a WDT manual reset': 1, 'a WDT power-on reset': 1}
        if tlb:
            want['an associative-write multiple hit (imprecise reset)'] = 1
            if not re.search(r'after=mhit EXPEVT ', log):
                f.append('no row after the multiple-hit reset (EXPEVT 0x140)')
    if kinds != want:
        f.append(f'resets {kinds}, want {want}')
    if r.get('log') and not re.search(r['log'], log, re.M):
        f.append(f'no "{r["log"]}"')
    if r.get('status') and not re.search(r['status'], st, re.M):
        f.append(f'no "{r["status"]}" in soc_tb\'s output')
    if prog == 'uartx_c3' and scif is not None:
        got = scif[len('UARTX line A at 115200\n#'):]
        if not got or len(got) >= 19:
            f.append(f'{len(got)} of the 19 swept frames arrived: the sweep does not straddle the frame\'s end')
    if r.get('scif'):
        if scif is None:
            f.append('no --scif transcript given')
        elif not re.fullmatch(r['scif'], scif):
            f.append(f'SCIF transcript {scif!r}, want /{r["scif"]}/')
    return f


def self_prog(prog, log, st, scif):
    """perturbations of a passing run that check_prog must reject"""
    tries = []
    okl = [l for l in log.splitlines() if l.endswith(' OK') or ' OK ' in l]
    if okl:
        tries.append(('a FAIL row', log.replace(okl[len(okl) // 2], okl[len(okl) // 2].replace(' OK', ' FAIL'), 1), st, scif))
    dl = [l for l in log.splitlines() if re.search(PROG_RULES[prog]['done'], l)]
    if dl:
        tries.append(('no done line', log.replace(dl[0], ''), st, scif))
    tries.append(('soc_tb FAIL', log, st.replace('soc_tb: PASS', 'soc_tb: FAIL'), scif))
    other = 'pass' if PROG_RULES[prog]['lock'] != 'pass' else 'reset'
    tries.append(('another lockstep verdict', log, re.sub(r', lockstep \w+', ', lockstep ' + other, st, count=1), scif))
    rl = [l for l in st.splitlines() if l.startswith('soc_tb: reset: ')]
    if rl:
        tries.append(('a reset fewer', log, st.replace(rl[-1] + '\n', '', 1), scif))
    else:
        tries.append(('a reset more', log, st + '\nsoc_tb: reset: a WDT manual reset at cycle 1\n', scif))
    if scif:
        tries.append(('a changed transcript', log, st, scif[:-2] + '?\n'))
    if PROG_RULES[prog].get('status'):
        tries.append(('no stop-bit cut counted', log, re.sub(PROG_RULES[prog]['status'], 'x', st, flags=re.M), scif))
    bad = [n for n, lg, s2, sc in tries if not check_prog(prog, lg, s2, sc)]
    return len(tries), bad


def opt(name):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv and sys.argv.index(name) + 1 < len(sys.argv) else None


def main():
    vals = {opt(o) for o in ('--progs', '--status', '--scif')}
    args = [a for a in sys.argv[1:] if not a.startswith('--') and a not in vals]
    if not args:
        print(__doc__)
        return 2
    text = open(args[0]).read()
    prog = opt('--progs')
    if prog and prog != 'wdtrst':
        if prog not in PROG_RULES or not opt('--status'):
            print(__doc__)
            return 2
        st = open(opt('--status'), errors='replace').read()
        scif = open(opt('--scif'), errors='replace').read() if opt('--scif') else None
        if '--self' in sys.argv:
            n, bad = self_prog(prog, text, st, scif)
            for b in bad:
                print(f'  {b}: accepted')
            print(f'm4 check.py --self --progs {prog}: {n} perturbations, {len(bad)} accepted: {"FAIL" if bad else "PASS"}')
            return 1 if bad else 0
        f = check_prog(prog, text, st, scif)
        for x in f:
            print('  ' + x)
        print(f'm4 {prog}: {len(f)} failures: {"FAIL" if f else "PASS"}')
        return 1 if f else 0
    if '--self' in sys.argv:
        rows, _ = parse(text)
        bad, tried = [], 0
        for (kind, reg), (b, s, g) in rows.items():
            rule = RULES[reg][0 if kind == 'manual' else 1]
            p = perturb(rule, s, g)
            t = text.replace(f'kind={kind} {reg} set={s:08x} got={g:08x}', f'kind={kind} {reg} set={s:08x} got={p:08x}')
            tried += 1
            if t == text or not check(t)[0]:
                bad.append(f'{kind} {reg}: got {p:08x} accepted')
        drop = '\n'.join(l for l in text.splitlines() if ' kind=por SCLSR2 ' not in l)
        tried += 1
        if not check(drop)[0]:
            bad.append('a missing line accepted')
        for b in bad:
            print('  ' + b)
        print(f'm4 check.py --self: {tried} perturbations, {len(bad)} accepted: {"FAIL" if bad else "PASS"}')
        return 1 if bad else 0
    fails, n, weak = check(text)
    for f in fails:
        print('  ' + f)
    if weak:
        print(f'  (uninformative: the value set is the expected one: {", ".join(weak)})')
    print(f'm4 wdtrst: {n} register rows, {len(fails)} failures: {"FAIL" if fails else "PASS"}')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
