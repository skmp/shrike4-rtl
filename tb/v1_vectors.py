#!/usr/bin/env python3
"""Test vectors for the v1 direct-run testbench (tb/v1_tb.cpp), and the report of its results.  Unit D of rtl/v1/ARCH.md.

v1 runs the sweep's tests the way the console ran them: the runner's own jit_enter / jit_exit, each case's JIT loop
    L: A ; add #1,r14 x n ; B ; add #1,r14 x DRAIN ; restores ; dt r13 ; bf/s L ; (sett|nop) ; -> jit_exit
its scratch memory, literal pool and register image, all rebuilt from model/timings2/tests/chunk_*.bin exactly as
runner/t2run.c does (parse_case, scratch_build, jit_build, pool_build, state_build).  So a vector is a chunk record as
the console read it -- with its own init / mem lists, which differ between the per-n records of a PC-relative or
jump-target B -- plus the values the console measured for it.

Expected values: the four passes (par, brk, frzreg, frzfpu) are joined by (A, B, mode, variant, code, T flag) and n,
the join of tb/timing_proxy_vectors.py (its rnd / consensus / taken are imported, so the scoping by determinism is the
same, point for point): the par pass was measured with its own case set, whose ids differ from tests/cases.jsonl, and a
key can span several chunk records (one per n).  Per (record, n): T (par-shape cycles / iteration), Tb (brk shape),
par / brkp (0x14 parallel issues, par / brk shape), frzreg (0x28), frzfpu (0x29).  A negative value is not compared:
-1 not measured, -2 not integral (the loop's time or count varied), -3 T only: the passes disagree.

Output (build/v1/): vectors.bin, little endian:
    "V1VC" u32 version (1), u32 nrec, u32 S, CODE, EXIT, SR, VBR, DRAIN       (the chunk header's layout; one for all)
    per record: u32 id (tests/cases.jsonl id), u32 tprid (the timing proxy's rid of this loop, for its --trace),
                u32 fpscr, u16 tfix, u8 align, u8 flags, u8 nn, u8 nrest, u8 ninit, u8 nmem,
                u16 code[2 + nrest], padded to 4 bytes,
                ninit x {u8 regid (gen.py REGID), u8 0, u16 0, u32 value}, nmem x {u8 space (0 scratch, 1 pool), u8 0,
                u16 offset, u32 value}  (the chunk's own 8-byte entries),
                nn x {u8 n, u8 0, i16 T, Tb, par, brkp, frzreg, frzfpu}
    flags: bit0 A is a taken branch, bit1 B is, bit2 T fix is SETT, bit3 out of scope (TAS.B, bus-locked: its time is
           the external bus's), bit4 a cache-block form (OCBI / OCBP / OCBWB / MOVCA.L; shown on their own line)
and records.jsonl: {id, tprid, a, b, mode, var} per record.

Report (after tb/v1_tb.cpp wrote <dir>/results_*.bin: per point u32 id, u8 n, u8 flags, i16 T, Tb, par, brkp, frzreg,
frzfpu; flags bit0 not periodic, bit1 functional mismatch against the ISS, bit2 a run did not halt / hit the cycle
limit / accessed memory outside the image, bit3 timing not run (--func-only), bit4 functional check not run
(--time-only)): agreement per metric like tb/timing_proxy_report.py, per-record agreement, functional totals, and the
largest mismatch clusters by (metric, A, B, variant kind, model - measured by n).

usage: v1_vectors.py [out_dir]                                            (default build/v1)
       v1_vectors.py report [dir] [--all] [--top 40] [--metric T] [--form SUBSTR] [--dump FILE]
"""
import collections
import glob
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
T2 = os.path.join(HERE, '..', 'model', 'timings2')
sys.path.insert(0, T2)
sys.path.insert(0, HERE)
import t2res                                    # noqa: E402
import timing_proxy_vectors as tpv              # noqa: E402  (the proxy's join rules, imported, not copied)

METRICS = tpv.METRICS                           # ('T', 'Tb', 'par', 'brkp', 'frzreg', 'frzfpu')
WHY = {-1: 'not measured', -2: 'not integral', -3: 'passes disagree'}
F_OOS, F_CACHE = 8, 16
R_NONPER, R_FUNC, R_NOHALT, R_NOTIME, R_NOFUNC = 1, 2, 4, 8, 16
R_PHASE = 32        # soc_tb --sweep (M3-fix, SOC.md 5): the timing matched only in a retry at another refresh phase


