/* mmutest3.c -- third MMU hardware run for rtl/v1/spec/mmu.md.
 *   V  where the cost of an ITLB refill goes: cycles plus PMCR events (IC misses 0x08, IC fill cycles 0x21, freeze by
 *      IC miss 0x24, instruction fetches 0x06) for a refill at a jump target, a speculative refill past an RTS, and a
 *      refill when straight-line code crosses a page (cached and uncached page)
 *   P  does PREF of a translated (P0) address fill the operand cache?  (OC address array after PREF)
 *   M  the load right after an MMUCR AT 1 -> 0 write: which memory did it read?
 */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <malloc.h>
#include <dc/perfctr.h>
#include <arch/cache.h>
#include <arch/irq.h>

#define R32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define PTEH   0xFF000000u
#define TEA    0xFF00000Cu
#define MMUCR  0xFF000010u
#define EXPEVT 0xFF000024u
#define PMCTR_LO(c) (0xFF100008u + 8u * (c))
#define ITLB_AA(e)  (0xF2000000u | ((uint32_t)(e) << 8))
#define ITLB_DA1(e) (0xF3000000u | ((uint32_t)(e) << 8))
#define ITLB_DA2(e) (0xF3800000u | ((uint32_t)(e) << 8))
#define UTLB_AA(e)  (0xF6000000u | ((uint32_t)(e) << 8))
#define UTLB_DA1(e) (0xF7000000u | ((uint32_t)(e) << 8))
#define UTLB_DA2(e) (0xF7800000u | ((uint32_t)(e) << 8))
#define OC_AA(pa)   (0xF4000000u | ((pa) & 0x3FE0u))
#define M_AT 1u
#define M_TI 4u
#define M_LRUI(x) (((uint32_t)(x) & 63u) << 26)
#define PA(p)  ((uint32_t)(uintptr_t)(p) & 0x1FFFFFFFu)
#define P2(pa) (((uint32_t)(pa) & 0x1FFFFFFFu) | 0xA0000000u)
typedef unsigned long UL;

extern uint32_t p_ld32(uint32_t), p_pref(uint32_t), p_ocbi(uint32_t), p_ocbp(uint32_t), p_ifetch(uint32_t);
extern void mmucr_set(uint32_t);
extern uint32_t t_call(uint32_t tgt, uint32_t aa, uint32_t val, uint32_t ctr);
extern uint32_t p_mlat0(uint32_t, uint32_t, uint32_t), p_mlat1(uint32_t, uint32_t, uint32_t),
    p_mlat2(uint32_t, uint32_t, uint32_t), p_mlat3(uint32_t, uint32_t, uint32_t);

typedef struct { uint32_t n, expevt, tea; } exc_t;
volatile exc_t X;
volatile uint32_t g_recover, g_recover_sr, g_unexpected;
static void hnd(irq_t code, irq_context_t *c, void *d) {
    (void)d; (void)code;
    X.n++; X.expevt = R32(EXPEVT); X.tea = R32(TEA);
    if (g_recover) { c->pc = g_recover; if (g_recover_sr) c->sr = g_recover_sr; }
    else { g_unexpected++; R32(MMUCR) = M_TI; c->pc += 2; }
}
static const irq_t codes[] = {0x040, 0x060, 0x080, 0x0A0, 0x0C0, 0x0E0, 0x100, 0x180, 0x1A0};
#define NCODES (sizeof codes / sizeof codes[0])
static irq_cb_t old_cb[NCODES];
static void xclr(void) { X.n = 0; X.expevt = 0; X.tea = 0; }
static void xdone(void) { g_recover = 0; }

