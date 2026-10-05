/* mmubm.c -- bare-metal port of the MMU hardware probes tools/hw/mmu/mmutest{,2,3}.c (rtl/v1/spec/mmu.md section 6)
 * for the v1 SoC (rtl/v1/SOC.md 7.1: tb/bm/<spec>/, run by WI-SOC at milestone M2).  Owner: WI-MMU.
 *
 * The probe routines are the console's own: tools/hw/mmu/probes.S and probes2.S, assembled unchanged.  This file is
 * mmutest*.c without KOS: printf is replaced by rec(name, idx, value), KOS's exception dispatch by crt0.S, memalign
 * by the console run's own buffer addresses (run1.log "code 0c090000 data 0c0b0000 chase 0c0d0000 sq 0c073d20"), so
 * every TEA / PTEH / PA of the logs is reproduced as printed.  Everything the logs print that depended on KOS (its
 * register garbage, code addresses) is recorded relative to the probe labels or not compared (tb/bm/mmu/expect.py).
 *
 * The result protocol (read by the testbench after the program halts; expect.json names the address):
 *   physical 0x0C0F0000: u32 magic 0x4D4D5542 ('MMUB', written last), u32 count, u32 unexpected exceptions,
 *                        u32 interrupts, then count records {u32 id, u32 value}
 *   id = (fnv1a32(name) & 0x00FFFFFF) | (idx << 24); expect.json lists {name, idx, id, expect, mask, requires}.
 * The program ends by writing its exit code (0) to the sim-control word at physical 0x18000000 and sleeping with
 * SR.BL = 1 (crt0.S).
 */
#include <stdint.h>

#define R32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define R16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define PTEH   0xFF000000u
#define PTEL   0xFF000004u
#define TTB    0xFF000008u
#define TEA    0xFF00000Cu
#define MMUCR  0xFF000010u
#define PTEA   0xFF000034u
#define PMCR(c)     (0xFF000084u + 4u * (c))
#define PMCTR_LO(c) (0xFF100008u + 8u * (c))
#define ITLB_AA(e)  (0xF2000000u | ((uint32_t)(e) << 8))
#define ITLB_DA1(e) (0xF3000000u | ((uint32_t)(e) << 8))
#define ITLB_DA2(e) (0xF3800000u | ((uint32_t)(e) << 8))
#define UTLB_AA(e)  (0xF6000000u | ((uint32_t)(e) << 8))
#define UTLB_ASSOC  0xF6000080u
#define UTLB_DA1(e) (0xF7000000u | ((uint32_t)(e) << 8))
#define UTLB_DA2(e) (0xF7800000u | ((uint32_t)(e) << 8))
#define OC_AA(pa)   (0xF4000000u | ((pa) & 0x3FE0u))
#define M_AT      1u
#define M_TI      4u
#define M_SV      0x100u
#define M_SQMD    0x200u
#define M_URC(x)  (((uint32_t)(x) & 63u) << 10)
#define M_URB(x)  (((uint32_t)(x) & 63u) << 18)
#define M_LRUI(x) (((uint32_t)(x) & 63u) << 26)
#define G_URC(m)  (((m) >> 10) & 63u)
#define G_LRUI(m) (((m) >> 26) & 63u)
#define PA(p)  ((uint32_t)(uintptr_t)(p) & 0x1FFFFFFFu)
#define P2(pa) (((uint32_t)(pa) & 0x1FFFFFFFu) | 0xA0000000u)
#define MARK   0x7EA7EA00u
#define ESC    0x80000100u
#define USR    0x000000F0u

/* ------------------------------------------------------------------ results */
#define RES_PA 0x0C0F0000u
static uint32_t nrec;
static uint32_t fnv(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h;
}
static void rec(const char *name, uint32_t idx, uint32_t v) {
    volatile uint32_t *r = (volatile uint32_t *)(uintptr_t)P2(RES_PA);
    r[4 + 2 * nrec] = (fnv(name) & 0x00FFFFFFu) | (idx << 24);
    r[5 + 2 * nrec] = v;
    nrec++;
}
static uint32_t pack4(char a, char b, char c, char d) {   /* four ITLB page characters, e0 in the low byte */
    return (uint32_t)(uint8_t)a | ((uint32_t)(uint8_t)b << 8) | ((uint32_t)(uint8_t)c << 16) | ((uint32_t)(uint8_t)d << 24);
}
void *memset(void *d, int c, unsigned n) { uint8_t *p = d; while (n--) *p++ = (uint8_t)c; return d; }
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *p = d; const uint8_t *q = s; while (n--) *p++ = *q++; return d; }

/* ------------------------------------------------------------------ probes (tools/hw/mmu/probes.S, probes2.S) */
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
extern uint32_t p_ifetch(uint32_t), p_stf32(uint32_t);
extern void p_dummy(void);
extern uint32_t p_user(uint32_t pc, uint32_t sr, uint32_t a6, uint32_t a7);
extern void mmucr_set(uint32_t);
extern void ldtlb_do(void);
extern uint32_t t_call(uint32_t tgt, uint32_t aa, uint32_t val, uint32_t ctr);
extern uint32_t t_chase(uint32_t p, uint32_t n, uint32_t ctr);
extern uint32_t p_bsr_ds(uint32_t, uint32_t, uint32_t), p_jsr_ill(uint32_t, uint32_t, uint32_t);
extern uint32_t p_jsr_ds2(uint32_t, uint32_t, uint32_t);
typedef uint32_t (*lat_fn)(uint32_t r4, uint32_t r5, uint32_t r6);
extern uint32_t p_mlat0(uint32_t, uint32_t, uint32_t), p_mlat1(uint32_t, uint32_t, uint32_t),
    p_mlat2(uint32_t, uint32_t, uint32_t), p_mlat3(uint32_t, uint32_t, uint32_t), p_mlat4(uint32_t, uint32_t, uint32_t),
    p_mlat5(uint32_t, uint32_t, uint32_t), p_mlat6(uint32_t, uint32_t, uint32_t), p_mlat7(uint32_t, uint32_t, uint32_t),
    p_mlat8(uint32_t, uint32_t, uint32_t);
extern uint32_t p_mjmp0(uint32_t, uint32_t, uint32_t), p_mjmp1(uint32_t, uint32_t, uint32_t),
    p_mjmp2(uint32_t, uint32_t, uint32_t), p_mjmp3(uint32_t, uint32_t, uint32_t), p_mjmp4(uint32_t, uint32_t, uint32_t),
    p_mjmp5(uint32_t, uint32_t, uint32_t), p_mjmp6(uint32_t, uint32_t, uint32_t), p_mjmp7(uint32_t, uint32_t, uint32_t),
    p_mjmp8(uint32_t, uint32_t, uint32_t), p_mjmp9(uint32_t, uint32_t, uint32_t), p_mjmp10(uint32_t, uint32_t, uint32_t);
extern uint32_t p_llat0(uint32_t, uint32_t, uint32_t), p_llat1(uint32_t, uint32_t, uint32_t),
    p_llat2(uint32_t, uint32_t, uint32_t), p_llat3(uint32_t, uint32_t, uint32_t);
extern uint32_t p_ulat0(uint32_t, uint32_t, uint32_t), p_ulat1(uint32_t, uint32_t, uint32_t),
    p_ulat2(uint32_t, uint32_t, uint32_t), p_ulat3(uint32_t, uint32_t, uint32_t);
extern char p_ld8_at[], p_ld16_at[], p_ld32_at[], p_ldf32_at[], p_ldf64_at[], p_st8_at[], p_st16_at[], p_st32_at[];
extern char p_and_gbr_at[], p_tst_gbr_at[], p_tas_at[], p_ocbi_at[], p_ocbp_at[], p_ocbwb_at[], p_pref_at[];
extern char p_movca_at[], p_macl_at[], p_ldpi_at[], p_stpd_at[], p_pair_f_at[], p_pair_l_at[], p_ld_st_at[];
extern char p_st_st_at[], p_ld_fadd_at[], p_bra_ds_at[], p_jsr_ds_at[], p_ifetch_at[];
extern char p_bsr_ds_at[], p_jsr_ill_at[], p_jsr_ds2_at[];
static const lat_fn mlat[9] = {p_mlat0, p_mlat1, p_mlat2, p_mlat3, p_mlat4, p_mlat5, p_mlat6, p_mlat7, p_mlat8};
static const lat_fn mjmp[11] = {p_mjmp0, p_mjmp1, p_mjmp2, p_mjmp3, p_mjmp4, p_mjmp5, p_mjmp6, p_mjmp7, p_mjmp8,
                                p_mjmp9, p_mjmp10};
