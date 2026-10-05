#!/usr/bin/env python3
"""check.py -- tb/bm/soc-adv/m5 (M5, WI-SOC; rtl/v1/SOC.md 8.3 M5): how the M5 gate's runs must end.

usage: check.py --progs boardx|tasdma|tasc LOG --status STATUS [--bus] [--mem ideal] [--self]
       check.py --hello NAME --status STATUS --scif SCIF [--self]
       check.py --kos PROG LOG --status STATUS [--self]
  --progs boardx  tb/bm/soc-adv/m5/boardx (the B1 / B2 features through the pins, without KOS; the rules in its source's
                  header): soc_tb PASS, lockstep pass, every row of BOARDX_ROWS present once and OK, "BX done fails=0"
  --progs tasdma  (M5-fix, M5-verify F3) TAS.B's locked pair against the DMAC (tasdma.c's header): soc_tb PASS, lockstep
                  pass (a DMAC write between TAS's read and write leaves SR.T different from the model's), no monitor
                  violation, rows A-H once each and OK, G / H with TMU0 takes, "TD done fails=0".  --bus: STATUS is
                  the run's soc_tb --bus-trace stream (read line by line, never held): every bus START that reads the
                  TAS byte (a byte read of "TD start"'s tgt: only TAS.B reads it) must be followed, as the very next
                  START, by TAS's byte write of it (data bit 7 set) -- pm TAS.B: "Another memory access is not
                  executed between the two TAS.B accesses"; SOC.md 3.3 -- and at least one pair must be seen
  --progs tasc    (M5-fix, M5-verify F4) TAS.B against the operand cache's line states (tasc.c's header): soc_tb PASS,
                  lockstep pass, no monitor violation, TASC_ROWS rows (C1-C3 once, C4-C8 at n = 0..11), each OK, and
                  "TC done fails=0"; C2's ideal-memory form only with --mem ideal (an im run: the data-less OC)
  --hello NAME    a tools/boot/hello target (hello1f hello2f hello1 hello2 ...) on the RTL chip: soc_tb PASS (exit code 0
                  through the exit stub), lockstep pass, "golden NAME ok" (tb/dc_board/dc_golden.h: the transcript, level
                  >= 1 6-7 vblank IRQs over thd_sleep(100), level 2 one maple device and the no-disc drive), no monitor
                  violation; level 1: "KOS init took N ms" within 5 % of the console's 396 ms ([HW] board.md 6.2, the
                  level-1 program boardio: 377-415) and the vblank count 6-7 read again from the SCIF transcript
  --kos PROG      a console probe unmodified (tools/hw/<area>/PROG.elf) behind the dcload stand-in (soc_tb --dcload): soc_tb
                  PASS (the DCLOAD_EXIT syscall wrote exit code 0), lockstep pass, no monitor violation, and KOS took the
                  console's dcload path: its banner, "dc-load console support enabled" and "arch: exit return code 0" in
                  LOG (the sim-control stream).  The probe's own values are the suite checkers' (tb/mk/v1_soc.mk v1-soc-m5)
  --self          every rule must reject a run changed to violate it (exit 0 when all are rejected; --bus: a DMAC write
                  put between the trace's first TAS pair must be rejected too)
Exit 0 when every rule holds.
"""
import re
import sys

BOARDX_ROWS = ('V vblank-in wakes SLEEP', 'T TMU0 wakes SLEEP', 'G g1 status', 'D gd req_stat intrq',
               'M maple devinfo A0 B0', 'S tas.b')
TASDMA_ROWS = tuple('ABCDEFGH')
TASC_ROWS = (('C1 clean-p1', (0,)), ('C2 clean-p2', (0,)), ('C3 dirty-nb', (0,))) + \
    tuple((c, tuple(range(12))) for c in ('C4 pref', 'C5 ld-miss', 'C6 st0', 'C6 st80', 'C7 ocbwb', 'C8 ocbp'))
BUS_START = re.compile(r'^c(\d+) xb START ([RW]) area \d+ addr ([0-9A-F]{8}) size (\d+) .* wdata ([0-9A-F]{16})')
INIT_MS, INIT_TOL = 396, 0.05          # [HW] board.md 6.2 (boardio: INIT_IRQ | INIT_FS_PTY under dcload), SOC.md 8.3 M5
MON = re.compile(r'^soc_tb: monitors: .*violations MA (\d+) xl (\d+) P4 (\d+) fetch (\d+) bce (\d+) dmahs (\d+) frz (\d+) '
                 r'arr (\d+) SDRAM (\d+)', re.M)


