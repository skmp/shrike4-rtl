/* mmu0_asm.S -- user-mode snippets and the way into and out of user mode (mmu0.c).
 *   uint32_t to_user(uint32_t entry_p0, uint32_t arg)   SSR <- 0x000000F0 (MD 0, IMASK 15), SPC <- entry, r4 <- arg,
 *                                                        RTE.  Every snippet ends with an illegal instruction (or
 *                                                        faults earlier); the handler resumes at _to_user_ret
 *                                                        (g_resume, privileged).  Returns r0 as the snippet left it.
 * Snippets (user mode, r4 = the address under test): one operation, then .word 0xFFFD (EXPEVT 0x180 = "the operation
 * did not fault"). */
        .text
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

        .macro  SNIP name
        .balign 16
        .global _u_\name
_u_\name:
        .endm
        SNIP    ld
        mov.l   @r4,r0
        .word   0xFFFD
        SNIP    st
        mov.l   r4,@r4
        .word   0xFFFD
        SNIP    ldw
        mov.w   @r4,r0
        .word   0xFFFD
        SNIP    pref
        pref    @r4
        .word   0xFFFD
        SNIP    ocbi
        ocbi    @r4
        .word   0xFFFD
        SNIP    ocbwb
        ocbwb   @r4
        .word   0xFFFD
        SNIP    movca
        movca.l r0,@r4
        .word   0xFFFD
        SNIP    stcsr
        stc     sr,r0
        .word   0xFFFD
        SNIP    ldcsr
        ldc     r4,sr
        .word   0xFFFD
        SNIP    stcvbr
        stc     vbr,r0
        .word   0xFFFD
        SNIP    stcgbr
        stc     gbr,r0
        .word   0xFFFD
        SNIP    ldcgbr
        ldc     r4,gbr
        .word   0xFFFD
        SNIP    rte
        rte
        nop
        .word   0xFFFD
        SNIP    sleep
        sleep
        .word   0xFFFD
        SNIP    ldtlb
        ldtlb
        .word   0xFFFD
        SNIP    stsfpscr
        sts     fpscr,r0
        .word   0xFFFD
        SNIP    trapa
        trapa   #0x12
        .word   0xFFFD
        SNIP    jmp
        jmp     @r4
        nop
        .word   0xFFFD
        SNIP    ldcrbank
        ldc     r4,r1_bank
        .word   0xFFFD
        SNIP    stcsgr
        stc     sgr,r0
        .word   0xFFFD
        SNIP    slotill
        bra     1f
        .word   0xFFFD
1:      .word   0xFFFD
        SNIP    tas
        tas.b   @r4
        .word   0xFFFD
        .balign 16
        .global _u_target
_u_target:
        nop
        .word   0xFFFD
