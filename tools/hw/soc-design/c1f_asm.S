! c1f_asm.S -- soc-design probe C1, run 6 (rtl/v1/SOC.md): PMCTR brackets taken inside steady-state loops.
! Runs 1-5: loop periods give E = 6 / 8 (D-stage / commit-type) in every configuration, single-shot PMCTR brackets give
! E = 8 / 10 in most forms.  Here the bracket is taken on every iteration of a loop: A = PMCTR2L read into r10 (not
! banked, so the handler sees it), handler HD = "B: mov.l @r1,r2 ; sub r10,r2 ; mov.l r2,@r5 ; nop ; stc spc ..."
! stores B - A.  The same loop gives the period.  Reference loops reach a byte-identical copy by BRA.
!   f0: A at the head (0 mod 32), X right after it (same fetch word)
!   f2: NOP ; A (odd halfword) ; X (next word)
!   fb: A ; 3 NOPs ; BRA to a 32-byte-aligned X (the single-shot b3 form)
!   rK: 2K NOPs ; X   (distance from the previous RTE return; no A), K = 0 2 4 8
    .macro LDL reg, val
    mov.l   91f, \reg
    bra     92f
    nop
    .align 2
91: .long   \val
92:
    .endm



    .macro HD
    mov.l   @r1, r2
    sub     r10, r2
    mov.l   r2, @r5
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

    .section .text.c1f,"ax"
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
    HD

    .macro XI cls, hp
    .ifc \cls,ill
    .word   0xfffd
    .endif
    .ifc \cls,dae
    mov.l   @r9, r11
    .endif
    .ifc \cls,ref
    bra     \hp
    nop
    nop
93:
    .endif
    .endm

    .macro FSET cls, hp
    .ifc \cls,dae
    LDL     r9, _c1_scratch+1
    .endif
    .ifc \cls,ref
    LDL     r1, 0xFF100010
    LDL     r5, _c1_ts
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
    LDL     r13, _c1_n
    mov.l   @r13, r13
    LDL     r12, 0xFF100010
    .endm

    .macro LF form, cls, hp
    .balign 32
    .globl _tf_\form\()_\cls
_tf_\form\()_\cls:
    sts     pr, r14
    FSET    \cls, \hp
    bra     90f
    nop
    .balign 32
90:
    .ifc \form,f0
    mov.l   @r12, r10
    XI      \cls, \hp
    .endif
    .ifc \form,f2
    nop
    mov.l   @r12, r10
    XI      \cls, \hp
    .endif
    .ifc \form,fb
    mov.l   @r12, r10
    nop
    nop
    nop
    bra     81f
    nop
    .balign 32
81: XI      \cls, \hp
    .endif
    .ifc \form,r0
    XI      \cls, \hp
    .endif
    .ifc \form,r2
    .rept 4
    nop
    .endr
    XI      \cls, \hp
    .endif
    .ifc \form,r4
    .rept 8
    nop
    .endr
    XI      \cls, \hp
    .endif
    .ifc \form,r8
    .rept 16
    nop
    .endr
    XI      \cls, \hp
    .endif
    dt      r13
    bf      90b
    lds     r14, pr
    rts
    nop
    .endm

    .macro FORM form
    .balign 32
_c1_hp_\form:
    HD
    LF      \form, ill, _c1_hp_\form
    LF      \form, dae, _c1_hp_\form
    LF      \form, ref, _c1_hp_\form
    .endm

    FORM f0
    FORM f2
    FORM fb
    FORM r0
    FORM r2
    FORM r4
    FORM r8
    .globl _c1_code_end
_c1_code_end:
