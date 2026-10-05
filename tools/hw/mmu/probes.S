! probes.S -- single-instruction probes for the MMU hardware test (mmutest.c).
!
! Every probe stores the address of its recovery label (90:) in _g_recover before the instruction under test; the
! C exception handler (mmutest.c: hnd) sends the exception return there.  A probe that does not fault falls through
! into the same label.  _<name>_at marks the instruction whose SPC is expected.

    .text
    .little

    .macro PBEGIN name
    .align 2
    .globl _\name
_\name:
    mov.l   91f, r1
    mov.l   92f, r2
    mov.l   r2, @r1
    .endm

    .macro PAT name
    .globl _\name\()_at
_\name\()_at:
    .endm

    .macro PREC
90:
    .endm

    .macro PPOOL
    .align 2
91: .long _g_recover
92: .long 90b
    .endm

! ---------------------------------------------------------------- loads / stores (r4 = address, r5 = value)
    PBEGIN p_ld8
    mov     #-1, r0
    PAT p_ld8
    mov.b   @r4, r0
    PREC
    rts
    nop
    PPOOL

    PBEGIN p_ld16
    mov     #-1, r0
    PAT p_ld16
    mov.w   @r4, r0
    PREC
    rts
    nop
    PPOOL

    PBEGIN p_ld32
    mov     #-1, r0
    PAT p_ld32
    mov.l   @r4, r0
    PREC
    rts
    nop
    PPOOL

    PBEGIN p_st8
    PAT p_st8
    mov.b   r5, @r4
    PREC
    rts
    nop
    PPOOL

    PBEGIN p_st16
    PAT p_st16
    mov.w   r5, @r4
    PREC
    rts
    nop
    PPOOL

    PBEGIN p_st32
    PAT p_st32
    mov.l   r5, @r4
    PREC
    rts
    nop
    PPOOL

! FMOV @r4,DR0 with FPSCR.SZ = 1 (64-bit load); FPSCR restored from r3 on both paths
    PBEGIN p_ldf64
    sts     fpscr, r3
    mov     #1, r0
    shll16  r0
    shll2   r0
    shll2   r0
    or      r3, r0
    lds     r0, fpscr
    PAT p_ldf64
    .word   0xF048          ! fmov @r4,dr0
    PREC
    lds     r3, fpscr
    rts
    nop
    PPOOL

! FMOV.S @r4,FR0 (32-bit FP load)
    PBEGIN p_ldf32
    PAT p_ldf32
    fmov.s  @r4, fr0
    PREC
    rts
    nop
    PPOOL

! ---------------------------------------------------------------- read-modify-write forms
    PBEGIN p_and_gbr
    stc     gbr, r3
    ldc     r4, gbr
    mov     #0, r0
    PAT p_and_gbr
    and.b   #0x7f, @(r0,gbr)
    PREC
    ldc     r3, gbr
    rts
    nop
    PPOOL

    PBEGIN p_tst_gbr
    stc     gbr, r3
    ldc     r4, gbr
    mov     #0, r0
    PAT p_tst_gbr
    tst.b   #0x80, @(r0,gbr)
    PREC
    ldc     r3, gbr
    movt    r0
    rts
    nop
    PPOOL

    PBEGIN p_tas
    PAT p_tas
    tas.b   @r4
    PREC
    movt    r0
    rts
    nop
    PPOOL

! ---------------------------------------------------------------- cache-block ops
    PBEGIN p_ocbi
    PAT p_ocbi
    ocbi    @r4
    PREC
    rts
    nop
    PPOOL

    PBEGIN p_ocbp
    PAT p_ocbp
    ocbp    @r4
    PREC
    rts
    nop
    PPOOL

    PBEGIN p_ocbwb
    PAT p_ocbwb
    ocbwb   @r4
    PREC
    rts
    nop
    PPOOL

    PBEGIN p_pref
    PAT p_pref
    pref    @r4
    PREC
    rts
    nop
    PPOOL

    PBEGIN p_movca
    mov     r5, r0
    PAT p_movca
    movca.l r0, @r4
    PREC
    rts
    nop
    PPOOL

! ---------------------------------------------------------------- two-access and post-increment forms
! r4 = first address, r5 = second address, r6 = out[4] = {r4, r5, MACH, MACL} after the probe
    PBEGIN p_macl
    clrmac
    PAT p_macl
    mac.l   @r4+, @r5+
    PREC
    mov.l   r4, @r6
    mov.l   r5, @(4,r6)
    sts     mach, r0
    mov.l   r0, @(8,r6)
    sts     macl, r0
    mov.l   r0, @(12,r6)
    rts
    nop
    PPOOL

! returns r4 after MOV.L @r4+,r1
    PBEGIN p_ldpi
    PAT p_ldpi
    mov.l   @r4+, r1
    PREC
    mov     r4, r0
    rts
    nop
    PPOOL

! returns r4 after MOV.L r5,@-r4
    PBEGIN p_stpd
    PAT p_stpd
    mov.l   r5, @-r4
    PREC
    mov     r4, r0
    rts
    nop
    PPOOL

! ---------------------------------------------------------------- precision of the cancel
! add #1,r5 (leader) ; mov.l @r4,r0 (follower, may fault) -- returns r5
    PBEGIN p_pair_f
    nop
    nop
    .align 2
    add     #1, r5
    PAT p_pair_f
    mov.l   @r4, r0
    PREC
    mov     r5, r0
    rts
    nop
    PPOOL

