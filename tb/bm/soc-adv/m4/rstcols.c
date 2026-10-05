/* M4-fix (WI-SOC): M4-verify's program (agents/reports/integration/M4-verify.md), the M4 gate's regression test
 * since M4-fix (tb/mk/v1_soc.mk v1-soc-m4-sub; the verdict: m4/check.py --progs). */
/* rstcols.c -- M4 verification: SOC.md 2.3 rows that tb/bm/soc-adv/m4/wdtrst does not check, across a WDT manual
 * reset, a WDT power-on reset and (TRANSLATE = 1) an associative-write multiple hit, each through the boot stub
 * (soc_tb --resets 3; run without --ccr: CCR stays 0 after a reset, and m4v_early reads the arrays before crt0's CCR
 * write invalidates the caches).  The rows (SOC.md 2.3 columns pin POR / WDT POR / manual):
 *   IC and OC V, U                       clear / clear / held
 *   IC / OC tags and data                held / held / held
 *   TLB V bits / TLB contents            clear / held (POR) -- held / held (manual)
 *   CCR                                  0 / 0
 *   FRQCR                                from MD / held / held
 *   PMCR / PMCTR                         0 [P] / 0 [P] / held [P]
 *   BSC registers (RFCR: not rewritten by the stub)   init / init / held, refresh continues
 *   PDTRA, PDTRB                         never reset
 *   EXPEVT                               000 / 000 / 020 (0x140 multiple hit)
 * Boot 1 gives each a distinctive state (5 OC lines: 3 dirty, 2 clean; the IC lines of a routine that spins until the
 * reset (its tag and V = 1 expected); UTLB 60-63 and ITLB 2-3 through the arrays; FRQCR CKOEN 0 (no relock: v1_cpg); PMCR2 counting cycles;
 * RFCR 0x200; PCTRB outputs with PDTRB 0xA) and fires a WDT manual reset; boot 2 checks and repeats with other values
 * and a WDT power-on reset; boot 3 checks, then (UTLB present) makes two UTLB entries match one VPN / ASID and
 * writes the UTLB address array associatively (mmu.md 2.2: a multiple hit resets, EXPEVT 0x140); boot 4 checks.
 * Output: "RCOL boot=<n> after=<manual|por|mhit> <row> set=<hex> got=<hex> OK|FAIL"; "RCOL done fails=<n>". */
#include "m4v.h"

#define MAGIC 0x52434F4Cu                                  /* "RCOL" */
#define NOC 5
#define OCL(k) (0x8C7A3800u + 32u * (uint32_t)(k))         /* OC index 0x1C0 + k: past the image and .bss (0x3454), below the stack */
#define OCI(k) (((OCL(k)) >> 5) & 511u)

/* PER words: 0 magic, 1 phase, 2 fails, 3 has_tlb; 8..: the set values */
#define S_OCA   16                                          /* [NOC] */
#define S_OCD   24                                          /* [NOC][8] */
#define S_ICN   64
#define S_ICI   65                                          /* [8] */
#define S_ICA   73                                          /* [8] */
#define S_UA    81                                          /* [4] UTLB 60-63 address */
#define S_UD    85                                          /* [4] data */
#define S_IA    89                                          /* [2] ITLB 2-3 */
#define S_ID    91
#define S_PMC2  93
#define S_PMC1  94
#define S_RFCR  95
#define S_PDTRB 96
#define S_FRQ   97
#define S_MHA   98                                          /* [2] UTLB 10-11 before the multiple hit */
#define S_MHD   100

static void h_any(irq_t code, irq_context_t *ctx, void *d) {
  (void)d;
  printf("RCOL unexpected code %03lx spc %08lx\n", (unsigned long)code, (unsigned long)ctx->pc);
  R32(SIM_EXIT) = 0x3FE;
  for (;;) {}
}

/* the routine whose IC line is watched: it spins until the reset (one 32-byte line, aligned) */
__attribute__((noinline, noreturn, aligned(32))) void icfn(void) {
  for (;;) __asm__ __volatile__("nop");
}
static uint32_t ic_want(void) { return ((uint32_t)(uintptr_t)icfn & 0x1FFFFC00u) | 1u; }   /* tag PA[28:10], V */