static uint8_t *codebuf, *databuf;
#define CODE(k)  ((uint32_t)(uintptr_t)(codebuf + (k) * 0x1000))
#define DATA(k)  ((uint32_t)(uintptr_t)(databuf + (k) * 0x1000))
#define VA(p1)   (0x40000000u | PA(p1))
static const uint32_t szmask[4] = {0xFFFFFC00u, 0xFFFFF000u, 0xFFFF0000u, 0xFFF00000u};
static void umapc(int e, uint32_t p1, int c) {
    R32(UTLB_DA2(e)) = 0;
    R32(UTLB_DA1(e)) = (PA(p1) & 0x1FFFFC00u) | 0x100u | 0x10u | 0x60u | ((uint32_t)c << 3) | 0x04u;
    R32(UTLB_AA(e)) = (VA(p1) & 0xFFFFFC00u) | 0x300u;
}
static void hard_clear(void) {
    for (int e = 0; e < 64; e++) { R32(UTLB_AA(e)) = 0; R32(UTLB_DA1(e)) = 0; R32(UTLB_DA2(e)) = 0; }
    for (int e = 0; e < 4; e++) { R32(ITLB_AA(e)) = 0; R32(ITLB_DA1(e)) = 0; R32(ITLB_DA2(e)) = 0; }
    mmucr_set(M_TI); mmucr_set(0);
}
static int usz(int e) { uint32_t d = R32(UTLB_DA1(e)); return (int)(((d >> 6) & 2u) | ((d >> 4) & 1u)); }
static int tlb_check(void) {
    int bad = 0;
    for (int i = 0; i < 64; i++) {
        uint32_t a = R32(UTLB_AA(i)); if (!(a & 0x100)) continue;
        for (int j = i + 1; j < 64; j++) {
            uint32_t b = R32(UTLB_AA(j)); if (!(b & 0x100)) continue;
            if (((a ^ b) & szmask[usz(i)] & szmask[usz(j)]) == 0) bad++;
        }
    }
    for (int i = 0; i < 4; i++) if (R32(ITLB_AA(i)) & 0x100) bad++;   /* ITLB must start empty here */
    return bad;
}
static int at_on(uint32_t bits) { if (tlb_check()) { g_unexpected += 1000; return -1; } mmucr_set(bits | M_AT); return 0; }
static void at_off(void) { mmucr_set(0); }
static int find_itlb(uint32_t va) {
    for (int e = 0; e < 4; e++) { uint32_t a = R32(ITLB_AA(e)); if ((a & 0x100) && ((a ^ va) & 0xFFFFF000u) == 0) return e; }
    return -1;
}

/* ================================================================== V: cycles and events of a call */
enum { K_HIT, K_DROP };
/* one variant: call tgt 16 times, dropping the ITLB entry of page 'drop' (VA) before each call when kind == K_DROP;
   returns min / max cycles and min / max PRFC1 events per call in mode 'mode' */
