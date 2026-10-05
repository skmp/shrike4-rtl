#!/usr/bin/env python3
# mkexpected.py -- expected.json of tb/bm/soc-design from the console logs of tools/hw/soc-design (rtl/v1/SOC.md
# Appendix A: runs 1-8 of C1 and the C2 array-cost runs).  Owner: WI-CORE-A.
#
# Every line the console printed between "<prog>:" and "<prog>: done" becomes one entry: its text and its tokens
# (split on white space and '/'), each token with a compare mode and a class:
#   mode  'w'  a word: must be equal (the line's structure)
#         'i'  a number compared as an integer: the console value must be within 0.03 of an integer (a loop period
#              (N2 - N1) / 256 carries 1/256-cycle noise on the console: 26.004, 27.988), the run's value too, and the
#              two integers equal.  A console value farther from an integer is class 'open' (SOC.md 9.2)
#         'x'  compared as a string
#         's'  not compared (the first, cold repetition of a bracket; words the console printed as garbage)
#         'iset:W[;C]'  compared as an integer against a set (check.py): the values the structure gives over the Bclk /
#              Pclk phases, where the bare-metal build cannot reproduce the console's phase (M1a, WI-SOC)
#   class 'core'  reproducible by v1_core on v1_ideal_mem with an Iclk-class P4 stub, v1_ubc and v1_perfctr
#                 (make v1-exc: check.py --level core)
#         'fetch' needs WI-CORE-B's fetch schedule (c1b j0: the word after a JSR target fetched before the target
#                 issues; SOC.md 9.1)
#         'soc'   needs the real P4 bridge (Bclk / Pclk classes, the array slave's stall) or the LSU / fetch events
#                 (M1a: --level soc)
#         'cache' needs the IC / OC (IC-evicted repetitions on the console, the forced IC miss after an array access:
#                 M3, --level cache)
#         'open'  a console value the model does not claim (SOC.md 9.2 points, non-integral loops)
#         'env'   the console's environment (addresses, the SR the C caller had, garbage the program printed)
#   python3 mkexpected.py [tools/hw/soc-design] > expected.json
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from check import keys                                         # the pairing key of a line (check.py)
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', '..', 'tools', 'hw', 'soc-design')
NUM = re.compile(r'^-?\d+(\.\d+)?$')

LOGS = {'c1': 'c1_run1.log', 'c1b': 'c1b_run1.log', 'c1c': 'c1c_run1.log', 'c1d': 'c1d_run1.log',
        'c1e': 'c1e_run1.log', 'c1f': 'c1f_run1.log', 'c1g': 'c1g_run1.log', 'c1h': 'c1h_run1.log',
        'c2': 'c2_run2.log', 'c2b': 'c2b_run1.log', 'c2c': 'c2c_run1.log'}


def toks(line):
    return re.findall(r'[^\s/()]+', line)


def block(prog):
    """the console lines of the program's last complete run"""
    ls = [l.rstrip('\n') for l in open(os.path.join(SRC, LOGS[prog]), errors='replace')]
    starts = [k for k, l in enumerate(ls) if l.startswith(prog + ': ') and not l.startswith(prog + ': done')]
    for s in reversed(starts):
        for e in range(s, len(ls)):
            if ls[e].startswith(prog + ': done'):
                return [l for l in ls[s:e + 1] if l.strip()]
    raise SystemExit('no complete run of %s in %s' % (prog, LOGS[prog]))


def near_int(v):
    return abs(v - round(v)) <= 0.03


def mk(line, cls, why, skip=(), cold_first=False, tok_cls=None):
    """an entry: numbers 'i' (non-integral console values -> 'open'), words 'w'; `skip`: token indices not compared;
    cold_first: the first number after the words is the cold repetition; tok_cls: {index: class} overrides"""
    t = toks(line)
    out, first_num = [], True
    for k, x in enumerate(t):
        c = (tok_cls or {}).get(k, cls)
        if NUM.match(x):
            m = 'i'
            if k in skip or (cold_first and first_num):
                m = 's'
            elif not near_int(float(x)):
                m, c = 's', 'open'
            first_num = False
            out.append([x, m, c])
        else:
            out.append([x, 's' if k in skip else 'w', c])
    return {'text': line, 'class': cls, 'why': why, 'tokens': out}


def env(line, why):
    return {'text': line, 'class': 'env', 'why': why, 'tokens': [[x, 's', 'env'] for x in toks(line)]}


