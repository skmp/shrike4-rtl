/* mmutest.c -- SH7091 MMU hardware checks for shrike4-rtl/rtl/v1/spec/mmu.md.
 *
 * Groups (each prints its results with the MMU off before the next starts):
 *   A  MMU register read-back (PTEH PTEL PTEA TTB TEA MMUCR), MMUCR.TI effect on the arrays
 *   B  memory-mapped ITLB / UTLB arrays: implemented bits, entry decode, V/D sharing, associative writes (AT = 0 / 1)
 *   C  LDTLB: which PTEH/PTEL/PTEA bits land in the UTLB, URC after LDTLB
 *   D  URC: what increments it (per access kind), wrap with URB, URC > URB
 *   E  ITLB: LRUI update / replacement, refill field copy, speculative fetch across a page, delay slot in a new page
 *   F  data TLB exceptions: codes, TEA / PTEH, priorities, RMW / cache ops / MAC.L, precision of the cancel
 *   G  translation: page sizes, VPN / PPN low bits, ASID / SH / SV, P3, area 7 through the TLB
 *   S  store queues with the MMU on
 *   T  timing: ITLB refill penalty, UTLB data-access cost (PMCR counters)
 *   U  user mode: protection, address errors, ITLB PR, SQMD
 * Safety: every AT = 1 phase starts from a hard clear of both TLBs; tlb_check() refuses to enable translation when
 * two valid entries overlap (a multiple hit would reset the console).  Every exception taken is caught by hnd() and
 * sent to the probe's recovery label.  All state is restored at the end.
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
#define PTEL   0xFF000004u
#define TTB    0xFF000008u
#define TEA    0xFF00000Cu
#define MMUCR  0xFF000010u
#define CCR    0xFF00001Cu
#define EXPEVT 0xFF000024u
#define PTEA   0xFF000034u
#define QACR0  0xFF000038u
#define QACR1  0xFF00003Cu
#define PMCTR_LO(c) (0xFF100008u + 8u * (c))

#define ITLB_AA(e)  (0xF2000000u | ((uint32_t)(e) << 8))
#define ITLB_DA1(e) (0xF3000000u | ((uint32_t)(e) << 8))
#define ITLB_DA2(e) (0xF3800000u | ((uint32_t)(e) << 8))
#define UTLB_AA(e)  (0xF6000000u | ((uint32_t)(e) << 8))
#define UTLB_ASSOC  0xF6000080u
#define UTLB_DA1(e) (0xF7000000u | ((uint32_t)(e) << 8))
#define UTLB_DA2(e) (0xF7800000u | ((uint32_t)(e) << 8))

#define M_AT      1u
#define M_TI      4u
#define M_SV      0x100u
#define M_SQMD    0x200u
#define M_URC(x)  (((uint32_t)(x) & 63u) << 10)
#define M_URB(x)  (((uint32_t)(x) & 63u) << 18)
#define M_LRUI(x) (((uint32_t)(x) & 63u) << 26)
#define G_URC(m)  (((m) >> 10) & 63u)
#define G_URB(m)  (((m) >> 18) & 63u)
#define G_LRUI(m) (((m) >> 26) & 63u)

#define PA(p)  ((uint32_t)(uintptr_t)(p) & 0x1FFFFFFFu)
#define P2(pa) (((uint32_t)(pa) & 0x1FFFFFFFu) | 0xA0000000u)
#define MARK   0x7EA7EA00u
#define ESC    0x80000100u          /* user-mode escape: a load from here is an address error */
#define USR    0x000000F0u          /* user SR: MD = 0, BL = 0, IMASK = 15 */

typedef unsigned long UL;

/* ------------------------------------------------------------------ probes (probes.S) */
extern uint32_t p_ld8(uint32_t), p_ld16(uint32_t), p_ld32(uint32_t), p_ldf32(uint32_t), p_ldf64(uint32_t);
extern uint32_t p_st8(uint32_t, uint32_t), p_st16(uint32_t, uint32_t), p_st32(uint32_t, uint32_t);
extern uint32_t p_and_gbr(uint32_t), p_tst_gbr(uint32_t), p_tas(uint32_t);
extern uint32_t p_ocbi(uint32_t), p_ocbp(uint32_t), p_ocbwb(uint32_t), p_pref(uint32_t), p_movca(uint32_t, uint32_t);
extern uint32_t p_macl(uint32_t, uint32_t, uint32_t *);
extern uint32_t p_ldpi(uint32_t), p_stpd(uint32_t, uint32_t);
extern uint32_t p_pair_f(uint32_t, uint32_t), p_pair_l(uint32_t, uint32_t);
extern uint32_t p_ld_st(uint32_t, uint32_t, volatile uint32_t *), p_st_st(uint32_t, uint32_t, volatile uint32_t *);
extern uint32_t p_ld_fadd(uint32_t, uint32_t, volatile uint32_t *);
extern uint32_t p_bra_ds(uint32_t), p_jsr_ds(uint32_t, uint32_t, uint32_t);
extern uint32_t p_ifetch(uint32_t);
extern void p_dummy(void);
extern uint32_t p_user(uint32_t pc, uint32_t sr, uint32_t a6, uint32_t a7);
extern void mmucr_set(uint32_t);
extern void ldtlb_do(void);
extern uint32_t t_call(uint32_t tgt, uint32_t aa, uint32_t val, uint32_t ctr);
extern uint32_t t_chase(uint32_t p, uint32_t n, uint32_t ctr);
extern char p_ld8_at[], p_ld16_at[], p_ld32_at[], p_ldf32_at[], p_ldf64_at[], p_st8_at[], p_st16_at[], p_st32_at[];
extern char p_and_gbr_at[], p_tst_gbr_at[], p_tas_at[], p_ocbi_at[], p_ocbp_at[], p_ocbwb_at[], p_pref_at[];
extern char p_movca_at[], p_macl_at[], p_ldpi_at[], p_stpd_at[], p_pair_f_at[], p_pair_l_at[], p_ld_st_at[];
extern char p_st_st_at[], p_ld_fadd_at[], p_bra_ds_at[], p_jsr_ds_at[], p_ifetch_at[];

/* ------------------------------------------------------------------ exception capture */
typedef struct { uint32_t n, code, expevt, tea, pteh, mmucr, spc, ssr, pr, r[16], fr[16]; } exc_t;
volatile exc_t X;
volatile uint32_t g_recover, g_recover_sr, g_unexpected;

static void hnd(irq_t code, irq_context_t *c, void *d) {
    (void)d;
    X.n++; X.code = code;
    X.expevt = R32(EXPEVT); X.tea = R32(TEA); X.pteh = R32(PTEH); X.mmucr = R32(MMUCR);
    X.spc = c->pc; X.ssr = c->sr; X.pr = c->pr;
    for (int i = 0; i < 16; i++) { X.r[i] = c->r[i]; X.fr[i] = c->fr[i]; }
    if (g_recover) {
        c->pc = g_recover;
        if (g_recover_sr) c->sr = g_recover_sr;
    } else {                        /* not expected: turn translation off and step over */
        g_unexpected++;
        R32(MMUCR) = M_TI;
        c->pc += 2;
        if (g_recover_sr) c->sr = g_recover_sr;
    }
}
static const irq_t codes[] = {0x040, 0x060, 0x080, 0x0A0, 0x0C0, 0x0E0, 0x100, 0x180, 0x1A0};
#define NCODES (sizeof codes / sizeof codes[0])
static irq_cb_t old_cb[NCODES];

static void xclr(void) { X.n = 0; X.expevt = 0; X.tea = 0; X.spc = 0; X.pteh = 0; X.pr = 0; R32(TEA) = MARK; }
static void xdone(void) { g_recover = 0; }

/* ------------------------------------------------------------------ buffers and TLB helpers */
static uint8_t *codebuf, *databuf, *chasebuf;
static volatile uint32_t *sqbuf;
#define CODE(k)   ((uint32_t)(uintptr_t)(codebuf + (k) * 0x1000))
#define DATA(k)   ((uint32_t)(uintptr_t)(databuf + (k) * 0x1000))
#define CHASE(k)  ((uint32_t)(uintptr_t)(chasebuf + (k) * 0x1000))
#define VA(p1)    (0x40000000u | PA(p1))            /* P0 alias with the same low bits as the physical address */
#define SQA       (0xE0000000u | (PA(sqbuf) & 0x000FFFE0u))

enum { SZ1K = 0, SZ4K = 1, SZ64K = 2, SZ1M = 3 };
static const uint32_t szmask[4] = {0xFFFFFC00u, 0xFFFFF000u, 0xFFFF0000u, 0xFFF00000u};

