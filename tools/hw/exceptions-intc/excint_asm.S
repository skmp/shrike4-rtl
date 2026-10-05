/* excint_asm.S -- raw exception vector table, trampoline and test snippets for excint.c.
 *
 * The C side calls run_asm(fn, a0, a1, a2) with interrupts masked (irq_disable).  run_asm saves the callee-saved
 * state, points VBR at rawvbr, calls fn(a0, a1, a2) and restores everything.  Any exception or interrupt taken while
 * VBR = rawvbr lands in `common`, which appends one 64-byte record to g_recs and then either
 *   - resumes at `recover` (SPC = g_resume, SSR = g_resume_sr: privileged, bank 0, BL = 0, IMASK = 15), or
 *   - for an interrupt with g_irq_mode = 0: clears the source (TMU1 UNF/UNIE or WDT IOVF/TME) and returns to SPC.
 * An exception always resumes (g_exc_mode is not used for anything else).  After 24 records the handler always
 * resumes, so an interrupt storm cannot hang the console.
 * Bank-1 R4 holds the PMCTR0L address (0xFF100008, KOS keeps PRFC0 counting CPU cycles) so the first handler
 * instruction can timestamp the entry.  The handler never touches the FPU (SR.FD may be 1) and never faults.
 */
        .text

/* ---------------------------------------------------------------- data */
        .data
        .balign 32
        .global _g_recs
_g_recs:        .skip   64*32
        .global _g_nrec, _g_irq_mode, _g_resume, _g_resume_sr, _g_ret, _g_recovered, _g_fpscr_after, _g_save
_g_nrec:        .long   0
_g_irq_mode:    .long   0
_g_resume:      .long   0
_g_resume_sr:   .long   0
_g_ret:         .long   0
_g_recovered:   .long   0
_g_fpscr_after: .long   0
        .global _g_fr_after
_g_fr_after:    .long   0, 0
_g_save:        .skip   64

        .text
/* ---------------------------------------------------------------- vector table */
        .balign 1024
        .global _rawvbr
_rawvbr:
        .skip   0x100
        mov.l   @r4,r6          /* +0x100 general exceptions (first instruction must not be a branch) */
        bra     common
        mov     #1,r7
        .skip   0x400-0x106
        mov.l   @r4,r6          /* +0x400 TLB miss */
        bra     common
        mov     #2,r7
        .skip   0x600-0x406
        mov.l   @r4,r6          /* +0x600 interrupts */
        bra     common
        mov     #3,r7
common:
        mov.l   .Lh_tcnt1,r1
        mov.l   @r1,r5          /* TCNT1 */
        mov.l   .Lh_nrec,r0
        mov.l   @r0,r2
        mov     #24,r1
        cmp/hs  r1,r2
        bt      resume          /* too many records: stop whatever is going on */
        mov     r2,r3
        shll2   r3
        shll2   r3
        shll2   r3              /* *64 */
        mov.l   .Lh_recs,r1
        add     r1,r3
        add     #1,r2
        mov.l   r2,@r0
        mov.l   r7,@(0,r3)
        mov.l   .Lh_ff000000,r1
        mov.l   @(0x24,r1),r0
        mov.l   r0,@(4,r3)      /* EXPEVT */
        mov.l   @(0x28,r1),r0
        mov.l   r0,@(8,r3)      /* INTEVT */
        mov.l   @(0x20,r1),r0
        mov.l   r0,@(12,r3)     /* TRA */
        mov.l   @(0x0c,r1),r0
        mov.l   r0,@(16,r3)     /* TEA */
        stc     spc,r0
        mov.l   r0,@(20,r3)
        stc     ssr,r0
        mov.l   r0,@(24,r3)
        stc     sgr,r0
        mov.l   r0,@(28,r3)
        stc     sr,r0
        mov.l   r0,@(32,r3)
        sts     pr,r0
        mov.l   r0,@(36,r3)
        mov.l   r15,@(40,r3)
        stc     r0_bank,r0
        mov.l   r0,@(44,r3)
        stc     r1_bank,r0
        mov.l   r0,@(48,r3)
        stc     r2_bank,r0
        mov.l   r0,@(52,r3)
        mov.l   r6,@(56,r3)     /* PMCTR0L at entry */
        mov.l   r5,@(60,r3)     /* TCNT1 */
        mov     r7,r0
        cmp/eq  #3,r0
        bf      resume          /* exceptions always resume */
        mov.l   .Lh_ff000000,r1
        mov.l   @(0x28,r1),r0   /* INTEVT */
        mov.w   .Lh_420,r2
        cmp/eq  r2,r0
        bt      irq_tmu1
        mov.w   .Lh_560,r2
        cmp/eq  r2,r0
        bt      irq_wdt
        bra     resume          /* unexpected interrupt: resume with IMASK = 15, it stays pending for KOS */
        nop
