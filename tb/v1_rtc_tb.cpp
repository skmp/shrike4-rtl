// v1_rtc_tb -- unit test of rtl/v1/v1_rtc.sv (WI-CLK; spec/timers-clock.md 8.1 v1_rtc_tb).
//
// Every Icyc the RTL's outputs (rtc_tick, ati, pri, cui, and rdata / hit of a strobe) are compared with sh4t::Rtc fed
// the same inputs.  Directed tests assert the console's facts independently of the model:
//   * every tc3 case (the calendar through 30-second adjustments, ADJ with START = 0, the alarm comparator), printed
//     in tc3.c's format and compared with the console's lines verbatim; likewise every tc4 case (the RCR2.RTCEN gate of
//     the alarm comparator, the day-overflow rule, out-of-range / invalid-BCD fields, ADJ with the START it writes);
//   * the P / H lines of tc2 / tc (write masks, RCR1 / RCR2 flag rules, ADJ, RESET, PES without an oscillator, module
//     stop), verbatim;
//   * the Gregorian rule for every BCD year 0000-9999 (02-28 -> 02-29 iff leap, 02-29 -> 03-01) and every month end
//     of 1900-2100, 9999-12-31 -> 0000-01-01;
//   * with a (test-only) 32.768 kHz tick: rtc_tick every 2nd tick, R64CNT every 256 ticks, the seconds every 32768
//     with CF, START = 0 / RTCEN = 0, PEF at every PES period, CF on an R64CNT read in the Pcyc of a 128 Hz carry,
//     ADJ / RESET restarting the divider, module stop (counters run, registers read 0), the request lines, the resets.
// Then random traffic with a dense oscillator tick against the model.
//
//   tb [--seed S] [--cycles N]
#include "Vv1_rtc_tb.h"
#include "verilated.h"
#include "sh4_timers.h"
#include "v1_clk_tbutil.h"
#include <string>

static Vv1_rtc_tb *d;
static sh4t::Rtc m;
static Fails F("v1-rtc");
static uint64_t cyc = 0;

struct Pins { bool pin = false, por = false, man = false, cpu = false, osc = false, mstp = false; } P;
static int pph = 0;
static bool o_tick = false, o_ati = false, o_pri = false, o_cui = false;

