// v1_tmu_tb -- unit test of rtl/v1/v1_tmu.sv (WI-CLK; spec/timers-clock.md 8.1 v1_tmu_tb).
//
// Two kinds of checks run together:
//  * every Icyc the RTL's outputs (tuni, ticpi2, dreq_ic, tclk_out / tclk_oe, next_evt, and rdata / hit / stall of a
//    strobe) are compared with the C++ model tb/sh4_timers.h (sh4t::Tmu), fed the same inputs;
//  * directed tests assert the console's numbers directly, independent of the model: the rates per TPSC (A), the
//    TCOR + 1 period and the D sequences, the start phase (B / Q), writes while running (C / Q), the access sizes and
//    write rules (E / Q), module stop (I), standby, TPSC 6 / 7, input capture and the DMAC request, the interrupt
//    lines, the resets and next_evt against the observed underflow.
// Then a random phase: random register traffic (every offset, holes, every size), random TCLK / RTC / DACK / module
// stop / standby / reset events, and Pclk periods of 2, 3, 4, 6 and 8 Icyc, compared with the model every cycle.
//
//   tb [--seed S] [--cycles N]
#include "Vv1_tmu_tb.h"
#include "verilated.h"
#include "sh4_timers.h"
#include "v1_clk_tbutil.h"
#include <vector>

static Vv1_tmu_tb *d;
static sh4t::Tmu m;
static Fails F("v1-tmu");
static uint64_t cyc = 0;

// harness-held inputs
struct Pins { bool pin = false, por = false, man = false, cpu = false, stby = false, mstp = false, tclk = false,
              rtc = false, dack = false; } P;
static int pper = 4, pph = 0;             // Pclk period in Icyc (4 on the DC) and the phase: pce when pph == pper - 1
// the RTL's outputs in the last stepped cycle (sampled before its clock edge)
static uint32_t o_tuni = 0, o_next = 0; static bool o_ticpi2 = false, o_dreq = false;

enum : uint32_t { TOCR = 0xFD80000, TSTR = 0xFD80004, TCOR0 = 0xFD80008, TCNT0 = 0xFD8000C, TCR0 = 0xFD80010,
                  TCOR1 = 0xFD80014, TCNT1 = 0xFD80018, TCR1 = 0xFD8001C, TCOR2 = 0xFD80020, TCNT2 = 0xFD80024,
                  TCR2 = 0xFD80028, TCPR2 = 0xFD8002C };
static uint32_t TCOR(int n) { return TCOR0 + 12 * n; }
static uint32_t TCNT(int n) { return TCNT0 + 12 * n; }
static uint32_t TCR(int n) { return TCR0 + 12 * n; }

static bool is_pce() { return pph == pper - 1; }

