! crt0.S -- entry of the timers-clock probe ports.  The boot stub (SOC.md 7.3) jumps here with the post-BIOS chip state.
! This clears .bss, turns the caches on as KOS's startup does (CCR = 0x090D: invalidate and enable both, P1
! copy-back; written from P2), calls main, writes its return value to the sim-control exit port (board.md 3.10:
! 0x18000000, reached through P2) and sleeps.
    .section .text.start, "ax"
    .globl  _start
_start:
    mov.l   .Lbss_s, r0
    mov.l   .Lbss_e, r1
    mov     #0, r2
1:  cmp/hs  r1, r0
    bt      2f
    mov.l   r2, @r0
    bra     1b
    add     #4, r0
2:  mova    .Lp2, r0            ! run the CCR write from the P2 alias of this code
    mov.l   .Lp2mask, r1
    or      r1, r0
    jmp     @r0
    nop
    .align  2
.Lp2:
    mov.l   .Lccr, r1
    mov.l   .Lccrv, r2
    mov.l   r2, @r1
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    mov.l   .Lmain, r0
    jsr     @r0
    nop
    mov.l   .Lexit, r1
    mov.l   r0, @r1
3:  sleep
    bra     3b
    nop
    .align  2
.Lbss_s:  .long __bss_start
.Lbss_e:  .long __bss_end
.Lp2mask: .long 0xA0000000
.Lccr:    .long 0xFF00001C
.Lccrv:   .long 0x0000090D
.Lmain:   .long _main
.Lexit:   .long 0xB8000000
