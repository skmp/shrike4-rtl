/* misc_asm.S -- (misc.c)
 *   uint32_t ie_trapa<k>(sr) / ie_adr<k>(sr) / ie_ldtlb<k>(sr)   k = 0..14: SR <- sr (IMASK 15 -> 0 with TMU0 pending),
 *            k NOPs, then TRAPA #0x55 / a misaligned load (the handler resumes at _ie_resume) / LDTLB, then 16 NOPs.
 *            _ie_*<k>_x labels the instruction.
 *   uint32_t rmw_gbr(uint32_t gbr, uint32_t *out)   AND.B / OR.B / XOR.B / TST.B #imm,@(R0,GBR) on P4 bytes:
 *            out[0] T after TST.B #0x80 at gbr+0; then OR.B #0x01, XOR.B #0x03, AND.B #0xFE at gbr+0 (out[1..3] = the byte
 *            read back after each); out[4] = T after TAS.B @gbr+0; out[5] = the byte after TAS.B */
        .text
        .macro  IE name, n, op
        .balign 32
        .global _ie_\name\n, _ie_\name\n\()_x
_ie_\name\n:
        mov.l   5f,r1
        ldc     r4,sr
        .rept   \n
        nop
        .endr
_ie_\name\n\()_x:
        \op
        .rept   16
        nop
        .endr
        rts
        nop
        .balign 4
5:      .long   0x8C0F0002
        .endm
        .irp    k,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14
        IE      trapa, \k, "trapa #0x55"
        IE      adr, \k, "mov.l @r1,r2"
        IE      ldtlb, \k, "ldtlb"
        .endr
        .global _ie_resume
_ie_resume:
        rts
        nop

        .data
        .balign 4
        .global _ie_trapa_tab, _ie_adr_tab, _ie_ldtlb_tab, _ie_trapa_x_tab, _ie_adr_x_tab, _ie_ldtlb_x_tab
_ie_trapa_tab:
        .irp    k,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14
        .long   _ie_trapa\k
        .endr
_ie_adr_tab:
        .irp    k,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14
        .long   _ie_adr\k
        .endr
_ie_ldtlb_tab:
        .irp    k,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14
        .long   _ie_ldtlb\k
        .endr
_ie_trapa_x_tab:
        .irp    k,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14
        .long   _ie_trapa\k\()_x
        .endr
_ie_adr_x_tab:
        .irp    k,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14
        .long   _ie_adr\k\()_x
        .endr
_ie_ldtlb_x_tab:
        .irp    k,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14
        .long   _ie_ldtlb\k\()_x
        .endr

        .text
        .balign 32
        .global _rmw_gbr
_rmw_gbr:
        stc     gbr,r7
        ldc     r4,gbr
        mov     #0,r0
        tst.b   #0x80,@(r0,gbr)
        movt    r1
        mov.l   r1,@(0,r5)
        or.b    #0x01,@(r0,gbr)
        mov.b   @r4,r1
        extu.b  r1,r1
        mov.l   r1,@(4,r5)
        xor.b   #0x03,@(r0,gbr)
        mov.b   @r4,r1
        extu.b  r1,r1
        mov.l   r1,@(8,r5)
        and.b   #0xFE,@(r0,gbr)
        mov.b   @r4,r1
        extu.b  r1,r1
        mov.l   r1,@(12,r5)
        tas.b   @r4
        movt    r1
        mov.l   r1,@(16,r5)
        mov.b   @r4,r1
        extu.b  r1,r1
        mov.l   r1,@(20,r5)
        rts
        ldc     r7,gbr
