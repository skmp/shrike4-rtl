#!/usr/bin/env python3
"""Parse Table 8.3 (Execution Cycles) of the SH7091 programming manual (pdftotext -layout of ../../docs/sh7091pm_e.pdf)
into manual_table.json, keyed by the isa.py form names where they match.  Comparison only: nothing in the sweep or the
analysis uses these numbers as input."""
import json, re, sys
import isa
txt = open(sys.argv[1]).read().split('\n')
txt = [l.lstrip('\f') for l in txt]
start = next(i for i, l in enumerate(txt) if l.startswith('Table 8.3') and 'Execution Cycles' in l and i > 7000)
rows = []
row_re = re.compile(r'^\s*(?:[A-Za-z-]+\s+)?(\d{1,3})\s{2,}(\S+(?: \S+)?)\s+(.*?)\s{2,}(MT|EX|BR|LS|FE|CO)\s+(\S+)\s+(\S+)\s+(#\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s*$')
for l in txt[start:start + 700]:
    m = row_re.match(l)
    if not m:
        continue
    no, mn, ops, grp, issue, lat, pat, stage, st, cyc = m.groups()
    ops = re.sub(r'\s+', '', ops)
    mnem = re.sub(r'\s+', ' ', mn).strip()
    if ' ' in mnem:          # 'CMP/STR Rm,Rn' packed in one column
        mnem, ops2 = mnem.split(' ', 1)
        ops = ops2.replace(' ', '') + ops
    rows.append({'no': int(no), 'mnem': mnem, 'ops': ops, 'group': grp, 'issue': issue, 'latency': lat, 'pattern': pat,
                 'lock_stage': stage, 'lock_start': st, 'lock_cycles': cyc})
def key(name):
    n = name.split(' (')[0]
    parts = n.split(' ', 1)
    return (parts[0], parts[1].replace(' ', '') if len(parts) > 1 else '')
table = {}
forms = {key(f.name): f.name for f in isa.FORMS}
for r in rows:
    k = (r['mnem'], r['ops'].replace('label', 'label'))
    names = [nm for kk, nm in forms.items() if kk[0] == r['mnem'] and kk[1] == r['ops']]
    r['forms'] = [f.name for f in isa.FORMS if key(f.name) == (r['mnem'], r['ops'])]
    table[r['no']] = r
json.dump(table, open('manual_table.json', 'w'), indent=1)
matched = {n for r in table.values() for n in r['forms']}
print(len(rows), 'rows;', len(matched), 'of', len(isa.FORMS), 'forms matched', file=sys.stderr)
print('unmatched forms:', [f.name for f in isa.FORMS if f.name not in matched], file=sys.stderr)