def read_chunks(tests):
    """every chunk record, parsed like runner/t2run.c parse_case; returns (layout, [record])"""
    lay, recs = None, []
    files = sorted(glob.glob(os.path.join(tests, 'chunk_*.bin')))
    assert files, f'no chunks in {tests}'
    for fn in files:
        b = open(fn, 'rb').read()
        magic, ver, ncase, S, CODE, EXIT, SR, VBR, DRAIN, _ = struct.unpack_from('<4sIIIIIIIII', b, 0)
        assert magic == b'T2CH' and ver == 1, fn
        l = (S, CODE, EXIT, SR, VBR, DRAIN)
        # the testbench loads one runner image: every chunk must have been generated for the same layout
        assert lay is None or lay == l, f'{fn}: layout {l} differs from {lay}'
        lay = l
        p = 40
        for _ in range(ncase):
            ln, = struct.unpack_from('<H', b, p)
            q = p + 4
            cid, fpscr, tfix, align, nn = struct.unpack_from('<IIHBB', b, q)
            q += 12
            ns = list(b[q:q + nn])
            q += (nn + 3) & ~3
            nrest, ninit, nmem = b[q], b[q + 1], b[q + 2]
            q += 4
            code = list(struct.unpack_from(f'<{2 + nrest}H', b, q))
            q += ((2 + nrest) * 2 + 3) & ~3
            init = b[q:q + 8 * ninit]
            q += 8 * ninit
            mem = b[q:q + 8 * nmem]
            q += 8 * nmem
            assert q == p + ln, f'{fn}: case {cid} length {ln} but parsed {q - p}'
            p += ln
            recs.append({'id': cid, 'fpscr': fpscr, 'tfix': tfix, 'align': align, 'ns': ns, 'code': code,
                         'ninit': ninit, 'init': init, 'nmem': nmem, 'mem': mem})
        assert p == len(b), f'{fn}: {len(b) - p} trailing bytes'
    return lay, recs


def build(out):
    os.makedirs(out, exist_ok=True)
    lay, recs = read_chunks(os.path.join(T2, 'tests'))
    meta = {}
    for line in open(os.path.join(T2, 'tests', 'cases.jsonl')):
        d = json.loads(line)
        meta[d['id']] = d
    # the four passes, keyed like tb/timing_proxy_vectors.py (status != 0: the console recorded an exception)
    passes = {}
    for p in ('par', 'brk', 'frzreg', 'frzfpu'):
        d = {}
        for r in t2res.load(p).values():
            if r['status']:
                continue
            m = r['meta']
            key = (m['a'], m['b'], m['mode'], m['var'], tuple(m['code']), m['T'])
            d.setdefault(key, {}).update(r['pts'])
        passes[p] = d
        print(f'{p}: {len(d)} keys', file=sys.stderr)
    # the proxy's record numbering (its vectors.bin order), so a v1 loop can be traced in the proxy with --rid
    pkeys = sorted(set(passes['par']) | set(passes['frzreg']) | set(passes['frzfpu']) | set(passes['brk']),
                   key=lambda k: (k[2], k[0], k[1], k[3], k[4]))
    tprid = {k: i for i, k in enumerate(pkeys)}
    blob = bytearray()
    names = []
    npts = 0
    dropped = collections.Counter()
    nokey = 0
    for r in recs:
        m = meta[r['id']]
        assert [int(w, 16) for w in m['code']] == r['code'], f'case {r["id"]}: cases.jsonl code differs from the chunk'
        assert m['ns'] == r['ns'], f'case {r["id"]}: ns differ'
        a, b = m['a'], m['b']
        key = (a, b, m['mode'], m['var'], tuple(m['code']), m['T'])
        if key not in tprid:
            nokey += 1
        oos = a in tpv.OUT_OF_SCOPE or b in tpv.OUT_OF_SCOPE
        flags = (tpv.taken(a) << 0) | (tpv.taken(b) << 1) | ((r['tfix'] == 0x0018) << 2) | (oos << 3) | \
                ((a in tpv.CACHE_OPS or b in tpv.CACHE_OPS) << 4)
        nrest = len(r['code']) - 2
        blob += struct.pack('<IIIHBBBBBB', r['id'], tprid.get(key, 0xFFFFFFFF), r['fpscr'], r['tfix'], r['align'],
                            flags, len(r['ns']), nrest, r['ninit'], r['nmem'])
        blob += struct.pack(f'<{2 + nrest}H', *r['code']) + bytes((-(2 + nrest) * 2) % 4)
        blob += r['init'] + r['mem']
        for n in r['ns']:
            def get(p, i):
                v = passes[p].get(key, {}).get(n)
                return None if v is None else v[i]
            tb = tpv.rnd(get('brk', 0))
            if tpv.rnd(get('par', 0), 0.0) == tpv.JITTER:
                tb = tpv.JITTER                                  # the par pass saw this loop's time vary
            vals = (tpv.consensus([get(p, 0) for p in ('par', 'frzreg', 'frzfpu')]), tb,
                    tpv.rnd(get('par', 1), 0.0), tpv.rnd(get('brk', 1), 0.0), tpv.rnd(get('frzreg', 1), 0.0),
                    tpv.rnd(get('frzfpu', 1), 0.0))
            if not oos:
                for nm, v in zip(METRICS, vals):
                    if v < 0:
                        dropped[(nm, v)] += 1
            blob += struct.pack('<BBhhhhhh', n, 0, *vals)
            npts += 1
        names.append({'id': r['id'], 'tprid': tprid.get(key, -1), 'a': a, 'b': b, 'mode': m['mode'], 'var': m['var']})
    with open(os.path.join(out, 'vectors.bin'), 'wb') as f:
        f.write(b'V1VC' + struct.pack('<II6I', 1, len(recs), *lay) + blob)
    with open(os.path.join(out, 'records.jsonl'), 'w') as f:
        for d in names:
            f.write(json.dumps(d) + '\n')
    print(f'{len(recs)} records, {npts} points -> {out}   (layout S {lay[0]:08x} CODE {lay[1]:08x} EXIT {lay[2]:08x} '
          f'SR {lay[3]:08x} VBR {lay[4]:08x} DRAIN {lay[5]}; {nokey} records measured by no pass)', file=sys.stderr)
    print('dropped in scope (not measured / not integral / passes disagree): ' +
          ', '.join(f'{m} {dropped[(m, -1)]}/{dropped[(m, -2)]}/{dropped[(m, -3)]}' for m in METRICS), file=sys.stderr)


