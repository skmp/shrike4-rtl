/* dcload.S -- the dcload-ip stand-in of the v1 SoC testbench (rtl/v1/SOC.md 7.2, 8.3 M5; rtl/v1/spec/board.md 5.2).
 *
 * Every console probe (tools/hw/<area>/*.elf) ran under dcload-ip 2.0.3: KOS found 0xDEADBEEF at 0x8C004004, took the
 * dcload branches (fs_dcload_init_console: DCLOAD_ASSIGNWRKMEM returned -1, so dcload_type = IP; dbgio -> dcload's
 * write syscall; "dc-load console support enabled"; arch_real_exit -> the DCLOAD_EXIT syscall) and sent its console
 * over the Broadband Adapter.  The board has no BBA; this stand-in keeps KOS's dcload path and hands the console text to
 * the sim-control putchar port instead (board.md 3.10), so an unmodified probe ELF prints exactly what it printed on the
 * console, and its SCIF stays free (the serial probes drive it themselves).  Its time is not dcload's: the console's
 * write syscall ran dcload's network code (and evicted cache lines), this one stores one word per byte.
 *
 * Layout (dcload-ip's addresses; the board loads this image at physical 0x0C004000, which is otherwise zero):
 *   0x8C004000  dcload's re-entry (the PR of the dcload start state, tb/dc_board/dc_loader.h StartState::dcload):
 *               a program that returns there exits with its r4, as through the exit stub
 *   0x8C004004  DCLOADMAGICVALUE 0xDEADBEEF (kernel/arch/dreamcast/include/dc/fs_dcload.h)
 *   0x8C004008  the syscall entry KOS jumps to (kernel/arch/dreamcast/fs/dcload-syscall.s: mov.l @0x8C004008,r0; jmp @r0)
 * Syscalls (r4 = number, r5-r7 = arguments, r0 = result; r1 / r2 used, caller-saved in the SH ABI):
 *   1  DCLOAD_WRITE(fd, buf, len)   fd 1 / 2: each byte to 0xB8000004 (sim-control putchar), returns len; other fds -1
 *   15 DCLOAD_EXIT(code)            code to 0xB8000000 (the sim-control exit register), then SLEEP forever
 *   every other number (ASSIGNWRKMEM, GETHOSTINFO, the file calls): -1, as dcload-ip answers ASSIGNWRKMEM
 * Every return purges (OCBP) the operand-cache line 0x8C004000, the one KOS's stub loaded to read the entry word: on the
 * console dcload-ip's network code leaves that entry (OC index 0) invalid -- caches-sq cachetest's array rows read tag
 * 0x01848000 (a Broadband Adapter buffer line) with V = 0 there after a printf, and its "WT store miss: no allocate
 * (V)" row reads V = 0; without the purge the stand-in left 0x8C004000 valid in entry 0 and that row read 1 (M5).
 * Build: make (sh-elf binutils); the image is dcload.bin.  Owner WI-BOARD (M5, WI-SOC).
 */
        .little
        .text
        .global _start
_start:                                 /* 0x8C004000 */
        bra     reenter
        nop
        .long   0xDEADBEEF              /* 0x8C004004 */
        .long   syscall                 /* 0x8C004008 */
        .long   0
syscall:
        mov     #1, r0
        cmp/eq  r0, r4
        bt      write
        mov     #15, r0
        cmp/eq  r0, r4
        bt      exit
        bra     ret
        mov     #-1, r0
write:
        mov     r5, r0                  /* fd 1 or 2 */
        cmp/eq  #1, r0
        bt      1f
        cmp/eq  #2, r0
        bt      1f
        bra     ret
        mov     #-1, r0
1:      mov.l   putc_p, r1
        mov     r7, r0                  /* the result: len */
        tst     r7, r7
        bt      ret
2:      mov.b   @r6+, r2
        extu.b  r2, r2
        dt      r7
        bf/s    2b
        mov.l   r2, @r1
ret:    mov.l   self_p, r1              /* the entry word's line: purged, as dcload-ip's code leaves it */
        rts
        ocbp    @r1
exit:
        mov     r5, r4
reenter:
        mov.l   exit_p, r1
        mov.l   r4, @r1
4:      sleep
        bra     4b
        nop
        .align  2
self_p: .long   0x8C004000
putc_p: .long   0xB8000004
exit_p: .long   0xB8000000