// one Icyc with an optional strobe; returns the RTL's response
static sh4t::Resp step(const sh4t::Strobe *s = nullptr) {
  const bool pce = is_pce(), bce = pce || (pph & 1);
  d->rst_pin = P.pin; d->rst_por = P.por || P.pin; d->rst_man = P.man; d->rst_cpu = P.pin || P.por || P.man || P.cpu;
  d->bce = bce; d->pce = pce; d->stby = P.stby; d->mstp = P.mstp;
  d->tclk_in = P.tclk; d->rtc_tick = P.rtc; d->dack_ic = P.dack;
  d->pq_rd = s && s->rd; d->pq_wr = s && s->wr; d->pq_dma = 0;
  d->pq_size = s ? s->size : 0; d->pq_off = s ? s->off : 0; d->pq_wdata = s ? s->wdata : 0;
  d->clk = 0; d->eval();
  sh4t::Tmu::In in;
  in.rst.pin = P.pin; in.rst.por = P.por || P.pin; in.rst.man = P.man; in.rst.cpu = d->rst_cpu;
  in.ce.bce = bce; in.ce.pce = pce; in.stby = P.stby; in.mstp = P.mstp; in.tclk_in = P.tclk; in.rtc_tick = P.rtc;
  in.dack_ic = P.dack;
  m.mstp_now = P.mstp;
  const sh4t::Tmu::Out o = m.out(in);
  sh4t::Resp mr; if (s) mr = m.strobe(*s);
  F.ok(d->tuni == o.tuni, "cyc %lu tuni rtl %x model %x", (unsigned long)cyc, d->tuni, o.tuni);
  F.ok(d->ticpi2 == o.ticpi2, "cyc %lu ticpi2 rtl %d model %d", (unsigned long)cyc, d->ticpi2, o.ticpi2);
  F.ok(d->dreq_ic == o.dreq_ic, "cyc %lu dreq_ic rtl %d model %d", (unsigned long)cyc, d->dreq_ic, o.dreq_ic);
  F.ok(d->tclk_out == o.tclk_out && d->tclk_oe == o.tclk_oe, "cyc %lu tclk rtl %d/%d model %d/%d", (unsigned long)cyc,
       d->tclk_out, d->tclk_oe, o.tclk_out, o.tclk_oe);
  F.ok(d->next_evt == o.next_evt, "cyc %lu next_evt rtl %u model %u", (unsigned long)cyc, d->next_evt, o.next_evt);
  F.ok(d->pr_stall == 0, "cyc %lu stall", (unsigned long)cyc);
  o_tuni = d->tuni; o_next = d->next_evt; o_ticpi2 = d->ticpi2; o_dreq = d->dreq_ic;
  sh4t::Resp r; r.rdata = d->pr_rdata; r.hit = d->pr_hit;
  if (s) F.ok(r.rdata == mr.rdata && r.hit == mr.hit, "cyc %lu strobe %s off %07x size %d: rtl %08x/%d model %08x/%d",
              (unsigned long)cyc, s->wr ? "wr" : "rd", s->off, s->size, r.rdata, r.hit, mr.rdata, mr.hit);
  else F.ok(r.rdata == 0 && !r.hit, "cyc %lu response without a strobe", (unsigned long)cyc);
  d->clk = 1; d->eval();
  m.step(in);
  pph = (pph + 1) % pper;
  cyc++;
  P.rtc = false; P.dack = false;           // pulses
  return r;
}
static void idle(uint64_t n) { while (n--) step(); }
static void to_pce() { while (!is_pce()) step(); }
static uint32_t rd(uint32_t off, int size) {
  to_pce();
  sh4t::Strobe s; s.rd = true; s.size = size; s.off = off;
  return step(&s).rdata;
}
static void wr(uint32_t off, int size, uint32_t v) {
  to_pce();
  sh4t::Strobe s; s.wr = true; s.size = size; s.off = off; s.wdata = v;
  step(&s);
}
static void por() { P.por = true; idle(8); P.por = false; idle(1); }
// TMU1-style setup as tc.c's t1_setup: stop the channel, then TCR, TCOR, TCNT
static void setup(int n, uint16_t tcr, uint32_t tcor, uint32_t tcnt) {
  wr(TSTR, 0, rd(TSTR, 0) & ~(1u << n));
  wr(TCR(n), 1, tcr); wr(TCOR(n), 2, tcor); wr(TCNT(n), 2, tcnt);
}
static void start(int n) { wr(TSTR, 0, rd(TSTR, 0) | (1u << n)); }
static void stop(int n) { wr(TSTR, 0, rd(TSTR, 0) & ~(1u << n)); }

// read TCNTn at every pce until it changes; returns the cycle of the read that saw the change (0 on timeout)
static uint64_t wait_change(int n, uint32_t v, uint32_t *nv, uint64_t lim) {
  const uint64_t c0 = cyc;
  for (;;) {
    const uint32_t x = rd(TCNT(n), 2);
    if (x != v) { *nv = x; return cyc - 1; }
    if (cyc - c0 > lim) { *nv = v; return 0; }
  }
}

// ---- A: rates per TPSC -------------------------------------------------------------------------------------------
static void test_rates() {
  F.set("A rates");
  static const uint32_t per[5] = {16, 64, 256, 1024, 4096};
  for (int t = 0; t < 8; t++) {
    setup(1, t, 0xFFFFFFFF, 0xFFFFFFFF);
    start(1);
    if (t >= 5) {                                            // reserved / RTC clock / TCLK: never count on the DC
      const uint32_t a = rd(TCNT1, 2); idle(20000); const uint32_t b = rd(TCNT1, 2);
      F.ok(a == 0xFFFFFFFF && b == a, "tpsc %d counted %08x -> %08x", t, a, b);
      stop(1);
      continue;
    }
    uint32_t v0, v1;
    const uint64_t e0 = wait_change(1, rd(TCNT1, 2), &v0, 3 * per[t]);
    uint64_t e = e0; uint32_t v = v0;
    for (int k = 0; k < 6; k++) {
      const uint64_t e1 = wait_change(1, v, &v1, 3 * per[t]);
      F.ok(e1 - e == per[t] && v1 == v - 1, "tpsc %d: count %d after %lu Icyc (%08x -> %08x), expected %u", t, k,
           (unsigned long)(e1 - e), v, v1, per[t]);
      e = e1; v = v1;
    }
    stop(1);
  }
}

