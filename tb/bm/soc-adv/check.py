#!/usr/bin/env python3
"""check.py -- expected output of the SoC adversarial bare-metal programs (tb/bm/soc-adv; written by the M1a
independent verification, agents/reports/integration/M1a-verify.md, and made the M1a gate's regression tests by
M1a-fix.md).

  check.py DIR [--progs a,b]   every program (or the listed ones): DIR/<prog>.log, a run's sim-control output
  check.py PROG LOG            one program's output
  check.py --self DIR          every pinned line of every passing log in DIR, perturbed in one hex digit at a time,
                               must be rejected (the checks can fail); DIR must hold passing logs of every program
  --mem real                   (any form) the run was on the real memory (rm / rmx: the cache arrays are real; M3-fix)

Every expected line is a regex that must match one output line, in order; the sources of the values are in the
comments (hm = the SH7750 hardware manual, SOC.md = rtl/v1/SOC.md).  Fields the manual does not pin (a take's exact
position, counted cycles) are left open with \\S+ and reported by the programs for the record only.
"""
import re
import sys
import os

H = r'[0-9a-f]+'
REAL_MEM = False        # --mem real: the run was on rm / rmx (IDEAL_MEM = 0: the real cache arrays), M3-fix


def exp_iprwin():
    e = []
    # raise: IPRA 0 -> 5 with the source pending: the interrupt is legal and must be taken for every k
    e += [rf'IPRW raise k={k} taken=1 intevt=400 at=\S+ adds=48' for k in range(20)]
    # rdback: hm 19.3 "first read one of the interrupt priority registers ... This will secure the necessary timing":
    # the old level must never be taken after a read-back
    e += [rf'IPRW rdback k={k} taken=0 adds=48' for k in range(20)]
    return e


def iprwin_extra(lines):
    """unfclr / lower / lowerbusy: a take may only happen in a window right after the store (monotone in k) that ends by
    the structure's landing window (Sh4Sys::intc_window, SOC.md 6.2): the LDC SR's S is at least ack + 1 + k / 2 (NOPs
    issue in pairs) and the lowered IMASK reaches the request 9 later, so a take at k means t >= ack + 10 + k / 2.  On
    the idle path the old level is gone by ack + 16 (strobe <= ack + 2, landing + 8, request + 6): never at k >= 12
    (lower, unfclr); behind the longword store by ack + 19 (strobe <= ack + 5): never at k >= 18 (lowerbusy).  lower and
    lowerbusy must take it at k = 0 (the INTC still presents the old level when the lowered IMASK reaches it: the
    window exists, so the lockstep allowance is exercised)"""
    bad = []
    for mode in ('unfclr', 'lower', 'lowerbusy'):
        taken = []
        for k in range(20):
            m = [l for l in lines if l.startswith(f'IPRW {mode} k={k} ')]
            if not m:
                bad.append(f'{mode} k={k}: missing')
                continue
            t = re.search(r'taken=(\d+)', m[0])
            taken.append(int(t.group(1)) if t else -1)
            if t and t.group(1) == '1' and 'intevt=400' not in m[0]:
                bad.append(f'{mode} k={k}: wrong code: {m[0]}')
        for k in range(1, len(taken)):
            if taken[k] and not taken[k - 1]:
                bad.append(f'{mode}: taken at k={k} but not at k={k-1} (not a window)')
        lim = 18 if mode == 'lowerbusy' else 12
        if any(taken[lim:]):
            bad.append(f'{mode}: taken at k >= {lim}')
        if mode != 'unfclr' and taken and taken[0] != 1:
            bad.append(f'{mode}: not taken at k=0 (the landing window vanished)')
    return bad


