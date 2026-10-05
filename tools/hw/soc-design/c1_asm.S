! c1_asm.S -- soc-design probe C1 (rtl/v1/SOC.md): exception entry timing of four exception classes, measured
! both as loop periods and as PMCTR brackets in one program with one handler, and the point at which a P4 PMCTR
! read samples the counter.  Driven by c1.c.
!
! Everything that runs during a measurement (trampoline, vector table, handler copy, tests) lives in one 8 KB-aligned
! section smaller than 8 KB, so no two of these lines share an instruction-cache index.
!
! c1_call(fn, vbr, skip, r5val) saves the C context, loads the bank-1 registers the handlers use, sets bank-0
! r13 = PMCTR2L, switches VBR, calls fn and restores everything.  Tests run with IMASK 15, BL 0, MD 1, RB 0.
! Bank-1 registers for the lean handler:  r1 = 0xFF100010 (PMCTR2L, PRFC1 = elapsed CPU cycles)
!   r3 = exception budget (0 -> abort to C)   r5 = where to store the entry time stamp   r7 = bytes added to SPC
! The lean handler H (VBR + 0x100 / 0x400 / 0x600) and its byte-identical copy _c1_hp (reached by BRA, bank 0, for
! the reference path) are:  B: mov.l @r1,r2 ; mov.l r2,@r5 ; stc spc,r0 ; add r7,r0 ; ldc r0,spc ; dt r3 ; bt abort ;
! rte ; nop.

    .macro LDL reg, val
    mov.l   91f, \reg
    bra     92f
    nop
    .align 2
91: .long   \val
92:
    .endm

    .macro HANDLER
    mov.l   @r1, r2             ! B: entry time stamp (first handler instruction)
    mov.l   r2, @r5
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

    .section .text.c1,"ax"
    .balign 8192
! =====================================================================================================================
! vector table (lean handler at +0x100 / +0x400 / +0x600) and the reference copy
    .globl _c1_vt
_c1_vt:
    .org 0x100
    HANDLER
    .org 0x400
    HANDLER
    .org 0x600
    HANDLER

! =====================================================================================================================
! vector table F: replica of ci2's recording handler (rtl/v1/spec/core-integration.md run 2), bank-1 r1 = PMCTR2L,
! r3 budget, r5 record pointer (64-byte records, time stamp at +0), r6 = 0, r7 skip.
    .org 0x800                  ! section offsets: the section starts 8 KB-aligned
    .globl _c1_vf
_c1_vf:
    .org 0x900
    mov.l   @r1, r2
    bra     rec_common
    mov     #1, r0
    .org 0xC00
    mov.l   @r1, r2
    bra     rec_common
    mov     #4, r0
    .org 0xE00
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
.Labort_f: .long _c1_abort

! =====================================================================================================================
! c1_call(fn, vbr, skip, r5val) and the abort path
    .balign 32
    .globl _c1_call
_c1_call:
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
    sts     mach, r1
    mov.l   r1, @(56,r0)
    sts     macl, r1
    mov.l   r1, @(60,r0)
    mov.l   .Lpmctr2, r1
    ldc     r1, r1_bank
    mov.l   .Lbudget, r1
    mov.l   @r1, r1
    ldc     r1, r3_bank
    mov     #0, r1
    ldc     r1, r4_bank
    ldc     r1, r6_bank
    ldc     r6, r7_bank
    ldc     r7, r5_bank
    mov.l   .Lpmctr2, r13
    ldc     r5, vbr
    jsr     @r4
    nop
_c1_back:
    stc     r5_bank, r1
    mov.l   .Lr5out, r2
    mov.l   r1, @r2
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
.Lr5out:   .long _c1_r5out
.Laborted: .long _c1_aborted

