#!/usr/bin/env python3
# mkexpected.py -- expected output of the bare-metal exceptions-intc probes (tb/bm/exceptions-intc), taken from the
# console logs tools/hw/exceptions-intc/results_run{1,2,3}.txt (excint / excint2 / excint3 on the Dreamcast,
# 2026-09-23).  Writes expected.json next to this script; check.py compares a SoC run's output with it.
#
# Every console line between "<prog>: SR ..." and "<prog>: done" becomes one entry, in order:
#   id       the line's leading label (what check.py pairs a run's line with)
#   console  the console's text
#   mode     exact   the run must print the same text
#            match   the same text except link-dependent values: pointers into the image / stack (8c......, ac......)
#                    are wildcards, tied together by `rel` where the console shows a relation (TEA = buf + d, TEA =
#                    SPC, PR = SPC + 4, r1 = buf + d, ...); store-queue addresses (the probe's 0xE0000000 | (buffer &
#                    0x03FFFFC0): e0 to e3, 64-byte aligned) are wildcards of that form, TEA tied to r1 where the
#                    console shows them equal; SR.T in ssr / sr is masked where the C code around the probe decides it;
#                    FR values are leftovers of the C run-time except in the probes that set them
#            timing  a cycle / count value that depends on the Pclk phase or on the instruction cache: `regex` accepts
#                    the console's value and the value(s) the v1 SoC model predicts (warm, both Bclk phases); `note`
#                    says which unit owns the number
#            stub    the chip state the SoC testbench's boot stub leaves (rtl/v1/SOC.md 7.3) instead of KOS's; SR.T is
#                    masked there too (the C code before the print decides it: the console printed T = 1, and so does
#                    the bare-metal image on Sh4Sys)
#            board   needs Holly's IRL (board.md; milestone M4): checked only with check.py --board.  M4 (WI-SOC): the
#                    bare-metal run-time sets KOS's Holly routing and acknowledges the routed events at the window edges
#                    as KOS's handler did (crt0.S, shim/kos.h), so the rows have rules: "I holly" the IML routing exact
#                    (the pending ISTNRM / ISTEXT / ISTERR are the board's SPG events at the print, which no level routes:
#                    any value); the irl rows as `match` records with INT kept, SPC the DT or the BF of the wait loop
#            info    recorded, not checked (a fetch effect outside these units, noted)
#   regex    for match / timing / stub: the whole line must match (Python re.fullmatch)
#   rel      for match: [a, op, b, k] relations between named groups: a == b + k
#   soc_regex for a record printed before the program's first interrupt probe: the regex a SoC run must match instead of
#            `regex`.  It pins INT=000: after the power-on reset and the boot stub INTEVT is 0 (the stub line pins it)
#            and nothing is accepted until the first probe (IMASK 15), so a core whose entry also writes INTEVT on a
#            general exception shows up in every such record.  The console's own INT values there (400 / 3a0) are
#            KOS's TMU0 / Holly vblank interrupts, which `regex` leaves open for --self.
import json, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
LOGS = os.path.join(ROOT, 'tools', 'hw', 'exceptions-intc')

PTR = re.compile(r'^(8c|8d|ac|ad)[0-9a-f]{6}$')
SQA_RX = r'e[0-3][0-9a-f]{4}[048c]0'            # 0xE0000000 | (buffer & 0x03FFFFC0): the probe's SQ0 address
SQA = re.compile('^' + SQA_RX + '$')
STUB_SR = r'SR 400000f[01]'                     # the boot stub's SR 400000F0 (SOC.md 7.3), T left by the C code
FR_KEEP = {'pk_fcmp': ('fr0',), 'pk_fadd': ('fr0', 'fr1'), 'pk_fpe': ('fr0',)}
T_KEEP = {'pk_sett', 'pk_fcmp', 'rte_slot_t'}
INT_KEEP = {'prio_irq_ill', 'irl2_imask1', 'irl4_imask3', 'irl6_imask5'}   # (the irl rows: M4, the level's code)


def is_ptr(v):
    return bool(PTR.match(v))