static const lat_fn llat[4] = {p_llat0, p_llat1, p_llat2, p_llat3};
static const lat_fn ulat[4] = {p_ulat0, p_ulat1, p_ulat2, p_ulat3};
extern void ic_inval(void);

/* ------------------------------------------------------------------ exception capture (crt0.S _exc_common) */
typedef struct { uint32_t n, expevt, tea, pteh, mmucr, spc, ssr, pr; } exc_t;
volatile exc_t X;
volatile uint32_t g_recover, g_recover_sr, g_unexpected, g_irqs;
static void xclr(void) { X.n = 0; X.expevt = 0; X.tea = 0; X.spc = 0; X.pteh = 0; X.pr = 0; R32(TEA) = MARK; }
static void xdone(void) { g_recover = 0; }

/* ------------------------------------------------------------------ KOS helpers, bare metal */
static void dc_flush(uint32_t a, uint32_t n) { for (uint32_t p = a & ~31u; p < a + n; p += 32) __asm__ volatile("ocbwb @%0" : : "r"(p) : "memory"); }
static void dc_purge(uint32_t a, uint32_t n) { for (uint32_t p = a & ~31u; p < a + n; p += 32) __asm__ volatile("ocbp @%0" : : "r"(p) : "memory"); }
static void delay(int n) { for (volatile int i = 0; i < n; i++) ; }
/* perf_cntr_start(counter, mode, PMCR_COUNT_CPU_CYCLES): PMCR = RUN (0xC000) | mode (KOS perfctr.c) */
static void pmc_start(int c, int mode) { R16(PMCR(c)) = (uint16_t)(0xC000u | (uint32_t)mode); }
static uint32_t pmc_lo(int c) { return R32(PMCTR_LO(c)); }

/* ------------------------------------------------------------------ buffers and TLB helpers (mmutest.c) */
#define CODEBUF 0x8C090000u
#define DATABUF 0x8C0B0000u
#define CHASEBUF 0x8C0D0000u
#define SQBUF   0x8C073D20u
#define CODE(k)   (CODEBUF + (uint32_t)(k) * 0x1000u)
#define DATA(k)   (DATABUF + (uint32_t)(k) * 0x1000u)
#define CHASE(k)  (CHASEBUF + (uint32_t)(k) * 0x1000u)
#define VA(p1)    (0x40000000u | PA(p1))
#define SQA       (0xE0000000u | (PA(SQBUF) & 0x000FFFE0u))
enum { SZ1K = 0, SZ4K = 1, SZ64K = 2, SZ1M = 3 };
static const uint32_t szmask[4] = {0xFFFFFC00u, 0xFFFFF000u, 0xFFFF0000u, 0xFFF00000u};

static void utlb_set(int e, uint32_t va, uint32_t pa, int sz, int pr, int c, int d, int sh, int wt, int asid, int sa,
                     int tc) {
    R32(UTLB_DA2(e)) = ((uint32_t)tc << 3) | (uint32_t)sa;
    R32(UTLB_DA1(e)) = (pa & 0x1FFFFC00u) | (1u << 8) | (((uint32_t)sz & 2u) << 6) | (((uint32_t)sz & 1u) << 4) |
                       ((uint32_t)pr << 5) | ((uint32_t)c << 3) | ((uint32_t)d << 2) | ((uint32_t)sh << 1) | (uint32_t)wt;
    R32(UTLB_AA(e)) = (va & 0xFFFFFC00u) | ((uint32_t)d << 9) | (1u << 8) | ((uint32_t)asid & 0xFFu);
}
static void umap(int e, uint32_t p1, int pr, int d) { utlb_set(e, VA(p1), PA(p1), SZ4K, pr, 1, d, 0, 0, 0, 0, 0); }
static void umapc(int e, uint32_t p1, int c) {                  /* mmutest3.c: 4K, PR 11, D, C per page */
    R32(UTLB_DA2(e)) = 0;
    R32(UTLB_DA1(e)) = (PA(p1) & 0x1FFFFC00u) | 0x100u | 0x10u | 0x60u | ((uint32_t)c << 3) | 0x04u;
    R32(UTLB_AA(e)) = (VA(p1) & 0xFFFFFC00u) | 0x300u;
}
static void hard_clear(void) {
    for (int e = 0; e < 64; e++) { R32(UTLB_AA(e)) = 0; R32(UTLB_DA1(e)) = 0; R32(UTLB_DA2(e)) = 0; }
    for (int e = 0; e < 4; e++) { R32(ITLB_AA(e)) = 0; R32(ITLB_DA1(e)) = 0; R32(ITLB_DA2(e)) = 0; }
    mmucr_set(M_TI);
    mmucr_set(0);
}
static int usz(int e) { uint32_t d = R32(UTLB_DA1(e)); return (int)(((d >> 6) & 2u) | ((d >> 4) & 1u)); }
static int isz(int e) { uint32_t d = R32(ITLB_DA1(e)); return (int)(((d >> 6) & 2u) | ((d >> 4) & 1u)); }
static int tlb_check(void) {                                    /* refuse overlapping valid entries (a reset) */
    int bad = 0;
    for (int i = 0; i < 64; i++) {
        uint32_t a = R32(UTLB_AA(i));
        if (!(a & 0x100)) continue;
        for (int j = i + 1; j < 64; j++) {
            uint32_t b = R32(UTLB_AA(j));
            if (!(b & 0x100)) continue;
            if (((a ^ b) & szmask[usz(i)] & szmask[usz(j)]) == 0) bad++;
        }
    }
    for (int i = 0; i < 4; i++) {
        uint32_t a = R32(ITLB_AA(i));
        if (!(a & 0x100)) continue;
        for (int j = i + 1; j < 4; j++) {
            uint32_t b = R32(ITLB_AA(j));
            if (!(b & 0x100)) continue;
            if (((a ^ b) & szmask[isz(i)] & szmask[isz(j)]) == 0) bad++;
        }
    }
    return bad;
}
static int at_on(uint32_t bits) {
    if (tlb_check()) { g_unexpected += 1000; return -1; }
    mmucr_set(bits | M_AT);
    return 0;
}
static void at_off(void) { mmucr_set(0); }
static uint32_t urc(void) { return G_URC(R32(MMUCR)); }
static char ipage(int e) {
    uint32_t a = R32(ITLB_AA(e));
    if (!(a & 0x100)) return '-';
    uint32_t v = a & 0xFFFFF000u;
    for (int k = 0; k < 16; k++) if (v == (VA(CODE(k)) & 0xFFFFF000u)) return (char)(k < 10 ? '0' + k : 'a' + k - 10);
    return '?';
}
static uint32_t ipages(void) { return pack4(ipage(0), ipage(1), ipage(2), ipage(3)); }
static int find_itlb(uint32_t va) {
    for (int e = 0; e < 4; e++) {
        uint32_t a = R32(ITLB_AA(e));
        if ((a & 0x100) && ((a ^ va) & 0xFFFFF000u) == 0) return e;
    }
    return -1;
}
static uint32_t vmin(const uint32_t *v, int n) { uint32_t m = v[0]; for (int i = 1; i < n; i++) if (v[i] < m) m = v[i]; return m; }
static uint32_t vmax(const uint32_t *v, int n) { uint32_t m = v[0]; for (int i = 1; i < n; i++) if (v[i] > m) m = v[i]; return m; }

/* ================================================================== A: registers */
static void grp_A(void) {
    static const uint32_t regs[5] = {PTEH, PTEL, PTEA, TTB, TEA};
    for (int i = 0; i < 5; i++) rec("A0", (uint32_t)i, R32(regs[i]));
    rec("A0", 5, R32(MMUCR));
    for (int i = 0; i < 5; i++) {
        uint32_t old = R32(regs[i]);
        R32(regs[i]) = 0xFFFFFFFFu; rec("A1", (uint32_t)(3 * i), R32(regs[i]));
        R32(regs[i]) = 0;           rec("A1", (uint32_t)(3 * i + 1), R32(regs[i]));
        R32(regs[i]) = 0x5A5AA5A5u; rec("A1", (uint32_t)(3 * i + 2), R32(regs[i]));
        R32(regs[i]) = old;
    }
    mmucr_set(M_LRUI(0x3F) | M_URB(0x3F) | M_URC(0x3F) | M_SQMD | M_SV | M_TI); rec("A2", 0, R32(MMUCR));
    mmucr_set(M_LRUI(0x07) | M_URB(0x15) | M_URC(0x2A));                      rec("A2", 1, R32(MMUCR));
    mmucr_set(M_LRUI(0x38) | M_TI);                                             rec("A2", 2, R32(MMUCR));
    mmucr_set(0);
    R32(UTLB_DA2(5)) = 0xD; R32(UTLB_DA1(5)) = 0x0ABCD000u | 0x1FFu; R32(UTLB_AA(5)) = 0x12345000u | 0x300u | 0x33u;
    R32(ITLB_DA2(2)) = 0xA; R32(ITLB_DA1(2)) = 0x0BCDE000u | 0x1FFu; R32(ITLB_AA(2)) = 0x23456000u | 0x100u | 0x44u;
    static const uint32_t ad[6] = {UTLB_AA(5), UTLB_DA1(5), UTLB_DA2(5), ITLB_AA(2), ITLB_DA1(2), ITLB_DA2(2)};
    for (int i = 0; i < 6; i++) rec("A3b", (uint32_t)i, R32(ad[i]));
    mmucr_set(M_TI);
    for (int i = 0; i < 6; i++) rec("A3a", (uint32_t)i, R32(ad[i]));
    rec("A3a", 6, R32(MMUCR));
    hard_clear();
}

/* ================================================================== B: memory-mapped arrays */
static void assoc_setup(uint32_t X0, int uv, int ud, int uasid, int ush, int iv) {
    R32(UTLB_DA2(20)) = 0;
    R32(UTLB_DA1(20)) = 0x0C000000u | ((uint32_t)uv << 8) | (1u << 4) | (3u << 5) | ((uint32_t)ud << 2) |
                        ((uint32_t)ush << 1);
    R32(UTLB_AA(20)) = X0 | ((uint32_t)ud << 9) | ((uint32_t)uv << 8) | (uint32_t)uasid;
    R32(ITLB_DA2(1)) = 0;
    R32(ITLB_DA1(1)) = 0x0C000000u | ((uint32_t)iv << 8) | (1u << 4) | (1u << 6);
    R32(ITLB_AA(1)) = X0 | ((uint32_t)iv << 8);
}
static void assoc_case(int at, int c, uint32_t data) {
    uint32_t m0 = R32(MMUCR);
    R32(UTLB_ASSOC) = data;
    uint32_t m1 = R32(MMUCR);
    uint32_t b = (uint32_t)(at * 64 + c * 8);
    rec("B5", b + 0, R32(UTLB_AA(20))); rec("B5", b + 1, R32(UTLB_DA1(20)));
    rec("B5", b + 2, R32(ITLB_AA(1)));  rec("B5", b + 3, R32(ITLB_DA1(1)));
    rec("B5", b + 4, G_URC(m0));        rec("B5", b + 5, G_URC(m1));
}
static void assoc_suite(int at) {
    const uint32_t X0 = 0x2A000000u;
    hard_clear();
    R32(PTEH) = 0;
    assoc_setup(X0, 1, 0, 0, 0, 1);
    if (at && at_on(0)) return;
    assoc_case(at, 0, X0 | 0x300);
    assoc_case(at, 1, X0 | 0x000);
    at_off(); assoc_setup(X0, 1, 0, 0, 0, 1); if (at && at_on(0)) return;
    assoc_case(at, 2, (X0 + 0x800) | 0x000);
    at_off(); assoc_setup(X0, 1, 0, 0x55, 0, 1); if (at && at_on(0)) return;
    assoc_case(at, 3, X0 | 0x000);
    at_off(); assoc_setup(X0, 1, 0, 0x55, 1, 1); if (at && at_on(0)) return;
    assoc_case(at, 4, X0 | 0x000);
    at_off(); assoc_setup(X0, 0, 0, 0, 0, 1); if (at && at_on(0)) return;
    assoc_case(at, 5, X0 | 0x000);
    at_off(); assoc_setup(X0, 1, 0, 0, 0, 0); if (at && at_on(0)) return;
    assoc_case(at, 6, X0 | 0x300);
    at_off(); assoc_setup(X0, 1, 1, 0, 0, 1); if (at && at_on(0)) return;
    assoc_case(at, 7, X0 | 0x100);
    at_off();
    hard_clear();
}
static void grp_B(void) {
    uint32_t v[40]; int k = 0;
    hard_clear();
    R32(ITLB_AA(1)) = 0xFFFFFFFFu; v[k++] = R32(ITLB_AA(1)); v[k++] = R32(ITLB_DA1(1));
    R32(ITLB_DA1(1)) = 0; v[k++] = R32(ITLB_AA(1)); v[k++] = R32(ITLB_DA1(1));
    R32(ITLB_DA1(1)) = 0xFFFFFFFFu; v[k++] = R32(ITLB_DA1(1)); v[k++] = R32(ITLB_AA(1));
    R32(ITLB_DA2(1)) = 0xFFFFFFFFu; v[k++] = R32(ITLB_DA2(1)); v[k++] = R32(ITLB_DA1(1));
    R32(ITLB_AA(1)) = 0; v[k++] = R32(ITLB_AA(1)); v[k++] = R32(ITLB_DA1(1)); v[k++] = R32(ITLB_DA2(1));
    R32(UTLB_AA(37)) = 0xFFFFFFFFu; v[k++] = R32(UTLB_AA(37)); v[k++] = R32(UTLB_DA1(37));
    R32(UTLB_DA1(37)) = 0; v[k++] = R32(UTLB_AA(37)); v[k++] = R32(UTLB_DA1(37));
    R32(UTLB_DA1(37)) = 0xFFFFFFFFu; v[k++] = R32(UTLB_DA1(37)); v[k++] = R32(UTLB_AA(37));
    R32(UTLB_DA2(37)) = 0xFFFFFFFFu; v[k++] = R32(UTLB_DA2(37)); v[k++] = R32(UTLB_DA1(37));
    R32(UTLB_AA(37)) = 0x200u; v[k++] = R32(UTLB_AA(37)); v[k++] = R32(UTLB_DA1(37));
    R32(UTLB_AA(37)) = 0; v[k++] = R32(UTLB_AA(37)); v[k++] = R32(UTLB_DA1(37)); v[k++] = R32(UTLB_DA2(37));
    for (int i = 0; i < k; i++) rec("B12", (uint32_t)i, v[i]);
    for (int e = 0; e < 4; e++) R32(ITLB_AA(e)) = (0x10000000u * (uint32_t)(e + 1)) | (uint32_t)(0x11 * (e + 1));
    for (int e = 0; e < 4; e++) {
        rec("B3", (uint32_t)(4 * e), R32(ITLB_AA(e)));
        rec("B3", (uint32_t)(4 * e + 1), R32(0xF2000000u | ((uint32_t)e << 16)));
        rec("B3", (uint32_t)(4 * e + 2), R32(ITLB_AA(e) | 0x80u));
        rec("B3", (uint32_t)(4 * e + 3), R32(ITLB_AA(e) | 0x00FFFC00u));
    }
    int ubad = 0;
    for (int e = 0; e < 64; e++) R32(UTLB_AA(e)) = ((uint32_t)e << 12) | (uint32_t)e;
    for (int e = 0; e < 64; e++) if (R32(UTLB_AA(e)) != (((uint32_t)e << 12) | (uint32_t)e)) ubad++;
    rec("B4", 0, (uint32_t)ubad);
    hard_clear();
    assoc_suite(0);
    at_off();
    hard_clear();
    assoc_suite(1);
    at_off();
    hard_clear();
}

/* ================================================================== C: LDTLB */
static void grp_C(void) {
    uint32_t o0 = R32(PTEH), o1 = R32(PTEL), o2 = R32(PTEA);
    hard_clear();
    R32(PTEH) = 0xFFFFFFFFu; R32(PTEL) = 0xFFFFFFFFu; R32(PTEA) = 0xFFFFFFFFu;
    mmucr_set(M_URC(0x2A));
    ldtlb_do();
    rec("C", 0, R32(UTLB_AA(0x2A))); rec("C", 1, R32(UTLB_DA1(0x2A))); rec("C", 2, R32(UTLB_DA2(0x2A)));
    rec("C", 3, R32(MMUCR));
    R32(PTEH) = 0x87654021u; R32(PTEL) = 0x0ABCD000u | 0x100u | 0x80u | 0x40u | 0x04u | 0x01u; R32(PTEA) = 0x6u;
    mmucr_set(M_URC(0x15) | M_URB(0x10));
    ldtlb_do();
    rec("C", 4, R32(UTLB_AA(0x15))); rec("C", 5, R32(UTLB_DA1(0x15))); rec("C", 6, R32(UTLB_DA2(0x15)));
    rec("C", 7, R32(MMUCR));
    R32(PTEH) = o0; R32(PTEL) = o1; R32(PTEA) = o2;
    hard_clear();
}

