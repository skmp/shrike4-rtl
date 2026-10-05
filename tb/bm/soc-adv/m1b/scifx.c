/* scifx.c -- tb/bm/soc-adv/m1b (M1b-verify's, the M1b gate's since M1b-fix):
 * the SCIF against the CPG's module stop and power-down, and the DMAC.
 *   A  module stop in the middle of a frame (SOC.md 2.4: "SCIF (MSTP3): state frozen, registers kept [HW]; access reads
 *      0 / ignored [P]"; 3.4: a module-stopped unit drives its requests 0): a loopback frame at 512 Icyc per bit is
 *      frozen ~2000 Icyc in for ~20000 Icyc; the registers read 0 and a write (SCBRR2, SCFTDR2) is ignored while
 *      stopped; after MSTP3 = 0 the frame completes (the one byte, no error, SCBRR2 kept) in about the time it had
 *      left.  TXI pending (TIE, TDFE) under IMASK 15: with MSTP3 = 1 IMASK 0 takes nothing; MSTP3 = 0 takes 0x760.
 *   B  SLEEP right behind the store of a loopback byte: RXI (0x720) wakes it, SPC = SLEEP + 2; the same in deep sleep
 *      (hm 9.4.1: "except for the DMAC, on-chip peripheral modules continue to operate").
 *   C  DMAC channel 1 feeding SCFTDR2 on the SCIF's TX request (as KOS / scif_probe S17: RS 1010, byte, cycle steal),
 *      then SLEEP until a TMU0 underflow ~30000 Icyc later: in sleep the DMAC runs (all 16 bytes sent before the
 *      wake); in deep sleep it stops (hm 9.4.1: SOC.md 2.5 dmac_run = 0) and resumes after the wake; either way the
 *      16 bytes arrive once each, in order, and DMATCR1 ends at 0 with TE. */
#include "m1b.h"

__asm__(
"        .text\n"
"        .balign 32\n"
"        .global _s_st_sleep, _s_st_sleep_s\n"
"_s_st_sleep: mov.b r5,@r4\n"              /* r4 SCFTDR2, r5 the byte */
"_s_st_sleep_s: sleep\n"
"        rts\n"
"        nop\n"
"        .balign 32\n"
"        .global _s_sleep, _s_sleep_s\n"
"_s_sleep: mov.l r5,@r4\n"                /* r4 CHCR1, r5 its value: the DMA starts, then SLEEP */
"_s_sleep_s: sleep\n"
"        rts\n"
"        nop\n");
extern char s_st_sleep_s[], s_sleep_s[];
void s_st_sleep_f(uint32_t a, uint32_t v) __asm__("_s_st_sleep");
void s_sleep_f(uint32_t a, uint32_t v) __asm__("_s_sleep");

static volatile uint32_t g_tc_wake, g_fdr_wake;

static void h_irq(irq_t code, irq_context_t *ctx, void *d) {
    (void)d;
    uint32_t a = 0;
    switch (code) {
    case 0x400:
        g_tc_wake = R32(DMATCR(1)); g_fdr_wake = R16(SCFDR2);
        R16(TCR(0)) = 0; (void)R16(TCR(0));
        break;
    case 0x720: a = R16(SCFDR2); R16(SCSCR2) = (uint16_t)(R16(SCSCR2) & ~0x48); (void)R16(SCSCR2); break;
    case 0x760: R16(SCSCR2) = (uint16_t)(R16(SCSCR2) & ~0x80); (void)R16(SCSCR2); break;
    default:
        printf("SCIFX unexpected code %03lx spc %08lx\n", (unsigned long)code, (unsigned long)ctx->pc);
        R32(0xB8000000u) = 0x3FE;
        for (;;) {}
    }
    logent(code, ctx->pc, ctx->sr, a, 0);
}

static void stbcr(uint8_t v) { R8(STBCR) = v; (void)R8(STBCR); }
static void wait_icyc(uint32_t n) { uint32_t t0 = pmc(); while (pmc() - t0 < n) ; }

static uint8_t dbuf[16] __attribute__((aligned(32)));