def record_rule(line, bufs, int_pin=None):
    """match rule for a record line (NAME #i k1 EXP=... / NAME none ...); int_pin: the INT value a SoC run must print"""
    toks = line.split(' ')
    name = toks[0]
    parts, rels, groups, sq = [], [], {}, {}
    for t in toks:
        if t == '':
            parts.append('')
            continue
        m = re.fullmatch(r'([A-Za-z0-9_]+)=([0-9a-f]{8})(\(L[+-]\d+\))?', t)
        if m:
            k, v, suf = m.group(1), m.group(2), m.group(3) or ''
            g = k.replace('+', '')
            if k in ('fr0', 'fr1') and k not in FR_KEEP.get(name, ()):
                parts.append(f'{k}=[0-9a-f]{{8}}')
                continue
            if k in ('ssr', 'sr') and name not in T_KEEP:
                parts.append(f'{k}={v[:7]}[{int(v[7], 16) & 0xE:x}{int(v[7], 16) | 1:x}]')
                continue
            if is_ptr(v) or suf:
                parts.append(f'{k}=(?P<{g}>[0-9a-f]{{8}})' + re.escape(suf))
                groups[g] = int(v, 16)
                continue
            if SQA.match(v):
                # a store-queue address: its buffer is link-dependent (e0063740 under KOS, e0015180 in the bm image)
                parts.append(f'{k}=(?P<{g}>{SQA_RX})')
                sq[g] = int(v, 16)
                continue
            parts.append(re.escape(t))
            continue
        m = re.fullmatch(r'INT=([0-9a-f]{3})', t)
        if m and name not in INT_KEEP:
            parts.append(f'INT={int_pin}' if int_pin else 'INT=[0-9a-f]{3}')
            continue
        parts.append(re.escape(t))
    # relations the console shows: one value is another + a small offset
    names = list(groups)
    for a in names:
        for b in names + [f'buf{i}' for i in range(len(bufs))]:
            if a == b:
                continue
            bv = groups[b] if b in groups else bufs[int(b[3:])]
            d = groups[a] - bv
            if a in ('TEA', 'r1', 'r2', 'pr') and 0 <= d < 256 and (b.startswith('buf') or b == 'spc'):
                if a == 'pr' and d != 4:
                    continue
                rels.append([a, b, d])
    # a store-queue TEA is the address the access used: TEA = r1 where the console shows them equal
    for a in sq:
        if a != 'r1' and 'r1' in sq and sq[a] == sq['r1']:
            rels.append([a, 'r1', 0])
    # keep one relation per value (the buffer, else the SPC)
    seen, keep = set(), []
    for r in rels:
        if r[0] not in seen:
            seen.add(r[0])
            keep.append(r)
    return '^' + ' '.join(parts) + '$', keep


# [HW] the warm S PI sweep (tools/hw/exceptions-intc/piwarm.elf.hwlog, M1a-fix: 7 warm repetitions, identical)
PI_WARM = [18, 16, 15, 14, 17, 16, 15, 14, 17, 16, 15, 14]


def alt(*vals):
    return '(?:' + '|'.join(sorted({str(v) for v in vals}, key=lambda s: (len(s), s))) + ')'


def rx_pairs(label, pairs, tail):
    return '^' + re.escape(label) + ''.join(' ' + p for p in pairs) + tail + '$'


# the lines of a probe that takes an interrupt: from the first one on, INTEVT is no longer the power-on 000
IRQ_PROBES = ('C1 ', 'C2 ', 'C4 ', 'prio_irq', 'S PH', 'S PB', 'S PS', 'S PF', 'S PI', 'I holly', 'irl')


