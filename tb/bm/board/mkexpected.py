#!/usr/bin/env python3
"""mkexpected.py -- expected values of the board probes, taken from the console logs (rtl/v1/SOC.md 8.1: "the
bare-metal ports of its spec's probes, tb/bm/<spec>/, expected values from the console logs"; spec board.md 6).

The board spec's probes are KOS programs (tools/hw/board/boardio.c, boardregs.c) and the board exists to boot KOS, so
the port is the unmodified ELF (tools/hw/board/*.elf), run on the model (make dc-board-bm: Sh4Sys + DcBoard) or later
on the RTL SoC (WI-SOC, M4 / M5), and compared by check.py.  This script parses every "B ..." line of the console logs
into expected.json (one record per line, keyed as check.py keys the run's lines).  Owner WI-BOARD.

    python3 tb/bm/board/mkexpected.py [--hw tools/hw/board] [-o tb/bm/board/expected.json]
"""
import argparse
import json
import os
import sys

sys.dont_write_bytecode = True       # no __pycache__ in the repo
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check import parse_log  # noqa: E402  (one parser for both sides)

PROGS = ('boardio', 'boardregs')


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.normpath(os.path.join(here, '..', '..', '..'))
    ap = argparse.ArgumentParser()
    ap.add_argument('--hw', default=os.path.join(root, 'tools', 'hw', 'board'))
    ap.add_argument('-o', default=os.path.join(here, 'expected.json'))
    a = ap.parse_args()
    out = {'_source': 'console logs tools/hw/board/{boardio,boardregs}.out (board.md 6.1 / 6.2; dcload-ip 2.0.3, '
                      'VGA, controller on A0, CD-R in the drive)'}
    for p in PROGS:
        with open(os.path.join(a.hw, p + '.out'), errors='replace') as f:
            recs = parse_log(f.read())
        if not recs:
            sys.exit('no B lines in %s.out' % p)
        out[p] = recs
    with open(a.o, 'w') as f:
        json.dump(out, f, indent=1, sort_keys=True)
        f.write('\n')
    print('wrote %s: %s' % (a.o, ', '.join('%s %d lines' % (p, len(out[p])) for p in PROGS)))


if __name__ == '__main__':
    main()
