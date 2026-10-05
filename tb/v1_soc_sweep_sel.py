#!/usr/bin/env python3
"""The point sets of make v1-soc-sweep: the pairwise sweep's vectors on the whole chip (rtl/v1/SOC.md 5, 8.3; WI-SOC, M3).

The full sweep on v1_soc (1.8M points) is M6's and the lead's (make v1-soc-sweep-full).  M3 gates a stratified subset,
and the user's subsample rule wants a smaller one for incremental use:

  gate   (make v1-soc-sweep, the M3 gate; SOC.md 5 "one n per record for every record class, every cache-block record
         at every n, the six OCBI records, the LDTLB records"):
           classes  one record per (A form, mode, variant kind) and one per (B form, mode, variant kind) -- the lowest
                    id not chosen yet -- at one n (the record's n list at index id % len: the distances rotate)
           cache    every cache-block record (OCBI / OCBP / OCBWB / MOVCA.L: vectors flag bit 4) of make v1's 1/50
                    subsample (records 0, 50, 100, ...) at every n
           ocbgbr   OCBI / OCBP / OCBWB followed by a store through @(disp,GBR) (the six OCBI records -- OCBI then
                    MOV.B / W / L R0,@(disp,GBR) to the same line or the same index, +1 over the old model at n = 0..2,
                    SOC.md 5 -- and their OCBP / OCBWB and load siblings) at every n
           ldtlb    the LDTLB x cache-op records of tb/mk/v1.mk's v1-ldtlb (SOC.md 5's directed check) at every n
           macs     MAC.W / MAC.L next to an SR.S writer (SETS, CLRS, LDC / LDC.L SR) at every n: lockstep is the only
                    check that sees a MAC's result (the runner restores MACH / MACL before the final state), and the
                    full sweep found MAC.L ; SETS saturating with the younger SETS's S (id 42512, SOC.md 5)
           ocbpc    MOVA @(disp,PC),R0 ; OCBI @R0 at every n: the points of the full sweep that match the console
                    only at another refresh phase (soc_tb's retry, SOC.md 5)
  sub    (make v1-soc-sweep-sub, incremental): the six OCBI records at every n, the ldtlb records at one n, every 10th
         class record at one n, the four MAC x SETS records at every n
  full   (make v1-soc-sweep-full, M6): every record at every n
  phase  (make v1-soc-sweep-phase, M3-fix): the refresh-phase records -- PHASE_IDS, points whose timing on the chip
         depends on the free-running refresh timer's phase (M3-verify F3: FMOV DR2,@-R1 | OCBP @R1 at n 40, an odd N2 - N1
         at some phases), at every n; the target runs them at chosen phases with and without soc_tb's retry

usage: v1_soc_sweep_sel.py SRC_DIR DST_DIR gate|sub|full|phase
  reads SRC_DIR/vectors.bin ("V1VC", tb/v1_vectors.py) and SRC_DIR/records.jsonl; writes DST_DIR/vectors.bin (each
  kept record with only its selected points) and DST_DIR/records.jsonl, which tb/soc_tb.cpp --sweep and
  `tb/v1_vectors.py report DST_DIR` read as they are
"""
import json
import os
import struct
import sys

LDTLB_IDS = {85338, 85339, 85340, 85341, 85342, 85343, 85886, 86744, 87244, 87744, 88244}
SIX_OCBI = {86478, 86479, 86481, 86482, 86484, 86485}     # OCBI ; MOV.B / W / L R0,@(disp,GBR), mem:same / mem:line
F_CACHE = 16
PHASE_IDS = {127775}                                       # M3-verify F3's point (n 40)
# MAC.W / MAC.L with an SR.S writer before or behind it (make v1-soc-sweep-full, 2026-10-05: id 42512)
MACS_IDS = {42417, 42419, 42436, 42437, 42438, 42512, 43323, 43325, 43342, 43414, 73545, 73546, 74282, 74283,
            77736, 77737, 77738, 77739, 77740, 77741, 88776, 88777}
MACS_SUB = {42512, 43414, 88776, 88777}                    # MAC.L / MAC.W ; SETS and SETS ; MAC.L / MAC.W
OCBPC_IDS = {23612, 23614}                                 # MOVA @(disp,PC),R0 ; OCBI @R0: refresh-phase points


def parse(b):
    ver, nrec = struct.unpack_from('<II', b, 4)
    head = b[12:36]
    p = 36
    recs = []
    for i in range(nrec):
        start = p
        rid, _tp, _fpscr, _tfix, _align, flags, nn, nrest, ninit, nmem = struct.unpack_from('<IIIHBBBBBB', b, p)
        p += 20
        p += (2 * (2 + nrest) + 3) & ~3
        p += 8 * (ninit + nmem)
        pts_at = p
        p += 14 * nn
        recs.append({'i': i, 'id': rid, 'flags': flags, 'start': start, 'pts_at': pts_at, 'end': p,
                     'ns': [b[pts_at + 14 * k] for k in range(nn)]})
    assert p == len(b), f'{len(b) - p} trailing bytes'
    return ver, head, recs


def main():
    if len(sys.argv) != 4 or sys.argv[3] not in ('gate', 'sub', 'full', 'phase'):
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    src, dst, mode = sys.argv[1:]
    b = open(os.path.join(src, 'vectors.bin'), 'rb').read()
    assert b[:4] == b'V1VC', 'not a V1VC vector file'
    ver, head, recs = parse(b)
    meta = {}
    for line in open(os.path.join(src, 'records.jsonl')):
        m = json.loads(line)
        meta[m['id']] = m
    pick = {}                                        # record index -> set of n (None: every n)
    why = {'classes': 0, 'cache': 0, 'ocbgbr': 0, 'ldtlb': 0, 'macs': 0, 'ocbpc': 0}

    def add(r, ns, tag):
        cur = pick.get(r['i'], set())
        if ns is None:
            ns = set(r['ns'])
        new = set(ns) - cur
        if new:
            why[tag] += len(new)
        pick[r['i']] = cur | set(ns)

    def one_n(r):
        return {r['ns'][r['id'] % len(r['ns'])]}

    if mode == 'full':
        for r in recs:
            add(r, None, 'classes')
    elif mode == 'phase':
        for r in recs:
            if r['id'] in PHASE_IDS:
                add(r, None, 'classes')
    else:
        # class representatives
        seen, reps = set(), []
        for r in recs:
            m = meta[r['id']]
            vk = m['var'].split(':')[0]
            for key in (('A', m['a'], m['mode'], vk), ('B', m['b'], m['mode'], vk)):
                if key not in seen:
                    seen.add(key)
                    reps.append(r)
                    break
            else:
                continue
        reps_kept = reps if mode == 'gate' else reps[::10]
        for r in reps_kept:
            add(r, one_n(r), 'classes')
        for r in recs:
            m = meta[r['id']]
            if r['id'] in LDTLB_IDS:
                add(r, None if mode == 'gate' else one_n(r), 'ldtlb')
            if mode == 'gate' and m['a'].startswith(('OCBI', 'OCBP', 'OCBWB')) and '@(disp,GBR)' in m['b']:
                add(r, None, 'ocbgbr')
            if r['id'] in SIX_OCBI:
                add(r, None, 'ocbgbr')
            if mode == 'gate' and r['flags'] & F_CACHE and r['i'] % 50 == 0:
                add(r, None, 'cache')
            if r['id'] in (MACS_IDS if mode == 'gate' else MACS_SUB):
                add(r, None, 'macs')
            if mode == 'gate' and r['id'] in OCBPC_IDS:
                add(r, None, 'ocbpc')
    os.makedirs(dst, exist_ok=True)
    out, keep_ids, npts = [], set(), 0
    for r in recs:
        if r['i'] not in pick:
            continue
        ns = pick[r['i']]
        rec = bytearray(b[r['start']:r['pts_at']])
        pts = b''.join(b[r['pts_at'] + 14 * k:r['pts_at'] + 14 * (k + 1)] for k, n in enumerate(r['ns']) if n in ns)
        rec[16] = len(pts) // 14                      # nn
        out.append(bytes(rec) + pts)
        keep_ids.add(r['id'])
        npts += len(pts) // 14
    with open(os.path.join(dst, 'vectors.bin'), 'wb') as f:
        f.write(b'V1VC' + struct.pack('<II', ver, len(out)) + head + b''.join(out))
    with open(os.path.join(dst, 'records.jsonl'), 'w') as f:
        for line in open(os.path.join(src, 'records.jsonl')):
            if json.loads(line)['id'] in keep_ids:
                f.write(line)
    print(f'v1-soc-sweep {mode}: {len(out)} records, {npts} points -> {dst} '
          f'(new points by reason: {", ".join(f"{k} {v}" for k, v in why.items())})', file=sys.stderr)


if __name__ == '__main__':
    main()
