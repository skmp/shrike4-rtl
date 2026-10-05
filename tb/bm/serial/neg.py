#!/usr/bin/env python3
# neg.py -- negative test of check.py's poll-phase rules (tb/bm/serial; M1b, WI-SOC): the console's own output (with
# the documented RTL differences, the lines check.py expects) must pass at every gating class including timing; each
# planted error below must fail it, and each legal phase variation must pass.
#
#   neg.py            exit 0 when every case behaves
import json, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CLS = 'functional,trace,diff,env,timing'


def base(prog):
    doc = json.load(open(os.path.join(HERE, 'expected', prog + '.json')))
    out = []
    for e in doc['lines']:
        if e['class'] in ('diff', 'env'):
            if e['rtl'] is not None:
                out.append(e['rtl'])
        else:
            out.append(e['text'])
    return out


def run(prog, lines):
    with tempfile.NamedTemporaryFile('w', suffix='.log', delete=False) as f:
        f.write('\n'.join(lines) + '\n')
        name = f.name
    r = subprocess.run([sys.executable, os.path.join(HERE, 'check.py'), name,
                        os.path.join(HERE, 'expected', prog + '.json'), '--classes', CLS, '--quiet'],
                       capture_output=True, text=True)
    os.unlink(name)
    return r.returncode == 0


def idx(lines, pred, nth=0):
    for i, l in enumerate(lines):
        if pred(l):
            if nth == 0:
                return i
            nth -= 1
    raise SystemExit('neg.py: no such line')


def row(lo, hi, rest):
    return '  %6d-%6d %s' % (lo, hi, rest)