# ---- per program
C1_EVICTED = {'b1_ill', 'b3_ill', 'b4_ill', 'b1_fpe', 'b2_fpe', 'b6_dae', 'b3_trap', 'b4_trap'}
# M3 (WI-SOC; tb/bm/graft): run grafted -- the console's own image, so every IC index is the console's -- these handler
# lines stay warm in every repetition on rm (13-17, the core values), so the console's 27-36 came from what ran between
# its repetitions that the graft does not have: the printf of each value (newlib, KOS's fs, dcload's syscall and network
# code); b3_ill is evicted by the probe's own code in the console layout as well (the model: 30, the console 27)
C1_PRINTF_EVICTED = C1_EVICTED - {'b3_ill'}
PRINTF_WHY = ('an IC eviction by the console\'s printf between repetitions (dcload\'s network path): warm in the '
              'grafted image on rm (M3, tb/bm/graft)')
C1_OPEN = {'b6_ref': 'SOC.md 9.2: the BRA-reference b6 bracket is 11, the model 9',
           'b0_dae': 'SOC.md 9.2: b0_dae 19 (a misaligned load issued in the ack cycle of a PMCTR read), the model 14'}


def c1(ls):
    out = [env(ls[0], 'addresses, the SR of the C caller')]
    for l in ls[1:]:
        t = toks(l)
        if t[0] == 'L' or t[0] == 'E':
            out.append(mk(l, 'core', 'loop period / E = L - Lref + 2 (SOC.md A.1 run 1)'))
        elif t[0] == 'B':
            nm = t[1]
            if nm in C1_PRINTF_EVICTED:
                out.append(mk(l, 'open', PRINTF_WHY, cold_first=True))
            elif nm in C1_EVICTED:
                out.append(mk(l, 'cache', 'IC-evicted repetitions on the console (SOC.md A.1: 27-36); evicted by the '
                                          'probe\'s own code in the console layout too (M3)', cold_first=True))
            elif nm in C1_OPEN:
                out.append(mk(l, 'open', C1_OPEN[nm], cold_first=True))
            else:
                out.append(mk(l, 'core', 'PMCTR bracket B - A (the first repetition cold)', cold_first=True))
        elif t[0] == 'P':
            # P p_x lean: r1..r6 ci2-handler (pass1 pass2): a1 b1 ... a6 b6: the first repetition of each is cold
            sk = {3, 12, 13}
            out.append(mk(l, 'core', 'two-pass brackets (the first repetition cold)', skip=sk))
        elif t[0] == 'Q':
            out.append(mk(l, 'core', 'direct brackets, recording handler', cold_first=True))
        elif t[0] == 'W':
            nm = t[1]
            if nm == 'ipra16':
                # the B - A values (27) open, the C - B values (3) the Pclk write's, as the other IPRA rows
                oc = {k: 'open' for k, x in enumerate(t) if k >= 4 and (k - 4) % 2 == 0}
                out.append(mk(l, 'soc', 'IPRA write, 16 NOPs: B - A 27 is an IC miss on the console (the others are 6 / 7 '
                                        '/ 9): ' + PRINTF_WHY, tok_cls=oc))
            elif nm.startswith('ipra'):
                out.append(mk(l, 'soc', 'a Pclk-class write (IPRA): the real bridge (the core harness stub is Iclk)'))
            else:
                out.append(mk(l, 'core', 'an Iclk write (BARA) between PMCTR reads (SOC.md A.4)'))
        elif t[0] == 'S' and t[1] == 'event':
            out.append(mk(l, 'core', 'section header'))
        elif t[0] == 'S':
            out.append(mk(l, 'core', 'PMCTR sample point (SOC.md A.3; the first repetition cold)', cold_first=True))
        else:
            out.append(mk(l, 'core', 'end'))
    return out