static void utlb_set(int e, uint32_t va, uint32_t pa, int sz, int pr, int c, int d, int sh, int wt, int asid, int sa,
                     int tc) {
    R32(UTLB_DA2(e)) = ((uint32_t)tc << 3) | (uint32_t)sa;
    R32(UTLB_DA1(e)) = (pa & 0x1FFFFC00u) | (1u << 8) | (((uint32_t)sz & 2u) << 6) | (((uint32_t)sz & 1u) << 4) |
                       ((uint32_t)pr << 5) | ((uint32_t)c << 3) | ((uint32_t)d << 2) | ((uint32_t)sh << 1) | (uint32_t)wt;
    R32(UTLB_AA(e)) = (va & 0xFFFFFC00u) | ((uint32_t)d << 9) | (1u << 8) | ((uint32_t)asid & 0xFFu);
}
static void umap(int e, uint32_t p1, int pr, int d) {           /* 4K page, VA = P0 alias, cacheable */
    utlb_set(e, VA(p1), PA(p1), SZ4K, pr, 1, d, 0, 0, 0, 0, 0);
}
static void hard_clear(void) {                                  /* MMU must be off */
    for (int e = 0; e < 64; e++) { R32(UTLB_AA(e)) = 0; R32(UTLB_DA1(e)) = 0; R32(UTLB_DA2(e)) = 0; }
    for (int e = 0; e < 4; e++) { R32(ITLB_AA(e)) = 0; R32(ITLB_DA1(e)) = 0; R32(ITLB_DA2(e)) = 0; }
    mmucr_set(M_TI);
    mmucr_set(0);
}
static int usz(int e) { uint32_t d = R32(UTLB_DA1(e)); return (int)(((d >> 6) & 2u) | ((d >> 4) & 1u)); }
static int isz(int e) { uint32_t d = R32(ITLB_DA1(e)); return (int)(((d >> 6) & 2u) | ((d >> 4) & 1u)); }
/* refuse overlapping valid entries (ASID ignored: conservative) */
static int tlb_check(void) {
    int bad = 0;
    for (int i = 0; i < 64; i++) {
        uint32_t a = R32(UTLB_AA(i));
        if (!(a & 0x100)) continue;
        for (int j = i + 1; j < 64; j++) {
            uint32_t b = R32(UTLB_AA(j));
            if (!(b & 0x100)) continue;
            uint32_t m = szmask[usz(i)] & szmask[usz(j)];
            if (((a ^ b) & m) == 0) { printf("  !! UTLB overlap %d %d (%08lx %08lx)\n", i, j, (UL)a, (UL)b); bad++; }
        }
    }
    for (int i = 0; i < 4; i++) {
        uint32_t a = R32(ITLB_AA(i));
        if (!(a & 0x100)) continue;
        for (int j = i + 1; j < 4; j++) {
            uint32_t b = R32(ITLB_AA(j));
            if (!(b & 0x100)) continue;
            uint32_t m = szmask[isz(i)] & szmask[isz(j)];
            if (((a ^ b) & m) == 0) { printf("  !! ITLB overlap %d %d\n", i, j); bad++; }
        }
    }
    return bad;
}
static int at_on(uint32_t bits) {
    if (tlb_check()) { printf("  !! translation NOT enabled\n"); return -1; }
    mmucr_set(bits | M_AT);
    return 0;
}
static void at_off(void) { mmucr_set(0); }
static uint32_t urc(void) { return G_URC(R32(MMUCR)); }

/* which code page (0..15) an ITLB entry maps, '-' if invalid, '?' otherwise */
static char ipage(int e) {
    uint32_t a = R32(ITLB_AA(e));
    if (!(a & 0x100)) return '-';
    uint32_t v = a & 0xFFFFF000u;
    for (int k = 0; k < 16; k++) if (v == (VA(CODE(k)) & 0xFFFFF000u)) return (char)(k < 10 ? '0' + k : 'a' + k - 10);
    return '?';
}
static int find_itlb(uint32_t va) {
    for (int e = 0; e < 4; e++) {
        uint32_t a = R32(ITLB_AA(e));
        if ((a & 0x100) && ((a ^ va) & 0xFFFFF000u) == 0) return e;
    }
    return -1;
}
static void delay(int n) { for (volatile int i = 0; i < n; i++) ; }

/* ================================================================== A: registers */
static void grp_A(void) {
    static const uint32_t regs[5] = {PTEH, PTEL, PTEA, TTB, TEA};
    static const char *nm[5] = {"PTEH", "PTEL", "PTEA", "TTB", "TEA"};
    uint32_t r1[5], r0[5], r2[5], old[5];
    printf("A0 initial PTEH %08lx PTEL %08lx PTEA %08lx TTB %08lx TEA %08lx MMUCR %08lx CCR %08lx QACR %08lx %08lx\n",
           (UL)R32(PTEH), (UL)R32(PTEL), (UL)R32(PTEA), (UL)R32(TTB), (UL)R32(TEA), (UL)R32(MMUCR), (UL)R32(CCR),
           (UL)R32(QACR0), (UL)R32(QACR1));
    int irq = irq_disable();
    for (int i = 0; i < 5; i++) {
        old[i] = R32(regs[i]);
        R32(regs[i]) = 0xFFFFFFFFu; r1[i] = R32(regs[i]);
        R32(regs[i]) = 0; r0[i] = R32(regs[i]);
        R32(regs[i]) = 0x5A5AA5A5u; r2[i] = R32(regs[i]);
        R32(regs[i]) = old[i];
    }
    uint32_t m1, m2, m3;
    mmucr_set(M_LRUI(0x3F) | M_URB(0x3F) | M_URC(0x3F) | M_SQMD | M_SV | M_TI); m1 = R32(MMUCR);
    mmucr_set(M_LRUI(0x07) | M_URB(0x15) | M_URC(0x2A)); m2 = R32(MMUCR);
    mmucr_set(M_LRUI(0x38) | M_TI); m3 = R32(MMUCR);
    mmucr_set(0);
    /* TI: one UTLB and one ITLB entry with everything set, then TI */
    uint32_t b[6], a[6];
    R32(UTLB_DA2(5)) = 0xD; R32(UTLB_DA1(5)) = 0x0ABCD000u | 0x1FFu; R32(UTLB_AA(5)) = 0x12345000u | 0x300u | 0x33u;
    R32(ITLB_DA2(2)) = 0xA; R32(ITLB_DA1(2)) = 0x0BCDE000u | 0x1FFu; R32(ITLB_AA(2)) = 0x23456000u | 0x100u | 0x44u;
    b[0] = R32(UTLB_AA(5)); b[1] = R32(UTLB_DA1(5)); b[2] = R32(UTLB_DA2(5));
    b[3] = R32(ITLB_AA(2)); b[4] = R32(ITLB_DA1(2)); b[5] = R32(ITLB_DA2(2));
    mmucr_set(M_TI);
    a[0] = R32(UTLB_AA(5)); a[1] = R32(UTLB_DA1(5)); a[2] = R32(UTLB_DA2(5));
    a[3] = R32(ITLB_AA(2)); a[4] = R32(ITLB_DA1(2)); a[5] = R32(ITLB_DA2(2));
    uint32_t m4 = R32(MMUCR);
    hard_clear();
    irq_restore(irq);
    for (int i = 0; i < 5; i++)
        printf("A1 %-4s wr ffffffff rd %08lx | wr 0 rd %08lx | wr 5a5aa5a5 rd %08lx\n", nm[i], (UL)r1[i], (UL)r0[i],
               (UL)r2[i]);
    printf("A2 MMUCR wr fcfcff04 rd %08lx | wr 1c56a800 rd %08lx | wr e0000004 rd %08lx\n", (UL)m1, (UL)m2, (UL)m3);
    printf("A3 TI  before: UTLB5 aa %08lx da1 %08lx da2 %08lx  ITLB2 aa %08lx da1 %08lx da2 %08lx\n", (UL)b[0], (UL)b[1],
           (UL)b[2], (UL)b[3], (UL)b[4], (UL)b[5]);
    printf("A3 TI  after : UTLB5 aa %08lx da1 %08lx da2 %08lx  ITLB2 aa %08lx da1 %08lx da2 %08lx  MMUCR %08lx\n",
           (UL)a[0], (UL)a[1], (UL)a[2], (UL)a[3], (UL)a[4], (UL)a[5], (UL)m4);
}