def exp_prio():
    # hm Table 19.5: by IPR level, then the default priority TMU0 > TMU1 > TMU2 > (RTC) > SCI1 > SCIF > WDT > REF
    return [r'PRIO equal5 order=400,420,440,520,760,560,580 n=7',
            r'PRIO ascend order=580,560,760,520,440,420,400 n=7',
            r'PRIO descend order=400,420,440,760,520,580,560 n=7',
            r'PRIO tmu2top order=440,400,420,520,760,560,580 n=7',
            r'PRIO sciffirst order=760,400,420,440,520,560,580 n=7']


def exp_cpureset():
    e = []
    # phase 0 (before any reset): the state the program set; PTEH.VPN from the misaligned load (hm 5.6 (6))
    b0 = ' '.join(f'r{i}=b0{(0x111111 * i):06x}' for i in range(8))
    b1 = ' '.join(f'b1r{i}=b1{(0x111111 * i):06x}' for i in range(8))
    e.append(r'RST phase=0 entry r0=00000000 .*')
    common_ccn = (r'intevt=a60 tra=0cc tea=8c0f0001 pteh=8c0f0055 ptel=0c0001fc ttb=8c0ffff0 ptea=5 mmucr=00000200 '
                  r'ccr=00000105 qacr0=4 qacr1=4 basra=5a pmcr1=c023 pmcr2=0023')
    e.append(r'RST phase=0 ccn expevt=0e0 ' + common_ccn)
    tmu = (r'icr=8000 tstr=02 tocr=00 tcr0=0003 tcr1=0000 tcr2=0001 tcor0=0f0f0f0f tcor1=12345678 tcor2=%s '
           r'tcnt0=55aa55aa stbcr=03 stbcr2=00 bara=87654320 frqcr=0e0a')
    e.append(r'RST phase=0 p4 ipra=1230 iprb=4560 iprc=0000 ' + tmu % 'cafe0000')
    e.append(r'RST phase=0 misc sgr=\S+ tmu1_running=1')
    # after each CPU-only reset (hm 5.5.3: "the registers of the other modules retain their contents"; SOC.md 2.3):
    # EXPEVT = 0x020, R0-R7 of both banks held, every unit held, the posted store before the fault performed
    for ph, ipr, tc2 in ((1, 'ipra=1230 iprb=4560 iprc=0a00', 'cafe0000'),
                         (2, 'ipra=1230 iprb=7890 iprc=0a00', 'cafe0000'),
                         (3, 'ipra=1230 iprb=7890 iprc=0a00', 'beef0123')):
        e.append(rf'RST phase={ph} entry ' + b0 + ' ' + b1)
        e.append(rf'RST phase={ph} ccn expevt=020 ' + common_ccn)
        e.append(rf'RST phase={ph} p4 {ipr} ' + tmu % tc2)
        e.append(rf'RST phase={ph} misc sgr=\S+ tmu1_running=1')
    e.append(r'cpureset: done')
    return e


def exp_sleepwk():
    # every case wakes once by TMU0 (0x400) with SPC = SLEEP + 2; SSR keeps BL = 1 for the BL = 1 sleeps (hm 9.5.4)
    e = [r'SLP tstr n=1 intevt=400 at=2 ssr=40000000 counted=\d+',
         r'SLP unie n=1 intevt=400 at=2 ssr=40000000 counted=\d+',
         r'SLP ipra n=1 intevt=400 at=2 ssr=40000000 counted=\d+',
         r'SLP dslp n=1 intevt=400 at=2 ssr=40000000 counted=\d+',
         r'SLP bl1 n=1 intevt=400 at=2 ssr=50000000 counted=\d+',
         r'SLP bl1unie n=1 intevt=400 at=2 ssr=50000000 counted=\d+']
    # the IMASK race: taken before the SLEEP (SPC = SLEEP, then a second wake after RTE) or woken from it (SLEEP + 2)
    for k in range(13):
        e.append(rf'SLP imask k={k} n=(1 intevt=400 at=2|2 intevt=400 at=(0|-\d+)) ssr=40000000 counted=\d+')
    e.append(r'sleepwk: done')
    return e


