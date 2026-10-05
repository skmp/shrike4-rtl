#!/usr/bin/env python3
# mkexpected.py -- builds expected/dmac1.json and expected/dmac2.json for the bare-metal DMAC probes (tb/bm/dmac,
# WI-DMAC; README.md) from the console logs tools/hw/dmac/dmac1.elf.hwlog and dmac2.elf.hwlog.
#
# Every console line the bare-metal build prints (dmac1: the preamble, S2-S12, "done"; dmac2: the preamble, T2-T5, the
# end line) gets a rule:
#   regex    a full-match pattern: the console text with the tokens that may differ replaced by named groups
#            (x_* hexadecimal, f_* decimal fraction, others decimal integers)
#   cls      functional  the regex and every `func` relation must hold on any correct chip (and on the functional
#                        C++ model): register values, masks, flags, error classes, data, the S10 priority structure
#            timing      the `timing` targets [expression, console value, tolerance] (cycle counts, the S6 / S7 unit
#                        counts that depend on the rate); the regex must match too
#            cache       exact console text; needs the operand cache (S12, coherency)
#            env         matched for its structure and its definitions only (the buffer addresses)
#            group       an S10 sample line, checked with its header (the header's `group` rule)
#   func     Python expressions over the groups and the definitions (bufA, bufB) that must be true
#   defines  groups that later lines use
# A line with timing targets is also a functional line when it has a regex; `check.py --classes` selects.
# The console buffers were bufA 0c0725c0 / bufB 0c0625c0 (dmac1) and 0c070360 / 0c060360 (dmac2); the bare-metal
# build's differ, so every address is checked relative to them.
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.join(HERE, '..', '..', '..', 'tools', 'hw', 'dmac')
HEX8 = '[0-9a-f]{8}'


def exact(line, cls='functional'):
    return {'cls': cls, 'console': line, 'regex': re.escape(line)}


def sub(line, reps, cls='functional', func=(), timing=(), defines=(), group=None):
    """reps: list of (literal console text, group regex); each literal is replaced once, left to right."""
    rx, pos = '', 0
    for lit, grp in reps:
        k = line.index(lit, pos)
        rx += re.escape(line[pos:k]) + grp
        pos = k + len(lit)
    rx += re.escape(line[pos:])
    e = {'cls': cls, 'console': line, 'regex': rx}
    if func:
        e['func'] = list(func)
    if timing:
        e['timing'] = [list(t) for t in timing]
    if defines:
        e['defines'] = list(defines)
    if group:
        e['group'] = group
    if not re.fullmatch(rx, line):
        raise SystemExit(f'mkexpected: rule does not match its own console line: {line!r}')
    return e


def g(name, pat=r'-?\d+'):
    return f'(?P<{name}>{pat})'


def body(path, first, last):
    ls = [l.rstrip('\n') for l in open(path, encoding='latin-1')]
    i = next(k for k, l in enumerate(ls) if l.startswith(first))
    j = next(k for k in range(i, len(ls)) if ls[k].startswith(last))
    return ls[i:j + 1]


# ------------------------------------------------------------------------------------------------ (M6) KOS mode
# The unmodified console probes under KOS (tools/hw/dmac/dmacN.elf behind soc_tb's dcload stand-in, the phase-2 board:
# SOC.md 8.3 M6).  Besides the bare-metal lines: S0 (the state at main and at the end), S1 (maple over DDT: the
# `multi` rule, check.py s1_check), S13 (CH2-DMA through Holly), T1 (CPU writes to the DDT channel 0).
# Board tokens: values the board's preset sets, not the chip (each allowed value with its reason):
BOARD_CHCR13 = ([0x5440, 0x0], 'CHCR1 / CHCR3 0x5440 are the BIOS / dcload leftovers of the console (dmac.md 4.4: '
                'reset values are fine); the direct boot starts them at their reset value 0')
BOARD_SAR2 = ([0xacd9e540, 0x0], 'SAR2 0xACD9E540 is a leftover of the console (dmac.md 4.4); the boot stub writes only '
              'CHCR2 (SOC.md 7.3), so the direct boot reads SAR2 at its reset value 0 (S13 restores what it read)')