/* ================================================================== B: memory-mapped arrays */
static char aline[12][160]; static int naline;
static void assoc_case(const char *nm, uint32_t data, int ue, int ie) {
    uint32_t m0 = R32(MMUCR);
    R32(UTLB_ASSOC) = data;
    uint32_t m1 = R32(MMUCR);
    if (naline < 12)
        snprintf(aline[naline++], sizeof aline[0], "  %-34s -> UTLB%d aa %08lx da1 %08lx  ITLB%d aa %08lx da1 %08lx  urc %lu->%lu",
                 nm, ue, (UL)R32(UTLB_AA(ue)), (UL)R32(UTLB_DA1(ue)), ie, (UL)R32(ITLB_AA(ie)), (UL)R32(ITLB_DA1(ie)),
                 (UL)G_URC(m0), (UL)G_URC(m1));
}
static void assoc_setup(uint32_t X0, int uv, int ud, int uasid, int ush, int iv) {
    /* UTLB 20 and ITLB 1 map VPN X0 (4K); the rest of both TLBs is invalid */
    R32(UTLB_DA2(20)) = 0;
    R32(UTLB_DA1(20)) = 0x0C000000u | ((uint32_t)uv << 8) | (1u << 4) | (3u << 5) | ((uint32_t)ud << 2) |
                        ((uint32_t)ush << 1);
    R32(UTLB_AA(20)) = X0 | ((uint32_t)ud << 9) | ((uint32_t)uv << 8) | (uint32_t)uasid;
    R32(ITLB_DA2(1)) = 0;
    R32(ITLB_DA1(1)) = 0x0C000000u | ((uint32_t)iv << 8) | (1u << 4) | (1u << 6);
    R32(ITLB_AA(1)) = X0 | ((uint32_t)iv << 8);
}
static void assoc_suite(int at) {
    const uint32_t X0 = 0x2A000000u;   /* nothing else maps it */
    naline = 0;
    hard_clear();
    R32(PTEH) = 0;
    assoc_setup(X0, 1, 0, 0, 0, 1);
    if (at && at_on(0)) return;
    assoc_case("a: VPN+D1+V1 (U:V1 D0, I:V1)", X0 | 0x300, 20, 1);
    assoc_case("b: VPN+D0+V0", X0 | 0x000, 20, 1);
    at_off(); assoc_setup(X0, 1, 0, 0, 0, 1); if (at && at_on(0)) return;
    assoc_case("c: VPN+0x800 (same page) V0", (X0 + 0x800) | 0x000, 20, 1);
    at_off(); assoc_setup(X0, 1, 0, 0x55, 0, 1); if (at && at_on(0)) return;
    assoc_case("d: UTLB ASID55 SH0, PTEH.ASID0, V0", X0 | 0x000, 20, 1);
    at_off(); assoc_setup(X0, 1, 0, 0x55, 1, 1); if (at && at_on(0)) return;
    assoc_case("e: UTLB ASID55 SH1, V0", X0 | 0x000, 20, 1);
    at_off(); assoc_setup(X0, 0, 0, 0, 0, 1); if (at && at_on(0)) return;
    assoc_case("f: UTLB V0, ITLB V1: write V0", X0 | 0x000, 20, 1);
    at_off(); assoc_setup(X0, 1, 0, 0, 0, 0); if (at && at_on(0)) return;
    assoc_case("g: UTLB V1, ITLB V0: write V1 D1", X0 | 0x300, 20, 1);
    at_off(); assoc_setup(X0, 1, 1, 0, 0, 1); if (at && at_on(0)) return;
    assoc_case("h: UTLB V1 D1, ITLB V1: write V1 D0", X0 | 0x100, 20, 1);
    at_off();
    hard_clear();
}
static void grp_B(void) {
    uint32_t v[40]; int k = 0;
    int irq = irq_disable();
    hard_clear();
    /* ITLB entry 1: bits */
    R32(ITLB_AA(1)) = 0xFFFFFFFFu; v[k++] = R32(ITLB_AA(1)); v[k++] = R32(ITLB_DA1(1));
    R32(ITLB_DA1(1)) = 0; v[k++] = R32(ITLB_AA(1)); v[k++] = R32(ITLB_DA1(1));
    R32(ITLB_DA1(1)) = 0xFFFFFFFFu; v[k++] = R32(ITLB_DA1(1)); v[k++] = R32(ITLB_AA(1));
    R32(ITLB_DA2(1)) = 0xFFFFFFFFu; v[k++] = R32(ITLB_DA2(1)); v[k++] = R32(ITLB_DA1(1));
    R32(ITLB_AA(1)) = 0; v[k++] = R32(ITLB_AA(1)); v[k++] = R32(ITLB_DA1(1)); v[k++] = R32(ITLB_DA2(1));
    /* UTLB entry 37: bits */
    R32(UTLB_AA(37)) = 0xFFFFFFFFu; v[k++] = R32(UTLB_AA(37)); v[k++] = R32(UTLB_DA1(37));
    R32(UTLB_DA1(37)) = 0; v[k++] = R32(UTLB_AA(37)); v[k++] = R32(UTLB_DA1(37));
    R32(UTLB_DA1(37)) = 0xFFFFFFFFu; v[k++] = R32(UTLB_DA1(37)); v[k++] = R32(UTLB_AA(37));
    R32(UTLB_DA2(37)) = 0xFFFFFFFFu; v[k++] = R32(UTLB_DA2(37)); v[k++] = R32(UTLB_DA1(37));
    R32(UTLB_AA(37)) = 0x200u; v[k++] = R32(UTLB_AA(37)); v[k++] = R32(UTLB_DA1(37));
    R32(UTLB_AA(37)) = 0; v[k++] = R32(UTLB_AA(37)); v[k++] = R32(UTLB_DA1(37)); v[k++] = R32(UTLB_DA2(37));
    /* ITLB entry decode: V = 0, distinct VPN / ASID per entry */
    uint32_t ie[4], ie16[4], ie80[4], ieff[4];
    for (int e = 0; e < 4; e++) R32(ITLB_AA(e)) = (0x10000000u * (uint32_t)(e + 1)) | (uint32_t)(0x11 * (e + 1));
    for (int e = 0; e < 4; e++) {
        ie[e] = R32(ITLB_AA(e));
        ie16[e] = R32(0xF2000000u | ((uint32_t)e << 16));
        ie80[e] = R32(ITLB_AA(e) | 0x80u);
        ieff[e] = R32(ITLB_AA(e) | 0x00FFFC00u);
    }
    /* UTLB entry decode */
    int ubad = 0; uint32_t ualias[4], ua80, uda2;
    for (int e = 0; e < 64; e++) R32(UTLB_AA(e)) = ((uint32_t)e << 12) | (uint32_t)e;
    for (int e = 0; e < 64; e++) if (R32(UTLB_AA(e)) != (((uint32_t)e << 12) | (uint32_t)e)) ubad++;
    ualias[0] = R32(0xF6000000u | (5u << 8) | 0x00FFC000u);
    ualias[1] = R32(0xF6000000u | (5u << 8) | 0x7Fu);
    ualias[2] = R32(0xF7000000u | (5u << 8) | 0x007FC000u);
    ualias[3] = R32(0xF7000000u | (5u << 8) | 0x00800000u);
    ua80 = R32(UTLB_AA(5) | 0x80u);
    R32(UTLB_DA2(9)) = 0x5; uda2 = R32(0xF7800000u | (9u << 8) | 0x7FC000u);
    hard_clear();
    irq_restore(irq);
    k = 0;
    printf("B1 ITLB1 aa=ffffffff: aa %08lx da1 %08lx | da1=0: aa %08lx da1 %08lx | da1=ffffffff: da1 %08lx aa %08lx\n",
           (UL)v[0], (UL)v[1], (UL)v[2], (UL)v[3], (UL)v[4], (UL)v[5]);
    printf("B1 ITLB1 da2=ffffffff: da2 %08lx da1 %08lx | aa=0: aa %08lx da1 %08lx da2 %08lx\n", (UL)v[6], (UL)v[7],
           (UL)v[8], (UL)v[9], (UL)v[10]);
    printf("B2 UTLB37 aa=ffffffff: aa %08lx da1 %08lx | da1=0: aa %08lx da1 %08lx | da1=ffffffff: da1 %08lx aa %08lx\n",
           (UL)v[11], (UL)v[12], (UL)v[13], (UL)v[14], (UL)v[15], (UL)v[16]);
    printf("B2 UTLB37 da2=ffffffff: da2 %08lx da1 %08lx | aa=200: aa %08lx da1 %08lx | aa=0: aa %08lx da1 %08lx da2 %08lx\n",
           (UL)v[17], (UL)v[18], (UL)v[19], (UL)v[20], (UL)v[21], (UL)v[22], (UL)v[23]);
    for (int e = 0; e < 4; e++)
        printf("B3 ITLB%d (e<<8) %08lx  (e<<16) %08lx  (|80) %08lx  (|ffc00) %08lx\n", e, (UL)ie[e], (UL)ie16[e],
               (UL)ie80[e], (UL)ieff[e]);
    printf("B4 UTLB decode e<<8: %d mismatches; aa5|ffc000 %08lx aa5|7f %08lx da1-5|7fc000 %08lx da1-5|800000(=da2) %08lx "
           "aa5|80 %08lx da2-9|7fc000 %08lx\n", ubad, (UL)ualias[0], (UL)ualias[1], (UL)ualias[2], (UL)ualias[3],
           (UL)ua80, (UL)uda2);
    for (int at = 0; at < 2; at++) {
        irq = irq_disable();
        assoc_suite(at);
        at_off();
        hard_clear();
        irq_restore(irq);
        printf("B5 associative UTLB writes, AT=%d (UTLB20 / ITLB1 hold VPN 2a000000, 4K)\n", at);
        for (int i = 0; i < naline; i++) printf("%s\n", aline[i]);
    }
}

