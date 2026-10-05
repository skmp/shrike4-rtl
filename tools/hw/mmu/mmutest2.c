/* mmutest2.c -- second MMU hardware run for rtl/v1/spec/mmu.md (follow-ups of mmutest.c).
 *   R1  speculative ITLB refill: how far past a routine's end the fetch unit looks up the next page (cold / warm IC)
 *   R2  cost of the speculative refill, and of a refill when straight-line code crosses into a new page
 *   R3  PR of BSR / JSR when the delay slot takes an exception (TLB miss, address error, slot illegal)
 *   R4  URC for OCBI, MOVCA.L, FMOV.S store, PREF of a line that is not cached
 *   R5  how many instructions after an MMUCR write a P0 load / a jump to P0 sees the new AT
 *   R6  how many instructions after LDTLB / a UTLB address-array write a load sees the new entry
 *   R7  a store-queue write that takes a TLB miss: does it still write the queue?
 * Same safety rules as mmutest.c (hard clear before every AT = 1 phase, overlap check, all exceptions caught).
 */
#include <kos.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <malloc.h>
#include <arch/cache.h>
#include <arch/irq.h>

#define R32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define PTEH   0xFF000000u
#define PTEL   0xFF000004u
#define TEA    0xFF00000Cu
#define MMUCR  0xFF000010u
#define EXPEVT 0xFF000024u
#define PTEA   0xFF000034u
#define PMCTR_LO(c) (0xFF100008u + 8u * (c))
#define ITLB_AA(e)  (0xF2000000u | ((uint32_t)(e) << 8))
#define ITLB_DA1(e) (0xF3000000u | ((uint32_t)(e) << 8))
#define ITLB_DA2(e) (0xF3800000u | ((uint32_t)(e) << 8))
#define UTLB_AA(e)  (0xF6000000u | ((uint32_t)(e) << 8))
#define UTLB_DA1(e) (0xF7000000u | ((uint32_t)(e) << 8))
#define UTLB_DA2(e) (0xF7800000u | ((uint32_t)(e) << 8))
#define M_AT 1u
#define M_TI 4u
#define M_URC(x)  (((uint32_t)(x) & 63u) << 10)
#define M_LRUI(x) (((uint32_t)(x) & 63u) << 26)
#define G_URC(m)  (((m) >> 10) & 63u)
#define G_LRUI(m) (((m) >> 26) & 63u)
#define PA(p)  ((uint32_t)(uintptr_t)(p) & 0x1FFFFFFFu)
#define P2(pa) (((uint32_t)(pa) & 0x1FFFFFFFu) | 0xA0000000u)
#define MARK   0x7EA7EA00u
typedef unsigned long UL;

extern uint32_t p_ld32(uint32_t), p_st32(uint32_t, uint32_t), p_pref(uint32_t), p_ocbi(uint32_t), p_ocbwb(uint32_t);
extern uint32_t p_movca(uint32_t, uint32_t), p_ifetch(uint32_t), p_stf32(uint32_t);
extern uint32_t p_bsr_ds(uint32_t, uint32_t, uint32_t), p_jsr_ill(uint32_t, uint32_t, uint32_t);
extern uint32_t p_jsr_ds2(uint32_t, uint32_t, uint32_t);
extern void p_dummy(void);
extern void mmucr_set(uint32_t);
extern uint32_t t_call(uint32_t tgt, uint32_t aa, uint32_t val, uint32_t ctr);
typedef uint32_t (*lat_fn)(uint32_t r4, uint32_t r5, uint32_t r6);
extern uint32_t p_mlat0(uint32_t, uint32_t, uint32_t), p_mlat1(uint32_t, uint32_t, uint32_t), p_mlat2(uint32_t, uint32_t, uint32_t),
    p_mlat3(uint32_t, uint32_t, uint32_t), p_mlat4(uint32_t, uint32_t, uint32_t), p_mlat5(uint32_t, uint32_t, uint32_t),
    p_mlat6(uint32_t, uint32_t, uint32_t), p_mlat7(uint32_t, uint32_t, uint32_t), p_mlat8(uint32_t, uint32_t, uint32_t);
extern uint32_t p_mjmp0(uint32_t, uint32_t, uint32_t), p_mjmp1(uint32_t, uint32_t, uint32_t), p_mjmp2(uint32_t, uint32_t, uint32_t),
    p_mjmp3(uint32_t, uint32_t, uint32_t), p_mjmp4(uint32_t, uint32_t, uint32_t), p_mjmp5(uint32_t, uint32_t, uint32_t),
    p_mjmp6(uint32_t, uint32_t, uint32_t), p_mjmp7(uint32_t, uint32_t, uint32_t), p_mjmp8(uint32_t, uint32_t, uint32_t),
    p_mjmp9(uint32_t, uint32_t, uint32_t), p_mjmp10(uint32_t, uint32_t, uint32_t);
extern uint32_t p_llat0(uint32_t, uint32_t, uint32_t), p_llat1(uint32_t, uint32_t, uint32_t), p_llat2(uint32_t, uint32_t, uint32_t),
    p_llat3(uint32_t, uint32_t, uint32_t);
extern uint32_t p_ulat0(uint32_t, uint32_t, uint32_t), p_ulat1(uint32_t, uint32_t, uint32_t), p_ulat2(uint32_t, uint32_t, uint32_t),
    p_ulat3(uint32_t, uint32_t, uint32_t);
extern char p_bsr_ds_at[], p_jsr_ill_at[], p_jsr_ds2_at[];
static const lat_fn mlat[9] = {p_mlat0, p_mlat1, p_mlat2, p_mlat3, p_mlat4, p_mlat5, p_mlat6, p_mlat7, p_mlat8};
static const lat_fn mjmp[11] = {p_mjmp0, p_mjmp1, p_mjmp2, p_mjmp3, p_mjmp4, p_mjmp5, p_mjmp6, p_mjmp7, p_mjmp8, p_mjmp9,
                                p_mjmp10};
static const lat_fn llat[4] = {p_llat0, p_llat1, p_llat2, p_llat3};
static const lat_fn ulat[4] = {p_ulat0, p_ulat1, p_ulat2, p_ulat3};

/* ------------------------------------------------------------------ exception capture (as mmutest.c) */
typedef struct { uint32_t n, expevt, tea, pteh, spc, pr; } exc_t;
volatile exc_t X;
volatile uint32_t g_recover, g_recover_sr, g_unexpected;
static void hnd(irq_t code, irq_context_t *c, void *d) {
    (void)d; (void)code;
    X.n++; X.expevt = R32(EXPEVT); X.tea = R32(TEA); X.pteh = R32(PTEH); X.spc = c->pc; X.pr = c->pr;
    if (g_recover) { c->pc = g_recover; if (g_recover_sr) c->sr = g_recover_sr; }
    else { g_unexpected++; R32(MMUCR) = M_TI; c->pc += 2; }
}
static const irq_t codes[] = {0x040, 0x060, 0x080, 0x0A0, 0x0C0, 0x0E0, 0x100, 0x180, 0x1A0};
#define NCODES (sizeof codes / sizeof codes[0])
static irq_cb_t old_cb[NCODES];
static void xclr(void) { X.n = 0; X.expevt = 0; X.tea = 0; X.spc = 0; X.pr = 0; R32(TEA) = MARK; }
static void xdone(void) { g_recover = 0; }

