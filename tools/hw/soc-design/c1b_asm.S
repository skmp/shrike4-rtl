! c1b_asm.S -- soc-design probe C1, run 2 (rtl/v1/SOC.md): handler-shape dependence of the exception entry cost.
! Run 1 (c1_asm.S) gave loop E = 6 / 8 (D-stage / commit-type) but PMCTR-bracket E = 8 / 10.  This run times five
! handler shapes, each through an exception (vector table VTv, handler at VBR + 0x100) and through a byte-identical
! copy reached by BRA (reference):
!   H0: B ; st ; TAIL          (B = mov.l @r1,r2: PMCTR2L time stamp)          H1: nop x4 ; B ; st ; TAIL
!   H2: TAIL (ci's lean handler, STC SPC first)                                H3: mov.l @r4,r6 ; mov.l r6,@r5 ; TAIL
!   H4: stc spc,r0 ; B ; st ; add r7,r0 ; ldc r0,spc ; dt r3 ; bt ; rte ; nop
!   TAIL = stc spc,r0 ; add r7,r0 ; ldc r0,spc ; dt r3 ; bt abort ; rte ; nop
! Loops: "X ; dt ; bf" (X = H'FFFD / misaligned MOV.L / TRAPA / FADD EN.I) against "BRA copy ; NOP ; NOP ; dt ; bf".
! Brackets (form b3: A = PMCTR2L read, 2 NOP pairs, BRA to a 32-byte-aligned X): B - A.
! Direct forms (A right before X): A as a BRA target at 0 / 2 mod 4, and A as the JSR target itself (excint's shape).
! Results are stored and printed after all runs (printing between runs evicted lines in run 1).

    .macro LDL reg, val
    mov.l   91f, \reg
    bra     92f
    nop
    .align 2
91: .long   \val
92:
    .endm

    .macro HTAIL
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

    .macro HVAR v
    .if \v == 0
    mov.l   @r1, r2
    mov.l   r2, @r5
    HTAIL
    .endif
    .if \v == 1
    nop
    nop
    nop
    nop
    mov.l   @r1, r2
    mov.l   r2, @r5
    HTAIL
    .endif
    .if \v == 2
    HTAIL
    .endif
    .if \v == 3
    mov.l   @r4, r6
    mov.l   r6, @r5
    HTAIL
    .endif
    .if \v == 4
    stc     spc, r0
    mov.l   @r1, r2
    mov.l   r2, @r5
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
    .endif
    .endm

    .section .text.c1b,"ax"
    .balign 8192
    .globl _c1_base
_c1_base:
    bra     _c1_call_body           ! (never executed; keeps the trampoline below 0x400)
    nop
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
    ldc     r1, r4_bank
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
    .globl _c1_vt0, _c1_vt1, _c1_vt2, _c1_vt3, _c1_vt4
_c1_vt0:
    .org 0x500
    HVAR 0
    .org 0x800
_c1_vt1:
    .org 0x900
    HVAR 1
    .org 0xC00
_c1_vt2:
    .org 0xD00
    HVAR 2
    .org 0x1000
_c1_vt3:
    .org 0x1100
    HVAR 3
    .org 0x1400
_c1_vt4:
    .org 0x1500
    HVAR 4

    .macro REFSETUP res
    LDL     r1, 0xFF100010
    mov     r1, r4
    LDL     r3, 0x7FFFFFFF
    LDL     r5, _c1_ts
    mov     #0, r7
    stc     sr, r0
    ldc     r0, ssr
    LDL     r0, \res
    ldc     r0, spc
    .endm

    .macro CLSSETUP cls, res
    .ifc \cls,fpe
    sts     fpscr, r8
    fldi1   fr4
    fldi1   fr5
    LDL     r2, 0x00040080
    lds     r2, fpscr
    .endif
    .ifc \cls,dae
    LDL     r9, _c1_scratch+1
    .endif
    .ifc \cls,ref
    REFSETUP \res
    .endif
    .rept 8
    nop
    .endr
    .endm

    .macro CLSDONE cls
    .ifc \cls,fpe
    lds     r8, fpscr
    .endif
    .endm

    .macro XINS cls, v
    .ifc \cls,ill
    .word   0xfffd
    .endif
    .ifc \cls,fpe
    fadd    fr4, fr5
    .endif
    .ifc \cls,dae
    mov.l   @r9, r11
    .endif
    .ifc \cls,trap
    trapa   #1
    .endif
    .ifc \cls,ref
    bra     _c1_hp\v
    nop
    .endif
    .endm

    .macro TLOOP v, cls
    .balign 32
    .globl _tl_v\v\()_\cls
_tl_v\v\()_\cls:
    sts     pr, r14
    CLSSETUP \cls, 93f
    LDL     r13, _c1_n
    mov.l   @r13, r13
    bra     90f
    nop
    .balign 32
90:
    XINS    \cls, \v
    .ifc \cls,ref
    nop
93:
    .endif
    dt      r13
    bf      90b
    CLSDONE \cls
    lds     r14, pr
    rts
    nop
    .endm

    .macro BB3 v, cls
    .balign 32
    .globl _tb_v\v\()_\cls
_tb_v\v\()_\cls:
    sts     pr, r14
    LDL     r12, _c1_out
    CLSSETUP \cls, 93f
    bra     80f
    nop
    .balign 32
80: mov.l   @r13, r10
    nop
    nop
    nop
    bra     81f
    nop
    .balign 32
81: XINS    \cls, \v
93: mov.l   r10, @r12
    CLSDONE \cls
    lds     r14, pr
    rts
    nop
    .endm

    ! direct: A as a BRA target at \al mod 4, X right after it
    .macro BD v, al, cls
    .balign 32
    .globl _td_v\v\()_\al\()_\cls
_td_v\v\()_\al\()_\cls:
    sts     pr, r14
    LDL     r12, _c1_out
    CLSSETUP \cls, 93f
    bra     80f
    nop
    .balign 32
    .if \al == 2
    nop
    .endif
80: mov.l   @r13, r10
    XINS    \cls, \v
93: mov.l   r10, @r12
    CLSDONE \cls
    lds     r14, pr
    rts
    nop
    .endm

    ! jsr target: the function starts with A (excint's t_cost_*); A's value goes to c1_out[0] after the return
    .macro BJ v, al, cls
    .balign 32
    .if \al == 2
    nop
    .endif
    .globl _tj_v\v\()_\al\()_\cls
_tj_v\v\()_\al\()_\cls:
    mov.l   @r13, r10
    XINS    \cls, \v
    mov.l   91f, r0
    rts
    mov.l   r10, @r0
    .align 2
91: .long   _c1_out
    .endm

    .macro VARSET v
    .balign 32
    .globl _c1_hp\v
_c1_hp\v:
    HVAR \v
    TLOOP \v, ill
    TLOOP \v, dae
    TLOOP \v, ref
    .ifne \v-2
    BB3 \v, ill
    BB3 \v, dae
    BB3 \v, ref
    .endif
    .endm

    VARSET 0
    BD 0, 0, ill
    BD 0, 2, ill
    VARSET 1
    VARSET 2
    TLOOP 2, trap
    TLOOP 2, fpe
    VARSET 3
    BD 3, 0, ill
    BD 3, 2, ill
    BD 3, 0, ref
    BD 3, 2, ref
    BJ 3, 0, ill
    BJ 3, 2, ill
    BJ 3, 0, trap
    BJ 3, 2, trap
    VARSET 4
    .globl _c1_code_end
_c1_code_end:
