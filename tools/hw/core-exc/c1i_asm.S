! c1i_asm.S -- core-exc probe c1i (WI-CORE-A step A2), run 9 of the C1 entry-timing series; derived from
! tools/hw/soc-design/c1h_asm.S (run 8).  rtl/v1/SOC.md 9.1 (the P rule's extent and forms, the EXC_SAVE tag's reach)
! and 9.16 (the UBC data break: take point, cost, the flag restore of the group after the access's).
!
! Loop form (c1h's): "90: nop ; A ; X ; Y ; dt r13 ; bf 90b", 90 at 0 mod 32, A = PMCTR2L read into r10, X = H'FFFD
! (ill), TRAPA #1 (trap) or "bra copy ; nop ; nop" (ref: a byte-identical copy of the handler reached by BRA, which
! returns to Y by RTE).  The handler returns to Y in every form.  Loop period L; E = L(X) - L(ref) + 2 (SOC.md A.1).
! Rows <y>_<h>: Y the instruction(s) after X, h the handler's first instruction(s), followed by HD (c1h's handler, with
! its record at one fixed address: B = PMCTR2L read, store B - A, skip, RTE; c1h's per-iteration array made the loop
! period depend on which array lines the operand cache still held):
!   Y:  ad  ADD #1,R8 (no access)       ld  MOV.L @R11,R8          lp  MOV.L @(disp,PC),R8     pf  PREF @R11
!       ob  OCBWB @R11                  fs  FMOV.S @R11,FR5        st  MOV.L R10,@R11
!       l2  ADD #1,R4 ; MOV.L @R11,R8   l3  ADD #1,R4 ; ADD #1,R4 ; MOV.L @R11,R8
!       p2  ADD #1,R4 ; MOV.L @(disp,PC),R8                        f2  ADD #1,R4 ; PREF @R11
!   h:  h0  none (c1h's handler: its first instruction is the P4 load B)
!       n   NOP                 spc STC SPC,R6        gbr STC GBR,R6        vbr STC VBR,R6        dbr STC DBR,R6
!       pr  STS PR,R6           gm  MOV.L @(0,GBR),R0 (GBR = the scratch words)
!       rts RTS ; NOP to one shared HD (PR set by the loop's setup)
! UBC rows (9.16): X = MOV.L @R9,R2 (R9 = the scratch word, 0x3F800000); the C side sets the UBC up around each call:
!   brk   channel B data break (BBRB operand read long, BDRB = 0x3F800000, BDMRB = 0, BRCR.DBEB)
!   op    channel A operand-address break (BBRA operand read, BRCR = 0)
!   uref  the UBC off: "MOV.L @R9,R2 ; bra copy ; nop ; nop"
!   Y:  un  NOP ; SETT ; NOP (the access pairs with the NOP: SETT leads the next group, its T written at the end of
!           EX before the access reaches S; the handler's SSR.T is 0 only if that write is undone or never made)
!       up  SETT (the access's follower candidate: in the access's group)
!   handler ssr: stores SSR and SPC (c1_ts[1], c1_ts[2]), then HD's B - A (c1_ts[0])
! Registers in the loops (bank 0): r4 filler, r8 Y's destination, r9 the scratch, r10 A, r11 c1_out, r12 PMCTR2L,
! r13 the count, r14 the caller's PR; the ref handler copy runs in bank 0 on r0-r3, r5-r7 (set by FSET).
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

    .macro HPRE h
    .ifc \h,n
    nop
    .endif
    .ifc \h,spc
    stc     spc, r6
    .endif
    .ifc \h,gbr
    stc     gbr, r6
    .endif
    .ifc \h,vbr
    stc     vbr, r6
    .endif
    .ifc \h,dbr
    stc     dbr, r6
    .endif
    .ifc \h,pr
    sts     pr, r6
    .endif
    .ifc \h,gm
    mov.l   @(0,gbr), r0
    .endif
    .ifc \h,ssr
    stc     ssr, r6
    mov.l   r6, @(4,r5)
    stc     spc, r6
    mov.l   r6, @(8,r5)
    .endif
    .endm

    .macro HND h
    .ifc \h,rts
    rts
    nop
    .else
    HPRE    \h
    HD
    .endif
    .endm

    .section .text.c1i,"ax"
    .balign 8192
    .globl _c1_base
_c1_base:
! c1_call(fn, vbr, skip, r5val): bank 1 r1 = PMCTR2L, r3 = budget, r5 = r5val, r6 = 0, r7 = skip; bank 0
! r13 = PMCTR2L
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
! c1_ubc_wait(): a read of BRCR (the UBC writes before it have completed), then 16 NOPs (the fetch-side copy settles)
    .globl _c1_ubc_wait
_c1_ubc_wait:
    mov.l   .Lbrcr, r0
    mov.w   @r0, r0
    .rept 16
    nop
    .endr
    rts
    nop
    .align 2
.Lctx:     .long _c1_ctx
.Lpmctr2:  .long 0xFF100010
.Lbudget:  .long _c1_budget
.Laborted: .long _c1_aborted
.Lbrcr:    .long 0xFF200020

! the handlers at VBR + 0x100: c1_h_<h> (VBR = c1_h_<h> - 0x100)
    .macro VH h
    .balign 256
    .globl _c1_h_\h
_c1_h_\h:
    HND     \h
    .endm
    VH      h0
    VH      n
    VH      spc
    VH      gbr
    VH      vbr
    VH      dbr
    VH      pr
    VH      gm
    VH      rts
    VH      ssr
! the body both RTS-first handlers return to
    .balign 32
    .globl _c1_hb_rts
_c1_hb_rts:
    HD

    .macro XI cls, hp
    .ifc \cls,ill
    .word   0xfffd
    .endif
    .ifc \cls,trap
    trapa   #1
    .endif
    .ifc \cls,ref
    bra     \hp
    nop
    nop
93:
    .endif
    .ifc \cls,brk
    mov.l   @r9, r2
    .endif
    .ifc \cls,op
    mov.l   @r9, r2
    .endif
    .ifc \cls,uref
    mov.l   @r9, r2
    bra     \hp
    nop
    nop
93:
    .endif
    .endm

    .macro FSET cls
    .ifc \cls,ref
    FREF
    .endif
    .ifc \cls,uref
    FREF
    .endif
    .rept 8
    nop
    .endr
    LDL     r13, _c1_n
    mov.l   @r13, r13
    LDL     r12, 0xFF100010
    .endm

    .macro FREF
    LDL     r1, 0xFF100010
    LDL     r5, _c1_ts
    mov     #0, r7
    LDL     r3, 0x7FFFFFFF
    stc     sr, r0
    ldc     r0, ssr
    LDL     r0, 93f
    ldc     r0, spc
    .endm

    .macro YI y
    .ifc \y,ad
    add     #1, r8
    .endif
    .ifc \y,ld
    mov.l   @r11, r8
    .endif
    .ifc \y,lp
    mov.l   71f, r8
    .endif
    .ifc \y,pf
    pref    @r11
    .endif
    .ifc \y,ob
    ocbwb   @r11
    .endif
    .ifc \y,fs
    fmov.s  @r11, fr5
    .endif
    .ifc \y,st
    mov.l   r10, @r11
    .endif
    .ifc \y,l2
    add     #1, r4
    mov.l   @r11, r8
    .endif
    .ifc \y,l3
    add     #1, r4
    add     #1, r4
    mov.l   @r11, r8
    .endif
    .ifc \y,p2
    add     #1, r4
    mov.l   71f, r8
    .endif
    .ifc \y,f2
    add     #1, r4
    pref    @r11
    .endif
    .ifc \y,un
    nop
    sett
    nop
    .endif
    .ifc \y,up
    sett
    .endif
    .endm

    .macro LY y, h, cls
    .balign 32
    .globl _ty_\y\()_\h\()_\cls
_ty_\y\()_\h\()_\cls:
    sts     pr, r14
    FSET    \cls
    .ifc \h,rts
    LDL     r0, _c1_hb_rts
    lds     r0, pr
    .endif
    LDL     r0, _c1_scratch
    ldc     r0, gbr
    LDL     r9, _c1_scratch
    LDL     r11, _c1_out
    clrt
    bra     90f
    nop
    .balign 32
90: nop
    mov.l   @r12, r10
    XI      \cls, _c1_hp_\y\()_\h
    YI      \y
    dt      r13
    bf      90b
    lds     r14, pr
    rts
    nop
    .align 2
71: .long   0x3F800000
    .endm

    .macro YSET y, h
    .balign 32
_c1_hp_\y\()_\h:
    HND     \h
    LY      \y, \h, ill
    LY      \y, \h, trap
    LY      \y, \h, ref
    .endm

    .macro USET y
    .balign 32
_c1_hp_\y\()_ssr:
    HND     ssr
    LY      \y, ssr, brk
    LY      \y, ssr, op
    LY      \y, ssr, uref
    .endm

    YSET    ad, h0
    YSET    ld, h0
    YSET    lp, h0
    YSET    pf, h0
    YSET    ob, h0
    YSET    fs, h0
    YSET    st, h0
    YSET    l2, h0
    YSET    l3, h0
    YSET    p2, h0
    YSET    f2, h0
    YSET    ad, n
    YSET    ld, n
    YSET    ad, spc
    YSET    ld, spc
    YSET    st, spc
    YSET    ad, gbr
    YSET    ad, vbr
    YSET    ad, dbr
    YSET    ad, pr
    YSET    ad, gm
    YSET    ad, rts
    USET    un
    USET    up
    .globl _c1_code_end
_c1_code_end:
