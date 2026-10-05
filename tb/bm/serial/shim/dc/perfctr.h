/* shim/dc/perfctr.h -- KOS's PRFC0 cycle timer: PMCR1 stopped, cleared, then 0xC023 (elapsed cycles, CPU clock) */
#ifndef BM_PERFCTR_H
#define BM_PERFCTR_H
void perf_cntr_timer_enable(void);
int perf_cntr_timer_enabled(void);
#endif
