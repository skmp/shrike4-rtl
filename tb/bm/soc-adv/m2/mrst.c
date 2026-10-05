/* mrst.c -- M2 verification: the resets the MMU requests, through the whole chip, on imx (TRANSLATE = 1) in lockstep
 * (SOC.md 2.2, 2.3, 6.2 "Resets": a fetch / data multiple hit is the core's own reset request, in lockstep: EXC 0x140
 * with handler 0xA0000000; an exception with SR.BL = 1 is the CPU-only reset, EXC 0x020).  Every phase first gives the
 * units a distinctive state, then triggers one reset; the boot stub runs again, jumps to 0x8C010000, and the next phase
 * reports what each unit kept (checked by m2/check.py against SOC.md 2.3's columns; lockstep compares every
 * non-volatile read).
 *   phase 0 -> data UTLB multiple hit (two valid UTLB entries for one VPN, a load)            manual, EXPEVT 0x140
 *   phase 1 -> fetch ITLB multiple hit (two valid ITLB entries for one VPN, a JSR there)       manual, EXPEVT 0x140
 *   phase 2 -> fetch refill multiple hit (ITLB empty, two UTLB entries, a JSR there)           manual, EXPEVT 0x140
 *   phase 3 -> a data TLB miss with SR.BL = 1                                                  CPU-only, EXPEVT 0x020
 *   phase 4 -> PTEH and MMUCR stores with an illegal instruction right behind them, SR.BL = 1   CPU-only, EXPEVT 0x020:
 *              both stores were acked before the take, so both land (SOC.md 2.3 p4bus row: "a transaction already
 *              strobed or acked completes")
 *   phase 5 -> report, end
 * Output: "MRST phase=<p> <reg>=<value> ..." per phase. */
#include "m2.h"

extern uint32_t call_va(uint32_t va, uint32_t a, uint32_t b, uint32_t v);
extern uint32_t ld_bl1(uint32_t va);                     /* SR.BL <- 1, then a load of va */
extern void st_ill_bl1(volatile uint32_t *reg, uint32_t val, volatile uint32_t *reg2, uint32_t val2);

#define VA_D   0x10200000u
#define PA_D   0x0C200000u
#define VA_C   0x20400000u
#define PA_C   0x0C400000u

static void setup(uint32_t ph) {
  /* the state each unit must keep or lose (SOC.md 2.3) */
  R16(IPRA) = (uint16_t)(0x1230u + ph); R16(IPRB) = 0x4560; R16(IPRC) = 0x0780;
  tmu_reset();
  R8(TOCR) = 0x00;
  R32(TCOR0) = 0x0F0F0F00u + ph; R32(TCNT0) = 0x55AA55AAu; R16(TCR0) = 0x0003;
  R32(TCOR1) = 0x12345678u; R32(TCNT1) = 0x7FFFFFFFu; R16(TCR1) = 0x0000;
  R8(TSTR) = 2;
  R32(PTEL) = 0x0C0001FCu; R32(TTB) = 0x8C0FFFF0u + ph; R32(PTEA) = 0x5;
  R32(TEA) = 0xCAFE0000u + ph;
  R32(QACR0) = 0x0C; R32(QACR1) = 0x10;
  R32(BARA) = 0x87654320u + ph; R8(BASRA) = (uint8_t)(0x5A + ph);
  R16(PMCR2) = 0x0023;
  R8(STBCR2) = 0x00;
  R16(SCSMR2) = 0x0003; R8(SCBRR2) = 0x21;
  R8(SCSMR1) = 0x02; R8(SCBRR1) = 0x44;
  __asm__ __volatile__("trapa #0x33");                 /* TRA = 0xCC */
}

