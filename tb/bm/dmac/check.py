#!/usr/bin/env python3
# check.py -- checks the output of the bare-metal DMAC probes (tb/bm/dmac: dmac1, dmac2; WI-DMAC, README.md) against
# expected/<prog>.json (mkexpected.py, from the console logs).
#   check.py LOG expected/<prog>.json [--classes functional,timing,cache]
#       LOG: the text the program printed (the sim-control putchar stream; other programs' lines may be around it).
#       The program's lines run from the rule file's `first` line to its `last` line and pair with the rules in
#       order.  Classes (default functional): functional = the regex and every `func` relation (and the S10 priority
#       structure); timing = the `timing` targets within their tolerance; cache = S12's exact text.  An env line only
#       has to match its regex (it defines bufA / bufB).  Exit 0 when every selected check passes.
#   check.py --self expected/<prog>.json
#       the console log itself passes every class, and every perturbation is caught: one digit of a compared token
#       changed on each line, one line dropped, one relation broken per line with relations, the S10 structure broken.
#   (M6, WI-SOC) expected/<prog>_kos.json (mkexpected.py --kos): the console probe unmodified under KOS (tools/hw/dmac/
#       dmacN.elf behind soc_tb's dcload stand-in with the phase-2 board, --ddt): every line of the console's output,
#       S0 / S1 / S13 / T1 included.  Two more rule kinds: `multi` (dmac1 S1's samples: every consecutive line with that
#       prefix, checked by s1_check -- the structure of Holly's channel-0 DTRs, dmac.md 8 item 4 "compared by structure
#       (DTR per unit, CHCR0 values), not by timestamp") and `board` groups (a token set by the board's preset rather
#       than by the chip: the BIOS / dcload leftovers of dmac.md 4.4 and board.md 5.3, each allowed value named with
#       its reason).  Class `doc` (a timing token of SOC.md 9.25's open rows): reported with its bound, failing outside
#       it.
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..', '..', '..')


def conv(name, v):
    if name.startswith('x_'):
        return int(v, 16)
    if name.startswith('f_'):
        return float(v)
    return int(v)


def samples(lines):
    out = []
    for l in lines:
        for t, a, b in re.findall(r'(\d+):(\d+)/(\d+)', l):
            out.append((int(t), int(a), int(b)))
    return out


def s10_check(mode, sm, started):
    """the priority structure of one dmac1 S10 run (spec 3.5): returns an error string or None"""
    after = [s for s in sm if s[0] >= started]
    if len(after) < 4:
        return 'too few samples after channel 1 started'
    if mode == 'fixed_ch1_first':                       # PR=10 (2>0>1>3): channel 3 frozen while channel 1 runs
        run = [s for s in after if 0 < s[1] < 1024]
        if len(run) < 2:
            return 'channel 1 never observed running'
        if run[0][2] - run[-1][2] > 1:                  # (one unit of channel 3 may be in flight at the start)
            return f'channel 3 moved from {run[0][2]} to {run[-1][2]} while channel 1 ran'
        if any(s[1] == 1024 for s in after[2:]):
            return 'channel 1 did not start'
    elif mode == 'fixed_ch3_first':                     # PR=01 (0>2>3>1): channel 1 waits while channel 3 runs
        if any(s[1] != 1024 for s in after if s[2] > 0):
            return 'channel 1 ran while channel 3 still had work'
    elif mode == 'round_robin':                         # PR=11: one unit each, alternately
        both = [(p, q) for p, q in zip(after, after[1:]) if q[1] > 0 and q[2] > 0 and p[1] < 1024]
        if len(both) < 2:
            return 'no interval with both channels running'
        for p, q in both:
            d1, d3 = p[1] - q[1], p[2] - q[2]
            if d1 <= 0 or d3 <= 0 or abs(d1 - d3) > 1:
                return f'interval {p[0]}..{q[0]}: channel 1 did {d1} units, channel 3 {d3}'
    return None


