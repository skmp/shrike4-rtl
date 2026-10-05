#!/usr/bin/env python3
"""Every K-th record of the v1 direct-run vectors: the incremental regression set of `make v1`.

The full sweep (build/v1/vectors.bin, 1.8M points) is `make v1-full`, run only when a deliverable is complete (the
user's rule: incremental regressions on a 1/50 subsample, the full regression at the end).  The records are in the
test chunks' order (mode, A, B, variant), so every K-th record covers every form pair and mode.

usage: v1_subsample.py SRC_DIR DST_DIR K
  reads SRC_DIR/vectors.bin ("V1VC", tb/v1_vectors.py) and SRC_DIR/records.jsonl, writes DST_DIR/vectors.bin and
  DST_DIR/records.jsonl with records 0, K, 2K, ...  (tb/v1_tb.cpp and `tb/v1_vectors.py report` run on DST_DIR as is)
"""
import json
import os
import struct
import sys


def main():
    src, dst, k = sys.argv[1], sys.argv[2], int(sys.argv[3])
    b = open(os.path.join(src, 'vectors.bin'), 'rb').read()
    assert b[:4] == b'V1VC', 'not a V1VC vector file'
    ver, nrec = struct.unpack_from('<II', b, 4)
    head = b[12:36]                                  # the six layout words, copied
    p = 36
    keep, ids = [], set()
    for i in range(nrec):
        start = p
        rid, _tp, _fpscr, _tfix, _align, _flags, nn, nrest, ninit, nmem = struct.unpack_from('<IIIHBBBBBB', b, p)
        p += 20
        p += (2 * (2 + nrest) + 3) & ~3              # code, padded to 4 bytes
        p += 8 * (ninit + nmem) + 14 * nn            # init / mem entries, points
        if i % k == 0:
            keep.append(b[start:p])
            ids.add(rid)
    assert p == len(b), f'{len(b) - p} trailing bytes'
    os.makedirs(dst, exist_ok=True)
    with open(os.path.join(dst, 'vectors.bin'), 'wb') as f:
        f.write(b'V1VC' + struct.pack('<II', ver, len(keep)) + head + b''.join(keep))
    n = 0
    with open(os.path.join(dst, 'records.jsonl'), 'w') as f:
        for line in open(os.path.join(src, 'records.jsonl')):
            if json.loads(line)['id'] in ids:
                f.write(line)
                n += 1
    assert n == len(keep), f'records.jsonl has {n} of the {len(keep)} kept records'
    print(f'v1 subsample: every {k}th record: {len(keep)} of {nrec} records -> {dst}', file=sys.stderr)


if __name__ == '__main__':
    main()
