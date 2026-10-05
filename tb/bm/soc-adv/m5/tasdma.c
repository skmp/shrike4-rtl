/* tasdma.c -- M5 (the M5 verification's program, agents/reports/integration/M5-verify.md F3; the M5 gate's regression
 * test since M5-fix.md): TAS.B's locked read-modify-write against DMAC transfers that write the same byte (SOC.md 8.3
 * M5 B2 "TAS.B through the bus"; pm TAS.B: "Another memory access is not executed between the two TAS.B accesses";
 * SOC.md 3.3 / v1_bsc_arb: a locked read makes the pair, only its port's write is accepted next).  Run on rm (and rmx;
 * never im, SOC.md 1.1: the ideal memory has no TAS lock against the DMAC) with soc_tb --lockstep --bus-trace: the
 * lockstep, and m5/tasbus.py's structural check of the trace (every bus START that reads the TAS byte is followed, as
 * the very next START, by the write of that byte): make v1-soc-m5-sub.  Mutant lock_dma (the DMAC granted inside the
 * pair: v1-soc-mutants) fails the lockstep here and nowhere else.
 * Built with the m1b run-time (tb/bm/serial crt0 / bmrt with BM_M1B_RT: CCR 0x090D, IMASK 15 at main).
 *
 * Each case: DMAC channel 1 (and 2) in auto request (RS 4), fixed source / fixed destination, writes the source byte(s)
 * onto the target T while the CPU runs TAS.B on T in a loop until the channel's TE:
 *   A  cycle steal, 1-byte units of 0x00, TAS on T through P2
 *   B  the same, TAS through P1 (copy-back: TAS purges the line first)
 *   C  burst mode (TM 1), 1-byte units, TAS through P2
 *   D  cycle steal, 32-byte units of zeros onto T's whole line, TAS through P2
 *   E  cycle steal, channels 1 (0x00) and 2 (0x01) onto T at once, TAS through P2
 *   F  cycle steal, 1-byte units of 0x00, TAS through P1 while the same line is also dirtied by stores between the TASes
 *      (TAS's purge writes the dirty line back first: the DMA's byte must not be overwritten by a stale write-back
 *      except where the program's own store put it)
 *   G  as B (P1) and H as A (P2), with TMU0 interrupting every 32 counts (512 Icyc) at IMASK 0: interrupts are accepted
 *      only between instructions, never between TAS.B's purge / read / write (the lockstep checks every take's SPC and
 *      the T each TAS leaves); the handler clears UNF and counts its entries (must be > 0)
 * The program judges: every TAS completes; T and the neighbour bytes hold only values the sources and TAS can make;
 * the channel ends with TE, DMATCR 0, SAR / DAR unchanged (fixed); the TAS count of T = 1 (a DMA write seen) is at
 * most the number of DMA units.  Atomicity itself is the bus trace's (m5v_bus.py) and the lockstep's (a DMA write
 * lost between TAS's read and write leaves SR.T different from the model's, which applies the DMAW record at the
 * RTL's time).
 * Output: "TD <case> units=<n> tas=<n> t1=<n> final=<b> nb=<ok|BAD> chcr=<..> tcr=<n> sar=<ok|BAD> dar=<ok|BAD> OK|FAIL",
 * "TD done fails=<n>". */
#include "m1b.h"

#define CH_DM(x) ((uint32_t)(x) << 14)
#define CH_SM(x) ((uint32_t)(x) << 12)
#define CH_RS(x) ((uint32_t)(x) << 8)
#define CH_TM    (1u << 7)
#define CH_TS(x) ((uint32_t)(x) << 4)
#define CH_TE    (1u << 1)
#define CH_DE    1u
#define PHYS(p)  ((uint32_t)(uintptr_t)(p) & 0x1FFFFFFFu)
#define P2(p)    (PHYS(p) | 0xA0000000u)
#define P1(p)    (PHYS(p) | 0x80000000u)

static volatile uint8_t tgt[64] __attribute__((aligned(32)));      /* T = tgt[0]; tgt[1..31] its line's neighbours */
static volatile uint8_t src0[32] __attribute__((aligned(32)));     /* zeros */
static volatile uint8_t src1[32] __attribute__((aligned(32)));     /* 0x01 */
static int fails;
static volatile uint32_t g_ntmu;
static void h_tmu(irq_t code, irq_context_t *ctx, void *d) {
  (void)code; (void)ctx; (void)d;
  R16(TCR(0)) = (uint16_t)(R16(TCR(0)) & ~UNF);
  (void)R16(TCR(0));
  g_ntmu++;
}

static int tas(uint32_t a) {
  int t;
  __asm__ __volatile__("tas.b @%1\n\tmovt %0" : "=r"(t) : "r"(a) : "t", "memory");
  return t;
}

static void ch_start(int ch, uint32_t sar, uint32_t dar, uint32_t n, uint32_t ts, int tm) {
  (void)R32(CHCR(ch));
  R32(CHCR(ch)) = 0;
  R32(SAR(ch)) = sar; R32(DAR(ch)) = dar; R32(DMATCR(ch)) = n;
  R32(CHCR(ch)) = CH_DM(0) | CH_SM(0) | CH_RS(4) | (tm ? CH_TM : 0) | CH_TS(ts) | CH_DE;
}

