/* bm.ld -- the serial bare-metal probe ports at 0x8C010000 (P1), where the direct-boot stub jumps (SOC.md 7.3) */
ENTRY(_start)
SECTIONS
{
  . = 0x8C010000;
  .text : { *(.text.start) *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }
  .data : { *(.data .data.*) }
  . = ALIGN(4);
  _bss_start = .;
  .bss : { *(.bss .bss.* COMMON) }
  . = ALIGN(4);
  _bss_end = .;
  /DISCARD/ : { *(.comment) *(.note*) }
}
