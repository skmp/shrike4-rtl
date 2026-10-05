#!/usr/bin/env python3
"""check.py -- tb/bm/soc-adv/m6 (M6, WI-SOC; rtl/v1/SOC.md 8.3 M6): how the M6 runs (board phase 2, soc_tb --ddt) must
end.

usage: check.py --progs PROG LOG --status STATUS [--self]      PROG: ddtx ddtmix ddtrst ddtrst_c1 ddtae_c0..3
       check.py --kos PROG LOG --status STATUS [--self]
       check.py --ddtprobe LOG [--console CONSOLE] [--self]
       check.py --hello NAME --status STATUS --scif SCIF [--self]
  --progs ddtx   tb/bm/soc-adv/m6/ddtx (Holly's DMAs over DDT without KOS; the rules in its source's header): soc_tb PASS,
                 lockstep pass, no monitor violation, every row of DDTX_ROWS once and OK, "DX done fails=0", and the run's
                 "soc_tb: DDT:" line: exactly DDTX_DTRS channel-0 DTRs (P 128 + Q 32 + A 32 + B 32 + M 3 + W 2 + X 128 + N 32;
                 DDTX_RD read / DDTX_WR written units, Y's 32 among them), DDTX_CH2 channel-2 requests (C 64 + R 32 + S 2048), TDACK# with every
                 channel-0 read beat, no time-out, no stray beat
  (M6-fix: every --ddt run) the "soc_tb: DDT TDACK#:" line: TDACK# + ID 2 on every channel-2 read beat, every
                 channel-0 write beat's TDACK# at its data pull (3..7 Bclk before it, a unit's beats at one distance:
                 tb/dc_board/dc_ddt.h), no pull without a beat, and no BAVL# low without a grant (DDT = 0's pin levels)
                 unless the program makes them (ddtrst, ddtae_c1)
  --progs ddtmix / ddtrst / ddtrst_c1 / ddtae_c<n>   (M6-fix) the M6 verification's programs (their sources' heads):
                 ddtmix every row of DDTMIX_ROWS once and OK ("MX"), no time-out -- rows G / H: no CPU read completes
                 inside a channel-1 burst that Holly / CH2 units preempt (M6-verify F1); ddtrst (a WDT manual reset
                 during a PVR-DMA, soc_tb --resets 1; M6-verify F2): the run ends PASS (no DDT protocol error), BAVL#
                 low without a grant seen (the DDT = 0 window), exactly one unit given up and the first DMA's texture
                 1016 of 1024 words, "RX done fails=0"; ddtrst_c1 (the control): no time-out, 1024 words; ddtae_c0 (AE)
                 and c1 (DDT off): exactly one time-out and 1016 words (SOC.md 9.30 (c)), c2 / c3: none and 1024
  --kos PROG     a console probe unmodified (tools/hw/<area>/PROG.elf) behind the dcload stand-in with the phase-2 board
                 (soc_tb --dcload --ddt): m5/check.py --kos's rules (soc_tb PASS, lockstep pass, no monitor violation,
                 KOS's dcload banner / console / exit) and the DDT line: at least one DTR (KOS's maple runs through
                 channel 0), TDACK# with every read beat, no time-out, no stray beat.  The probe's own values are
                 tb/bm/dmac/check.py --kos's and tb/bm/board/check.py's
  --ddtprobe     tools/hw/board/ddtprobe.elf (SOC.md 9.11's probe: channel 0 sampled while Holly's G2-DMA both ways and
                 PVR-DMA run, 4 KB each) unmodified on the chip with the phase-2 board, against the console's log
                 (CONSOLE, default tools/hw/board/ddtprobe.out), line by line (check_ddtprobe):
                   * exact: the buffers, "data mismatches 0", PVR-DMA's progress registers, the end of each DMA's
                     moving register (SAR0 / DAR0 = buffer + 4 KB), CHCR0 after each DMA;
                   * per DMA: the end time within 1 % of the console's (Holly's paces, board.md 4.3), SAR0 / DAR0 moves
                     120-128 for the moving register (127 or 128 steps of 0x20; the console's g2in sampling missed some:
                     95 -- its lower bound), 0 for the other, DMATCR0 at most 1, CHCR0 only a DTR's value for the
                     direction (0x852C1 / 0x852C3 or 0x853C1 / 0x853C3) besides the value before the DMA;
                   * every sample line: the moving register 32-byte aligned inside its buffer (or the value before the
                     first unit), the other register constant, DMATCR0 0 / 1, times increasing;
                   * board tokens: the channel-0 registers at entry (the console's dcload / BIOS maple leftovers; the
                     direct boot's 0 and the stub's CHCR0 0x853C1: TE cannot be written 1), ISTNRM's SPG bits 3-5
                     (vblank-in / out, hblank: the SPG's phase against the program);
                   * GD: the board has no disc (board.md 3.7.3, B5): "gd status rv 0 status 7 disc 0" and "gd toc 2" in
                     place of the console's CD-R read (its GD-DMA lines are not the board's), and the exit line with
                     the PVR-DMA's registers
  --hello NAME   a tools/boot/hello target booted with the phase-2 board (soc_tb --ddt): m5/check.py --hello's rules (the
                 golden transcript, lockstep, level 1's KOS init time and vblank count) and the DDT line with at least
                 one DTR (level 2's maple device answers through channel 0)
  --self         every rule must reject a run changed to violate it
Exit 0 when every rule holds.
"""
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'm5'))
import check as m5  # noqa: E402  (m5/check.py: run_ok, check_kos)

