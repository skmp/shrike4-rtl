! tc2_asm.S -- tools/hw/timers-clock/tc2_asm.S unchanged, for the bare-metal tc2.c (tb/bm/timers-clock).  Run 2 (tc2.c): tc_asm.S with the interrupt handler reading TCNT1 as
! its FIRST action (rec[9]) and PMCTR0L second, plus single-access latency routines lat1_*.
!
! uint32_t lat_none(uint32_t a)            PMCTR0L read, PMCTR0L read: cycles between them
! uint32_t lat_rd32 / lat_rd16 / lat_rd8(uint32_t a)       16 back-to-back loads from a, bracketed by PMCTR reads
! uint32_t lat_wr32 / lat_wr16 / lat_wr8(uint32_t a, uint32_t v)   16 back-to-back stores of v to a
! uint32_t lat_wr16rd16(uint32_t a, uint32_t v)            8 x (store.w v -> a ; load.w a)
!
! uint32_t tc_irq_trial(uint32_t *rec, uint32_t mode, uint32_t tstr)
!   Caller: interrupts masked (IMASK 15), SR.RB = 0, IPRA = TMU1 only, TMU1 stopped and configured.
!   Installs tc_vbr, lowers IMASK to 14 (modes 0, 1) or keeps 15 (mode 2), reads PMCTR0L (rec[4]), writes TSTR = tstr
!   (starts TMU1) and then:  mode 0 spins until the handler sets rec[0];  mode 1 executes SLEEP;  mode 2 polls TCR1.UNF.
!   rec: [0] flag (1 irq, 2 irq with SPC at SLEEP (skipped), 3 exception, 4 poll hit)  [1] PMCTR0L at handler entry /
!   poll hit  [2] SPC  [3] INTEVT  [4] PMCTR0L before the TSTR write  [5] PMCTR0L after SLEEP returned  [6] EXPEVT
!   [7] SSR  [8] loop counter left  [9] TCNT1 read in the handler  [10] address of the SLEEP instruction
!   The handler clears TCR1 (UNIE = 0), sets SSR.IMASK = 15 (so nothing can re-enter) and skips a SLEEP at SPC, so no
!   outcome can hang the console.  Any exception goes to tc_bail with the caller's SR.

    .text
    .align 2
    .globl _l2_none, _l2_rd32, _l2_rd16, _l2_rd8, _l2_wr32, _l2_wr16, _l2_wr8, _l2_wr16rd16
    .globl _tc2_irq_trial, _tc2_vbr

_l2_none:
    mov.l   .Lpm, r1
    mov.l   @r1, r2
    mov.l   @r1, r3
    sub     r2, r3
    rts
    mov     r3, r0

_l2_rd32:
    mov.l   .Lpm, r1
    mov.l   @r1, r2
    .rept 16
    mov.l   @r4, r0
    .endr
    mov.l   @r1, r3
    sub     r2, r3
    rts
    mov     r3, r0

_l2_rd16:
    mov.l   .Lpm, r1
    mov.l   @r1, r2
    .rept 16
    mov.w   @r4, r0
    .endr
    mov.l   @r1, r3
    sub     r2, r3
    rts
    mov     r3, r0

_l2_rd8:
    mov.l   .Lpm, r1
    mov.l   @r1, r2
    .rept 16
    mov.b   @r4, r0
    .endr
    mov.l   @r1, r3
    sub     r2, r3
    rts
    mov     r3, r0

_l2_wr32:
    mov.l   .Lpm, r1
    mov.l   @r1, r2
    .rept 16
    mov.l   r5, @r4
    .endr
    mov.l   @r1, r3
    sub     r2, r3
    rts
    mov     r3, r0

_l2_wr16:
    mov.l   .Lpm, r1
    mov.l   @r1, r2
    .rept 16
    mov.w   r5, @r4
    .endr
    mov.l   @r1, r3
    sub     r2, r3
    rts
    mov     r3, r0

