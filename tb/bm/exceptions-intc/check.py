#!/usr/bin/env python3
# check.py -- checks the output of the bare-metal exceptions-intc probes (tb/bm/exceptions-intc: excint, excint2,
# excint3) from a v1 SoC run against expected.json (mkexpected.py, from the console logs).
#   usage: check.py RUN_OUTPUT.txt [--board] [--progs P[,P]] [--level cache] [--kos] [--self]
#     RUN_OUTPUT.txt  the text the programs printed (the sim-control putchar stream; several programs may be in one file)
#     --board         also check the Holly IRL lines (milestone M4 and later)
#     --level cache   M3 (rm: the real I-cache, SOC.md 8.3's M3 row; M3-fix, M3-verify F1): an entry's `cache_regex`
#                     replaces its rule -- excint C4 sleep pinned to the console's own 63-91 (TCNT1 = TCOR - 3 / - 4), no
#                     warm fallback (the architect's ruling, M1b-verify F5); C4 busy the console's 1083-1095 (TCOR - 3 /
#                     - 4) or the warm model's 1062 (TCOR - 1 / - 2).  With --self the console lines must satisfy it too,
#                     and PERTURB_CACHE's lines must not
#     --progs         only these programs (excint, excint2, excint3; default all three)
#     --kos           (M5, WI-SOC; SOC.md 8.3 M5) the console's own ELF (tools/hw/exceptions-intc/*.elf) unmodified under
#                     KOS on the board with the dcload stand-in (soc_tb --dcload): the console's rules (`regex`, not
#                     `soc_regex`, whose INT = 000 holds only for a bare-metal port that took no interrupt -- KOS's vblank
#                     and TMU interrupts ran before, as on the console); a stub line is KOS's own state, so it must be
#                     the console's line except the exception registers the BIOS and dcload left before KOS and KOS's
#                     last interrupt (EXPEVT, INTEVT, TRA: KOS_PREBOOT); implies --board and --level cache (the real
#                     caches).  With --self, the console logs must pass in this mode too
#     --self          check the console logs themselves (a sanity test of the rules: exits 0 when every console line
#                     satisfies its own rule, except the stub lines, whose KOS state is expected to differ); and a
#                     negative test of every `soc_regex` (records before the first interrupt probe, INT pinned to the
#                     SoC's power-on 000): the console line with INT=000 must match it, with INT = its EXP code (a core
#                     whose general-exception entry also writes INTEVT) must not; and the PERTURB lines below: a console
#                     line changed to a value the gate must reject (its regex and its `rel` relations)
# Lines are paired in order, program by program; modes exact / match / timing / stub fail the check, board only with
# --board, info never.  Exit code 1 on any failure.
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))

