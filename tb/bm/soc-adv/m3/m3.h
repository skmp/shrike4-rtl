/* m3.h -- shared declarations of the M3 verification's adversarial programs (tb/bm/soc-adv/m3, from agents/verify/M3/adv/src; run-time
 * m3rt.S, printf from tb/bm/soc-design/bm_rt.c).  Physical memory map the programs use (area 3, 16 MB at 0x0C000000):
 *   0x0C010000 ..           the image (text, data, bss, 64 KB stack above it)
 *   0x0C400000 .. 0x0C4FFFFF  the test region (TR): cached / uncached / SQ / DMA traffic
 *   0x0C600000 .. 0x0C6FFFFF  DMA sources / destinations (DR)
 *   0x0C7F0000 ..           the persistent block (G_*), always accessed through P2 (a CPU-only reset re-runs the boot
 *                           stub, whose CCR write invalidates the operand cache without a write-back) */
#pragma once
#include <stdint.h>
#include "stdio.h"

#define R32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define R16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define R8(a)  (*(volatile uint8_t *)(uintptr_t)(a))
#define R64(a) (*(volatile uint64_t *)(uintptr_t)(a))

#define CCR    0xFF00001Cu
#define TRA    0xFF000020u
#define EXPEVT 0xFF000024u
#define INTEVT 0xFF000028u
#define MMUCR  0xFF000010u
#define QACR0  0xFF000038u
#define QACR1  0xFF00003Cu
#define PMCR1  0xFF000084u
#define PMCR2  0xFF000088u
#define PMCTR1H 0xFF100004u
#define PMCTR1L 0xFF100008u
#define PMCTR2H 0xFF10000Cu
#define PMCTR2L 0xFF100010u
#define BCR1   0xFF800000u
#define BCR2   0xFF800004u
#define MCR    0xFF800014u
#define RTCSR  0xFF80001Cu
#define RTCNT  0xFF800020u
#define RTCOR  0xFF800024u
#define RFCR   0xFF800028u
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
#define SCFTDR2 0xFFE8000Cu
#define DMAC_BASE 0xFFA00000u
#define SAR(c)   (DMAC_BASE + (c) * 0x10u + 0x0u)
#define DAR(c)   (DMAC_BASE + (c) * 0x10u + 0x4u)
#define DMATCR(c) (DMAC_BASE + (c) * 0x10u + 0x8u)
#define CHCR(c)  (DMAC_BASE + (c) * 0x10u + 0xCu)
#define DMAOR    (DMAC_BASE + 0x40u)
#define CH_DM(x) ((uint32_t)(x) << 14)
#define CH_SM(x) ((uint32_t)(x) << 12)
#define CH_RS(x) ((uint32_t)(x) << 8)
#define CH_TM    (1u << 7)
#define CH_TS(x) ((uint32_t)(x) << 4)
#define CH_IE    (1u << 2)
#define CH_TE    (1u << 1)
#define CH_DE    1u

/* CCR bits */
#define CCR_OCE 0x001u
#define CCR_WT  0x002u
#define CCR_CB  0x004u
#define CCR_OCI 0x008u
#define CCR_ORA 0x020u
#define CCR_OIX 0x080u
#define CCR_ICE 0x100u
#define CCR_ICI 0x800u
#define CCR_IIX 0x8000u
#define CCR_KOS 0x0105u                 /* ICE OCE CB (0x090D less the ICI / OCI pulses) */

/* the arrays */
#define ICAA(e)  (0xF0000000u | ((uint32_t)(e) << 5))
#define ICDA(e, w) (0xF1000000u | ((uint32_t)(e) << 5) | ((uint32_t)(w) << 2))
#define OCAA(e)  (0xF4000000u | ((uint32_t)(e) << 5))
#define OCAA_ASSOC 0x8u
#define OCDA(e, w) (0xF5000000u | ((uint32_t)(e) << 5) | ((uint32_t)(w) << 2))

