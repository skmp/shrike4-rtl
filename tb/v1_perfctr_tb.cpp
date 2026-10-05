// v1_perfctr_tb -- unit testbench of rtl/v1/v1_perfctr.sv (PMCR1 / PMCR2, PMCTR1 / PMCTR2) through
// tb/v1_perfctr_tb_top.sv.  The C++ side plays the P4 bridge (Iclk read strobe in MA1, write strobe in MA3), the core's
// event sources, the CPG's ratio_inc and the reset controller.  The RTL runs in lock step with the independent C++
// model dbg::Pmc (tb/sh4_dbg.h, cycle mode): every read strobe's rdata / hit is compared, in every section.
//
// Sections (each prints its counts; any mismatch fails the run, exit 1):
//   regs     p4probe P / ubcbrk A register behaviour: the 0xC13F read-back mask bit by bit, the size rule (32- / 8-bit
//            PMCR accesses read 0, writes ignored), the CCN mirror write, PMCTR read only, the a[4:2] decode (000 reads
//            0, 1xx = PMCTR2L), the size-independent PMCTR reads (16-bit at +0 / +2, 8-bit at +0 / +3) with
//            PMCTR1H = 1 (the counter is run past 2^32 in ratio mode first, as ubcbrk A did)
//   control  p4probe P "run" / "cont" / "clr-running": both PMEN and PMST needed, a stopped counter holds and continues,
//            PMCLR clears only a stopped counter (E023 while running does not; 2023 from running stops, no clear), KOS's
//            stop / clear / start; ratio mode adds ratio_inc per event (x12 at I:B = 2 [HW 3600864 / 300072]); the
//            counters hold while run = 0 (SLEEP); a power-on reset clears, a manual / CPU-only reset holds
//   timing   the SOC.md 3.7 port-level target: "PMCR2 <- C023; 4 x PMCTR2L -> 0 3 6 9; PMCR2 <- 0023; 4 x -> 21 21 21
//            21" [HW ubcevt P] and the 18 values of SOC.md App. A.3 (events 0x13 / 0x14 / 0x23 between two PMCTR1L
//            reads, c1 run 1 section S), with the MA1 / MA3 / event schedules from tb/v1_dbg_sched.h (v1's issue rules)
//   random   N cycles (default 2M) of random strobes (every offset class, size, mirror), events, run, ratio_inc and
//            resets against the model
// Options: --n <random cycles>, --seed <s>.
#include "Vv1_perfctr_tb_top.h"
#include "verilated.h"
#include "sh4_dbg.h"
#include "v1_dbg_sched.h"
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static Vv1_perfctr_tb_top *d;
static dbg::Pmc M;
static long g_fail = 0, g_chk = 0;
static uint64_t g_cyc = 0;

