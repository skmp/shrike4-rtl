// v1_cpg_tb -- unit test of rtl/v1/v1_cpg.sv inside the WI-CLK clock subsystem (v1_rstc + v1_cpg + v1_wdt + v1_tmu +
// v1_rtc wired as v1_soc wires them; spec/timers-clock.md 8.1 v1_cpg_tb; SOC.md 2).
//
// Every Icyc every observed output (reset levels and code, bce / pce, div, ratio_inc, cpu_run, dmac_run, stby, mstp,
// STATUS, the WDT settle handshake and reset requests, the TMU / RTC lines, and each strobe's response) is compared
// with the composite model sh4t::Sh4Timers.  Directed checks, independent of the model:
//   * pin power-on reset: FRQCR from MD2-0 for all 8 modes (DC: 0x0E0A), STBCR = STBCR2 = 0; ph = 0 in the first cycle
//     after the reset (bce at +1, +3, ..., pce at +3, +7, ...);
//   * the enables for every FRQCR of hm Table 10.4 (bce / pce periods = the B / P dividers, pce implies bce, aligned
//     edges) and ratio_inc = 24 / Bdiv (12 at 0x0E0A; IFC is treated as x1 like the enables, SOC.md 9.18: 6 at 0x0C5A);
//   * STBCR / STBCR2 storage and masks, mstp = STBCR[4:0], FRQCR bits 15:12 read 0, sizes;
//   * sleep (SOC.md 2.5, M1b): cpu_run falls the cycle after sleep_req rises (the core halts), the CPU clock (cpu_clk)
//     runs on until the 9th pce edge after the request (the clock-stop handshake, ENTRY), then SLEEP (cpu_clk = 0,
//     STATUS = sleep); the enables and div keep running; a wake arriving in each of the 4 Pclk phases raises cpu_run
//     after the 4th pce edge (14 / 15 / 16 / 17 Icyc for a wake seen in ph 2 / 1 / 0 / 3; v1_intc's wake rises in ph 1
//     or 3 only) with cpu_clk = 0 until then; a wake during ENTRY resynchronises with cpu_clk = 1 throughout (the clock
//     never stopped); a sleep_req held high after the wake does not sleep again, a new rising edge does, and so does a
//     one-cycle pulse;
//   * deep sleep (dmac_run = 1 through ENTRY, 0 from DSLEEP until RUN), MSTP4 (dmac_run = 0);
//   * standby, entered with STBY = 1 and DSLP = 1 (STBY wins, hm 9.2.4): the enables and div stop, stby = 1, STATUS =
//     standby, TSTR cleared except TPSC 6; a plain `wake` does not end it; wake_stby -> the WDT counts from WTCNT (16
//     counts from 0xF0 at CKS 0) with no flag, then the 4-pce resynchroniser with STATUS normal (hm 9.5.2), then RUN;
//   * the clock stop waits for a Pclk write in flight (p4_wpend) to land and decides the mode with the STBCR it left
//     (standby), at the 9th pce or at the landing if that is later (SOC.md 2.5, M1a / M1b);
//   * PLL relock: PLL1EN 0 -> 1 with PLL2 on counts twice (the second from the value software set), with PLL2 off once;
//     a BFC change with PLL2 on once, with PLL2 off never; a one-cycle sleep_req in the cycle the relocking write lands
//     is taken when the settle ends;
//   * the WDT's resets through v1_rstc: RSTS = 0 -> a power-on reset (STBCR cleared, FRQCR / WTCNT / WTCSR held with
//     WOVF, the TMU initialised, the enable phase restarted); RSTS = 1 -> a manual reset (STBCR held); STATUS = reset.
// Then random traffic to the four register blocks with random power-down / wake / reset / pin events against the model.
//
//   tb [--seed S] [--cycles N]
#include "Vv1_cpg_tb.h"
#include "verilated.h"
#include "sh4_timers.h"
#include "v1_clk_tbutil.h"
#include <vector>

static Vv1_cpg_tb *d;
static sh4t::Sh4Timers T;
static Fails F("v1-cpg");
static uint64_t cyc = 0;

enum : uint32_t { FRQCR = 0xFFC00000, STBCR = 0xFFC00004, WTCNT = 0xFFC00008, WTCSR = 0xFFC0000C, STBCR2 = 0xFFC00010,
                  TSTR = 0xFFD80004, TCR0 = 0xFFD80010, TCOR0 = 0xFFD80008, TCNT0 = 0xFFD8000C, RCR2 = 0xFFC8003C };

// the last stepped cycle's outputs (sampled before its clock edge)
struct Obs { bool bce, pce, cpu_run, cpu_clk, dmac_run, stby, settle, sovf, rst_por, rst_man, rst_pin; uint32_t div, status,
             mstp, ratio; };
static Obs o;

struct Blk { int b; };                    // 23 CPG, 24 WDT, 25 RTC, 27 TMU, -1 none
static int blk_of(uint32_t a) {
  if ((a & 0xFFFC0000u) == 0xFFC00000u) return ((a & 0x3FFF0u) == 0 && (a & 8)) ? 24 : 23;
  if ((a & 0xFFFC0000u) == 0xFFC80000u) return 25;
  if ((a & 0xFFFC0000u) == 0xFFD80000u) return 27;
  return -1;
}