S1_CHCR0 = (0x852c1, 0x852c3, 0x853c1, 0x853c3)


def s1_samples(lines):
    out = []
    for ln in lines:
        m = re.fullmatch(r'S1 t=\s*(\d+) SAR0 ([0-9a-f]{8}) DAR0 ([0-9a-f]{8}) DMATCR0 ([0-9a-f]{8}) CHCR0 ([0-9a-f]{8}) '
                         r'MDST ([0-9a-f]+)', ln)
        if not m:
            return None, ln
        out.append(tuple(int(x, 16) if i else int(x) for i, x in enumerate(m.groups())))
    return out, None


def s1_check(lines, mdstar, nmin, nmax):
    """dmac1 S1: channel 0 sampled while KOS's maple DMAs run through DDT ([HW] dmac1 S1; dmac.md 3.9.3, 4.2): every
    change of SAR0 / DAR0 / DMATCR0 / CHCR0 / MDST.  The structure every correct chip + Holly gives (not the times):
      * every sample: CHCR0 one of a DTR's (0x852C1 / 0x852C3 for R/W 0, 0x853C1 / 0x853C3 for R/W 1: SZ 32 B, MD 01,
        TE by the unit's end), DMATCR0 0 or 1 (COUNT 1), MDST 0 / 1, SAR0 and DAR0 32-byte aligned system RAM, SAR0 =
        MDSTAR + 0x20 x k (the list fetched in 32-byte units from MDSTAR, k 1..8), times increasing;
      * SAR0 moves only in samples of an R/W 0 DTR, DAR0 only in samples of an R/W 1 DTR;
      * each DMA (a sample with MDST 1 after one with MDST 0) opens with the list fetch (R/W 0) before any response: its
        first change of SAR0 / DAR0 / CHCR0 is an R/W 0 DTR's (MDST reads 1 from the CPU's write on, and the list
        fetch's DTR lands a unit later: on the chip ~170 Icyc, one sample in 11 DMAs; the console's samples never fell
        in that window);
      * at least 3 DMAs, and a multi-unit response (DAR0 + 0x20 from one sample to the next within a DMA)
    returns a list of failure strings"""
    sm, bad = s1_samples(lines)
    if sm is None:
        return [f'S1 sample line does not parse: {bad!r}']
    why = []
    if not nmin <= len(sm) <= nmax:
        why.append(f'S1: {len(sm)} samples, want {nmin}..{nmax}')
    for i, (t, sa, da, tc, ch, md) in enumerate(sm):
        if ch not in S1_CHCR0 or tc > 1 or md > 1 or sa & 31 or da & 31 or not 0x0C000000 <= sa < 0x0D000000 or \
                not 0x0C000000 <= da < 0x0D000000:
            why.append(f'S1 sample {i}: CHCR0 {ch:08x} DMATCR0 {tc} MDST {md} SAR0 {sa:08x} DAR0 {da:08x} is no DTR state')
        elif not any(sa == mdstar + 0x20 * k for k in range(1, 9)):
            why.append(f'S1 sample {i}: SAR0 {sa:08x} is not MDSTAR {mdstar:08x} + 32 k (the list fetch)')
        if i and t <= sm[i - 1][0]:
            why.append(f'S1 sample {i}: time {t} not after {sm[i - 1][0]}')
    ndma, multi, opening = 0, False, None
    for i in range(1, len(sm)):
        p, q = sm[i - 1], sm[i]
        rw = (q[4] >> 8) & 1
        if q[1] != p[1] and rw != 0:
            why.append(f'S1 sample {i}: SAR0 moved with an R/W 1 CHCR0 {q[4]:08x}')
        if q[2] != p[2] and rw != 1:
            why.append(f'S1 sample {i}: DAR0 moved with an R/W 0 CHCR0 {q[4]:08x}')
        if q[5] == 1 and p[5] == 0:
            ndma += 1
            opening = (p[1], p[2], p[4])                 # the registers before the DMA's first DTR
        if opening is not None and q[5] == 1 and (q[1], q[2], q[4]) != opening:
            if rw != 0:
                why.append(f'S1 sample {i}: a DMA opens with CHCR0 {q[4]:08x}, not the list fetch (R/W 0)')
            opening = None
        if q[5] == 1 and p[5] == 1 and q[2] == p[2] + 0x20:
            multi = True
    if ndma < 3:
        why.append(f'S1: {ndma} maple DMAs seen, want >= 3')
    if not multi:
        why.append('S1: no multi-unit response (DAR0 + 0x20 within a DMA)')
    return why[:12]