static void run(uint32_t tgt, int kind, uint32_t drop, int mode, uint32_t r[4]) {
    uint32_t cmin = ~0u, cmax = 0, emin = ~0u, emax = 0;
    perf_cntr_start(PRFC1, (perf_cntr_event_t)mode, PMCR_COUNT_CPU_CYCLES);
    for (int i = 0; i < 16; i++) {
        uint32_t aa = 0;
        if (kind == K_DROP) { int e = find_itlb(drop); if (e >= 0) aa = ITLB_AA(e); }
        uint32_t e0 = R32(PMCTR_LO(1));
        uint32_t c = t_call(tgt, aa, 0, PMCTR_LO(0));
        uint32_t e1 = R32(PMCTR_LO(1));
        uint32_t ev = e1 - e0;
        if (c < cmin) cmin = c;
        if (c > cmax) cmax = c;
        if (ev < emin) emin = ev;
        if (ev > emax) emax = ev;
    }
    r[0] = cmin; r[1] = cmax; r[2] = emin; r[3] = emax;
}
static const int modes[4] = {0x08, 0x21, 0x24, 0x06};
static const char *mname[4] = {"ICmiss", "ICfillcyc", "frzICmiss", "ifetch"};
static void var(const char *nm, uint32_t tgt, int kind, uint32_t drop, int pa, int pb, int ca, int cb) {
    char line[4][48]; uint32_t r[4], c0 = 0, c1 = 0;
    for (int m = 0; m < 4; m++) {
        int irq = irq_disable();
        hard_clear();
        umapc(pa, CODE(pa), ca); if (pb >= 0) umapc(pb, CODE(pb), cb);
        at_on(M_LRUI(0));
        p_ifetch(tgt); p_ifetch(tgt); xdone();
        run(tgt, kind, drop, modes[m], r);
        at_off();
        hard_clear();
        irq_restore(irq);
        snprintf(line[m], sizeof line[m], "%s %lu..%lu", mname[m], (UL)r[2], (UL)r[3]);
        if (m == 0) { c0 = r[0]; c1 = r[1]; }
    }
    printf("V %-40s cyc %lu..%lu | %s | %s | %s | %s\n", nm, (UL)c0, (UL)c1, line[0], line[1], line[2], line[3]);
}
static void grp_V(void) {
    const uint32_t t0 = VA(CODE(0)), t5 = VA(CODE(5)) + 0xFFC, t11 = VA(CODE(11)) + 0xFF0;
    var("jsr target p0, ITLB hit", t0, K_HIT, 0, 0, -1, 1, 1);
    var("jsr target p0, p0 dropped (refill)", t0, K_DROP, t0, 0, -1, 1, 1);
    var("rts;nop at p5+ffc, p6 in ITLB", t5, K_HIT, 0, 5, 6, 1, 1);
    var("rts;nop at p5+ffc, p6 dropped", t5, K_DROP, VA(CODE(6)), 5, 6, 1, 1);
    var("rts;nop at p5+ffc, p6 not mapped", t5, K_HIT, 0, 5, -1, 1, 1);
    var("8 nops p11 -> p12, both in ITLB", t11, K_HIT, 0, 11, 12, 1, 1);
    var("8 nops p11 -> p12, p12 dropped", t11, K_DROP, VA(CODE(12)), 11, 12, 1, 1);
    var("8 nops p11 -> p12 (C=0), both in ITLB", t11, K_HIT, 0, 11, 12, 1, 0);
    var("8 nops p11 -> p12 (C=0), p12 dropped", t11, K_DROP, VA(CODE(12)), 11, 12, 1, 0);
    var("rts;nop p5+ffc, p6 C=0 in ITLB", t5, K_HIT, 0, 5, 6, 1, 0);
    var("rts;nop p5+ffc, p6 C=0 dropped", t5, K_DROP, VA(CODE(6)), 5, 6, 1, 0);
}

/* ================================================================== P: PREF of a translated address */
static void grp_P(void) {
    uint32_t pa = PA(DATA(0)) + 0x600, v = VA(DATA(0)) + 0x600, a[6];
    int irq = irq_disable();
    hard_clear();
    umapc(16, DATA(0), 1);
    R32(UTLB_AA(16)) = (VA(DATA(0)) & 0xFFFFFC00u) | 0x300u;
    at_on(0);
    p_ocbp(v); xdone(); a[0] = R32(OC_AA(pa));               /* line purged */
    p_pref(v); xdone();
    for (volatile int i = 0; i < 500; i++) ;
    a[1] = R32(OC_AA(pa));                                     /* after PREF through the P0 alias */
    p_ocbp(v); xdone(); a[2] = R32(OC_AA(pa));
    p_pref(DATA(0) + 0x600); xdone();
    for (volatile int i = 0; i < 500; i++) ;
    a[3] = R32(OC_AA(pa));                                     /* after PREF through P1 */
    p_ocbp(v); xdone(); a[4] = R32(OC_AA(pa));
    p_ocbp(DATA(9) + 0x600); xdone();                          /* (P1) make sure that line is not cached */
    p_pref(VA(DATA(9)) + 0x600); xdone();                      /* unmapped P0: no exception, no fill? */
    for (volatile int i = 0; i < 500; i++) ;
    a[5] = R32(OC_AA(PA(DATA(9)) + 0x600));
    at_off();
    hard_clear();
    irq_restore(irq);
    printf("P OC address array at the line's index (tag PA[28:10], U bit 1, V bit 0): purged %08lx | PREF P0 %08lx | "
           "purged %08lx | PREF P1 %08lx | purged %08lx | PREF unmapped P0 %08lx   (PA %08lx)\n", (UL)a[0], (UL)a[1],
           (UL)a[2], (UL)a[3], (UL)a[4], (UL)a[5], (UL)pa);
}