irq_tmu1:
        mov.l   .Lh_tcr1,r1
        mov.w   @r1,r0
        mov.w   .Lh_nmask,r2
        and     r2,r0           /* UNF = 0, UNIE = 0 */
        mov.w   r0,@r1
        bra     irq_done
        mov.w   @r1,r0          /* read back (write completion) */
irq_wdt:
        mov.l   .Lh_wtcsr,r1
        mov.w   .Lh_a500,r0
        mov.w   r0,@r1          /* TME = 0, IOVF = 0, interval mode */
        mov.b   @r1,r0
irq_done:
        mov.l   .Lh_irqmode,r1
        mov.l   @r1,r0
        cmp/eq  #1,r0
        bt      resume
        rte
        nop
resume:
        mov.l   .Lh_resume,r1
        mov.l   @r1,r0
        ldc     r0,spc
        mov.l   .Lh_resume_sr,r1
        mov.l   @r1,r0
        ldc     r0,ssr
        rte
        nop
        .balign 4
.Lh_tcnt1:      .long   0xffd80018
.Lh_tcr1:       .long   0xffd8001c
.Lh_wtcsr:      .long   0xffc0000c
.Lh_ff000000:   .long   0xff000000
.Lh_nrec:       .long   _g_nrec
.Lh_recs:       .long   _g_recs
.Lh_irqmode:    .long   _g_irq_mode
.Lh_resume:     .long   _g_resume
.Lh_resume_sr:  .long   _g_resume_sr
.Lh_420:        .word   0x420
.Lh_560:        .word   0x560
.Lh_nmask:      .word   0xfedf
.Lh_a500:       .word   0xa500

/* ---------------------------------------------------------------- trampoline */
/* uint32_t run_asm(void *fn, uint32_t a0, uint32_t a1, uint32_t a2) */
        .balign 4
        .global _run_asm
_run_asm:
        mov.l   .Lt_save,r0
        mov.l   r8,@(0,r0)
        mov.l   r9,@(4,r0)
        mov.l   r10,@(8,r0)
        mov.l   r11,@(12,r0)
        mov.l   r12,@(16,r0)
        mov.l   r13,@(20,r0)
        mov.l   r14,@(24,r0)
        mov.l   r15,@(28,r0)
        sts     pr,r1
        mov.l   r1,@(32,r0)
        stc     sr,r1
        mov.l   r1,@(36,r0)
        stc     vbr,r1
        mov.l   r1,@(40,r0)
        sts     fpscr,r1
        mov.l   r1,@(44,r0)
        stc     gbr,r1
        mov.l   r1,@(48,r0)
        sts     mach,r1
        mov.l   r1,@(52,r0)
        sts     macl,r1
        mov.l   r1,@(56,r0)
        stc     sr,r1
        mov     #-16,r2
        extu.b  r2,r2
        or      r2,r1
        mov.l   .Lt_resume_sr,r2
        mov.l   r1,@r2
        mov.l   .Lt_recover,r1
        mov.l   .Lt_resume,r2
        mov.l   r1,@r2
        mov.l   .Lt_pmctr,r1
        ldc     r1,r4_bank
        mov.l   .Lt_rawvbr,r1
        ldc     r1,vbr
        mov     r4,r1
        mov     r5,r4
        mov     r6,r5
        mov     r7,r6
        jsr     @r1
        nop
        bra     finish
        mov     #0,r1