// ---- D: the count sequence through an underflow; B: a start never counts at once ---------------------------------
static void test_underflow_seq() {
  F.set("D sequences");
  struct Case { uint32_t tcor, tcnt; std::vector<uint32_t> seq; uint64_t window; } cs[3] = {
    {2, 2, {1, 0, 2, 1, 0, 2, 1, 0, 2, 1, 0, 2}, 60000},
    {0, 0, {}, 25000},
    {3, 1, {0, 3, 2, 1, 0, 3, 2}, 30000},
  };
  for (auto &c : cs) {
    setup(1, 4, c.tcor, c.tcnt);                             // Pphi/1024: 4096 Icyc per tick, as run 1
    start(1);
    // every change of (TCNT1, TCR1.UNF), sampled as the console did: TCNT then TCR, back to back
    std::vector<uint32_t> vs; std::vector<uint64_t> at; uint64_t unf_at = 0;
    uint32_t pv = rd(TCNT1, 2); bool pu = rd(TCR1, 1) & 0x100;
    F.ok(pv == c.tcnt && !pu, "TCOR %u: right after the start TCNT %u UNF %d", c.tcor, pv, pu);
    const uint64_t c0 = cyc;
    while (cyc - c0 < c.window) {
      const uint32_t v = rd(TCNT1, 2); const uint64_t tv = cyc - 1;
      const bool u = rd(TCR1, 1) & 0x100;
      if (v != pv) { vs.push_back(v); at.push_back(tv); pv = v; }
      if (u && !pu) unf_at = tv;
      pu = u;
    }
    stop(1);
    if (c.tcor == 0) {                                       // TCOR 0 TCNT 0: TCNT stays 0, UNF at the first tick
      F.ok(vs.empty() && unf_at && unf_at - c0 <= 4096 + 16, "TCOR 0: changes %zu UNF at %lu", vs.size(),
           (unsigned long)(unf_at - c0));
      continue;
    }
    F.ok(vs.size() >= c.seq.size(), "TCOR %u: %zu changes, expected %zu", c.tcor, vs.size(), c.seq.size());
    for (size_t i = 0; i < c.seq.size() && i < vs.size(); i++) {
      F.ok(vs[i] == c.seq[i], "TCOR %u TCNT %u change %zu: %u, expected %u", c.tcor, c.tcnt, i, vs[i], c.seq[i]);
      if (i) F.ok(at[i] - at[i - 1] == 4096, "TCOR %u change %zu came %lu Icyc after the previous (one tick = 4096)",
                  c.tcor, i, (unsigned long)(at[i] - at[i - 1]));
    }
    // UNF is set by the tick that reloads TCNT (0 -> TCOR).  Each loop pass reads TCNT, then TCR 4 Icyc later, and
    // passes are 8 Icyc apart: the tick lands either before a pass (UNF and the reload seen in the same pass) or
    // between its two reads (UNF seen one pass before the reload: TCNT 0 with UNF, as the console's "(9168 0 0104)").
    size_t first = 0; while (first < vs.size() && !(vs[first] == c.tcor && (first ? vs[first - 1] : c.tcnt) == 0)) first++;
    F.ok(first < vs.size() && (unf_at == at[first] || unf_at + 8 == at[first]),
         "TCOR %u: UNF first seen in the pass at %lu, the reload in the pass at %lu", c.tcor, (unsigned long)unf_at,
         (unsigned long)(first < at.size() ? at[first] : 0));
  }
  // B / Q: the first decrement after a start falls on the free-running prescaler phase, whatever the start phase
  F.set("B start phase");
  for (int t : {2, 4}) {
    const uint32_t per = t == 2 ? 256 : 4096;
    int64_t phase0 = -1;
    for (int k = 0; k < 8; k++) {
      setup(1, t, 0xFFFFFFFF, 1000);
      idle(100 + k * (per / 8 + 5));
      to_pce();
      const uint64_t st = cyc;
      start(1);                                              // the start strobe is this pce cycle
      F.ok(rd(TCNT1, 2) == 1000, "tpsc %d trial %d: read right after the start is not the value written", t, k);
      uint32_t v; const uint64_t e1 = wait_change(1, 1000, &v, 2 * per);
      F.ok(v == 999 && e1 > st, "tpsc %d trial %d: first change %u", t, k, v);
      F.ok(e1 - st <= per + 4, "tpsc %d trial %d: first decrement %lu Icyc after the start (more than one period)",
           t, k, (unsigned long)(e1 - st));
      const int64_t ph = (int64_t)(e1 % per);
      if (phase0 < 0) phase0 = ph;
      F.ok(ph == phase0, "tpsc %d trial %d: first decrement at phase %ld, the others at %ld (prescaler restarted?)", t,
           k, (long)ph, (long)phase0);
    }
    stop(1);
  }
}

