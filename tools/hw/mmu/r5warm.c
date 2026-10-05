/* r5warm.c -- M2 follow-up of mmutest2 R5 / R2 (agents/reports/integration/M2.md; rtl/v1/spec/mmu.md 7.4).
 *   J  a JSR to an unmapped P0 VA n instructions after an MMUCR AT 0 -> 1 store (040 = the target was translated).
 *      mmutest2 found n0:000 n1:040; the RTL gives n0:040.  Each probe (r5probes.S: n = 0 / 1 at mmutest2.elf's own
 *      cache-line layout, n = 0 with the delay slot in the same line, n = 1 with it in the next line; probes2.S's
 *      p_mjmp0..2 at whatever layout this link gives) is run
 *        cold      its lines invalidated right before the call (icache_flush_range),
 *        warm      right after the same call made with AT unchanged (MMUCR <- 0: the JSR target runs untranslated),
 *        slotcold  warm, then only the delay slot's line invalidated,
 *      4 times each.  Does the n = 0 result depend on the instruction cache?
 *   L  the same for the loads p_mlat0..3 (mmutest2: n0-2 000, n3 040).
 *   Q  per-call cycles of the speculative ITLB miss that also misses the UTLB (an RTS at p5+ffc, p6 unmapped):
 *      mmutest2 R2's setup (16 x 13) and mmutest3 V's (11..13), 16 calls each, twice.
 * The safety rules of mmutest.c / mmutest2.c: a hard clear before every AT = 1 phase, the overlap check, every
 * exception caught by a handler that returns to the probe's recovery label; interrupts off while AT = 1.
 */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <malloc.h>
#include <arch/cache.h>
#include <arch/irq.h>

#define R32(a) (*(volatile uint32_t *)(uintptr_t)(a))
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
#define M_AT 1u
#define M_TI 4u
#define M_LRUI(x) (((uint32_t)(x) & 63u) << 26)
#define PA(p)  ((uint32_t)(uintptr_t)(p) & 0x1FFFFFFFu)
#define MARK   0x7EA7EA00u
typedef unsigned long UL;

extern void mmucr_set(uint32_t);
extern uint32_t t_call(uint32_t tgt, uint32_t aa, uint32_t val, uint32_t ctr);
extern uint32_t p_ifetch(uint32_t);
typedef uint32_t (*lat_fn)(uint32_t r4, uint32_t r5, uint32_t r6);
extern uint32_t p_mlat0(uint32_t, uint32_t, uint32_t), p_mlat1(uint32_t, uint32_t, uint32_t),
    p_mlat2(uint32_t, uint32_t, uint32_t), p_mlat3(uint32_t, uint32_t, uint32_t);
extern uint32_t p_mjmp0(uint32_t, uint32_t, uint32_t), p_mjmp1(uint32_t, uint32_t, uint32_t),
    p_mjmp2(uint32_t, uint32_t, uint32_t);
extern uint32_t p_mjc0(uint32_t, uint32_t, uint32_t), p_mjc1(uint32_t, uint32_t, uint32_t),
    p_mjm0(uint32_t, uint32_t, uint32_t), p_mjn1(uint32_t, uint32_t, uint32_t);
extern char p_mlat0_at[], p_mlat1_at[], p_mlat2_at[], p_mlat3_at[];
extern char p_mjmp0_at[], p_mjmp1_at[], p_mjmp2_at[];

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
static void xclr(void) { X.n = 0; X.expevt = 0; X.tea = 0; R32(TEA) = MARK; }
static void xdone(void) { g_recover = 0; }

static uint8_t *codebuf, *databuf;
#define CODE(k)  ((uint32_t)(uintptr_t)(codebuf + (k) * 0x1000))
#define DATA(k)  ((uint32_t)(uintptr_t)(databuf + (k) * 0x1000))
#define VA(p1)   (0x40000000u | PA(p1))
static const uint32_t szmask[4] = {0xFFFFFC00u, 0xFFFFF000u, 0xFFFF0000u, 0xFFF00000u};
static void umap(int e, uint32_t p1) {                          /* 4 KB, PR 11, C, D (mmutest2.c umap) */
    R32(UTLB_DA2(e)) = 0;
    R32(UTLB_DA1(e)) = (PA(p1) & 0x1FFFFC00u) | 0x100u | 0x10u | 0x60u | 0x08u | 0x04u;
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
    for (int i = 0; i < 4; i++) if (R32(ITLB_AA(i)) & 0x100) bad++;          /* the ITLB starts empty here */
    return bad;
}
static int find_itlb(uint32_t va) {
    for (int e = 0; e < 4; e++) { uint32_t a = R32(ITLB_AA(e)); if ((a & 0x100) && ((a ^ va) & 0xFFFFF000u) == 0) return e; }
    return -1;
}

