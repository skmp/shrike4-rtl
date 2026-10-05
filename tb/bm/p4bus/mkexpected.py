#!/usr/bin/env python3
# mkexpected.py -- expected.json of tb/bm/p4bus from the console log of the P4-bridge probe (tools/hw/p4bus/p4b.c,
# p4b.elf.hwlog, 2026-09-23; SOC.md 3.1.5, 9.13).  Owner: WI-P4.
#
# p4b prints one "S <sequence>" row per access sequence: B - A (a PMCTR1L bracket around the sequence) for k = 0..11
# one-cycle fillers after a Pclk-aligning IPRA read, the minimum of 8 runs; an "X" row follows when some run differed
# (its maxima).  Each of the 12 values gets a class:
#   soc    k = 2..9: the fillers move the bracket's first read A one Icyc each, so k = 2..5 and k = 6..9 put A at the
#          four Pclk phases p0..p3 twice (p = (k - 2) mod 4; the rd16 row is the phase meter: 16 19 18 17).  The two k
#          of a phase agree -- these are the SOC.md 3.1.5 P4 costs at every phase, the M1a gate (SOC.md 8.3).  Where
#          the two k of a phase disagree, the smaller is the phase's value (min of 8 is a lower envelope; the larger
#          is a one-function layout effect) and the larger is 'open'
#   fetch  k = 0, 1 (the first fillers pair with the instructions before A: A stays at p0, the console's plateau) and
#          k = 10, 11 (A in the function's second 32-byte line): they depend on the issue / fetch schedule around the
#          probe as well as on the bridge.  Compared at level 'all' only
#   open   a value the model does not claim: a phase's larger outlier, or a k = 0 / 1 value off the p0 plateau
#          (an I-cache conflict of that one function with the caller's loop on the console)
#   env    the "p4b: start" line (the TMU / INTC state KOS left) and the X rows (the runs' maxima: interrupts were
#          masked, so these are the cold and conflicting runs)
#   python3 mkexpected.py [tools/hw/p4bus/p4b.elf.hwlog] > expected.json
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
LOG = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', '..', 'tools', 'hw', 'p4bus', 'p4b.elf.hwlog')
NK = 12
PHASE_K = list(range(2, 10))                        # the k that place A at a known Pclk phase


def phase_of(k):
    return (k - 2) % 4 if k in PHASE_K else None


def parse(lines):
    rows, xmax, start, restored, header = [], {}, None, None, None
    for l in lines:
        l = l.rstrip()
        t = l.split()
        if l.startswith('p4b: start;'):
            start = l
        elif l.startswith('p4b: restored'):
            restored = l
        elif l.startswith('p4b: B - A'):
            header = l
        elif len(t) >= 2 + NK and t[0] in ('S', 'X'):
            vals = [int(x) for x in t[2:2 + NK]]
            if t[0] == 'S':
                rows.append((t[1], vals, len(t) > 2 + NK and t[2 + NK] == '*'))
            else:
                xmax[t[1]] = vals
    return rows, xmax, start, restored, header


def classify(name, vals):
    cls = [None] * NK
    per_phase = [None] * 4
    for k in PHASE_K[:4]:
        a, b = vals[k], vals[k + 4]
        p = phase_of(k)
        per_phase[p] = min(a, b)
        cls[k] = 'soc' if a <= b else 'open'
        cls[k + 4] = 'soc' if b <= a else 'open'
    for k in (0, 1):
        cls[k] = 'fetch' if vals[k] == per_phase[0] else 'open'
    for k in (10, 11):
        cls[k] = 'fetch'
    return cls, per_phase


def main():
    lines = open(LOG, errors='replace').read().splitlines()
    rows, xmax, start, restored, header = parse(lines)
    if not rows or start is None or restored is None:
        raise SystemExit('no complete p4b run in ' + LOG)
    out_rows, nsoc, nfetch, nopen = [], 0, 0, 0
    for name, vals, star in rows:
        cls, per_phase = classify(name, vals)
        nsoc += cls.count('soc'); nfetch += cls.count('fetch'); nopen += cls.count('open')
        out_rows.append({'name': name, 'values': vals, 'class': cls, 'per_phase': per_phase,
                         'console_max': xmax.get(name), 'console_varied': star})
    doc = {
        'program': 'p4b',
        'source': 'tools/hw/p4bus/p4b.elf.hwlog',
        'what': 'B - A (PMCTR1L bracket) per access sequence for k = 0..11 fillers; min of 8 console runs',
        'nk': NK,
        'phase_of_k': [phase_of(k) for k in range(NK)],
        'classes': {
            'soc': 'k = 2..9: the SOC.md 3.1.5 P4 costs at each Pclk phase (p = (k - 2) mod 4): the M1a gate',
            'fetch': 'k = 0, 1 (the pairing plateau) and 10, 11 (A in the second 32-byte line): compared at level all',
            'open': 'not claimed: the larger of a phase\'s two k when they disagree; a k = 0 / 1 value off the plateau',
            'env': 'the start line (the TMU / INTC state KOS left) and the X rows (maxima of the 8 runs)'},
        'levels': {'struct': [], 'soc': ['soc'], 'all': ['soc', 'fetch']},
        'start': start,
        'header': header,
        'restored': restored,
        'rows': out_rows,
        'counts': {'rows': len(out_rows), 'soc': nsoc, 'fetch': nfetch, 'open': nopen},
    }
    json.dump(doc, sys.stdout, indent=1)
    sys.stdout.write('\n')
    sys.stderr.write('mkexpected: %d rows; %d soc, %d fetch, %d open values\n' % (len(out_rows), nsoc, nfetch, nopen))


if __name__ == '__main__':
    main()
