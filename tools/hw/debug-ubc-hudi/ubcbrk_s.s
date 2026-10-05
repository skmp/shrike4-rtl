! ubcbrk_s.s -- user break test code (debug-ubc-hudi spec, run 2).
! Test functions count executed "add #1,r9" instructions in r9 (callee-saved: pushed / popped) and return r9.
! The DBR handler (BRCR.UBDE = 1) records SPC SSR SGR EXPEVT BRCR, a count, R9, R10 and PMCTR1L, then disables both
! channels (BBRA = BBRB = 0), clears CMFA / CMFB and returns with RTE (a pre-execution break then re-executes the
! instruction without breaking again).

	.text

! ------------------------------------------------------------------------------------------------ DBR handler
! record (_ubc_rec, longs): 0 SPC 1 SSR 2 SGR 3 EXPEVT 4 BRCR 5 count 6 R9 7 R10 8 PMCTR1L at entry
	.align 5
	.global _ubc_dbr_handler
_ubc_dbr_handler:
	mov.l	.Lh_pm,r3
	mov.l	@r3,r3			! PMCTR1L as early as possible
	mov.l	.Lh_rec,r0
	mov.l	r3,@(32,r0)
	stc	spc,r1
	mov.l	r1,@r0
	stc	ssr,r1
	mov.l	r1,@(4,r0)
	stc	sgr,r1
	mov.l	r1,@(8,r0)
	mov.l	.Lh_expevt,r2
	mov.l	@r2,r1
	mov.l	r1,@(12,r0)
	mov.l	.Lh_brcr,r2
	mov.w	@r2,r1
	extu.w	r1,r1
	mov.l	r1,@(16,r0)
	mov.l	@(20,r0),r1
	add	#1,r1
	mov.l	r1,@(20,r0)
	mov.l	r9,@(24,r0)
	mov.l	r10,@(28,r0)
	mov.l	.Lh_bbra,r2
	mov	#0,r1
	mov.w	r1,@r2			! BBRA = 0
	add	#12,r2
	mov.w	r1,@r2			! BBRB = 0
	mov.l	.Lh_brcr,r2
	mov.w	@r2,r1
	mov.w	.Lh_cmf,r3
	and	r3,r1
	mov.w	r1,@r2			! CMFA = CMFB = 0
	rte
	nop
	.align 2
.Lh_pm:		.long	0xFF100008
.Lh_rec:	.long	_ubc_rec
.Lh_expevt:	.long	0xFF000024
.Lh_brcr:	.long	0xFF200020
.Lh_bbra:	.long	0xFF200008
.Lh_cmf:	.word	0x3FFF
	.align 2

! ------------------------------------------------------------------------------------------------ helpers
	.global _set_dbr, _get_dbr, _get_sr, _ubc_wait
_set_dbr:
	rts
	ldc	r4,dbr
_get_dbr:
	rts
	stc	dbr,r0
_get_sr:
	rts
	stc	sr,r0
_ubc_wait:
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
	nop
	nop
	nop
	nop
	rts
	nop

! ------------------------------------------------------------------------------------------------ t_lin
	.align 5
	.global _t_lin, _t_lin_a, _t_lin_b, _t_lin_c
_t_lin:
	mov.l	r9,@-r15
	mov	#0,r9
	add	#1,r9
_t_lin_a:
	add	#1,r9
_t_lin_b:
	add	#1,r9
_t_lin_c:
	add	#1,r9
	add	#1,r9
	add	#1,r9
	add	#1,r9
	mov	r9,r0
	rts
	mov.l	@r15+,r9

! ------------------------------------------------------------------------------------------------ t_slot (taken BRA)
	.align 5
	.global _t_slot, _t_slot_br, _t_slot_ds, _t_slot_ovr, _t_slot_tgt
_t_slot:
	mov.l	r9,@-r15
	mov	#0,r9
_t_slot_br:
	bra	_t_slot_tgt
_t_slot_ds:
	add	#1,r9
_t_slot_ovr:
	add	#16,r9
	add	#16,r9
_t_slot_tgt:
	add	#1,r9
	mov	r9,r0
	rts
	mov.l	@r15+,r9

! ------------------------------------------------------------------------------------------------ t_bfs (not-taken BF/S)
	.align 5
	.global _t_bfs, _t_bfs_br, _t_bfs_ds, _t_bfs_nx
_t_bfs:
	mov.l	r9,@-r15
	mov	#0,r9
	sett
_t_bfs_br:
	bf/s	1f
_t_bfs_ds:
	add	#1,r9
_t_bfs_nx:
	add	#1,r9
1:	add	#1,r9
	mov	r9,r0
	rts
	mov.l	@r15+,r9

! ------------------------------------------------------------------------------------------------ t_seq (r4 = 0: skip X)
	.align 5
	.global _t_seq, _t_seq_x, _t_seq_y
_t_seq:
	mov.l	r9,@-r15
	mov	#0,r9
	tst	r4,r4
	bt	1f
_t_seq_x:
	add	#1,r9
1:	nop
	nop
	nop
_t_seq_y:
	add	#1,r9
	add	#1,r9
	mov	r9,r0
	rts
	mov.l	@r15+,r9

! ------------------------------------------------------------------------------------------------ operand accesses
! t_ld(addr): MOV.L @r4,r1 then 8 adds;  t_ldw(addr): MOV.W @r4,r1 then 8 adds;  t_st(addr, val): MOV.L r5,@r4 then 8 adds
	.align 5
	.global _t_ld, _t_ld_i
_t_ld:
	mov.l	r9,@-r15
	mov	#0,r9
_t_ld_i:
	mov.l	@r4,r1
	add	#1,r9
	add	#1,r9
	add	#1,r9
	add	#1,r9
	add	#1,r9
	add	#1,r9
	add	#1,r9
	add	#1,r9
	mov	r9,r0
	rts
	mov.l	@r15+,r9

	.align 5
	.global _t_ldw, _t_ldw_i
_t_ldw:
	mov.l	r9,@-r15
	mov	#0,r9
_t_ldw_i:
	mov.w	@r4,r1
	add	#1,r9
	add	#1,r9
	add	#1,r9
	add	#1,r9
	add	#1,r9
	add	#1,r9
	add	#1,r9
	add	#1,r9
	mov	r9,r0
	rts
	mov.l	@r15+,r9

	.align 5
	.global _t_st, _t_st_i
_t_st:
	mov.l	r9,@-r15
	mov	#0,r9
_t_st_i:
	mov.l	r5,@r4
	add	#1,r9
	add	#1,r9
	add	#1,r9
	add	#1,r9
	add	#1,r9
	add	#1,r9
	add	#1,r9
	add	#1,r9
	mov	r9,r0
	rts
	mov.l	@r15+,r9

! t_ldu(addr): load then an immediate use of the loaded value, then 8 adds
	.align 5
	.global _t_ldu, _t_ldu_i
_t_ldu:
	mov.l	r9,@-r15
	mov	#0,r9
_t_ldu_i:
	mov.l	@r4,r1
	add	r1,r9
	add	#1,r9
	add	#1,r9
	add	#1,r9
	add	#1,r9
	add	#1,r9
	add	#1,r9
	add	#1,r9
	mov	r9,r0
	rts
	mov.l	@r15+,r9

! ------------------------------------------------------------------------------------------------ t_bl (r4 = SR | BL)
	.align 5
	.global _t_bl, _t_bl_tgt
_t_bl:
	mov.l	r9,@-r15
	stc	sr,r2
	mov	#0,r9
	ldc	r4,sr
	nop
	nop
	nop
_t_bl_tgt:
	add	#1,r9
	add	#1,r9
	ldc	r2,sr
	nop
	mov	r9,r0
	rts
	mov.l	@r15+,r9

! ------------------------------------------------------------------------------------------------ t_lat (r4 = &PMCTR1L)
! R10 = PMCTR1L just before the target; the handler takes PMCTR1L at its entry
	.align 5
	.global _t_lat, _t_lat_tgt
_t_lat:
	mov.l	r10,@-r15
	mov.l	r9,@-r15
	mov	#0,r9
	mov.l	@r4,r10
_t_lat_tgt:
	add	#1,r9
	add	#1,r9
	mov	r9,r0
	mov.l	@r15+,r9
	rts
	mov.l	@r15+,r10

! ------------------------------------------------------------------------------------------------ settle time
! t_setK(&BBRA, value): write BBRA, K fillers, then the target (pre-execution break armed on the target by C)
	.macro	SETN k
	.align 5
	.global _t_setn\k, _t_setn\k\()_tgt
_t_setn\k:
	mov.w	r5,@r4
	.rept	\k
	nop
	.endr
_t_setn\k\()_tgt:
	nop
	rts
	nop
	.endm

	.macro	SETA k
	.align 5
	.global _t_seta\k, _t_seta\k\()_tgt
_t_seta\k:
	mov.w	r5,@r4
	.rept	\k
	add	#0,r1
	.endr
_t_seta\k\()_tgt:
	nop
	rts
	nop
	.endm

	SETN	0
	SETN	1
	SETN	2
	SETN	3
	SETN	4
	SETN	5
	SETN	6
	SETN	7
	SETN	8
	SETN	9
	SETN	10
	SETN	11
	SETN	12
	SETN	13
	SETN	14
	SETA	0
	SETA	1
	SETA	2
	SETA	3
	SETA	4
	SETA	5
	SETA	6
	SETA	7
	SETA	8

! ------------------------------------------------------------------------------------------------ area 7 via the UTLB
! t_a7(out): out[0..3] saved MMUCR PTEH PTEL PTEA; loads 3 UTLB entries (1 MB, C=0, SH=1, PR=RW, D=1) mapping VPN
! 1F000000 / 1FD00000 / 1F200000 to the same physical addresses, enables AT, reads through area 7:
! out[4] @1F000030  out[5] @1F00001C  out[6] @1FD80024  out[7] @FFD80024  out[8] BARA after writing it at 1F200000
! out[9] r16 @1FD0000C  out[10] = 0x600DF00D when the whole sequence ran.  Then AT = 0, TI = 1, registers restored.
! A TLB exception handler in C sets PC to _t_a7_bail.
	.align 5
	.global _t_a7, _t_a7_bail
_t_a7:
	mov.l	r8,@-r15
	mov	r4,r8
	mov.l	.La_mmucr,r1
	mov.l	.La_pteh,r2
	mov.l	.La_ptea,r3
	mov.l	@r1,r0
	mov.l	r0,@(0,r8)
	mov.l	@r2,r0
	mov.l	r0,@(4,r8)
	mov.l	@(4,r2),r0
	mov.l	r0,@(8,r8)
	mov.l	@r3,r0
	mov.l	r0,@(12,r8)
	mov	#4,r0
	mov.l	r0,@r1			! MMUCR = TI (invalidate all), URC = 0
	nop
	nop
	nop
	nop
	mov	#0,r0
	mov.l	r0,@r3			! PTEA = 0
	mov.l	.La_vpn0,r0
	mov.l	r0,@r2			! PTEH
	mov.l	.La_ptel0,r0
	mov.l	r0,@(4,r2)		! PTEL
	nop
	ldtlb
	nop
	mov.l	.La_urc1,r0
	mov.l	r0,@r1			! URC = 1
	nop
	nop
	nop
	nop
	mov.l	.La_vpn1,r0
	mov.l	r0,@r2
	mov.l	.La_ptel1,r0
	mov.l	r0,@(4,r2)
	nop
	ldtlb
	nop
	mov.l	.La_urc2,r0
	mov.l	r0,@r1			! URC = 2
	nop
	nop
	nop
	nop
	mov.l	.La_vpn2,r0
	mov.l	r0,@r2
	mov.l	.La_ptel2,r0
	mov.l	r0,@(4,r2)
	nop
	ldtlb
	nop
	mov.l	.La_at,r0
	mov.l	r0,@r1			! MMUCR = AT, URC = 3
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	mov.l	.La_a,r5
	mov.l	@r5,r0
	mov.l	r0,@(16,r8)
	mov.l	.La_b,r5
	mov.l	@r5,r0
	mov.l	r0,@(20,r8)
	mov.l	.La_c,r5
	mov.l	@r5,r0
	mov.l	r0,@(24,r8)
	mov.l	.La_d,r5
	mov.l	@r5,r0
	mov.l	r0,@(28,r8)
	mov.l	.La_e,r5
	mov.l	.La_val,r0
	mov.l	r0,@r5
	mov.l	.La_f,r5
	mov.l	@r5,r0
	mov.l	r0,@(32,r8)
	mov.l	.La_g,r5
	mov.w	@r5,r0
	extu.w	r0,r0
	mov.l	r0,@(36,r8)
	mov.l	.La_ok,r0
	mov.l	r0,@(40,r8)