/* ================================================================== D: URC */
static uint32_t sqbuf_at(int i) { return P2(PA(SQBUF)) + 4u * (uint32_t)i; }
static void grp_D(void) {
    uint32_t mac[4]; uint32_t k = 0;
#define REC(s) do { rec("D1u", k, urc()); rec("D1e", k, X.n ? X.expevt : 0); k++; xclr(); (void)(s); } while (0)
    hard_clear();
    umap(16, DATA(0), 3, 1);
    umap(17, DATA(1), 0, 1);
    umap(0, CODE(0), 3, 1);
    utlb_set(32, 0xE0000000u, PA(SQBUF) & 0x1FF00000u, SZ1M, 1, 0, 1, 0, 0, 0, 0, 0);
    xclr();
    mmucr_set(M_URC(5));
    REC("AT0 start");
    p_ld32(DATA(0)); xdone(); REC("AT0 P1 load");
    p_ld32(VA(DATA(0))); xdone(); REC("AT0 P0 load");
    (void)R32(UTLB_AA(16)); REC("AT0 UTLB aa read");
    R32(UTLB_ASSOC) = 0x2A000000u; REC("AT0 assoc write (no match)");
    if (at_on(M_URC(5))) return;
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
#undef REC
    static const uint8_t wr[6][2] = {{8, 6}, {8, 0x3D}, {0, 0x3E}, {1, 0}, {0x3F, 0x3D}, {2, 1}};
    for (int t = 0; t < 6; t++) {
        mmucr_set(M_AT | M_URB(wr[t][0]) | M_URC(wr[t][1]));
        rec("D2", (uint32_t)(t * 9), urc());
        for (int i = 1; i < 9; i++) { p_ld32(VA(DATA(0))); xdone(); rec("D2", (uint32_t)(t * 9 + i), urc()); }
    }
    at_off();
    hard_clear();
}

/* ================================================================== E: ITLB */
static void e_setup5(void) { hard_clear(); for (int k = 0; k < 5; k++) umap(k, CODE(k), 3, 1); }
static void grp_E(void) {
    static const int seq[] = {0, 1, 2, 3, 0, 4, 1, 0, 2, 3, 4, 4, 1};
    e_setup5();
    if (at_on(M_LRUI(0))) return;
    for (unsigned i = 0; i < sizeof seq / sizeof seq[0]; i++) {
        xclr(); p_ifetch(VA(CODE(seq[i]))); xdone();
        rec("E1", 3 * i, G_LRUI(R32(MMUCR))); rec("E1", 3 * i + 1, ipages()); rec("E1", 3 * i + 2, X.n);
    }
    at_off();
    static const uint8_t lv[] = {0x38, 0x06, 0x01, 0x00, 0x3F, 0x1E, 0x07};
    for (unsigned t = 0; t < sizeof lv; t++) {
        e_setup5();
        if (at_on(M_LRUI(0))) return;
        for (int p = 0; p < 4; p++) { xclr(); p_ifetch(VA(CODE(p))); xdone(); }
        rec("E1b", 5 * t, G_LRUI(R32(MMUCR))); rec("E1b", 5 * t + 1, ipages());
        mmucr_set(M_AT | M_LRUI(lv[t]));
        xclr(); p_ifetch(VA(CODE(4))); xdone();
        rec("E1b", 5 * t + 2, G_LRUI(R32(MMUCR))); rec("E1b", 5 * t + 3, ipages()); rec("E1b", 5 * t + 4, X.n);
        at_off();
    }
    e_setup5();
    at_on(M_LRUI(0));
    xclr(); p_ifetch(VA(CODE(3))); xdone();
    rec("E1c", 0, ipages());
    mmucr_set(M_AT | M_LRUI(0));
    xclr(); p_ifetch(VA(CODE(0))); xdone();
    rec("E1c", 1, ipages()); rec("E1c", 2, G_LRUI(R32(MMUCR)));
    at_off();
    /* E2 */
    hard_clear();
    utlb_set(0, VA(CODE(0)), PA(CODE(0)), SZ4K, 3, 1, 1, 1, 1, 0x5A, 5, 1);
    R32(PTEH) = 0; R32(TEA) = MARK;
    at_on(M_LRUI(0));
    xclr(); R32(TEA) = MARK;
    p_ifetch(VA(CODE(0)) + 0xC00); xdone();
    rec("E2", 0, R32(ITLB_AA(3))); rec("E2", 1, R32(ITLB_DA1(3))); rec("E2", 2, R32(ITLB_DA2(3)));
    rec("E2", 3, R32(UTLB_AA(0))); rec("E2", 4, R32(UTLB_DA1(0))); rec("E2", 5, R32(UTLB_DA2(0)));
    rec("E2", 6, R32(TEA)); rec("E2", 7, R32(PTEH)); rec("E2", 8, X.n);
    at_off();
    hard_clear();
    uint32_t v1m = 0x60000000u | (PA(CODE(0)) & 0x0FF00000u);
    utlb_set(5, v1m, PA(CODE(0)) & 0x1FF00000u, SZ1M, 2, 1, 1, 0, 0, 0, 0, 0);
    at_on(M_LRUI(0));
    xclr(); p_ifetch(v1m + (PA(CODE(0)) & 0xFFFFFu) + 0x800); xdone();
    rec("E2", 9, R32(ITLB_AA(3))); rec("E2", 10, R32(ITLB_DA1(3))); rec("E2", 11, X.n);
    at_off();
    hard_clear();
    /* E4: RTS ; NOP at the end of page 5 (the fetch unit's run-ahead into page 6) */
    for (int var = 0; var < 4; var++) {
        int map6 = !(var & 1);
        int evm = (var & 2) ? 0x03 : 0x07;
        hard_clear();
        umap(5, CODE(5), 3, 1);
        if (map6) umap(6, CODE(6), 3, 1);
        at_on(M_LRUI(0));
        pmc_start(1, evm);
        uint32_t c0 = pmc_lo(1);
        xclr(); p_ifetch(VA(CODE(5)) + 0xFFC); xdone();
        uint32_t c1 = pmc_lo(1);
        rec("E4", (uint32_t)(4 * var), c1 - c0); rec("E4", (uint32_t)(4 * var + 1), ipages());
        rec("E4", (uint32_t)(4 * var + 2), X.n); rec("E4", (uint32_t)(4 * var + 3), X.expevt);
        at_off();
    }
    /* E4c / d: RTS at p7 + 0xFFE, its delay slot at p8 + 0 */
    for (int var = 0; var < 2; var++) {
        hard_clear();
        umap(7, CODE(7), 3, 1);
        if (var) umap(8, CODE(8), 3, 1);
        at_on(M_LRUI(0));
        pmc_start(1, 0x07);
        uint32_t c0 = pmc_lo(1);
        xclr(); R32(PTEH) = 0; p_ifetch(VA(CODE(7)) + 0xFFE); xdone();
        uint32_t c1 = pmc_lo(1);
        uint32_t b = (uint32_t)(8 * var);
        rec("E4c", b, X.n); rec("E4c", b + 1, X.expevt); rec("E4c", b + 2, X.tea); rec("E4c", b + 3, X.pteh);
        rec("E4c", b + 4, X.n ? X.spc - (VA(CODE(7)) + 0xFFE) : 0);
        rec("E4c", b + 5, X.n ? X.pr - ((uint32_t)(uintptr_t)p_ifetch_at + 4) : 0);
        rec("E4c", b + 6, c1 - c0);
        at_off();
    }
    hard_clear();
}

