/* mmubm.ld -- the bare-metal MMU probe port, linked to run at 0x8C010000 (P1), where the boot stub jumps (SOC.md 7.3) */
ENTRY(_start)
SECTIONS
{
  . = 0x8C010000;
  .text : { *(.text.start) *(.text) *(.text.*) }
  .rodata : { *(.rodata) *(.rodata.*) }
  .data : { *(.data) *(.data.*) }
  . = ALIGN(4);
  .bss : { __bss_start = .; *(.bss) *(.bss.*) *(COMMON) . = ALIGN(4); __bss_end = .; }
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame) }
}
