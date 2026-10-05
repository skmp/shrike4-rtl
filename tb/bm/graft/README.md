# tb/bm/graft — console probes grafted (WI-SOC, M3)

The bare-metal ports under `tb/bm/<suite>` recompile each console probe against a small KOS stand-in. That keeps the
probe's instructions but moves its code and data. With `IDEAL_MEM = 1` (M1a–M2) nothing depends on the layout. With the
real caches and the SDRAM (M3, `rm` / `rmx`), every instruction- and operand-cache index and every SDRAM row a probe
touches does. So a port measures its own layout, not the console's:

- soc-design `c1g` on the port puts `c1_ts` (the handler's per-iteration record array) on the operand-cache indices of
  `c1_asm`'s literal pools. The records then evict the literals, which costs +0.1 Icyc per loop iteration that the
  console never paid.
- core-integration `ci4` and soc-design `c1` show a slow N2 run ("max" 15.082 / 3.086 where the console has 14.996 /
  3.000). An instruction-cache conflict between the harness and the loop makes every run take two misses. These cancel
  out in the minimum, but in one run a miss meets a refresh.

A **grafted** image is the console build's own ELF (`tools/hw/<suite>/<prog>.elf`): KOS and the probe, byte for byte, at
the console's addresses. A handful of KOS entry points are redirected to a small runtime placed above the image
(`graft.py`):

| KOS entry point | goes to |
|---|---|
| `start` (0x8C010000, where the boot stub jumps) | `crt0.S` `_bm_start` |
| `printf` `putchar` `fflush` `puts` `fputs` `fputc` | `tb/bm/soc-design/bm_rt.c`, `rt_graft.c` (the sim-control putchar port) |
| `malloc` `memalign` `calloc` `free` `realloc` `abort` `exit` | `rt_graft.c` (`--alloc`: the console run's buffer addresses first) |

Each redirect overwrites the first 10 or 12 bytes of the KOS function with `mov.l @(disp,PC),r0 ; jmp @r0 ; nop` and
the target address. Everything else runs as KOS's own code: `irq_disable` / `irq_restore`, the perf-counter calls, the
cache calls, newlib's `snprintf` and `memset`.

`crt0.S` sets up KOS's start-up state and then calls the console's `main(0, 0)`:

- the stack pointer: `r15` = 0x8CFFFF94, which is KOS's `arch_stack_16m` less `arch_main`'s 108-byte frame;
- the image's `.bss` and the runtime's `.bss` cleared;
- `FPSCR` = 0x00040000;
- `PMCR1` = 0xC023, as `perf_cntr_timer_enable` leaves it;
- a fatal `VBR` table.

With `--irq` it also repeats KOS's `irq_init()` through KOS's own functions and tables (`rt_graft.c`
`bm_kos_irq_init`). KOS is built with LTO, which inlines `irq_init` into `arch_main`, so it cannot be called directly.
This is for the probes that install handlers with `irq_set_handler`: those handlers then run through KOS's own dispatch.

`graft.py` writes `<prog>.elf` with two PT_LOAD segments: the image at 0x8C010000 and the runtime at `RT_BASE`, the
64 KB boundary above the image's `.bss`. `soc_tb` and `Sh4Sys` load it like any ELF. It also writes `<prog>.graft.txt`,
which lists the redirects.

The graft needs the ELF kept in `tools/hw` to be the image the console ran. bsc-memmap's is not (its log is older than
its ELF), so bsc-memmap keeps its port, linked at the run's addresses (`tb/bm/bsc-memmap/bm.ld`, `bm_bscmm.ld`,
`layout.py`). The DMAC probes' ports (`tb/bm/dmac`) are rewrites of the console programs, so there is nothing to graft.
The MMU probe (`tb/bm/mmu`) has its own result-block harness.

    make -C tb/bm/graft [OUT=dir] [PROGS="suite/prog ..."]     -> build/bm_graft/<suite>/<prog>.elf

`make v1-soc-m3` (tb/mk/v1_soc.mk) runs the grafted images on `rm` and checks them at level `cache`.
