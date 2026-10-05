/* kos.h -- the KallistiOS services the soc-design / core-integration console probes use (tools/hw/soc-design/ and
 * tools/hw/core-integration/), so that tb/bm/soc-design and tb/bm/core-integration build those sources unmodified
 * for the bare-metal v1 testbenches (WI-CORE-A: make v1-exc; WI-SOC: the M1a replays).
 *   irq_disable(): SR <- SR & 0xEFFFFF0F | 0xF0 (BL = 0, IMASK = 15), returns the old SR  (KOS arch/irq.h)
 *   irq_restore(): SR <- the saved value
 *   printf / fflush / memset / memcpy and the dc/perfctr.h API: ../soc-design/bm_rt.c */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t irq_mask_t;
static inline irq_mask_t irq_disable(void) {
  uint32_t sr, n;
  __asm__ volatile("stc sr,%0" : "=r"(sr));
  n = (sr & 0xEFFFFF0Fu) | 0xF0u;
  __asm__ volatile("ldc %0,sr" : : "r"(n) : "memory");
  return sr;
}
static inline void irq_restore(irq_mask_t v) { __asm__ volatile("ldc %0,sr" : : "r"(v) : "memory"); }
/* the interrupt-handler registry (KOS arch/irq.h), for tools/hw/core-exc/c1k.c's safety net (a user break that misses
 * the DBR path): the bare-metal run has no KOS dispatcher, so get returns an empty entry and set does nothing */
typedef uint32_t irq_t;
typedef struct { uint32_t r[16], pc, pr, gbr, vbr, mach, macl, sr; } irq_context_t;
typedef void (*irq_handler)(irq_t code, irq_context_t *ctx, void *data);
typedef struct { irq_handler hdl; void *data; } irq_cb_t;
static inline irq_cb_t irq_get_handler(irq_t code) { (void)code; irq_cb_t c = {0, 0}; return c; }
static inline int irq_set_handler(irq_t code, irq_handler h, void *data) { (void)code; (void)h; (void)data; return 0; }