/* ================================================================== F: data TLB exceptions */
static uint32_t F_k;
static void frow(const void *at, uint32_t r) {
    rec("Fn", F_k, X.n); rec("Fev", F_k, X.expevt); rec("Ftea", F_k, X.tea); rec("Fpteh", F_k, X.pteh);
    rec("Fspc", F_k, X.n ? X.spc - (uint32_t)(uintptr_t)at : 0); rec("Fret", F_k, r);
    F_k++;
}
static void grp_F(void) {
    volatile uint32_t sink = 0, fout = 0; uint32_t mac[4], r;
    const uint32_t d0 = VA(DATA(0)), d1 = VA(DATA(1)), d2 = VA(DATA(2)), d3 = VA(DATA(3)), d4 = VA(DATA(4)),
                   d5 = VA(DATA(5)), un = VA(DATA(7));
#define RUN(at_, expr) do { xclr(); R32(PTEH) = 0; r = (expr); xdone(); frow(at_, r); } while (0)
    F_k = 0;
    hard_clear();
    umap(16, DATA(0), 3, 1); umap(17, DATA(1), 0, 1); umap(18, DATA(2), 2, 1);
    umap(19, DATA(3), 1, 0); umap(20, DATA(4), 0, 0); umap(21, DATA(5), 3, 0);
    if (at_on(0)) return;
    RUN(p_ld32_at, p_ld32(un + 0x124));
    RUN(p_st32_at, p_st32(un + 0x124, 1));
    RUN(p_ld8_at, p_ld8(un + 0x3FF));
    RUN(p_ld16_at, p_ld16(un + 1));
    RUN(p_st32_at, p_st32(un + 2, 1));
    RUN(p_ld32_at, p_ld32(d0 + 2));
    RUN(p_ldf64_at, p_ldf64(d0 + 4));
    RUN(p_ldf64_at, p_ldf64(un));
    RUN(p_ld32_at, p_ld32(d1));
    RUN(p_st32_at, p_st32(d1, 1));
    RUN(p_ld32_at, p_ld32(d2));
    RUN(p_st32_at, p_st32(d2, 1));
    RUN(p_ld32_at, p_ld32(d3));
    RUN(p_st32_at, p_st32(d3, 1));
    RUN(p_st32_at, p_st32(d4, 1));
    RUN(p_st8_at, p_st8(d5 + 3, 1));
    RUN(p_st32_at, p_st32(d0 + 0x100, 0x12345678));
    RUN(p_and_gbr_at, p_and_gbr(un));
    RUN(p_and_gbr_at, p_and_gbr(d1 + 0x10));
    RUN(p_and_gbr_at, p_and_gbr(d3 + 0x10));
    RUN(p_tst_gbr_at, p_tst_gbr(d1 + 0x10));
    RUN(p_tst_gbr_at, p_tst_gbr(un));
    RUN(p_tas_at, p_tas(un + 5));
    RUN(p_tas_at, p_tas(d1 + 0x11));
    RUN(p_tas_at, p_tas(d3 + 0x11));
    RUN(p_ocbi_at, p_ocbi(un + 0x40));
    RUN(p_ocbp_at, p_ocbp(un + 0x40));
    RUN(p_ocbwb_at, p_ocbwb(un + 0x40));
    RUN(p_pref_at, p_pref(un + 0x40));
    RUN(p_movca_at, p_movca(un + 0x40, 1));
    RUN(p_ocbi_at, p_ocbi(d1 + 0x40));
    RUN(p_ocbi_at, p_ocbi(d3 + 0x40));
    RUN(p_ocbp_at, p_ocbp(d1 + 0x40));
    RUN(p_ocbwb_at, p_ocbwb(d1 + 0x40));
    RUN(p_movca_at, p_movca(d1 + 0x40, 1));
    RUN(p_movca_at, p_movca(d3 + 0x40, 1));
    RUN(p_pref_at, p_pref(d1 + 0x40));
    RUN(p_ocbi_at, p_ocbi(d0 + 0x203));
    RUN(p_macl_at, (p_macl(d0 + 0x80, un + 8, mac), mac[0]));
    uint32_t m1a = mac[0], m1b = mac[1];
    RUN(p_macl_at, (p_macl(un + 8, d0 + 0x80, mac), mac[0]));
    uint32_t m2a = mac[0], m2b = mac[1];
    RUN(p_ldpi_at, p_ldpi(un + 0x20));
    RUN(p_stpd_at, p_stpd(un + 0x24, 7));
    RUN(p_pair_f_at, p_pair_f(un, 100));
    RUN(p_pair_l_at, p_pair_l(un, 100));
    RUN(p_pair_f_at, p_pair_f(d0, 100));
    sink = 0;
    RUN(p_ld_st_at, (p_ld_st(un, 0x1234, &sink), sink));
    sink = 0;
    RUN(p_st_st_at, (p_st_st(un, 0x1234, &sink), sink));
    fout = 0xFFFFFFFFu;
    RUN(p_ld_fadd_at, (p_ld_fadd(un, 0, &fout), fout));
    fout = 0xFFFFFFFFu;
    RUN(p_ld_fadd_at, (p_ld_fadd(d0, 0, &fout), fout));
    RUN(p_bra_ds_at, p_bra_ds(un));
    RUN(p_jsr_ds_at, p_jsr_ds(un, (uint32_t)(uintptr_t)p_dummy, 0x12345678u));
    rec("Fjsrpr", 0, X.pr - ((uint32_t)(uintptr_t)p_jsr_ds_at + 4));
    xclr(); R32(PTEH) = 0x00000300u; r = p_ld32(un + 0x3FC); xdone();
    rec("Fx", 0, X.pteh); rec("Fx", 1, X.tea);
    at_off();
    R32(PTEH) = 0x12345678u;
    RUN(p_ld32_at, p_ld32(DATA(0) + 2));
    R32(PTEH) = 0x12345678u; xclr(); r = p_st16(DATA(0) + 1, 0); xdone();
    rec("Fx", 2, X.n); rec("Fx", 3, X.expevt); rec("Fx", 4, X.tea); rec("Fx", 5, X.pteh);
    rec("Fmac", 0, m1a); rec("Fmac", 1, m1b); rec("Fmac", 2, m2a); rec("Fmac", 3, m2b);
    R32(PTEH) = 0;
    hard_clear();
#undef RUN
}

/* ================================================================== G: translation */
static void grp_G(void) {
    uint32_t k = 0;
#define RD(va_) do { uint32_t _v = (va_); xclr(); uint32_t _g = p_ld32(_v); xdone(); \
        rec("Gva", k, _v); rec("Ggot", k, _g); rec("Gn", k, X.n); rec("Gev", k, X.expevt); k++; } while (0)
    const uint32_t pd = PA(DATA(0));
    hard_clear();
    umap(16, DATA(0), 3, 1);
    uint32_t v1k = 0x30000000u | pd, v64 = 0x50000000u | (pd & 0x0FFF0000u), v1m = 0x60000000u | (pd & 0x0FF00000u);
    utlb_set(24, v1k, pd, SZ1K, 3, 0, 1, 0, 0, 0, 0, 0);
    utlb_set(25, v64, pd & 0x1FFF0000u, SZ64K, 3, 0, 1, 0, 0, 0, 0, 0);
    utlb_set(26, v1m, pd & 0x1FF00000u, SZ1M, 3, 0, 1, 0, 0, 0, 0, 0);
    if (at_on(0)) return;
    RD(v1k); RD(v1k + 0x3FC); RD(v1k + 0x400);
    RD(VA(DATA(0)) + 0xFFC); RD(VA(DATA(6)));
    RD(v64 + (pd & 0xFFFF)); RD(v64 + 0xFFFC); RD(v64 + 0x10000);
    RD(v1m + (pd & 0xFFFFF)); RD(v1m + 0xFFFFC); RD(v1m + 0x100000);
    at_off();
    hard_clear();
    const uint32_t vx = 0x08000000u | PA(DATA(1)), px = PA(DATA(1));
    utlb_set(27, vx | 0x400, px, SZ4K, 3, 0, 1, 0, 0, 0, 0, 0);
    at_on(0);
    RD(vx); RD(vx + 0x400); RD(vx + 0xC00);
    at_off(); hard_clear();
    utlb_set(27, vx, px | 0x400, SZ4K, 3, 0, 1, 0, 0, 0, 0, 0);
    at_on(0);
    RD(vx); RD(vx); RD(vx + 0x800);
    at_off(); hard_clear();
    utlb_set(25, v64, (pd & 0x1FFF0000u) | 0xFC00u, SZ64K, 3, 0, 1, 0, 0, 0, 0, 0);
    at_on(0);
    RD(v64 + 0x100);
    at_off(); hard_clear();
    const uint32_t va_ = 0x20000000u | PA(DATA(2)), pa_ = PA(DATA(2));
    utlb_set(28, va_, pa_, SZ4K, 3, 0, 1, 0, 0, 0x12, 0, 0);
    R32(PTEH) = 0;
    at_on(0);
    RD(va_);
    R32(PTEH) = 0x12;
    RD(va_);
    RD(va_ + 0x1000);
    rec("Gpteh", 0, X.pteh);
    R32(PTEH) = 0;
    at_off(); hard_clear();
    utlb_set(28, va_, pa_, SZ4K, 3, 0, 1, 1, 0, 0x12, 0, 0);
    at_on(0);
    RD(va_);
    at_off(); hard_clear();
    mmucr_set(M_SV | M_TI);
    utlb_set(28, va_, pa_, SZ4K, 3, 0, 1, 0, 0, 0x12, 0, 0);
    if (!tlb_check()) mmucr_set(M_SV | M_AT);
    RD(va_);
    mmucr_set(M_TI); hard_clear();
    const uint32_t v3 = 0xC0000000u | PA(DATA(3));
    utlb_set(29, v3, PA(DATA(3)), SZ4K, 3, 0, 1, 0, 0, 0, 0, 0);
    at_on(0);
    RD(v3); RD(v3 + 0x1000);
    at_off(); hard_clear();
    utlb_set(30, 0x3F000000u, 0x1F000000u, SZ1M, 1, 0, 1, 0, 0, 0, 0, 0);
    at_on(0);
    R32(TEA) = MARK;
    xclr(); R32(TEA) = 0x13572468u; uint32_t a7tea = p_ld32(0x3F00000Cu); xdone(); uint32_t a7n1 = X.n;
    xclr(); uint32_t a7mm = p_ld32(0x3F000010u); xdone(); uint32_t a7n2 = X.n;
    uint32_t mmnow = R32(MMUCR);
    at_off(); hard_clear();
    rec("Ga7", 0, a7tea); rec("Ga7", 1, a7n1); rec("Ga7", 2, a7mm); rec("Ga7", 3, a7n2); rec("Ga7", 4, mmnow);
