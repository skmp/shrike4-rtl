! ubcevt_s.s -- debug-ubc-hudi spec, run 3: settle time with EX fillers and for operand breaks, data-break position,
! branch / issue-count event snippets, PMCR start / stop latency.  Uses _ubc_dbr_handler, _meas from ubcbrk_s.s.

	.text

! ------------------------------------------------------------------------------------------------ settle, ADD fillers
! t_sxK(&BBRA, value): write BBRA, K "add #0,r1" (EX, never pair with each other), target nop (instruction break)
	.macro	SETX k
	.align 5
	.global _t_sx\k, _t_sx\k\()_tgt
_t_sx\k:
	mov.w	r5,@r4
	.rept	\k
	add	#0,r1
	.endr
_t_sx\k\()_tgt:
	nop
	rts
	nop
	.endm
	SETX	9
	SETX	10
	SETX	11
	SETX	12
	SETX	13
	SETX	14
	SETX	15
	SETX	16
	SETX	17
	SETX	18
	SETX	20
	SETX	22
	SETX	24

! ------------------------------------------------------------------------------------------------ settle, operand break
! t_soK(&BBRA, value, addr): write BBRA, K NOPs, then MOV.L @r6,r0 (operand-read break armed on addr by C), 4 adds
	.macro	SETO k
	.align 5
	.global _t_so\k
_t_so\k:
	mov.w	r5,@r4
	.rept	\k
	nop
	.endr
	mov.l	@r6,r0
	add	#1,r1
	add	#1,r1
	add	#1,r1
	add	#1,r1
	rts
	nop
	.endm
	SETO	0
	SETO	1
	SETO	2
	SETO	3
	SETO	4
	SETO	5
	SETO	6

! ------------------------------------------------------------------------------------------------ data-break position
! all: r4 = addr, r5 = store value; the access is at label _dN_i; returns r9 (count of executed "add #1,r9")
	.align 5
	.global _d1, _d1_i            ! load then NOP pairs
_d1:
	mov.l	r9,@-r15
	mov	#0,r9
_d1_i:
	mov.l	@r4,r1
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	add	#1,r9
	mov	r9,r0
	rts
	mov.l	@r15+,r9

	.align 5
	.global _d2, _d2_i            ! store then NOP pairs
_d2:
	mov.l	r9,@-r15
	mov	#0,r9
_d2_i:
	mov.l	r5,@r4
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	add	#1,r9
	mov	r9,r0
	rts
	mov.l	@r15+,r9

	.align 5
	.global _d3, _d3_i            ! store as the follower of an add, then adds
_d3:
	mov.l	r9,@-r15
	mov	#0,r9
	nop
	add	#1,r9
_d3_i:
	mov.l	r5,@r4
	add	#1,r9
	add	#1,r9
	add	#1,r9
	add	#1,r9
	add	#1,r9
	mov	r9,r0
	rts
	mov.l	@r15+,r9

	.align 5
	.global _d4, _d4_i            ! load, then a stalled consumer 2 instructions later
_d4:
	mov.l	r9,@-r15
	mov	#0,r9
_d4_i:
	mov.l	@r4,r1
	add	#1,r9
	add	r1,r2
	add	#1,r9
	add	#1,r9
	add	#1,r9
	add	#1,r9
	mov	r9,r0
	rts
	mov.l	@r15+,r9

	.align 5
	.global _d5, _d5_i            ! two stores back to back (the second one also matches)
_d5:
	mov.l	r9,@-r15
	mov	#0,r9
_d5_i:
	mov.l	r5,@r4
	mov.l	r5,@r4
	add	#1,r9
	add	#1,r9
	add	#1,r9
	add	#1,r9
	mov	r9,r0
	rts
	mov.l	@r15+,r9

! ------------------------------------------------------------------------------------------------ event snippets
	.align 5
	.global _e_btfar              ! BT taken over 3 instructions
