! ci2_asm.S -- second core-integration probe (follow-ups to ci_asm.S; same infrastructure).  Driven by ci2.c.
! behaviour and cost, precise exceptions in dual-issue pairs and with the FPU, delay-slot faults, SLEEP wake-up, and
! the cost of P4 register accesses.  Driven by ci.c.
!
! Every test is a leaf function called through ci_call(fn, vbr), which saves the whole C context, loads the bank-1
! registers the handlers use, switches VBR to one of the two vector tables below, calls fn and restores everything.
! The tests run with interrupts masked (IMASK 15, set by ci.c), BL = 0, MD = 1, RB = 0, in P1 cacheable memory.
!
! Bank-1 registers (the handlers run with RB = 1):
!   r1 = 0xFF100010 (PMCTR of PRFC1, elapsed CPU cycles, low word)   r3 = exception budget (0 -> abort the test)
!   r5 = record pointer (recording handler)   r6 = resume address (0 = SPC + r7)   r7 = bytes to add to SPC
! Vector table F records one 64-byte record per exception (entry time, vector, SPC, SSR, SGR, PR, EXPEVT, INTEVT,
! TEA, PTEH, TRA, SR) and returns to r6 or SPC + r7 with SSR.IMASK forced to 15.  Vector table T is lean (timing
! loops): SPC += r7, RTE.  Both abort a runaway test (budget exhausted) to ci_abort, which returns to C.

    .macro LDL reg, val
    mov.l   91f, \reg
    bra     92f
    nop
    .align 2
91: .long   \val
92:
    .endm

    .macro OUTREG idx, reg
    mov.l   93f, r0
    mov.l   \reg, @(\idx*4,r0)
    bra     94f
    nop
    .align 2
93: .long   _ci_out
94:
    .endm

    .macro OUTADDR idx, lab
    mov.l   95f, r0
    mov.l   96f, r1
    mov.l   r1, @(\idx*4,r0)
    bra     97f
    nop
    .align 2
95: .long   _ci_out
96: .long   \lab
97:
    .endm

    .macro RESUME lab
    mov.l   98f, r0
    ldc     r0, r6_bank
    bra     99f
    nop
    .align 2
98: .long   \lab
99:
    .endm

    .macro SKIP n
    mov     #\n, r0
    ldc     r0, r7_bank
    .endm

    ! functional test: r13 = PMCTR address, r9 = scratch, r14 = return PR; r10 = time stamp taken by the test
    .macro FTEST name
    .text
    .align 2
    .globl _\name
_\name:
    sts     pr, r14
    LDL     r13, 0xFF100010
    LDL     r9, _ci_scratch
    .endm

    .macro FEND
    OUTREG  15, r10
    lds     r14, pr
    rts
    nop
    .endm

    ! timing test: loop of ci_n iterations
    .macro TFUNC name
    .text
    .align 2
    .globl _\name
_\name:
    sts     pr, r14
    .endm

    .macro TEND
    lds     r14, pr
    rts
    nop
    .endm

    .macro TLOOP_HEAD
    LDL     r13, _ci_n
    mov.l   @r13, r13
    bra     90f
    nop
    .balign 32
90:
    .endm

    .macro TLOOP_TAIL
    dt      r13
    bf      90b
    .endm

    ! MMU on (AT = 1, TI = 1: every UTLB / ITLB entry invalid) / back to the saved MMUCR.  r11 / r12 are kept.
    .macro MMU_ON
    LDL     r12, 0xFF000010
    mov.l   @r12, r11
    mov     #5, r0
    mov.l   r0, @r12
    .rept 8
    nop
    .endr
    .endm

    .macro MMU_OFF
    mov     r11, r0
    or      #4, r0
    mov.l   r0, @r12
    .rept 8
    nop
    .endr
    .endm

! =====================================================================================================================
! ci_call(fn, vbr) and the abort path
    .text
    .align 2
    .globl _ci_call
