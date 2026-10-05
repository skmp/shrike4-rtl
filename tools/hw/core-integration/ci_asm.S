! ci_asm.S -- core-integration probes of the SH7091 (rtl/v1/spec/core-integration.md): exception / interrupt entry
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
    or      #0xf0, r0
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
! functional tests (vector table F).  out[k] = label addresses / values, out[15] = time stamp before the event.

! --- faults in delay slots: which PC is saved, is PR written
    FTEST t_slot_bra
    add     #1, r9
    RESUME  8f
    OUTADDR 0, 1f
    OUTADDR 1, 2f
    mov.l   @r13, r10
1:  bra     3f
2:  mov.l   @r9, r1
    nop
3:  nop
8:
    FEND

    FTEST t_slot_bts
    add     #1, r9
    RESUME  8f
    OUTADDR 0, 1f
    OUTADDR 1, 2f
    clrt
    mov.l   @r13, r10
1:  bt/s    3f
2:  mov.l   @r9, r1
    nop
3:  nop
8:
    FEND

    FTEST t_slot_bsr
    add     #1, r9
    RESUME  8f
    OUTADDR 0, 1f
    OUTADDR 1, 2f
    LDL     r2, 0x12345678
    lds     r2, pr
    mov.l   @r13, r10
1:  bsr     3f
2:  mov.l   @r9, r1
    nop
3:  nop
8:
    sts     pr, r11
    OUTREG  3, r11
    FEND

    FTEST t_slot_jsr
    add     #1, r9
    RESUME  8f
    OUTADDR 0, 1f
    OUTADDR 1, 2f
    LDL     r2, 0x12345678
    lds     r2, pr
    LDL     r11, 3f
    mov.l   @r13, r10
1:  jsr     @r11
2:  mov.l   @r9, r1
    nop
3:  nop
8:
    sts     pr, r11
    OUTREG  3, r11
    FEND

    FTEST t_slot_rts
    add     #1, r9
    RESUME  8f
    OUTADDR 0, 1f
    OUTADDR 1, 2f
    LDL     r2, 3f
    lds     r2, pr
    mov.l   @r13, r10
1:  rts
2:  mov.l   @r9, r1
    nop
3:  nop
8:
    FEND

    FTEST t_slot_fpudis
    RESUME  8f
    OUTADDR 0, 1f
    OUTADDR 1, 2f
    fldi1   fr4
    fldi1   fr5
    stc     sr, r12
    LDL     r2, 0x8000
    or      r12, r2
    ldc     r2, sr
    mov.l   @r13, r10
1:  bra     3f
2:  fadd    fr4, fr5
    nop
3:  nop
8:
    ldc     r12, sr
    FEND

    FTEST t_slot_illegal
    RESUME  8f
    OUTADDR 0, 1f
    OUTADDR 1, 2f
    mov.l   @r13, r10
1:  bra     3f
2:  .word   0xfffd
    nop
3:  nop
8:
    FEND

    FTEST t_slot_trapa
    RESUME  8f
    OUTADDR 0, 1f
    OUTADDR 1, 2f
    mov.l   @r13, r10
1:  bra     3f
2:  trapa   #5
    nop
3:  nop
8:
    FEND

! --- plain exceptions (not in a slot)
    FTEST t_illegal
    RESUME  8f
    OUTADDR 0, 1f
    mov.l   @r13, r10
1:  .word   0xfffd
    nop
8:
    FEND

    FTEST t_trapa
    RESUME  8f
    OUTADDR 0, 1f
    OUTADDR 1, 2f
    mov.l   @r13, r10
1:  trapa   #0x5a
2:  nop
8:
    FEND

    FTEST t_adr_rd
    add     #1, r9
    RESUME  8f
    OUTADDR 0, 1f
    mov.l   @r13, r10
1:  mov.l   @r9, r1
    nop
8:
    OUTREG  2, r9
    FEND

    FTEST t_adr_wr
    add     #1, r9
    RESUME  8f
    OUTADDR 0, 1f
    mov.l   @r13, r10
1:  mov.w   r1, @r9
    nop
