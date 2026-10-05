/* prio2.c -- tb/bm/soc-adv/m1b (M1b-verify's, the M1b gate's since M1b-fix):
 * every on-chip interrupt source a bare board can raise, pending at once, then IMASK
 * lowered: the acceptance order must be hm Table 19.5's (by IPR level, then the default order H-UDI > GPIO > DMAC >
 * TMU0 > TMU1 > TMU2 > RTC > SCI > SCIF > WDT > REF, and inside a unit ATI > PRI > CUI, ERI > RXI > TXI > TEI (SCI),
 * ERI > RXI > BRI > TXI (SCIF), RCMI > ROVI).  The lockstep checker takes the INTEVT from the RTL (M1a-verify F5), so
 * only the program sees a mis-wired or mis-ordered source.  The M1b units raise theirs without a board:
 *   RTC   (no crystal on the DC) ATI by the alarm comparator (RSECAR = ENB | RSECCNT; it runs without a clock [HW tc3]),
 *         PRI by PEF written 1 with PES != 0 [M], CUI by CF written 1 with CIE [HW tc*: CF written 1 sets]
 *   SCI   TXI and TEI with TE (TDRE = TEND = 1 at init)
 *   SCIF  loopback: 17 frames -> RDF (RXI) and ORER (BRI, with REIE); TDFE (TXI)
 *   WDT   interval mode, IOVF;   TMU0-2 UNF;   REF CMF with CMIE;   DMAC ch3 auto-request end (DMTE3)
 * Arrangements: all equal (level 5); ascending by default order (the reverse); level == IMASK (none taken), then one
 * lower.  Each handler clears its source and reads the register back before the RTE (hm 19.3). */
#include "m1b.h"

/* the sources, in the order of the pending mask (the default order; the codes the handler logs) */
static const uint32_t CODES[] __attribute__((unused)) = {0x6A0, 0x400, 0x420, 0x440, 0x480, 0x4A0, 0x4C0, 0x520, 0x540,
                                                         0x720, 0x740, 0x760, 0x560, 0x580};

static void h_irq(irq_t code, irq_context_t *ctx, void *d) {
    (void)d;
    logent(code, ctx->pc, ctx->sr, 0, 0);
    switch (code) {
    case 0x400: case 0x420: case 0x440: {
        int n = (int)(code - 0x400) / 0x20;
        R16(TCR(n)) = (uint16_t)(R16(TCR(n)) & ~(UNF | UNIE));
        (void)R16(TCR(n));
        break;
    }
    case 0x480: R8(RCR1) = (uint8_t)(R8(RCR1) & ~0x08); (void)R8(RCR1); break;          /* AIE (AF stays: match) */
    case 0x4A0: R8(RCR2) = (uint8_t)(R8(RCR2) & ~0x80); (void)R8(RCR2); break;          /* PEF */
    case 0x4C0: R8(RCR1) = (uint8_t)(R8(RCR1) & ~0x90); (void)R8(RCR1); break;          /* CF, CIE */
    case 0x520: R8(SCSCR1) = (uint8_t)(R8(SCSCR1) & ~0x80); (void)R8(SCSCR1); break;    /* TIE */
    case 0x540: R8(SCSCR1) = (uint8_t)(R8(SCSCR1) & ~0x04); (void)R8(SCSCR1); break;    /* TEIE */
    case 0x720: R16(SCSCR2) = (uint16_t)(R16(SCSCR2) & ~0x40); (void)R16(SCSCR2); break; /* RIE (REIE keeps BRI) */
    case 0x740: R16(SCSCR2) = (uint16_t)(R16(SCSCR2) & ~0x48); (void)R16(SCSCR2); break; /* REIE */
    case 0x760: R16(SCSCR2) = (uint16_t)(R16(SCSCR2) & ~0x80); (void)R16(SCSCR2); break; /* TIE */
    case 0x560: R16(WTCSR) = 0xA500; (void)R8(WTCSR); break;                               /* TME = IOVF = 0 */
    case 0x580: { uint16_t v = R16(RTCSR); R16(RTCSR) = (uint16_t)(0xA500 | (v & 0x3F)); (void)R16(RTCSR); break; }
    case 0x6A0: R32(CHCR(3)) = R32(CHCR(3)) & ~0x5u; (void)R32(CHCR(3)); break;           /* IE, DE */
    default:
        printf("PRIO2 unexpected code %03lx spc %08lx\n", (unsigned long)code, (unsigned long)ctx->pc);
        R32(0xB8000000u) = 0x3FE;
        for (;;) {}
    }
}

static uint32_t dma_src[8] __attribute__((aligned(32)));
static uint32_t dma_dst[8] __attribute__((aligned(32)));

