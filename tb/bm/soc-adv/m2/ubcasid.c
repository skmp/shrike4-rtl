/* ubcasid.c -- M2-fix (agents/reports/integration/M2-fix.md; M2-verify F6): the console probe tools/hw/mmu/ptehubc on
 * the SoC, in lockstep.  A PTEH.ASID 1 -> 2 store k NOPs before
 *   N / O  (k = 0..14) the target of a pre-execution instruction break on channel A with the ASID compared (BAMRA 0),
 *          BASRA 2 / 1: a break = the target compared with the new / the old ASID
 *   P / Q  (k = 0..6) a load, the target of an operand-read break with the ASID compared, BASRA 2 / 1
 *   T / S  the references (ubcbrk T, ubcevt S): a word store to BBRA arms the break (ASID masked) k NOPs before the
 *          target / the load
 * Each call is made first with an unchanged store (PTEH 1 -> 1, BBRA 0 -> 0), then measured: the console's warm rows
 * (imx has no I-cache).  The snippet is called through ua_call, whose JSR slot is a NOP and whose argument registers are
 * set well before it, so the store issues at its JSR target without an operand stall, as at ubcbrk T's call site (the
 * console's ptehubc call site loads r4 in the slot; the console's T boundary is 13 with either, v1's is not: 9.22).  Console, two runs x 3 repetitions, warm: N breaks from k = 13, O up to k = 12; P from k = 3,
 * Q up to k = 2; T from k = 13; S from k = 3 [HW tools/hw/mmu/ptehubc].  Output lines "UA <case> k=<k> n=<breaks>
 * [<EXPEVT> spc=+<offset from the snippet>]" (m2/check.py).  The run-time (m2rt.S) logs the 0x1E0 and resumes at
 * _ua_ret (a return) with SR = g_resume_sr.
 * ubcasid_c1 (CASE bit 0; M3-fix, M3-verify F2, SOC.md 9.22 (b)): the console's COLD rows on rmx (the real I-cache):
 * instead of the warm-up call, the snippet's three lines are invalidated first (ua_icinval: KOS's icache_flush_range
 * from P2, as ptehubc did), ua_call itself warm.  Console, cold: N breaks from k = 11, O up to k = 10; T from k = 13;
 * P / Q / S as warm [HW tools/hw/mmu/ptehubc]. */
#include "m2.h"

extern uint32_t ua_call(char *fn, uint32_t reg, uint32_t val, uint32_t data);   /* ubcasid_asm.S */
extern void ua_icinval(uint32_t start, uint32_t len);
#if defined(CASE) && ((CASE) & 1)
#define UA_COLD 1
#else
#define UA_COLD 0
#endif
#define DK(k) extern char uk##k[], uk##k##_tgt[], ut##k[], ut##k##_tgt[];
DK(0) DK(1) DK(2) DK(3) DK(4) DK(5) DK(6) DK(7) DK(8) DK(9) DK(10) DK(11) DK(12) DK(13) DK(14)
#define DO(k) extern char uo##k[], uo##k##_ld[], us##k[], us##k##_ld[];
DO(0) DO(1) DO(2) DO(3) DO(4) DO(5) DO(6)
extern char ua_ret[];
static char *const UK[15] = {uk0, uk1, uk2, uk3, uk4, uk5, uk6, uk7, uk8, uk9, uk10, uk11, uk12, uk13, uk14};
static char *const UKT[15] = {uk0_tgt, uk1_tgt, uk2_tgt, uk3_tgt, uk4_tgt, uk5_tgt, uk6_tgt, uk7_tgt, uk8_tgt, uk9_tgt,
                              uk10_tgt, uk11_tgt, uk12_tgt, uk13_tgt, uk14_tgt};
static char *const UT[15] = {ut0, ut1, ut2, ut3, ut4, ut5, ut6, ut7, ut8, ut9, ut10, ut11, ut12, ut13, ut14};
static char *const UTT[15] = {ut0_tgt, ut1_tgt, ut2_tgt, ut3_tgt, ut4_tgt, ut5_tgt, ut6_tgt, ut7_tgt, ut8_tgt, ut9_tgt,
                              ut10_tgt, ut11_tgt, ut12_tgt, ut13_tgt, ut14_tgt};
static char *const UO[7] = {uo0, uo1, uo2, uo3, uo4, uo5, uo6};
static char *const UOL[7] = {uo0_ld, uo1_ld, uo2_ld, uo3_ld, uo4_ld, uo5_ld, uo6_ld};
static char *const US[7] = {us0, us1, us2, us3, us4, us5, us6};
static char *const USL[7] = {us0_ld, us1_ld, us2_ld, us3_ld, us4_ld, us5_ld, us6_ld};
static uint32_t dbuf[8] __attribute__((aligned(32)));

static void one(char kind, int k) {
  const int t = kind == 'T' || kind == 'S', op = kind == 'P' || kind == 'Q' || kind == 'S';
  char *fn = op ? (kind == 'S' ? US[k] : UO[k]) : (t ? UT[k] : UK[k]);
  char *tgt = op ? (kind == 'S' ? USL[k] : UOL[k]) : (t ? UTT[k] : UKT[k]);
  const uint8_t basra = (kind == 'N' || kind == 'P') ? 2 : 1;
  const uint16_t bbra = op ? 0x0024 : 0x0014;              /* operand read / instruction access (pre-execution) */
  const uint32_t reg = t ? BBRA : PTEH, val = t ? bbra : 2u, same = t ? 0u : 1u;
  for (int pass = UA_COLD; pass < 2; pass++) {             /* 0: the warm-up call, 1: the measured one */
    R16(BBRA) = 0; R16(BBRB) = 0; R16(BRCR) = 0; (void)R16(BRCR);
    R32(PTEH) = 1u;
    if (UA_COLD) {                                         /* ubcasid_c1: ua_call warm, the snippet's lines cold */
      ua_call(ua_ret, PTEH, 1u, (uint32_t)dbuf);
      ua_icinval((uint32_t)fn & ~31u, 0x60);
    }
    R32(BARA) = op ? (uint32_t)dbuf : (uint32_t)tgt; R8(BAMRA) = t ? 0x04 : 0x00; R8(BASRA) = basra;
    R16(BBRA) = t ? 0 : bbra; (void)R16(BBRA);
    for (volatile int d = 0; d < 20; d++) ;              /* the arming reaches the fetch-side copy */
    clrlog();
    ua_call(fn, reg, pass ? val : same, (uint32_t)dbuf);
  }
  R16(BBRA) = 0; R16(BRCR) = 0; (void)R16(BRCR);
  R32(PTEH) = 0;
  printf("UA %c k=%d n=%lu", kind, k, (unsigned long)g_nlog);
  for (uint32_t i = 0; i < g_nlog; i++)
    printf(" [%03lx spc=+%ld]", (unsigned long)g_log[i].expevt, (long)(g_log[i].spc - (uint32_t)fn));
  printf("\n");
}

int main(void) {
  printf("ubcasid: start\n");
  g_resume = (uint32_t)ua_ret; g_resume_sr = 0x400000F0u;
  for (int i = 0; i < 8; i++) dbuf[i] = 0x0DA7A000u + (uint32_t)i;
  static const char cases[6] = {'N', 'O', 'T', 'P', 'Q', 'S'};
  for (int c = 0; c < 6; c++)
    for (int k = 0; k < (c < 3 ? 15 : 7); k++) one(cases[c], k);
  g_resume = 0;
  printf("ubcasid: done\n");
  return 0;
}
