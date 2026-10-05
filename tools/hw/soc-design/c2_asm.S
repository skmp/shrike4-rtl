! c2_asm.S -- soc-design probe C2 (rtl/v1/SOC.md section 3.1.5): what makes a P4 cache / TLB array read cost more
! than an Iclk register read.  core-integration run 1 measured 8 back-to-back reads per loop iteration at 40 Icyc
! (address arrays, OC data array) and 54 (IC data array) against 26 for Iclk registers; no constant per-access stall
! fits both 40 and 54.  These loops separate a per-access cost from a port that stays busy after the access.
!
! Every test is  void fn(uint32_t n, uint32_t a, uint32_t b, uint32_t c)  run with interrupts masked, cached (P1):
!   r4 = iterations, r5 = address A, r6 = address B, r7 = cached scratch / PMCTR address (brackets).
! Loads go to r1 / r3; fillers are "add #0,r2" (EX class: one per cycle, may pair with a load as its follower).
! Only reads: nothing here changes a cache, TLB or register.

    .macro TL name
    .text
    .balign 32
    .globl _\name
_\name:
    bra     90f
    nop
    .balign 32
90:
    .endm

    .macro TE
    dt      r4
    bf      90b
    rts
    nop
    .endm

! ---- 8 reads of A per iteration (reproduces core-integration run 1)
    TL c2_x8
    .rept 8
    mov.l   @r5, r1
    .endr
    TE

! ---- one read of A per iteration
    TL c2_x1
    mov.l   @r5, r1
    TE

! ---- A ; K fillers ; A   (K = 0..6)
    .macro GAP k
    TL c2_g\k
    mov.l   @r5, r1
    .rept \k
    add     #0, r2
    .endr
    mov.l   @r5, r3
    TE
    .endm
    GAP 0
    GAP 1
    GAP 2
    GAP 3
    GAP 4
    GAP 5
    GAP 6

! ---- A ; B alternating, 4 pairs per iteration
    TL c2_alt
    .rept 4
    mov.l   @r5, r1
    mov.l   @r6, r3
    .endr
    TE

! ---- A ; 4 fillers ; A ; 4 fillers ... 4 reads per iteration (a gap long enough for any port to free)
    TL c2_sp4
    .rept 4
    mov.l   @r5, r1
    add     #0, r2
    add     #0, r2
    add     #0, r2
    add     #0, r2
    .endr
    TE

! ---- brackets: uint32_t fn(pmctr, A) = PMCTR after - PMCTR before, with X = one / two reads of A between
    .text
    .balign 32
    .globl _c2_br0
_c2_br0:
    bra     91f
    nop
    .balign 32
91: mov.l   @r4, r1
    mov.l   @r4, r3
    sub     r1, r3
    rts
    mov     r3, r0

    .balign 32
    .globl _c2_br1
_c2_br1:
    bra     92f
    nop
    .balign 32
92: mov.l   @r4, r1
    mov.l   @r5, r2
    mov.l   @r4, r3
    sub     r1, r3
    rts
    mov     r3, r0

    .balign 32
    .globl _c2_br2
_c2_br2:
    bra     93f
    nop
    .balign 32
93: mov.l   @r4, r1
    mov.l   @r5, r2
    mov.l   @r5, r2
    mov.l   @r4, r3
    sub     r1, r3
    rts
    mov     r3, r0

! ==================================================================================================================
! run 3 (c2b): where the extra cost of an array read goes.  Brackets with K EX fillers between the array read and B
! (straight code: does sequential fetch after an array read stall?), and loops "X ; K fillers ; dt ; bf".
    .macro BRK k
    .text
    .balign 32
    .globl _c2_bk\k
_c2_bk\k:
    bra     95f
    nop
    .balign 32
95: mov.l   @r4, r1
    mov.l   @r5, r2
    .rept \k
    add     #0, r7
    .endr
    mov.l   @r4, r3
    sub     r1, r3
    rts
    mov     r3, r0
    .endm
    BRK 0
    BRK 1
    BRK 2
    BRK 3
    BRK 4
    BRK 6
    BRK 8
    BRK 12
    BRK 16
    BRK 24

    .macro XK k
    TL c2_xk\k
    mov.l   @r5, r1
    .rept \k
    add     #0, r2
    .endr
    TE
    .endm
    XK 1
    XK 2
    XK 3
    XK 4
    XK 6
    XK 8
    XK 12
    XK 16
    XK 24
