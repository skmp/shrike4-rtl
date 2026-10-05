/* shim/arch/timer.h -- milliseconds from the board's time register (board.md 3.10: Icyc at 0x18000008 / C).  The
 * simulated clock runs BM_ICYC_PER_MS Icyc per "millisecond" (bmrt.c; 2000 by default, 100 x fast) so that the
 * programs' second-long waits stay short; every line that depends on them is excluded (expected JSON). */
#ifndef BM_TIMER_H
#define BM_TIMER_H
#include <stdint.h>
uint64_t timer_ms_gettime64(void);
#endif
