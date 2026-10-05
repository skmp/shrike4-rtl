/* excint3_asm.S -- run-3 snippets (see excint3.c).  The vector table, handler and trampoline are excint_asm.S's. */
#include "excint_asm.S"
        .text
        .balign 32
/* t_rteirq(ssr, pmctr, sr_before): SR <- sr_before; PMCTR -> r1; RTE (SSR = ssr); slot nop; PMCTR at the target -> r2;
 * 64 adds.  The handler records r1 / r2 (bank 0) and its own PMCTR. */
GL(t_rteirq)
        ldc     r6,sr
        mov.l   .Lr3t,r2
        ldc     r2,spc
        ldc     r4,ssr
        mov     #0,r0
        nop
        nop
        nop
        nop
        mov.l   @r5,r1
GL(L_r3rte)
        rte
        nop
GL(L_r3t)
        mov.l   @r5,r2
GL(L_r3c)
        ADD64
        sub     r1,r2
        rts
        mov     r2,r0
        .balign 4
.Lr3t:  .long   _L_r3t
/* the same with LDC SR instead of RTE (reference) */
GL(t_ldcirq)
        ldc     r6,sr
        mov     #0,r0
        nop
        nop
        nop
        nop
        mov.l   @r5,r1
        ldc     r4,sr
GL(L_l3t)
        mov.l   @r5,r2
GL(L_l3c)
        ADD64
        sub     r1,r2
        rts
        mov     r2,r0
/* JMP to a target in the middle of a line / right after its slot (baseline for run 2's 29-cycle JMP) */
        .balign 32
GL(t_jmp_mid)
        mov.l   .Ljm3,r3
        nop
        nop
        mov.l   @r5,r1
        jmp     @r3
        nop
        nop
        nop
        nop
GL(jm3)
        mov.l   @r5,r2
        sub     r1,r2
        rts
        mov     r2,r0
        .balign 4
.Ljm3:  .long   _jm3
