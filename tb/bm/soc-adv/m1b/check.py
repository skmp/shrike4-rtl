#!/usr/bin/env python3
"""check.py -- expected output of the tb/bm/soc-adv/m1b programs (the M1b independent verification's adversarial
programs, agents/reports/integration/M1b-verify.md, adopted into the M1b gate by M1b-fix.md).

  check.py DIR [--progs a,b]   every program (or the listed ones): DIR/<prog>.log (a run's sim-control output);
                               ubcx_f<n> is ubcx with part F's case n alone
  check.py FILE.log --progs P  one log of program P (the planted-bug runs)
  check.py --self DIR          every pinned line of every log in DIR, perturbed one value at a time, must be rejected
                               (the checks can fail); run it on passing logs

Every expectation is a regex that must match one output line, in order (a line can carry a function for numeric
relations).  The sources of the values are in the comments: hm = the SH7750 hardware manual, SOC.md = rtl/v1/SOC.md.
Lines marked `report` are printed for the record and not pinned (the manual leaves them open).
"""
import re
import sys
import os

HX = r'[0-9a-f]+'


class E:
    def __init__(self, rx, fn=None, report=False, why=''):
        self.rx, self.fn, self.report, self.why = rx, fn, report, why


# ---------------------------------------------------------------------------------------------------------- prio2
def exp_prio2():
    # hm Table 19.5: by IPR level, then the default order H-UDI > GPIO > DMAC (DMTE0..3, DMAE) > TMU0 > TMU1 > TMU2
    # (TUNI2, TICPI2) > RTC (ATI, PRI, CUI) > SCI (ERI, RXI, TXI, TEI) > SCIF (ERI, RXI, BRI, TXI) > WDT > REF (RCMI, ROVI)
    eq = '6a0,400,420,440,480,4a0,4c0,520,540,720,740,760,560,580'
    return [
        E(rf'PRIO2 equal5 pending=3fff held=14 order={eq} n=14$', why='all at level 5: the default order'),
        E(r'PRIO2 equal5 dma_bad=0$'),
        # IPRA 2345 (TMU0 2, TMU1 3, TMU2 4, RTC 5), IPRB 986x (WDT 9, REF 8, SCI 6), IPRC x17x (DMAC 1, SCIF 7)
        E(r'PRIO2 ascend pending=3fff held=14 order=560,580,720,740,760,520,540,480,4a0,4c0,440,420,400,6a0 n=14$'),
        E(r'PRIO2 ascend dma_bad=0$'),
        # IPRA 1819 (TMU0 1, TMU1 8, TMU2 1, RTC 9), IPRB 111x, IPRC x19x (DMAC 1, SCIF 9): RTC before SCIF at 9
        E(r'PRIO2 mixed pending=3fff held=14 order=480,4a0,4c0,720,740,760,420,6a0,400,440,520,540,560,580 n=14$'),
        E(r'PRIO2 mixed dma_bad=0$'),
        # level == IMASK: not accepted (hm 19.2.1: a level above IMASK); one lower: all, in the default order
        E(rf'PRIO2 atmask pending=3fff held=0 order={eq} n=14$'),
        E(r'PRIO2 atmask dma_bad=0$'),
    ]


# ---------------------------------------------------------------------------------------------------------- rtcx
def exp_rtcx():
    st = {}

    def init(m):
        st['sec'] = int(m.group(1), 16)
        return None

    def restarted(m):
        if int(m.group(1), 16) != (0x80 | st.get('sec', -1)):
            return f'RSECAR {m.group(1)} is not ENB | the seconds ({st.get("sec", -1):02x})'
        return None
    e = [
        # hm Table 11.2: power-on RCR1 = 00, RCR2 = 09 (RTCEN, START)
        E(r'RTCX init rcr1=00 rcr2=09 sec=([0-9a-f]{2}) min=\S+ ar=\S+$', init),
        # armed: AF | AIE; MSTP1: every register reads 0 [HW, SOC.md 2.4]; the stopped unit drives ATI 0 (SOC.md 3.4)
        E(r'RTCX A armed rcr1=09 stopped rcr1=00 ar=00 sec=00 rcr2=00 yr=0000 taken=0$'),
        # MSTP1 cleared: the held values (the writes while stopped were ignored) and ATI taken
        E(r'RTCX A restarted rcr1=09 ar=([0-9a-f]{2}) taken=1 code=480$', restarted),
        # [HW tc3 / tc4]: AF written 0 while matching reads 1; no match: 0; RTCEN = 0: no AF; every ENB 0: AF 0
        E(r'RTCX B match_clr=01 nomatch_clr=00 rtcen0=00 rtcen1=01 noenb=00$'),
    ]
    # the alarm match from a store right before SLEEP wakes it (on-chip source, hm 9.3.2), SPC = SLEEP + 2 [HW 36 of 36]
    for d in range(2):
        for k in range(16):
            e.append(E(rf'RTCX C dslp={d} k={k} n=1 code=480 at=wake pm=\d+$'))
    return e