static sh4t::Resp step(const sh4t::Strobe *s = nullptr, uint32_t addr = 0) {
  d->reset_n = T.reset_n; d->sck2_i = T.sck2; d->hudi_rst = T.hudi; d->mhit_req = T.mhit; d->arr_mhit = T.arr_mhit;
  d->cpu_rst_req = T.cpu_rst_req; d->md = T.md; d->sleep_req = T.sleep_req; d->wake = T.wake; d->wake_stby = T.wake_stby;
  d->p4_wpend = T.p4_wpend;
  d->osc_tick = T.osc; d->tclk_in = T.tclk_in; d->dack_ic = T.dack_ic;
  d->pq_rd = s && s->rd; d->pq_wr = s && s->wr; d->pq_size = s ? s->size : 0; d->pq_addr = addr;
  d->pq_wdata = s ? s->wdata : 0;
  d->clk = 0; d->eval();
  T.eval();
  sh4t::Resp mr;
  if (s) {
    sh4t::Strobe ms = *s; ms.off = addr & 0x0FFFFFFF;
    switch (blk_of(addr)) {
      case 23: mr = T.cpg.strobe(ms); break;
      case 24: mr = T.wdt.strobe(ms); break;
      case 25: mr = T.rtc.strobe(ms); break;
      case 27: mr = T.tmu.strobe(ms); break;
      default: break;
    }
  }
  const auto &c = T.co;
  // compare from the first power-on reset on: the power-up state is undefined, and the reset controller's synchronisers
  // read 0 at time 0, so the chip starts under a manual-looking reset that initialises nothing in the CPG
  static bool armed = false, por_seen = false;
  armed = por_seen;                        // the cycle after the first power-on reset cycle: its edge initialised the state
  if (d->rst_por) por_seen = true;
  const bool ok = !armed ||
      d->rst_pin == T.rst.pin && d->rst_por == T.rst.por && d->rst_man == T.rst.man && d->rst_cpu == T.rst.cpu &&
      d->rst_code == T.rst_code && d->bce == c.ce.bce && d->pce == c.ce.pce && d->div == c.div &&
      d->ratio_inc == (uint32_t)c.ratio_inc && d->cpu_run == c.cpu_run && d->cpu_clk == c.cpu_clk &&
      d->dmac_run == c.dmac_run && d->stby == c.stby &&
      d->mstp == c.mstp && d->status == (uint32_t)c.status && d->wdt_settle == c.wdt_settle &&
      d->settle_ovf == T.wo.settle_ovf && d->iti == T.wo.iti && d->wdt_rst_por == T.wo.rst_por &&
      d->wdt_rst_man == T.wo.rst_man && d->tuni == T.to.tuni && d->ticpi2 == T.to.ticpi2 && d->dreq_ic == T.to.dreq_ic &&
      d->tclk_out == T.to.tclk_out && d->tclk_oe == T.to.tclk_oe && d->next_evt == T.to.next_evt &&
      d->rtc_tick == T.ro.rtc_tick && d->ati == T.ro.ati && d->pri == T.ro.pri && d->cui == T.ro.cui;
  F.ok(ok, "cyc %lu outputs differ: rtl rst %d%d%d%d/%d ce %d%d div %03x ri %u run %d clk %d dm %d sb %d mstp %02x st %u "
       "set %d sovf %d tuni %x next %u | model rst %d%d%d%d/%d ce %d%d div %03x ri %d run %d clk %d dm %d sb %d mstp %02x "
       "st %d set %d sovf %d tuni %x next %u", (unsigned long)cyc, d->rst_pin, d->rst_por, d->rst_man, d->rst_cpu,
       d->rst_code, d->bce, d->pce, d->div, d->ratio_inc, d->cpu_run, d->cpu_clk, d->dmac_run, d->stby, d->mstp, d->status,
       d->wdt_settle, d->settle_ovf, d->tuni, d->next_evt, T.rst.pin, T.rst.por, T.rst.man, T.rst.cpu, T.rst_code, c.ce.bce,
       c.ce.pce, c.div, c.ratio_inc, c.cpu_run, c.cpu_clk, c.dmac_run, c.stby, c.mstp, c.status, c.wdt_settle,
       T.wo.settle_ovf, T.to.tuni, T.to.next_evt);
  F.ok(!d->pr_stall, "stall");
  sh4t::Resp r; r.rdata = d->pr_rdata; r.hit = d->pr_hit;
  if (s) F.ok(r.rdata == mr.rdata && r.hit == mr.hit, "cyc %lu strobe %s %08x size %d: rtl %08x/%d model %08x/%d",
              (unsigned long)cyc, s->wr ? "wr" : "rd", addr, s->size, r.rdata, r.hit, mr.rdata, mr.hit);
  o = {(bool)d->bce, (bool)d->pce, (bool)d->cpu_run, (bool)d->cpu_clk, (bool)d->dmac_run, (bool)d->stby, (bool)d->wdt_settle,
       (bool)d->settle_ovf, (bool)d->rst_por, (bool)d->rst_man, (bool)d->rst_pin, d->div, d->status, d->mstp, d->ratio_inc};
  d->clk = 1; d->eval();
  T.edge();
  cyc++;
  T.wake = false; T.wake_stby = false; T.osc = false; T.dack_ic = false;   // pulses
  T.hudi = T.mhit = T.arr_mhit = T.cpu_rst_req = false;
  return r;
}
static void idle(uint64_t n) { while (n--) step(); }
// the next cycle is a pce cycle (the model predicts the RTL's enables exactly; the per-cycle compare checks that)
static bool next_pce() { T.eval(); return T.co.ce.pce; }
static void to_pce(uint64_t lim = 100000) { uint64_t n = 0; while (!next_pce() && n++ < lim) step(); }
static uint32_t rd(uint32_t a, int size) {
  to_pce(); sh4t::Strobe s; s.rd = true; s.size = size; return step(&s, a).rdata;
}
static void wr(uint32_t a, int size, uint32_t v) {
  to_pce(); sh4t::Strobe s; s.wr = true; s.size = size; s.wdata = v; step(&s, a);
}
static void pin_reset(unsigned md) {
  T.md = md; T.reset_n = false; T.sck2 = true; idle(12); T.reset_n = true;
  int n = 0;                                                  // bounded: a reset that never ends fails, not hangs
  while (!o.rst_pin && n++ < 100) step();
  while (o.rst_pin && n++ < 100) step();
  F.ok(n < 100, "pin reset: rst.pin did not assert and end");
}
static void wait_run(uint64_t lim = 200000) { uint64_t n = 0; while (!o.cpu_run && n++ < lim) step(); }