static int fails;
static const char *after_name(uint32_t ph) { return ph == 1 ? "manual" : ph == 2 ? "por" : "mhit"; }
static void row(uint32_t ph, const char *name, uint32_t set, uint32_t got, int ok) {
  printf("RCOL boot=%lu after=%s %s set=%08lx got=%08lx %s\n", (unsigned long)CAP->boots, after_name(ph), name,
         (unsigned long)set, (unsigned long)got, ok ? "OK" : "FAIL");
  if (!ok) fails++;
}

static void utlb_w(int e, uint32_t a, uint32_t d) { R32(UTLB_A(e)) = a; R32(UTLB_D(e)) = d; }

static void setup(uint32_t ph) {
  volatile uint32_t *P = PER;
  /* OC: memory patterns through P2, the lines invalidated, then 3 dirty (store misses) and 2 clean (load misses) */
  for (int k = 0; k < NOC; k++) {
    for (int w = 0; w < 8; w++) R32(P2A(OCL(k)) + 4u * (uint32_t)w) = 0x3C000000u | (ph << 16) | ((uint32_t)k << 8) | (uint32_t)w;
    __asm__ __volatile__("ocbi @%0" : : "r"(OCL(k)) : "memory");
  }
  for (int k = 0; k < 3; k++)
    for (int w = 0; w < 8; w += 2) R32(OCL(k) + 4u * (uint32_t)w) = 0xD1000000u | (ph << 16) | ((uint32_t)k << 8) | (uint32_t)w;
  for (int k = 3; k < NOC; k++) (void)R32(OCL(k) + 4);
  /* UTLB 60-63, ITLB 2-3 (TRANSLATE = 0: the arrays read 0 and ignore writes) */
  for (int k = 0; k < 4; k++)
    utlb_w(60 + k, (0x20000000u + 0x100000u * (uint32_t)k + 0x1000u * ph) | 0x100u | ((k & 1) ? 0 : 0x200u) | (0x30u + (uint32_t)k + ph),
           (0x0C800000u + 0x1000u * (uint32_t)k + 0x10000u * ph) | 0x100u | 0x10u | 0x60u | 0x8u | ((k & 1) ? 0 : 0x4u));
  for (int k = 0; k < 2; k++) {
    R32(ITLB_A(2 + k)) = (0x30000000u + 0x100000u * (uint32_t)k + 0x1000u * ph) | 0x100u | (0x40u + (uint32_t)k + ph);
    R32(ITLB_D(2 + k)) = (0x0C880000u + 0x1000u * (uint32_t)k + 0x10000u * ph) | 0x100u | 0x10u | 0x40u | 0x8u;
  }
  /* FRQCR CKOEN 0 (a PLL enable or divider change would relock: v1_cpg; CKOEN is not one) */
  R16(FRQCR) = 0x060A; (void)R16(FRQCR);
  /* PMCR2: elapsed cycles, cleared first (only a stopped counter clears) */
  R16(PMCR2) = 0x2000; R16(PMCR2) = 0xC023;
  /* RFCR (key 101001 in bits 15:10) */
  R16(RFCR) = (uint16_t)(0xA400u | (ph ? 0x280u : 0x200u));
  /* port B: PB19-16 outputs, the latch */
  R32(PCTRB) = 0x55; R16(PDTRB) = (uint16_t)(ph ? 0x5 : 0xA);
  /* the IC line: icfn's (it spins there when the reset comes) */
  P[S_ICN] = 1;
  P[S_ICI] = (((uint32_t)(uintptr_t)icfn) >> 5) & 255u;
  /* let PMCTR2 grow well past what a reset and the stub take (a cleared counter would read less) */
  { const uint32_t t = pmc(); while (pmc() - t < 300000u) ; }
  /* the set values, read back (a register that does not store a bit reads it back the same after a held reset) */
  for (int k = 0; k < NOC; k++) {
    P[S_OCA + k] = R32(OC_ADDR(OCI(k)));
    for (int w = 0; w < 8; w++) P[S_OCD + 8 * k + w] = R32(OC_DATA(OCI(k), w));
  }
  P[S_ICA] = ic_want();
  for (int k = 0; k < 4; k++) { P[S_UA + k] = R32(UTLB_A(60 + k)); P[S_UD + k] = R32(UTLB_D(60 + k)); }
  for (int k = 0; k < 2; k++) { P[S_IA + k] = R32(ITLB_A(2 + k)); P[S_ID + k] = R32(ITLB_D(2 + k)); }
  P[S_RFCR] = R16(RFCR); P[S_PDTRB] = R16(PDTRB); P[S_FRQ] = R16(FRQCR);
  WATCH->n_ocw = NOC; for (int k = 0; k < NOC; k++) WATCH->ocw_idx[k] = OCI(k);
  WATCH->n_icw = 1; WATCH->icw_idx[0] = P[S_ICI];
}

static __attribute__((noreturn)) void fire(uint32_t csr) {
  PER[S_PMC2] = R32(PMCTR2L); PER[S_PMC1] = pmc();
  (void)wdt_sync(csr);
  icfn();
}

/* the checks after a reset of kind ph (1 manual, 2 WDT POR, 3 multiple hit) */
static void check(uint32_t ph) {
  volatile uint32_t *P = PER;
  const volatile cap_t *c = CAP;
  const int por = ph == 2;
  const uint32_t want_ev = ph == 1 ? 0x020 : ph == 2 ? 0x000 : 0x140;
  row(ph, "EXPEVT", want_ev, c->expevt, c->expevt == want_ev);
  row(ph, "CCR", 0, c->ccr, c->ccr == 0);
  /* OC: V U clear on POR, held on manual; tags and data held */
  for (int k = 0; k < NOC; k++) {
    char n[24];
    const uint32_t set = P[S_OCA + k], got = c->oca[k], want = por ? (set & ~3u) : set;
    sprintf(n, "OC[%03lx].addr", (unsigned long)OCI(k));
    /* the set value itself: V, the line's tag, U on the three stored lines (else the row proves nothing) */
    const int sane = (set & 1) && (set >> 10) == ((OCL(k) & 0x1FFFFFFFu) >> 10) && ((set >> 1) & 1) == (k < 3);
    row(ph, n, set, got, sane && c->n_ocw == NOC && c->ocw_idx[k] == OCI(k) && got == want);
    int dok = 1; for (int w = 0; w < 8; w++) if (c->ocd[k][w] != P[S_OCD + 8 * k + w]) dok = 0;
    sprintf(n, "OC[%03lx].data", (unsigned long)OCI(k));
    row(ph, n, P[S_OCD + 8 * k], c->ocd[k][0], dok);
  }
  {
    char n[24];
    const uint32_t set = P[S_ICA], got = c->ica[0], want = por ? (set & ~1u) : set;
    sprintf(n, "IC[%02lx].addr", (unsigned long)P[S_ICI]);
    row(ph, n, set, got, c->n_icw == 1 && c->icw_idx[0] == P[S_ICI] && got == want);
  }
  /* UTLB / ITLB: V (address bit 8, data bit 8) clear on POR, the rest held; everything held on manual */
  for (int k = 0; k < 4; k++) {
    char n[24];
    sprintf(n, "UTLB[%d].addr", 60 + k);
    row(ph, n, P[S_UA + k], c->utlb_a[60 + k], c->utlb_a[60 + k] == (por ? (P[S_UA + k] & ~0x100u) : P[S_UA + k]));
    sprintf(n, "UTLB[%d].data", 60 + k);
    row(ph, n, P[S_UD + k], c->utlb_d[60 + k], c->utlb_d[60 + k] == (por ? (P[S_UD + k] & ~0x100u) : P[S_UD + k]));
  }
  for (int k = 0; k < 2; k++) {
    char n[24];
    sprintf(n, "ITLB[%d].addr", 2 + k);
    row(ph, n, P[S_IA + k], c->itlb_a[2 + k], c->itlb_a[2 + k] == (por ? (P[S_IA + k] & ~0x100u) : P[S_IA + k]));
    sprintf(n, "ITLB[%d].data", 2 + k);
    row(ph, n, P[S_ID + k], c->itlb_d[2 + k], c->itlb_d[2 + k] == (por ? (P[S_ID + k] & ~0x100u) : P[S_ID + k]));
  }
  row(ph, "FRQCR", P[S_FRQ], c->frqcr, c->frqcr == P[S_FRQ]);
  /* PMCR2 held / 0; PMCTR2 held and counting (not below its value before the reset) / 0 */
  row(ph, "PMCR2", 0xC023, c->pmcr2, por ? c->pmcr2 == 0 : c->pmcr2 == 0xC023);
  row(ph, "PMCTR2", P[S_PMC2], c->pmctr2l, por ? (c->pmctr2l == 0 && c->pmctr2h == 0) : (c->pmctr2l >= P[S_PMC2]));
  /* PMCTR1: PMCR1 is rewritten by the stub (0xC023, no PMCLR): manual held and counting, POR from 0 */
  row(ph, "PMCTR1", P[S_PMC1], c->pmctr1l, por ? c->pmctr1l < P[S_PMC1] : c->pmctr1l >= P[S_PMC1]);
  /* RFCR: held and counting refreshes (one per 3040 Icyc) on manual, from 0 on POR */
  row(ph, "RFCR", P[S_RFCR], c->rfcr,
      por ? c->rfcr < 0x40 : (c->rfcr >= P[S_RFCR] && c->rfcr < P[S_RFCR] + 0x40));
  /* PDTRB: never reset (the stub's PCTRB = 0 makes it read the pins: outputs again, then the latch) */
  R32(PCTRB) = 0x55;
  const uint32_t pb = R16(PDTRB) & 0xF;
  row(ph, "PDTRB", P[S_PDTRB] & 0xF, pb, pb == (P[S_PDTRB] & 0xF));
  if (ph == 3) {
    row(ph, "UTLB[10].addr", P[S_MHA], c->utlb_a[10], c->utlb_a[10] == P[S_MHA]);
    row(ph, "UTLB[11].addr", P[S_MHA + 1], c->utlb_a[11], c->utlb_a[11] == P[S_MHA + 1]);
    printf("RCOL boot=%lu after=mhit TEA=%08lx PTEH=%08lx (info)\n", (unsigned long)c->boots, (unsigned long)c->tea,
           (unsigned long)c->pteh);
  }
}