static uint8_t *codebuf, *databuf;
static volatile uint32_t *sqbuf;
#define CODE(k)  ((uint32_t)(uintptr_t)(codebuf + (k) * 0x1000))
#define DATA(k)  ((uint32_t)(uintptr_t)(databuf + (k) * 0x1000))
#define VA(p1)   (0x40000000u | PA(p1))
#define SQA      (0xE0000000u | (PA(sqbuf) & 0x000FFFE0u))
enum { SZ1K = 0, SZ4K = 1, SZ64K = 2, SZ1M = 3 };
static const uint32_t szmask[4] = {0xFFFFFC00u, 0xFFFFF000u, 0xFFFF0000u, 0xFFF00000u};
static void utlb_set(int e, uint32_t va, uint32_t pa, int sz, int pr, int c, int d) {
    R32(UTLB_DA2(e)) = 0;
    R32(UTLB_DA1(e)) = (pa & 0x1FFFFC00u) | (1u << 8) | (((uint32_t)sz & 2u) << 6) | (((uint32_t)sz & 1u) << 4) |
                       ((uint32_t)pr << 5) | ((uint32_t)c << 3) | ((uint32_t)d << 2);
    R32(UTLB_AA(e)) = (va & 0xFFFFFC00u) | ((uint32_t)d << 9) | (1u << 8);
}
static void umap(int e, uint32_t p1) { utlb_set(e, VA(p1), PA(p1), SZ4K, 3, 1, 1); }
static void hard_clear(void) {
    for (int e = 0; e < 64; e++) { R32(UTLB_AA(e)) = 0; R32(UTLB_DA1(e)) = 0; R32(UTLB_DA2(e)) = 0; }
    for (int e = 0; e < 4; e++) { R32(ITLB_AA(e)) = 0; R32(ITLB_DA1(e)) = 0; R32(ITLB_DA2(e)) = 0; }
    mmucr_set(M_TI); mmucr_set(0);
}
static int usz(int e) { uint32_t d = R32(UTLB_DA1(e)); return (int)(((d >> 6) & 2u) | ((d >> 4) & 1u)); }
static int isz(int e) { uint32_t d = R32(ITLB_DA1(e)); return (int)(((d >> 6) & 2u) | ((d >> 4) & 1u)); }
static int tlb_check(void) {
    int bad = 0;
    for (int i = 0; i < 64; i++) {
        uint32_t a = R32(UTLB_AA(i)); if (!(a & 0x100)) continue;
        for (int j = i + 1; j < 64; j++) {
            uint32_t b = R32(UTLB_AA(j)); if (!(b & 0x100)) continue;
            if (((a ^ b) & szmask[usz(i)] & szmask[usz(j)]) == 0) bad++;
        }
    }
    for (int i = 0; i < 4; i++) {
        uint32_t a = R32(ITLB_AA(i)); if (!(a & 0x100)) continue;
        for (int j = i + 1; j < 4; j++) {
            uint32_t b = R32(ITLB_AA(j)); if (!(b & 0x100)) continue;
            if (((a ^ b) & szmask[isz(i)] & szmask[isz(j)]) == 0) bad++;
        }
    }
    return bad;
}
static int at_on(uint32_t bits) { if (tlb_check()) { g_unexpected += 1000; return -1; } mmucr_set(bits | M_AT); return 0; }
static void at_off(void) { mmucr_set(0); }
static uint32_t urc(void) { return G_URC(R32(MMUCR)); }
static char ipage(int e) {
    uint32_t a = R32(ITLB_AA(e));
    if (!(a & 0x100)) return '-';
    for (int k = 0; k < 16; k++) if (((a ^ VA(CODE(k))) & 0xFFFFF000u) == 0) return (char)(k < 10 ? '0' + k : 'a' + k - 10);
    return '?';
}
static int find_itlb(uint32_t va) {
    for (int e = 0; e < 4; e++) { uint32_t a = R32(ITLB_AA(e)); if ((a & 0x100) && ((a ^ va) & 0xFFFFF000u) == 0) return e; }
    return -1;
}
static void itlb_inval_all(void) { for (int e = 0; e < 4; e++) R32(ITLB_AA(e)) = 0; }
static void delay(int n) { for (volatile int i = 0; i < n; i++) ; }
static uint32_t vmin(uint32_t *v, int n) { uint32_t m = v[0]; for (int i = 1; i < n; i++) if (v[i] < m) m = v[i]; return m; }
static uint32_t vmax(uint32_t *v, int n) { uint32_t m = v[0]; for (int i = 1; i < n; i++) if (v[i] > m) m = v[i]; return m; }

