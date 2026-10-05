/* tasc.c -- M5 (the M5 verification's program, agents/reports/integration/M5-verify.md F4; the M5 gate's regression
 * test since M5-fix.md: make v1-soc-m5-sub, mutant tas_clean_keep in v1-soc-mutants): TAS.B against the operand
 * cache's line states and the accesses in flight around it (SOC.md 8.3 M5 B2 "TAS.B"; pm TAS.B: "purges the cache
 * block ... If there is a cache hit and the corresponding cache block is dirty (U bit = 1), the contents of that cache
 * block are written back ... and the cache block is then invalidated. If there is a cache hit and the corresponding
 * cache block is clean (U bit = 0), the cache block is simply invalidated. A purge is not executed in the event of a
 * cache miss, or if the accessed memory location is non-cacheable").  boardx's S row covers one state (a dirty P1
 * line); these cover the rest, and TAS.B issued while a fill, a write-back or a posted write of the same line is still
 * in flight (a sweep of 0-11 NOPs between the two, so that TAS meets each stage).  Run on rm / rmx (the real memory;
 * on im C2 detects the data-less operand cache) with soc_tb --lockstep.
 * Built with the m1b run-time (IMASK 15 at main, CCR 0x090D: OC on, copy-back P1).
 *   C1  clean hit through P1: P1 read X (the line valid, clean, X = 0), TAS.B @X (P1): T = 1; the next P1 read of X
 *       misses and reads 0x80 (the line was invalidated); memory 0x80.
 *   C2  clean hit, TAS.B through P2 (non-cacheable: no purge): T = 1, memory 0x80, the P1 read still hits the stale 0
 *       (on IDEAL_MEM = 1, im, the OC holds no data: the P1 read sees memory, 0x80; the program detects that first).
 *   C3  dirty line, TAS.B on another byte of it through P1: X+1 = 0x22 in the cache only; after TAS memory holds X+1 =
 *       0x22 (written back) and X = 0x80; T = 1.
 *   C4  PREF @X then TAS.B @X after n NOPs (the fill in flight): T = 1, then the P1 read of X = 0x80, memory 0x80 (a fill
 *       that lands after the purge must not validate the pre-TAS line).
 *   C5  a load of another longword of X's line (a miss, the fill in flight; the value unused) then TAS.B @X after n
 *       NOPs: as C4.
 *   C6  a P2 store of 0 to X then TAS.B @X (P2) after n NOPs (the posted write in the write buffer): T = 1 (TAS's read
 *       sees the store), memory 0x80; and a P2 store of 0x80 then TAS: T = 0.
 *   C7  memory X = 0x55; a P1 store X = 0 (allocates the line dirty), OCBWB @X (the write-back in flight, the line clean
 *       and valid), TAS.B @X (P1) after n NOPs: TAS's read must see 0 (T = 1) -- the write-back reaches memory first --,
 *       then memory 0x80 and the P1 read 0x80 (the clean line invalidated by the purge).
 *   C8  as C7 with OCBP (the line invalidated, its write-back in flight): T = 1, memory and P1 0x80.
 * Output: "TC <case> n=<nops> T=<t> mem=<b> p1=<b> [x1=<b>] OK|FAIL" (one line per case and n), "TC done fails=<n>". */
#include "m1b.h"

#define PHYS(p) ((uint32_t)(uintptr_t)(p) & 0x1FFFFFFFu)
#define P2(p)   (PHYS(p) | 0xA0000000u)
#define P1(p)   (PHYS(p) | 0x80000000u)
#define NN 12

static volatile uint8_t buf[8][64] __attribute__((aligned(32)));   /* one 32-byte line per case run: buf[k][0] */
static int fails;

static int tas(uint32_t a) {
  int t;
  __asm__ __volatile__("tas.b @%1\n\tmovt %0" : "=r"(t) : "r"(a) : "t", "memory");
  return t;
}

