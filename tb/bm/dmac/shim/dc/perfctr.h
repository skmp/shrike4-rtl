/* shim/dc/perfctr.h -- KOS 2.1's performance-counter API (kernel/arch/dreamcast/kernel/perfctr.c): PMCR1 / PMCR2 at
 * 0xFF000084 / 88, PMCTRnH / L at 0xFF100004 + 8n / 0xFF100008 + 8n. */
#ifndef BM_DC_PERFCTR_H
#define BM_DC_PERFCTR_H
#include <stdint.h>
typedef enum perf_cntr { PRFC0, PRFC1 } perf_cntr_t;
typedef enum perf_cntr_clock { PMCR_COUNT_CPU_CYCLES, PMCR_COUNT_RATIO_CYCLES } perf_cntr_clock_t;
typedef enum perf_cntr_event { PMCR_ELAPSED_TIME_MODE = 0x23 } perf_cntr_event_t;
void perf_cntr_start(perf_cntr_t counter, perf_cntr_event_t event, perf_cntr_clock_t clock);
void perf_cntr_stop(perf_cntr_t counter);
void perf_cntr_clear(perf_cntr_t counter);
uint64_t perf_cntr_count(perf_cntr_t counter);
#endif