/* ================================================================== R1: speculative refill distance */
static void grp_R1(void) {
    static const uint16_t pos[] = {0xFFC, 0xFF8, 0xFF4, 0xFF0, 0xFEC, 0xFE8, 0xFE4, 0xFE0, 0xFDC, 0xFD8, 0xFD0, 0xFC0,
                                   0xFA0, 0xF80};
    const int np = sizeof pos / sizeof pos[0];
    char cold[14][24], warm[14][24];
    for (int i = 0; i < np; i++) {
        int irq = irq_disable();
        hard_clear();
        icache_flush_range((uintptr_t)CODE(5), 0x2000);   /* cold IC for p5 / p6 */
        umap(5, CODE(5)); umap(6, CODE(6));
        at_on(M_LRUI(0));
        xclr(); p_ifetch(VA(CODE(5)) + pos[i]); xdone();
        snprintf(cold[i], sizeof cold[i], "[%c%c%c%c] %02lx %s", ipage(0), ipage(1), ipage(2), ipage(3),
                 (UL)G_LRUI(R32(MMUCR)), X.n ? "X" : "");
        itlb_inval_all();
        mmucr_set(M_AT | M_LRUI(0));
        xclr(); p_ifetch(VA(CODE(5)) + pos[i]); xdone();
        snprintf(warm[i], sizeof warm[i], "[%c%c%c%c] %02lx %s", ipage(0), ipage(1), ipage(2), ipage(3),
                 (UL)G_LRUI(R32(MMUCR)), X.n ? "X" : "");
        at_off();
        irq_restore(irq);
    }
    printf("R1 jsr to 'rts;nop' at p5+pos, p6 mapped in the UTLB only; ITLB [e0..e3] LRUI after (cold IC | warm IC)\n");
    for (int i = 0; i < np; i++) printf("  pos %03x: cold %-16s warm %s\n", pos[i], cold[i], warm[i]);
}

/* ================================================================== R2: cost of refills */
static void grp_R2(void) {
    uint32_t a[16], b[16], c[16], d[16], e_[16], f[16];
    const uint32_t ctr = PMCTR_LO(0);
    const uint32_t t5 = VA(CODE(5)) + 0xFFC, t11 = VA(CODE(11)) + 0xFF0;
    int irq = irq_disable();
    hard_clear();
    umap(5, CODE(5)); umap(6, CODE(6));
    at_on(M_LRUI(0));
    p_ifetch(t5); p_ifetch(t5); xdone();
    for (int i = 0; i < 16; i++) a[i] = t_call(t5, 0, 0, ctr);                           /* p5, p6 in the ITLB */
    for (int i = 0; i < 16; i++) { int e = find_itlb(VA(CODE(6))); b[i] = t_call(t5, e >= 0 ? ITLB_AA(e) : 0, 0, ctr); }
    char s6b[8]; snprintf(s6b, sizeof s6b, "%c%c%c%c", ipage(0), ipage(1), ipage(2), ipage(3));
    R32(UTLB_AA(6)) = 0;                                                                   /* p6 gone from the UTLB */
    { int e = find_itlb(VA(CODE(6))); if (e >= 0) R32(ITLB_AA(e)) = 0; }
    for (int i = 0; i < 16; i++) c[i] = t_call(t5, 0, 0, ctr);
    at_off();
    /* straight-line crossing: 8 nops at p11+ff0, rts;nop at p12+0 */
    hard_clear();
    umap(11, CODE(11)); umap(12, CODE(12));
    at_on(M_LRUI(0));
    p_ifetch(t11); p_ifetch(t11); xdone();
    char s11[8]; snprintf(s11, sizeof s11, "%c%c%c%c", ipage(0), ipage(1), ipage(2), ipage(3));
    for (int i = 0; i < 16; i++) d[i] = t_call(t11, 0, 0, ctr);
    for (int i = 0; i < 16; i++) { int e = find_itlb(VA(CODE(12))); e_[i] = t_call(t11, e >= 0 ? ITLB_AA(e) : 0, 0, ctr); }
    for (int i = 0; i < 16; i++) { int e = find_itlb(VA(CODE(11))); f[i] = t_call(t11, e >= 0 ? ITLB_AA(e) : 0, 0, ctr); }
    at_off();
    hard_clear();
    irq_restore(irq);
    printf("R2 call 'rts;nop' at p5+ffc: p6 in ITLB min %lu max %lu | p6 ITLB entry dropped each time (UTLB hit) min %lu "
           "max %lu [itlb %s] | p6 not in UTLB min %lu max %lu\n", (UL)vmin(a, 16), (UL)vmax(a, 16), (UL)vmin(b, 16),
           (UL)vmax(b, 16), s6b, (UL)vmin(c, 16), (UL)vmax(c, 16));
    printf("R2 call 8 nops at p11+ff0 falling into rts;nop at p12: both in ITLB min %lu max %lu [itlb %s] | p12 dropped "
           "min %lu max %lu | p11 dropped min %lu max %lu\n", (UL)vmin(d, 16), (UL)vmax(d, 16), s11, (UL)vmin(e_, 16),
           (UL)vmax(e_, 16), (UL)vmin(f, 16), (UL)vmax(f, 16));
}