def exp_p4rules():
    e = []
    # W: SOC.md 3.1.4 [HW]: a register answers only its own size; the wrong-size writes are ignored
    for n, v in [('IPRA', '0'), ('IPRC', '0'), ('ICR', '8000'), ('TCOR0', '11223344'), ('TCNT1', '55667788'),
                 ('TCR0', '2'), ('TSTR', '0'), ('TOCR', '0'), ('STBCR', '3'), ('STBCR2', '0'), ('FRQCR', 'e0a'),
                 ('PTEH', '0'), ('TTB', '0'), ('TRA', '0'), ('EXPEVT', None), ('INTEVT', '0'), ('QACR0', '4'),
                 ('MMUCR', '0'), ('CCR', '105'), ('PMCR2', '0'), ('BASRA', '0'), ('BARA', '0'), ('BBRA', '0'),
                 ('PCTRB', '0'), ('GPIOIC', '0'), ('SAR1', 'c001000')]:
        if v is None:
            e.append(rf'P4R W {n} before=(?P<x>{H}) after=(?P=x)')
        else:
            e.append(rf'P4R W {n} before={v} after={v}')
    # N: P4B_NONE / P4B_RSV (G5) / the arrays (IDEAL_MEM null slaves, TRANSLATE = 0 TLB arrays): read 0, writes ignored.
    # --mem real (M3-fix, M3-verify F10: rm, the real IC / OC): the cache arrays keep the longword write 5A5AA5A5 (hm 4.5:
    # an address array {tag PA[28:10], (OC: U bit 1,) V bit 0}: 1A5AA401; a data array the word), and a word / byte
    # access reads 0 and writes nothing (SOC.md 3.1.4)
    for n in ['none_ff040000', 'none_ff140000', 'none_ff300000', 'none_ffb00000', 'none_fff80000', 'none_ffc00020',
              'rsv_e4000000', 'rsv_eff00000', 'rsv_f8000000', 'rsv_fef00000', 'icaa', 'icda', 'itlbaa', 'ocaa', 'ocda',
              'utlbaa', 'utlbda', 'utlbaa_assoc']:
        r32 = {'icaa': '1a5aa401', 'ocaa': '1a5aa401', 'icda': '5a5aa5a5', 'ocda': '5a5aa5a5'}.get(n, '0') if REAL_MEM else '0'
        e.append(rf'P4R N {n} r32={r32} r16=0 r8=0')
    # Q: two longword transactions, a = FR(2n), a + 4 = FR(2n+1) (SOC.md 3.1.4); PTEH [9:8] read 0; DMAC ch0 in DDT
    # mode loads 0 into SAR0 / DAR0 (dmac.md 2.2, measured); 16-bit registers ignore both longword halves
    e += [r'P4R Q tmu tcor0=aaaa0001 tcnt0=bbbb0002 q=aaaa0001,bbbb0002',
          r'P4R Q pteh=12345c55 ptel=c0001fc q=12345c55,c0001fc',
          r'P4R Q tra=3fc expevt=fe0 q=3fc,fe0',
          r'P4R Q sar0=0 dar0=0 q=0,0',
          r'P4R Q icr=8000 ipra=1230 q=0,0']
    # A: hm 5.6 (6): a data address error, TEA = the address, the access not performed
    e += [r'P4R A ffd8000a n=1 expevt=0e0 tea=ffd8000a r0=7ead',
          r'P4R A ffd00005 n=1 expevt=0e0 tea=ffd00005 r0=7ead',
          r'P4R A ff000022 n=1 expevt=0e0 tea=ff000022 r0=7ead',
          r'P4R A ffd80009 n=1 expevt=100 tea=ffd80009 r0=0',
          r'P4R A tcor0_after=badc0de']
    # L: debug-ubc-hudi 2.2.1 [P]: the latest operand read's right-aligned data; a store, PREF, STC do not change it
    e.append(r'P4R L ld32=deadbeef ld16s=8001 ld8=f0 pclk=badc0de afterstore=12345678 r16=5678 pref=12345678 '
             r'sts=12345678')
    e.append(r'p4rules: done')
    return e


