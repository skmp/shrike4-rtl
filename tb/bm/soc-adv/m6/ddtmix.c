/* ddtmix.c -- the M6 independent verification's program (agents/reports/integration/M6-verify.md), the M6 gate's
 * regression test since M6-fix (agents/reports/integration/M6-fix.md; rows G / H are M6-verify F1's repro): board phase
 * 2 with every Holly engine, a DMAC channel and the CPU on the bus AT ONCE (soc_tb --ddt, rm, --lockstep --ccr 0x090D).  ddtx (the M6
 * gate's program) runs one engine at a time or one engine against channel 1; here the DDT port serialises several
 * engines' units, channel 2's direct requests share the pins with channel 0's handshakes, the single-cycle FIFO of
 * v1_dmac_xfer holds channel-0 writes beside channel-2 reads, and the BSC's CPU turn (SOC.md 3.3.2) meets locked TAS.B
 * pairs, SQ bursts and uncached stores.  DMAOR.PR 10 (the stub's 0x8201): CH2 > CH0 > CH1 > CH3.
 * Rows (each prints "MX <row> ... OK|FAIL"; "MX done fails=<n>"):
 *   A  all at once: PVR-DMA RAM -> texture 4 KB (128 R/W 0 units), G2-DMA AICA -> RAM 1 KB (32 R/W 1 units), a maple
 *      GETCOND to A0 (a list unit, a response unit), CH2-DMA 64 units to the texture path, channel 1 copying 2 KB in
 *      word units by auto-request cycle steal, and the CPU running TAS.B on a P2 byte, uncached stores, SQ bursts to
 *      RAM, uncached reads and DMAOR rewrites (same value) until every engine ended: every destination byte right,
 *      every engine's end state, the CPU's own data right, the CPU made progress (TAS.B count)
 *   B  channel-0 write units against channel-2 read units, swept: a 256-byte PVR-DMA texture -> RAM (8 R/W 1 units,
 *      251 Icyc apart) with a 32-unit CH2-DMA started 8 j Icyc after it, j = 0..39 (one PVR unit period and more):
 *      the RAM and texture bytes right in every case (v1_dmac_xfer's sgl_q holds a write beside reads)
 *   C  G2-DMA RAM -> AICA 2 KB (64 R/W 0 units, 1,212 Icyc apart) with a maple GETCOND started 0 / 9,000 / 23,000 /
 *      31,000 Icyc after it (its response unit, R/W 1, lands between G2's read units): both right
 *   E  SLEEP while PVR-DMA 4 KB and a maple GETCOND run, woken by ISTNRM 11 / 12 at level 2 (the bus is all the
 *      DMAC's while the CPU sleeps; TMU0 underflows every ~20 kIcyc at level 3 as a backstop wake-up, so a take
 *      between the loop's test and its SLEEP cannot hang the run): the data right, both Holly interrupts taken
 *   F  CH2-DMA 64 KB with the CPU running TAS.B (a locked pair) on a P2 byte all through it (the CPU turn at every
 *      break in Holly's requests, the lock never broken): more than 50 TAS.B done before the end, the byte's value
 *      and T sequence consistent (the first TAS sets T, every later one clears it), the texture words right
 *   G  a channel-1 burst (1,024 word units, TM 1) with PVR-DMA 1 KB units preempting it at unit boundaries and the
 *      CPU issuing uncached RAM reads: a burst session never releases the bus [HW dmac2 T4], so no CPU read completes
 *      while DMATCR1 is still above 0 (at most one, the read in flight when the burst started); both copies right
 *   H  the same burst with a 64 KB CH2-DMA (Holly-paced) under DMAOR.PR 11 (round robin; with PR 10 CH2 above
 *      channel 1 simply keeps channel 1 waiting until it ends): both programmed with DME 0, then DME 1; channel 1's
 *      burst and CH2's units alternate ("the bus is not released to the CPU until channel 1 transfer ends", hm
 *      14.3.4): no CPU read may complete while 0 < DMATCR1 < 1024 (at most the one in flight); both copies right */
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
#define QACR0    0xFF000038u
#define QACR1    0xFF00003Cu
#define P2(p)    (((uint32_t)(uintptr_t)(p) & 0x1FFFFFFFu) | 0xA0000000u)
#define PHYS(p)  ((uint32_t)(uintptr_t)(p) & 0x1FFFFFFFu)
#define AICA     0xA0820000u
#define TEXP     0xA4600000u                 /* PVR-DMA's texture window (64-bit path) */
#define TEXC     0xA4500000u                 /* CH2-DMA's texture window (C2DSTAT 0x11500000) */

static int fails;
static volatile uint32_t g_ack;
static volatile int g_n11, g_n12;

static void h_all(irq_t code, irq_context_t *ctx, void *d) {
  (void)d;
  if (code == 0x400) {                                  /* TMU0 underflow (row E's backstop wake-up) */
    R16(TCR(0)) = (uint16_t)(R16(TCR(0)) & ~UNF);
    (void)R16(TCR(0));
    return;
  }
  if (code == 0x3A0) {                                  /* IRL level 2: Holly ISTNRM (IML2NRM) */
    const uint32_t n = R32(ISTNRM);
    if (n & (1u << 11)) g_n11++;
    if (n & (1u << 12)) g_n12++;
    R32(ISTNRM) = n & g_ack;
    (void)R32(ISTNRM);
    logent(code, ctx->pc, ctx->sr, n, R32(MDST));
    return;
  }
  printf("MX unexpected code %03lx spc %08lx\n", (unsigned long)code, (unsigned long)ctx->pc);
  R32(0xB8000000u) = 0x3FE;
  for (;;) {}
}

static char g_b[360];
static void row(const char *name, int ok) {
  printf("MX %s %s %s\n", name, g_b, ok ? "OK" : "FAIL");
  if (!ok) fails++;
}
static void holly_route(uint32_t l2n) {
  R32(IML2NRM) = l2n; R32(IML2EXT) = 0; R32(IML2ERR) = 0;
  R32(IML4NRM) = 0; R32(IML4EXT) = 0; R32(IML4ERR) = 0;
  R32(IML6NRM) = 0; R32(IML6EXT) = 0; R32(IML6ERR) = 0;
  R32(ISTNRM) = 0x3FFFFF; R32(ISTERR) = 0xFFFFFFFF;
  (void)R32(ISTNRM);
}
static int tas(uint32_t a) {
  int t;
  __asm__ __volatile__("tas.b @%1\n\tmovt %0" : "=r"(t) : "r"(a) : "t", "memory");
  return t;
}
__asm__(".text\n.align 2\n_mx_sleep: sleep\n rts\n nop\n");
void mx_sleep(void) __asm__("_mx_sleep");

static uint32_t pat(uint32_t k, uint32_t i) { return (k * 0x01000193u) ^ (i * 0x9E3779B1u) ^ 0x5A000000u; }
static void sq_burst(uint32_t pa, uint32_t k) {           /* one 32-byte SQ burst of pat(k, word) to physical pa */
  volatile uint32_t *d = (volatile uint32_t *)(0xE0000000u | (pa & 0x03FFFFE0u));
  for (int w = 0; w < 8; w++) d[w] = pat(k, (pa >> 2) + (uint32_t)w);
  __asm__ __volatile__("pref @%0" : : "r"(d) : "memory");
}

static uint32_t bufA[1024] __attribute__((aligned(32)));      /* PVR-DMA source (4 KB) */
static uint32_t bufB[1024] __attribute__((aligned(32)));      /* G2-DMA destination / PVR-DMA texture -> RAM */
static uint32_t bufC[512] __attribute__((aligned(32)));       /* channel 1 source (2 KB) */
static uint32_t bufD[512] __attribute__((aligned(32)));       /* channel 1 destination */
static uint32_t bufE[512] __attribute__((aligned(32)));       /* CH2-DMA source (64 units) */
static uint32_t bufF[512] __attribute__((aligned(32)));       /* the CPU's uncached stores */
static uint32_t bufS[512] __attribute__((aligned(32)));       /* the CPU's SQ bursts (64 lines) */
static uint32_t mlist[8] __attribute__((aligned(32)));
static uint32_t rx_a[8] __attribute__((aligned(32)));
static uint8_t tasb_mem[32] __attribute__((aligned(32)));
#define tasb ((volatile uint8_t *)P2(tasb_mem))           /* every access through P2 (TAS.B works on memory) */
static uint32_t big[16384] __attribute__((aligned(32)));      /* F: 64 KB */

static void maple_start(void) {
  volatile uint32_t *l = (volatile uint32_t *)P2(mlist), *ra = (volatile uint32_t *)P2(rx_a);
  for (int i = 0; i < 8; i++) ra[i] = 0x5A5A5A5Au;
  l[0] = 0x80000001u;                                    /* the last frame: port A, 1 more word */
  l[1] = PHYS(rx_a);
  l[2] = 0x01002009u;                                    /* GETCOND to A0 */
  l[3] = 0x01000000u;                                    /* function: controller */
  R32(MDAPRO) = 0x6155404Fu; R32(MSYS) = 0xC3500000u; R32(MDTSEL) = 0; R32(MDEN) = 1;
  R32(MDSTAR) = PHYS(mlist);
  R32(MDST) = 1;
}
static int maple_ok(void) {
  volatile uint32_t *ra = (volatile uint32_t *)P2(rx_a);
  return ra[0] == 0x03200008u && ra[1] == 0x01000000u;
}
static void ch2_start(uint32_t src, uint32_t n, uint32_t vofs) {
  (void)R32(CHCR(2)); R32(CHCR(2)) = 0;
  R32(SAR(2)) = src; R32(DAR(2)) = 0x0C0FFEE0u; R32(DMATCR(2)) = n;
  R32(CHCR(2)) = 0x12C1u;                                /* SM +, RS 0010 (external, single, memory -> device), burst, 32 B */
  R32(LMMODE0) = 0;
  R32(C2DSTAT) = 0x11000000u | vofs; R32(C2DLEN) = n * 32u;
  R32(C2DST) = 1;
}
static void ch1_start(uint32_t n, uint32_t chcr) {
  (void)R32(CHCR(1)); R32(CHCR(1)) = 0;
  R32(SAR(1)) = PHYS(bufC); R32(DAR(1)) = PHYS(bufD); R32(DMATCR(1)) = n;
  R32(CHCR(1)) = chcr;
}

