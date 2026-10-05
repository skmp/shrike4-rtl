/* dmadslp.c -- tb/bm/soc-adv/m1b (M1b-verify's, the M1b gate's since M1b-fix):
 * a deep sleep entered while DMAC channel 1 is feeding SCFTDR2 (the SCIF TX request,
 * byte units, cycle steal), with k = 0..23 loop iterations between the DMA start and the SLEEP, woken by a TMU0
 * underflow ~30000 Icyc later.  hm 9.4.1: in deep sleep the DMAC stops; hm 14.6 note 3: "DMA operation is not
 * guaranteed if ... deep sleep mode is entered without confirming that DMA transfer has ended".  The v1 DMAC's own
 * contract (rtl/v1/v1_dmac.sv header, SOC.md 2.5): `run` = 0 stops new units and "a unit already on the bus still
 * completes its handshakes", so after the wake the channel is expected to finish [P]: DMATCR1 = 0, TE, 16 bytes in
 * order.  Control: the same with plain sleep (the DMAC keeps running).  Reported per k. */
#include "m1b.h"

__asm__(
"        .text\n"
"        .balign 32\n"
"_d_sleep: sleep\n"
"        rts\n"
"        nop\n");
void d_sleep(void) __asm__("_d_sleep");

static void h_irq(irq_t code, irq_context_t *ctx, void *d) {
    (void)d;
    if (code == 0x400) { R16(TCR(0)) = 0; (void)R16(TCR(0)); }
    else {
        printf("DMADSLP unexpected code %03lx spc %08lx\n", (unsigned long)code, (unsigned long)ctx->pc);
        R32(0xB8000000u) = 0x3FE;
        for (;;) {}
    }
    logent(code, ctx->pc, ctx->sr, 0, 0);
}

static uint8_t dbuf[16] __attribute__((aligned(32)));

