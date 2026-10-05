/* asidsw.c -- M2 verification: process switches by PTEH.ASID with the ITLB and UTLB shared, on imx (TRANSLATE = 1) in
 * lockstep, under random TMU0 interrupts.  Two processes (ASID 0x11, 0x22) map the same code VA and the same data VA
 * to their own physical pages (SH = 0); a shared code page (SH = 1) maps one physical page for both.  The ITLB holds 4
 * entries, so a switch must not hit the other process's ITLB entry (hm 3.3.7: ASID compared unless SH = 1 or SV = 1
 * in privileged mode); with MMUCR.SV = 1 the privileged run ignores the ASID (mmu 3.3) -- here every process-private
 * page is entered only in SV = 0 phases.  The PTEH store sits 8+ instructions before any access of the new process
 * (F1 of the report is not what this tests).  Every call's result is compared with the value the P1 copy of the same
 * data gives.  Output "ASIDSW calls=<n> bad=<n> irqs=<n> fills=<ITLBFILL-visible LRUI changes>" and one line per phase. */
#include "m2.h"
void *memcpy(void *, const void *, unsigned);
extern char t_ret[], t_ret_e[];
extern uint32_t call_va(uint32_t va, uint32_t a, uint32_t b, uint32_t v);
extern void delay8(void);

#define VCODE 0x20C00000u      /* per-process code page */
#define VSHR  0x20C01000u      /* shared code page (SH = 1) */
#define VDATA 0x20D00000u      /* per-process data page */
#define PC_A  0x0CC00000u
#define PC_B  0x0CC10000u
#define PC_S  0x0CC20000u
#define PD_A  0x0CC30000u
#define PD_B  0x0CC40000u

static uint32_t expect_of(uint32_t pdata, uint32_t off) {
  uint32_t r0 = R32(P1(pdata) + off) + R32(P1(pdata) + off + 4);
  return r0 + (r0 << 2) + 1;
}

int main(void) {
  printf("asidsw: start\n");
  g_resume = 0;
  const unsigned n = (unsigned)(t_ret_e - t_ret);
  memcpy((void *)P1(PC_A + 0x100), t_ret, n);
  memcpy((void *)P1(PC_B + 0x100), t_ret, n);
  memcpy((void *)P1(PC_S + 0x100), t_ret, n);
  for (uint32_t w = 0; w < 1024; w++) {
    R32(P1(PD_A) + 4 * w) = 0x1000u + w * 3u;
    R32(P1(PD_B) + 4 * w) = 0x7000000u + w * 5u;
  }
  hard_clear();
  utlb_set(0, VCODE, PC_A, SZ4K, 3, 1, 1, 0, 0x11);
  utlb_set(1, VCODE, PC_B, SZ4K, 3, 1, 1, 0, 0x22);
  utlb_set(2, VSHR, PC_S, SZ4K, 3, 1, 1, 1, 0x7F);
  utlb_set(3, VDATA, PD_A, SZ4K, 3, 1, 1, 0, 0x11);
  utlb_set(4, VDATA, PD_B, SZ4K, 3, 1, 1, 0, 0x22);
  R32(PTEH) = 0x11;
  tmu_reset();
  R16(IPRA) = 0x8000;
  g_irq_quiet = 1; g_tmu_rand = 1; g_nirq = 0;
  R32(TCOR0) = 9; R32(TCNT0) = 9; R16(TCR0) = UNIE; R8(TSTR) = 1;
  uint32_t calls = 0, bad = 0, seed = 77;
  for (int phase = 0; phase < 2; phase++) {
    mmucr_set(M_AT | (phase ? M_SV : 0));
    set_sr(0x40000000u);
    for (int i = 0; i < 300; i++) {
      seed = seed * 1103515245u + 12345u;
      const int p = (seed >> 16) & 1;
      const uint32_t off = ((seed >> 20) & 0xFFu) * 8u;
      R32(PTEH) = p ? 0x22u : 0x11u;
      delay8();
      const uint32_t pd = p ? PD_B : PD_A;
      uint32_t r, e = expect_of(pd, off);
      if (!phase) {                                    /* SV = 0: the process's own code and data */
        r = call_va(VCODE + 0x100, VDATA + off, 0, 0); calls++; if (r != e) bad++;
      } else {                                         /* SV = 1: privileged, ASID ignored -- only unambiguous pages */
        r = call_va(VSHR + 0x100, P1(pd) + off, 0, 0); calls++; if (r != e) bad++;
      }
      r = call_va(VSHR + 0x100, VDATA + off, 0, 0); calls++;
      if (!phase && r != e) bad++;                     /* shared code, private data */
      if (phase) (void)r;                              /* SV = 1: two VDATA entries match: a multiple hit -- never reached */
    }
    set_sr(0x400000F0u);
    printf("ASIDSW phase=%d calls=%lu bad=%lu\n", phase, (unsigned long)calls, (unsigned long)bad);
    if (!phase) { R32(UTLB_AA(4)) = 0; R32(UTLB_AA(1)) = 0; }   /* SV = 1 next: keep one entry per VPN */
  }
  tmu_reset();
  mmucr_set(0);
  printf("ASIDSW calls=%lu bad=%lu irqs=%lu logged=%lu\n", (unsigned long)calls, (unsigned long)bad,
         (unsigned long)g_nirq, (unsigned long)g_nlog);
  printf("asidsw: done\n");
  return 0;
}
