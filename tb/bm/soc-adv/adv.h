/* adv.h -- shared declarations of the SoC adversarial bare-metal programs (tb/bm/soc-adv). */
#pragma once
#include <stdint.h>
#include "stdio.h"

#define R32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define R16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define R8(a)  (*(volatile uint8_t *)(uintptr_t)(a))

/* registers */
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
#define PVR    0xFF000030u
#define PTEA   0xFF000034u
#define QACR0  0xFF000038u
#define QACR1  0xFF00003Cu
#define PMCR1  0xFF000084u
#define PMCR2  0xFF000088u
#define PMCTR1L 0xFF100008u
#define PMCTR2H 0xFF10000Cu
#define PMCTR2L 0xFF100010u
#define BARA   0xFF200000u
#define BAMRA  0xFF200004u
#define BBRA   0xFF200008u
#define BRCR   0xFF200020u
#define RTCSR  0xFF80001Cu
#define RTCNT  0xFF800020u
#define RTCOR  0xFF800024u
#define FRQCR  0xFFC00000u
#define STBCR  0xFFC00004u
#define STBCR2 0xFFC00010u
#define WTCNT  0xFFC00008u
#define WTCSR  0xFFC0000Cu
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
#define TCPR2  0xFFD8002Cu
#define SCSCR1 0xFFE00008u
#define SCSCR2 0xFFE80008u

#define UNF  0x0100
#define UNIE 0x0020

typedef struct { uint32_t kind, expevt, intevt, tra, tea, spc, ssr, pmc; } rec_t;
extern uint32_t g_entry[36];
extern rec_t g_log[48];
extern volatile uint32_t g_nlog, g_resume, g_resume_sr, g_clr_unie, g_noreadback, g_phase;
extern volatile uint32_t g_scratch[64];

static inline void set_sr(uint32_t v) { __asm__ __volatile__("ldc %0,sr" : : "r"(v) : "memory"); }
static inline uint32_t get_sr(void) { uint32_t v; __asm__ __volatile__("stc sr,%0" : "=r"(v)); return v; }
static inline uint32_t pmc(void) { return R32(PMCTR1L); }
static inline void spin(int n) { for (volatile int i = 0; i < n; i++) ; }
static inline void clrlog(void) { g_nlog = 0; }
/* the persistent phase across a CPU-only reset (M3-fix, M3-verify F10): g_phase lives in cached .data, and the boot
 * stub's CCR write after the reset (--ccr 0x090D: OCI) drops a dirty line, so on the real memory (rm) the new value is
 * written back at once (OCBWB; a no-op for the data on im) */
static inline void phase_set(uint32_t v) { g_phase = v; __asm__ __volatile__("ocbwb @%0" : : "r"(&g_phase) : "memory"); }
/* stop every TMU channel, clear their flags, UNIE off */
static inline void tmu_reset(void) {
  R8(TSTR) = 0;
  R16(TCR0) = 0; R16(TCR1) = 0; R16(TCR2) = 0;
  (void)R16(TCR2);
}