def entries_excint(lines):
    out, bufs, irq_seen = [], [], False
    for ln in lines:
        irq_seen = irq_seen or ln.startswith(IRQ_PROBES)
        lab = ln.split(' ')[0] if ln.split(' ')[0] not in ('A', 'C', 'C1', 'C1g', 'C1h', 'C2', 'C4', 'D') else \
            ' '.join(ln.split(' ')[:2])
        e = {'id': lab, 'console': ln}
        if ln.startswith('excint: SR'):
            e.update(mode='stub', regex=r'^excint: ' + STUB_SR + r' VBR 8c000000 ICR 8000 IPRA 0000 IPRB 0000 '
                     r'IPRC 0000 IPRD\(FFD00010\) 0000 STBCR 03 STBCR2 00$',
                     note='state under KOS on the console; the SoC boot stub leaves SR 400000F0 (T: what the C code '
                          'before the print left, masked), VBR 8C000000, the INTC at its reset values, STBCR 03 (SOC.md '
                          '7.3)')
        elif ln.startswith('excint: EXPEVT'):
            e.update(mode='stub', regex=r'^excint: EXPEVT 00000000 INTEVT 00000000 TRA 00000000 TEA 00000000 MMUCR '
                     r'00000000 QACR0 00000004 WTCSR 00 WTCNT 00$',
                     note='after a power-on reset and the boot stub (QACR0 = 4); KOS / dcload left 060 / 400 / 080')
        elif ln.startswith('A masks: EXPEVT'):
            e.update(mode='exact')
        elif ln.startswith('A masks: SR'):
            e.update(mode='match', regex='^' + re.escape(ln[:ln.index('(SGR')]) +
                     r'\(SGR [0-9a-f]{8} R15 [0-9a-f]{8}\)$', rel=[])
        elif ln.startswith('buf='):
            e['id'] = 'buf='
            bufs.append(int(ln[4:12], 16))
            e.update(mode='match', regex=r'^buf=(?P<buf>[0-9a-f]{8})$', rel=[], defines='buf')
        elif ln.startswith('C: '):
            e.update(mode='match', regex=r'^C: sr6=4000006[01] sr15=400000f[01]$', rel=[])
        elif ln.startswith('C1 '):
            k = ln.split()[1]
            # model (tb/v1_intc_tb.cpp progs): the IMASK path gives acceptance at s + 9 / s + 10 by Bclk phase; the
            # console's first trial is cold (IC)
            model = {'ldc_sr': ((7, 8), (21, 22)), 'ldc.l_sr': ((7, 8), (21, 22)), 'rte': ((6, 7), (23, 24)),
                     'bl_1to0': ((0,), (14,))}
            if k in model:
                ks, cs = model[k]
                cons = re.findall(r' (\d+)/(\d+)', ln)
                ccyc = re.search(r'cyc\(pmctr before unmask -> handler\):((?: \d+)+)', ln).group(1).split()
                kv = alt(*ks, *[int(a) for a, b in cons])
                cv = alt(*cs, *ccyc)
                e.update(mode='timing', regex=r'^C1 ' + re.escape(k) + r' +k:' + f'( {kv}/{kv}){{8}}' +
                         r'  cyc\(pmctr before unmask -> handler\):' + f'( {cv}){{8}}' +
                         r'  INT=420 ssr=4000006[01]$',
                         note='WI-EXC IMASK path (k: acceptance at s + 9 / s + 10, v1 schedule) + WI-CORE-A entry '
                              '(h = t + 6) + WI-P4 / WI-DBG PMCTR sample (cyc); console trial 1 is instruction-cache cold'
                              + ('; the RTE handler line missed on the console (cyc 50, spec 3.14-6)' if k == 'rte'
                                 else ''))
            else:
                # the console: 13 / 17 / 15 x 6 (warm 15).  SOC.md 9.19, closed at M1a (WI-SOC): a Pclk write lands at
                # its slave at the end of its bus cycle, 8 Icyc after the strobe (MA1 + 1..4) for a word; with the
                # INTC's 6 (IPR) / 4-5 (TMU level) that gives k = 14..17 by the store's Pclk phase, and 18 when the
                # store's MA1 follows a Pclk read's ack (its entry is taken one cycle late).  [HW] the whole phase set:
                # the warm PI sweep (tools/hw/exceptions-intc/piwarm, M1a-fix) is 18 16 15 14 17 16 15 14 17 16 15 14,
                # 18 at n = 0 (the store right behind the TCNT1 read).  This store's phase comes from the C code between
                # tmu1_pend's last TCR1 read and it, which the bare-metal build does not share with KOS (SOC.md 8.3):
                # trials 1-2 (cold on the console: 13, 17) 13-18; trials 3-8 warm, so one value, in the set 14-18
                e.update(mode='timing', regex=r'^C1 ' + re.escape(k) + r' +k:( (?:1[3-8])/(?:1[3-8])){2}'
                         r' (?P<w>1[4-8])/(?P=w)( (?P=w)/(?P=w)){5}'
                         r'  INT=420 ssr=4000006[01]$',
                         note='an IPR / TCR.UNIE store enabling a pending TMU1: console k = 13 / 17 / 15 x 6 (warm 15). '
                              'SOC.md 9.19 (closed at M1a): the Pclk write lands at the end of its bus cycle (strobe + 8 '
                              'for a word), then IPR -> request 6 / TMU level -> request 4 / 5 (WI-EXC): k = 14..17 by '
                              'the store\'s Pclk phase (the PI sweep shows 17 16 15 14 on the console), 18 when the '
                              'store follows a Pclk read\'s ack ([HW] piwarm: the warm PI sweep gives the whole set); '
                              'the console\'s 13 / 17 are its cold first trials.  Trials 1-2 13-18, trials 3-8 one '
                              'warm value in 14-18 (the store\'s phase is the bare-metal C code\'s)')
        elif ln.startswith('C1g'):
            e.update(mode='timing', regex=r'^' + re.escape(' '.join(ln.split()[:3])) + r' k:( \d+/\d+){12}$',
                     note='TMU1 start -> interrupt: WI-CLK (TMU tick) + WI-EXC (request 4 / 5 after the level); the '
                          'console values (k / cycles) are recorded for M1a comparison, not pinned here')
        elif ln.startswith('C1h'):
            e.update(mode='info', note='BRA to the instruction right after its slot: the fetch quirk of excint2 R jmp '
                                       '(22-29 cycles, spec 3.14 run 3) moves these; outside WI-EXC')
        elif ln.startswith('prio_irq_ill'):
            # which entry comes first is decided by the acceptance time, not by function: an interrupt accepted before
            # the 0xFFFD reaches D is taken first (a k3 record at L+0, then the 0x180 as a second record: what a model
            # without the IMASK path, Sh4Sys stand-alone, prints).  The console printed one record, the 0x180 (INT=420
            # is C1h's last interrupt); v1's schedule gives the same: the 0xFFFD reaches D at s + 2, the request s + 9
            # / 10 (SOC.md 3.4), and the recording handler resumes with IMASK 15, which masks the request at once
            rx, rel = record_rule(ln, bufs)
            e.update(mode='timing', regex=rx, rel=rel,
                     note='C1i: LDC SR unmasks a pending TMU1, the next instruction is 0xFFFD.  Console and the v1 SoC '
                          'model: the 0x180 is taken first (the 0xFFFD reaches D at s + 2, the request at s + 9 / 10), the '
                          'handler resumes with IMASK 15 and the interrupt is never taken: one record.  The outcome is '
                          'acceptance timing (WI-EXC IMASK path, WI-CORE-A D stage), not function')
        elif ln.startswith('C2 '):
            m = re.match(r'^(C2 \S+ +tmu=\d wdt=\d:)(.*)$', ln)
            recs = re.findall(r'INT=([0-9a-f]{3})@(\d+)', m.group(2))
            parts = ''.join(f' INT={i}@' + alt(o, 7, 8) for i, o in recs) if recs else ' none'
            e.update(mode='timing', regex='^' + re.escape(m.group(1)) + parts + '$',
                     note='order and codes exact [HW]; @ = (SPC - L_tw1) / 2, the adds executed after LDC SR: warm '
                          'model 7 / 8 (acceptance at s + 9 / s + 10: C1 ldc_sr, the same LDC SR -> add sequence, is '
                          '7 / 8 on the console warm; the earlier "14 / 16" were byte offsets, corrected at M1a by '
                          'WI-SOC), console 0 / 4 (instruction-cache cold refetch)')
        elif ln.startswith('irl_imask'):
            e.update(mode='match', regex='^' + re.escape(ln) + '$', rel=[],
                     note='no IRL request reaches IMASK 5 / 6 (KOS routes the ASIC to level 2; no board before M4)')
        elif ln.startswith('C4 '):
            m = re.match(r'^C4 (sleep|busy) +INT=420 spc=L\+(\d+) cyc\(start->handler\)=(\d+) tcnt1=([0-9a-f]{8}) '
                         r'ssr=([0-9a-f]{8})$', ln)
            kind = m.group(1)
            spc = r'L\+2' if kind == 'sleep' else r'L\+[02]'
            # M1a (WI-SOC): the console's cycles and TCNT1 include instruction-cache misses of the handler (vector
            # +0x600 and `common`: busy 1083-1095 against the warm 1062, TCNT1 at TCOR - 3 / - 4 against TCOR - 1): the
            # warm handler's TCNT1 read comes 1 / 2 counts after the reload (timers-clock J [HW]: TCOR - 1 in 32 of 32
            # for a handler's 2nd-instruction read; this handler reads it as its 5th instruction, 1 / 2 with the SLEEP
            # wake cost).  sleep (M1b, WI-SOC; M1a-verify F6): PMCTR frozen while the clock is stopped, so far fewer
            # than the ~1040 cycles of the sleep are counted; the console's 63-91 hold the cold handler, whose offset
            # the busy row of the same program shows (1083-1095 against the warm SoC's 1055: +28..+40), so the warm
            # equivalent is 23-63 (M1b, im; the SoC gives 53 with v1_cpg's clock-stop handshake, 22 without) and the
            # console's own 63-91 hold with the cold handler (M3): 23-91
            cyc = r'(?:2[3-9]|[3-8]\d|9[01])' if kind == 'sleep' else r'1[01]\d\d'
            # M3 (level cache, check.py --level cache: the real IC, the handler's misses in the run; SOC.md 8.3's M3 row,
            # M3-fix / M3-verify F1): sleep pinned to the console's own 63-91 with TCNT1 at TCOR - 3 / - 4, no warm
            # fallback (the architect's ruling, M1b-verify F5); busy the console's 1083-1095 with TCOR - 3 / - 4, or the
            # warm model's 1062 with TCOR - 1 / - 2
            ccyc = (r'(?:6[3-9]|[78]\d|9[01]) tcnt1=ffffff(?:fb|fc)' if kind == 'sleep' else
                    r'(?:(?:108[3-9]|109[0-5]) tcnt1=ffffff(?:fb|fc)|1062 tcnt1=ffffff(?:fd|fe))')
            e['cache_regex'] = f'^C4 {kind} +INT=420 spc={spc} cyc\\(start->handler\\)={ccyc} ssr=4000006[01]$'
            e.update(mode='timing', regex=f'^C4 {kind} +INT=420 spc={spc} cyc\\(start->handler\\)={cyc} '
                     r'tcnt1=ffffff(?:fa|fb|fc|fd|fe) ssr=4000006[01]$',
                     note='SLEEP: SPC = SLEEP + 2 exact [HW]; counted 23-91 (the console\'s 63-91 less the cold '
                          'handler\'s +28..+40 the busy row shows: 23-63 warm; the clock-stop handshake and the 4-Pcyc '
                          'resync of v1_cpg, SOC.md 2.5); TCNT1 1-2 counts after the reload warm (timers-clock J), 3-4 on '
                          'the console (the handler\'s lines missed the IC); busy: SPC = the dt or the bf')
        elif ln.startswith('D cost'):
            warm = re.escape('[base 3 trapa 11 ill 8 dae 11 fd 8 fpe 11]')
            cold = re.escape(re.findall(r'\[[^]]*\]', ln)[0])
            e.update(mode='timing', regex=r'^D cost \(cyc\): (?:' + warm + '|' + cold + ')' + (' ' + warm) * 3 + '$',
                     note='WI-CORE-A entry model (App. A.2): the console\'s first bracket is instruction-cache cold '
                          '(23 35 ...); the SoC at M1a has no cold fetch')
        elif ' none ' in ln or ' #' in ln:
            rx, rel = record_rule(ln, bufs)
            e.update(mode='match', regex=rx, rel=rel)
            if not irq_seen and ' INT=' in ln:
                e['soc_regex'] = record_rule(ln, bufs, '000')[0]
        else:
            e.update(mode='exact')
        out.append(e)
    return out