/* ================================================================== J / L: MMUCR AT 0 -> 1, then a JSR / a load */
enum { V_COLD, V_WARM, V_SLOT };
static const char *vname[3] = {"cold", "warm", "slotcold"};
/* one measured call: 0 no exception, else EXPEVT; `slot` is the address whose line 'slotcold' invalidates */
static uint32_t one(lat_fn f, uint32_t tgt, int v, uint32_t entry, uint32_t slot) {
    hard_clear(); umap(16, DATA(0));
    if (tlb_check()) { g_unexpected += 1000; return 0xFFF; }
    if (v == V_COLD) icache_flush_range(entry & ~31u, 0x60);
    else {
        xclr(); f(MMUCR, 0, tgt); xdone();                        /* warm-up: AT stays 0, the target is untranslated */
        if (X.n) g_unexpected += 100;
        if (v == V_SLOT) icache_flush_range(slot & ~31u, 32);
    }
    xclr(); f(MMUCR, M_AT, tgt); xdone();
    uint32_t r = X.n ? X.expevt : 0;
    mmucr_set(0);
    return r;
}
static void grp_J(void) {
    struct { const char *nm; lat_fn f; uint32_t entry, slot; } P[] = {
        {"mjc0 n0 console layout, slot next line", p_mjc0, (uint32_t)(uintptr_t)p_mjc0, (uint32_t)(uintptr_t)p_mjc0 + 0xC},
        {"mjc1 n1 console layout, slot same line", p_mjc1, (uint32_t)(uintptr_t)p_mjc1, (uint32_t)(uintptr_t)p_mjc1 + 0xE},
        {"mjm0 n0 mid-line, slot same line", p_mjm0, (uint32_t)(uintptr_t)p_mjm0, (uint32_t)(uintptr_t)p_mjm0 + 0xC},
        {"mjn1 n1, slot next line", p_mjn1, (uint32_t)(uintptr_t)p_mjn1, (uint32_t)(uintptr_t)p_mjn1 + 0xE},
        {"mjmp0 (probes2)", p_mjmp0, (uint32_t)(uintptr_t)p_mjmp0, (uint32_t)(uintptr_t)p_mjmp0_at + 2},
        {"mjmp1 (probes2)", p_mjmp1, (uint32_t)(uintptr_t)p_mjmp1, (uint32_t)(uintptr_t)p_mjmp1_at + 2},
        {"mjmp2 (probes2)", p_mjmp2, (uint32_t)(uintptr_t)p_mjmp2, (uint32_t)(uintptr_t)p_mjmp2_at + 2},
    };
    const uint32_t ucode = VA(CODE(15));                          /* not in the UTLB; rts;nop when untranslated */
    uint32_t r[7][3][4];
    int irq = irq_disable();
    for (int k = 0; k < 7; k++)
        for (int v = 0; v < 3; v++)
            for (int i = 0; i < 4; i++) r[k][v][i] = one(P[k].f, ucode, v, P[k].entry, P[k].slot);
    hard_clear();
    irq_restore(irq);
    printf("J MMUCR AT 0->1, jsr to unmapped P0 (040 = translated); per variant 4 repetitions\n");
    for (int k = 0; k < 7; k++) {
        printf("  J %-40s at %08lx slot %08lx:", P[k].nm, (UL)P[k].entry, (UL)P[k].slot);
        for (int v = 0; v < 3; v++) printf(" %s %03lx %03lx %03lx %03lx |", vname[v], (UL)r[k][v][0], (UL)r[k][v][1],
                                          (UL)r[k][v][2], (UL)r[k][v][3]);
        printf("\n");
    }
}
static void grp_L(void) {
    const lat_fn f[4] = {p_mlat0, p_mlat1, p_mlat2, p_mlat3};
    const char *at[4] = {p_mlat0_at, p_mlat1_at, p_mlat2_at, p_mlat3_at};
    const uint32_t un = VA(DATA(7));                              /* not in the UTLB; RAM when untranslated */
    uint32_t r[4][3][4];
    int irq = irq_disable();
    for (int k = 0; k < 4; k++)
        for (int v = 0; v < 3; v++)
            for (int i = 0; i < 4; i++) r[k][v][i] = one(f[k], un, v, (uint32_t)(uintptr_t)f[k], (uint32_t)(uintptr_t)at[k]);
    hard_clear();
    irq_restore(irq);
    printf("L MMUCR AT 0->1, load of unmapped P0 (040 = translated); per variant 4 repetitions\n");
    for (int k = 0; k < 4; k++) {
        printf("  L mlat%d at %08lx load %08lx:", k, (UL)(uintptr_t)f[k], (UL)(uintptr_t)at[k]);
        for (int v = 0; v < 3; v++) printf(" %s %03lx %03lx %03lx %03lx |", vname[v], (UL)r[k][v][0], (UL)r[k][v][1],
                                          (UL)r[k][v][2], (UL)r[k][v][3]);
        printf("\n");
    }
}

