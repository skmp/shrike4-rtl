// v1_clkwake_tb -- the CPG's wake paths with the real v1_intc (WI-CLK; SOC.md 2.5, 4.4.3; timers-clock 4.5.1, 7.2).
// The clock subsystem (v1_rstc, v1_cpg, v1_wdt, v1_tmu, v1_rtc) and v1_intc (WI-EXC's file, used read-only) as v1_soc
// wires them; this testbench plays the core (SLEEP: asleep + sleep_req held until the take; the take: irq_ack /
// nmi_ack, asleep and sleep_req dropped).  Directed checks:
//   * sleep, TMU1 underflow (32 trials, TCNT 100..131, 4 sleep-entry phases): v1_intc's `wake` and `irq.req` rise in
//     the same cycle and the same number of Icyc after tuni as the running case's irq.req; the wake arrives in a ph-1
//     cycle; the CPG's 4-pce resynchroniser adds exactly 15 Icyc (the number the v1_cpg header gives and M1b's
//     "S sleep irq" rule gates end to end), with the CPU clock (cpu_clk) stopped from the sleep until cpu_run rises;
//     irq.req is 1 when cpu_run rises (the core takes it at its first D cycle);
//   * sleep, NMI edges in each of the 4 Pclk phases, after the clock-stop handshake: the wake arrives in ph 1 or ph 3
//     and the CPG adds 15 / 17 Icyc;
//   * standby, a TPSC 6 (RTC clock) TMU0 underflow: wake_stby ends it, the WDT settles (16 counts from 0xF0 at CKS 0),
//     and cpu_run rises only after v1_intc presents irq.req again (its request registers stopped with bce): irq.req
//     is 1 in the cycle cpu_run rises.
// Prints the measured numbers.
//
//   tb
#include "Vv1_clkwake_tb.h"
#include "verilated.h"
#include "v1_clk_tbutil.h"
#include <map>

static Vv1_clkwake_tb *d;
static Fails F("v1-clkwake");
static uint64_t cyc = 0;

enum : uint32_t { STBCR = 0xFFC00004, WTCNT = 0xFFC00008, WTCSR = 0xFFC0000C, ICR = 0xFFD00000, IPRA = 0xFFD00004,
                  TSTR = 0xFFD80004, TCOR0 = 0xFFD80008, TCNT0 = 0xFFD8000C, TCR0 = 0xFFD80010, TCOR1 = 0xFFD80014,
                  TCNT1 = 0xFFD80018, TCR1 = 0xFFD8001C, RCR2 = 0xFFC8003C };

// this cycle's outputs (after the negedge eval, before the clock edge)
struct Obs { bool pce, bce, cpu_run, cpu_clk, stby, settle, sovf, req, nmi, wake, wake_stby; unsigned tuni, status; };
static Obs o;
static void pre() {
  d->clk = 0; d->eval();
  o = {(bool)d->pce, (bool)d->bce, (bool)d->cpu_run, (bool)d->cpu_clk, (bool)d->stby, (bool)d->wdt_settle, (bool)d->settle_ovf,
       (bool)d->irq_req, (bool)d->irq_nmi, (bool)d->wake, (bool)d->wake_stby, d->tuni, d->status};
}
static void edge() { d->clk = 1; d->eval(); cyc++; d->pq_rd = d->pq_wr = 0; d->irq_ack = d->nmi_ack = 0; }
static void step() { pre(); edge(); }
static void idle(int n) { while (n--) step(); }
static void to_pce() { for (int n = 0; n < 100000; n++) { pre(); if (o.pce) return; edge(); } }
static uint32_t acc(bool wr, int size, uint32_t a, uint32_t v) {
  to_pce();                                      // the bridge strobes a Pclk slave in a pce cycle (SOC.md 3.1.5)
  d->pq_rd = !wr; d->pq_wr = wr; d->pq_size = size; d->pq_addr = a; d->pq_wdata = v;
  pre(); const uint32_t r = d->pr_rdata; edge();
  return r;
}
static void wr(uint32_t a, int size, uint32_t v) { acc(true, size, a, v); }
static uint32_t rd(uint32_t a, int size) { return acc(false, size, a, 0); }
static int ph_of() { return o.pce ? 3 : o.bce ? 1 : -1; }   // ph 1 / 3 are the bce cycles (ph 0 / 2 are not)