! =====================================================================================================================
! reference-path setup: bank-0 r1 / r3 / r5 / r7 as the handler expects, SSR = SR, SPC = the resume label
    .macro REFSETUP res
    LDL     r1, 0xFF100010
    LDL     r3, 0x7FFFFFFF
    LDL     r5, _c1_ts
    mov     #0, r7
    stc     sr, r0
    ldc     r0, ssr
    LDL     r0, \res
    ldc     r0, spc
    .endm

    .macro FPEON
    sts     fpscr, r8
    fldi1   fr4
    fldi1   fr5
    LDL     r2, 0x00040080          ! EN.I, DN
    lds     r2, fpscr
    .endm

    .macro CLSSETUP cls, res
    .ifc \cls,fpe
    FPEON
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

    .macro XINS cls
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
    bra     _c1_hp
    nop
    .endif
    .endm

! =====================================================================================================================
! loops: X at a 32-byte-aligned loop head (a branch target), "X ; dt r13 ; bf head"; the handler returns to X + 2.
! ref: "bra _c1_hp ; nop ; nop ; dt r13 ; bf head" with SPC = the dt (same 2-mod-4 return alignment as X + 2).
    .macro TLOOP cls
    .balign 32
    .globl _tl_\cls
_tl_\cls:
    sts     pr, r14
    CLSSETUP \cls, 93f
    LDL     r13, _c1_n
    mov.l   @r13, r13
    bra     90f
    nop
    .balign 32
90:
    XINS    \cls
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

    TLOOP ill
    TLOOP fpe
    TLOOP dae
    TLOOP trap


    .balign 32
    .globl _tl_nop
_tl_nop:
    sts     pr, r14
    LDL     r13, _c1_n
    mov.l   @r13, r13
    bra     90f
    nop
    .balign 32
90: nop
    dt      r13
    bf      90b
    lds     r14, pr
    rts
    nop

! =====================================================================================================================
! brackets.  A = mov.l @r13,r10 (PMCTR2L), stored to c1_out[0]; the handler stores B to c1_ts.
! direct form "d": A at 32k+2 (a branch target at an odd halfword, as in excint / ci2), X right after it.
! BRA form "bK": A at 32k (branch target), K NOPs, BRA to a 32-byte-aligned X (X is a branch target: a leader).
! two-pass form "p": the direct form run twice in a loop (as ci2), c1_out[1] = the second pass's A.
    .macro BDIRECT cls
    .balign 32
    .globl _tb_d_\cls
_tb_d_\cls:
    sts     pr, r14
    LDL     r12, _c1_out
    CLSSETUP \cls, 93f
    bra     80f
    nop
    .balign 32
    nop
80: mov.l   @r13, r10
    XINS    \cls
93: mov.l   r10, @r12
    CLSDONE \cls
    lds     r14, pr
    rts
    nop
    .endm

    .macro BBRA cls, k
    .balign 32
    .globl _tb_b\k\()_\cls
_tb_b\k\()_\cls:
    sts     pr, r14
    LDL     r12, _c1_out
    CLSSETUP \cls, 93f
    bra     80f
    nop
    .balign 32
80: mov.l   @r13, r10
    .rept \k
    nop
    .endr
    bra     81f
    nop
    .balign 32
81: XINS    \cls
93: mov.l   r10, @r12
    CLSDONE \cls
    lds     r14, pr
    rts
    nop
    .endm

    .macro BPASS cls
    .balign 32
    .globl _tb_p_\cls
_tb_p_\cls:
    sts     pr, r14
    LDL     r12, _c1_out
    CLSSETUP \cls, 93f
    mov     #2, r8
    bra     80f
    nop
    .balign 32
    nop
80: mov.l   @r13, r10
    XINS    \cls
93: mov.l   r10, @r12
    add     #4, r12
    dt      r8
    bf      80b
    CLSDONE \cls
    lds     r14, pr
    rts
    nop
    .endm

    .macro BALL cls
    BDIRECT \cls
    BBRA    \cls, 0
    BBRA    \cls, 1
    BBRA    \cls, 2
    BBRA    \cls, 3
    BBRA    \cls, 4
    BBRA    \cls, 6
    .endm

    BALL ill
    BALL fpe
    BALL dae
    BALL trap

! the reference handler copy sits next to the reference tests (BRA reaches +-4 KB)
    .balign 32
    .globl _c1_hp
_c1_hp:
    HANDLER

    TLOOP ref
    BALL ref
    BPASS ill
    BPASS trap

