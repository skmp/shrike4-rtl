#!/usr/bin/env python3
"""Validation vectors for rtl/timing-proxy from the pairwise timing sweep (model/timings2).

Every measured record becomes a replayable loop.  The testbench (tb/timing_proxy_tb.cpp) rebuilds, for every distance n,
exactly what the console ran:
    L: A ; add #1,r14 x n ; B ; add #1,r14 x 40 ; restores ; dt r13 ; bf/s L ; (sett|nop)
(brk shape: the first add after B is stc dbr,r11) and compares cycles / iteration and the three PRFC0 events.

Scope is decided per (point, metric) by determinism, not per instruction form: a value is compared when the console
measured it deterministically, whatever the forms.  A measurement is a count over 128 iterations divided by 128.  Event
counts are exact in every pass, and so are the cycle counts of the par pass; the brk / frzreg / frzfpu passes add
+-2..12 / 128 of cycle-counter jitter to every loop (4 % of their points).  So a value is "integral" when it is an exact
integer in the par pass or for an event, and within TOL (12.8 / 128) of one for a brk / frzreg / frzfpu cycle count; a
loop whose own time varies (external-memory traffic of the cache-block forms) shows it as a non-zero remainder in the
par pass, never hidden by jitter there.  Expected values per (record, n):
    T      cycles / iteration, par-shape loop.  Three passes ran this loop (par, frzreg, frzfpu): T is their common value
           when at least two of them measured it, every one that measured it is integral, and they all agree.
    Tb     cycles / iteration, brk-shape loop (pass brk only).  Its jitter cannot tell a varying loop from the counter,
           so the par pass decides for it: the brk loop is the par loop with one pad replaced, and when the par pass saw
           the par loop's time vary, Tb is dropped too (on the exact par points the brk remainder is within +-4 / 128
           on all but 47, at most +-12, the counter's own jitter; on the varying ones it exceeds +-12 on 2,160 of
           2,568).
    par    0x14 parallel issues / iteration (par shape)      brkp  0x14 in the brk shape
    frzreg 0x28 freeze by CPU register / iteration           frzfpu 0x29 freeze by FPU / iteration
A value that is not compared is negative and says why:
    -1 not measured (no pass, or fewer than two for T)       -2 not integral (the loop's time or count varied)
    -3 T only: the passes that measured it disagree
TAS.B is the one form taken out of scope as a whole: a bus-locked read-modify-write whose time (23.3 cycles on
average) is set by the external bus, so even its integral points are coincidences of the bus clock.

Output (build/timing_proxy/): vectors.bin (little endian)
    "TPV1" u32 nrec
    per record: u32 rid, u8 mode (0 M0, 1 M1, 2 M2), u8 flags, u8 nrest, u8 nn, u16 A, u16 B, u16 rest[nrest],
                then nn x { u8 n, u8 pad, i16 T, i16 Tb, i16 par, i16 brkp, i16 frzreg, i16 frzfpu }
    flags: bit0 A taken branch, bit1 B taken branch, bit2 T fix is sett (else nop), bit3 out of scope (TAS.B),
           bit4 a cache-block form (OCBI, OCBP, OCBWB, MOVCA.L: out of scope before scoping by determinism; the report
           shows them on their own line)
and records.jsonl: one line per record {rid, a, b, mode, var}.  Prints the dropped values per metric and reason.
usage: timing_proxy_vectors.py [out_dir]
"""
import collections
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
T2 = os.path.join(HERE, '..', 'model', 'timings2')
sys.path.insert(0, T2)
import isa      # noqa: E402
import t2res    # noqa: E402

OUT_OF_SCOPE = {'TAS.B @Rn'}
CACHE_OPS = {'OCBI @Rn', 'OCBP @Rn', 'OCBWB @Rn', 'MOVCA.L R0,@Rn'}
MODE_NO = {'M0': 0, 'M1': 1, 'M2': 2}
METRICS = ('T', 'Tb', 'par', 'brkp', 'frzreg', 'frzfpu')
TOL = 0.1
MISSING, JITTER, DISAGREE = -1, -2, -3


def taken(form_name):
    f = isa.BYNAME[form_name]
    if not f.br:
        return False
    if f.br in ('cond_t', 'cond_f'):
        return '(taken)' in form_name
    return True


