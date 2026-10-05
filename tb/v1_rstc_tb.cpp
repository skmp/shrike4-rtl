// v1_rstc_tb -- unit test of rtl/v1/v1_rstc.sv (WI-CLK; SOC.md 2.2, 2.3).
//
// Every Icyc the RTL's rst levels and rst_code are compared with sh4t::Rstc.  Directed checks, independent of the
// model: the RESET pin with SCK2 high is a pin power-on reset (pin, por, cpu; EXPEVT code 000) and with SCK2 low a
// manual reset (man, cpu; 020), asserted from the 3rd cycle after RESET falls (the 2-flop synchroniser and the output
// register) to the 8th cycle after it rises (+2); every request pulse (WDT power-on / manual, H-UDI, TLB multiple hit
// from the core or an associative array write, CPU-only) asserts its kind from the next cycle for exactly 8 cycles with
// its code (000 / 020 / 000 / 140 / 140 / 020); merges keep the strongest kind (pin > por > man > cpu) and extend the
// hold; a multiple hit merged into a manual reset gives 140; SCK2 high at any time while RESET is low makes it a
// power-on reset.  Then random request traffic against the model.
//
// Also run with +verilator+rand+reset+2 +verilator+seed+N (random power-up state): the reset controller must reach
// the pin power-on reset from any state of its flops.
//
//   tb [--seed S] [--cycles N]
#include "Vv1_rstc_tb.h"
#include "verilated.h"
#include "sh4_timers.h"
#include "v1_clk_tbutil.h"

static Vv1_rstc_tb *d;
static sh4t::Rstc m;
static Fails F("v1-rstc");
static uint64_t cyc = 0;
static sh4t::Rstc::In in;
struct Out { bool pin, por, man, cpu; int code; };
static Out o;                             // the outputs of the last stepped cycle

static void step() {
  d->reset_n = in.reset_n; d->sck2_i = in.sck2; d->wdt_rst_por = in.wdt_por; d->wdt_rst_man = in.wdt_man;
  d->hudi_rst = in.hudi; d->mhit_req = in.mhit; d->arr_mhit = in.arr_mhit; d->cpu_rst_req = in.cpu_req;
  d->clk = 0; d->eval();
  const sh4t::Rst r = m.rst();
  // the first 4 cycles hold whatever the flops powered up with (run with +verilator+rand+reset+2 to randomise it):
  // RESET is low from time 0, and after the synchronisers the RTL and the model must agree whatever that state was
  if (cyc >= 4) F.ok(d->rst_pin == r.pin && d->rst_por == r.por && d->rst_man == r.man && d->rst_cpu == r.cpu && d->rst_code == m.code(),
       "cyc %lu rtl pin %d por %d man %d cpu %d code %d, model %d %d %d %d %d", (unsigned long)cyc, d->rst_pin, d->rst_por,
       d->rst_man, d->rst_cpu, d->rst_code, r.pin, r.por, r.man, r.cpu, m.code());
  o = {(bool)d->rst_pin, (bool)d->rst_por, (bool)d->rst_man, (bool)d->rst_cpu, d->rst_code};
  d->clk = 1; d->eval();
  m.step(in);
  cyc++;
  in.wdt_por = in.wdt_man = in.hudi = in.mhit = in.arr_mhit = in.cpu_req = false;      // pulses
}
static void idle(int n) { while (n--) step(); }
static bool any() { return o.pin || o.por || o.man || o.cpu; }
// run until the reset ends; returns the number of cycles it was asserted from now
static int drain() { int n = 0; step(); while (any() && n < 1000) { n++; step(); } return n; }

