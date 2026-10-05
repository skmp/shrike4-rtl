#!/usr/bin/env python3
"""mkexpected.py -- builds expected.json for the timers-clock bare-metal ports (tb/bm/timers-clock: tc1, tc2, tc3, tc4, tck)
from the console logs tools/hw/timers-clock/{tc,tc2,tc3,tc4}.elf.hwlog (spec rtl/v1/spec/timers-clock.md 6).  Owner: WI-CLK.

Every console line of a program becomes one entry, in print order:
  id       the line's leading text (check.py pairs lines by position and verifies the id)
  console  the console line, verbatim
  line     (exact) the line the port must print: the console line with the port's documented text changes (`sub`)
  sub      [[console text, port text], ...]: the port's shortened observation windows name the window they used
  mode     exact   the port's line equals `line`
           match   the line fully matches `regex` (fields the console shows as undefined or KOS-dependent are wild)
           timing  `regex` must match and every `func` expression holds in every profile (functional facts that show
                   through timed samples); the `timing` expressions are checked only with --timing (the SoC run; the
                   ISS runner's timing is nominal)
           irq     an interrupt test (J, S): checked only with --irq; without, the port prints the `noirq` entry's line
           noirq   the skip line the port prints when built with -DBM_NO_IRQ (only without --irq)
           board   needs the board model (tck: the AICA RTC; M4): checked only with --board
           info    never checked (values the silicon leaves undefined)
Expressions (func / timing) are Python, evaluated by check.py with: L the line, G the regex's named groups (strings),
T the tuples of the line (the whitespace-split contents of every (...) or [...] group, in order), h(x) = int(x, 16),
d(x) = int(x), cspread(values, period) = the spread of values on a circle of that period, nonincr(values), and the R
helpers rtwo / rwin / rlat (check.py).

    python3 tb/bm/timers-clock/mkexpected.py         writes tb/bm/timers-clock/expected.json
"""
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
LOGS = os.path.join(ROOT, 'tools', 'hw', 'timers-clock')

X8, X4, X2 = '[0-9a-f]{8}', '[0-9a-f]{4}', '[0-9a-f]{2}'
TUP = r'(?: \([^()]*\))'
BRK = r'(?: \[[^\[\]]*\])'


def esc(s):
    return re.escape(s).replace('\\ ', ' ')


def lines_of(log, first, last):
    ls = [l.rstrip('\r\n') for l in open(os.path.join(LOGS, log), errors='replace')]
    i = next(k for k, l in enumerate(ls) if l.startswith(first))
    j = next(k for k in range(i, len(ls)) if ls[k].startswith(last))
    return [l for l in ls[i:j + 1] if l.strip()]


def ident(line):
    """the leading text check.py verifies: the first words before the first ':', '(', '[' or '->' (at most 4)"""
    if line.startswith('F   '):                                    # F rows: the access's name (the total is timed)
        return line[:line.index('total')].rstrip()
    if line.startswith('T bracket'):
        return 'T bracket'
    head = re.split(r':|\(|\[| ->', line, maxsplit=1)[0]               # the text before any value
    return ' '.join(head.split()[:4])


def exact(line, sub=None, note=None):
    port = line
    for a, b in sub or []:
        assert a in port, (a, line)
        port = port.replace(a, b)
    e = {'mode': 'exact', 'line': port}
    if sub:
        e['sub'] = sub
    if note:
        e['note'] = note
    return e


def match(regex, note, sub=None):
    e = {'mode': 'match', 'regex': regex, 'note': note}
    if sub:
        e['sub'] = sub
    return e


def timing(regex, func, tim, note, sub=None):
    e = {'mode': 'timing', 'regex': regex, 'func': func, 'timing': tim, 'note': note}
    if sub:
        e['sub'] = sub
    return e


# ---- run 1 (tc.c -> tc1.c) --------------------------------------------------------------------------------------------
A_PER = {0: 16, 1: 64, 2: 256, 3: 1024, 4: 4096}
F_TOL = 16            # F rows: 16 accesses, so +-1 Icyc per access