/* ================================================================== C: LDTLB */
static void grp_C(void) {
    uint32_t o0 = R32(PTEH), o1 = R32(PTEL), o2 = R32(PTEA);
    uint32_t r[2][4];
    int irq = irq_disable();
    hard_clear();
    R32(PTEH) = 0xFFFFFFFFu; R32(PTEL) = 0xFFFFFFFFu; R32(PTEA) = 0xFFFFFFFFu;
    mmucr_set(M_URC(0x2A));
    ldtlb_do();
    r[0][0] = R32(UTLB_AA(0x2A)); r[0][1] = R32(UTLB_DA1(0x2A)); r[0][2] = R32(UTLB_DA2(0x2A)); r[0][3] = R32(MMUCR);
    R32(PTEH) = 0x87654021u; R32(PTEL) = 0x0ABCD000u | 0x100u | 0x80u | 0x40u | 0x04u | 0x01u; R32(PTEA) = 0x6u;
    mmucr_set(M_URC(0x15) | M_URB(0x10));
    ldtlb_do();
    r[1][0] = R32(UTLB_AA(0x15)); r[1][1] = R32(UTLB_DA1(0x15)); r[1][2] = R32(UTLB_DA2(0x15)); r[1][3] = R32(MMUCR);
    R32(PTEH) = o0; R32(PTEL) = o1; R32(PTEA) = o2;
    hard_clear();
    irq_restore(irq);
    printf("C1 LDTLB all-ones -> UTLB42 aa %08lx da1 %08lx da2 %08lx MMUCR %08lx\n", (UL)r[0][0], (UL)r[0][1],
           (UL)r[0][2], (UL)r[0][3]);
    printf("C2 LDTLB PTEH 87654021 PTEL 0abcd1c5 PTEA 6, URC 21 URB 16 -> UTLB21 aa %08lx da1 %08lx da2 %08lx MMUCR %08lx\n",
           (UL)r[1][0], (UL)r[1][1], (UL)r[1][2], (UL)r[1][3]);
}

/* ================================================================== D: URC */
static void grp_D(void) {
    static const char *nm[48]; uint32_t u[48]; uint32_t ev[48]; int k = 0;
    uint32_t mac[4];
#define REC(s) do { nm[k] = (s); ev[k] = X.n ? X.expevt : 0; u[k++] = urc(); xclr(); } while (0)
    int irq = irq_disable();
    hard_clear();
    umap(16, DATA(0), 3, 1);
    umap(17, DATA(1), 0, 1);
    umap(0, CODE(0), 3, 1);
    utlb_set(32, 0xE0000000u, PA(sqbuf) & 0x1FF00000u, SZ1M, 1, 0, 1, 0, 0, 0, 0, 0);
    xclr();
    /* MMU off */
    mmucr_set(M_URC(5));
    REC("AT0 start");
    p_ld32(DATA(0)); xdone(); REC("AT0 P1 load");
    p_ld32(VA(DATA(0))); xdone(); REC("AT0 P0 load");
    (void)R32(UTLB_AA(16)); REC("AT0 UTLB aa read");
    R32(UTLB_ASSOC) = 0x2A000000u; REC("AT0 assoc write (no match)");
    /* MMU on */
    if (at_on(M_URC(5))) { irq_restore(irq); return; }
    REC("AT1 start");
    p_ld32(DATA(0)); xdone(); REC("P1 load");
    p_st32(DATA(0) + 0x40, 1); xdone(); REC("P1 store");
    p_ld32(VA(DATA(0))); xdone(); REC("P0 load");
    p_st32(VA(DATA(0)) + 0x40, 2); xdone(); REC("P0 store");
    p_ld32(VA(DATA(0))); p_ld32(VA(DATA(0)) + 4); p_ld32(VA(DATA(0)) + 8); p_ld32(VA(DATA(0)) + 12); xdone();
    REC("4 P0 loads");
    p_ld8(VA(DATA(0)) + 1); xdone(); REC("P0 load byte");
    p_ldf32(VA(DATA(0))); xdone(); REC("P0 fmov.s load");
    p_ldf64(VA(DATA(0))); xdone(); REC("P0 fmov dr load");
    p_pref(VA(DATA(0)) + 0x60); xdone(); REC("P0 pref");
    p_ocbwb(VA(DATA(0)) + 0x60); xdone(); REC("P0 ocbwb");
    p_ocbp(VA(DATA(0)) + 0x60); xdone(); REC("P0 ocbp");
    p_macl(VA(DATA(0)) + 0x80, VA(DATA(0)) + 0x88, mac); xdone(); REC("P0 mac.l (2 reads)");
    p_tst_gbr(VA(DATA(0)) + 0xA0); xdone(); REC("P0 tst.b gbr");
    p_and_gbr(VA(DATA(0)) + 0xA0); xdone(); REC("P0 and.b gbr (rmw)");
    p_tas(VA(DATA(0)) + 0xA1); xdone(); REC("P0 tas.b");
    p_ld32(VA(DATA(0)) + 2); xdone(); REC("P0 misaligned load (AE)");
    p_ld32(VA(DATA(7))); xdone(); REC("P0 load TLB miss");
    p_st32(VA(DATA(1)), 3); xdone(); REC("P0 store prot viol");
    (void)R32(UTLB_AA(16)); REC("UTLB aa read");
    (void)R32(UTLB_DA1(16)); REC("UTLB da1 read");
    R32(UTLB_AA(60)) = 0; REC("UTLB aa write (non-assoc)");
    R32(UTLB_ASSOC) = 0x2A000000u; REC("UTLB assoc write (no match)");
    R32(UTLB_ASSOC) = (VA(DATA(0)) & 0xFFFFF000u) | 0x300u; REC("UTLB assoc write (match)");
    (void)R32(ITLB_AA(0)); REC("ITLB aa read");
    p_ifetch(VA(CODE(0))); xdone(); REC("P0 ifetch (ITLB miss->refill)");
    p_ifetch(VA(CODE(0))); xdone(); REC("P0 ifetch (ITLB hit)");
    p_ifetch(VA(CODE(15))); xdone(); REC("P0 ifetch (UTLB miss exc)");
    p_st32(SQA, 0x11); xdone(); REC("SQ store (mapped)");
    p_pref(SQA); xdone(); REC("SQ pref (mapped)");
    R32(PTEH) = 0; R32(PTEL) = 0; R32(PTEA) = 0;
    mmucr_set(M_AT | M_URC(60)); REC("MMUCR URC=60");
    ldtlb_do(); REC("LDTLB");
    p_ld32(VA(DATA(0))); xdone(); REC("P0 load");
    at_off();
    int k1 = k;
    /* wrap behaviour: 8 P0 loads after setting URB / URC */
    static const uint8_t wr[6][2] = {{8, 6}, {8, 0x3D}, {0, 0x3E}, {1, 0}, {0x3F, 0x3D}, {2, 1}};
    uint32_t w[6][9];
    for (int t = 0; t < 6; t++) {
        mmucr_set(M_AT | M_URB(wr[t][0]) | M_URC(wr[t][1]));
        w[t][0] = urc();
        for (int i = 1; i < 9; i++) { p_ld32(VA(DATA(0))); xdone(); w[t][i] = urc(); }
    }
    at_off();
    hard_clear();
    irq_restore(irq);
    printf("D1 URC after each step (URB = 0; exc code if one was taken):\n");
    for (int i = 0; i < k1; i++) printf("  %-32s urc %2lu%s%s\n", nm[i], (UL)u[i], ev[i] ? "  exc " : "",
                                        ev[i] ? (ev[i] == 0x40 ? "040" : ev[i] == 0x60 ? "060" : ev[i] == 0xC0 ? "0c0" :
                                                 ev[i] == 0xE0 ? "0e0" : "other") : "");
    for (int t = 0; t < 6; t++) {
        printf("D2 URB %2u URC %2u : %2lu ->", wr[t][0], wr[t][1], (UL)w[t][0]);
        for (int i = 1; i < 9; i++) printf(" %lu", (UL)w[t][i]);
        printf("\n");
    }
#undef REC
}

