/* ccrx.c -- M3 verification: CCR writes, cache-array accesses and cache-state changes while the memory system has
 * transactions in flight, on the whole chip (rm).  Each case prints "CCRX <case> ... got=<v> want=<v> ok=<0|1>";
 * `want` is the manual's result (hm 4.2-4.5; model-free), and --lockstep checks every access against Sh4Sys as well.
 *   A pref_oci k   PREF of an absent line, k cycles, OCI: the fill is in flight at the OCI.  Memory is then changed
 *                  through P2: a P1 read must see the new value (hm 4.3.2 OCI: every V bit cleared; a fill killed after
 *                  its IFILL / accept may not validate the line afterwards), and the OC address array shows V = 0
 *   B ld_oci k     the same after a load miss (acked at the critical word, the fill's tail in flight); the load's value
 *   C st_oci k     a store miss (write-allocate) then OCI: the line is dropped without a write-back, memory keeps OLD
 *   D evict_oci    a dirty line evicted by a load of its alias, OCI at once: the write-back in the WBB still lands
 *   E ocbp_oci     OCBP of a dirty line, OCI at once: the write-back lands
 *   F sq_oci       a store-queue burst (PREF), OCI at once: the burst lands
 *   G ici k        a cold P1 routine is called (its IC fill), k cycles, ICI; memory is patched: the next call runs the
 *                  new code.  G0 (control): without the ICI the stale line runs (the IC is not coherent with memory)
 *   H icaa k       an IC associative address-array write (V = 0) while the routine's fill runs: then the new code runs
 *   I ice          CCR.ICE = 0: the patched code runs (fetched uncached); ICE = 1 again without ICI: the stale line
 *                  (still valid) runs again
 *   J oce          CCR.OCE = 0 with a dirty line: P1 reads / writes go to memory; OCE = 1: the dirty line is back, and
 *                  its OCBP writes it over the uncached store
 *   K ocaa-state   the OC address array's tag / U / V after store, OCBWB, OCBP, PREF, MOVCA.L, OCBI (hm 4.5.3)
 *   L ocda         an OC data-array write: read back through P1; U is not set (hm 4.5.4: a clean line's OCBP does not
 *                  write it back); on a dirty line the OCBP writes the array's word
 *   M ocaa-w       non-associative write {tag Y, U = 1, V = 1} over a dirty line X: X is written back first, then the
 *                  entry holds "Y" with X's data (a P1 read of Y returns it, OCBP writes it to Y); {tag X, V = 0}:
 *                  written back and invalid; associative V = 0 on a matching dirty line: written back and invalid;
 *                  associative with a mismatching tag: no operation
 *   A1 B1 C1       A / B / C from P1 (x1_*: the CCR store 2 + k cycles behind the access, the fill still in flight;
 *                  from P2 the CCR store's own fetch queues behind the fill); ccrx_c1 adds G1, the ICI 2 + k cycles
 *                  behind a cold routine's return (its fill in flight)
 *   N ora          CCR.ORA = 1 (after a flush): the 8 KB OC RAM at 0x7C00xxxx, every size, its 4 KB aliases (hm 4.3.6's
 *                  table), with cached traffic around it; ORA = 0 with OCI
 * Output ends "CCRX summary cases=<n> bad=<n>", "CCRX done". */
#include "m3.h"

extern void x_pref_oci(uint32_t line, uint32_t ccr, uint32_t k, uint32_t aa_p2);
extern uint32_t x_ld_oci(uint32_t line, uint32_t ccr, uint32_t k, uint32_t aa_p2);
extern void x_st_oci(uint32_t line, uint32_t ccr, uint32_t k, uint32_t v);
extern uint32_t x_evict_oci(uint32_t x, uint32_t ccr, uint32_t y, uint32_t v);
extern void x_ocbp_oci(uint32_t line, uint32_t ccr);
extern void x_pref_sq_oci(uint32_t sqa, uint32_t ccr);
extern void x_icseq(uint32_t fn, uint32_t res_p2, uint32_t k);
extern void x_icaa_seq(uint32_t fn, uint32_t res_p2, uint32_t k);
extern void x_ice_seq(uint32_t fn, uint32_t res_p2);
extern void x_oce_seq(uint32_t x, uint32_t res_p2, uint32_t v1, uint32_t v2);
#define RES(i) R32(P2(G_PA) + 0x200u + 4u * (uint32_t)(i))
extern void x_st32(uint32_t a, uint32_t v);
extern uint32_t x_ld32(uint32_t a);
extern void x_flush_ccr(uint32_t dummy_p1, uint32_t ccr);
extern uint32_t r_one(void), r_two(void);
extern void x1_pref_oci(uint32_t line, uint32_t ccr, uint32_t k, uint32_t aa_p2);
extern uint32_t x1_ld_oci(uint32_t line, uint32_t ccr, uint32_t k, uint32_t aa_p2);
extern void x1_st_oci(uint32_t line, uint32_t ccr, uint32_t k, uint32_t v);
extern uint32_t x1_call_ici(uint32_t fn, uint32_t ccr, uint32_t k);

static int ncase, nbad;
static void rep(const char *c, int k, uint32_t got, uint32_t want) {
  int ok = got == want;
  ncase++; if (!ok) nbad++;
  printf("CCRX %s k=%d got=%08lx want=%08lx ok=%d\n", c, k, (unsigned long)got, (unsigned long)want, ok);
}
#define C_PA (TR_PA + 0x8000u)                          /* the cases' lines: C_PA + 0x40 * n (distinct OC entries) */
static inline uint32_t cl(int n) { return C_PA + 0x40u * (uint32_t)n; }
static inline uint32_t oce(uint32_t pa) { return (P1(pa) >> 5) & 0x1FFu; }   /* OC entry, OIX = 0 */
static inline uint32_t ice_(uint32_t va) { return (va >> 5) & 0xFFu; }        /* IC entry, IIX = 0 */
static uint32_t ocaa(uint32_t pa) { return x_ld32(OCAA(oce(pa))) & 0x1FFFFC03u; }
static uint32_t tagv(uint32_t pa, int u, int v) { return (pa & 0x1FFFFC00u) | ((uint32_t)u << 1) | (uint32_t)v; }
#define OCI (CCR_KOS | CCR_OCI)
#define ICI (CCR_KOS | CCR_ICI)

int main(void) {
  printf("CCRX start ccr=%08lx\n", (unsigned long)ccr_get());
  ccr_set(CCR_KOS);
  R32(QACR0) = 0x0C; R32(QACR1) = 0x0C;
  /* ---- A / B / C: fills in flight at the OCI */
  for (uint32_t k = 0; k <= 24; k += 3) {
    uint32_t x = cl(1);
    ocbp(P1(x)); R32(P2(x)) = 0x0A000000u + k; R32(P2(x) + 16) = 0x0A100000u + k;
    x_pref_oci(P1(x), OCI, k, P2(G_PA) + 0x200u);
    uint32_t aa = RES(0);
    R32(P2(x)) = 0x0A200000u + k;
    rep("A pref_oci aa.V", (int)k, aa & 1u, 0);
    rep("A pref_oci", (int)k, R32(P1(x)), 0x0A200000u + k);
    x = cl(2);
    ocbp(P1(x)); R32(P2(x)) = 0x0B000000u + k; R32(P2(x) + 24) = 0x0B100000u + k;
    uint32_t v = x_ld_oci(P1(x), OCI, k, P2(G_PA) + 0x200u);
    rep("B ld_oci value", (int)k, v, 0x0B000000u + k);
    rep("B ld_oci aa.V", (int)k, RES(0) & 1u, 0);
    R32(P2(x) + 24) = 0x0B200000u + k;
    rep("B ld_oci", (int)k, R32(P1(x) + 24), 0x0B200000u + k);
    x = cl(3);
    ocbp(P1(x)); R32(P2(x) + 8) = 0x0C000000u + k;
    x_st_oci(P1(x) + 8, OCI, k, 0x0C100000u + k);
    rep("C st_oci mem", (int)k, R32(P2(x) + 8), 0x0C000000u + k);
    rep("C st_oci p1", (int)k, R32(P1(x) + 8), 0x0C000000u + k);
  }
  /* ---- A1 / B1 / C1 / G1: the same from P1, the CCR store 2 + k cycles behind the access (the fill in flight); each
   * routine is called once dry first (no OCI / ICI) so that its lines are in the I-cache */
  for (uint32_t k = 0; k <= 20; k += 2) {
    uint32_t x = cl(12);
    x1_pref_oci(P1(x), CCR_KOS, k, P2(G_PA) + 0x200u);
    ocbp(P1(x)); R32(P2(x)) = 0x4A000000u + k;
    x1_pref_oci(P1(x), OCI, k, P2(G_PA) + 0x200u);
    rep("A1 pref_oci aa.V", (int)k, RES(0) & 1u, 0);
    R32(P2(x)) = 0x4A200000u + k;
    rep("A1 pref_oci", (int)k, R32(P1(x)), 0x4A200000u + k);
    x = cl(13);
    (void)x1_ld_oci(P1(x), CCR_KOS, k, P2(G_PA) + 0x200u);
    ocbp(P1(x)); R32(P2(x)) = 0x4B000000u + k; R32(P2(x) + 24) = 0x4B100000u + k;
    uint32_t v = x1_ld_oci(P1(x), OCI, k, P2(G_PA) + 0x200u);
    rep("B1 ld_oci value", (int)k, v, 0x4B000000u + k);
    rep("B1 ld_oci aa.V", (int)k, RES(0) & 1u, 0);
    R32(P2(x) + 24) = 0x4B200000u + k;
    rep("B1 ld_oci", (int)k, R32(P1(x) + 24), 0x4B200000u + k);
    x = cl(14);
    x1_st_oci(P1(x) + 8, CCR_KOS, k, 0x4C0F0000u);
    ocbp(P1(x)); R32(P2(x) + 8) = 0x4C000000u + k;
    x1_st_oci(P1(x) + 8, OCI, k, 0x4C100000u + k);
    rep("C1 st_oci mem", (int)k, R32(P2(x) + 8), 0x4C000000u + k);
    rep("C1 st_oci p1", (int)k, R32(P1(x) + 8), 0x4C000000u + k);
#if defined(CASE) && ((CASE) & 1)
    /* G1 (ccrx_c1): fn cold (an IC associative purge through P2), called from the warm P1 routine, ICI 2 + k cycles
     * later.  An IFILL accepted after the ICI landed but before the CCR store retired: tb/soc_checker applied it before
     * the store's step and its lockstep failed (M3-verify F4); since M3-fix it waits for the store's step */
    uint32_t fn = (uint32_t)(uintptr_t)r_two;
    x_st32(P2((uint32_t)(uintptr_t)r_two), 0x000BE001u);
    (void)x1_call_ici(fn, CCR_KOS, k);               /* dry: fn and the routine warm */
    x_st32(ICAA((fn >> 5) & 0xFFu) | 8u, fn & 0xFFFFFC00u);   /* fn's line invalid (its fill will be in flight) */
    uint32_t r = x1_call_ici(fn, CCR_KOS | CCR_ICI, k);
    x_st32(P2(fn), 0x000BE002u);
    rep("G1 ici value", (int)k, r, 1);
    rep("G1 ici", (int)k, r_two(), 2);
#endif
  }
  /* ---- D / E / F: write-backs and bursts in flight at the OCI */
  {
    uint32_t x = cl(4), y = cl(4) + 0x4000u;           /* same OC entry */
    ocbp(P1(x)); ocbp(P1(y)); R32(P2(x)) = 0x0D000000u; R32(P2(y)) = 0x0D100000u;
    uint32_t v = x_evict_oci(P1(x), OCI, P1(y), 0x0D200000u);
    rep("D evict_oci y", 0, v, 0x0D100000u);
    rep("D evict_oci mem", 0, R32(P2(x)), 0x0D200000u);
    x = cl(5);
    ocbp(P1(x)); R32(P2(x) + 4) = 0x0E000000u; R32(P1(x) + 4) = 0x0E100000u;
    x_ocbp_oci(P1(x), OCI);
    rep("E ocbp_oci mem", 0, R32(P2(x) + 4), 0x0E100000u);
    x = cl(6);
    ocbp(P1(x));
    uint32_t sqa = 0xE0000000u | (x & 0x03FFFFE0u);
    for (int w = 0; w < 8; w++) R32(sqa + 4u * (uint32_t)w) = 0x0F000000u + (uint32_t)w;
    x_pref_sq_oci(sqa, OCI);
    rep("F sq_oci mem", 0, R32(P2(x) + 28), 0x0F000007u);
  }
  /* ---- G / H / I: the IC sequences (ccrx_asm.S, all in P2) */
  {
    uint32_t fn1 = (uint32_t)(uintptr_t)r_one, fn2 = (uint32_t)(uintptr_t)r_two;
    for (uint32_t k = 0; k <= 20; k += 2) {
      x_icseq(fn1, P2(G_PA) + 0x200u, k);
      if (k == 0) { rep("G0 cold", 0, RES(0), 1); rep("G0 stale", 0, RES(1), 1); rep("G0 after ici", 0, RES(2), 2); }
      rep("G ici value", (int)k, RES(3), 1);
      rep("G ici aa.V", (int)k, RES(4) & 1u, 0);
      rep("G ici", (int)k, RES(5), 2);
    }
    for (uint32_t k = 0; k <= 12; k += 3) {
      x_icaa_seq(fn2, P2(G_PA) + 0x200u, k);
      rep("H icaa value", (int)k, RES(0), 1);
      rep("H icaa aa.V", (int)k, RES(1) & 1u, 0);
      rep("H icaa", (int)k, RES(2), 2);
    }
    x_ice_seq(fn1, P2(G_PA) + 0x200u);
    rep("I ice warm", 0, RES(0), 1);
    rep("I ice off", 0, RES(1), 3);
    rep("I ice on stale", 0, RES(2), 1);
    rep("I ice on ici", 0, RES(3), 3);
    ccr_set(CCR_KOS | CCR_ICI);
  }
  /* ---- J: OCE off / on with a dirty line (ccrx_asm.S x_oce_seq: all in P2, no other data access) */
  {
    uint32_t x = cl(7);
    ocbp(P1(x)); R32(P2(x)) = 0x1A000000u;
    x_oce_seq(P1(x), P2(G_PA) + 0x200u, 0x1A100000u, 0x1A200000u);
    rep("J oce off read", 0, RES(0), 0x1A000000u);
    rep("J oce off store", 0, RES(1), 0x1A200000u);
    rep("J oce on read", 0, RES(2), 0x1A100000u);
    rep("J oce on ocbp", 0, RES(3), 0x1A100000u);
  }
  /* ---- K: the OC address array's line state; L: the OC data array.  Every value is gathered first (x_ld32 from P2,
   * no printf in between: printf's own data could take the entry) and reported afterwards */
  {
    uint32_t x = cl(8), y = cl(8) + 0x8000u, g[16];
    ocbp(P1(x)); ocbp(P1(y));
    g[0] = ocaa(x) & 1u;
    R32(P1(x) + 4) = 1;
    g[1] = ocaa(x);
    ocbwb(P1(x));
    g[2] = ocaa(x);
    pref(P1(y)); (void)R32(P1(y));
    g[3] = ocaa(x);
    movca(P1(x), 5);
    g[4] = ocaa(x);
    for (int w = 1; w < 8; w++) R32(P1(x) + 4u * (uint32_t)w) = (uint32_t)w;
    ocbi(P1(x));
    g[5] = ocaa(x) & 3u;
    (void)R32(P1(x));
    g[6] = ocaa(x);
    uint32_t z = cl(9);
    ocbp(P1(z)); R32(P2(z)) = 0x2A000000u;
    (void)R32(P1(z));                                  /* clean */
    x_st32(OCDA(oce(z), 0), 0x2A100000u);
    g[7] = R32(P1(z));
    g[8] = ocaa(z);
    ocbp(P1(z));
    g[9] = R32(P2(z));
    R32(P1(z) + 8) = 0x2A200000u;                      /* dirty */
    x_st32(OCDA(oce(z), 0), 0x2A300000u);
    g[10] = R32(P1(z));
    ocbp(P1(z));
    g[11] = R32(P2(z)); g[12] = R32(P2(z) + 8);
    rep("K after ocbp", 0, g[0], 0);
    rep("K store", 0, g[1], tagv(x, 1, 1));
    rep("K ocbwb", 0, g[2], tagv(x, 0, 1));
    rep("K pref alias", 0, g[3], tagv(y, 0, 1));
    rep("K movca", 0, g[4], tagv(x, 1, 1));
    rep("K ocbi", 0, g[5], 0);
    rep("K load", 0, g[6], tagv(x, 0, 1));
    rep("L clean read", 0, g[7], 0x2A100000u);
    rep("L clean U", 0, g[8], tagv(z, 0, 1));
    rep("L clean ocbp mem", 0, g[9], 0x2A000000u);
    rep("L dirty read", 0, g[10], 0x2A300000u);
    rep("L dirty ocbp mem", 0, g[11], 0x2A300000u);
    rep("L dirty ocbp mem8", 0, g[12], 0x2A200000u);
  }
  /* ---- M: OC address-array writes (values gathered first, reported afterwards) */
  {
    uint32_t x = cl(10), y = cl(10) + 0x10000u, g[16];
    ocbp(P1(x)); ocbp(P1(y)); R32(P2(x)) = 0x3A000000u; R32(P2(y)) = 0x3A100000u;
    R32(P1(x)) = 0x3A200000u;                          /* dirty X */
    x_st32(OCAA(oce(x)), tagv(y, 1, 1));
    g[0] = R32(P2(x)); g[1] = ocaa(x); g[2] = R32(P1(y));
    ocbp(P1(y));
    g[3] = R32(P2(y));
    R32(P1(x)) = 0x3A300000u;                          /* dirty X again */
    x_st32(OCAA(oce(x)), tagv(x, 0, 0));
    g[4] = R32(P2(x)); g[5] = ocaa(x) & 3u;
    R32(P1(x)) = 0x3A400000u;
    x_st32(OCAA(oce(x)) | OCAA_ASSOC, P1(x) & 0xFFFFFC00u);
    g[6] = R32(P2(x)); g[7] = ocaa(x) & 3u;
    R32(P1(x)) = 0x3A500000u;
    x_st32(OCAA(oce(x)) | OCAA_ASSOC, P1(y) & 0xFFFFFC00u);   /* mismatch: no operation */
    g[8] = ocaa(x); g[9] = R32(P2(x));
    ocbp(P1(x));
    g[10] = R32(P2(x));
    rep("M nonassoc wb", 0, g[0], 0x3A200000u);
    rep("M nonassoc aa", 0, g[1], tagv(y, 1, 1));
    rep("M nonassoc read y", 0, g[2], 0x3A200000u);
    rep("M nonassoc y mem", 0, g[3], 0x3A200000u);
    rep("M nonassoc v0 wb", 0, g[4], 0x3A300000u);
    rep("M nonassoc v0 aa", 0, g[5], 0);
    rep("M assoc wb", 0, g[6], 0x3A400000u);
    rep("M assoc aa", 0, g[7], 0);
    rep("M assoc miss aa", 0, g[8], tagv(x, 1, 1));
    rep("M assoc miss mem", 0, g[9], 0x3A400000u);
    rep("M assoc miss ocbp", 0, g[10], 0x3A500000u);
  }
  /* ---- N: OC RAM (ORA) */
  {
    x_flush_ccr(P1(0x0C500000u), CCR_KOS | CCR_ORA);
    rep("N ora ccr", 0, ccr_get() & 0xFFFFu, CCR_KOS | CCR_ORA);
    uint32_t sum = 0;
    for (uint32_t o = 0; o < 0x2000u; o += 4) R32(0x7C001000u + o) = 0x5A000000u ^ (o * 0x01000193u);
    for (uint32_t o = 0; o < 0x2000u; o += 64) {
      R8(0x7C001000u + o + 1) = (uint8_t)o;
      R16(0x7C001000u + o + 6) = (uint16_t)(o * 3u);
    }
    int bad = 0;
    for (uint32_t o = 0; o < 0x2000u; o += 4) {
      uint32_t w = 0x5A000000u ^ (o * 0x01000193u);
      if ((o & 63u) == 0) w = (w & 0xFFFF00FFu) | ((o & 0xFFu) << 8);
      if ((o & 63u) == 4) w = (w & 0x0000FFFFu) | (((o - 4u) * 3u & 0xFFFFu) << 16);
      uint32_t a = 0x7C001000u + o;
      uint32_t al = (o < 0x1000u) ? 0x7C000000u + o : 0x7C003000u + (o - 0x1000u);   /* hm 4.3.6: 4 KB aliases */
      uint32_t al2 = (o < 0x1000u) ? 0x7C004000u + o : 0x7C006000u + (o - 0x1000u);
      uint32_t g = R32(a), g2 = R32(al), g3 = R32(al2);
      sum = sum * 31u + g;
      if (g != w || g2 != w || g3 != w) { if (bad < 4) printf("CCRX N ora o=%lx got %08lx %08lx %08lx want %08lx\n", (unsigned long)o, (unsigned long)g, (unsigned long)g2, (unsigned long)g3, (unsigned long)w); bad++; }
      /* cached traffic in the other half */
      if ((o & 255u) == 0) { R32(P1(cl(11)) + (o >> 6)) = o; if (R32(P1(cl(11) + 0x2000u * (o >> 8 & 3u)))) {} }
    }
    {                                                    /* a quadword FMOV (FPSCR.SZ = 1) from and to the RAM */
      static uint32_t qd[2] __attribute__((aligned(8)));
      __asm__ __volatile__("fschg\n\tfmov @%1,dr0\n\tfmov dr0,@%0\n\tfschg" : : "r"(P1((uint32_t)(uintptr_t)qd)),
                           "r"(0x7C002008u) : "memory", "fr0", "fr1");
      rep("N ora quad lo", 0, qd[0], R32(0x7C002008u));
      rep("N ora quad hi", 0, qd[1], R32(0x7C00200Cu));
      qd[0] = 0x12345678u; qd[1] = 0x9ABCDEF0u;
      __asm__ __volatile__("fschg\n\tfmov @%1,dr0\n\tfmov dr0,@%0\n\tfschg" : : "r"(0x7C003010u),
                           "r"(P1((uint32_t)(uintptr_t)qd)) : "memory", "fr0", "fr1");
      rep("N ora quad st", 0, R32(0x7C002010u) ^ R32(0x7C002014u), 0x12345678u ^ 0x9ABCDEF0u);
      R32(0x7C001008u) = R32(0x7C002008u); R32(0x7C00100Cu) = R32(0x7C00200Cu);   /* keep the pattern check valid */
    }
    rep("N ora words bad", 0, (uint32_t)bad, 0);
    printf("CCRX N ora sum=%08lx\n", (unsigned long)sum);
    x_flush_ccr(P1(0x0C500000u), CCR_KOS | CCR_OCI);
    rep("N ora off ccr", 0, ccr_get() & 0xFFFFu, CCR_KOS);
    ocbp(P1(cl(11)));
    rep("N cached", 0, R32(P2(cl(11)) + 4), 256u);
  }
  printf("CCRX summary cases=%d bad=%d\n", ncase, nbad);
  printf("CCRX done\n");
  return nbad ? 1 : 0;
}