BOARD_C2DSTAT = ([0x10000000, 0x11480400], 'SB_C2DSTAT is what the last CH2-DMA before the program left: the console '
                 'run followed a program that used the TA path (0x10000000), the direct boot has board.md 5.3\'s preset '
                 '0x11480400')


def s0_rule(l, which):
    """S0: the 17 DMAC registers and Holly's SB CH2 registers at main (which = 1) and at the end (2)"""
    m = re.fullmatch(r'S0 ch(\d) SAR (\w+) DAR (\w+) DMATCR (\w+) CHCR (\w+)', l)
    if m:
        c = int(m.group(1))
        if c in (1, 3) and which == 1:
            return dict(sub(l, [('CHCR ' + m.group(5), 'CHCR ' + g('x_chcr', HEX8))]), board=[['x_chcr'] + list(BOARD_CHCR13)])
        if c == 2:
            return dict(sub(l, [('SAR ' + m.group(2), 'SAR ' + g('x_sar', HEX8))]), board=[['x_sar'] + list(BOARD_SAR2)])
        return exact(l)
    if l.startswith('S0 SB C2DSTAT') and which == 1:
        v = l.split()[3]
        return dict(sub(l, [('C2DSTAT ' + v, 'C2DSTAT ' + g('x_c2dstat', HEX8))]), board=[['x_c2dstat'] + list(BOARD_C2DSTAT)])
    if l.startswith('S0 IPRC'):
        v = l.split()[-1]
        return sub(l, [('MDSTAR ' + v, 'MDSTAR ' + g('x_mdstar', HEX8))], defines=['x_mdstar'], func=[f'x_mdstar == 0x{v}'])
    return exact(l)


def s13_rule(l):
    """S13: CH2-DMA to texture memory through Holly's DDT direct requests: every register / data token exact; the two
    times (the loop's poll that saw DMATCR2 == 0 -- 0: not before the ISTNRM 19 poll ended it -- and the one that saw
    ISTNRM 19) `doc` bounds (SOC.md 9.30 (b)).  The 64 KB transfer runs at Holly's paced rate with the CPU polling
    through it (board.md 4.3's input FIFO, the BSC's CPU turn): within 5 % of the console's.  The short ones end while
    Holly's FIFO still fills: the requests come back to back, no break lets the CPU's poll through before the end on
    the chip, where the console's poll came back during them -- Holly's request pattern at a transfer's start is not
    pinned by S13's three points [P]: bounded above only (1.05 x the console's)"""
    m = re.match(r'S13 .*: DMATCR2 0 at (\d+) cyc, ISTNRM19 at (\d+) cyc \|', l)
    t1, t2 = int(m.group(1)), int(m.group(2))
    r = sub(l, [(f'at {t1} cyc,', 'at ' + g('t_tcr') + ' cyc,'), (f'at {t2} cyc |', 'at ' + g('t_ist') + r' cyc \|')])
    if t2 > 10000:
        why = ('SOC.md 9.30 (b): CH2-DMA at Holly\'s paced rate (its input FIFO, 32 units drained at 11.70 Icyc, '
               'board.md 4.3) with the CPU polling through it (the BSC\'s CPU turn, SOC.md 3.3.2): within 5 %')
        r['doc'] = [['t_ist', int(t2 * 0.95), int(t2 * 1.05) + 1, why],
                    ['t_tcr', 0, int(t1 * 1.05) + 1, why + '; 0 = DMATCR2 == 0 first seen in the ISTNRM 19 poll\'s pass']]
    else:
        why = ('SOC.md 9.30 (b): a short CH2-DMA ends while Holly\'s input FIFO still fills (requests back to back): '
               'on the chip the CPU\'s first ISTNRM poll returns after the end (83), on the console during it; '
               'Holly\'s request pattern at a transfer\'s start is not pinned by S13 [P]: bounded above (1.05 x)')
        r['doc'] = [['t_ist', 0, int(t2 * 1.05) + 16, why],
                    ['t_tcr', 0, int(t1 * 1.05) + 16, why + '; 0 = DMATCR2 == 0 first seen in the ISTNRM 19 poll\'s pass']]
    r['doc_console'] = {'t_ist': t2, 't_tcr': t1}
    return r


