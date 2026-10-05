! pmcsurvey_s.s -- loop snippets for the PMC event survey of SOC.md open question 9.6 (WI-DBG): events the console
! never exercised.  Every snippet: r4 = iterations (>= 1), r5 / r6 = arguments; loops end with dt r4 ; bf/s 1b ; nop.
! They are measured through _meas (ubcbrk_s.s): (n2 = 80 call) - (n1 = 16 call) = exactly 64 iterations.
! Only caller-saved registers (r0-r7, fr0-fr11) are used; PR is saved where JSR is used.

	.text

! ---- 0x15 when the FPU instruction is the follower of a non-FPU leader (EX + FE pairs), and the reverse
	.align 5
	.global _s_fpfollow
_s_fpfollow:
1:	add	#1,r0
	fadd	fr2,fr3
	add	#1,r1
	fadd	fr4,fr5
	dt	r4
	bf/s	1b
	nop
	rts
	nop

	.align 5
	.global _s_fplead
_s_fplead:
1:	fadd	fr2,fr3
	add	#1,r0
	fadd	fr4,fr5
	add	#1,r1
	dt	r4
	bf/s	1b
	nop
	rts
	nop

! two FE instructions: never pair (2 groups with an FPU instruction)
	.align 5
	.global _s_fpalone
_s_fpalone:
1:	fadd	fr2,fr3
	fadd	fr4,fr5
	dt	r4
	bf/s	1b
	nop
	rts
	nop

! MT leader + FMOV (FPU opcode, LS class) follower, and the reverse
	.align 5
	.global _s_fmovfollow
_s_fmovfollow:
1:	mov	r0,r1
	fmov	fr2,fr3
	mov	r2,r3
	fmov	fr4,fr5
	dt	r4
	bf/s	1b
	nop
	rts
	nop

	.align 5
	.global _s_fmovlead
_s_fmovlead:
1:	fmov	fr2,fr3
	mov	r0,r1
	fmov	fr4,fr5
	mov	r2,r3
	dt	r4
	bf/s	1b
	nop
	rts
	nop

! one FTRV (vector unit, 4 array passes) per iteration: 0x13 / 0x15 of a multi-pass FPU instruction
	.align 5
	.global _s_ftrv
_s_ftrv:
1:	ftrv	xmtrx,fv4
	dt	r4
	bf/s	1b
	nop
	rts
	nop

! ---- RTE in 0x10 / 0x11 / 0x27: SSR = SR (unchanged state), SPC = the next label; the JMP twin is the reference
	.align 5
	.global _s_rte
_s_rte:
	stc	sr,r1
	mov.l	.Lrte_t,r2
1:	ldc	r1,ssr
	ldc	r2,spc
	rte
	nop
.Lrte_2:
	dt	r4
	bf/s	1b
	nop
	rts
	nop
	.align 2
.Lrte_t:	.long	.Lrte_2

	.align 5
	.global _s_rtejmp
_s_rtejmp:
	stc	sr,r1
	mov.l	.Lrj_t,r2
1:	ldc	r1,ssr
	ldc	r2,spc
	jmp	@r2
	nop
.Lrj_2:
	dt	r4
	bf/s	1b
	nop
	rts
	nop
	.align 2
.Lrj_t:	.long	.Lrj_2

! ---- 0x13 on micro-op instructions: STC.L GBR,@-R15 ; LDC.L @R15+,GBR (GBR unchanged)
	.align 5
	.global _s_stcl
_s_stcl:
1:	stc.l	gbr,@-r15
	ldc.l	@r15+,gbr
	dt	r4
	bf/s	1b
	nop
	rts
	nop

! MAC.W @r5+,@r6+ (two loads, one instruction), pointers put back
	.align 5
	.global _s_macw
_s_macw:
	mov	r5,r2
	mov	r6,r3
1:	mac.w	@r5+,@r6+
	mov	r2,r5
	mov	r3,r6
	dt	r4
	bf/s	1b
	nop
	rts
	nop

! ---- 0x0D / 0x09 / 0x02 for store-queue writes (r5 = an SQ0 address; nothing reaches memory without PREF)
	.align 5
	.global _s_sqw
