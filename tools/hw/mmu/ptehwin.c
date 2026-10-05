/* ptehwin.c -- M2 verification follow-up (agents/reports/integration/M2-verify.md): when does a PTEH.ASID store reach
 * the data translation?  SOC.md 6.2 gives MMUCR's AT / SV / SQMD a landing window of 3 instructions [HW mmu R5]; the v1
 * RTL has the same window for PTEH.ASID (an Iclk P4 write landing at its MA3, the EX-stage UTLB compare of the group
 * held behind it), but its lockstep model applies the ASID at once, and the manual sets no distance for PTEH.
 *   A  the page is ASID 1's (SH = 0), PTEH.ASID 1 -> 2, n = 0..4 instructions, a load of it (probes2.S p_mlat<n> with
 *      r4 = PTEH): 040 = translated with the new ASID, 000 = with the old one
 *   B  the page is ASID 2's, PTEH.ASID 1 -> 2: 000 = new ASID, 040 = old
 *   R  the reference: r5warm L's MMUCR AT 0 -> 1 then an unmapped load (console 000 000 000 040 for n = 0..3)
 * Each probe cold (its lines invalidated) and warm (the same call made first with PTEH unchanged), 4 repetitions.
 * Safety (mmutest2.c / r5warm.c): a hard clear before every AT = 1 phase, the overlap check, every exception caught by
 * a handler that returns to the probe's recovery label, interrupts off while AT = 1, PTEH and MMUCR restored at the end.
 * Build: source /opt/toolchains/dc/kos/environ.sh && make -f Makefile.ptehwin OUT=<dir>; run through ../hwrun.sh. */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <malloc.h>
#include <arch/cache.h>
#include <arch/irq.h>

#define R32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define PTEH   0xFF000000u
#define TEA    0xFF00000Cu
#define MMUCR  0xFF000010u
#define EXPEVT 0xFF000024u
#define ITLB_AA(e)  (0xF2000000u | ((uint32_t)(e) << 8))
#define ITLB_DA1(e) (0xF3000000u | ((uint32_t)(e) << 8))
#define ITLB_DA2(e) (0xF3800000u | ((uint32_t)(e) << 8))
#define UTLB_AA(e)  (0xF6000000u | ((uint32_t)(e) << 8))
#define UTLB_DA1(e) (0xF7000000u | ((uint32_t)(e) << 8))
#define UTLB_DA2(e) (0xF7800000u | ((uint32_t)(e) << 8))
#define M_AT 1u
#define M_TI 4u
#define PA(p)  ((uint32_t)(uintptr_t)(p) & 0x1FFFFFFFu)
#define MARK   0x7EA7EA00u
typedef unsigned long UL;

extern void mmucr_set(uint32_t);
typedef uint32_t (*lat_fn)(uint32_t r4, uint32_t r5, uint32_t r6);
extern uint32_t p_mlat0(uint32_t, uint32_t, uint32_t), p_mlat1(uint32_t, uint32_t, uint32_t),
    p_mlat2(uint32_t, uint32_t, uint32_t), p_mlat3(uint32_t, uint32_t, uint32_t), p_mlat4(uint32_t, uint32_t, uint32_t);
extern char p_mlat0_at[], p_mlat1_at[], p_mlat2_at[], p_mlat3_at[], p_mlat4_at[];

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

static uint8_t *databuf;
#define DATA(k)  ((uint32_t)(uintptr_t)(databuf + (k) * 0x1000))
#define VA(p1)   (0x40000000u | PA(p1))
static void umap_asid(int e, uint32_t p1, uint32_t asid) {     /* 4 KB, PR 11, C, D, SH = 0 */
    R32(UTLB_DA2(e)) = 0;
    R32(UTLB_DA1(e)) = (PA(p1) & 0x1FFFFC00u) | 0x100u | 0x10u | 0x60u | 0x08u | 0x04u;
    R32(UTLB_AA(e)) = (VA(p1) & 0xFFFFFC00u) | 0x300u | (asid & 0xFFu);
}
static void hard_clear(void) {
    for (int e = 0; e < 64; e++) { R32(UTLB_AA(e)) = 0; R32(UTLB_DA1(e)) = 0; R32(UTLB_DA2(e)) = 0; }
    for (int e = 0; e < 4; e++) { R32(ITLB_AA(e)) = 0; R32(ITLB_DA1(e)) = 0; R32(ITLB_DA2(e)) = 0; }
    mmucr_set(M_TI); mmucr_set(0);
}
static int tlb_check(void) {                                    /* one valid UTLB entry, an empty ITLB */
    int v = 0;
    for (int i = 0; i < 64; i++) if (R32(UTLB_AA(i)) & 0x100) v++;
    for (int i = 0; i < 4; i++) if (R32(ITLB_AA(i)) & 0x100) v += 10;
    return v != 1;
}