def check(lines, doc, classes):
    """lines: the program's output lines (first .. last); returns a list of failure strings"""
    fails = []
    rules = doc['lines']
    nwant = sum(1 for r in rules if 'multi' not in r)
    nmulti = sum(1 for ln in lines if any('multi' in r and ln.startswith(r['multi']) for r in rules))
    if len(lines) - nmulti != nwant:
        fails.append(f'{len(lines) - nmulti} lines printed, {nwant} expected')
    env = {}
    k, li = 0, 0
    while k < len(rules) and li < len(lines):
        r, ln = rules[k], lines[li]
        cls = r['cls']
        k += 1
        if 'multi' in r:                             # dmac1 S1's samples: every consecutive line with the prefix
            j = li
            while j < len(lines) and lines[j].startswith(r['multi']):
                j += 1
            if 'functional' in classes:
                for f in s1_check(lines[li:j], env.get('x_mdstar', 0), r['nmin'], r['nmax']):
                    fails.append(f'line {li + 1}: {f}')
            li = j
            continue
        li += 1
        if cls == 'group':
            continue
        sel = cls in classes or cls == 'env' or (cls == 'functional' and 'functional' in classes)
        if not sel and not (cls == 'functional' and 'timing' in classes):
            continue
        m = re.fullmatch(r['regex'], ln)
        if not m:
            fails.append(f'line {li}: {ln!r} does not match; console {r["console"]!r}')
            continue
        vals = dict(env)
        for n, v in m.groupdict().items():
            vals[n] = conv(n, v)
        for d in r.get('defines', []):
            env[d] = vals[d]
        if cls == 'env':
            continue
        if cls in classes:
            for e in r.get('func', []):
                try:
                    ok = eval(e, {}, vals)
                except Exception as x:                 # noqa: BLE001 -- report, do not crash
                    ok, e = False, f'{e} ({x})'
                if not ok:
                    fails.append(f'line {li}: {e} is false in {ln!r}')
            # (M6) board-preset tokens: each allowed value named with its reason (mkexpected.py --kos)
            for g, allowed, _why in r.get('board', []):
                if vals.get(g) not in allowed:
                    fails.append(f'line {li}: {g} = {vals.get(g)!r} is none of the allowed {allowed} ({_why})')
            if 'group' in r and r['group']['kind'] == 's10':
                n = r['group']['lines']
                err = s10_check(r['group']['mode'], samples(lines[li:li + n]), vals.get('started', 0))
                if err:
                    fails.append(f'line {li}: S10 {r["group"]["mode"]}: {err}')
        if 'timing' in classes:
            for e, want, tol in r.get('timing', []):
                got = eval(e, {}, vals)
                if abs(got - want) > tol:
                    fails.append(f'line {li}: {e} = {got}, console {want} (tolerance {tol}) in {ln!r}')
        # (M6) SOC.md 9.25's open timing tokens: not gated by the console's tolerance, bounded instead; reported
        if 'doc' in classes:
            for e, lo, hi, _why in r.get('doc', []):
                got = eval(e, {}, vals)
                if not lo <= got <= hi:
                    fails.append(f'line {li}: {e} = {got} outside its documented bound {lo}..{hi} ({_why}) in {ln!r}')
                else:
                    DOC_SEEN.append(f'line {li}: {e} = {got} (console {r.get("doc_console", {}).get(e, "?")}; bound '
                                    f'{lo}..{hi}: {_why})')
    return fails