DDTX_ROWS = ('P pvr-dma ram->tex 4k', 'Q pvr-dma tex->ram 1k', 'A g2-dma ram->aica 1k', 'B g2-dma aica->ram 1k',
             'M maple getcond A0 devinfo B0', 'C ch2 tex 64 units', 'R ch2 dmatcr2 64 c2dlen 32',
             'S ch2 64k with the cpu polling',
             'W cpu writes ch0 then a dtr', 'X ch0 units with ch1 and the cpu', 'Y ch0 writes against ch1 word units',
             'N ch0 units during a ch1 burst',
             'Z dmaor unchanged')
DDTX_RD, DDTX_WR, DDTX_CH2 = 128 + 32 + 1 + 2 + 128 + 32, 32 + 32 + 2 + 32, 64 + 32 + 2048
DDTX_DTRS = DDTX_RD + DDTX_WR
DDT = re.compile(r'^soc_tb: DDT: (\d+) DTRs, (\d+) units read / (\d+) written, (\d+) channel-2 requests, TDACK# on (\d+) '
                 r'of (\d+) read beats, (\d+) time-outs, (\d+) stray beats', re.M)
# (M6-fix) TDACK# on channel-2 read beats and channel-0 write beats (at their pulls), pulls without a beat, BAVL# low
# without a grant (Bclk) and falls that were no one-Bclk grant (tb/dc_board/dc_ddt.h, tb/soc_tb.cpp)
TDK = re.compile(r'^soc_tb: DDT TDACK#: channel 2 read beats (\d+) of (\d+), channel 0 write beats (\d+) of (\d+) \(the '
                 r'pull (\d+)\.\.(\d+) Bclk before the beat\), (\d+) pulls without a beat; BAVL# low without a grant (\d+) '
                 r'Bclk, not a one-Bclk grant (\d+)', re.M)
DDTMIX_ROWS = ('A all engines ch1 and the cpu at once', 'B ch0 writes against ch2 reads swept',
               'C g2 read units with a maple response', 'E sleep through pvr-dma and maple', 'F ch2 64k against tas.b',
               'G ch1 burst with ch0 units shuts the cpu out', 'H ch1 burst with ch2 units shuts the cpu out',
               'Z dmaor unchanged')


def tdack_line(st, why, bavl_low=False):
    """(M6-fix) the run's TDACK# / BAVL# line: channel-2 read beats and channel-0 write beats all with their TDACK#, no
    pull without a beat; BAVL# low without a grant only when the program makes DDT = 0's pin levels (bavl_low: required
    then, the program's window was exercised)"""
    m = TDK.search(st)
    if not m:
        why.append('no "soc_tb: DDT TDACK#:" line')
        return
    r2, r2a, w0, w0a, kmin, kmax, orph, held, false = (int(x) for x in m.groups())
    if r2 != r2a:
        why.append(f'TDACK# / ID 2 missing on {r2a - r2} of {r2a} channel-2 read beats')
    if w0 != w0a:
        why.append(f'{w0a - w0} of {w0a} channel-0 write beats without their TDACK# pull 3..7 Bclk before (seen {kmin}..{kmax})')
    if orph:
        why.append(f'{orph} TDACK# pulls without a write beat')
    if bavl_low and not held + false:
        why.append('no BAVL# low without a grant: the DDT = 0 window was not exercised')
    if not bavl_low and held + false:
        why.append(f'BAVL# low without a grant for {held} Bclk, {false} falls that were no grant (DDT = 0 pin levels)')


