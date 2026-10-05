#!/usr/bin/env python3
"""layout.py -- check that a bare-metal build has the console build's layout (bm.ld; M3, WI-SOC).

  layout.py NM OURS.elf CONSOLE.elf [--shift REGEX=DELTA ...]
        exit 0 when every compared symbol is at the console's address (plus DELTA for the symbols REGEX matches: the
        console's run had an image other than the ELF kept, bm_bscmm.ld)

Compared: the program's functions (meas, main, dump_regs), every timed kernel (t_*), every kernel-table entry (K_*,
hi.0), KOS's irq_disable / irq_restore -- whatever of them the console's ELF has."""
import re
import subprocess
import sys


def syms(nm, elf):
    out = {}
    for line in subprocess.run([nm, elf], capture_output=True, text=True, check=True).stdout.splitlines():
        t = line.split()
        if len(t) == 3:
            out[t[2]] = int(t[0], 16)
    return out


def main():
    nm, ours, cons = sys.argv[1:4]
    shifts = []
    rest = sys.argv[4:]
    while rest:
        assert rest[0] == '--shift' and len(rest) > 1, __doc__
        rx, d = rest[1].rsplit('=', 1)
        shifts.append((re.compile(rx), int(d, 0)))
        rest = rest[2:]
    a, b = syms(nm, ours), syms(nm, cons)
    for s in b:
        for rx, d in shifts:
            if rx.match(s):
                b[s] += d
                break
    want = [s for s in b if re.match(r'^_(meas|main|dump_regs|irq_disable|irq_restore|t_\w+|K_\w+|hi\.0)$', s)]
    bad = [s for s in want if a.get(s) != b[s]]
    for s in bad[:20]:
        print('layout: %s at %s, console %08x' % (s, '%08x' % a[s] if s in a else 'missing', b[s]))
    print('layout %s: %d symbols at the console\'s addresses, %d not' % (ours, len(want) - len(bad), len(bad)))
    sys.exit(1 if bad or not want else 0)


if __name__ == '__main__':
    main()