def run_ok(st, why):
    """soc_tb's verdict lines: PASS, lockstep pass, no monitor violation"""
    if not re.search(r'^soc_tb: PASS$', st, re.M):
        why.append('soc_tb did not PASS')
    m = re.search(r'lockstep (\w+)', st)
    if not m or m.group(1) != 'pass':
        why.append(f'lockstep verdict {m.group(1) if m else "missing"}, want pass')
    m = MON.search(st)
    if not m:
        why.append('no monitor line')
    elif any(int(x) for x in m.groups()):
        why.append('monitor violations: ' + m.group(0).split('violations ')[1])


def check_boardx(log, st):
    why = []
    run_ok(st, why)
    for r in BOARDX_ROWS:
        rows = [l for l in log.splitlines() if l.startswith('BX ' + r + ' ')]
        if len(rows) != 1:
            why.append(f'row "{r}": {len(rows)} lines, want 1')
        elif not rows[0].endswith(' OK'):
            why.append(f'row FAIL: {rows[0]}')
    other = [l for l in log.splitlines() if l.startswith('BX ') and not any(l.startswith('BX ' + r + ' ') for r in BOARDX_ROWS)
             and not l.startswith('BX done')]
    if other:
        why.append(f'unexpected lines: {other[:2]}')
    if not re.search(r'^BX done fails=0$', log, re.M):
        why.append('no "BX done fails=0"')
    return why


def rows_once(log, pfx, keys, why):
    """every key's row (a line starting pfx + key) once, ending OK"""
    for k in keys:
        rows = [l for l in log.splitlines() if l.startswith(pfx + k)]
        if len(rows) != 1:
            why.append(f'row "{pfx}{k}": {len(rows)} lines, want 1')
        elif not rows[0].endswith(' OK'):
            why.append(f'row FAIL: {rows[0]}')


def check_tasdma(log, st):
    why = []
    run_ok(st, why)
    rows_once(log, 'TD ', [c + ' units=' for c in TASDMA_ROWS], why)
    for c in 'GH':
        m = re.search(rf'^TD {c} tmu-takes=(\d+)$', log, re.M)
        if not m or int(m.group(1)) == 0:
            why.append(f'case {c}: no TMU0 take during the TAS loop')
    if not re.search(r'^TD start dmaor=[0-9a-f]{8} tgt=[0-9a-f]{8}$', log, re.M):
        why.append('no "TD start" line')
    if not re.search(r'^TD done fails=0$', log, re.M):
        why.append('no "TD done fails=0"')
    return why


def tas_bus(lines, tgt):
    """(pairs, interleaved, first bad) of TAS.B's locked pairs in a --bus-trace stream: every START reading the TAS
    byte (size 0) must be followed, as the next START, by a byte write of it with bit 7 set"""
    pend, pairs, bad, first = None, 0, 0, []
    for ln in lines:
        m = BUS_START.match(ln)
        if not m:
            continue
        rw, addr, size, wd = m.group(2), int(m.group(3), 16), int(m.group(4)), int(m.group(5), 16)
        if pend is not None:
            if rw == 'W' and addr == tgt and size == 0 and wd & 0x80:
                pairs += 1
                pend = None
                continue
            bad += 1
            if len(first) < 3:
                first.append(f'TAS read at c{pend}, then {ln.strip()[:100]}')
            pend = None
        if rw == 'R' and addr == tgt and size == 0:
            pend = m.group(1)
    return pairs, bad, first


def check_tasbus(log, stf, plant=False):
    """--bus: the TAS pairs of the trace in the status file (streamed); plant: a DMAC-like write put between the
    first pair (--self)"""
    m = re.search(r'^TD start .* tgt=([0-9a-f]{8})$', log, re.M)
    if not m:
        return ['--bus: no "TD start ... tgt=" line']
    tgt = int(m.group(1), 16)

    def stream():
        done = False
        with open(stf, errors='replace') as f:
            for ln in f:
                yield ln
                mm = BUS_START.match(ln)
                if plant and not done and mm and mm.group(2) == 'R' and int(mm.group(3), 16) == tgt and mm.group(4) == '0':
                    done = True
                    yield ln.replace(' START R ', ' START W ').rsplit('wdata', 1)[0] + 'wdata 0000000000000000\n'
    pairs, bad, first = tas_bus(stream(), tgt)
    why = []
    if bad:
        why.append(f'--bus: {bad} TAS.B pairs split by another bus access ({pairs} adjacent): ' + '; '.join(first))
    elif pairs == 0:
        why.append('--bus: no TAS.B pair in the trace (not a --bus-trace stream?)')
    return why