// ---- UNF: an underflow wins over a UNF-clearing TCR write in the same Pcyc (spec 7.4) ------------------------------------
// KOS clears UNF with a read-modify-write of TCR while the channel runs: a clear that lands in the underflow's pce must
// not lose that underflow (a lost timer interrupt).  TPSC 0 ticks in the pce whose prescaler value ends in 3, so with
// TCOR0 = 3, TCNT0 = 0 and the start written in a pce ending in 1, the underflow (reload to 3, UNF = 1) is the second
// pce after the start (U).  TCR0 = 0x0020 (UNIE, UNF = 0) is written in pce U - 1, U or U + 1: UNF must read 1, 1, 0
// in pce U + 2.
static void to_presc(unsigned v) { for (;;) { to_pce(); if ((m.presc & 3u) == v) return; step(); } }
static void test_unf_race() {
  F.set("UNF set vs clear");
  static const unsigned at[3] = {2, 3, 0};                  // the prescaler value (mod 4) of the clearing pce
  static const uint32_t want[3] = {0x0120, 0x0120, 0x0020};
  for (int k = 0; k < 3; k++) {
    setup(0, 0x0020, 3, 0);
    to_presc(1); wr(TSTR, 0, 1);                             // start in a pce with presc & 3 == 1
    to_presc(at[k]);
    const uint32_t tc0 = m.tcnt[0];                          // TCNT0 before the clearing pce: 0, 0, 3 (reloaded)
    wr(TCR0, 1, 0x0020);
    to_presc(1);                                             // read in pce U + 2 and U + 3 (the next tick is U + 4)
    const uint32_t tcr = rd(TCR0, 1), cnt = rd(TCNT0, 2);
    F.ok(tcr == want[k] && tc0 == (k < 2 ? 0u : 3u) && cnt == 3, "UNF clear in pce U%+d: TCR0 %04x (expected %04x), TCNT0 "
         "%u before the write and %u after (the underflow reloads 3 in pce U)", k - 1, tcr, want[k], tc0, cnt);
    stop(0); wr(TCR0, 1, 0);
  }
}

// ---- C / Q: writes while running -----------------------------------------------------------------------------------
static void test_writes_running() {
  F.set("C/Q writes while running");
  for (int t : {0, 4}) {
    setup(1, t, 0xFFFFFFFF, 0xFFFFFFFF);
    start(1);
    idle(1000);
    const uint32_t before = rd(TCNT1, 2);
    wr(TCNT1, 2, 5000);
    const uint32_t rb = rd(TCNT1, 2);
    F.ok(rb != 5000 && rb <= before && before - rb <= 1, "tpsc %d: TCNT write while running was taken (%u)", t, rb);
    stop(1);
    F.ok(rd(TCNT1, 2) != 5000, "tpsc %d: the write landed after the stop", t);
  }
  // TCOR written while running is used by the next reload (Q: TCOR1 1000 -> TCNT1 790 after 8000 Icyc)
  setup(1, 0, 0xFFFFFFFF, 300);
  start(1);
  idle(100);
  wr(TCOR1, 2, 1000);
  F.ok(rd(TCOR1, 2) == 1000, "TCOR read back");
  idle(8000);
  const uint32_t after = rd(TCNT1, 2);
  F.ok((rd(TCR1, 1) & 0x100) && after < 1000 && after > 700, "TCOR used at the next underflow: TCNT1 %u", after);
  stop(1);
  // TPSC 0 -> 4 while running: the edges come at the new rate
  setup(1, 0, 0xFFFFFFFF, 0xFFFFFFFF);
  start(1);
  idle(500);
  wr(TCR1, 1, 4);
  uint32_t v0, v1, v2;
  const uint64_t e0 = wait_change(1, rd(TCNT1, 2), &v0, 20000);
  const uint64_t e1 = wait_change(1, v0, &v1, 20000);
  const uint64_t e2 = wait_change(1, v1, &v2, 20000);
  F.ok(e1 - e0 == 4096 && e2 - e1 == 4096, "TPSC change: edges %lu and %lu apart", (unsigned long)(e1 - e0),
       (unsigned long)(e2 - e1));
  stop(1);
  // UNF cleared while running; UNIE written
  setup(1, 0, 0xFFFFFFFF, 10);
  start(1);
  idle(400);
  F.ok(rd(TCR1, 1) == 0x0100, "UNF after the underflow");
  wr(TCR1, 1, 0); F.ok(rd(TCR1, 1) == 0, "UNF clear while running");
  wr(TCR1, 1, 0x20); F.ok(rd(TCR1, 1) == 0x20, "UNIE write");
  wr(TCR1, 1, 0);
  stop(1);
  test_unf_race();
}