def c1b(ls):
    out = [env(ls[0], 'addresses, the SR of the C caller')]
    for l in ls[1:]:
        t = toks(l)
        if t[0] == 'L':
            if t[1] == 'v4':
                out.append(mk(l, 'open', 'SOC.md 9.2: handler H4 (STC SPC, then B) loops are non-integral (27.285)'))
            else:
                out.append(mk(l, 'core', 'loop period per handler shape (SOC.md A.1 run 2)'))
        elif t[0] == 'B':
            key = ' '.join(t[1:4])
            if key == 'v3 b3 ref':
                out.append(mk(l, 'cache', 'IC effects on the console (55 33 33 ...)', cold_first=True))
            elif t[1] == 'v4':
                out.append(mk(l, 'open', 'SOC.md 9.2: handler H4 brackets (40 / 23 on the console: IC or H4 behaviour)', cold_first=True))
            elif key == 'v3 j0 ill':
                out.append(mk(l, 'fetch', 'j0: the P claim needs the word after the JSR target fetched before the target issues '
                                          '(WI-CORE-B fetch schedule, SOC.md 9.1); v1_fetch gives 8', cold_first=True))
            else:
                out.append(mk(l, 'core', 'brackets: b3 / direct d0 d2 / JSR target j0 j2 (SOC.md A.2)', cold_first=True))
        else:
            out.append(mk(l, 'core', 'end'))
    return out


def c1c(ls):
    out = [env(ls[0], 'addresses, the SR of the C caller')]
    cur = None
    for l in ls[1:]:
        t = toks(l)
        if t[0].startswith('V') and t[0][1:].isdigit():
            cur = t[0]
            cls = 'soc' if cur in ('V2', 'V3') else 'core'
            why = 'a Bclk / Pclk register access inside the handler (BCR1, TCOR0): the real bridge' if cls == 'soc' \
                else 'stamps inside the handler: loop periods (SOC.md A.1 run 3)'
            out.append(mk(l, cls, why))
        elif t[0] in ('ill', 'ref'):
            cls = 'soc' if cur in ('V2', 'V3') else 'core'
            if cur == 'V4' and t[0] == 'ill':
                out.append(mk(l, 'open', 'SOC.md 9.2: run 3 V4 (B1 ; ld ; B2) enters at 13, the model and the other '
                                         'handler shapes 14', skip={4, 5, 6} | {6 + 3 * k for k in range(1, 7)}))
                continue
            # ill B1-A B2-B1 B3-B2: then triplets; the first triplet cold; B3-B2 is garbage after V0 (the program
            # prints a stale third stamp)
            sk = {4, 5, 6}
            if cur != 'V0':
                sk |= {6 + 3 * k for k in range(1, 7)}
            e = mk(l, cls, 'B1 - A / B2 - B1 / B3 - B2 per repetition (the first cold)', skip=sk)
            if cur in ('V2', 'V3'):
                # M1a (WI-SOC).  B2 - B1 brackets one Bclk (BCR1) / Pclk 32-bit (TCOR0) read in the handler: its cost is
                # the isolated read's by the phase of B1's MA1 (SOC.md 3.1.5 [HW p4b]: Bclk 8 / 9, Pclk rd32 17..20; +3
                # for B1 itself).  Nothing in the loop between repetitions synchronises to Bclk / Pclk, and the phase
                # drifts: the console's own repetitions show both Bclk values (V2 ill 11 11 12 12 12 12) and 20 / 22
                # (V3).  The bare-metal build's C code between repetitions is not KOS's, so its phase is its own: every
                # value of the set is the structure's
                ph = '11,12' if cur == 'V2' else '20,21,22,23'
                for k in range(7, len(e['tokens']), 3):                  # B2 - B1 of repetitions 2..6
                    x, m, c = e['tokens'][k + 1]
                    if m == 'i':
                        e['tokens'][k + 1] = [x, 'iset:' + ph, c]
                if cur == 'V3' and t[0] == 'ill':
                    # V3 ill B1 - A: 27 in every repetition on the console, = V2's cold first repetition (27) and 13
                    # above the model's 14 (every other handler shape, V2 ill included, gives 14 after the first): the
                    # V3 handler line (c1_vt3) was evicted from the instruction cache between repetitions on the
                    # console (an IC index conflict with the C code run between them); IDEAL_MEM = 1 has no miss.  M3
                    # (the real IC; the bare-metal layout differs from KOS's, so the conflict may not recur there)
                    for k in range(7, len(e['tokens']), 3):
                        x, m, c = e['tokens'][k]
                        if m == 'i':
                            e['tokens'][k] = [x, m, 'cache']
            out.append(e)
        else:
            out.append(mk(l, 'core', 'end'))
    return out