// the core's side: SLEEP (asleep, sleep_req held until the take) and the take
static void sleep_on() { d->asleep = 1; d->sleep_req = 1; }
static void take(bool nmi) { d->asleep = 0; d->sleep_req = 0; if (nmi) d->nmi_ack = 1; else d->irq_ack = 1; step(); }

static void test_tmu_sleep() {
  F.set("TMU wake from sleep");
  wr(IPRA, 1, 0x0F00);                                        // TMU1 at 15, IMASK 14: the only enabled source
  std::map<int, int> h_spin, h_cpg;
  int d_spin0 = -1;
  for (int t = 0; t < 32; t++) {
    const uint32_t N = 100 + t;
    // running: tuni1 -> irq.req
    wr(TSTR, 0, 0); wr(TCR1, 1, 0x0020); wr(TCOR1, 2, N); wr(TCNT1, 2, N); wr(TSTR, 0, 2);
    uint64_t tu = 0, tq = 0;
    for (int k = 0; k < 20000 && !tq; k++) { pre(); if ((o.tuni & 2) && !tu) tu = cyc; if (o.req && !tq) tq = cyc; edge(); }
    const int d_spin = (int)(tq - tu);
    h_spin[d_spin]++;
    if (d_spin0 < 0) d_spin0 = d_spin;
    F.ok(tu && tq && d_spin == d_spin0, "trial %d: irq.req %d Icyc after tuni1 (trial 0: %d)", t, d_spin, d_spin0);
    // asleep: tuni1 -> wake (= irq.req) -> the CPG's resynchroniser -> cpu_run
    wr(TSTR, 0, 0); wr(TCR1, 1, 0x0020); wr(TCNT1, 2, N); wr(TSTR, 0, 2);
    idle(40 + (t % 4));
    sleep_on(); step();
    pre(); F.ok(!o.cpu_run, "trial %d: not asleep", t); edge();
    uint64_t tu2 = 0, tw = 0, tr = 0; int wph = -2; bool req_at_run = false, clk_stopped = false, clk_in_rs = false;
    for (int k = 0; k < 20000 && !tr; k++) {
      pre();
      clk_stopped |= !o.cpu_clk;
      if (tw && !o.cpu_run) clk_in_rs |= o.cpu_clk;
      if ((o.tuni & 2) && !tu2) tu2 = cyc;
      if (o.wake && !tw) { tw = cyc; wph = ph_of(); F.ok(o.req, "trial %d: wake without irq.req", t); }
      if (o.cpu_run && !tr) { tr = cyc; req_at_run = o.req; }
      edge();
    }
    const int d_wake = (int)(tw - tu2), d_cpg = (int)(tr - tw);
    h_cpg[d_cpg]++;
    F.ok(tu2 && tw && tr && d_wake == d_spin && wph == 1 && d_cpg == 15 && req_at_run,
         "trial %d: wake %d Icyc after tuni1 (running irq.req %d) in ph %d, cpu_run %d Icyc after the wake (expected ph 1, "
         "15), irq.req when cpu_run rose %d", t, d_wake, d_spin, wph, d_cpg, req_at_run);
    F.ok(clk_stopped && !clk_in_rs, "trial %d: the CPU clock stopped %d, ran during the resync %d (a sleep's resync is "
         "not counted)", t, clk_stopped, clk_in_rs);
    take(false);
    wr(TSTR, 0, 0); wr(TCR1, 1, 0x0000);
  }
  printf("  TMU1 running: irq.req - tuni1 (Icyc: trials):"); for (auto &kv : h_spin) printf(" %d:%d", kv.first, kv.second);
  printf("\n  TMU1 asleep: cpu_run - wake, the CPG's resynchroniser:"); for (auto &kv : h_cpg) printf(" %d:%d", kv.first, kv.second);
  printf("\n  so a TMU underflow tick -> cpu_run = %d Icyc from a sleep (tuni1 rises the cycle after the tick)\n",
         1 + d_spin0 + 15);
  wr(IPRA, 1, 0x0000);
}

