#!/usr/bin/env python3
"""check.py -- compare a run of a board probe (tools/hw/board/boardio.elf, boardregs.elf, unmodified) with the console's
values in expected.json (mkexpected.py; board.md 6).  Owner WI-BOARD.

    python3 tb/bm/board/check.py PROG RUN.log [--timing [--open929]] [--phase2] [-v]

PROG is boardio or boardregs; RUN.log holds the program's console text (its "B ..." lines).

What is compared, per field:
  * functional values: exactly (board register values, maple responses and addresses, GD status / ireason / byte
    counts, mirrors, read-backs, SPG status bits, the vblank IRQ count over thd_sleep(100));
  * board-determined times (the board model's own calibration, board.md 4.2 / 4.4 / 9.1): maple DMA end within
    0.5 %, SPG frame / line / vblank-in -> out periods;
  * CPU-visible bus timing (access latencies, SQ / P1 / OCBWB costs, KOS init ms, the FFST sample times, and the GD
    step readings, which boardio takes through its own poll loop): only with --timing, i.e. on the RTL SoC (WI-SOC,
    M4 / M5), since the stand-alone model's timing is not a reference (SOC.md 6.2).  GD readings must fall in the
    console's own poll: DRQ within 40 (a poll is 114), BSY 66 (half a poll of 133), INTRQ / completion 60, 120 for
    REQ_STAT; req_stat and req_stat2 are the same command twice, so each of their readings is compared with the
    nearer of the console's two (its INTRQ read 2489 / 2645, its completion 3747 / 3871: the drive's own spread; M5-fix);
    the board's GD delays themselves are checked by dc_board_test "gdloop", which replays the loop with the console's
    bus costs.  The SQ rows' issue + drain + fifo (t0 to the G2 FIFO empty: the whole transfer) is compared too;
  * --open929 (with --timing; the M5 gate, WI-SOC M5-fix, M5-verify F1): SOC.md 9.29's rows, open, get a BOUNDED band
    instead of their tolerance -- a value in the band is reported "OPEN 9.29", not failed; outside it the row FAILs:
      - every GD step reading: console - tol .. console + tol + GD929 (80 Icyc: the chip's t0 lands with one G1 write
        on the bus and one in the WTB, so its first status poll starts 74 Icyc after t0, the console's ~28 -- a reading
        may carry that whole posting delay on top of its poll quantization; agents/scratch/M5/gdloop);
      - SQ -> VRAM32 / AICA RAM drain and fifo: their sum within the console's sum +- (max(40, 5 %) + one burst period,
        console issue / 64): the split point t1 falls anywhere in a burst of the throttled stream (the chip's AICA
        split read 14 / 3360 and 434 / 3376 in two runs with one total); issue and issue + drain + fifo stay gated;
  * skipped, with the reason printed: values that differ by construction -- the console had a CD-R in the drive (the
    board's default is the no-disc drive), phase-1 Holly DMA does not use DMAC channel 0 (SAR0 / DAR0 / CHCR0.TE: board
    phase 2, M6, --phase2), SPG-phase-dependent status bits and absolute event times, dcload's start state and list
    address, the junk the console left after a maple response (the model pads 0, board.md 3.6), the upper half of 32-bit
    AICA register reads (board.md 10.4), and chip registers (their own units' suites).
Exit 0 when nothing failed and no expected line is missing.
"""
import argparse
import json
import os
import re
import sys

HEX = re.compile(r'^[0-9a-fA-F]+$')


def _pairs(toks):
    """name value name value ... -> dict (values kept as strings)"""
    d = {}
    i = 0
    while i + 1 < len(toks):
        d[toks[i]] = toks[i + 1]
        i += 2
    return d