int main(void) {
  bm_default = h_any;
  imask(15);
  volatile uint32_t *P = PER;
  if (CAP->boots == 1 || P[0] != MAGIC) {
    P[0] = MAGIC; P[1] = 1; P[2] = 0;
    R32(UTLB_A(63)) = 0x7FFFF100u; P[3] = R32(UTLB_A(63)) != 0;   /* a UTLB (TRANSLATE = 1) answers */
    printf("RCOL start tlb=%lu\n", (unsigned long)P[3]);
    setup(0);
    fire(W_WTIT | W_RSTS);
  }
  const uint32_t ph = P[1];
  fails = (int)P[2];
  check(ph);
  P[2] = (uint32_t)fails;
  if (ph == 1) { P[1] = 2; setup(1); fire(W_WTIT); }
  if (ph == 2 && P[3]) {
    /* two UTLB entries for one VPN / ASID, then an associative write of that VPN: a multiple hit */
    P[1] = 3;
    setup(2);
    R32(PTEH) = 0x21;
    utlb_w(10, 0x10400000u | 0x100u | 0x21u, 0x0C900000u | 0x100u | 0x10u | 0x60u | 0x8u);
    utlb_w(11, 0x10400000u | 0x100u | 0x21u, 0x0C901000u | 0x100u | 0x10u | 0x60u | 0x8u);
    P[S_MHA] = R32(UTLB_A(10)); P[S_MHA + 1] = R32(UTLB_A(11));
    P[S_PMC2] = R32(PMCTR2L); P[S_PMC1] = pmc();
    R32(UTLB_A(0) | 0x80u) = 0x10400000u | 0x100u;           /* A = 1: the associative write */
    for (volatile int i = 0; i < 2000; i++) ;
    printf("RCOL no multiple-hit reset\n");
    fails++;
  }
  /* the end: the TLB entries invalid again, FRQCR / port B back */
  for (int k = 0; k < 64; k++) R32(UTLB_A(k)) = 0;
  for (int k = 0; k < 4; k++) R32(ITLB_A(k)) = 0;
  R16(FRQCR) = 0x0E0A; R32(PCTRB) = 0; R16(PMCR2) = 0;
  printf("RCOL done fails=%d\n", fails);
  P[0] = 0;
  return 0;
}
