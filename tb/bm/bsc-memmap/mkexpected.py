#!/usr/bin/env python3
"""mkexpected.py -- expected-value JSON of the bsc-memmap bare-metal ports, taken from the console logs (WI-BSC).

  python3 mkexpected.py      writes expected/{bscmm,bscmm2}.json from tools/hw/bsc-memmap/{bscmm,bscmm2}.elf.hwlog

Every line the program printed (from its first line to "DONE") becomes one entry with a class:
  functional    a property of the chip in the direct-boot state (board.md 5.3 / SOC.md 7.3) that any correct
                implementation reproduces: register values the boot stub writes, the size rule, the write keys, holes,
                PVR / CCR / QACR, the WCR3 / PCR write sizes
  timing        an exact console number of the chip with its SDRAM (the M rows of RAM, the P4 register classes, the
                operand cache and the store queues): the cycle-accurate v1_soc's target (SOC.md M3)
  timing-board  an exact console number that also needs the board model's Holly timing (RDY# waits, write FIFO,
                video contention: SB / PVR / G1 / G2 / texture memory rows)
  board         a value of the Dreamcast board (Holly registers, texture-memory mirrors, the port pins)
  env           depends on the console's run (the allocator's addresses, the refresh counters, polling jitter, the
                refresh-cost histograms): not compared
Tokens (whitespace-separated, 0-based) in "ignore" are not compared (a counter, a pin); tokens in "var" are compared
but a difference is reported, not failed (a value the console kernels measured with an instruction-cache conflict).
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.join(HERE, '..', '..', '..', 'tools', 'hw', 'bsc-memmap')

BOARD_M = ('sb_', 'pvr_', 'bootrom', 'flash', 'aica_', 'vram', 'ram/vram', 'ram/sbrev', 'oc_vram_miss', 'sq_vram',
           'ram>sb', 'sb>ram', 'vram>ram', 'ram>vram', 'sb/vram')


def classify(prog, line):
    """(class, why, key length, ignore indices, var indices)"""
    t = line.split()
    tag = t[0]
    if line == 'DONE':
        return 'functional', 'program end', 1, [], []
    if tag == 'R':
        if t[1] == 'BCR1':
            return 'functional', 'the BSC state the boot stub writes (board.md 5.3)', 2, [], []
        if t[1] == 'RTCSR':
            # RTCSR: CMF / OVF set by the timer since the boot (OVF after 1024 refreshes); RTCNT / RFCR count
            return 'functional', 'RTCOR; RTCSR flags, RTCNT, RFCR are counters', 2, [4, 8], [2]
        if t[1] == 'PCTRA':
            return 'functional', 'PCTRA / PCTRB / GPIOIC; PDTRA / PDTRB are the board\'s pins', 2, [4, 8], []
        if t[1] == 'PVR':
            return 'functional', 'PVR, PRR, CCR 0x105 (crt0 as KOS), QACR = 4 (the boot stub), MMUCR', 2, [], []
        if t[1] == 'SB_SBREV':
            return 'board', 'Holly registers; DMAOR / CHCR2 as the boot stub sets them', 2, [], []
        if t[1] == 'WCR3':
            return 'functional', 'registers restored by the program', 2, [], []
    if tag == 'W':
        if t[1] == 'RTCSR':
            return 'functional', 'wrong-size reads 0 [HW 6.2]; the word read is the flags', 2, [], [3]
        if t[1] == 'RFCR':
            return 'functional', 'wrong-size reads 0; the word read is the counter', 2, [3], []
        if t[1] == 'PDTRA':
            return 'functional', 'wrong-size reads 0; the word read is the pins', 2, [3], []
        if t[1] == 'PCTRB':
            return 'functional', 'wrong-size reads 0; PDTRB w is the pins', 2, [6], []
        return 'functional', 'wrong-size reads 0, holes 0 [HW 6.2]', 2, [], []
    if tag == 'K':
        if t[1] == 'RFCR':
            return 'functional', 'RFCR keys [HW 6.2] (the counter value before is the run\'s)', 2, [3, 5, 7], []
        return 'functional', 'RTCOR keys [HW 6.2]', 2, [], []
    if tag == 'Q':
        return 'functional', 'QACR restored', 2, [], []
    if tag == 'S':
        return 'functional', 'WCR3 / PCR write sizes [HW 6.2]', 2, [], []
    if tag == 'B':
        return 'env', 'the allocator\'s buffers', 2, [], []
    if tag == 'X':
        if t[1] == 'RAM':
            return 'env', 'RAM mirrors: the marker is the buffer\'s address', 2, [], []
        return 'board', 'texture-memory mirrors and holes', 2, [], []
    if tag == 'F':
        return 'env', 'refresh-timer polling (jitter), counter windows, refresh-cost histograms', 3, [], []
    if tag == 'P':
        k = 4 if t[1] in ('ram', 'vram') else 2
        return 'board', 'port pins at rest and after an MPX / SDRAM beat', k, [], []
    if tag == 'M':
        label = t[1]
        var = []
        if prog == 'bscmm' and ((label == 'oc_hit' and t[2] == 'c_rd') or (label == 'oc_miss' and t[2] == 'c_rd')):
            var = [6, 10]                        # run 1: K = 24 lost to instruction-cache conflicts (spec 6)
        if any(label.startswith(p) for p in BOARD_M):
            return 'timing-board', 'console timing with Holly on the other side', 3, [], var
        return 'timing', 'console timing of the chip and its SDRAM (bsc-memmap 4.7, 6.4)', 3, [], var
    return 'env', 'unclassified', 2, [], []


def build(prog, log):
    lines = [l.rstrip() for l in open(log, errors='replace').read().splitlines()]
    first = next(i for i, l in enumerate(lines) if l.startswith('R BCR1') or l.startswith('B ubuf'))
    last = next(i for i, l in enumerate(lines) if l == 'DONE')
    out = []
    for l in lines[first:last + 1]:
        if not l.strip():
            continue
        cls, why, kl, ign, var = classify(prog, l)
        out.append({'text': l, 'class': cls, 'why': why, 'key': l.split()[:kl], 'ignore': ign, 'var': var})
    return {'program': prog, 'source': os.path.relpath(log, HERE), 'lines': out}


def main():
    os.makedirs(os.path.join(HERE, 'expected'), exist_ok=True)
    for prog in ('bscmm', 'bscmm2'):
        doc = build(prog, os.path.join(HW, prog + '.elf.hwlog'))
        n = {}
        for e in doc['lines']:
            n[e['class']] = n.get(e['class'], 0) + 1
        json.dump(doc, open(os.path.join(HERE, 'expected', prog + '.json'), 'w'), indent=1)
        print('%s: %d lines: %s' % (prog, len(doc['lines']), ', '.join('%s %d' % kv for kv in sorted(n.items()))))


if __name__ == '__main__':
    main()
