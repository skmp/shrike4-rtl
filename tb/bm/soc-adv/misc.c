/* misc.c -- M1a adversarial test, two groups:
 *   IE   an interrupt made acceptable (LDC SR, IMASK 15 -> 0 with TMU0 pending) k NOPs before a TRAPA (completion
 *        type), a misaligned load (re-execution type) or LDTLB (TRANSLATE = 0): whichever entry comes first, the other
 *        must follow in the right order (an interrupt never between TRAPA's check and its entry; the address error
 *        re-raised after the interrupt's RTE).  The lockstep checker rules on each entry's legality and SPC.
 *   RMW  the GBR-relative byte read-modify-writes and TAS.B on P4 registers (the bridge sees a read then a write):
 *        TOCR (8-bit: TCOE, bit 0 only), TSTR (8-bit, bits 2-0), IPRA (16-bit: a byte access is the wrong size: reads 0,
 *        write ignored), STBCR2 (8-bit: DSLP, bit 7).
 * Output: "IE <op> k=<k> order=<kinds> spc=<offsets from the op>" and "RMW <reg> t0=.. or=.. xor=.. and=.. tas_t=.. tas=.." */
#include "adv.h"

typedef uint32_t (*ie_t)(uint32_t);
extern ie_t ie_trapa_tab[15], ie_adr_tab[15], ie_ldtlb_tab[15];
extern uint32_t ie_trapa_x_tab[15], ie_adr_x_tab[15], ie_ldtlb_x_tab[15];
extern char ie_resume[];
extern uint32_t rmw_gbr(uint32_t, uint32_t *);

static void ie(const char *name, ie_t *tab, uint32_t *xt) {
  for (int k = 0; k < 15; k++) {
    set_sr(0x400000F0u);
    tmu_reset();
    R32(TCOR0) = 0xFFFFFFFFu; R32(TCNT0) = 2; R16(TCR0) = UNIE; R8(TSTR) = 1;
    while (!(R16(TCR0) & UNF)) ;
    R8(TSTR) = 0; (void)R8(TSTR);
    clrlog();
    tab[k](0x40000000u);
    set_sr(0x400000F0u);
    printf("IE %s k=%d n=%lu", name, k, (unsigned long)g_nlog);
    for (uint32_t i = 0; i < g_nlog; i++)
      printf(" [%s %03lx spc=%ld]", g_log[i].kind == 3 ? "irq" : "exc",
             (unsigned long)(g_log[i].kind == 3 ? g_log[i].intevt : g_log[i].expevt),
             (long)(int32_t)(g_log[i].spc - xt[k]));
    printf("\n");
  }
}

static void rmw(const char *name, uint32_t a, int wide, uint32_t init) {
  uint32_t o[6];
  if (wide) R16(a) = (uint16_t)init; else R8(a) = (uint8_t)init;
  (void)R8(TSTR);
  rmw_gbr(a, o);
  const uint32_t after = wide ? R16(a) : R8(a);
  printf("RMW %s t0=%lu or=%lx xor=%lx and=%lx tas_t=%lu tas=%lx final=%lx\n", name, (unsigned long)o[0],
         (unsigned long)o[1], (unsigned long)o[2], (unsigned long)o[3], (unsigned long)o[4], (unsigned long)o[5],
         (unsigned long)after);
}

int main(void) {
  printf("misc: start\n");
  g_clr_unie = 1; g_noreadback = 0;
  g_resume = (uint32_t)ie_resume; g_resume_sr = 0x400000F0u;
  R16(IPRA) = 0x5000;
  ie("trapa", ie_trapa_tab, ie_trapa_x_tab);
  ie("adr", ie_adr_tab, ie_adr_x_tab);
  ie("ldtlb", ie_ldtlb_tab, ie_ldtlb_x_tab);
  tmu_reset(); R16(IPRA) = 0;
  g_resume = 0;
  rmw("TOCR", TOCR, 0, 0x00);
  rmw("TSTR", TSTR, 0, 0x00);
  R8(TSTR) = 0;
  rmw("IPRA", IPRA, 1, 0x1230);
  rmw("STBCR2", STBCR2, 0, 0x00);
  R8(STBCR2) = 0; (void)R8(STBCR2);
  printf("misc: done\n");
  return 0;
}
