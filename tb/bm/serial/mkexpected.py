#!/usr/bin/env python3
# mkexpected.py -- expected/<prog>.json from the console logs of the serial probes (WI-SER; README.md).
#
#   mkexpected.py            writes expected/scif_probe.json, scif_probe2.json, scif_probe3.json from
#                            tools/hw/serial/run1.log, run2.log, run3.log
#
# Every line the program printed on the console becomes one entry {n, text, class, t, tol[, rtl, why]}:
#   functional  register values and summaries that do not depend on the CPU's speed: exact, every token
#   trace       the value columns of the probes' poll traces (fdr / fsr / sptr / lsr per event, the event counts):
#               exact when the events are far apart compared with the poll period (all of them here); the Icyc
#               columns of the same lines are timing tokens
#   timing      Icyc numbers (event times "lo-hi", costs, poll counts) and the lines whose values depend on the
#               CPU's poll phase (the S15 feed loop, the S17 D1 DMAC trace, the D2 TDFE poll): for v1_soc (WI-SOC)
#   diff        a documented difference between the RTL and the silicon (serial.md 9): `rtl` is the line the RTL
#               must print instead (null: the line is absent), `why` the reference
#   env         depends on what ran on the console before the program: `rtl` is the value for a fresh chip
#   cache       a console value that includes the instruction cache's cold first pass (the program ran once under KOS,
#               printing between tests): the v1 SoC compares it only with the caches (rtl/v1/SOC.md 8.3 M3, level
#               cache); M1b (IDEAL_MEM = 1) reports it.  S5's unrolled once-run blocks (SCBRR2 rd8 587 and SCFDR2 rd16
#               562 are the same Pclk read class, 16 Icyc back to back [HW p4b]: the 25 is not the bus's) and the S15
#               feed loop (its first poll iteration took 156 Icyc on the console, 109 on the SoC with its I-cache
#               (rm), 60 without (im): every later time and fed count follows).
# `t` lists the token indices (check.py's tokenizer) that are timing; `tol` their tolerance in Icyc (a "lo-hi" pair:
# the poll interval + 16; any other number: 10 % + 8).  Nothing here is taken from an RTL run: the classes follow
# from what each value depends on, the `rtl` strings from the differences serial.md 9 documents.
# Poll-phase rules (M1b, WI-SOC; the console shows each of them itself):
#   poll1  a trace block in which a row that lasts one poll (the next change is seen by the very next poll) records
#          a state the poll caught between two register reads or during a transient, which another poll phase misses:
#          such rows are optional on both sides and the block's "N events" count is compared without them.  S6a / S6b
#          (a received byte lands between the SCFDR2 and the SCFSR2 read: the console splits S6b, not S6a), S17 D1 (a
#          popped byte caught before the DMAC's refill: console, v1 SoC and its I-cache build dip at different frames),
#          S17 D2 (the DMAC's 3-byte DR burst caught in the middle)
#          A block's first row is its initial state and stays, except in S17 D1, whose first poll ran cold on the
#          console (0-64 Icyc against 0-33): time for the DMAC's first transfer.  A dropped row's start moves to the
#          next row (the change began in its window), whose tolerances widen by the dropped width.  The trace rows'
#          Icyc columns are compared only with check.py --trace-times: they carry the poll phase and the free-running
#          base-tick phase at each write (the console's S6e pop at 800 Icyc, its E1 pop of the same configuration at
#          755); the E1 / E2 / E4 lines check the SCIF's timing.
#   alt    D2 dynamic TX: the poll that first reads TDFE reads SCFDR2 before SCFSR2, so the count it prints is the one
#          before the pop that raised TDFE (the console's ttrg 4 / 2 / 1) or after it (ttrg 8): either is accepted
#   e1     E1: each repetition's frame (T->0 then R->1) must take the console's time (R->1 lo - T->0 lo within the
#          tolerance of a console repetition's), and start (T->0 lo; TEND lo on the TEND line) inside the range of the
#          console's repetitions, widened by the tolerance: the start waits for the free-running base-tick (bit / 16)
#          edge, whose phase at the write the program's history sets -- the console's own repetitions spread over up
#          to 420 Icyc at cks 3 / brr 63 -- and its first repetition of every configuration ran cold (e.g. 217 / 1329
#          against 195 / 1307).  The range is widened by a base tick when that is longer than the tolerance; TEND's
#          interval from T->0 is compared within a base tick too (the console's own spread at cks 3: 345 Icyc)
#   erange E2 / E4 (M3-fix, M3-verify F2; M1b.md 7): the same rule for the other lines whose start waits for the
#          free-running base tick (bit 512, a 32-Icyc tick): every repetition's times per series (E2: R->1 and TEND,
#          E4: the R push and the DR) inside the range of the console's repetitions widened by max(tolerance, tick), and
#          the second series' interval from the first within that of one of the console's.  On rm the chip's history
#          moves the tick phase: E2 8E1's first repetitions 5608 / 5611 against the console's 5635-5664
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
SEP = re.compile(r'[\s\[\]\(\):,|/+]+|(?<=\d)-(?=\s*\d)')