/* every source pending (IMASK 15 throughout); returns the sources seen pending as a bit mask in CODES order */
static uint32_t raise_all(void) {
    uint32_t seen = 0;
    /* module stops: SCI and RTC run (the stub leaves STBCR = 0x03) */
    R8(STBCR) = 0x00; (void)R8(STBCR);
    /* TMU0-2: one underflow each, then stopped with UNF held */
    tmu_reset();
    for (int n = 0; n < 3; n++) { R32(TCOR(n)) = 0xFFFFFFFFu; R32(TCNT(n)) = 0; R16(TCR(n)) = UNIE; }
    R8(TSTR) = 7;
    spin(20);
    R8(TSTR) = 0;
    /* RTC: RCR2 RTCEN START, the alarm on the current seconds, AIE; PEF (PES 1/256 s); CF with CIE */
    R8(RCR2) = 0x09;
    R8(RSECAR) = (uint8_t)(0x80 | R8(RSECCNT));
    R8(RCR1) = 0x98;                                   /* CF (written 1 sets), CIE, AIE */
    R8(RCR2) = 0x99;                                   /* PEF (written 1 sets [M]), PES 1, RTCEN, START */
    /* SCI: TE with TIE and TEIE (TDRE = TEND = 1 after the MSTP0 clear) */
    R8(SCSCR1) = 0x20;
    R8(SCSCR1) = 0xA4;
    /* SCIF loopback at the fastest rate: 17 bytes -> 16 in the receive FIFO (RDF) and an overrun (ORER); TDFE */
    scif_loop(0, 0x48, 0);                             /* RIE, REIE; TIE below */
    for (int i = 0; i < 17; i++) {
        while ((R16(SCFDR2) >> 8) >= 16) ;
        R8(SCFTDR2) = (uint8_t)(0x40 + i);
    }
    for (int t = 0; t < 400000 && !(R16(SCLSR2) & 1); t++) ;
    R16(SCSCR2) = (uint16_t)(R16(SCSCR2) | 0x80);     /* TIE */
    /* WDT interval: 16 counts at CKS 0 */
    R16(WTCSR) = 0xA500;
    R16(WTCNT) = 0x5AF0;
    R16(WTCSR) = 0xA580;
    for (int t = 0; t < 20000 && !(R8(WTCSR) & 0x08); t++) ;
    R16(WTCSR) = 0xA508;                               /* stop, IOVF kept (written 1) */
    /* REF: CMIE (CMF sets at the next compare match; the stub's RTCSR 0xA510 / RTCOR 0x5E) */
    { uint16_t v = R16(RTCSR); R16(RTCSR) = (uint16_t)(0xA500 | 0x40 | (v & 0x3F)); }
    for (int t = 0; t < 20000 && !(R16(RTCSR) & 0x80); t++) ;
    /* DMAC ch3: 8 longwords auto-request, IE */
    for (int i = 0; i < 8; i++) { dma_src[i] = 0x11110000u * (uint32_t)(i + 1) + (uint32_t)i; dma_dst[i] = 0; }
    ocbp_range(dma_src, sizeof dma_src); ocbp_range(dma_dst, sizeof dma_dst);   /* DMA sees memory (M3-fix) */
    R32(CHCR(3)) = 0;
    R32(SAR(3)) = (uint32_t)(uintptr_t)dma_src & 0x1FFFFFFFu;
    R32(DAR(3)) = (uint32_t)(uintptr_t)dma_dst & 0x1FFFFFFFu;
    R32(DMATCR(3)) = 8;
    R32(CHCR(3)) = 0x00005435u;                        /* DM inc, SM inc, RS auto, TS long, IE, DE */
    for (int t = 0; t < 20000 && !(R32(CHCR(3)) & 2); t++) ;
    /* what is pending */
    for (int n = 0; n < 3; n++) if (R16(TCR(n)) & UNF) seen |= 1u << (1 + n);
    if (R32(CHCR(3)) & 2) seen |= 1u << 0;
    { uint8_t r1 = R8(RCR1), r2 = R8(RCR2);
      if ((r1 & 0x09) == 0x09) seen |= 1u << 4;
      if (r2 & 0x80) seen |= 1u << 5;
      if ((r1 & 0x90) == 0x90) seen |= 1u << 6; }
    { uint8_t s = R8(SCSSR1); if (s & 0x80) seen |= 1u << 7; if (s & 0x04) seen |= 1u << 8; }
    { uint16_t f = R16(SCFSR2); if (f & 0x02) seen |= 1u << 9; if (R16(SCLSR2) & 1) seen |= 1u << 10;
      if (f & 0x20) seen |= 1u << 11; }
    if (R8(WTCSR) & 0x08) seen |= 1u << 12;
    if (R16(RTCSR) & 0x80) seen |= 1u << 13;
    return seen;
}

static void run(const char *name, uint16_t ipra, uint16_t iprb, uint16_t iprc, unsigned mask_during) {
    imask(15);
    R16(IPRA) = 0; R16(IPRB) = 0; R16(IPRC) = 0;
    uint32_t seen = raise_all();
    R16(IPRA) = ipra; R16(IPRB) = iprb; R16(IPRC) = iprc;
    (void)R16(IPRC);
    g_nent = 0;
    imask(mask_during);
    spin(200);
    int n_at_mask = g_nent;
    imask(mask_during ? mask_during - 1 : 0);
    spin(200);
    imask(15);
    int n = g_nent;
    printf("PRIO2 %s pending=%04lx held=%d order=", name, (unsigned long)seen, n_at_mask);
    for (int i = 0; i < n && i < NLOG; i++) printf("%s%03lx", i ? "," : "", (unsigned long)g_ent[i].code);
    printf(" n=%d\n", n);
    /* the DMA copied what it was told to */
    int bad = 0;
    for (int i = 0; i < 8; i++) if (dma_dst[i] != dma_src[i]) bad++;
    printf("PRIO2 %s dma_bad=%d\n", name, bad);
}

int main(void) {
    bm_default = h_irq;
    /* all at level 5 */
    run("equal5", 0x5555, 0x5550, 0x0550, 0);
    /* ascending along the default order: REF highest ... DMAC lowest */
    run("ascend", 0x2345, 0x9860, 0x0170, 0);
    /* SCIF and RTC on top at 9, TMU1 at 8, the rest at 1 */
    run("mixed", 0x1819, 0x1110, 0x0190, 0);
    /* equal at 5 with IMASK 5 (nothing may be taken), then 4 */
    run("atmask", 0x5555, 0x5550, 0x0550, 5);
    return 0;
}