def parse_line(s):
    """one 'B ...' line (without the 'B ') -> (key, fields) or None"""
    t = s.replace('|', ' ').split()
    if not t:
        return None
    k = t[0]
    m = re.match(r'vblank irqs in thd_sleep\(100\): (\d+), (\d+) cycles, now (\d+) ms', s)
    if m:
        return 'vblank', {'irqs': m.group(1), 'cycles': m.group(2), 'now_ms': m.group(3)}
    if k == 'init_ms':
        return 'init_ms', {'ms': t[1]}
    if k == 'maple' and len(t) > 2 and t[1] == 'start':
        return 'maple start', _pairs(t[2:])
    if k == 'maple' and len(t) > 3 and re.match(r'rx\d+$', t[2]):
        return 'maple %s %s' % (t[1], t[2]), {'w': t[3:]}
    if k == 'maple' and len(t) > 2:
        return 'maple ' + t[1], _pairs(t[2:])
    if k == 'gd' and len(t) > 2 and t[1] == 'start':
        return 'gd start', _pairs(t[2:])
    if k == 'gd':
        m = re.search(r'data\((\d+)\)(.*)$', s)
        head = s[:m.start()] if m else s
        d = _pairs(head.replace('|', ' ').split()[2:])
        d['data'] = m.group(2).split() if m else []
        return 'gd ' + t[1], d
    if k in ('hbl', 'vbl') and len(t) > 2 and t[1] == 'events':
        return k + ' events', {'n': t[2]}
    if k == 'hbl':
        return 'hbl ' + t[1], {'t': t[2], 'st': t[3]}
    if k == 'vbl':
        return 'vbl ' + t[1], {'t': t[2], 'bit': t[3], 'st': t[4]}
    if k == 'line':
        return 'line ' + t[1], {'t': t[2], 'st': t[3]}
    if k == 'reg':
        return 'reg ' + t[1], {'addr': t[2], 'pre': t[4], 'post': t[6]}
    if k == 'start':
        return 'start', _pairs(t[3:])
    if k == 'cdrom_get_status':
        return 'cdrom_get_status', _pairs(t[1:])
    if k == 'ffst':
        m = re.search(r'\((\d+) cycles to issue\): (.*)$', s)
        return 'ffst', {'issue': m.group(1), 'samples': m.group(2).split()}
    if k == 'lat':
        m = re.match(r'lat (.*?)\s+(\d+) cycles / 256 = ([0-9.]+)$', s)
        if m:
            return 'lat ' + m.group(1).strip(), {'cycles': m.group(2), 'per': m.group(3)}
        m = re.match(r'lat P1 line miss \(256 lines\) (\d+) cycles, re-read hits (\d+) cycles', s)
        if m:
            return 'lat P1 line miss', {'cycles': m.group(1), 'hits': m.group(2)}
        m = re.match(r'lat OCBWB dirty line \(256 lines\) (\d+) cycles', s)
        if m:
            return 'lat OCBWB dirty line', {'cycles': m.group(1)}
        m = re.match(r'lat (SQ->\S+( \S+)?)\s+64 bursts issue (\d+) cycles \(\+drain (\d+), \+fifo (\d+)\)', s)
        if m:
            return 'lat ' + m.group(1), {'issue': m.group(3), 'drain': m.group(4), 'fifo': m.group(5)}
        return 'lat ' + s[4:], {}
    if k in ('aica', 'sq') and 'readback' in t:
        i = t.index('readback')
        rest = t[i + 1:]
        return ' '.join(t[:i + 1]), ({'w': rest} if rest and HEX.match(rest[0]) else _pairs(rest))
    if k == 'mirror':
        return 'mirror ' + t[1], _pairs(t[2:])
    if k == 'done':
        return 'done', {}
    return 'other ' + s, {}


def parse_log(text):
    """every 'B ...' line of a console / run log -> {key: fields} (the first occurrence of a key wins)"""
    out = {}
    for line in text.splitlines():
        line = line.rstrip('\r')
        if not line.startswith('B '):
            continue
        r = parse_line(line[2:].strip())
        if r and r[0] not in out:
            out[r[0]] = r[1]
    return out


def h(x):
    return int(x, 16)


GD929 = 80          # --open929: the GD readings' band above the tolerance (the docstring; SOC.md 9.29 (a))
SQ929 = ('lat SQ->VRAM32', 'lat SQ->AICA RAM')       # --open929: the rows of SOC.md 9.29 (b)


