/* shim/kos.h -- the part of KallistiOS's API the bsc-memmap console programs use (tools/hw/bsc-memmap/bscmm.c,
 * bscmm2.c), for a bare-metal build on the v1 SoC testbench (WI-BSC; ../README.md): printf (bmrt.c, to the sim-control
 * putchar port), memalign (a bump allocator), irq_disable / irq_restore (SR.IMASK), the KOS performance-counter
 * calls (shim/dc/perfctr.h). */
#ifndef BM_KOS_H
#define BM_KOS_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
typedef uint32_t irq_mask_t;
irq_mask_t irq_disable(void);
void irq_restore(irq_mask_t old);
#endif