# ---------------------------------------------------------------------------------------------------------- ubcx
ENT = re.compile(r'\[([0-9a-f]{3}) ([+-]\d+) bl=(\d) brcr=([0-9a-f]+)\]')


def d_line(m):
    """hm 20.3.7 (5): with an address + data break pending, a peripheral interrupt must not be taken before it: the first
    entry whose SPC is after the load (+2 or more) is the break; exactly one break and one interrupt per k"""
    ents = [(c, int(o)) for c, o, _, _ in ENT.findall(m.group(0))]
    codes = [c for c, _ in ents]
    if sorted(codes) != ['1e0', '400']:
        return f'entries {codes}, want one 1e0 and one 400'
    for c, o in ents:
        if c == '1e0':
            break
        if o > 0:
            return f'interrupt taken at +{o}, after the load, before its data break'
    return None


def exp_ubcx(fsel=None):
    e = [
        # A: operand-address read break on SCFRDR2: the load completes once (R0 = 11, SCFDR2.R 3 -> 2 at the break and
        # after), SPC = the next instruction (hm 20.3.7 (4)), CMFA (hm 20.3.6 1)
        E(r'UBCX A r0=11 fdr=2 brk_fdr=2 brk_r0=11 ents n=1 \[1e0 \+2 bl=0 brcr=8000\]$'),
        # B: pre-execution break on the load: not executed (no pop at the break), SPC = the load (hm 20.3.7 (1))
        E(r'UBCX B r0=22 fdr=1 brk_fdr=2 ents n=1 \[1e0 \+0 bl=0 brcr=8000\]$'),
        E(r'UBCX AB last=33 fdr=0$'),
        # C: address + data break on the 2nd store: every byte once, in order; SPC = the first suppressed instruction, 1 to
        # 4 after the store (hm 20.3.7 (5)); CMFB
        E(r'UBCX C rx=3: 11 5a 22 ents n=1 \[1e0 \+(4|6|8|10) bl=0 brcr=4080\]$'),
    ]
    for k in range(25):
        e.append(E(rf'UBCX D k={k} brcr_after=0080 ents n=2 .*$', d_line))
    e += [
        E(r'UBCX D summary bad=0 brk=25 irq=25 brk_first=\d+$'),
        E(r'UBCX Dctl summary bad=0 brk=0 irq=25 brk_first=0$', why='BDRB not matching: no break'),
        E(r'UBCX Dnoirq summary bad=0 brk=25 irq=0 brk_first=25$'),
        E(r'UBCX Dnosrc summary bad=0 brk=25 irq=0 brk_first=25$'),
        # E: a pre-execution break on SLEEP: SPC = SLEEP, then it sleeps; the wake-up SPC = SLEEP + 2
        E(r'UBCX E pre_on_sleep ents n=2 \[1e0 \+0 bl=0 brcr=8000\] \[400 \+2 bl=0 brcr=0000\]$'),
        # a pre-execution break on the instruction after SLEEP: the wake-up interrupt first (SOC.md 4.3.2: an interrupt
        # wins over a D-stage fault of the same leader [HW excint2 PF]), then the break, both at SLEEP + 2
        E(r'UBCX E pre_after_sleep ents n=2 \[400 \+2 bl=0 brcr=0000\] \[1e0 \+2 bl=0 brcr=8000\]$'),
        # hm 20.3.9 (1) / (2): "do not" -- only no hang is required (the run's exit code 0)
        E(r'UBCX E post_on_sleep ents n=\d+.*$', report=True),
        E(r'UBCX E operand_before_sleep ents n=\d+.*$', report=True),
    ]
    # F: hm 20.3.9 (3) a-d: which BL a break tests -- pre-execution BL before; post-execution masked by BL before or
    # after; operand masked by BL after.  The table's rows for an instruction that changes BL (0 -> 1: operand M, post
    # M; 1 -> 0: operand A, post M; pre: before) pinned here, not taken from the program's own `want`.  CMFA is set
    # whether the break is taken or masked (hm 20.2.6: the condition match flag).  An accepted break: one 0x1E0 entry,
    # SPC = the next instruction (operand / post, hm 20.3.7 (4) / (3)) or the instruction (pre, (1)), SSR.BL 0 (the BL
    # after for operand / post: 0 when taken); BRCR as the handler read it (CMFA, and PCBA 0x0400 for a post break)
    acc_op, acc_post, acc_pre = r'n=1 \[1e0 \+0 bl=0 brcr=8000\]', r'n=1 \[1e0 \+0 bl=0 brcr=8400\]', \
        r'n=1 \[1e0 \+0 bl=0 brcr=8000\]'
    for f, (nm, want, ents) in enumerate((
            ('op_ldcl_bl01', 'masked', 'n=0'), ('op_ldcl_bl10', 'accepted', acc_op),
            ('post_ldc_bl01', 'masked', 'n=0'), ('post_ldc_bl10', 'masked', 'n=0'),
            ('pre_ldc_bl01', 'accepted', acc_pre), ('pre_ldc_bl10', 'masked', 'n=0'),
            ('post_ldcl_bl01', 'masked', 'n=0'), ('post_ldcl_bl10', 'masked', 'n=0'),
            ('op_ldcl_bl00', 'accepted', acc_op), ('post_ldcl_bl00', 'accepted', acc_post),
            ('post_ldc_bl00', 'accepted', acc_post))):
        if fsel is None or f == fsel:                    # (ubcx_f<n>: part F's case n alone)
            e.append(E(rf'UBCX F {nm} want={want} got={want} cmfa=1 ents {ents}$'))
    return e


# ---------------------------------------------------------------------------------------------------------- scifx
def exp_scifx():
    def extra(m):
        x = int(m.group(1))
        # frozen ~20000 Icyc (wait_icyc 20000 plus the STBCR write and read-back): the frame resumes where it stopped.
        # Not frozen: ~17000 (the frame ends during the wait); restarted from its start: ~22000
        if not 20000 <= x <= 20000 + 512:
            return f'the stop added {x} Icyc, want 20000..20512 (frozen and resumed)'
        return None
    tail = ' '.join(f'{0xa0 + i:02x}' for i in range(16))
    return [
        # SOC.md 2.4: SCIF MSTP3 state frozen, registers kept [HW]; access reads 0 / ignored [P]
        E(r'SCIFX A stopped scr=0000 fdr=0000 fsr=0000 brr=00 resumed brr=03 scr=0030 fdr=0000$'),
        E(r'SCIFX A rx fdr=0001 er=0 brk=0 byte=5a ref_icyc=\d+ stopped_extra_icyc=(\d+)$', extra),
        # SOC.md 3.4: a module-stopped unit drives its requests 0
        E(r'SCIFX A txi stopped_taken=0 then n=1 code=760$'),
        # hm 9.3 / 9.4.1: the SCIF runs in sleep and deep sleep; RXI wakes, SPC = SLEEP + 2
        E(r'SCIFX B dslp=0 n=1 code=720 at=wake fdr_at_wake=1 byte=c3 icyc=\d+$'),
        E(r'SCIFX B dslp=1 n=1 code=720 at=wake fdr_at_wake=1 byte=c3 icyc=\d+$'),
        # sleep: the DMAC runs (all 16 sent before the wake); deep sleep: it stops (some left at the wake), resumes
        E(rf'SCIFX C dslp=0 wake=400 at=wake tc_at_wake=0 rx_at_wake=\d+ tx_at_wake=\d+ end tc=0 te=1 rx=16: {tail}$'),
        E(rf'SCIFX C dslp=1 wake=400 at=wake tc_at_wake=[1-9]\d* rx_at_wake=\d+ tx_at_wake=\d+ end tc=0 te=1 rx=16: {tail}$'),
    ]


# ---------------------------------------------------------------------------------------------------------- dmadslp
def exp_dmadslp():
    e = []
    # SOC.md 2.4: after a module stop the DMAC "must be reprogrammed" [M] -- and then it works: a fresh transfer completes
    for k in range(8):
        e.append(E(rf'DMADSLP mstp4 k={k} tc=0 te=1 rx=16 order=1 done$'))
    e.append(E(r'DMADSLP mstp4 summary stuck=0 of 8$'))
    for k in range(24):
        e.append(E(rf'DMADSLP dslp=0 k={k} wake=400 tc=0 te=1 de=1 rx=16 order=1 done$'))
    e.append(E(r'DMADSLP dslp=0 summary stuck=0 of 24$'))
    # [P] SOC.md 2.5 / v1_dmac: the unit in flight completes, the channel resumes after the wake (hm 14.6 (3) leaves
    # it unguaranteed): M1b-verify F2 (v1_dmac_req: the unit's end takes the channel out of service under bce alone)
    for k in range(24):
        e.append(E(rf'DMADSLP dslp=1 k={k} wake=400 tc=0 te=1 de=1 rx=16 order=1 done$'))
    e.append(E(r'DMADSLP dslp=1 summary stuck=0 of 24$'))
    return e


# ---------------------------------------------------------------------------------------------------------- wdtx
def exp_wdtx():
    def frame(m):
        f, r = int(m.group(1)), int(m.group(2))
        # the settle stops the Pclk (the SCIF) and the CPU clock (PMCTR) together: the frame measures as the reference
        if abs(f - r) > 64:
            return f'frame {f} Icyc against the reference {r}'
        return None

    def wdt(m):
        # M3-fix (M3-verify F10): on the real memory (rm) the CPU-only reset's boot stub runs from the boot ROM at the
        # bus's pace (~112,600 Icyc, against ~3,400 on im), so the interval timer (CKS 3: 256 Icyc per count) wraps:
        # WTCNT advances modulo 256, and IOVF (WTCSR bit 3, hm 10.2.3) is set exactly when it overflowed
        ws, d, i = int(m.group(1), 16), int(m.group(2)), int(m.group(3))
        c = i / 256.0
        if min(abs(d - (c % 256)), 256 - abs(d - (c % 256))) > 1.5:
            return f'WTCNT advanced {d} in {i} Icyc (CKS 3: {c:.1f}, modulo 256)'
        if abs(c - 256) > 1.5 and (ws == 0x8B) != (c >= 256):
            return f'WTCSR {ws:02x} after {c:.1f} counts: IOVF must be set exactly when WTCNT overflowed'
        return None
    return [
        # hm 10.8, 9.3 / 9.4: ITI wakes a sleep and a deep sleep, SPC = SLEEP + 2
        E(r'WDTX A dslp=0 n=1 code=560 at=wake$'),
        E(r'WDTX A dslp=1 n=1 code=560 at=wake$'),
        # hm 10.9: WTCNT 00 / 01 after the settle; FRQCR as written; the frame intact
        E(r'WDTX C frqcr=0e0a wtcnt=0[01] byte=3c er=0 brk=0 frame_icyc=(\d+) ref_icyc=(\d+)$', frame),
        # SOC.md 2.3 CPU-only reset: EXPEVT 020; WDT, RTC, UBC, SCSPTR1 held; SCI initial after the stub's MSTP0
        E(r'WDTX B expevt=020 wtcsr=(83|8b)/83 wtcnt_delta=(\d+) icyc=(\d+) expect=\d+$', wdt),
        E(r'WDTX B rtc rcr1=10/10 rcr2=29/29 ar=85,12/85,12$'),
        E(r'WDTX B ubc bara=8c0abcde/8c0abcde bbra=0000/0000$'),
        E(r'WDTX B sci sptr1_io=8a/00/8a scscr1=00$'),
    ]


PROGS = {'prio2': exp_prio2, 'rtcx': exp_rtcx, 'ubcx': exp_ubcx, 'scifx': exp_scifx, 'dmadslp': exp_dmadslp,
         'wdtx': exp_wdtx}


def exps_of(prog):
    m = re.fullmatch(r'ubcx_f(\d+)', prog)
    return exp_ubcx(int(m.group(1))) if m else PROGS[prog]()


def check_lines(prog, lines, verbose=True):
    """returns (n_checked, failures, matched: list of (expectation index, line index))"""
    exps = exps_of(prog)
    fails, matched, i = [], [], 0
    for k, x in enumerate(exps):
        rx = re.compile(x.rx)
        j = i
        while j < len(lines) and not rx.search(lines[j]):
            j += 1
        if j == len(lines):
            fails.append(f'{prog}: missing /{x.rx}/')
            continue
        m = rx.search(lines[j])
        if x.fn:
            why = x.fn(m)
            if why:
                fails.append(f'{prog}: {why}: {lines[j]}')
        if x.report and verbose:
            print(f'  {prog} report: {lines[j]}')
        matched.append((k, j))
        i = j + 1
    return len(exps), fails, matched


def load(d, prog):
    p = os.path.join(d, prog + '.log')
    if not os.path.exists(p):
        return None
    with open(p) as fh:
        return [l.rstrip('\n') for l in fh if l.strip()]


def perturb(line):
    """every perturbation of one value: one hex digit of each '=value' / ': value' / '[code' token incremented"""
    out = []
    for m in re.finditer(r'(?<=[=:\[ ])([0-9a-f]+)(?=[ ,/\]]|$)', line):
        s, e = m.span()
        v = m.group(1)
        for q in range(len(v)):
            c = v[q]
            n = '0123456789abcdef'[(int(c, 16) + 1) % 16] if not c.isdigit() or c != '9' else '0'
            out.append(line[:s] + v[:q] + n + v[q + 1:] + line[e:])
    return out


def selftest(d):
    total = caught = unpinned = 0
    for prog in PROGS:
        lines = load(d, prog)
        if lines is None:
            print(f'--self: {prog}: no log')
            continue
        exps = exps_of(prog)
        _, base_fails, matched = check_lines(prog, lines, verbose=False)
        for k, j in matched:
            if exps[k].report:
                continue
            ps = perturb(lines[j])
            hit = 0
            for p in ps:
                l2 = list(lines)
                l2[j] = p
                _, f2, _ = check_lines(prog, l2, verbose=False)
                if len(f2) > len(base_fails):
                    hit += 1
            total += 1
            if hit:
                caught += 1
            else:
                unpinned += 1
                print(f'--self: {prog}: no perturbation of this line is rejected: {lines[j]}')
    print(f'soc-adv/m1b check.py --self: {caught} / {total} pinned lines reject a perturbation')
    return unpinned == 0 and total > 0


def main(argv):
    if argv and argv[0] == '--self':
        ok = selftest(argv[1])
        print('soc-adv/m1b check.py --self: ' + ('PASS' if ok else 'FAIL'))
        return 0 if ok else 1
    d = argv[0]
    progs = list(PROGS)
    if '--progs' in argv:
        progs = argv[argv.index('--progs') + 1].split(',')
    one = os.path.isfile(d)                              # check.py FILE.log --progs NAME: one log of program NAME
    st = 0
    for prog in progs:
        lines = load(os.path.dirname(d) or '.', os.path.basename(d)[:-4]) if one else load(d, prog)
        if lines is None:
            print(f'{prog}: no log in {d}')
            st = 1
            continue
        n, fails, _ = check_lines(prog, lines)
        print(f'{prog}: {n} expectations, {len(fails)} failures')
        for f in fails[:40]:
            print('  FAIL ' + f)
        if fails:
            st = 1
    print('soc-adv/m1b check.py: ' + ('PASS' if st == 0 else 'FAIL'))
    return st


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