def rule_tc1(line):
    if line.startswith('0: PMCR0') or line.startswith('Z: PMCR0'):
        return exact(line)
    if line.startswith('0: IPRA') or line.startswith('Z: IPRA'):
        tag = line[:2]
        return match('^' + esc(tag) + r' IPRA f0f0 IPRB 0000 IPRC 0300 TOCR 00 TSTR 05 TCR0 0[01]20 TCR1 0000 '
                     r'TCR2 0[01]20$',
                     'KOS state (bmlib.c kos_timer_state); TMU0 / TMU2 run with UNIE, and with no KOS handler to clear '
                     'UNF the port may show it set (0120)')
    if line.startswith('0: TCOR0') or line.startswith('Z: TCOR0'):
        tag = line[:2]
        tcor1 = '000030b4' if tag == '0:' else 'ffffffff'
        return match('^' + esc(tag) + ' TCOR0 0001e70f TCNT0 ' + X8 + ' TCOR1 ' + tcor1 + ' TCNT1 ' + X8 +
                     ' TCOR2 00be41f0 TCNT2 ' + X8 + ' TCPR2 ' + X8 + '$',
                     'running counters are time-dependent; TCPR2 is never initialised (console ff9b8012, simulation 0)')
    if line.startswith('0: RTC') or line.startswith('Z: RTC'):
        return exact(line, note='MSTP1 = 1: every RTC register reads 0 [HW]')
    m = re.match(r'A tpsc (\d): ', line)
    if m:
        t = int(m.group(1))
        if t >= 5:
            return exact(line, [['over 4M cycles', 'over 200k cycles']], 'TPSC 5 / 6 / 7 never count on the DC [HW]')
        per = A_PER[t]
        return timing('^A tpsc %d: (?P<dc>\\d+) cycles / (?P<dn>\\d+) counts = (?P<r>\\d+\\.\\d{3}) cycles per count$' % t,
                      ['abs(float(G["r"]) - %d) <= %d * 0.001' % (per, per)], [],
                      'the rate: %d Icyc per count [HW] A (window shortened 10x)' % per)
    m = re.match(r'B tpsc (\d) ', line)
    if m:
        per = A_PER[int(m.group(1))]
        return timing('^' + esc(line.split(':')[0]) + r':(?: \[\d+ \d+ r\d+ v\d+\]){16}$',
                      ['all(t[2] == "r1000" and t[3] == "v999" for t in T)'],
                      ['all(1 <= d(t[0]) <= %d + 64 for t in T)' % per,
                       'all(abs(d(t[1]) - %d) <= 32 for t in T)' % per],
                      'a start never counts at once (r1000) and the first count is 999; first decrement within one '
                      'period of the start, the second one period later (%d; console %s)' % (per, 'B'))
    if line.startswith('C tpsc'):
        return timing('^' + esc(line.split(':')[0]) + r': \(cw-e0, ce-e0, ce-cw, readback\):(?: \[\d+ \d+ \d+ \d+\]){12}$',
                      ['all(d(t[3]) != 5000 and d(t[3]) >= 0xFFFFF000 for t in T)', 'nonincr([d(t[3]) for t in T])'],
                      ['all(30 <= d(t[2]) <= 64 for t in T)'],
                      'a TCNT write while running is ignored [HW] C: the read-backs continue the old countdown')
    m = re.match(r'D tpsc 4 TCOR (\d) TCNT (\d): ', line)
    if m:
        tcor, tcnt = int(m.group(1)), int(m.group(2))
        if tcor == 0:
            return timing('^' + esc(line.split(':')[0]) + r': \(cycle, TCNT, TCR\): \((?P<c>\d+) 0 0104\)$',
                          [], ['1 <= d(G["c"]) <= 4096 + 64'],
                          'TCOR 0, TCNT 0: TCNT stays 0 and UNF is set by the first tick [HW] D')
        seq = [tcnt]
        while len(seq) < 60:
            seq.append(tcor if seq[-1] == 0 else seq[-1] - 1)
        return timing('^' + esc(line.split(':')[0]) + r': \(cycle, TCNT, TCR\):(?: \(\d+ \d+ 0[01]04\))+$',
                      ['dseq(T) == %s[1:len(dseq(T)) + 1]' % seq, 'len(dseq(T)) >= 6',
                       'dunf(T, %d)' % tcor],
                      ['dgaps(T, 4096, 64)'],
                      'TCNT counts %s: period TCOR + 1 ticks, 0 held one tick, UNF with (or just before) the reload '
                      '[HW] D; one tick = 4096 Icyc apart' % ' '.join(map(str, seq[1:8])))
    if line.startswith('E TOCR'):
        return match('^' + esc(line.split('TCPR2')[0]) + 'TCPR2 ' + X8 + '; TSTR rd16 0000$',
                     'TCPR2 is never initialised (console ff9b8012, simulation 0)')
    if line.startswith('E '):
        return exact(line)
    if line.startswith('F empty'):
        return timing(r'^F empty PMCTR bracket (?P<b>\d+) cycles; per-access cycles \(x16 total - bracket\):$', [],
                      ['d(G["b"]) == 3'], 'a PMCTR read costs 3 Icyc [HW] F')
    m = re.match(r'F   (.+?)\s+total\s+(\d+)', line)
    if m:
        tot = int(m.group(2))
        pre = line[:line.index('total')]
        return timing('^' + esc(pre) + r'total\s+(?P<t>\d+)  per access \d+\.\d\d$', [],
                      ['abs(d(G["t"]) - %d) <= %d' % (tot, F_TOL)],
                      'back-to-back access cost (SOC.md 3.1.5; WI-P4 owns it): console total %d, +-1 Icyc per access'
                      % tot)
    if line.startswith('G CKS'):
        cks = int(line[6])
        per = 32 << cks
        return timing('^G CKS %d: edges \\(cycle,WTCNT\\):(?: \\(\\d+ \\d+\\))+ \\| reach 0x80 at (?P<r80>\\d+), IOVF at '
                      '(?P<io>\\d+) \\(WTCNT \\d+\\), end WTCSR %02x WTCNT %s, stopped (?P<s0>%s)->(?P<s1>%s)$'
                      % (cks, 0x88 | cks, X2, X2, X2),
                      ['G["s0"] == G["s1"]', 'nonincr([-d(t[1]) for t in T])'],
                      ['%d <= d(G["r80"]) <= %d' % (126 * per, 128 * per + 64),
                       '%d <= d(G["io"]) <= %d' % (254 * per, 256 * per + 64)],
                      'interval mode: IOVF after 256 counts at %d Icyc per count (the first count may be early), '
                      'WTCSR %02x, counting continues, TME = 0 holds [HW] G' % (per, 0x88 | cks))
    if line.startswith('G '):
        return exact(line)
    if line.startswith('H RTC as found'):
        return exact(line, [['over 20M cycles', 'over 200k cycles']], 'MSTP1 = 1: reads 0 [HW] H')
    if line.startswith('H RCR2=09'):
        return exact(line, [['over 40M cycles', 'over 400k cycles']], 'MSTP1 = 1: reads 0, writes ignored [HW] H')
    if line.startswith('H '):
        return exact(line, note='MSTP1 = 1: reads 0, writes ignored [HW] H')
    if line.startswith('I MSTP2 other'):
        return match(r'^I MSTP2 other regs before/after: TCR0 (0[01]20)/\1 TCR2 (0[01]20)/\2 TCOR0 0001e70f/0001e70f '
                     r'TCOR2 00be41f0/00be41f0 TOCR 00/00$',
                     'MSTP2 keeps TCR / TCOR / TOCR [HW] I (TCR0 / TCR2 UNF may be set: no KOS handler)')
    if line.startswith('I MSTP2:'):
        return timing(r'^I MSTP2: STBCR 03 \(07 while set\); TSTR 05 -> 00 after; (?P<c>\d+) cycles: TCNT1 -(?P<n1>\d+) '
                      r'counts, TCNT2 -(?P<n2>\d+) counts \(x16 = \d+\)$',
                      ['d(G["n1"]) <= 8', 'd(G["n2"]) <= 4'], ['160000 <= d(G["c"]) <= 160600'],
                      'MSTP2: TSTR 05 -> 00 and the counters stop (3 and 1 counts over 160k Icyc) [HW] I')
    m = re.match(r'J (spin|sleep|poll) ', line)
    if m:
        mode = m.group(1)
        rx = '^' + esc(line.split(':')[0]) + r':(?: \(\d+ \d \d+ [0-9a-f]{8} -?\d+ [0-9a-f]{3} \d+\)){16}$'
        if mode == 'spin':
            func = ['all(t[1] == "1" and t[5] == "420" for t in T)']
            tim = ['all(10 <= d(t[2]) - 16 * (d(t[0]) + 1) <= 48 for t in T)',
                   'all(0xFFFFFFF9 <= h(t[3]) <= 0xFFFFFFFE for t in T)']
            note = 'TMU1 underflow interrupt from a spin loop: INTEVT 0x420 [HW] J; handler 16(N+1) + 17..32 Icyc'
        elif mode == 'sleep':
            func = ['all(t[1] == "1" and t[4] == "2" and t[5] == "420" for t in T)']
            tim = ['all(30 <= d(t[2]) <= 110 for t in T)', 'all(0xFFFFFFF9 <= h(t[3]) <= 0xFFFFFFFE for t in T)']
            note = ('wake from SLEEP: SPC = SLEEP + 2, INTEVT 0x420 [HW] J; PMCTR stops while asleep (48-76 counted; the '
                    'clock-stop handshake after SLEEP is counted, SOC.md 2.5 M1b: the SoC gives 60)')
        else:
            func = ['all(t[1] == "4" for t in T)']
            tim = ['all(10 <= d(t[2]) - 16 * (d(t[0]) + 1) <= 60 for t in T)']
            note = 'polling TCR1.UNF with the interrupt masked (the SPC-off column is not meaningful here)'
        e = timing(rx, func, tim, note)
        e['mode'] = 'irq'
        return e
    if line == 'tc: done':
        return exact(line)
    raise SystemExit('tc1: no rule for %r' % line)