static uint32_t R(int o) { return 0xFC80000 + o; }
static bool is_pce() { return pph == 3; }
static sh4t::Resp step(const sh4t::Strobe *s = nullptr) {
  const bool pce = is_pce(), bce = pce || (pph & 1);
  d->rst_pin = P.pin; d->rst_por = P.por || P.pin; d->rst_man = P.man; d->rst_cpu = P.pin || P.por || P.man || P.cpu;
  d->bce = bce; d->pce = pce; d->osc_tick = P.osc; d->mstp = P.mstp;
  d->pq_rd = s && s->rd; d->pq_wr = s && s->wr; d->pq_dma = 0;
  d->pq_size = s ? s->size : 0; d->pq_off = s ? s->off : 0; d->pq_wdata = s ? s->wdata : 0;
  d->clk = 0; d->eval();
  sh4t::Rtc::In in;
  in.rst.pin = P.pin; in.rst.por = P.por || P.pin; in.rst.man = P.man; in.rst.cpu = d->rst_cpu;
  in.ce.bce = bce; in.ce.pce = pce; in.osc = P.osc; in.mstp = P.mstp;
  m.mstp_now = P.mstp;
  const sh4t::Rtc::Out o = m.out(in);
  sh4t::Resp mr; if (s) mr = m.strobe(*s);
  F.ok(d->rtc_tick == o.rtc_tick && d->ati == o.ati && d->pri == o.pri && d->cui == o.cui,
       "cyc %lu outputs rtl tick %d ati %d pri %d cui %d, model %d %d %d %d", (unsigned long)cyc, d->rtc_tick, d->ati,
       d->pri, d->cui, o.rtc_tick, o.ati, o.pri, o.cui);
  F.ok(d->pr_stall == 0, "stall");
  o_tick = d->rtc_tick; o_ati = d->ati; o_pri = d->pri; o_cui = d->cui;
  sh4t::Resp r; r.rdata = d->pr_rdata; r.hit = d->pr_hit;
  if (s) F.ok(r.rdata == mr.rdata && r.hit == mr.hit, "cyc %lu strobe %s off %07x size %d: rtl %08x/%d model %08x/%d",
              (unsigned long)cyc, s->wr ? "wr" : "rd", s->off, s->size, r.rdata, r.hit, mr.rdata, mr.hit);
  else F.ok(r.rdata == 0 && !r.hit, "response without a strobe");
  d->clk = 1; d->eval();
  m.step(in);
  pph = (pph + 1) & 3;
  cyc++;
  P.osc = false;
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
static void w8(int o, uint32_t v) { wr(R(o), 0, v); }
static uint32_t r8(int o) { return rd(R(o), 0); }
static uint32_t bcd_next(uint32_t s) { return s >= 0x59 ? 0 : sh4t::bcd_inc2(s); }
static void osc(uint64_t n, int gap = 2) { while (n--) { P.osc = true; step(); idle(gap - 1); } }

// tc3.c
static void set_time(uint16_t yr, uint8_t mon, uint8_t day, uint8_t wk, uint8_t hr, uint8_t min, uint8_t sec) {
  w8(0x3C, 0x08);
  wr(R(0x1C), 1, yr); w8(0x18, mon); w8(0x14, day); w8(0x10, wk); w8(0xC, hr); w8(8, min); w8(4, sec);
}
static std::string show(const char *t) {
  char b[160];
  const uint32_t yr = rd(R(0x1C), 1), mon = r8(0x18), day = r8(0x14), wk = r8(0x10), hr = r8(0xC), mi = r8(8), se = r8(4),
                 r64 = r8(0), c1 = r8(0x38), c2 = r8(0x3C);
  snprintf(b, sizeof b, "%-34s %04x-%02x-%02x wk%x %02x:%02x:%02x R64 %02x RCR1 %02x RCR2 %02x", t, yr, mon, day, wk, hr, mi,
           se, r64, c1, c2);
  return b;
}
static std::string adj_case(const char *t, uint16_t yr, uint8_t mon, uint8_t day, uint8_t wk, uint8_t hr, uint8_t min,
                            uint8_t sec) {
  set_time(yr, mon, day, wk, hr, min, sec);
  w8(0x38, 0x00); w8(0x3C, 0x09); w8(0x3C, 0x0D);
  idle(200);
  return show(t);
}
static void line(const std::string &got, const char *exp) {
  F.ok(got == exp, "\n      got      '%s'\n      console  '%s'", got.c_str(), exp);
}

static void test_tc3() {
  F.set("tc3");
  for (int i = 0; i < 6; i++) w8(0x20 + 4 * i, 0);
  struct C { const char *t; uint16_t y; uint8_t mo, da, wk, h, mi, s; const char *exp; } cs[] = {
    {"29 s rounds down:", 0x2000, 0x06, 0x15, 4, 0x12, 0x34, 0x29, "29 s rounds down:                  2000-06-15 wk4 12:34:00 R64 00 RCR1 00 RCR2 09"},
    {"30 s rounds up:", 0x2000, 0x06, 0x15, 4, 0x12, 0x34, 0x30, "30 s rounds up:                    2000-06-15 wk4 12:35:00 R64 00 RCR1 00 RCR2 09"},
    {"BCD minute 09->10:", 0x2000, 0x06, 0x15, 4, 0x12, 0x09, 0x45, "BCD minute 09->10:                 2000-06-15 wk4 12:10:00 R64 00 RCR1 00 RCR2 09"},
    {"hour 09:59->10:00:", 0x2000, 0x06, 0x15, 4, 0x09, 0x59, 0x45, "hour 09:59->10:00:                 2000-06-15 wk4 10:00:00 R64 00 RCR1 00 RCR2 09"},
    {"2000-02-28 23:59 (leap /400):", 0x2000, 0x02, 0x28, 1, 0x23, 0x59, 0x45, "2000-02-28 23:59 (leap /400):      2000-02-29 wk2 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"1900-02-28 (/100):", 0x1900, 0x02, 0x28, 3, 0x23, 0x59, 0x45, "1900-02-28 (/100):                 1900-03-01 wk4 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"2100-02-28 (/100):", 0x2100, 0x02, 0x28, 0, 0x23, 0x59, 0x45, "2100-02-28 (/100):                 2100-03-01 wk1 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"2004-02-28 (/4):", 0x2004, 0x02, 0x28, 6, 0x23, 0x59, 0x45, "2004-02-28 (/4):                   2004-02-29 wk0 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"2001-02-28:", 0x2001, 0x02, 0x28, 3, 0x23, 0x59, 0x45, "2001-02-28:                        2001-03-01 wk4 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"2004-02-29:", 0x2004, 0x02, 0x29, 0, 0x23, 0x59, 0x45, "2004-02-29:                        2004-03-01 wk1 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"2000-04-30 (30-day month):", 0x2000, 0x04, 0x30, 0, 0x23, 0x59, 0x45, "2000-04-30 (30-day month):         2000-05-01 wk1 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"2000-01-31 (31-day month):", 0x2000, 0x01, 0x31, 1, 0x23, 0x59, 0x45, "2000-01-31 (31-day month):         2000-02-01 wk2 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"2000-09-30:", 0x2000, 0x09, 0x30, 6, 0x23, 0x59, 0x45, "2000-09-30:                        2000-10-01 wk0 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"2000-10-31 (month 10 BCD):", 0x2000, 0x10, 0x31, 2, 0x23, 0x59, 0x45, "2000-10-31 (month 10 BCD):         2000-11-01 wk3 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"1999-12-31 wk5 (year carry):", 0x1999, 0x12, 0x31, 5, 0x23, 0x59, 0x45, "1999-12-31 wk5 (year carry):       2000-01-01 wk6 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"2009-12-31 (BCD year):", 0x2009, 0x12, 0x31, 4, 0x23, 0x59, 0x45, "2009-12-31 (BCD year):             2010-01-01 wk5 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"9999-12-31 (year wrap):", 0x9999, 0x12, 0x31, 5, 0x23, 0x59, 0x45, "9999-12-31 (year wrap):            0000-01-01 wk6 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"2000-02-31 (invalid day):", 0x2000, 0x02, 0x31, 1, 0x23, 0x59, 0x45, "2000-02-31 (invalid day):          2000-03-01 wk2 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"2000-06-15 wk6 (wk 6->0):", 0x2000, 0x06, 0x15, 6, 0x23, 0x59, 0x45, "2000-06-15 wk6 (wk 6->0):          2000-06-16 wk0 00:00:00 R64 00 RCR1 00 RCR2 09"},
  };
  for (auto &c : cs) line(adj_case(c.t, c.y, c.mo, c.da, c.wk, c.h, c.mi, c.s), c.exp);
  set_time(0x2000, 0x06, 0x15, 4, 0x12, 0x34, 0x45);
  w8(0x38, 0x00); w8(0x3C, 0x0C); idle(200);
  line(show("ADJ with START=0, 12:34:45:"), "ADJ with START=0, 12:34:45:        2000-06-15 wk4 12:34:00 R64 00 RCR1 00 RCR2 08");
  // the alarm comparator runs continuously
  set_time(0x2000, 0x06, 0x15, 4, 0x12, 0x34, 0x45);
  w8(0x3C, 0x09); w8(0x38, 0x00);
  w8(0x24, 0x80 | 0x35); w8(0x20, 0x80 | 0x00);
  idle(2000); const uint32_t a0 = r8(0x38);
  w8(0x3C, 0x0D); idle(200); const uint32_t a1 = r8(0x38);
  w8(0x38, 0x00); idle(2000); const uint32_t a2 = r8(0x38);
  w8(0x20, 0x00); w8(0x24, 0x00); w8(0x24, 0x80 | 0x35); idle(2000); const uint32_t a3 = r8(0x38);
  w8(0x4, 0x00); idle(2000); const uint32_t a4 = r8(0x38);
  char b[200];
  snprintf(b, sizeof b, "alarm: AF before %02x, after ADJ onto match %02x, after clear while matching %02x, alarm set to held "
           "match %02x, after counter write %02x", a0, a1, a2, a3, a4);
  line(b, "alarm: AF before 00, after ADJ onto match 01, after clear while matching 01, alarm set to held match 01, after counter write 01");
  line(show("after alarm test:"), "after alarm test:                  2000-06-15 wk4 12:35:00 R64 00 RCR1 01 RCR2 09");
  for (int i = 0; i < 6; i++) w8(0x20 + 4 * i, 0);
  w8(0x38, 0);
}

// tc4 (tools/hw/timers-clock/tc4.c, run 2026-09-23): which RCR2 bit gates the alarm comparator, the day-overflow rule
// and the calendar on out-of-range / invalid-BCD values, ADJ with the START bit written with it.  Console lines verbatim.
static void test_tc4() {
  F.set("tc4");
  for (int i = 0; i < 6; i++) w8(0x20 + 4 * i, 0);
  // AF gate: RSECAR / RMINAR match the held counters; RCR2 written, AF cleared, then read
  set_time(0x2000, 0x06, 0x15, 4, 0x12, 0x34, 0x45);
  w8(0x20, 0x80 | 0x45); w8(0x24, 0x80 | 0x34);
  static const uint8_t r2[6] = {0x00, 0x01, 0x08, 0x09, 0x00, 0x09};
  std::string g = "AF gate (RCR2 written -> read, RCR1):";
  for (int i = 0; i < 6; i++) {
    w8(0x3C, r2[i]); w8(0x38, 0x00); idle(2000);
    const uint32_t af = r8(0x38), rb = r8(0x3C);
    char b[32]; snprintf(b, sizeof b, " %02x->%02x %02x", r2[i], rb, af); g += b;
  }
  line(g, "AF gate (RCR2 written -> read, RCR1): 00->00 00 01->01 00 08->08 01 09->09 01 00->00 00 09->09 01");
  w8(0x20, 0); w8(0x24, 0); w8(0x3C, 0x09); w8(0x38, 0);
  line(show("after AF gate:"), "after AF gate:                     2000-06-15 wk4 12:34:45 R64 00 RCR1 00 RCR2 09");
  struct C { const char *t; uint16_t y; uint8_t mo, da, wk, h, mi, s; const char *exp; } cs[] = {
    {"2000-01-32 (day 32, 31-day month):", 0x2000, 0x01, 0x32, 1, 0x23, 0x59, 0x45, "2000-01-32 (day 32, 31-day month): 2000-02-01 wk2 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"2001-02-30 (day 30 in Feb):", 0x2001, 0x02, 0x30, 1, 0x23, 0x59, 0x45, "2001-02-30 (day 30 in Feb):        2001-03-01 wk2 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"2000-04-31 (day 31, 30-day):", 0x2000, 0x04, 0x31, 1, 0x23, 0x59, 0x45, "2000-04-31 (day 31, 30-day):       2000-05-01 wk2 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"2001-02-29 (29 in non-leap):", 0x2001, 0x02, 0x29, 1, 0x23, 0x59, 0x45, "2001-02-29 (29 in non-leap):       2001-03-01 wk2 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"2000-02-2a (day 2A, leap Feb):", 0x2000, 0x02, 0x2A, 1, 0x23, 0x59, 0x45, "2000-02-2a (day 2A, leap Feb):     2000-03-01 wk2 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"2000-01-3f (day 3F):", 0x2000, 0x01, 0x3F, 1, 0x23, 0x59, 0x45, "2000-01-3f (day 3F):               2000-02-01 wk2 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"2000-01-00 (day 00):", 0x2000, 0x01, 0x00, 1, 0x23, 0x59, 0x45, "2000-01-00 (day 00):               2000-01-01 wk2 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"2000-01-1a (day 1A):", 0x2000, 0x01, 0x1A, 1, 0x23, 0x59, 0x45, "2000-01-1a (day 1A):               2000-01-20 wk2 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"2000-00-31 (month 00):", 0x2000, 0x00, 0x31, 1, 0x23, 0x59, 0x45, "2000-00-31 (month 00):             2000-01-01 wk2 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"2000-00-28 (month 00, day 28):", 0x2000, 0x00, 0x28, 1, 0x23, 0x59, 0x45, "2000-00-28 (month 00, day 28):     2000-00-29 wk2 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"2000-13-31 (month 13):", 0x2000, 0x13, 0x31, 1, 0x23, 0x59, 0x45, "2000-13-31 (month 13):             2001-01-01 wk2 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"2000-1a-31 (month 1A):", 0x2000, 0x1A, 0x31, 1, 0x23, 0x59, 0x45, "2000-1a-31 (month 1A):             2001-01-01 wk2 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"2000-0a-31 (month 0A):", 0x2000, 0x0A, 0x31, 1, 0x23, 0x59, 0x45, "2000-0a-31 (month 0A):             2000-10-01 wk2 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"2000-0a-30 (month 0A, day 30):", 0x2000, 0x0A, 0x30, 1, 0x23, 0x59, 0x45, "2000-0a-30 (month 0A, day 30):     2000-0a-31 wk2 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"12:3a:45 (minute 3A):", 0x2000, 0x06, 0x15, 4, 0x12, 0x3A, 0x45, "12:3a:45 (minute 3A):              2000-06-15 wk4 12:40:00 R64 00 RCR1 00 RCR2 09"},
    {"12:5a:45 (minute 5A):", 0x2000, 0x06, 0x15, 4, 0x12, 0x5A, 0x45, "12:5a:45 (minute 5A):              2000-06-15 wk4 13:00:00 R64 00 RCR1 00 RCR2 09"},
    {"12:7f:45 (minute 7F):", 0x2000, 0x06, 0x15, 4, 0x12, 0x7F, 0x45, "12:7f:45 (minute 7F):              2000-06-15 wk4 13:00:00 R64 00 RCR1 00 RCR2 09"},
    {"1a:59:45 (hour 1A):", 0x2000, 0x06, 0x15, 4, 0x1A, 0x59, 0x45, "1a:59:45 (hour 1A):                2000-06-15 wk4 20:00:00 R64 00 RCR1 00 RCR2 09"},
    {"24:59:45 (hour 24):", 0x2000, 0x06, 0x15, 4, 0x24, 0x59, 0x45, "24:59:45 (hour 24):                2000-06-16 wk5 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"3f:59:45 (hour 3F):", 0x2000, 0x06, 0x15, 4, 0x3F, 0x59, 0x45, "3f:59:45 (hour 3F):                2000-06-16 wk5 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"23:59:45 wk7 (week 7):", 0x2000, 0x06, 0x15, 7, 0x23, 0x59, 0x45, "23:59:45 wk7 (week 7):             2000-06-16 wk0 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"12:34:5a (second 5A):", 0x2000, 0x06, 0x15, 4, 0x12, 0x34, 0x5A, "12:34:5a (second 5A):              2000-06-15 wk4 12:35:00 R64 00 RCR1 00 RCR2 09"},
    {"12:34:7f (second 7F):", 0x2000, 0x06, 0x15, 4, 0x12, 0x34, 0x7F, "12:34:7f (second 7F):              2000-06-15 wk4 12:35:00 R64 00 RCR1 00 RCR2 09"},
    {"12:34:2a (second 2A):", 0x2000, 0x06, 0x15, 4, 0x12, 0x34, 0x2A, "12:34:2a (second 2A):              2000-06-15 wk4 12:34:00 R64 00 RCR1 00 RCR2 09"},
    {"199a-12-31 (year 199A):", 0x199A, 0x12, 0x31, 4, 0x23, 0x59, 0x45, "199a-12-31 (year 199A):            2000-01-01 wk5 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"1a99-12-31 (year 1A99):", 0x1A99, 0x12, 0x31, 4, 0x23, 0x59, 0x45, "1a99-12-31 (year 1A99):            2000-01-01 wk5 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"ffff-12-31 (year FFFF):", 0xFFFF, 0x12, 0x31, 4, 0x23, 0x59, 0x45, "ffff-12-31 (year FFFF):            0000-01-01 wk5 00:00:00 R64 00 RCR1 00 RCR2 09"},
    {"2100-02-29 (29, /100 year):", 0x2100, 0x02, 0x29, 1, 0x23, 0x59, 0x45, "2100-02-29 (29, /100 year):        2100-03-01 wk2 00:00:00 R64 00 RCR1 00 RCR2 09"},
  };
  for (auto &c : cs) line(adj_case(c.t, c.y, c.mo, c.da, c.wk, c.h, c.mi, c.s), c.exp);
  set_time(0x2000, 0x06, 0x15, 4, 0x12, 0x34, 0x45);
  w8(0x38, 0x00); w8(0x3C, 0x0D); idle(200);
  line(show("START 0, then wr 0D (ADJ+START):"), "START 0, then wr 0D (ADJ+START):   2000-06-15 wk4 12:35:00 R64 00 RCR1 00 RCR2 09");
  set_time(0x2000, 0x06, 0x15, 4, 0x12, 0x34, 0x45);
  w8(0x38, 0x00); w8(0x3C, 0x09); w8(0x3C, 0x0C); idle(200);
  line(show("START 1, then wr 0C (ADJ, START 0):"), "START 1, then wr 0C (ADJ, START 0): 2000-06-15 wk4 12:34:00 R64 00 RCR1 00 RCR2 08");
  w8(0x3C, 0x09);
}

static std::string rtc_line(const char *t) {
  char b[256];
  const uint32_t r64 = r8(0), se = r8(4), mi = r8(8), hr = r8(0xC), wk = r8(0x10), da = r8(0x14), mo = r8(0x18),
                 yr = rd(R(0x1C), 1), a0 = r8(0x20), a1 = r8(0x24), a2 = r8(0x28), a3 = r8(0x2C), a4 = r8(0x30),
                 a5 = r8(0x34), c1 = r8(0x38), c2 = r8(0x3C);
  snprintf(b, sizeof b, "%s R64 %02x SEC %02x MIN %02x HR %02x WK %02x DAY %02x MON %02x YR %04x AR %02x %02x %02x %02x %02x %02x RCR1 %02x RCR2 %02x",
           t, r64, se, mi, hr, wk, da, mo, yr, a0, a1, a2, a3, a4, a5, c1, c2);
  return b;
}
static void test_tc2_P() {
  F.set("tc2 P");
  // the alarms as the console found them (undefined values, ENB clear)
  w8(0x20, 0x7F); w8(0x24, 0x7F); w8(0x28, 0x3F); w8(0x2C, 0x05); w8(0x30, 0x3F); w8(0x34, 0x00);
  w8(0x3C, 0x02);
  { char b[64]; snprintf(b, sizeof b, "P RCR2 wr 02 -> %02x", r8(0x3C)); line(b, "P RCR2 wr 02 -> 00"); }
  w8(4, 0x45); w8(8, 0x34); w8(0xC, 0x05); w8(0x10, 0x03); w8(0x14, 0x21); w8(0x18, 0x09); wr(R(0x1C), 1, 0x1999);
  line(rtc_line("P after writes 45 34 05 03 21 09 1999:"),
       "P after writes 45 34 05 03 21 09 1999: R64 00 SEC 45 MIN 34 HR 05 WK 03 DAY 21 MON 09 YR 1999 AR 7f 7f 3f 05 3f 00 RCR1 00 RCR2 00");
  for (int o = 4; o <= 0x18; o += 4) w8(o, 0xFF);
  wr(R(0x1C), 1, 0xFFFF);
  for (int i = 0; i < 6; i++) w8(0x20 + 4 * i, 0xFF);
  line(rtc_line("P after FF to every counter / alarm:"),
       "P after FF to every counter / alarm: R64 00 SEC 7f MIN 7f HR 3f WK 07 DAY 3f MON 1f YR ffff AR ff ff bf 87 bf 9f RCR1 00 RCR2 00");
  for (int i = 0; i < 6; i++) w8(0x20 + 4 * i, 0x00);
  w8(4, 0x45); w8(8, 0x34); w8(0xC, 0x05); w8(0x10, 0x03); w8(0x14, 0x21); w8(0x18, 0x09); wr(R(0x1C), 1, 0x1999);
  w8(0x38, 0x00); const uint32_t c0 = r8(0x38);
  w8(0x38, 0x80); const uint32_t c1 = r8(0x38);
  w8(0x38, 0x01); const uint32_t c2 = r8(0x38);
  w8(0x38, 0x66); const uint32_t c3 = r8(0x38);
  w8(0x38, 0x00);
  { char b[96]; snprintf(b, sizeof b, "P RCR1 wr 00 -> %02x, 80 -> %02x, 01 -> %02x, 66 -> %02x", c0, c1, c2, c3);
    line(b, "P RCR1 wr 00 -> 00, 80 -> 80, 01 -> 00, 66 -> 00"); }
  w8(0x3C, 0x09);
  { char b[64]; snprintf(b, sizeof b, "P RCR2 wr 09 -> %02x", r8(0x3C)); line(b, "P RCR2 wr 09 -> 09"); }
  idle(20000);                                               // no oscillator: nothing counts
  line(rtc_line("P after 1.2 s:"),
       "P after 1.2 s: R64 00 SEC 45 MIN 34 HR 05 WK 03 DAY 21 MON 09 YR 1999 AR 00 00 00 00 00 00 RCR1 00 RCR2 09");
  w8(0x3C, 0x0D); const uint32_t r2a = r8(0x3C);
  line(rtc_line("P after ADJ:"),
       "P after ADJ: R64 00 SEC 00 MIN 35 HR 05 WK 03 DAY 21 MON 09 YR 1999 AR 00 00 00 00 00 00 RCR1 00 RCR2 09");
  w8(0x3C, 0x0B); const uint32_t r2b = r8(0x3C);
  { char b[96]; snprintf(b, sizeof b, "P RCR2 after ADJ write %02x, after RESET write %02x, R64CNT %02x", r2a, r2b, r8(0));
    line(b, "P RCR2 after ADJ write 09, after RESET write 09, R64CNT 00"); }
  w8(0x3C, 0x19); idle(4000); const uint32_t r2c = r8(0x3C); w8(0x3C, 0x09);
  { char b[96]; snprintf(b, sizeof b, "P RCR2 PES=1 after 20 ms: %02x; wr 09 -> %02x", r2c, r8(0x3C));
    line(b, "P RCR2 PES=1 after 20 ms: 19; wr 09 -> 09"); }
  // H: MSTP1 = 1 (the DC boot state): every register reads 0, every write is ignored
  F.set("tc H");
  to_pce(); P.mstp = true;
  bool all0 = true;
  for (int o = 0; o <= 0x3C; o += 4) all0 &= rd(R(o), o == 0x1C ? 1 : 0) == 0;
  F.ok(all0, "a register read non-zero with MSTP1 = 1");
  w8(0x3C, 0x02); w8(4, 0x12); w8(0x38, 0x80); w8(0x20, 0xFF);
  to_pce(); P.mstp = false;
  F.ok(r8(0x3C) == 0x09 && r8(4) == 0x00 && r8(0x38) == 0 && r8(0x20) == 0, "a write landed while MSTP1 = 1");
}

static void test_gregorian() {
  F.set("Gregorian");
  auto tobcd = [](unsigned v, int digits) { unsigned r = 0; for (int i = 0; i < digits; i++) { r |= (v % 10) << (4 * i); v /= 10; } return r; };
  w8(0x3C, 0x09);
  int bad = 0;
  for (unsigned y = 0; y <= 9999; y++) {
    const bool leap = y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
    set_time(tobcd(y, 4), 0x02, 0x28, 0, 0x23, 0x59, 0x45);
    w8(0x3C, 0x09); w8(0x3C, 0x0D);
    const uint32_t mo = r8(0x18), da = r8(0x14), yr = rd(R(0x1C), 1);
    if (!F.ok(yr == tobcd(y, 4) && mo == (leap ? 0x02u : 0x03u) && da == (leap ? 0x29u : 0x01u),
              "year %u: 02-28 -> %04x-%02x-%02x (leap %d)", y, yr, mo, da, leap)) bad++;
    if (leap) {
      set_time(tobcd(y, 4), 0x02, 0x29, 0, 0x23, 0x59, 0x45);
      w8(0x3C, 0x09); w8(0x3C, 0x0D);
      F.ok(r8(0x18) == 0x03 && r8(0x14) == 0x01, "year %u: 02-29 -> %02x-%02x", y, r8(0x18), r8(0x14));
    }
    if (bad > 10) break;
  }
  static const unsigned ml[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  for (unsigned y = 1900; y <= 2100; y++) {
    for (unsigned mo = 1; mo <= 12; mo++) {
      unsigned last = ml[mo - 1] + (mo == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
      set_time(tobcd(y, 4), tobcd(mo, 2), tobcd(last, 2), 0, 0x23, 0x59, 0x45);
      w8(0x3C, 0x09); w8(0x3C, 0x0D);
      const unsigned nm = mo == 12 ? 1 : mo + 1, ny = mo == 12 ? y + 1 : y;
      F.ok(rd(R(0x1C), 1) == tobcd(ny, 4) && r8(0x18) == tobcd(nm, 2) && r8(0x14) == 1 && r8(0xC) == 0,
           "%u-%02u-%02u 23:59:45 + ADJ -> %04x-%02x-%02x", y, mo, last, rd(R(0x1C), 1), r8(0x18), r8(0x14));
    }
  }
}

static void test_counting() {
  F.set("counting");
  w8(0x3C, 0x0B);                                            // RESET: prescaler and R64CNT 0; RTCEN, START
  w8(0x38, 0x00);
  set_time(0x2000, 0x06, 0x15, 4, 0x12, 0x34, 0x56);
  w8(0x3C, 0x09);
  // rtc_tick every second oscillator tick
  int ticks = 0;
  for (int k = 0; k < 64; k++) { P.osc = true; step(); ticks += o_tick; idle(1); }
  F.ok(ticks == 32, "rtc_tick %d times in 64 oscillator ticks", ticks);
  osc(256 - 64);
  F.ok(r8(0) == 1, "R64CNT %02x after 256 oscillator ticks", r8(0));
  osc(256 * 127);
  F.ok(r8(0) == 0 && r8(4) == 0x57 && (r8(0x38) & 0x80), "1 s: R64 %02x SEC %02x RCR1 %02x", r8(0), r8(4), r8(0x38));
  w8(0x38, 0);
  // START = 0: R64CNT runs, the seconds do not, no CF
  w8(0x3C, 0x08);
  osc(32768);
  F.ok(r8(4) == 0x57 && !(r8(0x38) & 0x80), "START = 0 counted the seconds");
  // RTCEN = 0: nothing
  w8(0x3C, 0x01); const uint32_t r0 = r8(0);
  osc(2000);
  F.ok(r8(0) == r0, "RTCEN = 0 counted R64CNT");
  // 12:34:59 -> 12:35:00 by the 1 Hz carry, through the calendar
  set_time(0x2000, 0x12, 0x31, 6, 0x23, 0x59, 0x59); w8(0x3C, 0x0B); w8(0x3C, 0x09);
  osc(32768);
  F.ok(rd(R(0x1C), 1) == 0x2001 && r8(0x18) == 1 && r8(0x14) == 1 && r8(0x10) == 0 && r8(0xC) == 0 && r8(8) == 0 && r8(4) == 0,
       "year end by the 1 Hz carry");
  // PEF for every PES: the period in oscillator ticks
  static const unsigned per[8] = {0, 128, 512, 2048, 8192, 16384, 32768, 65536};
  for (int pes = 1; pes <= 7; pes++) {
    w8(0x3C, 0x0B | (pes << 4));                             // RESET, the period, RTCEN, START
    unsigned first = 0, second = 0, n = 0;
    for (unsigned k = 1; k <= 2 * per[pes] + 4 && !second; k++) {
      P.osc = true; step(); step();
      if (d->pri) {                                          // PEF & PES != 0, after this tick's edge
        if (!first) first = k; else if (k > first) second = k;
        to_pce(); sh4t::Strobe s; s.wr = true; s.off = R(0x3C); s.wdata = 0x09 | (pes << 4); step(&s);   // clear PEF
        n++;
      }
    }
    F.ok(second - first == per[pes], "PES %d: PEF every %u ticks, expected %u", pes, second - first, per[pes]);
  }
  w8(0x3C, 0x09);
  // CF on an R64CNT read in the Pcyc of a 128 Hz carry; not on a read away from it
  w8(0x3C, 0x0B); w8(0x3C, 0x09); w8(0x38, 0);
  osc(255);                                                   // pre = 255: the next tick carries
  to_pce(); step(); P.osc = true; step(); idle(2);          // the carry in the first cycle of a Pcyc
  { sh4t::Strobe s; s.rd = true; s.off = R(0); step(&s); }   // the read strobe (pce) of the same Pcyc
  F.ok(r8(0x38) & 0x80, "R64CNT read in the carry's Pcyc did not set CF");
  w8(0x38, 0); r8(0); idle(8); F.ok(!(r8(0x38) & 0x80), "an R64CNT read away from a carry set CF");
  // ADJ restarts the divider
  osc(100); w8(0x3C, 0x0D); F.ok(r8(0) == 0, "ADJ did not reset R64CNT");
  // module stop: the counters run on the oscillator, the registers read 0
  const uint32_t s0 = r8(4);
  to_pce(); P.mstp = true;
  osc(32768);
  to_pce(); P.mstp = false;
  F.ok(r8(4) == bcd_next(s0), "MSTP1: the seconds %02x -> %02x", s0, r8(4));
  // the request lines
  w8(0x38, 0x90); step(); F.ok(o_cui, "CUI with CF & CIE");
  to_pce(); P.mstp = true; step(); F.ok(!o_cui, "CUI while module-stopped"); to_pce(); P.mstp = false;
  w8(0x38, 0x00);
  w8(0x3C, 0x99); step(); F.ok(o_pri, "PRI with PEF & PES != 0");
  w8(0x3C, 0x89); step(); F.ok(!o_pri, "PRI with PES = 0");
  w8(0x3C, 0x09);
  w8(0x20, 0x80 | r8(4)); w8(0x38, 0x08); step(); step(); F.ok(o_ati, "ATI with AF & AIE");
  w8(0x20, 0); w8(0x38, 0); step(); F.ok(!o_ati, "ATI after clearing AF");
}

static void test_resets() {
  F.set("resets");
  set_time(0x2024, 0x07, 0x04, 4, 0x11, 0x22, 0x33);
  w8(0x20, 0x80 | 0x10); w8(0x24, 0x85); w8(0x38, 0x98); w8(0x3C, 0x49 | 0x80);
  P.cpu = true; idle(8); P.cpu = false; idle(1);
  F.ok(r8(0x38) == 0x98 && r8(0x3C) == 0xC9, "CPU-only reset changed RCR1 %02x RCR2 %02x", r8(0x38), r8(0x3C));
  P.man = true; idle(8); P.man = false; idle(1);
  F.ok(r8(0x38) == 0x80 && r8(0x3C) == 0x89 && r8(0x20) == 0x90, "manual reset: RCR1 %02x (80) RCR2 %02x (89) ENB %02x",
       r8(0x38), r8(0x3C), r8(0x20));
  // a manual reset holds RTCEN and START whatever their values, and clears PES (hm Table 11.2 note 2; spec 3.4)
  static const uint8_t mv[4][2] = {{0x20, 0x00}, {0x28, 0x08}, {0x21, 0x01}, {0x70, 0x00}};
  for (auto &c : mv) {
    w8(0x3C, c[0]);
    const uint32_t before = r8(0x3C);
    P.man = true; idle(8); P.man = false; idle(1);
    F.ok(before == c[0] && r8(0x3C) == c[1], "manual reset: RCR2 %02x -> %02x, expected %02x (RTCEN / START held, PES 0)",
         before, r8(0x3C), c[1]);
  }
  w8(0x38, 0x98); w8(0x3C, 0x42);
  P.por = true; idle(8); P.por = false; idle(1);
  F.ok(r8(0x38) == 0 && r8(0x3C) == 0x09 && r8(0x20) == 0x10 && r8(0x24) == 0x05, "power-on reset: RCR1 %02x RCR2 %02x "
       "RSECAR %02x RMINAR %02x", r8(0x38), r8(0x3C), r8(0x20), r8(0x24));
  F.ok(rd(R(0x1C), 1) == 0x2024 && r8(4) == 0x33, "a reset changed the counters");
}

static void test_random(uint64_t seed, uint64_t ncyc) {
  F.set("random");
  Rng r(seed);
  const uint64_t end = cyc + ncyc;
  while (cyc < end) {
    if (r.chance(400)) P.osc = true;
    if (r.chance(1) && is_pce()) P.mstp = !P.mstp;
    if (r.below(40000) == 0) { const int k = r.below(4); if (k == 0) P.pin = true; else if (k == 1) P.por = true;
                               else if (k == 2) P.man = true; else P.cpu = true; }
    else if (r.chance(100)) P.pin = P.por = P.man = P.cpu = false;
    if (is_pce() && r.chance(250)) {
      sh4t::Strobe s; s.rd = r.chance(500); s.wr = !s.rd;
      const int k = r.below(17);
      s.off = R(k < 16 ? 4 * k : r.below(0x80));
      s.size = r.chance(900) ? (k == 7 ? 1 : 0) : r.below(3);
      uint32_t v = r.u32();
      if (k >= 1 && k <= 6) v = r.chance(700) ? (r.below(6) << 4 | r.below(10)) : v;           // mostly valid BCD
      if (k == 15) v = (v & 0xF5) | (r.chance(50) ? 4 : 0) | (r.chance(50) ? 2 : 0) | (r.chance(900) ? 8 : 0);  // RTCEN mostly on
      if (k >= 8 && k <= 13 && r.chance(700)) v &= 0x7F;                                        // alarms mostly off
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
  d = new Vv1_rtc_tb;
  P.pin = true; idle(8); P.pin = false; idle(2);
  test_tc2_P();
  test_tc3();
  test_tc4();
  test_gregorian();
  test_counting();
  test_resets();
  test_random(seed, ncyc);
  d->final();
  delete d;
  return F.finish();
}