def cases():
    """(what, program, transform of the base lines, must pass)"""
    out = []
    # S6a: a one-poll split row (fdr before the byte, fsr after it) is a phase observation: legal; the same row held
    # for two polls is not; a mandatory row's value changed is not
    def s6a_split(ls, polls):
        i = idx(ls, lambda l: l.startswith('S6a 8N1'))
        r2 = i + 3                                              # the third row: fdr 0001 fsr 0062
        m = re.match(r'\s+(\d+)-\s*(\d+) fdr (\w+) fsr (\w+) (.*)', ls[r2])
        lo, hi = int(m.group(1)), int(m.group(2))
        prev = re.match(r'\s+\d+-\s*\d+ fdr (\w+) fsr (\w+) (.*)', ls[r2 - 1])
        split_hi = lo + 40                                      # the split row's poll
        new = row(lo, split_hi, 'fdr %s fsr %s %s' % (prev.group(1), m.group(4), m.group(5)))
        nxt_lo = split_hi if polls == 1 else split_hi + 40      # two polls: the next row does not follow at once
        ls = ls[:r2] + [new, row(nxt_lo, hi + 40, 'fdr %s fsr %s %s' % (m.group(3), m.group(4), m.group(5)))] + ls[r2 + 1:]
        ls[i] = re.sub(r': (\d+) events', lambda x: ': %d events' % (int(x.group(1)) + 1), ls[i])
        return ls
    out.append(('S6a: a one-poll split row (fdr before the byte, fsr after it)', 'scif_probe', lambda ls: s6a_split(ls, 1), True))
    out.append(('S6a: the same state held for two polls', 'scif_probe', lambda ls: s6a_split(ls, 2), False))

    def s6a_value(ls):
        i = idx(ls, lambda l: l.startswith('S6a 8N1'))
        ls = list(ls)
        ls[i + 3] = ls[i + 3].replace('fsr 0062', 'fsr 0066')
        return ls
    out.append(('S6a: a mandatory row\'s fsr changed', 'scif_probe', s6a_value, False))

    def s6a_count(ls):
        i = idx(ls, lambda l: l.startswith('S6a 8N1'))
        ls = list(ls)
        ls[i] = re.sub(r': (\d+) events', lambda x: ': %d events' % (int(x.group(1)) + 1), ls[i])
        return ls
    out.append(('S6a: the event count one higher with no split row', 'scif_probe', s6a_count, False))

    def s6c_split(ls):                                          # S6c is not a poll1 block: a split there fails
        i = idx(ls, lambda l: l.startswith('S6c 7E1'))
        m = re.match(r'\s+(\d+)-\s*(\d+) (.*)', ls[i + 2])
        lo = int(m.group(1))
        ls = ls[:i + 2] + [row(lo, lo + 40, m.group(3).replace('fdr 0000', 'fdr 0100', 1)),
                           row(lo + 40, int(m.group(2)) + 40, m.group(3))] + ls[i + 3:]
        ls[i] = re.sub(r': (\d+) events', lambda x: ': %d events' % (int(x.group(1)) + 1), ls[i])
        return ls
    out.append(('S6c: an extra one-poll row outside the poll1 blocks', 'scif_probe', s6c_split, False))

    # S17 D1: a missing refill dip is legal (one-poll row)
    def d1_nodip(ls):
        i = idx(ls, lambda l: re.match(r'\s+\d+-\s*\d+ fdr 0700 tcr 12 rx 3', l) is not None)
        return ls[:i] + ls[i + 1:]
    out.append(('S17 D1: a refill dip not seen', 'scif_probe', d1_nodip, True))

    def d1_tcr(ls):
        i = idx(ls, lambda l: re.match(r'\s+\d+-\s*\d+ fdr 0800 tcr 13 rx 2', l) is not None)
        ls = list(ls)
        ls[i] = ls[i].replace('rx 2', 'rx 3')
        return ls
    out.append(('S17 D1: a frame counted twice (rx wrong)', 'scif_probe', d1_tcr, False))

    # D2 ttrg: the count read with TDFE may be the one before the pop or after it, nothing else
    def ttrg(ls, v):
        i = idx(ls, lambda l: l.startswith('  ttrg 4: TDFE'))
        ls = list(ls)
        ls[i] = re.sub(r'with fdr \w+', 'with fdr ' + v, ls[i])
        return ls
    out.append(('D2 ttrg 4: the count after the pop (030c)', 'scif_probe2', lambda ls: ttrg(ls, '030c'), True))
    out.append(('D2 ttrg 4: a count two pops away (020c)', 'scif_probe2', lambda ls: ttrg(ls, '020c'), False))

    # E1: the frame interval and the start range
    def e1(ls, cfg, rep, dstart, dint):
        i = idx(ls, lambda l: l.startswith('  cks 0 brr %2d' % cfg if cfg >= 0 else '  cks 3 brr  0'))
        ls = list(ls)
        pairs = re.findall(r'\[(\d+)-(\d+) (\d+)-(\d+)\]', ls[i])
        a, b, c, d = map(int, pairs[rep])
        new = '[%d-%d %d-%d]' % (a + dstart, b + dstart, c + dstart + dint, d + dstart + dint)
        old = '[%d-%d %d-%d]' % (a, b, c, d)
        k = ls[i].index(old, [m.start() for m in re.finditer(re.escape(old), ls[i])][0])
        ls[i] = ls[i][:k] + new + ls[i][k + len(old):]
        return ls
    out.append(('E1 brr 0: a repetition one poll later throughout (the console\'s own spread)', 'scif_probe2',
                lambda ls: e1(ls, 0, 2, 24, 0), True))
    out.append(('E1 brr 0: the frame 48 Icyc longer', 'scif_probe2', lambda ls: e1(ls, 0, 2, 0, 48), False))
    out.append(('E1 brr 3: the start 120 Icyc late', 'scif_probe2', lambda ls: e1(ls, 3, 1, 120, 0), False))
    out.append(('E1 cks 3: the start 300 Icyc early (a base tick is 512)', 'scif_probe2', lambda ls: e1(ls, -1, 1, -300, 0),
                True))
    out.append(('E1 cks 3: the frame one base tick (512 Icyc) short', 'scif_probe2', lambda ls: e1(ls, -1, 1, 0, -512),
                False))

    def tend(ls, dv):
        i = idx(ls, lambda l: l.startswith('  cks 0 brr  1'))
        ls = list(ls)
        t = i + 1
        m = re.search(r'\[(\d+)-(\d+)\]', ls[t])
        a, b = int(m.group(1)), int(m.group(2))
        ls[t] = ls[t][:m.start()] + '[%d-%d]' % (a + dv, b + dv) + ls[t][m.end():]
        return ls
    out.append(('E1 brr 1 TEND: 20 Icyc later (within a poll)', 'scif_probe2', lambda ls: tend(ls, 20), True))
    out.append(('E1 brr 1 TEND: 80 Icyc later', 'scif_probe2', lambda ls: tend(ls, 80), False))

    # E2 / E4 (erange, M3-fix): every repetition inside the console's range widened by max(tol, a base tick); the chip's
    # rm run (M3-verify F2: 8E1's first two repetitions 5608 / 5611, TEND 5852 / 5831) passes, a frame format error fails
    def e2(ls, fmt, new):
        i = idx(ls, lambda l: l.startswith('  %s: [' % fmt))
        ls = list(ls)
        ls[i] = new
        return ls
    out.append(('E2 8E1: the chip\'s rm repetitions (the tick phase its history sets)', 'scif_probe2',
                lambda ls: e2(ls, '8E1', '  8E1: [5608-5632] [5611-5635] [5639-5663] [5635-5659] TEND [5852-5876] [5831-5855] '
                                     '[5855-5879] [5859-5883]'), True))
    out.append(('E2 8E1: one repetition 60 Icyc before the console\'s range', 'scif_probe2',
                lambda ls: e2(ls, '8E1', '  8E1: [5575-5599] [5635-5659] [5639-5663] [5635-5659] TEND [5885-5909] [5855-5879] '
                                     '[5852-5876] [5855-5879]'), False))
    out.append(('E2 8E1: the parity bit missing (one bit, 512 Icyc, short)', 'scif_probe2',
                lambda ls: e2(ls, '8E1', '  8E1: [5152-5176] [5123-5147] [5127-5151] [5123-5147] TEND [5373-5397] [5343-5367] '
                                     '[5340-5364] [5343-5367]'), False))
    out.append(('E2 8E1: TEND 60 Icyc after its R->1 more than the console\'s', 'scif_probe2',
                lambda ls: e2(ls, '8E1', '  8E1: [5664-5688] [5635-5659] [5639-5663] [5635-5659] TEND [5885-5909] [5855-5879] '
                                     '[5852-5876] [5915-5939]'), False))
    out.append(('E4 2 bytes: the DR 70 Icyc late in one repetition', 'scif_probe2',
                lambda ls: e2(ls, '2 byte(s)', '  2 byte(s): [10253-10297 17941-17981] [10256-10300 18011-18051] '
                                           '[10256-10300 17904-17944]'), False))
    return out


def main():
    bad = 0
    for prog in ('scif_probe', 'scif_probe2'):
        ok = run(prog, base(prog))
        print('%-8s %s: the console\'s own output passes' % ('ok' if ok else 'WRONG', prog))
        bad += not ok
    for what, prog, fn, must in cases():
        got = run(prog, fn(base(prog)))
        ok = got == must
        bad += not ok
        print('%-8s %s: %s (%s)' % ('ok' if ok else 'WRONG', prog, what, 'accepted' if got else 'rejected'))
    print('serial neg.py: ' + ('PASS' if not bad else 'FAIL (%d)' % bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