# ------------------------------------------------------------------------------------------------------------ dmac1
def dmac1(kos=False):
    lines = body(os.path.join(HW, 'dmac1.elf.hwlog'), 'dmac1: bufA', 'dmac1: done')
    # the bare-metal build runs S2-S12 only; the KOS build (M6, --kos) every line
    keep = lines if kos else [l for l in lines if l.startswith('dmac1:') or re.match(r'S([2-9]|1[0-2]) ', l)]
    out = []
    n_s0 = 0
    s10_modes = {'PR=10 (CH2>CH0>CH1>CH3)': 'fixed_ch1_first', 'PR=11 (round robin)': 'round_robin',
                 'PR=10 ch3 burst': 'burst', 'PR=01 (CH0>CH2>CH3>CH1)': 'fixed_ch3_first'}
    k = 0
    while k < len(keep):
        l = keep[k]
        if l.startswith('dmac1: bufA'):
            m = re.fullmatch(r'dmac1: bufA (\w+) bufB (\w+)', l)
            if kos:                                   # the console's own ELF: its own buffers
                out.append(sub(l, [(m.group(1), g('x_bufA', HEX8)), (m.group(2), g('x_bufB', HEX8))],
                               defines=['x_bufA', 'x_bufB'],
                               func=[f'x_bufA == 0x{m.group(1)}', f'x_bufB == 0x{m.group(2)}']))
            else:
                out.append(sub(l, [(m.group(1), g('x_bufA', HEX8)), (m.group(2), g('x_bufB', HEX8))], cls='env',
                               defines=['x_bufA', 'x_bufB']))
        elif kos and l.startswith('S0 '):
            if l.startswith('S0 ch0'):
                n_s0 += 1
            out.append(s0_rule(l, n_s0))
        elif kos and l.startswith('S1 t='):
            nsm = 0
            while k + nsm < len(keep) and keep[k + nsm].startswith('S1 t='):
                nsm += 1
            out.append({'cls': 'functional', 'multi': 'S1 t=', 'nmin': 24, 'nmax': 48,
                        'console': keep[k:k + nsm]})
            k += nsm - 1
        elif kos and l.startswith('S1 MDSTAR'):
            out.append(exact(l))
        elif kos and l.startswith('S13 '):
            out.append(s13_rule(l))
        elif l.startswith('dmac1: done') or l.startswith('S2 ') or l.startswith('S8 (b)') or \
                l.startswith('S8 (c)') or l.startswith('S8 (e)') or l.startswith('S9 (ii)') or \
                l.startswith('S9 (iv)'):
            out.append(exact(l))
        elif l.startswith('S3 w FFFFFFFF') or l.startswith('S3 CHCR2 was') or l.startswith('S3 DMAOR'):
            out.append(exact(l))
        elif l.startswith('S3 ch0'):
            # the "before" values are whatever Holly / the boot stub left; a CPU write loads 0 into SAR0 / DAR0 /
            # DMATCR0 and is dropped for CHCR0 (DDT mode, spec 2.2).  The stub writes CHCR0 = 0x853C3 (TE not
            # settable: 0x853C1); the console's Holly left TE = 1.
            m = re.match(r'S3 ch0 SAR (\w+)->(\w+) DMATCR (\w+)->(\w+) DAR (\w+)->(\w+) CHCR (\w+)->(\w+)', l)
            names = ['x_s0', 'x_s1', 'x_t0', 'x_t1', 'x_d0', 'x_d1', 'x_c0', 'x_c1']
            reps, pos = [], 0
            for i, nm in enumerate(names):
                reps.append((m.group(i + 1), g(nm, HEX8)))
            out.append(sub(l, reps, func=['x_s1 == 0', 'x_t1 == 0', 'x_d1 == 0', 'x_c1 == x_c0',
                                          '(x_c0 | 2) == 0x853c3']))
        elif l.startswith('S4 TS='):
            m = re.fullmatch(r'S4 TS=(\d) unit=\s*(\d+) n=64 cyc=(\d+) CHCR (\w+) SAR (\w+) \(start (\w+)\) DAR (\w+) '
                             r'\(start (\w+)\) DMATCR 0 data_ok=1 overrun=0', l)
            u = int(m.group(2))
            reps = [('cyc=' + m.group(3), 'cyc=' + g('cyc')), (m.group(5), g('x_sar', HEX8)),
                    (m.group(6), g('x_sa0', HEX8)), (m.group(7), g('x_dar', HEX8)), (m.group(8), g('x_da0', HEX8))]
            out.append(sub(l, reps, func=[f'x_sar == x_sa0 + {64 * u}', f'x_dar == x_da0 + {64 * u}',
                                          'x_sa0 == x_bufA', 'x_da0 == x_bufB'],
                           timing=[['cyc', int(m.group(3)), 64]]))
        elif l.startswith('S4 addr'):
            m = re.fullmatch(r'S4 addr SAR (\w+) DAR (\w+): cyc=(\d+) CHCR 000054c3 SAR (\w+) DAR (\w+) DMATCR 0 DMAOR '
                             r'00008201 data_ok=1 mirror_ok=(\d)', l)
            sa, da = int(m.group(1), 16), int(m.group(2), 16)
            cba, cbb = 0x0C0725C0, 0x0C0625C0
            reps = [(m.group(1), g('x_sa', HEX8)), (m.group(2), g('x_da', HEX8)),
                    ('cyc=' + m.group(3), 'cyc=' + g('cyc')), (m.group(4), g('x_sa1', HEX8)),
                    (m.group(5), g('x_da1', HEX8))]
            out.append(sub(l, reps, func=[f'(x_sa ^ x_bufA) == {sa ^ cba:#x}', f'(x_da ^ x_bufB) == {da ^ cbb:#x}',
                                          'x_sa1 == (x_sa + 128) & 0xFFFFFFFF', 'x_da1 == (x_da + 128) & 0xFFFFFFFF'],
                           timing=[['cyc', int(m.group(3)), 8]]))
        elif l.startswith('S5 '):
            m = re.match(r'S5 SM=\d DM=\d cyc=(\d+) ', l)
            out.append(sub(l, [('cyc=' + m.group(1), 'cyc=' + g('cyc'))], timing=[['cyc', int(m.group(1)), 12]]))
        elif l.startswith('S6 '):
            m = re.fullmatch(r'S6 .*: DMATCR (\w+) \((\d+) done\) CHCR 00000411 -> 00000410 SAR (\w+) DAR (\w+)', l)
            reps = [(m.group(1), g('x_t', HEX8)), ('(' + m.group(2), r'\(' + g('done')),
                    (m.group(3), g('x_sa', HEX8)), (m.group(4), g('x_da', HEX8))]
            out.append(sub(l, reps, func=['x_t == 0x1000000 - done', 'done > 0', 'x_sa == x_bufA',
                                          'x_da == x_bufB'],
                           timing=[['done', int(m.group(2)), 8]]))
        elif l.startswith('S7 suspend'):
            m = re.fullmatch(r'S7 suspend: DMATCR at DE=0 (\d+), 2000 cyc later (\d+); done (\d+); SAR-start (\d+) '
                             r'DAR-start (\d+) CHCR 00005430; copied words (\d+) bad (\d+)', l)
            reps = [('DE=0 ' + m.group(1), 'DE=0 ' + g('t1')), ('later ' + m.group(2), 'later ' + g('t2')),
                    ('done ' + m.group(3), 'done ' + g('done')), ('SAR-start ' + m.group(4), 'SAR-start ' + g('sa')),
                    ('DAR-start ' + m.group(5), 'DAR-start ' + g('da')),
                    ('words ' + m.group(6), 'words ' + g('cp')), ('bad ' + m.group(7), 'bad ' + g('bad'))]
            # t1: DMATCR read right behind the DE = 0 write; a unit in flight then still completes (hm 14.3.6, dmac.md
            # 3.1: the unit in progress ends), so t1 is t2 or t2 + 1 -- the console read 8030 / 8030, the chip under
            # KOS 8031 / 8030 (M6); nothing moves after that unit
            out.append(sub(l, reps, func=['t1 - t2 in (0, 1)', 'done == 8192 - t2', 'sa == 4 * done', 'da == 4 * done',
                                          'cp == done', 'bad == 0', '0 < done < 8192'],
                           timing=[['done', int(m.group(3)), 4]]))
        elif l.startswith('S7 resume'):
            m = re.match(r'S7 resume: cyc=(\d+) ', l)
            out.append(sub(l, [('cyc=' + m.group(1), 'cyc=' + g('cyc'))], func=['cyc > 0'],
                           timing=[['cyc', int(m.group(1)), 8030]]))
        elif l.startswith('S8 (a)'):
            m = re.match(r'S8 \(a\) done cyc=(\d+);', l)
            out.append(sub(l, [('cyc=' + m.group(1), 'cyc=' + g('cyc'))], func=['cyc > 0'],
                           timing=[['cyc', int(m.group(1)), 8]]))
        elif l.startswith('S9 (i) '):
            m = re.match(r'S9 \(i\) DMTE1: done (\d+) ', l)
            out.append(sub(l, [('done ' + m.group(1), 'done ' + g('done'))], func=['done > 0'],
                           timing=[['done', int(m.group(1)), 8]]))
        elif l.startswith('S9 (iii)'):
            m = re.search(r'CHCR 000054b1 SAR (\w+) \|', l)
            out.append(sub(l, [(m.group(1), g('x_sar', HEX8))], func=['x_sar == x_bufA + 2']))
        elif l.startswith('S10 ') and not l.startswith('S10  '):
            m = re.fullmatch(r'S10 (.*) \(ch3 (\w+), ch1 (\w+) started at (\d+); waits (-?\d+) (-?\d+)\): t:TCR1/TCR3', l)
            mode = s10_modes[m.group(1)]
            reps = [('at ' + m.group(4), 'at ' + g('started')), ('waits ' + m.group(5), 'waits ' + g('w3')),
                    (m.group(6) + ')', g('w1') + r'\)')]
            nsamp = 0
            while k + 1 + nsamp < len(keep) and keep[k + 1 + nsamp].startswith('S10  '):
                nsamp += 1
            fn = ['w3 >= 0', 'w1 >= 0']
            out.append(sub(l, reps, cls='functional' if mode != 'burst' else 'timing', func=fn,
                           group={'kind': 's10', 'mode': mode, 'lines': nsamp}))
            for q in range(nsamp):
                out.append({'cls': 'group', 'console': keep[k + 1 + q], 'regex': r'S10  ( \d+:\d+/\d+)+'})
            k += nsamp
        elif l.startswith('S11 16KB'):
            m = re.fullmatch(r'S11 16KB RAM->RAM TS=(\d) unit=\s*(\d+) (\w+): (\d+) cyc \(([\d.]+) cyc/unit, ([\d.]+) '
                             r'cyc/byte\) end DMATCR 0', l)
            n = 16384 // int(m.group(2))
            reps = [(': ' + m.group(4) + ' cyc', ': ' + g('cyc') + ' cyc'), (m.group(5), g('f_pu', r'[\d.]+')),
                    (m.group(6), g('f_pb', r'[\d.]+'))]
            out.append(sub(l, reps, func=['cyc > 0'], timing=[['cyc', int(m.group(4)), n]]))
        elif l.startswith('S11 one unit'):
            m = re.fullmatch(r'S11 one unit TS=(\d): min (\d+) max (\d+) cyc \(CHCR write to DMATCR==0 seen\)', l)
            reps = [('min ' + m.group(2), 'min ' + g('mn')), ('max ' + m.group(3), 'max ' + g('mx'))]
            out.append(sub(l, reps, func=['0 < mn <= mx'], timing=[['mn', int(m.group(2)), 8]]))
        elif l.startswith('S11 512'):
            m = re.fullmatch(r'S11 512 x 32B burst (\S+): (\d+) cyc \(([\d.]+) cyc/unit\)', l)
            reps = [(': ' + m.group(2), ': ' + g('cyc')), (m.group(3), g('f_pu', r'[\d.]+'))]
            out.append(sub(l, reps, func=['cyc > 0'], timing=[['cyc', int(m.group(2)), 512]]))
        elif l.startswith('S12 '):
            out.append(exact(l, cls='cache'))
        else:
            raise SystemExit(f'mkexpected: no rule for dmac1 line {l!r}')
        k += 1
    return out


