/* bm.ld -- the caches-sq bare-metal programs at 0x8C010000 (P1), where the direct-boot stub jumps (SOC.md 7.3).
 * kern.S's timed code is its own 8 KB-aligned section (.text.cachetest), so its I-cache indices are the console's:
 * run_kernel and the bodies on IC entries 0-42, the IC blocks on 64-170.  The programs' C code (hw_cachetest*.o) follows
 * it, starting on IC entry 43 (M3, WI-SOC): linked straight after the 8 KB section, as before, cachetest.c's
 * measure_raw() -- the per-repetition loop around run_kernel -- sat on IC entries 18 / 19 and evicted b_prefstream /
 * b_ocbwb between repetitions, so every timed body missed once (icmiss 0.01 per unit on those rows, never on the
 * console, whose KOS layout keeps the harness off the bodies).  From entry 43 the C code's hot loop lands on the free
 * entries 43-63; tb/bm/caches-sq/Makefile's layout check keeps it there. */
ENTRY(_start)
SECTIONS
{
  . = 0x8C010000;
  .text : {
    *(.text.start)
    *(EXCLUDE_FILE(*hw_cachetest*.o) .text EXCLUDE_FILE(*hw_cachetest*.o) .text.*)
    . = ALIGN(8192) + 0x560;
    *(.text .text.*)
  }
  .rodata : { *(.rodata .rodata.*) }
  .data : { *(.data .data.*) }
  . = ALIGN(4);
  _bss_start = .;
  .bss : { *(.bss .bss.* COMMON) }
  . = ALIGN(32);
  _bss_end = .;
  _end = .;
  __end = .;                               /* C's _end (SH symbols carry a leading underscore) */
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame) }
}
