#!/usr/bin/env python3
"""check.py -- the expected output of tb/bm/soc-adv/m2, the M2 verification's adversarial programs
(agents/reports/integration/M2-verify.md; its check_m2.py), the M2 gate's regression tests since M2-fix.md.

  check.py RUNDIR [--progs a,b]   RUNDIR/<prog>.log (the putchar stream) and RUNDIR/<prog>.status (soc_tb's verdict:
                                  exit code 0 with "lockstep pass"), every program or the listed ones
  check.py FILE.log --progs P     the output lines of one log of program P only (the planted-bug runs)
  check.py --self RUNDIR          every pinned line that passes must fail when one value in it is perturbed

Each expectation is a regex that must match one whole line of the program's output, with the source of the value:
[M] hm / pm, [S] SOC.md (section), [HW] a console measurement, [T] the test's own arithmetic (a translated run equal to
the untranslated reference), [P] the RTL's structure where the manual and the console are silent.  M2-fix: the window
cases win_c<HEX> and the unpinned dpage (the verifier's findings F1-F3, F6) are pinned and in lockstep too, and ubcasid
(the console's tools/hw/mmu/ptehubc) is new.
"""
import re, sys, os

E = {}   # prog -> list of (regex, source)

def exp(prog, rx, src):
    E.setdefault(prog, []).append((rx, src))

# ---------------------------------------------------------------- win (default parts, CASE 0x1F)
for n in (3, 4, 5, 8):
    exp('win', rf'WIN A n={n} exc=1 \[040 tea=10200040 pteh=10200002\] r0=deadbeef',
        '[M hm 3.3.7, 3.5.3; S 6.2] 3+ NOPs after PTEH.ASID 1 -> 2 the ASID-1 page misses: 0x040, TEA = VA, PTEH = VPN | ASID 2')
    exp('win', rf'WIN B n={n} exc=0 r0=600d0010', '[M] the ASID-2 page hits after the ASID change')
    exp('win', rf'WIN U n={n} exc=1 \[040 tea=10300048 pteh=10300001\] r0=deadbeef urc=5',
        '[M hm 3.2 (4 instructions); mmu 3.6] AT 0 -> 1 then an unmapped P0 load: 0x040, URC not counted (5)')
for n in (2, 3, 4, 5, 8):
    exp('win', rf'WIN S01 n={n} exc=0 r0=600d0011 urc=8',
        '[M hm 3.3.7 SV = 1: privileged ASID ignored] SV 0 -> 1, the ASID-2 page hits with PTEH.ASID 1; URC 7 + 1')
    exp('win', rf'WIN S10 n={n} exc=1 \[040 tea=10200044 pteh=10200001\] r0=deadbeef urc=7',
        '[M] SV 1 -> 0: ASID compared, 0x040; URC stays 7')
for n in (0, 1):
    exp('win', rf'WIN S01 n={n} exc=1 \[040 tea=10200044 pteh=10200001\] r0=deadbeef urc=7',
        '[S 6.2 landing window] SV 0 -> 1: the load in the group held in EX behind the store (lane 1 of the MT + LS '
        'pair) is translated with SV 0')
    exp('win', rf'WIN S10 n={n} exc=0 r0=600d0011 urc=8',
        '[S 6.2 landing window] SV 1 -> 0: translated with SV 1 (URC counts it: AT = 1 at its ack)')
for k in (16, 24):
    exp('win', rf'WIN K n={k} exc=1 \[1e0 spc=\+0\] r0=deadbeef',
        '[M hm 20.3.1: ASID compared unless BASM] a pre-execution break with BASRA = 2 after PTEH.ASID 1 -> 2, k '
        'instructions later: 0x1E0, SPC = the instruction, it does not execute')
for n in range(4):
    exp('win', rf'WIN Ll n={n} u9=30000311/0c3011c6',
        '[M hm 3.5.3, pm LDTLB] PTEL stored n = 0..3 NOPs before LDTLB: UTLB[URC = 9] takes the new PTEL (Linux\'s '
        'update_mmu_cache idiom); lockstep compares the LDTLB record')
    exp('win', rf'WIN Lh n={n} u9=30001322/0c30017c', '[M] PTEH stored n NOPs before LDTLB: the new VPN / ASID')
