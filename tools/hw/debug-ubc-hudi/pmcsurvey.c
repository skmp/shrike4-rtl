/* pmcsurvey.c -- SOC.md open question 9.6 (WI-DBG): a hardware survey of the PMC events the debug-ubc-hudi runs never
 * exercised (spec debug-ubc-hudi 4.7, 10.4).  Exact counts per loop iteration through ubcbrk_s.s's _meas bracket
 * ((80-iteration call) - (16-iteration call) = 64 iterations; PMCR1 = cycles, PMCR2 = the event):
 *   Q  0x15 with the FPU instruction as the follower of a non-FPU leader (EX + FE, MT + FMOV) and as the leader;
 *      FTRV; 0x10 / 0x11 / 0x27 for RTE (against the same loop with JMP); 0x13 on STC.L / LDC.L GBR and MAC.W;
 *      0x0D / 0x09 / 0x02 for store-queue writes and an SQ PREF burst; 0x05 / 0x0F for OC write misses; 0x18 TRAPA
 *   M  0x03 / 0x07 for UTLB-miss exceptions (data read / write, instruction fetch) and for UTLB hits / ITLB refills,
 *      with MMUCR.AT = 1 around the measurement only.
 * Safety: interrupts masked during every measurement; the TLB handlers skip the faulting access (SPC + 2) or return
 * from the call (PC = PR), and give up (AT = 0, a flag) if anything unexpected happens, after which every P0 address
 * used is a harmless access to the same physical RAM (P0 = the physical address of our own buffer / RTS stub).  The
 * store-queue PREF writes into our own buffer (QACR0 = its area, restored after).  Restores MMUCR PTEH PTEL PTEA QACR0
 * QACR1, the 0x040 / 0x060 handlers, the TRAPA #0x40 handler, PMCR1 (KOS timer) and PMCR2 = 0. */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <dc/perfctr.h>
#include <arch/trap.h>

#define R32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define R16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define PMCR1 0xFF000084u
#define PMCR2 0xFF000088u
#define PTEH 0xFF000000u
#define PTEL 0xFF000004u
#define MMUCR 0xFF000010u
#define PTEA 0xFF000034u
#define QACR0 0xFF000038u
#define QACR1 0xFF00003Cu
#define A(x) ((uint32_t)(uintptr_t)(x))
#define PHYS(x) (A(x) & 0x1FFFFFFFu)

extern void meas(void *fn, uint32_t n, uint32_t a, uint32_t *io);
extern char b_base[], s_fpfollow[], s_fplead[], s_fpalone[], s_fmovfollow[], s_fmovlead[], s_ftrv[], s_rte[],
    s_rtejmp[], s_stcl[], s_macw[], s_sqw[], s_sqpref[], s_stmiss[], s_trapa[], s_dmiss_rd[], s_dmiss_wr[], s_imiss[],
    s_istub[];
extern uint32_t mmu_on(uint32_t pteh, uint32_t ptel);
extern void mmu_off(uint32_t old);

volatile uint32_t ubc_rec[9];                                  /* ubcbrk_s.s's DBR handler record (unused here) */
static uint32_t dbuf[1024] __attribute__((aligned(4096)));     /* the TLB tests' data page */
static uint8_t wbuf[32768 + 64] __attribute__((aligned(32)));  /* OC write misses; the SQ PREF target */
static int16_t macbuf[16] __attribute__((aligned(4)));

static void ev_exact(void *fn, uint32_t a, uint32_t b, int32_t *de, int32_t *dc) {
    uint32_t o1[3] = {0, 0, b}, o2[3] = {0, 0, b};
    meas(fn, 80, a, o1);
    meas(fn, 16, a, o1);
    meas(fn, 80, a, o2);
    *de = (int32_t)(o2[0] - o1[0]);
    *dc = (int32_t)(o2[1] - o1[1]);
}
static void pm_set(uint16_t ev) {
    R16(PMCR1) = 0x0023; R16(PMCR1) = 0x2023; R16(PMCR1) = 0xC023;
    R16(PMCR2) = ev; R16(PMCR2) = 0x2000 | ev; R16(PMCR2) = 0xC000 | ev;
}

/* ---- handlers */
static volatile uint32_t h_n, h_bad, t_n, i_target;
static void dmiss_hnd(irq_t code, irq_context_t *ctx, void *d) {
    (void)d;
    h_n++;
    if (h_n > 200000 || (code != 0x040 && code != 0x060)) { h_bad++; R32(MMUCR) = 4; return; }   /* give up: AT = 0 */
    ctx->pc += 2;                                                  /* skip the faulting access */
}
static void imiss_hnd(irq_t code, irq_context_t *ctx, void *d) {
    (void)d;
    h_n++;
    if (h_n > 200000 || code != 0x040 || ctx->pc != i_target) { h_bad++; R32(MMUCR) = 4; return; }
    ctx->pc = ctx->pr;                                             /* the jsr returns */
}
static void trapa_hnd(trapa_t code, irq_context_t *ctx, void *d) { (void)code; (void)ctx; (void)d; t_n++; }

static const uint16_t evs[] = {0x03, 0x07, 0x02, 0x05, 0x09, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x13, 0x14, 0x15, 0x16,
                               0x18, 0x22, 0x25, 0x27, 0x23};
