! c1d_asm.S -- soc-design probe C1, run 4 (rtl/v1/SOC.md): exception entry cost against the distance from an
! earlier on-chip register (P4) access.  Runs 1-3 gave E = 6 (D-stage) / 8 (commit-type) in loops but E = 8 / 10 in
! PMCTR brackets whose read A came a few cycles before the exception.  Here the handler has no P4 access at all:
!   HN: nop ; nop ; nop ; nop ; stc spc,r0 ; add r7,r0 ; ldc r0,spc ; dt r3 ; bt abort ; rte ; nop
! Loops (head 32-byte aligned):  "ACC ; K x add #1,r8 ; X ; dt r13 ; bf head"   (X = H'FFFD or a misaligned MOV.L)
! against the reference "ACC ; K x add ; bra copy ; nop ; nop ; dt ; bf" (same return alignment).
! ACC = p: PVR read (P4, Iclk block, 3 cycles)   c: cached load   w: BARA write of its own value (P4, Iclk).
! E(ACC, K) = L(X) - L(ref) + 2.
    .macro LDL reg, val
    mov.l   91f, \reg
    bra     92f
    nop
    .align 2
91: .long   \val
92:
    .endm


    .macro HN
    nop
    nop
    nop
    nop
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
8:  .long   _c1_abort
    .endm

    .section .text.c1d,"ax"
    .balign 8192
    .globl _c1_base
_c1_base:
! c1_call(fn, vbr, skip, r5val): bank 1 r1 = r4 = PMCTR2L, r3 = budget, r5 = r5val, r6 = 0, r7 = skip; bank 0
! r13 = PMCTR2L
    .balign 32
    .globl _c1_call
_c1_call:
_c1_call_body:
    mov.l   .Lctx, r0
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
    mov.l   .Lpmctr2, r1
    ldc     r1, r1_bank
    mov.l   .Lbudget, r1
    mov.l   @r1, r1
    ldc     r1, r3_bank
    mov     #0, r1
    ldc     r1, r6_bank
    ldc     r6, r7_bank
    ldc     r7, r5_bank
    mov.l   .Lpmctr2, r13
    ldc     r5, vbr
    jsr     @r4
    nop
_c1_back:
    stc     r3_bank, r1
    mov.l   .Lbudget, r2
    mov.l   r1, @r2
    mov.l   .Lctx, r0
    mov.l   @(44,r0), r1
    ldc     r1, sr
    mov.l   @(48,r0), r1
    lds     r1, fpscr
    mov.l   @(52,r0), r1
    lds     r1, fpul
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
    .globl _c1_abort
_c1_abort:
    mov.l   .Lctx, r0
    mov.l   @(44,r0), r1
    ldc     r1, sr
    mov.l   .Laborted, r1
    mov     #1, r2
    mov.l   r2, @r1
    bra     _c1_back
    nop
    .align 2
.Lctx:     .long _c1_ctx
.Lpmctr2:  .long 0xFF100010
.Lbudget:  .long _c1_budget
.Laborted: .long _c1_aborted


    .org 0x400
    .globl _c1_vt
_c1_vt:
    .org 0x500
    HN
    .balign 32
    .globl _c1_hp
_c1_hp:
    HN

    ! r10 = the access address (PVR / scratch / BARA), r12 = BARA's value (for the write)
    .macro ACC a
    .ifc \a,p
    mov.l   @r10, r11
    .endif
    .ifc \a,c
    mov.l   @r10, r11
    .endif
    .ifc \a,w
    mov.l   r12, @r10
    .endif
    .endm

    .macro LSETUP a, cls
    .ifc \a,p
    LDL     r10, 0xFF000030
    .endif
    .ifc \a,c
    LDL     r10, _c1_scratch
    .endif
    .ifc \a,w
    LDL     r10, 0xFF200000
    mov.l   @r10, r12
    .endif
    .ifc \cls,dae
    LDL     r9, _c1_scratch+1
    .endif
    .ifc \cls,ref
    mov     #0, r7
    LDL     r3, 0x7FFFFFFF
    stc     sr, r0
    ldc     r0, ssr
    LDL     r0, 93f
    ldc     r0, spc
    .endif
    .rept 8
    nop
    .endr
    .endm

    .macro LOOP a, k, cls
    .balign 32
    .globl _tl_\a\k\()_\cls
_tl_\a\k\()_\cls:
    sts     pr, r14
    LSETUP  \a, \cls
    LDL     r13, _c1_n
    mov.l   @r13, r13
    bra     90f
    nop
    .balign 32
90: ACC     \a
    .rept \k
    add     #1, r8
    .endr
    .ifc \cls,ill
    .word   0xfffd
    .endif
    .ifc \cls,dae
    mov.l   @r9, r11
    .endif
    .ifc \cls,ref
    bra     _c1_hp_\a\k
    nop
    nop
93:
    .endif
    dt      r13
    bf      90b
    lds     r14, pr
    rts
    nop
    .endm

    .macro KSET a, k
    .balign 32
_c1_hp_\a\k:
    HN
    LOOP \a, \k, ill
    LOOP \a, \k, dae
    LOOP \a, \k, ref
    .endm

    KSET p, 0
    KSET p, 1
    KSET p, 2
    KSET p, 3
    KSET p, 4
    KSET p, 5
    KSET p, 6
    KSET p, 8
    KSET p, 10
    KSET c, 0
    KSET c, 2
    KSET c, 4
    KSET w, 0
    KSET w, 2
    KSET w, 4
    .globl _c1_code_end
_c1_code_end:
