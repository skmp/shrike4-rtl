#!/usr/bin/env python3
"""analyze.py [main pass] [bracket pass] [freeze-reg pass] [freeze-fpu pass] -- pipeline-shape tables from the sweep.

Measured, per case (A, B, variant) and distance n (n dependent `add #1,r14` pads between A and B):
    T(n) cycles per iteration (exact),  S(n) = T(n) - n,  P(n) = S(n) - S(40)  (extra cycles against far apart).

Read-out rules (the only model used; everything below is a stated consequence of these):
  cost(X)   cycles X adds to the pad stream = S(40) of (X, NOP) - S(40) of (NOP, NOP).  0: X issues alongside an
            `add` (it can pair with an EX-group instruction); 1: it cannot (EX group, or a 1-cycle CO); k: k issue cycles
            or a k-cycle redirect (taken branch).
  natural issue distance of B after A at n >= 1:  nat(n) = cost(A) + n, the cycle B can issue in with nothing to
            wait for (A issues in cycle 0): pad k issues in cycle cost(A) + k - 1, and a B that could co-issue with pad n
            loses nothing by waiting one cycle (it co-issues with the first drain pad instead).
  required distance D = nat(n) + P(n) at the first n >= 1 with P(n) > 0 (for a ramp every such n gives the same D);
            D is where B can issue at the earliest, counted from A's issue.  None: nothing binds B at n >= 1 (B can go
            as early as the pipeline lets any instruction).
  RAW latency of an operand pair = D of that 'ov' variant (reported only where it exceeds the 'ind' D of the same pair,
            i.e. where the data dependence, not a structural limit, is what B waits for).
  pairing (bracket pass): with `stc dbr,r11` (pairs with nothing) right after B, A;B at n = 0 costs one cycle less
            when they issue together.  paired(A,B) = P_brk(0) < P_brk-reference ... see pairing()
Curves that are not a clean ramp from n = 1 on are listed as they are (odd.md): those are the interesting ones.
"""
import collections
import csv
import json
import os
import sys

import t2res

HERE = os.path.dirname(os.path.abspath(__file__))
MODES = ('M0', 'M1', 'M2')


def load_rows(pas):
    cs = t2res.curves(t2res.load(pas))
    rows = {}
    for c, e in cs.items():
        m = e['meta']
        pts = e['pts']
        if e['status'] or 40 not in pts:
            continue
        s40 = pts[40][0] - 40
        P = {n: round(pts[n][0] - n - s40, 3) for n in pts if n != 40}
        rows[(m['a'], m['b'], m['mode'], m['var'])] = {
            'a': m['a'], 'b': m['b'], 'mode': m['mode'], 'var': m['var'], 'S40': s40, 'T40': pts[40][0], 'P': P,
            'ev': {n: round(pts[n][1], 3) for n in pts},
            'integral': all(abs(pts[n][0] - round(pts[n][0])) < 1e-6 for n in pts)}
    return rows


def is_ramp(P):
    ns = [n for n in range(1, 17) if n in P]
    if not ns:
        return True
    c = P[ns[0]] + ns[0] if P[ns[0]] > 0 else None
    if c is None:
        return all(abs(P[n]) < 1e-6 for n in ns)
    return all(abs(P[n] - max(0, c - n)) < 1e-6 for n in ns)


