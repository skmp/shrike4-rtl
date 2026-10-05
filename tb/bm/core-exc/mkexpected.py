#!/usr/bin/env python3
# mkexpected.py -- expected.json of tb/bm/core-exc from the console logs of tools/hw/core-exc (c1i_run1.log: WI-CORE-A
# step A2's discriminating run for rtl/v1/SOC.md 9.1 / 9.16; c1k_run1.log: the WI-CORE-A fixes' F3 run; c1j_run1.log:
# step A3's P claim before an interrupt, added at M1a by WI-SOC).  Owner: WI-CORE-A.
#
# The entry and token format and the classes are tb/bm/soc-design/mkexpected.py's, checked by tb/bm/soc-design/check.py
# (core-exc/check.py runs it on this directory's expected.json).  Per row:
#   core   every exception row's loops, E values and B - A (the P claim, 1 place behind; the EXC_SAVE tag's reach), the
#          TRAPA loops with the access 2 and 3 places behind, the UBC rows' reference loop and their functional record
#          (SSR.T, SPC: the data break's flag restore and SPC)
#   fetch  H'FFFD with the access 2 or 3 places behind (l2 l3 p2 f2, ill): the claim needs those words fetched at t,
#          which v1's 32-bit fetch has not done yet (the console's fetch has): WI-CORE-B's fetch schedule
#   open   the UBC rows' break loops and B - A: 2 cycles less than the model (SOC.md 9.16: a data / operand break with a
#          handler whose first instruction is STC SSR; which of the break's entry and the EXC_SAVE tag's hold of STC SSR
#          is shorter is not separable in this run), and every non-integral console value (mk)
#   python3 mkexpected.py [tools/hw/core-exc] > expected.json
import json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'soc-design'))
from check import keys, toks                                    # noqa: E402
from mkexpected import mk, env                                  # noqa: E402

SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', '..', 'tools', 'hw', 'core-exc')
LOGS = {'c1i': 'c1i_run1.log', 'c1j': 'c1j_run1.log', 'c1k': 'c1k_run1.log'}
FAR = ('l2_h0', 'l3_h0', 'p2_h0', 'f2_h0')                      # the access 2 / 3 places behind X
UBC = ('un_ssr', 'up_ssr')


def block(prog):
    ls = [l.rstrip('\n') for l in open(os.path.join(SRC, LOGS[prog]), errors='replace')]
    s = next(k for k, l in enumerate(ls) if l.startswith(prog + ': ') and not l.startswith(prog + ': done'))
    e = next(k for k in range(s, len(ls)) if ls[k].startswith(prog + ': done'))
    return [l for l in ls[s:e + 1] if l.strip()]


def c1i(ls):
    out = [env(ls[0], 'addresses, the SR of the C caller')]
    cur = None
    for l in ls[1:]:
        t = toks(l)
        if len(t) > 1 and t[1] == 'L':
            cur = t[0]
            if cur in FAR:
                # L ill (3), max ill (9), E ill (14)
                out.append(mk(l, 'core', 'loop periods, E; H\'FFFD with the access 2 / 3 behind: WI-CORE-B\'s fetch',
                              tok_cls={3: 'fetch', 9: 'fetch', 14: 'fetch'}))
            elif cur in UBC:
                # L brk (3), L op (5), max brk / op (9, 10), E brk (14), E op (16)
                out.append(mk(l, 'core', 'the UBC break loops: SOC.md 9.16, 2 cycles below the model (open); the '
                              'reference loop', tok_cls={3: 'open', 5: 'open', 9: 'open', 10: 'open', 14: 'open',
                                                         16: 'open'}))
            else:
                out.append(mk(l, 'core', 'loop periods, E (the P claim, the EXC_SAVE tag\'s reach: SOC.md 9.1)'))
        elif t[0] == 'B-A':
            out.append(mk(l, 'core', 'the handler\'s B - A of the last iteration',
                          tok_cls={2: 'fetch'} if cur in FAR else None))
        elif t[0] == 'T':
            # T/spc/B-A brk (4-6) op (8-10) ref (12-14): T and SPC are the break's functional result
            out.append(mk(l, 'core', 'SSR.T (the flag restore), SPC relative to the loop, B - A (breaks: open)',
                          tok_cls={6: 'open', 10: 'open'}))
        else:
            out.append(mk(l, 'core', 'end'))
    for e in out:                                                 # a token the model does not claim is not compared
        e['tokens'] = [[x, 's' if c == 'open' else m, c] for x, m, c in e['tokens']]
    return out