8:
    OUTREG  2, r9
    FEND

    FTEST t_adr_quad
    add     #4, r9
    RESUME  8f
    OUTADDR 0, 1f
    sts     fpscr, r12
    LDL     r2, 0x00140000
    lds     r2, fpscr
    mov.l   @r13, r10
1:  fmov    @r9, dr4
    nop
8:
    lds     r12, fpscr
    OUTREG  2, r9
    FEND

! --- precise exceptions in dual-issue pairs
    FTEST t_pair_ld_add
    add     #1, r9
    RESUME  8f
    OUTADDR 0, 1f
    mov     #0, r8
    mov.l   @r13, r10
    nop
    nop
1:  mov.l   @r9, r1
2:  add     #1, r8
    nop
    nop
8:
    OUTREG  3, r8
    FEND

    FTEST t_pair_add_ld
    add     #1, r9
    RESUME  8f
    OUTADDR 0, 2f
    mov     #0, r8
    mov.l   @r13, r10
    nop
    nop
1:  add     #1, r8
2:  mov.l   @r9, r1
    nop
    nop
8:
    OUTREG  3, r8
    FEND

    FTEST t_pair_ld_fadd
    add     #1, r9
    RESUME  8f
    OUTADDR 0, 1f
    fldi1   fr4
    fldi1   fr5
    mov.l   @r13, r10
    nop
    nop
1:  mov.l   @r9, r1
2:  fadd    fr4, fr5
    nop
    nop
8:
    flds    fr5, fpul
    sts     fpul, r11
    OUTREG  4, r11
    FEND

    ! FPU exception (V enable, signalling NaN operand) as leader / as follower of an ADD
    .macro FPU_V_SETUP
    sts     fpscr, r12
    fldi1   fr3
    fldi1   fr4
    fldi1   fr5
    fldi1   fr6
    fadd    fr6, fr6
    fldi1   fr7
    fadd    fr6, fr7
    LDL     r2, 0x7FC00000          ! SH-4 signalling NaN (fraction MSB set)
    lds     r2, fpul
    fsts    fpul, fr2
    .endm
    .macro FPU_OUT
    sts     fpscr, r11
    lds     r12, fpscr
    OUTREG  5, r11
    flds    fr3, fpul
    sts     fpul, r11
    OUTREG  4, r11
    flds    fr7, fpul
    sts     fpul, r11
    OUTREG  6, r11
    flds    fr5, fpul
    sts     fpul, r11
    OUTREG  7, r11
    flds    fr9, fpul
    sts     fpul, r11
    OUTREG  8, r11
    .endm

    FTEST t_pair_fadd_add
    RESUME  8f
    OUTADDR 0, 1f
    FPU_V_SETUP
    LDL     r2, 0x00040800
    lds     r2, fpscr
    mov     #0, r8
    mov.l   @r13, r10
    nop
    nop
1:  fadd    fr2, fr3
2:  add     #1, r8
    nop
    nop
8:
    OUTREG  3, r8
    FPU_OUT
    FEND

    FTEST t_pair_add_fadd
    RESUME  8f
    OUTADDR 0, 2f
    FPU_V_SETUP
    LDL     r2, 0x00040800
    lds     r2, fpscr
    mov     #0, r8
    mov.l   @r13, r10
    nop
    nop
1:  add     #1, r8
2:  fadd    fr2, fr3
    nop
    nop
8:
    OUTREG  3, r8
    FPU_OUT
    FEND

    ! an older FDIV in flight when a younger FADD traps: does the FDIV still write FR7 (3 / 2 = 1.5)?
    FTEST t_fdiv_trap
    RESUME  8f
    OUTADDR 0, 2f
    FPU_V_SETUP
    LDL     r2, 0x00040800
    lds     r2, fpscr
    mov.l   @r13, r10
    nop
    nop
1:  fdiv    fr6, fr7
2:  fadd    fr2, fr3
    nop
    nop
8:
    FPU_OUT
    FEND

    ! I enable: an exact FADD traps (decided from the opcode); destination and cause
    FTEST t_fpu_i
    RESUME  8f
    OUTADDR 0, 1f
    FPU_V_SETUP
    LDL     r2, 0x00040080
    lds     r2, fpscr
    mov.l   @r13, r10
