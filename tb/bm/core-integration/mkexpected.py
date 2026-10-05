#!/usr/bin/env python3
# mkexpected.py -- expected.json of tb/bm/core-integration from the console logs of tools/hw/core-integration (spec
# core-integration 6: ci_run1.out, ci_run2.out, ci_run3.out).  Owner: WI-CORE-A.
#
# The entry and token format and the classes are tb/bm/soc-design/mkexpected.py's, checked by
# tb/bm/soc-design/check.py (core-integration/check.py runs it on this directory's expected.json).  Two more classes:
#   fpu   needs WI-CORE-F: the FPU kill (a paired / younger FADD, FRCHG / FSCHG), the F1 operand trap (V / Z), the
#         divider's hold split (ci3 FDIV ; P4 read)
#   irq   needs step A3 and the TMU / INTC (the interrupt rows, SLEEP)
#   mmu   needs v1_mmu with TRANSLATE = 1 (the UTLB-miss rows)
# The F / I rows print code and data addresses, which the bare-metal link moves: a code address is compared relative
# to the test's own first recorded address (out[0], or the first record's SPC), a data address relative to the printed
# scratch buffer, PTEH as TEA's page, SSR / SR without T S Q M (the C caller's flags).
#   python3 mkexpected.py [tools/hw/core-integration] > expected.json
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'soc-design'))
from check import keys, toks                                    # noqa: E402
from mkexpected import mk, env, near_int                        # noqa: E402
NUMRE = re.compile(r'^-?\d+(\.\d+)?$')

SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', '..', 'tools', 'hw', 'core-integration')
LOGS = {'ci': ('ci_run1.out', 'ci: sr'), 'ci2': ('ci_run2.out', 'ci2: sr'), 'ci3': ('ci_run3.out', 'T ram_empty'),
        'ci4': ('ci4_run1.out', 'ci4: FPSCR')}
FLAGS = 'hm:fffffcfc'                                           # SSR / SR without M Q S T


def block(prog):
    f, st = LOGS[prog]
    ls = [l.rstrip('\n') for l in open(os.path.join(SRC, f), errors='replace')]
    s = next(k for k, l in enumerate(ls) if l.startswith(st))
    e = next(k for k in range(s, len(ls)) if ls[k].startswith(prog + ': done'))
    return [l for l in ls[s:e + 1] if l.strip()]


def hexv(x):
    try:
        return int(x.split('=')[-1], 16)
    except ValueError:
        return None


CI_T = {'fpu_v': 'fpu', 'fdiv+fpu_v': 'fpu', 'utlb_rd': 'mmu', 'utlb_wr': 'mmu', 'rd_p2ram': 'cache', 'wr_p2ram': 'cache',
        'rd_bcr1': 'soc', 'rd_sar1': 'soc', 'rd_frqcr': 'soc', 'rd_r64cnt': 'soc', 'rd_ipra': 'soc', 'rd_tcnt0': 'soc',
        'rd_scfsr2': 'soc', 'wr_pteh': 'soc', 'wr_ipra': 'soc', 'wr_tcor1': 'soc', 'rd_icaa': 'cache', 'rd_icda': 'cache',
        'rd_itlbaa': 'cache', 'rd_ocaa': 'cache', 'rd_ocda': 'cache', 'rd_utlbaa': 'cache',
        'irqloop': 'irq', 'vbyp': 'fpu', 'vfar': 'fpu'}
CI_WHY = {'fpu': 'needs WI-CORE-F (FPU kill, the F1 operand trap)', 'mmu': 'needs v1_mmu with TRANSLATE = 1',
          'soc': 'a Bclk / Pclk register or PTEH write: the real P4 bridge', 'cache': 'P2 memory (BSC) or an array '
          'access (the forced IC miss): M3', 'irq': 'needs step A3 and the TMU / INTC', 'core': 'the lean-handler loops '
          '(core-integration 6.1: E = cyc - 15) and Iclk / cached accesses'}
