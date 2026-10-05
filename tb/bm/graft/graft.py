#!/usr/bin/env python3
"""graft.py -- a console probe's own ELF as a bare-metal image for the v1 SoC testbench (WI-SOC, M3; README.md).

  graft.py CONSOLE.elf OUTDIR NAME [--irq] [--alloc A[,B...]] [--rt-base ADDR] [--cc PREFIX]

The bare-metal ports under tb/bm/<suite> recompile a probe against a small KOS stand-in, which moves its code and
data: on IDEAL_MEM = 1 nothing depends on that, but with the real caches every operand- / instruction-cache index and
every SDRAM row the probe touches is a property of the layout (e.g. soc-design c1g on the port puts c1_ts on the
operand-cache indices of c1_asm's literal pools: +0.1 Icyc per loop iteration the console never paid).  A grafted
image is the console's ELF itself -- KOS and the probe byte for byte at the console's addresses (every loadable
section of CONSOLE.elf) -- with a handful of KOS entry points redirected to a small runtime placed above the image
and run from P2 (uncached, so it takes no cache line):

  start (0x8C010000, where the boot stub jumps)       -> crt0.S _bm_start (KOS's start-up state, then main)
  printf putchar fflush puts fputs fputc               -> tb/bm/soc-design/bm_rt.c, rt_graft.c (sim-control putchar)
  malloc memalign calloc free realloc abort exit       -> rt_graft.c (--alloc: the console run's buffer addresses)
  timer_ms_gettime64 / us / ns, timer_ms_gettime,      -> rt_graft.c: the sim-control time register at 2000 Icyc per
  timer_spin_sleep                                        ms (tb/bm/debug-ubc-hudi/bmrt.c's; KOS's TMU is not running)

Each redirect overwrites the first 10 / 12 bytes of the KOS function with "mov.l @(disp,PC),r0 ; jmp @r0 ; nop" and
the target address.  Everything else (irq_disable / irq_restore, the perf-counter calls, the cache calls, newlib's
snprintf, memset, ...) runs as KOS's code.  --irq also repeats KOS's irq_init() before main (KOS's VBR table and handler
dispatch, for probes that install handlers with irq_set_handler).  The console run's image must be the ELF kept in
tools/hw (bsc-memmap's is not: tb/bm/bsc-memmap/bm_bscmm.ld).

Writes OUTDIR/NAME.elf (two PT_LOAD segments: the console image at 0x8C010000, the runtime at RT_BASE; soc_tb and
Sh4Sys load ELF images) and OUTDIR/NAME.graft.txt (the redirects).
"""
import os
import struct
import subprocess
import sys

REDIRECT = ['_start', '_printf', '_putchar', '_fflush', '_puts', '_fputs', '_fputc', '_malloc', '_memalign', '_calloc',
            '_free', '_realloc', '_abort', '_exit', '_timer_ms_gettime64', '_timer_us_gettime64', '_timer_ns_gettime64',
            '_timer_ms_gettime', '_timer_spin_sleep']
TARGET = {'_start': '_bm_start'}
IMG_BASE = 0x8C010000
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))


def parse_elf(path):
    b = open(path, 'rb').read()
    assert b[:4] == b'\x7fELF' and b[4] == 1 and b[5] == 1, 'an ELF32 little-endian image'
    shoff, = struct.unpack_from('<I', b, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', b, 0x2E)
    secs = []
    for i in range(shnum):
        nm, tp, fl, addr, off, size, link, info, al, es = struct.unpack_from('<IIIIIIIIII', b, shoff + i * shentsize)
        secs.append(dict(nm=nm, tp=tp, fl=fl, addr=addr, off=off, size=size, link=link, es=es))
    so = secs[shstrndx]['off']
    for s in secs:
        e = b.index(b'\0', so + s['nm'])
        s['name'] = b[so + s['nm']:e].decode()
    syms = {}
    for s in secs:
        if s['tp'] != 2:                                        # SHT_SYMTAB
            continue
        stro = secs[s['link']]['off']
        for k in range(s['size'] // 16):
            nm, val, size, info, oth, shn = struct.unpack_from('<IIIBBH', b, s['off'] + 16 * k)
            e = b.index(b'\0', stro + nm)
            name = b[stro + nm:e].decode()
            if name and (info >> 4) in (1, 2):                  # STB_GLOBAL / STB_WEAK
                syms[name] = (val, size, info & 15, shn)
            elif name and name not in syms:
                syms[name] = (val, size, info & 15, shn)
    return b, secs, syms


def trampoline(addr):
    """bytes of mov.l @(disp,PC),r0 ; jmp @r0 ; nop [; pad] and the offset of the 4-byte target within them"""
    if addr & 3 == 0:                                           # literal at addr + 8
        return struct.pack('<HHHH', 0xD001, 0x402B, 0x0009, 0x0009), 8
    return struct.pack('<HHH', 0xD001, 0x402B, 0x0009), 6        # addr = 2 mod 4: literal at addr + 6


def main():
    a = sys.argv[1:]
    if len(a) < 3:
        sys.exit(__doc__)
    elf, out, name = a[:3]
    opt_irq, allocs, rt_base, cc = False, [], None, '/opt/toolchains/dc/sh-elf/bin/sh-elf-'
    i = 3
    while i < len(a):
        if a[i] == '--irq':
            opt_irq = True
        elif a[i] == '--alloc':
            i += 1
            allocs = [int(x, 0) for x in a[i].split(',') if x]
        elif a[i] == '--rt-base':
            i += 1
            rt_base = int(a[i], 0)
        elif a[i] == '--cc':
            i += 1
            cc = a[i]
        else:
            sys.exit('graft.py: unknown option %s' % a[i])
        i += 1
    os.makedirs(out, exist_ok=True)
    b, secs, syms = parse_elf(elf)

    # the console image: every allocated section with contents at or above 0x8C010000
    load = [s for s in secs if s['fl'] & 2 and s['tp'] != 8 and s['size'] and s['addr'] >= IMG_BASE]
    hi = max(s['addr'] + s['size'] for s in load)
    img = bytearray(hi - IMG_BASE)
    for s in load:
        img[s['addr'] - IMG_BASE:s['addr'] - IMG_BASE + s['size']] = b[s['off']:s['off'] + s['size']]
    bss0, bss1 = syms['__bss_start'][0], syms['_end'][0]
    assert syms['_start'][0] == IMG_BASE, 'KOS start at 0x8C010000'

    # the redirects: (address, KOS symbol, runtime symbol)
    red = []
    for s in REDIRECT:
        if s not in syms:
            continue
        v, size, typ, _ = syms[s]
        if not IMG_BASE <= v < hi:
            continue
        t, _ = trampoline(v)
        need = len(t) + 4
        if size and size < need:
            sys.exit('graft.py: %s is %d bytes, the redirect needs %d' % (s, size, need))
        red.append((v, s, TARGET.get(s, s), need))
    red.sort()
    for (v0, s0, _, n0), (v1, s1, _, _) in zip(red, red[1:]):
        if v0 + n0 > v1:
            sys.exit('graft.py: the redirects of %s and %s overlap' % (s0, s1))
    open(os.path.join(out, name + '.con.bin'), 'wb').write(bytes(img))

    # conimg.S: the image as pieces of .incbin around the redirects
    L = ['/* generated by tb/bm/graft/graft.py from %s -- do not edit */' % os.path.relpath(elf, ROOT),
         '        .section .conimg,"ax"']
    pos = 0
    binp = os.path.join(out, name + '.con.bin')
    for v, s, t, n in red:
        off = v - IMG_BASE
        if off > pos:
            L.append('        .incbin "%s", %d, %d' % (binp, pos, off - pos))
        tb, _ = trampoline(v)
        L.append('        /* %s (0x%08X) -> %s */' % (s, v, t))
        L.append('        .short ' + ', '.join('0x%04X' % x for x in struct.unpack('<%dH' % (len(tb) // 2), tb)))
        L.append('        .long   %s' % t)
        pos = off + n
    if pos < len(img):
        L.append('        .incbin "%s", %d, %d' % (binp, pos, len(img) - pos))
    L += ['', '        .section .rodata.allocs,"a"', '        .balign 4', '        .global _bm_allocs', '_bm_allocs:']
    L += ['        .long   0x%08X' % x for x in allocs] + ['        .long   0', '']
    open(os.path.join(out, name + '.conimg.S'), 'w').write('\n'.join(L))

    if rt_base is None:
        rt_base = (bss1 + 0x1FFFF) & ~0xFFFF
    # the runtime runs from P2 (uncached): its code and data take no instruction- or operand-cache line, so what the
    # probes print between repetitions evicts nothing.  (On the console the printf path -- newlib, KOS's fs, dcload's
    # syscall and network code -- evicted lines of its own; the rows that caught those are class 'cache' / 'open' in
    # the expected files.)
    rt_base = (rt_base & 0x1FFFFFFF) | 0xA0000000
    for x in allocs:
        if (rt_base & 0x1FFFFFFF) - 0x100 <= (x & 0x1FFFFFFF) < (rt_base & 0x1FFFFFFF) + 0x20000:
            sys.exit('graft.py: console buffer 0x%08X near the runtime at 0x%08X: pass --rt-base' % (x, rt_base))
    # --irq: KOS's irq_init() is inlined into arch_main (KOS is built with LTO), so rt_graft.c's bm_kos_irq_init
    # repeats its steps through KOS's own functions and tables, whose console addresses are defined here
    KIRQ = ['_irq_set_handler', '_irq_def_timer', '_irq_handle_trapa', '_trapa_handlers', '_irq_def_fpu',
            '_irq_set_priority', '_irq_set_context', '_irq_context_default', '_irq_vma_table']
    kirq = {k: 0 for k in KIRQ}                                  # 0 unless --irq (bm_kos_irq_init is not called)
    if opt_irq:
        for k in KIRQ:
            m = [n for n in syms if n == k or n.startswith(k + '.lto_priv')]
            if not m:
                sys.exit('graft.py: --irq but no %s in %s' % (k, elf))
            kirq[k] = syms[m[0]][0]
    irq = 1 if opt_irq else 0
    ld = ['/* generated by tb/bm/graft/graft.py -- do not edit */',
          'ENTRY(_bm_img)',
          '_bm_img      = 0x%08X;' % IMG_BASE,
          '_con_main    = 0x%08X;' % syms['_main'][0],
          '_con_bss0    = 0x%08X;' % (bss0 & ~3),
          '_con_bss1    = 0x%08X;' % ((bss1 + 3) & ~3),
          '_bm_kos_irq = %d;' % irq,
          ] + ['_con%s = 0x%08X;' % (k, v) for k, v in kirq.items()] + [
          'SECTIONS',
          '{',
          '  .conimg 0x%08X : { *(.conimg) }' % IMG_BASE,
          '  .text 0x%08X : { *(.text.start) *(.text .text.*) }' % rt_base,
          '  .rodata : { *(.rodata .rodata.*) }',
          '  .data : { *(.data .data.*) }',
          '  . = ALIGN(4);',
          '  _bm_bss0 = .;',
          '  .bss : { *(.bss .bss.* COMMON) }',
          '  . = ALIGN(64);',
          '  _bm_bss1 = .;',
          '  _bm_heap0 = ALIGN(4096);',
          '  /DISCARD/ : { *(.comment) *(.note*) }',
          '}', '']
    open(os.path.join(out, name + '.ld'), 'w').write('\n'.join(ld))
    rep = ['%s: %s grafted (image 0x%08X-0x%08X, .bss 0x%08X-0x%08X), runtime at 0x%08X%s' %
           (name, os.path.relpath(elf, ROOT), IMG_BASE, hi, bss0, bss1, rt_base,
            ', KOS irq_init (VBR 0x%08X)' % kirq['_irq_vma_table'] if irq else '')]
    rep += ['  0x%08X %-10s -> %s' % (v, s, t) for v, s, t, _ in red]
    rep += ['  buffer %d at 0x%08X' % (k, x) for k, x in enumerate(allocs)]
    open(os.path.join(out, name + '.graft.txt'), 'w').write('\n'.join(rep) + '\n')
    print(rep[0])


if __name__ == '__main__':
    main()
