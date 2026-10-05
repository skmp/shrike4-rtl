/* dc/perfctr.h -- KOS 2.1's performance-counter API (kernel/arch/dreamcast/kernel/perfctr.c), implemented the same
 * way in ../../bm_rt.c: PMCR1 / PMCR2 at 0xFF000084 / 0xFF000088 (16-bit), PMCTRnH / L at 0xFF100004 + 8n / 0xFF100008
 * + 8n.  PRFC0 is PMCR1 (KOS's elapsed-time timer), PRFC1 is PMCR2. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef enum perf_cntr { PRFC0, PRFC1 } perf_cntr_t;
typedef enum perf_cntr_clock { PMCR_COUNT_CPU_CYCLES, PMCR_COUNT_RATIO_CYCLES } perf_cntr_clock_t;
typedef enum perf_cntr_event {
  PMCR_INIT_NO_MODE = 0x00, PMCR_INSTRUCTION_ISSUED_MODE = 0x13, PMCR_PARALLEL_INSTRUCTION_ISSUED_MODE = 0x14,
  PMCR_ELAPSED_TIME_MODE = 0x23
} perf_cntr_event_t;
void perf_cntr_start(perf_cntr_t counter, perf_cntr_event_t event, perf_cntr_clock_t clock);
void perf_cntr_stop(perf_cntr_t counter);
void perf_cntr_resume(perf_cntr_t counter);
void perf_cntr_clear(perf_cntr_t counter);
uint64_t perf_cntr_count(perf_cntr_t counter);
void perf_cntr_timer_enable(void);
void perf_cntr_timer_disable(void);
bool perf_cntr_timer_enabled(void);
