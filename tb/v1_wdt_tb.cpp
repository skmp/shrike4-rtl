// v1_wdt_tb -- unit test of rtl/v1/v1_wdt.sv (WI-CLK; spec/timers-clock.md 8.1 v1_wdt_tb).
//
// Every Icyc the RTL's outputs (settle_ovf, iti, rst_req_por / _man, and rdata / hit of a strobe) are compared with
// sh4t::Wdt fed the same inputs.  Directed tests assert the console's facts independently of the model:
//   rates of all eight CKS (32 << CKS Icyc per count; 32 / 256 / 4096 measured, G), the early first count when TME is
//   set in the tap's high half (the phase sweep of R: 10 phases at CKS 3 and CKS 7, and every phase at CKS 0), the
//   write keys and sizes (G, exact console values), IOVF set by a written 1, interval overflow -> IOVF / ITI with the
//   counter running on, watchdog overflow -> WOVF and a one-cycle reset request of the RSTS kind, TME = 0 holds, a
//   WTCNT write while counting keeps the regular edge (R), the settle mode (no flags, settle_ovf, the second phase
//   restarting from the software value), and the reset rules (only the RESET pin clears the WDT).
// Then random register traffic, settle windows, divider freezes (standby) and resets against the model.
//
//   tb [--seed S] [--cycles N]
#include "Vv1_wdt_tb.h"
#include "verilated.h"
#include "sh4_timers.h"
#include "v1_clk_tbutil.h"

static Vv1_wdt_tb *d;
static sh4t::Wdt m;
static Fails F("v1-wdt");
static uint64_t cyc = 0;

struct Pins { bool pin = false, por = false, man = false, cpu = false, settle = false, freeze = false; } P;
static int pph = 0;                       // Pclk phase (pce when 3)
static uint16_t dv = 0;                   // the CPG divider (free-running; frozen in standby)
static bool o_sovf = false, o_iti = false, o_rpor = false, o_rman = false;

enum : uint32_t { WTCNT = 0xFC00008, WTCSR = 0xFC0000C };

static bool is_pce() { return pph == 3; }
static sh4t::Resp step(const sh4t::Strobe *s = nullptr) {
  const bool pce = is_pce(), bce = pce || (pph & 1);
  d->rst_pin = P.pin; d->rst_por = P.por || P.pin; d->rst_man = P.man; d->rst_cpu = P.pin || P.por || P.man || P.cpu;
  d->bce = bce; d->pce = pce; d->div = dv; d->settle = P.settle;
  d->pq_rd = s && s->rd; d->pq_wr = s && s->wr; d->pq_dma = 0;
  d->pq_size = s ? s->size : 0; d->pq_off = s ? s->off : 0; d->pq_wdata = s ? s->wdata : 0;
  d->clk = 0; d->eval();
  sh4t::Wdt::In in;
  in.rst.pin = P.pin; in.rst.por = P.por || P.pin; in.rst.man = P.man; in.rst.cpu = d->rst_cpu;
  in.div = dv; in.settle = P.settle;
  const sh4t::Wdt::Out o = m.out(in);
  sh4t::Resp mr; if (s) mr = m.strobe(*s);
  F.ok(d->settle_ovf == o.settle_ovf && d->iti == o.iti && d->rst_req_por == o.rst_por && d->rst_req_man == o.rst_man,
       "cyc %lu outputs rtl sovf %d iti %d por %d man %d, model %d %d %d %d", (unsigned long)cyc, d->settle_ovf, d->iti,
       d->rst_req_por, d->rst_req_man, o.settle_ovf, o.iti, o.rst_por, o.rst_man);
  F.ok(d->pr_stall == 0, "stall");
  o_sovf = d->settle_ovf; o_iti = d->iti; o_rpor = d->rst_req_por; o_rman = d->rst_req_man;
  sh4t::Resp r; r.rdata = d->pr_rdata; r.hit = d->pr_hit;
  if (s) F.ok(r.rdata == mr.rdata && r.hit == mr.hit, "cyc %lu strobe %s off %07x size %d: rtl %08x/%d model %08x/%d",
              (unsigned long)cyc, s->wr ? "wr" : "rd", s->off, s->size, r.rdata, r.hit, mr.rdata, mr.hit);
  else F.ok(r.rdata == 0 && !r.hit, "response without a strobe");
  d->clk = 1; d->eval();
  m.step(in);
  pph = (pph + 1) & 3;
  if (!P.freeze) dv = (dv + 1) & 0xFFF;
  cyc++;
  return r;
}
static void idle(uint64_t n) { while (n--) step(); }
static void to_pce() { while (!is_pce()) step(); }
static uint32_t rd(uint32_t off, int size = 0) {
  to_pce(); sh4t::Strobe s; s.rd = true; s.size = size; s.off = off; return step(&s).rdata;
}
static void wr(uint32_t off, int size, uint32_t v) {
  to_pce(); sh4t::Strobe s; s.wr = true; s.size = size; s.off = off; s.wdata = v; step(&s);
}
static void w16(uint32_t off, uint32_t v) { wr(off, 1, v); }