recover:
        mov     #1,r1
finish:
        mov.l   .Lt_save,r2
        mov.l   @(36,r2),r3
        mov     #-16,r5
        extu.b  r5,r5
        or      r5,r3
        ldc     r3,sr           /* entry SR, IMASK 15: MD = 1, RB = 0, BL = 0, FD = 0 */
        mov.l   .Lt_ret,r3
        mov.l   r0,@r3
        mov.l   .Lt_recov,r3
        mov.l   r1,@r3
        sts     fpscr,r0
        mov.l   .Lt_fpscr_after,r3
        mov.l   r0,@r3
        flds    fr0,fpul        /* FR0 / FR1 after the snippet (FD = 0 here; FLDS ignores SZ) */
        sts     fpul,r0
        mov.l   r0,@(4,r3)
        flds    fr1,fpul
        sts     fpul,r0
        mov.l   r0,@(8,r3)
        mov.l   @(40,r2),r0
        ldc     r0,vbr
        mov.l   @(44,r2),r0
        lds     r0,fpscr
        mov.l   @(48,r2),r0
        ldc     r0,gbr
        mov.l   @(52,r2),r0
        lds     r0,mach
        mov.l   @(56,r2),r0
        lds     r0,macl
        mov.l   @(32,r2),r0
        lds     r0,pr
        mov.l   @(0,r2),r8
        mov.l   @(4,r2),r9
        mov.l   @(8,r2),r10
        mov.l   @(12,r2),r11
        mov.l   @(16,r2),r12
        mov.l   @(20,r2),r13
        mov.l   @(24,r2),r14
        mov.l   @(28,r2),r15
        mov.l   @(36,r2),r0
        ldc     r0,sr
        mov.l   .Lt_ret,r3
        rts
        mov.l   @r3,r0
        .balign 4
.Lt_save:       .long   _g_save
.Lt_resume_sr:  .long   _g_resume_sr
.Lt_resume:     .long   _g_resume
.Lt_recover:    .long   recover
.Lt_pmctr:      .long   0xff100008
.Lt_rawvbr:     .long   _rawvbr
.Lt_ret:        .long   _g_ret
.Lt_recov:      .long   _g_recovered
.Lt_fpscr_after: .long  _g_fpscr_after

/* ---------------------------------------------------------------- register facts */
/* t_regmask(uint32_t *out): writable bits of SR, VBR, SSR, SPC, DBR, GBR, FPSCR (write all ones, read back, restore) */
        .balign 4
        .global _t_regmask
_t_regmask:
        mov     #-1,r9
        stc     sr,r8
        ldc     r9,sr           /* MD RB BL FD IMASK all set: only r8-r15 used until SR is restored */
        stc     sr,r10
        ldc     r8,sr
        mov.l   r10,@(0,r4)
        stc     vbr,r8
        ldc     r9,vbr
        stc     vbr,r10
        ldc     r8,vbr
        mov.l   r10,@(4,r4)
        stc     ssr,r8
        ldc     r9,ssr
        stc     ssr,r10
        ldc     r8,ssr
        mov.l   r10,@(8,r4)
        stc     spc,r8
        ldc     r9,spc
        stc     spc,r10
        ldc     r8,spc
        mov.l   r10,@(12,r4)
        stc     dbr,r8
        ldc     r9,dbr
        stc     dbr,r10
        ldc     r8,dbr
        mov.l   r10,@(16,r4)
        stc     gbr,r8
        ldc     r9,gbr
        stc     gbr,r10
        ldc     r8,gbr
        mov.l   r10,@(20,r4)
        sts     fpscr,r8
        lds     r9,fpscr
        sts     fpscr,r10
        lds     r8,fpscr
        mov.l   r10,@(24,r4)
        stc     sgr,r10
        mov.l   r10,@(28,r4)
        mov.l   r15,@(32,r4)
        rts
        mov     #0,r0