_l2_wr8:
    mov.l   .Lpm, r1
    mov.l   @r1, r2
    .rept 16
    mov.b   r5, @r4
    .endr
    mov.l   @r1, r3
    sub     r2, r3
    rts
    mov     r3, r0

_l2_wr16rd16:
    mov.l   .Lpm, r1
    mov.l   @r1, r2
    .rept 8
    mov.w   r5, @r4
    mov.w   @r4, r0
    .endr
    mov.l   @r1, r3
    sub     r2, r3
    rts
    mov     r3, r0

    .align 2
.Lpm:   .long 0xFF100008

! ---------------------------------------------------------------------------------------------------------------------
_tc2_irq_trial:
    mov.l   r8, @-r15
    mov.l   r9, @-r15
    mov.l   r10, @-r15
    mov.l   r11, @-r15
    stc     vbr, r8             ! r8 = caller's VBR (r8-r15 are not banked: the handlers see them)
    stc     sr, r9              ! r9 = caller's SR (IMASK 15)
    mov.l   .Lvbr, r0
    ldc     r0, vbr
    mov     r9, r10
    mov.l   .Limclr, r0
    and     r0, r10             ! IMASK = 0
    mov     #2, r0
    cmp/eq  r0, r5
    bt      1f
    mov     #0x70, r0
    shll    r0                  ! 0xE0: IMASK 14
    bra     2f
    or      r0, r10
1:  mov     #0x78, r0
    shll    r0                  ! 0xF0: IMASK 15 (poll mode)
    or      r0, r10
2:  mov.l   .Lpm2, r11          ! PMCTR0L
    mov.l   .Ltstr, r1
    mov.l   .Ltcr1, r2
    mov.l   .Lloops, r3
    mova    .Lsleep_insn, r0
    mov.l   r0, @(40,r4)
    ldc     r10, sr             ! from here on TMU1 (priority 15) can interrupt in modes 0 and 1
    mov.l   @r11, r0
    mov.b   r6, @r1             ! TSTR: start TMU1
    mov.l   r0, @(16,r4)
    mov     #1, r0
    cmp/eq  r0, r5
    bt      .Lmsleep
    mov     #2, r0
    cmp/eq  r0, r5
    bt      .Lmpoll
.Lmspin:
    mov.l   @r4, r0
    tst     r0, r0
    bf      .Ldone
    dt      r3
    bf      .Lmspin
    bra     .Ldone
    nop
    .align 2
.Lsleep_insn:
.Lmsleep:
    sleep
    mov.l   @r11, r0
    mov.l   r0, @(20,r4)
    bra     .Ldone
    nop
.Lmpoll:
    mov.w   @r2, r0
    shlr8   r0
    tst     #1, r0
    bf      .Lphit
    dt      r3
    bf      .Lmpoll
    bra     .Ldone
    nop
.Lphit:
    mov     r2, r0
    add     #-4, r0
    mov.l   @r0, r0             ! TCNT1 right after UNF was seen
    mov.l   r0, @(36,r4)
    mov.l   @r11, r0
    mov.l   r0, @(4,r4)
    mov     #4, r0
    mov.l   r0, @r4
    bra     .Ldone
    nop
    .globl _tc2_bail
_tc2_bail:
.Ldone:
    mov.l   r3, @(32,r4)
    ldc     r9, sr              ! IMASK 15 again, RB 0
    ldc     r8, vbr
    mov.l   @r15+, r11
    mov.l   @r15+, r10
    mov.l   @r15+, r9
    mov.l   @r15+, r8
    mov.l   @r4, r0
    rts
    nop

    .align 2
.Lvbr:   .long _tc2_vbr
.Limclr: .long 0xFFFFFF0F
.Lpm2:   .long 0xFF100008
.Ltstr:  .long 0xFFD80004
.Ltcr1:  .long 0xFFD8001C
.Lloops: .long 1000000

! ---------------------------------------------------------------------------------------------------------------------
! Private vector table.  Handlers run with SR.RB = 1, so r0-r7 here are bank 1 (KOS treats bank 1 as scratch);
! rec is the caller's r4 (bank 0), read with STC R4_BANK.
    .section .text.tc2vbr, "ax"
    .balign 1024