def ddt_line(st, why, exact=None, max_tmo=0, bavl_low=False, dtr_tmo=0):
    """the run's DDT summary: TDACK# with every read beat, no time-out (max_tmo: the program drops that many units), no
    stray beat, a DTR per unit served (dtr_tmo more: the units given up after their DTR); exact = (dtrs, rd, wr, ch2);
    and (M6-fix) the TDACK# line"""
    tdack_line(st, why, bavl_low)
    m = DDT.search(st)
    if not m:
        why.append('no "soc_tb: DDT:" line (not a --ddt run?)')
        return
    dtrs, rd, wr, ch2, tok, tall, tmo, stray = (int(x) for x in m.groups())
    if tok != tall:
        why.append(f'TDACK# / ID 00 missing on {tall - tok} of {tall} channel-0 read beats')
    if tmo > max_tmo:
        why.append(f'{tmo} DDT units Holly gave up (never served; allowed {max_tmo})')
    if stray:
        why.append(f'{stray} stray channel-0 DACK beats')
    if dtrs != rd + wr + dtr_tmo:
        why.append(f'{dtrs} DTRs but {rd + wr} units served' + (f' and {dtr_tmo} given up after their DTR' if dtr_tmo else ''))
    if exact is not None:
        if (dtrs, rd, wr, ch2) != exact:
            why.append(f'DDT counts {dtrs} DTRs / {rd} read / {wr} written / {ch2} channel-2, want {exact[0]} / '
                       f'{exact[1]} / {exact[2]} / {exact[3]}')
    elif dtrs == 0:
        why.append('no DTR at all (KOS maple did not run through channel 0)')


def check_ddtx(log, st):
    why = []
    m5.run_ok(st, why)
    for r in DDTX_ROWS:
        rows = [ln for ln in log.splitlines() if ln.startswith('DX ' + r + ' ')]
        if len(rows) != 1:
            why.append(f'row "{r}": {len(rows)} lines, want 1')
        elif not rows[0].endswith(' OK'):
            why.append(f'row FAIL: {rows[0]}')
    other = [ln for ln in log.splitlines() if ln.startswith('DX ') and not ln.startswith('DX done')
             and not any(ln.startswith('DX ' + r + ' ') for r in DDTX_ROWS)]
    if other:
        why.append(f'unexpected lines: {other[:2]}')
    if not re.search(r'^DX done fails=0$', log, re.M):
        why.append('no "DX done fails=0"')
    ddt_line(st, why, (DDTX_DTRS, DDTX_RD, DDTX_WR, DDTX_CH2))
    return why


def run_ok_reset(st, why):
    """(M6-fix) a --resets run: soc_tb PASS, lockstep ended by the reset ("reset") or pass, no monitor violation, one
    re-boot through the stub"""
    if not re.search(r'^soc_tb: PASS$', st, re.M):
        why.append('soc_tb did not PASS: ' + ((re.search(r'^soc_tb: [^\n]*(exit code|DDT: |reached)[^\n]*', st, re.M) or
                                                re.search(r'^.*$', '?', re.M)).group(0)[:160]))
    m = re.search(r'lockstep (\w+)', st)
    if not m or m.group(1) not in ('pass', 'reset'):
        why.append(f'lockstep verdict {m.group(1) if m else "missing"}, want pass or reset')
    m = m5.MON.search(st)
    if not m or any(int(x) for x in m.groups()):
        why.append('monitor violations: ' + (m.group(0).split('violations ')[1] if m else 'no monitor line'))