for n in (3, 4, 5, 8):
    exp('win', rf'WIN O n={n} exc=1 \[1e0\] r0=00000000',
        '[M hm 20.3.1] an operand-read break with BASRA = 2 compared, PTEH.ASID 1 -> 2 n NOPs before the load: 0x1E0 '
        '(taken after the load: r0 holds the word)')
exp('win', r'win: done', '[T]')

# ---------------------------------------------------------------- dpage_c1 (code pinned, URB 3)
exp('dpage_c1', r'DPAGE iters=1200 refills=(1[0-9]{3}|[2-9][0-9]{3}) irqs=([1-9][0-9]{3}) exc=0 logged=0 '
    r'sum=([0-9a-f]{8}) ref=\3 diff=0', '[T] 1200 calls with >= 1000 LDTLB refills and >= 1000 TMU0 interrupts: '
    'every data word and the returned sum equal the untranslated run; no unexpected exception')
exp('dpage_c1', r'DPAGE mmucr urb=3 urc=[0-2]', '[M hm 3.2: URC counts up to URB - 1 then wraps to 0 (mmu 3.6 D2)]')
# dpage (unpinned, the code demand-paged too): in lockstep since M2-fix (F2: the model's ITLB victims)
exp('dpage', r'DPAGE iters=1200 refills=(1[0-9]{3}|[2-9][0-9]{3}) irqs=([1-9][0-9]{3}) exc=0 logged=0 '
    r'sum=([0-9a-f]{8}) ref=\3 diff=0', '[T] instruction TLB misses answered by LDTLB too: equal to the untranslated run')
exp('dpage', r'DPAGE mmucr urb=6 urc=[0-5]', '[M] URC < URB')

# ---------------------------------------------------------------- mrst
held_bas = {1: ('5a', '87654320'), 2: ('5b', '87654321'), 3: ('5c', '87654322'), 4: ('5d', '87654323')}
cap = {1: ('10200040', '10200033'), 2: ('20400000', '20400033'), 3: ('20400000', '20400033')}
for ph in (1, 2, 3):
    tea, pteh = cap[ph]
    ba, br = held_bas[ph]
    exp('mrst', rf'MRST phase={ph} ccn expevt=140 intevt=000 tra=0cc tea={tea} pteh={pteh} ptel=00000000 '
        rf'ttb=00000000 ptea=0 mmucr=00000000 qacr0=4 qacr1=4 basra={ba} bara={br} pmcr2=0023',
        '[S 2.3 manual column, 2.2 RC_140] a multiple hit: EXPEVT 0x140; TEA / PTEH.VPN = the multiple-hit address '
        '(the capture survives), PTEH.ASID held; PTEL / TTB / PTEA 0 [P]; MMUCR 0; TRA held; UBC, PMCR held; QACR = 4 '
        'is the boot stub\'s (SOC.md 7.3)')
    exp('mrst', rf'MRST phase={ph} p4 ipra=0000 iprb=0000 iprc=0000 icr=8000 tstr=00 tcr0=0000 tcor0=ffffffff '
        rf'tcor1=ffffffff scsmr2=0000 scbrr2=0c scsmr1=00 scbrr1=00 stbcr2=00',
        '[S 2.3 manual] INTC init (ICR = NMIL, the pin), TMU init; SCIF rewritten by the boot stub; SCI module-stopped')
exp('mrst', r'MRST phase=1 tlb u0=10200333/0c20017c u1=10200333/0c20117c i0=00000000/00000000 i1=00000000/00000000',
    '[S 2.3 manual: TLB V bits and contents held]')
exp('mrst', r'MRST phase=2 tlb u0=00000000/00000000 u1=00000000/00000000 i0=20400133/0c400158 i1=20400133/0c400158',
    '[S 2.3] the two ITLB entries of the fetch multiple hit held')
exp('mrst', r'MRST phase=3 tlb u0=20400333/0c40017c u1=00000000/00000000 i0=00000000/00000000 i1=00000000/00000000',
    '[S 2.3] the UTLB entries of the refill multiple hit held (entry 5 not printed); no ITLB entry written')
exp('mrst', r'MRST phase=4 ccn expevt=020 intevt=000 tra=0cc tea=cafe0003 pteh=00000033 ptel=0c0001fc ttb=8c0ffff3 '
    r'ptea=5 mmucr=00001001 qacr0=4 qacr1=4 basra=5d bara=87654323 pmcr2=0023',
    '[S 2.3 CPU-only column, 2.2 RC_020] a TLB miss with SR.BL = 1: EXPEVT 0x020; every MMU register held (AT = 1, '
    'URC 4: the faulting access not counted); TEA / PTEH not written by the BL = 1 miss (mmu 9 Q12: the model agrees)')
exp('mrst', r'MRST phase=4 p4 ipra=1233 iprb=4560 iprc=0780 icr=8000 tstr=02 tcr0=0003 tcor0=0f0f0f03 tcor1=12345678 '
    r'scsmr2=0000 scbrr2=0c scsmr1=00 scbrr1=00 stbcr2=00', '[S 2.3 CPU-only] INTC, TMU held (TMU1 still running)')
exp('mrst', r'MRST phase=4 tlb u0=10200333/0c20017c u1=00000000/00000000 i0=00000000/00000000 i1=00000000/00000000',
    '[S 2.3 CPU-only] TLB held')
exp('mrst', r'MRST phase=5 ccn expevt=020 intevt=000 tra=0cc tea=cafe0004 pteh=5555a477 ptel=0c0001fc ttb=8c0ffff4 '
    r'ptea=5 mmucr=00241e00 qacr0=4 qacr1=4 basra=5e bara=87654324 pmcr2=0023',
    '[S 2.3 CPU-only; p4bus row: an acked transaction completes] a PTEH and a MMUCR store right before an illegal '
    'instruction with SR.BL = 1: both land (PTEH 0x5555A477, MMUCR SQMD / URB 9 / URC 7); everything else held')
exp('mrst', r'MRST phase=5 p4 ipra=1234 iprb=4560 iprc=0780 icr=8000 tstr=02 tcr0=0003 tcor0=0f0f0f04 tcor1=12345678 '
    r'scsmr2=0000 scbrr2=0c scsmr1=00 scbrr1=00 stbcr2=00', '[S 2.3 CPU-only] INTC, TMU held')
for ph in (1, 2, 3, 4, 5):
    exp('mrst', rf'MRST phase={ph} entry sr=400000f1 vbr=8c000000 .*', '[S 7.3] the boot stub ran again (SR, VBR)')
exp('mrst', r'mrst: done', '[T] five resets, then the end')

# ---------------------------------------------------------------- pgx
for w in ('cold', 'warm'):
    exp('pgx', rf'PGX trapa {w} r0=00000001 n=1 \[160 tea=.* spc=\+4096\]',
        '[M hm 5.7: TRAPA SPC = the next instruction] TRAPA at page A\'s last word: SPC = page B + 0, the return runs B')
    exp('pgx', rf'PGX ill {w} r0=00000bad n=1 \[180 tea=.* spc=\+4094\]',
        '[M hm 5.6; S 4.1 item 2] the illegal instruction at A + 0xFFE; the speculative UTLB miss of B raises nothing')
    exp('pgx', rf'PGX slp {w} r0=00000001 n=1 \[400 tea=.* spc=\+4096\]',
        '[M hm 9.3] SLEEP at A\'s end, woken by TMU0: SPC = the next instruction (B + 0)')
    exp('pgx', rf'PGX p4rd {w} r0=00000001 n=0', '[M] a P4 read at A\'s end, RTS\'s slot in B: no exception')
    exp('pgx', rf'PGX ldcsr {w} r0=00000001 n=0', '[M] LDC SR at A\'s end: the refetch runs B')
    exp('pgx', rf'PGX dmiss {w} r0=00000bad n=1 \[040 tea=\+36880 spc=\+4092\]',
        '[M hm 3.6.1] a data TLB miss at A + 0xFFC: 0x040, TEA = the data VA, SPC = the load')
    exp('pgx', rf'PGX brau {w} r0=00000bad n=1 \[040 tea=\+24608 spc=\+24608\]',
        '[M hm 3.6.1] a JMP to an unmapped page: 0x040, SPC = TEA = the target')
    exp('pgx', rf'PGX braslot {w} r0=00000bad n=1 \[040 tea=\+4096 spc=\+4094\]',
        '[HW mmu E4c; M hm 5.x delay slot] the slot in the next, unmapped page: 0x040, TEA = the slot, SPC = the JMP')
    exp('pgx', rf'PGX uprot {w} r0=00000000 n=1 \[0a0 tea=\+64 spc=\+64\]',
        '[HW mmu U; M hm 3.6.3] a user fetch from a PR = 00 page: 0x0A0, SPC = TEA = the PC')
for w in ('cold', 'warm'):
    exp('pgx', rf'PGX a7fetch {w} r0=00000bad n=1 \[0e0 tea=\+64 spc=\+64\]',
        '[S 4.1 item 2, G5 [P]] a JMP to a page the UTLB maps into physical area 7: FX_ADDR 0x0E0 at the target')
exp('pgx', r'PGX umiss r0=0 n=1 \[040 tea=\+128 spc=\+128\]', '[M] RTE to an unmapped user page: 0x040 at the target')
exp('pgx', r'PGX cross irq sum=([0-9a-f]{8}) ref=\1 irqs=([1-9][0-9]+) logged=0',
    '[T] straight-line code across a page boundary, ITLB emptied every 2nd call, >= 10 random TMU0 interrupts: the '
    'sum equals the untranslated run')
exp('pgx', r'pgx: done', '[T]')

# ---------------------------------------------------------------- asidsw
exp('asidsw', r'ASIDSW phase=0 calls=600 bad=0', '[M hm 3.3.7] 300 process switches by PTEH.ASID: each call runs its '
    'own process\'s code page (two ITLB entries of one VPN, ASID 0x11 / 0x22) on its own data page, and the SH = 1 '
    'shared page for both')
exp('asidsw', r'ASIDSW phase=1 calls=1200 bad=0', '[M hm 3.3.7 SV = 1: privileged accesses ignore the ASID]')
exp('asidsw', r'ASIDSW calls=1200 bad=0 irqs=([1-9][0-9]{2,}) logged=0', '[T] >= 100 random TMU0 interrupts, no exception')

# ---------------------------------------------------------------- win_c<HEX>: the window cases, one per build (M2-fix)
# A / B: the TLBs take a PTEH.ASID store at once [HW tools/hw/mmu/ptehwin: n = 0..4 all with the new ASID; F1]
for n, c in ((0, '100'), (1, '200'), (2, '400')):
    exp('win_c' + c, rf'WIN A n={n} exc=1 \[040 tea=10200040 pteh=10200002\] r0=deadbeef',
        '[HW ptehwin A] the load right behind PTEH.ASID 1 -> 2 misses the ASID-1 page: 0x040, TEA = VA, PTEH = VPN | 2')
for n, c in ((0, '1000'), (1, '2000'), (2, '4000')):
    exp('win_c' + c, rf'WIN B n={n} exc=0 r0=600d0010', '[HW ptehwin B] the ASID-2 page hits right behind the store')
# U: AT 0 -> 1 then an unmapped P0 load in the group held in EX behind the store (win_asm pairs the store as lane 1 of
# an MT + LS pair, so n <= 1): not translated [HW mmu R5], and URC counts it at its ack with AT = 1 (5 -> 6) [S 3.8,
# normative; F3]; n = 2 is one group later: translated, the TLB miss, URC not counted
for n, c in ((0, '10000'), (1, '20000')):
    exp('win_c' + c, rf'WIN U n={n} exc=0 r0=[0-9a-f]{{8}} urc=6',
        '[HW R5; S 3.8] untranslated in the landing window; URC counted by the stored AT (5 -> 6)')
exp('win_c40000', r'WIN U n=2 exc=1 \[040 tea=10300048 pteh=10300000\] r0=deadbeef urc=5',
    '[HW R5; S 6.2] outside the window (the group after the held one): translated, 0x040, URC not counted')
# K / O: the UBC takes a PTEH.ASID store at its landing like its own registers [HW tools/hw/mmu/ptehubc; F6]
for k, c in ((0, '100000'), (2, '200000'), (4, '400000'), (8, '800000')):
    exp('win_c' + c, rf'WIN K n={k} exc=0 r0=00000001',
        '[HW ptehubc N: a pre-execution break (BASRA 2) k <= 12 NOPs after PTEH.ASID 1 -> 2 uses the old ASID (warm)] '
        'no break, the instruction executes')
