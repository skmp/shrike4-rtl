/* sleepwarm.c -- warm SLEEP wake-up probe for rtl/v1/SOC.md 2.5 / 8.3 M1b (WI-SOC): what the CPU counts, and how late
 * a TMU1 underflow is seen, across a SLEEP, with every trial repeated back to back (no printf until the end).
 *
 * timers-clock J / S (tc.c, tc2.c) and exceptions-intc C4 printed after every trial, so each one ran with the handler
 * and the trial code possibly cold.  Their counted cycles (PMCTR1L, elapsed-time mode, frozen while the CPU sleeps)
 * from the TSTR start to the handler were 48-88 on the console against 29-50 on the v1 SoC, and S's handler saw 2
 * TMU ticks after the underflow where the SoC sees 1.  This probe separates the parts:
 *   W  (warm J / S)  handler J reads PMCTR1L first, handler S reads TCNT1 first; TCNT1 = N = 100..103, no NOPs before
 *                    SLEEP; 1 warm-up + 8 trials each
 *   E  (entry)       handler J, TCNT1 = N = 0..11: the underflow sweeps across the SLEEP's entry, 16 Icyc per N; a
 *                    wake that comes before the CPU has stopped shows as counted cycles that still grow with N
 *   P  (phase)       handler J and S, N = 100, K = 0..7 NOPs between the TSTR start and the SLEEP
 * Per trial: flag (1 woke from SLEEP, SPC = SLEEP + 2; 2 the interrupt came before the SLEEP, which the handler then
 * skips, so a SLEEP never runs without a wake-up pending; 3 an exception), the counted cycles A -> the handler's PMCTR1L
 * read (hs), k = TMU1 ticks since the reload at the handler's TCNT1 read, SPC - SLEEP, and the counted cycles A -> the
 * PMCTR1L read after the SLEEP returns (af).
 * Safe on the console: only TMU1 (idle under KOS) and IPRA (saved / restored) are touched, TCR1.UNIE is cleared by the
 * handler, interrupts are masked outside each trial, and STBCR.STBY / STBCR2.DSLP are checked to be 0 (plain sleep).
 * Console: source /opt/toolchains/dc/kos/environ.sh && make sleepwarm.elf && ../hwrun.sh sleepwarm.elf
 * v1 SoC: tb/bm/timers-clock builds the same sources with -DBM (bmlib.c instead of KOS). */
#ifdef BM
#include "bmlib.h"
#else
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <arch/irq.h>
#define R8(a)  (*(volatile uint8_t  *)(a))
#define R16(a) (*(volatile uint16_t *)(a))
#define R32(a) (*(volatile uint32_t *)(a))
#endif

#define TSTR   0xFFD80004u
#define TCOR1  0xFFD80014u
#define TCNT1  0xFFD80018u
#define TCR1   0xFFD8001Cu
#define IPRA   0xFFD00004u
#define STBCR  0xFFC00004u
#define STBCR2 0xFFC00010u

typedef uint32_t (*trial_t)(uint32_t *rec, uint32_t tstr, uint32_t val, uint32_t vbr);
extern uint32_t sw_k0(uint32_t *, uint32_t, uint32_t, uint32_t), sw_k1(uint32_t *, uint32_t, uint32_t, uint32_t),
    sw_k2(uint32_t *, uint32_t, uint32_t, uint32_t), sw_k3(uint32_t *, uint32_t, uint32_t, uint32_t),
    sw_k4(uint32_t *, uint32_t, uint32_t, uint32_t), sw_k5(uint32_t *, uint32_t, uint32_t, uint32_t),
    sw_k6(uint32_t *, uint32_t, uint32_t, uint32_t), sw_k7(uint32_t *, uint32_t, uint32_t, uint32_t);
extern char sw_vbr_j[], sw_vbr_s[];
static const trial_t tk[8] = {sw_k0, sw_k1, sw_k2, sw_k3, sw_k4, sw_k5, sw_k6, sw_k7};