# ---- run 2 (tc2.c) ------------------------------------------------------------------------------------------------
def rule_tc2(line):
    if line.startswith('tc2:'):
        return exact(line)
    m = re.match(r'Q tpsc (\d) TCNT write 5000', line)
    if m:
        return timing('^' + esc(line.split(':')[0]) + r':(?: \[[0-9a-f]{8} [0-9a-f]{8} [0-9a-f]{8}\]){4}$',
                      ['all(h(v) != 5000 and h(v) >= 0xFFFFFF00 for t in T for v in t)',
                       'all(nonincr([h(v) for v in t]) for t in T)'], [],
                      'a TCNT write while running is ignored, also after the stop [HW] Q')
    if line.startswith('Q TCOR1 wr 1000'):
        return timing(r'^Q TCOR1 wr 1000 while running: reads 1000; TCNT1 after the underflow (?P<v>\d+) \(TCR1 0100\)$',
                      ['d(G["v"]) < 1000'], ['700 <= d(G["v"]) <= 999'],
                      'TCOR written while running is used at the next underflow [HW] Q')
    if line.startswith('Q TCR1 TPSC 0->4'):
        return timing(r'^Q TCR1 TPSC 0->4 while running: TCR1 0004, edges (?P<a>\d+) apart then (?P<b>\d+) '
                      r'\(values (?P<v0>[0-9a-f]{8}) (?P<v1>[0-9a-f]{8}) (?P<v2>[0-9a-f]{8})\)$',
                      ['h(G["v0"]) - h(G["v1"]) == 1 and h(G["v1"]) - h(G["v2"]) == 1'],
                      ['abs(d(G["a"]) - 4096) <= 64 and abs(d(G["b"]) - 4096) <= 64'],
                      'a TPSC change takes effect while running: edges one Pphi/1024 tick apart [HW] Q')
    m = re.match(r'Q tpsc (\d) restart', line)
    if m:
        per = A_PER[int(m.group(1))]
        return timing('^' + esc(line.split(':')[0]) + r': \(start mod per, first edge mod per, edge-start\):'
                      r'(?: \(\d+ \d+ \d+\)){8}$',
                      ['cspread([d(t[1]) for t in T], %d) <= 48' % per],
                      ['all(1 <= d(t[2]) <= %d + 64 for t in T)' % per],
                      'the prescaler is never restarted: the first edge after each of 8 starts falls on the same phase '
                      'modulo the tick period (console spread 24 / 20) [HW] Q')
    if line.startswith('Q '):
        return exact(line)
    m = re.match(r'R CKS (\d): ', line)
    if m:
        per = 32 << int(m.group(1))
        tol = 24 if per == 256 else 48
        return timing('^' + esc(line.split(':')[0]) + r': \(start mod per, e1-start, e2-e1, e3-e2, e1 mod per\):'
                      r'(?: \(\d+ \d+ \d+ \d+ \d+\)){10}$',
                      ['rtwo(T, %d, 48, 64)' % per, 'rwin(T, %d, %d, 64)' % (per, tol)],
                      ['all(abs(d(t[3]) - %d) <= 32 for t in T)' % per, 'abs(rlat(T, %d, %d, 64) - 19) <= 12' % (per, tol)],
                      'the WDT counts on the rising edge of (TME & tap): the second count is always on the regular '
                      'edge; the first one is early exactly when the TME write lands in the tap\'s high half (the half '
                      'period after the regular edge), else on it; the write lands L = 19 Icyc after the start sample '
                      '(the early trials\' e1 - start: 19 in 9 of 11 console trials, the other two are each line\'s '
                      'first trial, 39 / 31) [HW] R')
    if line.startswith('R WTCNT write'):
        return timing(r'^R WTCNT write while counting \(cw-e0, ce-e0, readback, next\):(?: \(\d+ \d+ 10 11\)){8}$',
                      ['all(abs(d(t[1]) - 4096) <= 48 for t in T)'], [],
                      'a WTCNT write while counting is taken and the next count stays on the regular edge (the divider '
                      'is never restarted) [HW] R')
    if line.startswith('R '):
        return exact(line)
    m = re.match(r'S (spin|sleep|poll) ', line)
    if m:
        mode = m.group(1)
        rx = ('^' + esc(line.split(':')[0]) + r':(?: \(\d+ \d \d+ \d+ \d+\)){16} \| k histogram 0\.\.7:'
              r'(?P<hist>(?: \d+){8})$')
        k1, k2 = 'd(G["hist"].split()[1])', 'd(G["hist"].split()[2])'
        if mode == 'spin':
            func, tim = ['all(t[1] == "1" for t in T)'], ['all(t[2] == "1" for t in T)', k1 + ' == 32']
            note = 'handler\'s first TCNT1 read: TCOR - 1 (one tick after the reload) in 32 of 32 [HW] S'
        elif mode == 'sleep':
            func, tim = ['all(t[1] == "1" for t in T)'], ['all(t[2] in ("1", "2") for t in T)',
                                                           k1 + ' + ' + k2 + ' == 32', k2 + ' >= 16',
                                                           'len([t for t in T if t[2] == "2"]) >= 8']
            note = ('from SLEEP: 2 ticks in 26 of 32, 1 in 6 (10 of the 16 shown) [HW] S.  The rule needs 2 ticks in at least '
                    '16 of 32 (8 of the 16 shown): the '
                    'wake cost (v1_cpg\'s resynchroniser, 15 Icyc for a TMU wake since M1b -- 4 Pcyc: 2 or 3 gave 1 '
                    'tick in every trial --, plus the core\'s restart, minus the spin loop\'s boundary wait) must move '
                    'most handler reads across the next Pphi/4 edge, as on the console; every trial at 1 tick is what '
                    'a zero or too small wake cost prints')
        else:
            func, tim = ['all(t[1] == "4" for t in T)'], ['all(t[2] in ("1", "2") for t in T)', k1 + ' + ' + k2 + ' == 32']
            note = 'polling: 1 tick in 12 of 32, 2 in 20 [HW] S'
        e = timing(rx, func, tim, note)
        e['mode'] = 'irq'
        return e
    if line.startswith('T bracket'):
        rx = (r'^T bracket (?P<b>\d+); single access \(bracketed total\): rd32 TCNT1 (?P<r32>\d+), rd16 TCR1 (?P<r16>\d+), '
              r'wr32 TCOR1 (?P<w32>\d+), wr16 TCR1 (?P<w16>\d+), wr16 TCR1\+rd16 IPRA (?P<wr>\d+), rd32 PMCTR (?P<pm>\d+)$')
        g = re.match(rx, line).groupdict()
        return timing(rx, [], ['abs(d(G["%s"]) - %s) <= 3' % (k, v) for k, v in g.items()],
                      'isolated access costs by Pclk phase (SOC.md 3.1.5; WI-P4 owns them): console %s, +-3 Icyc'
                      % ' '.join('%s %s' % kv for kv in g.items()))
    if line.startswith('P as found'):
        return {'mode': 'info', 'note': 'the RTC counters and alarms are never initialised: the console shows left-over '
                'values, the simulation 0 [P]'}
    if line.startswith('P R64CNT/RSECCNT/RCR1 samples'):
        return match(r'^P R64CNT/RSECCNT/RCR1 samples every 100k cycles: 00/(?P<s>[0-9a-f]{2})/00(?: 00/(?P=s)/00){7}$',
                     'no oscillator: nothing counts (the seconds hold their undefined value) [HW] P',
                     [['every 10M cycles', 'every 100k cycles']])
    if line.startswith('P TMU1 TPSC 6'):
        return exact(line, [['over 8M cycles', 'over 200k cycles']], 'TPSC 6 never counts on the DC [HW] P')
    if line.startswith('P after writes'):
        return match('^' + esc(line.split(' AR ')[0]) + ' AR' + ' [0-7][0-9a-f]' * 6 + ' RCR1 00 RCR2 00$',
                     'the alarm registers still hold their undefined power-up values (ENB 0)')
    if line.startswith('P RCR2 wr 09 ->'):
        return exact(line, [['every 20M cycles', 'every 100k cycles']], 'no oscillator: nothing counts [HW] P')
    if line.startswith('P '):
        return exact(line)
    raise SystemExit('tc2: no rule for %r' % line)


