/* kos.h -- the few KallistiOS services the exceptions-intc console probes use (tools/hw/exceptions-intc/excint*.c),
 * so that tb/bm/exceptions-intc builds those sources unmodified for the bare-metal SoC testbench.
 *   irq_disable(): SR <- SR & 0xEFFFFF0F | 0xF0 (BL = 0, IMASK = 15), returns the old SR  (KOS arch/irq.h)
 *   irq_restore(): SR <- the saved value
 * Both also stand in for KOS's service of the Holly (ASIC) events it routes to an IRL level (crt0.S sets KOS's
 * routing): the console ran main() with interrupts enabled, so KOS's handler acknowledged such an event as soon as it
 * arrived outside a probe's window.  The bare-metal run-time keeps IMASK 15 there instead and acknowledges the pending
 * routed events at the edges of every window (irq_disable opens one, irq_restore closes it): an event that arrives
 * inside a window stays pending, as it did on the console (excint2's I rows wait for the next vblank-in at levels
 * 2 / 4 / 6; M4, WI-SOC).  Two Holly register reads and two writes at each edge, outside the probes' measured code.
 *   printf():      tb/bm/exceptions-intc/bm_rt.c, to the sim-control putchar port (board.md 3.10) */
#pragma once
#include <stdint.h>
typedef uint32_t irq_mask_t;
#define KOS_IML_NRM 0x0007D008u          /* IML2NRM | IML4NRM | IML6NRM as crt0.S sets them */
#define KOS_IML_ERR 0x00003000u          /* IML2ERR | IML4ERR | IML6ERR */
/* (no compare: the acknowledge is written even when it is 0 -- write 1 to clear, 0 clears nothing -- so SR.T, which
 * the probes' records show in the SR the C code hands them, keeps what the surrounding code left) */
static inline void kos_asic_service(void) {
  volatile uint32_t *h = (volatile uint32_t *)0xA05F6900u;   /* ISTNRM (h[0]), ISTERR (h[2]): write 1 to clear */
  h[0] = h[0] & KOS_IML_NRM;
  h[2] = h[2] & KOS_IML_ERR;
}
static inline irq_mask_t irq_disable(void) {
  uint32_t sr, n;
  __asm__ volatile("stc sr,%0" : "=r"(sr));
  n = (sr & 0xEFFFFF0Fu) | 0xF0u;
  __asm__ volatile("ldc %0,sr" : : "r"(n) : "memory");
  kos_asic_service();
  return sr;
}
static inline void irq_restore(irq_mask_t v) {
  __asm__ volatile("ldc %0,sr" : : "r"(v) : "memory");
  kos_asic_service();
}
int printf(const char *fmt, ...);
