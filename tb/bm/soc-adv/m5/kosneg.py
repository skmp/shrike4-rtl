#!/usr/bin/env python3
"""kosneg.py -- M5 (WI-SOC): the suite checkers' --kos modes (and the board checker's --open929 band) cannot hide an
error (SOC.md 8.3 M5: "printed values equal to the console logs except timing lines documented as cache- or
phase-dependent").

For each suite, the console's own log must pass the mode the M5 gate uses; then single perturbations of that log:
  * a functional value changed                                 -> the checker must FAIL
  * a documented row changed within its bound                  -> the checker must PASS and report it documented
  * (M5-fix, M5-verify F2) a documented row changed as the chip changes it AND a functional token of the same line
    changed (INT=, ssr=, spc after SLEEP, vec, intevt, SCIF data / flags, a register's value sequence in a poll trace)
                                                               -> must FAIL (the entry frees its timing tokens only)
  * a documented timing value beyond its bound                 -> must FAIL (documented values are bounded)
  * a timing row that is NOT documented changed                -> must FAIL
Suites: exceptions-intc (KOS_DOC), core-integration ci and ci3 (soc-design KOS_DOC), serial (class cache, KOS_DOC poll
traces), timers-clock (no documented rows), caches-sq (M5-fix F5: --m3 --kos, KOS_DOC = M3's open rows and the
refresh-phase rows), board boardio / boardregs (M5-fix F1: --timing --open929, SOC.md 9.29's bounded band: the chip's
offsets are reported OPEN, a value beyond the band or a functional change fails).
usage: python3 tb/bm/soc-adv/m5/kosneg.py [--out DIR]    (exit 0 when every case behaves; run from the repo root; the
       perturbed logs go to DIR, default build/v1_soc/m5/kosneg -- the gate passes $(V1_SOC_M5_D)/kosneg)
"""
import json
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..'))
HW = os.path.join(ROOT, 'tools', 'hw')


def run(cmd):
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    return p.returncode, p.stdout


def perturb(text, pat, rep):
    t2 = re.sub(pat, rep, text, count=1, flags=re.M)
    return None if t2 == text else t2


