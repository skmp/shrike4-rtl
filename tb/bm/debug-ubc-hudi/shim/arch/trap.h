/* shim/arch/trap.h -- KOS's TRAPA handler table: bmrt.c's exception dispatcher sends EXPEVT 0x160 to the handler of
 * TRA >> 2 (the TRAPA immediate) */
#ifndef BM_TRAP_H
#define BM_TRAP_H
#include "../kos.h"
typedef uint8_t trapa_t;
typedef void (*trapa_handler)(trapa_t code, irq_context_t *context, void *data);
int trapa_set_handler(trapa_t code, trapa_handler hnd, void *data);
trapa_handler trapa_get_handler(trapa_t code, void **data);
#endif