# FPU_OUT's out[8] is FR9, which only fpu_z writes: in the other FPU_OUT tests it is the KOS / C code's stale FR9
FR9_STALE = ('pair_fadd_add', 'pair_add_fadd', 'fdiv_trap', 'fpu_i')
CI_F = {'pair_ld_fadd': 'fpu', 'pair_fadd_add': 'fpu', 'pair_add_fadd': 'fpu', 'fdiv_trap': 'fpu', 'fpu_z': 'fpu',
        'utlb_rd': 'mmu', 'utlb_wr': 'mmu', 'frchg_kill': 'fpu', 'fdiv_cause': 'fpu'}


def ci_lines(prog, ls):
    out = []
    # anchors: the header's scratch (data), the "sgr expected" line (the C stack)
    h = toks(ls[0])
    scr_k = h.index('scratch') + 1
    scr = hexv(h[scr_k])
    sgr_line = next((i for i, l in enumerate(ls) if l.startswith('ci: sgr expected')), None)
    out.append(env(ls[0], 'addresses, the KOS state; its scratch address is the data anchor'))
    cur_f = None                                                  # (line index of the F / I line, its class, name)
    first_rec = {}                                                # F line -> index of its first R line (SPC anchor)
    for i in range(1, len(ls)):
        l = ls[i]
        t = toks(l)
        if t[0] in ('F', 'I') and 'out:' in t:
            name = t[1]
            cls = 'irq' if t[0] == 'I' or name.startswith('irq') else CI_F.get(name, 'core')
            if name == 'cal':
                out.append(env(l, 'the PMCTR calibration values'))
                cur_f = (i, 'env', name)
                continue
            o = t.index('out:')
            e = mk(l, cls, 'a functional record (core-integration 6.1 / 6.2)')
            anchor = None
            for k in range(o + 1, len(t)):
                v = hexv(t[k])
                if v is None:
                    continue
                if k == o + 9 and name in FR9_STALE:
                    e['tokens'][k] = [t[k], 's', 'env']
                    continue
                if scr is not None and abs(v - scr) < 0x1000:
                    e['tokens'][k] = [t[k], 'r:0:%d' % scr_k, cls]
                elif 0x8C010000 <= v < 0x8C030000:
                    if anchor is None:
                        anchor = k
                        e['tokens'][k] = [t[k], 's', cls]
                    else:
                        e['tokens'][k] = [t[k], 'r:%d:%d' % (i, anchor), cls]
                else:
                    e['tokens'][k] = [t[k], 'x', cls]
            e['anchor'] = anchor
            out.append(e)
            cur_f = (i, cls, name)
            first_rec[i] = None
            continue
        if t[0] in ('F', 'I', 'T') and 'skipped' in t:
            out.append(env(l, 'a skipped test'))
            cur_f = None
            continue
        if l[:1].isspace() and t[0].startswith('R') and cur_f:
            fi, cls, name = cur_f
            if cls == 'env':
                out.append(env(l, 'calibration'))
                continue
            fe = out[fi]
            e = {'text': l, 'class': cls, 'why': 'the handler\'s record of the entry', 'tokens': []}
            if first_rec.get(fi) is None:
                first_rec[fi] = i
            for k, x in enumerate(t):
                f, _, v = x.partition('=')
                if k == 0:
                    e['tokens'].append([x, 'w', cls]); continue
                if f == 'dt':
                    # the entry time stamp: R0 of a single-pass test ran cold on the console (IC); a second pass is
                    # warm and exact (ci2's R1); the MAC / FRCHG ones print stale stamps
                    warm = prog == 'ci2' and t[0] == 'R1' and name in ('trapa', 'illegal')
                    e['tokens'].append([x, 'i' if False else ('x' if warm else 's'), cls if warm else 'cache'])
                elif f in ('vec', 'expevt', 'tra'):
                    e['tokens'].append([x, 'x', cls])
                elif f in ('ssr', 'sr'):
                    e['tokens'].append([x, FLAGS, cls])
                elif f == 'sgr':
                    e['tokens'].append([x, 'r:%d:%d' % (sgr_line, len(toks(ls[sgr_line])) - 1), cls]
                                       if sgr_line is not None else [x, 's', 'env'])
                elif f == 'spc' or f == 'pr':
                    hv = hexv(x)
                    if fe.get('anchor') is not None and 0x8C010000 <= hv < 0x8C030000 and \
                            (f == 'spc' or name in ('slot_bsr', 'slot_jsr', 'slot_rts')):
                        e['tokens'].append([x, 'r:%d:%d' % (fi, fe['anchor']), cls])
                    elif f == 'spc' and first_rec[fi] != i:
                        e['tokens'].append([x, 'r:%d:%d' % (first_rec[fi], k), cls])
                    else:
                        e['tokens'].append([x, 's', 'env'])     # the C caller's PR / no anchor
                elif f == 'tea':
                    hv = hexv(x)
                    if scr is not None and abs(hv - scr) < 0x1000:
                        e['tokens'].append([x, 'r:0:%d' % scr_k, cls])
                    else:
                        e['tokens'].append([x, 'x', 'mmu' if cls == 'mmu' else 'env'])
                elif f == 'pteh':
                    tk = next(j for j, y in enumerate(t) if y.startswith('tea='))
                    e['tokens'].append([x, 'pt:%d' % tk, cls if cls != 'irq' else 'irq'])
                elif f == 'intevt':
                    e['tokens'].append([x, 'x' if cls == 'irq' else 's', cls if cls == 'irq' else 'env'])
                else:
                    e['tokens'].append([x, 's', 'env'])
            out.append(e)
            continue
        if t[0] == 'T':
            name = t[1]
            cls = CI_T.get(name, 'core')
            e = mk(l, cls, CI_WHY[cls])
            # budget_left= words: the number of entries left in the handler's budget (exact where the loop is the core's)
            for k, x in enumerate(t):
                if x.startswith('budget_left='):
                    e['tokens'][k] = [x, 'x', cls]
            out.append(e)
            continue
        if l.startswith('ci: sgr expected'):
            out.append(env(l, 'the C stack pointer (the anchor of SGR)'))
            continue
        if l.startswith(prog + ': done') or l.startswith(prog + ': SLEEP'):
            out.append(env(l, 'KOS register values'))
            continue
        out.append(env(l, 'not compared'))
    return out


