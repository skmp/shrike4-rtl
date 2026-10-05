/* M4-fix (WI-SOC): M4-verify's run-time (agents/reports/integration/M4-verify.md), the M4 gate's regression test
 * since M4-fix (tb/mk/v1_soc.mk v1-soc-m4-sub; the verdict: m4/check.py --progs). */
/* m4v.h -- shared definitions of the M4 verification's programs (agents/verify/M4/adv/src; report
 * agents/reports/integration/M4-verify.md).  Run-time: m4vcrt0.S (tb/bm/serial/crt0.S + the early hook m4v_early),
 * tb/bm/serial/bmrt.c (BM_M1B_RT: a catch-all handler, IMASK 15 at main), tb/bm/soc-adv/m1b/m1b.h.  Register addresses:
 * hm (the SH7750 hardware manual); Holly: board.md 3 / 4.1.
 *
 * Persistent state lives in P2 RAM outside the image (the program is at 0x8C010000, the stack below 0x8D000000):
 *   CAP   0xACE00000  what m4v_early captured at this boot's start (before crt0 touches CCR)
 *   WATCH 0xACE01000  the cache lines the next boot's m4v_early captures (the program sets it before a reset)
 *   PER   0xACE02000  the program's own words (scenario, old / new patterns, timestamps)
 * RAM is 0 at time 0, so CAP->magic tells the first boot from a re-boot. */
#pragma once
#include "m1b.h"

#define CCR      0xFF00001Cu
#define PTEL     0xFF000004u
#define TTB      0xFF000008u
#define TEA      0xFF00000Cu
#define MMUCR    0xFF000010u
#define QACR0    0xFF000038u
#define QACR1    0xFF00003Cu
#define PMCR2    0xFF000088u
#define PMCTR1H  0xFF100004u
#define PMCTR2H  0xFF10000Cu
#define PMCTR2L  0xFF100010u
#define RFCR     0xFF800028u
#define PCTRB    0xFF800040u
#define PDTRB    0xFF800044u
/* the cache and TLB arrays (hm 4.5, 3.7): address / data, entry in the given address bits */
#define IC_ADDR(i)   (0xF0000000u | ((uint32_t)(i) << 5))        /* 256 entries, [12:5] */
#define OC_ADDR(i)   (0xF4000000u | ((uint32_t)(i) << 5))        /* 512 entries, [13:5] */
#define OC_DATA(i,w) (0xF5000000u | ((uint32_t)(i) << 5) | ((uint32_t)(w) << 2))
#define ITLB_A(i)    (0xF2000000u | ((uint32_t)(i) << 8))
#define ITLB_D(i)    (0xF3000000u | ((uint32_t)(i) << 8))
#define UTLB_A(i)    (0xF6000000u | ((uint32_t)(i) << 8))
#define UTLB_D(i)    (0xF7000000u | ((uint32_t)(i) << 8))
/* Holly (board.md 4.1), P2 */
#define ISTNRM   0xA05F6900u
#define ISTEXT   0xA05F6904u
#define ISTERR   0xA05F6908u
#define IML2NRM  0xA05F6910u
#define IML2EXT  0xA05F6914u
#define IML2ERR  0xA05F6918u
#define IML4NRM  0xA05F6920u
#define IML4EXT  0xA05F6924u
#define IML4ERR  0xA05F6928u
#define IML6NRM  0xA05F6930u
#define IML6EXT  0xA05F6934u
#define IML6ERR  0xA05F6938u
#define STARTRENDER 0xA05F8014u
#define SIM_EXIT 0xB8000000u
/* WDT (hm 10.7): WTCSR bits */
#define W_TME  0x80u
#define W_WTIT 0x40u
#define W_RSTS 0x20u
#define W_WOVF 0x10u
#define W_IOVF 0x08u

#define M4V_MAGIC 0x4D345631u                               /* "M4V1" */
typedef struct {
  uint32_t magic, boots;                                    /* boots: every entry, the first one 1 */
  uint32_t expevt, intevt, tra, tea, pteh, mmucr, ccr, qacr0, qacr1;
  uint32_t frqcr, stbcr, stbcr2, wtcsr, wtcnt;
  uint32_t pmcr1, pmcr2, pmctr1h, pmctr1l, pmctr2h, pmctr2l;
  uint32_t rfcr, pctrb, pdtrb, sr, early_pmc;
  uint32_t n_ocw, ocw_idx[8], oca[8], ocd[8][8];            /* OC watch: entry, address-array word, data words */
  uint32_t n_icw, icw_idx[8], ica[8];                       /* IC watch: entry, address-array word */
  uint32_t utlb_a[64], utlb_d[64], itlb_a[4], itlb_d[4];
} cap_t;
typedef struct { uint32_t n_ocw, ocw_idx[8], n_icw, icw_idx[8]; } watch_t;
#define CAP   ((volatile cap_t *)0xACE00000u)
#define WATCH ((volatile watch_t *)0xACE01000u)
#define PER   ((volatile uint32_t *)0xACE02000u)

void m4v_early(void);
/* arm the WDT (CKS 7: 4096 Icyc per count, v1_wdt) with WTCNT 0xFD and wait for the FE -> FF count, a regular tap
 * edge (the first count after TME may come early: v1_wdt's header); the overflow FF -> 00 then comes 4096 Icyc after
 * that edge.  csr: the mode bits (W_WTIT, W_RSTS); returns PMCTR1L right after the FF was seen. */
uint32_t wdt_sync(uint32_t csr);
void wdt_stop(void);
/* P2 alias of a P1 / physical address */
#define P2A(a) ((((uint32_t)(a)) & 0x1FFFFFFFu) | 0xA0000000u)
static inline uint32_t pmc2(void) { return R32(PMCTR2L); }