def rule_tc4(line):
    if line.startswith('tc4 as found'):
        return {'mode': 'info', 'note': 'the RTC counters are never initialised (left over from the previous run on the '
                'console, 0 in simulation)'}
    return exact(line, note='WOVF / RSTS written; the RTCEN gate of the alarm comparator; the day-overflow rule and the '
                 'calendar on invalid values; ADJ with the START it writes [HW] tc4')


def rule_tc3(line):
    if line.startswith('tc3 as found'):
        return {'mode': 'info', 'note': 'the RTC counters are never initialised (console f860-02-03 1e:11:00, simulation 0)'}
    return exact(line, note='the calendar chain through 30-second adjustments, ADJ with START = 0, the alarm comparator '
                 '[HW] tc3')


# ---- M1b (WI-SOC): the warm SLEEP probe tools/hw/timers-clock/sleepwarm.c (one console run, 2026-09-24) --------------
# (flag hs k spc af) per trial: flag 1 = woke from SLEEP, hs = PMCTR1L counted from the read before the TSTR start to
# the handler's read, k = TMU1 ticks at the handler's TCNT1 read, spc = SPC - SLEEP, af = counted to the read after the
# SLEEP returns.  The first trial of a W row is its warm-up (cold on the console).  The rules are the console's own warm
# ranges (the SoC is deterministic, the console's stop point varies by up to 4 Pcyc from trial to trial):
SW_TUP = r'(?: \(\d \d+ \d+ -?\d+ \d+\))'


