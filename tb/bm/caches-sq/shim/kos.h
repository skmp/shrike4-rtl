/* shim/kos.h -- the part of KallistiOS's API the caches-sq console programs use (tools/hw/caches-sq/cachetest*.c, built
 * unmodified for the v1 SoC testbench; WI-CACHE, see ../Makefile).  Exception handlers (irq_set_handler /
 * irq_get_handler, dispatched by ../crt0.S's VBR table), irq_disable / irq_restore (SR.IMASK). */
#ifndef BM_KOS_H
#define BM_KOS_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef uint32_t irq_t;                    /* the exception code (EXPEVT, or INTEVT for interrupts) */
typedef struct {                           /* ../crt0.S saves exactly this (KOS's field names) */
    uint32_t r[16];
    uint32_t pc, pr, sr, gbr, mach, macl;
} irq_context_t;
typedef void (*irq_handler)(irq_t code, irq_context_t *ctx, void *data);
typedef struct { irq_handler hdl; void *data; } irq_cb_t;
typedef uint32_t irq_mask_t;
#define EXC_DATA_ADDRESS_READ 0x00e0

int irq_set_handler(irq_t code, irq_handler hnd, void *data);
irq_cb_t irq_get_handler(irq_t code);
irq_mask_t irq_disable(void);
void irq_restore(irq_mask_t old);
#endif
