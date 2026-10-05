! probes2.S -- extra probes for mmutest2.c (same recovery protocol as probes.S: _g_recover, label 90)

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

! PR := r6 ; bsr ; delay slot mov.l @r4,r0 (may fault)
    PBEGIN p_bsr_ds
    sts     pr, r7
    lds     r6, pr
    PAT p_bsr_ds
    bsr     1f
    mov.l   @r4, r0
    bra     2f
    nop
1:
    rts
    nop
2:
    PREC
    lds     r7, pr
    rts
    nop
    PPOOL

! PR := r6 ; jsr @r5 ; delay slot = H'FFFD (slot illegal instruction)
    PBEGIN p_jsr_ill
    sts     pr, r7
    lds     r6, pr
    PAT p_jsr_ill
    jsr     @r5
    .word   0xFFFD
    PREC
    lds     r7, pr
    rts
    nop
    PPOOL

! PR := r6 ; jsr @r5 ; delay slot mov.l @r4,r0 (may fault)
    PBEGIN p_jsr_ds2
    sts     pr, r7
    lds     r6, pr
    PAT p_jsr_ds2
    jsr     @r5
    mov.l   @r4, r0
    PREC
    lds     r7, pr
    rts
    nop
    PPOOL

! ---- MMUCR write (r5 -> @r4), n instructions, then a load from r6
    .macro MLAT n
    PBEGIN p_mlat\n
    mov.l   r5, @r4
    .rept \n
    nop
    .endr
    PAT p_mlat\n
    mov.l   @r6, r0
    PREC
    rts
    nop
    PPOOL
    .endm
    MLAT 0
    MLAT 1
    MLAT 2
    MLAT 3
    MLAT 4
    MLAT 5
    MLAT 6
    MLAT 7
    MLAT 8

! ---- MMUCR write, n instructions, then jsr @r6
    .macro MJMP n
    PBEGIN p_mjmp\n
    sts     pr, r7
    mov.l   r5, @r4
    .rept \n
    nop
    .endr
    PAT p_mjmp\n
    jsr     @r6
    nop
    PREC
    lds     r7, pr
    rts
    nop
    PPOOL
    .endm
    MJMP 0
    MJMP 1
    MJMP 2
    MJMP 3
    MJMP 4
    MJMP 5
    MJMP 6
    MJMP 7
    MJMP 8
    MJMP 9
    MJMP 10

! ---- LDTLB, n instructions, then a load from r6
    .macro LLAT n
    PBEGIN p_llat\n
    ldtlb
    .rept \n
    nop
    .endr
    PAT p_llat\n
    mov.l   @r6, r0
    PREC
    rts
    nop
    PPOOL
    .endm
    LLAT 0
    LLAT 1
    LLAT 2
    LLAT 3

! ---- UTLB address-array write (r5 -> @r4), n instructions, then a load from r6
    .macro ULAT n
    PBEGIN p_ulat\n
    mov.l   r5, @r4
    .rept \n
    nop
    .endr
    PAT p_ulat\n
    mov.l   @r6, r0
    PREC
    rts
    nop
    PPOOL
    .endm
    ULAT 0
    ULAT 1
    ULAT 2
    ULAT 3

! FMOV.S FR0,@r4
    PBEGIN p_stf32
    PAT p_stf32
    fmov.s  fr0, @r4
    PREC
    rts
    nop
    PPOOL