#undef RD
}

/* ================================================================== S: store queues with the MMU on */
static void sq_case(int c, int map, int pr, int d, uint32_t tag) {
    uint32_t e1, e2, n1, n2, t1, u0, u1, u2;
    for (int i = 0; i < 8; i++) R32(sqbuf_at(i)) = 0;
    hard_clear();
    umap(16, DATA(0), 3, 1);
    if (map) utlb_set(32, 0xE0000000u, PA(SQBUF) & 0x1FF00000u, SZ1M, pr, 0, d, 0, 0, 0, 0, 0);
    if (at_on(0)) return;
    u0 = urc();
    xclr();
    for (int i = 0; i < 8; i++) { p_st32(SQA + 4 * (uint32_t)i, (tag << 16) | (uint32_t)i); xdone(); }
    n1 = X.n; e1 = X.expevt; t1 = X.tea; u1 = urc();
    xclr(); p_pref(SQA); xdone(); n2 = X.n; e2 = X.expevt; u2 = urc();
    at_off();
    delay(2000);
    uint32_t b = (uint32_t)(16 * c);
    rec("S", b, n1); rec("S", b + 1, e1); rec("S", b + 2, t1); rec("S", b + 3, n2); rec("S", b + 4, e2);
    rec("S", b + 5, u0); rec("S", b + 6, u1); rec("S", b + 7, u2);
    for (int i = 0; i < 7; i++) rec("Smem", (uint32_t)(8 * c + i), R32(sqbuf_at(i)));
}
static void grp_S(void) {
    static const struct { int map, pr, d; uint32_t tag; } C[5] = {
        {0, 1, 1, 0xA001}, {1, 1, 1, 0xA002}, {1, 0, 1, 0xA003}, {1, 1, 0, 0xA004}, {1, 2, 1, 0xA005}};
    for (int c = 0; c < 5; c++) { sq_case(c, C[c].map, C[c].pr, C[c].d, C[c].tag); hard_clear(); }
}

/* ================================================================== T: timing */
static void grp_T(void) {
    uint32_t hit[32], miss[32], miss63[32], im0, im1, im2, im3;
    const uint32_t ctr = PMCTR_LO(0), tgt = VA(CODE(0));
    pmc_start(1, 0x07);
    e_setup5();
    at_on(M_LRUI(0));
    p_ifetch(tgt); p_ifetch(tgt); xdone();
    for (int i = 0; i < 32; i++) hit[i] = t_call(tgt, 0, 0, ctr);
    im0 = pmc_lo(1);
    for (int i = 0; i < 32; i++) { int e = find_itlb(tgt); miss[i] = t_call(tgt, e >= 0 ? ITLB_AA(e) : 0, 0, ctr); }
    im1 = pmc_lo(1);
    at_off();
    hard_clear();
    umap(63, CODE(0), 3, 1);
    at_on(M_LRUI(0));
    p_ifetch(tgt); xdone();
    im2 = pmc_lo(1);
    for (int i = 0; i < 32; i++) { int e = find_itlb(tgt); miss63[i] = t_call(tgt, e >= 0 ? ITLB_AA(e) : 0, 0, ctr); }
    im3 = pmc_lo(1);
    at_off();
    hard_clear();
    for (int k = 0; k < 16; k++) umap(40 + k, CHASE(k), 3, 1);
    for (int k = 0; k < 16; k++) {
        uint32_t *n = (uint32_t *)(uintptr_t)(CHASE(k) + (uint32_t)k * 32u);
        uint32_t nx = CHASE((k + 1) & 15) + (uint32_t)((k + 1) & 15) * 32u;
        n[0] = nx;
        n[1] = VA(nx) + 4;
        uint32_t *q = (uint32_t *)(uintptr_t)(CHASE(0) + 0x800u + (uint32_t)k * 64u);
        uint32_t qx = CHASE(0) + 0x800u + (uint32_t)((k + 1) & 15) * 64u;
        q[2] = qx + 8;
        q[3] = VA(qx) + 12;
    }
    dc_flush(CHASEBUF, 0x10000);
    const uint32_t start[4] = {CHASE(0), VA(CHASE(0)) + 4, CHASE(0) + 0x808, VA(CHASE(0) + 0x800) + 12};
    at_on(0);
    for (int c = 0; c < 4; c++) {
        uint32_t v[8];
        t_chase(start[c], 4, ctr);
        for (int i = 0; i < 8; i++) v[i] = t_chase(start[c], 32, ctr);
        rec("T2", (uint32_t)c, vmin(v, 8));
    }
    at_off();
    hard_clear();
    rec("T1", 0, vmin(hit, 32)); rec("T1", 1, vmax(hit, 32)); rec("T1", 2, vmin(miss, 32)); rec("T1", 3, im1 - im0);
    rec("T1", 4, vmin(miss63, 32)); rec("T1", 5, im3 - im2);
}

/* ================================================================== U: user mode */
static void grp_U(void) {
    uint32_t k = 0;
#define UR(pc_, a6) do { xclr(); p_user((pc_), USR, (a6), ESC); xdone(); rec("Un", k, X.n); rec("Uev", k, X.expevt); \
        rec("Utea", k, X.tea); rec("Uspc", k, X.n ? X.spc - (pc_) : 0); k++; } while (0)
    const uint32_t ld = VA(CODE(9)), st = VA(CODE(9)) + 0x10, pg10 = VA(CODE(10));
    hard_clear();
    umap(9, CODE(9), 3, 1); umap(10, CODE(10), 1, 1);
    umap(16, DATA(0), 3, 1); umap(17, DATA(1), 0, 1); umap(18, DATA(2), 2, 1); umap(19, DATA(3), 1, 0);
    umap(21, DATA(5), 3, 0);
    utlb_set(32, 0xE0000000u, PA(SQBUF) & 0x1FF00000u, SZ1M, 3, 0, 1, 0, 0, 0, 0, 0);
    if (at_on(0)) return;
    UR(ld, VA(DATA(1)));
    UR(ld, VA(DATA(3)));
    UR(ld, VA(DATA(2)));
    UR(st, VA(DATA(2)));
    UR(st, VA(DATA(5)));
    UR(st, VA(DATA(0)) + 0x300);
    UR(ld, DATA(0));
    UR(st, SQA);
    mmucr_set(M_AT | M_SQMD);
    UR(st, SQA);
    mmucr_set(M_AT);
    UR(pg10, 0);
    int e10 = find_itlb(pg10);
    rec("Ui10", 0, (uint32_t)e10); rec("Ui10", 1, e10 >= 0 ? R32(ITLB_DA1(e10)) : 0);
    UR((uint32_t)(uintptr_t)p_dummy, 0);
    rec("Udummy", 0, X.tea - (uint32_t)(uintptr_t)p_dummy);
    at_off();
    mmucr_set(M_SQMD);
    UR(st, SQA);
    mmucr_set(0);
    hard_clear();