_e_btfar:
1:	sett
	bt	2f
	nop
	nop
	nop
2:	dt	r4
	bf/s	1b
	nop
	rts
	nop

	.align 5
	.global _e_bffar              ! BF taken over 3 instructions
_e_bffar:
1:	clrt
	bf	2f
	nop
	nop
	nop
2:	dt	r4
	bf/s	1b
	nop
	rts
	nop

	.align 5
	.global _e_bfshort            ! BF taken over 1 instruction
_e_bfshort:
1:	clrt
	bf	2f
	nop
2:	dt	r4
	bf/s	1b
	nop
	rts
	nop

	.align 5
	.global _e_btback             ! BT taken backwards (loop closed by BT, non-delayed)
_e_btback:
1:	nop
	dt	r4
	bf	1b
	rts
	nop

	.align 5
	.global _e_jsrjmp             ! JSR out, JMP back (no RTS)
_e_jsrjmp:
	sts.l	pr,@-r15
	mov.l	.Ljj_t,r6
	mov.l	.Ljj_b,r7
1:	jsr	@r6
	nop
.Ljj_bk:
	dt	r4
	bf/s	1b
	nop
	lds.l	@r15+,pr
	rts
	nop
.Ljj_sub:
	jmp	@r7
	nop
	.align 2
.Ljj_t:	.long	.Ljj_sub
.Ljj_b:	.long	.Ljj_bk

	.align 5
	.global _e_ldcgbr             ! LDC Rm,GBR (3 issue cycles)
_e_ldcgbr:
	stc	gbr,r7
1:	ldc	r7,gbr
	dt	r4
	bf/s	1b
	nop
	rts
	nop

	.align 5
	.global _e_fmov2              ! 2 FMOV (LS, cannot pair with each other)
_e_fmov2:
1:	fmov	fr0,fr1
	fmov	fr2,fr3
	dt	r4
	bf/s	1b
	nop
	rts
	nop

	.align 5
	.global _e_fadd2              ! 2 FADD (FE, cannot pair with each other)
_e_fadd2:
1:	fadd	fr0,fr1
	fadd	fr2,fr3
	dt	r4
	bf/s	1b
	nop
	rts
	nop

	.align 5
	.global _e_fpair              ! FADD + FMOV pairs twice
_e_fpair:
1:	fadd	fr0,fr1
	fmov	fr4,fr5
	fadd	fr2,fr3
	fmov	fr6,fr7
	dt	r4
	bf/s	1b
	nop
	rts
	nop

	.align 5
	.global _e_stp4               ! store to a P4 register (BASRB = 0) and a store queue write
_e_stp4:
1:	mov.b	r5,@r6
	dt	r4
	bf/s	1b
	nop
	rts
	nop

! ------------------------------------------------------------------------------------------------ PMCR start / stop latency
! pm_lat(out r4, &PMCR2 r5, &PMCTR2L r6): PMCR2 = 0xC023 then 6 back-to-back PMCTR2L reads; PMCR2 = 0x0023 then 6 reads
	.align 5
	.global _pm_lat
_pm_lat:
	mov.w	.Lpl_on,r0
	mov.w	r0,@r5
	mov.l	@r6,r1
	mov.l	@r6,r2
	mov.l	@r6,r3
	mov.l	@r6,r7
	mov.l	r1,@(0,r4)
	mov.l	r2,@(4,r4)
	mov.l	r3,@(8,r4)
	mov.l	r7,@(12,r4)
	mov.w	.Lpl_off,r0
	mov.w	r0,@r5
	mov.l	@r6,r1
	mov.l	@r6,r2
	mov.l	@r6,r3
	mov.l	@r6,r7
	mov.l	r1,@(16,r4)
	mov.l	r2,@(20,r4)
	mov.l	r3,@(24,r4)
	mov.l	r7,@(28,r4)
	rts
	nop
	.align 1
.Lpl_on:	.word	0xC023
.Lpl_off:	.word	0x0023
