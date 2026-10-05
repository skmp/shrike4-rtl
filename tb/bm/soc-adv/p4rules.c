/* p4rules.c -- M1a adversarial test: P4 access rules through the chip (SOC.md 3.1.2 - 3.1.6).
 *   W  wrong-size writes to the M1a units' registers (Iclk CCN / PMC / UBC, Pclk CPG / INTC / TMU, Bclk BSC): ignored
 *      (SOC.md 3.1.4: "any other size reads 0 (with hit = 1) and ignores the write" [HW]); read back at the right size
 *   N  writes to P4B_NONE / P4B_RSV / the arrays (IDEAL_MEM = 1: null slaves; TRANSLATE = 0: the TLB arrays read 0):
 *      ignored, reads 0
 *   Q  quadword FMOVs: two longword transactions a (FR(2n)) then a + 4 (SOC.md 3.1.4 [P]): TMU TCOR0 / TCNT0 (Pclk),
 *      PTEH / PTEL and TRA / EXPEVT (Iclk), DMAC SAR0 / DAR0 (Bclk), ICR / IPRA (16-bit registers: both halves the
 *      wrong size: ignored, read 0)
 *   A  misaligned P4 accesses: a data address error (EXPEVT 0x0E0 / 0x100, TEA), no bus access
 *   L  the undriven UBC window returns last_rdata (debug-ubc-hudi 2.2.1 [P]: the right-aligned data of the latest
 *      operand read; the lockstep checker takes UBC_NODRV reads as volatile, so only this test checks the rule)
 * Output lines "P4R <group> <name> <values>"; tb/bm/soc-adv/check.py holds the expected values. */
#include "adv.h"

extern void qwr(uint32_t, uint32_t, uint32_t), qrd(uint32_t, uint32_t *);
extern uint32_t mis_l(uint32_t), mis_w(uint32_t);
extern void mis_sl(uint32_t);
extern char mis_ret[];
extern void nodrv_seq(uint32_t ram, uint32_t *out);

#define NODRV 0xFF200100u
static volatile uint32_t ram[4] = {0xDEADBEEFu, 0x00008001u, 0x000000F0u, 0x12345678u};

static void w8(uint32_t a, uint8_t v) { R8(a) = v; }
static void w16(uint32_t a, uint16_t v) { R16(a) = v; }
static void w32(uint32_t a, uint32_t v) { R32(a) = v; }

/* the wrong sizes of a register of `size` bytes: every other size at every aligned offset inside it */
static void wrong(uint32_t a, int size, uint32_t pat) {
  if (size != 1) { for (int o = 0; o < size; o++) w8(a + o, (uint8_t)(pat >> (8 * o))); }
  if (size != 2) { if (size == 4) { w16(a, (uint16_t)pat); w16(a + 2, (uint16_t)(pat >> 16)); } else w16(a, (uint16_t)pat); }
  if (size != 4) w32(a, pat);
}
static uint32_t rd(uint32_t a, int size) { return size == 1 ? R8(a) : size == 2 ? R16(a) : R32(a); }

static void W(const char *name, uint32_t a, int size, uint32_t init, uint32_t pat) {
  if (init != 0xFFFFFFFFu) { if (size == 1) w8(a, (uint8_t)init); else if (size == 2) w16(a, (uint16_t)init); else w32(a, init); }
  const uint32_t before = rd(a, size);
  wrong(a, size, pat);
  const uint32_t after = rd(a, size);
  printf("P4R W %s before=%lx after=%lx\n", name, (unsigned long)before, (unsigned long)after);
}

static void N(const char *name, uint32_t a) {
  w32(a, 0x5A5AA5A5u); w16(a, 0x1234); w8(a, 0x56);
  printf("P4R N %s r32=%lx r16=%x r8=%x\n", name, (unsigned long)R32(a), R16(a), R8(a));
}

