/* win.c -- M2 verification: the landing of the Iclk P4 stores that steer translation (PTEH.ASID, MMUCR) and the
 * accesses behind them, on imx (TRANSLATE = 1) in lockstep.  SOC.md 6.2 gives MMUCR's AT / SV / SQMD a landing window
 * of 3 instructions; these probes ask the same of the other translation inputs and of what the window changes.
 * Parts (CASE bitmask, default = the parts that must pass: 0x7F):
 *   0x01 A  ASID 1 -> 2 (the page is ASID 1's, SH = 0), n = 3, 4, 5, 8 NOPs, then a P0 load: expected TLB miss 0x040
 *   0x02 B  ASID 1 -> 2 (the page is ASID 2's), n = 3, 4, 5, 8: expected the loaded word (no exception)
 *   0x04 S  MMUCR.SV 0 -> 1 / 1 -> 0 (page of ASID 2, PTEH.ASID 1), n = 0..5, 8: the window's SV pick (SOC.md 6.2)
 *   0x08 U  MMUCR AT 0 -> 1 with URC = 5, n = 3, 4, 5, 8, a P0 load of an unmapped page, then MMUCR read (URC)
 *   0x20 L  PTEL (or PTEH) store, n = 0..3 NOPs, LDTLB: the UTLB entry must hold the stored value (Linux's
 *           update_mmu_cache idiom: PTEL then LDTLB back to back); lockstep compares the LDTLB record
 *   0x40 O  UBC operand break (channel A: operand read, ASID compared, BASRA = 2) on the probe's load, PTEH.ASID
 *           1 -> 2 n = 3, 4, 8 NOPs before it: expected a 0x1E0 after the load (SPC = the next instruction)
 *   0x1000000 << n  O with n = 0, 1, 2 (the load in the group held behind the PTEH store)
 *   0x10 K  UBC pre-execution break with ASID compare (BAMRA.BASM = 0, BASRA = 2), PTEH.ASID 1 -> 2, the break
 *           instruction k = 16 / 24 instructions behind (outside any fetch run-ahead): expected a 0x1E0
 *   the window cases, each alone (a lockstep mismatch ends the checking of its run):
 *   0x100 << n  A with n = 0, 1, 2       0x1000 << n  B with n = 0, 1, 2
 *   0x10000 << n  U with n = 0, 1, 2     0x100000 << j  K with k = 0, 2, 4, 8 (bits 20-23)
 * Output lines "WIN <part> n=<n> exc=<count> [expevt tea spc-probe pteh] r0=<value> [urc=<URC>]" (m2/check.py).
 * M2-fix (agents/reports/integration/M2-fix.md): the window cases are the M2 gate's too, pinned by m2/check.py: A / B
 * n = 0..2 with the new ASID [HW tools/hw/mmu/ptehwin]; U n = 0, 1 untranslated, URC counted by the stored AT (SOC.md
 * 3.8); K k = 0..8 and O n = 0, 1 with the old ASID [HW tools/hw/mmu/ptehubc: the UBC's landing window]. */
#include "m2.h"

#ifndef CASE
#define CASE 0x7F
#endif

typedef uint32_t (*pw_fn)(volatile uint32_t *, uint32_t, uint32_t, uint32_t);
extern uint32_t pw0(volatile uint32_t *, uint32_t, uint32_t, uint32_t), pw1(volatile uint32_t *, uint32_t, uint32_t, uint32_t),
    pw2(volatile uint32_t *, uint32_t, uint32_t, uint32_t), pw3(volatile uint32_t *, uint32_t, uint32_t, uint32_t),
    pw4(volatile uint32_t *, uint32_t, uint32_t, uint32_t), pw5(volatile uint32_t *, uint32_t, uint32_t, uint32_t),
    pw8(volatile uint32_t *, uint32_t, uint32_t, uint32_t);
extern char pw_ret[];
typedef uint32_t (*pu_fn)(volatile uint32_t *, uint32_t, uint32_t);
extern uint32_t pu0(volatile uint32_t *, uint32_t, uint32_t), pu2(volatile uint32_t *, uint32_t, uint32_t),
    pu4(volatile uint32_t *, uint32_t, uint32_t), pu8(volatile uint32_t *, uint32_t, uint32_t),
    pu16(volatile uint32_t *, uint32_t, uint32_t), pu24(volatile uint32_t *, uint32_t, uint32_t);