def tokens(line):
    return [x for x in SEP.split(line) if x]


def spans(line):
    # (start, end) of every token in the line, in tokenizer order
    out, pos = [], 0
    for tk in tokens(line):
        i = line.index(tk, pos)
        out.append((i, i + len(tk)))
        pos = i + len(tk)
    return out


def tol_for(line, idx):
    # a token of a "lo-hi" pair gets the pair's width + 16; any other number 10 % + 8
    sp = spans(line)
    tk = tokens(line)
    for a, b in ((idx, idx + 1), (idx - 1, idx)):
        if 0 <= a and b < len(tk) and re.fullmatch(r'-\s*', line[sp[a][1]:sp[b][0]] or 'x'):
            if tk[a].isdigit() and tk[b].isdigit():
                return int(tk[b]) - int(tk[a]) + 16
    v = int(tk[idx]) if tk[idx].isdigit() else 0
    return v // 10 + 8


REGS = ('fdr', 'fsr', 'sptr', 'lsr', 'scr', 'chcr')


def all_numbers(line, start=0):
    # the decimal numbers from token `start` on; a register value (the token after fdr / fsr / ...) is not a number
    tk = tokens(line)
    return [i for i, x in enumerate(tk) if i >= start and x.isdigit() and not (i and tk[i - 1] in REGS)]


def entry(n, text, cls, t=(), rtl=None, why=None, has_rtl=False):
    e = {'n': n, 'text': text, 'class': cls, 't': list(t), 'tol': [tol_for(text, i) for i in t]}
    if has_rtl or cls in ('diff', 'env'):
        e['rtl'] = rtl
    if why:
        e['why'] = why
    return e


MPB = 'SCI MPB: the silicon reads MPB = 1 after RE was 1 with no frame received; the RTL changes MPB only on a ' \
      'received multiprocessor frame (serial.md 3.3 SCSSR1, 9 Q5)'
TFRST = 'TFRST transient: the silicon reads T = 1 for ~500 Icyc after a write during TFRST; the RTL discards the ' \
        'write and keeps T = 0 (serial.md 3.2.6, 9 Q2)'
FLIP = 'a mid-frame SCSMR2 change: the silicon uses the format bits partly live; the RTL latches the format per ' \
       'frame (at S / D), so the frame is sent and received in the old format (serial.md 3.3.7, 9 Q3)'
GSKIP = 'follows the receive error only the silicon produced with the G1-G3 flips (serial.md 9 Q3); the RTL finds ' \
        'no error and prints "G: no receive error produced" instead'
STALE = 'the stale FIFO RAM word at the read pointer was left by the programs that ran before on the console (run 2 ' \
        'F2 received 0x55 at slot 0); the RTL RAM is 0 at time 0 and this program received nothing at slot 0 before'

TRACE_ROW = re.compile(r'^\s+\d+-\s*\d+ fdr ')


