/* pgx.c -- M2 verification: exceptions, SLEEP, interrupts and holds at a translated page's end while the fetch runs
 * ahead into the next page (an ITLB refill in flight, or a UTLB-miss poison), on imx (TRANSLATE = 1) in lockstep.
 * SOC.md 3.2.1 / 4.1 item 4: a redirect -- the exception entry's included -- waits for `busy` (the ITLB refill); a
 * poisoned speculative word vanishes.  Each case (pgx_asm.S) runs cold (ITLB emptied through its address array) and
 * warm (the same call again).  Expected values (m2/check.py): hm 5 (exception codes, SPC, TEA), mmu 3.5 (E4c: a delay
 * slot's TLB miss has SPC = the branch, TEA = the slot), mmu 3.4 / U (a user fetch from a PR[1] = 0 page: 0x0A0).
 * Output: "PGX <case> <cold|warm> r0=<value> n=<records> [<code> tea=<TEA - A> spc=<SPC - A>] ..." (A = page A's VA;
 * an interrupt shows INTEVT) and "PGX cross irq sum=<translated> ref=<P1> irqs=<n>". */
#include "m2.h"

#define VBASE 0x20800000u
#define PBASE 0x0C800000u
#define VA_PG(k) (VBASE + (uint32_t)(k) * 0x1000u)
#define PA_PG(k) (PBASE + (uint32_t)(k) * 0x1000u)

void *memcpy(void *, const void *, unsigned);
extern void pgx_ici(void);                                  /* pgx_asm.S: CCR.ICI from P2 */
extern uint32_t call_va(uint32_t va, uint32_t a, uint32_t b, uint32_t v);
extern uint32_t to_user(uint32_t entry, uint32_t arg);
extern char to_user_ret[], pgx_ret[];
#define T(x) extern char x##_a[], x##_a_e[], x##_b[], x##_b_e[];
extern char trapa_a[], trapa_a_e[], trapa_b[], trapa_b_e[], ill_a[], ill_a_e[], slp_a[], slp_a_e[], slp_b[], slp_b_e[];
extern char p4rd_a[], p4rd_a_e[], p4rd_b[], p4rd_b_e[], ldcsr_a[], ldcsr_a_e[], ldcsr_b[], ldcsr_b_e[];
extern char dmiss_a[], dmiss_a_e[], dmiss_b[], dmiss_b_e[], cross_a[], cross_a_e[], cross_b[], cross_b_e[];
extern char brau_a[], brau_a_e[], braslot_a[], braslot_a_e[];

typedef struct { const char *name; char *a, *ae, *b, *be; int bmap; } case_t;
enum { C_TRAPA, C_ILL, C_SLP, C_P4RD, C_LDCSR, C_DMISS, C_CROSS, C_BRAU, C_BRASLOT, NC };
static const case_t C[NC] = {
  {"trapa", trapa_a, trapa_a_e, trapa_b, trapa_b_e, 1}, {"ill", ill_a, ill_a_e, 0, 0, 0},
  {"slp", slp_a, slp_a_e, slp_b, slp_b_e, 1},           {"p4rd", p4rd_a, p4rd_a_e, p4rd_b, p4rd_b_e, 1},
  {"ldcsr", ldcsr_a, ldcsr_a_e, ldcsr_b, ldcsr_b_e, 1}, {"dmiss", dmiss_a, dmiss_a_e, dmiss_b, dmiss_b_e, 1},
  {"cross", cross_a, cross_a_e, cross_b, cross_b_e, 1}, {"brau", brau_a, brau_a_e, 0, 0, 0},
  {"braslot", braslot_a, braslot_a_e, 0, 0, 0},
};
#define PG_A(i) (2 * (i))
#define PG_U0   (2 * NC)          /* a user page with PR = 00 (privileged only) */
#define PG_U1   (2 * NC + 1)      /* unmapped */
#define PG_C    (2 * NC + 2)      /* unmapped: brau's target */
#define PG_A7   (2 * NC + 3)      /* mapped to physical area 7 (0x1C000000): a translated fetch there is FX_ADDR */

