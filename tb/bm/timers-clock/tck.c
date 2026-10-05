/* tck.c -- bare-metal port of test K of tools/hw/timers-clock/tc.c: CPU cycles and TMU1 Pphi/4 counts per AICA RTC
 * second, over 3 seconds (timers-clock 2: 199,505,792 Icyc/s, 16.0000 Icyc per count).  It needs the board's AICA RTC
 * (board.md; G9: one second every 199,505,792 Icyc) and ~800 M simulated cycles, so it is its own program (M4).
 * Only the ratio (16.0000) is a check (timers-clock 8.3); the absolute rate is the board model's.
 */
#include "bmlib.h"

#define PMCTR0L 0xFF100008
#define TSTR    0xFFD80004
#define TCOR1   0xFFD80014
#define TCNT1   0xFFD80018
#define TCR1    0xFFD8001C
#define AICA_RTC_H 0xA0710000
#define AICA_RTC_L 0xA0710004

static inline uint32_t cyc(void) { return R32(PMCTR0L); }
static void spin(uint32_t n) { uint32_t c0 = cyc(); while(cyc() - c0 < n) ; }
static void t1_stop(void) { irq_mask_t m = irq_disable(); R8(TSTR) = R8(TSTR) & ~2; irq_restore(m); }
static void t1_setup(uint16_t tcr, uint32_t tcor, uint32_t tcnt) {
    t1_stop(); R16(TCR1) = tcr; R32(TCOR1) = tcor; R32(TCNT1) = tcnt;
}
static uint32_t aica_edge(uint32_t *c, uint32_t *tn) {
    irq_mask_t m = irq_disable();
    uint32_t s = R32(AICA_RTC_L) & 0xFFFF, c0 = cyc(), x, cc;
    do { cc = cyc(); x = R32(AICA_RTC_L) & 0xFFFF; } while(x == s && cc - c0 < 260000000u);
    *c = cyc(); *tn = R32(TCNT1);
    uint32_t hi = R32(AICA_RTC_H) & 0xFFFF;
    irq_restore(m);
    return (hi << 16) | x;
}
int main(void) {
    kos_timer_state();
    t1_setup(0, 0xFFFFFFFF, 0xFFFFFFFF);
    { irq_mask_t m = irq_disable(); R8(TSTR) = R8(TSTR) | 2; irq_restore(m); }
    uint32_t c0, c1, n0, n1;
    uint32_t s0 = aica_edge(&c0, &n0);
    spin(2 * 199500000u);
    uint32_t s1 = aica_edge(&c1, &n1);
    t1_stop();
    uint32_t ds = s1 - s0, dc = c1 - c0, dn = n0 - n1;
    printf("K AICA RTC %lu -> %lu (%lu s): %lu CPU cycles (%lu per s), %lu TMU1 P/4 counts (%lu per s), cycles/count %lu.%04lu\n",
           (unsigned long)s0, (unsigned long)s1, (unsigned long)ds, (unsigned long)dc, (unsigned long)(ds ? dc / ds : 0),
           (unsigned long)dn, (unsigned long)(ds ? dn / ds : 0), (unsigned long)(dn ? dc / dn : 0),
           (unsigned long)(dn ? (uint32_t)(((uint64_t)(dc % dn) * 10000) / dn) : 0));
    printf("tck: done\n");
    return 0;
}
