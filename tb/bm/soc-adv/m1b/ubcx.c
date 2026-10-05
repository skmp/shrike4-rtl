/* ubcx.c -- tb/bm/soc-adv/m1b (M1b-verify's, the M1b gate's since M1b-fix):
 * the UBC against the other M1b units, interrupts, SLEEP and SR.BL (hm 20.3; SOC.md 4.3.2).
 *   A  an operand-address read break on SCFRDR2 (a read with a side effect: the pop): the load completes once
 *      (hm 20.3.7 (4): SPC = the next instruction), R0 holds the popped byte, the FIFO count dropped by exactly one.
 *   B  a pre-execution break on that load: it does not execute (hm 20.3.7 (1): SPC = the load, no pop); after the
 *      handler clears the channel, the load pops once.
 *   C  a data break (channel B, DBEB: address + data) on the second of three SCFTDR2 stores (11, 5A, 22): the loopback
 *      receives 11 5A 22, each once (a store the break suppresses must not reach the SCIF, hm 20.3.7 (5)); CMFB.
 *   D  a data break on a RAM load while an interrupt becomes acceptable around it (LDC SR lowers IMASK k NOPs before
 *      the load, k = 0..24; TMU0 underflow pending at level 5): hm 20.3.7 (5): "if exception 2 is from a source not
 *      synchronized with an instruction (external interrupt or peripheral module interrupt), exception 1 [the break]
 *      is used for the exception jump destination and the value written to EXPEVT / INTEVT".  So the first entry whose
 *      SPC lies after the load must be the break.  A control run with BDRB not matching: no break in any k.
 *   E  SLEEP: a pre-execution break on the SLEEP (SPC = SLEEP; then it sleeps and wakes at SLEEP + 2); a pre-execution
 *      break on the instruction after it (the wake-up entry first, then the break, both SPC = SLEEP + 2); a post-
 *      execution break on the SLEEP and an operand break one instruction before it (hm 20.3.9 (1), (2): "do not"; the
 *      RTL must not hang, SOC.md 9.18): reported.
 *   F  hm 20.3.9 (3) a-d, on the instructions that change SR.BL (LDC.L @Rm+,SR, LDC Rm,SR): a pre-execution break
 *      tests BL before, a post-execution break is masked by BL before or after, an operand break by BL after.  So an
 *      operand break on LDC.L @Rm+,SR is masked for BL 0 -> 1 (CMFA set, no break) and accepted for 1 -> 0 (SPC = the
 *      next instruction); a post-execution break on either is masked for 0 -> 1 and 1 -> 0; controls with BL 0 -> 0
 *      are accepted.  (M1b-verify F1: the RTL tested one BL at the commit point; each case also builds alone as
 *      ubcx_f<n>, so that its lockstep verdict stands on its own.)
 */
#include "m1b.h"