def check_prog(prog, log, st):
    """(M6-fix) the M6 verification's programs, ported to tb/bm/soc-adv/m6 (see the head)"""
    why = []
    if prog == 'ddtmix':
        m5.run_ok(st, why)
        ddt_line(st, why)
        for r in DDTMIX_ROWS:
            rows = [ln for ln in log.splitlines() if ln.startswith('MX ' + r + ' ')]
            if len(rows) != 1:
                why.append(f'row "{r}": {len(rows)} lines, want 1')
            elif not rows[0].endswith(' OK'):
                why.append(f'row FAIL: {rows[0][:220]}')
        if not re.search(r'^MX done fails=0$', log, re.M):
            why.append('no "MX done fails=0"')
        return why
    m = DDT.search(st)
    tmo = int(m.group(7)) if m else -1
    if prog in ('ddtrst', 'ddtrst_c1'):
        run_ok_reset(st, why)
        want = 1 if prog == 'ddtrst' else 0
        ddt_line(st, why, max_tmo=want, bavl_low=prog == 'ddtrst')
        if tmo != want:
            why.append(f'{tmo} DDT time-outs, want {want} (the unit whose DBREQ# met DDT = 0\'s pin levels)'
                       if want else f'{tmo} DDT time-outs, want 0')
        if not re.search(r'^RX done fails=0$', log, re.M):
            why.append('no "RX done fails=0"')
        if not re.search(r'^RX boot=2 EXPEVT=020 ', log, re.M):
            why.append('no manual-reset re-boot (RX boot=2 EXPEVT=020)')
        g = re.search(r'^RX boot=2 first dma: PDST=0 after \d+ Icyc, texture words right (\d+) of 1024', log, re.M)
        if not g or int(g.group(1)) != 1024 - 8 * max(tmo, 0):
            why.append(f'first DMA: {g.group(1) if g else "no"} texture words right, want {1024 - 8 * max(tmo, 0)} '
                       f'(8 per unit given up)')
        return why
    if prog.startswith('ddtae_c') and prog[-1] in '0123':
        c = int(prog[-1])
        m5.run_ok(st, why)
        want = 1 if c in (0, 1) else 0
        ddt_line(st, why, max_tmo=want, bavl_low=c == 1, dtr_tmo=1 if c == 0 else 0)
        if tmo != want:
            why.append(f'{tmo} DDT time-outs, want {want}' + (' (the DTR taken while AE is set is dropped, spec 3.6)'
                                                             if c == 0 else ' (the DBREQ# that met DDT = 0)' if c == 1 else ''))
        if not re.search(r'^AE done fails=0$', log, re.M):
            why.append('no "AE done fails=0"')
        if not re.search(rf'words right {1024 - 8 * want} of 1024', log):
            why.append(f'not {1024 - 8 * want} of 1024 texture words right')
        return why
    return [f'unknown program {prog}']


# ---- ddtprobe (SOC.md 9.11) -------------------------------------------------------------------------------------
REGS = re.compile(r'SAR0 ([0-9a-f]{8}) DAR0 ([0-9a-f]{8}) DMATCR0 ([0-9a-f]{8}) CHCR0 ([0-9a-f]{8})')
SPG_BITS = 0x38                                        # ISTNRM 3-5: vblank-in / out, hblank (the SPG's phase)
DTR_CHCR = {0: (0x852c1, 0x852c3), 1: (0x853c1, 0x853c3)}


def dp_lines(text):
    ls = [ln.rstrip() for ln in text.splitlines()]
    try:
        i = next(k for k, ln in enumerate(ls) if ln.startswith('B ddt entry'))
        j = next(k for k in range(i, len(ls)) if ls[k] == 'B done')
    except StopIteration:
        return None
    return ls[i:j + 1]


def dp_sections(lines):
    """{name: {'head': [...], 'samples': [(t, sar, dar, tcr, chcr)], 'end': line}} for g2out / g2in / pvr"""
    sec = {}
    for ln in lines:
        m = re.match(r'B ddt (g2out|g2in|pvr) t=\s*(\d+) ' + REGS.pattern, ln)
        if m:
            sec.setdefault(m.group(1), {'samples': []})['samples'].append(
                (int(m.group(2)),) + tuple(int(m.group(k), 16) for k in range(3, 7)))
            continue
        m = re.match(r'B ddt (g2out|g2in|pvr) end (\d+) samples (\d+) SAR0-moves (\d+) DAR0-moves (\d+) DMATCR0-max (\d+) '
                     r'\(nonzero in (\d+) samples\) CHCR0 ([0-9a-f ]+?) first-move (\d+) last-move (\d+)$', ln)
        if m:
            d = sec.setdefault(m.group(1), {'samples': []})
            d['end'] = dict(t=int(m.group(2)), n=int(m.group(3)), sm=int(m.group(4)), dm=int(m.group(5)),
                            tmax=int(m.group(6)), chcr=[int(x, 16) for x in m.group(8).split()], last=int(m.group(10)))
    return sec