def exp_frq():
    # hm 10: FRQCR reads back what was written; the relock settle does not change that
    return [rf'FRQ k={k} first=0a0a r1=0e0a r2=0e0a' for k in range(9)] + [r'frq: done']


def exp_excrace():
    e = [r'XR expevt_trapa n=1 \[expevt=160 tra=044 spc=8\] now: expevt=160 tra=044',
         r'XR tra_trapa n=1 \[expevt=160 tra=088 spc=8\] now: expevt=160 tra=088',
         r'XR tra_illegal n=1 \[expevt=180 tra=3f0 spc=6\] now: expevt=180 tra=3f0',   # TRA is TRAPA's only
         r'XR expevt_slot n=1 \[expevt=160 tra=0cc spc=12\] now: expevt=160 tra=0cc',
         r'XR expevt_adrerr n=1 \[expevt=0e0 tra=0cc spc=8\] now: expevt=0e0 tra=0cc',
         r'XR trapa_slot n=1 \[expevt=1a0 tra=0cc spc=0\] now: expevt=1a0 tra=0cc']       # slot illegal, SPC = BRA
    # the entry writes INTEVT after the older store: 0x400 in the handler; 0x3E0 afterwards only if the store ran after
    for k in range(17):
        e.append(rf'XR intevt k={k} n=1 handler=400 at=((?:[1-9]\d*) after=400|(?:0|-\d+) after=3e0)')
    e.append(r'excrace: done')
    return e