# ---------------------------------------------------------------------------------------------------------------
# report
def load_vec(path):
    b = open(path, 'rb').read()
    assert b[:4] == b'V1VC'
    ver, nrec = struct.unpack_from('<II', b, 4)
    p = 36                                          # magic, version, nrec, six layout words
    out = {}
    for _ in range(nrec):
        rid, tp, fpscr, tfix, align, flags, nn, nrest, ninit, nmem = struct.unpack_from('<IIIHBBBBBB', b, p)
        p += 20
        code = struct.unpack_from(f'<{2 + nrest}H', b, p)
        p += (2 * (2 + nrest) + 3) & ~3
        p += 8 * (ninit + nmem)
        pts = {}
        for _ in range(nn):
            n, _pad, *v = struct.unpack_from('<BBhhhhhh', b, p)
            p += 14
            pts[n] = v
        out[rid] = {'flags': flags, 'code': code, 'pts': pts}
    return out


def load_res(paths):
    out = {}
    for path in paths:
        b = open(path, 'rb').read()
        for p in range(0, len(b) - len(b) % 18, 18):
            rid, n, fl, *v = struct.unpack_from('<IBBhhhhhh', b, p)
            out[(rid, n)] = (fl, v)
    return out


def vkind(var):
    return 'ov' if var.startswith('ov:') else 'mem' if var.startswith('mem:') else var


class Tally:
    def __init__(self):
        self.ok, self.tot, self.drop = collections.Counter(), collections.Counter(), collections.Counter()
        self.rec_ok = self.rec_tot = 0

    def table(self, title):
        print(f'{title}: records {self.rec_ok} / {self.rec_tot} match on every compared point and metric')
        for name in METRICS:
            if not self.tot[name] and not any(self.drop[(name, w)] for w in WHY):
                continue
            pc = 100.0 * self.ok[name] / self.tot[name] if self.tot[name] else 100.0
            dr = ', '.join(f'{self.drop[(name, w)]} {WHY[w]}' for w in WHY if self.drop[(name, w)])
            print(f'  {name:7s} {self.ok[name]:8d} / {self.tot[name]:8d}  ({pc:7.3f} %)' + (f'   dropped: {dr}' if dr else ''))