static void report(uint32_t ph) {
  printf("MRST phase=%lu entry sr=%08lx vbr=%08lx r0=%08lx r7=%08lx b1r0=%08lx\n", (unsigned long)ph,
         (unsigned long)g_entry[16], (unsigned long)g_entry[18], (unsigned long)g_entry[0], (unsigned long)g_entry[7],
         (unsigned long)g_entry[8]);
  printf("MRST phase=%lu ccn expevt=%03lx intevt=%03lx tra=%03lx tea=%08lx pteh=%08lx ptel=%08lx ttb=%08lx ptea=%lx "
         "mmucr=%08lx qacr0=%lx qacr1=%lx basra=%02x bara=%08lx pmcr2=%04x\n", (unsigned long)ph,
         (unsigned long)R32(EXPEVT), (unsigned long)R32(INTEVT), (unsigned long)R32(TRA), (unsigned long)R32(TEA),
         (unsigned long)R32(PTEH), (unsigned long)R32(PTEL), (unsigned long)R32(TTB), (unsigned long)R32(PTEA),
         (unsigned long)R32(MMUCR), (unsigned long)R32(QACR0), (unsigned long)R32(QACR1), R8(BASRA),
         (unsigned long)R32(BARA), R16(PMCR2));
  printf("MRST phase=%lu p4 ipra=%04x iprb=%04x iprc=%04x icr=%04x tstr=%02x tcr0=%04x tcor0=%08lx tcor1=%08lx "
         "scsmr2=%04x scbrr2=%02x scsmr1=%02x scbrr1=%02x stbcr2=%02x\n", (unsigned long)ph,
         R16(IPRA), R16(IPRB), R16(IPRC), R16(ICR), R8(TSTR), R16(TCR0), (unsigned long)R32(TCOR0),
         (unsigned long)R32(TCOR1), R16(SCSMR2), R8(SCBRR2), R8(SCSMR1), R8(SCBRR1), R8(STBCR2));
  printf("MRST phase=%lu tlb", (unsigned long)ph);
  for (int e = 0; e < 2; e++)
    printf(" u%d=%08lx/%08lx", e, (unsigned long)R32(UTLB_AA(e)), (unsigned long)R32(UTLB_DA1(e)));
  for (int e = 0; e < 2; e++)
    printf(" i%d=%08lx/%08lx", e, (unsigned long)R32(ITLB_AA(e)), (unsigned long)R32(ITLB_DA1(e)));
  printf("\n");
}

int main(void) {
  const uint32_t ph = g_phase;
  printf("mrst: phase %lu\n", (unsigned long)ph);
  g_resume = 0;
  if (ph > 0) report(ph);
  if (ph >= 5) { printf("mrst: done\n"); return 0; }
  phase_set(ph + 1);
  setup(ph);
  hard_clear();
  R32(PTEH) = 0x00000033u;                               /* ASID 0x33 */
  R32(P1(PA_C)) = 0x0009000Bu;                            /* rts ; nop */
  R32(P1(PA_D) + 0x40u) = 0x11112222u;
  switch (ph) {
  case 0:
    utlb_set(0, VA_D, PA_D, SZ4K, 3, 1, 1, 0, 0x33);
    utlb_set(1, VA_D, PA_D + 0x1000u, SZ4K, 3, 1, 1, 0, 0x33);
    mmucr_set(M_AT | M_URB(8) | M_URC(3));
    (void)R32(VA_D + 0x40u);
    break;
  case 1:
    itlb_set(0, VA_C, PA_C, SZ4K, 3, 1, 0, 0x33);
    itlb_set(1, VA_C, PA_C, SZ4K, 3, 1, 0, 0x33);
    mmucr_set(M_AT | M_SQMD);
    call_va(VA_C, 0, 0, 0);
    break;
  case 2:
    utlb_set(0, VA_C, PA_C, SZ4K, 3, 1, 1, 0, 0x33);
    utlb_set(5, VA_C, PA_C, SZ4K, 3, 1, 1, 0, 0x33);
    mmucr_set(M_AT | M_URC(9));
    call_va(VA_C, 0, 0, 0);
    break;
  case 3:
    utlb_set(0, VA_D, PA_D, SZ4K, 3, 1, 1, 0, 0x33);
    mmucr_set(M_AT | M_URC(4));
    ld_bl1(VA_D + 0x2000u);                              /* unmapped: TLB miss with BL = 1 */
    break;
  case 4:
    mmucr_set(M_URC(2));
    st_ill_bl1((volatile uint32_t *)MMUCR, M_SQMD | M_URB(9) | M_URC(7), (volatile uint32_t *)PTEH, 0x5555A477u);
    break;
  }
  printf("mrst: phase %lu: no reset\n", (unsigned long)ph);
  return 1;
}