#define PHYS(p) ((uint32_t)(uintptr_t)(p) & 0x1FFFFFFFu)
#define P0(pa) ((uint32_t)(pa) & 0x1FFFFFFFu)
#define P1(pa) (((uint32_t)(pa) & 0x1FFFFFFFu) | 0x80000000u)
#define P2(pa) (((uint32_t)(pa) & 0x1FFFFFFFu) | 0xA0000000u)
#define P3(pa) (((uint32_t)(pa) & 0x1FFFFFFFu) | 0xC0000000u)
#define SQA(q, w) (0xE0000000u | ((uint32_t)(q) << 5) | ((uint32_t)(w) << 2))

#define TR_PA  0x0C400000u
#define DR_PA  0x0C600000u
#define G_PA   0x0C7F0000u
/* the persistent block (P2): g_entry[36] at +0, then the program's own words */
#define G_ENTRY(i) R32(P2(G_PA) + 4u * (uint32_t)(i))
#define G_W(i)     R32(P2(G_PA) + 0x100u + 4u * (uint32_t)(i))

#define UNF  0x0100
#define UNIE 0x0020

typedef struct { uint32_t kind, expevt, intevt, tra, tea, spc, ssr, r0, r1, pmc, pr, phase; } rec_t;
extern rec_t g_log[48];
extern volatile uint32_t g_nlog, g_resume, g_resume_sr, g_clr_unie, g_phase, g_skip, g_nirq;
extern volatile uint32_t g_irq_quiet, g_tmu_rand, g_seed, g_nexc, g_ndmte[4];

static inline void set_sr(uint32_t v) { __asm__ __volatile__("ldc %0,sr" : : "r"(v) : "memory"); }
static inline uint32_t get_sr(void) { uint32_t v; __asm__ __volatile__("stc sr,%0" : "=r"(v)); return v; }
static inline void imask(int m) { set_sr((get_sr() & ~0xF0u) | ((uint32_t)m << 4)); }
static inline uint32_t pmc(void) { return R32(PMCTR1L); }
static inline void ocbp(uint32_t a) { __asm__ __volatile__("ocbp @%0" : : "r"(a) : "memory"); }
static inline void ocbi(uint32_t a) { __asm__ __volatile__("ocbi @%0" : : "r"(a) : "memory"); }
static inline void ocbwb(uint32_t a) { __asm__ __volatile__("ocbwb @%0" : : "r"(a) : "memory"); }
static inline void pref(uint32_t a) { __asm__ __volatile__("pref @%0" : : "r"(a) : "memory"); }
static inline void movca(uint32_t a, uint32_t v) {
  register uint32_t r0 __asm__("r0") = v;
  __asm__ __volatile__("movca.l r0,@%0" : : "r"(a), "r"(r0) : "memory");
}
static inline uint32_t tas(uint32_t a) {   /* TAS.B @a: returns T (1: the byte was 0) */
  uint32_t t;
  __asm__ __volatile__("tas.b @%1\n\tmovt %0" : "=r"(t) : "r"(a) : "memory", "t");
  return t;
}
static inline void flush_range(uint32_t a, uint32_t n) {   /* OCBP every line of [a, a + n) */
  for (uint32_t x = a & ~31u; x < a + n; x += 32) ocbp(P1(x));
}
static inline void tmu_reset(void) {
  R8(TSTR) = 0;
  R16(TCR0) = 0; R16(TCR1) = 0; R16(TCR2) = 0;
  (void)R16(TCR2);
}
/* an LCG (the programs' own; not the handler's g_seed) */
static inline uint32_t lcg(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return *s >> 8; }

extern void ccr_set(uint32_t v);        /* m3util.S: CCR <- v from P2, then 8 NOPs in P2 (hm 4.2: CCR from P2) */
extern uint32_t ccr_get(void);
extern void p2_call(void (*f)(void));   /* calls f through its P2 alias */
