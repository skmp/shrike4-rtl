/* ubcasid_asm.S -- the snippets of ubcasid.c, as tools/hw/mmu/ptehubc_s.s (each at a 32-byte line start; called
 * through a pointer, so the store is a JSR target and leads its issue group, as on the console):
 *   uint32_t uk<k>(&PTEH, val, &data)   PTEH <- val (longword), k NOPs, the target (a NOP), rts
 *   uint32_t ut<k>(&BBRA, val, &data)   BBRA <- val (word: arms the break), k NOPs, the target, rts
 *   uint32_t uo<k>(&PTEH, val, &data)   PTEH <- val, k NOPs, the load of @data (the operand break's target), rts
 *   uint32_t us<k>(&BBRA, val, &data)   BBRA <- val, k NOPs, the load of @data, rts
 *   _ua_ret                             rts: where the run-time resumes after the logged 0x1E0 (g_resume)
 *   uint32_t ua_call(fn, reg, val, data)   fn(reg, val, data) through a JSR whose slot is a NOP, the arguments moved
 *                                          into r4-r6 by MOVs 3-5 instructions earlier: the snippet's store issues at
 *                                          its JSR target without an operand stall (as ubcbrk T's call site; a load
 *                                          of r4 / r5 in the JSR's slot stalls the store one cycle, which v1_fetch
 *                                          turns into run-ahead the console does not show: SOC.md 9.22) */
        .text
        .balign 32
        .global _ua_call
_ua_call:
        sts.l   pr,@-r15
        mov     r4,r1
        mov     r5,r4
        mov     r6,r5
        mov     r7,r6
        nop
        nop
        jsr     @r1
        nop
        lds.l   @r15+,pr
        rts
        nop
        .macro  UK k
        .balign 32
        .global _uk\k, _uk\k\()_tgt
_uk\k:
        mov.l   r5,@r4
        .rept   \k
        nop
        .endr
_uk\k\()_tgt:
        nop
        rts
        nop
        .endm
        .macro  UT k
        .balign 32
        .global _ut\k, _ut\k\()_tgt
_ut\k:
        mov.w   r5,@r4
        .rept   \k
        nop
        .endr
_ut\k\()_tgt:
        nop
        rts
        nop
        .endm
        .macro  UO k
        .balign 32
        .global _uo\k, _uo\k\()_ld
_uo\k:
        mov.l   r5,@r4
        .rept   \k
        nop
        .endr
_uo\k\()_ld:
        mov.l   @r6,r0
        nop
        nop
        rts
        nop
        .endm
        .macro  US k
        .balign 32
        .global _us\k, _us\k\()_ld
_us\k:
        mov.w   r5,@r4
        .rept   \k
        nop
        .endr
_us\k\()_ld:
        mov.l   @r6,r0
        nop
        nop
        rts
        nop
        .endm
        UK 0
        UK 1
        UK 2
        UK 3
        UK 4
        UK 5
        UK 6
        UK 7
        UK 8
        UK 9
        UK 10
        UK 11
        UK 12
        UK 13
        UK 14
        UT 0
        UT 1
        UT 2
        UT 3
        UT 4
        UT 5
        UT 6
        UT 7
        UT 8
        UT 9
        UT 10
        UT 11
        UT 12
        UT 13
        UT 14
        UO 0
        UO 1
        UO 2
        UO 3
        UO 4
        UO 5
        UO 6
        US 0
        US 1
        US 2
        US 3
        US 4
        US 5
        US 6
        .balign 32
        .global _ua_ret
_ua_ret:
        rts
        nop

/* void ua_icinval(uint32_t start, uint32_t len) (M3-fix, ubcasid_c1: the console's cold rows): KOS 2.1's
 * icache_flush_range (arch/dreamcast/kernel/cache.s), as ptehubc called it -- from P2, per 32-byte line an OCBWB and a
 * non-associative IC address-array write of the line's tag with V = 0, then 7 instructions before returning to P1 */
        .balign 32
        .global _ua_icinval
_ua_icinval:
        mov.l   .Lui_real,r0
        mov.l   .Lui_p2,r1
        or      r1,r0
        jmp     @r0
        nop
        .balign 4
.Lui_real: .long .Lui_code
.Lui_p2:   .long 0xA0000000
.Lui_code:
        add     r4,r5
        mov.l   .Lui_amask,r0
        and     r0,r4
        mov.l   .Lui_icaa,r1
        mov.l   .Lui_ent,r2
        mov.l   .Lui_val,r3
1:      ocbwb   @r4
        mov     r4,r6
        and     r2,r6
        or      r1,r6
        mov     r4,r7
        and     r3,r7
        add     #32,r4
        cmp/hi  r4,r5
        bt/s    1b
        mov.l   r7,@r6
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
.Lui_amask: .long 0xFFFFFFE0
.Lui_icaa: .long 0xF0000000
.Lui_ent:  .long 0x00001FE0
.Lui_val:  .long 0xFFFFFC00
