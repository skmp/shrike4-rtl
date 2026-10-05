/* shim/kos.h -- the part of KallistiOS 2.1's API the serial console probes use (tools/hw/serial/scif_probe{,2,3}.c,
 * compiled unmodified), for a bare-metal build on the v1 SoC testbench (WI-SER; ../README.md): printf (bmrt.c, to the
 * sim-control putchar port), the interrupt handler table (irq_set_handler, dispatched by crt0.S's VBR table), SR.IMASK
 * (irq_disable / irq_restore), the IPR priorities (irq_get_priority / irq_set_priority), the OC range operations and
 * the PMCR / PMCTR cycle counter. */
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
