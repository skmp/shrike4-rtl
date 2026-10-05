/* crt0.S -- bare-metal start for the serial console probes on the v1 SoC testbench (WI-SER; README.md).
 *
 * Entered from the direct-boot stub at 0x8C010000 (rtl/v1/SOC.md 7.3: SR 0x400000F0 = MD, IMASK 15; VBR 0x8C000000;
 * caches off; STBCR 0x03, DMAOR 0x8201 and the KOS SCIF setup already written by the stub).  It
 *   1. sets up the stack at the top of the 16 MB RAM (KOS's stack is there too),
 *   2. turns the caches on as KOS leaves them (CCR <- 0x090D from P2, then 8 instructions),
 *   3. installs its own VBR table: every general exception, TLB miss and interrupt saves the interrupted context
 *      (shim/arch/irq.h irq_context_t), calls bm_exc_dispatch(kind, ctx) (bmrt.c: the irq_set_handler table) and
 *      returns with RTE,
 *   4. clears .bss, calls bm_kos_init() (the chip state KOS's own init leaves that the probes read or rely on: IPRC =
 *      0x0300, the DMAC's priority 3 from irq_init, and SR.IMASK = 0: KOS runs main with interrupts enabled), then
 *      main(0, 0), and ends the simulation with main's return value (sim-control exit port, board.md 3.10).
 */
        .little
        .section .text.start, "ax"
        .global _start
_start:
        mov.l   .Lstack, r15
        mov.l   .Lccr_p2, r0
        jsr     @r0
        nop
        mov.l   .Lvbr, r0
        ldc     r0, vbr
        mov.l   .Lbss_s, r1
        mov.l   .Lbss_e, r2
        mov     #0, r0
1:      cmp/hs  r2, r1
        bt      2f
        mov.l   r0, @r1
        bra     1b
        add     #4, r1
2:      mov.l   .Linit, r0
        jsr     @r0
        nop
        mov.l   .Lmain, r0
        mov     #0, r4
        jsr     @r0
        mov     #0, r5
        mov.l   .Lexit, r1
        mov.l   r0, @r1
3:      sleep
        bra     3b
        nop

/* CCR <- 0x090D (ICI ICE OCI CB OCE), executed through its P2 alias; 8 instructions follow the write (hm 4.3) */
        .align  2
_bm_ccr_set:
        mov.l   .Lccr, r1
        mov.l   .Lccr_v, r2
        mov.l   r2, @r1
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        rts
        nop

        .align  2
.Lstack:  .long 0x8D000000
.Lccr_p2: .long _bm_ccr_set + 0x20000000
.Lccr:    .long 0xFF00001C
.Lccr_v:  .long 0x0000090D
.Lvbr:    .long _bm_vbr
.Lbss_s:  .long _bss_start
.Lbss_e:  .long _bss_end
.Linit:   .long _bm_kos_init
.Lmain:   .long _main
.Lexit:   .long 0xB8000000

/* ---------------------------------------------------------------------------------------------------- VBR table */
        .text
        .balign 1024
        .global _bm_vbr
_bm_vbr:
        .space  0x100
        bra     _bm_exc_common              /* VBR + 0x100: general exceptions */
        mov     #1, r4
        .space  0x400 - 0x104
        bra     _bm_exc_common              /* VBR + 0x400: TLB miss */
        mov     #4, r4
        .space  0x600 - 0x404
        bra     _bm_exc_common              /* VBR + 0x600: interrupts */
        mov     #6, r4

/* SR.RB = 1 here: r0-r7 are bank 1 and free; the interrupted R0-R7 are bank 0.  Context layout (shim/arch/irq.h
 * irq_context_t): r[0..15] at 0, pc 64, pr 68, sr 72, gbr 76, mach 80, macl 84.  SR.BL = 1 while the handler runs,
 * so interrupts stay blocked until the RTE (as KOS's handlers run). */
_bm_exc_common:
        mov.l   .Lctx, r0
        stc     r0_bank, r1
        mov.l   r1, @(0, r0)
        stc     r1_bank, r1
        mov.l   r1, @(4, r0)
        stc     r2_bank, r1
        mov.l   r1, @(8, r0)
        stc     r3_bank, r1
        mov.l   r1, @(12, r0)
        stc     r4_bank, r1
        mov.l   r1, @(16, r0)
        stc     r5_bank, r1
        mov.l   r1, @(20, r0)
        stc     r6_bank, r1
        mov.l   r1, @(24, r0)
        stc     r7_bank, r1
        mov.l   r1, @(28, r0)
        mov.l   r8, @(32, r0)
        mov.l   r9, @(36, r0)
        mov.l   r10, @(40, r0)
        mov.l   r11, @(44, r0)
        mov.l   r12, @(48, r0)
        mov.l   r13, @(52, r0)
        mov.l   r14, @(56, r0)
        mov.l   r15, @(60, r0)
        mov     r0, r5
        add     #64, r0
        stc     spc, r1
        mov.l   r1, @(0, r0)
        sts     pr, r1
        mov.l   r1, @(4, r0)
        stc     ssr, r1
        mov.l   r1, @(8, r0)
        stc     gbr, r1
        mov.l   r1, @(12, r0)
        sts     mach, r1
        mov.l   r1, @(16, r0)
        sts     macl, r1
        mov.l   r1, @(20, r0)
        mov.l   .Lxstack, r15
        mov.l   .Ldisp, r0
        jsr     @r0                           /* bm_exc_dispatch(kind r4, ctx r5) */
        nop
        mov.l   .Lctx, r0
        add     #64, r0
        mov.l   @(0, r0), r1
        ldc     r1, spc
        mov.l   @(4, r0), r1
        lds     r1, pr
        mov.l   @(8, r0), r1
        ldc     r1, ssr
        mov.l   @(12, r0), r1
        ldc     r1, gbr
        mov.l   @(16, r0), r1
        lds     r1, mach
        mov.l   @(20, r0), r1
        lds     r1, macl
        add     #-64, r0
        mov.l   @(32, r0), r8
        mov.l   @(36, r0), r9
        mov.l   @(40, r0), r10
        mov.l   @(44, r0), r11
        mov.l   @(48, r0), r12
        mov.l   @(52, r0), r13
        mov.l   @(56, r0), r14
        mov.l   @(60, r0), r15
        mov.l   @(0, r0), r1
        ldc     r1, r0_bank
        mov.l   @(4, r0), r1
        ldc     r1, r1_bank
        mov.l   @(8, r0), r1
        ldc     r1, r2_bank
        mov.l   @(12, r0), r1
        ldc     r1, r3_bank
        mov.l   @(16, r0), r1
        ldc     r1, r4_bank
        mov.l   @(20, r0), r1
        ldc     r1, r5_bank
        mov.l   @(24, r0), r1
        ldc     r1, r6_bank
        mov.l   @(28, r0), r1
        ldc     r1, r7_bank
        rte
        nop

        .align  2
.Lctx:    .long _bm_ctx
.Lxstack: .long _bm_xstack + 1024
.Ldisp:   .long _bm_exc_dispatch

        .bss
        .align  2
        .global _bm_ctx
_bm_ctx:    .space 88
_bm_xstack: .space 1024