static void test_nmi_sleep() {
  F.set("NMI wake from sleep");
  std::map<int, int> h;
  for (int t = 0; t < 16; t++) {
    idle(20);
    sleep_on(); step(); idle(60);                             // past the clock-stop handshake (9 pce)
    pre(); F.ok(!o.cpu_clk, "NMI trial %d: the clock did not stop", t); edge();
    to_pce(); edge(); idle(t % 4);                            // the pin falls in each Pclk phase
    d->nmi_pin = 0;
    uint64_t tw = 0, tr = 0; int wph = -2; bool req_at_run = false;
    for (int k = 0; k < 200 && !tr; k++) {
      pre();
      if (o.wake && !tw) { tw = cyc; wph = ph_of(); }
      if (o.cpu_run && !tr) { tr = cyc; req_at_run = o.req && o.nmi; }
      edge();
    }
    const int d_cpg = (int)(tr - tw);
    h[wph * 100 + d_cpg]++;
    F.ok(tw && tr && ((wph == 1 && d_cpg == 15) || (wph == 3 && d_cpg == 17)) && req_at_run,
         "NMI trial %d: wake in ph %d, cpu_run %d Icyc later (expected ph 1: 15, ph 3: 17), NMI request at the run %d", t, wph,
         d_cpg, req_at_run);
    take(true);
    d->nmi_pin = 1; idle(20);
  }
  printf("  NMI wakes (ph: trials, CPG Icyc):");
  for (auto &kv : h) printf(" ph %d: %d x %d", kv.first / 100, kv.second, kv.first % 100);
  printf("\n");
  F.ok(h.count(115) && h.count(317), "the NMI wakes did not cover both ph 1 and ph 3");
}

static void test_standby() {
  F.set("TMU wake from standby");
  wr(IPRA, 1, 0xF000);                                        // TMU0 at 15
  wr(TSTR, 0, 0); wr(TCR0, 1, 0x0026); wr(TCOR0, 2, 0xFFFFFFFF); wr(TCNT0, 2, 20);   // TPSC 6 (the RTC clock), UNIE
  wr(RCR2, 0, 0x09);                                          // the RTC oscillator gate on (its power-on value)
  wr(TSTR, 0, 1);
  wr(WTCSR, 1, 0xA500); wr(WTCNT, 1, 0x5AF0);                // TME = 0, CKS 0, WTCNT F0
  wr(STBCR, 0, 0x80);
  sleep_on(); step(); idle(60);                               // the clock-stop handshake (9 pce), then standby
  pre(); F.ok(o.stby, "standby not entered"); edge();
  uint64_t t0 = cyc, tws = 0, tov = 0, tr = 0; bool osc = false, req_at_run = false; int req_lead = -1;
  uint64_t treq = 0;
  for (int k = 0; k < 100000 && !tr; k++) {
    osc = !osc; d->osc_tick = osc;                            // a 50 MHz "oscillator" to make the test short
    pre();
    if (o.wake_stby && !tws) tws = cyc;
    if (o.sovf && !tov) tov = cyc;
    if (tov && o.req && !treq) treq = cyc;
    if (o.cpu_run && !tr) { tr = cyc; req_at_run = o.req; }
    edge();
  }
  d->osc_tick = 0;
  if (treq && tr) req_lead = (int)(tr - treq);
  printf("  standby: wake_stby +%lu, WDT overflow +%lu, irq.req +%lu, cpu_run +%lu Icyc\n", (unsigned long)(tws - t0),
         (unsigned long)(tov - t0), (unsigned long)(treq - t0), (unsigned long)(tr - t0));
  F.ok(tws && tov > tws + 14 * 32 && tov <= tws + 16 * 32 + 2, "the standby exit's WDT settle: %lu Icyc",
       (unsigned long)(tov - tws));
  F.ok(tr && req_at_run && req_lead >= 1, "standby exit: irq.req %d Icyc before cpu_run (must be up when the core resumes)",
       req_lead);
  take(false);
  wr(TSTR, 0, 0); wr(TCR0, 1, 0); wr(STBCR, 0, 0); wr(IPRA, 1, 0);
}

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  d = new Vv1_clkwake_tb;
  d->reset_n = 0; d->sck2_i = 1; d->sleep_req = 0; d->osc_tick = 0; d->nmi_pin = 1; d->sr_imask = 14; d->sr_bl = 0;
  d->asleep = 0; d->irq_ack = 0; d->nmi_ack = 0; d->pq_rd = d->pq_wr = 0; d->pq_size = 0; d->pq_addr = 0; d->pq_wdata = 0;
  idle(16); d->reset_n = 1; idle(30);
  test_tmu_sleep();
  test_nmi_sleep();
  test_standby();
  d->final();
  delete d;
  return F.finish();
}
