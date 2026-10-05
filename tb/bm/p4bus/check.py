#!/usr/bin/env python3
# check.py -- checks the output of the bare-metal p4b probe (tb/bm/p4bus) against expected.json (mkexpected.py, from
# the console log tools/hw/p4bus/p4b.elf.hwlog).  Owner: WI-P4.
#   usage: check.py RUN_OUTPUT [--expected FILE] [--level struct|soc|all] [-v]
#          check.py --self [--expected FILE]
#     RUN_OUTPUT  the text the run printed (the sim-control putchar stream; other programs' lines are ignored)
#     --level     struct  the program ran to its end: the start / header / restored lines, every S row once, in the
#                         console's order, with 12 numbers; TCR1 / TCOR1 / TCNT1 restored to their start values.  No
#                         number is compared (the ISS run of run_iss.cpp: PMCTR counts instructions there)
#                 soc     (default) + every 'soc' value exact: the SOC.md 3.1.5 P4 costs at the four Pclk phases (k =
#                         2..9).  The M1a gate (SOC.md 8.3): v1_soc, IDEAL_MEM = 1, TRANSLATE = 0
#                 all     + every 'fetch' value (k = 0, 1, 10, 11): the pairing plateau and the second 32-byte line
#                 'open' values are never compared; the X rows (maxima) and a row's '*' are ignored
#     --self      the console's own output passes at every level; each compared number changed by one, each S row
#                 dropped, the rows reordered, and a TCR1 left unrestored are each rejected; the unit testbench's copy
#                 of the per-phase numbers (tb/v1_p4bus_tb.cpp, t_probe) equals expected.json's
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..', '..', '..')
LEVELS = {'struct': set(), 'soc': {'soc'}, 'all': {'soc', 'fetch'}}
START = re.compile(r'^p4b: start; TSTR ([0-9a-fA-F]+) TCR1 ([0-9a-fA-F]+) TCOR1 ([0-9a-fA-F]+) TCNT1 ([0-9a-fA-F]+) '
                   r'IPRA ([0-9a-fA-F]+)$')
RESTORED = re.compile(r'^p4b: restored TCR1 ([0-9a-fA-F]+) TCOR1 ([0-9a-fA-F]+) TCNT1 ([0-9a-fA-F]+); done$')


def parse(text, nk):
    """the run's p4b block: (start match, header seen, [(name, values)], restored match); X rows ignored"""
    start, header, rows, restored = None, False, [], None
    for l in text.splitlines():
        l = l.rstrip()
        m = START.match(l)
        if m:
            start, header, rows, restored = m, False, [], None           # the last run in the output counts
            continue
        if start is None:
            continue
        if l.startswith('p4b: B - A'):
            header = True
            continue
        m = RESTORED.match(l)
        if m:
            restored = m
            continue
        t = l.split()
        if len(t) >= 2 and t[0] == 'S':
            try:
                vals = [int(x) for x in t[2:2 + nk]]
            except ValueError:
                vals = []
            extra = t[2 + nk:]
            rows.append((t[1], vals if len(vals) == nk and extra in ([], ['*']) else None))
    return start, header, rows, restored


def check(text, exp, level, verbose=False, quiet=False):
    """returns the list of failures"""
    nk = exp['nk']
    cmp_cls = LEVELS[level]
    start, header, rows, restored = parse(text, nk)
    fails = []
    if start is None:
        return ['no "p4b: start" line: the program did not run']
    if not header:
        fails.append('no "p4b: B - A" header line')
    if restored is None:
        fails.append('no "p4b: restored ...; done" line: the program did not finish')
    else:
        # functional: the program restores the TMU1 registers it wrote (TMU1 stopped, so TCNT1 reads back unchanged)
        for i, n in enumerate(('TCR1', 'TCOR1', 'TCNT1')):
            if int(restored.group(1 + i), 16) != int(start.group(2 + i), 16):
                fails.append('%s restored to %s, it was %s at the start' % (n, restored.group(1 + i), start.group(2 + i)))
    names = [r['name'] for r in exp['rows']]
    got_names = [n for n, _ in rows]
    if got_names != names:
        missing = [n for n in names if n not in got_names]
        extra = [n for n in got_names if n not in names]
        dup = sorted({n for n in got_names if got_names.count(n) > 1})
        fails.append('S rows differ from the console\'s: missing %s, unexpected %s, repeated %s%s' %
                     (missing, extra, dup, '' if missing or extra or dup else ', order changed'))
    got = dict(rows)
    ncmp = nbad = 0
    for r in exp['rows']:
        v = got.get(r['name'], 'absent')
        if v == 'absent':
            continue
        if v is None:
            fails.append('S %s: not 12 numbers' % r['name'])
            continue
        for k in range(nk):
            if r['class'][k] not in cmp_cls:
                continue
            ncmp += 1
            if v[k] != r['values'][k]:
                nbad += 1
                p = exp['phase_of_k'][k]
                fails.append('S %-12s k = %2d (%s, %s): %d, console %d' % (
                    r['name'], k, 'phase p%d' % p if p is not None else 'no phase', r['class'][k], v[k], r['values'][k]))
    if not quiet:
        print('check.py p4b --level %s: %d rows, %d numbers compared, %d differ; %s' % (
            level, len(rows), ncmp, nbad, 'PASS' if not fails else 'FAIL (%d)' % len(fails)))
        for f in fails[:60 if verbose else 25]:
            print('  ' + f)
    return fails


