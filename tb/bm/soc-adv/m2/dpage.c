/* dpage.c -- M2 verification: demand paging with translated code, under an interrupt storm, on imx (TRANSLATE = 1)
 * in lockstep.  An OS-like run: 7 translated code pages and 6 translated data pages share 6 UTLB entries (MMUCR.URB = 6)
 * and the 4 ITLB entries, so the ITLB refills constantly and most refills and data accesses miss the UTLB; the m2rt.S
 * refill handler (P1) answers every TLB miss (EXPEVT 0x040 / 0x060, instruction and data) with LDTLB from a software
 * page table.  TMU0 interrupts at a random period of 7..22 Pclk/4 counts (m2rt.S g_tmu_rand) land at every phase of
 * the ITLB refill sequencer, its redirect wait, the UTLB-miss poisons, the exception entries and the LDTLBs.
 * The routines (dpage_asm.S) sit where the fetch crosses pages: an RTS whose run-ahead refills the next page, one whose
 * next page is unmapped (the speculative UTLB miss, no exception), an RTS whose delay slot is the next page's first
 * word, straight-line code across a boundary, MAC.L / MAC.W with operands on two pages, TAS.B, byte RMW, FMOV.S.
 * The same call sequence then runs untranslated from P1 on a mirror of the data (no MMU, no interrupts): every data
 * word and the sum of the returned values must be equal.  Lockstep checks every step, every LDTLB index (URC), every
 * ITLBFILL, every interrupt's legality and SPC.
 * Output: "DPAGE iters=<n> refills=<n> irqs=<n> exc=<unexpected> sum=<translated> ref=<mirror> diff=<words>" and the
 * URC / LRUI state at the end. */
#include "m2.h"

#ifndef ITERS
#define ITERS 1200
#endif

extern char t_end[], t_end_e[], t_slotx[], t_slotx_e[], t_mid[], t_mid_e[], t_cross[], t_cross_e[], t_mac[], t_mac_e[];
extern char t_slot0[], t_slot0_e[];
extern uint32_t call_va(uint32_t va, uint32_t a, uint32_t b, uint32_t v);
void *memcpy(void *, const void *, unsigned);

#define VC 0x20400000u      /* code pages: VA VC + k * 4K -> PA PCODE + k * 4K; pages 5 and 8 are holes */
#define PCODE 0x0C400000u
#define VD 0x20500000u      /* data pages: VA VD + d * 4K -> PA PDATA + d * 4K */
#define PDATA 0x0C500000u
#define MIRROR 0x8C600000u  /* the untranslated reference copy of the data */
#define NPD 6


typedef struct { int page; uint32_t off; char *t, *te; uint32_t va; } ent_t;
static ent_t E[9] = {
  {0, 0, t_end, t_end_e, 0}, {1, 0, t_end, t_end_e, 0}, {2, 0, t_end, t_end_e, 0},
  {3, 0, t_slotx, t_slotx_e, 0}, {4, 0x100, t_mid, t_mid_e, 0}, {4, 0, t_end, t_end_e, 0},
  {6, 0, t_cross, t_cross_e, 0}, {7, 0x400, t_mac, t_mac_e, 0}, {7, 0, t_end, t_end_e, 0},
};
static pte_t PT[16];
#ifndef CASE
#define CASE 0
#endif
#define PIN_CODE (CASE & 1)     /* dpage_c1: the code pages pinned in UTLB entries 8.. (outside URB): no instruction TLB
                                   miss exception, the ITLB still refills constantly */
static uint32_t seed;
static uint32_t rnd(void) { seed = seed * 1103515245u + 12345u; return seed >> 16; }

static uint32_t run(int translated) {
  uint32_t sum = 0;
  seed = 0x5EED1234u;
  for (int i = 0; i < ITERS; i++) {
    const int k = (int)(rnd() % 9u);
    const int d = (int)(rnd() % NPD), d2 = (d + 1 + (int)(rnd() % (NPD - 1))) % NPD;
    const uint32_t oa = (rnd() % 256u) * 16u, ob = (rnd() % 256u) * 16u;
    const uint32_t a = translated ? VD + (uint32_t)d * 0x1000u + oa : MIRROR + (uint32_t)d * 0x1000u + oa;
    const uint32_t b = translated ? VD + (uint32_t)d2 * 0x1000u + ob : MIRROR + (uint32_t)d2 * 0x1000u + ob;
    const uint32_t f = translated ? E[k].va : (uint32_t)E[k].t;
    sum += call_va(f, a, b, (uint32_t)i * 0x9E3779B9u);
  }
  return sum;
}

int main(void) {
  printf("dpage: start iters=%d pin=%d\n", ITERS, PIN_CODE);
  g_resume = 0;
  /* code pages: every routine copied to its place; t_slotx's slot to the next page's first word */
  for (int k = 0; k < 9; k++) {
    const uint32_t n = (uint32_t)(E[k].te - E[k].t);
    if (E[k].off == 0) E[k].off = 0x1000u - n;
    if (E[k].t == t_cross) E[k].off = 0x1000u - (n / 2u & ~1u);
    memcpy((void *)P1(PCODE + (uint32_t)E[k].page * 0x1000u + E[k].off), E[k].t, n);
    E[k].va = VC + (uint32_t)E[k].page * 0x1000u + E[k].off;
    if (E[k].t == t_slotx)
      memcpy((void *)P1(PCODE + (uint32_t)(E[k].page + 1) * 0x1000u), t_slot0, (unsigned)(t_slot0_e - t_slot0));
  }
  /* data and mirror */
  for (uint32_t w = 0; w < NPD * 1024u; w++) {
    const uint32_t v = w * 0x01000193u ^ 0xA5A5A5A5u;
    R32(P1(PDATA) + 4u * w) = v;
    R32(MIRROR + 4u * w) = v;
  }
  int n = 0;
  for (int k = 0; k < 9; k++) if (k != 5 && k != 8) {
    PT[n].vpn = VC + (uint32_t)k * 0x1000u; PT[n].mask = 0xFFFFF000u; PT[n].ptel = ptel_of(PCODE + (uint32_t)k * 0x1000u, SZ4K, 3, 1, 1, 0); n++;
  }
  for (int d = 0; d < NPD; d++) {
    PT[n].vpn = VD + (uint32_t)d * 0x1000u; PT[n].mask = 0xFFFFF000u; PT[n].ptel = ptel_of(PDATA + (uint32_t)d * 0x1000u, SZ4K, 3, 1, 1, 0); n++;
  }
  g_pt = PT; g_npt = (uint32_t)n;
  R32(PTEA) = 0;
  hard_clear();
  R32(PTEH) = 0;
  if (PIN_CODE)
    for (int k = 0; k < 7; k++) {
      const int pg = k < 5 ? k : k + 1;
      utlb_set(8 + k, VC + (uint32_t)pg * 0x1000u, PCODE + (uint32_t)pg * 0x1000u, SZ4K, 3, 1, 1, 0, 0);
    }
  g_refill = 1; g_irq_quiet = 1; g_tmu_rand = 1; g_clr_unie = 0; g_nirq = 0; g_nrefill = 0; g_nexc = 0; clrlog();
  mmucr_set(M_AT | M_URB(PIN_CODE ? 3 : 6));
  tmu_reset();
  R16(IPRA) = 0x8000;
  R32(TCOR0) = 10; R32(TCNT0) = 10; R16(TCR0) = UNIE;
  R8(TSTR) = 1;
  set_sr(0x40000000u);                                  /* MD = 1, BL = 0, IMASK = 0 */
  const uint32_t s1 = run(1);
  set_sr(0x400000F0u);
  tmu_reset();
  const uint32_t m = R32(MMUCR);
  mmucr_set(0);
  const uint32_t nref = g_nrefill, nirq = g_nirq, nexc = g_nexc;
  g_refill = 0; g_tmu_rand = 0;
  const uint32_t s2 = run(0);
  uint32_t diff = 0;
  for (uint32_t w = 0; w < NPD * 1024u; w++) if (R32(P1(PDATA) + 4u * w) != R32(MIRROR + 4u * w)) diff++;
  printf("DPAGE iters=%d refills=%lu irqs=%lu exc=%lu logged=%lu sum=%08lx ref=%08lx diff=%lu\n", ITERS,
         (unsigned long)nref, (unsigned long)nirq, (unsigned long)(nexc - nirq), (unsigned long)g_nlog,
         (unsigned long)s1, (unsigned long)s2, (unsigned long)diff);
  printf("DPAGE mmucr urb=%lu urc=%lu\n", (unsigned long)((m >> 18) & 63u), (unsigned long)G_URC(m));
  plog("DPAGE log");
  printf("dpage: done\n");
  return 0;
}