_ci_call:
    mov.l   .Lctx_a, r0
    mov.l   r8, @(0,r0)
    mov.l   r9, @(4,r0)
    mov.l   r10, @(8,r0)
    mov.l   r11, @(12,r0)
    mov.l   r12, @(16,r0)
    mov.l   r13, @(20,r0)
    mov.l   r14, @(24,r0)
    mov.l   r15, @(28,r0)
    sts     pr, r1
    mov.l   r1, @(32,r0)
    stc     gbr, r1
    mov.l   r1, @(36,r0)
    stc     vbr, r1
    mov.l   r1, @(40,r0)
    stc     sr, r1
    mov.l   r1, @(44,r0)
    sts     fpscr, r1
    mov.l   r1, @(48,r0)
    sts     fpul, r1
    mov.l   r1, @(52,r0)
    sts     mach, r1
    mov.l   r1, @(56,r0)
    sts     macl, r1
    mov.l   r1, @(60,r0)
    mov     r0, r2
    add     #64, r2
    fmov.s  fr12, @r2
    add     #4, r2
    fmov.s  fr13, @r2
    add     #4, r2
    fmov.s  fr14, @r2
    add     #4, r2
    fmov.s  fr15, @r2
    mov.l   .Lpmctr_a, r1
    ldc     r1, r1_bank
    mov.l   .Lbudget_a, r1
    mov.l   @r1, r1
    ldc     r1, r3_bank
    mov     #0, r1
    ldc     r1, r4_bank
    ldc     r1, r6_bank
    ldc     r1, r7_bank
    mov.l   .Lrecp_a, r1
    mov.l   @r1, r1
    ldc     r1, r5_bank
    ldc     r5, vbr
    jsr     @r4
    nop
_ci_back:
    stc     r5_bank, r1
    mov.l   .Lrecp_a, r2
    mov.l   r1, @r2
    stc     r3_bank, r1
    mov.l   .Lbudget_a, r2
    mov.l   r1, @r2
    mov.l   .Lctx_a, r0
    mov.l   @(44,r0), r1
    ldc     r1, sr
    mov.l   @(48,r0), r1
    lds     r1, fpscr
    mov     r0, r2
    add     #64, r2
    fmov.s  @r2+, fr12
    fmov.s  @r2+, fr13
    fmov.s  @r2+, fr14
    fmov.s  @r2+, fr15
    mov.l   @(52,r0), r1
    lds     r1, fpul
    mov.l   @(56,r0), r1
    lds     r1, mach
    mov.l   @(60,r0), r1
    lds     r1, macl
    mov.l   @(40,r0), r1
    ldc     r1, vbr
    mov.l   @(36,r0), r1
    ldc     r1, gbr
    mov.l   @(32,r0), r1
    lds     r1, pr
    mov.l   @(0,r0), r8
    mov.l   @(4,r0), r9
    mov.l   @(8,r0), r10
    mov.l   @(12,r0), r11
    mov.l   @(16,r0), r12
    mov.l   @(20,r0), r13
    mov.l   @(24,r0), r14
    mov.l   @(28,r0), r15
    rts
    nop

    .globl _ci_abort
_ci_abort:
    mov.l   .Lctx_a, r0
    mov.l   @(44,r0), r1
    ldc     r1, sr
    mov.l   .Laborted_a, r1
    mov     #1, r2
    mov.l   r2, @r1
    bra     _ci_back
    nop
    .align 2
.Lctx_a:     .long _ci_ctx
.Lpmctr_a:   .long 0xFF100010
.Lbudget_a:  .long _ci_budget
.Lrecp_a:    .long _ci_recp
.Laborted_a: .long _ci_aborted

! =====================================================================================================================
! vector table F: recording handler
    .section .text.civbrf,"ax"
    .balign 1024
    .globl _ci_vbr_f
_ci_vbr_f:
    .org 0x100
    mov.l   @r1, r2
    bra     rec_common
    mov     #1, r0
    .org 0x400
    mov.l   @r1, r2
    bra     rec_common
    mov     #4, r0
    .org 0x600
    mov.l   @r1, r2
    bra     rec_common
    mov     #6, r0