static uint32_t entry_va(int i) { return VA_PG(PG_A(i)) + 0x1000u - (uint32_t)(C[i].ae - C[i].a); }
static void itlb_clear(void) { for (int e = 0; e < 4; e++) R32(ITLB_AA(e)) = 0; }

static void show(const char *name, const char *mode, int i, uint32_t r) {
  const uint32_t A = VA_PG(PG_A(i));
  printf("PGX %s %s r0=%08lx n=%lu", name, mode, (unsigned long)r, (unsigned long)g_nlog);
  for (uint32_t k = 0; k < g_nlog; k++)
    printf(" [%03lx tea=%+ld spc=%+ld]", (unsigned long)(g_log[k].kind == 3 ? g_log[k].intevt : g_log[k].expevt),
           (long)(int32_t)(g_log[k].tea - A), (long)(int32_t)(g_log[k].spc - A));
  printf("\n");
}

static uint32_t run_case(int i, int warm) {
  uint32_t a = 0, b = 0;
  switch (i) {
  case C_P4RD: a = TCNT1; break;
  case C_LDCSR: b = 0x400000F0u; break;
  case C_DMISS: a = VA_PG(PG_U1) + 0x10u; break;
  case C_BRAU: a = VA_PG(PG_C) + 0x20u; break;
  case C_BRASLOT: a = (uint32_t)pgx_ret; break;
  default: break;
  }
  if (!warm) itlb_clear();
  if (i == C_SLP) {
    R32(TCOR0) = 200; R32(TCNT0) = 200; R16(TCR0) = UNIE; R8(TSTR) = 1;
    set_sr(0x40000000u);
  }
  const uint32_t r = call_va(entry_va(i), a, b, 0x1234u);
  if (i == C_SLP) { set_sr(0x400000F0u); tmu_reset(); }
  return r;
}

