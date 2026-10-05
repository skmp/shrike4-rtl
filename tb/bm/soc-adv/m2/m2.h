/* m2.h -- shared declarations of the M2 verification's adversarial programs (tb/bm/soc-adv/m2, the M2 gate's since
 * agents/reports/integration/M2-fix.md; run-time m2rt.S, printf from tb/bm/soc-design/bm_rt.c). */
#pragma once
#include <stdint.h>
#include "stdio.h"

#define R32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define R16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define R8(a)  (*(volatile uint8_t *)(uintptr_t)(a))

#define PTEH   0xFF000000u
#define PTEL   0xFF000004u
#define TTB    0xFF000008u
#define TEA    0xFF00000Cu
#define MMUCR  0xFF000010u
#define BASRA  0xFF000014u
#define BASRB  0xFF000018u
#define CCR    0xFF00001Cu
#define TRA    0xFF000020u
#define EXPEVT 0xFF000024u
#define INTEVT 0xFF000028u
#define PTEA   0xFF000034u
#define QACR0  0xFF000038u
#define QACR1  0xFF00003Cu
#define PMCR1  0xFF000084u
#define PMCR2  0xFF000088u
#define PMCTR1L 0xFF100008u
#define BARA   0xFF200000u
#define BAMRA  0xFF200004u
#define BBRA   0xFF200008u
#define BARB   0xFF20000Cu
#define BAMRB  0xFF200010u
#define BBRB   0xFF200014u
#define BRCR   0xFF200020u
#define STBCR  0xFFC00004u
#define STBCR2 0xFFC00010u
#define ICR    0xFFD00000u
#define IPRA   0xFFD00004u
#define IPRB   0xFFD00008u
#define IPRC   0xFFD0000Cu
#define TOCR   0xFFD80000u
#define TSTR   0xFFD80004u
#define TCOR0  0xFFD80008u
#define TCNT0  0xFFD8000Cu
#define TCR0   0xFFD80010u
#define TCOR1  0xFFD80014u
#define TCNT1  0xFFD80018u
#define TCR1   0xFFD8001Cu
#define TCOR2  0xFFD80020u
#define TCNT2  0xFFD80024u
#define TCR2   0xFFD80028u
#define SCSMR2 0xFFE80000u
#define SCBRR2 0xFFE80004u
#define SCSCR2 0xFFE80008u
#define SCSMR1 0xFFE00000u
#define SCBRR1 0xFFE00004u
#define WTCSR  0xFFC0000Cu

#define ITLB_AA(e)  (0xF2000000u | ((uint32_t)(e) << 8))
#define ITLB_DA1(e) (0xF3000000u | ((uint32_t)(e) << 8))
#define ITLB_DA2(e) (0xF3800000u | ((uint32_t)(e) << 8))
#define UTLB_AA(e)  (0xF6000000u | ((uint32_t)(e) << 8))
#define UTLB_ASSOC  0xF6000080u
#define UTLB_DA1(e) (0xF7000000u | ((uint32_t)(e) << 8))
#define UTLB_DA2(e) (0xF7800000u | ((uint32_t)(e) << 8))

#define M_AT      1u
#define M_TI      4u
#define M_SV      0x100u
#define M_SQMD    0x200u
#define M_URC(x)  (((uint32_t)(x) & 63u) << 10)
#define M_URB(x)  (((uint32_t)(x) & 63u) << 18)
#define G_URC(m)  (((m) >> 10) & 63u)

#ifndef P1
#define P1(pa) ((uint32_t)(pa) | 0x80000000u)
#endif

#define UNF  0x0100
#define UNIE 0x0020

/* the exception record of m2rt.S */
typedef struct { uint32_t kind, expevt, intevt, tra, tea, spc, ssr, pteh, mmucr, pmc, pr, phase; } rec_t;
typedef struct { uint32_t vpn, mask, ptel; } pte_t;
extern uint32_t g_entry[36];
extern rec_t g_log[48];
extern volatile uint32_t g_nlog, g_resume, g_resume_sr, g_clr_unie, g_phase, g_refill, g_nrefill, g_npt, g_nirq;
extern volatile uint32_t g_irq_quiet, g_tmu_rand, g_seed, g_nexc;
extern pte_t *volatile g_pt;
extern volatile uint32_t g_scratch[64];