class Checker:
    def __init__(self, timing, phase2, verbose, open929=False):
        self.timing, self.phase2, self.verbose, self.open929 = timing, phase2, verbose, open929
        self.n = self.bad = 0
        self.skips = {}
        self.fails = []
        self.opens = []

    def skip(self, key, why):
        self.skips.setdefault(why, []).append(key)

    def ok(self, cond, key, what):
        self.n += 1
        if not cond:
            self.bad += 1
            self.fails.append('%s: %s' % (key, what))
        elif self.verbose:
            print('  ok   %s: %s' % (key, what))

    def eq(self, key, f, e, r, base=16, mask=None):
        a, b = int(e, base), int(r, base)
        if mask is not None:
            a, b = a & mask, b & mask
        self.ok(a == b, key, '%s = %s, console %s%s' % (f, r, e, '' if mask is None else ' (mask %X)' % mask))

    def near(self, key, f, want, got, tol, timing=False, band=None, why=None):
        """band: (lo, hi) of SOC.md 9.29's open rows (--open929): a value outside the tolerance but inside it is
        reported OPEN, not failed (why: the band's reason, printed instead of it)"""
        if timing and not self.timing:
            self.skip(key + ' ' + f, 'CPU-visible timing: checked on the RTL SoC (--timing)')
            return
        what = '%s = %s, console %s (+- %s)' % (f, got, want, round(tol, 1))
        if abs(got - want) > tol and band is not None and self.open929 and band[0] <= got <= band[1]:
            self.n += 1
            self.opens.append('%s: %s; %s' % (key, what, why or 'in the band %s-%s' % band))
            return
        self.ok(abs(got - want) <= tol, key, what + ('' if band is None or not self.open929 else
                                                     '; outside the band %s-%s' % band))


DISC = 'the console had a CD-R in the drive; the board default is the no-disc drive (board.md 3.7.3, 10.1)'
PH1 = 'phase-1 Holly DMA does not use DMAC channel 0 (board phase 2, M6: --phase2)'
PHASE = 'SPG phase: event bits / absolute event times depend on when the program started'
SPG_BITS = 0x2038            # ISTNRM vblank-in / out, hblank-in, maple vblank-over


def gd_tol(key, f):
    """GD step reading tolerance on the RTL SoC (--timing): less than one poll of the loop that saw the event"""
    if f == 'drq':
        return 40            # alternate-status poll 114: a one-poll error is caught
    if f == 'bsy':
        return 66            # half a BSY-loop poll (ISTEXT + alternate status, 133)
    return 120 if 'req_stat' in key else 60   # INTRQ: 26-Icyc ISTEXT polls; REQ_STAT varied 2489 / 2645 on the console