def main():
    scr = sys.argv[sys.argv.index('--out') + 1] if '--out' in sys.argv else os.path.join(ROOT, 'build', 'v1_soc', 'm5',
                                                                                         'kosneg')
    os.makedirs(scr, exist_ok=True)
    rd = lambda *p: open(os.path.join(HW, *p), errors='replace').read()
    exc = rd('exceptions-intc', 'results_run1.txt') + rd('exceptions-intc', 'results_run2.txt') + \
        rd('exceptions-intc', 'results_run3.txt')
    ci = rd('core-integration', 'ci_run1.out')
    ci3 = rd('core-integration', 'ci_run3.out')
    scif = rd('serial', 'scif_probe.elf.hwlog')
    tc = ''.join(rd('timers-clock', f) for f in ('tc.elf.hwlog', 'tc2.elf.hwlog', 'tc3.elf.hwlog', 'tc4.elf.hwlog'))
    ct = rd('caches-sq', 'run1.log')
    bio, breg = rd('board', 'boardio.elf.hwlog'), rd('board', 'boardregs.elf.hwlog')
    sexp = json.load(open(os.path.join(ROOT, 'tb', 'bm', 'serial', 'expected', 'scif_probe.json')))
    sfunc = next(e['text'] for e in sexp['lines'] if e['class'] == 'functional' and re.search(r'\b[0-9a-f]{4}\b', e['text']))
    sfunc_bad = re.sub(r'\b([0-9a-f]{3})([0-9a-f])\b', lambda m: m.group(1) + ('1' if m.group(2) != '1' else '2'), sfunc, 1)
    KOSDOC = r'documented \(--kos'
    # name: (console text, checker command, the marker a documented row prints)
    suites = {
        'exceptions-intc': (exc, lambda f: ['python3', 'tb/bm/exceptions-intc/check.py', f, '--kos'], KOSDOC),
        'core-integration': (ci, lambda f: ['python3', 'tb/bm/core-integration/check.py', f, '--level', 'cache',
                                            '--translate', '1', '--prog', 'ci', '--kos'], KOSDOC),
        'ci3': (ci3, lambda f: ['python3', 'tb/bm/core-integration/check.py', f, '--level', 'cache', '--translate', '0',
                                '--refresh-slack', '0.094', '--prog', 'ci3', '--kos'], KOSDOC),
        'serial': (scif, lambda f: ['python3', 'tb/bm/serial/check.py', f, 'tb/bm/serial/expected/scif_probe.json',
                                    '--classes', 'functional,trace,diff,env,timing,cache', '--kos'], KOSDOC),
        'timers-clock': (tc, lambda f: ['python3', 'tb/bm/timers-clock/check.py', f, '--kos', '--progs',
                                        'tc1,tc2,tc3,tc4,tck'], KOSDOC),
        'caches-sq': (ct, lambda f: ['python3', 'tb/bm/caches-sq/check.py', f, 'tb/bm/caches-sq/expected/cachetest.json',
                                     '--m3', '--kos', '--quiet'], r'documented \(--kos, KOS_DOC\)'),
        'boardio': (bio, lambda f: ['python3', 'tb/bm/board/check.py', 'boardio', f, '--timing', '--open929'],
                    r'OPEN 9\.29'),
        'boardregs': (breg, lambda f: ['python3', 'tb/bm/board/check.py', 'boardregs', f, '--timing', '--open929'],
                      r'OPEN 9\.29'),
    }
    C4S = r'^(C4 sleep INT=)420( spc=L\+2 cyc\(start->handler\)=)91( tcnt1=)fffffffb( ssr=)40000061'
    BR2 = r'(I irq_br2 .*\n +R0 dt=\d+ vec=)6( spc=)8c012056( ssr=)400000e1( sgr=[0-9a-f]+ pr=[0-9a-f]+ expevt=060 intevt=)420'
    # (suite, what, pattern, replacement, expect): expect 'fail' or 'documented'
    cases = [
        ('exceptions-intc', 'functional: trapa taken as 0x180', r'^(trapa +#0 k1 EXP=)160', r'\g<1>180', 'fail'),
        ('exceptions-intc', 'functional: ill_fffd SPC off by 2', r'^(ill_fffd .*spc=[0-9a-f]{8}\(L)\+0\)', r'\1+2)', 'fail'),
        ('exceptions-intc', 'timing, not documented: C2 acceptance at add 14', r'^(C2 same_lvl10 .*INT=420)@0', r'\1@14', 'fail'),
        ('exceptions-intc', 'timing, documented: C1 rte k 4 (the chip\'s)', r'^(C1 rte +k:) 8/8', r'\1 4/4', 'documented'),
        # M5-fix (M5-verify F2): the chip's C4 sleep row (cyc 52, tcnt1 fffffffd) is documented; its functional tokens not
        ('exceptions-intc', 'documented: C4 sleep cyc 52 tcnt1 fffffffd (the chip\'s)', C4S,
         r'\g<1>420\g<2>52\g<3>fffffffd\g<4>40000061', 'documented'),
        ('exceptions-intc', 'F2: C4 sleep (the chip\'s) with INT=400', C4S, r'\g<1>400\g<2>52\g<3>fffffffd\g<4>40000061', 'fail'),
        ('exceptions-intc', 'F2: C4 sleep (the chip\'s) with spc=L+6', r'^(C4 sleep INT=420 spc=L\+)2( cyc\(start->handler\)=)91',
         r'\g<1>6\g<2>52', 'fail'),
        ('exceptions-intc', 'F2: C4 sleep (the chip\'s) with ssr 400000f1', C4S, r'\g<1>420\g<2>52\g<3>fffffffd\g<4>400000f1',
         'fail'),
        ('exceptions-intc', 'F2: C1 rte k 4 with INT=400', r'^(C1 rte +k:) 8/8(.*INT=)420', r'\1 4/4\g<2>400', 'fail'),
        ('exceptions-intc', 'F2: C1 ldc_sr k 0 with INT=600', r'^(C1 ldc_sr +k:) 7/7(.*INT=)420', r'\1 0/0\g<2>600', 'fail'),
        ('exceptions-intc', 'F2: C1 ipr_write k 7 with ssr 400000f1', r'^(C1 ipr_write +k:) 13/13(.*ssr=)40000061',
         r'\1 7/7\g<2>400000f1', 'fail'),
        ('exceptions-intc', 'bound: C4 sleep tcnt1 ffffffff (no count after the reload)', C4S,
         r'\g<1>420\g<2>52\g<3>ffffffff\g<4>40000061', 'fail'),
        ('exceptions-intc', 'bound: D cost trapa 70 (2 x 35)', r'^(D cost \(cyc\): \[base 23 trapa )35', r'\g<1>70', 'documented'),
        ('exceptions-intc', 'bound: D cost trapa 71', r'^(D cost \(cyc\): \[base 23 trapa )35', r'\g<1>71', 'fail'),
        ('exceptions-intc', 'bound: S PI k 43 (2 x 21 = 42)', r'^(S PI ipra store k\(n=0\.\.11\): 15/15 )21/21', r'\g<1>7/43',
         'fail'),
        ('core-integration', 'functional: irq_ldc nrec 2', r'^(I irq_ldc +ab=0 )nrec=1', r'\1nrec=2', 'fail'),
        ('core-integration', 'documented: irq_br2 R0 spc +12', r'(I irq_br2 .*\n +R0 dt=53 vec=6 spc=)8c012056', r'\g<1>8c012062',
         'documented'),
        ('core-integration', 'F2: irq_br2 spc +12 with vec 7', BR2, r'\g<1>7\g<2>8c012062\g<3>400000e1\g<4>420', 'fail'),
        ('core-integration', 'F2: irq_br2 spc +12 with intevt 400', BR2, r'\g<1>6\g<2>8c012062\g<3>400000e1\g<4>400', 'fail'),
        ('core-integration', 'F2: irq_br2 spc +12 with ssr 400000f1', BR2, r'\g<1>6\g<2>8c012062\g<3>400000f1\g<4>420', 'fail'),
        ('core-integration', 'F2: irq_ldcl spc +10 with intevt 600',
         r'(I irq_ldcl .*\n +R0 dt=\d+ vec=6 spc=)8c011cf6( ssr=[0-9a-f]+ sgr=[0-9a-f]+ pr=[0-9a-f]+ expevt=060 intevt=)420',
         r'\g<1>8c011d00\g<2>600', 'fail'),
        ('core-integration', 'bound: irq_br2 spc +34 (one IC line: 32)', r'(I irq_br2 .*\n +R0 dt=53 vec=6 spc=)8c012056',
         r'\g<1>8c012078', 'fail'),
        ('ci3', 'documented token (the max of five): ram_use max 6.109', r'^(T ram_use +[0-9.]+ \(max +)6\.000', r'\g<1>6.109',
         'documented'),
        ('ci3', 'undocumented token of a documented line: ram_use min 6.109', r'^(T ram_use +)5\.984', r'\g<1>6.109', 'fail'),
        ('ci3', 'bound: ram_use max 6.260 (0.25 above)', r'^(T ram_use +[0-9.]+ \(max +)6\.000', r'\g<1>6.260', 'fail'),
        ('serial', 'functional: a value in "%s"' % sfunc[:40], re.escape(sfunc), sfunc_bad.replace('\\', r'\\'), 'fail'),
        ('serial', 'class cache: S5 PMCTR rd 155', r'(S5 cost .*PMCTR rd )141', r'\g<1>155', 'documented'),
        ('serial', 'bound: S5 SCFDR2 rd16 9999', r'(S5 cost .*SCFDR2 rd16 )562', r'\g<1>9999', 'fail'),
        ('serial', 'F2: S15 FIFO dump data 84 -> 99 (class cache, no timing token)', r'^(S15 fifo: \[050f 0062 )84', r'\g<1>99',
         'fail'),
        ('serial', 'F2: S15 FIFO dump SCFSR2 0062 -> 00e2 (ER)', r'^(S15 fifo: \[050f )0062', r'\g<1>00e2', 'fail'),
        ('serial', 'documented: the S6c poll trace with a straddle row (batch 1\'s)',
         r'^(S6c 7E1 55 brr3 \(TEND cleared\): )4 events(.*\n.*\n.*\n)(    5169-)',
         r'\g<1>5 events\g<2>    5085-  5169 fdr 0000 fsr 0022 sptr 0011 lsr 0000\n\g<3>', 'documented'),
        ('serial', 'S6c header 5 events with 4 rows', r'(S6c 7E1 55 brr3 \(TEND cleared\): )4 events', r'\g<1>5 events', 'fail'),
        ('serial', 'F2: S6c a row\'s SCFSR2 0022 -> 0023', r'^(    5169-  5253 fdr 0001 fsr )0022', r'\g<1>0023', 'fail'),
        ('serial', 'F2: S15 flow control: SCSPTR2 never 0051 at the RTS threshold',
         r'^(   19352- 19440 fdr 090e fsr 0062 sptr )0051( fed 24 rd 0\n   19440- 19552 fdr 090f fsr 0062 sptr )0051',
         r'\g<1>0011\g<2>0011', 'fail'),
        ('serial', 'documented: S15 flow control, SCSPTR2\'s change one poll later (a phase)',
         r'^(   19352- 19440 fdr 090e fsr 0062 sptr )0051', r'\g<1>0011', 'documented'),
        ('timers-clock', 'functional: TPSC 5 counted', r'^(A tpsc 5: TCNT1 ffffffff -> )ffffffff', r'\1fffffffe', 'fail'),
        ('timers-clock', 'timing: TPSC 0 rate 16.100', r'^(A tpsc 0: .* = )15\.999', r'\g<1>16.100', 'fail'),
        # M5-fix (M5-verify F5): caches-sq's timing class under KOS by an explicit list
        ('caches-sq', 'F5: M3\'s open row ldgap32 miss within 2 x', r'^(T ldgap32 miss   .*cyc= +)7197', r'\g<1>14000',
         'documented'),
        ('caches-sq', 'F5: M3\'s open row ldgap32 miss beyond 2 x', r'^(T ldgap32 miss   .*cyc= +)7197', r'\g<1>15000', 'fail'),
        ('caches-sq', 'F5: refresh-phase row ldgap64 miss dirtyV -3', r'^(T ldgap64 miss dirtyV .*cyc= +)13943', r'\g<1>13940',
         'documented'),
        ('caches-sq', 'F5: refresh-phase row ldgap64 miss dirtyV +25', r'^(T ldgap64 miss dirtyV .*cyc= +)13943', r'\g<1>13968',
         'fail'),
        ('caches-sq', 'F5: a row in no list (ldgap4 miss +11)', r'^(T ldgap4 miss   .*cyc= +)1819', r'\g<1>1830', 'fail'),
        ('caches-sq', 'functional: CCR 0107', r'^(F CCR +)00000105', r'\g<1>00000107', 'fail'),
        # M5-fix (M5-verify F1): SOC.md 9.29's rows in a bounded band
        ('boardio', 'F1: GD drq +56 (the chip\'s 419)', r'^(B gd test_unit ok 1 drq )363', r'\g<1>419', 'documented'),
        ('boardio', 'F1: GD drq 9', r'^(B gd req_stat ok 1 drq )364', r'\g<1>9', 'fail'),
        ('boardio', 'F1: GD drq 485 (beyond the band: +40 +80)', r'^(B gd req_stat ok 1 drq )364', r'\g<1>485', 'fail'),
        ('boardio', 'F1: GD req_stat int 26410', r'^(B gd req_stat ok 1 drq \d+ pkt \d+ bsy \d+ int )2489', r'\g<1>26410', 'fail'),
        ('boardio', 'F1: GD test_unit int 99999', r'^(B gd test_unit ok 1 drq \d+ pkt \d+ bsy \d+ int )1885', r'\g<1>99999',
         'fail'),
        ('boardio', 'functional: GD req_stat ok 0', r'^(B gd req_stat )ok 1', r'\g<1>ok 0', 'fail'),
        ('boardio', 'F1: planted bug mpx_a0w8\'s req_stat row (every area-0 single write 32 Icyc longer: M5-verify)',
         r'^(B gd req_stat ok 1 )drq 364 pkt 583 bsy 910 int 2489 int2 3747', r'\g<1>drq 485 pkt 690 bsy 1090 int 2789 int2 4131',
         'fail'),
        ('boardio', 'F1: the chip\'s req_stat row (the M5 gate run)', r'^(B gd req_stat ok 1 )drq 364 pkt 583 bsy 910 int 2489 int2 3747',
         r'\g<1>drq 437 pkt 578 bsy 928 int 2641 int2 3975', 'documented'),
        ('boardregs', 'F1: SQ->VRAM32 drain / fifo 44 / 106 (the chip\'s split)',
         r'^(B lat SQ->VRAM32 .*\(\+drain )97, \+fifo 53', r'\g<1>44, +fifo 106', 'documented'),
        ('boardregs', 'F1: SQ->VRAM32 drain 99999', r'^(B lat SQ->VRAM32 .*\(\+drain )97', r'\g<1>99999', 'fail'),
        ('boardregs', 'F1: SQ->AICA RAM fifo 0', r'^(B lat SQ->AICA RAM .*\+fifo )2542', r'\g<1>0', 'fail'),
    ]
    bad = 0
    for name, (text, cmd, _) in suites.items():
        f = os.path.join(scr, name + '.log')
        open(f, 'w').write(text)
        rc, out = run(cmd(f))
        ok = rc == 0
        print('%-16s console log through its gate mode: %s' % (name, 'PASS' if ok else 'FAIL'))
        bad += not ok
    for name, what, pat, rep, expect in cases:
        text, cmd, marker = suites[name]
        t2 = perturb(text, pat, rep)
        if t2 is None:
            print('%-16s %s: the perturbation changed nothing (BAD)' % (name, what))
            bad += 1
            continue
        f = os.path.join(scr, name + '_p.log')
        open(f, 'w').write(t2)
        rc, out = run(cmd(f))
        doc = len(re.findall(marker, out))
        if expect == 'fail':
            ok = rc != 0
            res = 'rejected' if ok else 'ACCEPTED (BAD)'
        else:
            ok = rc == 0 and doc >= 1
            res = 'accepted as documented' if ok else ('rejected (BAD)' if rc else 'accepted but not reported (BAD)')
        print('%-16s %s: %s' % (name, what, res))
        bad += not ok
    print('kosneg: %s (%d console logs, %d perturbations)' % ('PASS' if not bad else 'FAIL', len(suites), len(cases)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