/* ================================================================== E: ITLB */
static void e_setup5(void) { hard_clear(); for (int k = 0; k < 5; k++) umap(k, CODE(k), 3, 1); }
static void grp_E(void) {
    char line[96];
    int irq = irq_disable();
    /* E1: LRUI sequence from LRUI = 0, empty ITLB */
    static const int seq[] = {0, 1, 2, 3, 0, 4, 1, 0, 2, 3, 4, 4, 1};
    char res[13][40];
    e_setup5();
    if (at_on(M_LRUI(0))) { irq_restore(irq); return; }
    for (unsigned i = 0; i < sizeof seq / sizeof seq[0]; i++) {
        xclr(); p_ifetch(VA(CODE(seq[i]))); xdone();
        uint32_t m = R32(MMUCR);
        snprintf(res[i], sizeof res[i], "p%d:%02lx[%c%c%c%c]%s", seq[i], (UL)G_LRUI(m), ipage(0), ipage(1), ipage(2),
                 ipage(3), X.n ? "X" : "");
    }
    at_off();
    irq_restore(irq);
    printf("E1 fetch page:LRUI-after[ITLB e0..e3] ...\n  ");
    for (unsigned i = 0; i < sizeof seq / sizeof seq[0]; i++) printf("%s ", res[i]);
    printf("\n");
    /* E1b: replacement with all four valid, LRUI forced */
    static const uint8_t lv[] = {0x38, 0x06, 0x01, 0x00, 0x3F, 0x1E, 0x07};
    for (unsigned t = 0; t < sizeof lv; t++) {
        irq = irq_disable();
        e_setup5();
        if (at_on(M_LRUI(0))) { irq_restore(irq); return; }
        for (int p = 0; p < 4; p++) { xclr(); p_ifetch(VA(CODE(p))); xdone(); }
        uint32_t m0 = R32(MMUCR);
        char before[5] = {ipage(0), ipage(1), ipage(2), ipage(3), 0};
        mmucr_set(M_AT | M_LRUI(lv[t]));
        xclr(); p_ifetch(VA(CODE(4))); xdone();
        uint32_t m1 = R32(MMUCR);
        snprintf(line, sizeof line, "E1b lrui(fill)=%02lx itlb[%s] set %02x -> fetch p4: lrui %02lx itlb [%c %c %c %c]%s",
                 (UL)G_LRUI(m0), before, lv[t], (UL)G_LRUI(m1), ipage(0), ipage(1), ipage(2), ipage(3), X.n ? " EXC" : "");
        at_off();
        irq_restore(irq);
        printf("%s\n", line);
    }
    /* E1c: invalid entries are not preferred?  e3 valid (page 3), e0..e2 invalid, LRUI = 0 -> page 0 */
    irq = irq_disable();
    e_setup5();
    at_on(M_LRUI(0));
    xclr(); p_ifetch(VA(CODE(3))); xdone();
    char c1[5] = {ipage(0), ipage(1), ipage(2), ipage(3), 0};
    mmucr_set(M_AT | M_LRUI(0));
    xclr(); p_ifetch(VA(CODE(0))); xdone();
    snprintf(line, sizeof line, "E1c after p3 [%s]; LRUI:=0, fetch p0 -> itlb [%c %c %c %c] lrui %02lx", c1, ipage(0),
             ipage(1), ipage(2), ipage(3), (UL)G_LRUI(R32(MMUCR)));
    at_off();
    irq_restore(irq);
    printf("%s\n", line);
    /* E2: what the refill copies (4K page fetched at +0xC00; SH=1 ASID 5a WT=1 SA=5 TC=1) */
    uint32_t e2[12];
    irq = irq_disable();
    hard_clear();
    utlb_set(0, VA(CODE(0)), PA(CODE(0)), SZ4K, 3, 1, 1, 1, 1, 0x5A, 5, 1);
    R32(PTEH) = 0; R32(TEA) = MARK;
    at_on(M_LRUI(0));
    xclr(); R32(TEA) = MARK;
    p_ifetch(VA(CODE(0)) + 0xC00); xdone();
    e2[0] = R32(ITLB_AA(3)); e2[1] = R32(ITLB_DA1(3)); e2[2] = R32(ITLB_DA2(3));
    e2[3] = R32(UTLB_AA(0)); e2[4] = R32(UTLB_DA1(0)); e2[5] = R32(UTLB_DA2(0));
    e2[6] = R32(TEA); e2[7] = R32(PTEH); e2[8] = X.n;
    at_off();
    /* 1M page: fetch at +offset+0x800 */
    hard_clear();
    uint32_t v1m = 0x60000000u | (PA(CODE(0)) & 0x0FF00000u);
    utlb_set(5, v1m, PA(CODE(0)) & 0x1FF00000u, SZ1M, 2, 1, 1, 0, 0, 0, 0, 0);
    at_on(M_LRUI(0));
    xclr(); p_ifetch(v1m + (PA(CODE(0)) & 0xFFFFFu) + 0x800); xdone();
    e2[9] = R32(ITLB_AA(3)); e2[10] = R32(ITLB_DA1(3)); e2[11] = X.n;
    at_off();
    hard_clear();
    irq_restore(irq);
    printf("E2 4K fetch at %08lx: ITLB3 aa %08lx da1 %08lx da2 %08lx | UTLB0 aa %08lx da1 %08lx da2 %08lx | TEA %08lx "
           "PTEH %08lx exc %lu\n", (UL)(VA(CODE(0)) + 0xC00), (UL)e2[0], (UL)e2[1], (UL)e2[2], (UL)e2[3], (UL)e2[4],
           (UL)e2[5], (UL)e2[6], (UL)e2[7], (UL)e2[8]);
    printf("E2 1M (PR=10) fetch at %08lx: ITLB3 aa %08lx da1 %08lx exc %lu\n",
           (UL)(v1m + (PA(CODE(0)) & 0xFFFFFu) + 0x800), (UL)e2[9], (UL)e2[10], (UL)e2[11]);
    /* E4: fetch near the end of a page */
    for (int var = 0; var < 4; var++) {
        int map6 = !(var & 1);
        perf_cntr_event_t evm = (var & 2) ? PMCR_UTLB_MISS_MODE : PMCR_INSTRUCTION_TLB_MISS_MODE;
        irq = irq_disable();
        hard_clear();
        umap(5, CODE(5), 3, 1);
        if (map6) umap(6, CODE(6), 3, 1);
        at_on(M_LRUI(0));
        perf_cntr_start(PRFC1, evm, PMCR_COUNT_CPU_CYCLES);
        uint64_t c0 = perf_cntr_count(PRFC1);
        xclr(); p_ifetch(VA(CODE(5)) + 0xFFC); xdone();
        uint64_t c1 = perf_cntr_count(PRFC1);
        snprintf(line, sizeof line, "E4 rts;nop at end of p5, p6 %s: %s count %lu, itlb [%c %c %c %c], exc %lu ev %03lx",
                 map6 ? "mapped  " : "unmapped", (var & 2) ? "UTLB-miss" : "ITLB-miss", (UL)(c1 - c0), ipage(0),
                 ipage(1), ipage(2), ipage(3), (UL)X.n, (UL)X.expevt);
        at_off();
        irq_restore(irq);
        printf("%s\n", line);
    }
    /* E4c/d: rts at p7+0xFFE, its delay slot at p8+0 */
    for (int var = 0; var < 2; var++) {
        irq = irq_disable();
        hard_clear();
        umap(7, CODE(7), 3, 1);
        if (var) umap(8, CODE(8), 3, 1);
        at_on(M_LRUI(0));
        perf_cntr_start(PRFC1, PMCR_INSTRUCTION_TLB_MISS_MODE, PMCR_COUNT_CPU_CYCLES);
        uint64_t c0 = perf_cntr_count(PRFC1);
        xclr(); R32(PTEH) = 0; p_ifetch(VA(CODE(7)) + 0xFFE); xdone();
        uint64_t c1 = perf_cntr_count(PRFC1);
        long spc = X.n ? (long)X.spc - (long)(VA(CODE(7)) + 0xFFE) : 0;
        long pr = X.n ? (long)X.pr - (long)((uintptr_t)p_ifetch_at + 4) : 0;
        snprintf(line, sizeof line, "E4%c delay slot in p8 (%s): exc %lu ev %03lx tea %08lx pteh %08lx spc-rts %+ld pr-ret %+ld itlbmiss %lu",
                 var ? 'd' : 'c', var ? "mapped" : "unmapped", (UL)X.n, (UL)X.expevt, (UL)X.tea, (UL)X.pteh, spc, pr,
                 (UL)(c1 - c0));
        at_off();
        irq_restore(irq);
        printf("%s   (p8 VA %08lx)\n", line, (UL)VA(CODE(8)));
    }
    hard_clear();
}