DOC_SEEN = []


def extract(text, doc):
    ls = [l.rstrip() for l in text.splitlines()]
    try:
        i = next(k for k, l in enumerate(ls) if l.startswith(doc['first']))
        j = next(k for k in range(i, len(ls)) if ls[k].startswith(doc['last']))
    except StopIteration:
        return None
    return [l for l in ls[i:j + 1] if l.strip()]


def selftest(doc):
    text = open(os.path.join(ROOT, doc['source']), encoding='latin-1').read()
    lines = extract(text, doc)
    # the console log carries the sections the bare-metal build leaves out: keep only the rule lines, in order
    keep, k = [], 0
    for l in lines:
        if k < len(doc['lines']) and l == doc['lines'][k]['console']:
            keep.append(l)
            k += 1
    allc = {'functional', 'timing', 'cache'}
    fails = check(keep, doc, allc)
    if fails:
        return ['the console log fails its own rules: ' + f for f in fails[:10]]
    errs, n = [], 0
    for idx, r in enumerate(doc['lines']):
        if r['cls'] in ('env', 'group'):
            continue
        ln = r['console']
        # 1. one digit of the fixed text (outside every group) changed
        m = re.fullmatch(r['regex'], ln)
        spans = [m.span(g) for g in m.groupdict()]
        pos = next((p for p, c in enumerate(ln) if c.isalnum() and not any(a <= p < b for a, b in spans)
                    and p > 3), None)
        if pos is not None:
            c = ln[pos]
            nc = '1' if c == '0' else '0' if c.isdigit() else ('b' if c == 'a' else 'a')
            mut = keep[:idx] + [ln[:pos] + nc + ln[pos + 1:]] + keep[idx + 1:]
            n += 1
            if not check(mut, doc, allc):
                errs.append(f'not caught: fixed text changed at {pos} in {ln!r}')
        # 2. each relation broken: a group it uses moved by 7 / 8, or (a bound such as cyc > 0) set to -1 / 0
        for e in r.get('func', []):
            names = [g for g in m.groupdict() if re.search(r'\b' + g + r'\b', e)]
            if not names or names[0].startswith('f_'):
                continue
            g0 = names[0]
            a, b = m.span(g0)
            v = m.group(g0)
            if g0.startswith('x_'):
                cands = [f'{(int(v, 16) + 8) & 0xFFFFFFFF:0{len(v)}x}', f'{0:0{len(v)}x}']
            else:
                cands = [str(int(v) + 7), '-1', '0']
            n += 1
            if not any(check(keep[:idx] + [ln[:a] + nv + ln[b:]] + keep[idx + 1:], doc, allc) for nv in cands):
                errs.append(f'not caught: {g0} {v} -> {cands} ({e}) in {ln!r}')
        # 3. a timing target moved outside its tolerance
        for e, want, tol in r.get('timing', []):
            if e not in m.groupdict():
                continue
            a, b = m.span(e)
            nv = str(int(m.group(e)) + tol + 1)
            mut = keep[:idx] + [ln[:a] + nv + ln[b:]] + keep[idx + 1:]
            n += 1
            if not check(mut, doc, allc):
                errs.append(f'not caught: timing {e} + {tol + 1} in {ln!r}')
        # 4. the S10 structure broken the way a wrong arbiter would: channel 3 keeps running beside a fixed-priority
        # channel 1 (PR=10), channel 1 runs beside channel 3 (PR=01), channel 3 frozen in round robin (PR=11)
        if 'group' in r and r['group']['mode'] != 'burst':
            g, mode = r['group']['lines'], r['group']['mode']
            def pert(q, mode=mode):
                t, a, b = int(q.group(1)), int(q.group(2)), int(q.group(3))
                if mode == 'fixed_ch1_first':
                    b = max(0, b - t // 100)
                elif mode == 'fixed_ch3_first':
                    a = max(1, 1024 - t // 3000) if t > 3000 else a
                else:
                    b = 8000
                return f'{t}:{a}/{b}'
            sw = [re.sub(r'(\d+):(\d+)/(\d+)', pert, l) for l in keep[idx + 1:idx + 1 + g]]
            mut = keep[:idx + 1] + sw + keep[idx + 1 + g:]
            n += 1
            if not check(mut, doc, allc):
                errs.append(f'not caught: S10 {mode} with a broken priority structure')
    # 5. a line dropped
    n += 1
    if not check(keep[:3] + keep[4:], doc, allc):
        errs.append('not caught: a dropped line')
    return errs + [f'{n} perturbations tried, {n - len(errs)} caught']


def selftest_kos(doc):
    """(M6) a --kos rule file: the console log (every line) passes every class, and every perturbation is caught: a digit
    of each line's fixed text, each relation, each timing target and documented bound, each board token set to a value
    none of its reasons allows, S1's structure broken four ways, a line dropped"""
    text = open(os.path.join(ROOT, doc['source']), encoding='latin-1').read()
    keep = extract(text, doc)
    allc = {'functional', 'timing', 'cache', 'doc'}
    fails = check(keep, doc, allc)
    if fails:
        return ['the console log fails its own rules: ' + f for f in fails[:10]]
    # rule index -> line index
    at, li = [], 0
    for r in doc['lines']:
        at.append(li)
        if 'multi' in r:
            while li < len(keep) and keep[li].startswith(r['multi']):
                li += 1
        else:
            li += 1
    errs, n = [], 0

    def caught(mut):
        return bool(check(mut, doc, allc))
    for idx, r in enumerate(doc['lines']):
        li = at[idx]
        if 'multi' in r:
            j = li
            while j < len(keep) and keep[j].startswith(r['multi']):
                j += 1
            sm = keep[li:j]
            resp = next(k for k, l in enumerate(sm) if ' CHCR0 000853c3 MDST 1' in l)
            muts = {'a CHCR0 no DTR loads': [sm[0].replace('CHCR0 000853c3', 'CHCR0 000854c3').replace('CHCR0 000852c3', 'CHCR0 000850c3')] + sm[1:],
                    'SAR0 off the list': [re.sub(r'SAR0 ([0-9a-f]{8})', lambda m: f'SAR0 {int(m.group(1), 16) + 0x400:08x}', sm[0])] + sm[1:],
                    'a DMA opening with a response': [l.replace('CHCR0 000852c3 MDST 1', 'CHCR0 000853c3 MDST 1') for l in sm],
                    'DMATCR0 2': sm[:resp] + [sm[resp].replace('DMATCR0 00000000', 'DMATCR0 00000002')] + sm[resp + 1:],
                    'half the samples': sm[:len(sm) // 3]}
            for nm, ms in muts.items():
                n += 1
                if not caught(keep[:li] + ms + keep[j:]):
                    errs.append(f'not caught: S1 {nm}')
            continue
        if r['cls'] == 'group':
            continue
        ln = keep[li]
        m = re.fullmatch(r['regex'], ln)
        spans = [m.span(g) for g in m.groupdict()]
        pos = next((p for p, c in enumerate(ln) if c.isalnum() and not any(a <= p < b for a, b in spans) and p > 3), None)
        if pos is not None:
            c = ln[pos]
            nc = '1' if c == '0' else '0' if c.isdigit() else ('b' if c == 'a' else 'a')
            n += 1
            if not caught(keep[:li] + [ln[:pos] + nc + ln[pos + 1:]] + keep[li + 1:]):
                errs.append(f'not caught: fixed text changed at {pos} in {ln!r}')
        for e in r.get('func', []):
            names = [g for g in m.groupdict() if re.search(r'\b' + g + r'\b', e)]
            if not names or names[0].startswith('f_'):
                continue
            g0 = names[0]; a, b = m.span(g0); v = m.group(g0)
            cands = ([f'{(int(v, 16) + 8) & 0xFFFFFFFF:0{len(v)}x}', f'{0:0{len(v)}x}'] if g0.startswith('x_')
                     else [str(int(v) + 7), '-1', '0'])
            n += 1
            if not any(caught(keep[:li] + [ln[:a] + nv + ln[b:]] + keep[li + 1:]) for nv in cands):
                errs.append(f'not caught: {g0} {v} -> {cands} ({e}) in {ln!r}')
        for g0, allowed, _why in r.get('board', []):
            a, b = m.span(g0); v = m.group(g0)
            nv = next(f'{x:0{len(v)}x}' for x in (0x5A5A5A5A, 0x12345678, 0x0BADF00D) if x not in allowed)
            n += 1
            if not caught(keep[:li] + [ln[:a] + nv[-len(v):] + ln[b:]] + keep[li + 1:]):
                errs.append(f'not caught: board token {g0} {v} -> {nv} in {ln!r}')
        for e, want, tol in r.get('timing', []):
            if e not in m.groupdict():
                continue
            a, b = m.span(e)
            n += 1
            if not caught(keep[:li] + [ln[:a] + str(int(m.group(e)) + tol + 1) + ln[b:]] + keep[li + 1:]):
                errs.append(f'not caught: timing {e} + {tol + 1} in {ln!r}')
        for e, lo, hi, _why in r.get('doc', []):
            if e not in m.groupdict():
                continue
            a, b = m.span(e)
            n += 1
            if not caught(keep[:li] + [ln[:a] + str(hi + 1) + ln[b:]] + keep[li + 1:]):
                errs.append(f'not caught: documented {e} above its bound {hi} in {ln!r}')
    n += 1
    if not caught(keep[:3] + keep[4:]):
        errs.append('not caught: a dropped line')
    return errs + [f'{n} perturbations tried, {n - len(errs)} caught']


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    classes = {'functional'}
    for i, a in enumerate(sys.argv):
        if a == '--classes' and i + 1 < len(sys.argv):
            classes = set(sys.argv[i + 1].split(','))
            args = [x for x in args if x != sys.argv[i + 1]]
    if '--self' in sys.argv:
        doc = json.load(open(args[0]))
        res = selftest_kos(doc) if doc.get('kos') else selftest(doc)
        bad = [x for x in res if x.startswith('not caught') or x.startswith('the console')]
        for x in res:
            print(f'{doc["program"]} self: {x}')
        print(f'{doc["program"]} self: {"FAIL" if bad else "PASS"}')
        return 1 if bad else 0
    if len(args) < 2:
        print('usage: check.py LOG expected/<prog>.json [--classes functional,timing,cache] | --self expected/<prog>.json')
        return 2
    doc = json.load(open(args[1]))
    lines = extract(open(args[0], encoding='latin-1').read(), doc)
    if lines is None:
        print(f'{doc["program"]}: no output ("{doc["first"]}" ... "{doc["last"]}" not found)')
        return 1
    fails = check(lines, doc, classes)
    for f in fails[:40]:
        print(f'{doc["program"]}: FAIL {f}')
    for d in DOC_SEEN:                                   # (M6) SOC.md 9.25's rows, inside their bounds
        print(f'{doc["program"]}: documented {d}')
    nchk = sum(1 for r in doc['lines'] if r['cls'] in classes)
    print(f'{doc["program"]}: {nchk} lines of class {",".join(sorted(classes))}, {len(fails)} failures: '
          f'{"FAIL" if fails else "PASS"}')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
