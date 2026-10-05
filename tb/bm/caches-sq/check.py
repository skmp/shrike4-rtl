#!/usr/bin/env python3
"""check.py -- compare a caches-sq bare-metal run's console output with expected/<prog>.json (mkexpected.py).  WI-CACHE.

  check.py <output log> <expected.json> [--classes functional,timing] [--quiet]
  check.py --self <expected.json> [--classes ...]     the console's own lines pass, and every perturbation (one
                                                       compared token changed, one line dropped) is caught
  check.py <output log> <expected.json> --m3 --kos    (M5, WI-SOC M5-fix, M5-verify F5) the console's own ELF unmodified
                                                       under KOS (soc_tb --dcload): as --m3, but a T row that KOS_DOC
                                                       lists (below: M3's open rows and the refresh-phase rows) and misses
                                                       is reported "documented" when only its totals differ, each within
                                                       the entry's bound; every other row, and the functional class,
                                                       must pass
  --m3   (M3, WI-SOC; SOC.md 8.3 "kern.S bodies exact where integral") the T rows by their totals: the cycle count
         and every event count per unit exact (within 0.03, the other suites' near-integer rule) where the console's
         per-unit value is integral; otherwise the total within M3_PHASE = 2 Icyc of the console's -- the timed window
         opens and closes at an Iclk PMCTR read, so each edge may meet the first / last bus cycle one Icyc later on the
         other machine (Bclk = 2 Icyc) --; c/u is derived from cyc and not compared on its own.  With --self, every
         perturbation is a total moved out of that band

Each expected line is found in the output by its shape and occurrence (mkexpected.py).  Tokens are compared after the
tag isomorphism: in each log, over the selected classes' compared tokens in line order, the k-th distinct tag
(PA[28:10] of a word with bits 9:2 = 0 in P0 / P1 / P2 form) becomes "T<k>" (keeping the segment and the low bits),
so a different link layout gives the same text.  Exit 0 when every selected line is present and equal.
"""
import json
import re
import sys
sys.dont_write_bytecode = True                                     # no __pycache__ in the source tree
sys.path.insert(0, __file__.rsplit('/', 1)[0] if '/' in __file__ else '.')
from mkexpected import shape_of, split_tok, HEX8  # noqa: E402


def tagkey(tok):
    if not HEX8.match(tok):
        return None
    v = int(tok, 16)
    seg = v >> 29
    t = v & 0x1FFFFC00
    if seg not in (0, 4, 5) or t == 0 or (v & 0x3FC) != 0:
        return None
    return t, seg, v & 0x3FF


def normalize(pairs, classes):
    """pairs: [(entry, tokens)] in log order -> {id(tokens): normalized tokens}"""
    tags = {}
    out = []
    for e, toks in pairs:
        n = []
        for i, t in enumerate(toks):
            h, val, tl = split_tok(t)
            k = tagkey(val) if val else None
            if k and e['class'] in classes and i not in e['var'] and i not in e['ignore']:
                if k[0] not in tags:
                    tags[k[0]] = 'T%d' % len(tags)
                val = '%s/%d+%03x' % (tags[k[0]], k[1], k[2])
            elif k:
                val = '?/%d+%03x' % (k[1], k[2])
            n.append(h + val + tl if val else t)
        out.append(n)
    return out


def match(out_lines, doc, classes):
    """[(entry, output tokens or None)] for the selected entries"""
    idx = {}
    for l in out_lines:
        sh = shape_of(l)
        idx.setdefault(sh, []).append(l.split())
    res = []
    for e in doc['lines']:
        if e['class'] not in classes:
            continue
        cand = idx.get(e['shape'], [])
        res.append((e, cand[e['occ']] if e['occ'] < len(cand) else None))
    return res


M3_PHASE = 2                                                       # Icyc: the window's two Bclk-phase edges