def entries_excint2(lines):
    out, bufs, irq_seen = [], [], False
    # the warm model of the PH sweep (tb/v1_intc_tb.cpp progs): k = 7 when s - m is even, 8 when odd, with the TCNT1
    # read's ack at ph 2 (v1_p4bus P_D2 = 11)
    ph_warm = [7, 7, 8, 7, 8, 7, 8, 7, 8, 7, 8, 7]
    for ln in lines:
        irq_seen = irq_seen or ln.startswith(IRQ_PROBES)
        toks = ln.split(' ')
        lab = toks[0] if toks[0] not in ('R', 'S', 'I') else ' '.join(toks[:3])
        e = {'id': lab, 'console': ln}
        if ln.startswith('excint2: SR'):
            bufs.append(int(ln.split()[-1], 16))
            e.update(mode='stub', regex=r'^excint2: ' + STUB_SR + r' buf (?P<buf>[0-9a-f]{8})$', rel=[],
                     defines='buf', note='SR under KOS 40000001; the boot stub leaves 400000F0 (T masked: the C code '
                                         'before the print decides it)')
        elif ln.startswith('R '):
            if 'jmp' in ln:
                e.update(mode='info', note='JMP to the instruction right after its slot: 22-29 on the console, 6 with '
                                           'the target 4 instructions later (excint3 jmp_mid): a fetch effect, WI-CORE-B')
            else:
                e.update(mode='exact', note='RTE 9 / LDC SR 8 from a PMCTR read (v1 timing; WI-CORE-A)')
        elif ln.startswith('S PH imask'):
            cons = re.findall(r' (\d+)/(\d+)', ln)
            pairs = [f'{alt(ph_warm[n], a)}/{alt(ph_warm[n], b)}' for n, (a, b) in enumerate(cons)]
            e.update(mode='timing', regex=rx_pairs('S PH imask 15->6 k(n=0..11):', pairs, ''),
                     note='WI-EXC IMASK synchroniser with the TCNT1 ack at ph 2 (WI-P4): warm k = 7 8 by the parity '
                          'of s - m; the console\'s 0 and 6 are instruction-cache artefacts (tb/v1_intc_tb.cpp progs)')
        elif ln.startswith('S PH BL'):
            e.update(mode='exact', note='BL applied at once by the core [HW 24 / 24]')
        elif ln.startswith('S PB') or ln.startswith('S PS'):
            head = ln[:ln.index('):') + 2]
            e.update(mode='timing', regex='^' + re.escape(head) + r'( \+(?:[0246]|[1-3][02468])/\d+){12}$',
                     note='never the delay slot (+8) [HW]; the target of this BRA / BF/S is the instruction right '
                          'after its slot (the fetch quirk of R jmp), so the exact offsets are fetch-owned')
        elif ln.startswith('S PF'):
            head = ln[:ln.index('):') + 2]
            e.update(mode='timing', regex='^' + re.escape(head) + r'( \[(?:180@\+0|420@\+0 180@\+0)\]){12}$',
                     note='an interrupt pending at the 0xFFFD\'s would-issue cycle wins (spec 3.5) [HW]; with warm '
                          'fetch the request arrives after it (s + 9 / 10 > s + 2): 180 first')
        elif ln.startswith('S PI'):
            # [HW] tools/hw/exceptions-intc/piwarm.elf.hwlog (M1a-fix, answering M1a-verify F6): the same snippets,
            # each point repeated 8 times with no printf between: the first repetition gives exactly run 2's values
            # (15 21 10 14 17 19 20 20 19 17 14 10: both of run 2's repetitions were cold, a printf between them), the
            # other 7 give PI_WARM at every n.  Each repetition: the warm value, or the console's own (cold) value
            cons = re.findall(r' (\d+)/(\d+)', ln)
            pairs = [f'{alt(PI_WARM[n], a)}/{alt(PI_WARM[n], b)}' for n, (a, b) in enumerate(cons)]
            e.update(mode='timing', regex=rx_pairs('S PI ipra store k(n=0..11):', pairs, ''),
                     note='IPRA store -> request: P4 posted-write strobe (WI-P4), the landing at the end of the bus '
                          'cycle 8 later (SOC.md 9.19, closed at M1a), IPR -> request 6 (WI-EXC): 18 at n = 0 (the store '
                          'right behind the TCNT1 read), then 16 15 14 17 by the phase; [HW] piwarm: the console warm '
                          'is exactly 18 16 15 14 17 16 15 14 17 16 15 14, run 2\'s values are its cold repetitions. '
                          'Each repetition: the warm value or the console\'s')
        elif ln.startswith('I holly'):
            tail = ln.split(' IML2 ', 1)[1]
            e.update(mode='board', regex=r'^I holly: ISTNRM [0-9a-f]{8} ISTEXT [0-9a-f]{8} ISTERR [0-9a-f]{8} IML2 ' +
                     re.escape(tail) + '$',
                     note='Holly IRL levels 2 / 4 / 6 (board.md 4.1; M4): KOS\'s routing (the bare-metal crt0 sets it) '
                          'exact; the pending events are the board\'s SPG state at the print (vblank-out / hblank-in are '
                          'routed nowhere; KOS had acknowledged them on the console): any value')
        elif ln.startswith('irl'):
            rx, rel = record_rule(ln, bufs)
            rx = re.sub(r'spc=L\\\+\d+', r'spc=L\\+[02]', rx)
            e.update(mode='board', regex=rx, rel=rel,
                     note='Holly IRL levels 2 / 4 / 6 (board.md 4.1; M4): the level\'s code (0x3A0 / 0x360 / 0x320) '
                          'accepted above IMASK only, SSR the probe\'s IMASK; the vblank-in arrives at any point of the '
                          'DT / BF wait loop: SPC = the DT (L+0) or the BF (L+2) [HW run 2: both]')
        elif ' none ' in ln or ' #' in ln:
            rx, rel = record_rule(ln, bufs)
            e.update(mode='match', regex=rx, rel=rel)
            if not irq_seen and ' INT=' in ln:
                e['soc_regex'] = record_rule(ln, bufs, '000')[0]
        else:
            e.update(mode='exact')
        out.append(e)
    return out