/* ---- the code under test (asm with labels for the SPC checks) */
__asm__(
"        .text\n"
"        .balign 32\n"
"        .global _u_ld, _u_ld_n\n"
"_u_ld:  mov.b   @r4,r0\n"                 /* A / B: the load of SCFRDR2 */
"_u_ld_n: nop\n"
"        rts\n"
"        nop\n"
"        .balign 32\n"
"        .global _u_st3, _u_st3_b, _u_st3_c\n"
"_u_st3: mov.b   r5,@r4\n"                 /* C: three stores to SCFTDR2 */
"_u_st3_b: mov.b r6,@r4\n"
"_u_st3_c: mov.b r7,@r4\n"
"        rts\n"
"        nop\n"
"        .balign 32\n"
"        .global _u_dbi, _u_dbi_ld\n"
"_u_dbi: mov     #24,r0\n"                 /* D: r4 SR IMASK 0, r5 &word, r6 k, r7 SR IMASK 15 */
"        sub     r6,r0\n"
"        shll    r0\n"
"        ldc     r4,sr\n"
"        braf    r0\n"
"        nop\n"
"        nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n"
"        nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n"
"_u_dbi_ld: mov.l @r5,r1\n"
"        add     #1,r1\n"
"        add     #1,r1\n"
"        add     #1,r1\n"
"        add     #1,r1\n"
"        nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n"
"        ldc     r7,sr\n"
"        rts\n"
"        mov     r1,r0\n"
"        .balign 32\n"
"        .global _u_slp, _u_slp_ld, _u_slp_s, _u_slp_n\n"
"_u_slp: ldc     r4,sr\n"                  /* E: r4 SR IMASK 0, r5 an address to load */
"        nop\n"
"_u_slp_ld: mov.l @r5,r1\n"
"_u_slp_s: sleep\n"
"_u_slp_n: nop\n"
"        rts\n"
"        nop\n"
"        .balign 32\n"
"        .global _u_bl01, _u_bl01_n, _u_bl10, _u_bl10_i, _u_bl10_n\n"
"_u_bl01: ldc.l  @r4+,sr\n"                /* F: r4 -> an SR with BL 1 (RB 0); r5 the SR to restore */
"_u_bl01_n: nop\n"
"        nop\n"
"        ldc     r5,sr\n"
"        rts\n"
"        nop\n"
"        .balign 32\n"
"_u_bl10: ldc    r5,sr\n"                  /* r5 an SR with BL 1, r4 -> an SR with BL 0 */
"        nop\n"
"_u_bl10_i: ldc.l @r4+,sr\n"
"_u_bl10_n: nop\n"
"        nop\n"
"        rts\n"
"        nop\n"
"        .balign 32\n"
"        .global _u_ldc01, _u_ldc01_n, _u_ldc10, _u_ldc10_i, _u_ldc10_n\n"
"_u_ldc01: ldc   r4,sr\n"                  /* F: r4 an SR with BL 1 (RB 0), r5 the SR to restore (BL 0) */
"_u_ldc01_n: nop\n"
"        nop\n"
"        ldc     r5,sr\n"
"        rts\n"
"        nop\n"
"        .balign 32\n"
"_u_ldc10: ldc   r4,sr\n"                  /* r4 an SR with BL 1, r5 an SR with BL 0 */
"        nop\n"
"_u_ldc10_i: ldc r5,sr\n"
"_u_ldc10_n: nop\n"
"        nop\n"
"        rts\n"
"        nop\n");
extern char u_ld[], u_ld_n[], u_st3[], u_st3_b[], u_st3_c[], u_dbi[], u_dbi_ld[], u_slp[], u_slp_ld[], u_slp_s[],
            u_slp_n[], u_bl01[], u_bl01_n[], u_bl10[], u_bl10_i[], u_bl10_n[], u_ldc01[], u_ldc01_n[], u_ldc10[],
            u_ldc10_i[], u_ldc10_n[];
uint32_t u_ld_f(uint32_t a) __asm__("_u_ld");
void u_st3_f(uint32_t a, uint32_t x, uint32_t y, uint32_t z) __asm__("_u_st3");
uint32_t u_dbi_f(uint32_t sr0, const volatile uint32_t *w, uint32_t k, uint32_t sr15) __asm__("_u_dbi");
void u_slp_f(uint32_t sr0, const volatile uint32_t *w) __asm__("_u_slp");
void u_bl01_f(const uint32_t *p, uint32_t sr) __asm__("_u_bl01");
void u_bl10_f(const uint32_t *p, uint32_t sr) __asm__("_u_bl10");
void u_ldc01_f(uint32_t sr1, uint32_t sr0) __asm__("_u_ldc01");
void u_ldc10_f(uint32_t sr1, uint32_t sr0) __asm__("_u_ldc10");

#define A(x) ((uint32_t)(uintptr_t)(x))

static volatile int g_brk_off;         /* the break handler clears BBRA (1), BBRB (2) */

static void h_all(irq_t code, irq_context_t *ctx, void *d) {
    (void)d;
    uint32_t a = 0, b = 0;
    if (code == 0x1E0) {
        uint16_t brcr = R16(BRCR);
        a = brcr;
        b = (uint32_t)(R16(SCFDR2) & 0x1F) | (ctx->r[0] & 0xFF) << 8;
        R16(BRCR) = (uint16_t)(brcr & 0x3FFF);                 /* CMFA / CMFB <- 0 */
        if (g_brk_off & 1) R16(BBRA) = 0;
        if (g_brk_off & 2) R16(BBRB) = 0;
        (void)R16(BRCR);
    } else if (code == 0x400) {
        R16(TCR(0)) = (uint16_t)(R16(TCR(0)) & ~UNF);
        (void)R16(TCR(0));
    } else {
        printf("UBCX unexpected code %03lx spc %08lx ssr %08lx\n", (unsigned long)code, (unsigned long)ctx->pc,
               (unsigned long)ctx->sr);
        R32(0xB8000000u) = 0x3FE;
        for (;;) {}
    }
    logent(code, ctx->pc, ctx->sr, a, b);
}