def check_ddtprobe(log, console):
    why = []
    L, C = dp_lines(log), dp_lines(console)
    if L is None:
        return ['no ddtprobe output ("B ddt entry" .. "B done")']
    gv = lambda pat, ls: next((re.search(pat, ln) for ln in ls if re.search(pat, ln)), None)
    mb = gv(r'^B ddt bufA ([0-9a-f]{8}) bufB ([0-9a-f]{8})$', L)
    cb = gv(r'^B ddt bufA ([0-9a-f]{8}) bufB ([0-9a-f]{8})$', C)
    if not mb or mb.group(0) != cb.group(0):
        return [f'buffers {mb.group(0) if mb else "missing"}, console {cb.group(0)}']
    bufA, bufB = int(mb.group(1), 16), int(mb.group(2), 16)
    # entry / g2out-before: board tokens only
    for tag in ('entry', 'g2out-before'):
        m = gv(rf'^B ddt {tag} ' + REGS.pattern + r' DMAOR ([0-9a-f]{8}) ISTNRM ([0-9a-f]{8}) ISTERR ([0-9a-f]{8})$', L)
        if not m:
            why.append(f'no "{tag}" line')
            continue
        tcr, ch, dmaor, ist, ister = int(m.group(3), 16), int(m.group(4), 16), m.group(5), int(m.group(6), 16), m.group(7)
        if tcr or ch not in (0x853c1, 0x853c3) or dmaor != '00008201' or ist & ~SPG_BITS or ister != '00000000':
            why.append(f'"{tag}": {m.group(0)} (want DMATCR0 0, CHCR0 853c1 / 853c3, DMAOR 8201, no ISTNRM / ISTERR)')
    # the three DMAs: rv / ISTNRM line, end summary, samples
    sec, csec = dp_sections(L), dp_sections(C)
    spec = {'g2out': (0, bufA, 15), 'g2in': (1, bufB, 15), 'pvr': (0, bufA, 11)}   # R/W, buffer, ISTNRM end bit
    for name, (rw, buf, bit) in spec.items():
        d, c = sec.get(name), csec.get(name)
        if not d or 'end' not in d:
            why.append(f'{name}: no end line')
            continue
        e, ce = d['end'], c['end']
        if abs(e['t'] - ce['t']) > 0.01 * ce['t']:
            why.append(f'{name}: end {e["t"]} Icyc, console {ce["t"]} (1 %, Holly\'s pace)')
        mv, other = (e['sm'], e['dm']) if rw == 0 else (e['dm'], e['sm'])
        if not (120 if name != 'g2in' else 95) <= mv <= 128 or other != 0:
            why.append(f'{name}: moves {e["sm"]} SAR0 / {e["dm"]} DAR0 (want 120-128 of the moving register, 0 of the other)')
        if e['tmax'] > 1:
            why.append(f'{name}: DMATCR0 reached {e["tmax"]} (COUNT 1)')
        pre = d['samples'][0][4] if d['samples'] else None
        bad = [x for x in e['chcr'] if x not in DTR_CHCR[rw] and x != pre]
        if bad:
            why.append(f'{name}: CHCR0 values {[hex(x) for x in bad]} are no {"R/W 1" if rw else "R/W 0"} DTR state')
        sm = d['samples']
        if len(sm) < 24:                                 # (the probe prints its first 24 and last 8 samples)
            why.append(f'{name}: {len(sm)} samples')
        fixed = sm[0][2 if rw == 0 else 1] if sm else 0
        moved = False                                  # the value before the DMA is allowed until the first unit
        for i, (t, sa, da, tc, ch) in enumerate(sm):
            mvr, oth = (sa, da) if rw == 0 else (da, sa)
            if i and mvr != (sm[i - 1][1] if rw == 0 else sm[i - 1][2]):
                moved = True
            inbuf = buf <= mvr <= buf + 4096 and not mvr & 31
            if (not inbuf and i != 0) or oth != fixed or tc > 1 or (i and t <= sm[i - 1][0]) or \
                    (ch not in DTR_CHCR[rw] and not (not moved and ch == pre)):
                why.append(f'{name} sample {i}: t={t} SAR0 {sa:08x} DAR0 {da:08x} DMATCR0 {tc} CHCR0 {ch:08x}')
                break
        last = sm[-1] if sm else None
        if not last or (last[1] if rw == 0 else last[2]) != buf + 4096 or last[4] != DTR_CHCR[rw][1]:
            why.append(f'{name}: the last sample {last} is not the end (moving register = buffer + 4 KB, CHCR0 TE)')
        mr = gv(rf'^B ddt {name}(?: rv 0)? ISTNRM ([0-9a-f]{{8}}) ISTERR 00000000', L)
        if not mr or (int(mr.group(1), 16) & ~SPG_BITS) != (1 << bit):
            why.append(f'{name}: ISTNRM at the end {mr.group(1) if mr else "missing"} (want bit {bit}, SPG bits aside)')
    for ln in ('B ddt g2in data mismatches 0', 'B ddt pvr data mismatches 0'):
        if ln not in L:
            why.append(f'missing "{ln}"')
    mp = gv(r'^B ddt pvr ISTNRM [0-9a-f]{8} ISTERR 00000000 (PDSTAPD .*)$', L)
    cp = gv(r'^B ddt pvr ISTNRM [0-9a-f]{8} ISTERR 00000000 (PDSTAPD .*)$', C)
    if not mp or mp.group(1) != cp.group(1):
        why.append(f'PVR-DMA progress registers {mp.group(1) if mp else "missing"}, console {cp.group(1)}')
    for tag, want in (('g2in-before', (bufA + 4096, 0x852c3)), ('pvr-before', (bufA + 4096, 0x853c3))):
        m = gv(rf'^B ddt {tag} ' + REGS.pattern, L)
        if not m or int(m.group(1), 16) != want[0] or int(m.group(4), 16) != want[1]:
            why.append(f'"{tag}": {m.group(0) if m else "missing"} (SAR0 {want[0]:08x}, CHCR0 {want[1]:08x})')
    # GD: the board has no disc
    for ln in ('B ddt gd status rv 0 status 7 disc 0', 'B ddt gd toc 2'):
        if ln not in L:
            why.append(f'missing "{ln}" (the no-disc board, board.md 3.7.3)')
    m = gv(r'^B ddt exit ' + REGS.pattern + r' DMAOR 00008201 ISTNRM ([0-9a-f]{8}) ISTERR 00000000$', L)
    if not m or (int(m.group(1), 16), int(m.group(2), 16), int(m.group(3), 16), int(m.group(4), 16)) != \
            (bufA + 4096, bufB + 4096, 0, 0x852c3) or int(m.group(5), 16) & ~SPG_BITS:
        why.append(f'exit line {m.group(0) if m else "missing"} (want the PVR-DMA\'s end registers)')
    return why


