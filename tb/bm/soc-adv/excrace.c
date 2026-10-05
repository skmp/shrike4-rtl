/* excrace.c -- M1a adversarial test: the exception registers' two writers (SOC.md 3.1.3, 3.4: the P4 bus through the
 * CCN window, and the entry's excw pulse at EXC0's S) meeting in the pipeline.  The handler logs EXPEVT / INTEVT / TRA
 * at entry; every value must be the entry's (the older store is architecturally first), and the lockstep checker compares
 * every later read.
 * Output: "XR <case> n=<entries> expevt=... tra=... spc=<SPC - label>" and "XR intevt k=.. handler=.. after=.." */
#include "adv.h"

extern uint32_t xr_expevt_trapa(void), xr_tra_trapa(void), xr_tra_illegal(void), xr_expevt_slot(void),
    xr_expevt_adrerr(void), xr_trapa_slot(void);
extern char xr_resume[], xr_slot_bra[];
typedef uint32_t (*xi_t)(uint32_t);
extern xi_t xi_tab[17];
extern uint32_t xi_st_tab[17];

static void one(const char *name, uint32_t (*f)(void), uint32_t ref) {
  clrlog();
  (void)f();
  printf("XR %s n=%lu", name, (unsigned long)g_nlog);
  for (uint32_t i = 0; i < g_nlog; i++)
    printf(" [expevt=%03lx tra=%03lx spc=%ld]", (unsigned long)g_log[i].expevt, (unsigned long)g_log[i].tra,
           (long)(int32_t)(g_log[i].spc - ref));
  printf(" now: expevt=%03lx tra=%03lx\n", (unsigned long)R32(EXPEVT), (unsigned long)R32(TRA));
}

int main(void) {
  printf("excrace: start\n");
  g_resume = (uint32_t)xr_resume; g_resume_sr = 0x400000F0u;
  one("expevt_trapa", xr_expevt_trapa, (uint32_t)xr_expevt_trapa);
  one("tra_trapa", xr_tra_trapa, (uint32_t)xr_tra_trapa);
  one("tra_illegal", xr_tra_illegal, (uint32_t)xr_tra_illegal);
  one("expevt_slot", xr_expevt_slot, (uint32_t)xr_expevt_slot);
  one("expevt_adrerr", xr_expevt_adrerr, (uint32_t)xr_expevt_adrerr);
  one("trapa_slot", xr_trapa_slot, (uint32_t)xr_slot_bra);

  /* INTEVT store against an interrupt entry: TMU0 pending at level 5, IMASK 15 -> 0, k NOPs, the store */
  g_clr_unie = 1; g_noreadback = 0;
  R16(IPRA) = 0x5000;
  for (int k = 0; k < 17; k++) {
    tmu_reset();
    R32(TCOR0) = 0xFFFFFFFFu; R32(TCNT0) = 2; R16(TCR0) = UNIE; R8(TSTR) = 1;
    while (!(R16(TCR0) & UNF)) ;
    R8(TSTR) = 0; (void)R8(TSTR);
    clrlog();
    const uint32_t after = xi_tab[k](0x40000000u);
    set_sr(0x400000F0u);
    printf("XR intevt k=%d n=%lu handler=%03lx at=%ld after=%03lx\n", k, (unsigned long)g_nlog,
           (unsigned long)(g_nlog ? g_log[0].intevt : 0), (long)(g_nlog ? (int32_t)(g_log[0].spc - xi_st_tab[k]) : 0),
           (unsigned long)after);
  }
  tmu_reset(); R16(IPRA) = 0;
  g_resume = 0;
  printf("excrace: done\n");
  return 0;
}