// ---- E / Q: access sizes and write rules ----------------------------------------------------------------------------
static void test_widths() {
  F.set("E sizes");
  setup(1, 0, 0xFFFFFFFF, 0x12345678);
  F.ok(rd(TCNT1, 2) == 0x12345678, "rd32");
  F.ok(rd(TCNT1, 1) == 0 && rd(TCNT1 + 2, 1) == 0, "rd16 of TCNT1 not 0");
  for (int b = 0; b < 4; b++) F.ok(rd(TCNT1 + b, 0) == 0, "rd8@%d of TCNT1 not 0", b);
  wr(TCNT1, 1, 0xAAAA); F.ok(rd(TCNT1, 2) == 0x12345678, "wr16 to TCNT1 taken");
  wr(TCNT1 + 3, 0, 0xBB); F.ok(rd(TCNT1, 2) == 0x12345678, "wr8 to TCNT1 taken");
  wr(TCOR1, 2, 0x87654321); F.ok(rd(TCOR1, 1) == 0 && rd(TCOR1 + 2, 1) == 0, "rd16 of TCOR1 not 0");
  wr(TCR1, 1, 0xFFFF); F.ok(rd(TCR1, 1) == 0x003F, "TCR1 wr FFFF -> %04x, expected 003f", rd(TCR1, 1));
  wr(TCR1, 1, 0); F.ok(rd(TCR1, 1) == 0, "TCR1 wr 0");
  F.ok(rd(TCR1, 2) == 0 && rd(TCR1, 0) == 0, "TCR1 rd32 / rd8");
  wr(TCR2, 1, 0xFFFF); F.ok(rd(TCR2, 1) == 0x00FF, "TCR2 wr FFFF -> %04x, expected 00ff (ICPF / UNF: 1 no effect)",
                            rd(TCR2, 1));
  wr(TCR2, 1, 0);
  // UNF: set by a real underflow, a written 1 has no effect, a written 0 clears
  setup(1, 0, 0xFFFFFFFF, 0);
  start(1); idle(200); stop(1);
  const uint32_t u0 = rd(TCR1, 1);
  wr(TCR1, 1, 0x100); const uint32_t u1 = rd(TCR1, 1);
  wr(TCR1, 1, 0); const uint32_t u2 = rd(TCR1, 1);
  wr(TCR1, 1, 0x100); const uint32_t u3 = rd(TCR1, 1);
  F.ok(u0 == 0x100 && u1 == 0x100 && u2 == 0 && u3 == 0, "UNF rules: %04x %04x %04x %04x, expected 0100 0100 0000 0000",
       u0, u1, u2, u3);
  wr(TOCR, 0, 0xFE); F.ok(rd(TOCR, 0) == 0, "TOCR wr FE");
  wr(TSTR, 0, 0x05); wr(TSTR, 0, 0x05 | 0xF8); F.ok(rd(TSTR, 0) == 0x05, "TSTR |F8 -> %02x", rd(TSTR, 0));
  F.ok(rd(TSTR, 1) == 0, "TSTR rd16");
  wr(TSTR, 0, 0);
  wr(TCR1, 1, 0x25);
  F.ok(rd(TCR1, 1) == 0x25 && rd(TCR1, 2) == 0 && rd(TCR1, 0) == 0 && rd(TCR1 + 1, 0) == 0, "TCR1=0025 sizes (Q)");
  wr(TCR1, 1, 0);
  // holes: hit = 0 (checked against the model in step()); a write to TCPR2 is ignored
  const uint32_t cp = rd(TCPR2, 2); wr(TCPR2, 2, ~cp); F.ok(rd(TCPR2, 2) == cp, "TCPR2 is read-only");
  for (uint32_t o : {0xFD80030u, 0xFD80040u, 0xFD80100u, 0xFD80001u}) { rd(o, 0); rd(o, 2); }
}

