/* M4-fix (WI-SOC): M4-verify's program (agents/reports/integration/M4-verify.md), the M4 gate's regression test
 * since M4-fix (tb/mk/v1_soc.mk v1-soc-m4-sub; the verdict: m4/check.py --progs). */
/* sleeprst.c -- M4 verification: WDT resets that land while the core sleeps or enters / leaves a sleep, through the
 * boot stub (soc_tb --resets N; im and rm).  SOC.md 2.5: v1_cpg's power-down machine "any --reset--> RUN"; the clock-
 * stop handshake (ENTRY: the CPU clock runs 9 pce after the request, 33-36 Icyc); a wake's RESYNC (4 pce); SOC.md
 * 2.3: STBCR2 held on a manual reset, 0 on a power-on reset; WTCSR held with WOVF set.
 * Boot 1 calibrates the reset time after wdt_sync (a timestamp loop, as rstfly).  Each later boot checks the previous
 * scenario's reset (EXPEVT, WTCSR, STBCR2), then that the chip works after it: PMCTR1 advances over a spin (the CPU
 * clock runs), and a normal SLEEP is woken by a TMU0 underflow whose handler runs once; then it starts its own:
 *   slp / dslp       SLEEP / deep sleep (STBCR2.DSLP) entered 2000 Icyc before a WDT manual reset
 *   ent / dent       SLEEP / deep sleep issued 0 .. 64 Icyc before the reset: the reset meets the clock-stop handshake
 *   wake             SLEEP early, a TMU0 underflow timed to wake the core -16 .. 400 Icyc before the reset: the reset
 *                    meets the wake's resync, the handler or the code after it
 *   pslp / pdslp     a WDT power-on reset while in SLEEP / deep sleep
 * Output: "RSLP j=<n> kind=<k> lead=<l> <result> OK|FAIL <why>", "RSLP done scenarios=<n> fails=<f>". */
#include "m4v.h"

#define MAGIC 0x52534C50u                                  /* "RSLP" */
enum { K_CAL = 0, K_SLP, K_DSLP, K_ENT, K_DENT, K_WAKE, K_PSLP, K_PDSLP, K_N };
static const char *const KN[K_N] = {"cal", "slp", "dslp", "ent", "dent", "wake", "pslp", "pdslp"};
static const int SW[K_N][3] = {{0, 0, 1}, {2000, 2000, 1}, {2000, 2000, 1}, {0, 64, 2}, {0, 64, 4}, {-16, 400, 8},
                               {2000, 2000, 1}, {2000, 2000, 1}};
#define P_MAGIC 0
#define P_J     1
#define P_TREF  2
#define P_TS    3
#define P_START 4
#define P_KIND  5
#define P_LEAD  6
#define P_FAILS 7
#define P_NSC   8

static volatile int g_tmu;
static void h_tmu0(irq_t code, irq_context_t *ctx, void *d) {
  (void)code; (void)ctx; (void)d;
  R8(TSTR) = 0;                                            /* stop first (TCOR0 = ~0: no second underflow) */
  R16(TCR(0)) = (uint16_t)(R16(TCR(0)) & ~UNF);
  (void)R16(TCR(0));
  g_tmu++;
}
static void h_any(irq_t code, irq_context_t *ctx, void *d) {
  (void)d;
  printf("RSLP unexpected code %03lx spc %08lx\n", (unsigned long)code, (unsigned long)ctx->pc);
  R32(SIM_EXIT) = 0x3FE;
  for (;;) {}
}

static int nsc(void) { int n = 0; for (int k = 1; k < K_N; k++) n += (SW[k][1] - SW[k][0]) / SW[k][2] + 1; return n; }
static void sc_of(int j, int *kind, int *lead) {           /* j = 0: the calibration */
  int i = j - 1;
  if (j <= 0) { *kind = K_CAL; *lead = 0; return; }
  for (int k = 1; k < K_N; k++) {
    const int n = (SW[k][1] - SW[k][0]) / SW[k][2] + 1;
    if (i < n) { *kind = k; *lead = SW[k][1] - i * SW[k][2]; return; }
    i -= n;
  }
  *kind = K_CAL; *lead = 0;
}
static __attribute__((noreturn)) void ts_loop(uint32_t t0) { for (;;) PER[P_TS] = pmc() - t0; }
static void sleep1(void) { __asm__ __volatile__("sleep" : : : "memory"); }

/* the chip after a reset: the CPU clock runs, a SLEEP is woken by TMU0 once */
static int alive(char *why) {
  int ok = 1;
  const uint32_t a = pmc();
  for (volatile int i = 0; i < 200; i++) ;
  const uint32_t b = pmc();
  if (b - a < 200 || b - a > 50000) { ok = 0; sprintf(why + strlen(why), " PMCTR1 advanced %lu over a spin", (unsigned long)(b - a)); }
  R8(STBCR2) = 0; (void)R8(STBCR2);
  tmu_reset();
  g_tmu = 0;
  R16(IPRA) = 0x5000;                                      /* TMU0 level 5 */
  R32(TCOR(0)) = 0xFFFFFFFFu; R32(TCNT(0)) = 200; R16(TCR(0)) = UNIE; /* P/4: 16 Icyc per count */
  (void)R16(TCR(0));
  R8(TSTR) = 1;
  imask(0);
  sleep1();
  imask(15);
  if (g_tmu != 1) { ok = 0; sprintf(why + strlen(why), " TMU0 wake-ups %d, want 1", g_tmu); }
  R16(IPRA) = 0; tmu_reset();
  return ok;
}