int main(void) {
  printf("p4rules: start\n");
  set_sr(0x400000F0u);
  tmu_reset();
  /* W: (init 0xFFFFFFFF = leave the register as it is) */
  W("IPRA", IPRA, 2, 0x0000, 0x5AA5F00F);
  W("IPRC", IPRC, 2, 0x0000, 0x5AA5F00F);
  W("ICR", ICR, 2, 0x0000, 0x00FF0380);
  W("TCOR0", TCOR0, 4, 0x11223344, 0xA5A5A5A5);
  W("TCNT1", TCNT1, 4, 0x55667788, 0xA5A5A5A5);
  W("TCR0", TCR0, 2, 0x0002, 0xFFFF0005);
  W("TSTR", TSTR, 1, 0x00, 0x00070007);
  W("TOCR", TOCR, 1, 0x00, 0x00010001);
  W("STBCR", STBCR, 1, 0x03, 0x00010001);
  W("STBCR2", STBCR2, 1, 0x00, 0x00800080);
  W("FRQCR", FRQCR, 2, 0xFFFFFFFF, 0x0000060A);
  W("PTEH", PTEH, 4, 0x00000000, 0x12345C55);
  W("TTB", TTB, 4, 0x00000000, 0x8C0FFFF0);
  W("TRA", TRA, 4, 0x00000000, 0x000003FC);
  W("EXPEVT", EXPEVT, 4, 0xFFFFFFFF, 0x00000FE0);
  W("INTEVT", INTEVT, 4, 0x00000000, 0x00000A60);
  W("QACR0", QACR0, 4, 0x00000004, 0x00000010);
  W("MMUCR", MMUCR, 4, 0x00000000, 0x00000201);
  W("CCR", CCR, 4, 0xFFFFFFFF, 0x00000000);
  W("PMCR2", PMCR2, 2, 0x0000, 0x0000C023);
  W("BASRA", BASRA, 1, 0x00, 0x005A005A);
  W("BARA", BARA, 4, 0x00000000, 0x87654321);
  W("BBRA", BBRA, 2, 0x0000, 0x00000014);
  W("PCTRB", 0xFF800040u, 4, 0x00000000, 0x0000000F);
  W("GPIOIC", 0xFF800048u, 2, 0x0000, 0x0000FFFF);
  W("SAR1", 0xFFA00010u, 4, 0x0C001000, 0x0C0F0000);

  /* N */
  N("none_ff040000", 0xFF040000u);
  N("none_ff140000", 0xFF140000u);
  N("none_ff300000", 0xFF300000u);
  N("none_ffb00000", 0xFFB00000u);
  N("none_fff80000", 0xFFF80000u);
  N("none_ffc00020", 0xFFC00020u);                   /* inside the CPG block, no register (full decode, no mirror) */
  N("rsv_e4000000", 0xE4000000u);
  N("rsv_eff00000", 0xEFF00000u);
  N("rsv_f8000000", 0xF8000000u);
  N("rsv_fef00000", 0xFEF00000u);
  N("icaa", 0xF0000000u);
  N("icda", 0xF1000000u);
  N("itlbaa", 0xF2000000u);
  N("ocaa", 0xF4000000u);
  N("ocda", 0xF5000000u);
  N("utlbaa", 0xF6000000u);
  N("utlbda", 0xF7000000u);
  N("utlbaa_assoc", 0xF6000080u);

  /* Q */
  uint32_t q[2];
  R32(TCOR0) = 0; R32(TCNT0) = 0;
  qwr(TCOR0, 0xAAAA0001u, 0xBBBB0002u);
  qrd(TCOR0, q);
  printf("P4R Q tmu tcor0=%lx tcnt0=%lx q=%lx,%lx\n", (unsigned long)R32(TCOR0), (unsigned long)R32(TCNT0),
         (unsigned long)q[0], (unsigned long)q[1]);
  qwr(PTEH, 0x12345C55u, 0x0C0001FCu);
  qrd(PTEH, q);
  printf("P4R Q pteh=%lx ptel=%lx q=%lx,%lx\n", (unsigned long)R32(PTEH), (unsigned long)R32(PTEL), (unsigned long)q[0],
         (unsigned long)q[1]);
  qwr(TRA, 0x000003FCu, 0x00000FE0u);
  qrd(TRA, q);
  printf("P4R Q tra=%lx expevt=%lx q=%lx,%lx\n", (unsigned long)R32(TRA), (unsigned long)R32(EXPEVT), (unsigned long)q[0],
         (unsigned long)q[1]);
  qwr(0xFFA00000u, 0x0C0E0000u, 0x0C0E0100u);
  qrd(0xFFA00000u, q);
  printf("P4R Q sar0=%lx dar0=%lx q=%lx,%lx\n", (unsigned long)R32(0xFFA00000u), (unsigned long)R32(0xFFA00004u),
         (unsigned long)q[0], (unsigned long)q[1]);
  R16(ICR) = 0; R16(IPRA) = 0x1230;
  qwr(ICR, 0xFFFFFFFFu, 0xFFFFFFFFu);
  qrd(ICR, q);
  printf("P4R Q icr=%x ipra=%x q=%lx,%lx\n", R16(ICR), R16(IPRA), (unsigned long)q[0], (unsigned long)q[1]);
  R16(IPRA) = 0;

  /* A */
  g_resume = (uint32_t)mis_ret; g_resume_sr = 0x400000F0u;
  R32(TCOR0) = 0x0BADC0DEu;
  const uint32_t tests[4] = {TCOR0 + 2, IPRA + 1, 0xFF000022u, TCOR0 + 1};
  for (int i = 0; i < 4; i++) {
    clrlog();
    uint32_t v = i == 1 ? mis_w(tests[i]) : i == 3 ? (mis_sl(tests[i]), 0) : mis_l(tests[i]);
    printf("P4R A %08lx n=%lu expevt=%03lx tea=%08lx r0=%lx\n", (unsigned long)tests[i], (unsigned long)g_nlog,
           (unsigned long)(g_nlog ? g_log[0].expevt : 0), (unsigned long)(g_nlog ? g_log[0].tea : 0), (unsigned long)v);
  }
  printf("P4R A tcor0_after=%lx\n", (unsigned long)R32(TCOR0));
  g_resume = 0;

  /* L: the undriven UBC window (asm: no compiler literal load between the probed read and the window read) */
  uint32_t l[8];
  ram[0] = 0xDEADBEEFu;
  nodrv_seq((uint32_t)ram, l);
  printf("P4R L ld32=%lx ld16s=%lx ld8=%lx pclk=%lx afterstore=%lx r16=%lx pref=%lx sts=%lx\n", (unsigned long)l[0],
         (unsigned long)l[1], (unsigned long)l[2], (unsigned long)l[3], (unsigned long)l[4], (unsigned long)l[5],
         (unsigned long)l[6], (unsigned long)l[7]);
  ram[0] = 0xDEADBEEFu;
  printf("p4rules: done\n");
  return 0;
}
