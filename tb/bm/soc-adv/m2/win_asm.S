/* win_asm.S -- the landing-window probes of win.c.
 *   uint32_t pw<n>(uint32_t *reg, uint32_t val, uint32_t va, uint32_t sentinel)
 *       r0 <- sentinel; @reg <- val (an Iclk P4 store: PTEH or MMUCR); n NOPs; r0 <- @va; 8 NOPs; return r0.
 *       Pairing (v1 = the console's rules): the store is LS, a NOP is MT; with n = 0, 1, 2 the load issues in the group
 *       right behind the store's (held in EX while the store occupies MA), with n = 3 one group later.
 *       A faulting load resumes at _pw_ret (g_resume) and returns the sentinel.
 *   uint32_t pu<n>(uint32_t *reg, uint32_t val, uint32_t sentinel)
 *       the UBC probe: r0 <- sentinel; @reg <- val; n NOPs; the instruction at _pu<n>_brk (mov #1,r0); 8 NOPs */
        .text
        .macro  PW n
        .balign 32
        .global _pw\n
_pw\n:
        mov     r7,r0
        mov.l   r5,@r4
        .rept   \n
        nop
        .endr
        mov.l   @r6,r0
        .rept   8
        nop
        .endr
        rts
        nop
        .endm
        PW 0
        PW 1
        PW 2
        PW 3
        PW 4
        PW 5
        PW 8
        .balign 32
        .global _pw_ret
_pw_ret:
        rts
        nop

        .macro  PU n
        .balign 32
        .global _pu\n, _pu\n\()_brk
_pu\n:
        mov     r6,r0
        mov.l   r5,@r4
        .rept   \n
        nop
        .endr
_pu\n\()_brk:
        mov     #1,r0
        .rept   8
        nop
        .endr
        rts
        nop
        .endm
        PU 0
        PU 2
        PU 4
        PU 8
        PU 16
        PU 24

/*   void pl<n>(uint32_t *reg, uint32_t val)   @reg <- val (PTEL / PTEH), n NOPs, LDTLB, 8 NOPs (part L) */
        .macro  PL n
        .balign 32
        .global _pl\n
_pl\n:
        mov.l   r5,@r4
        .rept   \n
        nop
        .endr
        ldtlb
        .rept   8
        nop
        .endr
        rts
        nop
        .endm
        PL 0
        PL 1
        PL 2
        PL 3