int main(void) {
    bm_default = h_irq;
    imask(15);
    R8(STBCR) = 0x00; (void)R8(STBCR);
    R16(IPRA) = 0x5000;
    /* ---- MSTP4 (SOC.md 2.4: the DMAC "must be reprogrammed afterwards [M]"), channel 3: the module stop k loop
     * iterations after the DMA start, ~20000 Icyc stopped, MSTP4 cleared, the channel reprogrammed from scratch: a
     * fresh 16-byte transfer must complete */
    {
        int stuck = 0;
        for (int k = 0; k < 8; k++) {
            scif_loop(0, 0, 0);
            for (int i = 0; i < 16; i++) dbuf[i] = (uint8_t)(0xA0 + i);
            ocbp_range(dbuf, sizeof dbuf);              /* the DMAC reads memory (M3-fix) */
            (void)R32(CHCR(3)); R32(CHCR(3)) = 0;
            R32(SAR(3)) = (uint32_t)(uintptr_t)dbuf & 0x1FFFFFFFu; R32(DAR(3)) = SCFTDR2; R32(DMATCR(3)) = 16;
            R32(CHCR(3)) = 0x00001A11u;
            for (volatile int i = 0; i < k; i++) ;
            R8(STBCR) = 0x10; (void)R8(STBCR);                    /* MSTP4 */
            uint32_t t0 = pmc(); while (pmc() - t0 < 20000) ;
            R8(STBCR) = 0x00; (void)R8(STBCR);
            /* the old transfer (DE still 1) goes on as soon as the DMAC's clock does (the stop froze it; hm 14.6 note 3
             * leaves it unguaranteed): stop the channel (hm 14.6 note 1: DE cleared first), let a unit in flight end,
             * and let the SCIF shift its last frame out (TEND, SCFDR2.T = 0) before the FIFO reset, so that no byte
             * of the old transfer lands behind it (M1b-fix: before the F2 fix the channel never resumed) */
            (void)R32(CHCR(3)); R32(CHCR(3)) = 0; (void)R32(CHCR(3));
            { uint32_t td = pmc(); while (pmc() - td < 500) ; }
            { uint32_t td = pmc(); while ((!(R16(SCFSR2) & 0x40) || (R16(SCFDR2) & 0x1F00)) && pmc() - td < 60000) ; }
            /* reprogram and run a fresh transfer */
            scif_loop(0, 0, 0);
            (void)R32(CHCR(3)); R32(CHCR(3)) = 0;
            R32(SAR(3)) = (uint32_t)(uintptr_t)dbuf & 0x1FFFFFFFu; R32(DAR(3)) = SCFTDR2; R32(DMATCR(3)) = 16;
            R32(CHCR(3)) = 0x00001A11u;
            uint32_t tw = pmc();
            while ((R32(DMATCR(3)) || (R16(SCFDR2) & 0x1F) < 16) && pmc() - tw < 60000) ;
            uint32_t ch = R32(CHCR(3)), tc = R32(DMATCR(3));
            unsigned n = R16(SCFDR2) & 0x1F, ok = 1;
            for (unsigned i = 0; i < n; i++) if (R8(SCFRDR2) != (uint8_t)(0xA0 + i)) ok = 0;
            int done = tc == 0 && ((ch >> 1) & 1) && n == 16 && ok;
            if (!done) stuck++;
            printf("DMADSLP mstp4 k=%d tc=%lu te=%lu rx=%u order=%u %s\n", k, (unsigned long)tc, (unsigned long)((ch >> 1) & 1),
                   n, ok, done ? "done" : "STUCK");
            (void)R32(CHCR(3)); R32(CHCR(3)) = 0;
        }
        printf("DMADSLP mstp4 summary stuck=%d of 8\n", stuck);
    }
    for (int dslp = 0; dslp < 2; dslp++) {
        int stuck = 0;
        for (int k = 0; k < 24; k++) {
            for (int i = 0; i < 16; i++) dbuf[i] = (uint8_t)(0xA0 + i);
            ocbp_range(dbuf, sizeof dbuf);              /* the DMAC reads memory (M3-fix) */
            scif_loop(0, 0, 0);
            (void)R32(CHCR(1));
            R32(CHCR(1)) = 0;
            R32(SAR(1)) = (uint32_t)(uintptr_t)dbuf & 0x1FFFFFFFu;
            R32(DAR(1)) = SCFTDR2;
            R32(DMATCR(1)) = 16;
            tmu_reset();
            R32(TCOR(0)) = 0xFFFFFFFFu; R32(TCNT(0)) = 1900; R16(TCR(0)) = UNIE;
            R8(STBCR2) = dslp ? 0x80 : 0x00; (void)R8(STBCR2);
            R8(TSTR) = 1;
            g_nent = 0;
            R32(CHCR(1)) = 0x00001A11u;
            for (volatile int i = 0; i < k; i++) ;
            imask(0);
            d_sleep();
            imask(15);
            R8(TSTR) = 0;
            R8(STBCR2) = 0x00; (void)R8(STBCR2);
            uint32_t tw = pmc();
            while ((R32(DMATCR(1)) || (R16(SCFDR2) & 0x1F) < 16) && pmc() - tw < 60000) ;
            uint32_t ch = R32(CHCR(1)), tc = R32(DMATCR(1));
            unsigned n = R16(SCFDR2) & 0x1F, ok = 1;
            for (unsigned i = 0; i < n; i++) if (R8(SCFRDR2) != (uint8_t)(0xA0 + i)) ok = 0;
            int done = tc == 0 && ((ch >> 1) & 1) && n == 16 && ok;
            if (!done) stuck++;
            printf("DMADSLP dslp=%d k=%d wake=%03lx tc=%lu te=%lu de=%lu rx=%u order=%u %s\n", dslp, k,
                   (unsigned long)(g_nent ? g_ent[0].code : 0), (unsigned long)tc, (unsigned long)((ch >> 1) & 1),
                   (unsigned long)(ch & 1), n, ok, done ? "done" : "STUCK");
            (void)R32(CHCR(1));
            R32(CHCR(1)) = 0;
        }
        printf("DMADSLP dslp=%d summary stuck=%d of 24\n", dslp, stuck);
    }
    R16(IPRA) = 0;
    R16(SCSCR2) = 0;
    R8(STBCR) = 0x03;
    return 0;
}