/* ================================================================== F: data TLB exceptions */
static void grp_F(void) {
    volatile uint32_t sink = 0, fout = 0; uint32_t mac[4], r;
    const uint32_t d0 = VA(DATA(0)), d1 = VA(DATA(1)), d2 = VA(DATA(2)), d3 = VA(DATA(3)), d4 = VA(DATA(4)),
                   d5 = VA(DATA(5)), un = VA(DATA(7));
    printf("F  (dp0 PR11 D1, dp1 PR00 D1, dp2 PR10 D1, dp3 PR01 D0, dp4 PR00 D0, dp5 PR11 D0, dp7 unmapped = %08lx)\n",
           (UL)un);
    struct { const char *nm; const void *at; uint32_t r, n, ev, tea, pteh, spc, pr; } L[64]; int k = 0;
#define RUN(name, at_, expr) do { xclr(); R32(PTEH) = 0; r = (expr); xdone(); \
        L[k].nm = name; L[k].at = at_; L[k].r = r; L[k].n = X.n; L[k].ev = X.expevt; L[k].tea = X.tea; \
        L[k].pteh = X.pteh; L[k].spc = X.spc; L[k].pr = X.pr; k++; } while (0)
    int irq = irq_disable();
    hard_clear();
    umap(16, DATA(0), 3, 1); umap(17, DATA(1), 0, 1); umap(18, DATA(2), 2, 1);
    umap(19, DATA(3), 1, 0); umap(20, DATA(4), 0, 0); umap(21, DATA(5), 3, 0);
    if (at_on(0)) { irq_restore(irq); return; }
    RUN("ld32 unmapped", p_ld32_at, p_ld32(un + 0x124));
    RUN("st32 unmapped", p_st32_at, p_st32(un + 0x124, 1));
    RUN("ld8 unmapped", p_ld8_at, p_ld8(un + 0x3FF));
    RUN("ld16 unmapped +1 (misal)", p_ld16_at, p_ld16(un + 1));
    RUN("st32 unmapped +2 (misal)", p_st32_at, p_st32(un + 2, 1));
    RUN("ld32 dp0 +2 (misal)", p_ld32_at, p_ld32(d0 + 2));
    RUN("ldf64 dp0 +4 (misal)", p_ldf64_at, p_ldf64(d0 + 4));
    RUN("ldf64 unmapped", p_ldf64_at, p_ldf64(un));
    RUN("ld32 dp1 PR00", p_ld32_at, p_ld32(d1));
    RUN("st32 dp1 PR00", p_st32_at, p_st32(d1, 1));
    RUN("ld32 dp2 PR10", p_ld32_at, p_ld32(d2));
    RUN("st32 dp2 PR10", p_st32_at, p_st32(d2, 1));
    RUN("ld32 dp3 PR01 D0", p_ld32_at, p_ld32(d3));
    RUN("st32 dp3 PR01 D0", p_st32_at, p_st32(d3, 1));
    RUN("st32 dp4 PR00 D0", p_st32_at, p_st32(d4, 1));
    RUN("st8 dp5 PR11 D0", p_st8_at, p_st8(d5 + 3, 1));
    RUN("st32 dp0 PR11 D1", p_st32_at, p_st32(d0 + 0x100, 0x12345678));
    RUN("and.b gbr unmapped", p_and_gbr_at, p_and_gbr(un));
    RUN("and.b gbr dp1 PR00", p_and_gbr_at, p_and_gbr(d1 + 0x10));
    RUN("and.b gbr dp3 D0", p_and_gbr_at, p_and_gbr(d3 + 0x10));
    RUN("tst.b gbr dp1 PR00", p_tst_gbr_at, p_tst_gbr(d1 + 0x10));
    RUN("tst.b gbr unmapped", p_tst_gbr_at, p_tst_gbr(un));
    RUN("tas.b unmapped", p_tas_at, p_tas(un + 5));
    RUN("tas.b dp1 PR00", p_tas_at, p_tas(d1 + 0x11));
    RUN("tas.b dp3 D0", p_tas_at, p_tas(d3 + 0x11));
    RUN("ocbi unmapped", p_ocbi_at, p_ocbi(un + 0x40));
    RUN("ocbp unmapped", p_ocbp_at, p_ocbp(un + 0x40));
    RUN("ocbwb unmapped", p_ocbwb_at, p_ocbwb(un + 0x40));
    RUN("pref unmapped", p_pref_at, p_pref(un + 0x40));
    RUN("movca unmapped", p_movca_at, p_movca(un + 0x40, 1));
    RUN("ocbi dp1 PR00", p_ocbi_at, p_ocbi(d1 + 0x40));
    RUN("ocbi dp3 D0", p_ocbi_at, p_ocbi(d3 + 0x40));
    RUN("ocbp dp1 PR00", p_ocbp_at, p_ocbp(d1 + 0x40));
    RUN("ocbwb dp1 PR00", p_ocbwb_at, p_ocbwb(d1 + 0x40));
    RUN("movca dp1 PR00", p_movca_at, p_movca(d1 + 0x40, 1));
    RUN("movca dp3 D0", p_movca_at, p_movca(d3 + 0x40, 1));
    RUN("pref dp1 PR00", p_pref_at, p_pref(d1 + 0x40));
    RUN("ocbi misaligned dp0+3", p_ocbi_at, p_ocbi(d0 + 0x203));
    RUN("mac.l dp0, unmapped", p_macl_at, (p_macl(d0 + 0x80, un + 8, mac), mac[0]));
    uint32_t m1a = mac[0], m1b = mac[1];
    RUN("mac.l unmapped, dp0", p_macl_at, (p_macl(un + 8, d0 + 0x80, mac), mac[0]));
    uint32_t m2a = mac[0], m2b = mac[1];
    RUN("mov.l @r4+ unmapped (r4)", p_ldpi_at, p_ldpi(un + 0x20));
    RUN("mov.l r5,@-r4 unmapped (r4)", p_stpd_at, p_stpd(un + 0x24, 7));
    RUN("add;ld pair, ld faults (r5)", p_pair_f_at, p_pair_f(un, 100));
    RUN("ld;add pair, ld faults (r5)", p_pair_l_at, p_pair_l(un, 100));
    RUN("add;ld pair, no fault (r5)", p_pair_f_at, p_pair_f(d0, 100));
    sink = 0;
    RUN("ld faults; st next (sink)", p_ld_st_at, (p_ld_st(un, 0x1234, &sink), sink));
    sink = 0;
    RUN("st faults; st next (sink)", p_st_st_at, (p_st_st(un, 0x1234, &sink), sink));
    fout = 0xFFFFFFFFu;
    RUN("ld faults; fadd (fr2)", p_ld_fadd_at, (p_ld_fadd(un, 0, &fout), fout));
    fout = 0xFFFFFFFFu;
    RUN("ld ok; fadd (fr2)", p_ld_fadd_at, (p_ld_fadd(d0, 0, &fout), fout));
    RUN("bra; ld in slot faults", p_bra_ds_at, p_bra_ds(un));
    RUN("jsr; ld in slot faults (PR)", p_jsr_ds_at, p_jsr_ds(un, (uint32_t)(uintptr_t)p_dummy, 0x12345678u));
    /* PTEH reserved bits and ASID after a miss */
    xclr(); R32(PTEH) = 0x00000300u; r = p_ld32(un + 0x3FC); xdone();
    uint32_t ph1 = X.pteh, tea1 = X.tea;
    at_off();
    /* MMU off: address errors also write TEA / PTEH? */
    R32(PTEH) = 0x12345678u;
    RUN("AT0 ld32 P1 misaligned", p_ld32_at, p_ld32(DATA(0) + 2));
    R32(PTEH) = 0x12345678u; xclr(); r = p_st16(DATA(0) + 1, 0); xdone();
    uint32_t ph2 = X.pteh, tea2 = X.tea, ev2 = X.expevt, n2 = X.n;
    R32(PTEH) = 0;
    hard_clear();
    irq_restore(irq);
    for (int i = 0; i < k; i++) {
        long off = L[i].n ? (long)L[i].spc - (long)(uintptr_t)L[i].at : 0;
        printf("  %-30s exc=%lu ev=%03lx tea=%08lx pteh=%08lx spc%+ld ret=%08lx%s\n", L[i].nm, (UL)L[i].n, (UL)L[i].ev,
               (UL)L[i].tea, (UL)L[i].pteh, off, (UL)L[i].r, L[i].at == p_jsr_ds_at ? "" : "");
        if (L[i].at == p_jsr_ds_at) printf("    PR at the exception %08lx (marker 12345678)\n", (UL)L[i].pr);
    }
    printf("  mac.l dp0,unmapped: r4 %08lx r5 %08lx (in %08lx %08lx) | mac.l unmapped,dp0: r4 %08lx r5 %08lx\n",
           (UL)m1a, (UL)m1b, (UL)(d0 + 0x80), (UL)(un + 8), (UL)m2a, (UL)m2b);
    printf("  PTEH preset 00000300, miss at %08lx: PTEH %08lx TEA %08lx\n", (UL)(un + 0x3FC), (UL)ph1, (UL)tea1);
    printf("  AT0 st16 P1 misaligned: exc %lu ev %03lx TEA %08lx PTEH %08lx (preset 12345678)\n", (UL)n2, (UL)ev2,
           (UL)tea2, (UL)ph2);
#undef RUN
}