# --self negative tests: (program, entry id, pattern, replacement, why) -- the console line with every match of pattern
# replaced must NOT satisfy the entry's rule
PERTURB = [
    ('excint', 'C1 ipr_write', r' \d+/\d+', ' 9/9', 'IPR store -> request 9: a write taking effect at its strobe '
     '(SOC.md 9.19 open: k 7-10), not the console'),
    ('excint', 'C1 unie_write', r' \d+/\d+', ' 11/11', 'UNIE store -> request 11: a landing 4 early, below 13-18'),
    ('excint', 'C1 ipr_write', r' \d+/\d+', ' 19/19', 'above 13-18 (a landing one Pcyc late)'),
    ('excint2', 'S PI ipra', r' \d+/\d+', ' 9/9', 'IPRA store sweep at 9: below the console 10-21'),
    # M1a-fix (piwarm [HW]): each point is its warm value or the console's own; a phase off by one is rejected
    ('excint2', 'S PI ipra', r'14/14 17/17', '13/13 17/17', 'n = 3 at 13: neither the warm 14 nor the console\'s 14'),
    ('excint2', 'S PI ipra', r' 15/15 21/21', ' 17/17 21/21', 'n = 0 at 17: neither the warm 18 nor the console\'s 15'),
    ('excint', 'C1 ipr_write', r' 15/15  INT', ' 16/16  INT', 'a warm trial (3-8) that differs from the others'),
    # C2 (M1a): @ counts adds, so the warm acceptance s + 9 / 10 is 7 / 8 (the old rule's 14 / 16 were byte offsets)
    ('excint', 'C2 same_lvl10', r'@0', '@14', 'the byte offset 14, not the add count 7 / 8'),
    ('excint', 'C2 tmu7_imask6', r'@4', '@9', 'accepted one add after s + 10'),
    # C4 (M1a): TCNT1 read before the reload (the interrupt before the underflow), or 5 counts late; PMCTR counting
    # while asleep (the whole ~1040-cycle sleep counted)
    ('excint', 'C4 busy', r'tcnt1=fffffffc', 'tcnt1=ffffffff', 'TCNT1 = TCOR at the handler: no count after the reload'),
    ('excint', 'C4 sleep', r'tcnt1=fffffffb', 'tcnt1=fffffff9', 'TCNT1 5 counts after the reload'),
    ('excint', 'C4 sleep', r'=91 ', '=1046 ', 'PMCTR counted the sleep'),
    # the link-dependent store-queue address (conform-WI-EXC, I109): any SQ0 address, but the form the probe computes,
    # and TEA the address the access used
    ('excint', 'u_wr_sq_md1', r'TEA=e0063740', 'TEA=e0063780', 'SQ TEA must equal r1 (the address the store used)'),
    ('excint', 'u_pref_sq_md1', r'e0063740', 'e0063744', 'the SQ0 address is 0xE0000000 | (buffer & 0x03FFFFC0)'),
    ('excint', 'u_wr_sq_md0', r'r1=e0063740', 'r1=a0063740', 'r1 must be a store-queue address'),
    # C1i is a timing line, but its outcome is pinned: the 0x180 first, the interrupt never taken (console and v1)
    ('excint', 'prio_irq_ill', r' k1 EXP=180', ' k3 EXP=180', 'the interrupt taken before the 0xFFFD'),
    # the stub SR masks T only
    ('excint3', 'excint3:', r'SR 40000001', 'SR 400000f3', 'the stub SR masks T (bit 0) only'),
    # the board rows (M4, WI-SOC): Holly's IRL levels
    ('excint2', 'I holly: ISTNRM', r'IML2 00001008', 'IML2 00000008', 'the maple DMA end not routed to level 2 (KOS\'s routing)'),
    ('excint2', 'irl2_imask1', r'INT=3a0', 'INT=360', 'Holly level 2 taken with level 4\'s code'),
    ('excint2', 'irl4_imask3', r'INT=360', 'INT=3a0', 'Holly level 4 taken with level 2\'s code'),
    ('excint2', 'irl6_imask5', r'INT=320', 'INT=340', 'Holly level 6 taken with a code no level drives'),
    ('excint2', 'irl6_imask5', r'ssr=40000050', 'ssr=40000060', 'level 6 accepted at IMASK 6 (not above it)'),
    ('excint2', 'irl2_imask1', r'spc=L\+2', 'spc=L+4', 'the take outside the DT / BF wait loop'),
    ('excint2', 'irl2_imask2', r' none ', ' #0 ', 'level 2 accepted at IMASK 2 (not above it)'),
]

# --self negative tests of the level-cache rules (M3): the console line perturbed to a value the M3 gate must reject
PERTURB_CACHE = [
    ('excint', 'C4 sleep', r'=91 ', '=53 ', 'the warm handler\'s value (the M1b window\'s derived half): no warm fallback at M3'),
    ('excint', 'C4 sleep', r'=91 ', '=92 ', 'above the console\'s 63-91'),
    ('excint', 'C4 sleep', r'tcnt1=fffffffb', 'tcnt1=fffffffd', 'TCNT1 at TCOR - 2: the warm handler\'s read'),
    ('excint', 'C4 busy', r'=1085 ', '=1055 ', 'below the console\'s 1083-1095 and not the warm 1062'),
    ('excint', 'C4 busy', r'=1085 ', '=1070 ', 'between the warm 1062 and the console\'s 1083'),
    ('excint', 'C4 busy', r'=1085 tcnt1=fffffffc', '=1062 tcnt1=fffffffc', 'the warm 1062 with the cold handler\'s TCNT1'),
]


