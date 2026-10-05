#!/usr/bin/env python3
"""Compare rtl/timing-proxy results (tb/timing_proxy_tb.cpp output) with the measured sweep and cluster the misses.

usage: timing_proxy_report.py [build/timing_proxy] [--all] [--top 40] [--metric T] [--form SUBSTR] [--dump FILE]
  reads <dir>/vectors.bin, <dir>/records.jsonl, <dir>/results_*.bin
  --all      also compare the out-of-scope form (TAS.B)
  --dump     write every mismatching (record, n, metric) as jsonl
Scope is per (point, metric) (tb/timing_proxy_vectors.py): a value is compared when the console measured it
deterministically; the dropped ones are counted per metric and reason.  Prints the agreement per metric for everything
in scope, then for its two parts: the cache-block forms (OCBI, OCBP, OCBWB, MOVCA.L, scoped by determinism) and the
rest of the ISA; per-record agreement (a record = one (A, B, mode, variant) curve; it matches when every compared point
of every metric matches); and the largest mismatch clusters by (A, B, variant kind, metric, model - measured by n).
"""
import collections
import glob
import json
import os
import struct
import sys

METRICS = ('T', 'Tb', 'par', 'brkp', 'frzreg', 'frzfpu')
WHY = {-1: 'not measured', -2: 'not integral', -3: 'passes disagree'}
F_OOS, F_CACHE = 8, 16


def load_vec(path):
    b = open(path, 'rb').read()
    assert b[:4] == b'TPV1'
    nrec, = struct.unpack_from('<I', b, 4)
    p = 8
    out = {}
    for _ in range(nrec):
        rid, mode, flags, nrest, nn = struct.unpack_from('<IBBBB', b, p)
        p += 8
        code = struct.unpack_from(f'<{2 + nrest}H', b, p)
        p += 2 * (2 + nrest)
        pts = {}
        for _ in range(nn):
            n, _pad, *v = struct.unpack_from('<BBhhhhhh', b, p)
            p += 14
            pts[n] = v
        out[rid] = {'mode': mode, 'flags': flags, 'code': code, 'pts': pts}
    return out


def load_res(paths):
    out = {}
    for path in paths:
        b = open(path, 'rb').read()
        for p in range(0, len(b), 18):
            rid, n, fl, *v = struct.unpack_from('<IBBhhhhhh', b, p)
            out[(rid, n)] = (fl, v)
    return out


def vkind(var):
    if var.startswith('ov:'):
        return 'ov'
    if var.startswith('mem:'):
        return 'mem'
    return var


class Tally:
    def __init__(self):
        self.ok = collections.Counter()
        self.tot = collections.Counter()
        self.drop = collections.Counter()
        self.rec_ok = 0
        self.rec_tot = 0

    def table(self, title):
        print(f'{title}: records {self.rec_ok} / {self.rec_tot} match on every compared point and metric')
        for name in METRICS:
            if not self.tot[name] and not any(self.drop[(name, w)] for w in WHY):
                continue
            pc = 100.0 * self.ok[name] / self.tot[name] if self.tot[name] else 100.0
            dr = ', '.join(f'{self.drop[(name, w)]} {WHY[w]}' for w in WHY if self.drop[(name, w)])
            print(f'  {name:7s} {self.ok[name]:8d} / {self.tot[name]:8d}  ({pc:7.3f} %)' +
                  (f'   dropped: {dr}' if dr else ''))


def main():
    args = sys.argv[1:]
    d = 'build/timing_proxy'
    if args and not args[0].startswith('--'):
        d = args.pop(0)
    opt = {'--all': False, '--top': 40, '--metric': None, '--form': None, '--dump': None}
    i = 0
    while i < len(args):
        a = args[i]
        if a == '--all':
            opt[a] = True
        elif a in opt:
            opt[a] = args[i + 1]
            i += 1
        i += 1
    top = int(opt['--top'])
    vec = load_vec(os.path.join(d, 'vectors.bin'))
    names = {}
    for line in open(os.path.join(d, 'records.jsonl')):
        m = json.loads(line)
        names[m['rid']] = m
    res = load_res(sorted(glob.glob(os.path.join(d, 'results_*.bin'))))
    groups = {'all': Tally(), 'rest': Tally(), 'cache': Tally(), 'oos': Tally()}
    clusters = collections.defaultdict(list)
    per_form = collections.Counter()
    nonper = 0
    dump = open(opt['--dump'], 'w') if opt['--dump'] else None
    missing = 0
    for rid, r in vec.items():
        nm = names[rid]
        oos = bool(r['flags'] & F_OOS)
        if oos and not opt['--all']:
            continue
        if opt['--form'] and opt['--form'] not in nm['a'] and opt['--form'] not in nm['b']:
            continue
        tl = [groups['oos']] if oos else [groups['all'], groups['cache' if r['flags'] & F_CACHE else 'rest']]
        good = True
        deltas = collections.defaultdict(dict)
        for n, meas in r['pts'].items():
            got = res.get((rid, n))
            if got is None:
                missing += 1
                good = False
                continue
            fl, mv = got
            if fl & 1:
                nonper += 1
            for k, name in enumerate(METRICS):
                if opt['--metric'] and name != opt['--metric']:
                    continue
                if meas[k] < 0:
                    for t in tl:
                        t.drop[(name, meas[k])] += 1
                    continue
                for t in tl:
                    t.tot[name] += 1
                if meas[k] == mv[k]:
                    for t in tl:
                        t.ok[name] += 1
                else:
                    good = False
                    deltas[name][n] = mv[k] - meas[k]
                    if dump:
                        dump.write(json.dumps({'rid': rid, 'a': nm['a'], 'b': nm['b'], 'mode': nm['mode'],
                                               'var': nm['var'], 'n': n, 'metric': name, 'meas': meas[k],
                                               'model': mv[k]}) + '\n')
        for t in tl:
            t.rec_tot += 1
            t.rec_ok += good
        for name, dl in deltas.items():
            sig = ' '.join(f'{n}:{dl[n]:+d}' for n in sorted(dl))
            clusters[(name, nm['a'], nm['b'], vkind(nm['var']), sig)].append((nm['mode'], nm['var'], rid))
            per_form[(name, nm['a'])] += 1
            per_form[(name, '  as B: ' + nm['b'])] += 1
    groups['all'].table('in scope (every form but TAS.B)')
    groups['rest'].table('  of which: the ISA without the cache-block forms')
    groups['cache'].table('  of which: OCBI / OCBP / OCBWB / MOVCA.L (formerly out of scope)')
    if opt['--all']:
        groups['oos'].table('out of scope: TAS.B')
    if missing or nonper:
        print(f'{missing} points without a result; {nonper} not periodic')
    print(f'\nlargest clusters (metric, A, B, variant kind, model - measured by n): {len(clusters)} clusters')
    for k, v in sorted(clusters.items(), key=lambda kv: -len(kv[1]))[:top]:
        name, a, b, vk, sig = k
        ex = v[0]
        print(f'  {len(v):5d}  {name:6s} {a:22s} | {b:22s} [{vk}] {sig[:70]}   e.g. {ex[0]} {ex[1]} rid {ex[2]}')
    print('\nforms with most mismatching curves:')
    for (name, f), c in per_form.most_common(top):
        print(f'  {c:6d}  {name:6s} {f}')


if __name__ == '__main__':
    main()