// ---- I: module stop; standby; the RTC clock ---------------------------------------------------------------------------
static void test_mstp_stby() {
  F.set("I module stop");
  setup(0, 0x20, 0x0001E70F, 0x0001E70F);                  // KOS's TMU0 / TMU2 setup, then TMU1 at Pphi/4
  setup(2, 0x20, 0x00BE41F0, 0x00BE41F0);
  setup(1, 0, 0xFFFFFFFF, 0xFFFFFFFF);
  wr(TSTR, 0, 0x07);
  idle(1000);
  const uint32_t n0 = rd(TCNT1, 2), m0 = rd(TCNT2, 2);
  to_pce(); P.mstp = true;                                 // STBCR.MSTP2 = 1 (the CPG's write lands at a pce)
  F.ok(rd(TSTR, 0) == 0 && rd(TCNT1, 2) == 0, "registers read 0 while MSTP2 [P]");
  wr(TCOR1, 2, 1234);                                       // ignored [P]
  idle(160000);
  to_pce(); P.mstp = false;
  const uint32_t n1 = rd(TCNT1, 2), m1 = rd(TCNT2, 2);
  F.ok(rd(TSTR, 0) == 0, "TSTR 07 -> %02x after MSTP2, expected 00", rd(TSTR, 0));
  F.ok(n0 - n1 <= 4 && m0 - m1 <= 1, "counters kept counting in MSTP2: TCNT1 -%u TCNT2 -%u", n0 - n1, m0 - m1);
  F.ok(rd(TCR0, 1) == 0x20 && rd(TCR2, 1) == 0x20 && rd(TCOR0, 2) == 0x0001E70F && rd(TCOR2, 2) == 0x00BE41F0 &&
       rd(TOCR, 0) == 0 && rd(TCOR1, 2) == 0xFFFFFFFF, "TCR / TCOR / TOCR changed by MSTP2");
  idle(1000);
  F.ok(rd(TSTR, 0) == 0 && rd(TCNT1, 2) == n1, "TSTR stays 0 after MSTP2 is cleared");
  // a channel on the RTC clock keeps its STR and counts in module stop and in standby [M]
  F.set("RTC clock");
  setup(1, 6, 0xFFFFFFFF, 100);
  setup(0, 0, 0xFFFFFFFF, 100);
  wr(TSTR, 0, 0x03);
  for (int k = 0; k < 5; k++) { idle(37); P.rtc = true; step(); }
  idle(8);
  F.ok(rd(TCNT1, 2) == 95, "TPSC 6: 5 RTC ticks -> TCNT1 %u, expected 95", rd(TCNT1, 2));
  to_pce(); P.mstp = true;
  for (int k = 0; k < 5; k++) { idle(37); P.rtc = true; step(); }
  idle(8);
  to_pce(); P.mstp = false;
  F.ok(rd(TSTR, 0) == 0x02 && rd(TCNT1, 2) == 90, "MSTP2 with TPSC 6: TSTR %02x TCNT1 %u, expected 02 90",
       rd(TSTR, 0), rd(TCNT1, 2));
  F.set("standby");
  wr(TSTR, 0, 0x03);
  const uint32_t c0 = rd(TCNT0, 2);
  P.stby = true;                                           // the CPG stops pce in standby: model it here
  const int sp = pper; pper = 1 << 30; pph = 0;
  for (int k = 0; k < 7; k++) { idle(29); P.rtc = true; step(); }
  idle(5);
  P.stby = false; pper = sp; pph = 0;
  F.ok(rd(TSTR, 0) == 0x02, "standby: TSTR %02x, expected 02 (TPSC 6 kept)", rd(TSTR, 0));
  F.ok(rd(TCNT1, 2) == 83 && c0 - rd(TCNT0, 2) <= 1, "standby: TCNT1 %u (expected 83) TCNT0 %u (held %u)", rd(TCNT1, 2),
       rd(TCNT0, 2), c0);
  wr(TSTR, 0, 0);
}