#define NEV (sizeof(evs) / sizeof(evs[0]))

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("pmcsurvey: start\n");
    /* ---------------- Q: no MMU */
    {
        uint32_t sq = 0xE0000000u | (PHYS(wbuf) & 0x03FFFFE0u);
        uint32_t qarea = ((PHYS(wbuf) >> 26) & 7) << 2;
        void *f[15] = {b_base, s_fpfollow, s_fplead, s_fpalone, s_fmovfollow, s_fmovlead, s_ftrv, s_rte, s_rtejmp,
                       s_stcl, s_macw, s_sqw, s_sqpref, s_stmiss, s_trapa};
        uint32_t a5[15] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, A(&macbuf[0]), sq, sq, A(wbuf), 0};
        uint32_t a6[15] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, A(&macbuf[8]), 0, 0, A(wbuf) + 16384, 0};
        const char *nm[15] = {"base", "fpfollow", "fplead", "fpalone", "fmovfollow", "fmovlead", "ftrv", "rte",
                              "rtejmp", "stcl", "macw", "sqw", "sqpref", "stmiss", "trapa"};
        uint32_t q0 = R32(QACR0), q1 = R32(QACR1);
        trapa_handler oth; void *otd;
        oth = trapa_get_handler(0x40, &otd);
        trapa_set_handler(0x40, trapa_hnd, NULL);
        printf("Q: exact d(event) over 64 iterations (n=80 minus n=16); snippets");
        for(int s = 0; s < 15; s++) printf(" %s", nm[s]);
        printf("\n");
        for(unsigned e = 0; e < NEV; e++) {
            int32_t de[15], dc[15];
            irq_mask_t old = irq_disable();
            pm_set(evs[e]);
            R32(QACR0) = qarea; R32(QACR1) = qarea;
            for(int s = 0; s < 15; s++) ev_exact(f[s], a5[s], a6[s], &de[s], &dc[s]);
            R32(QACR0) = q0; R32(QACR1) = q1;
            irq_restore(old);
            printf("Q %02x ev", evs[e]); for(int s = 0; s < 15; s++) printf(" %ld", (long)de[s]);
            printf(" | cyc"); for(int s = 0; s < 15; s++) printf(" %ld", (long)dc[s]); printf("\n");
        }
        trapa_set_handler(0x40, oth, otd);
        printf("Q trapa handler calls %lu (expected %lu)\n", (unsigned long)t_n, (unsigned long)(NEV * 176));
    }
    /* ---------------- M: UTLB / ITLB misses with MMUCR.AT = 1 around each measurement */
    {
        uint32_t pteh0 = R32(PTEH), ptel0 = R32(PTEL), ptea0 = R32(PTEA);
        irq_cb_t h40 = irq_get_handler(0x040), h60 = irq_get_handler(0x060);
        uint32_t dva = PHYS(dbuf), iva = PHYS(s_istub);                  /* P0 = the physical address */
        uint32_t asid = pteh0 & 0xFF;
        /* 4 KB pages, V, PR = 11, C, D, SH: PTEL = PPN | 0x17E */
        struct { const char *n; void *f; uint32_t a; uint32_t pteh, ptel; int inst; } c[5] = {
            {"dmiss_rd", s_dmiss_rd, dva, 0, 0, 0},
            {"dmiss_wr", s_dmiss_wr, dva, 0, 0, 0},
            {"dhit_rd", s_dmiss_rd, dva, (dva & 0xFFFFF000u) | asid, (dva & 0x1FFFF000u) | 0x17E, 0},
            {"imiss", s_imiss, iva, 0, 0, 1},
            {"ihit", s_imiss, iva, (iva & 0xFFFFF000u) | asid, (iva & 0x1FFFF000u) | 0x17E, 1},
        };
        printf("M: exact d(event) over 64 iterations, MMUCR.AT = 1; snippets");
        for(int s = 0; s < 5; s++) printf(" %s", c[s].n);
        printf("\n");
        uint32_t hn[5] = {0, 0, 0, 0, 0}, hb = 0;
        for(unsigned e = 0; e < NEV; e++) {
            int32_t de[5], dc[5];
            irq_mask_t old = irq_disable();
            pm_set(evs[e]);
            for(int s = 0; s < 5; s++) {
                h_n = 0;
                i_target = c[s].a;
                irq_set_handler(0x040, c[s].inst ? imiss_hnd : dmiss_hnd, NULL);
                irq_set_handler(0x060, dmiss_hnd, NULL);
                uint32_t om = mmu_on(c[s].pteh, c[s].ptel);
                ev_exact(c[s].f, c[s].a, 0, &de[s], &dc[s]);
                mmu_off(om);
                irq_set_handler(0x040, h40.hdl, h40.data);
                irq_set_handler(0x060, h60.hdl, h60.data);
                R32(PTEH) = pteh0; R32(PTEL) = ptel0; R32(PTEA) = ptea0;
                hn[s] += h_n;
            }
            hb += h_bad;
            h_bad = 0;
            irq_restore(old);
            printf("M %02x ev", evs[e]); for(int s = 0; s < 5; s++) printf(" %ld", (long)de[s]);
            printf(" | cyc"); for(int s = 0; s < 5; s++) printf(" %ld", (long)dc[s]); printf("\n");
        }
        printf("M handler exceptions per snippet:"); for(int s = 0; s < 5; s++) printf(" %lu", (unsigned long)hn[s]);
        printf(" (a miss snippet: %lu = 176 per event) | gave up %lu\n", (unsigned long)(NEV * 176), (unsigned long)hb);
        printf("M now MMUCR %08lx PTEH %08lx PTEL %08lx PTEA %08lx\n", (unsigned long)R32(MMUCR),
               (unsigned long)R32(PTEH), (unsigned long)R32(PTEL), (unsigned long)R32(PTEA));
    }
    R16(PMCR2) = 0;
    perf_cntr_timer_enable();
    printf("pmcsurvey: done\n");
    return 0;
}
