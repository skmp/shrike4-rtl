#!/usr/bin/env python3
"""v1_fetchx_counts.py -- WI-CORE-B: the fetch-dependent console numbers of the debug-ubc-hudi probes, checked on a
bare-metal run of the core (make v1-fetchx: build/obj_v1_exc/tb --bm build/bm/debug-ubc-hudi/<prog>.bin).

  v1_fetchx_counts.py <ubcbrk run log> <ubcevt run log>

Compared exactly against tb/bm/debug-ubc-hudi/expected/{ubcbrk,ubcevt}.json (the console's lines):
  * ubcbrk X rows (14 branch snippets, 64 iterations): events 0x06 0x0A (fetch words), 0x10 0x11 0x13 0x14 0x27 (the
    D stage, with the BT / BF skip form) and 0x23 (cycles), every column;
  * ubcbrk Y rows (10 snippets, 32 iterations): 0x06 0x0A 0x13 0x14 0x23 in the columns whose timing is the core's and
    the fetch's -- nop, ld, st (cached), ldCCN (an Iclk P4 read: its MA hold), br, fp; plus ocm's 0x06 / 0x0A (its
    misses are on the data side).  ldP2, ldTMU, icm and ocm's cycles need the memory system (M3);
  * ubcevt E rows (12 snippets, 64 iterations): 0x06 0x10 0x11 0x13 0x14 0x27 0x23, every column (btt / bfshort: the
    skip form; stP4: an Iclk P4 write's MA hold);
  * the UBC settle rows: ubcbrk T (NOP fillers) and ubcevt S (ADD fillers, operand break) -- the fetch-side copy delay
    of v1_ubc (IF_DLY = 3) against this fetch's run-ahead (SOC.md 9.5).
Exit 0 when every compared token is equal.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
EXP = os.path.join(HERE, 'bm', 'debug-ubc-hudi', 'expected')
Y_COLS = {0, 1, 2, 4, 6, 7}                     # nop ld st ldCCN br fp


def rows(path):
    return [l.rstrip('\n') for l in open(path)]


def find(log, key):
    for l in log:
        if l.split()[:len(key)] == key:
            return l
    return None


def check(prog, logf):
    doc = json.load(open(os.path.join(EXP, prog + '.json')))
    log = rows(logf)
    ok = bad = 0
    for e in doc['lines']:
        t = e['text'].split()
        if not t:
            continue
        cols = None                              # None: the whole line; else the event columns compared
        if t[0] in ('X', 'E') and len(t) > 2 and t[2] == 'ev':
            if int(t[1], 16) not in (0x06, 0x0A, 0x10, 0x11, 0x13, 0x14, 0x27, 0x23):
                continue
            cols = 'all'
        elif t[0] == 'Y' and len(t) > 2 and t[2] == 'ev':
            code = int(t[1], 16)
            if code not in (0x06, 0x0A, 0x13, 0x14, 0x23):
                continue
            cols = Y_COLS | ({8} if code in (0x06, 0x0A) else set())
        elif e['text'].startswith('T settle') or e['text'].startswith('S settle'):
            cols = None
        else:
            continue
        g = find(log, e['key'])
        if g is None:
            print('MISSING %s: %s' % (prog, e['text']))
            bad += 1
            continue
        gt = g.split()
        if cols is None:
            same = gt == t
        else:
            bar = t.index('|')
            want = t[3:bar]
            got = gt[3:gt.index('|')] if '|' in gt else []
            idx = range(len(want)) if cols == 'all' else sorted(cols)
            same = len(got) == len(want) and all(got[i] == want[i] for i in idx)
            if same and cols == 'all':           # the cycle columns too
                same = gt[bar + 1:] == t[bar + 1:]
        if same:
            ok += 1
        else:
            bad += 1
            print('MISMATCH %s\n   console: %s\n   rtl:     %s' % (prog, e['text'], g))
    print('v1_fetchx_counts %s: %d rows equal, %d differ' % (prog, ok, bad))
    return bad == 0 and ok > 0


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    a = check('ubcbrk', sys.argv[1])
    b = check('ubcevt', sys.argv[2])
    print('v1_fetchx_counts: %s' % ('PASS' if a and b else 'FAIL'))
    return 0 if a and b else 1


if __name__ == '__main__':
    sys.exit(main())