# M1a (WI-SOC, 2026-09-24): the interrupt rows whose acceptance goes through the IMASK path (an SR writer lowers IMASK
# with TMU1 pending: LDC SR, LDC.L SR, RTE, and LDC SR ; k NOPs ; BRA ; slot).  The request comes at s + 9 or s + 10
# after the SR writer's S by the Bclk parity of s (SOC.md 3.4 [HW]); the bare-metal build's code between the last
# synchronising access and the test is not KOS's, so its parity is its own, and the console's own repetitions show both
# (ci irq_ldc 18 then 16, ci2 irq_rte 12 / 14).  So a warm console SPC c admits c - 2, c, c + 2 (one add either way)
# -- or exactly the pair, where the console shows both in one test.  ci2's first pass (R0) ran cold on the console in
# most brK tests (14, 2, 18, 2, 22, 2, 18 against R1's 24 .. 30), so the brK sets come from R1.  Console records that ran with the
# instruction cache cold (the SPC is the word after the SR writer, or a line boundary: D starved, SPC = the fetch PC,
# SOC.md 4.4.1) are not the warm model: the warm set is taken from a warm record of the same test, and the cold value is
# accepted only at a level with the 'cache' class (M3); a token whose test has no warm console record is class 'cache'.
# The stale EXPEVT the ci I-row handlers record is the UTLB-miss probe's (F utlb_wr: 0x060), which runs only with
# TRANSLATE = 1: class 'mmu' (check.py --translate 0 leaves it out).
IRQ_WARM = {
    # (prog, test): warm set of each record in print order, and the cold values (None: the record is warm)
    # ci I rows: three repetitions of ldc / ldcl / rte, one of each brK (anchor out[0] = the SR writer)
    ('ci', 'irq_ldc'): ([(16, 18), (16, 18), (16, 18)], [2, None, None]),        # console 2 (cold), 18, 16
    ('ci', 'irq_ldcl'): ([(16, 18, 20)] * 3, [2, None, 2]),                      # console 2, 18, 2
    ('ci', 'irq_rte'): (None, None),                                             # console 14 14 14: target + 6 is a
                                                                                  # line boundary (cold); see ci2 rte
    ('ci', 'irq_br1'): ([(18, 20, 22)], [None]),                                # console 20
    ('ci', 'irq_br3'): ([(20, 22, 24)], [None]),                                # console 22
    ('ci', 'irq_br5'): ([(22, 24, 26)], [None]),                                # console 24
    # ci brK, K = 0 2 4 6 7: the console's one record is 2 (the word after LDC SR: cold), no warm record
    ('ci', 'irq_br0'): (None, None), ('ci', 'irq_br2'): (None, None), ('ci', 'irq_br4'): (None, None),
    ('ci', 'irq_br6'): (None, None), ('ci', 'irq_br7'): (None, None),
    # ci2 F rows: two passes (R0, R1); anchor out[0] = the first add after LDC SR / the RTE target / the LDC SR (brK)
    # ci2 irq_ldc is ci's irq_ldc sequence (LDC SR, then adds) with the anchor on the first add: ci's two warm console
    # values 18 / 16 from the LDC SR are both parities, so 16 / 14 here exactly
    ('ci2', 'irq_ldc'): ([(14, 16), (14, 16)], [0, None]),                       # 0 (cold), 14
    ('ci2', 'irq_rte'): ([(12, 14), (12, 14)], [None, None]),                   # 12, 14
    ('ci2', 'irq_br0'): ([(18, 20, 22)] * 2, [None, None]),                      # R0 18, R1 20
    ('ci2', 'irq_br1'): ([(22, 24, 26)] * 2, [14, None]),                        # 14 (cold), 24
    ('ci2', 'irq_br2'): ([(22, 24, 26)] * 2, [2, None]),
    ('ci2', 'irq_br3'): ([(24, 26, 28)] * 2, [None, None]),                      # 26, 26
    ('ci2', 'irq_br4'): ([(22, 24, 26)] * 2, [18, None]),
    ('ci2', 'irq_br5'): ([(24, 26, 28)] * 2, [2, None]),
    ('ci2', 'irq_br6'): ([(24, 26, 28)] * 2, [22, None]),
    ('ci2', 'irq_br7'): ([(28, 30, 32)] * 2, [2, None]),
    ('ci2', 'irq_br8'): ([(28, 30, 32)] * 2, [18, None]),
    ('ci2', 'irq_br9'): ([(30, 32, 34)] * 2, [None, None]),                      # R0 30, R1 32
}