def entries_excint3(lines):
    out = []
    for ln in lines:
        e = {'id': ' '.join(ln.split()[:3]) if not ln.startswith('excint3') else ln.split()[0], 'console': ln}
        if ln.endswith(': done'):
            e.update(mode='exact')
        elif ln.startswith('excint3: SR'):
            e.update(mode='stub', regex=r'^excint3: ' + STUB_SR + '$',
                     note='the boot stub leaves 400000F0 (T masked: the C code before the print decides it)')
        elif ln.startswith('jmp_mid'):
            e.update(mode='exact')
        elif 'none' in ln:
            e.update(mode='exact')
        elif 'BL1->0 pend' in ln:
            head = ln[:ln.index('handler:') + 8]
            e.update(mode='timing', regex='^' + re.escape(head) + r'( \d+/-1/\d+){8}$',
                     note='BL cleared with the request pending: taken at the target before it executes (k = -1) [HW]')
        else:
            head = ln[:ln.index('handler:') + 8]
            rte = ln.startswith('rte')
            tt = 9 if rte else 8
            ks = (4, 5) if rte else (5, 6)
            h2 = (12, 13, 33, 37) if rte else (12, 13, 37, 43, 52)
            e.update(mode='timing', regex='^' + re.escape(head) + r' \S+' +
                     f'( {tt}/{alt(*ks)}/{alt(*h2)}){{7}}$',
                     note='to-target exact [HW]; k: acceptance at s + 9 / s + 10 (WI-EXC, v1 schedule); target -> '
                          'handler 12 / 13 warm, 33-52 on the console (the handler line missed, spec 3.14-6); trial 1 '
                          'cold')
        out.append(e)
    return out