/* ================================================================== M: load right after MMUCR AT 1 -> 0 */
static void grp_M(void) {
    typedef uint32_t (*fn)(uint32_t, uint32_t, uint32_t);
    static const fn f[4] = {p_mlat0, p_mlat1, p_mlat2, p_mlat3};
    uint32_t v[4], n[4];
    const uint32_t un = VA(DATA(7)) + 0x10;       /* not in the UTLB */
    int irq = irq_disable();
    for (int i = 0; i < 4; i++) {
        hard_clear();
        umapc(16, DATA(0), 1);
        at_on(0);
        xclr(); v[i] = f[i](MMUCR, 0, un); xdone(); n[i] = X.n;
        at_off();
    }
    hard_clear();
    irq_restore(irq);
    printf("M AT 1->0 then load %08lx (physical %08lx holds %08lx):", (UL)un, (UL)(un & 0x1FFFFFFFu),
           (UL)R32(P2(un)));
    for (int i = 0; i < 4; i++) printf("  n%d: %08lx exc %lu", i, (UL)v[i], (UL)n[i]);
    printf("\n");
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    uint32_t o_mm = R32(MMUCR);
    printf("mmutest3: start\n");
    codebuf = memalign(0x10000, 0x10000);
    databuf = memalign(0x10000, 0x10000);
    if (!codebuf || !databuf) { printf("mmutest3: no memory\n"); return 1; }
    printf("mmutest3: code %08lx data %08lx\n", (UL)PA(codebuf), (UL)PA(databuf));
    uint16_t *cw = (uint16_t *)codebuf;
    for (int i = 0; i < 0x8000; i += 2) { cw[i] = 0x000B; cw[i + 1] = 0x0009; }
    for (int i = 0; i < 8; i++) cw[(11 * 0x1000 + 0xFF0) / 2 + i] = 0x0009;
    uint32_t *dw = (uint32_t *)databuf;
    for (int i = 0; i < 0x4000; i++) dw[i] = 0xD0000000u | (uint32_t)(i * 4);
    dcache_flush_range((uintptr_t)codebuf, 0x10000);
    icache_flush_range((uintptr_t)codebuf, 0x10000);
    dcache_flush_range((uintptr_t)databuf, 0x10000);
    for (unsigned i = 0; i < NCODES; i++) { old_cb[i] = irq_get_handler(codes[i]); irq_set_handler(codes[i], hnd, NULL); }
    int timer_was_on = perf_cntr_timer_enabled();
    if (!timer_was_on) perf_cntr_start(PRFC0, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES);
    grp_V();
    grp_P();
    grp_M();
    if (!timer_was_on) perf_cntr_clear(PRFC0);
    perf_cntr_clear(PRFC1);
    int irq = irq_disable();
    hard_clear();
    mmucr_set(o_mm & ~(M_AT | M_TI));
    irq_restore(irq);
    for (unsigned i = 0; i < NCODES; i++) irq_set_handler(codes[i], old_cb[i].hdl, old_cb[i].data);
    printf("mmutest3: done, unexpected exceptions %lu, MMUCR %08lx\n", (UL)g_unexpected, (UL)R32(MMUCR));
    return 0;
}