def check_kos(prog, log, st):
    why = m5.check_kos(prog, log, st)
    ddt_line(st, why)
    return why


def check_hello(name, st, scif):
    why = m5.check_hello(name, st, scif)
    ddt_line(st, why)
    return why


def selftest(kind, prog, log, st):
    """every rule rejects a run changed to violate it (hello: log is the SCIF transcript)"""
    f = (lambda lg, s: check_ddtx(lg, s)) if kind == 'ddtx' else \
        (lambda lg, s: check_prog(prog, lg, s)) if kind == 'prog' else \
        (lambda lg, s: check_hello(prog, s, lg)) if kind == 'hello' else (lambda lg, s: check_kos(prog, lg, s))
    base = f(log, st)
    if base:
        return [f'the run itself fails: {base[0]}']
    lk = 'lockstep reset' if prog in ('ddtrst', 'ddtrst_c1') else 'lockstep pass'
    muts = [('soc_tb FAIL', log, st.replace('soc_tb: PASS', 'soc_tb: FAIL')),
            ('lockstep fail', log, st.replace(lk, 'lockstep fail', 1)),
            # (M6-fix) the TDACK# line
            ('no TDACK# line', log, TDK.sub('soc_tb: DDT TDACKX:', st)),
            ('a channel-2 read beat without TDACK#', log,
             re.sub(r'channel 2 read beats (\d+) of (\d+)', lambda m: f'channel 2 read beats {m.group(1)} of {int(m.group(2)) + 1}', st)),
            ('a channel-0 write beat without its pull', log,
             re.sub(r'channel 0 write beats (\d+) of (\d+)', lambda m: f'channel 0 write beats {m.group(1)} of {int(m.group(2)) + 1}', st)),
            ('a pull without a beat', log, re.sub(r' (\d+) pulls without a beat', lambda m: f' {int(m.group(1)) + 1} pulls without a beat', st)),
            ('a monitor violation', log, re.sub(r'(violations MA )\d+', r'\g<1>1', st)),
            ('no DDT line', log, DDT.sub('soc_tb: DDX:', st)),
            ('TDACK# missing on a read beat', log, re.sub(r'TDACK# on (\d+) of', lambda m: f'TDACK# on {int(m.group(1)) - 1} of', st)),
            ('a time-out', log, re.sub(r' (\d+) time-outs', lambda m: f' {int(m.group(1)) + 1} time-outs', st)),
            ('a stray beat', log, re.sub(r' 0 stray beats', ' 3 stray beats', st)),
            ('DTRs != units', log, re.sub(r'DDT: (\d+) DTRs', lambda m: f'DDT: {int(m.group(1)) + 1} DTRs', st))]
    if prog not in ('ddtrst', 'ddtae_c1'):
        muts.append(('BAVL# low without a grant', log, re.sub(r'low without a grant (\d+) Bclk', 'low without a grant 7 Bclk', st)))
    else:
        muts.append(('the DDT = 0 window not exercised', log,
                     re.sub(r'low without a grant (\d+) Bclk, not a one-Bclk grant (\d+)', 'low without a grant 0 Bclk, '
                            'not a one-Bclk grant 0', st)))
    if kind == 'prog':
        tag = {'ddtmix': 'MX', 'ddtrst': 'RX', 'ddtrst_c1': 'RX'}.get(prog, 'AE')
        muts.append(('fails=1', log.replace(f'{tag} done fails=0', f'{tag} done fails=1'), st))
        if prog in ('ddtrst', 'ddtae_c0', 'ddtae_c1'):
            muts.append(('no time-out', log, re.sub(r' (\d+) time-outs', ' 0 time-outs', st)))
        if prog == 'ddtmix':
            for ln in log.splitlines():
                if ln.startswith('MX ') and ln.endswith(' OK'):
                    muts.append((f'row {ln[3]} FAIL', log.replace(ln, ln[:-3] + ' FAIL'), st))
        elif prog.startswith('ddtrst'):
            muts.append(('no re-boot', log.replace('RX boot=2 EXPEVT=020', 'RX boot=2 EXPEVT=000'), st))
            muts.append(('a texture word short', re.sub(r'texture words right (\d+) of 1024',
                                                        lambda m: f'texture words right {int(m.group(1)) - 1} of 1024', log), st))
        else:
            muts.append(('a texture word short', re.sub(r'words right (\d+) of 1024',
                                                        lambda m: f'words right {int(m.group(1)) - 1} of 1024', log), st))
    if kind == 'ddtx':
        rows = [ln for ln in log.splitlines() if ln.startswith('DX ') and ln.endswith(' OK')]
        for r in rows:
            muts.append((f'row FAIL {r[:24]}', log.replace(r, r[:-3] + ' FAIL'), st))
            muts.append((f'row missing {r[:24]}', log.replace(r + '\n', ''), st))
        muts.append(('fails=1', log.replace('DX done fails=0', 'DX done fails=1'), st))
        muts.append(('a channel-2 request short', log,
                     re.sub(r'written, (\d+) channel-2', lambda m: f'written, {int(m.group(1)) - 1} channel-2', st)))
        muts.append(('a unit short', log, re.sub(r'(\d+) units read', lambda m: f'{int(m.group(1)) - 1} units read', st)))
    elif kind == 'hello':
        muts.append(('no DTR', log, re.sub(r'DDT: \d+ DTRs, \d+ units read / \d+ written', 'DDT: 0 DTRs, 0 units read / 0 written', st)))
        muts.append(('no golden', log, st.replace(f'golden {prog} ok', f'golden {prog} bad')))
    elif kind == 'kos':
        muts.append(('no DTR', log, re.sub(r'DDT: \d+ DTRs, \d+ units read / \d+ written', 'DDT: 0 DTRs, 0 units read / 0 written', st)))
        muts.append(('no exit line', log.replace('arch: exit return code 0', 'arch: exit return code 1'), st))
    out = []
    for name, lg, s in muts:
        if not f(lg, s):
            out.append(f'not rejected: {name}')
    return out + [f'{len(muts)} perturbations, {len(muts) - len(out)} rejected']