/* ---------------------------------------------------------------- general exceptions, codes and SPC */
#define GL(x) .global _##x; _##x:
        .balign 4
GL(t_trapa)
        mov     #0,r0
GL(L_trapa)
        trapa   #0x5a
        rts
        nop

GL(t_ill)
GL(L_ill)
        .word   0xfffd
        rts
        nop

GL(t_zero)
        mov     #0,r0
GL(L_zero)
        .word   0x0000
        rts
        mov     #7,r0

GL(t_slot_ill)
GL(L_slot_ill)
        bra     1f
        .word   0xfffd
1:      rts
        nop

GL(t_slot_trapa)
GL(L_slot_trapa)
        bra     1f
        trapa   #1
1:      rts
        nop

GL(t_slot_bra)
GL(L_slot_bra)
        bra     1f
        .word   0xa000          /* bra to slot+4 */
1:      rts
        nop

GL(t_slot_mova)
GL(L_slot_mova)
        bra     1f
        .word   0xc701          /* mova @(4,pc),r0 */
1:      rts
        nop

GL(t_slot_movpc)
GL(L_slot_movpc)
        bra     1f
        .word   0x9001          /* mov.w @(2,pc),r0 */
1:      rts
        nop

GL(t_slot_ldcsr)
        stc     sr,r1
GL(L_slot_ldcsr)
        bra     1f
        ldc     r1,sr
1:      rts
        nop

GL(t_slot_bt)
GL(L_slot_bt)
        bra     1f
        .word   0x8900          /* bt to slot+4 */
1:      rts
        nop

GL(t_slot_zero)
GL(L_slot_zero)
        bra     1f
        .word   0x0000
1:      rts
        mov     #7,r0

/* ---------------------------------------------------------------- FPU disable (SR.FD = 1) */
#define SETFD   stc sr,r1; mov #1,r2; shll16 r2; shlr r2; or r2,r1; ldc r1,sr   /* FD = bit 15 */
GL(t_fd_fadd)
        SETFD
GL(L_fd_fadd)
        fadd    fr0,fr0
        rts
        nop
GL(t_fd_lds)
        sts     fpscr,r0        /* r0 = a sane FPSCR in case lds did not trap */
        SETFD
GL(L_fd_lds)
        lds     r0,fpscr
        rts
        nop
GL(t_fd_stsfpul)
        SETFD
GL(L_fd_stsfpul)
        sts     fpul,r0
        rts
        nop
GL(t_fd_fschg)
        SETFD
GL(L_fd_fschg)
        fschg
        rts
        nop
GL(t_fd_fmov)
        SETFD
GL(L_fd_fmov)
        fmov.s  @r15,fr0
        rts
        nop
GL(t_fd_fffd)
        SETFD
GL(L_fd_fffd)
        .word   0xfffd
        rts
        nop
GL(t_fd_slot)
        SETFD
GL(L_fd_slot)
        bra     1f
        fadd    fr0,fr0
1:      rts
        nop
GL(t_fd_slot_fffd)
        SETFD
GL(L_fd_slot_fffd)
        bra     1f
        .word   0xfffd
1:      rts
        nop
GL(t_fd_ldsl)
        SETFD
        mov     r15,r1
GL(L_fd_ldsl)
        lds.l   @r1+,fpul
        rts
        nop