def main():
    main_pass = sys.argv[1] if len(sys.argv) > 1 else 'par'
    brk_pass = sys.argv[2] if len(sys.argv) > 2 else None
    frz = sys.argv[3:5]
    out = os.path.join(HERE, 'analysis', main_pass)
    os.makedirs(out, exist_ok=True)
    R = load_rows(main_pass)
    B = load_rows(brk_pass) if brk_pass and os.path.isdir(os.path.join(HERE, 'results', brk_pass)) else {}
    F = [load_rows(p) if os.path.isdir(os.path.join(HERE, 'results', p)) else {} for p in frz]

    # ---- per-form cost
    cost = {}
    base = R[('NOP', 'NOP', 'M0', 'ind')]          # M1 / M2 have no (NOP, NOP): same integer loop, use M0's
    for md in MODES:
        base = R.get(('NOP', 'NOP', md, 'ind'), base)
        for (a, b, m, v), r in R.items():
            if m == md and b == 'NOP' and v == 'ind':
                cost[(a, md)] = round(r['S40'] - base['S40'], 3)

    # ---- pairing from the bracket pass: A;B adjacent vs B alone after the pads
    # with the bracket, n = 0 costs  (1 if A,B pair else 2) - cost(A) - (0 if B pairs with the pad else 1)  cycles over
    # the far reference, for single-cycle A and B; so  paired = P_brk(0) - P_brk_expected_unpaired == -1
    pairs_with = {}
    if B:
        for (a, b, md, v), r in B.items():
            if v != 'ind' or 0 not in r['P']:
                continue
            pairs_with[(a, b, md)] = r['P'][0]

    def b_pairs_pad(bname, md):
        c = cost.get((bname, md))
        return c is not None and c == 0

    def nat(a, b, md, n):
        # A issues in cycle 0 and holds the issue slot for cost(A) cycles; pad k issues in cycle cost(A) + k - 1.
        # A B that could co-issue with pad n loses nothing by waiting one cycle (it then co-issues with the first
        # drain pad), and a B that cannot pair with a pad issues right after pad n: either way B's free issue cycle is
        # cost(A) + n, and each cycle it has to wait beyond that shows up in P(n).
        ca = cost.get((a, md))
        if ca is None:
            return None
        return ca + n

    def required(r):
        P = r['P']
        for n in range(1, 17):
            if n in P and P[n] > 1e-6:
                v = nat(r['a'], r['b'], r['mode'], n)
                return None if v is None else round(v + P[n], 3)
        return None

    # ---- tables
    odd, lat_rows, struct_rows = [], [], []
    for key, r in sorted(R.items()):
        r['ramp'] = is_ramp(r['P'])
        r['D'] = required(r)
        if not r['ramp'] or not r['integral']:
            odd.append(r)
    for key, r in sorted(R.items()):
        a, b, md, v = key
        ind = R.get((a, b, md, 'ind'))
        if v == 'ind':
            if r['D'] is not None:
                struct_rows.append(r)
                # an implicit register A writes and B reads (T, FPUL, MACH/MACL, R0, PR, GBR, FPSCR): this 'ind'
                # curve is that register's latency as B sees it
                ra, wa = implicit_rw(a)
                rb, wb = implicit_rw(b)
                raw_imp = sorted((wa & rb) - {'pc'})
                if raw_imp:
                    lat_rows.append((dict(r, var='imp:' + '+'.join(raw_imp)), None))
        elif v.startswith('ov:') or v.startswith('mem:'):
            dind = ind['D'] if ind else None
            if r['D'] is not None and (dind is None or r['D'] > dind):
                lat_rows.append((r, dind))

    # per-form summary
    summ = collections.defaultdict(lambda: {'lat': collections.defaultdict(list), 'blocks': collections.defaultdict(list)})
    for r, dind in lat_rows:
        opname = r['var'].split(':', 1)[1]
        key_ = opname.split('>')[0] if '>' in opname else opname
        summ[(r['a'], r['mode'])]['lat'][key_].append((r['D'], r['b'], opname))
    for r in struct_rows:
        summ[(r['a'], r['mode'])]['blocks'][r['D']].append(r['b'])

    with open(os.path.join(out, 'curves.jsonl'), 'w') as f:
        for r in R.values():
            f.write(json.dumps({k: v for k, v in r.items()}) + '\n')
    with open(os.path.join(out, 'latency.csv'), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['A', 'B', 'mode', 'variant', 'D (required issue distance, cycles)', 'D of ind', 'P(0..16)'])
        for r, dind in lat_rows:
            w.writerow([r['a'], r['b'], r['mode'], r['var'], r['D'], dind, ' '.join(f'{r["P"].get(n, "-")}' for n in range(17))])
    with open(os.path.join(out, 'structural.csv'), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['A', 'B', 'mode', 'D', 'P(0..16)'])
        for r in struct_rows:
            w.writerow([r['a'], r['b'], r['mode'], r['D'], ' '.join(f'{r["P"].get(n, "-")}' for n in range(17))])

    # ---- report
    L = []
    L.append(f'# SH7091 pipeline shape from the pairwise sweep (pass `{main_pass}`)\n')
    L.append(f'{len(R)} cases with a curve; {sum(1 for r in R.values() if r["ramp"])} are clean ramps from n = 1 on; '
             f'{len(odd)} are not (or not integral) and are listed in odd.md.\n')
    L.append('Columns: **cost** = cycles in the `add` pad stream (0 = issues alongside an EX instruction); '
             '**latency** = required issue distance D from each output operand to the consumers that wait for it '
             '(min..max over consumers, the value most consumers see in bold); **blocks** = following instructions '
             'that cannot issue before distance D even with no data dependence (structural), as "D: count".\n')
    for md in MODES:
        L.append(f'\n## Mode {md} ({ {"M0": "PR=0 SZ=0", "M1": "PR=0 SZ=1", "M2": "PR=1 SZ=0"}[md] })\n')
        L.append('| form | cost | latency by output operand | blocks (D: #B) |')
        L.append('|---|---|---|---|')
        forms = sorted({a for (a, m) in cost if m == md} | {a for (a, m) in summ if m == md})
        for a in forms:
            s = summ.get((a, md))
            lat = ''
            if s:
                parts = []
                for opn, lst in sorted(s['lat'].items()):
                    ds = [d for d, _, _ in lst]
                    mode_d = collections.Counter(ds).most_common(1)[0][0]
                    lo, hi = min(ds), max(ds)
                    parts.append(f'{opn}: ' + (f'**{mode_d:g}**' if lo == hi else f'{lo:g}..**{mode_d:g}**..{hi:g}'))
                lat = '; '.join(parts)
                blk = ', '.join(f'{d:g}: {len(bs)}' for d, bs in sorted(s['blocks'].items()))
            else:
                blk = ''
            L.append(f'| {a} | {cost.get((a, md), "")} | {lat} | {blk} |')
    with open(os.path.join(out, 'report.md'), 'w') as f:
        f.write('\n'.join(L) + '\n')
    # ---- side by side with the manual's Table 8.3 (comparison only)
    try:
        man = {f: r for r in json.load(open(os.path.join(HERE, 'manual_table.json'))).values() for f in r['forms']}
    except OSError:
        man = {}
    C = ['# Measured vs the programming manual, Table 8.3 (M0)\n',
         'cost = cycles in the pad stream (0 = co-issues with an EX instruction, else issue cycles); latency = required '
         'issue distance to the consumers that wait for each output (min..most common..max).  The manual columns are '
         'copied as printed.\n',
         '| form | manual group | manual issue | cost | manual latency | measured latency |', '|---|---|---|---|---|---|']
    for a in sorted({a for (a, m) in cost if m == 'M0'}):
        s_ = summ.get((a, 'M0'))
        lat = ''
        if s_:
            parts = []
            for opn, lst in sorted(s_['lat'].items()):
                ds = [d for d, _, _ in lst]
                md_ = collections.Counter(ds).most_common(1)[0][0]
                parts.append(f'{opn} ' + (f'{md_:g}' if min(ds) == max(ds) else f'{min(ds):g}..{md_:g}..{max(ds):g}'))
            lat = '; '.join(parts)
        mr = man.get(a, {})
        C.append(f'| {a} | {mr.get("group", "")} | {mr.get("issue", "")} | {cost[(a, "M0")]:g} | '
                 f'{mr.get("latency", "")} | {lat} |')
    with open(os.path.join(out, 'compare.md'), 'w') as f:
        f.write('\n'.join(C) + '\n')
    with open(os.path.join(out, 'odd.md'), 'w') as f:
        f.write(f'# Curves that are not a clean ramp (pass `{main_pass}`)\n\nP(0..16), extra cycles against far apart; '
                'grouped by A.\n\n')
        by_a = collections.defaultdict(list)
        for r in odd:
            by_a[(r['a'], r['mode'])].append(r)
        for (a, md), lst in sorted(by_a.items()):
            f.write(f'\n## {a} ({md})\n\n')
            for r in lst:
                f.write(f'* {r["b"]} [{r["var"]}]{"" if r["integral"] else " (non-integral)"}: '
                        f'{" ".join(str(r["P"].get(n, "-")) for n in range(17))}\n')
    if B:
        pairing_report(R, B, cost, out)
    if any(F):
        freeze_report(R, F, frz, out)
    print(f'{len(R)} cases, {len(odd)} odd, {len(lat_rows)} latency rows, {len(struct_rows)} structural rows -> {out}')