rec_common:
    dt      r3
    bt      rec_abort
    mov.l   r2, @(0,r5)
    mov.l   r0, @(4,r5)
    stc     spc, r0
    mov.l   r0, @(8,r5)
    stc     ssr, r0
    mov.l   r0, @(12,r5)
    .word   0x003A              ! stc sgr, r0
    mov.l   r0, @(16,r5)
    sts     pr, r0
    mov.l   r0, @(20,r5)
    mov.l   .Lccn_f, r4
    mov.l   @(0x24,r4), r0      ! EXPEVT
    mov.l   r0, @(24,r5)
    mov.l   @(0x28,r4), r0      ! INTEVT
    mov.l   r0, @(28,r5)
    mov.l   @(0x0C,r4), r0      ! TEA
    mov.l   r0, @(32,r5)
    mov.l   @(0x00,r4), r0      ! PTEH
    mov.l   r0, @(36,r5)
    mov.l   @(0x20,r4), r0      ! TRA
    mov.l   r0, @(40,r5)
    stc     sr, r0
    mov.l   r0, @(44,r5)
    tst     r6, r6
    bt      1f
    ldc     r6, spc
    bra     2f
    nop
1:  stc     spc, r0
    add     r7, r0
    ldc     r0, spc
2:  stc     ssr, r0
    or      #0xf0, r0
    ldc     r0, ssr
    add     #64, r5
    rte
    nop
rec_abort:
    mov.l   .Labort_f, r0
    ldc     r0, spc
    rte
    nop
    .align 2
.Lccn_f:   .long 0xFF000000
.Labort_f: .long _ci_abort

! =====================================================================================================================
! vector table T: lean handler for the timing loops (SPC += r7)
    .section .text.civbrt,"ax"
    .balign 1024
    .globl _ci_vbr_t
_ci_vbr_t:
    .org 0x100
    stc     spc, r0
    add     r7, r0
    ldc     r0, spc
    dt      r3
    bt      9f
    rte
    nop
9:  mov.l   8f, r0
    ldc     r0, spc
    rte
    nop
    .align 2
8:  .long   _ci_abort
    .org 0x400
    stc     spc, r0
    add     r7, r0
    ldc     r0, spc
    dt      r3
    bt      9f
    rte
    nop
9:  mov.l   8f, r0
    ldc     r0, spc
    rte
    nop
    .align 2
8:  .long   _ci_abort
    .org 0x600
    stc     ssr, r0
    or      r2, r0                  ! r2_bank: bits forced into SSR (BL for the interrupt loop)
    ldc     r0, ssr
    dt      r3
    bt      9f
    rte
    nop
9:  mov.l   8f, r0
    ldc     r0, spc
    rte
    nop
    .align 2
8:  .long   _ci_abort

! =====================================================================================================================
! functional follow-ups (vector table F).  Tests that matter warm run their sequence twice (the second pass has the
! code in the instruction cache); pass p stores its time stamp in out[14 + p].

    ! TRAPA, twice: entry time stamps (calibrates the time-stamp offset of the other tests)
    FTEST t2_trapa
    LDL     r11, _ci_out+56
    mov     #2, r12
2:  mov.l   @r13, r10
    trapa   #1
    mov.l   r10, @r11
    add     #4, r11
    dt      r12
    bf      2b
    lds     r14, pr
    rts
    nop

    ! illegal instruction, twice (D-stage exception; same time-stamp structure as t2_trapa)
    FTEST t2_illegal
    mov     #2, r0
    ldc     r0, r7_bank
    LDL     r11, _ci_out+56
    mov     #2, r12
2:  mov.l   @r13, r10
    .word   0xfffd
    mov.l   r10, @r11
    add     #4, r11
    dt      r12
    bf      2b
    lds     r14, pr
    rts
    nop

    ! pending TMU1 interrupt unmasked by LDC SR (IMASK 15 -> 14), twice
    FTEST t2_irq_ldc
    LDL     r11, _ci_param
    mov.l   @r11, r11
    stc     sr, r12
    OUTADDR 0, 3f
    LDL     r7, _ci_out+56
    mov     #2, r4
2:  mov     #0, r8
    mov.l   @r13, r10
    ldc     r11, sr