_t_a7_bail:
	mov.l	.La_mmucr,r1
	mov.l	.La_pteh,r2
	mov.l	.La_ptea,r3
	mov	#4,r0
	mov.l	r0,@r1			! MMUCR = TI, AT = 0
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	mov.l	@(4,r8),r0
	mov.l	r0,@r2
	mov.l	@(8,r8),r0
	mov.l	r0,@(4,r2)
	mov.l	@(12,r8),r0
	mov.l	r0,@r3
	mov.l	@(0,r8),r0
	mov.l	r0,@r1
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	rts
	mov.l	@r15+,r8
	.align 2
.La_mmucr:	.long	0xFF000010
.La_pteh:	.long	0xFF000000
.La_ptea:	.long	0xFF000034
.La_vpn0:	.long	0x1F000000
.La_ptel0:	.long	0x1F0001F6
.La_vpn1:	.long	0x1FD00000
.La_ptel1:	.long	0x1FD001F6
.La_vpn2:	.long	0x1F200000
.La_ptel2:	.long	0x1F2001F6
.La_urc1:	.long	0x00000400
.La_urc2:	.long	0x00000800
.La_at:		.long	0x00000C01
.La_a:		.long	0x1F000030
.La_b:		.long	0x1F00001C
.La_c:		.long	0x1FD80024
.La_d:		.long	0xFFD80024
.La_e:		.long	0x1F200000
.La_val:	.long	0x13579BDF
.La_f:		.long	0xFF200000
.La_g:		.long	0x1FD0000C
.La_ok:		.long	0x600DF00D

! ------------------------------------------------------------------------------------------------ t_mask (own 1 KB block)
	.balign	1024
	.global _t_mask, _t_mask_mid
_t_mask:
	mov.l	r9,@-r15
	mov	#0,r9
	add	#1,r9
	bra	_t_mask_mid
	add	#1,r9
	.space	0x100
_t_mask_mid:
	add	#1,r9
	mov	r9,r0
	rts
	mov.l	@r15+,r9
	.balign	1024
	.space	64

! ------------------------------------------------------------------------------------------------ exact event harness
! meas(fn r4, n r5, a r6, io r7): calls fn(n, a, io[2]) and stores io[0] = d PMCTR2L, io[1] = d PMCTR1L across the call.
! The same code brackets every call, so (n2 call) - (n1 call) is exactly (n2 - n1) loop iterations.
	.align 5
	.global _meas
_meas:
	sts.l	pr,@-r15
	mov.l	r8,@-r15
	mov.l	r9,@-r15
	mov.l	r10,@-r15
	mov.l	r11,@-r15
	mov.l	r12,@-r15
	mov.l	r13,@-r15
	mov	r4,r8
	mov	r7,r11
	mov.l	.Lm_p1,r9
	mov.l	.Lm_p2,r10
	mov	r5,r4
	mov	r6,r5
	mov.l	@(8,r11),r6
	mov.l	@r10,r12
	jsr	@r8
	mov.l	@r9,r13
	mov.l	@r9,r1
	mov.l	@r10,r2
	sub	r13,r1
	sub	r12,r2
	mov.l	r2,@r11
	mov.l	r1,@(4,r11)
	mov.l	@r15+,r13
	mov.l	@r15+,r12
	mov.l	@r15+,r11
	mov.l	@r15+,r10
	mov.l	@r15+,r9
	mov.l	@r15+,r8
	lds.l	@r15+,pr
	rts
	nop
	.align 2
