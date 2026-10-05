! c1k_asm.S -- core-exc probe c1k (WI-CORE-A fixes, finding F3; rtl/v1/SOC.md 9.16): the channel-B data break (DBEB)
! on a load paired with a one-cycle delayed branch.  Which SPC does the console save, and is the branch kept?
! The DBR handler is ubcbrk's (tools/hw/debug-ubc-hudi/ubcbrk_s.s): it records SPC SSR SGR EXPEVT BRCR, a count, R9
! and R10, disables both channels (BBRA = BBRB = 0), clears CMFA / CMFB and returns with RTE to SPC.
! Every test function t_*(addr) counts in r9 (pushed / popped) and returns it: the slot adds 2, the fall-through 32
! (two adds of 16), the branch target 64.  The layout pairs the first four instructions two by two, so the load (at
! +8, longword aligned) pairs with the branch at +10 (LS + BR), as ubcbrk's t_ld paired its load with the ADD after it
! (opB_rd_data: SPC = t_ld + 8, r9 at the break 1).

	.text

! ------------------------------------------------------------------------------------------------ DBR handler
! record (_k_rec, longs): 0 SPC 1 SSR 2 SGR 3 EXPEVT 4 BRCR 5 count 6 R9 7 R10
	.align 5
	.global _k_dbr_handler
_k_dbr_handler:
	mov.l	.Lh_rec,r0
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
.Lh_rec:	.long	_k_rec
.Lh_expevt:	.long	0xFF000024
.Lh_brcr:	.long	0xFF200020
.Lh_bbra:	.long	0xFF200008
.Lh_cmf:	.word	0x3FFF
	.align 2

! ------------------------------------------------------------------------------------------------ helpers
	.global _k_set_dbr, _k_get_dbr, _k_get_sr, _k_ubc_wait
_k_set_dbr:
	rts
	ldc	r4,dbr
_k_get_dbr:
	rts
	stc	dbr,r0
_k_get_sr:
	rts
	stc	sr,r0
_k_ubc_wait:
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

! ------------------------------------------------------------------------------------------------ the tests
! t_ctl: control -- the load pairs with an ADD (+1), the BRA is the next group: SPC = the BRA on any rule
	.align 5
	.global _t_ctl, _t_ctl_ld, _t_ctl_br
_t_ctl:
	mov.l	r9,@-r15
	mov	#0,r9
	nop
	nop
_t_ctl_ld:
	mov.l	@r4,r1
	add	#1,r9
_t_ctl_br:
	bra	1f
	add	#2,r9			! slot
	add	#16,r9			! fall-through
	add	#16,r9
	bra	2f
	nop
1:	add	#64,r9			! target
2:	mov	r9,r0
	rts
	mov.l	@r15+,r9

! t_bra: the load paired with BRA
	.align 5
	.global _t_bra, _t_bra_ld, _t_bra_br
_t_bra:
	mov.l	r9,@-r15
	mov	#0,r9
	nop
	nop
_t_bra_ld:
	mov.l	@r4,r1
_t_bra_br:
	bra	1f
	add	#2,r9			! slot
	add	#16,r9			! fall-through
	add	#16,r9
	bra	2f
	nop
1:	add	#64,r9			! target
2:	mov	r9,r0
	rts
	mov.l	@r15+,r9

! t_bsr: the load paired with BSR (PR kept in r2 around it)
	.align 5
	.global _t_bsr, _t_bsr_ld, _t_bsr_br
_t_bsr:
	nop
	sts	pr,r2
	mov.l	r9,@-r15
	mov	#0,r9
_t_bsr_ld:
	mov.l	@r4,r1
_t_bsr_br:
	bsr	1f
	add	#2,r9			! slot
	add	#16,r9			! fall-through (the "return" path: never executed when the call is kept)
	add	#16,r9
	bra	2f
	nop
1:	add	#64,r9			! target
2:	lds	r2,pr
	mov	r9,r0
	rts
	mov.l	@r15+,r9

! t_bts: the load paired with a taken BT/S
	.align 5
	.global _t_bts, _t_bts_ld, _t_bts_br
_t_bts:
	sett
	mov.l	r9,@-r15
	mov	#0,r9
	nop
_t_bts_ld:
	mov.l	@r4,r1
_t_bts_br:
	bt/s	1f
	add	#2,r9			! slot
	add	#16,r9			! fall-through
	add	#16,r9
	bra	2f
	nop
1:	add	#64,r9			! target
2:	mov	r9,r0
	rts
	mov.l	@r15+,r9

! t_bfs: the load paired with a not-taken BF/S (T = 1)
	.align 5
	.global _t_bfs, _t_bfs_ld, _t_bfs_br
_t_bfs:
	sett
	mov.l	r9,@-r15
	mov	#0,r9
	nop
_t_bfs_ld:
	mov.l	@r4,r1
_t_bfs_br:
	bf/s	1f
	add	#2,r9			! slot
	add	#16,r9			! fall-through (taken path: not taken)
	add	#16,r9
	bra	2f
	nop
1:	add	#64,r9			! target
2:	mov	r9,r0
	rts
	mov.l	@r15+,r9