def rule_sw(line):
    if line.startswith('sleepwarm: start') or line.startswith('sleepwarm: (flag') or line == 'sleepwarm: done':
        return exact(line)
    m = re.match(r'([WEP]) ([JS]) (N|K)=(\d+):', line)
    if not m:
        raise SystemExit('sleepwarm: no rule for %r' % line)
    sweep, h, par, v = m.group(1), m.group(2), m.group(3), int(m.group(4))
    n = 9 if sweep == 'W' else 4
    rx = '^' + esc(line.split(':')[0]) + ':' + SW_TUP + '{%d}$' % n
    func = ['all(t[0] == "1" and t[3] == "2" for t in T)']          # every trial woke from SLEEP: SPC = SLEEP + 2
    W = 'T[1:]' if sweep == 'W' else 'T'                              # (the warm-up is cold on the console)
    if h == 'J':
        # handler reads PMCTR1L first: counted 50-66 warm (interquartile 58-66); 82 more to the read after the return
        lo, hi = (50, 68) if sweep == 'P' else (50, 66)
        ok = f'{lo} <= d(t[1]) <= {hi} and t[2] in ("2", "3") and d(t[4]) - d(t[1]) == 82'
        tim = [f'all({ok} for t in {W})'] if sweep != 'E' else [f'len([t for t in {W} if {ok}]) >= len({W}) - 1']
        note = ('the clock-stop handshake after SLEEP is counted, the sleep is not [HW sleepwarm]: 50-66 warm (the clock '
                'stopped at the request: 28); the handler-to-return part 82 exact; k 2 / 3 (TCNT1 read second); an E '
                'row (4 trials, the underflow after the entry) may hold one trial outside: the console\'s N = 7 has 46')
        if sweep == 'E' and v <= 3:                                   # the underflow across the entry: the console's
            tim = []                                                  # stop point varies (a running take at N = 0)
            note = 'the underflow sweeps across the clock-stop handshake: reported (the console\'s stop varies, SOC.md 9.20)'
    else:
        # handler reads TCNT1 first: counted 66-86 warm (66-82), 2 ticks after the reload in 24 of 32 warm trials
        lo, hi = (66, 90) if sweep == 'P' else (66, 86)
        tim = [f'len([t for t in {W} if {lo} <= d(t[1]) <= {hi} and t[2] in ("1", "2")]) >= len({W}) - 2',
               f'all(d(t[4]) - d(t[1]) == 60 for t in {W})']
        if sweep == 'W':                                              # 8 warm trials: the second tick in most
            tim.append(f'len([t for t in {W} if t[2] == "2"]) * 2 >= len({W})')
        note = ('[HW sleepwarm]: counted 66-86 warm with two outliers (110 / 122, k 4 / 5, one row); the handler\'s first '
                'TCNT1 read sees the second tick after the reload in at least half of a W row\'s 8 trials (24 of 32 warm: the '
                'wake costs 4 Pcyc of resync; P rows, 4 trials: 1 or 2); the handler-to-return part 60 exact')
    e = timing(rx, func, tim, note)
    e['mode'] = 'irq'
    return e