for n, c in ((0, '1000000'), (1, '2000000')):
    exp('win_c' + c, rf'WIN O n={n} exc=0 r0=00000000',
        '[HW ptehubc P: an operand break in the group held in EX behind the store uses the old ASID] no break')
exp('win_c4000000', r'WIN O n=2 exc=1 \[1e0\] r0=00000000',
    '[HW ptehubc P; S 6.2] one group after the held one (win_asm\'s pairing): the new ASID, 0x1E0 after the load')

for c in ('100', '200', '400', '1000', '2000', '4000', '10000', '20000', '40000', '100000', '200000', '400000', '800000',
          '1000000', '2000000', '4000000'):
    exp('win_c' + c, r'win: done', '[T] the case ran to its end')

# ---------------------------------------------------------------- ubcasid (M2-fix): the console's ptehubc, warm
for k in range(15):
    t = 2 + 2 * k                                        # the target's offset in its snippet
    brk = rf'n=1 \[1e0 spc=\+{t}\]'
    exp('ubcasid', rf'UA N k={k} ' + (brk if k >= 13 else 'n=0'),
        '[HW ptehubc N, 2 runs x 3, warm] BASRA 2: the old ASID for k <= 12 NOPs after PTEH.ASID 1 -> 2, the new from 13')
    exp('ubcasid', rf'UA O k={k} ' + (brk if k <= 12 else 'n=0'), '[HW ptehubc O] BASRA 1: the complement of N')
    exp('ubcasid', rf'UA T k={k} ' + (brk if k >= 13 else 'n=0'),
        '[HW ptehubc T = ubcbrk T] the reference: a BBRA store arms the break from the 14th instruction after it')
for k in range(7):
    brk = rf'n=1 \[1e0 spc=\+{4 + 2 * k}\]'                # SPC = the load + 2 (an operand break is post-access)
    exp('ubcasid', rf'UA P k={k} ' + (brk if k >= 3 else 'n=0'),
        '[HW ptehubc P] an operand-read break, BASRA 2: the old ASID for k <= 2, the new from 3')
    exp('ubcasid', rf'UA Q k={k} ' + (brk if k <= 2 else 'n=0'), '[HW ptehubc Q] BASRA 1: the complement of P')
    exp('ubcasid', rf'UA S k={k} ' + (brk if k >= 3 else 'n=0'), '[HW ptehubc S = ubcevt S] the operand reference')
exp('ubcasid', r'ubcasid: done', '[T]')

# ---------------------------------------------------------------- ubcasid_c1 (M3-fix, M3-verify F2): ptehubc, cold, rmx
# The snippet's lines invalidated before each measured call (KOS's icache_flush_range from P2, as ptehubc): SOC.md 9.22
# (b).  The console: the fetch-side boundary of the PTEH.ASID store is 11 cold (13 warm), the BBRA store's 13 cold and
# warm, the operand boundary 3 [HW ptehubc, two runs x 3 repetitions].  (The chip gives 13 for N / O cold: 9.22 (b) is
# open, v1-soc-m3 fails on these rows until it closes.)
for k in range(15):
    t = 2 + 2 * k
    brk = rf'n=1 \[1e0 spc=\+{t}\]'
    exp('ubcasid_c1', rf'UA N k={k} ' + (brk if k >= 11 else 'n=0'),
        '[HW ptehubc N cold] BASRA 2: the old ASID for k <= 10 NOPs after PTEH.ASID 1 -> 2, the new from 11 (9.22 (b))')
    exp('ubcasid_c1', rf'UA O k={k} ' + (brk if k <= 10 else 'n=0'), '[HW ptehubc O cold] BASRA 1: the complement of N')
    exp('ubcasid_c1', rf'UA T k={k} ' + (brk if k >= 13 else 'n=0'),
        '[HW ptehubc T cold] the BBRA reference: 13, cold as warm')