typedef struct { uint32_t flag, hs, k, af; int32_t spc; } res_t;
static uint32_t rec[16];

static void trial(int h, int k, uint32_t n, res_t *r) {
    irq_mask_t m = irq_disable();
    uint16_t ipra = R16(IPRA);
    R16(IPRA) = 0x0F00;                               /* TMU1 only, priority 15 */
    uint8_t ts = R8(TSTR) & ~2;
    R8(TSTR) = ts;
    R16(TCR1) = 0x0020;                               /* P/4, UNIE */
    R32(TCOR1) = 0xFFFFFFFF;
    R32(TCNT1) = n;
    memset(rec, 0, sizeof rec);
    tk[k](rec, TSTR, ts | 2, (uint32_t)(h ? sw_vbr_s : sw_vbr_j));
    R8(TSTR) = R8(TSTR) & ~2;
    R16(TCR1) = 0;
    (void)R16(TCR1);
    R16(IPRA) = ipra;
    irq_restore(m);
    r->flag = rec[0];
    r->hs = rec[1] - rec[4];
    r->k = 0xFFFFFFFFu - rec[9];
    r->spc = (int32_t)(rec[2] - rec[10]);
    r->af = rec[5] ? rec[5] - rec[4] : 0;
}

#define REPS 9                                         /* a warm-up, then 8 */
static res_t w[2][4][REPS], e[12][4], p[2][8][4];

static void pr(const res_t *r) { printf(" (%lu %lu %lu %ld %lu)", (unsigned long)r->flag, (unsigned long)r->hs,
                                        (unsigned long)r->k, (long)r->spc, (unsigned long)r->af); }

int main(void) {
#ifdef BM
    kos_timer_state();
#endif
    printf("sleepwarm: start\n");
    uint32_t sr;
    __asm__ volatile("stc sr,%0" : "=r"(sr));
    if (sr & 0x20000000u) { printf("sleepwarm: skipped: SR.RB = 1 (%08lx)\nsleepwarm: done\n", (unsigned long)sr); return 0; }
    if ((R8(STBCR) & 0x80) || (R8(STBCR2) & 0x80)) {
        printf("sleepwarm: skipped: STBCR %02x STBCR2 %02x\nsleepwarm: done\n", R8(STBCR), R8(STBCR2));
        return 0;
    }
    for (int h = 0; h < 2; h++)
        for (int n = 0; n < 4; n++)
            for (int t = 0; t < REPS; t++) trial(h, 0, 100 + n, &w[h][n][t]);
    for (int n = 0; n < 12; n++)
        for (int t = 0; t < 4; t++) trial(0, 0, n, &e[n][t]);
    for (int h = 0; h < 2; h++)
        for (int k = 0; k < 8; k++)
            for (int t = 0; t < 4; t++) trial(h, k, 100, &p[h][k][t]);
    printf("sleepwarm: (flag hs k spc-SLEEP af); hs = PMCTR1L A -> handler, k = TMU1 ticks since the reload at the "
           "handler's TCNT1 read, af = PMCTR1L A -> after the SLEEP; the first of each W row is the warm-up\n");
    for (int h = 0; h < 2; h++)
        for (int n = 0; n < 4; n++) {
            printf("W %c N=%d:", h ? 'S' : 'J', 100 + n);
            for (int t = 0; t < REPS; t++) pr(&w[h][n][t]);
            printf("\n");
        }
    for (int n = 0; n < 12; n++) {
        printf("E J N=%d:", n);
        for (int t = 0; t < 4; t++) pr(&e[n][t]);
        printf("\n");
    }
    for (int h = 0; h < 2; h++)
        for (int k = 0; k < 8; k++) {
            printf("P %c K=%d:", h ? 'S' : 'J', k);
            for (int t = 0; t < 4; t++) pr(&p[h][k][t]);
            printf("\n");
        }
    printf("sleepwarm: done\n");
    return 0;
}