// the cycle (>= c) in which the WDT counts first after TME lands at the end of cycle w: at once when the tap is high
// in cycle w + 1 (the gated clock rises with TME), else at the tap's next rising edge
static uint64_t expect_first(uint64_t w, int cks) {
  const int k = cks + 4;
  auto tap = [&](uint64_t c) { return ((dv + (c - cyc)) >> k) & 1; };   // dv is the divider in cycle `cyc`
  if (tap(w + 1)) return w + 1;
  uint64_t c = w + 2;
  while (!(tap(c) && !tap(c - 1))) c++;
  return c;
}

static void test_rates() {
  F.set("G rates");
  for (int cks = 0; cks < 8; cks++) {
    const uint32_t per = 32u << cks;
    w16(WTCSR, 0xA500 | cks); w16(WTCNT, 0x5A00);
    w16(WTCSR, 0xA580 | cks);
    uint32_t pv = rd(WTCNT); uint64_t pe = 0; int n = 0;
    const uint64_t c0 = cyc;
    while (n < 5 && cyc - c0 < 8 * per) {
      const uint32_t v = rd(WTCNT);
      if (v != pv) {
        if (pe && n >= 1) F.ok(cyc - 1 - pe == per, "CKS %d: counts %lu Icyc apart, expected %u", cks,
                                (unsigned long)(cyc - 1 - pe), per);
        pe = cyc - 1; pv = v; n++;
      }
    }
    F.ok(n == 5, "CKS %d: only %d counts", cks, n);
    w16(WTCSR, 0xA500 | cks);
  }
  // IOVF: 256 counts after the start (less the early first count), WTCSR 0x88 | CKS, the counter runs on (G)
  for (int cks : {0, 3}) {
    w16(WTCSR, 0xA500 | cks); w16(WTCNT, 0x5A00);
    const uint64_t c0 = cyc;
    w16(WTCSR, 0xA580 | cks);
    while (!(rd(WTCSR) & 0x08) && cyc - c0 < 300u * (32u << cks)) {}
    const uint64_t t = cyc - c0;
    F.ok(t >= 255u * (32u << cks) && t <= 256u * (32u << cks) + 8, "CKS %d: IOVF after %lu Icyc", cks, (unsigned long)t);
    F.ok(rd(WTCSR) == (0x88u | cks), "WTCSR %02x after IOVF, expected %02x", rd(WTCSR), 0x88 | cks);
    step(); F.ok(o_iti, "ITI not raised by IOVF");
    const uint32_t a = rd(WTCNT); idle(3 * (32u << cks)); const uint32_t b = rd(WTCNT);
    F.ok(b == a + 3 || b == a + 2 || b == a + 4, "counting stopped after IOVF (%02x -> %02x)", a, b);
    w16(WTCSR, 0xA500 | cks);                                // stop, clear IOVF
    step(); F.ok(!o_iti, "ITI after clearing IOVF");
    const uint32_t h0 = rd(WTCNT); idle(2000); F.ok(rd(WTCNT) == h0, "TME = 0 does not hold WTCNT");
  }
}

static void test_early_count() {
  F.set("R first count");
  for (int cks : {0, 3, 7}) {
    const uint32_t per = 32u << cks;
    const int trials = cks == 0 ? 32 : 10;
    int early = 0, regular = 0;
    for (int t = 0; t < trials; t++) {
      w16(WTCSR, 0xA500 | cks); w16(WTCNT, 0x5A00);
      idle(100 + t * (per / trials + 3));
      to_pce();
      const uint64_t w = cyc;                                // the TME write's strobe cycle
      const uint64_t x = expect_first(w, cks);
      w16(WTCSR, 0xA580 | cks);
      uint32_t v; uint64_t o1;
      do { v = rd(WTCNT); o1 = cyc - 1; } while (v == 0 && cyc - w < 2 * per + 20);
      F.ok(v == 1 && x < o1 && x + 4 >= o1, "CKS %d trial %d: first count seen by the read at +%lu, expected the count "
           "in cycle +%lu", cks, t, (unsigned long)(o1 - w), (unsigned long)(x - w));
      if (x == w + 1) early++; else regular++;
      // the next count comes at the tap's regular edge, less than one period after an early first count
      uint32_t v2; uint64_t o2;
      do { v2 = rd(WTCNT); o2 = cyc - 1; } while (v2 == 1 && cyc - w < 3 * per + 20);
      F.ok(v2 == 2 && (x == w + 1 ? o2 - o1 <= per : o2 - o1 >= per - 4 && o2 - o1 <= per + 4),
           "CKS %d trial %d: second count %lu after the first", cks, t, (unsigned long)(o2 - o1));
      w16(WTCSR, 0xA500 | cks);
    }
    F.ok(early > 0 && regular > 0, "CKS %d: the phase sweep hit only one case (early %d regular %d)", cks, early, regular);
  }
  // WTCNT written while counting (CKS 7): accepted, the next count stays on the regular edge (R)
  w16(WTCSR, 0xA507); w16(WTCNT, 0x5A00); w16(WTCSR, 0xA587);
  for (int t = 0; t < 6; t++) {
    uint32_t v0 = rd(WTCNT), v; uint64_t e0;
    uint64_t lim = cyc + 3 * 4096;                           // bounded: a WDT that never counts fails, not hangs
    do { v = rd(WTCNT); e0 = cyc - 1; } while (v == v0 && cyc < lim);
    idle(100 + t * 500);
    w16(WTCNT, 0x5A10);
    const uint32_t rb = rd(WTCNT); uint64_t ce;
    lim = cyc + 3 * 4096;
    do { v = rd(WTCNT); ce = cyc - 1; } while (v == rb && cyc < lim);
    F.ok(rb == 0x10 && v == 0x11 && ce - e0 == 4096, "WTCNT write while counting: rb %02x next %02x after %lu", rb, v,
         (unsigned long)(ce - e0));
  }
  w16(WTCSR, 0xA507);
}

static void test_keys() {
  F.set("G keys");
  w16(WTCSR, 0xA500); w16(WTCNT, 0x5A00);
  F.ok(rd(WTCSR) == 0 && rd(WTCNT) == 0, "after A500 / 5A00");
  wr(WTCSR, 0, 0x03);         F.ok(rd(WTCSR) == 0, "WTCSR wr8 03");
  wr(WTCSR, 1, 0x0003);       F.ok(rd(WTCSR) == 0, "WTCSR wr16 0003");
  wr(WTCSR, 1, 0x5A03);       F.ok(rd(WTCSR) == 0, "WTCSR wr16 5A03");
  wr(WTCSR, 2, 0x0000A503);   F.ok(rd(WTCSR) == 0, "WTCSR wr32 0000A503");
  wr(WTCSR, 2, 0xA5030000);   F.ok(rd(WTCSR) == 0, "WTCSR wr32 A5030000");
  wr(WTCNT, 1, 0x5A42);       F.ok(rd(WTCNT) == 0x42, "WTCNT wr16 5A42");
  wr(WTCNT, 0, 0x11);         F.ok(rd(WTCNT) == 0x42, "WTCNT wr8 11");
  wr(WTCNT, 1, 0xA522);       F.ok(rd(WTCNT) == 0x42 && rd(WTCSR) == 0, "WTCNT wr16 A522 (WTCSR must stay 00)");
  wr(WTCNT, 1, 0x0033);       F.ok(rd(WTCNT) == 0x42, "WTCNT wr16 0033");
  wr(WTCNT, 2, 0x00005A44);   F.ok(rd(WTCNT) == 0x42, "WTCNT wr32 00005A44");
  F.ok(rd(WTCNT, 1) == 0 && rd(WTCNT, 2) == 0, "WTCNT rd16 / rd32 not 0");
  w16(WTCSR, 0xA508);         F.ok(rd(WTCSR) == 0x08, "WTCSR IOVF written 1 -> %02x", rd(WTCSR));
  w16(WTCSR, 0xA500);         F.ok(rd(WTCSR) == 0x00, "WTCSR IOVF written 0");
  w16(WTCSR, 0xA510);         F.ok(rd(WTCSR) == 0x10, "WTCSR WOVF written 1 (tc4: 10)");
  w16(WTCSR, 0xA500);         F.ok(rd(WTCSR) == 0x00, "WTCSR A500 after WOVF (tc4: 00)");
  w16(WTCSR, 0xA520);         F.ok(rd(WTCSR) == 0x20, "WTCSR RSTS written (tc4: 20)");
  w16(WTCSR, 0xA518);         F.ok(rd(WTCSR) == 0x18, "WTCSR WOVF + IOVF written 1 (tc4: 18)");
  w16(WTCSR, 0xA500);
  for (uint32_t o : {0xFC00009u, 0xFC0000Au, 0xFC0000Du}) rd(o);      // holes (the model checks hit = 0)
  w16(WTCNT, 0x5A00);
}