def rule(e, level):
    """the regex an entry is checked with at this level"""
    return e.get('cache_regex', e['regex']) if level == 'cache' else e['regex']


def satisfies(e, ln, bufs, level=None):
    """the entry's regex and its `rel` relations hold for ln (bufs: the buffers the earlier lines defined)"""
    m = re.fullmatch(rule(e, level), ln)
    if not m:
        return False
    g = {k: int(v, 16) for k, v in m.groupdict().items() if v is not None}
    for a, b, d in e.get('rel', []):
        bv = bufs[int(b[3:])] if b.startswith('buf') and int(b[3:]) < len(bufs) else g.get(b)
        if bv is None or a not in g or (g[a] - bv - d) & 0xFFFFFFFF:
            return False
    return True


def extract(text, prog):
    ls = [l.rstrip() for l in text.splitlines()]
    try:
        i = next(k for k, l in enumerate(ls) if l.startswith(prog + ': SR'))
        j = next(k for k in range(i, len(ls)) if ls[k].startswith(prog + ': done'))
    except StopIteration:
        return None
    return [l for l in ls[i:j + 1] if l.strip()]


# (M5, --kos) the stub-line fields the console's pre-KOS environment (BIOS, dcload) and KOS's last interrupt set
KOS_PREBOOT = re.compile(r'(EXPEVT|INTEVT|TRA) [0-9a-f]{8}')

# (M5, --kos; SOC.md 8.3 M5: "printed values equal to the console logs except timing lines documented as cache- or
# phase-dependent") the timing rows whose value on the unmodified image under KOS depends on the instruction-cache state
# or a clock phase the testbench does not reproduce: the kept excint.elf is not the image the console ran (SOC.md 9.28:
# its code sits 12 bytes later), the dcload stand-in's printf evicts nothing where dcload's network code did (SOC.md
# 7.2), and the chip's IC-miss timing around redirects is open (SOC.md 9.23).  Such a row that misses its rule is
# reported "documented", not failed -- but only its timing TOKENS are free (M5-fix, M5-verify F2): each entry is
# (why, [(regex, kind)]), where every regex's group 'v' (every match) spans freed values; the line with those spans
# masked must equal the console's line masked the same way (INT=, ssr=, spc after SLEEP and every other token exact),
# and each freed value must hold its kind's bound against the console's values of the same regex on that line:
#   dec2x   a decimal count (k: ADDs retired before the take; cyc: Icyc), every number in the span 0 .. 2 x the console's
#           largest (a sanity bound: the cold / warm spreads seen are within 1.8 x -- D cost's trapa 60 against 35)
#   tcnt    TCNT1 after TMU0's reload: 1 .. 2 x the console's counts since the reload (TCOR = ffffffff; 0 counts: a
#           take before the underflow, PERTURB's 'tcnt1=ffffffff')
#   set:A,B one of these words (C4 busy's spc: the busy loop's two instructions, as the rule's L\+[02])
# Only `timing` rows may be listed (--self checks it), every other row must pass.
_K = r'k:(?P<v>(?: \d+/\d+)+)'
_C1CYC = r'handler\):(?P<v>(?: \d+)+)'
_C4 = [(r'cyc\(start->handler\)=(?P<v>\d+)', 'dec2x'), (r'tcnt1=(?P<v>[0-9a-f]{8})', 'tcnt')]
KOS_DOC = {
    ('excint', 'C1 ldc_sr'): ('cache: trial 1 IC-cold on both machines; its k / cycles follow the cold fetch (SOC.md '
                              '9.23)', [(_K, 'dec2x'), (_C1CYC, 'dec2x')]),
    ('excint', 'C1 ldc.l_sr'): ('cache: as C1 ldc_sr', [(_K, 'dec2x'), (_C1CYC, 'dec2x')]),
    ('excint', 'C1 rte'): ('cache: the RTE handler line misses between trials under KOS (console cyc 50, the chip 44-45); '
                           'the acceptance point during that miss is SOC.md 9.23\'s', [(_K, 'dec2x'), (_C1CYC, 'dec2x')]),
    ('excint', 'C1 ipr_write'): ('phase: the store\'s Pclk phase decides k (SOC.md 9.19, [HW] piwarm: 18 16 15 14 17 by '
                                 'phase); the kept image is not the console\'s (9.28), so its phase is not either',
                                 [(_K, 'dec2x')]),
    ('excint', 'C1 unie_write'): ('phase: as C1 ipr_write', [(_K, 'dec2x')]),
    ('excint', 'C4 sleep'): ('cache: the C4 handler\'s IC lines (SOC.md 9.28; the console TCNT1 - 3 / - 4 cold)', _C4),
    ('excint', 'C4 busy'): ('cache: as C4 sleep; the busy loop\'s landing (L+0 / L+2) pairs with another console line',
                            _C4 + [(r'spc=(?P<v>L\+\d+)', 'set:L+0,L+2')]),
    ('excint', 'D cost'): ('cache: the first bracket is the handlers\' cold pass',
                           [(r'\b%s (?P<v>\d+)' % x, 'dec2x') for x in ('base', 'trapa', 'ill', 'dae', 'fd', 'fpe')]),
    ('excint2', 'S PI ipra'): ('cache: a repetition whose probe line is IC-cold (the console: run 2\'s cold repetitions '
                               '17-21; the chip accepts during the miss: 4-7, SOC.md 9.23); the warm values are 9.19\'s',
                               [(r'k\(n=0\.\.11\):(?P<v>(?: \d+/\d+)+)', 'dec2x')]),
}