def report(args):
    d = os.path.join(HERE, '..', 'build', 'v1')
    if args and not args[0].startswith('--'):
        d = args.pop(0)
    opt = {'--all': False, '--top': 40, '--metric': None, '--form': None, '--dump': None}
    i = 0
    while i < len(args):
        if args[i] == '--all':
            opt['--all'] = True
        elif args[i] in opt:
            opt[args[i]] = args[i + 1]
            i += 1
        i += 1
    top = int(opt['--top'])
    vec = load_vec(os.path.join(d, 'vectors.bin'))
    names = {}
    for line in open(os.path.join(d, 'records.jsonl')):
        m = json.loads(line)
        names[m['id']] = m
    res = load_res(sorted(glob.glob(os.path.join(d, 'results_*.bin'))))
    groups = {'all': Tally(), 'rest': Tally(), 'cache': Tally(), 'oos': Tally()}
    clusters = collections.defaultdict(list)
    per_form = collections.Counter()
    fc = collections.Counter()
    dump = open(opt['--dump'], 'w') if opt['--dump'] else None
    for rid, r in vec.items():
        nm = names[rid]
        oos = bool(r['flags'] & F_OOS)
        if opt['--form'] and opt['--form'] not in nm['a'] and opt['--form'] not in nm['b']:
            continue
        for n in r['pts']:                               # functional results: every point, in scope or not
            got = res.get((rid, n))
            if got is None:
                fc['no result'] += 1
                continue
            fl = got[0]
            fc['points'] += 1
            if fl & R_NOHALT:
                fc['did not halt / bad access'] += 1
            if not fl & R_NOFUNC:
                fc['func compared'] += 1
                fc['func mismatch'] += bool(fl & R_FUNC)
            if fl & R_NONPER:
                fc['not periodic'] += 1
            if fl & R_PHASE:
                fc['phase'] += 1
        if oos and not opt['--all']:
            continue
        tl = [groups['oos']] if oos else [groups['all'], groups['cache' if r['flags'] & F_CACHE else 'rest']]
        good = True
        deltas = collections.defaultdict(dict)
        timed = False
        for n, meas in r['pts'].items():
            got = res.get((rid, n))
            if got is None or got[0] & R_NOTIME:
                good = good and got is not None
                continue
            timed = True
            fl, mv = got
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
                        dump.write(json.dumps({'id': rid, 'tprid': nm['tprid'], 'a': nm['a'], 'b': nm['b'],
                                               'mode': nm['mode'], 'var': nm['var'], 'n': n, 'metric': name,
                                               'meas': meas[k], 'model': mv[k]}) + '\n')
        if not timed:
            continue
        for t in tl:
            t.rec_tot += 1
            t.rec_ok += good
        for name, dl in deltas.items():
            sig = ' '.join(f'{n}:{dl[n]:+d}' for n in sorted(dl))
            clusters[(name, nm['a'], nm['b'], vkind(nm['var']), sig)].append((nm['mode'], nm['var'], rid, nm['tprid']))
            per_form[(name, nm['a'])] += 1
            per_form[(name, '  as B: ' + nm['b'])] += 1
    print(f'v1 direct run: {fc["points"]} points with a result ({fc["no result"]} without); '
          f'{fc["did not halt / bad access"]} did not halt or accessed memory outside the image; '
          f'{fc["not periodic"]} not periodic')
    print(f'functional (every register + scratch / code / pool / jit_save / state vs the ISS, 4 runs per point): '
          f'{fc["func compared"] - fc["func mismatch"]} / {fc["func compared"]} points match')
    if fc['phase']:
        print(f'refresh phase (soc_tb --sweep, SOC.md 5): {fc["phase"]} points matched the console only in a retry at '
              f'another phase of the free-running refresh timer')
    groups['all'].table('timing, in scope (every form but TAS.B)')
    groups['rest'].table('  of which: the ISA without the cache-block forms')
    groups['cache'].table('  of which: OCBI / OCBP / OCBWB / MOVCA.L')
    if opt['--all']:
        groups['oos'].table('out of scope: TAS.B')
    if clusters:
        print(f'\nlargest clusters (metric, A, B, variant kind, model - measured by n): {len(clusters)} clusters')
        for k, v in sorted(clusters.items(), key=lambda kv: -len(kv[1]))[:top]:
            name, a, b, vk, sig = k
            ex = v[0]
            print(f'  {len(v):5d}  {name:6s} {a:22s} | {b:22s} [{vk}] {sig[:70]}   e.g. {ex[0]} {ex[1]} id {ex[2]} '
                  f'(proxy rid {ex[3]})')
        print('\nforms with most mismatching curves:')
        for (name, f), c in per_form.most_common(top):
            print(f'  {c:6d}  {name:6s} {f}')
    ok = fc['points'] and not fc['no result'] and not fc['did not halt / bad access'] and not fc['func mismatch'] \
        and all(groups['all'].ok[m] == groups['all'].tot[m] for m in METRICS)
    print('v1: PASS' if ok else 'v1: FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'report':
        sys.exit(report(sys.argv[2:]))
    build(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', 'build', 'v1'))
