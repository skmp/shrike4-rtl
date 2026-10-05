! sleepwarm_asm.S -- warm SLEEP wake-up trials for sleepwarm.c (rtl/v1/SOC.md 2.5, 8.3 M1b; WI-SOC).  The same file
! builds the console program (tools/hw/timers-clock, KOS) and the bare-metal port for the v1 SoC (tb/bm/timers-clock).
!
! uint32_t sw_kK(uint32_t *rec, uint32_t tstr_addr, uint32_t tstr_val, uint32_t vbr)     K = 0..7
!   Installs `vbr` (sw_vbr_j or sw_vbr_s), lowers SR.IMASK to 14 (TMU1 is at priority 15), reads PMCTR1L (A), starts
!   TMU1 with the TSTR byte write, stores A, executes K NOPs and SLEEP, and after the wake-up reads PMCTR1L again.
!   rec: [0] flag: 1 the interrupt woke the SLEEP (SPC = SLEEP + 2), 2 it came before the SLEEP (SPC redirected past
!             it, so SLEEP never runs without a pending wake-up), 3 an exception
!        [1] PMCTR1L in the handler (its first read in sw_vbr_j, its second in sw_vbr_s)   [2] SPC   [3] INTEVT
!        [4] A   [5] PMCTR1L after the SLEEP   [9] TCNT1 in the handler (its first read in sw_vbr_s, its second in
!        sw_vbr_j)   [10] the SLEEP's address   [6] EXPEVT of an exception
! The handler clears TCR1 (UNIE = 0) and reads it back, returns with SSR.IMASK = 15, so nothing re-enters.
    .text
    .globl _sw_k0, _sw_k1, _sw_k2, _sw_k3, _sw_k4, _sw_k5, _sw_k6, _sw_k7, _sw_vbr_j, _sw_vbr_s, _sw_bail

    .macro  TRIAL k
    .align 5
_sw_k\k:
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    stc     vbr, r8             ! r8 = caller's VBR (r8-r15 are not banked: the handler sees them)
    stc     sr, r9              ! r9 = caller's SR (IMASK 15)
    ldc     r7, vbr
    mov.l   .Lsa\k, r0          ! (MOVA reaches 4-byte-aligned targets only: the SLEEP moves by 2 per NOP)
    mov.l   r0, @(40,r4)        ! rec[10] = the SLEEP's address
    mov.l   .Lpm\k, r11         ! PMCTR1L
    mov     r9, r10
    mov.l   .Lim\k, r0
    and     r0, r10
    mov     #0x70, r0
    shll    r0                  ! 0xE0: IMASK 14
    or      r0, r10
    ldc     r10, sr             ! TMU1 (priority 15) may interrupt from here
    mov.l   @r11, r0            ! A
    mov.b   r6, @r5             ! TSTR: start TMU1
    mov.l   r0, @(16,r4)        ! rec[4] = A
    .rept   \k
    nop
    .endr
.Lsl\k:
    sleep
    mov.l   @r11, r0
    mov.l   r0, @(20,r4)        ! rec[5]
    bra     _sw_done
    nop
    .align 2
.Lpm\k: .long 0xFF100008
.Lim\k: .long 0xFFFFFF0F
.Lsa\k: .long .Lsl\k
    .endm

    TRIAL   0
    TRIAL   1
    TRIAL   2
    TRIAL   3
    TRIAL   4
    TRIAL   5
    TRIAL   6
    TRIAL   7

    .align 5
_sw_bail:
_sw_done:
    ldc     r9, sr              ! IMASK 15 again, RB 0
    ldc     r8, vbr
    mov.l   @r15+, r11
    mov.l   @r15+, r10
    mov.l   @r15+, r9
    mov.l   @r15+, r8
    mov.l   @r4, r0
    rts
    nop

! ---- the two vector tables: general exceptions bail out (flag 3); the interrupt at +0x600 records and returns
    .macro  EXCVEC
    .org 0x100
    nop                         ! (no branch as the first instruction at VBR + 0x100 / 0x400 / 0x600)
    stc     r4_bank, r2
    mov.l   1f, r0
    mov.l   @r0, r1
    mov.l   r1, @(24,r2)        ! rec[6] = EXPEVT
    stc     spc, r1
    mov.l   r1, @(8,r2)
    mov     #3, r1
    mov.l   r1, @r2
    ldc     r9, ssr             ! back to the caller's SR (IMASK 15, RB 0)
    mov.l   2f, r0
    ldc     r0, spc
    rte
    nop
    .align 2
1:  .long 0xFF000024
2:  .long _sw_bail
    .org 0x400
    nop
    bra     _sw_exc_\@
    nop
_sw_exc_\@:
    stc     r4_bank, r2
    mov     #3, r1
    mov.l   r1, @r2
    ldc     r9, ssr
    mov.l   3f, r0
    ldc     r0, spc
    rte
    nop
    .align 2
3:  .long _sw_bail
    .endm

    ! the common tail of both handlers: SPC, INTEVT, TCR1 cleared and read back, IMASK 15 on return, and the SPC
    ! rule: SLEEP + 2 is the wake-up (flag 1); anything else came before the SLEEP (flag 2): return past it
    .macro  TAIL
    stc     spc, r1
    mov.l   r1, @(8,r2)
    mov.l   5f, r0
    mov.l   @r0, r1
    mov.l   r1, @(12,r2)        ! rec[3] = INTEVT
    mov.l   6f, r0
    mov     #0, r1
    mov.w   r1, @r0             ! TCR1 = 0: UNF and UNIE cleared
    mov.w   @r0, r1             ! read back: the request is gone before RTE
    stc     ssr, r1
    mov     #0x78, r0
    shll    r0
    or      r0, r1
    ldc     r1, ssr             ! return with IMASK 15
    mov.l   @(40,r2), r3        ! the SLEEP
    add     #2, r3
    stc     spc, r1
    cmp/eq  r1, r3
    bt/s    7f
    mov     #1, r1
    ldc     r3, spc             ! before the SLEEP: return past it
    mov     #2, r1
7:  mov.l   r1, @r2
    rte
    nop
    .align 2
5:  .long 0xFF000028
6:  .long 0xFFD8001C
    .endm

    .section .text.swvbrj, "ax"
    .balign 1024
_sw_vbr_j:                      ! the handler reads PMCTR1L first, then TCNT1 (timers-clock J)
    EXCVEC
    .org 0x600
    mov.l   8f, r0
    mov.l   @r0, r1             ! PMCTR1L
    stc     r4_bank, r2
    mov.l   r1, @(4,r2)
    mov.l   9f, r0
    mov.l   @r0, r1             ! TCNT1
    mov.l   r1, @(36,r2)
    TAIL
    .align 2
8:  .long 0xFF100008
9:  .long 0xFFD80018

    .section .text.swvbrs, "ax"
    .balign 1024
_sw_vbr_s:                      ! the handler reads TCNT1 first, then PMCTR1L (timers-clock S)
    EXCVEC
    .org 0x600
    mov.l   8f, r0
    mov.l   @r0, r1             ! TCNT1
    stc     r4_bank, r2
    mov.l   r1, @(36,r2)
    mov.l   9f, r0
    mov.l   @r0, r1             ! PMCTR1L
    mov.l   r1, @(4,r2)
    TAIL
    .align 2
8:  .long 0xFFD80018
9:  .long 0xFF100008
