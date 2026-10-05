/* shim/dc/perfctr.h -- KOS's perf_cntr_timer_enable: PMCR1 (PRFC0) stopped, cleared, then 0xC023 (elapsed cycles) */
#ifndef BM_PERFCTR_H
#define BM_PERFCTR_H
void perf_cntr_timer_enable(void);
#endif