POLL1 = {'scif_probe': ('S6a', 'S6b', 'S17')}
CACHE_S5 = ('the unrolled blocks ran once, cold, under KOS: SCBRR2 rd8 587 and SCFDR2 rd16 562 are one Pclk read class '
            '(16 Icyc back to back, p4b [HW])')
CACHE_S15 = ('the feed loop\'s first poll iteration took 156 Icyc on the console (cold), 109 on the SoC with its I-cache, '
             '60 without: the later times and fed counts follow')


def d2_alt(s):
    # the D2 line with the other count: "with fdr X (previous fdr P)": X = P (read before the pop) or P - 0x100
    m = re.search(r'with fdr ([0-9a-f]{4}) \(previous fdr ([0-9a-f]{4})\)', s)
    x, p = int(m.group(1), 16), int(m.group(2), 16)
    y = p - 0x100 if x == p else p
    return s[:m.start(1)] + '%04x' % y + s[m.end(1):]


def classify(prog, lines):
    out = []
    sect = ''
    last_pair = ''
    for n, s in enumerate(lines, 1):
        top = not s.startswith(' ')
        if top:
            sect = s.split(' ')[0].rstrip(':')
        tk = tokens(s)
        e = None
        if prog == 'scif_probe':
            if re.match(r'S5 cost', s):
                e = entry(n, s, 'cache', all_numbers(s, 3), why=CACHE_S5)
            elif re.search(r': \d+ events, \d+ polls in \d+ cyc$', s):
                e = entry(n, s, 'trace', [tk.index('polls') - 1])
            elif sect in ('S6d',) and not top:
                e = entry(n, s, 'timing', all_numbers(s, 1))
            elif sect == 'S15' and not top:
                e = entry(n, s, 'cache', all_numbers(s), why=CACHE_S15)
            elif s.startswith('S15 fifo'):
                e = entry(n, s, 'cache', [], why='the dump runs while the transmitter still sends: T and R move with the '
                                                  'CPU speed; ' + CACHE_S15)
            elif sect == 'S17' and not top and 'tcr' in s and ' rx ' in s:
                # the lo-hi times only: with the poll1 rule the refill dips (one-poll rows) are optional and the
                # remaining rows' DMATCR1 and received counts are exact (M1b, WI-SOC)
                e = entry(n, s, 'timing', [0, 1])
            elif sect == 'S17' and not top:
                e = entry(n, s, 'trace', [0, 1])
            elif TRACE_ROW.match(s):
                e = entry(n, s, 'trace', [0, 1])
            elif re.match(r'^\s+\+\d+ INTEVT', s):
                e = entry(n, s, 'functional', [0])
        elif prog == 'scif_probe2':
            if s.startswith('A2 SCI w/r'):
                e = entry(n, s, 'diff', rtl=s.replace('RE=1: 86)', 'RE=1: 84)'), why=MPB)
            elif s.startswith('A2 SCI SSR1'):
                e = entry(n, s, 'diff', rtl=s.replace('SSR1 86 w00->86 w01->87 w00->86', 'SSR1 84 w00->84 w01->85 w00->84'),
                          why=MPB)
            elif s.startswith('A3 32 SCSSR1'):
                e = entry(n, s, 'timing', all_numbers(s, 3))
            elif s.startswith('C SCFTDR2 writes with TFRST=1'):
                e = entry(n, s, 'diff', rtl=s.replace('fdr 0100,', 'fdr 0000,'), why=TFRST)
            elif sect == 'D2' and not top:
                e = entry(n, s, 'timing', [4, 5],
                          why='which poll first sees TDFE, and the count it reads with it, depend on the poll phase')
                e['alt'] = [d2_alt(s)]
            elif sect in ('E1', 'E2', 'E4') and not top:
                # "cks 0 brr 0 bit 128: [lo-hi lo-hi] ...", "TEND: [lo-hi] ...", "7N1: [...] TEND [...]", "1 byte(s): [...]"
                e = entry(n, s, 'timing', all_numbers(s, 6 if tk[0] == 'cks' else 1))
                if sect in ('E2', 'E4'):
                    e['mode'] = 'erange'
                    e['erange'] = {'bit': 512, 'layout': 'blocks' if sect == 'E2' else 'inter'}
                if sect == 'E1':
                    e['mode'] = 'e1pair' if tk[0] == 'cks' else 'e1tend'
                    if tk[0] == 'cks':
                        last_pair = s
                    else:
                        e['pair'] = last_pair       # the configuration's T->0 / R->1 line (the TEND interval)
            elif s.startswith('E5'):
                e = entry(n, s, 'timing', all_numbers(s, 3))
            elif sect == 'E3' and not top:
                e = entry(n, s, 'trace', [i for i in range(len(tk)) if i % 3 != 2])
            elif sect == 'E6' and not top:
                e = entry(n, s, 'trace', [i for i in range(len(tk)) if i % 4 in (0, 1)])
            elif sect in ('F1', 'F2') and not top:
                if sect == 'F2' and tk[2:] != ['0062', '55', '0001']:
                    rtl = s[:s.index(')') + 1] + ' 0062 55 0001'
                    e = entry(n, s, 'diff', [1], rtl=rtl, why=FLIP)
                else:
                    e = entry(n, s, 'functional', [1])
        elif prog == 'scif_probe3':
            if s.startswith('I SCI SSR1'):
                e = entry(n, s, 'diff', rtl=s.replace(' 86', ' 84'), why=MPB)
            elif s.startswith('H1 TFRST=1'):
                e = entry(n, s, 'diff', rtl=s.replace('after writes 0100 0100 0100', 'after writes 0000 0000 0000'),
                          why=TFRST)
            elif s.startswith('H2 '):
                e = entry(n, s, 'env', rtl=s.replace('read 55', 'read 00'), why=STALE)
            elif sect in ('G1', 'G3') and not top and tk[1:4] != ['0062', '0001', '0000']:
                e = entry(n, s, 'diff', rtl=s[:s.index(tk[1])] + '0062 0001 0000 ' + tk[4], why=FLIP)
            elif s.startswith('G using'):
                e = entry(n, s, 'diff', rtl='G: no receive error produced', why=GSKIP)
            elif sect in ('G4', 'G5', 'G6'):
                e = entry(n, s, 'diff', rtl=None, why=GSKIP)
        if e is None:
            e = entry(n, s, 'functional')
        if sect in POLL1.get(prog, ()):
            e['poll1'] = True
        if prog == 'scif_probe' and s.startswith('S17 D1'):
            # the first poll after CHCR1's start ran cold on the console (0-64 Icyc against 0-33 on the SoC): time for
            # the DMAC's first transfer (tcr 23 against 24): the block's first row is a one-poll row like the others
            e['poll1_first'] = True
        out.append(e)
    return out