def rnd(v, tol=TOL):
    """the integral value of one measurement, or why there is none"""
    if v is None:
        return MISSING
    r = round(v)
    return int(r) if abs(v - r) <= tol else JITTER


def consensus(vs):
    """T from the passes that ran the par-shape loop (par exact, frzreg / frzfpu within TOL): >= 2 measured, all
    integral, all equal"""
    vs = [rnd(v, tol) for v, tol in zip(vs, (0.0, TOL, TOL)) if v is not None]
    if len(vs) < 2:
        return MISSING
    if any(v == JITTER for v in vs):
        return JITTER
    return vs[0] if all(v == vs[0] for v in vs) else DISAGREE


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', 'build', 'timing_proxy')
    os.makedirs(out, exist_ok=True)
    passes = {}
    for p in ('par', 'brk', 'frzreg', 'frzfpu'):
        recs = t2res.load(p)
        d = {}
        for r in recs.values():
            m = r['meta']
            if r['status']:
                continue
            # the tb places A at a 4-byte boundary; another alignment would need its own records and CODE address
            assert m.get('align', 0) == 0, f'{p}: case {m["id"]} has align {m["align"]}; the tb only models align 0'
            key = (m['a'], m['b'], m['mode'], m['var'], tuple(m['code']), m['T'])
            e = d.setdefault(key, {})
            e.update(r['pts'])
        passes[p] = d
        print(f'{p}: {len(d)} records', file=sys.stderr)
    keys = sorted(set(passes['par']) | set(passes['frzreg']) | set(passes['frzfpu']) | set(passes['brk']),
                  key=lambda k: (k[2], k[0], k[1], k[3], k[4]))
    blob = bytearray()
    meta = []
    npts = 0
    dropped = collections.Counter()
    for rid, k in enumerate(keys):
        a, b, mode, var, code, tflag = k
        code = [int(w, 16) for w in code]
        rest = code[2:]
        ns = sorted(set().union(*(passes[p].get(k, {}).keys() for p in passes)))
        oos = a in OUT_OF_SCOPE or b in OUT_OF_SCOPE
        flags = (taken(a) << 0) | (taken(b) << 1) | ((1 if tflag else 0) << 2) | (oos << 3) | \
                ((a in CACHE_OPS or b in CACHE_OPS) << 4)
        blob += struct.pack('<IBBBB', rid, MODE_NO[mode], flags, len(rest), len(ns))
        blob += struct.pack(f'<{2 + len(rest)}H', *code)
        for n in ns:
            def get(p, i):
                v = passes[p].get(k, {}).get(n)
                return None if v is None else v[i]
            tb = rnd(get('brk', 0))
            if rnd(get('par', 0), 0.0) == JITTER:
                tb = JITTER                                     # the loop's time varies (see the header)
            vals = (consensus([get(p, 0) for p in ('par', 'frzreg', 'frzfpu')]), tb,
                    rnd(get('par', 1), 0.0), rnd(get('brk', 1), 0.0), rnd(get('frzreg', 1), 0.0),
                    rnd(get('frzfpu', 1), 0.0))
            if not oos:
                for name, v in zip(METRICS, vals):
                    if v < 0:
                        dropped[(name, v)] += 1
            blob += struct.pack('<BBhhhhhh', n, 0, *vals)
            npts += 1
        meta.append({'rid': rid, 'a': a, 'b': b, 'mode': mode, 'var': var})
    with open(os.path.join(out, 'vectors.bin'), 'wb') as f:
        f.write(b'TPV1' + struct.pack('<I', len(keys)) + blob)
    with open(os.path.join(out, 'records.jsonl'), 'w') as f:
        for m in meta:
            f.write(json.dumps(m) + '\n')
    print(f'{len(keys)} records, {npts} points -> {out}', file=sys.stderr)
    print('dropped in scope (not measured / not integral / passes disagree): ' +
          ', '.join(f'{m} {dropped[(m, MISSING)]}/{dropped[(m, JITTER)]}/{dropped[(m, DISAGREE)]}' for m in METRICS),
          file=sys.stderr)


if __name__ == '__main__':
    main()