static int bdiv_of(unsigned c) { static const int v[8] = {1, 2, 3, 4, 6, 8, 8, 8}; return v[c & 7]; }
static int pdiv_of(unsigned c) { static const int v[8] = {2, 3, 4, 6, 8, 8, 8, 8}; return v[c & 7]; }

// measure the enables over n cycles: periods of bce / pce and the alignment
static void check_enables(const char *what, int bper, int pper, int n = 96) {
  std::vector<uint64_t> bs, ps;
  for (int k = 0; k < n; k++) {
    step();
    if (o.bce) bs.push_back(cyc - 1);
    if (o.pce) { ps.push_back(cyc - 1); F.ok(o.bce, "%s: pce without bce", what); }
  }
  bool bok = bs.size() > 2, pok = ps.size() > 2;
  for (size_t i = 1; i < bs.size(); i++) bok &= bs[i] - bs[i - 1] == (uint64_t)bper;
  for (size_t i = 1; i < ps.size(); i++) pok &= ps[i] - ps[i - 1] == (uint64_t)pper;
  F.ok(bok && pok, "%s: bce every %d, pce every %d expected; got %zu / %zu edges", what, bper, pper, bs.size(), ps.size());
}

static void test_reset_frqcr() {
  F.set("pin reset / FRQCR");
  static const uint16_t init[8] = {0x0E1A, 0x0E23, 0x0E13, 0x0E13, 0x0E0A, 0x0E0A, 0x0E0A, 0x0E0A};
  for (unsigned md = 0; md < 8; md++) {
    pin_reset(md);
    const uint32_t f = rd(FRQCR, 1);
    F.ok(f == init[md], "MD %u: FRQCR %04x, expected %04x", md, f, init[md]);
    F.ok(rd(STBCR, 0) == 0 && rd(STBCR2, 0) == 0, "MD %u: STBCR / STBCR2 not 0", md);
    check_enables("reset FRQCR", bdiv_of((f >> 3) & 7), pdiv_of(f & 7));
  }
  // DC mode: ph = 0 in the first cycle after the reset: bce at +1 +3, pce at +3 +7
  T.md = 5; T.reset_n = false; idle(12); T.reset_n = true;
  for (int n = 0; !o.rst_pin && n < 100; n++) step();
  for (int n = 0; o.rst_pin && n < 100; n++) step();         // o is the first cycle out of reset
  const uint64_t r0 = cyc - 1;
  std::vector<uint64_t> b, p;
  if (o.bce) b.push_back(0);
  for (int k = 1; k < 12; k++) { step(); if (o.bce) b.push_back(cyc - 1 - r0); if (o.pce) p.push_back(cyc - 1 - r0); }
  F.ok(b.size() >= 3 && b[0] == 1 && b[1] == 3 && p.size() >= 2 && p[0] == 3 && p[1] == 7,
       "phase after reset: bce at +%lu +%lu, pce at +%lu +%lu", (unsigned long)(b.size() > 0 ? b[0] : 99),
       (unsigned long)(b.size() > 1 ? b[1] : 99), (unsigned long)(p.size() > 0 ? p[0] : 99),
       (unsigned long)(p.size() > 1 ? p[1] : 99));
  F.ok(o.ratio == 12, "ratio_inc %u at 0x0E0A, expected 12", o.ratio);
}

static void test_ratios() {
  F.set("Table 10.4");
  static const uint16_t tbl[15] = {0x008, 0x00A, 0x00C, 0x011, 0x013, 0x01A, 0x01C, 0x023, 0x02C, 0x05A, 0x05C, 0x063,
                                   0x06C, 0x0A3, 0x0EC};
  wr(FRQCR, 1, 0x0C0A);                                      // PLL2 off: BFC changes switch at once, no WDT count
  for (uint16_t v : tbl) {
    wr(FRQCR, 1, 0x0C00 | v);
    F.ok(o.cpu_run, "FRQCR %04x with PLL2 off stopped the chip", 0x0C00 | v);
    to_pce(); step();                                         // the new dividers from the next pce
    const int idiv = bdiv_of((v >> 6) & 7), bdv = bdiv_of((v >> 3) & 7), pdv = pdiv_of(v & 7);
    char w[32]; snprintf(w, sizeof w, "FRQCR %04x", 0x0C00 | v);
    check_enables(w, bdv, pdv, 120);
    (void)idiv;                                               // IFC is stored and read back only (SOC.md 9.18)
    F.ok(o.ratio == (uint32_t)(24 / bdv), "%s: ratio_inc %u, expected %d (24 / Bdiv)", w, o.ratio, 24 / bdv);
    F.ok(rd(FRQCR, 1) == (0x0C00u | v), "%s read back", w);
  }
  wr(FRQCR, 1, 0x0C5A); to_pce(); step(); step();            // I x1/2, B x1/4: bce every 4 clk, so 24 / 4 = 6
  F.ok(o.ratio == 6, "FRQCR 0C5A: ratio_inc %u, expected 6 (IFC treated as x1)", o.ratio);
  wr(FRQCR, 1, 0x0C0A); to_pce(); step();
  wr(FRQCR, 1, 0xFE0A);  F.ok(rd(FRQCR, 1) == 0x0E0A, "FRQCR bits 15:12 read %04x", rd(FRQCR, 1));
  wait_run();
  F.set("registers");
  wr(FRQCR, 0, 0x55); wr(FRQCR, 2, 0x55); F.ok(rd(FRQCR, 1) == 0x0E0A && rd(FRQCR, 0) == 0 && rd(FRQCR, 2) == 0, "FRQCR sizes");
  wr(STBCR, 0, 0xE0); F.ok(rd(STBCR, 0) == 0xE0 && o.mstp == 0, "STBCR E0");
  wr(STBCR, 0, 0x1F); step(); F.ok(rd(STBCR, 0) == 0x1F && o.mstp == 0x1F && !o.dmac_run, "STBCR 1F: mstp %02x dmac_run %d", o.mstp, o.dmac_run);
  wr(STBCR, 1, 0x00); F.ok(rd(STBCR, 0) == 0x1F && rd(STBCR, 1) == 0, "STBCR word access");
  wr(STBCR, 0, 0x03); step(); F.ok(o.mstp == 0x03 && o.dmac_run, "STBCR 03 (the DC boot value)");
  wr(STBCR2, 0, 0xFF); F.ok(rd(STBCR2, 0) == 0x80, "STBCR2 FF -> %02x", rd(STBCR2, 0));
  wr(STBCR2, 0, 0x00);
  rd(0xFFC00014, 0); rd(0xFFC00100, 1); rd(0xFFC00009, 0);    // holes
}

// ENTRY: the core halts at the request; the CPU clock stops after the 9th pce edge that follows the request cycle (no
// Pclk write in flight).  Returns the number of the last cycle with cpu_clk = 1 (0: it never stopped within `lim`)
static const int ENTRY_PCE = 9, RESYNC_PCE = 4;
static uint64_t wait_stop(uint64_t r0, int *pces_seen, uint64_t lim = 200) {
  int pces = 0; uint64_t last = 0;
  while (cyc - r0 < lim) {
    step();
    if (!o.cpu_clk) break;
    last = cyc - 1; pces += o.pce;
  }
  if (pces_seen) *pces_seen = pces;
  return o.cpu_clk ? 0 : last;
}

static void test_sleep() {
  F.set("sleep");
  wr(STBCR, 0, 0x00); wr(STBCR2, 0, 0x00);
  for (int ph = 0; ph < 4; ph++) {
    idle(3 + ph);
    T.sleep_req = true; step();                               // SLEEP completes: sleep_req rises
    F.ok(o.cpu_run && o.cpu_clk, "cpu_run / cpu_clk fell in the request cycle");
    const uint64_t r0 = cyc - 1;
    step();
    F.ok(!o.cpu_run && o.cpu_clk && o.dmac_run, "entry: run %d clk %d dmac %d (the core halts, the clock runs)",
         o.cpu_run, o.cpu_clk, o.dmac_run);
    int pces = 0;
    const uint64_t last = wait_stop(r0, &pces);
    F.ok(last && pces == ENTRY_PCE && o.pce == false, "entry %d: the clock stopped after %d pce edges (expected %d; the "
         "last clocked cycle a pce), %lu Icyc after the request", ph, pces, ENTRY_PCE, (unsigned long)(last - r0));
    F.ok(!o.cpu_run && !o.cpu_clk && o.status == sh4t::STS_SLEEP && o.dmac_run, "sleep: run %d clk %d status %u dmac %d",
         o.cpu_run, o.cpu_clk, o.status, o.dmac_run);
    const uint32_t d0 = o.div; int bces = 0, pcs = 0;
    for (int k = 0; k < 40; k++) { step(); bces += o.bce; pcs += o.pce; }
    F.ok(bces == 20 && pcs == 10 && o.div == ((d0 + 40) & 0xFFF), "enables / div stopped in sleep");
    // the wake in Pclk phase ph (ph = 0: the cycle right after a pce)
    to_pce(); step(); idle(ph);
    T.wake = true; step();
    const uint64_t w = cyc - 1;
    bool clk_rs = false;
    while (!o.cpu_run && cyc - w < 100) { step(); clk_rs |= o.cpu_clk && !o.cpu_run; }
    const int expect[4] = {16, 15, 14, 17};                    // RUN after the 4th pce edge following the wake cycle
    F.ok(cyc - 1 - w == (uint64_t)expect[ph] && !clk_rs, "wake in phase %d: cpu_run after %lu Icyc, expected %d; the "
         "clock ran during the resync %d (it must not: it stopped in SLEEP)", ph, (unsigned long)(cyc - 1 - w), expect[ph],
         clk_rs);
    F.ok(o.status == sh4t::STS_NORMAL && o.cpu_clk, "status / clock after the wake");
    idle(20);
    F.ok(o.cpu_run, "sleep_req held high after the wake slept again");
    T.sleep_req = false; idle(2);
  }
  // a one-cycle sleep_req pulse sleeps as well
  T.sleep_req = true; step(); T.sleep_req = false; const uint64_t p0 = cyc - 1; step();
  F.ok(!o.cpu_run && o.cpu_clk, "a one-cycle sleep_req: run %d clk %d (entry)", o.cpu_run, o.cpu_clk);
  F.ok(wait_stop(p0, nullptr) && o.status == sh4t::STS_SLEEP, "a one-cycle sleep_req: no clock stop (status %u)", o.status);
  idle(30); F.ok(!o.cpu_run, "woke without a wake");
  T.wake = true; step(); wait_run(200); idle(2);
  // a wake pending when the sleep starts: the entry resynchronises at once, the clock never stops
  T.wake = true; T.sleep_req = true; step();
  T.wake = true; step(); T.wake = true; step();
  bool stopped = false; int n = 0;
  while (!o.cpu_run && n < 50) { step(); n++; stopped |= !o.cpu_clk; }
  F.ok(n <= 18 && !stopped, "pending wake: back in %d Icyc, the clock stopped %d", n, stopped);
  T.sleep_req = false; idle(2);
  // a wake during the entry (after 4 of its pce edges): RESYNC with the clock running throughout (never stopped)
  T.sleep_req = true; step(); T.sleep_req = false;
  { int p = 0; while (p < 4) { step(); p += o.pce; } }
  F.ok(!o.cpu_run && o.cpu_clk, "wake in the entry: not in the entry (run %d clk %d)", o.cpu_run, o.cpu_clk);
  T.wake = true; step();
  { const uint64_t w = cyc - 1; bool stp = false;
    while (!o.cpu_run && cyc - w < 100) { step(); stp |= !o.cpu_clk; }
    F.ok(o.cpu_run && !stp && cyc - 1 - w >= 14 && cyc - 1 - w <= 17, "wake in the entry: cpu_run after %lu Icyc (14-17), "
         "the clock stopped %d", (unsigned long)(cyc - 1 - w), stp); }
  idle(2);
  F.set("deep sleep");
  wr(STBCR2, 0, 0x80);
  T.sleep_req = true; step(); const uint64_t q0 = cyc - 1; step();
  F.ok(!o.cpu_run && o.dmac_run, "deep sleep entry: run %d dmac %d (the DMAC runs until the clock stops)", o.cpu_run,
       o.dmac_run);
  wait_stop(q0, nullptr);
  F.ok(!o.cpu_run && !o.dmac_run && o.status == sh4t::STS_SLEEP, "deep sleep: run %d dmac %d", o.cpu_run, o.dmac_run);
  T.wake = true; step();
  bool dm = false; for (int n = 0; !o.cpu_run && n < 100; n++) { step(); dm |= o.dmac_run && !o.cpu_run; }
  F.ok(!dm && o.dmac_run, "dmac_run during the deep-sleep resync");
  T.sleep_req = false; idle(2);
  wr(STBCR2, 0, 0x00);
}

static void test_standby() {
  F.set("standby");
  // the TMU: channel 0 at Pphi/4, channel 1 on the RTC clock (TPSC 6)
  wr(0xFFD80014, 2, 0xFFFFFFFF); wr(0xFFD8001C, 1, 6); wr(0xFFD80018, 2, 1000);
  wr(TCR0, 1, 0); wr(TCOR0, 2, 0xFFFFFFFF); wr(TCNT0, 2, 0xFFFFFFFF);
  wr(TSTR, 0, 3);
  wr(WTCSR, 1, 0xA500); wr(WTCNT, 1, 0x5AF0);                // TME = 0, CKS 0, WTCNT F0
  wr(STBCR, 0, 0x80);
  wr(STBCR2, 0, 0x80);                                       // DSLP = 1 too: STBY wins (hm 9.2.4: DSLP needs STBY = 0)
  T.sleep_req = true; step(); const uint64_t r0 = cyc - 1;
  F.ok(wait_stop(r0, nullptr) != 0, "standby: the clock-stop handshake did not end");
  F.ok(!o.cpu_run && o.stby && o.status == sh4t::STS_STANDBY && !o.dmac_run, "standby (STBY = DSLP = 1): run %d stby %d "
       "status %u", o.cpu_run, o.stby, o.status);
  const uint32_t d0 = o.div; int ce = 0; bool left = false;
  for (int k = 0; k < 200; k++) {
    if (k % 20 == 5) T.osc = true;
    if (k == 50 || k == 120) T.wake = true;                  // a plain wake (the sleep path's) must not end standby
    step(); ce += o.bce + o.pce; left |= !o.stby || o.settle || o.cpu_run;
  }
  F.ok(ce == 0 && o.div == d0 && !left, "standby: enables %d, div %03x -> %03x, left standby on a plain wake %d", ce, d0,
       o.div, left);
  T.wake_stby = true; step();
  const uint64_t w = cyc - 1;
  bool set_seen = false, rs_bad = false; int ovfs = 0, pces = 0; uint64_t tov = 0, p2 = 0;
  while (!o.cpu_run && cyc - w < 5000) {
    step();
    set_seen |= o.settle; ovfs += o.sovf;
    if (o.sovf) { tov = cyc - 1; continue; }
    if (!tov || o.cpu_run) continue;
    // after the overflow: the clocks run, STATUS is normal (hm 9.5.2), the core waits for the 4-pce resynchroniser
    // (the CPU clock stays stopped: the resync ends a standby, not an entry)
    rs_bad |= o.stby || o.settle || o.status != sh4t::STS_NORMAL || !o.dmac_run || o.cpu_clk;
    if (o.pce && ++pces == RESYNC_PCE) p2 = cyc - 1;
  }
  const uint64_t t = tov + 1 - w;
  // 16 counts from F0 at /32: 16 rising edges of the tap, or an early first count (the gated clock rises with settle
  // when the tap is high) and 15 edges
  F.ok(set_seen && ovfs == 1 && t >= 14 * 32 + 17 && t <= 16 * 32 + 3, "standby exit: settle %d, %d overflow(s) after "
       "%lu Icyc (16 counts at /32 from F0)", set_seen, ovfs, (unsigned long)t);
  // cpu_run only after the 4th pce that follows the overflow: v1_intc's request registers (bce, stopped in standby)
  // present irq.req 3-4 Icyc after the clocks restart, so the core must not resume at the overflow itself
  F.ok(o.cpu_run && p2 && cyc - 1 == p2 + 1 && !rs_bad, "standby exit: overflow at +%lu, 4th pce at +%lu, RUN at +%lu "
       "(expected the cycle after the 4th pce), resync state bad %d", (unsigned long)(tov - w),
       (unsigned long)(p2 ? p2 - w : 0), (unsigned long)(cyc - 1 - w), rs_bad);
  T.sleep_req = false;
  wr(STBCR2, 0, 0x00);
  F.ok(rd(WTCSR, 0) == 0x00 && rd(WTCNT, 0) <= 1, "after the settle: WTCSR %02x (no flag) WTCNT %02x (00-01)", rd(WTCSR, 0),
       rd(WTCNT, 0));
  F.ok(rd(TSTR, 0) == 0x02, "standby: TSTR %02x, expected 02 (the TPSC 6 channel kept)", rd(TSTR, 0));
  wr(TSTR, 0, 0); wr(STBCR, 0, 0x00);
}

// SOC.md 2.5 / 3.1.5 Landing (M1a, M1b; WI-SOC): the clock stop waits for a Pclk write in flight (p4_wpend, the
// bridge's pclk_wpend) to land and decides the mode with the STBCR that write left: hm 9.5.1's "set STBY, then execute
// SLEEP" with the STBCR write posted right before the SLEEP.  The core halts at the request whatever the bus does
static void test_sleep_wpend() {
  F.set("sleep vs a Pclk write in flight");
  wr(STBCR, 0, 0x00); wr(STBCR2, 0, 0x00);
  wr(WTCSR, 1, 0xA500); wr(WTCNT, 1, 0x5AF0);
  // (a) the request rises while the STBCR write is in flight; the write lands 6 cycles later (STBY), well inside the
  //     entry: the stop at the 9th pce enters standby
  T.p4_wpend = true; T.sleep_req = true; step(); const uint64_t r0 = cyc - 1; T.sleep_req = false;
  int pa = 0;
  for (int k = 0; k < 5; k++) { step(); pa += o.pce; F.ok(!o.cpu_run && o.cpu_clk, "(a) +%d: run %d clk %d (the entry)", k,
                                                            o.cpu_run, o.cpu_clk); }
  while (!next_pce()) { step(); pa += o.pce; }
  { sh4t::Strobe s; s.wr = true; s.size = 0; s.wdata = 0x80; step(&s, STBCR); pa += o.pce; }   // the landing
  T.p4_wpend = false;
  int pces = 0;
  F.ok(wait_stop(r0, &pces) && pa + pces == ENTRY_PCE, "(a) the clock stopped after %d + %d pce edges (expected %d)", pa,
       pces, ENTRY_PCE);
  F.ok(!o.cpu_run && o.stby && o.status == sh4t::STS_STANDBY, "(a) STBY landed during the entry: run %d stby %d status %u "
       "(expected standby)", o.cpu_run, o.stby, o.status);
  T.wake_stby = true; step(); wait_run(5000);
  F.ok(o.cpu_run, "(a) no standby exit");
  idle(2);
  wr(STBCR, 0, 0x00); wr(WTCNT, 1, 0x5AF0);
  // (b) the write is still in flight at the 9th pce: the clock runs until it lands, and then stops (sleep)
  T.p4_wpend = true; T.sleep_req = true; step(); const uint64_t b0 = cyc - 1; T.sleep_req = false;
  { int p = 0; while (p < ENTRY_PCE + 3 && cyc - b0 < 200) { step(); p += o.pce; } }
  F.ok(!o.cpu_run && o.cpu_clk, "(b) the clock stopped under a Pclk write in flight (run %d clk %d)", o.cpu_run, o.cpu_clk);
  T.p4_wpend = false; step();
  step();
  F.ok(!o.cpu_run && !o.cpu_clk && o.status == sh4t::STS_SLEEP, "(b) no stop the cycle after the landing: run %d clk %d "
       "status %u", o.cpu_run, o.cpu_clk, o.status);
  T.wake = true; step(); wait_run(200); idle(2);
  // (c) no write in flight: the stop at the 9th pce
  T.sleep_req = true; step(); const uint64_t c0 = cyc - 1; T.sleep_req = false;
  pces = 0;
  F.ok(wait_stop(c0, &pces) && pces == ENTRY_PCE && o.status == sh4t::STS_SLEEP, "(c) no Pclk write in flight: %d pce edges "
       "(expected %d), status %u", pces, ENTRY_PCE, o.status);
  T.wake = true; step(); wait_run(200); idle(2);
}

