/* asidsw_asm.S -- templates of asidsw.c (copied into translated pages): r4 = a data VA; return r0.
 *   t_ret<k>   r0 <- @r4 + k (each process's copy returns its own constant plus its own data word) */
        .text
        .global _t_ret, _t_ret_e
        .balign 4
_t_ret:
        mov.l   @r4,r0
        mov.l   @(4,r4),r1
        add     r1,r0
        mov     r0,r2
        shll2   r2
        add     r2,r0
        rts
        add     #1,r0
_t_ret_e:
        .balign 32
        .global _delay8
_delay8:
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        rts
        nop
