/* kos.h -- the KallistiOS services tools/hw/p4bus/p4b.c uses, so that tb/bm/p4bus builds it unmodified for the v1
 * testbenches (WI-P4; SOC.md 8.3 M1a).  <kos.h> in KOS pulls in stdio and arch/irq.h; p4b.c needs printf and
 * irq_disable / irq_restore only. */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <arch/irq.h>