/* ================================================================== R3: PR when the delay slot faults */
static void grp_R3(void) {
    struct { const char *nm; uint32_t n, ev, spc_off, pr; } L[8]; int k = 0;
    uint32_t rets[8];
#define PR3(name, at_, expr) do { xclr(); rets[k] = (expr); xdone(); L[k].nm = name; L[k].n = X.n; L[k].ev = X.expevt; \
        L[k].spc_off = X.spc - (uint32_t)(uintptr_t)(at_); L[k].pr = X.pr; k++; } while (0)
    const uint32_t dummy = (uint32_t)(uintptr_t)p_dummy, un = VA(DATA(7));
    int irq = irq_disable();
    hard_clear();
    umap(16, DATA(0));
    at_on(0);
    PR3("bsr; slot TLB miss", p_bsr_ds_at, p_bsr_ds(un, 0, 0x12345678u));
    PR3("jsr; slot TLB miss", p_jsr_ds2_at, p_jsr_ds2(un, dummy, 0x12345678u));
    PR3("jsr; slot misaligned (AE)", p_jsr_ds2_at, p_jsr_ds2(VA(DATA(0)) + 2, dummy, 0x12345678u));
    PR3("jsr; slot illegal FFFD", p_jsr_ill_at, p_jsr_ill(0, dummy, 0x12345678u));
    PR3("jsr to unmapped P0; slot ok", p_jsr_ds2_at, p_jsr_ds2(VA(DATA(0)), VA(CODE(15)), 0x12345678u));
    at_off();
    hard_clear();
    irq_restore(irq);
    for (int i = 0; i < k; i++)
        printf("R3 %-30s exc %lu ev %03lx spc-branch %+ld PR %08lx (marker 12345678, return address = branch+4)\n",
               L[i].nm, (UL)L[i].n, (UL)L[i].ev, (long)(int32_t)L[i].spc_off, (UL)L[i].pr);
    printf("R3 branch addresses: bsr %08lx jsr %08lx jsr(ill) %08lx\n", (UL)(uintptr_t)p_bsr_ds_at,
           (UL)(uintptr_t)p_jsr_ds2_at, (UL)(uintptr_t)p_jsr_ill_at);
#undef PR3
}

/* ================================================================== R4: URC for the remaining ops */
static void grp_R4(void) {
    static const char *nm[12]; uint32_t u[12]; int k = 0;
#define REC(s) do { nm[k] = (s); u[k++] = urc(); } while (0)
    const uint32_t d0 = VA(DATA(0));
    int irq = irq_disable();
    hard_clear();
    umap(16, DATA(0));
    at_on(M_URC(10));
    REC("start");
    p_ocbwb(d0 + 0x400); xdone(); REC("ocbwb (line maybe cached)");
    p_ocbi(d0 + 0x400); xdone(); REC("ocbi");
    p_pref(d0 + 0x400); xdone(); REC("pref, line not cached");
    delay(200);
    p_pref(d0 + 0x400); xdone(); REC("pref, line cached");
    p_movca(d0 + 0x440, 5); xdone(); REC("movca.l");
    p_stf32(d0 + 0x480); xdone(); REC("fmov.s store");
    p_ld32(d0 + 0x400); xdone(); REC("load");
    at_off();
    hard_clear();
    irq_restore(irq);
    printf("R4 URC:");
    for (int i = 0; i < k; i++) printf("  %s %lu", nm[i], (UL)u[i]);
    printf("\n");
#undef REC
}