static uint64_t g_rs = 0x243F6A8885A308D3ull;
static inline uint64_t rnd64() {
  uint64_t z = (g_rs += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}
static inline uint32_t rndn(uint32_t n) { return (uint32_t)((rnd64() >> 32) * n >> 32); }

static void fail(const char *fmt, const char *what, uint64_t got, uint64_t exp) {
  g_fail++;
  if (g_fail <= 30) { printf("FAIL cyc %llu %s: ", (unsigned long long)g_cyc, what); printf(fmt, got, exp); printf("\n"); }
}
static void chk(const char *what, uint64_t got, uint64_t exp) {
  g_chk++;
  if (got != exp) fail("got %llx expected %llx", what, got, exp);
}

// ---------------------------------------------------------------------------------------------------- one Icyc
// offsets are a[27:0]: PMCR1 0xF000084, PMCR2 0xF000088, PMCTR 0xF100000 + x
enum { P_NONE, P_CCN, P_CTR };
struct Cyc {
  bool run = true;
  int ratio = 12;
  uint64_t ev = 0;
  int rst = 0;              // {pin, por, man, cpu}
  int port = P_NONE;
  bool wr = false;
  int size = 2;
  uint32_t off = 0, wdata = 0;
};
struct Rd { uint32_t v; bool hit; };

static Rd cycle(const Cyc &c) {
  d->clk = 0;
  d->rst_i = c.rst; d->run = c.run; d->ratio_inc = c.ratio; d->ev = c.ev;
  d->c_rd = c.port == P_CCN && !c.wr; d->c_wr = c.port == P_CCN && c.wr;
  d->t_rd = c.port == P_CTR && !c.wr; d->t_wr = c.port == P_CTR && c.wr;
  d->c_size = d->t_size = c.size;
  d->c_off = d->t_off = c.off;
  d->c_wdata = d->t_wdata = c.wdata;
  d->eval();
  Rd r{0, false};
  chk("stall", d->c_stall | d->t_stall, 0);
  if (c.port == P_NONE) { chk("idle rdata", d->c_rdata | d->t_rdata, 0); chk("idle hit", d->c_hit | d->t_hit, 0); }
  if (c.port != P_NONE && !c.rst) {
    r.v = c.port == P_CCN ? d->c_rdata : d->t_rdata;
    r.hit = c.port == P_CCN ? d->c_hit : d->t_hit;
    uint32_t mv = 0;
    bool mh = c.wr ? M.wr(c.off, c.size, c.wdata, false) : M.rd(c.off, c.size, mv, false);
    if (!c.wr) chk("rdata vs model", r.v, mv);
    chk("hit vs model", r.hit, mh);
  }
  d->clk = 1; d->eval();
  d->clk = 0; d->eval();
  if (c.rst & 4) M.reset_por();
  else {
    M.clock(c.ev, c.run, c.ratio);
    if (c.rst) M.reset_other();
  }
  g_cyc++;
  return r;
}
static void idle(int k, uint64_t ev = 1ull << 0x23) { Cyc c; c.ev = ev; for (int i = 0; i < k; i++) cycle(c); }
static uint32_t rd(int port, uint32_t off, int size) {
  Cyc c; c.ev = 1ull << 0x23; c.port = port; c.off = off; c.size = size; return cycle(c).v;
}
static void wr(int port, uint32_t off, int size, uint32_t v) {
  Cyc c; c.ev = 1ull << 0x23; c.port = port; c.off = off; c.size = size; c.wdata = v; c.wr = true; cycle(c);
}
static const uint32_t PMCR1 = 0xF000084, PMCR2 = 0xF000088, CTR = 0xF100000;
static void pmcr(int n, uint32_t v) { wr(P_CCN, n ? PMCR2 : PMCR1, 1, v); }
static uint32_t pmctr_l(int n) { return rd(P_CTR, CTR + (n ? 0x10 : 0x08), 2); }
static uint32_t pmctr_h(int n) { return rd(P_CTR, CTR + (n ? 0x0C : 0x04), 2); }
static void reset(int kind) { Cyc c; c.rst = kind; c.ev = 0; for (int i = 0; i < 8; i++) cycle(c); }

// ---------------------------------------------------------------------------------------------------- sections
static void sec_regs() {
  long f0 = g_fail, c0 = g_chk;
  reset(0xF);
  // p4probe P readback PMCR2: 0000 0001 003f 0040 0080 0100 0200 0400 0800 1000 2000 4000 8000 c000 ffff 0000
  static const uint32_t w[] = {0x0000, 0x0001, 0x003f, 0x0040, 0x0080, 0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000,
                               0x4000, 0x8000, 0xc000, 0xffff, 0x0000};
  static const uint32_t e[] = {0x0000, 0x0001, 0x003f, 0x0000, 0x0000, 0x0100, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
                               0x4000, 0x8000, 0xc000, 0xc13f, 0x0000};
  for (int i = 0; i < 16; i++) { pmcr(1, w[i]); chk("PMCR2 read-back", rd(P_CCN, PMCR2, 1), e[i]); }
  // sizes: "PMCR2 r32 00000000"; ubcbrk A "PMCR2 w16@+100 0015 -> 0015, w32 00000016 -> 0015, w8 17 -> 0015"
  pmcr(1, 0x0023);
  chk("PMCR2 r32", rd(P_CCN, PMCR2, 2), 0);
  chk("PMCR2 r8", rd(P_CCN, PMCR2, 0), 0);
  pmcr(1, 0);
  wr(P_CCN, PMCR2 + 0x100, 1, 0x0015); chk("PMCR2 mirror write", rd(P_CCN, PMCR2, 1), 0x0015);
  chk("PMCR2 mirror read", rd(P_CCN, PMCR2 + 0x2000, 1), 0x0015);
  wr(P_CCN, PMCR2, 2, 0x16); chk("PMCR2 w32 ignored", rd(P_CCN, PMCR2, 1), 0x0015);
  wr(P_CCN, PMCR2, 0, 0x17); chk("PMCR2 w8 ignored", rd(P_CCN, PMCR2, 1), 0x0015);
  chk("PMCR1 independent", rd(P_CCN, PMCR1, 1), 0);
  // counters: PMCTR1 past 2^32 (ratio mode, ratio_inc 31, event 0x23), PMCTR2 small (event 0x23)
  pmcr(0, 0x0123); pmcr(0, 0x2123); pmcr(1, 0x0023); pmcr(1, 0x2023);
  pmcr(1, 0xC023); idle(1000); pmcr(1, 0x0023);
  pmcr(0, 0xC123);
  {                                                              // 2^32 / 31 + a bit: a lean loop, model in step
    d->rst_i = 0; d->run = 1; d->ratio_inc = 31; d->ev = 1ull << 0x23;
    d->c_rd = d->c_wr = d->t_rd = d->t_wr = 0;
    for (uint32_t i = 0; i < 139000000u; i++) {
      d->clk = 1; d->eval(); d->clk = 0; d->eval();
      M.clock(1ull << 0x23, true, 31);
    }
    g_cyc += 139000000u;
  }
  pmcr(0, 0x0123);                                               // stop: hold
  uint32_t h1 = pmctr_h(0), l1 = pmctr_l(0), h2 = pmctr_h(1), l2 = pmctr_l(1);
  chk("PMCTR1H = 1", h1, 1);
  chk("PMCTR2H = 0", h2, 0);
  // "A PMCTR stopped, ff100000..3c: 0 H1 L1 H2 L2 L2 L2 L2 0 H1 L1 H2 L2 L2 L2 L2"
  const uint32_t pat[8] = {0, h1, l1, h2, l2, l2, l2, l2};
  for (int k = 0; k < 16; k++) chk("PMCTR a[4:2] decode", rd(P_CTR, CTR + 4 * k, 2), pat[k & 7]);
  // "PMCTR1H r16 0001 r16+2 0001 r8 01 r8+3 00 | ff100100 00000000 ff101004 00000001 ff120004 00000001"
  chk("PMCTR1H r16", rd(P_CTR, CTR + 4, 1), 1);
  chk("PMCTR1H r16+2", rd(P_CTR, CTR + 6, 1), 1);
  chk("PMCTR1H r8", rd(P_CTR, CTR + 4, 0), 1);
  chk("PMCTR1H r8+3", rd(P_CTR, CTR + 7, 0), 0);
  chk("ff100100", rd(P_CTR, CTR + 0x100, 2), 0);
  chk("ff101004", rd(P_CTR, CTR + 0x1004, 2), 1);
  chk("ff120004", rd(P_CTR, CTR + 0x20004, 2), 1);
  // "PMCTR2L r16+0 / r16+2": both bits 15:0; bytes
  chk("PMCTR2L r16", rd(P_CTR, CTR + 0x10, 1), l2 & 0xFFFF);
  chk("PMCTR2L r16+2", rd(P_CTR, CTR + 0x12, 1), l2 & 0xFFFF);
  chk("PMCTR1L r8+1 [P]", rd(P_CTR, CTR + 0x09, 0), (l1 >> 8) & 0xFF);
  // hole 000 -> hit 0; PMCTR is read only: "ctr-write ... L 00000000" (written while disabled and running)
  { Cyc c; c.port = P_CTR; c.off = CTR; c.size = 2; Rd r = cycle(c); chk("PMCTR 000 hit", r.hit, 0); }
  wr(P_CTR, CTR + 0x08, 2, 0x12345678); wr(P_CTR, CTR + 0x04, 2, 0xFFFF);
  chk("PMCTR1L read only", pmctr_l(0), l1);
  chk("PMCTR1H read only", pmctr_h(0), 1);
  printf("regs: %ld checks, %ld failures\n", g_chk - c0, g_fail - f0);
}

static void sec_control() {
  long f0 = g_fail, c0 = g_chk;
  reset(0xF);
  // "P run 0023 / 4023 / 8023: counts nothing"; "c023 counts"
  for (uint32_t v : {0x0023u, 0x4023u, 0x8023u}) {
    pmcr(1, v & 0x3FFF); pmcr(1, (v & 0x3FFF) | 0x2000);          // stop, clear
    pmcr(1, v); idle(3000);
    chk("one of PMEN / PMST: no count", pmctr_l(1), 0);
  }
  pmcr(1, 0x0023); pmcr(1, 0x2023); pmcr(1, 0xC023); idle(3000);
  chk("C023 counts from the cycle after its strobe", pmctr_l(1), 3000);
  // "cont": disabled holds, after a spin still holds, re-enabled continues from the value
  pmcr(1, 0x0023); uint32_t h = pmctr_l(1); idle(1000);
  chk("stopped counter holds", pmctr_l(1), h);
  pmcr(1, 0xC023); idle(6);
  chk("re-enabled continues", pmctr_l(1) > h, 1);
  // PMST-only / PMEN-only stop it too
  pmcr(1, 0x4023); h = pmctr_l(1); idle(100); chk("PMST-only stops", pmctr_l(1), h);
  pmcr(1, 0x8023); idle(100); chk("PMEN-only stops", pmctr_l(1), h);
  // "clr-running": E023 while running does not clear; 2023 from running stops without clearing
  pmcr(1, 0xC023); idle(200);
  uint32_t b = pmctr_l(1);
  pmcr(1, 0xE023); uint32_t b2 = pmctr_l(1);
  chk("E023 while running: no clear", b2 > b, 1);
  chk("E023 reads back C023", rd(P_CCN, PMCR2, 1), 0xC023);
  pmcr(1, 0x2023); uint32_t b3 = pmctr_l(1); idle(100);
  chk("2023 from running: stopped, not cleared", b3 > b2 && pmctr_l(1) == b3, 1);
  pmcr(1, 0xC023); idle(10); chk("restart continues", pmctr_l(1) > b3, 1);
  // KOS perf_cntr_timer_enable: PMCR &= ~0xC000; |= 0x2000 (clears: stopped now); = 0xC023
  pmcr(1, 0x0023); pmcr(1, 0x2023);
  chk("stop then clear clears", pmctr_l(1), 0);
  // exact start: the first read after a write whose strobe is cycle w reads at w + 1 -> 0, then +1 per cycle
  pmcr(1, 0xC023);
  { uint32_t r0 = pmctr_l(1), r1 = pmctr_l(1); idle(1); uint32_t r2 = pmctr_l(1);
    chk("first read after start", r0, 0); chk("next cycle", r1, 1); chk("two later", r2, 3); }
  // ratio mode: PMCR1 C123 (ratio) and PMCR2 C023 (cycles) over the same window: d1 = 12 x d2 [HW]
  pmcr(0, 0x0123); pmcr(0, 0x2123); pmcr(1, 0x0023); pmcr(1, 0x2023);
  pmcr(0, 0xC123); pmcr(1, 0xC023); idle(300072); pmcr(0, 0x0123); pmcr(1, 0x0023);
  uint32_t r1 = pmctr_l(0), r2 = pmctr_l(1);
  chk("ratio mode = 12 x cycles", r1, 12ull * r2);           // both run for the same number of cycles
  // run = 0 (SLEEP): nothing advances, events of those cycles are lost
  pmcr(1, 0xC023); idle(10);
  { Cyc c; c.run = false; c.ev = 1ull << 0x23; uint32_t s0 = pmctr_l(1);
    for (int i = 0; i < 500; i++) cycle(c);
    Cyc r = c; r.port = P_CTR; r.off = CTR + 0x10; r.size = 2; chk("asleep: holds", cycle(r).v, s0 + 1); }
  // an event that never fires counts nothing; PMM selects exactly its bit
  pmcr(0, 0x0015); pmcr(0, 0x2015); pmcr(0, 0xC015);
  { Cyc c; c.ev = ~(1ull << 0x15); for (int i = 0; i < 100; i++) cycle(c); }
  chk("PMM selects its bit", pmctr_l(0), 0);
  { Cyc c; c.ev = 1ull << 0x15; for (int i = 0; i < 100; i++) cycle(c); }
  chk("PMM counts its bit", pmctr_l(0), 98);                  // two cycles still in ev_q / ev_qq
  // resets: manual / CPU-only hold the registers and counters, power-on clears them
  uint32_t k1 = rd(P_CCN, PMCR1, 1);
  pmcr(1, 0x0023); uint32_t k2 = pmctr_l(1);
  reset(0x3);                                                    // manual (+cpu)
  chk("manual reset holds PMCR1", rd(P_CCN, PMCR1, 1), k1);
  chk("manual reset holds PMCTR2", pmctr_l(1), k2);
  reset(0x1);                                                    // CPU-only
  chk("CPU-only reset holds PMCTR2", pmctr_l(1), k2);
  reset(0x5);                                                    // WDT / H-UDI power-on (+cpu)
  chk("power-on reset clears PMCR1", rd(P_CCN, PMCR1, 1), 0);
  chk("power-on reset clears PMCTR2", pmctr_l(1), 0);
  printf("control: %ld checks, %ld failures\n", g_chk - c0, g_fail - f0);
}

// ---- the port-level timing target, from v1's issue rules (tb/v1_dbg_sched.h)
static void run_sched(const dsched::Sched &s, const std::vector<std::pair<int, std::pair<int, uint32_t>>> &acts,
                      std::vector<uint32_t> &reads, bool events) {
  // acts: (cycle, (port or -1 = write PMCR2 / -2 = write PMCR1, offset or data))
  for (int t = 0; t < s.cycles; t++) {
    Cyc c;
    c.ev = 1ull << 0x23;
    if (events) c.ev |= ((uint64_t)s.ev13[t] << 0x13) | ((uint64_t)s.ev14[t] << 0x14);
    for (auto &a : acts)
      if (a.first == t) {
        if (a.second.first < 0) { c.port = P_CCN; c.wr = true; c.size = 1; c.off = a.second.first == -1 ? PMCR2 : PMCR1;
                                   c.wdata = a.second.second; }
        else { c.port = P_CTR; c.size = 2; c.off = a.second.second; }
      }
    Rd r = cycle(c);
    if (c.port == P_CTR) reads.push_back(r.v);
  }
}

static void sec_timing() {
  using namespace dsched;
  long f0 = g_fail, c0 = g_chk;
  reset(0xF);
  // ---- ubcevt P: pm_lat(out, &PMCR2, &PMCTR2L) after PMCR2 = 0023, 2023
  //   mov.w .Lpl_on,r0 ; mov.w r0,@r5 ; 4 x mov.l @r6,rX ; 4 x mov.l rX,@(d,r4) ; mov.w .Lpl_off,r0 ; mov.w r0,@r5 ; ...
  {
    pmcr(1, 0x0023); pmcr(1, 0x2023); idle(5);
    std::vector<Ins> p = {ldpc(0), st(0, 5, true), ld(6, 1, true), ld(6, 2, true), ld(6, 3, true), ld(6, 7, true),
                          st(1, 4), st(2, 4), st(3, 4), st(7, 4), ldpc(0), st(0, 5, true), ld(6, 1, true),
                          ld(6, 2, true), ld(6, 3, true), ld(6, 7, true), st(1, 4), st(2, 4), st(3, 4), st(7, 4)};
    Sched s = run(p);
    std::vector<std::pair<int, std::pair<int, uint32_t>>> acts;
    acts.push_back({s.ma_last[1], {-1, 0xC023}});               // Iclk write: strobe in MA3
    acts.push_back({s.ma_last[11], {-1, 0x0023}});
    for (int i : {2, 3, 4, 5, 12, 13, 14, 15}) acts.push_back({s.ma1[i], {P_CTR, CTR + 0x10}});   // read: MA1
    std::vector<uint32_t> r;
    run_sched(s, acts, r, false);
    static const uint32_t exp[8] = {0, 3, 6, 9, 21, 21, 21, 21};
    printf("timing: pm_lat reads");
    for (int i = 0; i < 8; i++) { printf(" %u", r[i]); chk("pm_lat [HW ubcevt P]", r[i], exp[i]); }
    printf("  (console: 0 3 6 9 | 21 21 21 21)\n");
  }
  // ---- SOC.md App. A.3: A1 ; ... ; B1 (PMCTR1L reads), PRFC0 = PMCR1 = event 0x13 / 0x14 / 0x23
  {
    const int R13 = 13, R9 = 9;
    struct Seq { const char *n; std::vector<Ins> p; int e13, e14, e23; };
    std::vector<Seq> seqs = {
      {"A ; B", {ld(R13, 10, true), ld(R13, 11, true), nop(), nop(), nop(), nop()}, 2, 1, 3},
      {"A ; L ; L ; B ; L ; L", {ld(R13, 10, true), ld(R9, 1), ld(R9, 2), ld(R13, 11, true), ld(R9, 3), ld(R9, 4),
                                 nop(), nop()}, 3, 0, 5},
      {"A ; L ; L ; B ; MOV(B) ; L", {ld(R13, 10, true), ld(R9, 1), ld(R9, 2), ld(R13, 11, true), mov(11, 3),
                                      ld(R9, 4), nop(), nop()}, 3, 0, 5},
      {"A ; MOV(A) ; L ; B ; L ; L", {ld(R13, 10, true), mov(10, 1), ld(R9, 2), ld(R13, 11, true), ld(R9, 3),
                                      ld(R9, 4), nop(), nop()}, 2, 1, 5},
      {"A ; 3 NOP ; B", {ld(R13, 10, true), nop(), nop(), nop(), ld(R13, 11, true), nop(), nop()}, 2, 2, 4},
      {"A ; 4 ADD ; B", {ld(R13, 10, true), add(1), add(1), add(1), add(1), ld(R13, 11, true), nop(), nop()}, 3, 1, 5},
    };
    printf("timing: App. A.3        0x13 0x14 0x23   (console)\n");
    for (auto &q : seqs) {
      Sched s = run(q.p);
      int b = -1;
      for (int i = 1; i < (int)q.p.size(); i++) if (q.p[i].p4) { b = i; break; }
      int got[3];
      int evc[3] = {0x13, 0x14, 0x23};
      for (int k = 0; k < 3; k++) {
        pmcr(0, evc[k]); pmcr(0, 0x2000 | evc[k]); pmcr(0, 0xC000 | evc[k]); idle(4);
        std::vector<std::pair<int, std::pair<int, uint32_t>>> acts = {{s.ma1[0], {P_CTR, CTR + 0x08}},
                                                                       {s.ma1[b], {P_CTR, CTR + 0x08}}};
        std::vector<uint32_t> r;
        run_sched(s, acts, r, true);
        got[k] = (int)(r[1] - r[0]);
      }
      printf("timing:   %-26s %4d %4d %4d   (%d %d %d)\n", q.n, got[0], got[1], got[2], q.e13, q.e14, q.e23);
      chk("A.3 0x13", got[0], q.e13); chk("A.3 0x14", got[1], q.e14); chk("A.3 0x23", got[2], q.e23);
    }
  }
  printf("timing: %ld checks, %ld failures\n", g_chk - c0, g_fail - f0);
}

static void sec_random(long n) {
  long f0 = g_fail, c0 = g_chk;
  reset(0xF);
  static const int hot[] = {0x23, 0x13, 0x14, 0x06, 0x25, 0x00, 0x3F};
  for (long i = 0; i < n; i++) {
    Cyc c;
    c.run = rndn(16) != 0;
    c.ratio = rndn(4) ? 12 : (int)rndn(32);
    c.ev = 0;
    for (int k = 0; k < 7; k++) if (rndn(3) == 0) c.ev |= 1ull << hot[k];
    if (rndn(4) == 0) c.ev |= rnd64();
    if (rndn(200000) == 0) {                                     // a reset now and then (8 cycles, no strobes)
      int kind = rndn(3) == 0 ? 0x5 : rndn(2) ? 0x3 : 0x1;
      for (int k = 0; k < 8; k++) { Cyc r = c; r.rst = kind; cycle(r); }
      continue;
    }
    switch (rndn(8)) {
    case 0: {                                                    // PMCR write: mostly 16-bit, command-like values
      c.port = P_CCN; c.wr = true; c.size = rndn(8) ? 1 : rndn(3);
      c.off = (rndn(2) ? PMCR1 : PMCR2) + (rndn(4) ? 0 : (rndn(1024) << 8));
      static const uint32_t cmd[] = {0x0000, 0x2000, 0xC000, 0xE000, 0x4000, 0x8000, 0x0100, 0xC100};
      c.wdata = rndn(4) ? (cmd[rndn(8)] | hot[rndn(7)]) : (uint32_t)rnd64();
      break;
    }
    case 1:                                                      // PMCR read
      c.port = P_CCN; c.size = rndn(8) ? 1 : rndn(3); c.off = (rndn(2) ? PMCR1 : PMCR2) + (rndn(1024) << 8);
      break;
    case 2: case 3:                                              // PMCTR read / write, any offset and size
      c.port = P_CTR; c.wr = rndn(8) == 0; c.size = rndn(3);
      c.off = CTR + (rndn(4) ? 4 * rndn(16) : (rndn(0x10000) << 2)) + (c.size == 0 ? rndn(4) : c.size == 1 ? 2 * rndn(2) : 0);
      c.off &= 0x3FFFF | 0xF100000;
      c.wdata = (uint32_t)rnd64();
      break;
    default: break;
    }
    cycle(c);
  }
  // final state through reads
  for (uint32_t o = 0; o < 0x20; o += 4) rd(P_CTR, CTR + o, 2);
  rd(P_CCN, PMCR1, 1); rd(P_CCN, PMCR2, 1);
  printf("random: %ld cycles, %ld checks, %ld failures\n", n, g_chk - c0, g_fail - f0);
}

int main(int argc, char **argv) {
  long n = 2000000;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--n") && i + 1 < argc) n = atol(argv[++i]);
    else if (!strcmp(argv[i], "--seed") && i + 1 < argc) g_rs = strtoull(argv[++i], 0, 0);
  }
  Verilated::commandArgs(argc, argv);
  d = new Vv1_perfctr_tb_top;
  M.cycle_mode = true;
  sec_regs();
  sec_control();
  sec_timing();
  sec_random(n);
  printf("v1-perfctr: %ld checks, %ld failures\n", g_chk, g_fail);
  printf("v1-perfctr: %s\n", g_fail ? "FAIL" : "PASS");
  delete d;
  return g_fail ? 1 : 0;
}
