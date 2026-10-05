/* p4b.c -- WI-P4 P4-bridge probe (rtl/v1/SOC.md open question 9.13: the Pclk synchroniser / posted-write structure).
 *
 * Settles two misses found by the WI-P4 verifier against timers-clock F (tc.elf.hwlog):
 *   16 x mov.l to TCOR1 = 181 (the RTL gives 184..187 at every phase), and
 *   8 x (mov.w -> TCR1 ; mov.w TCR1 ->) = 192 (the RTL gives 194..197),
 * by measuring each access sequence at every Pclk phase of the bracket: a Pclk read (IPRA) aligns the pipeline to a
 * Pclk edge, then k = 0 .. 11 one-cycle fillers move the bracket's first PMCTR read (see gen.py).  The "rd16" row (one
 * TCR1 read) is the phase meter.  Each (sequence, k) runs 8 times with interrupts masked; min and max are printed.
 *
 * Safety: only TMU1 (idle under KOS; stopped here first, exactly as tools/hw/timers-clock/tc.c does), reads of IPRA
 * and PMCTR0L.  TCR1 / TCOR1 / TCNT1 are saved and restored.  No exception, no sleep, no watchdog, bounded loops.
 * Build: source /opt/toolchains/dc/kos/environ.sh && python3 gen.py && make.   Run: HW_LOCK_WAIT=540 ../hwrun.sh p4b.elf
 */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <arch/irq.h>
#include "p4b_tab.h"

#define R8(a)  (*(volatile uint8_t  *)(a))
#define R16(a) (*(volatile uint16_t *)(a))
#define R32(a) (*(volatile uint32_t *)(a))

#define PMCTR0L 0xFF100008
#define TSTR    0xFFD80004
#define TCOR1   0xFFD80014
#define TCNT1   0xFFD80018
#define TCR1    0xFFD8001C
#define IPRA    0xFFD00004

typedef uint32_t (*mfn)(uint32_t tcr1, uint32_t tcor1, uint32_t ipra, uint32_t pm);
extern const mfn p4b_tab[P4B_NS * P4B_NK];

static void t1_stop(void) { irq_mask_t m = irq_disable(); R8(TSTR) = R8(TSTR) & ~2; irq_restore(m); }

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    t1_stop();
    uint16_t tcr = R16(TCR1);
    uint32_t tcor = R32(TCOR1), tcnt = R32(TCNT1);
    printf("p4b: start; TSTR %02x TCR1 %04x TCOR1 %08lx TCNT1 %08lx IPRA %04x\n", R8(TSTR), tcr, (unsigned long)tcor,
           (unsigned long)tcnt, R16(IPRA));
    printf("p4b: B - A (PMCTR0L bracket) for k = 0..%d fillers after a Pclk-aligning IPRA read; min of 8 runs"
           " (a '*' marks a row where some k had max != min)\n", P4B_NK - 1);
    for(int s = 0; s < P4B_NS; s++) {
        uint32_t mn[P4B_NK], mx[P4B_NK];
        int var = 0;
        for(int k = 0; k < P4B_NK; k++) {
            mn[k] = 0xFFFFFFFF; mx[k] = 0;
            for(int r = 0; r < 8; r++) {
                irq_mask_t m = irq_disable();
                uint32_t x = p4b_tab[s * P4B_NK + k](TCR1, TCOR1, IPRA, PMCTR0L);
                irq_restore(m);
                if(x < mn[k]) mn[k] = x;
                if(x > mx[k]) mx[k] = x;
            }
            if(mx[k] != mn[k]) var = 1;
        }
        printf("S %-12s", p4b_names[s]);
        for(int k = 0; k < P4B_NK; k++) printf(" %4lu", (unsigned long)mn[k]);
        printf("%s\n", var ? " *" : "");
        if(var) {
            printf("X %-12s", p4b_names[s]);
            for(int k = 0; k < P4B_NK; k++) printf(" %4lu", (unsigned long)mx[k]);
            printf("\n");
        }
    }
    R16(TCR1) = tcr; R32(TCOR1) = tcor; R32(TCNT1) = tcnt;
    printf("p4b: restored TCR1 %04x TCOR1 %08lx TCNT1 %08lx; done\n", R16(TCR1), (unsigned long)R32(TCOR1),
           (unsigned long)R32(TCNT1));
    return 0;
}
