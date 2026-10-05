! crt0.S -- start-up, exception vectors and cache helpers of the bare-metal MMU probe port (tb/bm/mmu/mmubm.c).
! Owner: WI-MMU.  Replaces what KOS gave tools/hw/mmu/mmutest*.c: the machine state, the exception dispatch to a
! recovery label (mmutest.c hnd()), and the cache maintenance calls.
!
! Entry: _start at 0x8C010000 (P1), from the boot stub (SOC.md 7.3) or a testbench that loads the image there.  The
! program runs privileged, SR = 0x400000F0 (MD = 1, RB = 0, BL = 0, IMASK = 15: no interrupt is ever taken), with its
! own VBR, R15 = 0x8C00F400 (the boot stub's stack), FPSCR = 0x00040000 (KOS's value: p_ldf64's return depends on it),
! the caches on as KOS left them for the console runs (CCR = 0x0105: IC on, OC on, P1 copy-back), written from P2.
!
! Exceptions (VBR + 0x100 general, VBR + 0x400 TLB miss) run _exc_common in register bank 1, so the interrupted
! code's R0-R7 (bank 0) and R8-R15 are untouched.  It records EXPEVT, TEA, PTEH, MMUCR, SPC, SSR and PR in _X (the
! layout of exc_t in mmubm.c) and returns to _g_recover (with SR = _g_recover_sr when that is set), exactly as
! mmutest.c's hnd() did through KOS.  With _g_recover = 0 the exception is unexpected: it counts _g_unexpected, turns
! translation off (MMUCR = TI) and steps over the instruction.  An interrupt (VBR + 0x600) cannot happen with
! IMASK = 15; if one does, it is counted and returned from.

    .little
    .section .text.start, "ax"
    .globl _start
_start:
    mov.l   .Lsr, r0
    ldc     r0, sr
    mov.l   .Lstack, r15
    mov.l   .Lvbr, r0
    ldc     r0, vbr
    mov.l   .Lfpscr, r0
    lds     r0, fpscr
    mov.l   .Lccr_p2, r0                ! CCR must be written from P2, 8 instructions before cached code
    jsr     @r0
    nop
    ! clear .bss
    mov.l   .Lbss0, r1
    mov.l   .Lbss1, r2
    mov     #0, r0
1:  cmp/hs  r2, r1
    bt      2f
    mov.l   r0, @r1
    bra     1b
    add     #4, r1
2:  mov.l   .Lmain, r0
    jsr     @r0
    nop
    ! main returned: the results are in memory; tell the board (sim control, board.md 3.10: exit code at physical
    ! 0x18000000, harmless in ideal memory) and halt with BL = 1 so that nothing can wake the core
    mov.l   .Lexit, r1
    mov.l   r0, @r1
    mov.l   .Lsr_bl, r0
    ldc     r0, sr
3:  sleep
    bra     3b
    nop
    .align 2
.Lsr:       .long 0x400000F0
.Lsr_bl:    .long 0x500000F0
.Lstack:    .long 0x8C00F400
.Lvbr:      .long _vbr_base
.Lfpscr:    .long 0x00040000
.Lccr_p2:   .long _ccr_init + 0x20000000
.Lbss0:     .long __bss_start
.Lbss1:     .long __bss_end
.Lmain:     .long _main
.Lexit:     .long 0xB8000000

! ---- CCR = 0x0905 (ICI | ICE | OCI | CB | OCE: both caches invalidated, then on; KOS's CCR 0x0105), run from P2
    .text
    .align 2
_ccr_init:
    mov.l   .Lccr, r1
    mov.l   .Lccr_v, r0
    mov.l   r0, @r1
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
    .align 2
.Lccr:      .long 0xFF00001C
.Lccr_v:    .long 0x00000905

! ---- void ic_inval(void): invalidate the whole instruction cache (CCR.ICI), from P2
    .align 2
    .globl _ic_inval
_ic_inval:
    mov.l   .Lici_p2, r0
    jmp     @r0
    nop
    .align 2
.Lici_p2:   .long _ic_inval_p2 + 0x20000000
_ic_inval_p2:
    mov.l   .Lccr2, r1
    mov.l   @r1, r0
    mov.l   .Lici, r2
    or      r2, r0
    mov.l   r0, @r1
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
    .align 2
.Lccr2:     .long 0xFF00001C
.Lici:      .long 0x00000800

! ---- the vector table (VBR must be 1 KB aligned in practice; the handlers are at +0x100, +0x400, +0x600)
    .balign 1024
    .globl _vbr_base
_vbr_base:
    .skip   0x100
    bra     _exc_common                 ! VBR + 0x100: general exceptions
    nop
    .skip   0x400 - 0x104
    bra     _exc_common                 ! VBR + 0x400: TLB miss
    nop
    .skip   0x600 - 0x404
    bra     _irq_common                 ! VBR + 0x600: interrupt (never with IMASK = 15)
    nop

    .align 2
_exc_common:
    mov.l   .LX, r0
    mov.l   @r0, r1
    add     #1, r1
    mov.l   r1, @r0                     ! X.n++
    mov.l   .Lexpevt, r2
    mov.l   @r2, r1
    mov.l   r1, @(4, r0)                ! X.expevt
    mov.l   .Ltea, r2
    mov.l   @r2, r1
    mov.l   r1, @(8, r0)                ! X.tea
    mov.l   .Lpteh, r2
    mov.l   @r2, r1
    mov.l   r1, @(12, r0)               ! X.pteh
    mov.l   .Lmmucr, r2
    mov.l   @r2, r1
    mov.l   r1, @(16, r0)               ! X.mmucr
    stc     spc, r1
    mov.l   r1, @(20, r0)               ! X.spc
    stc     ssr, r1
    mov.l   r1, @(24, r0)               ! X.ssr
    sts     pr, r1
    mov.l   r1, @(28, r0)               ! X.pr
    mov.l   .Lgrec, r2
    mov.l   @r2, r1
    tst     r1, r1
    bt      .Lunexp
    ldc     r1, spc                     ! return to the probe's recovery label
    mov.l   .Lgrecsr, r2
    mov.l   @r2, r1
    tst     r1, r1
    bt      1f
    ldc     r1, ssr                     ! back to privileged mode after a user-mode probe
1:  rte
    nop
.Lunexp:
    mov.l   .Lgunexp, r2
    mov.l   @r2, r1
    add     #1, r1
    mov.l   r1, @r2
    mov.l   .Lmmucr, r2
    mov     #4, r1
    mov.l   r1, @r2                     ! MMUCR = TI: translation off, TLBs invalid
    stc     spc, r1
    add     #2, r1
    ldc     r1, spc
    mov.l   .Lgrecsr, r2
    mov.l   @r2, r1
    tst     r1, r1
    bt      2f
    ldc     r1, ssr
2:  rte
    nop

_irq_common:
    mov.l   .Lgirq, r2
    mov.l   @r2, r1
    add     #1, r1
    mov.l   r1, @r2
    rte
    nop

    .align 2
.LX:        .long _X
.Lexpevt:   .long 0xFF000024
.Ltea:      .long 0xFF00000C
.Lpteh:     .long 0xFF000000
.Lmmucr:    .long 0xFF000010
.Lgrec:     .long _g_recover
.Lgrecsr:   .long _g_recover_sr
.Lgunexp:   .long _g_unexpected
.Lgirq:     .long _g_irqs