def selftest_ddtprobe(log, console):
    base = check_ddtprobe(log, console)
    if base:
        return [f'the run itself fails: {base[0]}']
    muts = [('a data mismatch', log.replace('B ddt pvr data mismatches 0', 'B ddt pvr data mismatches 3')),
            ('a DMATCR0 of 2', re.sub(r'(B ddt g2out t=\s*\d+ SAR0 \w+ DAR0 \w+ DMATCR0 )00000000', r'\g<1>00000002', log, count=1)),
            ('an R/W 1 CHCR0 in g2out', re.sub(r'(B ddt g2out t=\s*\d+ SAR0 \w+ DAR0 \w+ DMATCR0 \w+ CHCR0 )000852c3', r'\g<1>000853c3',
                                                log, count=3)),
            ('SAR0 outside the buffer', re.sub(r'(B ddt pvr t=\s*\d+ SAR0 )0c033e', r'\g<1>0c043e', log, count=1)),
            ('DAR0 moving in pvr', re.sub(r'(B ddt pvr t=\s*\d+ SAR0 \w+ DAR0 )0c033dc0', r'\g<1>0c033de0', log, count=1)),
            ('g2out 3 % slow', re.sub(r'B ddt g2out end (\d+)', lambda m: f'B ddt g2out end {int(int(m.group(1)) * 1.03)}', log)),
            ('pvr 60 moves', re.sub(r'(B ddt pvr end \d+ samples \d+ SAR0-moves )\d+', r'\g<1>60', log)),
            ('another ISTNRM bit', re.sub(r'B ddt pvr ISTNRM ([0-9a-f]{8})', 'B ddt pvr ISTNRM 00001830', log)),
            ('PDSTARD', log.replace('PDSTARD 0c034dc0', 'PDSTARD 0c034de0')),
            ('the disc status', log.replace('status 7 disc 0', 'status 2 disc 0')),
            ('the exit registers', re.sub(r'(B ddt exit SAR0 )0c034dc0', r'\g<1>0c034da0', log)),
            ('an entry CHCR0', re.sub(r'(B ddt entry SAR0 \w+ DAR0 \w+ DMATCR0 \w+ CHCR0 )\w+', r'\g<1>000852c3', log)),
            ('the buffers', log.replace('bufA 0c033dc0', 'bufA 0c033de0'))]
    out = [f'not rejected: {nm}' for nm, lg in muts if not check_ddtprobe(lg, console)]
    return out + [f'{len(muts)} perturbations, {len(muts) - len(out)} rejected']