def body(fn, start, end):
    ls = open(os.path.join(LOGS, fn)).read().splitlines()
    i = next(k for k, l in enumerate(ls) if l.startswith(start))
    j = next(k for k, l in enumerate(ls) if l.startswith(end))
    return [l.rstrip() for l in ls[i:j + 1] if l.strip() and not l.startswith('buf=') or l.startswith('buf=')]


def main():
    progs = {
        'excint': entries_excint(body('results_run1.txt', 'excint: SR', 'excint: done')),
        'excint2': entries_excint2(body('results_run2.txt', 'excint2: SR', 'excint2: done')),
        'excint3': entries_excint3(body('results_run3.txt', 'excint3: SR', 'excint3: done')),
    }
    for p, es in progs.items():
        for e in es:
            if e['console'].endswith(': done'):
                e.update(mode='exact')
    doc = {
        'about': 'Expected output of tb/bm/exceptions-intc (excint, excint2, excint3), from the console logs '
                 'tools/hw/exceptions-intc/results_run{1,2,3}.txt.  Generated by mkexpected.py; checked by check.py.',
        'programs': progs,
    }
    with open(os.path.join(HERE, 'expected.json'), 'w') as f:
        json.dump(doc, f, indent=1)
    for p, es in progs.items():
        cnt = {}
        for e in es:
            cnt[e['mode']] = cnt.get(e['mode'], 0) + 1
        print(p, len(es), 'lines', cnt)


if __name__ == '__main__':
    main()