def c1k(ls):
    """c1k (WI-CORE-A fixes, F3): the data break on a load paired with a delayed branch.  Every K line is compared
    (the break count, SPC relative to the function, the offsets, EXPEVT, BRCR at the handler and after, R9 at the break,
    the return value) except SSR, which holds the T the C caller left (the console's KOS code, not the program's)"""
    out = []
    for l in ls:
        t = toks(l)
        if t[0] == 'K':
            e = mk(l, 'core', 'the data break next to a delayed branch: SPC, the slot done at the break, the branch '
                   'kept', skip=tuple(k for k, x in enumerate(t) if x.startswith('ssr=')))
            # the values are name=value / +offset words: compared as strings ('x', counted)
            e['tokens'] = [[x, 'x' if m == 'w' and ('=' in x or x.startswith('+')) else m, c] for x, m, c in e['tokens']]
            out.append(e)
        elif l.startswith('c1k: DBR'):
            out.append(env(l, 'addresses, the SR of the C caller'))
        else:
            out.append(mk(l, 'core', 'start / end'))
    return out


def c1j(ls):
    """c1j (WI-CORE-A step A3; SOC.md 4.3.3 / 4.4.1 [HW c1j], expected values added by WI-SOC at M1a): ci2's irqloop
    (one TMU1 interrupt per iteration, taken at X right after LDC SR clears BL; the lean handler reads SSR first) with a
    memory instruction at X or 1 / 2 / 3 places after it.  Per variant: the loop with the interrupt (irq, class irq: the
    real TMU / INTC), the same loop at IMASK 15 (ref, class core) and E = irq - ref (irq): the P claim.  The max values
    are compared as the loops (the SoC is deterministic); a non-integral console value is open (mk).  budget_left = the
    handler budget left after the last N2 run: 9 (271 interrupts in 272 iterations: the first iteration's request comes
    through the IMASK path, IMASK 15 -> 14, at s + 9 / 10 [SOC.md 3.4], in the second iteration of a loop of 9 or more
    cycles, whatever the Bclk parity: checked on the SoC with the loop entry shifted by 1 / 2 / 3 NOPs,
    agents/scratch/M1a/c1j_phase).  The console printed 8 for ld1 / st1 (one more interrupt: the request inside the first
    iteration, which then ran with the loop's line cold, 20+ cycles) and 9 for the other nine: 9 is the warm model, 8 a
    cold value (M3).  The header and the done line (KOS's IPRA / TSTR, the link addresses) are env."""
    out = []
    for l in ls:
        t = toks(l)
        if t[0] == 'V':
            # V name irq X max Y ref Z max W E v budget_left=a 280
            e = mk(l, 'irq', 'irq loop (irq), reference loop (core), E = the entry cost with the P claim (irq)',
                   tok_cls={6: 'core', 7: 'core', 8: 'core', 9: 'core'})
            for k, (x, m, c) in enumerate(e['tokens']):
                if x.startswith('budget_left='):
                    e['tokens'][k] = [x, 'iset:9;8', 'irq']
                elif k == len(e['tokens']) - 1 and x.isdigit():
                    e['tokens'][k] = [x, 'x', 'core']              # the IMASK 15 run's budget: no interrupt, 280
            out.append(e)
        else:
            out.append(env(l, 'addresses, the SR of the C caller; KOS\'s IPRA / TSTR at the end'))
    return out


def main():
    progs = []
    for p in LOGS:
        lines = (c1k if p == 'c1k' else c1j if p == 'c1j' else c1i)(block(p))
        for e, k in zip(lines, keys([e['text'] for e in lines])):
            e['key'] = k
        counts = {}
        for e in lines:
            for _, m, c in e['tokens']:
                if m not in ('s', 'w'):
                    counts[c] = counts.get(c, 0) + 1
        progs.append({'program': p, 'log': 'tools/hw/core-exc/' + LOGS[p], 'compared_tokens_by_class': counts,
                      'lines': lines})
    json.dump({'suite': 'core-exc', 'build': 'make -C tb/bm/core-exc (build/bm/core-exc/<prog>.bin at 0x8C010000)',
               'check': 'tb/bm/core-exc/check.py <run output> --level core|fetch|soc|cache', 'programs': progs},
              sys.stdout, indent=1)
    sys.stdout.write('\n')


if __name__ == '__main__':
    main()