/* ================================================================== G: translation */
static void grp_G(void) {
    struct { const char *nm; uint32_t va, got, exp, n, ev; } L[40]; int k = 0;
#define RD(name, va_, pa_) do { uint32_t _v = (va_); xclr(); uint32_t _g = p_ld32(_v); xdone(); \
        L[k].nm = name; L[k].va = _v; L[k].got = _g; L[k].exp = (pa_) == 0xFFFFFFFFu ? 0 : R32(P2(pa_)); \
        L[k].n = X.n; L[k].ev = X.expevt; k++; } while (0)
    const uint32_t pd = PA(DATA(0));
    int irq = irq_disable();
    hard_clear();
    umap(16, DATA(0), 3, 1);
    uint32_t v1k = 0x30000000u | pd, v64 = 0x50000000u | (pd & 0x0FFF0000u), v1m = 0x60000000u | (pd & 0x0FF00000u);
    utlb_set(24, v1k, pd, SZ1K, 3, 0, 1, 0, 0, 0, 0, 0);
    utlb_set(25, v64, pd & 0x1FFF0000u, SZ64K, 3, 0, 1, 0, 0, 0, 0, 0);
    utlb_set(26, v1m, pd & 0x1FF00000u, SZ1M, 3, 0, 1, 0, 0, 0, 0, 0);
    if (at_on(0)) { irq_restore(irq); return; }
    RD("1K +0", v1k, pd); RD("1K +3fc", v1k + 0x3FC, pd + 0x3FC); RD("1K +400 (miss)", v1k + 0x400, 0xFFFFFFFFu);
    RD("4K dp0 +ffc", VA(DATA(0)) + 0xFFC, pd + 0xFFC); RD("4K dp6 (miss)", VA(DATA(6)), 0xFFFFFFFFu);
    RD("64K +0", v64 + (pd & 0xFFFF), pd); RD("64K +fffc", v64 + 0xFFFC, (pd & 0x1FFF0000u) + 0xFFFC);
    RD("64K +10000 (miss)", v64 + 0x10000, 0xFFFFFFFFu);
    RD("1M +off", v1m + (pd & 0xFFFFF), pd); RD("1M +ffffc", v1m + 0xFFFFC, (pd & 0x1FF00000u) + 0xFFFFC);
    RD("1M +100000 (miss)", v1m + 0x100000, 0xFFFFFFFFu);
    at_off();
    /* VPN low bits in a 4K entry (VPN bit 10 set), then PPN bit 10 set */
    hard_clear();
    const uint32_t vx = 0x08000000u | PA(DATA(1)), px = PA(DATA(1));
    utlb_set(27, vx | 0x400, px, SZ4K, 3, 0, 1, 0, 0, 0, 0, 0);
    at_on(0);
    RD("4K VPN|400: +0", vx, px); RD("4K VPN|400: +400", vx + 0x400, px + 0x400); RD("4K VPN|400: +c00", vx + 0xC00, px + 0xC00);
    at_off(); hard_clear();
    utlb_set(27, vx, px | 0x400, SZ4K, 3, 0, 1, 0, 0, 0, 0, 0);
    at_on(0);
    RD("4K PPN|400: +0 (vs pa+0)", vx, px); RD("4K PPN|400: +0 (vs pa+400)", vx, px + 0x400);
    RD("4K PPN|400: +800", vx + 0x800, px + 0x800);
    at_off(); hard_clear();
    /* 64K entry with PPN bits 15:10 set */
    utlb_set(25, v64, (pd & 0x1FFF0000u) | 0xFC00u, SZ64K, 3, 0, 1, 0, 0, 0, 0, 0);
    at_on(0);
    RD("64K PPN|fc00: +100", v64 + 0x100, (pd & 0x1FFF0000u) + 0x100);
    at_off(); hard_clear();
    /* ASID / SH / SV */
    const uint32_t va_ = 0x20000000u | PA(DATA(2)), pa_ = PA(DATA(2));
    utlb_set(28, va_, pa_, SZ4K, 3, 0, 1, 0, 0, 0x12, 0, 0);
    R32(PTEH) = 0;
    at_on(0);
    RD("ASID12 SH0, PTEH.ASID 00", va_, 0xFFFFFFFFu);
    R32(PTEH) = 0x12;
    RD("ASID12 SH0, PTEH.ASID 12", va_, pa_);
    RD("PTEH.ASID 12, miss elsewhere", va_ + 0x1000, 0xFFFFFFFFu);
    uint32_t pteh_after = X.pteh;
    R32(PTEH) = 0;
    at_off(); hard_clear();
    utlb_set(28, va_, pa_, SZ4K, 3, 0, 1, 1, 0, 0x12, 0, 0);
    at_on(0);
    RD("ASID12 SH1, PTEH.ASID 00", va_, pa_);
    at_off(); hard_clear();
    mmucr_set(M_SV | M_TI);
    utlb_set(28, va_, pa_, SZ4K, 3, 0, 1, 0, 0, 0x12, 0, 0);
    if (!tlb_check()) mmucr_set(M_SV | M_AT);
    RD("SV=1 priv, ASID12 SH0, PTEH 00", va_, pa_);
    mmucr_set(M_TI); hard_clear();
    /* P3 */
    const uint32_t v3 = 0xC0000000u | PA(DATA(3));
    utlb_set(29, v3, PA(DATA(3)), SZ4K, 3, 0, 1, 0, 0, 0, 0, 0);
    at_on(0);
    RD("P3 mapped", v3, PA(DATA(3))); RD("P3 next page (miss)", v3 + 0x1000, 0xFFFFFFFFu);
    at_off(); hard_clear();
    /* area 7 through the TLB: VA 3f000000 (1M) -> PA 1f000000 */
    utlb_set(30, 0x3F000000u, 0x1F000000u, SZ1M, 1, 0, 1, 0, 0, 0, 0, 0);
    at_on(0);
    R32(TEA) = MARK;
    xclr(); R32(TEA) = 0x13572468u; uint32_t a7tea = p_ld32(0x3F00000Cu); xdone(); uint32_t a7n1 = X.n;
    xclr(); uint32_t a7mm = p_ld32(0x3F000010u); xdone(); uint32_t a7n2 = X.n;
    uint32_t mmnow = R32(MMUCR);
    at_off(); hard_clear();
    irq_restore(irq);
    for (int i = 0; i < k; i++)
        printf("  G %-30s va %08lx got %08lx phys %08lx %s exc %lu ev %03lx\n", L[i].nm, (UL)L[i].va, (UL)L[i].got,
               (UL)L[i].exp, L[i].n ? "  " : (L[i].got == L[i].exp ? "==" : "!="), (UL)L[i].n, (UL)L[i].ev);
    printf("  G PTEH after a miss with PTEH.ASID=12: %08lx\n", (UL)pteh_after);
    printf("  G area7 via TLB: [3f00000c] %08lx (TEA written 13572468) exc %lu; [3f000010] %08lx exc %lu (MMUCR %08lx)\n",
           (UL)a7tea, (UL)a7n1, (UL)a7mm, (UL)a7n2, (UL)mmnow);
#undef RD
}

/* ================================================================== S: store queues with the MMU on */
static char sline[160];
static void sq_case(const char *nm, int map, int pr, int d, uint32_t tag) {
    uint32_t e1, e2, n1, n2, t1, u0, u1, u2;
    sline[0] = 0;
    for (int i = 0; i < 8; i++) R32(P2(PA(sqbuf)) + 4 * i) = 0;
    hard_clear();
    umap(16, DATA(0), 3, 1);
    if (map) utlb_set(32, 0xE0000000u, PA(sqbuf) & 0x1FF00000u, SZ1M, pr, 0, d, 0, 0, 0, 0, 0);
    if (at_on(0)) return;
    u0 = urc();
    xclr();
    for (int i = 0; i < 8; i++) { p_st32(SQA + 4 * i, (tag << 16) | (uint32_t)i); xdone(); }
    n1 = X.n; e1 = X.expevt; t1 = X.tea; u1 = urc();
    xclr(); p_pref(SQA); xdone(); n2 = X.n; e2 = X.expevt; u2 = urc();
    at_off();
    delay(2000);
    int o = snprintf(sline, sizeof sline, "S %-14s st x8: exc %lu ev %03lx tea %08lx | pref: exc %lu ev %03lx | urc %lu->%lu->%lu | mem:",
                     nm, (UL)n1, (UL)e1, (UL)t1, (UL)n2, (UL)e2, (UL)u0, (UL)u1, (UL)u2);
    for (int i = 0; i < 8 && o < (int)sizeof sline - 10; i++) o += snprintf(sline + o, sizeof sline - o, " %08lx", (UL)R32(P2(PA(sqbuf)) + 4 * i));
}
static void grp_S(void) {
    static const struct { const char *nm; int map, pr, d; uint32_t tag; } C[5] = {
        {"no SQ mapping", 0, 1, 1, 0xA001}, {"1M PR01 D1", 1, 1, 1, 0xA002}, {"1M PR00 D1", 1, 0, 1, 0xA003},
        {"1M PR01 D0", 1, 1, 0, 0xA004}, {"1M PR10 D1", 1, 2, 1, 0xA005}};
    printf("S  SQ address %08lx -> buffer PA %08lx (values tag<<16|i)\n", (UL)SQA, (UL)PA(sqbuf));
    for (int c = 0; c < 5; c++) {
        int irq = irq_disable();
        sq_case(C[c].nm, C[c].map, C[c].pr, C[c].d, C[c].tag);
        hard_clear();
        irq_restore(irq);
        printf("%s\n", sline);
    }
}