def implicit_rw(name):
    """implicit (non-field) resources a form reads / writes; FPSCR split into mode bits and cause/flag bits"""
    import isa
    f = isa.BYNAME[name]
    rd, wr = set(), set()
    field = ('Rn', 'Rm', 'FRn', 'FRm', 'DRn', 'DRm', 'XDn', 'XDm', 'FVn', 'FVm', 'FIPRn', 'BANKn', 'BANKm')
    for op in f.ops:
        if op.name in field:
            continue
        nm = op.name
        if nm == 'FPSCR':
            if op.acc == 'r':
                rd.add('fpscr.mode')
                if name.startswith('STS'):
                    rd.add('fpscr.cause')
            elif op.acc == 'rw':          # FP arithmetic: reads the mode, writes cause / flags
                rd.add('fpscr.mode'); wr.add('fpscr.cause')
            else:                         # LDS ...,FPSCR
                wr |= {'fpscr.mode', 'fpscr.cause'}
            continue
        if nm in ('FRB', 'SZB'):
            rd.add('fpscr.mode'); wr.add('fpscr.mode'); continue
        if 'r' in op.acc:
            rd.add(nm)
        if 'w' in op.acc:
            wr.add(nm)
    if f.br:
        rd.add('pc'); wr.add('pc')
    return rd, wr