def check_boardio(c, exp, run):
    for key, e in exp.items():
        r = run.get(key)
        if r is None:
            if key.startswith('hbl ') and key != 'hbl events':
                c.skip(key, PHASE)
            else:
                c.ok(False, key, 'line missing from the run')
            continue
        if key == 'init_ms':
            c.near(key, 'ms', int(e['ms']), int(r['ms']), 0.05 * int(e['ms']), timing=True)
        elif key == 'vblank':
            c.ok(6 <= int(r['irqs']) <= 7, key, 'irqs = %s, console %s (6-7, SOC.md M5)' % (r['irqs'], e['irqs']))
            c.skip(key + ' cycles', 'chip: PMCTR while the CPU sleeps (debug-ubc-hudi)')
            c.near(key, 'now_ms', int(e['now_ms']), int(r['now_ms']), 0.05 * int(e['now_ms']), timing=True)
        elif key == 'maple start':
            for f in ('MDEN', 'MSYS', 'MDTSEL', 'MST'):
                c.eq(key, f, e[f], r[f])
            c.skip(key + ' MDSTAR', "dcload's maple command list address (board.md 5.3 presets the BIOS one)")
        elif key.startswith('maple ') and ' rx' in key:
            ew, rw = e['w'], r['w']
            n = 8 if ew[0].lower() == 'ffffffff' else 1 + (h(ew[0]) >> 24)
            for i in range(min(n, 8)):
                c.eq(key, 'w%d' % i, ew[i], rw[i] if i < len(rw) else '0')
            if n < 8:
                c.skip(key + ' tail', 'the console left the previous response after the frame; the model pads 0 (board.md 3.6)')
        elif key.startswith('maple '):
            for f in ('mdst0', 'int'):
                c.near(key, f, int(e[f]), int(r[f]), 0.005 * int(e[f]))
            c.eq(key, 'istnrm', e['istnrm'], r['istnrm'], mask=~SPG_BITS & 0xFFFFFFFF)
            for f in ('isterr', 'MTXDAD', 'MRXDAD'):
                c.eq(key, f, e[f], r[f])
            for f in ('SAR0', 'DAR0', 'CHCR0', 'DMATCR0'):
                if c.phase2:
                    c.eq(key, f, e[f], r[f])
                else:
                    c.skip(key + ' ' + f, PH1)
        elif key == 'gd start':
            for f in ('alt', 'istext', 'gdst'):
                c.eq(key, f, e[f], r[f])
            c.skip(key + ' secnum', DISC)
        elif key.startswith('gd '):
            c.eq(key, 'ok', e['ok'], r['ok'], 10)
            # Each GD step number is an event seen by one poll of boardio's own loop (t0 before the posted command
            # write, 106-Icyc alternate-status / 18-Icyc ISTEXT polls), so it depends on the CPU's bus costs as much as
            # on the drive: CPU-visible timing.  With the console's costs (--timing: the RTL SoC) the board's delays
            # put every reading into the console's poll (dc_board_test "gdloop" replays the loop); the stand-alone
            # model's GD reads cost ~89 (no MPX baseline), so its readings are not compared.
            for f in ('drq', 'pkt', 'bsy', 'int', 'int2'):
                tol = 200 if f == 'pkt' else gd_tol(key, f)
                g = int(r[f])
                if key in ('gd req_stat', 'gd req_stat2'):    # the same command twice: the nearer console run
                    w = min((int(exp[k][f]) for k in ('gd req_stat', 'gd req_stat2')), key=lambda v: abs(v - g))
                else:
                    w = int(e[f])
                c.near(key, f, w, g, tol, timing=True, band=(w - tol, w + tol + GD929))
            chk = 0xFE if key == 'gd test_unit' else 0xFF      # CHECK (bit 0): no medium
            for f in ('st', 'alt'):
                c.eq(key, f, e[f], r[f], mask=chk)
            for f in ('ir', 'st2', 'ir2', 'istext'):
                c.eq(key, f, e[f], r[f])
            c.eq(key, 'bc', e['bc'], r['bc'], 10)
            c.eq(key, 'istnrm', e['istnrm'], r['istnrm'], mask=~SPG_BITS & 0xFFFFFFFF)
            for f in ('err', 'secnum', 'data'):
                c.skip(key + ' ' + f, DISC)
        elif key == 'hbl events':
            c.ok(2 <= int(r['n']) <= 3, key, 'n = %s, console %s (2-3 in the 40 ms window)' % (r['n'], e['n']))
        elif key.startswith('hbl '):
            c.eq(key, 'st', e['st'], r['st'])
            c.skip(key + ' t', PHASE)
        elif key == 'done':
            c.ok(True, key, 'present')
        else:
            c.skip(key, 'not compared')
    if 'hbl 0' in run and 'hbl 1' in run:
        c.near('hbl', 'period', int(exp['hbl 1']['t']) - int(exp['hbl 0']['t']),
               int(run['hbl 1']['t']) - int(run['hbl 0']['t']), 60)


BOARD_REG_SKIP = {
    'SB_ISTNRM:pre': 'the BIOS left vblank / hblank events latched (0x38); the direct boot presets 0 (board.md 5.3)',
    'SB_MRXDAD:post': "maple scan phase at the dump (the console's last frame had a response)",
    'SPG_STATUS:pre': 'time of the read', 'SPG_STATUS:post': 'time of the read',
}
AICA_LOW16 = ('AICA_2808', 'AICA_28B8', 'AICA_RTC_LO', 'AICA_RTC_HI')


