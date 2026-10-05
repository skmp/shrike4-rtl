/* bm.ld -- link map of the bare-metal exceptions-intc probes: one image at 0x8C010000 (where the SoC testbench's
 * boot stub jumps, rtl/v1/SOC.md 7.2 / 7.3), start-up first.  The probe snippets keep their console layout modulo
 * 1 KB (excint_asm.S opens its text with .balign 1024), so fetch-word pairing and 32-byte line positions inside each
 * snippet are the console's. */
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
  . = . + 0x4000;
  __stack_top = .;
  /DISCARD/ : { *(.comment) *(.note*) }
}