/* ---------------------------------------------------------------- FPU exceptions (r4 = FPSCR to load) */
GL(t_fpe_fadd)
        fldi1   fr0
        fldi1   fr1
        lds     r4,fpscr
GL(L_fpe_fadd)
        fadd    fr1,fr0
        rts
        nop
GL(t_fpe_slot)
        fldi1   fr0
        fldi1   fr1
        lds     r4,fpscr
GL(L_fpe_slot)
        bra     1f
        fadd    fr1,fr0
1:      rts
        nop
GL(t_fpe_sqrtneg)
        fldi1   fr0
        fneg    fr0
        lds     r4,fpscr
GL(L_fpe_sqrtneg)
        fsqrt   fr0
        rts
        nop
GL(t_fpe_sqrtpos)
        fldi1   fr0
        lds     r4,fpscr
GL(L_fpe_sqrtpos)
        fsqrt   fr0
        rts
        nop
GL(t_fpe_div0)
        fldi1   fr0
        fldi0   fr1
        lds     r4,fpscr
GL(L_fpe_div0)
        fdiv    fr1,fr0
        rts
        nop
GL(t_fpe_div1)
        fldi1   fr0
        fldi1   fr1
        lds     r4,fpscr
GL(L_fpe_div1)
        fdiv    fr1,fr0
        rts
        nop

/* ---------------------------------------------------------------- data address errors (r4 = 64-byte aligned buffer) */
GL(t_dae_wrd)
        mov     r4,r1
        add     #1,r1
GL(L_dae_wrd)
        mov.w   @r1,r0
        rts
        nop
GL(t_dae_lwr)
        mov     r4,r1
        add     #2,r1
GL(L_dae_lwr)
        mov.l   r0,@r1
        rts
        nop
GL(t_dae_lpost)
        mov     r4,r1
        add     #2,r1
        mov     #0,r2
GL(L_dae_lpost)
        mov.l   @r1+,r2
        rts
        nop
GL(t_dae_lpre)
        mov     r4,r1
        add     #6,r1
GL(L_dae_lpre)
        mov.l   r0,@-r1
        rts
        nop
GL(t_dae_fmovd_rd)
        mov.l   .Lfpsz,r2
        lds     r2,fpscr
        mov     r4,r1
        add     #4,r1
GL(L_dae_fmovd_rd)
        .word   0xf018          /* fmov @r1,dr0 (SZ = 1) */
        rts
        nop
GL(t_dae_fmovd_wr)
        mov.l   .Lfpsz,r2
        lds     r2,fpscr
        mov     r4,r1
        add     #4,r1
GL(L_dae_fmovd_wr)
        .word   0xf10a          /* fmov dr0,@r1 (SZ = 1) */
        rts
        nop
GL(t_dae_fmovs)
        mov     r4,r1
        add     #2,r1
GL(L_dae_fmovs)
        fmov.s  @r1,fr0
        rts
        nop
GL(t_dae_movca)
        mov     r4,r1
        add     #2,r1
GL(L_dae_movca)
        movca.l r0,@r1
        rts
        nop
GL(t_dae_ocbwb)
        mov     r4,r1
        add     #1,r1
GL(L_dae_ocbwb)
        ocbwb   @r1
        rts
        mov     #7,r0
GL(t_dae_ocbp)
        mov     r4,r1
        add     #1,r1
GL(L_dae_ocbp)
        ocbp    @r1
        rts
        mov     #7,r0
GL(t_dae_ocbi)
        mov     r4,r1
        add     #1,r1
GL(L_dae_ocbi)
        ocbi    @r1
        rts
        mov     #7,r0
GL(t_dae_pref)
        mov     r4,r1
        add     #1,r1
GL(L_dae_pref)
        pref    @r1
        rts
        mov     #7,r0
GL(t_dae_tas)
        mov     r4,r1
        add     #1,r1
GL(L_dae_tas)
        tas.b   @r1
        rts
        mov     #7,r0