def program_lines(log, head):
    lines, on = [], False
    for s in open(log).read().splitlines():
        if s.startswith(head):
            on = True
        if on:
            lines.append(s.rstrip())
            if s.startswith(head.split(' ')[0] + ' done'):
                break
    return lines


def main():
    progs = [('scif_probe', 'run1.log', 'scif_probe run1'), ('scif_probe2', 'run2.log', 'scif_probe2 run2'),
             ('scif_probe3', 'run3.log', 'scif_probe3 run3')]
    for prog, log, head in progs:
        path = os.path.join(ROOT, 'tools', 'hw', 'serial', log)
        lines = program_lines(path, head)
        ent = classify(prog, lines)
        doc = {'program': prog, 'log': 'tools/hw/serial/' + log,
               'note': 'generated by tb/bm/serial/mkexpected.py from the console log; classes in README.md',
               'lines': ent}
        with open(os.path.join(HERE, 'expected', prog + '.json'), 'w') as f:
            json.dump(doc, f, indent=1)
        cnt = {}
        for e in ent:
            cnt[e['class']] = cnt.get(e['class'], 0) + 1
        print('%s: %d lines %s' % (prog, len(ent), ' '.join('%s %d' % kv for kv in sorted(cnt.items()))))
    return 0


if __name__ == '__main__':
    sys.exit(main())