_tc2_vbr:
    .org 0x100
    nop                         ! (no branch as the first instruction at VBR + 0x100 / 0x400 / 0x600)
.Lexc:
    stc     r4_bank, r2
    mov.l   .Lexpevt, r0
    mov.l   @r0, r1
    mov.l   r1, @(24,r2)
    stc     spc, r1
    mov.l   r1, @(8,r2)
    mov     #3, r1
    mov.l   r1, @r2
    ldc     r9, ssr             ! back to the caller's SR (IMASK 15, RB 0)
    mov.l   .Lbail, r0
    ldc     r0, spc
    rte
    nop
    .align 2
.Lexpevt: .long 0xFF000024
.Lbail:   .long _tc2_bail

    .org 0x400
    nop
    bra     .Lexc
    nop

    .org 0x600
    mov.l   .Ltcnt1h, r0
    mov.l   @r0, r3             ! TCNT1 first
    mov.l   .Lpm3, r0
    mov.l   @r0, r1             ! then PMCTR0L
    stc     r4_bank, r2
    mov.l   r3, @(36,r2)
    mov.l   r1, @(4,r2)
    stc     spc, r1
    mov.l   r1, @(8,r2)
    mov.l   .Lintevt, r0
    mov.l   @r0, r1
    mov.l   r1, @(12,r2)
    stc     ssr, r1
    mov.l   r1, @(28,r2)
    mov.l   .Ltcr1h, r0
    mov     #0, r1
    mov.w   r1, @r0             ! TCR1 = 0: UNF and UNIE cleared
    mov.w   @r0, r1             ! read back: the request is gone before RTE
    stc     ssr, r1
    mov     #0x78, r0
    shll    r0
    or      r0, r1
    ldc     r1, ssr             ! return with IMASK 15
    stc     spc, r3
    mov.w   @r3, r0
    extu.w  r0, r0
    mov     #0x1B, r1
    cmp/eq  r1, r0
    bf      1f
    add     #2, r3
    ldc     r3, spc             ! SPC pointed at SLEEP: skip it (flag 2)
    bra     2f
    mov     #2, r1
1:  mov     #1, r1
2:  mov.l   r1, @r2
    rte
    nop
    .align 2
.Lpm3:    .long 0xFF100008
.Lintevt: .long 0xFF000028
.Ltcnt1h: .long 0xFFD80018
.Ltcr1h:  .long 0xFFD8001C

! single accesses: PMCTR read, one access, PMCTR read
    .text
    .align 2
    .globl _lat1_rd32, _lat1_rd16, _lat1_wr32, _lat1_wr16, _lat1_wr16_rd16other
_lat1_rd32:
    mov.l   .Lpm1, r1
    mov.l   @r1, r2
    mov.l   @r4, r0
    mov.l   @r1, r3
    sub     r2, r3
    rts
    mov     r3, r0
_lat1_rd16:
    mov.l   .Lpm1, r1
    mov.l   @r1, r2
    mov.w   @r4, r0
    mov.l   @r1, r3
    sub     r2, r3
    rts
    mov     r3, r0
_lat1_wr32:
    mov.l   .Lpm1, r1
    mov.l   @r1, r2
    mov.l   r5, @r4
    mov.l   @r1, r3
    sub     r2, r3
    rts
    mov     r3, r0
_lat1_wr16:
    mov.l   .Lpm1, r1
    mov.l   @r1, r2
    mov.w   r5, @r4
    mov.l   @r1, r3
    sub     r2, r3
    rts
    mov     r3, r0
! store.w v -> a, then load.w from a second peripheral register b (r6)
_lat1_wr16_rd16other:
    mov.l   .Lpm1, r1
    mov.l   @r1, r2
    mov.w   r5, @r4
    mov.w   @r6, r0
    mov.l   @r1, r3
    sub     r2, r3
    rts
    mov     r3, r0
    .align 2
.Lpm1:  .long 0xFF100008