static void test_pll() {
  F.set("PLL relock");
  wr(WTCSR, 1, 0xA500); wr(WTCNT, 1, 0x5AF0);
  struct C { uint16_t from, to; int phases; } cs[] = {
    {0x0A0A, 0x0E0A, 2},                                     // PLL1 on with PLL2 on: PLL1, then PLL2
    {0x080A, 0x0C0A, 1},                                     // PLL1 on with PLL2 off
    {0x0E0A, 0x0E12, 1},                                     // BFC x1/2 -> x1/3 with PLL2 on (PFC 010 kept)
    {0x0C0A, 0x0C12, 0},                                     // the same with PLL2 off: at once
  };
  for (auto &c : cs) {
    wr(FRQCR, 1, c.from); wait_run();
    wr(WTCNT, 1, 0x5AF0);
    wr(FRQCR, 1, c.to);
    int ovfs = 0; uint64_t t0 = cyc, t1 = 0;
    step();
    while (!o.cpu_run && cyc - t0 < 5000) { step(); if (o.sovf) { ovfs++; if (!t1) t1 = cyc - 1; } }
    F.ok(ovfs == c.phases, "FRQCR %04x -> %04x: %d WDT counts, expected %d", c.from, c.to, ovfs, c.phases);
    if (c.phases == 2) F.ok(cyc - 1 - t1 >= 16 * 32 && cyc - 1 - t1 <= 16 * 32 + 2,
                            "second phase %lu Icyc: it restarts from F0", (unsigned long)(cyc - 1 - t1));
  }
  wr(FRQCR, 1, 0x0E0A); wait_run();
  to_pce(); step();
  // a one-cycle sleep_req in the cycle a relocking FRQCR write lands (a posted FRQCR write draining after SLEEP): the
  // relock's settle wins the cycle and the sleep is taken when it ends [P]
  F.set("sleep vs relock");
  wr(STBCR, 0, 0x00); wr(STBCR2, 0, 0x00);
  for (int lvl = 0; lvl < 2; lvl++) {                        // a one-cycle pulse, then a level held until the wake
    wr(WTCNT, 1, 0x5AF0);
    to_pce();
    { sh4t::Strobe s; s.wr = true; s.size = 1; s.wdata = 0x0E12; T.sleep_req = true; step(&s, FRQCR); }
    if (!lvl) T.sleep_req = false;
    step();
    F.ok(!o.cpu_run && o.settle, "%s: the BFC relock did not settle", lvl ? "level" : "pulse");
    int n = 0; while (o.settle && n++ < 5000) step();
    idle(2);
    { int k = 0; while (o.cpu_clk && k++ < 80) step(); }    // the clock-stop handshake after the settle (9 pce)
    F.ok(!o.cpu_run && o.status == sh4t::STS_SLEEP && !o.settle, "%s sleep_req in the relocking write's cycle: after the "
         "settle run %d status %u (expected asleep)", lvl ? "a level" : "a one-cycle", o.cpu_run, o.status);
    T.wake = true; step(); wait_run(200);
    F.ok(o.cpu_run, "no wake after the pending sleep");
    T.sleep_req = false; idle(2);
    wr(WTCNT, 1, 0x5AF0); wr(FRQCR, 1, 0x0E0A); wait_run(); to_pce(); step();
  }
}

static void test_wdt_resets() {
  F.set("WDT resets");
  for (int rsts = 0; rsts < 2; rsts++) {
    wr(STBCR, 0, 0x03); wr(STBCR2, 0, 0x80);
    wr(FRQCR, 1, 0x0C0A); wait_run();                          // PLL2 off (a value only the pin resets)
    wr(TCR0, 1, 0x25); wr(TSTR, 0, 1);
    wr(WTCSR, 1, 0xA540 | (rsts << 5)); wr(WTCNT, 1, 0x5AF8);
    wr(WTCSR, 1, 0xA5C0 | (rsts << 5));
    bool seen = false; int sts_reset = 0;
    for (int k = 0; k < 600; k++) { step(); if (o.rst_por || o.rst_man) { seen = true; sts_reset += o.status == sh4t::STS_RESET; } }
    F.ok(seen && sts_reset == 8, "RSTS %d: reset seen %d, STATUS = reset for %d cycles (8)", rsts, seen, sts_reset);
    F.ok(rd(FRQCR, 1) == 0x0C0A, "RSTS %d: FRQCR %04x, the WDT reset must hold it", rsts, rd(FRQCR, 1));
    F.ok((rd(WTCSR, 0) & 0x10) && rd(WTCSR, 0) & 0x80, "RSTS %d: WTCSR %02x (WOVF and TME held)", rsts, rd(WTCSR, 0));
    if (rsts == 0) F.ok(rd(STBCR, 0) == 0 && rd(STBCR2, 0) == 0, "WDT power-on reset: STBCR %02x STBCR2 %02x", rd(STBCR, 0), rd(STBCR2, 0));
    else F.ok(rd(STBCR, 0) == 0x03 && rd(STBCR2, 0) == 0x80, "WDT manual reset: STBCR %02x STBCR2 %02x", rd(STBCR, 0), rd(STBCR2, 0));
    F.ok(rd(TCR0, 1) == 0 && rd(TSTR, 0) == 0, "RSTS %d: the TMU was not initialised", rsts);
    wr(WTCSR, 1, 0xA500); wr(WTCNT, 1, 0x5A00);
  }
  pin_reset(5);
  F.ok(rd(FRQCR, 1) == 0x0E0A && rd(WTCSR, 0) == 0, "the pin reset");
}

static void test_random(uint64_t seed, uint64_t ncyc) {
  F.set("random");
  Rng r(seed);
  const uint64_t end = cyc + ncyc;
  static const uint32_t regs[] = {FRQCR, STBCR, STBCR2, WTCNT, WTCSR, 0xFFC00014, 0xFFD80000, TSTR, TCOR0, TCNT0, TCR0,
                                  0xFFD80014, 0xFFD80018, 0xFFD8001C, 0xFFD80020, 0xFFD80024, 0xFFD80028, 0xFFD8002C,
                                  0xFFC80000, 0xFFC80004, 0xFFC80020, 0xFFC80038, RCR2, 0xFFC8001C};
  while (cyc < end) {
    if (r.chance(4)) T.sleep_req = !T.sleep_req;
    if (r.chance(6)) T.p4_wpend = !T.p4_wpend;              // a Pclk write in flight now and then (SOC.md 2.5)
    if (r.chance(10)) T.wake = true;
    if (r.chance(5)) T.wake_stby = true;
    if (r.chance(100)) T.osc = true;
    if (r.chance(3)) T.tclk_in = !T.tclk_in;
    if (r.chance(5)) T.dack_ic = true;
    if (r.below(50000) == 0) { switch (r.below(6)) { case 0: T.reset_n = false; break; case 1: T.hudi = true; break;
                               case 2: T.mhit = true; break; case 3: T.arr_mhit = true; break; case 4: T.cpu_rst_req = true; break;
                               default: T.sck2 = !T.sck2; } }
    if (!T.reset_n && r.chance(50)) { T.reset_n = true; T.sck2 = true; }
    if (r.below(2000) == 0) T.md = r.below(8);
    if (next_pce() && r.chance(200)) {
      sh4t::Strobe s; s.rd = r.chance(500); s.wr = !s.rd;
      const uint32_t a = regs[r.below(sizeof regs / sizeof regs[0])] + (r.chance(30) ? r.below(4) : 0);
      const int b = blk_of(a);
      int own = 0;
      if (a == FRQCR) own = 1; else if (b == 27) own = sh4t::Tmu::size_of(sh4t::Tmu::reg_of(a) < 0 ? 0 : sh4t::Tmu::reg_of(a));
      else if (a == 0xFFC8001C) own = 1; else if (b == 24 && s.wr) own = 1;
      s.size = r.chance(850) ? own : r.below(3);
      uint32_t v = r.u32();
      if (a == FRQCR) v = r.chance(700) ? (0x0E0A ^ (r.chance(300) ? (1u << r.below(12)) : 0)) : v;
      if (a == STBCR) v = r.chance(500) ? (v & 0x1F) : (r.chance(50) ? 0x80 : v & 0x7F);
      if (a == WTCNT || a == WTCSR) v = r.chance(800) ? (((a == WTCNT ? 0x5Au : 0xA5u) << 8) | (v & (a == WTCSR ? 0xBF : 0xFF))) : v;
      if (a == WTCSR && r.chance(900)) v &= ~0x40u;          // watchdog mode only now and then
      if (a == RCR2) v = (v & 0xF7) | (r.chance(900) ? 8 : 0);     // RTCEN mostly on (a manual reset holds it)
      s.wdata = v;
      step(&s, a);
    } else step();
  }
}

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  const uint64_t seed = arg_u64(argc, argv, "--seed", 1);
  const uint64_t ncyc = arg_u64(argc, argv, "--cycles", 3000000);
  d = new Vv1_cpg_tb;
  test_reset_frqcr();
  pin_reset(5);
  test_ratios();
  test_sleep();
  test_standby();
  test_sleep_wpend();
  test_pll();
  test_wdt_resets();
  test_random(seed, ncyc);
  d->final();
  delete d;
  return F.finish();
}