def kos_doc_check(spec, ln, console):
    """(M5-fix) a documented row: None when only its freed tokens differ and each holds its bound, else the reason"""
    def spans(s, rx):
        return [(m.start('v'), m.end('v')) for m in re.finditer(rx, s)]

    def masked(s):                                   # every freed span's values replaced by '#' (spans never overlap)
        out, p = [], 0
        for a, b in sorted(x for rx, _ in spec for x in spans(s, rx)):
            out.append(s[p:a] + re.sub(r'[0-9a-fA-F]+|L\+\d+', '#', s[a:b]))
            p = b
        return ''.join(out) + s[p:]

    if masked(ln) != masked(console):
        return f'a token the entry does not free differs: {masked(ln)!r} against the console\'s {masked(console)!r}'
    for rx, kind in spec:
        gv = [v for a, b in spans(ln, rx) for v in re.findall(r'L\+\d+|[0-9a-f]+', ln[a:b])]
        wv = [v for a, b in spans(console, rx) for v in re.findall(r'L\+\d+|[0-9a-f]+', console[a:b])]
        if kind == 'dec2x':
            hi = 2 * max(int(v) for v in wv)
            bad = [v for v in gv if not (v.isdigit() and 0 <= int(v) <= hi)]
        elif kind == 'tcnt':
            hi = 2 * max(0xFFFFFFFF - int(v, 16) for v in wv)
            bad = [v for v in gv if not (re.fullmatch(r'[0-9a-f]{8}', v) and 1 <= 0xFFFFFFFF - int(v, 16) <= hi)]
        else:
            bad = [v for v in gv if v not in kind[4:].split(',')]
            hi = kind[4:]
        if bad:
            return f'freed value(s) {", ".join(bad)} outside the bound ({kind}: {hi}) of {rx!r}'
    return None


documented = []                          # (M5, --kos) KOS_DOC rows that missed their rule