def implicit_dep(a, b):
    ra, wa = implicit_rw(a)
    rb, wb = implicit_rw(b)
    ww = (wa & wb) - {'fpscr.cause'}         # two FP operations both setting the cause bits: no hazard (measured)
    return bool((wa & rb) or ww or (ra & wb))


def pairing_report(R, B, cost, out):
    """bracket pass: `stc dbr,r11` (never co-issues) replaces the first pad after B.  At n = 0, for single-cycle A and B
    with no structural stall between them (their 'ind' curve is flat from n = 1 on), the adjacent pair costs
        Q = P_brk(0) + cost(A) + cost(B)  =  1 if A and B issue in the same cycle,  2 if not
    over what the two cost when far apart; the parallel-issue counter confirms it (one more dual issue when paired)."""
    Q = {}
    for (a, b, md, v), rb in B.items():
        if v != 'ind' or 0 not in rb['P']:
            continue
        rm = R.get((a, b, md, v))
        ca, cb = cost.get((a, md)), cost.get((b, md))
        if rm is None or ca is None or cb is None:
            continue
        if any(abs(rm['P'].get(n, 0)) > 1e-6 for n in range(1, 17)) or not rb['integral']:
            continue            # a structural stall between A and B hides the pairing
        if ca > 1 or cb > 1:
            continue            # multi-cycle issue or a redirect: not a single-cycle pair
        if implicit_dep(a, b):
            continue            # an implicit shared register (T, R0, FPUL, MAC, ...): a dependency, not pairing
        q = round(rb['P'][0] + ca + cb, 3)
        dev = round(rb['ev'][0] - rb['ev'][40], 3)
        Q[(a, b, md)] = (q, dev)
    forms = sorted({a for (a, b, md) in Q if md == 'M0'})
    pairs = {k: v[0] == 1 for k, v in Q.items() if v[0] in (1, 2)}
    bad = {k: v for k, v in Q.items() if v[0] not in (1, 2)}
    # classes: forms whose pairing rows (as first and as second instruction) never disagree where both were measured.
    # Greedy over the best-measured forms first; a class keeps the union of its members' known entries.
    def row(x):
        r = {}
        for y in forms:
            if (x, y, 'M0') in pairs:
                r[('a', y)] = pairs[(x, y, 'M0')]
            if (y, x, 'M0') in pairs:
                r[('b', y)] = pairs[(y, x, 'M0')]
        return r
    rows_ = {x: row(x) for x in forms}
    classes = []                                  # [members, merged row]
    for x in sorted(forms, key=lambda f: -len(rows_[f])):
        rx = rows_[x]
        for cl in classes:
            common = [k for k in rx if k in cl[1]]
            if len(common) >= 8 and all(rx[k] == cl[1][k] for k in common):
                cl[0].append(x)
                cl[1].update(rx)
                break
        else:
            classes.append([[x], dict(rx)])
    groups = {i: cl[0] for i, cl in enumerate(classes)}
    # a form whose measured row is also consistent with another class is not determined by the data
    ambiguous = {}
    for cl in classes:
        for x in cl[0]:
            rx = rows_[x]
            fits = [j for j, other in enumerate(classes)
                    if all(rx[k] == other[1][k] for k in rx if k in other[1] and k[1] != x)]
            if len(fits) > 1:
                ambiguous[x] = len(fits)
    L = ['# Dual issue: which instruction pairs can issue in the same cycle (bracket pass, mode M0)\n',
         f'{len(Q)} single-cycle pairs measured; {sum(pairs.values())} co-issue, {len(pairs) - sum(pairs.values())} do not; '
         f'{len(bad)} give neither 1 nor 2 (listed at the end).\n',
         'Forms with identical pairing rows (as first and as second instruction) are one class below.\n']
    cls = sorted(groups.values(), key=lambda g: (-len(g), g[0]))
    cls = [sorted(g) for g in cls]
    try:
        man = {f: r['group'] for r in json.load(open(os.path.join(HERE, 'manual_table.json'))).values() for f in r['forms']}
    except OSError:
        man = {}
    for i, g in enumerate(cls):
        mg = collections.Counter(man.get(x, '?') for x in g)
        L.append(f'\n## class {i + 1} ({len(g)} forms; manual groups of the members: '
                 + ', '.join(f'{k} {v}' for k, v in mg.most_common()) + ')\n\n'
                 + ', '.join(x + (' (not determined: fits %d classes)' % ambiguous[x] if x in ambiguous else '')
                             + (f' [manual {man[x]}]' if man.get(x) and mg.most_common(1)[0][0] != man[x] else '') for x in g) + '\n')
    # class x class table: does a member of class i (first) co-issue with a member of class j (second)
    L.append('\n## class pairing table (first = row, second = column; Y co-issue, - no, ? mixed / not measured)\n')
    L.append('| | ' + ' | '.join(str(j + 1) for j in range(len(cls))) + ' |')
    L.append('|---' * (len(cls) + 1) + '|')
    for i, gi in enumerate(cls):
        cells = []
        for j, gj in enumerate(cls):
            vals = {pairs.get((x, y, 'M0')) for x in gi for y in gj} - {None}
            cells.append('Y' if vals == {True} else '-' if vals == {False} else '?')
        L.append(f'| {i + 1} | ' + ' | '.join(cells) + ' |')
    if bad:
        L.append('\n## pairs with Q not in {1, 2}\n')
        for (a, b, md), (q, dev) in sorted(bad.items()):
            L.append(f'* {a} ; {b} ({md}): Q = {q}, parallel-issue delta {dev}')
    with open(os.path.join(out, 'pairing.md'), 'w') as f:
        f.write('\n'.join(L) + '\n')
    with open(os.path.join(out, 'pairing.csv'), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['A', 'B', 'mode', 'Q', 'parallel-issue delta n=0 vs 40'])
        for (a, b, md), (q, dev) in sorted(Q.items()):
            w.writerow([a, b, md, q, dev])


def freeze_report(R, F, names, out):
    """freeze-counter passes: cycles per iteration the pipeline was frozen by a register dependency / by the FPU, at
    each n, minus the far value -- which kind of wait each stall is"""
    with open(os.path.join(out, 'freeze.csv'), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['A', 'B', 'mode', 'variant', 'P(0..4)'] + [f'{nm} extra (0..4)' for nm in names])
        for key, r in sorted(R.items()):
            if not any(abs(r['P'].get(n, 0)) > 1e-6 for n in range(0, 5)):
                continue
            cols = []
            for Fp in F:
                fr = Fp.get(key)
                cols.append('' if fr is None else ' '.join(f'{round(fr["ev"].get(n, 0) - fr["ev"][40], 3):g}' for n in range(5)))
            w.writerow([r['a'], r['b'], r['mode'], r['var'], ' '.join(f'{r["P"].get(n, "-")}' for n in range(5))] + cols)


if __name__ == '__main__':
    main()