/* ================================================================== T: timing */
static uint32_t tmin(uint32_t *v, int n) { uint32_t m = v[0]; for (int i = 1; i < n; i++) if (v[i] < m) m = v[i]; return m; }
static uint32_t tmax(uint32_t *v, int n) { uint32_t m = v[0]; for (int i = 1; i < n; i++) if (v[i] > m) m = v[i]; return m; }
static void grp_T(void) {
    uint32_t hit[32], miss[32], miss63[32]; uint64_t im0, im1, im2, im3;
    const uint32_t ctr = PMCTR_LO(0), tgt = VA(CODE(0));
    int timer_was_on = perf_cntr_timer_enabled();   /* KOS may use PRFC0 as its ns timer: never restart it */
    int irq = irq_disable();
    if (!timer_was_on) perf_cntr_start(PRFC0, PMCR_ELAPSED_TIME_MODE, PMCR_COUNT_CPU_CYCLES);
    perf_cntr_start(PRFC1, PMCR_INSTRUCTION_TLB_MISS_MODE, PMCR_COUNT_CPU_CYCLES);
    e_setup5();
    at_on(M_LRUI(0));
    p_ifetch(tgt); p_ifetch(tgt); xdone();
    for (int i = 0; i < 32; i++) hit[i] = t_call(tgt, 0, 0, ctr);
    im0 = perf_cntr_count(PRFC1);
    for (int i = 0; i < 32; i++) {
        int e = find_itlb(tgt);
        miss[i] = t_call(tgt, e >= 0 ? ITLB_AA(e) : 0, 0, ctr);
    }
    im1 = perf_cntr_count(PRFC1);
    at_off();
    hard_clear();
    umap(63, CODE(0), 3, 1);
    at_on(M_LRUI(0));
    p_ifetch(tgt); xdone();
    im2 = perf_cntr_count(PRFC1);
    for (int i = 0; i < 32; i++) {
        int e = find_itlb(tgt);
        miss63[i] = t_call(tgt, e >= 0 ? ITLB_AA(e) : 0, 0, ctr);
    }
    im3 = perf_cntr_count(PRFC1);
    at_off();
    /* pointer chase: P1 vs P0 (16 pages / 16 UTLB entries) vs P0 one page */
    hard_clear();
    for (int k = 0; k < 16; k++) umap(40 + k, CHASE(k), 3, 1);
    for (int k = 0; k < 16; k++) {
        uint32_t *n = (uint32_t *)(uintptr_t)(CHASE(k) + (uint32_t)k * 32u);
        uint32_t nx = CHASE((k + 1) & 15) + (uint32_t)((k + 1) & 15) * 32u;
        n[0] = nx;                     /* P1 chain, word 0 */
        n[1] = VA(nx) + 4;             /* P0 chain, word 1 */
        uint32_t *q = (uint32_t *)(uintptr_t)(CHASE(0) + 0x800u + (uint32_t)k * 64u);
        uint32_t qx = CHASE(0) + 0x800u + (uint32_t)((k + 1) & 15) * 64u;
        q[2] = qx + 8;                 /* P1 one-page chain, word 2 */
        q[3] = VA(qx) + 12;            /* P0 one-page chain, word 3 */
    }
    const uint32_t start[4] = {CHASE(0), VA(CHASE(0)) + 4, CHASE(0) + 0x808, VA(CHASE(0) + 0x800) + 12};
    uint32_t best[4];
    at_on(0);
    for (int c = 0; c < 4; c++) {
        uint32_t v[8];
        t_chase(start[c], 4, ctr);
        for (int i = 0; i < 8; i++) v[i] = t_chase(start[c], 32, ctr);
        best[c] = tmin(v, 8);
    }
    at_off();
    hard_clear();
    if (!timer_was_on) perf_cntr_clear(PRFC0);
    perf_cntr_clear(PRFC1);
    irq_restore(irq);
    printf("T1 jsr to a P0 'rts;nop' (IC warm): ITLB hit min %lu max %lu | ITLB miss (UTLB idx 0) min %lu max %lu "
           "(itlb-miss events %lu / 32) | UTLB idx 63 min %lu max %lu (events %lu / 32)\n", (UL)tmin(hit, 32),
           (UL)tmax(hit, 32), (UL)tmin(miss, 32), (UL)tmax(miss, 32), (UL)(im1 - im0), (UL)tmin(miss63, 32),
           (UL)tmax(miss63, 32), (UL)(im3 - im2));
    printf("T2 256 dependent loads (32 x 8 + loop): P1 16 pages %lu | P0 16 pages / 16 UTLB entries %lu | P1 1 page %lu "
           "| P0 1 page %lu cycles\n", (UL)best[0], (UL)best[1], (UL)best[2], (UL)best[3]);
}

/* ================================================================== U: user mode */
static void grp_U(void) {
    struct { const char *nm; uint32_t pc, n, ev, tea, spc; } L[16]; int k = 0;
#define UR(name, pc_, a6) do { xclr(); p_user((pc_), USR, (a6), ESC); xdone(); L[k].nm = name; L[k].pc = (pc_); \
        L[k].n = X.n; L[k].ev = X.expevt; L[k].tea = X.tea; L[k].spc = X.spc; k++; } while (0)
    const uint32_t ld = VA(CODE(9)), st = VA(CODE(9)) + 0x10, pg10 = VA(CODE(10));
    int irq = irq_disable();
    hard_clear();
    umap(9, CODE(9), 3, 1); umap(10, CODE(10), 1, 1);
    umap(16, DATA(0), 3, 1); umap(17, DATA(1), 0, 1); umap(18, DATA(2), 2, 1); umap(19, DATA(3), 1, 0);
    umap(21, DATA(5), 3, 0);
    utlb_set(32, 0xE0000000u, PA(sqbuf) & 0x1FF00000u, SZ1M, 3, 0, 1, 0, 0, 0, 0, 0);
    if (at_on(0)) { irq_restore(irq); return; }
    UR("ld dp1 PR00", ld, VA(DATA(1)));
    UR("ld dp3 PR01", ld, VA(DATA(3)));
    UR("ld dp2 PR10 (ok -> esc)", ld, VA(DATA(2)));
    UR("st dp2 PR10", st, VA(DATA(2)));
    UR("st dp5 PR11 D0", st, VA(DATA(5)));
    UR("st dp0 PR11 D1 (ok -> esc)", st, VA(DATA(0)) + 0x300);
    UR("ld P1 (address error)", ld, DATA(0));
    UR("st SQ, SQMD0 (ok -> esc)", st, SQA);
    mmucr_set(M_AT | M_SQMD);
    UR("st SQ, SQMD1", st, SQA);
    mmucr_set(M_AT);
    UR("fetch page PR01 (ITLB PR0)", pg10, 0);
    int e10 = find_itlb(pg10);
    uint32_t i10 = e10 >= 0 ? R32(ITLB_DA1(e10)) : 0;
    UR("fetch P1", (uint32_t)(uintptr_t)p_dummy, 0);
    at_off();
    mmucr_set(M_SQMD);
    UR("AT0 st SQ, SQMD1", st, SQA);
    mmucr_set(0);
    hard_clear();
    irq_restore(irq);
    for (int i = 0; i < k; i++)
        printf("  U %-28s exc=%lu ev=%03lx tea=%08lx spc-pc%+ld%s\n", L[i].nm, (UL)L[i].n, (UL)L[i].ev, (UL)L[i].tea,
               L[i].n ? (long)L[i].spc - (long)L[i].pc : 0L, (L[i].ev == 0xE0 && L[i].tea == ESC) ? "  (= escape: access allowed)" : "");
    printf("  U ITLB entry for the PR01 page: e%d da1 %08lx\n", e10, (UL)i10);
#undef UR
}

/* ================================================================== main */
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    uint32_t o_pteh = R32(PTEH), o_ptel = R32(PTEL), o_ptea = R32(PTEA), o_ttb = R32(TTB), o_tea = R32(TEA);
    uint32_t o_mmucr = R32(MMUCR), o_q0 = R32(QACR0), o_q1 = R32(QACR1);
    printf("mmutest: start\n");
    codebuf = memalign(0x10000, 0x10000);
    databuf = memalign(0x10000, 0x10000);
    chasebuf = memalign(0x10000, 0x10000);
    sqbuf = memalign(32, 64);
    if (!codebuf || !databuf || !chasebuf || !sqbuf) { printf("mmutest: no memory\n"); return 1; }
    printf("mmutest: code %08lx data %08lx chase %08lx sq %08lx\n", (UL)PA(codebuf), (UL)PA(databuf), (UL)PA(chasebuf),
           (UL)PA(sqbuf));
    /* code pages: rts ; nop everywhere; p7 ends nop, rts; p8 starts nop; p9 / p10 user snippets */
    uint16_t *cw = (uint16_t *)codebuf;
    for (int i = 0; i < 0x8000; i += 2) { cw[i] = 0x000B; cw[i + 1] = 0x0009; }
    cw[(7 * 0x1000 + 0xFFC) / 2] = 0x0009; cw[(7 * 0x1000 + 0xFFE) / 2] = 0x000B;
    cw[(8 * 0x1000) / 2] = 0x0009;
    cw[(9 * 0x1000 + 0x00) / 2] = 0x6062; cw[(9 * 0x1000 + 0x02) / 2] = 0x6072;   /* mov.l @r6,r0 ; mov.l @r7,r0 */
    cw[(9 * 0x1000 + 0x10) / 2] = 0x2652; cw[(9 * 0x1000 + 0x12) / 2] = 0x6072;   /* mov.l r5,@r6 ; mov.l @r7,r0 */
    cw[(10 * 0x1000 + 0x00) / 2] = 0x6072; cw[(10 * 0x1000 + 0x02) / 2] = 0x0009; /* mov.l @r7,r0 */
    uint32_t *dw = (uint32_t *)databuf;
    for (int i = 0; i < 0x4000; i++) dw[i] = 0xD0000000u | (uint32_t)(i * 4);
    memset(chasebuf, 0, 0x10000);
    dcache_flush_range((uintptr_t)codebuf, 0x10000);
    icache_flush_range((uintptr_t)codebuf, 0x10000);
    dcache_flush_range((uintptr_t)databuf, 0x10000);
    dcache_purge_range((uintptr_t)sqbuf, 64);
    for (unsigned i = 0; i < NCODES; i++) { old_cb[i] = irq_get_handler(codes[i]); irq_set_handler(codes[i], hnd, NULL); }

    grp_A();
    grp_B();
    grp_C();
    grp_D();
    grp_E();
    grp_F();
    grp_G();
    grp_S();
    grp_T();
    grp_U();

    int irq = irq_disable();
    hard_clear();
    R32(PTEH) = o_pteh; R32(PTEL) = o_ptel; R32(PTEA) = o_ptea; R32(TTB) = o_ttb; R32(TEA) = o_tea;
    R32(QACR0) = o_q0; R32(QACR1) = o_q1;
    mmucr_set(o_mmucr & ~(M_AT | M_TI));
    irq_restore(irq);
    for (unsigned i = 0; i < NCODES; i++) irq_set_handler(codes[i], old_cb[i].hdl, old_cb[i].data);
    printf("mmutest: done, unexpected exceptions %lu, MMUCR %08lx\n", (UL)g_unexpected, (UL)R32(MMUCR));
    return 0;
}