#undef UR
}

/* ================================================================== mmutest2: R1 R2 R3 R4 R5 R6 R7 */
static void grp_R1(void) {
    static const uint16_t pos[] = {0xFFC, 0xFF8, 0xFF4, 0xFF0, 0xFEC, 0xFE8, 0xFE4, 0xFE0, 0xFDC, 0xFD8, 0xFD0, 0xFC0,
                                   0xFA0, 0xF80};
    for (unsigned i = 0; i < sizeof pos / sizeof pos[0]; i++) {
        hard_clear();
        ic_inval();                                   /* KOS icache_flush_range(p5, 0x2000): cold IC */
        umap(5, CODE(5), 3, 1); umap(6, CODE(6), 3, 1);
        at_on(M_LRUI(0));
        xclr(); p_ifetch(VA(CODE(5)) + pos[i]); xdone();
        rec("R1c", 3 * i, ipages()); rec("R1c", 3 * i + 1, G_LRUI(R32(MMUCR))); rec("R1c", 3 * i + 2, X.n);
        for (int e = 0; e < 4; e++) R32(ITLB_AA(e)) = 0;
        mmucr_set(M_AT | M_LRUI(0));
        xclr(); p_ifetch(VA(CODE(5)) + pos[i]); xdone();
        rec("R1w", 3 * i, ipages()); rec("R1w", 3 * i + 1, G_LRUI(R32(MMUCR))); rec("R1w", 3 * i + 2, X.n);
        at_off();
    }
}
static void grp_R2(void) {
    uint32_t a[16], b[16], c[16], d[16], e_[16], f[16];
    const uint32_t ctr = PMCTR_LO(0);
    const uint32_t t5 = VA(CODE(5)) + 0xFFC, t11 = VA(CODE(11)) + 0xFF0;
    hard_clear();
    umap(5, CODE(5), 3, 1); umap(6, CODE(6), 3, 1);
    at_on(M_LRUI(0));
    p_ifetch(t5); p_ifetch(t5); xdone();
    for (int i = 0; i < 16; i++) a[i] = t_call(t5, 0, 0, ctr);
    for (int i = 0; i < 16; i++) { int e = find_itlb(VA(CODE(6))); b[i] = t_call(t5, e >= 0 ? ITLB_AA(e) : 0, 0, ctr); }
    uint32_t s6b = ipages();
    R32(UTLB_AA(6)) = 0;
    { int e = find_itlb(VA(CODE(6))); if (e >= 0) R32(ITLB_AA(e)) = 0; }
    for (int i = 0; i < 16; i++) c[i] = t_call(t5, 0, 0, ctr);
    at_off();
    hard_clear();
    umap(11, CODE(11), 3, 1); umap(12, CODE(12), 3, 1);
    at_on(M_LRUI(0));
    p_ifetch(t11); p_ifetch(t11); xdone();
    uint32_t s11 = ipages();
    for (int i = 0; i < 16; i++) d[i] = t_call(t11, 0, 0, ctr);
    for (int i = 0; i < 16; i++) { int e = find_itlb(VA(CODE(12))); e_[i] = t_call(t11, e >= 0 ? ITLB_AA(e) : 0, 0, ctr); }
    for (int i = 0; i < 16; i++) { int e = find_itlb(VA(CODE(11))); f[i] = t_call(t11, e >= 0 ? ITLB_AA(e) : 0, 0, ctr); }
    at_off();
    hard_clear();
    rec("R2", 0, vmin(a, 16)); rec("R2", 1, vmax(a, 16)); rec("R2", 2, vmin(b, 16)); rec("R2", 3, vmax(b, 16));
    rec("R2", 4, s6b); rec("R2", 5, vmin(c, 16)); rec("R2", 6, vmax(c, 16));
    rec("R2", 7, vmin(d, 16)); rec("R2", 8, vmax(d, 16)); rec("R2", 9, s11); rec("R2", 10, vmin(e_, 16));
    rec("R2", 11, vmax(e_, 16)); rec("R2", 12, vmin(f, 16)); rec("R2", 13, vmax(f, 16));
}
static void grp_R3(void) {
    const uint32_t dummy = (uint32_t)(uintptr_t)p_dummy, un = VA(DATA(7));
    uint32_t k = 0;
#define PR3(at_, expr) do { xclr(); (void)(expr); xdone(); rec("R3n", k, X.n); rec("R3ev", k, X.expevt); \
        rec("R3spc", k, X.spc - (uint32_t)(uintptr_t)(at_)); rec("R3pr", k, X.pr - ((uint32_t)(uintptr_t)(at_) + 4)); \
        rec("R3abs", k, X.spc); k++; } while (0)
    hard_clear();
    umap(16, DATA(0), 3, 1);
    at_on(0);
    PR3(p_bsr_ds_at, p_bsr_ds(un, 0, 0x12345678u));
    PR3(p_jsr_ds2_at, p_jsr_ds2(un, dummy, 0x12345678u));
    PR3(p_jsr_ds2_at, p_jsr_ds2(VA(DATA(0)) + 2, dummy, 0x12345678u));
    PR3(p_jsr_ill_at, p_jsr_ill(0, dummy, 0x12345678u));
    PR3(p_jsr_ds2_at, p_jsr_ds2(VA(DATA(0)), VA(CODE(15)), 0x12345678u));
    at_off();
    hard_clear();
#undef PR3
}
static void grp_R4(void) {
    const uint32_t d0 = VA(DATA(0));
    uint32_t k = 0;
    hard_clear();
    umap(16, DATA(0), 3, 1);
    at_on(M_URC(10));
    rec("R4", k++, urc());
    p_ocbwb(d0 + 0x400); xdone(); rec("R4", k++, urc());
    p_ocbi(d0 + 0x400); xdone(); rec("R4", k++, urc());
    p_pref(d0 + 0x400); xdone(); rec("R4", k++, urc());
    delay(200);
    p_pref(d0 + 0x400); xdone(); rec("R4", k++, urc());
    p_movca(d0 + 0x440, 5); xdone(); rec("R4", k++, urc());
    p_stf32(d0 + 0x480); xdone(); rec("R4", k++, urc());
    p_ld32(d0 + 0x400); xdone(); rec("R4", k++, urc());
    at_off();
    hard_clear();
}
static void grp_R5(void) {
    const uint32_t un = VA(DATA(7)), ucode = VA(CODE(15)), v8 = VA(DATA(8));
    for (int n = 0; n < 9; n++) {
        hard_clear(); umap(16, DATA(0), 3, 1);
        if (tlb_check()) break;
        xclr(); mlat[n](MMUCR, M_AT, un); xdone(); rec("R5on", (uint32_t)n, X.n ? X.expevt : 0);
        at_off();
    }
    for (int n = 0; n < 9; n++) {
        hard_clear(); umap(16, DATA(0), 3, 1);
        at_on(0);
        xclr(); mlat[n](MMUCR, 0, un); xdone(); rec("R5off", (uint32_t)n, X.n ? X.expevt : 0);
        at_off();
    }
    for (int n = 0; n < 11; n++) {
        hard_clear(); umap(16, DATA(0), 3, 1);
        if (tlb_check()) break;
        xclr(); mjmp[n](MMUCR, M_AT, ucode); xdone(); rec("R5jsr", (uint32_t)n, X.n ? X.expevt : 0);
        at_off();
    }
    for (int n = 0; n < 4; n++) {
        hard_clear(); umap(16, DATA(0), 3, 1);
        R32(PTEH) = v8 & 0xFFFFFC00u;
        R32(PTEL) = (PA(DATA(8)) & 0x1FFFFC00u) | 0x100u | 0x10u | 0x60u | 0x04u;
        R32(PTEA) = 0;
        at_on(M_URC(50));
        xclr(); llat[n](0, 0, v8); xdone(); rec("R6l", (uint32_t)n, X.n ? X.expevt : 0);
        at_off();
    }
    R32(PTEH) = 0; R32(PTEL) = 0;
    for (int n = 0; n < 4; n++) {
        hard_clear(); umap(16, DATA(0), 3, 1);
        R32(UTLB_DA1(50)) = (PA(DATA(8)) & 0x1FFFFC00u) | 0x10u | 0x60u | 0x04u;
        at_on(0);
        xclr(); ulat[n](UTLB_AA(50), (v8 & 0xFFFFFC00u) | 0x300u, v8); xdone(); rec("R6u", (uint32_t)n, X.n ? X.expevt : 0);
        at_off();
    }
    hard_clear();
}
static void grp_R7(void) {
    uint32_t n1, e1, n2, e2;
    for (int i = 0; i < 8; i++) R32(sqbuf_at(i)) = 0;
    hard_clear(); umap(16, DATA(0), 3, 1);
    at_on(0);
    xclr();
    for (int i = 0; i < 8; i++) { p_st32(SQA + 4 * (uint32_t)i, 0xB0010000u | (uint32_t)i); xdone(); }
    n1 = X.n; e1 = X.expevt;
    at_off();
    utlb_set(32, 0xE0000000u, PA(SQBUF) & 0x1FF00000u, SZ1M, 1, 0, 1, 0, 0, 0, 0, 0);
    at_on(0);
    xclr(); p_pref(SQA); xdone(); n2 = X.n; e2 = X.expevt;
    at_off();
    delay(2000);
    rec("R7", 0, n1); rec("R7", 1, e1); rec("R7", 2, n2); rec("R7", 3, e2);
    for (int i = 0; i < 8; i++) rec("R7mem", (uint32_t)i, R32(sqbuf_at(i)));
    hard_clear();
}

