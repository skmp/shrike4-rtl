/* shim/kos.h -- the part of KallistiOS's API that tools/hw/dmac/dmac1.c and dmac2.c use, for their bare-metal build
 * on the v1 SoC testbench (WI-DMAC; see ../README.md): the interrupt handler table (irq_set_handler, dispatched by
 * crt0.S's VBR table), irq_disable / irq_restore (SR.IMASK), printf / snprintf, the OC range operations and the
 * performance counters.  The console programs compile unmodified against it. */
#ifndef BM_KOS_H
#define BM_KOS_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <arch/irq.h>
#include <arch/cache.h>
#include <dc/perfctr.h>
#endif