int main(void) {
  bm_default = h_all;
  imask(15);
  tmu_reset();
  holly_route(0);
  const uint32_t dmaor = R32(DMAOR);
  ocbp_range(bufA, sizeof bufA); ocbp_range(bufB, sizeof bufB); ocbp_range(bufC, sizeof bufC);
  ocbp_range(bufD, sizeof bufD); ocbp_range(bufE, sizeof bufE); ocbp_range(bufF, sizeof bufF);
  ocbp_range(bufS, sizeof bufS); ocbp_range(mlist, sizeof mlist); ocbp_range(rx_a, sizeof rx_a);
  ocbp_range(tasb_mem, sizeof tasb_mem); ocbp_range(big, sizeof big);
  R32(QACR0) = 0x0C; R32(QACR1) = 0x0C;                 /* the store queues write area 3 */
  R32(PDAPRO) = 0x6702007Fu; R32(G2APRO) = 0x4659007Fu;

  /* ---- A: everything at once ---- */
  {
    volatile uint32_t *a = (volatile uint32_t *)P2(bufA), *b = (volatile uint32_t *)P2(bufB),
                      *cs = (volatile uint32_t *)P2(bufC), *cd = (volatile uint32_t *)P2(bufD),
                      *e = (volatile uint32_t *)P2(bufE), *f = (volatile uint32_t *)P2(bufF),
                      *s = (volatile uint32_t *)P2(bufS), *tp = (volatile uint32_t *)TEXP,
                      *tc = (volatile uint32_t *)TEXC, *w = (volatile uint32_t *)AICA;
    for (int i = 0; i < 1024; i++) { a[i] = pat(1, (uint32_t)i); b[i] = 0xEEEEEEEEu; tp[i] = 0; }
    for (int i = 0; i < 256; i++) w[i] = pat(2, (uint32_t)i);
    for (int i = 0; i < 512; i++) { cs[i] = pat(3, (uint32_t)i); cd[i] = 0; e[i] = pat(4, (uint32_t)i);
                                    f[i] = 0; s[i] = 0; tc[i] = 0xEEEEEEEEu; }
    tasb[0] = 0;
    R32(ISTNRM) = (1u << 11) | (1u << 12) | (1u << 15) | (1u << 19);
    /* start them back to back */
    ch1_start(1024, 0x5421u);                            /* DM SM +, auto, cycle steal, word, DE */
    R32(PDSTAP) = 0x04600000u; R32(PDSTAR) = PHYS(bufA); R32(PDLEN) = 4096; R32(PDDIR) = 0; R32(PDTSEL) = 0;
    R32(PDEN) = 1; R32(PDST) = 1;
    R32(ADSTAG) = 0x00820000u; R32(ADSTAR) = PHYS(bufB); R32(ADLEN) = 1024; R32(ADDIR) = 1; R32(ADTSEL) = 0;
    R32(ADEN) = 1; R32(ADST) = 1;
    maple_start();
    ch2_start(PHYS(bufE), 64, 0x500000u);
    uint32_t ntas = 0, nt1 = 0, nst = 0, nsq = 0, sum = 0, iter = 0;
    int pd = 0, gd = 0, md = 0, c2 = 0, c1 = 0;
    const uint32_t t0 = pmc();
    while (pmc() - t0 < 6000000u && !(pd && gd && md && c2 && c1)) {
      nt1 += (uint32_t)tas(P2(&tasb[0])); ntas++;
      f[iter & 511] = pat(5, iter & 511); nst++;
      if ((iter & 7) == 0 && nsq < 64) { sq_burst(PHYS(bufS) + 32u * nsq, 6); nsq++; }
      sum += cs[(iter * 37) & 511];
      if ((iter & 15) == 5) R32(DMAOR) = dmaor;          /* a DMAOR rewrite of the same value (DDT, PR, DME) */
      iter++;
      pd = !(R32(PDST) & 1); gd = !(R32(ADST) & 1); md = !(R32(MDST) & 1);
      c2 = (R32(ISTNRM) >> 19) & 1; c1 = R32(DMATCR(1)) == 0;
    }
    const uint32_t tend = pmc() - t0;
    spin(3000);                                          /* maple's response unit may follow MDST 0 ([HW] S1) */
    int ga = 0, gb = 0, gcd = 0, gtc = 0, gf = 0, gs = 0;
    for (int i = 0; i < 1024; i++) ga += tp[i] == pat(1, (uint32_t)i);
    for (int i = 0; i < 256; i++) gb += b[i] == pat(2, (uint32_t)i);
    for (int i = 256; i < 1024; i++) gb += b[i] == 0xEEEEEEEEu;          /* nothing beyond the 1 KB */
    for (int i = 0; i < 512; i++) { gcd += cd[i] == pat(3, (uint32_t)i); gtc += tc[i] == pat(4, (uint32_t)i); }
    for (uint32_t i = 0; i < 512; i++) gf += f[i] == (i < nst ? pat(5, i) : 0u);
    for (uint32_t l = 0; l < 64; l++)
      for (uint32_t k = 0; k < 8; k++) gs += s[8 * l + k] == (l < nsq ? pat(6, ((PHYS(bufS) + 32u * l) >> 2) + k) : 0u);
    const uint32_t ist = R32(ISTNRM), c2r = R32(CHCR(2)), t2 = R32(DMATCR(2)), c1r = R32(CHCR(1));
    const int ok = pd && gd && md && c2 && c1 && ga == 1024 && gb == 1024 && gcd == 512 && gtc == 512 && gf == 512 &&
                   gs == 512 && maple_ok() && nt1 == 1 && tasb[0] == 0x80 && ntas > 100 && nsq > 8 &&
                   (ist & (1u << 11)) && (ist & (1u << 12)) && (ist & (1u << 15)) && c2r == 0x12C3u && t2 == 0 &&
                   (c1r & 2) && R32(DMAOR) == dmaor;
    sprintf(g_b, "ends pvr=%d g2=%d maple=%d ch2=%d ch1=%d at %lu | tex=%d g2ram=%d ch1=%d ch2tex=%d cpu=%d sq=%d/%lu "
            "maple=%d | tas=%lu t1=%lu byte=%02x stores=%lu sum=%08lx ISTNRM=%08lx CHCR2=%08lx DMATCR2=%lu CHCR1=%08lx",
            pd, gd, md, c2, c1, (unsigned long)tend, ga, gb, gcd, gtc, gf, gs, (unsigned long)nsq, maple_ok(),
            (unsigned long)ntas, (unsigned long)nt1, tasb[0], (unsigned long)nst, (unsigned long)sum,
            (unsigned long)ist, (unsigned long)c2r, (unsigned long)t2, (unsigned long)c1r);
    row("A all engines ch1 and the cpu at once", ok);
    (void)R32(CHCR(1)); R32(CHCR(1)) = 0; (void)R32(CHCR(2)); R32(CHCR(2)) = 0;
    R32(ISTNRM) = (1u << 11) | (1u << 12) | (1u << 15) | (1u << 19);
    R32(PDEN) = 0; R32(ADEN) = 0; R32(MDEN) = 0;
  }

  /* ---- B: channel-0 write units against channel-2 read units, swept ---- */
  {
    volatile uint32_t *b = (volatile uint32_t *)P2(bufB), *e = (volatile uint32_t *)P2(bufE),
                      *tp = (volatile uint32_t *)TEXP, *tc = (volatile uint32_t *)TEXC;
    int nbad = 0, first_bad = -1, nend = 0;
    for (int j = 0; j < 40; j++) {
      for (int i = 0; i < 72; i++) { tp[i] = pat(10 + (uint32_t)j, (uint32_t)i); b[i] = 0xEEEEEEEEu; }
      for (int i = 0; i < 256; i++) { e[i] = pat(60 + (uint32_t)j, (uint32_t)i); tc[i] = 0xEEEEEEEEu; }
      R32(ISTNRM) = (1u << 11) | (1u << 19);
      R32(PDSTAP) = 0x04600000u; R32(PDSTAR) = PHYS(bufB); R32(PDLEN) = 256; R32(PDDIR) = 1; R32(PDEN) = 1;
      const uint32_t t0 = pmc();
      R32(PDST) = 1;
      while (pmc() - t0 < 8u * (uint32_t)j) {}
      ch2_start(PHYS(bufE), 32, 0x500000u);
      const uint32_t tw = pmc();
      int pd = 0, c2 = 0;
      while (pmc() - tw < 400000u && !(pd && c2)) { pd = !(R32(PDST) & 1); c2 = (R32(ISTNRM) >> 19) & 1; }
      spin(300);
      int gb = 0, gt = 0;
      for (int i = 0; i < 64; i++) gb += b[i] == pat(10 + (uint32_t)j, (uint32_t)i);
      for (int i = 64; i < 72; i++) gb += b[i] == 0xEEEEEEEEu;
      for (int i = 0; i < 256; i++) gt += tc[i] == pat(60 + (uint32_t)j, (uint32_t)i);
      nend += pd && c2;
      if (!(pd && c2 && gb == 72 && gt == 256 && R32(DAR(0)) == PHYS(bufB) + 256 && R32(CHCR(2)) == 0x12C3u)) {
        nbad++;
        if (first_bad < 0) {
          first_bad = j;
          printf("MX B detail j=%d pd=%d c2=%d gb=%d gt=%d DAR0-start=%ld CHCR2=%08lx DMATCR2=%lu b0=%08lx want %08lx tc0=%08lx want %08lx\n",
                 j, pd, c2, gb, gt, (long)(R32(DAR(0)) - PHYS(bufB)), (unsigned long)R32(CHCR(2)),
                 (unsigned long)R32(DMATCR(2)), (unsigned long)b[0], (unsigned long)pat(10 + (uint32_t)j, 0),
                 (unsigned long)tc[0], (unsigned long)pat(60 + (uint32_t)j, 0));
        }
      }
      (void)R32(CHCR(2)); R32(CHCR(2)) = 0;
      R32(ISTNRM) = (1u << 11) | (1u << 19);
    }
    R32(PDEN) = 0;
    sprintf(g_b, "cases=40 ended=%d bad=%d first_bad=%d", nend, nbad, first_bad);
    row("B ch0 writes against ch2 reads swept", nbad == 0 && nend == 40);
  }

  /* ---- C: G2-DMA RAM -> AICA read units with a maple response unit among them ---- */
  {
    static const uint32_t off[4] = {0, 9000, 23000, 31000};
    volatile uint32_t *a = (volatile uint32_t *)P2(bufA), *w = (volatile uint32_t *)AICA;
    int nbad = 0;
    char sub[4][40];
    for (int k = 0; k < 4; k++) {
      for (int i = 0; i < 512; i++) { a[i] = pat(100 + (uint32_t)k, (uint32_t)i); w[i] = 0; }
      R32(ISTNRM) = (1u << 12) | (1u << 15);
      R32(ADSTAG) = 0x00820000u; R32(ADSTAR) = PHYS(bufA); R32(ADLEN) = 2048; R32(ADDIR) = 0; R32(ADTSEL) = 0;
      R32(ADEN) = 1;
      const uint32_t t0 = pmc();
      R32(ADST) = 1;
      while (pmc() - t0 < off[k]) {}
      maple_start();
      const uint32_t tw = pmc();
      int gd = 0, md = 0;
      while (pmc() - tw < 2000000u && !(gd && md)) { gd = !(R32(ADST) & 1); md = !(R32(MDST) & 1); }
      spin(3000);
      int gw = 0;
      for (int i = 0; i < 512; i++) gw += w[i] == pat(100 + (uint32_t)k, (uint32_t)i);
      const int ok = gd && md && gw == 512 && maple_ok() && R32(SAR(0)) != 0;
      sprintf(sub[k], "%lu:%d/%d/%d", (unsigned long)off[k], gd && md, gw, maple_ok());
      nbad += !ok;
      R32(ADEN) = 0; R32(MDEN) = 0;
      R32(ISTNRM) = (1u << 12) | (1u << 15);
    }
    sprintf(g_b, "offset:ended/aica words/maple %s %s %s %s bad=%d", sub[0], sub[1], sub[2], sub[3], nbad);
    row("C g2 read units with a maple response", nbad == 0);
  }

  /* ---- E: SLEEP while PVR-DMA and maple run ---- */
  {
    volatile uint32_t *a = (volatile uint32_t *)P2(bufA), *tp = (volatile uint32_t *)TEXP;
    for (int i = 0; i < 1024; i++) { a[i] = pat(200, (uint32_t)i); tp[i] = 0; }
    holly_route((1u << 11) | (1u << 12));
    g_ack = (1u << 11) | (1u << 12); g_nent = 0; g_n11 = 0; g_n12 = 0;
    R32(PDSTAP) = 0x04600000u; R32(PDSTAR) = PHYS(bufA); R32(PDLEN) = 4096; R32(PDDIR) = 0; R32(PDEN) = 1;
    R32(PDST) = 1;
    maple_start();
    R16(IPRA) = 0x3000;                                  /* TMU0 at level 3 */
    R8(TOCR) = 0; R32(TCOR(0)) = 1250; R32(TCNT(0)) = 1250; R16(TCR(0)) = UNIE; (void)R16(TCR(0));
    R8(TSTR) = (uint8_t)(R8(TSTR) | 1u);
    imask(1);
    int sleeps = 0;
    const uint32_t t0 = pmc();
    while ((g_n11 == 0 || g_n12 == 0) && pmc() - t0 < 3000000u) { mx_sleep(); sleeps++; }
    imask(15);
    tmu_reset(); R16(IPRA) = 0;
    spin(3000);
    int gt = 0;
    for (int i = 0; i < 1024; i++) gt += tp[i] == pat(200, (uint32_t)i);
    sprintf(g_b, "sleeps=%d takes=%d n11=%d n12=%d tex=%d maple=%d", sleeps, g_nent, g_n11, g_n12, gt, maple_ok());
    row("E sleep through pvr-dma and maple", g_n11 >= 1 && g_n12 >= 1 && gt == 1024 && maple_ok() && sleeps >= 1);
    holly_route(0);
    R32(PDEN) = 0; R32(MDEN) = 0;
  }

  /* ---- F: CH2-DMA 64 KB against TAS.B ---- */
  {
    volatile uint32_t *g = (volatile uint32_t *)P2(big), *tc = (volatile uint32_t *)0xA4400000u;
    for (uint32_t i = 0; i < 16384; i++) { g[i] = pat(300, i); tc[i] = 0; }
    tasb[4] = 0;
    R32(ISTNRM) = 1u << 19;
    ch2_start(PHYS(big), 2048, 0x400000u);
    uint32_t ntas = 0, nt1 = 0, tend = 0;
    const uint32_t t0 = pmc();
    while (pmc() - t0 < 4000000u) {
      nt1 += (uint32_t)tas(P2(&tasb[4])); ntas++;
      if ((R32(ISTNRM) >> 19) & 1) { tend = pmc() - t0; break; }
    }
    spin(300);
    uint32_t gt = 0;
    for (uint32_t i = 0; i < 16384; i++) gt += tc[i] == pat(300, i);
    sprintf(g_b, "tas=%lu t1=%lu byte=%02x end=%lu tex=%lu CHCR2=%08lx DMATCR2=%lu", (unsigned long)ntas,
            (unsigned long)nt1, tasb[4], (unsigned long)tend, (unsigned long)gt, (unsigned long)R32(CHCR(2)),
            (unsigned long)R32(DMATCR(2)));
    row("F ch2 64k against tas.b", ntas > 50 && nt1 == 1 && tasb[4] == 0x80 && tend > 0 && gt == 16384 &&
        R32(CHCR(2)) == 0x12C3u && R32(DMATCR(2)) == 0);
    (void)R32(CHCR(2)); R32(CHCR(2)) = 0;
    R32(ISTNRM) = 1u << 19;
  }

  /* ---- G: a channel-1 burst, PVR-DMA units preempting it, the CPU shut out ---- */
  {
    volatile uint32_t *a = (volatile uint32_t *)P2(bufA), *cs = (volatile uint32_t *)P2(bufC),
                      *cd = (volatile uint32_t *)P2(bufD), *tp = (volatile uint32_t *)TEXP,
                      *f = (volatile uint32_t *)P2(bufF);
    for (int i = 0; i < 256; i++) { a[i] = pat(400, (uint32_t)i); tp[i] = 0; }
    for (int i = 0; i < 512; i++) { cs[i] = pat(401, (uint32_t)i); cd[i] = 0; }
    R32(ISTNRM) = 1u << 11;
    R32(PDSTAP) = 0x04600000u; R32(PDSTAR) = PHYS(bufA); R32(PDLEN) = 1024; R32(PDDIR) = 0; R32(PDEN) = 1;
    R32(PDST) = 1;
    spin(20);
    uint32_t during = 0, reads = 0, sum = 0, first_tcr = 0;
    static uint32_t gsm[40][2];
    ch1_start(1024, 0x54A1u);                            /* DM SM +, auto, burst, word, DE */
    const uint32_t t0 = pmc();
    while (pmc() - t0 < 2000000u) {
      sum += f[reads & 511];                             /* an uncached RAM read: waits for the bus */
      const uint32_t t1 = R32(DMATCR(1));                /* P4: no bus */
      reads++;
      if (reads <= 40) { gsm[reads - 1][0] = t1; gsm[reads - 1][1] = pmc() - t0; }
      if (t1 != 0) { during++; if (!first_tcr) first_tcr = t1; }
      if (t1 == 0) break;
    }
    while ((R32(PDST) & 1) && pmc() - t0 < 4000000u) {}
    spin(300);
    int gd = 0, gt = 0;
    for (int i = 0; i < 512; i++) gd += cd[i] == pat(401, (uint32_t)i);
    for (int i = 0; i < 256; i++) gt += tp[i] == pat(400, (uint32_t)i);
    sprintf(g_b, "cpu reads done while DMATCR1>0: %lu (first at DMATCR1=%lu) of %lu | ch1=%d tex=%d CHCR1=%08lx",
            (unsigned long)during, (unsigned long)first_tcr, (unsigned long)reads, gd, gt, (unsigned long)R32(CHCR(1)));
    printf("MX G detail (DMATCR1@Icyc):");
    for (uint32_t k = 0; k < reads && k < 40; k++) printf(" %lu@%lu", (unsigned long)gsm[k][0], (unsigned long)gsm[k][1]);
    printf("\n");
    row("G ch1 burst with ch0 units shuts the cpu out", during <= 1 && gd == 512 && gt == 256 &&
        (R32(CHCR(1)) & 2) && !(R32(PDST) & 1));
    (void)R32(CHCR(1)); R32(CHCR(1)) = 0;
    R32(ISTNRM) = 1u << 11; R32(PDEN) = 0;
  }

  /* ---- H: a channel-1 burst with CH2-DMA units (channel 2 above channel 1) preempting it, the CPU shut out ---- */
  {
    volatile uint32_t *g = (volatile uint32_t *)P2(big), *tc = (volatile uint32_t *)0xA4400000u,
                      *cs = (volatile uint32_t *)P2(bufC), *cd = (volatile uint32_t *)P2(bufD),
                      *f = (volatile uint32_t *)P2(bufF);
    for (uint32_t i = 0; i < 16384; i++) { g[i] = pat(500, i); tc[i] = 0; }
    for (int i = 0; i < 512; i++) { cs[i] = pat(501, (uint32_t)i); cd[i] = 0; }
    R32(ISTNRM) = 1u << 19;
    uint32_t during = 0, reads = 0, sum = 0, before = 0;
    const uint32_t dm = R32(DMAOR);
    R32(DMAOR) = (dm & ~1u) | 0x300u;                    /* DME 0, PR 11 (round robin): program both */
    ch2_start(PHYS(big), 2048, 0x400000u);               /* 64 KB at Holly's paced rate (~23.7 kIcyc) */
    ch1_start(1024, 0x54A1u);                            /* DM SM +, auto, burst, word, DE */
    spin(50);
    R32(DMAOR) = dm | 0x300u;                            /* DME 1: channel 1's burst and CH2's units alternate (round
                                                            robin: channel 1 restarts after each CH2 unit, hm 14.3.4) */
    const uint32_t t0 = pmc();
    while (pmc() - t0 < 2000000u) {
      sum += f[reads & 511];                             /* an uncached RAM read */
      const uint32_t t1 = R32(DMATCR(1));
      reads++;
      if (t1 == 1024) before++;                          /* channel 1 not started yet (CH2 alone: the CPU's turn) */
      else if (t1 != 0) during++;
      if (t1 == 0) break;
    }
    const uint32_t tw = pmc();
    while (!((R32(ISTNRM) >> 19) & 1) && pmc() - tw < 400000u) {}
    spin(300);
    R32(DMAOR) = dm;
    uint32_t gt = 0;
    int gd = 0;
    for (int i = 0; i < 512; i++) gd += cd[i] == pat(501, (uint32_t)i);
    for (uint32_t i = 0; i < 16384; i++) gt += tc[i] == pat(500, i);
    sprintf(g_b, "cpu reads done while 0<DMATCR1<1024: %lu (before ch1 started %lu) of %lu | ch1=%d ch2tex=%lu CHCR2=%08lx",
            (unsigned long)during, (unsigned long)before, (unsigned long)reads, gd, (unsigned long)gt,
            (unsigned long)R32(CHCR(2)));
    row("H ch1 burst with ch2 units shuts the cpu out", during <= 1 && gd == 512 && gt == 16384 &&
        R32(CHCR(2)) == 0x12C3u);
    (void)R32(CHCR(1)); R32(CHCR(1)) = 0; (void)R32(CHCR(2)); R32(CHCR(2)) = 0;
    R32(ISTNRM) = 1u << 19;
  }

  sprintf(g_b, "DMAOR=%08lx", (unsigned long)R32(DMAOR));
  row("Z dmaor unchanged", R32(DMAOR) == dmaor && dmaor == 0x8201u);
  tmu_reset();
  printf("MX done fails=%d\n", fails);
  return 0;
}