def check_tasc(log, st, real=True):
    why = []
    run_ok(st, why)
    for c, ns in TASC_ROWS:
        for n in ns:
            rows = [l for l in log.splitlines() if re.match(rf'TC {re.escape(c)}( \(ideal mem\))? n={n} ', l)]
            if len(rows) != 1:
                why.append(f'row "{c} n={n}": {len(rows)} lines, want 1')
            elif not rows[0].endswith(' OK'):
                why.append(f'row FAIL: {rows[0]}')
            elif '(ideal mem)' in rows[0] and real:
                why.append(f'{rows[0]}: the ideal-memory form on a run with the SDRAM (the OC holds data)')
    if not re.search(r'^TC done fails=0$', log, re.M):
        why.append('no "TC done fails=0"')
    return why


def check_hello(name, st, scif):
    why = []
    run_ok(st, why)
    if f'golden {name} ok' not in st:
        why.append(f'no "golden {name} ok"')
    m = re.match(r'hello(\d)f?$', name)
    level = int(m.group(1)) if m else -1
    if level < 0:
        why.append(f'unknown hello target {name}')
    t = scif.replace('\r', '')
    mi = re.search(r'boot level (\d): KOS init took (\d+) ms', t)
    if not mi or int(mi.group(1)) != level:
        why.append('no "boot level N: KOS init took" line of this level')
    elif level == 1:
        ms = int(mi.group(2))
        lo, hi = INIT_MS * (1 - INIT_TOL), INIT_MS * (1 + INIT_TOL)
        if not lo <= ms <= hi:
            why.append(f'KOS init took {ms} ms, want {INIT_MS} ms +- 5 % ({lo:.1f}-{hi:.1f}) (SOC.md 8.3 M5)')
    if level >= 1:
        mv = re.search(r'hello: (\d+) vblank IRQs in 100 ms', t)
        if not mv or not 6 <= int(mv.group(1)) <= 7:
            why.append(f'vblank IRQs over thd_sleep(100): {mv.group(1) if mv else "none"}, want 6-7')
    return why


def check_kos(prog, log, st):
    why = []
    run_ok(st, why)
    for s in ('\n--\nKallistiOS v2.1.0\n', 'dc-load console support enabled\n', '\narch: exit return code 0\n'):
        if s not in log:
            why.append('the console stream lacks ' + repr(s))
    if 'exit code 0;' not in st:
        why.append('the run did not end with exit code 0 (the DCLOAD_EXIT syscall)')
    return why


def perturbations(mode, a, b, c):
    """(label, args) -- each must be rejected"""
    out = []
    sub = lambda s, p, r: re.sub(p, r, s, count=1, flags=re.M)
    st = b
    common = [('soc_tb FAIL', 'st', r'^soc_tb: PASS$', 'soc_tb: FAIL'),
              ('lockstep mismatch', 'st', r'lockstep pass', 'lockstep FAIL'),
              ('a monitor violation', 'st', r'SDRAM 0$', 'SDRAM 1')]
    if mode == 'boardx':
        per = common + [('a FAIL row', 'log', r' OK$', ' FAIL'), ('a row missing', 'log', r'^BX G g1 status .*\n', ''),
                        ('no done line', 'log', r'^BX done fails=0$', ''), ('a row twice', 'log', r'^(BX T .*)$', r'\1\n\1')]
    elif mode == 'tasdma':
        per = common + [('a FAIL row', 'log', r'(^TD C .*) OK$', r'\1 FAIL'), ('a row missing', 'log', r'^TD E units.*\n', ''),
                        ('no done line', 'log', r'^TD done fails=0$', ''), ('a row twice', 'log', r'^(TD A units.*)$', r'\1\n\1'),
                        ('no TMU0 take in H', 'log', r'^TD H tmu-takes=\d+$', 'TD H tmu-takes=0')]
    elif mode == 'tasc':
        per = common + [('a FAIL row', 'log', r'(^TC C7 ocbwb n=5 .*) OK$', r'\1 FAIL'),
                        ('a row missing', 'log', r'^TC C4 pref n=11 .*\n', ''), ('no done line', 'log', r'^TC done fails=0$', ''),
                        ('a row twice', 'log', r'^(TC C1 .*)$', r'\1\n\1')]
        if opt('--mem') != 'ideal':
            per.append(('C2\'s ideal-memory form on the real memory', 'log', r'^TC C2 clean-p2 n=0 .*$',
                        'TC C2 clean-p2 (ideal mem) n=0 T=1 mem=80 p1=80 OK'))
    elif mode == 'hello':
        per = common + [('no golden', 'st', r'golden \w+ ok', 'golden x FAIL')]
        if re.search(r'boot level 1:', c):
            per += [('KOS init 376 ms', 'scif', r'init took \d+ ms', 'init took 376 ms'),
                    ('KOS init 416 ms', 'scif', r'init took \d+ ms', 'init took 416 ms'),
                    ('5 vblank IRQs', 'scif', r'hello: \d+ vblank', 'hello: 5 vblank'),
                    ('8 vblank IRQs', 'scif', r'hello: \d+ vblank', 'hello: 8 vblank')]
        per += [('another level', 'scif', r'boot level \d', 'boot level 9')]
    else:
        per = common + [('no dcload console', 'log', r'dc-load console support enabled\n', ''),
                        ('a non-zero exit', 'log', r'exit return code 0', 'exit return code 1'),
                        ('no KOS banner', 'log', r'KallistiOS v2.1.0', 'KallistiOS v2.0.0'),
                        ('exit code 1', 'st', r'exit code 0;', 'exit code 1;')]
    for label, which, p, r in per:
        la, lb, lc = a, b, c
        if which == 'log':
            la = sub(a, p, r)
            if la == a: out.append((label, None)); continue
        elif which == 'st':
            lb = sub(b, p, r)
            if lb == b: out.append((label, None)); continue
        else:
            lc = sub(c, p, r)
            if lc == c: out.append((label, None)); continue
        out.append((label, (la, lb, lc)))
    return out