static int check_prev(int j, int kind, int lead) {
  char why[200]; why[0] = 0;
  int ok = 1;
  const int por = kind == K_PSLP || kind == K_PDSLP;
  const int dslp = kind == K_DSLP || kind == K_DENT || kind == K_PDSLP;
  const uint32_t wev = por ? 0x000 : 0x020, wws = por ? 0xD7 : 0xF7, wsb = (!por && dslp) ? 0x80 : 0x00;
  if (CAP->expevt != wev) { ok = 0; sprintf(why + strlen(why), " EXPEVT %03lx, want %03lx", (unsigned long)CAP->expevt, (unsigned long)wev); }
  if (CAP->wtcsr != wws) { ok = 0; sprintf(why + strlen(why), " WTCSR %02lx, want %02lx", (unsigned long)CAP->wtcsr, (unsigned long)wws); }
  if (CAP->stbcr2 != wsb) { ok = 0; sprintf(why + strlen(why), " STBCR2 %02lx, want %02lx", (unsigned long)CAP->stbcr2, (unsigned long)wsb); }
  if (!alive(why)) ok = 0;
  const uint32_t st = PER[P_START], ts = PER[P_TS];
  printf("RSLP j=%d kind=%s lead=%d start=%ld ts=%lu tmu=%lu %s%s\n", j, KN[kind], lead, st == ~0u ? -1L : (long)st,
         (unsigned long)ts, (unsigned long)PER[9], ok ? "OK" : "FAIL", why);
  return ok;
}

static __attribute__((noreturn)) void run(int j) {
  int kind, lead;
  sc_of(j, &kind, &lead);
  PER[P_J] = (uint32_t)j; PER[P_KIND] = (uint32_t)kind; PER[P_LEAD] = (uint32_t)lead; PER[P_START] = ~0u; PER[P_TS] = 0;
  PER[9] = 0;
  const int por = kind == K_PSLP || kind == K_PDSLP;
  const int dslp = kind == K_DSLP || kind == K_DENT || kind == K_PDSLP;
  R8(STBCR2) = dslp ? 0x80 : 0x00; (void)R8(STBCR2);
  const uint32_t tgt = PER[P_TREF] - (uint32_t)lead;
  uint32_t t0, t;
  if (kind == K_CAL) { t0 = wdt_sync(W_WTIT | W_RSTS); PER[P_START] = 0; ts_loop(t0); }
  if (kind == K_WAKE) {
    tmu_reset();
    g_tmu = 0;
    R16(IPRA) = 0x5000;
    R32(TCOR(0)) = 0xFFFFFFFFu;
    R16(TCR(0)) = UNIE;
    t0 = wdt_sync(W_WTIT | W_RSTS);
    t = pmc() - t0;
    uint32_t n = (tgt - t - 60) / 16;                      /* the underflow ~ (n + 1) * 16 after the start */
    R32(TCNT(0)) = n;
    R8(TSTR) = 1;
    PER[9] = pmc() - t0 + (n + 1) * 16;                    /* the underflow's expected time (report) */
    PER[P_START] = pmc() - t0;
    imask(0);
    sleep1();
    ts_loop(t0);
  }
  t0 = wdt_sync(por ? W_WTIT : (W_WTIT | W_RSTS));
  while ((t = pmc() - t0) < tgt) ;
  sleep1();
  PER[P_START] = t;                                        /* reached only when the reset came before the sleep */
  for (;;) sleep1();
}

int main(void) {
  bm_default = h_any;
  irq_set_handler(0x400, h_tmu0, 0);
  imask(15);
  if (CAP->boots == 1 || PER[P_MAGIC] != MAGIC) {
    PER[P_MAGIC] = MAGIC; PER[P_FAILS] = 0; PER[P_NSC] = (uint32_t)nsc();
    printf("RSLP start scenarios=%lu\n", (unsigned long)PER[P_NSC]);
    run(0);
  }
  const int j = (int)PER[P_J];
  if (!check_prev(j, (int)PER[P_KIND], (int)PER[P_LEAD])) PER[P_FAILS] = PER[P_FAILS] + 1;
  if (j == 0) PER[P_TREF] = PER[P_TS];
  if (j < (int)PER[P_NSC]) run(j + 1);
  printf("RSLP done scenarios=%lu fails=%lu\n", (unsigned long)PER[P_NSC], (unsigned long)PER[P_FAILS]);
  PER[P_MAGIC] = 0;
  return 0;
}