GL(t_dae_mac2)
        mov     r4,r1
        mov     r4,r2
        add     #2,r2
        clrmac
GL(L_dae_mac2)
        mac.l   @r1+,@r2+
        rts
        nop
GL(t_dae_mac1)
        mov     r4,r1
        add     #2,r1
        mov     r4,r2
        add     #6,r2
        clrmac
GL(L_dae_mac1)
        mac.l   @r1+,@r2+
        rts
        nop
GL(t_dae_macw2)
        mov     r4,r1
        mov     r4,r2
        add     #1,r2
        clrmac
GL(L_dae_macw2)
        mac.w   @r1+,@r2+
        rts
        nop
GL(t_dae_ldclsr)
        mov     r4,r1
        add     #2,r1
GL(L_dae_ldclsr)
        ldc.l   @r1+,sr
        rts
        nop
        .balign 4
.Lfpsz: .long   0x00140000

/* ---------------------------------------------------------------- delay-slot faults (r4 = buffer) */
GL(t_ds_bra)
        mov     r4,r1
        add     #1,r1
GL(L_ds_bra)
        bra     1f
        mov.w   @r1,r0
1:      rts
        nop
GL(t_ds_bsr)
        sts     pr,r13
        mov.l   .Lmark,r2
        lds     r2,pr
        mov     r4,r1
        add     #1,r1
GL(L_ds_bsr)
        bsr     2f
        mov.w   @r1,r0
        lds     r13,pr
        rts
        nop
2:      rts
        nop
GL(t_ds_jsr)
        sts     pr,r13
        mov.l   .Lsubrts,r3
        mov.l   .Lmark,r2
        lds     r2,pr
        mov     r4,r1
        add     #1,r1
GL(L_ds_jsr)
        jsr     @r3
        mov.w   @r1,r0
        lds     r13,pr
        rts
        nop
GL(t_ds_bfs_nt)
        sett
        mov     r4,r1
        add     #1,r1
GL(L_ds_bfs_nt)
        bf/s    1f
        mov.w   @r1,r0
1:      rts
        nop
GL(t_ds_bts_t)
        sett
        mov     r4,r1
        add     #1,r1
GL(L_ds_bts_t)
        bt/s    1f
        mov.w   @r1,r0
1:      rts
        nop
GL(t_ds_rts)
        sts     pr,r13
        mov     r4,r1
        add     #1,r1
        mov.l   .Lrtsback,r2
        lds     r2,pr
GL(L_ds_rts)
        rts
        mov.w   @r1,r0
GL(rtsback)
        lds     r13,pr
        rts
        nop
GL(t_ds_jmp_iae)
        mov     #0,r0
        mov.l   .Loddtgt,r2
GL(L_ds_jmp_iae)
        jmp     @r2
        add     #1,r0
GL(oddtgt)
        rts
        nop
GL(t_ds_rts_iae)
        sts     pr,r13
        mov     #0,r0
        mov.l   .Loddtgt2,r2
        lds     r2,pr
GL(L_ds_rts_iae)
        rts
        add     #1,r0
GL(oddtgt2)
        lds     r13,pr
        rts
        nop
GL(subrts)
        rts
        nop
        .balign 4
.Lmark:     .long   0x13572468
.Lsubrts:   .long   _subrts
.Lrtsback:  .long   _rtsback
.Loddtgt:   .long   _oddtgt+1
.Loddtgt2:  .long   _oddtgt2+1

/* ---------------------------------------------------------------- RTE delay slot: which RB / T does the slot see */
GL(t_rte_rb)
        mov.l   .Lx11,r0
        mov.l   .Lx22,r1
        ldc     r1,r0_bank      /* bank 1 R0 = 0x22, bank 0 R0 = 0x11 */
        stc     sr,r2
        mov.l   .Lrbbit,r3
        or      r3,r2
        ldc     r2,ssr          /* SSR = SR with RB = 1 */
        mov.l   .Lrterb1,r3
        ldc     r3,spc
        mov     #0,r8
        rte
        mov     r0,r8           /* slot: 0x22 if it runs with the new RB */