int main(void) {
  printf("pgx: start\n");
  g_resume = (uint32_t)pgx_ret; g_resume_sr = 0x400000F0u;
  hard_clear();
  int e = 0;
  for (int i = 0; i < NC; i++) {
    const uint32_t na = (uint32_t)(C[i].ae - C[i].a);
    memcpy((void *)P1(PA_PG(PG_A(i)) + 0x1000u - na), C[i].a, na);
    utlb_set(e++, VA_PG(PG_A(i)), PA_PG(PG_A(i)), SZ4K, 3, 1, 1, 0, 0);
    if (C[i].bmap) {
      memcpy((void *)P1(PA_PG(PG_A(i) + 1)), C[i].b, (unsigned)(C[i].be - C[i].b));
      utlb_set(e++, VA_PG(PG_A(i) + 1), PA_PG(PG_A(i) + 1), SZ4K, 3, 1, 1, 0, 0);
    }
  }
  R32(P1(PA_PG(PG_U0)) + 0x40u) = 0x0009FFFDu;                         /* user code: .word 0xFFFD ; nop */
  /* the copies reach memory and no I-cache line holds an older image (M3-fix, M3-verify F10: on the real memory the
   * copy-back OC held them and the IC fetched stale memory; data no-ops on imx) */
  for (int i = 0; i < NC; i++) {
    const uint32_t na = (uint32_t)(C[i].ae - C[i].a);
    ocbwb_range((void *)P1(PA_PG(PG_A(i)) + 0x1000u - na), na);
    if (C[i].bmap) ocbwb_range((void *)P1(PA_PG(PG_A(i) + 1)), (uint32_t)(C[i].be - C[i].b));
  }
  ocbwb_range((void *)(P1(PA_PG(PG_U0)) + 0x40u), 4);
  pgx_ici();
  utlb_set(e++, VA_PG(PG_U0), PA_PG(PG_U0), SZ4K, 0, 1, 1, 0, 0);     /* PR = 00 */
  utlb_set(e++, VA_PG(PG_A7), 0x1C000000u, SZ4K, 3, 1, 1, 0, 0);    /* PA in area 7 */
  R32(PTEH) = 0;
  tmu_reset();
  R16(IPRA) = 0x8000;
  mmucr_set(M_AT);
  for (int i = 0; i < NC; i++) {
    if (i == C_CROSS) continue;
    for (int w = 0; w < 2; w++) {
      clrlog();
      const uint32_t r = run_case(i, w);
      show(C[i].name, w ? "warm" : "cold", i, r);
    }
  }
  /* a JMP to a page the UTLB maps into physical area 7: SOC.md 4.1 item 2 / G5 [P]: FX_ADDR (0x0E0), SPC = TEA = it */
  for (int w = 0; w < 2; w++) {
    clrlog();
    if (!w) itlb_clear();
    const uint32_t r = call_va(VA_PG(PG_A7) + 0x40u, 0, 0, 0);
    printf("PGX a7fetch %s r0=%08lx n=%lu", w ? "warm" : "cold", (unsigned long)r, (unsigned long)g_nlog);
    for (uint32_t k = 0; k < g_nlog; k++)
      printf(" [%03lx tea=%+ld spc=%+ld]", (unsigned long)g_log[k].expevt,
             (long)(int32_t)(g_log[k].tea - VA_PG(PG_A7)), (long)(int32_t)(g_log[k].spc - VA_PG(PG_A7)));
    printf("\n");
  }
  /* user mode: a fetch from a PR = 00 page (0x0A0), from an unmapped page (0x040) */
  g_resume = (uint32_t)to_user_ret;
  for (int w = 0; w < 2; w++) {
    clrlog();
    if (!w) itlb_clear();
    uint32_t r = to_user(VA_PG(PG_U0) + 0x40u, 0);
    printf("PGX uprot %s r0=%08lx n=%lu", w ? "warm" : "cold", (unsigned long)r, (unsigned long)g_nlog);
    for (uint32_t k = 0; k < g_nlog; k++)
      printf(" [%03lx tea=%+ld spc=%+ld]", (unsigned long)g_log[k].expevt,
             (long)(int32_t)(g_log[k].tea - VA_PG(PG_U0)), (long)(int32_t)(g_log[k].spc - VA_PG(PG_U0)));
    printf("\n");
  }
  clrlog();
  (void)to_user(VA_PG(PG_U1) + 0x80u, 0);
  printf("PGX umiss r0=0 n=%lu", (unsigned long)g_nlog);
  for (uint32_t k = 0; k < g_nlog; k++)
    printf(" [%03lx tea=%+ld spc=%+ld]", (unsigned long)g_log[k].expevt,
           (long)(int32_t)(g_log[k].tea - VA_PG(PG_U1)), (long)(int32_t)(g_log[k].spc - VA_PG(PG_U1)));
  printf("\n");
  g_resume = (uint32_t)pgx_ret;
  /* straight-line crossing under random TMU0 interrupts, the ITLB emptied before every call */
  uint32_t sum = 0, ref = 0;
  clrlog();
  g_irq_quiet = 1; g_tmu_rand = 1; g_nirq = 0;
  R32(TCOR0) = 9; R32(TCNT0) = 9; R16(TCR0) = UNIE; R8(TSTR) = 1;
  set_sr(0x40000000u);
  for (int k = 0; k < 200; k++) {
    if (k & 1) itlb_clear();
    sum += call_va(entry_va(C_CROSS), (uint32_t)k * 0x10001u, (uint32_t)k ^ 0x5A5Au, 0x1234u + (uint32_t)k);
  }
  set_sr(0x400000F0u);
  tmu_reset();
  const uint32_t nirq = g_nirq;
  g_irq_quiet = 0; g_tmu_rand = 0;
  mmucr_set(0);
  for (int k = 0; k < 200; k++)
    ref += call_va((uint32_t)cross_a, (uint32_t)k * 0x10001u, (uint32_t)k ^ 0x5A5Au, 0x1234u + (uint32_t)k);
  printf("PGX cross irq sum=%08lx ref=%08lx irqs=%lu logged=%lu\n", (unsigned long)sum, (unsigned long)ref,
         (unsigned long)nirq, (unsigned long)g_nlog);
  g_resume = 0;
  printf("pgx: done\n");
  return 0;
}