def check_boardregs(c, exp, run):
    for key, e in exp.items():
        r = run.get(key)
        kind = key.split(' ')[0]
        if r is None:
            if kind in ('vbl', 'hbl', 'line') and key not in ('vbl events', 'hbl events'):
                c.skip(key, PHASE)
            else:
                c.ok(False, key, 'line missing from the run')
            continue
        if kind == 'reg':
            name = key[4:]
            if h(e['addr']) >= 0xE0000000:
                c.skip(key, 'chip register: checked by its own unit suite, not the board')
                continue
            for f in ('pre', 'post'):
                why = BOARD_REG_SKIP.get(name + ':' + f)
                if why:
                    c.skip(key + ' ' + f, why)
                elif name.startswith('GD_'):
                    c.skip(key + ' ' + f, DISC)
                elif name == 'AICA_RTC_LO' and f == 'post':
                    # (M5, WI-SOC) the seconds counter one run later: the console crossed one second boundary between
                    # its pre and post dumps, at the phase its RTC had when the program started; the board seeds the
                    # counter at a second's start at time 0 (board.md 3.8.2), so a run shorter than a second may or may
                    # not cross one: the console's value or one less (the chip's 191M-Icyc boardregs reads 0C2D; the
                    # stand-alone model, longer in board time, 0C2E).  pre stays exact
                    a, b = int(e[f], 16) & 0xFFFF, int(r[f], 16) & 0xFFFF
                    c.ok(b in (a, (a - 1) & 0xFFFF), key, '%s = %s, console %s (mask FFFF; or one less: the RTC phase '
                         'at the program start)' % (f, r[f], e[f]))
                    c.skip(key + ' upper half', 'the upper half of a 32-bit AICA read (board.md 10.4, open)')
                elif name in AICA_LOW16:
                    c.eq(key, f, e[f], r[f], mask=0xFFFF)
                    if f == 'post':
                        c.skip(key + ' upper half', 'the upper half of a 32-bit AICA read (board.md 10.4, open)')
                else:
                    c.eq(key, f, e[f], r[f])
        elif key == 'start':
            c.skip(key, "the start state: dcload's on the console, the testbench column here (board.md 5.2)")
        elif key == 'cdrom_get_status':
            c.eq(key, 'rv', e['rv'], r['rv'], 10)
            c.skip(key + ' status', DISC)
        elif key in ('vbl events', 'hbl events'):
            c.ok(abs(int(r['n']) - int(e['n'])) <= 1, key, 'n = %s, console %s (+- 1: window phase)' % (r['n'], e['n']))
        elif kind == 'vbl':
            c.eq(key, 'bit', e['bit'], r['bit'])
            c.eq(key, 'st', e['st'], r['st'])
            c.skip(key + ' t', PHASE)
        elif kind in ('hbl', 'line'):
            if kind == 'line':
                c.eq(key, 'st bits 13:10', e['st'], r['st'], mask=0x3C00)
            else:
                c.eq(key, 'st', e['st'], r['st'])
            c.skip(key + ' t / line', PHASE)
        elif kind == 'lat':
            split = None
            if key in SQ929 and c.timing:              # SOC.md 9.29 (b): the drain / fifo split, bounded by its sum
                ws, gs = int(e['drain']) + int(e['fifo']), int(r['drain']) + int(r['fifo'])
                w = round(max(40, 0.05 * ws) + int(e['issue']) / 64.0)
                c.near(key, 'drain + fifo', ws, gs, w, timing=True)
                split = ws - w <= gs <= ws + w
            for f, v in e.items():
                if f == 'per':
                    c.near(key, f, float(v), float(r[f]), max(2.0, 0.03 * float(v)), timing=True)
                elif f in ('drain', 'fifo') and split:   # open: only the sum (above) bounds the split
                    c.near(key, f, int(v), int(r[f]), max(40, 0.05 * int(v)), timing=True,
                           band=(int(r[f]), int(r[f])), why='the split; drain + fifo = %d is within %d +- %d' % (gs, ws, w))
                elif f in ('issue', 'cycles', 'hits', 'drain', 'fifo'):
                    c.near(key, f, int(v), int(r[f]), max(40, 0.05 * int(v)), timing=True)
            if 'issue' in e and 'drain' in e:               # the whole transfer: t0 to the queues / FIFO empty
                wt = sum(int(e[f]) for f in ('issue', 'drain', 'fifo'))
                gt = sum(int(r[f]) for f in ('issue', 'drain', 'fifo'))
                c.near(key, 'issue + drain + fifo', wt, gt, max(40, 0.05 * wt), timing=True)
        elif key == 'ffst':
            ev = [x.split(':')[1] for x in e['samples']]
            rv = [x.split(':')[1] for x in r['samples']]
            c.ok(ev == rv, key, 'FFST sequence %s, console %s' % (' '.join(rv), ' '.join(ev)))
            c.near(key, 'issue', int(e['issue']), int(r['issue']), 20, timing=True)
            for a, b in zip(e['samples'], r['samples']):
                c.near(key, 'sample time', int(a.split(':')[0]), int(b.split(':')[0]), 40, timing=True)
        elif kind in ('aica', 'sq', 'mirror'):
            for f, v in e.items():
                if isinstance(v, list):
                    for i, x in enumerate(v):
                        c.eq(key, '%s%d' % (f, i), x, r[f][i] if i < len(r[f]) else '0')
                else:
                    c.eq(key, f, v, r.get(f, '0'))
        elif key == 'done':
            c.ok(True, key, 'present')
        else:
            c.skip(key, 'not compared')
    # board-determined SPG periods (board.md 4.2): same-bit vblank events one frame apart, vblank-in -> out 26 lines,
    # consecutive line samples
    def vb(d):
        return sorted((int(v['t']), v['bit']) for k, v in d.items() if re.match(r'vbl \d+$', k))
    for bit, want in (('08', 3328311), ('10', 3328311)):
        ts = [t for t, b in vb(run) if b == bit]
        for a, b in zip(ts, ts[1:]):
            c.near('vbl', 'period (bit %s)' % bit, want, b - a, 60)
    ev = vb(run)
    for (t0, b0), (t1, b1) in zip(ev, ev[1:]):
        if b0 == '08' and b1 == '10':
            c.near('vbl', 'vblank-in -> out', 164804, t1 - t0, 60)
    ln = sorted((int(k.split()[1]), int(v['t']), h(v['st']) & 0x3FF) for k, v in run.items() if re.match(r'line \d+$', k))
    dts = [t1 - t0 for (_, t0, l0), (_, t1, l1) in zip(ln, ln[1:]) if l1 == l0 + 1]
    if dts:
        c.near('line', 'mean line period over %d steps' % len(dts), 6339.65, sum(dts) / len(dts), 2.0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('prog', choices=('boardio', 'boardregs'))
    ap.add_argument('log')
    ap.add_argument('--expected', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'expected.json'))
    ap.add_argument('--timing', action='store_true', help='also check CPU-visible timing (the RTL SoC)')
    ap.add_argument('--open929', action='store_true', help="with --timing: SOC.md 9.29's rows in their bounded band "
                    'are reported OPEN, not failed (the M5 gate)')
    ap.add_argument('--phase2', action='store_true', help='also check DMAC channel 0 after Holly DMAs (board phase 2)')
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    with open(a.expected) as f:
        exp = json.load(f)[a.prog]
    with open(a.log, errors='replace') as f:
        run = parse_log(f.read())
    c = Checker(a.timing, a.phase2, a.v, a.open929)
    (check_boardio if a.prog == 'boardio' else check_boardregs)(c, exp, run)
    for x in c.fails:
        print('  FAIL ' + x)
    for x in c.opens:
        print('  OPEN 9.29 ' + x)
    nskip = sum(len(v) for v in c.skips.values())
    if a.v:
        for why, keys in sorted(c.skips.items()):
            print('  skip %3d  %s' % (len(keys), why))
    print('bm/board %s: %d checks, %d failed, %d fields skipped by rule%s%s' %
          (a.prog, c.n, c.bad, nskip, '' if a.timing else ' (functional; --timing on the RTL SoC)',
           ', %d open in their SOC.md 9.29 band' % len(c.opens) if a.open929 else ''))
    return 1 if c.bad else 0


if __name__ == '__main__':
    sys.exit(main())
