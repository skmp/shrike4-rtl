/* shim/kos.h -- the part of KallistiOS's API the debug-ubc-hudi console programs use, for a bare-metal build on the v1
 * SoC testbench (WI-DBG; see ../README.md).  The programs (tools/hw/debug-ubc-hudi/{p4probe,ubcbrk,ubcevt}.c) compile
 * unmodified against it: printf / sprintf (bmrt.c, to the sim-control putchar port), the exception handler table
 * (irq_set_handler / irq_get_handler, dispatched by crt0.S's VBR table), irq_disable / irq_restore (SR.IMASK),
 * perf_cntr_timer_enable and timer_ms_gettime64 (the board's time register). */
#ifndef BM_KOS_H
#define BM_KOS_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t irq_t;                    /* the exception code (EXPEVT, or INTEVT for interrupts) */
typedef struct {                           /* crt0.S saves exactly this; the programs use pc, sr and r[9] */
    uint32_t r[16];
    uint32_t pc, pr, sr, gbr, mach, macl;
} irq_context_t;
typedef void (*irq_handler)(irq_t code, irq_context_t *ctx, void *data);
typedef struct { irq_handler hdl; void *data; } irq_cb_t;
typedef uint32_t irq_mask_t;

int irq_set_handler(irq_t code, irq_handler hnd, void *data);
irq_cb_t irq_get_handler(irq_t code);
irq_mask_t irq_disable(void);
void irq_restore(irq_mask_t old);
#endif