/* ================================================================== R5/R6: when a write takes effect */
static void grp_R5(void) {
    uint32_t on[9], off[9], jon[11], lt[4], ut[4];
    const uint32_t un = VA(DATA(7));          /* not in the UTLB; physical RAM when untranslated */
    const uint32_t ucode = VA(CODE(15));      /* not in the UTLB; rts;nop when untranslated */
    int irq = irq_disable();
    for (int n = 0; n < 9; n++) {             /* AT 0 -> 1, then a load of an unmapped VA: exception = new AT */
        hard_clear(); umap(16, DATA(0));
        if (tlb_check()) break;
        xclr(); mlat[n](MMUCR, M_AT, un); xdone(); on[n] = X.n ? X.expevt : 0;
        at_off();
    }
    for (int n = 0; n < 9; n++) {             /* AT 1 -> 0 */
        hard_clear(); umap(16, DATA(0));
        at_on(0);
        xclr(); mlat[n](MMUCR, 0, un); xdone(); off[n] = X.n ? X.expevt : 0;
        at_off();
    }
    for (int n = 0; n < 11; n++) {            /* AT 0 -> 1, then jsr to an unmapped P0 VA */
        hard_clear(); umap(16, DATA(0));
        if (tlb_check()) break;
        xclr(); mjmp[n](MMUCR, M_AT, ucode); xdone(); jon[n] = X.n ? X.expevt : 0;
        at_off();
    }
    /* LDTLB into entry 50 (URC = 50), then a load through it */
    const uint32_t v8 = VA(DATA(8));
    for (int n = 0; n < 4; n++) {
        hard_clear(); umap(16, DATA(0));
        R32(PTEH) = v8 & 0xFFFFFC00u;
        R32(PTEL) = (PA(DATA(8)) & 0x1FFFFC00u) | 0x100u | 0x10u | 0x60u | 0x04u;   /* V, 4K, PR11, D, C=0 */
        R32(PTEA) = 0;
        at_on(M_URC(50));
        xclr(); llat[n](0, 0, v8); xdone(); lt[n] = X.n ? X.expevt : 0;
        at_off();
    }
    R32(PTEH) = 0; R32(PTEL) = 0;
    /* UTLB address-array write (makes entry 50 valid), then a load through it */
    for (int n = 0; n < 4; n++) {
        hard_clear(); umap(16, DATA(0));
        R32(UTLB_DA1(50)) = (PA(DATA(8)) & 0x1FFFFC00u) | 0x10u | 0x60u | 0x04u;    /* V = 0 */
        at_on(0);
        xclr(); ulat[n](UTLB_AA(50), (v8 & 0xFFFFFC00u) | 0x300u, v8); xdone(); ut[n] = X.n ? X.expevt : 0;
        at_off();
    }
    hard_clear();
    irq_restore(irq);
    printf("R5 MMUCR AT 0->1, n instr, load unmapped P0 (040 = translated):");
    for (int n = 0; n < 9; n++) printf(" n%d:%03lx", n, (UL)on[n]);
    printf("\nR5 MMUCR AT 1->0, n instr, load unmapped P0 (040 = still translated):");
    for (int n = 0; n < 9; n++) printf(" n%d:%03lx", n, (UL)off[n]);
    printf("\nR5 MMUCR AT 0->1, n instr, jsr to unmapped P0 (040 = translated):");
    for (int n = 0; n < 11; n++) printf(" n%d:%03lx", n, (UL)jon[n]);
    printf("\nR6 LDTLB, n instr, load through the new entry (040 = not yet visible):");
    for (int n = 0; n < 4; n++) printf(" n%d:%03lx", n, (UL)lt[n]);
    printf("\nR6 UTLB aa write (V=1), n instr, load through it (040 = not yet visible):");
    for (int n = 0; n < 4; n++) printf(" n%d:%03lx", n, (UL)ut[n]);
    printf("\n");
}