static void ubc_off(void) {
    R16(BBRA) = 0; R16(BBRB) = 0; R16(BRCR) = 0; (void)R16(BRCR);
}
/* the SPC relative to a label, for printing */
static long rel(uint32_t spc, const char *lab) { return (long)spc - (long)A(lab); }

static void pr_ents(const char *tag, const char *lab) {
    printf("%s n=%d", tag, g_nent);
    for (int i = 0; i < g_nent && i < NLOG; i++) {
        long r = rel(g_ent[i].spc, lab);
        printf(" [%03lx %s%ld bl=%lu brcr=%04lx]", (unsigned long)g_ent[i].code, r < 0 ? "-" : "+", r < 0 ? -r : r,
               (unsigned long)((g_ent[i].ssr >> 28) & 1), (unsigned long)g_ent[i].a);
    }
    printf("\n");
}

static volatile uint32_t g_word __attribute__((aligned(32))) = 0x13579BDFu;
static uint32_t g_sr_bl1[1] __attribute__((aligned(4)));
static uint32_t g_sr_bl0[1] __attribute__((aligned(4)));

int main(void) {
    bm_default = h_all;
    imask(15);
    ubc_off();
    R8(STBCR) = 0x00; (void)R8(STBCR);

    /* ---- A: operand read break on SCFRDR2 */
    scif_loop(0, 0, 0);
    R8(SCFTDR2) = 0x11; R8(SCFTDR2) = 0x22; R8(SCFTDR2) = 0x33;
    while ((R16(SCFDR2) & 0x1F) != 3) ;
    R32(BARA) = SCFRDR2; R8(BAMRA) = 0x04; R16(BBRA) = 0x25;     /* operand, read, byte; BASM */
    R16(BRCR) = 0; (void)R16(BRCR);
    g_nent = 0; g_brk_off = 1;
    uint32_t va = u_ld_f(SCFRDR2) & 0xFF;
    uint32_t fa = R16(SCFDR2) & 0x1F;
    printf("UBCX A r0=%02lx fdr=%lu brk_fdr=%lu brk_r0=%02lx ", (unsigned long)va, (unsigned long)fa,
           (unsigned long)(g_nent ? g_ent[0].b & 0x1F : 99), (unsigned long)(g_nent ? g_ent[0].b >> 8 : 0));
    pr_ents("ents", u_ld);

    /* ---- B: pre-execution break on the load */
    R32(BARA) = A(u_ld); R8(BAMRA) = 0x04; R16(BBRA) = 0x14;    /* instruction, read, any size */
    R16(BRCR) = 0; (void)R16(BRCR);
    g_nent = 0; g_brk_off = 1;
    uint32_t vb = u_ld_f(SCFRDR2) & 0xFF;
    uint32_t fb = R16(SCFDR2) & 0x1F;
    printf("UBCX B r0=%02lx fdr=%lu brk_fdr=%lu ", (unsigned long)vb, (unsigned long)fb,
           (unsigned long)(g_nent ? g_ent[0].b & 0x1F : 99));
    pr_ents("ents", u_ld);
    uint32_t vlast = R8(SCFRDR2);
    printf("UBCX AB last=%02lx fdr=%lu\n", (unsigned long)vlast, (unsigned long)(R16(SCFDR2) & 0x1F));
    ubc_off();

    /* ---- C: data break on the second store to SCFTDR2 */
    scif_loop(0, 0, 0);
    R32(BARB) = SCFTDR2; R8(BAMRB) = 0x04; R16(BBRB) = 0x29;    /* operand, write, byte; BASM */
    R32(BDRB) = 0x5A5A; R32(BDMRB) = 0;
    R16(BRCR) = 0x0080; (void)R16(BRCR);                        /* DBEB */
    g_nent = 0; g_brk_off = 2;
    u_st3_f(SCFTDR2, 0x11, 0x5A, 0x22);
    for (int t = 0; t < 100000 && (R16(SCFDR2) & 0x1F) < 3; t++) ;
    spin(3000);
    unsigned nr = R16(SCFDR2) & 0x1F;
    printf("UBCX C rx=%u:", nr);
    for (unsigned i = 0; i < nr; i++) printf(" %02x", R8(SCFRDR2));
    printf(" ");
    pr_ents("ents", u_st3);
    ubc_off();

    /* ---- D: data break vs an interrupt made acceptable around the load */
    const uint32_t sr = get_sr();
    const uint32_t sr0 = sr & ~0xF0u, sr15 = sr | 0xF0u;
    R16(IPRA) = 0x5000;                                          /* TMU0 at 5 */
    for (int ctl = 0; ctl < 4; ctl++) {          /* 0: the test, 1: BDRB not matching, 2: no IRQ, 3: IMASK 0, no source */
        /* arm the channel last: reading g_word with BBRB already set would break (an address-only break before DBEB) */
        const uint32_t w = g_word;
        g_brk_off = 0;
        R16(BBRB) = 0;
        R32(BDRB) = ctl == 1 ? w ^ 1u : w; R32(BDMRB) = 0;
        R16(BRCR) = 0x0080;
        R32(BARB) = A(&g_word); R8(BAMRB) = 0x04; R16(BBRB) = 0x27;  /* operand, read, long */
        (void)R16(BRCR);
        int bad = 0, nbrk = 0, nirq = 0, first_brk = 0;
        for (int k = 0; k <= 24; k++) {
            /* TMU0: one underflow pending, stopped */
            tmu_reset();
            R32(TCOR(0)) = 0xFFFFFFFFu; R32(TCNT(0)) = 0; R16(TCR(0)) = ctl == 3 ? 0 : UNIE;
            R8(TSTR) = 1; spin(10); R8(TSTR) = 0;
            while (!(R16(TCR(0)) & UNF)) ;
            g_nent = 0; g_brk_off = 0;
            u_dbi_f(ctl == 2 ? sr15 : sr0, &g_word, (uint32_t)k, sr15);
            /* the first entry after the load's address must be the break (unless no break is armed) */
            int seen_brk = 0, verdict_bad = 0;
            for (int i = 0; i < g_nent; i++) {
                if (g_ent[i].code == 0x1E0) { seen_brk = 1; nbrk++; if (i == 0) first_brk++; }
                else { nirq++; if (ctl != 1 && !seen_brk && g_ent[i].spc > A(u_dbi_ld)) verdict_bad = 1; }
            }
            if (ctl != 1 && !seen_brk) verdict_bad = 1;
            if (ctl == 1 && seen_brk) verdict_bad = 1;
            bad += verdict_bad;
            printf("UBCX D%s k=%d %sbrcr_after=%04x ", ctl == 1 ? "ctl" : ctl == 2 ? "noirq" : ctl == 3 ? "nosrc" : "", k, verdict_bad ? "BAD " : "",
                   R16(BRCR));
            R16(BRCR) = 0x0080; (void)R16(BRCR);
            pr_ents("ents", u_dbi_ld);
        }
        printf("UBCX D%s summary bad=%d brk=%d irq=%d brk_first=%d\n", ctl == 1 ? "ctl" : ctl == 2 ? "noirq" : ctl == 3 ? "nosrc" : "", bad, nbrk,
               nirq, first_brk);
    }
    ubc_off();
    R16(IPRA) = 0;

    /* ---- E: SLEEP */
    R16(IPRA) = 0x5000;
    for (int e = 0; e < 4; e++) {
        static const char *nm[4] = {"pre_on_sleep", "pre_after_sleep", "post_on_sleep", "operand_before_sleep"};
        tmu_reset();
        R32(TCOR(0)) = 0xFFFFFFFFu; R32(TCNT(0)) = 400; R16(TCR(0)) = UNIE;   /* ~6400 Icyc */
        switch (e) {
        case 0: R32(BARA) = A(u_slp_s); R8(BAMRA) = 0x04; R16(BBRA) = 0x14; R16(BRCR) = 0x0000; break;
        case 1: R32(BARA) = A(u_slp_n); R8(BAMRA) = 0x04; R16(BBRA) = 0x14; R16(BRCR) = 0x0000; break;
        case 2: R32(BARA) = A(u_slp_s); R8(BAMRA) = 0x04; R16(BBRA) = 0x14; R16(BRCR) = 0x0400; break;  /* PCBA */
        case 3: R32(BARA) = A(&g_word); R8(BAMRA) = 0x04; R16(BBRA) = 0x27; R16(BRCR) = 0x0000; break;
        }
        (void)R16(BRCR);
        g_nent = 0; g_brk_off = 1;
        R8(TSTR) = 1;
        u_slp_f(sr0, &g_word);
        imask(15);
        R8(TSTR) = 0;
        printf("UBCX E %s ", nm[e]);
        pr_ents("ents", u_slp_s);
        ubc_off();
    }
    R16(IPRA) = 0;

    /* ---- F: the BL a break tests (hm 20.3.9 (3) a-d: pre-execution BL before; post-execution masked by BL before or
     * after; operand access BL after), on the instructions that change BL: LDC.L @Rm+,SR and LDC Rm,SR */
#ifndef FCASES
#define FCASES 0x7FF
#endif
    g_sr_bl0[0] = sr15 & ~0x30000000u;                           /* BL 0, RB 0, IMASK 15 */
    g_sr_bl1[0] = (sr15 & ~0x20000000u) | 0x10000000u;           /* BL 1, RB 0 (the bank must not switch) */
    for (int f = 0; f < 11; f++) {
        static const char *nm[11] = {"op_ldcl_bl01", "op_ldcl_bl10", "post_ldc_bl01", "post_ldc_bl10", "pre_ldc_bl01",
                                     "pre_ldc_bl10", "post_ldcl_bl01", "post_ldcl_bl10", "op_ldcl_bl00",
                                     "post_ldcl_bl00", "post_ldc_bl00"};
        static const char *want[11] = {"masked", "accepted", "masked", "masked", "accepted", "masked", "masked",
                                       "masked", "accepted", "accepted", "accepted"};
        if (!((FCASES >> f) & 1)) continue;
        uint32_t bar; uint16_t bbr, brcr; const char *lab;
        switch (f) {
        case 0: bar = A(g_sr_bl1); bbr = 0x27; brcr = 0; lab = u_bl01_n; break;       /* operand read long */
        case 1: bar = A(g_sr_bl0); bbr = 0x27; brcr = 0; lab = u_bl10_n; break;
        case 2: bar = A(u_ldc01); bbr = 0x14; brcr = 0x0400; lab = u_ldc01_n; break; /* instruction, PCBA: post */
        case 3: bar = A(u_ldc10_i); bbr = 0x14; brcr = 0x0400; lab = u_ldc10_n; break;
        case 4: bar = A(u_ldc01); bbr = 0x14; brcr = 0; lab = u_ldc01; break;         /* pre */
        case 5: bar = A(u_ldc10_i); bbr = 0x14; brcr = 0; lab = u_ldc10_i; break;
        case 6: bar = A(u_bl01); bbr = 0x14; brcr = 0x0400; lab = u_bl01_n; break;    /* post on LDC.L */
        case 7: bar = A(u_bl10_i); bbr = 0x14; brcr = 0x0400; lab = u_bl10_n; break;
        case 8: bar = A(g_sr_bl0); bbr = 0x27; brcr = 0; lab = u_bl01_n; break;       /* controls: BL 0 -> 0 */
        case 9: bar = A(u_bl01); bbr = 0x14; brcr = 0x0400; lab = u_bl01_n; break;
        default: bar = A(u_ldc01); bbr = 0x14; brcr = 0x0400; lab = u_ldc01_n; break;
        }
        R16(BBRA) = 0; R16(BRCR) = brcr; R32(BARA) = bar; R8(BAMRA) = 0x04; R16(BBRA) = bbr; (void)R16(BRCR);
        spin(4);
        g_nent = 0; g_brk_off = 1;
        switch (f) {
        case 0: case 6: u_bl01_f(g_sr_bl1, sr15); break;
        case 1: case 7: u_bl10_f(g_sr_bl0, g_sr_bl1[0]); break;
        case 2: case 4: u_ldc01_f(g_sr_bl1[0], g_sr_bl0[0]); break;
        case 8: case 9: u_bl01_f(g_sr_bl0, sr15); break;
        case 10: u_ldc01_f(g_sr_bl0[0], g_sr_bl0[0]); break;
        default: u_ldc10_f(g_sr_bl1[0], g_sr_bl0[0]); break;
        }
        uint16_t after = R16(BRCR);
        printf("UBCX F %s want=%s got=%s cmfa=%d ", nm[f], want[f], g_nent ? "accepted" : "masked",
               (after | (g_nent ? g_ent[0].a : 0)) & 0x8000 ? 1 : 0);
        pr_ents("ents", lab);
        ubc_off();
    }
    imask(15);
    return 0;
}