static void run(char name, int p1, int tm, int ts32, int two, int dirty, int irq) {
  const uint32_t n = ts32 ? 64 : 600;
  ocbp_range(tgt, 64); ocbp_range(src0, 32); ocbp_range(src1, 32);   /* crt0 cleared the BSS through P1: dirty lines */
  for (int i = 0; i < 32; i++) { R8(P2(&src0[i])) = 0; R8(P2(&src1[i])) = 1; }
  for (int i = 0; i < 64; i++) R8(P2(&tgt[i])) = (uint8_t)(0x40 + i);
  R8(P2(&tgt[0])) = 0x80;
  ocbp_range(tgt, 64);                                      /* no stale copy of T's line in the operand cache */
  const uint32_t ta = p1 ? P1(&tgt[0]) : P2(&tgt[0]);
  const uint32_t dar = ts32 ? PHYS(&tgt[0]) : PHYS(&tgt[0]);
  ch_start(1, PHYS(ts32 ? &src0[0] : &src0[0]), dar, n, ts32 ? 4 : 1, tm);
  if (two) ch_start(2, PHYS(&src1[0]), dar, n, 1, tm);
  uint32_t ntas = 0, nt1 = 0, t0 = pmc(), timeout = 0;
  g_ntmu = 0;
  if (irq) {                                                /* TMU0 every 32 counts (512 Icyc), level 5, IMASK 0 */
    R8(TSTR) = 0; R16(IPRA) = (uint16_t)(5u << 12);
    R32(TCOR(0)) = 31; R32(TCNT(0)) = 31; R16(TCR(0)) = UNIE; (void)R16(TCR(0));
    R8(TSTR) = 1; imask(0);
  }
  for (;;) {
    const int busy = !(R32(CHCR(1)) & CH_TE) || (two && !(R32(CHCR(2)) & CH_TE));
    if (!busy) break;
    if (dirty && p1) *(volatile uint8_t *)P1(&tgt[4]) = (uint8_t)(0xA0 + (ntas & 15));   /* dirty the line */
    nt1 += (uint32_t)tas(ta);
    ntas++;
    if (pmc() - t0 > 40000000u) { timeout = 1; break; }
  }
  if (irq) { imask(15); R8(TSTR) = 0; R16(TCR(0)) = 0; (void)R16(TCR(0)); if (g_ntmu == 0) timeout = 1; }
  /* the end state */
  const uint32_t c1 = R32(CHCR(1)), tcr = R32(DMATCR(1)), s1 = R32(SAR(1)), d1 = R32(DAR(1));
  int ok = !timeout && (c1 & CH_TE) && tcr == 0 && s1 == PHYS(&src0[0]) && d1 == dar;
  if (two) ok = ok && (R32(CHCR(2)) & CH_TE) && R32(DMATCR(2)) == 0;
  /* word reads: in the bus trace the only byte reads of T are TAS.B's (m5v_bus.py) */
  const uint8_t fin = (uint8_t)R32(P2(&tgt[0]));
  const uint8_t fin_p1 = (uint8_t)*(volatile uint32_t *)P1(&tgt[0]);  /* no stale cache copy after TAS's purge */
  int nb = 1;
  for (int i = 1; i < 64; i++) {
    uint8_t v = R8(P2(&tgt[i])), want = (uint8_t)(0x40 + i);
    if (ts32 && i < 32) want = 0;                           /* the 32-byte units zero the line */
    if (dirty && i == 4) { if ((v & 0xF0) != 0xA0) nb = 0; continue; }
    if (v != want) nb = 0;
  }
  if (!nb) ok = 0;
  if (two) { if (fin != 0 && fin != 1 && fin != 0x80 && fin != 0x81) ok = 0; }
  else if (fin != 0 && fin != 0x80) ok = 0;
  if (fin_p1 != fin) ok = 0;
  if (!two && nt1 > n) ok = 0;
  if (ntas == 0) ok = 0;
  if (irq) printf("TD %c tmu-takes=%lu\n", name, (unsigned long)g_ntmu);
  printf("TD %c units=%lu tas=%lu t1=%lu final=%02x/%02x nb=%s chcr=%08lx tcr=%lu sar=%s dar=%s %s\n", name,
         (unsigned long)n, (unsigned long)ntas, (unsigned long)nt1, fin, fin_p1, nb ? "ok" : "BAD", (unsigned long)c1,
         (unsigned long)tcr, s1 == PHYS(&src0[0]) ? "ok" : "BAD", d1 == dar ? "ok" : "BAD", ok ? "OK" : "FAIL");
  if (!ok) fails++;
  (void)R32(CHCR(1)); R32(CHCR(1)) = 0;
  if (two) { (void)R32(CHCR(2)); R32(CHCR(2)) = 0; }
}

int main(void) {
  imask(15);
  printf("TD start dmaor=%08lx tgt=%08lx\n", (unsigned long)R32(DMAOR), (unsigned long)PHYS(&tgt[0]));
  run('A', 0, 0, 0, 0, 0, 0);
  run('B', 1, 0, 0, 0, 0, 0);
  run('C', 0, 1, 0, 0, 0, 0);
  run('D', 0, 0, 1, 0, 0, 0);
  run('E', 0, 0, 0, 1, 0, 0);
  run('F', 1, 0, 0, 0, 1, 0);
  irq_set_handler(0x400, h_tmu, 0);
  run('G', 1, 0, 0, 0, 0, 1);
  run('H', 0, 0, 0, 0, 0, 1);
  printf("TD done fails=%d\n", fails);
  return fails ? 1 : 0;
}