1:  fadd    fr4, fr5
    nop
8:
    FPU_OUT
    FEND

    ! Z enable: FDIV by zero (FR9 / FR8, FR8 = 0)
    FTEST t_fpu_z
    RESUME  8f
    OUTADDR 0, 1f
    FPU_V_SETUP
    fldi0   fr8
    fldi1   fr9
    LDL     r2, 0x00040400
    lds     r2, fpscr
    mov.l   @r13, r10
1:  fdiv    fr8, fr9
    nop
8:
    FPU_OUT
    FEND

! --- UTLB miss (MMU on, empty TLB), read and write
    FTEST t_utlb_rd
    RESUME  8f
    OUTADDR 0, 1f
    LDL     r9, 0x00100000
    MMU_ON
    mov.l   @r13, r10
1:  mov.l   @r9, r1
    nop
8:
    MMU_OFF
    FEND

    FTEST t_utlb_wr
    RESUME  8f
    OUTADDR 0, 1f
    LDL     r9, 0x00100004
    MMU_ON
    mov.l   @r13, r10
1:  mov.l   r1, @r9
    nop
8:
    MMU_OFF
    FEND

! --- time stamp calibration: two back-to-back PMCTR reads, and one 8 NOPs later
    FTEST t_cal
    mov.l   @r13, r10
    mov.l   @r13, r11
    .rept 8
    nop
    .endr
    mov.l   @r13, r12
    OUTREG  3, r11
    OUTREG  4, r12
    FEND

! --- interrupt acceptance (a TMU1 underflow interrupt at level 15 is pending, IMASK = 15 until the test unmasks it;
!     _ci_param[0] = the SR with IMASK = 14).  The handler returns to SPC with IMASK = 15.
    FTEST t_irq_ldc
    LDL     r11, _ci_param
    mov.l   @r11, r11
    stc     sr, r12
    OUTADDR 0, 1f
    OUTADDR 1, 2f
    mov     #0, r8
    mov.l   @r13, r10
1:  ldc     r11, sr
2:  .rept 32
    add     #1, r8
    .endr
8:
    OUTREG  3, r8
    ldc     r12, sr
    FEND

    FTEST t_irq_ldcl
    LDL     r11, _ci_param
    mov.l   @r11, r11
    mov.l   r11, @r9
    stc     sr, r12
    OUTADDR 0, 1f
    OUTADDR 1, 2f
    mov     #0, r8
    mov.l   @r13, r10
1:  ldc.l   @r9+, sr
2:  .rept 32
    add     #1, r8
    .endr
8:
    OUTREG  3, r8
    ldc     r12, sr
    FEND

    FTEST t_irq_rte
    LDL     r11, _ci_param
    mov.l   @r11, r11
    stc     sr, r12
    OUTADDR 0, 1f
    OUTADDR 1, 2f
    OUTADDR 2, 3f
    ldc     r11, ssr
    LDL     r2, 3f
    ldc     r2, spc
    mov     #0, r8
    mov.l   @r13, r10
1:  rte
2:  add     #1, r8
    add     #1, r8
    add     #1, r8
3:  .rept 32
    add     #1, r8
    .endr
8:
    OUTREG  3, r8
    ldc     r12, sr
    FEND

    ! LDC SR (unmask); k NOPs; BRA; slot; target.  Where is the interrupt accepted relative to the branch and its slot?
    .macro IRQBR k
    FTEST t_irq_br\k
    LDL     r11, _ci_param
    mov.l   @r11, r11
    stc     sr, r12
    OUTADDR 0, 1f
    OUTADDR 1, 2f
    OUTADDR 2, 3f
    OUTADDR 3, 4f
    mov     #0, r8
    mov.l   @r13, r10
1:  ldc     r11, sr
    .rept \k
    nop
    .endr
2:  bra     4f
3:  add     #1, r8
    add     #1, r8
    add     #1, r8
4:  .rept 16
    add     #1, r8
    .endr
8:
    OUTREG  4, r8
    ldc     r12, sr
    FEND
    .endm
    IRQBR 0
    IRQBR 1
    IRQBR 2
    IRQBR 3
    IRQBR 4
    IRQBR 5
    IRQBR 6
    IRQBR 7

    ! pending interrupt, IMASK 14 but BL = 1 (held), then BL cleared by LDC SR
    FTEST t_irq_bl
    LDL     r11, _ci_param
    mov.l   @r11, r11
    stc     sr, r12
    OUTADDR 0, 1f
    OUTADDR 1, 2f
    OUTADDR 2, 3f
    LDL     r2, 0x10000000
    or      r11, r2
    mov     #0, r8
    mov.l   @r13, r10
1:  ldc     r2, sr
    .rept 8
    add     #1, r8
    .endr
2:  ldc     r11, sr
3:  .rept 16
    add     #1, r8
    .endr
8:
    OUTREG  3, r8
    ldc     r12, sr
    FEND

    ! SLEEP with TMU1 running (not yet underflowed): the wake-up interrupt; the handler resumes past the SLEEP
    FTEST t_irq_sleep
    LDL     r11, _ci_param
    mov.l   @r11, r11
    stc     sr, r12
    RESUME  8f
    OUTADDR 0, 1f
    OUTADDR 1, 2f
    OUTADDR 2, 3f
    mov     #0, r8
    mov.l   @r13, r10
1:  ldc     r11, sr
2:  sleep
3:  add     #1, r8
    nop
8:
    OUTREG  3, r8
    ldc     r12, sr
    FEND

! =====================================================================================================================
! timing loops (vector table T): cycles per iteration = (run(N2) - run(N1)) / (N2 - N1)

    TFUNC tt_nop
    TLOOP_HEAD
    nop
    TLOOP_TAIL
    TEND

    TFUNC tt_trapa
    TLOOP_HEAD
    trapa   #0x20
    TLOOP_TAIL
    TEND

    TFUNC tt_illegal
    SKIP    2
    TLOOP_HEAD
    .word   0xfffd
    TLOOP_TAIL
    TEND

    TFUNC tt_adr_rd
    LDL     r9, _ci_scratch+1
    SKIP    2
    TLOOP_HEAD
    mov.l   @r9, r1
    TLOOP_TAIL
    TEND

    TFUNC tt_adr_wr
    LDL     r9, _ci_scratch+1
    SKIP    2
    TLOOP_HEAD
    mov.l   r1, @r9
    TLOOP_TAIL
    TEND

    TFUNC tt_ld
    LDL     r9, _ci_scratch
    TLOOP_HEAD
    mov.l   @r9, r1
    TLOOP_TAIL
    TEND

    TFUNC tt_st
    LDL     r9, _ci_scratch
    TLOOP_HEAD
    mov.l   r1, @r9
    TLOOP_TAIL
    TEND

    TFUNC tt_fpudis
    fldi1   fr4
    fldi1   fr5
    stc     sr, r12
    LDL     r2, 0x8000
    or      r12, r2
    SKIP    2
    ldc     r2, sr
    TLOOP_HEAD
    fadd    fr4, fr5
    TLOOP_TAIL
    ldc     r12, sr
    TEND

    TFUNC tt_fadd
    fldi1   fr4
    fldi1   fr5
    TLOOP_HEAD
    fadd    fr4, fr5
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

    TFUNC tt_fpu_i
    FPSCR_ON 0x00040080
    TLOOP_HEAD
    fadd    fr4, fr5
    TLOOP_TAIL
    lds     r12, fpscr
    TEND

    TFUNC tt_fpu_v
    FPSCR_ON 0x00040800
    TLOOP_HEAD
    fadd    fr2, fr3
    TLOOP_TAIL
    lds     r12, fpscr
    TEND

    TFUNC tt_fpu_v_ok
    FPSCR_ON 0x00040800
    TLOOP_HEAD
    fadd    fr4, fr5
    TLOOP_TAIL
    lds     r12, fpscr
    TEND

    TFUNC tt_fdiv_v
    FPSCR_ON 0x00040800
    TLOOP_HEAD
    fdiv    fr6, fr7
    fadd    fr2, fr3
    TLOOP_TAIL
    lds     r12, fpscr
    TEND

    TFUNC tt_fdiv_ok
    FPSCR_ON 0x00040800
    TLOOP_HEAD
    fdiv    fr6, fr7
    fadd    fr4, fr5
    TLOOP_TAIL
    lds     r12, fpscr
    TEND

    TFUNC tt_fdiv
    FPSCR_ON 0x00040800
    TLOOP_HEAD
    fdiv    fr6, fr7
    TLOOP_TAIL
    lds     r12, fpscr
    TEND

    TFUNC tt_utlb_rd
    LDL     r9, 0x00100000
    SKIP    2
    MMU_ON
    TLOOP_HEAD
    mov.l   @r9, r1
    TLOOP_TAIL
    MMU_OFF
    TEND

    TFUNC tt_utlb_wr
    LDL     r9, 0x00100004
    SKIP    2
    MMU_ON
    TLOOP_HEAD
    mov.l   r1, @r9
    TLOOP_TAIL
    MMU_OFF
    TEND

    TFUNC tt_at_p1
    LDL     r9, _ci_scratch
    MMU_ON
    TLOOP_HEAD
    mov.l   @r9, r1
    TLOOP_TAIL
    MMU_OFF
    TEND

    ! trap + the recording-free handler, with the handler's skip done by TRAPA's own SPC (completion type)
    TFUNC tt_trapa2
    TLOOP_HEAD
    trapa   #0x20
    nop
    TLOOP_TAIL
    TEND

! --- P4 / memory access cost: 8 accesses per iteration
    .macro P4RD name, addr, op
    TFUNC \name
    LDL     r9, \addr
    TLOOP_HEAD
    .rept 8
    \op     @r9, r1
    .endr
    TLOOP_TAIL
    TEND
    .endm

    .macro P4WR name, addr, ldop, stop
    TFUNC \name
    LDL     r9, \addr
    \ldop   @r9, r1
    TLOOP_HEAD
    .rept 8
    \stop   r1, @r9
    .endr
    TLOOP_TAIL
    TEND
    .endm

    P4RD tr_ram,      _ci_scratch, mov.l
    P4RD tr_p2ram,    _ci_scratch+0x20000000, mov.l
    P4RD tr_expevt,   0xFF000024, mov.l
    P4RD tr_ccr,      0xFF00001C, mov.l
    P4RD tr_expevt7,  0x1F000024, mov.l
    P4RD tr_pmctr,    0xFF100010, mov.l
    P4RD tr_bara,     0xFF200000, mov.l
    P4RD tr_bcr1,     0xFF800000, mov.l
    P4RD tr_sar1,     0xFFA00010, mov.l
    P4RD tr_frqcr,    0xFFC00000, mov.w
    P4RD tr_r64cnt,   0xFFC80000, mov.b
    P4RD tr_ipra,     0xFFD00004, mov.w
    P4RD tr_tcnt0,    0xFFD8000C, mov.l
    P4RD tr_scfsr2,   0xFFE80010, mov.w
    P4RD tr_icaa,     0xF0000000, mov.l
    P4RD tr_icda,     0xF1000000, mov.l
    P4RD tr_itlbaa,   0xF2000000, mov.l
    P4RD tr_ocaa,     0xF4000000, mov.l
    P4RD tr_ocda,     0xF5000000, mov.l
    P4RD tr_utlbaa,   0xF6000000, mov.l

    P4WR tw_ram,      _ci_scratch, mov.l, mov.l
    P4WR tw_p2ram,    _ci_scratch+0x20000000, mov.l, mov.l
    P4WR tw_pteh,     0xFF000000, mov.l, mov.l
    P4WR tw_bara,     0xFF200000, mov.l, mov.l
    P4WR tw_ipra,     0xFFD00004, mov.w, mov.w
    P4WR tw_tcor1,    0xFFD80014, mov.l, mov.l

    TFUNC tw_sq
    LDL     r9, 0xE0000000
    TLOOP_HEAD
    mov.l   r1, @r9
    mov.l   r1, @(4,r9)
    mov.l   r1, @(8,r9)
    mov.l   r1, @(12,r9)
    mov.l   r1, @(16,r9)
    mov.l   r1, @(20,r9)
    mov.l   r1, @(24,r9)
    mov.l   r1, @(28,r9)
    TLOOP_TAIL
    TEND
