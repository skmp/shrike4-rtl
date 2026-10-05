/* bm.ld -- the bsc-memmap bare-metal programs at 0x8C010000 (P1), where the direct-boot stub jumps (SOC.md 7.3).
 *
 * The console's layout (M3, WI-SOC).  The timed kernels' costs depend on what the code between two repetitions does to
 * the caches and on the Bclk phase at which the next kernel starts (bscmm.c meas(): irq_disable, prep, the call; no
 * access between two kernels synchronises to Bclk).  Both are the console build's only if that code is the console's
 * at the console's addresses, so bm_<prog>.ld (which INCLUDEs this file) places, at the addresses of
 * tools/hw/bsc-memmap/<prog>.elf:
 *   BM_PROG_TEXT   the program's .text (meas, dump_regs) and .text.startup (main) -- the console's object order, after
 *                  KOS's startup and crtbegin -- then the kernels (tests.S), which follow at the console's address
 *                  (the Makefile checks every symbol it compares)
 *   BM_KOSIRQ      KOS's own irq_disable / irq_enable / irq_restore code (bmrt.c section .kosirq, 2 bytes of
 *                  padding first: the console's irq_disable is at 2 mod 4)
 *   BM_PROG_RODATA the program's kernel table (kern_t K_*, read by meas() between repetitions)
 *   _bm_alloc_N    memalign's buffers (the console's "B" line: KOS malloc's addresses), _bm_nalloc of them
 * and crt0.S starts main at KOS's stack pointer (0x8D000000 - arch_main's 108-byte frame). */
ENTRY(_start)
SECTIONS
{
  . = 0x8C010000;
  .text.start : { *(.text.start) }
  ASSERT(. <= BM_PROG_TEXT, "crt0 reaches the console's program address")
  .text BM_PROG_TEXT : {
    EXCLUDE_FILE(*hw_tests.o) *hw_*.o(.text .text.startup .text.*)
    *hw_tests.o(.text)
  }
  .text.rt : { *(.text .text.*) }            /* the runtime and crt0's vector table (1 KB aligned) */
  .rodata : { *(EXCLUDE_FILE(*hw_*.o) .rodata EXCLUDE_FILE(*hw_*.o) .rodata.*) *(.rodata.str*) }
  .data : { *(.data .data.*) }
  . = ALIGN(4);
  _bss_start = .;
  .bss : { *(.bss .bss.* COMMON) }
  . = ALIGN(64);
  _bss_end = .;
  _heap_start = .;
  ASSERT(. <= BM_KOSIRQ, "the image reaches KOS's irq code address")
  .kosirq BM_KOSIRQ : { *(.kosirq) }
  ASSERT(. <= BM_PROG_RODATA, "KOS's irq code reaches the program's kernel table")
  .progrodata BM_PROG_RODATA : { *hw_*.o(.rodata .rodata.*) }
  ASSERT(. <= _bm_alloc_0, "the image reaches the first console buffer")
  /DISCARD/ : { *(.comment) *(.note*) }
}