def exp_mmu0():
    x = r'n=1 r0=\S+ \[{c} tea=(?:{t}) spc=\+?{s} ssr=000000f[01]\]'
    rows = [
        ('ld', '0c012ee0', 0, '180', H, 2), ('ld', '8c012ee0', 0, '0e0', '8c012ee0', 0),
        ('ld', 'cc012ee0', 0, '0e0', 'cc012ee0', 0), ('ld', 'ffd80008', 0, '0e0', 'ffd80008', 0),
        ('ld', '7c001000', 0, '180', H, 2), ('ld', '0c012ee2', 0, '0e0', '0c012ee2', 0),
        ('ldw', '0c012ee1', 0, '0e0', '0c012ee1', 0), ('st', 'ac012ee0', 0, '100', 'ac012ee0', 0),
        ('st', 'e0000000', 0, '180', H, 2), ('st', 'e0000000', 1, '100', 'e0000000', 0),
        ('st', 'e3ffffe0', 1, '100', 'e3ffffe0', 0), ('pref', 'e0000000', 0, '180', H, 2),
        ('pref', 'e0000000', 1, '0e0', 'e0000000', 0), ('pref', '8c012ee0', 0, '0e0', '8c012ee0', 0),
        ('pref', '0c012ee0', 0, '180', H, 2), ('ocbi', '8c012ee0', 0, '100', '8c012ee0', 0),
        ('ocbwb', '8c012ee0', 0, '0e0', '8c012ee0', 0), ('movca', '8c012ee0', 0, '100', '8c012ee0', 0),
        ('tas', '8c012ee0', 0, '0e0', '8c012ee0', 0), ('tas', '0c012ee4', 0, '180', H, 2),
        ('stcsr', '00000000', 0, '180', H, 0), ('ldcsr', '00000000', 0, '180', H, 0),
        ('stcvbr', '00000000', 0, '180', H, 0), ('stcgbr', '00000000', 0, '180', H, 2),
        ('ldcgbr', '8c000000', 0, '180', H, 2), ('rte', '00000000', 0, '180', H, 0),
        ('sleep', '00000000', 0, '180', H, 0), ('ldtlb', '00000000', 0, '180', H, 0),
        ('stsfpscr', '00000000', 0, '180', H, 2)]
    e = []
    for n, a, sq, c, t, s in rows:
        e.append(rf'MMU0 {n} {a} sqmd={sq} ' + x.format(c=c, t=t, s=s))
    e.append(r'MMU0 trapa 00000000 sqmd=0 n=2 r0=\S+ \[160 tea=\S+ spc=\+2 ssr=000000f0\] \[180 tea=\S+ spc=\+2 ssr=000000f0\]')
    e.append(r'MMU0 ldcrbank 00000000 sqmd=0 ' + x.format(c='180', t=H, s=0))
    e.append(r'MMU0 stcsgr 00000000 sqmd=0 ' + x.format(c='180', t=H, s=0))
    e.append(r'MMU0 slotill 00000000 sqmd=0 ' + x.format(c='1a0', t=H, s=0))
    e.append(r'MMU0 jmp 0c0\S+ sqmd=0 n=1 r0=\S+ \[180 tea=\S+ spc=\+\d+ ssr=000000f0\]')
    e.append(r'MMU0 jmp (?P<a>8c0\S+) sqmd=0 n=1 r0=\S+ \[0e0 tea=(?P=a) spc=\S+ ssr=000000f0\]')
    e.append(r'MMU0 jmp (?P<a>0c0\S+) sqmd=0 n=1 r0=\S+ \[0e0 tea=(?P=a) spc=\S+ ssr=000000f0\]')
    e.append(r'MMU0 jmp fffffff0 sqmd=0 n=1 r0=\S+ \[0e0 tea=fffffff0 spc=\S+ ssr=000000f0\]')
    e.append(r'MMU0 ld_at1 0c012ee8 sqmd=0 ' + x.format(c='180', t=H, s=2))
    # AT = 1, TRANSLATE = 0: identity (SOC.md 3.6); MMUCR.TI reads 0
    e.append(r'MMU0 at1 mmucr=00000001 p0=13579bdf p3=13579bdf p1=13579bdf st_p0_rd_p1=2468ace0')
    e.append(r'mmu0: done')
    return e


def exp_misc():
    e = []
    # an interrupt made acceptable before a TRAPA: TRAPA's entry first (SPC = next) and the interrupt after its RTE, or
    # the interrupt first at the TRAPA (SPC = the TRAPA, which then runs); never between (SOC.md 4.3.2, 6.2)
    for k in range(15):
        e.append(rf'IE trapa k={k} n=2 (\[exc 160 spc=2\] \[irq 400 spc=2\]|\[irq 400 spc=0\] \[exc 160 spc=2\])')
    # a misaligned load: re-execution type, SPC = the load (the handler resumes with IMASK 15: the interrupt stays
    # pending unless it came first)
    for k in range(15):
        e.append(rf'IE adr k={k} (n=1 \[exc 0e0 spc=0\]|n=2 \[irq 400 spc=0\] \[exc 0e0 spc=0\])')
    for k in range(15):
        e.append(rf'IE ldtlb k={k} n=1 \[irq 400 spc=\d*[02468]\]')
    # RMW / TAS.B on P4 bytes: each register's own write mask; a byte access to IPRA (16-bit) reads 0, write ignored
    e += [r'RMW TOCR t0=1 or=1 xor=0 and=0 tas_t=1 tas=0 final=0',
          r'RMW TSTR t0=1 or=1 xor=2 and=2 tas_t=0 tas=2 final=2',
          r'RMW IPRA t0=1 or=0 xor=0 and=0 tas_t=1 tas=0 final=1230',
          r'RMW STBCR2 t0=1 or=0 xor=0 and=0 tas_t=1 tas=80 final=80',
          r'misc: done']
    return e


