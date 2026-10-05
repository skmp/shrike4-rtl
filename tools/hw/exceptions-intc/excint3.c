/* excint3.c -- run 3 of the SH7091 exception / interrupt probes (spec: rtl/v1/spec/exceptions-intc.md).
 * RTE into a pending interrupt that the RTE unmasks: cycles to the target (PMCTR read there) and from the target to
 * the handler, for an IMASK change (BL = 0 before) and for a BL change (BL = 1 before, the ISR-return case);
 * the same through LDC SR; and a JMP baseline. */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#define R32(a) (*(volatile uint32_t *)(a))
#define R16(a) (*(volatile uint16_t *)(a))
#define R8(a)  (*(volatile uint8_t *)(a))
#define IPRA 0xFFD00004
#define IPRB 0xFFD00008
#define IPRC 0xFFD0000C
#define TSTR 0xFFD80004
#define TCOR1 0xFFD80014
#define TCNT1 0xFFD80018
#define TCR1 0xFFD8001C
typedef struct { uint32_t kind, expevt, intevt, tra, tea, spc, ssr, sgr, sr, pr, r15, r0, r1, r2, pm, tcnt1; } rec_t;
extern rec_t g_recs[32];
extern volatile uint32_t g_nrec, g_irq_mode, g_ret;
extern uint32_t run_asm(void *fn, uint32_t a0, uint32_t a1, uint32_t a2);
extern char t_rteirq[], L_r3c[], t_ldcirq[], L_l3c[], t_jmp_mid[];
static uint16_t s_ipra, s_iprb, s_iprc;
static irq_mask_t s_im;
static void tmu1_stop(void) { R8(TSTR) &= ~2; R16(TCR1) = 0; (void)R16(TCR1); }
static void win_open(uint16_t ipra) {
    s_im = irq_disable(); s_ipra = R16(IPRA); s_iprb = R16(IPRB); s_iprc = R16(IPRC);
    R16(IPRA) = ipra; R16(IPRB) = 0; (void)R16(IPRC); g_nrec = 0; g_irq_mode = 1;
}
static void win_close(void) {
    tmu1_stop(); R16(IPRA) = s_ipra; R16(IPRB) = s_iprb; R16(IPRC) = s_iprc; (void)R16(IPRC); irq_restore(s_im);
}
static int tmu1_pend(void) {
    tmu1_stop(); R32(TCOR1) = 0xFFFFFFFF; R32(TCNT1) = 0; R16(TCR1) = 0x20; R8(TSTR) |= 2;
    for (int i = 0; i < 100000; i++) if (R16(TCR1) & 0x100) break;
    R8(TSTR) &= ~2; return (R16(TCR1) & 0x100) != 0;
}
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    uint32_t sr; __asm__ volatile("stc sr,%0" : "=r"(sr));
    uint32_t sr15 = sr | 0xF0, sr6 = (sr15 & ~0xF0u) | 0x60, pm = 0xFF100008;
    printf("excint3: SR %08lx\n", (unsigned long)sr);
    static const struct { const char *nm; int rte, pend; uint32_t before, after; } v[6] = {
        {"rte imask15->6 pend", 1, 1, 0, 0}, {"rte BL1->0 pend", 1, 1, 1, 0}, {"rte imask15->6 none", 1, 0, 0, 0},
        {"ldc imask15->6 pend", 0, 1, 0, 0}, {"ldc BL1->0 pend", 0, 1, 1, 0}, {"rte BL1->0 none", 1, 0, 1, 0}};
    for (int i = 0; i < 6; i++) {
        uint32_t before = v[i].before ? (sr6 | 0x10000000) : sr15;
        printf("%-20s to-target/k/target-to-handler:", v[i].nm);
        for (int rep = 0; rep < 8; rep++) {
            win_open(0x0F00);
            if (v[i].pend) tmu1_pend();
            uint32_t ret = run_asm(v[i].rte ? t_rteirq : t_ldcirq, sr6, pm, before);
            win_close();
            rec_t *r = &g_recs[0];
            if (!g_nrec) { printf(" [none ret %lu]", (unsigned long)ret); continue; }
            printf(" %lu/%ld/%lu", (unsigned long)(r->r2 - r->r1), (long)(r->spc - (uint32_t)(v[i].rte ? L_r3c : L_l3c)) / 2,
                   (unsigned long)(r->pm - r->r2));
        }
        printf("\n");
    }
    printf("jmp_mid cyc:");
    for (int rep = 0; rep < 5; rep++) { win_open(0); uint32_t c = run_asm(t_jmp_mid, 0, pm, 0); win_close(); printf(" %lu", (unsigned long)c); }
    printf("\nexcint3: done\n");
    return 0;
}