! =====================================================================================================================
! PMCTR sample point.  PRFC0 (PMCR1) is set by C to event 0x13 (issue groups) or 0x23 (cycles); A1 / B1 read
! PMCTR1L (r13 = 0xFF100008 here).  r9 = aligned cached scratch.  c1_out[2] = B1 - A1 (and [3] = C1 - B1 where used).
    .macro SFUNC name
    .balign 32
    .globl _ts_\name
_ts_\name:
    sts     pr, r14
    LDL     r12, _c1_out
    LDL     r13, 0xFF100008
    LDL     r9, _c1_scratch
    mov.l   @r9, r0             ! scratch line in the operand cache
    bra     80f
    nop
    .balign 32
80:
    .endm

    .macro SEND
    sub     r10, r11
    mov.l   r11, @(8,r12)
    lds     r14, pr
    rts
    nop
    .endm

    SFUNC ab                    ! A1 ; B1
    mov.l   @r13, r10
    mov.l   @r13, r11
    nop
    nop
    nop
    nop
    SEND

    SFUNC llb_i                 ! A1 ; L ; L ; B1 ; Li ; L
    mov.l   @r13, r10
    mov.l   @r9, r1
    mov.l   @r9, r2
    mov.l   @r13, r11
    mov.l   @r9, r3
    mov.l   @r9, r4
    nop
    nop
    SEND

    SFUNC llb_d                 ! A1 ; L ; L ; B1 ; Ld (uses B1) ; L
    mov.l   @r13, r10
    mov.l   @r9, r1
    mov.l   @r9, r2
    mov.l   @r13, r11
    mov     r11, r3
    mov.l   @r9, r4
    nop
    nop
    SEND

    SFUNC dlb_i                 ! A1 ; Ld (uses A1) ; L ; B1 ; Li ; L
    mov.l   @r13, r10
    mov     r10, r1
    mov.l   @r9, r2
    mov.l   @r13, r11
    mov.l   @r9, r3
    mov.l   @r9, r4
    nop
    nop
    SEND

    SFUNC nnb                   ! A1 ; 3 x NOP ; B1
    mov.l   @r13, r10
    nop
    nop
    nop
    mov.l   @r13, r11
    nop
    nop
    SEND

    SFUNC eeb                   ! A1 ; 4 x ADD (EX) ; B1
    mov.l   @r13, r10
    add     #1, r1
    add     #1, r1
    add     #1, r1
    add     #1, r1
    mov.l   @r13, r11
    nop
    nop
    SEND

! =====================================================================================================================
! P4 write posting seen by a later PMCTR read (PMCTR2L, r13 set by c1_call):  A ; W ; B ; C  -> out[0] = B - A,
! out[1] = C - B.  W = 16-bit IPRA write of its own value (Pclk) / 32-bit BARA write of its own value (Iclk).
    .macro WFUNC name, reg, wr, gap
    .balign 32
    .globl _tw_\name
_tw_\name:
    sts     pr, r14
    LDL     r12, _c1_out
    LDL     r9, \reg
    .ifc \wr,w
    mov.w   @r9, r8
    .else
    mov.l   @r9, r8
    .endif
    .rept 8
    nop
    .endr
    bra     80f
    nop
    .balign 32
80: mov.l   @r13, r10
    .ifc \wr,w
    mov.w   r8, @r9
    .else
    mov.l   r8, @r9
    .endif
    .rept \gap
    nop
    .endr
    mov.l   @r13, r11
    mov.l   @r13, r1
    sub     r11, r1
    sub     r10, r11
    mov.l   r11, @(0,r12)
    mov.l   r1, @(4,r12)
    lds     r14, pr
    rts
    nop
    .endm

    WFUNC ipra0, 0xFFD00004, w, 0
    WFUNC ipra4, 0xFFD00004, w, 4
    WFUNC ipra8, 0xFFD00004, w, 8
    WFUNC ipra16, 0xFFD00004, w, 16
    WFUNC bara0, 0xFF200000, l, 0
    WFUNC bara4, 0xFF200000, l, 4

    .globl _c1_code_end
_c1_code_end:
