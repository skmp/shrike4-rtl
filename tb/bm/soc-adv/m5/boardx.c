/* boardx.c -- M5 (WI-SOC): the B1 / B2 board features of SOC.md 8.3 M5 through the chip's pins, without KOS: the
 * incremental subset's stand-in for hello1 / hello2 (make v1-soc-m5-sub; ~12M Icyc against their ~200M).  Run on rm
 * (and im) with the full board, soc_tb --lockstep --ccr 0x090D.  Built with the m1b run-time (tb/bm/serial crt0 / bmrt
 * with BM_M1B_RT: a catch-all handler, IMASK 15 at main).  Each row prints "BX <row> ... OK|FAIL"; the program judges
 * its rows, m5/check.py checks the row set and the done line.  Expected values and their sources:
 *   B1 (board.md 5.5 "B1")
 *   V    Holly's vblank-in (ISTNRM bit 3) routed to level 2 (IML2NRM, as KOS's vblank_init) wakes SLEEP: five takes of
 *        0x3A0 (hm Table 19.3: IRL 1101), each at SPG line 520 with bit 13 set ([HW] board.md 6.1: status 0x2208 at
 *        vblank-in; bit 12, the hsync pulse, depends on the read's place in the line and is not checked); the periods
 *        between the warm takes 2-5, timed by TMU1 (P/4 = 16 Icyc per count, running while the CPU sleeps), within
 *        one count of the console's 3,328,284-3,328,344 Icyc ([HW] board.md 6.1: 208,017-208,022).  The first period
 *        is printed, not judged: the first take runs a cold handler (on rm its I- / O-cache misses delay the TCNT1
 *        read by ~11 counts: 208,009)
 *   T    TMU0 wakes SLEEP: 0x400 once; TCNT0 = 1000 underflows on the 1001st P/4 count after TSTR (the channels share
 *        the prescaler, so TMU1 counts the same edges), and the handler reads TCNT1 after the wake and the run-time's
 *        entry (context save, dispatch): 1001-1040 TMU1 counts (the entry under 39 counts = 624 Icyc; the model 1016)
 *   B2 (board.md 5.5 "B2")
 *   G    G1 protection status 0x5F74EC = 3 (board.md 3.7.4; KOS cdrom_init reads the ROM otherwise); SB_GDST 0x5F7418
 *        reads 0; the no-disc drive idle: alternate status 0x50 (board.md 3.7.3)
 *   D    a GD REQ_STAT packet (ATA PACKET 0xA0, DRQ, six words; board.md 3.7.3 / 6.2): its data INTRQ reaches ISTEXT
 *        bit 0, routed to level 4 (IML4EXT, as KOS's cdrom_init) and taken as 0x360 at IMASK 3 (IRL 1011); the
 *        handler's status read (0x58: DRDY DSC DRQ) clears it; ireason 2, byte count 10, the data 07 00 04 02 ...
 *        (status 7: no disc, as KOS's cdrom_get_status reads it: hello2's "cdrom status 7"); the completion INTRQ
 *        after the last word, status 0x50
 *   M    a maple list (board.md 3.6 / 4.4): DEVINFO to A0 (the controller) and to B0 (nothing): ISTNRM bit 12 (maple
 *        DMA end) routed to level 2 (IML2NRM, as KOS's maple_init) taken as 0x3A0 with MDST 0; A0's response header
 *        0x1C200005, function 0x01000000, 0xFE060F00 ([HW] boardio), B0's block 0xFFFFFFFF ([HW]); the list's time
 *        by TMU1 within 0.5 % of board.md 4.4's frame sum (the [HW] boardio calibration): 406,566 Icyc = 25,410 counts
 *   S    TAS.B through the bus (hm TAS.B: a locked read-modify-write; the operand-cache line purged first): on an
 *        uncached (P2) byte 0 -> T = 1, 0x80; again T = 0; on a cached (P1, copy-back) byte written 0 in the cache,
 *        T = 1 and memory (read through P2) 0x80, the P1 read 0x80
 * Output: "BX <row> ... OK|FAIL", "BX done fails=<n>". */
#include "m1b.h"

