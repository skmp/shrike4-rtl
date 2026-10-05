/* shim/arch/irq.h -- KOS 2.1 arch/irq.h, the part the serial probes use (WI-SER).  irq_t is the exception code
 * (INTEVT for interrupts, EXPEVT otherwise); the handler table is bmrt.c's, called from crt0.S's VBR table. */
#ifndef BM_ARCH_IRQ_H
#define BM_ARCH_IRQ_H
#include <stdint.h>

typedef uint32_t irq_t;
enum {
    EXC_SCI_ERI = 0x04e0, EXC_SCI_RXI = 0x0500, EXC_SCI_TXI = 0x0520, EXC_SCI_TEI = 0x0540,
    EXC_SCIF_ERI = 0x0700, EXC_SCIF_RXI = 0x0720, EXC_SCIF_BRI = 0x0740, EXC_SCIF_TXI = 0x0760
};
typedef struct {                           /* crt0.S saves exactly this */
    uint32_t r[16];
    uint32_t pc, pr, sr, gbr, mach, macl;
} irq_context_t;
typedef void (*irq_handler)(irq_t code, irq_context_t *ctx, void *data);
typedef uint32_t irq_mask_t;

/* KOS's IPR source numbering: IPR(src / 4) = IPRA / IPRB / IPRC, field (src % 4) * 4 */
typedef enum irq_src {
    IRQ_SRC_RTC, IRQ_SRC_TMU2, IRQ_SRC_TMU1, IRQ_SRC_TMU0, _IRQ_SRC_RESV, IRQ_SRC_SCI1, IRQ_SRC_REF, IRQ_SRC_WDT,
    IRQ_SRC_HUDI, IRQ_SRC_SCIF, IRQ_SRC_DMAC, IRQ_SRC_GPIO
} irq_src_t;

int irq_set_handler(irq_t code, irq_handler hnd, void *data);
irq_mask_t irq_disable(void);
void irq_restore(irq_mask_t old);
void irq_set_priority(irq_src_t src, unsigned int prio);
unsigned int irq_get_priority(irq_src_t src);
#endif