def ev_total(v, n):
    """the count a per-unit value v (printed floor(t / n) '.' floor((t % n) * 100 / n)) stands for"""
    lo = max(int(float(v) * n) - 2, 0)
    for t in range(lo, lo + 6):
        if '%d.%02d' % (t // n, (t % n) * 100 // n) == v:
            return t
    return None


def t_fields(toks):
    """[(name, value string, index of the value token)] of a T line's name=value fields ('n=  64' or 'n=128')"""
    out, i = [], 0
    while i < len(toks):
        t = toks[i]
        if '=' in t:
            k, v = t.split('=', 1)
            if v == '' and i + 1 < len(toks):
                out.append((k, toks[i + 1], i + 1))
                i += 2
                continue
            out.append((k, v, i))
        i += 1
    return out


def t_totals(toks):
    """n and {name: total} of a T line: 'cyc' and every event (c/u is derived from cyc: not a total)"""
    f = {k: v for k, v, _ in t_fields(toks)}
    n = int(f['n'])
    tot = {'cyc': int(f['cyc'])}
    for k, v in f.items():
        if k not in ('n', 'cyc', 'c/u'):
            tot[k] = ev_total(v, n)
    return n, tot, None


def m3_diffs(got, want):
    """the M3 rule on one T line (see the usage): a list of 'name: expected->got' differences"""
    gn, gt, _ = t_totals(got)
    wn, wt, _ = t_totals(want)
    gf, wf = t_fields(got), t_fields(want)
    gw = [t for i, t in enumerate(got) if '=' not in t and i not in {j for _, _, j in gf}]
    ww = [t for i, t in enumerate(want) if '=' not in t and i not in {j for _, _, j in wf}]
    diffs = ['%s->%s' % (b, a) for a, b in zip(gw, ww) if a != b]
    if gn != wn or len(gw) != len(ww) or [k for k, _, _ in gf] != [k for k, _, _ in wf]:
        return diffs + ['shape %s->%s' % (' '.join(want), ' '.join(got))]
    for k, w in wt.items():
        g = gt.get(k)
        if g is None or w is None:
            diffs.append('%s: %s->%s' % (k, w, g))
            continue
        u = w / wn
        if abs(u - round(u)) <= 0.03:
            ok = abs(g / gn - round(u)) <= 0.03
        else:
            ok = abs(g - w) <= M3_PHASE
        if not ok:
            diffs.append('%s: %d->%d (%+d)' % (k, w, g, g - w))
    return diffs


def m3_perturbed(toks):
    """every T-line perturbation that the M3 rule must catch: each total moved by max(M3_PHASE + 1, 0.04 n)"""
    n, tot, _ = t_totals(toks)
    d = max(M3_PHASE + 1, -(-4 * n // 100))
    out = []
    for k, _, i in t_fields(toks):
        if k not in tot or tot[k] is None:
            continue
        v = tot[k] + d
        c = list(toks)
        s = str(v) if k == 'cyc' else '%d.%02d' % (v // n, (v % n) * 100 // n)
        c[i] = (k + '=' + s) if c[i].startswith(k + '=') else s
        out.append(c)
    return out


M3 = False
KOS = False

# (M5-fix, --kos) the T rows the unmodified probes may miss under KOS, by (program, row name: the text between 'T ' and
# 'n='), with a bound on every total of the row: 'x2' -- console / 2 .. 2 x console (a sanity bound: M3's open rows
# differ by at most 128 Icyc / 64 events, frzoc 200 -> 136 the largest ratio); 'r24' -- within 24 Icyc, one SDRAM refresh
# collision [HW bsc-memmap 6.2].  M3's open rows are the rows v1-soc-m3 reports (SOC.md 9.23 / 9.24: the caches' own
# timing, where M3 is partial; the lists below are M3's failing set on the bare-metal ports, 2026-09-25); the refresh-
# phase rows pass at M3 and miss by 3 Icyc under KOS, whose interrupts and print path set the refresh phase at the
# timed window (M5 gate run).  A new cache-timing miss under KOS is not in the list and fails
_M3OPEN = {
    'cachetest': ('WT ststream hit s4', 'chain miss s32 w0 dirtyV', 'icB bra/line P2', 'icB bra/line cold',
                  'icC bra@24 P2', 'icC bra@24 cold', 'ldgap16 miss dirtyV', 'ldgap32 miss', 'ldgap32 miss dirtyV',
                  'ldgap4 miss dirtyV', 'ldgap64 miss', 'ldgap8 miss', 'ldgap8 miss dirtyV', 'ldstream miss dirtyV',
                  'prefld d16 miss', 'prefld d40 miss', 'prefld d48 miss', 'prefld d56 miss', 'prefstream miss',
                  'prefstream miss dirtyV', 'sq SQ0 gap1', 'sq SQ0 gap2', 'sq SQ0 gap4', 'sq SQ0 only',
                  'sq SQ0/SQ1 alternate', 'stgap16 miss', 'stgap64 miss', 'stgap8 miss', 'ststream P2 s4',
                  'ststream P2 s8', 'ststream miss', 'ststream miss dirtyV'),
    'cachetest2': ('ld2gap7 miss', 'ld2gap8 miss', 'ldsto miss', 'stldgap8 miss', 'ststream miss s64'),
    'cachetest3': ('ocbpgap2 dirty', 'ocbwbgap1 dirty', 'ocbwbgap2 dirty', 'sq SQ0 only (ref)', 'sqld hit', 'sqld miss'),
}
KOS_DOC = {(p, r): ('x2', "M3's open row (SOC.md 9.23 / 9.24; v1-soc-m3 reports it)") for p, rs in _M3OPEN.items()
           for r in rs}
KOS_DOC.update({('cachetest', 'ldgap64 miss dirtyV'): ('r24', 'refresh phase under KOS (M5 gate: cyc 13943 -> 13940)'),
                ('cachetest', 'sq pref0 st0'): ('r24', 'refresh phase under KOS (M5 gate: cyc 1167 -> 1164)')})


def t_name(text):
    """a T line's row name: the text between 'T ' and 'n='"""
    return text[2:].split(' n=')[0].strip()


def kos_bound(kind, got, want):
    """None when every total of the T line lies within the KOS_DOC bound, else the first that does not"""
    gn, gt, _ = t_totals(got)
    wn, wt, _ = t_totals(want)
    if gn != wn or set(gt) != set(wt):
        return 'shape'
    for k, w in wt.items():
        g = gt[k]
        if g is None or w is None:
            return '%s: %s->%s' % (k, w, g)
        ok = (w / 2 <= g <= 2 * w) if kind == 'x2' else abs(g - w) <= 24
        if not ok:
            return '%s: %d->%d outside %s' % (k, w, g, kind)
    return None


documented = []                                                   # (--kos) the KOS_DOC rows that missed


def compare(out_lines, doc, classes, quiet, report=True):
    pairs = match(out_lines, doc, classes)
    want = normalize([(e, e['text'].split()) for e, _ in pairs], classes)
    got = normalize([(e, g) for e, g in pairs if g is not None], classes)
    gi = iter(got)
    ok = bad = miss = varn = 0
    for (e, g), w in zip(pairs, want):
        if g is None:
            miss += 1
            if report:
                print('MISSING  [%s] %s' % (e['class'], e['text']))
            continue
        gn = next(gi)
        diffs, vd = [], []
        if M3 and e['class'] == 'timing' and e['text'].startswith('T '):
            diffs = m3_diffs(g, e['text'].split())
            kd = KOS_DOC.get((doc['prog'], t_name(e['text']))) if KOS and diffs else None
            if kd is not None and not any(d.startswith('shape') or ':' not in d for d in diffs):
                why = kos_bound(kd[0], g, e['text'].split())
                if why is None:
                    documented.append('%s -- %s: %s' % (t_name(e['text']), kd[1], ' '.join(diffs)))
                    ok += 1
                    continue
                diffs.append('[KOS_DOC bound: %s]' % why)
            if diffs:
                bad += 1
                if report:
                    print('MISMATCH [%s] %s\n         got: %s\n         diff (expected->got): %s' %
                          (e['class'], e['text'], ' '.join(g), ' '.join(diffs)))
            else:
                ok += 1
            continue
        for i in range(max(len(gn), len(w))):
            a = gn[i] if i < len(gn) else '<none>'
            b = w[i] if i < len(w) else '<none>'
            if a == b or i in e['ignore']:
                continue
            (vd if i in e['var'] else diffs).append('%s->%s' % (b, a))
        if diffs:
            bad += 1
            if report:
                print('MISMATCH [%s] %s\n         got: %s\n         diff (expected->got): %s' %
                      (e['class'], e['text'], ' '.join(g), ' '.join(diffs)))
        else:
            ok += 1
            if vd:
                varn += 1
                if report and not quiet:
                    print('variable [%s] %s: %s' % (e['class'], e['text'], ' '.join(vd)))
    return ok, bad, miss, varn


def perturb(tok):
    h, v, tl = split_tok(tok)
    if v:
        return h + perturb_v(v) + tl
    return perturb_v(tok)


def perturb_v(tok):
    for i in range(len(tok) - 1, -1, -1):
        c = tok[i]
        if c.isdigit():
            return tok[:i] + ('1' if c != '1' else '2') + tok[i + 1:]
        if c in 'abcdef':
            return tok[:i] + ('0' if c != '0' else '1') + tok[i + 1:]
    return tok + 'x'


def main():
    args = sys.argv[1:]
    classes = ['functional', 'timing']
    quiet = '--quiet' in args
    global M3, KOS
    M3 = '--m3' in args
    KOS = '--kos' in args
    args = [a for a in args if a not in ('--m3', '--kos')]
    if '--classes' in args:
        classes = args[args.index('--classes') + 1].split(',')
    if args[0] == '--self':
        doc = json.load(open(args[1]))
        lines = [e['text'] for e in doc['lines']]
        ok, bad, miss, _ = compare(lines, doc, classes, True, False)
        if bad or miss:
            sys.exit('self-test: the console log itself does not pass (%d mismatches, %d missing)' % (bad, miss))
        sel = [i for i, e in enumerate(doc['lines']) if e['class'] in classes]
        caught = total = 0
        for i in sel:
            e = doc['lines'][i]
            toks = e['text'].split()
            if M3 and e['class'] == 'timing' and e['text'].startswith('T '):
                for c in m3_perturbed(toks):
                    pl = list(lines)
                    pl[i] = ' '.join(c)
                    total += 1
                    _, b2, m2, _ = compare(pl, doc, classes, True, False)
                    caught += bool(b2 or m2)
                pl = lines[:i] + lines[i + 1:]
                total += 1
                _, b2, m2, _ = compare(pl, doc, classes, True, False)
                caught += bool(b2 or m2)
                continue
            for j, t in enumerate(toks):
                if j in e['var'] or j in e['ignore'] or not split_tok(t)[1]:
                    continue
                pl = list(lines)
                toks2 = list(toks)
                toks2[j] = perturb(t)
                pl[i] = ' '.join(toks2)
                total += 1
                _, b2, m2, _ = compare(pl, doc, classes, True, False)
                caught += bool(b2 or m2)
            pl = lines[:i] + lines[i + 1:]
            total += 1
            _, b2, m2, _ = compare(pl, doc, classes, True, False)
            caught += bool(b2 or m2)
        print('%s: self-test %d / %d perturbations caught (%d lines of %s)' % (doc['prog'], caught, total, len(sel),
                                                                               ','.join(classes)))
        sys.exit(0 if caught == total else 1)
    out = [l.rstrip() for l in open(args[0], errors='replace').read().splitlines() if l.strip()]
    doc = json.load(open(args[1]))
    if KOS:                                            # every entry names a timing T row of its program
        for (p, r), _ in KOS_DOC.items():
            if p == doc['prog'] and not any(e['class'] == 'timing' and e['text'].startswith('T ') and
                                            t_name(e['text']) == r for e in doc['lines']):
                sys.exit('check.py --kos: KOS_DOC %s %r is not a timing T row' % (p, r))
    ok, bad, miss, varn = compare(out, doc, classes, quiet)
    for x in documented:
        print('documented (--kos, KOS_DOC): ' + x)
    print('%s: %d equal, %d mismatched, %d missing, %d with variable tokens (classes %s)%s' %
          (doc['prog'], ok, bad, miss, varn, ','.join(classes),
           ', %d documented (KOS_DOC)' % len(documented) if KOS else ''))
    sys.exit(0 if not bad and not miss else 1)


if __name__ == '__main__':
    main()