3:  .rept 16
    add     #1, r8
    .endr
    ldc     r12, sr
    mov.l   r10, @r7
    add     #4, r7
    dt      r4
    bf      2b
    lds     r14, pr
    rts
    nop

    ! the same, unmasked by clearing BL (IMASK already 14 while BL = 1), twice
    FTEST t2_irq_bl
    LDL     r11, _ci_param
    mov.l   @r11, r11
    stc     sr, r12
    LDL     r2, 0x10000000
    or      r11, r2
    OUTADDR 0, 3f
    LDL     r7, _ci_out+56
    mov     #2, r4
2:  mov     #0, r8
    ldc     r2, sr
    .rept 12
    add     #1, r8
    .endr
    mov.l   @r13, r10
    ldc     r11, sr
3:  .rept 16
    add     #1, r8
    .endr
    ldc     r12, sr
    mov.l   r10, @r7
    add     #4, r7
    dt      r4
    bf      2b
    lds     r14, pr
    rts
    nop

    ! unmasked by RTE (SSR = IMASK 14), twice: slot, then the target sled
    FTEST t2_irq_rte
    LDL     r11, _ci_param
    mov.l   @r11, r11
    stc     sr, r12
    OUTADDR 0, 3f
    LDL     r7, _ci_out+56
    mov     #2, r4
2:  mov     #0, r8
    ldc     r11, ssr
    LDL     r1, 3f
    ldc     r1, spc
    mov.l   @r13, r10
    rte
    add     #1, r8
    add     #1, r8
    add     #1, r8
3:  .rept 16
    add     #1, r8
    .endr
    ldc     r12, sr
    mov.l   r10, @r7
    add     #4, r7
    dt      r4
    bf      2b
    lds     r14, pr
    rts
    nop

    ! LDC SR (unmask); k NOPs; BRA; slot; target, twice
    .macro IRQBR2 k
    FTEST t2_irq_br\k
    LDL     r11, _ci_param
    mov.l   @r11, r11
    stc     sr, r12
    OUTADDR 0, 1f
    OUTADDR 1, 3f
    OUTADDR 2, 4f
    OUTADDR 3, 5f
    LDL     r7, _ci_out+56
    mov     #2, r4
2:  mov     #0, r8
    mov.l   @r13, r10
1:  ldc     r11, sr
    .rept \k
    nop
    .endr
3:  bra     5f
4:  add     #1, r8
    add     #1, r8
    add     #1, r8
5:  .rept 16
    add     #1, r8
    .endr
    ldc     r12, sr
    mov.l   r10, @r7
    add     #4, r7
    dt      r4
    bf      2b
    lds     r14, pr
    rts
    nop
    .endm
    IRQBR2 0
    IRQBR2 1
    IRQBR2 2
    IRQBR2 3
    IRQBR2 4
    IRQBR2 5
    IRQBR2 6
    IRQBR2 7
    IRQBR2 8
    IRQBR2 9

    ! MAC.L @Rm+,@Rn+ (m = r10, n = r11) with misaligned operands: which access faults first (TEA), are Rm / Rn
    ! updated.  out[2k] = r10 after, out[2k+1] = r11 after, for the three cases
    .macro MACMIS k, offm, offn
    RESUME  8f
    LDL     r10, _ci_scratch+\offm
    LDL     r11, _ci_scratch+\offn
    mac.l   @r10+, @r11+
    nop
8:
    OUTREG  (2*\k), r10
    OUTREG  (2*\k+1), r11
    .endm
    FTEST t2_mac_mis
    MACMIS  0, 1, 17
    MACMIS  1, 32, 49
    MACMIS  2, 33, 48
    lds     r14, pr
    rts
    nop

    ! a faulting load paired with FRCHG / FSCHG: is the FPSCR mode flip cancelled?  out[0] / out[1] = FPSCR after
    FTEST t2_frchg_kill
    add     #1, r9
    sts     fpscr, r12
    RESUME  8f
    mov.l   @r13, r10
    nop
    nop
1:  mov.l   @r9, r1
    frchg
    nop
    nop
8:
    sts     fpscr, r11
    lds     r12, fpscr
    OUTREG  0, r11
    RESUME  8f
    nop
    nop
1:  mov.l   @r9, r1
    fschg
    nop
    nop
8:
    sts     fpscr, r11
    lds     r12, fpscr
    OUTREG  1, r11
    FEND

    ! an older inexact FDIV (1/3) in flight when a younger FADD traps on V: FPSCR after the handler.
    ! out[5] = FPSCR, out[8] = FR9 (the quotient)
    FTEST t2_fdiv_cause
    RESUME  8f
    sts     fpscr, r12
    fldi1   fr3
    fldi1   fr6
    fadd    fr6, fr6
    fldi1   fr8
    fadd    fr6, fr8
    fldi1   fr9
    LDL     r2, 0x7FC00000
    lds     r2, fpul
    fsts    fpul, fr2
    LDL     r2, 0x00040800
    lds     r2, fpscr
    mov.l   @r13, r10
    nop
    nop
1:  fdiv    fr8, fr9
    fadd    fr2, fr3
    nop
    nop
8:
    sts     fpscr, r11
    lds     r12, fpscr
    OUTREG  5, r11
    flds    fr9, fpul
    sts     fpul, r11
    OUTREG  8, r11
    FEND

! =====================================================================================================================
! timing follow-ups (vector table T)

    ! one TMU1 interrupt per iteration: LDC SR clears BL (IMASK 14 throughout), the interrupt is taken at the next
    ! instruction, the handler returns with SSR.BL = 1
    TFUNC tt2_irqloop
    LDL     r11, _ci_param
    mov.l   @r11, r11
    stc     sr, r12
    LDL     r0, 0x10000000
    ldc     r0, r2_bank
    TLOOP_HEAD
    ldc     r11, sr
    nop
    TLOOP_TAIL
    ldc     r12, sr
    TEND

    ! the same loop with IMASK 15 (no interrupt)
    TFUNC tt2_ldcloop
    stc     sr, r12
    TLOOP_HEAD
    ldc     r12, sr
    nop
    TLOOP_TAIL
    TEND

    .macro FPSCR_ON val
    sts     fpscr, r12
    fldi1   fr3
    fldi1   fr4
    fldi1   fr5
    fldi1   fr6
    fldi1   fr7
    LDL     r2, 0x7FC00000
    lds     r2, fpul
    fsts    fpul, fr2
    LDL     r2, \val
    lds     r2, fpscr
    SKIP    2
    .endm

    ! V enable: FADD of a signalling NaN just loaded (the operand reaches F1 through the LS bypass) / loaded 4 NOPs
    ! earlier; _ok variants load 1.0 (no trap)
    .macro VBYP name, off, gap
    TFUNC \name
    FPSCR_ON 0x00040800
    LDL     r9, _ci_scratch+\off
    TLOOP_HEAD
    fmov.s  @r9, fr2
    .rept \gap
    nop
    .endr
    fadd    fr2, fr3
    TLOOP_TAIL
    lds     r12, fpscr
    TEND
    .endm
    VBYP tt2_vbyp,       0, 0
    VBYP tt2_vbyp_ok,    4, 0
    VBYP tt2_vfar,       0, 4
    VBYP tt2_vfar_ok,    4, 4

    ! a misaligned load as follower (lane 1) of an ADD, and as leader of an ADD; skip past the load / past both
    TFUNC tt2_pair_l1
    LDL     r9, _ci_scratch+1
    SKIP    2
    TLOOP_HEAD
    add     #1, r8
    mov.l   @r9, r1
    TLOOP_TAIL
    TEND

    TFUNC tt2_pair_l0
    LDL     r9, _ci_scratch+1
    SKIP    4
    TLOOP_HEAD
    mov.l   @r9, r1
    add     #1, r8
    TLOOP_TAIL
    TEND

    TFUNC tt2_pair_ok
    LDL     r9, _ci_scratch
    TLOOP_HEAD
    add     #1, r8
    mov.l   @r9, r1
    TLOOP_TAIL
    TEND