static inline void set_sr(uint32_t v) { __asm__ __volatile__("ldc %0,sr" : : "r"(v) : "memory"); }
static inline uint32_t get_sr(void) { uint32_t v; __asm__ __volatile__("stc sr,%0" : "=r"(v)); return v; }
static inline void clrlog(void) { g_nlog = 0; }
static inline void tmu_reset(void) {
  R8(TSTR) = 0;
  R16(TCR0) = 0; R16(TCR1) = 0; R16(TCR2) = 0;
  (void)R16(TCR2);
}

/* page attributes (PTEL / UTLB DA1 layout): V 0x100, SZ1 0x80, PR 0x60, SZ0 0x10, C 0x08, D 0x04, SH 0x02, WT 0x01 */
enum { SZ1K = 0, SZ4K = 1, SZ64K = 2, SZ1M = 3 };
static const uint32_t szmask[4] = {0xFFFFFC00u, 0xFFFFF000u, 0xFFFF0000u, 0xFFF00000u};
static inline uint32_t ptel_of(uint32_t pa, int sz, int pr, int c, int d, int sh) {
  return (pa & 0x1FFFFC00u) | 0x100u | (((uint32_t)sz & 2u) << 6) | (((uint32_t)sz & 1u) << 4) | ((uint32_t)pr << 5) |
         ((uint32_t)c << 3) | ((uint32_t)d << 2) | ((uint32_t)sh << 1);
}
static inline void utlb_set(int e, uint32_t va, uint32_t pa, int sz, int pr, int c, int d, int sh, int asid) {
  R32(UTLB_DA2(e)) = 0;
  R32(UTLB_DA1(e)) = ptel_of(pa, sz, pr, c, d, sh);
  R32(UTLB_AA(e)) = (va & 0xFFFFFC00u) | ((uint32_t)d << 9) | (1u << 8) | ((uint32_t)asid & 0xFFu);
}
static inline void itlb_set(int e, uint32_t va, uint32_t pa, int sz, int pr, int c, int sh, int asid) {
  R32(ITLB_DA2(e)) = 0;
  R32(ITLB_DA1(e)) = ptel_of(pa, sz, pr, c, 0, sh);
  R32(ITLB_AA(e)) = (va & 0xFFFFFC00u) | (1u << 8) | ((uint32_t)asid & 0xFFu);
}
extern void mmucr_set(uint32_t v);        /* m2util.S: the store, then 8 NOPs (hm 3.2.x: 4 / 8 instructions) */
/* the persistent phase across a reset (M3-fix, M3-verify F10): written back at once, since the boot stub's CCR write
 * after the reset (--ccr 0x090D: OCI) drops a dirty line on the real memory (a no-op for the data on imx) */
static inline void phase_set(uint32_t v) { g_phase = v; __asm__ __volatile__("ocbwb @%0" : : "r"(&g_phase) : "memory"); }
/* the lines of [p, p + n) written back (OCBWB) / written back and invalidated (OCBP) */
static inline void ocbwb_range(const volatile void *p, uint32_t n) {
  for (uint32_t a = (uint32_t)(uintptr_t)p & ~31u; a < (uint32_t)(uintptr_t)p + n; a += 32)
    __asm__ __volatile__("ocbwb @%0" : : "r"(a) : "memory");
}
static inline void hard_clear(void) {
  for (int e = 0; e < 64; e++) { R32(UTLB_AA(e)) = 0; R32(UTLB_DA1(e)) = 0; R32(UTLB_DA2(e)) = 0; }
  for (int e = 0; e < 4; e++) { R32(ITLB_AA(e)) = 0; R32(ITLB_DA1(e)) = 0; R32(ITLB_DA2(e)) = 0; }
  mmucr_set(M_TI);
  mmucr_set(0);
}
static inline void plog(const char *tag) {
  printf("%s n=%lu", tag, (unsigned long)g_nlog);
  for (uint32_t i = 0; i < g_nlog; i++)
    printf(" [%lu %03lx tea=%08lx spc=%08lx pteh=%08lx]", (unsigned long)g_log[i].kind,
           (unsigned long)(g_log[i].kind == 3 ? g_log[i].intevt : g_log[i].expevt), (unsigned long)g_log[i].tea,
           (unsigned long)g_log[i].spc, (unsigned long)g_log[i].pteh);
  printf("\n");
}