GL(rterb1)
        stc     sr,r2           /* RB = 1 here: R2/R3 are bank 1, fine */
        mov.l   .Lnrbbit,r3
        and     r3,r2
        ldc     r2,sr
        rts
        mov     r8,r0
GL(t_rte_t)
        stc     sr,r2
        mov     #1,r3
        or      r3,r2
        ldc     r2,ssr          /* SSR = SR with T = 1 */
        mov.l   .Lrtet1,r3
        ldc     r3,spc
        clrt
        mov     #5,r8
        rte
        movt    r8              /* slot: 1 if it sees the new T */
GL(rtet1)
        rts
        mov     r8,r0
        .balign 4
.Lx11:      .long   0x11
.Lx22:      .long   0x22
.Lrbbit:    .long   0x20000000
.Lnrbbit:   .long   0xdfffffff
.Lrterb1:   .long   _rterb1
.Lrtet1:    .long   _rtet1

/* ---------------------------------------------------------------- user mode: t_user(pc, sr, r1 arg) */
GL(t_user)
        mov     r6,r1
        mov     #0,r8
        mov     #0,r0
        ldc     r5,ssr
        ldc     r4,spc
        rte
        nop
        .balign 4
GL(u_rd)
        mov.l   @r1,r0
        trapa   #0xee
GL(u_wr)
        mov.l   r0,@r1
        trapa   #0xee
GL(u_pref)
        pref    @r1
        trapa   #0xee
GL(u_ocbi)
        ocbi    @r1
        trapa   #0xee
GL(u_stcsr)
        stc     sr,r0
        trapa   #0xee
GL(u_gbr)
        stc     gbr,r0
        ldc     r0,gbr
        trapa   #0xee
GL(u_stcsgr)
        stc     sgr,r0
        trapa   #0xee
GL(u_ldtlb)
        ldtlb
        trapa   #0xee
GL(u_sleep)
        sleep
        trapa   #0xee
GL(u_rte)
        mov     r8,r0
        cmp/eq  #1,r0
        bt      1f
        mov     #1,r8
GL(L_u_rte)
        rte
        nop
1:      trapa   #0xef
GL(u_slot)
        bra     1f
        stc     sr,r0
1:      trapa   #0xee
GL(u_nop)
        nop
        trapa   #0xee

/* ---------------------------------------------------------------- interrupt acceptance chains */
#define ADD8  add #1,r0; add #1,r0; add #1,r0; add #1,r0; add #1,r0; add #1,r0; add #1,r0; add #1,r0
#define ADD64 ADD8; ADD8; ADD8; ADD8; ADD8; ADD8; ADD8; ADD8
/* t_chain_ldcsr(sr, pmctr) */
GL(t_chain_ldcsr)
        mov     #0,r0
        mov.l   @r5,r1
GL(L_cl0)
        ldc     r4,sr
GL(L_cl1)
        ADD64
        rts
        nop
/* t_chain_ldclsr(&sr, pmctr) */
GL(t_chain_ldclsr)
        mov     #0,r0
        mov.l   @r5,r1
GL(L_cll0)
        ldc.l   @r4+,sr
GL(L_cll1)
        ADD64
        rts
        nop
/* t_chain_rte(sr, pmctr) */
GL(t_chain_rte)
        mov.l   .Lcr1,r2
        ldc     r2,spc
        ldc     r4,ssr
        mov     #0,r0
        mov.l   @r5,r1
GL(L_cr0)
        rte
        nop
GL(L_cr1)
        ADD64
        rts
        nop
        .balign 4
.Lcr1:  .long   _L_cr1
/* t_chain_bl(sr_bl0, sr_bl1, pmctr) */
GL(t_chain_bl)
        ldc     r5,sr
        mov     #0,r0
        mov.l   @r6,r1