for k in range(7):
    brk = rf'n=1 \[1e0 spc=\+{4 + 2 * k}\]'
    exp('ubcasid_c1', rf'UA P k={k} ' + (brk if k >= 3 else 'n=0'), '[HW ptehubc P cold] the operand boundary 3')
    exp('ubcasid_c1', rf'UA Q k={k} ' + (brk if k <= 2 else 'n=0'), '[HW ptehubc Q cold]')
    exp('ubcasid_c1', rf'UA S k={k} ' + (brk if k >= 3 else 'n=0'), '[HW ptehubc S cold]')
exp('ubcasid_c1', r'ubcasid: done', '[T]')

LOCKSTEP = set(E)                                        # every program ends "exit code 0 ... lockstep pass" (M2-fix)


def status_ok(rundir, prog):
    try:
        s = open(os.path.join(rundir, prog + '.status')).read()
    except OSError:
        return False, 'no status'
    ok = 'exit code 0' in s and 'lockstep pass' in s
    return ok, ('' if ok else (re.search(r'FAIL[^\n]*', s).group(0)[:160] if re.search(r'FAIL[^\n]*', s)
                               else 'no "exit code 0 ... lockstep pass"'))


def check_lines(lines, prog):
    bad = []
    for rx, src in E.get(prog, []):
        r = re.compile(rx + r'\Z')
        if not any(r.match(l) for l in lines):
            bad.append((rx, src))
    return bad


def main():
    args = sys.argv[1:]
    self_mode = '--self' in args
    args = [a for a in args if a != '--self']
    progs = None
    if '--progs' in args:
        i = args.index('--progs'); progs = args[i + 1].split(','); del args[i:i + 2]
    rundir = args[0] if args else '.'
    total = fails = 0
    if not self_mode:
        one = os.path.isfile(rundir)                     # check.py FILE.log --progs P: the lines of one log only
        for prog in (progs or sorted(E)):
            if prog not in E:
                print(f'{prog}: FAIL no expectations for it'); fails += 1; continue
            try:
                lines = open(rundir if one else os.path.join(rundir, prog + '.log')).read().splitlines()
            except OSError:
                print(f'{prog}: FAIL no output'); fails += 1; continue
            bad = check_lines(lines, prog)
            total += len(E[prog])
            if prog in LOCKSTEP and not one:
                ok, why = status_ok(rundir, prog)
                if not ok:
                    print(f'{prog}: FAIL lockstep / exit: {why}'); fails += 1
            for rx, src in bad:
                print(f'{prog}: FAIL missing /{rx}/  {src}'); fails += 1
            print(f'{prog}: {len(E[prog]) - len(bad)} / {len(E[prog])} expectations')
        print(f'soc-adv/m2 check.py: {total} expectations, {fails} failures -> {"PASS" if fails == 0 else "FAIL"}')
        return 0 if fails == 0 else 1
    # --self: every '=value' field a pinned line constrains, perturbed alone, must make its expectation fail
    fields = flagged = weak = 0
    for prog in (progs or sorted(E)):
        try:
            lines = open(os.path.join(rundir, prog + '.log')).read().splitlines()
        except OSError:
            continue
        for rx, src in E[prog]:
            r = re.compile(rx + r'\Z')
            hit = [i for i, l in enumerate(lines) if r.match(l)]
            if not hit:
                continue
            i = hit[0]
            l = lines[i]
            pinned = 0
            for mm in re.finditer(r'=([+-]?[0-9a-f]+)', l):
                v = mm.group(1)
                nc = '1' if v[-1] != '1' else '2'
                pl = l[:mm.end(1) - 1] + nc + l[mm.end(1):]
                if r.match(pl):
                    continue                                  # a wildcard field of this expectation
                pinned += 1
                fields += 1
                pert = [pl if j == i else x for j, x in enumerate(lines) if j == i or not r.match(x)]
                if any(b[0] == rx for b in check_lines(pert, prog)):
                    flagged += 1
                else:
                    print(f'{prog}: perturbed field {mm.group(0)} of /{rx}/ not flagged')
            if '=' in rx and pinned == 0:
                weak += 1
                print(f'{prog}: /{rx}/ pins no field')
    ok = fields > 0 and flagged == fields and weak == 0
    print(f'soc-adv/m2 check.py --self: {flagged} / {fields} pinned fields flagged when perturbed alone, {weak} '
          f'expectations pinning nothing -> {"PASS" if ok else "FAIL"}')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
