/* cpureset.c -- M1a adversarial test: CPU-only resets (hm 5.5.3; SOC.md 2.3 "CPU-only (BL = 1)" column) through the
 * whole chip: v1_core's cpu_rst_req, v1_rstc, every unit's rst.cpu handling, the bridge's posted-write drain (SOC.md
 * 3.1.6: an acked posted write is performed; the Pclk landing comes after the reset starts), and the boot stub run
 * again from 0xA0000000.
 *   phase 0: every unit gets a distinctive state; TRA / INTEVT / TEA set; then SR.BL = 1, a posted IPRC store and an
 *            illegal instruction.
 *   phase 1: (after the reset) report; then BL = 1, a posted IPRB store and TRAPA.
 *   phase 2: report; then BL = 1, a posted TCOR2 longword store (lands at strobe + 12) and a misaligned load.
 *   phase 3: report; end.
 * Expected (SOC.md 2.3, CPU-only column; hm 5.5.3): EXPEVT = 0x020; TRA, INTEVT, TEA held; IPR, TMU (TSTR running,
 * TCOR / TCR / TOCR), MMU registers, UBC, PMCR2, STBCR2 held; the posted store performed; R0-R7 of both banks held
 * (the boot stub touches R8-R14 only). */
#include "adv.h"

extern void go_illegal(uint32_t, uint32_t, uint32_t);
extern void go_trapa(uint32_t, uint32_t, uint32_t);
extern void go_adrerr(uint32_t, uint32_t, uint32_t);
extern uint32_t get_sgr(void), get_dbr(void);
extern void do_trapa33(void), do_misaligned(void), do_misaligned_ret(void);

static const char *ename[36] = {"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "b1r0", "b1r1", "b1r2", "b1r3",
                                "b1r4", "b1r5", "b1r6", "b1r7", "sr", "gbr", "vbr", "ssr", "spc", "sgr", "dbr", "pr",
                                "mach", "macl", "fpul", "fpscr", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"};

static void report(int ph) {
  printf("RST phase=%d entry", ph);
  for (int i = 0; i < 16; i++) printf(" %s=%08lx", ename[i], (unsigned long)g_entry[i]);
  printf("\n");
  printf("RST phase=%d ccn expevt=%03lx intevt=%03lx tra=%03lx tea=%08lx pteh=%08lx ptel=%08lx ttb=%08lx ptea=%lx "
         "mmucr=%08lx ccr=%08lx qacr0=%lx qacr1=%lx basra=%02x pmcr1=%04x pmcr2=%04x\n", ph,
         (unsigned long)R32(EXPEVT), (unsigned long)R32(INTEVT), (unsigned long)R32(TRA), (unsigned long)R32(TEA),
         (unsigned long)R32(PTEH), (unsigned long)R32(PTEL), (unsigned long)R32(TTB), (unsigned long)R32(PTEA),
         (unsigned long)R32(MMUCR), (unsigned long)R32(CCR), (unsigned long)R32(QACR0), (unsigned long)R32(QACR1),
         R8(BASRA), R16(PMCR1), R16(PMCR2));
  printf("RST phase=%d p4 ipra=%04x iprb=%04x iprc=%04x icr=%04x tstr=%02x tocr=%02x tcr0=%04x tcr1=%04x tcr2=%04x "
         "tcor0=%08lx tcor1=%08lx tcor2=%08lx tcnt0=%08lx stbcr=%02x stbcr2=%02x bara=%08lx frqcr=%04x\n", ph,
         R16(IPRA), R16(IPRB), R16(IPRC), R16(ICR), R8(TSTR), R8(TOCR), R16(TCR0), R16(TCR1), R16(TCR2),
         (unsigned long)R32(TCOR0), (unsigned long)R32(TCOR1), (unsigned long)R32(TCOR2), (unsigned long)R32(TCNT0),
         R8(STBCR), R8(STBCR2), (unsigned long)R32(BARA), R16(FRQCR));
  const uint32_t a = R32(TCNT1), b = R32(TCNT1);
  printf("RST phase=%d misc sgr=%08lx tmu1_running=%d\n", ph, (unsigned long)get_sgr(), a != b);
}

int main(void) {
  const uint32_t ph = g_phase;
  if (ph == 0) {
    phase_set(1);
    printf("cpureset: phase 0\n");
    R16(IPRA) = 0x1230; R16(IPRB) = 0x4560; R16(IPRC) = 0x0000;
    tmu_reset();
    R8(TOCR) = 0x00;
    R32(TCOR0) = 0x0F0F0F0Fu; R32(TCNT0) = 0x55AA55AAu; R16(TCR0) = 0x0003;
    R32(TCOR1) = 0x12345678u; R32(TCNT1) = 0x7FFFFFFFu; R16(TCR1) = 0x0000;
    R32(TCOR2) = 0xCAFE0000u; R16(TCR2) = 0x0001;
    R8(TSTR) = 2;                                         /* TMU1 runs through every reset */
    R32(PTEH) = 0x12345C55u; R32(PTEL) = 0x0C0001FCu; R32(TTB) = 0x8C0FFFF0u; R32(PTEA) = 0x5;
    R32(MMUCR) = 0x00000200u;                             /* SQMD */
    R32(BARA) = 0x87654320u; R8(BASRA) = 0x5A;
    R16(PMCR2) = 0x0023;
    do_trapa33();                                         /* TRA = 0xCC, EXPEVT 0x160 */
    g_resume = (uint32_t)do_misaligned_ret; g_resume_sr = 0x400000F0u;
    do_misaligned();                                      /* TEA = 0x8C0F0001, EXPEVT 0x0E0 */
    g_resume = 0;
    R32(INTEVT) = 0x00000A60u;                            /* INTEVT is writable (hm 5.3.3) */
    printf("cpureset: phase 0 state set, nlog=%lu tea=%08lx tra=%03lx\n", (unsigned long)g_nlog,
           (unsigned long)R32(TEA), (unsigned long)R32(TRA));
    report(0);
    go_illegal(IPRC, 0x0A00, 0x500000F0u);               /* IPRC <- 0x0A00 (DMAC level 10), posted */
  } else if (ph == 1) {
    phase_set(2);
    report(1);
    go_trapa(IPRB, 0x7890, 0x500000F0u);                 /* IPRB <- 0x7890 (WDT 7, REF 8, SCI 9), posted */
  } else if (ph == 2) {
    phase_set(3);
    report(2);
    go_adrerr(TCOR2, 0xBEEF0123u, 0x500000F0u);          /* TCOR2 <- BEEF0123 (a Pclk longword) */
  } else {
    report(3);
    tmu_reset();
    R16(IPRA) = 0; R16(IPRB) = 0; R16(IPRC) = 0;
    printf("cpureset: done\n");
    return 0;
  }
  return 0x55;
}