def check_prog(prog, exp, lines, board, selfmode, level=None, kos=False):
    fails, checked, bufs = [], 0, []
    if lines is None:
        return [f'{prog}: no output (no "{prog}: SR" ... "{prog}: done")'], 0
    if len(lines) != len(exp):
        fails.append(f'{prog}: {len(lines)} lines printed, {len(exp)} expected (pairing by position may drift)')
    for n, e in enumerate(exp):
        if n >= len(lines):
            fails.append(f'{prog}: missing line for {e["id"]!r}')
            break
        ln, mode = lines[n], e['mode']
        if not ln.startswith(e['id']):
            fails.append(f'{prog} line {n}: {ln[:60]!r} where {e["id"]!r} was expected')
            continue
        if mode == 'info' or (mode == 'board' and not board) or (mode == 'stub' and selfmode):
            if mode == 'stub' and 'buf' in e.get('defines', ''):
                m = re.search(r'buf ([0-9a-f]{8})', ln)
                if m:
                    bufs.append(int(m.group(1), 16))
            continue
        checked += 1
        if mode == 'stub' and (kos or ln == e['console']):
            # a grafted run (tb/bm/graft, M3) or the unmodified image under KOS (--kos, M5) starts from KOS's own state:
            # the console's line (--kos: KOS_PREBOOT's fields free); the buffers it defines are recorded
            if 'buf' in e.get('defines', ''):
                m = re.search(r'buf ([0-9a-f]{8})', ln)
                if m:
                    bufs.append(int(m.group(1), 16))
            if kos and KOS_PREBOOT.sub(r'\1 *', ln) != KOS_PREBOOT.sub(r'\1 *', e['console']):
                fails.append(f'{prog} {e["id"]} (stub, --kos): {ln!r} is not KOS\'s state {e["console"]!r}')
            continue
        if selfmode and 'soc_regex' in e:
            m0 = re.search(r' EXP=([0-9a-f]{3}) ', ln)
            good = re.sub(r' INT=[0-9a-f]{3} ', ' INT=000 ', ln)
            bad = re.sub(r' INT=[0-9a-f]{3} ', f' INT={m0.group(1)} ', ln) if m0 else None
            if not re.fullmatch(e['soc_regex'], good):
                fails.append(f'{prog} {e["id"]}: soc_regex rejects the SoC form {good!r}')
            if bad is None or re.fullmatch(e['soc_regex'], bad):
                fails.append(f'{prog} {e["id"]}: soc_regex accepts INT = EXP ({bad!r})')
        rx = e['soc_regex'] if not selfmode and not kos and 'soc_regex' in e else e.get('regex')
        if level == 'cache' and 'cache_regex' in e:
            rx = e['cache_regex']
        if mode == 'exact' or rx is None:
            if ln != e['console']:
                fails.append(f'{prog} {e["id"]}: {ln!r} != console {e["console"]!r}')
            continue
        m = re.fullmatch(rx, ln)
        if not m:
            if kos and mode == 'timing' and (prog, e['id']) in KOS_DOC:
                why, spec = KOS_DOC[(prog, e['id'])]
                bad = kos_doc_check(spec, ln, e['console'])
                if bad is None:
                    documented.append(f'{prog} {e["id"]}: {ln!r} (console {e["console"]!r}) -- {why}')
                    continue
                fails.append(f'{prog} {e["id"]} (timing, KOS_DOC): {ln!r}: {bad}')
                continue
            fails.append(f'{prog} {e["id"]} ({mode}): {ln!r} does not match; console {e["console"]!r}'
                         + (f'; {e["note"]}' if 'note' in e else ''))
            continue
        g = {k: int(v, 16) for k, v in m.groupdict().items() if v is not None}
        if e.get('defines') == 'buf' and 'buf' in g:
            bufs.append(g['buf'])
        for a, b, d in e.get('rel', []):
            bv = bufs[int(b[3:])] if b.startswith('buf') and int(b[3:]) < len(bufs) else g.get(b)
            if bv is None or a not in g or (g[a] - bv - d) & 0xFFFFFFFF:
                fails.append(f'{prog} {e["id"]}: {a} should be {b} + {d} ({ln!r})')
    if selfmode:
        for p, eid, pat, rep, why in PERTURB:
            if p != prog:
                continue
            e = next((x for x in exp if x['id'] == eid), None)
            if e is None:
                fails.append(f'{prog}: PERTURB entry {eid!r} not found')
                continue
            bad = re.sub(pat, rep, e['console'])
            if bad == e['console']:
                fails.append(f'{prog}: PERTURB {eid!r}: {pat!r} changed nothing')
            elif satisfies(e, bad, bufs):
                fails.append(f'{prog} {eid}: the rule accepts a perturbed line ({why}): {bad!r}')
            checked += 1
        for p, eid, pat, rep, why in PERTURB_CACHE:                   # the level-cache rules, at every level
            if p != prog:
                continue
            e = next((x for x in exp if x['id'] == eid), None)
            if e is None or 'cache_regex' not in e:
                fails.append(f'{prog}: PERTURB_CACHE entry {eid!r} not found or without a cache_regex')
                continue
            bad = re.sub(pat, rep, e['console'])
            if bad == e['console']:
                fails.append(f'{prog}: PERTURB_CACHE {eid!r}: {pat!r} changed nothing')
            elif satisfies(e, bad, bufs, 'cache'):
                fails.append(f'{prog} {eid}: the level-cache rule accepts a perturbed line ({why}): {bad!r}')
            elif not satisfies(e, e['console'], bufs, 'cache'):
                fails.append(f'{prog} {eid}: the level-cache rule rejects the console line {e["console"]!r}')
            checked += 1
    return fails, checked


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    board, selfmode, kos = '--board' in sys.argv, '--self' in sys.argv, '--kos' in sys.argv
    level = None
    if kos:                                            # M5: the board rows and the real caches
        board, level = True, 'cache'
    if '--level' in sys.argv:
        k = sys.argv.index('--level')
        level = sys.argv[k + 1]
        args = [a for a in args if a != level]
        if level != 'cache':
            print(f'check.py: unknown level {level!r} (cache)')
            return 2
    only = None
    if '--progs' in sys.argv:                          # only these programs (the others' output is not in the file)
        k = sys.argv.index('--progs')
        only = set(sys.argv[k + 1].split(','))
        args = [a for a in args if a != sys.argv[k + 1]]
    doc = json.load(open(os.path.join(HERE, 'expected.json')))
    total_fail, total = 0, 0
    for prog, exp in doc['programs'].items():
        if only is not None and prog not in only:
            continue
        if selfmode:
            logs = {'excint': 'results_run1.txt', 'excint2': 'results_run2.txt', 'excint3': 'results_run3.txt'}
            text = open(os.path.join(HERE, '..', '..', '..', 'tools', 'hw', 'exceptions-intc', logs[prog])).read()
        else:
            if not args:
                print(__doc__ if __doc__ else 'usage: check.py RUN_OUTPUT.txt [--board]')
                return 2
            text = open(args[0]).read()
        fails, n = check_prog(prog, exp, extract(text, prog), board, selfmode, level)
        if selfmode and kos:                           # the console's own lines pass the --kos rules
            f2, n2 = check_prog(prog, exp, extract(text, prog), True, False, 'cache', kos=True)
            fails += [f'--kos on the console log: {x}' for x in f2]
            n += n2
            for (p, eid), why in KOS_DOC.items():      # only timing rows may be documented
                if p != prog:
                    continue
                e = next((x for x in exp if x['id'] == eid), None)
                if e is None or e['mode'] != 'timing':
                    fails.append(f'KOS_DOC {p} {eid!r}: {"no such entry" if e is None else "mode " + e["mode"]}, '
                                 'only timing rows may be documented')
        elif kos:
            fails, n = check_prog(prog, exp, extract(text, prog), board, False, level, kos=True)
        total += n
        total_fail += len(fails)
        print(f'{prog}: {n} lines checked, {len(fails)} failures')
        for f in fails[:30]:
            print('  ' + f)
    for d in documented:
        print('  documented (--kos, KOS_DOC): ' + d)
    if kos and not selfmode:
        print(f'exceptions-intc bm: {len(documented)} timing rows documented as cache- or phase-dependent (KOS_DOC)')
    print(f'exceptions-intc bm: {total} lines checked, {total_fail} failures')
    print('exceptions-intc bm: ' + ('PASS' if total_fail == 0 else 'FAIL'))
    return 1 if total_fail else 0


if __name__ == '__main__':
    sys.exit(main())