GL(L_cb0)
        ldc     r4,sr
GL(L_cb1)
        ADD64
        rts
        nop
/* t_chain_st16(addr, val, sr) */
GL(t_chain_st16)
        ldc     r6,sr
        mov     #0,r0
GL(L_cs0)
        mov.w   r5,@r4
GL(L_cs1)
        ADD64
        ADD64
        rts
        nop
/* t_chain_st8(addr, val, sr) */
GL(t_chain_st8)
        ldc     r6,sr
        mov     #0,r0
        mov.l   .Lpm0,r3
        mov.l   @r3,r1
GL(L_c80)
        mov.b   r5,@r4
GL(L_c81)
        ADD64
        ADD64
        ADD64
        ADD64
        rts
        nop
        .balign 4
.Lpm0:  .long   0xff100008
/* t_twoirq(sr) */
GL(t_twoirq)
        mov     #0,r0
        ldc     r4,sr
GL(L_tw1)
        ADD64
        rts
        nop
/* t_prio_ill(sr): interrupt pending and unmasked by LDC SR, then an illegal instruction */
GL(t_prio_ill)
        mov     #0,r0
        ldc     r4,sr
GL(L_pi)
        .word   0xfffd
        ADD8
        rts
        nop
/* t_brapN(sr): N adds after the unmasking LDC SR, then BRA + slot */
#define ADD1  add #1,r0
#define MOV0  mov #0,r0
#define BRAP(n) \
GL(t_brap##n); MOV0; ldc r4,sr; .rept n; ADD1; .endr; \
GL(L_brap##n); bra 1f; ADD1; 1: ADD8; ADD8; ADD8; ADD8; rts; nop
BRAP(0)
BRAP(1)
BRAP(2)
BRAP(3)
BRAP(4)
BRAP(5)
BRAP(6)
BRAP(7)
/* t_sleep(tstr, val, sr): start TMU1, SLEEP; the handler records SPC and the entry time */
GL(t_sleep)
        ldc     r6,sr
        mov.l   .Lpm1,r3
        mov     #0,r0
        mov.l   @r3,r1
        mov.b   r5,@r4
GL(L_sl)
        sleep
GL(L_sl2)
        ADD8
        rts
        nop
/* t_busy(tstr, val, sr): same with a busy loop instead of SLEEP */
GL(t_busy)
        ldc     r6,sr
        mov.l   .Lpm1,r3
        mov.l   .Lbusyn,r2
        mov     #0,r0
        mov.l   @r3,r1
        mov.b   r5,@r4
GL(L_bz)
1:      dt      r2
        bf      1b
        rts
        nop
        .balign 4
.Lpm1:   .long  0xff100008
.Lbusyn: .long  1000000
/* t_wait(sr, count) */
GL(t_wait)
        ldc     r4,sr
GL(L_wt)
1:      dt      r5
        bf      1b
        rts
        mov     #0,r0

/* ---------------------------------------------------------------- exception entry cost (r4 = PMCTR0L address) */
GL(t_cost_base)
        mov.l   @r4,r1
        mov.l   @r4,r2
        sub     r1,r2
        rts
        mov     r2,r0
GL(t_cost_trapa)
        mov.l   @r4,r1
        trapa   #1
        rts
        nop
GL(t_cost_ill)
        mov.l   @r4,r1
        .word   0xfffd
        rts
        nop
GL(t_cost_dae)
        mov.l   @r4,r1
        mov.w   @r5,r0
        rts
        nop
GL(t_cost_fd)
        SETFD
        mov.l   @r4,r1
        fadd    fr0,fr0
        rts
        nop
GL(t_cost_fpe)
        fldi1   fr0
        lds     r5,fpscr
        mov.l   @r4,r1
        fadd    fr0,fr0
        rts
        nop
        .balign 4