.Lm_p1:	.long	0xFF100008
.Lm_p2:	.long	0xFF100010

! ------------------------------------------------------------------------------------------------ branch snippets
! r4 = iterations; r5 = 0 (BRAF / BSRF offset).  Every loop ends with dt r4 ; bf/s 1b ; nop.
	.align 5
	.global _b_base
_b_base:
1:	nop
	dt	r4
	bf/s	1b
	nop
	rts
	nop

	.align 5
	.global _b_btnt
_b_btnt:
1:	clrt
	bt	2f
2:	dt	r4
	bf/s	1b
	nop
	rts
	nop

	.align 5
	.global _b_btt
_b_btt:
1:	sett
	bt	2f
	nop
2:	dt	r4
	bf/s	1b
	nop
	rts
	nop

	.align 5
	.global _b_btst
_b_btst:
1:	sett
	bt/s	2f
	nop
	nop
2:	dt	r4
	bf/s	1b
	nop
	rts
	nop

	.align 5
	.global _b_bfnt
_b_bfnt:
1:	sett
	bf	2f
2:	dt	r4
	bf/s	1b
	nop
	rts
	nop

	.align 5
	.global _b_bfsnt
_b_bfsnt:
1:	sett
	bf/s	2f
	nop
2:	dt	r4
	bf/s	1b
	nop
	rts
	nop

	.align 5
	.global _b_bra
_b_bra:
1:	bra	2f
	nop
	nop
2:	dt	r4
	bf/s	1b
	nop
	rts
	nop

	.align 5
	.global _b_braf
_b_braf:
1:	braf	r5
	nop
	dt	r4
	bf/s	1b
	nop
	rts
	nop

	.align 5
	.global _b_bsrf
_b_bsrf:
	sts.l	pr,@-r15
1:	bsrf	r5
	nop
	dt	r4
	bf/s	1b
	nop
	lds.l	@r15+,pr
	rts
	nop

	.align 5
	.global _b_jmp
_b_jmp:
	mov.l	.Lj_t,r6
1:	jmp	@r6
	nop
	nop
.Lj_2:	dt	r4
	bf/s	1b
	nop
	rts
	nop
	.align 2
.Lj_t:	.long	.Lj_2

	.align 5
	.global _b_bsr
_b_bsr:
	sts.l	pr,@-r15
1:	bsr	5f
	nop
	dt	r4
	bf/s	1b
	nop
	lds.l	@r15+,pr
	rts
	nop
5:	rts
	nop

	.align 5
	.global _b_jsr
_b_jsr:
	sts.l	pr,@-r15
	mov.l	.Ljs_t,r6
1:	jsr	@r6
	nop
	dt	r4
	bf/s	1b
	nop
	lds.l	@r15+,pr
	rts
	nop
.Ljs_sub:
	rts
	nop
	.align 2
.Ljs_t:	.long	.Ljs_sub

	.align 5
	.global _b_rts
_b_rts:
	sts.l	pr,@-r15
	mov.l	.Lr_t,r6
1:	lds	r6,pr
	rts
	nop
	nop
.Lr_2:	dt	r4
	bf/s	1b
	nop
	lds.l	@r15+,pr
	rts
	nop
	.align 2
.Lr_t:	.long	.Lr_2

! 8 non-pairing EX instructions (0x13 check)
	.align 5
	.global _b_ex8
_b_ex8:
1:	add	#1,r0
	add	#1,r1
	add	#1,r2
	add	#1,r3
	add	#1,r0
	add	#1,r1
	add	#1,r2
	add	#1,r3
	dt	r4
	bf/s	1b
	nop
	rts
	nop
