/* m1b.h -- shared definitions of the M1b programs of tb/bm/soc-adv/m1b (the M1b independent verification's adversarial
 * programs, agents/reports/integration/M1b-verify.md, adopted as the M1b gate's regression tests by M1b-fix.md).
 * Run-time: tb/bm/serial's crt0.S / bmrt.c (built with BM_M1B_RT: a catch-all handler, IMASK 15 at main) / shim (a C
 * handler table by INTEVT / EXPEVT, printf to the sim-control port).  Register addresses: hm (the SH7750 hardware
 * manual). */
#pragma once
#include <stdint.h>
#include "kos.h"

#define R32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define R16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define R8(a)  (*(volatile uint8_t *)(uintptr_t)(a))

/* CCN */
#define PTEH    0xFF000000u
#define BASRA   0xFF000014u
#define BASRB   0xFF000018u
#define TRA     0xFF000020u
#define EXPEVT  0xFF000024u
#define INTEVT  0xFF000028u
#define PMCR1   0xFF000084u
#define PMCTR1L 0xFF100008u
/* UBC */
#define BARA    0xFF200000u
#define BAMRA   0xFF200004u
#define BBRA    0xFF200008u
#define BARB    0xFF20000Cu
#define BAMRB   0xFF200010u
#define BBRB    0xFF200014u
#define BDRB    0xFF200018u
#define BDMRB   0xFF20001Cu
#define BRCR    0xFF200020u
/* REF */
#define RTCSR   0xFF80001Cu
#define RTCNT   0xFF800020u
#define RTCOR   0xFF800024u
/* DMAC */
#define SAR(n)    (0xFFA00000u + 0x10u * (n))
#define DAR(n)    (0xFFA00004u + 0x10u * (n))
#define DMATCR(n) (0xFFA00008u + 0x10u * (n))
#define CHCR(n)   (0xFFA0000Cu + 0x10u * (n))
#define DMAOR   0xFFA00040u
/* CPG / WDT */
#define FRQCR   0xFFC00000u
#define STBCR   0xFFC00004u
#define WTCNT   0xFFC00008u
#define WTCSR   0xFFC0000Cu
#define STBCR2  0xFFC00010u
/* RTC */
#define R64CNT  0xFFC80000u
#define RSECCNT 0xFFC80004u
#define RMINCNT 0xFFC80008u
#define RHRCNT  0xFFC8000Cu
#define RWKCNT  0xFFC80010u
#define RDAYCNT 0xFFC80014u
#define RMONCNT 0xFFC80018u
#define RYRCNT  0xFFC8001Cu
#define RSECAR  0xFFC80020u
#define RMINAR  0xFFC80024u
#define RHRAR   0xFFC80028u
#define RWKAR   0xFFC8002Cu
#define RDAYAR  0xFFC80030u
#define RMONAR  0xFFC80034u
#define RCR1    0xFFC80038u
#define RCR2    0xFFC8003Cu
/* INTC */
#define ICR     0xFFD00000u
#define IPRA    0xFFD00004u
#define IPRB    0xFFD00008u
#define IPRC    0xFFD0000Cu
/* TMU */
#define TOCR    0xFFD80000u
#define TSTR    0xFFD80004u
#define TCOR(n) (0xFFD80008u + 0xCu * (n))
#define TCNT(n) (0xFFD8000Cu + 0xCu * (n))
#define TCR(n)  (0xFFD80010u + 0xCu * (n))
/* SCI */
#define SCSMR1  0xFFE00000u
#define SCBRR1  0xFFE00004u
#define SCSCR1  0xFFE00008u
#define SCTDR1  0xFFE0000Cu
#define SCSSR1  0xFFE00010u
#define SCRDR1  0xFFE00014u
#define SCSCMR1 0xFFE00018u
#define SCSPTR1 0xFFE0001Cu
/* SCIF */
#define SCSMR2  0xFFE80000u
#define SCBRR2  0xFFE80004u
#define SCSCR2  0xFFE80008u
#define SCFTDR2 0xFFE8000Cu
#define SCFSR2  0xFFE80010u
#define SCFRDR2 0xFFE80014u
#define SCFCR2  0xFFE80018u
#define SCFDR2  0xFFE8001Cu
#define SCSPTR2 0xFFE80020u
#define SCLSR2  0xFFE80024u
/* H-UDI */
#define SDIR    0xFFF00000u
#define SDDR    0xFFF00008u

#define UNF  0x0100
#define UNIE 0x0020

static inline void set_sr(uint32_t v) { __asm__ __volatile__("ldc %0,sr" : : "r"(v) : "memory"); }
static inline uint32_t get_sr(void) { uint32_t v; __asm__ __volatile__("stc sr,%0" : "=r"(v)); return v; }
static inline void imask(unsigned m) { set_sr((get_sr() & ~0xF0u) | ((m & 15u) << 4)); }
static inline uint32_t pmc(void) { return R32(PMCTR1L); }
static inline void spin(int n) { for (volatile int i = 0; i < n; i++) ; }
/* a DMA buffer's lines written back and invalidated (OCBP) before the DMAC reads or writes it (M3-fix, M3-verify F10:
 * the DMAC does not snoop the copy-back operand cache on the real memory; a no-op for the data on im) */
static inline void ocbp_range(const volatile void *p, uint32_t n) {
  for (uint32_t a = (uint32_t)(uintptr_t)p & ~31u; a < (uint32_t)(uintptr_t)p + n; a += 32)
    __asm__ __volatile__("ocbp @%0" : : "r"(a) : "memory");
}

extern irq_handler bm_default;             /* bmrt.c: the catch-all handler (a code no entry names) */

/* the entry log every handler appends to */
typedef struct { uint32_t code, spc, ssr, a, b; } ent_t;
#define NLOG 64
extern ent_t g_ent[NLOG];
extern volatile int g_nent;
static inline void logent(uint32_t code, uint32_t spc, uint32_t ssr, uint32_t a, uint32_t b) {
    if (g_nent < NLOG) { ent_t *e = &g_ent[g_nent]; e->code = code; e->spc = spc; e->ssr = ssr; e->a = a; e->b = b; }
    g_nent++;
}

/* TMU: stop every channel, clear the flags */
static inline void tmu_reset(void) {
    R8(TSTR) = 0;
    R16(TCR(0)) = 0; R16(TCR(1)) = 0; R16(TCR(2)) = 0;
    (void)R16(TCR(2));
}
/* SCIF loopback at bit rate N (CKS 0): FIFOs reset, 8N1, TE RE, LOOP; scr_extra OR-ed into SCSCR2 */
static inline void scif_loop(unsigned brr, unsigned scr_extra, unsigned fcr_trg) {
    R16(SCSCR2) = 0;
    R16(SCFCR2) = 0x0006;                   /* TFRST RFRST */
    R16(SCSMR2) = 0;
    R8(SCBRR2) = (uint8_t)brr;
    R16(SCFCR2) = (uint16_t)(0x0001 | fcr_trg);   /* LOOP, triggers */
    (void)R16(SCFSR2);
    R16(SCFSR2) = 0;                        /* clear the flags that read 1 (TEND / TDFE stay: they re-set) */
    R16(SCLSR2) = 0;
    R16(SCSCR2) = (uint16_t)(0x0030 | scr_extra);
}