def main(argv):
    args, i, kind, prog, status, self_, scif = [], 1, None, None, None, False, None
    console = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..', 'tools', 'hw', 'board',
                           'ddtprobe.out')
    while i < len(argv):
        a = argv[i]
        if a == '--progs':
            prog = argv[i + 1]; kind = 'ddtx' if prog == 'ddtx' else 'prog'; i += 2
        elif a == '--kos':
            kind = 'kos'; prog = argv[i + 1]; i += 2
        elif a == '--status':
            status = argv[i + 1]; i += 2
        elif a == '--ddtprobe':
            kind = 'ddtprobe'; i += 1
        elif a == '--hello':
            kind = 'hello'; prog = argv[i + 1]; i += 2
        elif a == '--scif':
            scif = argv[i + 1]; i += 2
        elif a == '--console':
            console = argv[i + 1]; i += 2
        elif a == '--self':
            self_ = True; i += 1
        else:
            args.append(a); i += 1
    if kind == 'ddtprobe' and args:
        log, con = open(args[0], errors='replace').read(), open(console, errors='replace').read()
        if self_:
            res = selftest_ddtprobe(log, con)
            bad = [x for x in res if x.startswith('not rejected') or x.startswith('the run itself')]
            for x in res:
                print(f'm6 check.py --self ddtprobe: {x}')
            print(f'm6 check.py --self ddtprobe: {"FAIL" if bad else "PASS"}')
            return 1 if bad else 0
        why = check_ddtprobe(log, con)
        for w in why:
            print(f'  {w}')
        print(f'm6 check.py --ddtprobe: {"FAIL" if why else "PASS"}')
        return 1 if why else 0
    if kind == 'hello' and status and scif:
        st, sc = open(status, errors='replace').read(), open(scif, errors='replace').read()
        if self_:
            res = selftest('hello', prog, sc, st)
            bad = [x for x in res if x.startswith('not rejected') or x.startswith('the run itself')]
            for x in res:
                print(f'm6 check.py --self {prog}: {x}')
            print(f'm6 check.py --self {prog}: {"FAIL" if bad else "PASS"}')
            return 1 if bad else 0
        why = check_hello(prog, st, sc)
        for w in why:
            print(f'  {w}')
        print(f'm6 check.py --hello {prog}: {"FAIL" if why else "PASS"}')
        return 1 if why else 0
    if kind is None or not args or status is None:
        print(__doc__)
        return 2
    log = open(args[0], errors='replace').read()
    st = open(status, errors='replace').read()
    name = prog
    if self_:
        res = selftest(kind, prog, log, st)
        bad = [x for x in res if x.startswith('not rejected') or x.startswith('the run itself')]
        for x in res:
            print(f'm6 check.py --self {name}: {x}')
        print(f'm6 check.py --self {name}: {"FAIL" if bad else "PASS"}')
        return 1 if bad else 0
    why = check_ddtx(log, st) if kind == 'ddtx' else check_prog(prog, log, st) if kind == 'prog' else \
        check_kos(prog, log, st)
    for w in why:
        print(f'  {w}')
    print(f'm6 check.py {"--kos " if kind == "kos" else ""}{name}: {"FAIL" if why else "PASS"}')
    return 1 if why else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
