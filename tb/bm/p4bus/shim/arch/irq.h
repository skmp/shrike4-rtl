/* arch/irq.h -- KOS 2.1's interrupt mask calls, as kernel/arch/dreamcast/kernel/entry.s implements them (tb/bm/p4bus):
 *   irq_disable(): SR <- SR & 0xEFFFFF0F | 0xF0 (BL = 0, IMASK = 15), returns the old SR
 *   irq_restore(): SR <- the saved value */
#pragma once
#include <stdint.h>
typedef uint32_t irq_mask_t;
static inline irq_mask_t irq_disable(void) {
  uint32_t sr, n;
  __asm__ volatile("stc sr,%0" : "=r"(sr));
  n = (sr & 0xEFFFFF0Fu) | 0xF0u;
  __asm__ volatile("ldc %0,sr" : : "r"(n) : "memory");
  return sr;
}
static inline void irq_restore(irq_mask_t v) { __asm__ volatile("ldc %0,sr" : : "r"(v) : "memory"); }
