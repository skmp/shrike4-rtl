/* bmlib.h -- the few services the timers-clock probe ports need instead of KOS: console output through the testbench's
 * sim-control putchar (board.md 3.10), a small printf, irq_disable / irq_restore (SR.IMASK), memset. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#define R8(a)  (*(volatile uint8_t  *)(a))
#define R16(a) (*(volatile uint16_t *)(a))
#define R32(a) (*(volatile uint32_t *)(a))

typedef uint32_t irq_mask_t;
irq_mask_t irq_disable(void);          /* SR.IMASK = 15, returns the old SR */
void irq_restore(irq_mask_t m);
int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void *memset(void *d, int c, size_t n);

/* The state KOS 2.1.0 leaves in these units when a program's main() starts (timers-clock 5, run 1 line 0): the boot
 * stub has already written STBCR = 0x03 and PMCR1 = 0xC023 (SOC.md 7.3). */
void kos_timer_state(void);
