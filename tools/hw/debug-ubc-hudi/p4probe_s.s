! p4probe_s.s -- loop snippets for the PMCR event survey (debug-ubc-hudi spec, run 1).
! Every snippet: r4 = iterations (>= 1), r5 / r6 / r7 = snippet arguments.  Only caller-saved registers are
! used (r0-r7, fr0-fr7); PR is saved where BSR is used.  Loops end with "dt r4 ; bf/s ; nop".

	.text

! 8 NOPs per iteration
	.align 5
	.global _sn_nop
_sn_nop:
1:	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	dt	r4
	bf/s	1b
	nop
	rts
	nop

! 4 cached loads from @r5 (P1)
	.align 5
	.global _sn_ld
_sn_ld:
1:	mov.l	@r5,r0
	mov.l	@r5,r1
	mov.l	@r5,r2
	mov.l	@r5,r3
	dt	r4
	bf/s	1b
	nop
	rts
	nop

! 4 cached stores to @r5 (P1)
	.align 5
	.global _sn_st
_sn_st:
1:	mov.l	r0,@r5
	mov.l	r1,@r5
	mov.l	r2,@r5
	mov.l	r3,@r5
	dt	r4
	bf/s	1b
	nop
	rts
	nop

! 1 load per iteration from @r5 (used for P2 uncached RAM, CCN register, TMU register)
	.align 5
	.global _sn_ld1
_sn_ld1:
1:	mov.l	@r5,r0
	dt	r4
	bf/s	1b
	nop
	rts
	nop

! branches: BT not taken, BRA taken, BSR/RTS, BF/S taken (loop)
	.align 5
	.global _sn_br
_sn_br:
	sts.l	pr,@-r15
1:	clrt
	bt	2f
	bra	3f
	nop
2:	nop
3:	bsr	4f
	nop
	dt	r4
	bf/s	1b
	nop
	lds.l	@r15+,pr
	rts
	nop
4:	rts
	nop

! FPU: 2 FADD + 2 FMOV per iteration
	.align 5
	.global _sn_fp
_sn_fp:
1:	fadd	fr0,fr1
	fadd	fr2,fr3
	fmov	fr4,fr5
	fmov	fr6,fr7
	dt	r4
	bf/s	1b
	nop
	rts
	nop

! operand cache conflict: @r5 and @r6 = r5 + 16 KB (P1) -> 2 misses per iteration
	.align 5
	.global _sn_ocm
_sn_ocm:
1:	mov.l	@r5,r0
	mov.l	@r6,r1
	dt	r4
	bf/s	1b
	nop
	rts
	nop

! instruction cache conflict: two blocks 8 KB apart (same IC index) -> 2 misses per iteration
	.align 5
	.global _sn_icm
_sn_icm:
	mov.l	.Licm_far,r6
	mov.l	.Licm_back,r7
1:	jmp	@r6
	nop
.Licm_bk:
	dt	r4
	bf/s	1b
	nop
	rts
	nop
	.align 2
.Licm_far:	.long	_sn_icm_far
.Licm_back:	.long	.Licm_bk
	.space	8192 - 32
	.align 5
	.global _sn_icm_far
_sn_icm_far:
	jmp	@r7
	nop
	.space	64

! busy spin: r4 iterations of a 2-instruction loop
	.align 5
	.global _spin
_spin:
1:	dt	r4
	bf	1b
	rts
	nop
