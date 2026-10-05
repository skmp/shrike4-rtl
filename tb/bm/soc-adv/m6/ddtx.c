/* ddtx.c -- M6 (WI-SOC): board phase 2 through the chip's pins, without KOS: Holly's DMAs through DMAC channel 0 over
 * DDT (a DTR per 32-byte unit: DBREQ# -> BAVL# -> the DTR on D63:0 with TR#, the data on DACK beats) and CH2-DMA's
 * direct requests on DBREQ# + TR# (SOC.md 3.3.3, 8.3 M6; tb/dc_board/dc_ddt.h).  The incremental subset's stand-in for
 * dmac1 / dmac2 / ddtprobe under KOS (make v1-soc-m6-sub; a few M Icyc against their ~200M).  Run on rm with the full
 * board and soc_tb --ddt --lockstep --ccr 0x090D.  Built with the m1b run-time (as m5/boardx).  Each row prints
 * "DX <row> ... OK|FAIL"; the program judges its rows, m6/check.py checks the row set, the done line and the run.
 * Expected values and their sources (dmac.md 3.9.3, 4.2, 4.3, 7.4; board.md 4.3; the [HW] runs named):
 *   P    PVR-DMA RAM -> texture memory, 4 KB, channel 0 sampled while it runs ([HW] ddtprobe C): every Holly unit is a
 *        channel-0 DTR SZ 32 B / MD 01 / R/W 0 / COUNT 1, so SAR0 steps by 0x20 inside the buffer, CHCR0 reads 0x852C1
 *        (a DTR loaded, TE 0) or 0x852C3 (its unit done), DMATCR0 0 or 1; at the end SAR0 = start + 4 KB, DAR0 kept,
 *        CHCR0 0x852C3, DMATCR0 0, PDSTARD = start + LEN, PDLEND 0, ISTNRM 11; the texture words equal the buffer;
 *        the time within 3 % of Holly's 128 x 251 Icyc ([HW] ddtprobe 32,095 for 4 KB: board.md 4.3)
 *   Q    PVR-DMA texture memory -> RAM, 1 KB: R/W 1, DAR0 = start + 1 KB at the end, CHCR0 0x853C3; the RAM equals the
 *        texture words (Holly's data on the DACK write beats, stored by the SDRAM)
 *   A    G2-DMA RAM -> AICA wave RAM, 1 KB (the AICA channel, TSEL 0): R/W 0 per unit, SAR0 = start + 1 KB, CHCR0
 *        0x852C3, ISTNRM 15; wave RAM equals the buffer; 32 x 1,212 Icyc within 3 % ([HW] ddtprobe A)
 *   B    G2-DMA AICA -> RAM, 1 KB: R/W 1, DAR0 = start + 1 KB, CHCR0 0x853C3; 32 x 1,331 within 3 % ([HW] ddtprobe B)
 *   M    a maple list, GETCOND to A0 and DEVINFO to B0 (nothing there): the list fetched as one R/W 0 unit (SAR0 =
 *        MDSTAR + 0x20 after it, [HW] dmac1 S1), the responses as R/W 1 units (DAR0 = the last unit + 0x20: B0's
 *        time-out block after A0's GETCOND), CHCR0 0x853C3, ISTNRM 12 taken at level 2; A0's response 0x03200008,
 *        0x01000000 ([HW] boardio), B0's block 0xFFFFFFFF; the list's time within 0.5 % of board.md 4.4's frame sum
 *   C    CH2-DMA to the texture path, 64 units (DMATCR2 = C2DLEN / 32 = 64; [HW] dmac1 S13 "tex 64-bit path"): SAR2 +
 *        2,048, DAR2 unchanged, DMATCR2 0, CHCR2 0x12C3, C2DSTAT + 2,048, C2DLEN 0, C2DST 0, ISTNRM 19; the texture
 *        words equal the buffer
 *   R    CH2-DMA with DMATCR2 = 64 and C2DLEN = 32 units ([HW] S13): Holly stops after its 32 requests: DMATCR2 32,
 *        CHCR2 0x12C1 (TE 0, DE 1), SAR2 + 1,024
 *   S    CH2-DMA of 64 KB (2,048 units) with the CPU polling ISTNRM through it, dmac1 S13's loop ([HW] S13 "tex N=2048":
 *        ISTNRM 19 seen at 23,613 Icyc, the polls running all along): Holly's input FIFO paces its requests (board.md
 *        4.3, dc_sb.h), so the DMAC's level-sense burst has breaks and the CPU's reads get the bus -- more than 100 polls
 *        complete before the end, and the end within 5 % of the console's 23,613
 *   W    CPU writes to channel 0 in DDT mode ([HW] dmac2 T1): SAR0 / DAR0 / DMATCR0 load 0, CHCR0 unchanged; Holly's
 *        next DTR (a 2-unit PVR-DMA) loads them again: SAR0 = start + 64, CHCR0 0x852C3
 *   X    Holly's units against the other bus masters: a 4 KB PVR-DMA while channel 1 copies 2 KB RAM -> RAM (auto,
 *        cycle steal, 32-byte units, CHCR1 0x5441 -- DMAOR.PR 10: channel 0 above channel 1) and the CPU reads RAM
 *        uncached: every byte of both copies right, both ends seen, channel 1 TE
 *   Y    Holly -> memory units against channel-1 word units (dmac1 S11's shape under KOS's maple): a 1 KB PVR-DMA
 *        texture memory -> RAM (32 single-address writes) while channel 1 copies 2 KB in word units by auto-request cycle
 *        steal (CHCR1 0x5421): every written byte right -- a single write's data pulls come after its accept, when the
 *        engine may run channel 1's next unit, and must carry the write's own byte enables (M6: v1_dmac_xfer's sgl_q;
 *        before, a maple response landed as 8 + 2 + 2 + 2 bytes)
 *   N    Holly's units during a channel-1 burst (dmac1 S11's 16 KB copies under KOS's maple): a 1 KB PVR-DMA started
 *        right behind a 2 KB channel-1 burst (TS 2, 1,024 word units, ~47 kIcyc, CHCR1 0x54A1); channel 0 is above
 *        channel 1 (PR 10), so its units take over at the burst's unit boundaries (hm 14.3.4 "the channel 0 transfer is
 *        started immediately"; dmac.md 3.4): polled over P4 only (the burst holds the external bus), SAR0 reaches start
 *        + 1 KB while DMATCR1 is still above 0; both copies right.  (M6-fix) The poll loop (n_poll) runs from the
 *        instruction cache: one call before the burst warms its lines, so none misses during it -- a fetch miss waits
 *        for the burst's end ("the bus is not released to the CPU until channel 1 transfer ends", hm 14.3.4; SOC.md
 *        3.3.2).  Before M6-fix the chip let the CPU's IC fills through after each Holly unit (M6-verify F1), which
 *        this row relied on
 * Output: "DX <row> ... OK|FAIL", "DX done fails=<n>". */