static void test_watchdog() {
  F.set("watchdog");
  for (int rsts = 0; rsts < 2; rsts++) {
    w16(WTCSR, 0xA540 | (rsts << 5)); w16(WTCNT, 0x5AF8);
    w16(WTCSR, 0xA5C0 | (rsts << 5));                        // TME, WT/IT = 1, CKS 0
    int pulses = 0; uint64_t c0 = cyc; bool seen = false;
    while (cyc - c0 < 400) {
      step();
      if (o_rpor || o_rman) { pulses++; F.ok(rsts ? (o_rman && !o_rpor) : (o_rpor && !o_rman), "reset kind"); seen = true; }
      F.ok(!o_iti, "ITI in watchdog mode");
    }
    F.ok(seen && pulses == 1, "RSTS %d: %d reset request cycles", rsts, pulses);
    F.ok(rd(WTCSR) == (0xD0u | (rsts << 5)), "WTCSR %02x after the overflow", rd(WTCSR));
    // a WDT-caused power-on reset keeps WTCNT / WTCSR (WOVF survives); only the pin clears them
    P.por = true; idle(8); P.por = false; idle(1);
    F.ok((rd(WTCSR) & 0x10) && rd(WTCNT) != 0, "WDT reset cleared the WDT");
    P.man = true; idle(8); P.man = false; P.cpu = true; idle(8); P.cpu = false; idle(1);
    F.ok((rd(WTCSR) & 0x10), "manual / CPU reset cleared WOVF");
    P.pin = true; idle(8); P.pin = false; idle(1);
    F.ok(rd(WTCSR) == 0 && rd(WTCNT) == 0, "the pin reset did not clear the WDT");
  }
}

static void test_settle() {
  F.set("settle");
  w16(WTCSR, 0xA500 | 0); w16(WTCNT, 0x5AF0);             // TME = 0, CKS 0: 16 counts to the overflow
  to_pce();
  const uint64_t c0 = cyc;
  P.settle = true;
  int n = 0; uint64_t o1 = 0, o2 = 0;
  while (cyc - c0 < 2 * 256 * 32 && n < 2) {
    step();
    if (o_sovf) { n++; (n == 1 ? o1 : o2) = cyc - 1; }
  }
  F.ok(n == 2 && o1 - c0 >= 14 * 32 + 17 && o1 - c0 <= 16 * 32 + 1, "settle: first overflow %lu Icyc after the start",
       (unsigned long)(o1 - c0));
  // the second phase restarts from the value software set (0xF0): another 16 counts
  F.ok(n == 2 && o2 - o1 == 16 * 32, "settle: second phase %lu Icyc, expected %u", (unsigned long)(o2 - o1), 16 * 32);
  P.settle = false; step();
  F.ok((rd(WTCSR) & 0x18) == 0, "settle set a flag");
  const uint32_t v = rd(WTCNT); idle(500);
  F.ok(v <= 1 && rd(WTCNT) == v, "the counter after the settle: %02x (00-01, stopped)", v);
}

static void test_random(uint64_t seed, uint64_t ncyc) {
  F.set("random");
  Rng r(seed);
  const uint64_t end = cyc + ncyc;
  while (cyc < end) {
    if (r.chance(1)) P.settle = !P.settle;
    if (r.chance(1)) P.freeze = !P.freeze;
    if (r.below(30000) == 0) { const int k = r.below(4); if (k == 0) P.pin = true; else if (k == 1) P.por = true;
                               else if (k == 2) P.man = true; else P.cpu = true; }
    else if (r.chance(100)) P.pin = P.por = P.man = P.cpu = false;
    if (is_pce() && r.chance(200)) {
      sh4t::Strobe s; s.rd = r.chance(500); s.wr = !s.rd;
      s.off = r.chance(900) ? (r.chance(500) ? WTCNT : WTCSR) : 0xFC00000 + r.below(32);
      s.size = r.chance(800) ? (s.rd ? 0 : 1) : r.below(3);
      uint32_t v = r.u32();
      if (s.wr && r.chance(800)) v = ((s.off == WTCNT ? 0x5Au : 0xA5u) << 8) | (v & 0xFF);
      if (s.wr && s.off == WTCSR && r.chance(700)) v &= ~0x40u | (r.chance(300) ? 0x40u : 0u);
      s.wdata = v;
      step(&s);
    } else step();
  }
  P = Pins();
}

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  const uint64_t seed = arg_u64(argc, argv, "--seed", 1);
  const uint64_t ncyc = arg_u64(argc, argv, "--cycles", 4000000);
  d = new Vv1_wdt_tb;
  P.pin = true; idle(8); P.pin = false; idle(2);
  test_keys();
  test_rates();
  test_early_count();
  test_watchdog();
  test_settle();
  test_random(seed, ncyc);
  d->final();
  delete d;
  return F.finish();
}