def irq_sets(prog, out):
    """rewrite the SPC tokens of the IMASK-path interrupt rows (IRQ_WARM) and the stale EXPEVT of ci's I rows"""
    seen = {}
    cur = None
    for e in out:
        t = toks(e['text'])
        if t and t[0] in ('F', 'I') and 'out:' in t:
            cur = t[1] if t[1].startswith('irq') else None
            continue
        if not cur or not e['text'][:1].isspace() or not t[0].startswith('R'):
            if not e['text'][:1].isspace():
                cur = None
            continue
        if prog == 'ci':
            for k, (x, m, c) in enumerate(e['tokens']):
                if x.startswith('expevt=') and c == 'irq':
                    e['tokens'][k] = [x, m, 'mmu']
        key = (prog, cur)
        if key not in IRQ_WARM:
            continue
        n = seen.get(key, 0)
        seen[key] = n + 1
        warm, cold = IRQ_WARM[key]
        # the record's index within its test (R0, R1): ci repeats a test as separate I lines, ci2 prints R0 and R1
        ri = n if prog == 'ci' else int(t[0][1:])
        if prog == 'ci2':
            seen[key] = n + 1
        for k, (x, m, c) in enumerate(e['tokens']):
            if not x.startswith('spc=') or not m.startswith('r:'):
                continue
            if warm is None:
                e['tokens'][k] = [x, m, 'cache']
                continue
            i = ri if prog == 'ci2' else min(ri, len(warm) - 1)
            spec = ','.join(str(v) for v in warm[i]) + ('' if cold[i] is None else ';%d' % cold[i])
            e['tokens'][k] = [x, 'rset:' + m[2:] + ':' + spec, c]
    return out


def ci3_lines(ls):
    out = []
    for l in ls:
        t = toks(l)
        if t[0] == 'T':
            n = t[1]
            if n.startswith('p2_'):
                out.append(mk(l, 'cache', 'P2 (uncached) loads / stores: the BSC (M3)'))
            elif n == 'p4_fdiv':
                out.append(mk(l, 'fpu', 'FDIV across a P4 read: the divider keeps running under mah (WI-CORE-F hold_div, '
                                        'SOC.md 4.5): 15, v1\'s frozen divider 17'))
            else:
                out.append(mk(l, 'core', 'a cached / P4 (Iclk) load at the loop head (core-integration 6.3)'))
        else:
            out.append(env(l, 'KOS register values'))
    return out