/* ================================================================== R7: SQ write that misses the UTLB */
static void grp_R7(void) {
    uint32_t n1, e1, n2, e2, mem[8];
    int irq = irq_disable();
    for (int i = 0; i < 8; i++) R32(P2(PA(sqbuf)) + 4 * i) = 0;
    hard_clear(); umap(16, DATA(0));
    at_on(0);
    xclr();
    for (int i = 0; i < 8; i++) { p_st32(SQA + 4 * i, 0xB0010000u | (uint32_t)i); xdone(); }
    n1 = X.n; e1 = X.expevt;
    at_off();
    utlb_set(32, 0xE0000000u, PA(sqbuf) & 0x1FF00000u, SZ1M, 1, 0, 1);
    at_on(0);
    xclr(); p_pref(SQA); xdone(); n2 = X.n; e2 = X.expevt;
    at_off();
    delay(2000);
    for (int i = 0; i < 8; i++) mem[i] = R32(P2(PA(sqbuf)) + 4 * i);
    hard_clear();
    irq_restore(irq);
    printf("R7 SQ writes with no UTLB entry: exc %lu ev %03lx; then mapped + PREF: exc %lu ev %03lx; memory:", (UL)n1,
           (UL)e1, (UL)n2, (UL)e2);
    for (int i = 0; i < 8; i++) printf(" %08lx", (UL)mem[i]);
    printf("\n");
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    uint32_t o_pteh = R32(PTEH), o_ptel = R32(PTEL), o_ptea = R32(PTEA), o_tea = R32(TEA), o_mm = R32(MMUCR);
    printf("mmutest2: start\n");
    codebuf = memalign(0x10000, 0x10000);
    databuf = memalign(0x10000, 0x10000);
    sqbuf = memalign(32, 64);
    if (!codebuf || !databuf || !sqbuf) { printf("mmutest2: no memory\n"); return 1; }
    printf("mmutest2: code %08lx data %08lx sq %08lx\n", (UL)PA(codebuf), (UL)PA(databuf), (UL)PA(sqbuf));
    uint16_t *cw = (uint16_t *)codebuf;
    for (int i = 0; i < 0x8000; i += 2) { cw[i] = 0x000B; cw[i + 1] = 0x0009; }
    for (int i = 0; i < 8; i++) cw[(11 * 0x1000 + 0xFF0) / 2 + i] = 0x0009;       /* 8 nops at p11+ff0 */
    uint32_t *dw = (uint32_t *)databuf;
    for (int i = 0; i < 0x4000; i++) dw[i] = 0xD0000000u | (uint32_t)(i * 4);
    dcache_flush_range((uintptr_t)codebuf, 0x10000);
    icache_flush_range((uintptr_t)codebuf, 0x10000);
    dcache_flush_range((uintptr_t)databuf, 0x10000);
    dcache_purge_range((uintptr_t)sqbuf, 64);
    for (unsigned i = 0; i < NCODES; i++) { old_cb[i] = irq_get_handler(codes[i]); irq_set_handler(codes[i], hnd, NULL); }
    grp_R1();
    grp_R2();
    grp_R3();
    grp_R4();
    grp_R5();
    grp_R7();
    int irq = irq_disable();
    hard_clear();
    R32(PTEH) = o_pteh; R32(PTEL) = o_ptel; R32(PTEA) = o_ptea; R32(TEA) = o_tea;
    mmucr_set(o_mm & ~(M_AT | M_TI));
    irq_restore(irq);
    for (unsigned i = 0; i < NCODES; i++) irq_set_handler(codes[i], old_cb[i].hdl, old_cb[i].data);
    printf("mmutest2: done, unexpected exceptions %lu, MMUCR %08lx\n", (UL)g_unexpected, (UL)R32(MMUCR));
    return 0;
}