static void expect_kind(const char *what, bool pin, bool por, bool man, int code) {
  F.ok(o.pin == pin && o.por == por && o.man == man && o.cpu && o.code == code, "%s: pin %d por %d man %d cpu %d code %d,"
       " expected %d %d %d 1 %d", what, o.pin, o.por, o.man, o.cpu, o.code, pin, por, man, code);
}

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  const uint64_t seed = arg_u64(argc, argv, "--seed", 1);
  const uint64_t ncyc = arg_u64(argc, argv, "--cycles", 2000000);
  d = new Vv1_rstc_tb;
  in.reset_n = false; in.sck2 = true; idle(4); in.reset_n = true; idle(20);
  F.set("pin");
  for (int sck2 = 1; sck2 >= 0; sck2--) {
    idle(3);
    in.sck2 = sck2; in.reset_n = false;
    const uint64_t a = cyc;
    int first = -1;
    for (int k = 0; k < 40; k++) { step(); if (any() && first < 0) first = (int)(cyc - 1 - a); }
    F.ok(first == 3, "RESET low: asserted from +%d, expected +3", first);
    if (sck2) expect_kind("RESET, SCK2 high", true, true, false, sh4t::RC_000);
    else expect_kind("RESET, SCK2 low", false, false, true, sh4t::RC_020);
    in.reset_n = true;
    const uint64_t b = cyc;                                  // the first cycle with RESET high
    int n = 0; while (any() && n < 100) { step(); n++; }
    // the last low cycle is b - 1: asserted through b - 1 + 2 + 8
    F.ok(cyc - 1 == b + 10, "RESET high: deasserted after +%lu, expected +10", (unsigned long)(cyc - 1 - b));
    in.sck2 = true;
  }
  F.set("pulses");
  struct P { const char *n; int which; bool pin, por, man; int code; } ps[] = {
    {"wdt_rst_por", 0, false, true, false, sh4t::RC_000}, {"hudi_rst", 1, false, true, false, sh4t::RC_000},
    {"wdt_rst_man", 2, false, false, true, sh4t::RC_020}, {"mhit_req", 3, false, false, true, sh4t::RC_140},
    {"arr_mhit", 4, false, false, true, sh4t::RC_140}, {"cpu_rst_req", 5, false, false, false, sh4t::RC_020}};
  auto fire = [](int w) { switch (w) { case 0: in.wdt_por = true; break; case 1: in.hudi = true; break;
                                       case 2: in.wdt_man = true; break; case 3: in.mhit = true; break;
                                       case 4: in.arr_mhit = true; break; default: in.cpu_req = true; } };
  for (auto &p : ps) {
    idle(5);
    fire(p.which); step();
    F.ok(!any(), "%s: asserted in the request cycle", p.n);
    step(); expect_kind(p.n, p.pin, p.por, p.man, p.code);
    const int n = drain();
    F.ok(n == 7, "%s: asserted %d cycles, expected 8", p.n, n + 1);
  }
  F.set("merges");
  idle(5);
  in.wdt_man = true; step(); idle(3);
  in.cpu_req = true; step(); step(); expect_kind("cpu into manual", false, false, true, sh4t::RC_020);
  in.mhit = true; step(); step(); expect_kind("multiple hit into manual", false, false, true, sh4t::RC_140);
  in.wdt_man = true; step(); step(); expect_kind("manual after the multiple hit", false, false, true, sh4t::RC_140);
  in.wdt_por = true; step(); step(); expect_kind("power-on into manual", false, true, false, sh4t::RC_000);
  in.wdt_man = true; step(); step(); expect_kind("manual into power-on", false, true, false, sh4t::RC_000);
  F.ok(drain() == 7, "the last request did not extend the hold to 8");
  idle(3);
  in.cpu_req = true; step(); step(); expect_kind("cpu", false, false, false, sh4t::RC_020);
  F.ok(!o.man && !o.por && o.cpu, "CPU-only reset asserted another kind");
  in.wdt_man = true; step(); step(); expect_kind("manual into cpu", false, false, true, sh4t::RC_020);
  drain();
  // SCK2 high at any time while RESET is low: a power-on reset
  in.sck2 = false; in.reset_n = false; idle(6); in.sck2 = true; step(); in.sck2 = false; idle(6);
  expect_kind("SCK2 pulse while RESET low", true, true, false, sh4t::RC_000);
  in.reset_n = true; in.sck2 = true; drain();
  F.set("random");
  Rng r(seed);
  for (uint64_t k = 0; k < ncyc; k++) {
    if (r.chance(3)) in.reset_n = !in.reset_n;
    if (r.chance(20)) in.sck2 = !in.sck2;
    if (r.chance(4)) fire(r.below(6));
    if (r.chance(4)) fire(r.below(6));
    step();
  }
  d->final();
  delete d;
  return F.finish();
}