extern void pl0(volatile uint32_t *, uint32_t), pl1(volatile uint32_t *, uint32_t), pl2(volatile uint32_t *, uint32_t),
    pl3(volatile uint32_t *, uint32_t);
static void (*const PL[4])(volatile uint32_t *, uint32_t) = {pl0, pl1, pl2, pl3};
extern char pu0_brk[], pu2_brk[], pu4_brk[], pu8_brk[], pu16_brk[], pu24_brk[];

static const struct { int n; pw_fn f; } PW[7] = {{0, pw0}, {1, pw1}, {2, pw2}, {3, pw3}, {4, pw4}, {5, pw5}, {8, pw8}};
static const struct { int k; pu_fn f; char *brk; } PU[6] = {{0, pu0, pu0_brk}, {2, pu2, pu2_brk}, {4, pu4, pu4_brk},
                                                           {8, pu8, pu8_brk}, {16, pu16, pu16_brk}, {24, pu24, pu24_brk}};

#define VA_X   0x10200000u              /* P0 page under test */
#define PA_X   0x0C200000u              /* its physical page (P1 0x8C200000) */
#define VA_Y   0x10300000u              /* a P0 page never mapped */
#define SENT   0xDEADBEEFu

static void report(const char *part, int n, uint32_t r0, int urc) {
  printf("WIN %s n=%d exc=%lu", part, n, (unsigned long)g_nlog);
  for (uint32_t i = 0; i < g_nlog; i++)
    printf(" [%03lx tea=%08lx pteh=%08lx]", (unsigned long)g_log[i].expevt, (unsigned long)g_log[i].tea,
           (unsigned long)g_log[i].pteh);
  printf(" r0=%08lx", (unsigned long)r0);
  if (urc >= 0) printf(" urc=%d", urc);
  printf("\n");
}

/* part A / B: the page belongs to ASID `owner`; PTEH.ASID 1 -> 2 n NOPs before the load */
static void asid_case(const char *part, int owner, int i) {
  hard_clear();
  utlb_set(0, VA_X, PA_X, SZ4K, 3, 1, 1, 0, owner);
  R32(PTEH) = 0x00000001u;
  mmucr_set(M_AT);
  clrlog();
  const uint32_t r = PW[i].f((volatile uint32_t *)PTEH, 0x00000002u, VA_X + 0x40u, SENT);
  mmucr_set(0);
  report(part, PW[i].n, r, -1);
}

/* part S: page of ASID 2, PTEH.ASID 1, privileged load; MMUCR SV from -> to (AT = 1 both) */
static void sv_case(int from, int to, int i) {
  hard_clear();
  utlb_set(0, VA_X, PA_X, SZ4K, 3, 1, 1, 0, 2);
  R32(PTEH) = 0x00000001u;
  mmucr_set(M_AT | (from ? M_SV : 0));
  clrlog();
  const uint32_t r = PW[i].f((volatile uint32_t *)MMUCR, M_AT | (to ? M_SV : 0) | M_URC(7), VA_X + 0x44u, SENT);
  const int u = (int)G_URC(R32(MMUCR));
  mmucr_set(0);
  report(from ? "S10" : "S01", PW[i].n, r, u);
}

/* part U: MMUCR AT 0 -> 1 (URC = 5) n NOPs before a load of the unmapped VA_Y; URC read 8+ instructions later */
static void urc_case(int i) {
  hard_clear();
  utlb_set(0, VA_X, PA_X, SZ4K, 3, 1, 1, 0, 0);
  mmucr_set(M_URC(5));
  clrlog();
  const uint32_t r = PW[i].f((volatile uint32_t *)MMUCR, M_AT | M_URC(5), VA_Y + 0x48u, SENT);
  const int u = (int)G_URC(R32(MMUCR));
  mmucr_set(0);
  report("U", PW[i].n, r, u);
}

/* part K: UBC channel A pre-execution break on the probe's mov #1,r0 with ASID 2 compared; PTEH.ASID 1 -> 2 */
static void ubc_case(int j) {
  R16(BBRA) = 0; R16(BRCR) = 0; (void)R16(BRCR);
  R32(PTEH) = 0x00000001u;
  R32(BARA) = (uint32_t)PU[j].brk; R8(BAMRA) = 0x00; R8(BASRA) = 0x02; R16(BBRA) = 0x14;
  R16(BRCR) = 0; (void)R16(BRCR);
  for (volatile int d = 0; d < 20; d++) ;
  clrlog();
  const uint32_t r = PU[j].f((volatile uint32_t *)PTEH, 0x00000002u, SENT);
  R16(BBRA) = 0; R16(BRCR) = 0; (void)R16(BRCR);
  printf("WIN K n=%d exc=%lu", PU[j].k, (unsigned long)g_nlog);
  for (uint32_t i = 0; i < g_nlog; i++)
    printf(" [%03lx spc=%+ld]", (unsigned long)g_log[i].expevt, (long)(int32_t)(g_log[i].spc - (uint32_t)PU[j].brk));
  printf(" r0=%08lx\n", (unsigned long)r);
  R32(PTEH) = 0;
}

/* part L: PTEL / PTEH store n NOPs before LDTLB; URC = 9: UTLB[9] must hold the stored PTEL / PTEH */
static void ldtlb_case(int n, int pteh) {
  hard_clear();
  mmucr_set(M_URC(9));
  R32(PTEH) = 0x30000011u; R32(PTEL) = ptel_of(0x0C300000u, SZ4K, 3, 1, 1, 0); R32(PTEA) = 0;
  if (pteh) PL[n]((volatile uint32_t *)PTEH, 0x30001022u);
  else PL[n]((volatile uint32_t *)PTEL, ptel_of(0x0C301000u, SZ64K, 2, 0, 1, 1));
  printf("WIN L%s n=%d u9=%08lx/%08lx\n", pteh ? "h" : "l", n, (unsigned long)R32(UTLB_AA(9)),
         (unsigned long)R32(UTLB_DA1(9)));
  hard_clear();
}

/* part O: UBC channel A operand-read break on VA_O with ASID 2 compared; PTEH.ASID 1 -> 2 n NOPs before the load */
#define VA_O 0x8C200100u
static void ubco_case(int i) {
  R16(BBRA) = 0; R16(BRCR) = 0; (void)R16(BRCR);
  R32(PTEH) = 0x00000001u;
  R32(BARA) = VA_O; R8(BAMRA) = 0x00; R8(BASRA) = 0x02; R16(BBRA) = 0x27;   /* operand, read, longword */
  R16(BRCR) = 0; (void)R16(BRCR);
  for (volatile int d = 0; d < 20; d++) ;
  clrlog();
  const uint32_t r = PW[i].f((volatile uint32_t *)PTEH, 0x00000002u, VA_O, SENT);
  R16(BBRA) = 0; R16(BRCR) = 0; (void)R16(BRCR);
  printf("WIN O n=%d exc=%lu", PW[i].n, (unsigned long)g_nlog);
  for (uint32_t k = 0; k < g_nlog; k++) printf(" [%03lx]", (unsigned long)g_log[k].expevt);
  printf(" r0=%08lx\n", (unsigned long)r);
  R32(PTEH) = 0;
}

int main(void) {
  printf("win: start case=%x\n", CASE);
  g_resume = (uint32_t)pw_ret; g_resume_sr = 0x400000F0u;
  for (int k = 0; k < 64; k++) R32(0x8C200000u + 4u * (uint32_t)k) = 0x600D0000u + (uint32_t)k;
  if (CASE & 0x01) for (int i = 3; i < 7; i++) asid_case("A", 1, i);
  if (CASE & 0x02) for (int i = 3; i < 7; i++) asid_case("B", 2, i);
  if (CASE & 0x04) for (int i = 0; i < 7; i++) { sv_case(0, 1, i); sv_case(1, 0, i); }
  if (CASE & 0x08) for (int i = 3; i < 7; i++) urc_case(i);
  if (CASE & 0x10) for (int j = 4; j < 6; j++) ubc_case(j);
  if (CASE & 0x20) for (int n = 0; n < 4; n++) { ldtlb_case(n, 0); ldtlb_case(n, 1); }
  if (CASE & 0x40) for (int i = 3; i < 7; i++) ubco_case(i);
  for (int i = 0; i < 3; i++) if (CASE & (0x1000000 << i)) ubco_case(i);
  for (int i = 0; i < 3; i++) {
    if (CASE & (0x100 << i)) asid_case("A", 1, i);
    if (CASE & (0x1000 << i)) asid_case("B", 2, i);
    if (CASE & (0x10000 << i)) urc_case(i);
  }
  for (int j = 0; j < 4; j++) if (CASE & (0x100000 << j)) ubc_case(j);
  g_resume = 0;
  printf("win: done\n");
  return 0;
}