#define ISTNRM   0xA05F6900u
#define ISTEXT   0xA05F6904u
#define ISTERR   0xA05F6908u
#define IML2NRM  0xA05F6910u
#define IML2EXT  0xA05F6914u
#define IML2ERR  0xA05F6918u
#define IML4NRM  0xA05F6920u
#define IML4EXT  0xA05F6924u
#define IML4ERR  0xA05F6928u
#define IML6NRM  0xA05F6930u
#define IML6EXT  0xA05F6934u
#define IML6ERR  0xA05F6938u
#define SPG_STATUS 0xA05F810Cu
#define MDSTAR   0xA05F6C04u
#define MDTSEL   0xA05F6C10u
#define MDEN     0xA05F6C14u
#define MDST     0xA05F6C18u
#define MSYS     0xA05F6C80u
#define MDAPRO   0xA05F6C8Cu
#define G1_PROT  0xA05F74ECu
#define SB_GDST  0xA05F7418u
#define GD_ALT   0xA05F7018u
#define GD_DATA  0xA05F7080u
#define GD_IREASON 0xA05F7088u
#define GD_BCL   0xA05F7090u
#define GD_BCH   0xA05F7094u
#define GD_CMD   0xA05F709Cu
#define P2(p)    (((uint32_t)(uintptr_t)(p) & 0x1FFFFFFFu) | 0xA0000000u)

static int fails;
static volatile uint32_t g_istnrm_ack;                     /* the ISTNRM bits the level-2 handler acknowledges */

static void h_all(irq_t code, irq_context_t *ctx, void *d) {
  (void)d;
  const uint32_t t1 = R32(TCNT(1));
  if (code == 0x3A0) {                                     /* level 2: vblank-in or the maple DMA end */
    const uint32_t st = R32(SPG_STATUS), n = R32(ISTNRM), md = R32(MDST);
    R32(ISTNRM) = n & g_istnrm_ack;
    (void)R32(ISTNRM);
    logent(code, t1, st, n, md);
    return;
  }
  if (code == 0x360) {                                     /* level 4: the GD INTRQ (ISTEXT bit 0) */
    const uint32_t ext = R32(ISTEXT);
    const uint32_t st = R8(GD_CMD);                        /* the status read clears INTRQ */
    (void)R32(ISTEXT);
    logent(code, t1, ctx->sr, ext, st);
    return;
  }
  if (code == 0x400) {
    R8(TSTR) = (uint8_t)(R8(TSTR) & ~1u);
    R16(TCR(0)) = (uint16_t)(R16(TCR(0)) & ~UNF);
    (void)R16(TCR(0));
    logent(code, t1, ctx->sr, 0, 0);
    return;
  }
  printf("BX unexpected code %03lx spc %08lx\n", (unsigned long)code, (unsigned long)ctx->pc);
  R32(0xB8000000u) = 0x3FE;
  for (;;) {}
}

static char g_b[256];                                      /* a row's values (sprintf) */
static void row(const char *name, int ok) {
  printf("BX %s %s %s\n", name, g_b, ok ? "OK" : "FAIL");
  if (!ok) fails++;
}

static void holly_route(uint32_t l2n, uint32_t l4e) {
  R32(IML2NRM) = l2n; R32(IML2EXT) = 0; R32(IML2ERR) = 0;
  R32(IML4NRM) = 0; R32(IML4EXT) = l4e; R32(IML4ERR) = 0;
  R32(IML6NRM) = 0; R32(IML6EXT) = 0; R32(IML6ERR) = 0;
  R32(ISTNRM) = 0x3FFFFF; R32(ISTERR) = 0xFFFFFFFF;
  (void)R32(ISTNRM);
}
/* TMU1: free-running P/4 time base (TCOR1 = TCNT1 = ~0, no interrupt) */
static void tbase(void) {
  R8(TOCR) = 0; R32(TCOR(1)) = 0xFFFFFFFFu; R32(TCNT(1)) = 0xFFFFFFFFu; R16(TCR(1)) = 0; (void)R16(TCR(1));
  R8(TSTR) = (uint8_t)(R8(TSTR) | 2u);
}
static int spin_until(volatile int *n, int want, uint32_t max_icyc) {   /* PMCTR1 counts Icyc while awake */
  const uint32_t t = pmc();
  while (*n < want) if (pmc() - t > max_icyc) return 0;
  return 1;
}

static uint32_t mlist[16] __attribute__((aligned(32)));
static uint32_t rx_a[32] __attribute__((aligned(32)));
static uint32_t rx_b[8] __attribute__((aligned(32)));
static volatile uint8_t tas_b[64] __attribute__((aligned(32)));

static int tas(uint32_t a) {
  int t;
  __asm__ __volatile__("tas.b @%1\n\tmovt %0" : "=r"(t) : "r"(a) : "t", "memory");
  return t;
}

int main(void) {
  bm_default = h_all;
  imask(15);
  tmu_reset();
  tbase();

  /* ---- V: vblank-in wakes SLEEP ---- */
  holly_route(1u << 3, 0);
  g_istnrm_ack = 1u << 3; g_nent = 0;
  imask(1);
  while (g_nent < 5) __asm__ __volatile__("sleep" : : : "memory");
  imask(15);
  {
    int ok = g_nent == 5, lines = 1;
    char per[80]; per[0] = 0;
    for (int i = 0; i < 5 && i < g_nent; i++) {
      if (g_ent[i].code != 0x3A0) ok = 0;
      if ((g_ent[i].ssr & 0x3FF) != 520 || !(g_ent[i].ssr & 0x2000)) lines = 0;
      if (i) {
        const uint32_t p = g_ent[i - 1].spc - g_ent[i].spc;
        sprintf(per + strlen(per), "%s%lu", i == 1 ? "(" : i > 2 ? "," : ") ", (unsigned long)p);
        if (i > 1 && (p < 208017 - 1 || p > 208022 + 1)) ok = 0;
      }
    }
    sprintf(g_b, "takes=%d code=%03lx status0=%04lx periods(TMU P/4)=%s", g_nent, (unsigned long)g_ent[0].code,
            (unsigned long)(g_ent[0].ssr & 0xFFFF), per);
    row("V vblank-in wakes SLEEP", ok && lines);
  }
  holly_route(0, 0);

  /* ---- T: TMU0 wakes SLEEP ---- */
  g_nent = 0;
  R16(IPRA) = (uint16_t)(5u << 12);
  R32(TCOR(0)) = 0xFFFFFFFFu; R32(TCNT(0)) = 1000; R16(TCR(0)) = UNIE; (void)R16(TCR(0));
  {
    const uint32_t t0 = R32(TCNT(1));
    R8(TSTR) = (uint8_t)(R8(TSTR) | 1u);
    imask(4);
    __asm__ __volatile__("sleep" : : : "memory");
    imask(15);
    const uint32_t dt = g_nent ? t0 - g_ent[0].spc : 0;
    sprintf(g_b, "takes=%d code=%03lx counts=%lu", g_nent, (unsigned long)g_ent[0].code, (unsigned long)dt);
    row("T TMU0 wakes SLEEP", g_nent == 1 && g_ent[0].code == 0x400 && dt >= 1001 && dt <= 1040);
  }
  R16(TCR(0)) = 0; R16(IPRA) = 0;

  /* ---- G: G1 status, SB_GDST, the idle drive ---- */
  {
    const uint32_t prot = R32(G1_PROT), gdst = R32(SB_GDST), alt = R8(GD_ALT);
    sprintf(g_b, "prot=%lx gdst=%lx alt=%02lx", (unsigned long)prot, (unsigned long)gdst, (unsigned long)alt);
    row("G g1 status", prot == 3 && gdst == 0 && alt == 0x50);
  }

  /* ---- D: a GD REQ_STAT packet, INTRQ at level 4 ---- */
  holly_route(0, 1);
  g_nent = 0;
  imask(3);
  {
    R8(GD_CMD) = 0xA0;                                     /* PACKET */
    int drq = 0;
    for (int i = 0; i < 2000 && !drq; i++) drq = (R8(GD_ALT) & 0x88) == 0x08;
    static const uint16_t pkt[6] = {0x0010, 0x0000, 0x000A, 0x0000, 0x0000, 0x0000};   /* REQ_STAT, 10 bytes */
    for (int i = 0; i < 6; i++) R16(GD_DATA) = pkt[i];
    const int got1 = spin_until(&g_nent, 1, 200000);
    const uint32_t ir = R8(GD_IREASON), bc = R8(GD_BCL) | (uint32_t)R8(GD_BCH) << 8;
    uint16_t w[5];
    for (int i = 0; i < 5; i++) w[i] = R16(GD_DATA);
    const int got2 = spin_until(&g_nent, 2, 200000);
    imask(15);
    const int ok = drq && got1 && got2 && g_nent == 2 && g_ent[0].code == 0x360 && g_ent[1].code == 0x360 &&
                   (g_ent[0].a & 1) && g_ent[0].b == 0x58 && g_ent[1].b == 0x50 && (R32(ISTEXT) & 1) == 0 && ir == 2 &&
                   bc == 10 && w[0] == 0x0007 && w[1] == 0x0204 && w[2] == 0 && w[3] == 0 && w[4] == 0;
    sprintf(g_b, "drq=%d takes=%d codes=%03lx,%03lx istext=%lx st=%02lx,%02lx ireason=%lx bc=%lu data=%04x %04x %04x",
            drq, g_nent, (unsigned long)g_ent[0].code, (unsigned long)g_ent[1].code, (unsigned long)g_ent[0].a,
            (unsigned long)g_ent[0].b, (unsigned long)g_ent[1].b, (unsigned long)ir, (unsigned long)bc, w[0], w[1], w[2]);
    row("D gd req_stat intrq", ok);
  }
  holly_route(0, 0);

  /* ---- M: a maple list, DEVINFO to A0 and B0 ---- */
  {
    volatile uint32_t *l = (volatile uint32_t *)P2(mlist), *ra = (volatile uint32_t *)P2(rx_a),
                      *rb = (volatile uint32_t *)P2(rx_b);
    for (int i = 0; i < 32; i++) ra[i] = 0x5A5A5A5Au;
    for (int i = 0; i < 8; i++) rb[i] = 0x5A5A5A5Au;
    l[0] = 0x00000000u;                                    /* port A, pattern 0, 0 more words */
    l[1] = P2(rx_a) & 0x1FFFFFFFu;
    l[2] = 0x00002001u;                                    /* DEVINFO, to A0 (0x20) from the host (0x00) */
    l[3] = 0x80010000u;                                    /* the last frame: port B */
    l[4] = P2(rx_b) & 0x1FFFFFFFu;
    l[5] = 0x00406001u;                                    /* DEVINFO, to B0 (0x60) from the host (0x40) */
    R32(MDAPRO) = 0x6155404Fu; R32(MSYS) = 0xC3500000u; R32(MDTSEL) = 0; R32(MDEN) = 1;
    R32(MDSTAR) = P2(mlist) & 0x1FFFFFFFu;
    holly_route(1u << 12, 0);
    g_istnrm_ack = 1u << 12; g_nent = 0;
    imask(1);
    const uint32_t t0 = R32(TCNT(1));
    R32(MDST) = 1;
    const int got = spin_until(&g_nent, 1, 2000000);
    imask(15);
    const uint32_t dt = got ? t0 - g_ent[0].spc : 0;
    const int ok = got && g_nent == 1 && g_ent[0].code == 0x3A0 && (g_ent[0].a & (1u << 12)) && g_ent[0].b == 0 &&
                   ra[0] == 0x1C200005u && ra[1] == 0x01000000u && ra[2] == 0xFE060F00u && rb[0] == 0xFFFFFFFFu &&
                   dt >= 25283 && dt <= 25537;
    sprintf(g_b, "takes=%d code=%03lx mdst=%lu rxA=%08lx %08lx %08lx rxB=%08lx counts=%lu", g_nent,
            (unsigned long)g_ent[0].code, (unsigned long)g_ent[0].b, (unsigned long)ra[0], (unsigned long)ra[1],
            (unsigned long)ra[2], (unsigned long)rb[0], (unsigned long)dt);
    row("M maple devinfo A0 B0", ok);
    R32(MDEN) = 0;
  }
  holly_route(0, 0);

  /* ---- S: TAS.B through the bus ---- */
  {
    const uint32_t u = P2(&tas_b[0]), c = (uint32_t)(uintptr_t)&tas_b[32];
    R8(u) = 0;
    const int t1 = tas(u), v1 = R8(u), t2 = tas(u), v2 = R8(u);
    R8(P2(c)) = 0x55;                                      /* memory 0x55 ... */
    (void)*(volatile uint8_t *)c;                          /* ... the line in the operand cache */
    *(volatile uint8_t *)c = 0;                            /* 0 in the cache only (copy-back) */
    const int t3 = tas(c), m3 = R8(P2(c)), c3 = *(volatile uint8_t *)c;
    sprintf(g_b, "p2: T=%d %02x T=%d %02x p1: T=%d mem=%02x cache=%02x", t1, v1, t2, v2, t3, m3, c3);
    row("S tas.b", t1 == 1 && v1 == 0x80 && t2 == 0 && v2 == 0x80 && t3 == 1 && m3 == 0x80 && c3 == 0x80);
  }

  tmu_reset();
  printf("BX done fails=%d\n", fails);
  return 0;
}