#include "m1b.h"

#define ISTNRM   0xA05F6900u
#define IML2NRM  0xA05F6910u
#define IML2EXT  0xA05F6914u
#define IML2ERR  0xA05F6918u
#define IML4NRM  0xA05F6920u
#define IML4EXT  0xA05F6924u
#define IML4ERR  0xA05F6928u
#define IML6NRM  0xA05F6930u
#define IML6EXT  0xA05F6934u
#define IML6ERR  0xA05F6938u
#define ISTERR   0xA05F6908u
#define C2DSTAT  0xA05F6800u
#define C2DLEN   0xA05F6804u
#define C2DST    0xA05F6808u
#define LMMODE0  0xA05F6884u
#define MDSTAR   0xA05F6C04u
#define MDTSEL   0xA05F6C10u
#define MDEN     0xA05F6C14u
#define MDST     0xA05F6C18u
#define MSYS     0xA05F6C80u
#define MDAPRO   0xA05F6C8Cu
#define ADSTAG   0xA05F7800u
#define ADSTAR   0xA05F7804u
#define ADLEN    0xA05F7808u
#define ADDIR    0xA05F780Cu
#define ADTSEL   0xA05F7810u
#define ADEN     0xA05F7814u
#define ADST     0xA05F7818u
#define G2APRO   0xA05F78BCu
#define PDSTAP   0xA05F7C00u
#define PDSTAR   0xA05F7C04u
#define PDLEN    0xA05F7C08u
#define PDDIR    0xA05F7C0Cu
#define PDTSEL   0xA05F7C10u
#define PDEN     0xA05F7C14u
#define PDST     0xA05F7C18u
#define PDAPRO   0xA05F7C80u
#define PDSTARD  0xA05F7CF4u
#define PDLEND   0xA05F7CF8u
#define P2(p)    (((uint32_t)(uintptr_t)(p) & 0x1FFFFFFFu) | 0xA0000000u)
#define PHYS(p)  ((uint32_t)(uintptr_t)(p) & 0x1FFFFFFFu)

static int fails;
static volatile uint32_t g_istnrm_ack;

static void h_all(irq_t code, irq_context_t *ctx, void *d) {
  (void)d;
  const uint32_t t1 = R32(TCNT(1));
  if (code == 0x3A0) {
    const uint32_t n = R32(ISTNRM), md = R32(MDST);
    R32(ISTNRM) = n & g_istnrm_ack;
    (void)R32(ISTNRM);
    logent(code, t1, ctx->sr, n, md);
    return;
  }
  printf("DX unexpected code %03lx spc %08lx\n", (unsigned long)code, (unsigned long)ctx->pc);
  R32(0xB8000000u) = 0x3FE;
  for (;;) {}
}

