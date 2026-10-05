/* bm.ld -- link map of the bare-metal p4b probe (tb/bm/p4bus): one image at 0x8C010000, where the SoC testbench's boot
 * stub jumps (SOC.md 7.2 / 7.3), start-up first.  p4b_asm.S aligns every measured function to 32 bytes (.align 5), so
 * each function's position inside its 32-byte lines -- what the fetch sees -- is the console's. */
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
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