def c1de(ls):
    out = [env(ls[0], 'addresses, the SR of the C caller')]
    for l in ls[1:]:
        out.append(mk(l, 'core', 'E against an older register access / cached load / P4 write K instructions before X '
                                   '(SOC.md A.1 runs 4 / 5)'))
    return out


def c1fgh(ls, prog):
    out = [env(ls[0], 'addresses, the SR of the C caller')]
    cur = None
    for l in ls[1:]:
        t = toks(l)
        if len(t) > 1 and t[1] == 'L':
            cur = t[0]
            out.append(mk(l, 'core', 'loop periods, E (SOC.md A.1 runs 6-8)'))
        elif t[0] == 'B-A':
            if prog == 'c1f' and cur and cur.startswith('r'):
                out.append(env(l, 'the program prints stale stamps for its r rows'))
            elif prog == 'c1h' and cur == 'st' and t[1] == 'ill:':
                out.append(mk(l, 'open', 'SOC.md 9.2: c1h Y = store, the loop is non-integral; its brackets 10 / 11',
                              cold_first=True))
            elif prog == 'c1g' and cur == 'p2' and t[1] == 'ill:':
                out.append(mk(l, 'open', 'SOC.md 9.2: run 7 p2 brackets alternate 10 / 11 on the console', cold_first=True))
            else:
                out.append(mk(l, 'core', 'in-loop brackets' + (' (the first cold)' if prog != 'c1f' else ''),
                              cold_first=prog != 'c1f'))
        else:
            out.append(mk(l, 'core', 'end'))
    return out


C2C_COLS = {3: 'soc', 4: 'soc', 5: 'cache', 6: 'cache', 7: 'cache', 8: 'soc', 9: 'core', 10: 'core', 11: 'soc',
            12: 'soc', 14: 'core'}


def c2(ls, prog):
    out = []
    for i, l in enumerate(ls):
        t = toks(l)
        arr = any(x in ('ocaa', 'ocda', 'utlbaa', 'utlbda', 'icaa', 'icda', 'itlbaa') for x in t)
        if i == 0:
            out.append(env(l, 'addresses') if prog == 'c2' else mk(l, 'core', 'header'))
        elif t[0] == 'E':
            if arr:
                out.append(mk(l, 'cache', 'events of the array loops: the forced IC miss (SOC.md A.5, M3)'))
            else:
                # events 0x06 0x0a (fetch: WI-CORE-B), 0x08 0x21 0x24 (IC), 0x25 0x0d 0x09 (LSU / OC): M1a / M3;
                # 0x27, 0x13 and the cycles are the core's
                out.append(mk(l, 'core', 'PMC events per iteration (columns by owner)', tok_cls=C2C_COLS))
        elif t[0] == 'L' or t[0] == 'A':
            out.append(mk(l, 'cache' if arr else 'core', 'array loops: the forced IC miss (M3)' if arr
                          else 'Iclk register / cached RAM loops'))
        elif t[0] == 'B':
            out.append(mk(l, 'soc' if arr else 'core', 'array brackets: the array slave\'s stall (the real bridge)' if arr
                          else 'Iclk register / cached RAM brackets'))
        else:
            out.append(mk(l, 'core', 'end'))
    return out


def main():
    progs = []
    for p in LOGS:
        ls = block(p)
        if p == 'c1':
            lines = c1(ls)
        elif p == 'c1b':
            lines = c1b(ls)
        elif p == 'c1c':
            lines = c1c(ls)
        elif p in ('c1d', 'c1e'):
            lines = c1de(ls)
        elif p in ('c1f', 'c1g', 'c1h'):
            lines = c1fgh(ls, p)
        else:
            lines = c2(ls, p)
        for e, k in zip(lines, keys([e['text'] for e in lines])):
            e['key'] = k
        counts = {}
        for e in lines:
            for _, m, c in e['tokens']:
                if m != 's' and m != 'w':
                    counts[c] = counts.get(c, 0) + 1
        progs.append({'program': p, 'log': 'tools/hw/soc-design/' + LOGS[p], 'compared_numbers_by_class': counts,
                      'lines': lines})
    json.dump({'suite': 'soc-design', 'build': 'make -C tb/bm/soc-design (build/bm/soc-design/<prog>.bin at 0x8C010000)',
               'check': 'tb/bm/soc-design/check.py <run output> --level core|fetch|soc|cache', 'programs': progs},
              sys.stdout, indent=1)
    sys.stdout.write('\n')


if __name__ == '__main__':
    main()