static char g_b[320];
static void row(const char *name, int ok) {
  printf("DX %s %s %s\n", name, g_b, ok ? "OK" : "FAIL");
  if (!ok) fails++;
}
static void holly_route(uint32_t l2n) {
  R32(IML2NRM) = l2n; R32(IML2EXT) = 0; R32(IML2ERR) = 0;
  R32(IML4NRM) = 0; R32(IML4EXT) = 0; R32(IML4ERR) = 0;
  R32(IML6NRM) = 0; R32(IML6EXT) = 0; R32(IML6ERR) = 0;
  R32(ISTNRM) = 0x3FFFFF; R32(ISTERR) = 0xFFFFFFFF;
  (void)R32(ISTNRM);
}
static void tbase(void) {
  R8(TOCR) = 0; R32(TCOR(1)) = 0xFFFFFFFFu; R32(TCNT(1)) = 0xFFFFFFFFu; R16(TCR(1)) = 0; (void)R16(TCR(1));
  R8(TSTR) = (uint8_t)(R8(TSTR) | 2u);
}
static int spin_until(volatile int *n, int want, uint32_t max_icyc) {
  const uint32_t t = pmc();
  while (*n < want) if (pmc() - t > max_icyc) return 0;
  return 1;
}

/* row N's poll (M6-fix): P4 reads only, from the instruction cache (warmed by one call before the burst): SAR0 until
 * it reaches `end`, DMATCR1 at that moment, then on until DMATCR1 is 0 or `budget` Icyc pass */
static uint32_t __attribute__((noinline, aligned(32))) n_poll(uint32_t end, uint32_t budget, uint32_t *s0_out) {
  uint32_t tcr_at_end = 0xFFFFFFFFu, s0 = 0;
  const uint32_t tw = pmc();
  while (pmc() - tw < budget) {
    s0 = R32(SAR(0));
    const uint32_t t1 = R32(DMATCR(1));
    if (s0 == end && tcr_at_end == 0xFFFFFFFFu) tcr_at_end = t1;
    if (t1 == 0 && s0 == end) break;
  }
  *s0_out = s0;
  return tcr_at_end;
}

static uint32_t bufA[1024] __attribute__((aligned(32)));      /* 4 KB */
static uint32_t bufB[1024] __attribute__((aligned(32)));
static uint32_t bufC[512] __attribute__((aligned(32)));       /* 2 KB: channel 1's source ... */
static uint32_t bufD[512] __attribute__((aligned(32)));       /* ... and destination */
static uint32_t mlist[8] __attribute__((aligned(32)));
static uint32_t rx_a[8] __attribute__((aligned(32)));
static uint32_t rx_b[8] __attribute__((aligned(32)));

/* channel-0 samples while a Holly DMA runs (dmac1 S1 / ddtprobe's method: every change of the four registers) */
#define NS 160
static uint32_t smp[NS][4];
static int nsmp, smp_bad;
static uint32_t smp_lo, smp_hi, smp_rw;                           /* the moving register's window; R/W expected */
static uint32_t ch0_sig(void) {
  return R32(SAR(0)) ^ (R32(DAR(0)) * 3) ^ (R32(DMATCR(0)) * 7) ^ (R32(CHCR(0)) * 13);
}
/* pre: ch0_sig() before the DMA's start (the samples equal to it are the state before the first DTR) */
static int sample_until(uint32_t reg, uint32_t mask, uint32_t tmo_icyc, uint32_t pre) {
  uint32_t last = pre;
  const uint32_t t0 = pmc();
  nsmp = 0; smp_bad = 0;
  for (;;) {
    const uint32_t s = R32(SAR(0)), d = R32(DAR(0)), t = R32(DMATCR(0)), c = R32(CHCR(0));
    const uint32_t sig = s ^ (d * 3) ^ (t * 7) ^ (c * 13);
    if (sig != last) {
      last = sig;
      if (nsmp < NS) { smp[nsmp][0] = s; smp[nsmp][1] = d; smp[nsmp][2] = t; smp[nsmp][3] = c; nsmp++; }
      const uint32_t mv = smp_rw ? d : s;
      /* a DTR's CHCR0: 0x852C1 / 0x853C1 loaded, TE set by its unit's end; COUNT 1: DMATCR0 1 or 0 */
      if ((c | 2u) != (smp_rw ? 0x853C3u : 0x852C3u) || t > 1 || mv < smp_lo || mv > smp_hi || (mv & 31)) smp_bad++;
    }
    if (!(R32(reg) & mask)) return 1;
    if (pmc() - t0 > tmo_icyc) return 0;
  }
}