// ---- TCLK counting, input capture, the DMAC request; interrupt lines -------------------------------------------------
static void tclk_edge(bool v) { P.tclk = v; idle(3 * pper + 1); }
static void test_tclk_capture() {
  F.set("TCLK");
  for (int ck = 0; ck < 3; ck++) {
    setup(1, 7 | (ck << 3), 0xFFFFFFFF, 1000);
    P.tclk = false; idle(20);
    start(1);
    for (int k = 0; k < 4; k++) { tclk_edge(true); tclk_edge(false); }
    const uint32_t v = rd(TCNT1, 2);
    const uint32_t exp = ck == 2 ? 992 : 996;
    F.ok(v == exp, "TPSC 7 CKEG %d: 4 pulses -> TCNT1 %u, expected %u", ck, v, exp);
    stop(1);
  }
  // TCOE = 1: the pin is the RTC clock output; no TCLK counting
  wr(TOCR, 0, 1);
  setup(1, 7, 0xFFFFFFFF, 1000); start(1);
  for (int k = 0; k < 4; k++) { tclk_edge(true); tclk_edge(false); }
  F.ok(rd(TCNT1, 2) == 1000, "TCOE = 1 still counts TCLK");
  for (int k = 0; k < 3; k++) { P.rtc = true; step(); idle(10); }
  stop(1); wr(TOCR, 0, 0);
  F.set("input capture");
  setup(2, 0x00C0 | 0x20 | 0, 0xFFFFFFFF, 0x00100000);     // ICPE 11, UNIE, rising edge, Pphi/4
  start(2);
  idle(100);
  tclk_edge(true);
  const uint32_t cap1 = rd(TCPR2, 2);
  F.ok((rd(TCR2, 1) & 0x200) && cap1 < 0x00100000 && cap1 > 0x00100000 - 20, "capture: TCPR2 %08x ICPF %d", cap1,
       (rd(TCR2, 1) >> 9) & 1);
  F.ok(o_ticpi2 && o_dreq, "TICPI2 / DMAC request not raised");
  tclk_edge(false); idle(200); tclk_edge(true);
  F.ok(rd(TCPR2, 2) == cap1, "TCPR2 overwritten while ICPF = 1");
  P.dack = true; step(); idle(4);
  F.ok(!o_dreq, "dack_ic did not drop the request");
  wr(TCR2, 1, 0x00C0 | 0x20);                               // clear ICPF
  step();
  F.ok(!o_ticpi2, "TICPI2 after clearing ICPF");
  tclk_edge(false); idle(50); tclk_edge(true);
  F.ok(rd(TCPR2, 2) != cap1 && o_dreq, "second capture");
  wr(TCR2, 1, 0x0080);                                      // ICPE 10: capture, no interrupt
  step();
  F.ok(!o_ticpi2, "ICPE 10 raises TICPI2");
  P.dack = true; step(); idle(4);
  wr(TCR2, 1, 0); stop(2);
  F.set("interrupt lines");
  setup(0, 0x20, 3, 3); start(0);
  idle(80);
  F.ok(o_tuni == 1, "TUNI0 not raised (tuni %x)", o_tuni);
  to_pce(); P.mstp = true; step(); F.ok(o_tuni == 0, "TUNI0 while module-stopped");
  to_pce(); P.mstp = false;
  wr(TCR0, 1, 0); step(); F.ok(o_tuni == 0, "TUNI0 after clearing UNF");
  stop(0);
}

// ---- resets ---------------------------------------------------------------------------------------------------------
static void test_resets() {
  F.set("resets");
  setup(0, 0x2C, 0x55, 0x66); wr(TOCR, 0, 1); wr(TSTR, 0, 1);
  const uint32_t cp = rd(TCPR2, 2);
  P.cpu = true; idle(8); P.cpu = false; idle(1);
  F.ok(rd(TCR0, 1) == 0x2C && rd(TCOR0, 2) == 0x55 && rd(TSTR, 0) == 1 && rd(TOCR, 0) == 1, "CPU-only reset changed the TMU");
  for (int k = 0; k < 2; k++) {
    if (k == 0) { P.man = true; idle(8); P.man = false; idle(1); } else por();
    F.ok(rd(TCR0, 1) == 0 && rd(TCOR0, 2) == 0xFFFFFFFF && rd(TCNT0, 2) == 0xFFFFFFFF && rd(TSTR, 0) == 0 &&
         rd(TOCR, 0) == 0, "%s reset did not initialise the TMU", k ? "power-on" : "manual");
    F.ok(rd(TCPR2, 2) == cp, "TCPR2 changed by a reset");
    setup(0, 0x2C, 0x55, 0x66); wr(TOCR, 0, 1); wr(TSTR, 0, 1);
  }
  por();
}