int main(void) {
    bm_default = h_irq;
    imask(15);
    stbcr(0x00);
    R16(IPRC) = 0x0050;                                  /* SCIF at 5 */

    /* ---- A: module stop mid-frame; first the same frame unstopped (the reference) */
    scif_loop(3, 0, 0);                                  /* 32 * 4 Pcyc = 512 Icyc per bit */
    uint32_t tr0 = pmc();
    R8(SCFTDR2) = 0x5A;
    while (!(R16(SCFDR2) & 0x1F) && pmc() - tr0 < 200000) ;
    uint32_t ref = pmc() - tr0;
    (void)R8(SCFRDR2);
    scif_loop(3, 0, 0);
    uint32_t t0 = pmc();
    R8(SCFTDR2) = 0x5A;
    wait_icyc(2000);
    stbcr(0x08);                                         /* MSTP3 */
    uint16_t s_scr = R16(SCSCR2), s_fdr = R16(SCFDR2), s_fsr = R16(SCFSR2);
    uint8_t s_brr = R8(SCBRR2);
    R8(SCBRR2) = 0x77;                                   /* ignored while stopped */
    R8(SCFTDR2) = 0x99;                                  /* ignored while stopped */
    wait_icyc(20000);
    stbcr(0x00);
    uint32_t t1 = pmc();
    uint8_t r_brr = R8(SCBRR2);
    uint16_t r_scr = R16(SCSCR2), r_fdr0 = R16(SCFDR2);
    while (!(R16(SCFDR2) & 0x1F) && pmc() - t1 < 200000) ;
    uint32_t t2 = pmc();
    uint16_t r_fdr = R16(SCFDR2), r_fsr = R16(SCFSR2);
    uint8_t byte = R8(SCFRDR2);
    printf("SCIFX A stopped scr=%04x fdr=%04x fsr=%04x brr=%02x resumed brr=%02x scr=%04x fdr=%04x\n", s_scr, s_fdr, s_fsr,
           s_brr, r_brr, r_scr, r_fdr0);
    printf("SCIFX A rx fdr=%04x er=%d brk=%d byte=%02x ref_icyc=%lu stopped_extra_icyc=%lu\n", r_fdr, (r_fsr >> 7) & 1,
           (r_fsr >> 4) & 1, byte, (unsigned long)ref, (unsigned long)((t2 - t0) - ref));
    /* TXI gated by the module stop */
    R16(SCSCR2) = (uint16_t)(R16(SCSCR2) | 0x80);         /* TIE: TDFE = 1, TXI pending */
    stbcr(0x08);
    g_nent = 0;
    imask(0); spin(200); imask(15);
    int n_stopped = g_nent;
    stbcr(0x00);
    imask(0); spin(200); imask(15);
    printf("SCIFX A txi stopped_taken=%d then n=%d code=%03lx\n", n_stopped, g_nent,
           (unsigned long)(g_nent ? g_ent[0].code : 0));

    /* ---- B: RXI wakes a SLEEP (and a deep sleep) */
    for (int dslp = 0; dslp < 2; dslp++) {
        scif_loop(0, 0x40, 0);                           /* RIE, trigger 1 */
        R8(STBCR2) = dslp ? 0x80 : 0x00; (void)R8(STBCR2);
        g_nent = 0;
        imask(0);
        uint32_t tb = pmc();
        s_st_sleep_f(SCFTDR2, 0xC3);
        imask(15);
        uint32_t dt = pmc() - tb;
        uint8_t rb = R8(SCFRDR2);
        printf("SCIFX B dslp=%d n=%d code=%03lx at=%s fdr_at_wake=%lu byte=%02x icyc=%lu\n", dslp, g_nent,
               (unsigned long)(g_nent ? g_ent[0].code : 0),
               g_nent && g_ent[0].spc == (uint32_t)(uintptr_t)s_st_sleep_s + 2 ? "wake" : "other",
               (unsigned long)(g_nent ? g_ent[0].a & 0x1F : 0), rb, (unsigned long)dt);
    }
    R8(STBCR2) = 0x00;

    /* ---- C: DMAC ch1 -> SCFTDR2 across a SLEEP / deep sleep */
    R16(IPRA) = 0x5000;                                  /* TMU0 at 5 */
    for (int dslp = 0; dslp < 2; dslp++) {
        for (int i = 0; i < 16; i++) dbuf[i] = (uint8_t)(0xA0 + i);
        ocbp_range(dbuf, sizeof dbuf);                  /* the DMAC reads memory (M3-fix) */
        scif_loop(0, 0, 0);                              /* 1280 Icyc per frame; TTRG 8 */
        R32(CHCR(1)) = 0;
        R32(SAR(1)) = (uint32_t)(uintptr_t)dbuf & 0x1FFFFFFFu;
        R32(DAR(1)) = SCFTDR2;
        R32(DMATCR(1)) = 16;
        tmu_reset();
        R32(TCOR(0)) = 0xFFFFFFFFu; R32(TCNT(0)) = 1900; R16(TCR(0)) = UNIE;     /* ~30400 Icyc */
        R8(STBCR2) = dslp ? 0x80 : 0x00; (void)R8(STBCR2);
        g_tc_wake = 99; g_fdr_wake = 0xFFFF;
        g_nent = 0;
        R8(TSTR) = 1;
        imask(0);
        s_sleep_f(CHCR(1), 0x00001A11u);                 /* SM inc, RS SCIF TX, byte, DE */
        imask(15);
        R8(TSTR) = 0;
        uint32_t tw = pmc();
        while ((R32(DMATCR(1)) || (R16(SCFDR2) & 0x1F) < 16) && pmc() - tw < 400000) ;
        uint32_t ch = R32(CHCR(1)), tc = R32(DMATCR(1));
        unsigned n = R16(SCFDR2) & 0x1F;
        printf("SCIFX C dslp=%d wake=%03lx at=%s tc_at_wake=%lu rx_at_wake=%lu tx_at_wake=%lu end tc=%lu te=%lu rx=%u:",
               dslp, (unsigned long)(g_nent ? g_ent[0].code : 0),
               g_nent && g_ent[0].spc == (uint32_t)(uintptr_t)s_sleep_s + 2 ? "wake" : "other",
               (unsigned long)g_tc_wake, (unsigned long)(g_fdr_wake & 0x1F), (unsigned long)((g_fdr_wake >> 8) & 0x1F),
               (unsigned long)tc, (unsigned long)((ch >> 1) & 1), n);
        for (unsigned i = 0; i < n; i++) printf(" %02x", R8(SCFRDR2));
        printf("\n");
        R32(CHCR(1)) = 0;
    }
    R8(STBCR2) = 0x00;
    R16(IPRA) = 0; R16(IPRC) = 0;
    R16(SCSCR2) = 0;
    stbcr(0x03);
    return 0;
}