int main(void) {
  bm_default = h_all;
  imask(15);
  tmu_reset();
  tbase();
  holly_route(0);
  const uint32_t dmaor = R32(DMAOR);
  /* crt0 cleared .bss through the copy-back operand cache: write the DMA buffers' dirty lines back and invalidate them
   * before the program uses them through P2 and the DMAC (a later eviction would write the cleared line over them,
   * M3-fix's rule, as dmac1 / dmac2 purge theirs) */
  ocbp_range(bufA, sizeof bufA); ocbp_range(bufB, sizeof bufB); ocbp_range(bufC, sizeof bufC);
  ocbp_range(bufD, sizeof bufD); ocbp_range(mlist, sizeof mlist); ocbp_range(rx_a, sizeof rx_a);
  ocbp_range(rx_b, sizeof rx_b);

  /* ---- P: PVR-DMA RAM -> texture memory, 4 KB ---- */
  {
    volatile uint32_t *a = (volatile uint32_t *)P2(bufA), *v = (volatile uint32_t *)0xA4600000u;
    for (int i = 0; i < 1024; i++) { a[i] = 0xA1000000u + (uint32_t)i * 0x10001u; v[i] = 0; }
    R32(PDAPRO) = 0x6702007Fu;
    R32(PDSTAP) = 0x04600000u; R32(PDSTAR) = PHYS(bufA); R32(PDLEN) = 4096; R32(PDDIR) = 0; R32(PDTSEL) = 0;
    R32(PDEN) = 1;
    smp_lo = PHYS(bufA); smp_hi = PHYS(bufA) + 4096; smp_rw = 0;
    const uint32_t d0 = R32(DAR(0)), pre = ch0_sig(), t0 = R32(TCNT(1));
    R32(PDST) = 1;
    const int done = sample_until(PDST, 1, 4000000, pre);
    const uint32_t dt = (t0 - R32(TCNT(1))) * 16;
    int good = 0;
    for (int i = 0; i < 1024; i++) good += v[i] == 0xA1000000u + (uint32_t)i * 0x10001u;
    const uint32_t s = R32(SAR(0)), d = R32(DAR(0)), t = R32(DMATCR(0)), c = R32(CHCR(0)), ist = R32(ISTNRM);
    const int ok = done && good == 1024 && s == PHYS(bufA) + 4096 && d == d0 && t == 0 && c == 0x852C3u &&
                   (ist & (1u << 11)) && R32(PDSTARD) == PHYS(bufA) + 4096 && R32(PDLEND) == 0 && nsmp > 8 &&
                   smp_bad == 0 && dt >= 31160 && dt <= 33100;
    sprintf(g_b, "done=%d words=%d SAR0-start=%ld DAR0 kept=%d DMATCR0=%lu CHCR0=%08lx ist11=%lu samples=%d bad=%d icyc=%lu",
            done, good, (long)(s - PHYS(bufA)), d == d0, (unsigned long)t, (unsigned long)c,
            (unsigned long)((ist >> 11) & 1), nsmp, smp_bad, (unsigned long)dt);
    row("P pvr-dma ram->tex 4k", ok);
    R32(ISTNRM) = 1u << 11;
  }

  /* ---- Q: PVR-DMA texture memory -> RAM, 1 KB ---- */
  {
    volatile uint32_t *b = (volatile uint32_t *)P2(bufB), *v = (volatile uint32_t *)0xA4600000u;
    for (int i = 0; i < 256; i++) b[i] = 0xEEEEEEEEu;
    R32(PDSTAP) = 0x04600000u; R32(PDSTAR) = PHYS(bufB); R32(PDLEN) = 1024; R32(PDDIR) = 1;
    smp_lo = PHYS(bufB); smp_hi = PHYS(bufB) + 1024; smp_rw = 1;
    const uint32_t pre = ch0_sig();
    R32(PDST) = 1;
    const int done = sample_until(PDST, 1, 2000000, pre);
    int good = 0;
    for (int i = 0; i < 256; i++) good += b[i] == v[i];
    const uint32_t d = R32(DAR(0)), c = R32(CHCR(0));
    sprintf(g_b, "done=%d words=%d DAR0-start=%ld CHCR0=%08lx samples=%d bad=%d", done, good,
            (long)(d - PHYS(bufB)), (unsigned long)c, nsmp, smp_bad);
    row("Q pvr-dma tex->ram 1k", done && good == 256 && d == PHYS(bufB) + 1024 && c == 0x853C3u && smp_bad == 0);
    R32(ISTNRM) = 1u << 11;
    R32(PDEN) = 0;
  }

  /* ---- A / B: G2-DMA RAM <-> AICA wave RAM, 1 KB each way ---- */
  {
    volatile uint32_t *a = (volatile uint32_t *)P2(bufA), *b = (volatile uint32_t *)P2(bufB);
    for (int i = 0; i < 256; i++) { a[i] = 0x62000000u + (uint32_t)i * 3u; b[i] = 0xEEEEEEEEu; }
    R32(G2APRO) = 0x4659007Fu;
    R32(ADSTAG) = 0x00820000u; R32(ADSTAR) = PHYS(bufA); R32(ADLEN) = 1024; R32(ADDIR) = 0; R32(ADTSEL) = 0;
    R32(ADEN) = 1;
    smp_lo = PHYS(bufA); smp_hi = PHYS(bufA) + 1024; smp_rw = 0;
    uint32_t pre = ch0_sig(), t0 = R32(TCNT(1));
    R32(ADST) = 1;
    int done = sample_until(ADST, 1, 4000000, pre);
    uint32_t dt = (t0 - R32(TCNT(1))) * 16;
    const uint32_t s = R32(SAR(0)), c = R32(CHCR(0)), ist = R32(ISTNRM);
    const uint32_t w0 = R32(0xA0820000u), w1 = R32(0xA0820000u + 4 * 127), w2 = R32(0xA0820000u + 4 * 255);
    int ok = done && s == PHYS(bufA) + 1024 && c == 0x852C3u && (ist & (1u << 15)) && smp_bad == 0 &&
             w0 == 0x62000000u && w1 == 0x62000000u + 127 * 3 && w2 == 0x62000000u + 255 * 3 && dt >= 37620 && dt <= 39950;
    sprintf(g_b, "done=%d SAR0-start=%ld CHCR0=%08lx ist15=%lu aica=%08lx %08lx %08lx samples=%d bad=%d icyc=%lu", done,
            (long)(s - PHYS(bufA)), (unsigned long)c, (unsigned long)((ist >> 15) & 1), (unsigned long)w0,
            (unsigned long)w1, (unsigned long)w2, nsmp, smp_bad, (unsigned long)dt);
    row("A g2-dma ram->aica 1k", ok);
    R32(ISTNRM) = 1u << 15;
    R32(ADSTAR) = PHYS(bufB); R32(ADDIR) = 1;
    smp_lo = PHYS(bufB); smp_hi = PHYS(bufB) + 1024; smp_rw = 1;
    pre = ch0_sig(); t0 = R32(TCNT(1));
    R32(ADST) = 1;
    done = sample_until(ADST, 1, 4000000, pre);
    dt = (t0 - R32(TCNT(1))) * 16;
    int good = 0;
    for (int i = 0; i < 256; i++) good += b[i] == 0x62000000u + (uint32_t)i * 3u;
    const uint32_t d = R32(DAR(0)), c2 = R32(CHCR(0));
    ok = done && good == 256 && d == PHYS(bufB) + 1024 && c2 == 0x853C3u && smp_bad == 0 && dt >= 41310 && dt <= 43870;
    sprintf(g_b, "done=%d words=%d DAR0-start=%ld CHCR0=%08lx samples=%d bad=%d icyc=%lu", done, good,
            (long)(d - PHYS(bufB)), (unsigned long)c2, nsmp, smp_bad, (unsigned long)dt);
    row("B g2-dma aica->ram 1k", ok);
    R32(ISTNRM) = 1u << 15;
    R32(ADEN) = 0;
  }

  /* ---- M: maple GETCOND to A0, DEVINFO to B0 ---- */
  {
    volatile uint32_t *l = (volatile uint32_t *)P2(mlist), *ra = (volatile uint32_t *)P2(rx_a),
                      *rb = (volatile uint32_t *)P2(rx_b);
    for (int i = 0; i < 8; i++) { ra[i] = 0x5A5A5A5Au; rb[i] = 0x5A5A5A5Au; }
    l[0] = 0x00000001u;                                    /* port A, 1 more word */
    l[1] = PHYS(rx_a);
    l[2] = 0x01002009u;                                    /* GETCOND to A0 */
    l[3] = 0x01000000u;                                    /* function: controller */
    l[4] = 0x80010000u;                                    /* the last frame: port B */
    l[5] = PHYS(rx_b);
    l[6] = 0x00406001u;                                    /* DEVINFO to B0 */
    R32(MDAPRO) = 0x6155404Fu; R32(MSYS) = 0xC3500000u; R32(MDTSEL) = 0; R32(MDEN) = 1;
    R32(MDSTAR) = PHYS(mlist);
    holly_route(1u << 12);
    g_istnrm_ack = 1u << 12; g_nent = 0;
    imask(1);
    const uint32_t t0 = R32(TCNT(1));
    R32(MDST) = 1;
    const int got = spin_until(&g_nent, 1, 3000000);
    imask(15);
    spin(2000);                                            /* the last response unit may follow MDST 0 ([HW] S1) */
    const uint32_t dt = got ? t0 - g_ent[0].spc : 0;
    const uint32_t s = R32(SAR(0)), d = R32(DAR(0)), c = R32(CHCR(0));
    /* 40,543 + 205,273 - 511 = 245,305 Icyc = 15,332 TMU1 counts (board.md 4.4) */
    const int ok = got && g_nent == 1 && g_ent[0].code == 0x3A0 && (g_ent[0].a & (1u << 12)) && g_ent[0].b == 0 &&
                   ra[0] == 0x03200008u && ra[1] == 0x01000000u && rb[0] == 0xFFFFFFFFu && s == PHYS(mlist) + 32 &&
                   d == PHYS(rx_b) + 32 && c == 0x853C3u && dt >= 15255 && dt <= 15409;
    sprintf(g_b, "takes=%d mdst=%lu rxA=%08lx %08lx rxB=%08lx SAR0-list=%ld DAR0-rxB=%ld CHCR0=%08lx counts=%lu", g_nent,
            (unsigned long)g_ent[0].b, (unsigned long)ra[0], (unsigned long)ra[1], (unsigned long)rb[0],
            (long)(s - PHYS(mlist)), (long)(d - PHYS(rx_b)), (unsigned long)c, (unsigned long)dt);
    row("M maple getcond A0 devinfo B0", ok);
    R32(MDEN) = 0;
    holly_route(0);
  }

  /* ---- C / R: CH2-DMA to the texture path (dmac1 S13) ---- */
  for (int k = 0; k < 2; k++) {
    const uint32_t n_dmac = 64, n_holly = k ? 32 : 64, vofs = k ? 0x480000u : 0x400000u;
    volatile uint32_t *a = (volatile uint32_t *)P2(bufA), *v = (volatile uint32_t *)(uintptr_t)(0xA4000000u + vofs);
    for (uint32_t i = 0; i < n_dmac * 8; i++) a[i] = 0xC0DE0000u + i;
    for (uint32_t i = 0; i < n_dmac * 8 + 16; i++) v[i] = 0xEEEEEEEEu;
    R32(ISTNRM) = 1u << 19;
    (void)R32(CHCR(2)); R32(CHCR(2)) = 0;
    R32(SAR(2)) = PHYS(bufA); R32(DAR(2)) = 0x0C0FFEE0u; R32(DMATCR(2)) = n_dmac;
    R32(CHCR(2)) = 0x12C1u;
    R32(LMMODE0) = 0;
    R32(C2DSTAT) = 0x11000000u | vofs; R32(C2DLEN) = n_holly * 32u;
    R32(C2DST) = 1;
    int ended = 0;
    const uint32_t t0 = pmc();
    while (pmc() - t0 < 4000000) if (R32(ISTNRM) & (1u << 19)) { ended = 1; break; }
    spin(500);
    uint32_t good = 0, eeee = 0, other = 0;
    for (uint32_t i = 0; i < n_dmac * 8 + 16; i++) {
      if (i < n_holly * 8 && v[i] == 0xC0DE0000u + i) good++; else if (v[i] == 0xEEEEEEEEu) eeee++; else other++;
    }
    const uint32_t s = R32(SAR(2)), d = R32(DAR(2)), t = R32(DMATCR(2)), c = R32(CHCR(2)), cs = R32(C2DSTAT),
                   cl = R32(C2DLEN), st = R32(C2DST);
    const int ok = ended && s == PHYS(bufA) + n_holly * 32 && d == 0x0C0FFEE0u && t == n_dmac - n_holly &&
                   c == (k ? 0x12C1u : 0x12C3u) && cs == (0x11000000u | vofs) + n_holly * 32 && cl == 0 && st == 0 &&
                   good == n_holly * 8 && other == 0;
    sprintf(g_b, "ist19=%d SAR2-start=%ld DAR2=%08lx DMATCR2=%lu CHCR2=%08lx C2DSTAT=%08lx C2DLEN=%lu C2DST=%lu "
            "good=%lu untouched=%lu other=%lu", ended, (long)(s - PHYS(bufA)), (unsigned long)d, (unsigned long)t,
            (unsigned long)c, (unsigned long)cs, (unsigned long)cl, (unsigned long)st, (unsigned long)good,
            (unsigned long)eeee, (unsigned long)other);
    row(k ? "R ch2 dmatcr2 64 c2dlen 32" : "C ch2 tex 64 units", ok);
    (void)R32(CHCR(2)); R32(CHCR(2)) = 0;
    R32(ISTNRM) = 1u << 19;
  }

  /* ---- S: a 64 KB CH2-DMA with the CPU polling (dmac1 S13's loop) ---- */
  {
    static uint32_t big[16384] __attribute__((aligned(32)));
    volatile uint32_t *a = (volatile uint32_t *)P2(big);
    ocbp_range(big, sizeof big);
    for (uint32_t i = 0; i < 16384; i++) a[i] = 0x5C000000u + i;
    R32(ISTNRM) = 1u << 19;
    (void)R32(CHCR(2)); R32(CHCR(2)) = 0;
    R32(SAR(2)) = PHYS(big); R32(DAR(2)) = 0x0C0FFEE0u; R32(DMATCR(2)) = 2048;
    R32(CHCR(2)) = 0x12C1u;
    R32(LMMODE0) = 0;
    R32(C2DSTAT) = 0x11400000u; R32(C2DLEN) = 2048u * 32u;
    const uint32_t t0 = pmc();
    uint32_t t_ist = 0, polls = 0, t_first = 0;
    R32(C2DST) = 1;
    while (pmc() - t0 < 4000000u) {
      const uint32_t t = pmc() - t0;
      const uint32_t ist = R32(ISTNRM);
      if (!t_first) t_first = pmc() - t0;
      polls++;
      if (ist & (1u << 19)) { t_ist = t; break; }
    }
    spin(500);
    const uint32_t tc = R32(DMATCR(2)), c = R32(CHCR(2));
    sprintf(g_b, "polls=%lu first poll back at %lu ISTNRM19 at %lu (console 23613) DMATCR2=%lu CHCR2=%08lx",
            (unsigned long)polls, (unsigned long)t_first, (unsigned long)t_ist, (unsigned long)tc, (unsigned long)c);
    row("S ch2 64k with the cpu polling", polls > 100 && t_ist >= 22432 && t_ist <= 24794 && tc == 0 && c == 0x12C3u);
    (void)R32(CHCR(2)); R32(CHCR(2)) = 0;
    R32(ISTNRM) = 1u << 19;
  }

  /* ---- W: CPU writes to channel 0 in DDT mode, then Holly's next DTR (dmac2 T1) ---- */
  {
    const uint32_t c0 = R32(CHCR(0));
    R32(SAR(0)) = 0x0C0DEAD0u; const uint32_t s1 = R32(SAR(0));
    R32(DAR(0)) = 0x0C0BEEF0u; const uint32_t d1 = R32(DAR(0));
    R32(DMATCR(0)) = 7; const uint32_t t1 = R32(DMATCR(0));
    R32(CHCR(0)) = 0; const uint32_t c1 = R32(CHCR(0));
    volatile uint32_t *a = (volatile uint32_t *)P2(bufA);
    for (int i = 0; i < 16; i++) a[i] = 0x77000000u + (uint32_t)i;
    R32(PDSTAP) = 0x04600000u; R32(PDSTAR) = PHYS(bufA); R32(PDLEN) = 64; R32(PDDIR) = 0; R32(PDEN) = 1;
    R32(PDST) = 1;
    const uint32_t tw = pmc();
    while ((R32(PDST) & 1) && pmc() - tw < 400000) {}
    spin(200);
    const uint32_t s2 = R32(SAR(0)), c2 = R32(CHCR(0)), t2 = R32(DMATCR(0));
    sprintf(g_b, "CHCR0 before=%08lx | after w: SAR0=%08lx DAR0=%08lx DMATCR0=%08lx CHCR0=%08lx | after 2 units: "
            "SAR0-start=%ld DMATCR0=%lu CHCR0=%08lx", (unsigned long)c0, (unsigned long)s1, (unsigned long)d1,
            (unsigned long)t1, (unsigned long)c1, (long)(s2 - PHYS(bufA)), (unsigned long)t2, (unsigned long)c2);
    row("W cpu writes ch0 then a dtr", s1 == 0 && d1 == 0 && t1 == 0 && c1 == c0 && s2 == PHYS(bufA) + 64 &&
        t2 == 0 && c2 == 0x852C3u);
    R32(ISTNRM) = 1u << 11; R32(PDEN) = 0;
  }

  /* ---- X: Holly's units against channel 1 and the CPU ---- */
  {
    volatile uint32_t *a = (volatile uint32_t *)P2(bufA), *v = (volatile uint32_t *)0xA4600000u,
                      *cs = (volatile uint32_t *)P2(bufC), *cd = (volatile uint32_t *)P2(bufD);
    for (int i = 0; i < 1024; i++) { a[i] = 0x58000000u ^ ((uint32_t)i * 0x9E3779B9u); v[i] = 0; }
    for (int i = 0; i < 512; i++) { cs[i] = 0x43000000u + (uint32_t)i; cd[i] = 0; }
    (void)R32(CHCR(1)); R32(CHCR(1)) = 0;
    R32(SAR(1)) = PHYS(bufC); R32(DAR(1)) = PHYS(bufD); R32(DMATCR(1)) = 64;
    R32(PDSTAP) = 0x04600000u; R32(PDSTAR) = PHYS(bufA); R32(PDLEN) = 4096; R32(PDDIR) = 0; R32(PDEN) = 1;
    R32(PDST) = 1;
    R32(CHCR(1)) = 0x5441u;                                /* DM SM +, auto (RS 0100), cycle steal, 32 B, DE */
    uint32_t sum = 0;
    const uint32_t tw = pmc();
    int pdone = 0, cdone = 0;
    while (pmc() - tw < 4000000 && !(pdone && cdone)) {
      for (int i = 0; i < 16; i++) sum += cs[i * 32];       /* the CPU on the bus too */
      pdone = !(R32(PDST) & 1);
      cdone = R32(DMATCR(1)) == 0;
    }
    spin(200);
    int gv = 0, gd = 0;
    for (int i = 0; i < 1024; i++) gv += v[i] == (0x58000000u ^ ((uint32_t)i * 0x9E3779B9u));
    for (int i = 0; i < 512; i++) gd += cd[i] == 0x43000000u + (uint32_t)i;
    const uint32_t c1 = R32(CHCR(1)), s0 = R32(SAR(0)), c0 = R32(CHCR(0));
    sprintf(g_b, "pvr=%d ch1=%d tex=%d dst=%d CHCR1=%08lx SAR0-start=%ld CHCR0=%08lx sum=%08lx", pdone, cdone, gv, gd,
            (unsigned long)c1, (long)(s0 - PHYS(bufA)), (unsigned long)c0, (unsigned long)sum);
    row("X ch0 units with ch1 and the cpu", pdone && cdone && gv == 1024 && gd == 512 && (c1 & 2) &&
        s0 == PHYS(bufA) + 4096 && c0 == 0x852C3u);
    (void)R32(CHCR(1)); R32(CHCR(1)) = 0;
    R32(ISTNRM) = 1u << 11; R32(PDEN) = 0;
  }

  /* ---- Y: Holly -> memory units against channel-1 word units (cycle steal) ---- */
  {
    volatile uint32_t *b = (volatile uint32_t *)P2(bufB), *v = (volatile uint32_t *)0xA4600000u,
                      *cs = (volatile uint32_t *)P2(bufC), *cd = (volatile uint32_t *)P2(bufD);
    for (int i = 0; i < 256; i++) { v[i] = 0x59000000u + (uint32_t)i * 0x01010101u; b[i] = 0xEEEEEEEEu; }
    for (int i = 0; i < 512; i++) { cs[i] = 0x2B000000u ^ (uint32_t)i; cd[i] = 0; }
    (void)R32(CHCR(1)); R32(CHCR(1)) = 0;
    R32(SAR(1)) = PHYS(bufC); R32(DAR(1)) = PHYS(bufD); R32(DMATCR(1)) = 1024;          /* 1,024 word units */
    R32(PDSTAP) = 0x04600000u; R32(PDSTAR) = PHYS(bufB); R32(PDLEN) = 1024; R32(PDDIR) = 1; R32(PDEN) = 1;
    R32(CHCR(1)) = 0x5421u;                                 /* DM SM +, auto, cycle steal, TS 2 (word), DE */
    R32(PDST) = 1;
    const uint32_t tw = pmc();
    int pdone = 0, cdone = 0;
    while (pmc() - tw < 4000000 && !(pdone && cdone)) { pdone = !(R32(PDST) & 1); cdone = R32(DMATCR(1)) == 0; }
    spin(300);
    int gb = 0, gd = 0;
    for (int i = 0; i < 256; i++) gb += b[i] == 0x59000000u + (uint32_t)i * 0x01010101u;
    for (int i = 0; i < 512; i++) gd += cd[i] == (0x2B000000u ^ (uint32_t)i);
    sprintf(g_b, "pvr=%d ch1=%d ram words=%d ch1 words=%d DAR0-start=%ld CHCR0=%08lx", pdone, cdone, gb, gd,
            (long)(R32(DAR(0)) - PHYS(bufB)), (unsigned long)R32(CHCR(0)));
    row("Y ch0 writes against ch1 word units", pdone && cdone && gb == 256 && gd == 512 &&
        R32(DAR(0)) == PHYS(bufB) + 1024 && R32(CHCR(0)) == 0x853C3u);
    (void)R32(CHCR(1)); R32(CHCR(1)) = 0;
    R32(ISTNRM) = 1u << 11; R32(PDEN) = 0;
  }

  /* ---- N: Holly's units during a channel-1 burst ---- */
  {
    volatile uint32_t *a = (volatile uint32_t *)P2(bufA), *cs = (volatile uint32_t *)P2(bufC),
                      *cd = (volatile uint32_t *)P2(bufD);
    for (int i = 0; i < 256; i++) a[i] = 0x4E000000u + (uint32_t)i;
    for (int i = 0; i < 512; i++) { cs[i] = 0x61000000u ^ (uint32_t)i; cd[i] = 0; }
    (void)R32(CHCR(1)); R32(CHCR(1)) = 0;
    R32(SAR(1)) = PHYS(bufC); R32(DAR(1)) = PHYS(bufD); R32(DMATCR(1)) = 8192;   /* 2 KB x 8: the copy wraps */
    R32(SAR(1)) = PHYS(bufC); R32(DAR(1)) = PHYS(bufD);
    R32(PDSTAP) = 0x04600000u; R32(PDSTAR) = PHYS(bufA); R32(PDLEN) = 1024; R32(PDDIR) = 0; R32(PDEN) = 1;
    R32(DMATCR(1)) = 0;
    uint32_t s0 = 0;
    (void)n_poll(R32(SAR(0)), 2000, &s0);                   /* warm its lines: DMATCR1 0, SAR0 at `end` at once */
    R32(DMATCR(1)) = 1024;                                  /* 1,024 word units: 2 KB, SM / DM +, ~47 kIcyc */
    R32(PDST) = 1;
    R32(CHCR(1)) = 0x54A1u;                                 /* DM SM +, auto, burst, TS 2 (word), DE */
    const uint32_t tcr_at_end = n_poll(PHYS(bufA) + 1024, 2000000, &s0);
    spin(300);
    while (R32(PDST) & 1) {}
    int gd = 0;
    for (int i = 0; i < 512; i++) gd += cd[i] == (0x61000000u ^ (uint32_t)i);
    volatile uint32_t *v = (volatile uint32_t *)0xA4600000u;
    int gv = 0;
    for (int i = 0; i < 256; i++) gv += v[i] == 0x4E000000u + (uint32_t)i;
    sprintf(g_b, "DMATCR1 when SAR0 reached the end=%lu SAR0-start=%ld ch1 words=%d tex=%d", (unsigned long)tcr_at_end,
            (long)(s0 - PHYS(bufA)), gd, gv);
    row("N ch0 units during a ch1 burst", tcr_at_end != 0xFFFFFFFFu && tcr_at_end > 0 && gd == 512 && gv == 256);
    (void)R32(CHCR(1)); R32(CHCR(1)) = 0;
    R32(ISTNRM) = 1u << 11; R32(PDEN) = 0;
  }

  sprintf(g_b, "DMAOR=%08lx", (unsigned long)R32(DMAOR));
  row("Z dmaor unchanged", R32(DMAOR) == dmaor && dmaor == 0x8201u);
  tmu_reset();
  printf("DX done fails=%d\n", fails);
  return 0;
}