# ci4 (M3, WI-SOC; SOC.md 8.3 "core-integration ci4 ... its expected values are M3's to add"): cycles per iteration
# (min of 5 x N2 - min of 5 x N1) / 256, as ci3.  The ram_* loops (a cached load between an FPU / MUL op and its
# consumer) are the FPU / MA hold's (class fpu, WI-CORE-F) and integral: exact.  The p2_* (an uncached load) and miss_*
# (an operand-cache miss) loops go through the BSC (class cache) and carry the refresh share: one refresh per 3008 Icyc
# costs 24 Icyc to a stream of RAM accesses [HW bsc-memmap 6.2], and the minimum N2 and N1 runs may each meet one
# refresh more or fewer on the console than in the model (the refresh timer runs free of the program).  A line prints
# the period from the minimum runs and, after "max", from the maximum runs.  M3-fix (M3-verify F8): a minimum run meets
# a collision only if all five do, so the minimum period is compared within one collision, 24 / 256 = 0.094 Icyc (token
# mode f:0.0938 of tb/bm/soc-design/check.py; the 2 x 24 / 256 used before accepted the bare-metal layout's artifacts);
# the maximum runs are the ones that met the most, and the N2 and the N1 maximum may each carry one collision more or
# fewer on either machine: two collisions, 2 x 24 / 256 = 0.1875 (the grafted chip's miss_fdm max 34.094 against the
# console's 34.242: 38 Icyc per 256 iterations, its minimum 34.070 against 34.125)
CI4_TOL = 24 / 256
CI4_TOL_MAX = 2 * 24 / 256


def ci4_lines(ls):
    out = []
    for l in ls:
        t = toks(l)
        if t[0] == 'T':
            n = t[1]
            if n.startswith(('p2_', 'miss_')):
                e = mk(l, 'cache', 'an uncached load (p2_) or an operand-cache miss (miss_) in the loop: the BSC and '
                                   'the refresh share (M3), within %.4f per iteration (after "max": %.4f)' %
                                   (CI4_TOL, CI4_TOL_MAX))
                after_max = False
                for x in e['tokens']:
                    if NUMRE.match(x[0]):
                        x[1], x[2] = 'f:%.4f' % (CI4_TOL_MAX if after_max else CI4_TOL), 'cache'
                    after_max = x[0] == 'max'
                out.append(e)
            else:
                out.append(mk(l, 'fpu', 'a cached load between an FPU / MUL op and its consumer: the FPU / MA hold '
                                        '(WI-CORE-F, SOC.md 4.5)'))
        elif l.startswith('ci4: done'):
            out.append(mk(l, 'core', 'end'))
        else:
            out.append(env(l, 'the KOS FPSCR'))
    return out


def main():
    progs = []
    for p in LOGS:
        ls = block(p)
        lines = (ci3_lines(ls) if p == 'ci3' else ci4_lines(ls) if p == 'ci4' else irq_sets(p, ci_lines(p, ls)))
        for e, k in zip(lines, keys([e['text'] for e in lines])):
            e['key'] = k
            e.pop('anchor', None)
        counts = {}
        for e in lines:
            for _, m, c in e['tokens']:
                if m not in ('s', 'w'):
                    counts[c] = counts.get(c, 0) + 1
        ent = {'program': p, 'log': 'tools/hw/core-integration/' + LOGS[p][0], 'compared_tokens_by_class': counts,
               'lines': lines}
        if p in ('ci3', 'ci4'):
            ent['start'] = LOGS[p][1]
        progs.append(ent)
    json.dump({'suite': 'core-integration',
               'build': 'make -C tb/bm/core-integration (build/bm/core-integration/<prog>.bin at 0x8C010000)',
               'check': 'tb/bm/core-integration/check.py <run output> --level core|fetch|soc|cache', 'programs': progs},
              sys.stdout, indent=1)
    sys.stdout.write('\n')


if __name__ == '__main__':
    main()
