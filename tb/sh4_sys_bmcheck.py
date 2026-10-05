#!/usr/bin/env python3
# sh4_sys_bmcheck.py -- the FUNCTIONAL check of the exceptions-intc bare-metal ports (tb/bm/exceptions-intc, WI-EXC)
# run on the system reference model (tb/sh4_sys_bm.cpp; WI-REF).  tb/bm/exceptions-intc/check.py checks an RTL run
# line by line in print order; a functional model has no timing, so this wrapper uses the same expected.json with the
# same regexes and `rel` relations, and differs only in what a functional run cannot reproduce:
#   * lines are paired by their whole id in order (an extra or missing line does not shift the rest);
#   * not compared: modes timing / info / board; exact lines that print cycle counts ("cyc", "none ret").
# Every other line must match its regex and its `rel` relations exactly as check.py applies them.  (The three
# relaxations this wrapper once applied -- the stub lines' SR.T, the KOS store-queue buffer address, prio_irq_ill's
# class -- are gone: WI-EXC's mkexpected.py now writes those entries so that a correct run passes as they are,
# SOC.md 8.4 WI-EXC (3) / WI-REF (8).)
#   usage: sh4_sys_bmcheck.py RUN_OUTPUT.txt [-v]
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
EXP = os.path.join(HERE, 'bm', 'exceptions-intc', 'expected.json')


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('-')]
    verbose = '-v' in sys.argv
    if not args:
        print('usage: sh4_sys_bmcheck.py RUN_OUTPUT.txt [-v]')
        return 2
    text = open(args[0], errors='replace').read()
    out = [l.rstrip() for l in text.splitlines() if l.strip()]
    doc = json.load(open(EXP))
    tot_n = tot_f = tot_s = 0
    for prog, exp in doc['programs'].items():
        try:
            i0 = next(k for k, l in enumerate(out) if l.startswith(prog + ': SR'))
            i1 = next(k for k in range(i0, len(out)) if out[k].startswith(prog + ': done'))
        except StopIteration:
            print(f'{prog}: no output')
            tot_f += 1
            continue
        lines = out[i0:i1 + 1]
        pos, n, fails, skipped, bufs = 0, 0, [], 0, []
        for e in exp:
            j = pos
            idw = e['id'][-1:].isalnum() or e['id'].endswith('_')          # the whole id: fd_slot is not fd_slot_fffd
            idrx = re.compile(re.escape(e['id']) + (r'(?![A-Za-z0-9_])' if idw else ''))
            while j < len(lines) and not idrx.match(lines[j]):
                j += 1
            ln = lines[j] if j < len(lines) else None
            if ln is not None:
                pos = j + 1
            mode = e['mode']
            timing = mode in ('timing', 'info', 'board') or \
                (mode == 'exact' and ('cyc' in e['console'] or 'none ret' in e['console']))
            if timing:
                skipped += 1
                continue
            n += 1
            if ln is None:
                fails.append(f'{prog} {e["id"]!r}: missing')
                continue
            rx = e.get('regex')
            if mode == 'exact' or rx is None:
                if ln != e['console']:
                    fails.append(f'{prog} {e["id"]}: {ln!r} != console {e["console"]!r}')
                continue
            m = re.fullmatch(rx, ln)
            if not m:
                fails.append(f'{prog} {e["id"]} ({mode}): {ln!r} does not match; console {e["console"]!r}')
                continue
            g = {k: int(v, 16) for k, v in m.groupdict().items() if v is not None}
            if e.get('defines') == 'buf' and 'buf' in g:
                bufs.append(g['buf'])
            for a, b, d in e.get('rel', []):
                bv = bufs[int(b[3:])] if b.startswith('buf') and int(b[3:]) < len(bufs) else g.get(b)
                if bv is None or a not in g or (g[a] - bv - d) & 0xFFFFFFFF:
                    fails.append(f'{prog} {e["id"]}: {a} should be {b} + {d} ({ln!r})')
        print(f'{prog}: {n} functional lines checked, {skipped} timing / board / info lines not compared, '
              f'{len(fails)} failures')
        for f in fails[:40]:
            print('  ' + f)
        tot_n += n
        tot_f += len(fails)
        tot_s += skipped
    print(f'exceptions-intc bm on Sh4Sys: {tot_n} functional lines checked, {tot_s} not compared, {tot_f} failures')
    print('exceptions-intc bm on Sh4Sys: ' + ('PASS' if tot_f == 0 else 'FAIL'))
    return 1 if tot_f else 0


if __name__ == '__main__':
    sys.exit(main())