def opt(name):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else None


def main():
    selfm = '--self' in sys.argv
    vals = {opt(o) for o in ('--progs', '--status', '--scif', '--hello', '--kos', '--mem')}
    pos = [x for x in sys.argv[1:] if not x.startswith('--') and x not in vals]
    rd = lambda f: open(f, errors='replace').read() if f else ''
    bus = '--bus' in sys.argv
    if opt('--progs'):
        if opt('--progs') not in ('boardx', 'tasdma', 'tasc') or not pos or not opt('--status') or \
                (bus and opt('--progs') != 'tasdma'):
            print(__doc__); return 2
        mode = name = opt('--progs')
        a, c = rd(pos[0]), ''
        if bus:                                        # the status file is a bus trace: only its soc_tb lines, streamed
            with open(opt('--status'), errors='replace') as f:
                b = ''.join(l for l in f if l.startswith('soc_tb'))
        else:
            b = rd(opt('--status'))
        base = {'boardx': check_boardx, 'tasdma': check_tasdma,
                'tasc': lambda a, b: check_tasc(a, b, opt('--mem') != 'ideal')}[mode]
        fn = lambda a, b, c: base(a, b) + (check_tasbus(a, opt('--status')) if bus else [])
    elif opt('--hello'):
        if not opt('--status') or not opt('--scif'):
            print(__doc__); return 2
        mode, name, a, b, c = 'hello', opt('--hello'), '', rd(opt('--status')), rd(opt('--scif'))
        fn = lambda a, b, c: check_hello(name, b, c)
    elif opt('--kos'):
        if not pos or not opt('--status'):
            print(__doc__); return 2
        mode, name, a, b, c = 'kos', opt('--kos'), rd(pos[0]), rd(opt('--status')), ''
        fn = lambda a, b, c: check_kos(name, a, b)
    else:
        print(__doc__); return 2
    why = fn(a, b, c)
    if selfm:
        if why:
            print(f'm5 check.py --self {mode} {name}: the unperturbed run fails ({why[0]})')
            return 1
        bad = []
        per = perturbations(mode, a, b, c)
        for label, args in per:
            if args is None:
                bad.append(f'{label}: the perturbation changed nothing')
            elif not fn(*args):
                bad.append(f'{label}: accepted')
        if bus:                                        # a DMAC write between the trace's first TAS pair
            per.append(('a write between a TAS pair (--bus)', None))
            if not check_tasbus(a, opt('--status'), plant=True):
                bad.append('a write between a TAS pair (--bus): accepted')
        for x in bad:
            print('  ' + x)
        print(f'm5 check.py --self {mode} {name}: {len(per)} perturbations, {len(bad)} accepted: {"FAIL" if bad else "PASS"}')
        return 1 if bad else 0
    for x in why:
        print('  FAIL ' + x)
    print(f'm5 check.py {mode} {name}: {"FAIL" if why else "PASS"}')
    return 1 if why else 0


if __name__ == '__main__':
    sys.exit(main())