/* PREF @a, n NOPs, TAS.B @a */
#define PREF_TAS(N) static int pref_tas_##N(uint32_t a) { int t; __asm__ __volatile__( \
  "pref @%1\n\t.rept " #N "\n\tnop\n\t.endr\n\ttas.b @%1\n\tmovt %0" : "=r"(t) : "r"(a) : "t", "memory"); return t; }
/* MOV.L @(8,a) (a miss, the value unused), n NOPs, TAS.B @a */
#define LD_TAS(N) static int ld_tas_##N(uint32_t a) { int t, d; __asm__ __volatile__( \
  "mov.l @(8,%2),%1\n\t.rept " #N "\n\tnop\n\t.endr\n\ttas.b @%2\n\tmovt %0" : "=&r"(t), "=&r"(d) : "r"(a) : "t", "memory"); \
  (void)d; return t; }
/* MOV.B v,@a (P2, posted), n NOPs, TAS.B @a */
#define ST_TAS(N) static int st_tas_##N(uint32_t a, uint32_t v) { int t; __asm__ __volatile__( \
  "mov.b %2,@%1\n\t.rept " #N "\n\tnop\n\t.endr\n\ttas.b @%1\n\tmovt %0" : "=r"(t) : "r"(a), "r"(v) : "t", "memory"); return t; }
/* MOV.B #0,@a (P1: allocate, dirty), OCBWB / OCBP @a, n NOPs, TAS.B @a */
#define WB_TAS(N) static int wb_tas_##N(uint32_t a) { int t; __asm__ __volatile__( \
  "mov #0,r0\n\tmov.b r0,@%1\n\tocbwb @%1\n\t.rept " #N "\n\tnop\n\t.endr\n\ttas.b @%1\n\tmovt %0" : "=r"(t) : "r"(a) : "r0", "t", "memory"); \
  return t; }
#define PB_TAS(N) static int pb_tas_##N(uint32_t a) { int t; __asm__ __volatile__( \
  "mov #0,r0\n\tmov.b r0,@%1\n\tocbp @%1\n\t.rept " #N "\n\tnop\n\t.endr\n\ttas.b @%1\n\tmovt %0" : "=r"(t) : "r"(a) : "r0", "t", "memory"); \
  return t; }
#define ALL(M) M(0) M(1) M(2) M(3) M(4) M(5) M(6) M(7) M(8) M(9) M(10) M(11)
ALL(PREF_TAS) ALL(LD_TAS) ALL(ST_TAS) ALL(WB_TAS) ALL(PB_TAS)
#define TBL(p) { p##_0, p##_1, p##_2, p##_3, p##_4, p##_5, p##_6, p##_7, p##_8, p##_9, p##_10, p##_11 }
static int (*const t_pref[NN])(uint32_t) = TBL(pref_tas);
static int (*const t_ld[NN])(uint32_t) = TBL(ld_tas);
static int (*const t_st[NN])(uint32_t, uint32_t) = TBL(st_tas);
static int (*const t_wb[NN])(uint32_t) = TBL(wb_tas);
static int (*const t_pb[NN])(uint32_t) = TBL(pb_tas);

/* the line of buf[k] in memory: every byte 0x40 + i, X = buf[k][0] = x0; no copy in the operand cache */
static volatile uint8_t *fresh(int k, uint8_t x0) {
  volatile uint8_t *b = buf[k];
  ocbp_range(b, 64);
  for (int i = 0; i < 64; i++) R8(P2(&b[i])) = (uint8_t)(0x40 + i);
  R8(P2(&b[0])) = x0;
  (void)R8(P2(&b[0]));                                     /* the posted writes done */
  return b;
}

static void row(const char *c, int n, int t, int wt, uint8_t mem, uint8_t wm, uint8_t p1, uint8_t wp, int x1, int wx) {
  const int ok = t == wt && mem == wm && p1 == wp && x1 == wx;
  if (x1 >= 0)
    printf("TC %s n=%d T=%d mem=%02x p1=%02x x1=%02x %s\n", c, n, t, mem, p1, x1, ok ? "OK" : "FAIL");
  else
    printf("TC %s n=%d T=%d mem=%02x p1=%02x %s\n", c, n, t, mem, p1, ok ? "OK" : "FAIL");
  if (!ok) fails++;
}

int main(void) {
  imask(15);
  printf("TC start buf=%08lx\n", (unsigned long)PHYS(buf));
  { /* C1 */
    volatile uint8_t *b = fresh(0, 0);
    (void)R8(P1(b));                                       /* the line valid and clean */
    const int t = tas(P1(b));
    const uint8_t p1 = R8(P1(b)), mem = R8(P2(b));
    row("C1 clean-p1", 0, t, 1, mem, 0x80, p1, 0x80, -1, -1);
  }
  { /* C2 (IDEAL_MEM = 1, im: the operand cache holds no data, a P1 read sees memory -- the stale value exists only on
     * the real-memory builds; probed first with a plain P2 store under a cached line) */
    volatile uint8_t *b = fresh(1, 0);
    (void)R8(P1(b));
    R8(P2(b)) = 0x33; (void)R8(P2(b));
    const int ideal = R8(P1(b)) == 0x33;
    b = fresh(1, 0);
    (void)R8(P1(b));
    const int t = tas(P2(b));
    const uint8_t mem = R8(P2(b)), p1 = R8(P1(b));
    row(ideal ? "C2 clean-p2 (ideal mem)" : "C2 clean-p2", 0, t, 1, mem, 0x80, p1, ideal ? 0x80 : 0x00, -1, -1);
    ocbp_range(b, 32);
  }
  { /* C3 */
    volatile uint8_t *b = fresh(2, 0);
    R8(P1(&b[1])) = 0x22;                                  /* allocate, dirty */
    const int t = tas(P1(b));
    const uint8_t mem = R8(P2(b)), x1 = R8(P2(&b[1]));
    const uint8_t p1 = R8(P1(b));
    row("C3 dirty-nb", 0, t, 1, mem, 0x80, p1, 0x80, x1, 0x22);
  }
  for (int n = 0; n < NN; n++) { /* C4 */
    volatile uint8_t *b = fresh(3, 0);
    const int t = t_pref[n](P1(b));
    const uint8_t p1 = R8(P1(b)), mem = R8(P2(b));
    row("C4 pref", n, t, 1, mem, 0x80, p1, 0x80, -1, -1);
  }
  for (int n = 0; n < NN; n++) { /* C5 */
    volatile uint8_t *b = fresh(4, 0);
    const int t = t_ld[n](P1(b));
    const uint8_t p1 = R8(P1(b)), mem = R8(P2(b));
    row("C5 ld-miss", n, t, 1, mem, 0x80, p1, 0x80, -1, -1);
  }
  for (int n = 0; n < NN; n++) { /* C6 */
    volatile uint8_t *b = fresh(5, 0x55);
    const int t0 = t_st[n](P2(b), 0);
    const uint8_t m0 = R8(P2(b));
    row("C6 st0", n, t0, 1, m0, 0x80, m0, 0x80, -1, -1);
    const int t1 = t_st[n](P2(b), 0x80);
    const uint8_t m1 = R8(P2(b));
    row("C6 st80", n, t1, 0, m1, 0x80, m1, 0x80, -1, -1);
  }
  for (int n = 0; n < NN; n++) { /* C7 */
    volatile uint8_t *b = fresh(6, 0x55);
    const int t = t_wb[n](P1(b));
    const uint8_t mem = R8(P2(b)), p1 = R8(P1(b));
    row("C7 ocbwb", n, t, 1, mem, 0x80, p1, 0x80, -1, -1);
  }
  for (int n = 0; n < NN; n++) { /* C8 */
    volatile uint8_t *b = fresh(7, 0x55);
    const int t = t_pb[n](P1(b));
    const uint8_t mem = R8(P2(b)), p1 = R8(P1(b));
    row("C8 ocbp", n, t, 1, mem, 0x80, p1, 0x80, -1, -1);
  }
  printf("TC done fails=%d\n", fails);
  return fails ? 1 : 0;
}