EXP = {'iprwin': exp_iprwin, 'prio': exp_prio, 'cpureset': exp_cpureset, 'sleepwk': exp_sleepwk,
       'p4rules': exp_p4rules, 'frq': exp_frq, 'excrace': exp_excrace, 'mmu0': exp_mmu0, 'misc': exp_misc}


def check(prog, lines):
    bad = []
    pos = 0
    for rx in EXP[prog]():
        r = re.compile(rx + r'$')
        for i in range(pos, len(lines)):
            if r.match(lines[i]):
                pos = i + 1
                break
        else:
            bad.append(f'missing / wrong: {rx}')
    if prog == 'iprwin':
        bad += iprwin_extra(lines)
    return bad


def load(path):
    with open(path) as f:
        return [l.rstrip('\n') for l in f]


def self_test(d):
    """every pinned line of every passing log in d, perturbed in its first hex field's last digit, must be rejected"""
    ok = True
    for prog in EXP:
        ls = load(os.path.join(d, prog + '.log'))
        if check(prog, ls):
            print(f'--self {prog}: the reference log does not pass'); ok = False; continue
        n = caught = 0
        for i, l in enumerate(ls):
            m = re.search(r'=([0-9a-f]+)', l)
            if not m or 'counted=' in l[m.start():m.start() + 9]:
                continue
            d0 = m.group(1)[-1]
            pert = l[:m.end() - 1] + ('1' if d0 != '1' else '2') + l[m.end():]
            ls2 = ls[:i] + [pert] + ls[i + 1:]
            if any(rx for rx in EXP[prog]() if re.compile(rx + '$').match(l)):
                n += 1
                if check(prog, ls2):
                    caught += 1
        print(f'--self {prog}: {caught} / {n} perturbed pinned lines rejected')
        ok = ok and caught == n
    # the iprwin window rules: a gap in the window, a take at k >= 12, and no take at k = 0 are each rejected
    ls = load(os.path.join(d, 'iprwin.log'))
    for mode, k, v in (('lower', 0, 0), ('lowerbusy', 19, 1), ('lower', 12, 1), ('unfclr', 1, 0)):
        rx = re.compile(rf'IPRW {mode} k={k} ')
        ls2 = []
        for l in ls:
            if rx.match(l):
                l = f'IPRW {mode} k={k} taken=1 intevt=400 at=5 adds=48' if v else f'IPRW {mode} k={k} taken=0 adds=48'
            ls2.append(l)
        if ls2 == ls or not check('iprwin', ls2):
            print(f'--self iprwin window rule ({mode} k={k} -> taken={v}): NOT rejected'); ok = False
        else:
            print(f'--self iprwin window rule ({mode} k={k} -> taken={v}): rejected')
    print('--self:', 'PASS' if ok else 'FAIL')
    return 0 if ok else 1


def main():
    global REAL_MEM
    a = sys.argv[1:]
    if '--mem' in a:
        i = a.index('--mem')
        REAL_MEM = a[i + 1] == 'real'
        del a[i:i + 2]
    if a and a[0] == '--self':
        return self_test(a[1])
    if len(a) == 2 and a[0] in EXP:
        bad = check(a[0], load(a[1]))
        print(f'{a[0]}: {"PASS" if not bad else "FAIL"}' + ''.join(f'\n    {b}' for b in bad[:10]))
        return 1 if bad else 0
    d = a[0]
    progs = a[a.index('--progs') + 1].split(',') if '--progs' in a else list(EXP)
    st = 0
    for prog in progs:
        p = os.path.join(d, prog + '.log')
        bad = check(prog, load(p)) if os.path.exists(p) else [f'no log {p}']
        print(f'soc-adv {prog:9s} {"PASS" if not bad else "FAIL"}' + ''.join(f'\n    {b}' for b in bad[:6]))
        st |= bool(bad)
    print('soc-adv check:', 'PASS' if not st else 'FAIL')
    return st


if __name__ == '__main__':
    sys.exit(main())