def unit_tb_table():
    """the per-phase rows of the unit testbench's t_probe (tb/v1_p4bus_tb.cpp)"""
    src = open(os.path.join(ROOT, 'tb', 'v1_p4bus_tb.cpp')).read()
    body = src[src.index('static void t_probe()'):]
    body = body[:body.index('int nbad')]
    return {m.group(1): [int(x) for x in m.group(2).split(',')]
            for m in re.finditer(r'\{"(\w+)",\s*(?:\{[^{}]*\}|rep\([^)]*\)),\s*\{([-\d,\s]+)\}\}', body)}


def self_test(exp):
    log = open(os.path.join(ROOT, exp['source']), errors='replace').read()
    bad = 0
    for lvl in LEVELS:
        if check(log, exp, lvl, quiet=True):
            print('self: the console log fails its own expected values at level %s:' % lvl)
            for f in check(log, exp, lvl, quiet=True)[:10]:
                print('  ' + f)
            bad += 1
    lines = log.splitlines()
    srow = {l.split()[1]: i for i, l in enumerate(lines) if l.startswith('S ')}
    # every compared number changed by one must be rejected, at level all and (for 'soc') at level soc
    nneg = 0
    for r in exp['rows']:
        i = srow[r['name']]
        for k in range(exp['nk']):
            if r['class'][k] == 'open':
                continue
            t = lines[i].split()
            t[2 + k] = str(int(t[2 + k]) + 1)
            mut = '\n'.join(lines[:i] + [' '.join(t)] + lines[i + 1:])
            for lvl in ('all',) + (('soc',) if r['class'][k] == 'soc' else ()):
                nneg += 1
                if not check(mut, exp, lvl, quiet=True):
                    print('self: S %s k = %d changed by one passes at level %s' % (r['name'], k, lvl))
                    bad += 1
            if r['class'][k] == 'fetch' and check(mut, exp, 'soc', quiet=True):
                print('self: S %s k = %d (fetch) changed by one fails at level soc' % (r['name'], k))
                bad += 1
    # each S row dropped; two rows swapped; TCR1 not restored; the run cut before its end
    for name, i in srow.items():
        nneg += 1
        if not check('\n'.join(lines[:i] + lines[i + 1:]), exp, 'struct', quiet=True):
            print('self: dropping S %s passes' % name)
            bad += 1
    a, b = srow['rd16'], srow['rd32']
    sw = list(lines)
    sw[a], sw[b] = sw[b], sw[a]
    nneg += 2
    if not check('\n'.join(sw), exp, 'struct', quiet=True):
        print('self: swapped rows pass')
        bad += 1
    if not check(log.replace('p4b: restored TCR1 0000', 'p4b: restored TCR1 0020'), exp, 'struct', quiet=True):
        print('self: an unrestored TCR1 passes')
        bad += 1
    nneg += 1
    if not check(log[:log.index('p4b: restored')], exp, 'struct', quiet=True):
        print('self: a run without its "restored ...; done" line passes')
        bad += 1
    # the unit testbench's copy of the per-phase numbers
    tb = unit_tb_table()
    for r in exp['rows']:
        if tb.get(r['name']) != r['per_phase']:
            print('self: tb/v1_p4bus_tb.cpp t_probe row %s = %s, expected.json per_phase %s' %
                  (r['name'], tb.get(r['name']), r['per_phase']))
            bad += 1
    if set(tb) != {r['name'] for r in exp['rows']}:
        print('self: t_probe rows %s vs expected.json rows' % sorted(set(tb) ^ {r['name'] for r in exp['rows']}))
        bad += 1
    print('check.py --self: the console log passes at %s; %d perturbations rejected; t_probe (%d rows) agrees; %s' % (
        ' / '.join(LEVELS), nneg - bad, len(tb), 'PASS' if not bad else 'FAIL (%d)' % bad))
    return bad


def main():
    args = sys.argv[1:]
    expf = os.path.join(HERE, 'expected.json')
    level, verbose, selft, outs = 'soc', False, False, []
    i = 0
    while i < len(args):
        a = args[i]
        if a == '--expected':
            expf = args[i + 1]; i += 1
        elif a == '--level':
            level = args[i + 1]; i += 1
        elif a == '-v':
            verbose = True
        elif a == '--self':
            selft = True
        else:
            outs.append(a)
        i += 1
    if level not in LEVELS:
        raise SystemExit('unknown level ' + level)
    exp = json.load(open(expf))
    if selft:
        sys.exit(1 if self_test(exp) else 0)
    if len(outs) != 1:
        raise SystemExit(__doc__ if __doc__ else 'usage: check.py RUN_OUTPUT [--level struct|soc|all]')
    sys.exit(1 if check(open(outs[0], errors='replace').read(), exp, level, verbose) else 0)


if __name__ == '__main__':
    main()