// ---- next_evt against the observed underflow ---------------------------------------------------------------------------
static void test_next_evt() {
  F.set("next_evt");
  Rng r(7);
  int done = 0;
  for (int k = 0; k < 60; k++) {
    const int t = r.below(3);
    setup(1, 0x20 | t, 0xFFFFFFFF, r.below(12));
    start(1);
    idle(r.below(200));
    step();
    if (o_tuni) { stop(1); wr(TCR1, 1, 0); continue; }       // it already underflowed
    const uint64_t c = cyc - 1; const uint32_t ne = o_next;
    do step(); while (!o_tuni && cyc - c < 100000);
    // tuni is UNF & UNIE: it shows in the cycle after the underflow's edge, so the underflow was cycle c + ne
    F.ok(cyc - 1 == c + ne + 1, "trial %d tpsc %d: next_evt %u, the underflow came %lu Icyc later", k, t, ne,
         (unsigned long)(cyc - 2 - c));
    done++;
    stop(1); wr(TCR1, 1, 0);
  }
  F.ok(done >= 30, "only %d next_evt trials ran", done);
  step();
  F.ok(o_next == 0xFFFFFFFF, "next_evt with nothing running: %u", o_next);
}

// ---- random traffic vs the model --------------------------------------------------------------------------------------
static void test_random(uint64_t seed, uint64_t ncyc) {
  F.set("random");
  Rng r(seed);
  static const int pers[5] = {4, 2, 3, 6, 8};
  const uint64_t end = cyc + ncyc;
  int seg = 0;
  while (cyc < end) {
    pper = pers[seg++ % 5]; pph = 0;
    const uint64_t send = cyc + 20000 + r.below(20000);
    while (cyc < send) {
      if (r.chance(2)) P.tclk = !P.tclk;
      if (r.chance(20)) P.rtc = true;
      if (r.chance(5)) P.dack = true;
      if (r.chance(1) && is_pce()) P.mstp = !P.mstp;
      if (r.chance(1)) P.stby = !P.stby;
      if (r.below(20000) == 0) { const int k = r.below(3); if (k == 0) P.por = true; else if (k == 1) P.man = true; else P.cpu = true; }
      else if (r.chance(100)) P.por = P.man = P.cpu = false;
      if (is_pce() && r.chance(300)) {
        sh4t::Strobe s;
        s.rd = r.chance(500); s.wr = !s.rd;
        const int k = r.below(13);
        s.off = 0xFD80000 + (k < 12 ? 4 * k : (r.below(64) & ~0u));
        const int own = k <= 1 ? 0 : (k == 4 || k == 7 || k == 10) ? 1 : 2;
        s.size = r.chance(850) ? own : r.below(3);
        uint32_t v = r.u32();
        if (k == 1) v &= r.chance(500) ? 7 : 0xFF;
        if (k == 3 || k == 6 || k == 9 || k == 2 || k == 5 || k == 8) v = r.chance(700) ? r.below(40) : v;
        if (k == 4 || k == 7 || k == 10) v = r.chance(800) ? ((v & 0x3F8) | r.below(3)) : (v & 0xFFFF);
        s.wdata = v;
        step(&s);
      } else step();
    }
  }
  P = Pins(); pper = 4; pph = 0;
  por();
}

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  const uint64_t seed = arg_u64(argc, argv, "--seed", 1);
  const uint64_t ncyc = arg_u64(argc, argv, "--cycles", 4000000);
  d = new Vv1_tmu_tb;
  P.pin = true; idle(8); P.pin = false; idle(2);
  test_rates();
  test_underflow_seq();
  test_writes_running();
  test_widths();
  test_mstp_stby();
  test_tclk_capture();
  test_resets();
  test_next_evt();
  test_random(seed, ncyc);
  d->final();
  delete d;
  return F.finish();
}
