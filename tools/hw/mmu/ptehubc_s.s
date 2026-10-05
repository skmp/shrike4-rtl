! ptehubc_s.s -- the snippets and the DBR-path handler of ptehubc.c (M2-fix, M2-verify F6).
! Handler (BRCR.UBDE = 1, entered at DBR): records SPC, EXPEVT and a count in _pu_rec, disarms both channels (BBRA =
! BBRB = 0), clears CMFA / CMFB and returns with RTE (a pre-execution break then re-executes the instruction without
! breaking again).  It uses r0-r3 only (as tools/hw/debug-ubc-hudi/ubcbrk_s.s's handler); the snippets use r4 / r5.

	.text

	.align 5
	.global _pu_dbr_handler
_pu_dbr_handler:
	mov.l	.Lh_rec,r0
	stc	spc,r1
	mov.l	r1,@r0			! rec[0] = SPC
	mov.l	.Lh_expevt,r2
	mov.l	@r2,r1
	mov.l	r1,@(4,r0)		! rec[1] = EXPEVT
	mov.l	@(8,r0),r1
	add	#1,r1
	mov.l	r1,@(8,r0)		! rec[2] = count
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
.Lh_rec:	.long	_pu_rec
.Lh_expevt:	.long	0xFF000024
.Lh_brcr:	.long	0xFF200020
.Lh_bbra:	.long	0xFF200008
.Lh_cmf:	.word	0x3FFF
	.align 2

	.global _pu_set_dbr, _pu_get_dbr, _pu_wait
_pu_set_dbr:
	rts
	ldc	r4,dbr
_pu_get_dbr:
	rts
	stc	dbr,r0
_pu_wait:				! 24 NOPs: a UBC register write settles (ubcbrk_s.s _ubc_wait)
	.rept	24
	nop
	.endr
	rts
	nop

! pk<k>(&PTEH, value): the PTEH store (longword), k NOP fillers, then the target (a NOP; the pre-execution break is
! armed on it by the C side).  The store is the snippet's first instruction, at a 32-byte line start, as ubcbrk T.
	.macro	PK k
	.align 5
	.global _pk\k, _pk\k\()_tgt
_pk\k:
	mov.l	r5,@r4
	.rept	\k
	nop
	.endr
_pk\k\()_tgt:
	nop
	rts
	nop
	.endm

! pt<k>(&BBRA, value): ubcbrk T's snippet exactly (a word store to BBRA arms the break, k NOPs, the target): the
! reference, console T settle: breaks from k = 13 NOPs, never 12 [HW ubcbrk.elf.hwlog line 167]
	.macro	PT k
	.align 5
	.global _pt\k, _pt\k\()_tgt
_pt\k:
	mov.w	r5,@r4
	.rept	\k
	nop
	.endr
_pt\k\()_tgt:
	nop
	rts
	nop
	.endm

	PK	0
	PK	1
	PK	2
	PK	3
	PK	4
	PK	5
	PK	6
	PK	7
	PK	8
	PK	9
	PK	10
	PK	11
	PK	12
	PK	13
	PK	14
	PT	0
	PT	1
	PT	2
	PT	3
	PT	4
	PT	5
	PT	6
	PT	7
	PT	8
	PT	9
	PT	10
	PT	11
	PT	12
	PT	13
	PT	14

! po<k>(&PTEH or &BBRA, value, &data): the store, k NOP fillers, then the load (the operand-read break target) of
! @r6; the operand cases (a break after the load, SPC = the instruction after it); k = 0..6
	.macro	PO k
	.align 5
	.global _po\k, _po\k\()_ld
_po\k:
	mov.l	r5,@r4
	.rept	\k
	nop
	.endr
_po\k\()_ld:
	mov.l	@r6,r0
	nop
	nop
	rts
	nop
	.endm
	PO	0
	PO	1
	PO	2
	PO	3
	PO	4
	PO	5
	PO	6

! ps<k>(&BBRA, value, &data): the operand reference (ubcevt S operand): a word store to BBRA arms the operand-read
! break k NOPs before the load (console: breaks from k = 3, never k <= 2 [HW ubcevt S])
	.macro	PS k
	.align 5
	.global _ps\k, _ps\k\()_ld
_ps\k:
	mov.w	r5,@r4
	.rept	\k
	nop
	.endr
_ps\k\()_ld:
	mov.l	@r6,r0
	nop
	nop
	rts
	nop
	.endm
	PS	0
	PS	1
	PS	2
	PS	3
	PS	4
	PS	5
	PS	6
