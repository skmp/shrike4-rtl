/* bm.ld -- link map of the bare-metal soc-design / core-integration probes: one image at 0x8C010000 (where the SoC
 * testbench's boot stub jumps, rtl/v1/SOC.md 7.2 / 7.3), start-up first.  The probes' asm keeps its console alignment
 * (c1_asm.S: one 8 KB-aligned window), so fetch-word pairing and line positions inside it are the console's. */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_start)
SECTIONS
{
  . = 0x8C010000;
  .text : { *(.text.start) *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }
  .data : { *(.data .data.*) }
  . = ALIGN(4);
  __bss_start = .;
  .bss : { *(.bss .bss.* COMMON) }
  . = ALIGN(4);
  __bss_end = .;
  . = ALIGN(64);
  . = . + 0x8000;
  __stack_top = .;
  /DISCARD/ : { *(.comment) *(.note*) }
}
