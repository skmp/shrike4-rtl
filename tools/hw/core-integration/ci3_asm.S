! ci3_asm.S -- third core-integration probe: what a slow data access holds.  Driven by ci3.c.
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
! timing loops: a slow access (P4 register EXPEVT: 3 Icyc; uncached RAM through P2: 16 Icyc) followed by work that
! does not use its result.  Every loop has a twin with a cache-hit load in the same place (r9 = scratch).
! r9 = the address (set by the macro argument), r12 = the current SR (LDC SR rewrites it unchanged: IMASK 15)

    .macro TLD name, addr, body
    TFUNC \name
    LDL     r9, \addr
    stc     sr, r12
    fldi1   fr4
    fldi1   fr5
    fldi1   fr6
    fldi1   fr7
    TLOOP_HEAD
    mov.l   @r9, r1
    \body
    TLOOP_TAIL
    TEND
    .endm

    .macro NOP8
    .rept 8
    nop
    .endr
    .endm
    .macro NOP16
    .rept 16
    nop
    .endr
    .endm
    .macro LDCSR
    ldc     r12, sr
    nop
    .endm
    .macro STCSR
    stc     sr, r2
    nop
    .endm
    .macro USE
    add     r1, r2
    .endm
    .macro FDIVUSE
    fadd    fr7, fr5
    .endm
    .macro EMPTY
    .endm

    TLD t3_ram_nop8,   _ci_scratch,            NOP8
    TLD t3_p4_nop8,    0xFF000024,             NOP8
    TLD t3_p2_nop8,    _ci_scratch+0x20000000, NOP8
    TLD t3_ram_nop16,  _ci_scratch,            NOP16
    TLD t3_p2_nop16,   _ci_scratch+0x20000000, NOP16
    TLD t3_ram_ldcsr,  _ci_scratch,            LDCSR
    TLD t3_p4_ldcsr,   0xFF000024,             LDCSR
    TLD t3_p2_ldcsr,   _ci_scratch+0x20000000, LDCSR
    TLD t3_ram_stcsr,  _ci_scratch,            STCSR
    TLD t3_p4_stcsr,   0xFF000024,             STCSR
    TLD t3_p2_stcsr,   _ci_scratch+0x20000000, STCSR
    TLD t3_ram_use,    _ci_scratch,            USE
    TLD t3_p4_use,     0xFF000024,             USE
    TLD t3_p2_use,     _ci_scratch+0x20000000, USE
    TLD t3_ram_empty,  _ci_scratch,            EMPTY
    TLD t3_p4_empty,   0xFF000024,             EMPTY
    TLD t3_p2_empty,   _ci_scratch+0x20000000, EMPTY

    ! FDIV issued just before the slow load, FADD consuming the quotient right after it: does the divider keep
    ! running while the pipeline waits for the load?
    .macro TFD name, addr
    TFUNC \name
    LDL     r9, \addr
    fldi1   fr5
    fldi1   fr6
    fldi1   fr7
    TLOOP_HEAD
    fdiv    fr6, fr7
    mov.l   @r9, r1
    fadd    fr7, fr5
    TLOOP_TAIL
    TEND
    .endm
    TFD t3_ram_fdiv, _ci_scratch
    TFD t3_p4_fdiv,  0xFF000024
    TFD t3_p2_fdiv,  _ci_scratch+0x20000000

    ! the same with the FDIV's consumer far away (only the divider pitch matters)
    .macro TFD2 name, addr
    TFUNC \name
    LDL     r9, \addr
    fldi1   fr5
    fldi1   fr6
    fldi1   fr7
    TLOOP_HEAD
    fdiv    fr6, fr7
    mov.l   @r9, r1
    TLOOP_TAIL
    TEND
    .endm
    TFD2 t3_ram_fdiv2, _ci_scratch
    TFD2 t3_p2_fdiv2,  _ci_scratch+0x20000000

    ! uncached store (posted) followed by NOPs, and a store followed by a load of another uncached word
    .macro TST name, addr, body
    TFUNC \name
    LDL     r9, \addr
    mov.l   @r9, r1                 ! store back the value the location holds
    TLOOP_HEAD
    mov.l   r1, @r9
    \body
    TLOOP_TAIL
    TEND
    .endm
    TST t3_ram_st_nop8, _ci_scratch,            NOP8
    TST t3_p2_st_nop8,  _ci_scratch+0x20000000, NOP8
    TST t3_p4_st_nop8,  0xFF200000,             NOP8
