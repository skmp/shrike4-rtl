/* bm.ld -- bare-metal link of the timers-clock probe ports: a flat image at P1 0x8C010000 (SOC.md 7.2), started by the
   boot stub (SOC.md 7.3: R15 0x8C00F400, VBR 0x8C000000, SR 0x400000F0, STBCR 0x03, PMCR1 0xC023). */
ENTRY(_start)
SECTIONS
{
  . = 0x8C010000;
  .text   : { *(.text.start) *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }
  .data   : { *(.data .data.*) }
  . = ALIGN(4);
  __bss_start = .;
  .bss    : { *(.bss .bss.*) *(COMMON) }
  . = ALIGN(4);
  __bss_end = .;
  /DISCARD/ : { *(.comment) *(.eh_frame) }
}
