/* shim/dc/perfctr.h -- KOS 2.1's performance-counter calls, as kernel/arch/dreamcast/kernel/perfctr.c does them:
 * PMCR1 / PMCR2 at 0xFF000084 / 0xFF000086 (16 bit), RUN = 0xC000, CLR = 0x2000, the clock type at bit 8 */
#ifndef BM_PERFCTR_H
#define BM_PERFCTR_H
typedef enum { PRFC0, PRFC1 } perf_cntr_t;
typedef enum { PMCR_COUNT_CPU_CYCLES, PMCR_COUNT_RATIO_CYCLES } perf_cntr_clock_t;
typedef enum { PMCR_ELAPSED_TIME_MODE = 0x23 } perf_cntr_event_t;
void perf_cntr_start(perf_cntr_t counter, perf_cntr_event_t event, perf_cntr_clock_t clock);
void perf_cntr_stop(perf_cntr_t counter);
void perf_cntr_clear(perf_cntr_t counter);
void perf_cntr_timer_enable(void);
void perf_cntr_timer_disable(void);
#endif