def build(prog, lines, rule):
    out = []
    for l in lines:
        if prog == 'tc1' and l.startswith('K AICA'):
            continue                                                  # test K is tck (board)
        e = {'console': l}
        e.update(rule(l))
        port = l
        for a, b in e.get('sub', []):
            port = port.replace(a, b)
        e = dict(id=ident(port), **e)
        out.append(e)
        if prog == 'tc1' and l.startswith('J poll'):
            out.append({'id': 'J skipped', 'console': None, 'mode': 'noirq',
                        'line': 'J skipped: no interrupts in this runner'})
        if prog == 'tc2' and l.startswith('S poll'):
            out.append({'id': 'S skipped', 'console': None, 'mode': 'noirq',
                        'line': 'S skipped: no interrupts in this runner'})
    return out


def main():
    tc1 = lines_of('tc.elf.hwlog', '0: PMCR0', 'tc: done')
    tc2 = lines_of('tc2.elf.hwlog', 'tc2: STBCR', 'tc2: done')
    tc3 = lines_of('tc3.elf.hwlog', 'tc3 as found', 'tc3: done')
    tc4 = lines_of('tc4.elf.hwlog', 'tc4: STBCR', 'tc4: done')
    sw = lines_of('sleepwarm.elf.hwlog', 'sleepwarm: start', 'sleepwarm: done')
    k = next(l for l in tc1 if l.startswith('K AICA'))
    doc = {
        'about': 'Expected output of tb/bm/timers-clock (tc1, tc2, tc3, tck, sleepwarm) from the console logs '
                 'tools/hw/timers-clock/{tc,tc2,tc3,tc4}.elf.hwlog (runs of 2026-09-23) and sleepwarm.elf.hwlog (M1b, '
                 '2026-09-24).  Generated by mkexpected.py; checked by check.py (see its docstring for the profiles).',
        'programs': {
            'tc1': build('tc1', tc1, rule_tc1),
            'tc2': build('tc2', tc2, rule_tc2),
            'tc3': build('tc3', tc3, rule_tc3),
            'tc4': build('tc4', tc4, rule_tc4),
            'sleepwarm': build('sleepwarm', sw, rule_sw),
            'tck': [
                dict(id='K AICA RTC', console=k, mode='board',
                     regex=r'^K AICA RTC \d+ -> \d+ \((?P<s>\d+) s\): \d+ CPU cycles \(\d+ per s\), \d+ TMU1 P/4 counts '
                           r'\(\d+ per s\), cycles/count (?P<r>\d+\.\d{4})$',
                     func=['d(G["s"]) >= 2', 'abs(float(G["r"]) - 16.0) <= 0.0002'], timing=[],
                     note='Pphi/4 = 16.0000 Icyc per count against the AICA RTC [HW] K (the absolute rate is the board '
                          'model\'s, G9)'),
                dict(id='tck: done', console=None, mode='board', line='tck: done'),
            ],
        },
    }
    with open(os.path.join(HERE, 'expected.json'), 'w') as f:
        json.dump(doc, f, indent=1)
        f.write('\n')
    for p, es in doc['programs'].items():
        n = {}
        for e in es:
            n[e['mode']] = n.get(e['mode'], 0) + 1
        print(p, len(es), 'entries', n)


if __name__ == '__main__':
    sys.exit(main())