! mov.l @r4,r0 (leader, may fault) ; add #1,r5 (follower) -- returns r5
    PBEGIN p_pair_l
    nop
    nop
    .align 2
    PAT p_pair_l
    mov.l   @r4, r0
    add     #1, r5
    PREC
    mov     r5, r0
    rts
    nop
    PPOOL

! mov.l @r4,r0 (may fault) ; mov.l r5,@r6 (must not happen on a fault)
    PBEGIN p_ld_st
    .align 2
    PAT p_ld_st
    mov.l   @r4, r0
    mov.l   r5, @r6
    PREC
    rts
    nop
    PPOOL

! mov.l r5,@r4 (may fault) ; mov.l r5,@r6
    PBEGIN p_st_st
    .align 2
    PAT p_st_st
    mov.l   r5, @r4
    mov.l   r5, @r6
    PREC
    rts
    nop
    PPOOL

! fr1 = 1.0, fr2 = 0.0 ; mov.l @r4,r0 (may fault) ; fadd fr1,fr2 -- fr2 stored to @r6 on both paths
    PBEGIN p_ld_fadd
    fldi1   fr1
    fldi0   fr2
    nop
    nop
    nop
    nop
    .align 2
    PAT p_ld_fadd
    mov.l   @r4, r0
    fadd    fr1, fr2
    PREC
    fmov.s  fr2, @r6
    rts
    nop
    PPOOL

! bra ; delay slot mov.l @r4,r0 (may fault) -- SPC should be the BRA
    PBEGIN p_bra_ds
    PAT p_bra_ds
    bra     1f
    mov.l   @r4, r0
1:
    PREC
    rts
    nop
    PPOOL

! PR := r6 ; jsr @r5 ; delay slot mov.l @r4,r0 (may fault) -- the handler records PR (write inhibited?)
    PBEGIN p_jsr_ds
    sts     pr, r7
    lds     r6, pr
    PAT p_jsr_ds
    jsr     @r5
    mov.l   @r4, r0
    PREC
    lds     r7, pr
    rts
    nop
    PPOOL

    .align 2
    .globl _p_dummy
_p_dummy:
    rts
    nop

! ---------------------------------------------------------------- instruction fetch: jsr @r4 (a P0 routine)
    PBEGIN p_ifetch
    sts     pr, r7
    PAT p_ifetch
    jsr     @r4
    nop
    PREC
    lds     r7, pr
    rts
    nop
    PPOOL

! ---------------------------------------------------------------- user mode: r4 = user PC, r5 = user SR,
! r6 / r7 passed to the user code.  Back through the exception handler (g_recover / g_recover_sr).
    .align 2
    .globl _p_user
_p_user:
    sts.l   pr, @-r15
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    mov.l   r12, @-r15
    mov.l   r13, @-r15
    mov.l   r14, @-r15
    mov.l   .Lu_gsr, r1
    stc     sr, r0
    mov.l   r0, @r1
    mov.l   .Lu_grec, r1
    mov.l   .Lu_back, r0
    mov.l   r0, @r1
    ldc     r5, ssr
    ldc     r4, spc
    rte
    nop
.Lub:
    mov.l   @r15+, r14
    mov.l   @r15+, r13
    mov.l   @r15+, r12
    mov.l   @r15+, r11
    mov.l   @r15+, r10
    mov.l   @r15+, r9
    mov.l   @r15+, r8
    lds.l   @r15+, pr
    mov.l   .Lu_gsr, r1
    mov     #0, r0
    mov.l   r0, @r1
    mov.l   .Lu_grec, r1
    mov.l   r0, @r1
    rts
    nop
    .align 2
.Lu_gsr:  .long _g_recover_sr
.Lu_grec: .long _g_recover
.Lu_back: .long .Lub

! ---------------------------------------------------------------- MMUCR write followed by 8 instructions
    .align 2
    .globl _mmucr_set
_mmucr_set:
    mov.l   .Lmmucr, r1
    mov.l   r4, @r1
    mov.l   @r1, r0         ! read back (serialises)
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
.Lmmucr: .long 0xFF000010

    .align 2
    .globl _ldtlb_do
_ldtlb_do:
    ldtlb
    nop
    nop
    nop
    nop
    rts
    nop

! ---------------------------------------------------------------- timing
! uint32_t t_call(target r4, itlb_aa r5 (0 = none), value r6, counter address r7)
    .align 2
    .globl _t_call
_t_call:
    sts.l   pr, @-r15
    tst     r5, r5
    bt      1f
    mov.l   r6, @r5
1:
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    mov.l   @r7, r2
    jsr     @r4
    nop
    mov.l   @r7, r3
    sub     r2, r3
    mov     r3, r0
    lds.l   @r15+, pr
    rts
    nop

! uint32_t t_chase(p r4, iterations r5 (x8 loads), counter address r6)
    .align 5
    .globl _t_chase
_t_chase:
    mov.l   @r6, r2
1:
    mov.l   @r4, r4
    mov.l   @r4, r4
    mov.l   @r4, r4
    mov.l   @r4, r4
    mov.l   @r4, r4
    mov.l   @r4, r4
    mov.l   @r4, r4
    mov.l   @r4, r4
    dt      r5
    bf      1b
    mov.l   @r6, r3
    sub     r2, r3
    mov     r3, r0
    rts
    nop
