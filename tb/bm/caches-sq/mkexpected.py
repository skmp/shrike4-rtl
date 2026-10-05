#!/usr/bin/env python3
"""mkexpected.py -- expected/<prog>.json from a caches-sq console log (tools/hw/caches-sq/run{1,2,3}.log).  WI-CACHE.

  mkexpected.py <run.log> <prog>  >  expected/<prog>.json

Every line the program printed (from "<prog>: CCR ..." to "<prog> DONE") becomes an entry {class, shape, occ, text,
ignore, var}.  check.py finds the output line with the same shape and occurrence and compares its tokens.
  shape   the line with every value token masked: 8-digit hex words, decimal / fixed-point numbers, and the value of a
          name=value token (so the same program run on a different link layout has the same shapes)
  class   functional  cache / SQ / CCR facts any correct implementation prints: "F" lines (with or without the
                      program's own want / ok verdict), the SQ-area and CCN-mirror reads of the cache registers,
                      "exceptions caught", DONE
          timing      the "T" rows (cycles per unit and event counts: the M3 gate "kern.S bodies exact where
                      integral"), the CCR write cycles, the event-0x0B count
          env         depends on what ran before on the console or on the link layout: the header (KOS's CCR / QACR /
                      MCR), region / X / Y / R0 addresses, the BSC / CPG / MMU / exception-register values KOS left,
                      stale array contents, the SQ-read junk, and run 1's rows that dcload's network code evicted
                      (the DIFF lines and the 0x01848000 RX-buffer tags; run 2 repeated them cleanly, caches-sq 6)
  var     token indexes that are reported, not failed (a stale IC entry on an otherwise functional line)
Tag words (a PA[28:10] tag with flag bits, or a P0 / P1 / P2 address of one) are compared through an isomorphism: the
k-th distinct tag of the functional lines is "T<k>" in both logs (check.py), so the link layout does not matter.
"""
import json
import re
import sys

HEX8 = re.compile(r'^[0-9a-fA-F]{8}$')
NUM = re.compile(r'^-?\d+(\.\d+)?$')


def is_value(tok):
    return bool(HEX8.match(tok) or NUM.match(tok))


def split_tok(t):
    """(head, value, tail) of a token: head = '(' and / or 'name=', tail = trailing ',' ':' ')'; value '' if none"""
    core = t.rstrip(',:)')
    tail = t[len(core):]
    head = ''
    if core.startswith('('):
        head, core = '(', core[1:]
    if '=' in core and not core.endswith('='):
        n, v = core.split('=', 1)
        head, core = head + n + '=', v
    return (head, core, tail) if is_value(core) else (t, '', '')


def shape_of(line):
    out = []
    for t in line.split():
        h, v, tl = split_tok(t)
        out.append(h + '#' + tl if v else t)
    return ' '.join(out)


ENV_PREFIX = ('regions:', 'R0 ', 'F X=', 'F SQ test line', 'F BSC ', 'F CPG ', 'F SQ area read')
ENV_CCN = {'ff001000', 'ff00100c', 'ff001020', 'ff001024', 'ff001028'}      # PTEH TEA TRA EXPEVT INTEVT (KOS's)


def classify(line, prog):
    toks = line.split()
    if line.startswith(prog + ':') or line.startswith(ENV_PREFIX):
        return 'env', []
    if line.startswith('T ') or line.startswith('F CCR write cycles') or 'event 0x0B' in line:
        return 'timing', []
    if 'DIFF' in toks or '01848000' in toks:
        return 'env', []
    if line.startswith('F read ff') or line.startswith('F read FF'):
        return ('env' if toks[2].lower() in ENV_CCN else 'functional'), []
    if 'entry 511 word 0' in line:                                  # stale data of a RAM-mode entry
        return 'env', []
    return 'functional', []


def main():
    log, prog = sys.argv[1], sys.argv[2]
    lines = open(log, errors='replace').read().splitlines()
    try:
        start = next(i for i, l in enumerate(lines) if l.startswith(prog + ':'))
        end = next(i for i, l in enumerate(lines) if l.strip() == prog + ' DONE')
    except StopIteration:
        sys.exit('mkexpected: no %s output in %s' % (prog, log))
    doc = {'prog': prog, 'source': log.split('/')[-1], 'lines': []}
    occ = {}
    iix = 0
    for l in lines[start:end + 1]:
        l = l.rstrip()
        if not l:
            continue
        sh = shape_of(l)
        k = occ.get(sh, 0)
        occ[sh] = k + 1
        cls, var = classify(l, prog)
        if l.startswith('F IIX=1 call'):                             # the first call: entry 160 holds a stale line
            var = [len(l.split()) - 1] if iix == 0 else []
            iix += 1
        m = re.match(r'^F .* entry ([0-9a-f]{8}):', l)
        if prog == 'cachetest3' and m and m.group(1) != '00000000':
            # run 3's snapshots read U = 0 in the address-array word even after a store hit, although the OCBP that
            # follows wrote the line back (it was dirty); runs 1 / 2 read U with the same helper.  Unexplained console
            # observation (WI-CACHE report, open question): reported, not failed.  The data words are compared.
            var = [l.split().index(m.group(1) + ':')]
        doc['lines'].append({'class': cls, 'shape': sh, 'occ': k, 'text': l, 'ignore': [], 'var': var})
    json.dump(doc, sys.stdout, indent=1)
    sys.stdout.write('\n')


if __name__ == '__main__':
    main()
