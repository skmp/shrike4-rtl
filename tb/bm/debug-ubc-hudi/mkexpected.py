#!/usr/bin/env python3
"""mkexpected.py -- expected-value JSON of the debug-ubc-hudi bare-metal ports, taken from the console logs.

  python3 mkexpected.py            writes expected/{p4probe,ubcbrk,ubcevt,pmcsurvey}.json from tools/hw/debug-ubc-hudi/*.out

Every line the console printed between "<prog>: start" and "<prog>: done" becomes one entry, with a class:
  functional  the value is a property of the chip that any correct implementation reproduces, a functional model
              included (tb/bm/debug-ubc-hudi/run_iss.cpp runs these on the ISS + the WI-DBG unit models)
  timing      an exact console number that depends on the chip's timing (event counts, break latency, settle points,
              PMCTR sample points, data-break SPCs): must match on the cycle-accurate v1_soc
  env         depends on what ran before on the console (KOS / dcload state, power-on garbage, the KOS build's
              compiled code, KOS interrupt sources): not compared
  param       SDDR's power-on content: compared only when v1_hudi's SDDR_INIT is 0x5573A3AB
  slow        needs seconds of real time (the 2.2 s ratio-mode carry, the 1 s interrupt count): not simulated
  mmu         the area-7 UTLB test and the survey's TLB-miss rows: need TRANSLATE = 1 (M2)
Tokens (whitespace-separated) listed in "ignore" are not compared (the SR T bit a C caller left, a garbage r9, the
survey's columns that include KOS's exception handler path);
tokens in "var" are console numbers that varied from run to run (SDRAM refresh): reported, not failed;
tokens in "cache" (ubcbrk's Y rows) need the instruction / operand caches and the bus (rtl/v1/SOC.md 8.3 M3): every
column of an event the caches own (SOC.md 3.7: v1_icache 0x08 / 0x21 / 0x24, v1_ocache 0x01 / 0x02 / 0x04 / 0x05 /
0x0B / 0x0E / 0x0F / 0x22), and the ldP2 / ocm / icm columns of every event (an uncached read, an OC miss and an IC miss
hold the pipeline for the bus: the fetch's dropped requests (0x06), the freeze cycles (0x25), the elapsed time, ...).
IDEAL_MEM = 1 (M1a, M1b) has neither: check.py compares them only when the class list names "cache" (M3), and
otherwise reports a difference as deferred.
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.join(HERE, '..', '..', '..', 'tools', 'hw', 'debug-ubc-hudi')

OWN_D = {'BASRA', 'BASRB', 'PVR?', 'CVR?', 'PRR?', 'PMCR1', 'BARA', 'BAMRA', 'BBRA', 'BARB', 'BAMRB', 'BBRB', 'BRCR',
         'SDIR'}
Y_SNIP = ['nop', 'ld', 'st', 'ldP2', 'ldCCN', 'ldTMU', 'br', 'fp', 'ocm', 'icm']
MEM_SNIP = {3, 8, 9}                           # ldP2, ocm, icm: SDRAM refresh makes their bus times vary
MEM_EV = {0x21, 0x22, 0x23, 0x24, 0x25}
CACHE_EV = {0x08, 0x21, 0x24, 0x01, 0x02, 0x04, 0x05, 0x0B, 0x0E, 0x0F, 0x22}   # SOC.md 3.7: v1_icache, v1_ocache


def cache_tokens(prog, line):
    """the token indices that need the caches and the bus (M3; the module docstring)"""
    t = line.split()
    if prog != 'ubcbrk' or len(t) < 13 or t[0] != 'Y' or t[2] != 'ev':
        return []
    ev = int(t[1], 16)
    return [3 + k for k in range(10) if ev in CACHE_EV or k in MEM_SNIP]


def after(t, *labels):
    """indices of the tokens that follow each occurrence of the given labels (the values printed after them)"""
    return [i + 1 for i, x in enumerate(t[:-1]) if x in labels]


def classify(prog, line):
    """(class, why, ignore-token-predicate or None, var token indices)"""
    t = line.split()
    tag = t[0] if t else ''
    if line.endswith(': start') or line.endswith(': done'):
        return 'functional', 'program marker', [], []
    if prog == 'p4probe':
        if tag == 'D':
            name = t[1]
            if name in OWN_D:
                return 'functional', 'this spec\'s register after reset / KOS ubc_init (all 0), PVR, PMCR1 = C023 (boot stub)', [], []
            if name == 'SDDR':
                return 'param', 'SDDR power-on content', [], []
            return 'env', 'KOS / dcload state of this or another block (counters, garbage, the board state)', [], []
        if tag == 'W':
            return ('param' if t[1] == 'fff00000:' else 'env'), 'sweep includes state-dependent registers', [], []
        if tag == 'H':
            if t[1] in ('ff100014', 'ff100018'):
                return 'env', 'PMCTR2L frozen by an earlier program', [], []
            return 'functional', 'hole reads 0 at every size', [], []
        if tag == 'M':
            if t[1].startswith('BARA') or t[1].startswith('PMCTR1L'):
                return 'env', 'undriven UBC window = the KOS build\'s last load / a running counter', [], []
            return 'functional', 'mirror rule of the block', [], []
        if tag == 'S':
            if t[1] in ('ffd00004', 'ffd0000c', 'ffd80008'):
                return 'env', 'IPRA / IPRC / TCOR0 values KOS set', [], []
            return 'functional', 'wrong-size views read 0 (register values: boot stub / CCR 0x105)', [], []
        if tag == 'U':
            return 'functional', 'UBC read-back masks and the size rule', [], []
        if tag == 'I':
            return 'param', 'SDDR power-on content (SDIR / write rules also on this line)', [], []
        if tag == 'P':
            if t[1] == 'readback':
                return 'functional', 'PMCR mask 0xC13F', [], []
            if t[1] == 'carry:':
                return 'slow', '2.2 s of ratio mode', [], []
            # The rule tokens of these lines are functional; the counts of a running counter depend on the compiled
            # C code between the accesses, and a read right after a start write reads the PMCR-to-count latency
            # (pm_lat 0 on the console): those tokens are ignored (the rules' timing is v1-perfctr's).
            if t[1] == 'run':
                if t[2] == 'c023:':            # counting: rb and the cleared value only
                    return ('functional', 'PMCR read-back, PMCLR clears a stopped counter (counts ignored)',
                            after(t, 'start', 'after-spin', 'disabled', 're-enabled'), [])
                return ('functional', 'PMEN & PMST: 0023 / 4023 / 8023 never count (the read after the C023 restart '
                        'is ignored)', after(t, 're-enabled'), [])
            if t[1] == 'cont:':
                return ('functional', 'PMCTRH while stopped (counts ignored)',
                        after(t, 'running', 'disabled', 'after-spin', 're-enabled', 'PMST-only', 'PMEN-only', 'both'),
                        [])
            if t[1] == 'clr-running:':
                return ('functional', 'PMCLR does not read back (counts ignored)',
                        after(t, 'before', 'now', 'after-spin', 'C023', '2023'), [])
            if t[1] == 'ctr-write:':
                return ('functional', 'PMCTR is read only (the read after the start write and the running count '
                        'ignored)', [t.index('enabled') + 2, t.index('write-while-running') + 2], [])
            if t[1] == 'ratio:':
                return ('functional', 'PMCR1 read-back with PMCLK (the counts ignored)',
                        list(range(t.index('rb') + 2, len(t))), [])
            if t[1] == 'sizes:':
                return ('functional', 'size rule of PMCR (r32 / r8 read 0) and PMCTR2H (the running PMCTR2L ignored)',
                        [t.index('PMCTR2L') + 2, t.index('PMCTR2L') + 4], [])
            return 'env', 'values depend on the KOS build\'s C code between the accesses (rules: v1-perfctr)', [], []
        if tag == 'E' or tag == 'E:':
            return 'env', 'C-bracketed survey (bracket residue), superseded by ubcbrk Y', [], []
    if prog == 'ubcbrk':
        if tag == 'A':
            if t[1] == 'UBC' or t[1] == 'PMCR2':
                return 'functional', 'register / mirror / size rules', [], []
            if t[1] in ('PMCTR', 'PMCTR1H'):
                return 'slow', 'PMCTR1H = 1 needs 2.2 s of ratio mode', [], []
            return 'env', 'undriven UBC window = the KOS build\'s last load (last_rdata)', [], []
        if tag in ('X:', 'Y:'):
            return 'functional', 'header', [], []
        if tag == 'X':
            return 'timing', 'exact event counts, branch snippets', [], []
        if tag == 'Y':
            ev = int(t[1], 16)
            var = []
            for k in range(10):
                if k in MEM_SNIP and ev in MEM_EV:
                    var.append(3 + k)
                if k in MEM_SNIP:
                    var.append(3 + 10 + 2 + k)
            return 'timing', 'exact event counts, run-1 snippets', [], var
        if tag == 'Z':
            return 'env', 'KOS interrupt sources', [], []
        if tag == 'ubcbrk:' and t[1] == 'DBR':
            return 'env', 'KOS SR / PTEH / stack', [], []
        if tag == 'ubcbrk:':
            return 'functional', 'marker', [], []
        if tag == 'B':
            name = t[1]
            ign = ['ssr=']                     # the SR T bit the C caller left
            if name == 'mask10_pre':
                ign.append('r9@brk=')          # the caller's r9: the break is at the function entry
            if name in ('opB_wr_data', 'opB_wr_masked', 'opB_rd_data', 'opB_rd_data_use'):
                return 'timing', 'data-break SPC = the first instruction after the access\'s issue group', ign, []
            return 'functional', 'break, SPC, BRCR, events', ign, []
        if tag == 'L':
            return 'timing', 'break-to-handler latency (App. A.2)', [], []
        if tag == 'T':
            return 'timing', 'settle (open question 9.5)', [], []
        if tag == 'V':
            return 'functional', 'VBR path', ['sr='], []
        if tag == '7':
            return 'mmu', 'area 7 through the UTLB', [], []
    if prog == 'ubcevt':
        if tag in ('S', 'D', 'P'):
            return 'timing', 'settle / data-break position / PMCR start-stop', [], []
        if tag == 'E:':
            return 'functional', 'header', [], []
        if tag == 'E':
            return 'timing', 'exact event counts', [], []
    if prog == 'pmcsurvey':
        if tag in ('Q:', 'M:'):
            return 'functional', 'header', [], []
        if tag == 'Q' and t[1] == 'trapa':
            return 'functional', 'TRAPA handler calls', [], []
        if tag == 'Q':
            ev = int(t[1], 16)
            n = 15
            ign, var = [], []
            if ev not in (0x16, 0x18):                 # the trapa column also counts the handler path (KOS's here)
                ign.append(3 + 14)
            ign.append(3 + n + 2 + 14)
            for k in (12, 13):                         # sqpref / stmiss: bus times vary with SDRAM refresh
                if ev in (0x22, 0x25, 0x23):
                    var.append(3 + k)
                var.append(3 + n + 2 + k)
            return 'timing', 'exact event counts (SOC.md 9.6 survey)', ign, var
        if tag == 'M' and t[1] not in ('handler', 'now'):
            ev = int(t[1], 16)
            ign = []
            for k in (0, 1, 3):                        # the miss snippets run the TLB-miss handler (KOS's here)
                if ev not in (0x03, 0x07, 0x16, 0x18):
                    ign.append(3 + k)
                ign.append(3 + 5 + 2 + k)
            return 'mmu', 'UTLB / ITLB miss events with MMUCR.AT = 1 (SOC.md 9.6 survey)', ign, []
        if tag == 'M' and t[1] == 'handler':
            return 'mmu', 'TLB-miss exceptions taken', [], []
        return 'env', 'MMU registers KOS left', [], []
    return 'env', 'unclassified', [], []


def build(prog):
    raw = open(os.path.join(HW, prog + '.out')).read().splitlines()
    try:
        a = raw.index(prog + ': start')
        b = raw.index(prog + ': done')
    except ValueError:
        sys.exit('mkexpected: no start / done markers in %s.out' % prog)
    lines = [l.rstrip() for l in raw[a:b + 1] if l.strip()]
    ents = []
    for l in lines:
        cls, why, ign, var = classify(prog, l)
        toks = l.split()
        ig = sorted(set([i for i in ign if isinstance(i, int)] +
                        [i for i, x in enumerate(toks) if any(x.startswith(p) for p in ign if isinstance(p, str))]))
        ents.append({'text': l, 'class': cls, 'why': why, 'ignore': ig, 'var': var, 'cache': cache_tokens(prog, l)})
    # the key of each line: the shortest token prefix unique among the program's lines
    for e in ents:
        toks = e['text'].split()
        for n in range(1, len(toks) + 1):
            k = toks[:n]
            if sum(1 for o in ents if o['text'].split()[:n] == k) == 1 or n == len(toks):
                e['key'] = k
                break
    counts = {}
    for e in ents:
        counts[e['class']] = counts.get(e['class'], 0) + 1
    doc = {
        'program': prog,
        'source': 'tools/hw/debug-ubc-hudi/%s.out (console run, KOS 2.1.0 under dcload-ip, 2026-09-23)' % prog,
        'build': 'tb/bm/debug-ubc-hudi (make v1-dbg-bm): %s.bin at 0x8C010000' % prog,
        'compare': 'tb/bm/debug-ubc-hudi/check.py <output log> expected/%s.json --classes functional,timing' % prog,
        'counts': counts,
        'lines': ents,
    }
    os.makedirs(os.path.join(HERE, 'expected'), exist_ok=True)
    with open(os.path.join(HERE, 'expected', prog + '.json'), 'w') as f:
        json.dump(doc, f, indent=1)
    print('mkexpected: %s: %d lines %s' % (prog, len(ents), counts))


if __name__ == '__main__':
    for p in ('p4probe', 'ubcbrk', 'ubcevt', 'pmcsurvey'):
        build(p)