_s_sqw:
1:	mov.l	r0,@r5
	mov.l	r0,@(4,r5)
	mov.l	r0,@(8,r5)
	mov.l	r0,@(12,r5)
	dt	r4
	bf/s	1b
	nop
	rts
	nop

! a full SQ0 (8 longs) and PREF: one 32-byte burst per iteration to the buffer QACR0 / r5 point at
	.align 5
	.global _s_sqpref
_s_sqpref:
1:	mov.l	r0,@r5
	mov.l	r0,@(4,r5)
	mov.l	r0,@(8,r5)
	mov.l	r0,@(12,r5)
	mov.l	r0,@(16,r5)
	mov.l	r0,@(20,r5)
	mov.l	r0,@(24,r5)
	mov.l	r0,@(28,r5)
	pref	@r5
	dt	r4
	bf/s	1b
	nop
	rts
	nop

! ---- 0x05 (OC write miss): stores to two P1 lines 16 KB apart (same OC index): 2 write misses per iteration
	.align 5
	.global _s_stmiss
_s_stmiss:
1:	mov.l	r0,@r5
	mov.l	r0,@r6
	dt	r4
	bf/s	1b
	nop
	rts
	nop

! ---- 0x18: one TRAPA #0x40 per iteration (a KOS trapa handler that returns)
	.align 5
	.global _s_trapa
_s_trapa:
1:	trapa	#0x40
	dt	r4
	bf/s	1b
	nop
	rts
	nop

! ---- 0x03 / 0x07 on TLB-miss exceptions (MMUCR.AT = 1; the C side installs the handlers and the MMU state).
! r5 = a P0 address: without a UTLB entry the access raises a UTLB miss and the handler skips it (SPC + 2); with
! translation off it would be a harmless access to the same physical RAM (P0 = the physical address).
	.align 5
	.global _s_dmiss_rd
_s_dmiss_rd:
1:	mov.l	@r5,r0
	dt	r4
	bf/s	1b
	nop
	rts
	nop

	.align 5
	.global _s_dmiss_wr
_s_dmiss_wr:
1:	mov.l	r0,@r5
	dt	r4
	bf/s	1b
	nop
	rts
	nop

! r5 = a P0 alias of _s_istub's physical page: without an entry the fetch raises an ITLB miss and the handler returns
! to PR; with an entry (or translation off) the stub runs and returns.
	.align 5
	.global _s_imiss
_s_imiss:
	sts.l	pr,@-r15
1:	jsr	@r5
	nop
	dt	r4
	bf/s	1b
	nop
	lds.l	@r15+,pr
	rts
	nop

	.balign	4096
	.global _s_istub
_s_istub:
	rts
	nop
	.space	4096 - 4

! ---- MMU helpers (as ubcbrk_s.s _t_a7: 8 instructions after every MMUCR write)
! mmu_on(pteh r4, ptel r5): MMUCR = TI (invalidate, URC 0); if r5 != 0, LDTLB {PTEH r4, PTEL r5, PTEA 0}; MMUCR = AT.
! Returns the old MMUCR.  mmu_off(old r4): MMUCR = TI (AT 0), then MMUCR = old.  PTEH / PTEL / PTEA are the
! caller's to save and restore (pmcsurvey.c).
	.align 5
	.global _mmu_on, _mmu_off
_mmu_on:
	mov.l	.Lm_mmucr,r1
	mov.l	.Lm_pteh,r2
	mov.l	.Lm_ptea,r3
	mov.l	@r1,r7
	mov	#4,r0
	mov.l	r0,@r1
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	tst	r5,r5
	bt	2f
	mov	#0,r0
	mov.l	r0,@r3
	mov.l	r4,@r2
	mov.l	r5,@(4,r2)
	nop
	ldtlb
	nop
2:	mov	#1,r0
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
	mov	r7,r0
_mmu_off:
	mov.l	.Lm_mmucr,r1
	mov	#4,r0
	mov.l	r0,@r1
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	mov.l	r4,@r1
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
	.align 2
.Lm_mmucr:	.long	0xFF000010
.Lm_pteh:	.long	0xFF000000
.Lm_ptea:	.long	0xFF000034