enum { V_COLD, V_WARM };
static const char *vname[2] = {"cold", "warm"};
/* one measured call: 0 = no exception, else EXPEVT.  kind 'A' / 'B': PTEH 1 -> 2 with the page's ASID 1 / 2;
 * 'R': MMUCR 0 -> AT with the page unmapped (the reference) */
static uint32_t one(lat_fn f, int kind, int v, uint32_t entry) {
    const uint32_t tgt = VA(DATA(kind == 'R' ? 7 : 0)) + 0x40u;
    hard_clear();
    umap_asid(16, DATA(0), kind == 'B' ? 2u : 1u);
    if (tlb_check()) { g_unexpected += 1000; return 0xFFF; }
    R32(PTEH) = 0x00000001u;
    if (kind != 'R') mmucr_set(M_AT);
    if (v == V_COLD) icache_flush_range(entry & ~31u, 0x60);
    else {                                                        /* warm-up: the same store without a change */
        xclr();
        if (kind == 'R') f(MMUCR, 0, tgt); else f(PTEH, 0x00000001u, tgt);
        xdone();
        if (kind == 'B' ? X.n != 1 : X.n != 0) g_unexpected += 100;
    }
    xclr();
    if (kind == 'R') f(MMUCR, M_AT, tgt); else f(PTEH, 0x00000002u, tgt);
    xdone();
    const uint32_t r = X.n ? X.expevt : 0;
    mmucr_set(0);
    R32(PTEH) = 0;
    return r;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    const uint32_t o_mm = R32(MMUCR), o_pteh = R32(PTEH);
    printf("ptehwin: start (MMUCR %08lx PTEH %08lx)\n", (UL)o_mm, (UL)o_pteh);
    databuf = memalign(0x10000, 0x10000);
    if (!databuf) { printf("ptehwin: no memory\n"); return 1; }
    uint32_t *dw = (uint32_t *)databuf;
    for (int i = 0; i < 0x4000; i++) dw[i] = 0xD0000000u | (uint32_t)(i * 4);
    dcache_flush_range((uintptr_t)databuf, 0x10000);
    printf("ptehwin: data %08lx\n", (UL)PA(databuf));
    for (unsigned i = 0; i < NCODES; i++) { old_cb[i] = irq_get_handler(codes[i]); irq_set_handler(codes[i], hnd, NULL); }
    const lat_fn f[5] = {p_mlat0, p_mlat1, p_mlat2, p_mlat3, p_mlat4};
    const char *at[5] = {p_mlat0_at, p_mlat1_at, p_mlat2_at, p_mlat3_at, p_mlat4_at};
    static const struct { int kind; const char *what; } K[3] = {
        {'A', "PTEH.ASID 1->2, page ASID 1 (040 = new ASID)"},
        {'B', "PTEH.ASID 1->2, page ASID 2 (000 = new ASID)"},
        {'R', "MMUCR AT 0->1, page unmapped (040 = new AT; console R5: 000 000 000 040)"},
    };
    uint32_t r[3][5][2][4];
    int irq = irq_disable();
    for (int k = 0; k < 3; k++)
        for (int n = 0; n < 5; n++)
            for (int v = 0; v < 2; v++)
                for (int i = 0; i < 4; i++) r[k][n][v][i] = one(f[n], K[k].kind, v, (uint32_t)(uintptr_t)f[n]);
    hard_clear();
    R32(PTEH) = o_pteh;
    mmucr_set(o_mm & ~(M_AT | M_TI));
    irq_restore(irq);
    for (int k = 0; k < 3; k++) {
        printf("%c %s\n", K[k].kind, K[k].what);
        for (int n = 0; n < 5; n++) {
            printf("  %c n%d at %08lx load %08lx:", K[k].kind, n, (UL)(uintptr_t)f[n], (UL)(uintptr_t)at[n]);
            for (int v = 0; v < 2; v++) printf(" %s %03lx %03lx %03lx %03lx |", vname[v], (UL)r[k][n][v][0],
                                              (UL)r[k][n][v][1], (UL)r[k][n][v][2], (UL)r[k][n][v][3]);
            printf("\n");
        }
    }
    for (unsigned i = 0; i < NCODES; i++) irq_set_handler(codes[i], old_cb[i].hdl, old_cb[i].data);
    printf("ptehwin: done, unexpected exceptions %lu, MMUCR %08lx, PTEH %08lx\n", (UL)g_unexpected, (UL)R32(MMUCR),
           (UL)R32(PTEH));
    return 0;
}