# ------------------------------------------------------------------------------------------------------------ dmac2
def dmac2(kos=False):
    lines = body(os.path.join(HW, 'dmac2.elf.hwlog'), 'dmac2: bufA', 'dmac2: end')
    keep = lines if kos else [l for l in lines if l.startswith('dmac2:') or re.match(r'T[2-5] ', l)]
    out = []
    cba = 0x0C070360
    for l in keep:
        if l.startswith('dmac2: bufA'):
            m = re.fullmatch(r'dmac2: bufA (\w+) bufB (\w+) DMAOR 00008201', l)
            out.append(sub(l, [(m.group(1), g('x_bufA', HEX8)), (m.group(2), g('x_bufB', HEX8))],
                           defines=['x_bufA', 'x_bufB'],
                           func=[f'x_bufA == 0x{m.group(1)}', f'x_bufB == 0x{m.group(2)}'] if kos else ()))
        elif kos and l.startswith('T1 after'):
            # the maple DMAs over 20M Icyc (100 ms: 6 vblanks); the registers Holly's DTRs left, as before the writes
            m = re.match(r'T1 after 100 ms \((\d+) maple DMAs seen\)', l)
            out.append(sub(l, [('(' + m.group(1) + ' maple', r'\(' + g('ndma') + ' maple')],
                           timing=[['ndma', int(m.group(1)), 1]]))
        elif kos and l.startswith('T1 '):
            out.append(exact(l))
        elif l.startswith('dmac2: end'):
            m = re.fullmatch(r'dmac2: end DMAOR 00008201 CHCR0-3 (\w+) 00000000 000012c0 00000000', l)
            out.append(sub(l, [(m.group(1), g('x_c0', HEX8))], func=['(x_c0 | 2) == 0x853c3']))
        elif l.startswith('T2 ') or l.startswith('T3 TSTR'):
            out.append(exact(l))
        elif l.startswith('T3 '):
            m = re.fullmatch(r'(T3 .{46}) DMAOR (\w+) DMATCR (\d+) CHCR (\w+) SAR (\w+) DAR-start ([+-]\d+) \| dst (\w+) '
                             r'(\w+) (\w+) \| DMAOR after (\w+)', l)
            tag = m.group(1).strip()
            sar = int(m.group(5), 16)
            reps, func = [], []
            if tag.find('SAR=RAM') >= 0:          # SAR relative to the buffer (RAM, RAM + 1)
                reps.append((m.group(5), g('x_sar', HEX8)))
                func.append(f'x_sar == x_bufA + {sar - cba}')
            if tag.find('SAR=RAM (not on-chip)') >= 0 or tag.find('no module') >= 0:
                # the junk a class-B unit writes (0x04040404 on the console) is undefined (spec 3.8, 9.8)
                reps.append(('dst ' + m.group(7), 'dst ' + g('x_junk', HEX8)))
            if tag.find('DAR misaligned') >= 0:
                reps.append((m.group(5), g('x_sar', HEX8)))
                func.append('x_sar == x_bufA')
            out.append(sub(l, reps, func=func) if reps else exact(l))
        elif l.startswith('T4 reference'):
            m = re.fullmatch(r'T4 reference: alu_loop\(50000\) (\d+) cyc; burst of 2048 x 32B (\d+) cyc', l)
            reps = [(') ' + m.group(1), r'\) ' + g('alu')), ('32B ' + m.group(2), '32B ' + g('burst'))]
            out.append(sub(l, reps, cls='timing', timing=[['alu', int(m.group(1)), 0], ['burst', int(m.group(2)), 2048]]))
        elif l.startswith('T4 probe'):
            m = re.fullmatch(r'(T4 probe .*): t1\s+(\d+)\s+end\s+(\d+) \(wait (-?\d+)\)', l)
            reps = [('t1 ' + l.split('t1 ')[1].split('end')[0], r't1\s+' + g('t1') + r'\s+'),
                    ('end ' + l.split('end ')[1].split(' (')[0], r'end\s+' + g('end')),
                    ('(wait ' + m.group(4), r'\(wait ' + g('w'))]
            t1, end = int(m.group(2)), int(m.group(3))
            out.append(sub(l, reps, cls='timing', timing=[['t1', t1, max(64, t1 // 50)], ['end', end, max(64, end // 50)]]))
        elif l.startswith('T5 '):
            m = re.fullmatch(r'(T5 .*): 2000 uncached RAM reads (\d+) cyc \(([\d.]+) each\); DMA units in that time (\d+)', l)
            reps = [('reads ' + m.group(2), 'reads ' + g('cyc')), ('(' + m.group(3), r'\(' + g('f_each', r'[\d.]+')),
                    ('time ' + m.group(4), 'time ' + g('units'))]
            units = int(m.group(4))
            out.append(sub(l, reps, cls='timing', func=['units == 0' if units == 0 else 'units > 0'],
                           timing=[['cyc', int(m.group(2)), int(m.group(2)) // 50], ['units', units, units // 50]]))
        else:
            raise SystemExit(f'mkexpected: no rule for dmac2 line {l!r}')
    return out


# (M6) SOC.md 9.25's rows under KOS: timing targets the chip misses by the open question's mechanism become `doc` bounds
# (reported, failing outside the bound); every other timing target stays gated.  (line prefix, token, lo, hi, why)
W25_SHORT = ('SOC.md 9.25, the DMAC outside the steady unit: a short transfer timed from the CPU\'s CHCR write to its '
             'DMATCR poll carries the start latency and the P4 write / read phases (the console 12-58 Icyc apart from '
             'the model) and, under KOS, the timed code\'s I-cache state (9.23): the chip gives 245-293 for S4\'s '
             'forms under KOS against 271-283 in the bare-metal layout; bounded 0.75-1.25 x the console')
W25_STEAL = ('SOC.md 9.25, cycle steal while the CPU polls DMATCR: 52.19-52.39 Icyc per unit on the console, the chip '
             '50.1 (its rate with the CPU idle, which the console meets too: S10 50.0-50.6); bounded 49.5-52.6 per unit')
W25_ONE = ('SOC.md 9.25, one unit from the CHCR write to DMATCR == 0: min 89 on the console, 101 on the chip; bounded '
           '80-125')
W25_T5 = ('SOC.md 9.25, a CPU read loop against cycle steal (hm Table 13.16\'s DMAC-write -> CPU-read and CPU-read -> '
          'DMAC-read pipelining not modelled): 71.0 / 70.1 Icyc per pair on the chip against 66.4 / 64.6; bounded '
          '0.97-1.10 x the console')
W23_COLD = ('SOC.md 9.23, the first call\'s cold lines: alu_loop(50000) is 6 Icyc per iteration on both, its first call '
            'from cold lines 300,142 on the chip under KOS (300,087 in M3\'s bare-metal layout) against 300,140; bounded '
            '+-8')
W25_S9 = ('SOC.md 9.25 / 9.23: S9 (i)\'s "done" is wait_tcr0\'s own time, from its first poll (after the CHCR write and '
          'the call) to DMATCR == 0 for a 4-unit longword burst: on the chip under KOS the call reaches its first poll '
          'late (57; 218 in the bare-metal layout; the console 189); bounded 20-260')
W25_S8 = ('SOC.md 9.25 / 9.23: S8 (a)\'s "cyc" is wait_tcr0\'s own time for a 4-unit 32-byte burst, from its first poll '
          'to DMATCR == 0: the call reaches its first poll after the CHCR write and, when its lines were evicted, an '
          'I-cache fill that the burst holds off [HW dmac2 T4] -- 51-181 on the chip under KOS (the lines evicted or not '
          'by KOS activity before S8), 257 in the bare-metal layout, 209 on the console; bounded 40-262')
W25_S7 = ('SOC.md 9.25: S7 resume, 8,030 cycle-steal units while the CPU polls (the steal row\'s mechanism): bounded '
          '0.95-1.02 x the console')


def _f(lo, hi):
    return lambda c: (int(c * lo), int(c * hi + 0.999))


# (program, line prefix), timing token, bound (a function of the console value), why
KOS_DOC = [(('dmac1', 'S4 addr SAR '), 'cyc', _f(0.75, 1.25), W25_SHORT),
           (('dmac1', 'S5 SM='), 'cyc', _f(0.75, 1.25), W25_SHORT),
           (('dmac1', 'S7 resume'), 'cyc', _f(0.95, 1.02), W25_S7),
           (('dmac1', 'S8 (a)'), 'cyc', lambda c: (40, 262), W25_S8),
           (('dmac1', 'S9 (i)'), 'done', lambda c: (20, 260), W25_S9),
           (('dmac1', 'S11 one unit'), 'mn', lambda c: (80, 125), W25_ONE),
           (('dmac2', 'T4 reference'), 'alu', lambda c: (c - 8, c + 8), W23_COLD),
           (('dmac2', 'T5 cycle steal'), 'cyc', _f(0.97, 1.10), W25_T5)] + \
          [(('dmac1', f'S11 16KB RAM->RAM TS={ts} unit={u:2d} steal'), 'cyc',
            (lambda n: lambda c: (int(49.5 * n), int(52.6 * n + 0.999)))(16384 // u), W25_STEAL)
           for ts, u in ((1, 1), (2, 2), (3, 4), (0, 8), (4, 32))]


def kos_doc(name, rules):
    used = set()
    for r in rules:
        if not isinstance(r.get('console'), str):
            continue
        for (prog, pfx), tok, bound, why in KOS_DOC:
            if prog == name and r['console'].startswith(pfx):
                t = [x for x in r.get('timing', []) if x[0] == tok]
                if not t:
                    raise SystemExit(f'mkexpected: KOS_DOC {pfx!r} {tok}: no such timing target')
                lo, hi = bound(t[0][1])
                r['timing'] = [x for x in r['timing'] if x[0] != tok]
                r.setdefault('doc', []).append([tok, lo, hi, why])
                r.setdefault('doc_console', {})[tok] = t[0][1]
                used.add((prog, pfx))
    for (prog, pfx), *_ in KOS_DOC:
        if prog == name and (prog, pfx) not in used:
            raise SystemExit(f'mkexpected: KOS_DOC entry {pfx!r} matches no {name} line')
    return rules


def main():
    os.makedirs(os.path.join(HERE, 'expected'), exist_ok=True)
    kos = '--kos' in sys.argv                            # (M6) expected/<prog>_kos.json: every console line
    for name, fn in (('dmac1', dmac1), ('dmac2', dmac2)):
        rules = kos_doc(name, fn(kos=True)) if kos else fn()
        doc = {'program': name, 'source': f'tools/hw/dmac/{name}.elf.hwlog', 'first': f'{name}: bufA',
               'last': f'{name}: done' if name == 'dmac1' else f'{name}: end', 'lines': rules}
        if kos:
            doc['kos'] = True
        with open(os.path.join(HERE, 'expected', name + ('_kos' if kos else '') + '.json'), 'w') as f:
            json.dump(doc, f, indent=1)
            f.write('\n')
        cnt = {}
        for r in rules:
            cnt[r['cls']] = cnt.get(r['cls'], 0) + 1
        print(f'{name}{"_kos" if kos else ""}: {len(rules)} rules', ', '.join(f'{k} {v}' for k, v in sorted(cnt.items())))


if __name__ == '__main__':
    main()