/* ================================================================== mmutest3: V P M */
static void vrun(uint32_t tgt, int drop_kind, uint32_t drop, int mode, uint32_t r[4]) {
    uint32_t cmin = ~0u, cmax = 0, emin = ~0u, emax = 0;
    pmc_start(1, mode);
    for (int i = 0; i < 16; i++) {
        uint32_t aa = 0;
        if (drop_kind) { int e = find_itlb(drop); if (e >= 0) aa = ITLB_AA(e); }
        uint32_t e0 = pmc_lo(1);
        uint32_t c = t_call(tgt, aa, 0, PMCTR_LO(0));
        uint32_t ev = pmc_lo(1) - e0;
        if (c < cmin) cmin = c;
        if (c > cmax) cmax = c;
        if (ev < emin) emin = ev;
        if (ev > emax) emax = ev;
    }
    r[0] = cmin; r[1] = cmax; r[2] = emin; r[3] = emax;
}
static void vvar(uint32_t vi, uint32_t tgt, int kind, uint32_t drop, int pa, int pb, int ca, int cb) {
    static const int modes[4] = {0x08, 0x21, 0x24, 0x06};
    uint32_t r[4];
    for (int m = 0; m < 4; m++) {
        hard_clear();
        umapc(pa, CODE(pa), ca); if (pb >= 0) umapc(pb, CODE(pb), cb);
        at_on(M_LRUI(0));
        p_ifetch(tgt); p_ifetch(tgt); xdone();
        vrun(tgt, kind, drop, modes[m], r);
        at_off();
        hard_clear();
        if (m == 0) { rec("Vcyc", 2 * vi, r[0]); rec("Vcyc", 2 * vi + 1, r[1]); }
        rec("Vev", 8 * vi + 2 * (uint32_t)m, r[2]); rec("Vev", 8 * vi + 2 * (uint32_t)m + 1, r[3]);
    }
}
static void grp_V(void) {
    const uint32_t t0 = VA(CODE(0)), t5 = VA(CODE(5)) + 0xFFC, t11 = VA(CODE(11)) + 0xFF0;
    vvar(0, t0, 0, 0, 0, -1, 1, 1);
    vvar(1, t0, 1, t0, 0, -1, 1, 1);
    vvar(2, t5, 0, 0, 5, 6, 1, 1);
    vvar(3, t5, 1, VA(CODE(6)), 5, 6, 1, 1);
    vvar(4, t5, 0, 0, 5, -1, 1, 1);
    vvar(5, t11, 0, 0, 11, 12, 1, 1);
    vvar(6, t11, 1, VA(CODE(12)), 11, 12, 1, 1);
    vvar(7, t11, 0, 0, 11, 12, 1, 0);
    vvar(8, t11, 1, VA(CODE(12)), 11, 12, 1, 0);
    vvar(9, t5, 0, 0, 5, 6, 1, 0);
    vvar(10, t5, 1, VA(CODE(6)), 5, 6, 1, 0);
}
static void grp_P(void) {
    uint32_t pa = PA(DATA(0)) + 0x600, v = VA(DATA(0)) + 0x600;
    hard_clear();
    umapc(16, DATA(0), 1);
    R32(UTLB_AA(16)) = (VA(DATA(0)) & 0xFFFFFC00u) | 0x300u;
    at_on(0);
    p_ocbp(v); xdone(); rec("P", 0, R32(OC_AA(pa)));
    p_pref(v); xdone();
    delay(500);
    rec("P", 1, R32(OC_AA(pa)));
    p_ocbp(v); xdone(); rec("P", 2, R32(OC_AA(pa)));
    p_pref(DATA(0) + 0x600); xdone();
    delay(500);
    rec("P", 3, R32(OC_AA(pa)));
    p_ocbp(v); xdone(); rec("P", 4, R32(OC_AA(pa)));
    p_ocbp(DATA(9) + 0x600); xdone();
    p_pref(VA(DATA(9)) + 0x600); xdone();
    delay(500);
    rec("P", 5, R32(OC_AA(PA(DATA(9)) + 0x600)));
    at_off();
    hard_clear();
}
static void grp_M(void) {
    const uint32_t un = VA(DATA(7)) + 0x10;
    for (int i = 0; i < 4; i++) {
        hard_clear();
        umapc(16, DATA(0), 1);
        at_on(0);
        xclr(); uint32_t v = mlat[i](MMUCR, 0, un); xdone();
        rec("Mv", (uint32_t)i, v); rec("Mn", (uint32_t)i, X.n);
        at_off();
    }
    hard_clear();
}

/* ================================================================== main */
int main(void) {
    volatile uint32_t *res = (volatile uint32_t *)(uintptr_t)P2(RES_PA);
    res[0] = 0; nrec = 0;
    pmc_start(0, 0x23);                                /* PRFC0: elapsed cycles (KOS's ns timer, t_call's counter) */
    /* the buffers as mmutest*.c built them (code pages: rts ; nop everywhere; p7 ends nop, rts; p8 starts nop; p9 /
       p10 user snippets; 8 nops at p11 + ff0), written through P1 then written back */
    uint16_t *cw = (uint16_t *)(uintptr_t)CODEBUF;
    for (int i = 0; i < 0x8000; i += 2) { cw[i] = 0x000B; cw[i + 1] = 0x0009; }
    cw[(7 * 0x1000 + 0xFFC) / 2] = 0x0009; cw[(7 * 0x1000 + 0xFFE) / 2] = 0x000B;
    cw[(8 * 0x1000) / 2] = 0x0009;
    cw[(9 * 0x1000 + 0x00) / 2] = 0x6062; cw[(9 * 0x1000 + 0x02) / 2] = 0x6072;
    cw[(9 * 0x1000 + 0x10) / 2] = 0x2652; cw[(9 * 0x1000 + 0x12) / 2] = 0x6072;
    cw[(10 * 0x1000 + 0x00) / 2] = 0x6072; cw[(10 * 0x1000 + 0x02) / 2] = 0x0009;
    for (int i = 0; i < 8; i++) cw[(11 * 0x1000 + 0xFF0) / 2 + i] = 0x0009;
    uint32_t *dw = (uint32_t *)(uintptr_t)DATABUF;
    for (int i = 0; i < 0x4000; i++) dw[i] = 0xD0000000u | (uint32_t)(i * 4);
    uint32_t *chw = (uint32_t *)(uintptr_t)CHASEBUF;
    for (int i = 0; i < 0x4000; i++) chw[i] = 0;
    dc_flush(CODEBUF, 0x10000);
    dc_flush(DATABUF, 0x10000);
    dc_flush(CHASEBUF, 0x10000);
    dc_purge(SQBUF, 64);
    ic_inval();
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
    grp_R1();
    grp_R2();
    grp_R3();
    grp_R4();
    grp_R5();
    grp_R7();
    grp_V();
    grp_P();
    grp_M();
    hard_clear();
    rec("END", 0, R32(MMUCR));
    res[1] = nrec;
    res[2] = g_unexpected;
    res[3] = g_irqs;
    res[0] = 0x4D4D5542u;                              /* 'MMUB': the record block is complete */
    return 0;
}
