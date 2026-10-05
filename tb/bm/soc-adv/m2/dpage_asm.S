/* dpage_asm.S -- position-independent routine templates for dpage.c, copied into translated code pages, and the
 * translated-call helper.  Every template: r4 = pointer A, r5 = pointer B (data pages, 16-byte records), r6 = a value;
 * returns r0; clobbers r0-r3, MACH / MACL, FR0-FR3.  No literal pools (they run from any address).
 *   t_end    8 instructions, RTS + a slot load at the end: placed so the slot is the page's last word; the fetch runs
 *            ahead into the next page (a speculative ITLB refill, or a speculative UTLB miss when it is unmapped)
 *   t_slotx  the same, placed so the RTS is the page's last word: its delay slot is the next page's first word
 *            (a demand ITLB refill for the slot, E4c)
 *   t_mid    loads / stores / FMOV.S / TAS.B / a byte RMW, mid-page
 *   t_cross  32 instructions placed across a page boundary (straight-line crossing: a demand ITLB refill)
 *   t_mac    MAC.L @r4+,@r5+ twice (two data pages per instruction), MAC.W, then MACL returned
 *   t_slot0  the slot instruction t_slotx needs at the next page's offset 0 (copied there)
 * (call_va is m2util.S's) */
        .text
        .global _t_end, _t_end_e, _t_slotx, _t_slotx_e, _t_mid, _t_mid_e, _t_cross, _t_cross_e, _t_mac, _t_mac_e
        .global _t_slot0, _t_slot0_e
        .balign 4
_t_end:
        mov.l   @r4,r0
        add     r6,r0
        mov.l   r0,@r4
        mov.l   @(4,r4),r1
        xor     r1,r0
        mov.l   r0,@(8,r4)
        rts
        mov.l   @(12,r5),r0
_t_end_e:

        .balign 4
_t_slotx:
        mov.l   @r5,r0
        add     r6,r0
        mov.l   r0,@r5
        mov.l   @(4,r4),r1
        add     r1,r0
        mov.l   r0,@(4,r5)
        rts
_t_slotx_e:
_t_slot0:
        add     #1,r0
_t_slot0_e:

        .balign 4
_t_mid:
        mov.l   @r4,r0
        mov.l   @r5,r1
        add     r1,r0
        add     r6,r0
        mov.l   r0,@(8,r5)
        fmov.s  @r4,fr0
        fmov.s  @r5,fr1
        mov     r4,r2
        add     #12,r2
        tas.b   @r2
        mov     #15,r3
        mov.b   @r2,r1
        and     r3,r1
        add     r1,r0
        mov     r5,r2
        add     #13,r2
        mov.b   @r2,r1
        add     #3,r1
        mov.b   r1,@r2
        fmov.s  fr1,@r4
        rts
        mov.l   r0,@(4,r4)
_t_mid_e:

        .balign 4
_t_cross:
        mov.l   @r4,r0
        add     r6,r0
        mov.l   @(4,r5),r1
        add     r1,r0
        shll    r0
        mov.l   r0,@(4,r4)
        dt      r1
        mov.l   r1,@(4,r5)
        add     #7,r0
        shlr    r0
        mov.l   @(8,r4),r2
        add     r2,r0
        mov.l   r0,@(12,r4)
        xor     r6,r0
        mov.l   r0,@(8,r4)
        mov     r0,r3
        add     r3,r3
        add     r3,r0
        mov.l   @r5,r1
        sub     r1,r0
        mov.l   r0,@r5
        not     r0,r1
        and     r6,r1
        or      r1,r0
        mov.l   @(12,r5),r2
        add     r2,r0
        mov.l   r0,@(12,r5)
        swap.w  r0,r0
        add     #1,r0
        extu.w  r0,r0
        rts
        mov.l   r0,@(8,r5)
_t_cross_e:

        .balign 4
_t_mac:
        clrmac
        mov     r4,r2
        mov     r5,r3
        mac.l   @r2+,@r3+
        mac.l   @r2+,@r3+
        mac.w   @r2+,@r3+
        sts     macl,r0
        sts     mach,r1
        xor     r1,r0
        add     r6,r0
        rts
        mov.l   r0,@(12,r4)
_t_mac_e:

