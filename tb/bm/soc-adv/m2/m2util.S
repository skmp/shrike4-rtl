/* m2util.S -- helpers shared by the M2 verification's programs.
 *   void mmucr_set(uint32_t v)       MMUCR <- v from P1, then 8 NOPs before anything else (hm 3.2: a data access to
 *                                    P0 / P3 / U0 / SQ at least 4 instructions, a branch to them 8 after the store)
 *   uint32_t to_user(entry, arg)     SSR <- 0x000000F0 (MD 0), SPC <- entry, r4 <- arg, RTE; the program's handler
 *                                    resumes at _to_user_ret (g_resume) when the user code faults
 *   uint32_t call_va(va, a, b, v)    calls the routine at va (translated or not) with r4 = a, r5 = b, r6 = v, r0 = 0;
 *                                    returns its r0 (callers keep 8+ instructions after any MMUCR write) */
        .text
        .balign 32
        .global _mmucr_set
_mmucr_set:
        mov.l   .Lmmucr,r1
        mov.l   r4,@r1
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        rts
        nop
        .balign 4
.Lmmucr: .long  0xFF000010

        .balign 32
        .global _to_user, _to_user_ret
_to_user:
        mov.l   .Lusr,r0
        ldc     r0,ssr
        ldc     r4,spc
        mov     r5,r4
        mov     #0,r0
        rte
        nop
_to_user_ret:
        rts
        nop
        .balign 4
.Lusr:  .long   0x000000F0

        .balign 32
        .global _call_va
_call_va:
        sts.l   pr,@-r15
        mov     r4,r1
        mov     r5,r4
        mov     r6,r5
        mov     r7,r6
        jsr     @r1
        mov     #0,r0
        lds.l   @r15+,pr
        rts
        nop
