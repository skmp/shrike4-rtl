! ci4_asm.S -- fourth core-integration probe (rtl/v1/SOC.md 9.3, WI-CORE-F): what the MA hold `mah` holds in the FPU.
! Driven by ci4.c.  Timing loops only (no exceptions, no interrupts, MMU off); the infrastructure below (ci_call, the
! two vector tables, the TFUNC / TLOOP macros) is ci3_asm.S's, copied unchanged.  The loop bodies are
! tb/v1_fpuhold_probe.h's, in the same order: a slow load (uncached P2, or a cacheable miss: r9 alternates between two
! lines of one operand-cache set) around FPU work whose result a consumer after the load uses, each with a cache-hit
! twin.  Registers: r9 the load address, r10 the toggle (0x4000, or 0), r5 = r6 = 3, FR0-FR8 = 1.0; the double loops
! set FPSCR.PR (ci_call restores FPSCR, FR12-FR15, FPUL, MACH / MACL afterwards).
!

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
! the probe loops

    .macro PSETUP addr, pr, xr
    LDL     r9, \addr
    fldi1   fr0
    fldi1   fr1
    fldi1   fr2
    fldi1   fr3
    fldi1   fr4
    fldi1   fr5
    fldi1   fr6
    fldi1   fr7
    fldi1   fr8
    mov     #3, r5
    mov     #3, r6
    mov     #\xr, r10
    shll8   r10
    .if \pr
    mov     #12, r1
    shll16  r1
    add     #1, r1
    lds     r1, fpscr
    .endif
    .endm

    .macro TP name, addr, pr, xr, body
    TFUNC \name
    PSETUP \addr, \pr, \xr
    TLOOP_HEAD
    \body
    TLOOP_TAIL
    TEND
    .endm

    .macro NOPS n
    .rept \n
    nop
    .endr
    .endm
    .macro B_FD
    fdiv    fr6, fr7
    mov.l   @r9, r1
    fadd    fr7, fr5
    .endm
    .macro B_FDN n
    fdiv    fr6, fr7
    NOPS    \n
    mov.l   @r9, r1
    fadd    fr7, fr5
    .endm
    .macro B_FDN2
    B_FDN 2
    .endm
    .macro B_FDN4
    B_FDN 4
    .endm
    .macro B_FDN6
    B_FDN 6
    .endm
    .macro B_FDN8
    B_FDN 8
    .endm
    .macro B_FDU2
    fdiv    fr6, fr7
    mov.l   @r9, r1
    nop
    nop
    fadd    fr7, fr5
    .endm
    .macro B_FDD
    fdiv    dr6, dr4
    mov.l   @r9, r1
    fadd    dr4, dr8
    .endm
    .macro B_FIPR
    fipr    fv0, fv4
    mov.l   @r9, r1
    fadd    fr7, fr8
    .endm
    .macro B_FTRV
    ftrv    xmtrx, fv4
    mov.l   @r9, r1
    fadd    fr7, fr8
    .endm
    .macro B_FSCA
    fsca    fpul, dr2
    mov.l   @r9, r1
    fadd    fr3, fr8
    .endm
    .macro B_MUL
    mul.l   r6, r5
    mov.l   @r9, r1
    sts     macl, r7
    .endm
    .macro B_DBL
    fadd    dr0, dr2
    mov.l   @r9, r1
    fadd    dr2, dr4
    .endm
    .macro B_EFE
    mov.l   @r9, r1
    fadd    fr4, fr8
    .endm
    .macro B_EEX
    mov.l   @r9, r1
    add     r5, r7
    .endm
    .macro B_FDM
    xor     r10, r9
    fdiv    fr6, fr7
    mov.l   @r9, r1
    fadd    fr7, fr5
    .endm
    .macro B_FDMN4
    xor     r10, r9
    fdiv    fr6, fr7
    NOPS    4
    mov.l   @r9, r1
    fadd    fr7, fr5
    .endm
    .macro B_EM
    xor     r10, r9
    mov.l   @r9, r1
    .endm

    .macro KIND pfx, addr
    TP \pfx\()_fd,     \addr, 0, 0, B_FD
    TP \pfx\()_fd_n2,  \addr, 0, 0, B_FDN2
    TP \pfx\()_fd_n4,  \addr, 0, 0, B_FDN4
    TP \pfx\()_fd_n6,  \addr, 0, 0, B_FDN6
    TP \pfx\()_fd_n8,  \addr, 0, 0, B_FDN8
    TP \pfx\()_fd_u2,  \addr, 0, 0, B_FDU2
    TP \pfx\()_fdd,    \addr, 1, 0, B_FDD
    TP \pfx\()_fipr,   \addr, 0, 0, B_FIPR
    TP \pfx\()_ftrv,   \addr, 0, 0, B_FTRV
    TP \pfx\()_fsca,   \addr, 0, 0, B_FSCA
    TP \pfx\()_mul,    \addr, 0, 0, B_MUL
    TP \pfx\()_dbl,    \addr, 1, 0, B_DBL
    TP \pfx\()_efe,    \addr, 0, 0, B_EFE
    TP \pfx\()_eex,    \addr, 0, 0, B_EEX
    .endm

    KIND t4_ram, _ci_scratch
    KIND t4_p2,  _ci_scratch+0x20000000
    TP t4_ram_fdm,     _ci4_big, 0, 0,    B_FDM
    TP t4_miss_fdm,    _ci4_big, 0, 0x40, B_FDM
    TP t4_ram_fdm_n4,  _ci4_big, 0, 0,    B_FDMN4
    TP t4_miss_fdm_n4, _ci4_big, 0, 0x40, B_FDMN4
    TP t4_ram_em,      _ci4_big, 0, 0,    B_EM
    TP t4_miss_em,     _ci4_big, 0, 0x40, B_EM
