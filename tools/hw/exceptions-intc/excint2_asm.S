/* excint2_asm.S -- run-2 snippets (see excint2.c).  The vector table, handler and trampoline are excint_asm.S's. */
#include "excint_asm.S"

        .text
        .balign 4
/* ---------------------------------------------------------------- MAC: second access (@Rm) faults, @Rn is fine */
GL(t_mac_rm)                    /* r4 = buffer: Rn = r2 = buf, Rm = r1 = buf+2 */
        mov     r4,r2
        mov     r4,r1
        add     #2,r1
        clrmac
GL(L_mac_rm)
        mac.l   @r1+,@r2+
        rts
        nop
GL(t_macw_rm)                   /* Rn = r2 = buf, Rm = r1 = buf+1 */
        mov     r4,r2
        mov     r4,r1
        add     #1,r1
        clrmac
GL(L_macw_rm)
        mac.w   @r1+,@r2+
        rts
        nop

/* ---------------------------------------------------------------- a faulting leader and its paired follower */
/* r4 = odd address.  CLRMAC (CO) issues alone, so the load leads the next issue group with the follower. */
GL(t_pk_add)
        mov     #0,r2
        clrmac
GL(L_pk_add)
        mov.w   @r4,r0
        add     #1,r2
        rts
        nop
GL(t_pk_sett)
        clrt
        clrmac
GL(L_pk_sett)
        mov.w   @r4,r0
        sett
        rts
        nop
GL(t_pk_fcmp)
        fldi1   fr0
        clrt
        clrmac
GL(L_pk_fcmp)
        mov.w   @r4,r0
        fcmp/eq fr0,fr0
        rts
        nop
GL(t_pk_fadd)
        fldi0   fr1
        fldi1   fr0
        clrmac
GL(L_pk_fadd)
        mov.w   @r4,r0
        fadd    fr0,fr1
        rts
        nop
GL(t_pk_frchg)
        clrmac
GL(L_pk_frchg)
        mov.w   @r4,r0
        frchg
        rts
        nop
GL(t_pk_older)
        mov     #0,r2
        clrmac
        add     #1,r2
GL(L_pk_older)
        mov.w   @r4,r0
        rts
        nop
/* FPU exception (r5 = FPSCR with the I enable) with a paired younger EX instruction */
GL(t_pk_fpe)
        fldi1   fr0
        lds     r5,fpscr
        mov     #0,r2
        clrmac
GL(L_pk_fpe)
        fadd    fr0,fr0
        add     #1,r2
        rts
        nop
/* TRAPA with the instruction before it paired */
GL(t_pk_trapa)
        mov     #0,r2
        clrmac
        add     #1,r2
GL(L_pk_trapa)
        trapa   #3
        add     #1,r2
        rts
        nop

/* ---------------------------------------------------------------- RTE / LDC SR / JMP timing (no interrupt pending) */
/* t_rtet(ssr, pmctr): cycles from a PMCTR read before RTE to a PMCTR read at the target */
GL(t_rtet)
        mov.l   .Lrtt,r2
        ldc     r2,spc
        ldc     r4,ssr
        mov     #0,r0
        nop
        nop
        nop
        nop
        mov.l   @r5,r1
        rte
        nop
GL(rtt1)
        mov.l   @r5,r2
        sub     r1,r2
        rts
        mov     r2,r0
GL(t_jmpt)
        mov.l   .Ljmt,r3
        nop
        nop
        nop
        nop
        mov.l   @r5,r1
        jmp     @r3
        nop
GL(jmt1)
        mov.l   @r5,r2
        sub     r1,r2
        rts
        mov     r2,r0
GL(t_ldcsrt)
        nop
        nop
        nop
        nop
        mov.l   @r5,r1
        ldc     r4,sr
        mov.l   @r5,r2
        sub     r1,r2
        rts
        mov     r2,r0
        .balign 4
.Lrtt:  .long   _rtt1
.Ljmt:  .long   _jmt1

/* ---------------------------------------------------------------- acceptance phase sweep
 * r4 = final SR, r5 = a Pclk register address (the load synchronises the core to Pclk), r6 = SR before the load.
 * PH(n):  sync load, n dependent adds on r3, LDC SR (unmask), 32 adds on r0.
 * PB(n):  same, then 3 adds, BRA + slot at index 3/4.
 * PS(n):  same, then 3 adds, BF/S not taken (T = 1) + slot at index 3/4.
 * PI(n):  sync load, n adds, IPRA store (r4 = value), 32 adds on r0 (SR IMASK 6 already).
 * PF(n):  sync load, n adds, LDC SR, then 0xFFFD.
 */
#define DLY(n) .rept n; add #1,r3; .endr
#define ADD3 ADD1; ADD1; ADD1
#define PH(n) GL(t_ph##n); ldc r6,sr; MOV0; mov.l @r5,r3; DLY(n); ldc r4,sr; GL(L_ph##n); ADD8; ADD8; ADD8; ADD8; rts; nop
#define PB(n) GL(t_pb##n); ldc r6,sr; MOV0; mov.l @r5,r3; DLY(n); ldc r4,sr; GL(L_pb##n); ADD3; bra 1f; ADD1; 1: ADD8; ADD8; rts; nop
#define PS(n) GL(t_ps##n); ldc r6,sr; sett; MOV0; mov.l @r5,r3; DLY(n); ldc r4,sr; GL(L_ps##n); ADD3; bf/s 1f; ADD1; 1: ADD8; ADD8; rts; nop
#define PI(n) GL(t_pi##n); ldc r6,sr; MOV0; mov #-48,r2; shll16 r2; add #4,r2; mov.l @r5,r3; DLY(n); mov.w r4,@r2; GL(L_pi##n); ADD8; ADD8; ADD8; ADD8; rts; nop
#define PF(n) GL(t_pf##n); ldc r6,sr; MOV0; mov.l @r5,r3; DLY(n); ldc r4,sr; GL(L_pf##n); .word 0xfffd; ADD8; rts; nop
#define ALL(M) M(0); M(1); M(2); M(3); M(4); M(5); M(6); M(7); M(8); M(9); M(10); M(11)
ALL(PH)
        .balign 4
ALL(PB)
        .balign 4
ALL(PS)
        .balign 4
ALL(PF)
        .balign 4
ALL(PI)
