! macs_asm.S -- probe: which SR.S does MAC.L / MAC.W saturate with when a younger instruction changes S right
! behind it?  (rtl/v1/v1_fpu.sv took SR.S when the multiplier writes MACH / MACL, a few cycles after the MAC issues;
! the reference ISS takes it in program order.  Found by make v1-soc-sweep-full: id 42512, MAC.L @R2+,@R1+ ; SETS.)
!
! Every test is  void fn(const void *a, const void *b, uint32_t *io)  run with interrupts masked, cached (P1):
!   r4 = the @Rn operand's address, r5 = the @Rm operand's, r6 = io: io[2] / io[3] = MACH / MACL before the MAC,
!   io[0] / io[1] = MACH / MACL after it.  The caller's SR.S is restored.
!   \pre  = the S the MAC is issued under (clrs / sets), settled by 6 NOPs
!   \post = the younger instruction, \k NOPs behind the MAC (sets / clrs; nop for the controls)
! 12 NOPs after \post let the multiplier finish before the STS.

    .macro MT name, op, pre, post, k
    .text
    .balign 32
    .globl _\name
_\name:
    bra     90f
    nop
    .balign 32
90:
    stc     sr,r7
    mov.l   @(8,r6),r0
    lds     r0,mach
    mov.l   @(12,r6),r0
    lds     r0,macl
    mov     r4,r1
    mov     r5,r2
    \pre
    nop
    nop
    nop
    nop
    nop
    nop
    \op     @r2+,@r1+
    .rept   \k
    nop
    .endr
    \post
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    sts     mach,r0
    mov.l   r0,@r6
    sts     macl,r0
    mov.l   r0,@(4,r6)
    mov     r7,r0
    tst     #2,r0
    bt      91f
    sets
    bra     92f
    nop
91: clrs
92: rts
    nop
    .endm

    .irp k,0,1,2,3,4,5,6,7
    MT mt_l_cs_\k, mac.l, clrs, sets, \k
    .endr
    .irp k,0,1,2,3,4,5,6,7
    MT mt_l_sc_\k, mac.l, sets, clrs, \k
    .endr
    .irp k,0,1,2,3,4,5,6,7
    MT mt_w_cs_\k, mac.w, clrs, sets, \k
    .endr
    .irp k,0,1,2,3,4,5,6,7
    MT mt_w_sc_\k, mac.w, sets, clrs, \k
    .endr
    MT mt_l_cn, mac.l, clrs, nop, 0
    MT mt_l_sn, mac.l, sets, nop, 0
    MT mt_w_cn, mac.w, clrs, nop, 0
    MT mt_w_sn, mac.w, sets, nop, 0

! the table main() walks: l cs 0..7, l sc 0..7, w cs 0..7, w sc 0..7, then the controls l cn, l sn, w cn, w sn
    .data
    .balign 4
    .globl _mt_table
_mt_table:
    .irp m,l_cs,l_sc,w_cs,w_sc
    .irp k,0,1,2,3,4,5,6,7
    .long   _mt_\m\()_\k
    .endr
    .endr
    .long   _mt_l_cn, _mt_l_sn, _mt_w_cn, _mt_w_sn
