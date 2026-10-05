! c1j_asm.S -- core-exc probe c1j (WI-CORE-A step A3; rtl/v1/SOC.md 9.1: the P claim before an interrupt).  Driven by
! c1j.c.  The infrastructure (ci_call, ci_abort, the lean vector table T, the loop macros) is a verbatim copy of
! tools/hw/core-integration/ci2_asm.S, which ran on the console (ci_run2.out: irqloop 29.000, ldcloop 9.000).
!
! Every loop is ci2's tt2_irqloop with a variant between the LDC SR and the loop tail:
!     90: ldc r11,sr        (IMASK 14, BL 0; with a TMU1 interrupt pending at level 15: taken at the next instruction X)
!         X ; Y ...         (the variant: X is the D leader at the take, Y ... the instructions after it)
!         dt r13 ; bf 90b
! The lean handler (+0x600) sets SSR.BL and returns to X, so there is one interrupt per iteration.  With r11 = IMASK 15
! (no interrupt) the same loop is the reference; E = loop - reference.  r9 = the scratch buffer (cached, hit).
!
! Bank-1 registers (the handlers run with RB = 1): r1 = 0xFF100010 (PMCTR), r3 = budget, r5 = record pointer,
! r6 / r7 = resume control (unused by table T at +0x600), r2 = bits the +0x600 handler ORs into SSR (BL).

    .macro LDL reg, val
    mov.l   91f, \reg
    bra     92f
    nop
    .align 2
91: .long   \val
92:
    .endm

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

! =====================================================================================================================
! ci_call(fn, vbr) and the abort path (ci2_asm.S)
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
! vector table T: lean handler for the timing loops (ci2_asm.S).  +0x100 / +0x400: SPC += r7, RTE (unexpected here: a
! fault would consume the budget and abort); +0x600: SSR |= r2_bank (BL), RTE to SPC
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
! the loops (vector table T).  VHEAD: r11 = ci_param[0] (the SR: IMASK 14 or 15), r12 = the entry SR, r2_bank = BL,
! r9 = the scratch buffer, r13 = ci_n; then the loop head (32-byte aligned).  VTAIL: the loop tail, SR restored.
    .macro VHEAD name
    TFUNC \name
    LDL     r11, _ci_param
    mov.l   @r11, r11
    stc     sr, r12
    LDL     r0, 0x10000000
    ldc     r0, r2_bank
    LDL     r9, _ci_scratch
    TLOOP_HEAD
    ldc     r11, sr
    .endm

    .macro VTAIL
    TLOOP_TAIL
    ldc     r12, sr
    TEND
    .endm

    ! v_none: X = NOP (ci2's irqloop: 29.000 / ldcloop 9.000)
    VHEAD v_none
    nop
    VTAIL

    ! v_ld1: X = NOP, then a load (1 place after the leader)
    VHEAD v_ld1
    nop
    mov.l   @r9, r2
    VTAIL

    ! v_ldx: X = the load (the leader itself), then a NOP
    VHEAD v_ldx
    mov.l   @r9, r2
    nop
    VTAIL

    ! v_add1: X = NOP, then an ADD (the control of v_ld1: same length, no memory instruction)
    VHEAD v_add1
    nop
    add     #1, r3
    VTAIL

    ! v_ld2: X = NOP, NOP, then a load (2 places after the leader)
    VHEAD v_ld2
    nop
    nop
    mov.l   @r9, r2
    VTAIL

    ! v_ld3: X = NOP, ADD, ADD, then a load (3 places after the leader)
    VHEAD v_ld3
    nop
    add     #1, r3
    add     #1, r4
    mov.l   @r9, r2
    VTAIL

    ! v_st1: X = NOP, then a store
    VHEAD v_st1
    nop
    mov.l   r2, @r9
    VTAIL

    ! v_pc1: X = NOP, then a PC-relative load
    VHEAD v_pc1
    nop
    mov.l   1f, r2
    bra     2f
    nop
    .align 2
1:  .long   0x12345678
2:
    VTAIL

    ! v_pcref: the same shape as v_pc1 without the load (X = NOP, ADD, BRA, NOP)
    VHEAD v_pcref
    nop
    add     #1, r3
    bra     2f
    nop
    .align 2
1:  .long   0x12345678
2:
    VTAIL

    ! v_ldxld1: X = a load, then another load
    VHEAD v_ldxld1
    mov.l   @r9, r2
    mov.l   @r9, r3
    VTAIL

    ! v_pref1: X = NOP, then PREF @r9
    VHEAD v_pref1
    nop
    pref    @r9
    VTAIL
