/* iprwin_asm.S -- a posted Pclk store, k NOPs, then LDC SR lowering IMASK with a TMU0 underflow pending
 * (tb/bm/soc-adv; see iprwin.c).
 *   uint32_t pw<k>(uint32_t addr, uint32_t val, uint32_t sr, uint32_t tcor1)   k = 0..19: store val (16 bit) at addr,
 *                        k NOPs, SR <- sr, 48 adds (r8 counts them), SR <- 0x400000F0; returns r8
 *   uint32_t pr<k>(...)  the same with a read-back of addr right after the store (hm 19.3: "first read one of the
 *                        interrupt priority registers, then clear the BL bit")
 *   uint32_t pb<k>(...)  the same behind a longword store to TCOR1 (r7 = its address, stored as the value): the store
 *                        enters the Pclk path when the longword's entry is released and waits for its 3-Pcyc bus cycle,
 *                        the latest strobe after its ack (ack + 5), so the latest landing (Sh4Sys::intc_window)
 * _pw<k>_ldc / _pr<k>_ldc / _pb<k>_ldc label the LDC SR (the handler's SPC is compared with it). */
        .text
        .macro  PW n, rb
        .balign 32
        .global _p\rb\n
_p\rb\n:
        mov.l   r8,@-r15
        mov     #0,r8
        .ifc    \rb,b
        mov.l   r7,@r7
        .endif
        mov.w   r5,@r4
        .ifc    \rb,r
        mov.w   @r4,r1
        .endif
        .rept   \n
        nop
        .endr
        .global _p\rb\n\()_ldc
_p\rb\n\()_ldc:
        ldc     r6,sr
        .rept   48
        add     #1,r8
        .endr
        mov.l   1f,r1
        ldc     r1,sr
        mov     r8,r0
        rts
        mov.l   @r15+,r8
        .balign 4
1:      .long   0x400000F0
        .endm

        .irp    k,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19
        PW      \k,w
        PW      \k,r
        PW      \k,b
        .endr

        .data
        .balign 4
        .global _pw_tab, _pr_tab, _pb_tab, _pw_ldc_tab, _pr_ldc_tab, _pb_ldc_tab
_pw_tab:
        .irp    k,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19
        .long   _pw\k
        .endr
_pr_tab:
        .irp    k,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19
        .long   _pr\k
        .endr
_pb_tab:
        .irp    k,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19
        .long   _pb\k
        .endr
_pb_ldc_tab:
        .irp    k,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19
        .long   _pb\k\()_ldc
        .endr
_pw_ldc_tab:
        .irp    k,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19
        .long   _pw\k\()_ldc
        .endr
_pr_ldc_tab:
        .irp    k,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19
        .long   _pr\k\()_ldc
        .endr
