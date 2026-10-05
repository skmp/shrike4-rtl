"""Read tests/cases.jsonl + results/<pass>/chunk_*.bin into per-case curves."""
import json
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))


def load(pass_name, tests=os.path.join(HERE, 'tests'), results=None):
    """returns {record id: {'meta': ..., 'status': int, 'pts': {n: (cycles/iter, event/iter)}}}"""
    results = results or os.path.join(HERE, 'results', pass_name)
    meta = {}
    mfile = os.path.join(results, 'cases.jsonl')          # the case set this pass was measured with
    if not os.path.exists(mfile):
        mfile = os.path.join(tests, 'cases.jsonl')
    for line in open(mfile):
        d = json.loads(line)
        meta[d['id']] = d
    out = {}
    for fn in sorted(os.listdir(results)):
        if not fn.startswith('chunk_'):
            continue
        b = open(os.path.join(results, fn), 'rb').read()
        assert b[:4] == b'T2RS', fn
        ncase, event, dn = struct.unpack_from('<III', b, 4)
        p = 16
        for _ in range(ncase):
            cid, status, nn = struct.unpack_from('<IHB', b, p)
            p += 8
            vals = struct.unpack_from(f'<{2 * nn}I', b, p)
            p += 8 * nn
            m = meta[cid]
            pts = {}
            if status == 0:
                for j, n in enumerate(m['ns']):
                    pts[n] = (vals[2 * j] / dn, vals[2 * j + 1] / dn)
            out[cid] = {'meta': m, 'status': status, 'pts': pts, 'raw': vals, 'event': event}
    return out


def curves(recs):
    """merge per-n records of a case: {case index: {'meta', 'pts'}}"""
    out = {}
    for r in recs.values():
        c = r['meta']['case']
        e = out.setdefault(c, {'meta': r['meta'], 'pts': {}, 'status': 0})
        e['pts'].update(r['pts'])
        e['status'] |= r['status']
    return out