/* ================================================================== Q: the speculative UTLB miss past an RTS */
static void qline(const char *nm, uint32_t *c) {
    printf("  Q %-44s", nm);
    for (int i = 0; i < 16; i++) printf(" %lu", (UL)c[i]);
    printf("\n");
}
static void grp_Q(void) {
    const uint32_t ctr = PMCTR_LO(0), t5 = VA(CODE(5)) + 0xFFC;
    uint32_t a[16], b[16];
    printf("Q cycles per call of an RTS at p5+ffc with p6 not mapped (mmutest2 R2: 13 x 16; mmutest3 V: 11..13)\n");
    for (int rep = 0; rep < 2; rep++) {
        int irq = irq_disable();
        /* mmutest2 R2's order: p5 and p6 mapped, 16 calls dropping p6's ITLB entry each time, then p6 unmapped */
        hard_clear();
        umap(5, CODE(5)); umap(6, CODE(6));
        if (tlb_check()) { g_unexpected += 1000; irq_restore(irq); return; }
        mmucr_set(M_LRUI(0) | M_AT);
        p_ifetch(t5); p_ifetch(t5); xdone();
        for (int i = 0; i < 16; i++) { int e = find_itlb(VA(CODE(6))); b[i] = t_call(t5, e >= 0 ? ITLB_AA(e) : 0, 0, ctr); }
        R32(UTLB_AA(6)) = 0;
        { int e = find_itlb(VA(CODE(6))); if (e >= 0) R32(ITLB_AA(e)) = 0; }
        for (int i = 0; i < 16; i++) a[i] = t_call(t5, 0, 0, ctr);
        mmucr_set(0);
        irq_restore(irq);
        qline("R2 order: p6 dropped each call (UTLB hit)", b);
        qline("R2 order: after the p6-dropped calls", a);
        /* mmutest3 V's order: a hard clear, p5 only */
        irq = irq_disable();
        hard_clear();
        umap(5, CODE(5));
        if (tlb_check()) { g_unexpected += 1000; irq_restore(irq); return; }
        mmucr_set(M_LRUI(0) | M_AT);
        p_ifetch(t5); p_ifetch(t5); xdone();
        for (int i = 0; i < 16; i++) a[i] = t_call(t5, 0, 0, ctr);
        mmucr_set(0);
        hard_clear();
        irq_restore(irq);
        qline("V order: hard clear, p5 only", a);
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    uint32_t o_mm = R32(MMUCR);
    printf("r5warm: start\n");
    codebuf = memalign(0x10000, 0x10000);
    databuf = memalign(0x10000, 0x10000);
    if (!codebuf || !databuf) { printf("r5warm: no memory\n"); return 1; }
    printf("r5warm: code %08lx data %08lx\n", (UL)PA(codebuf), (UL)PA(databuf));
    uint16_t *cw = (uint16_t *)codebuf;
    for (int i = 0; i < 0x8000; i += 2) { cw[i] = 0x000B; cw[i + 1] = 0x0009; }   /* rts;nop everywhere */
    uint32_t *dw = (uint32_t *)databuf;
    for (int i = 0; i < 0x4000; i++) dw[i] = 0xD0000000u | (uint32_t)(i * 4);
    dcache_flush_range((uintptr_t)codebuf, 0x10000);
    icache_flush_range((uintptr_t)codebuf, 0x10000);
    dcache_flush_range((uintptr_t)databuf, 0x10000);
    for (unsigned i = 0; i < NCODES; i++) { old_cb[i] = irq_get_handler(codes[i]); irq_set_handler(codes[i], hnd, NULL); }
    grp_J();
    grp_L();
    grp_Q();
    int irq = irq_disable();
    hard_clear();
    mmucr_set(o_mm & ~(M_AT | M_TI));
    irq_restore(irq);
    for (unsigned i = 0; i < NCODES; i++) irq_set_handler(codes[i], old_cb[i].hdl, old_cb[i].data);
    printf("r5warm: done, unexpected exceptions %lu, MMUCR %08lx\n", (UL)g_unexpected, (UL)R32(MMUCR));
    return 0;
}
