/* shim/arch/irq.h -- KOS's interrupt API as the dmac programs use it: irq_set_handler with KOS's handler signature
 * (crt0.S saves the interrupted context and ../bmrt.c dispatches by EXPEVT / INTEVT), irq_disable / irq_restore on
 * SR.IMASK. */
#ifndef BM_ARCH_IRQ_H
#define BM_ARCH_IRQ_H
#include <stdint.h>
typedef uint32_t irq_t;                    /* the exception code (EXPEVT, or INTEVT for interrupts) */
typedef struct {                           /* what crt0.S saves (the programs never look inside) */
    uint32_t r[16];
    uint32_t pc, pr, sr, gbr, mach, macl;
} irq_context_t;
typedef void (*irq_handler)(irq_t code, irq_context_t *ctx, void *data);
typedef uint32_t irq_mask_t;
int irq_set_handler(irq_t code, irq_handler hnd, void *data);
irq_mask_t irq_disable(void);
void irq_restore(irq_mask_t old);
#endif
