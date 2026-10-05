// sh4_timers.h -- functional and cycle model of the SH7091 clock / reset / timer units (WI-CLK; rtl/v1/SOC.md 2,
// 3.8; rtl/v1/spec/timers-clock.md): the CPG (FRQCR, STBCR, STBCR2, the Bclk / Pclk enables, the WDT divider, the
// power-down state machine, ratio_inc), the reset controller, the WDT, the TMU and the RTC.
//
// Two ways to use it.
//  * Cycle mode (the unit testbenches, which compare the RTL with it every Icyc).  Per Icyc:
//        o = unit.out(in);              // Moore / combinational outputs of this cycle (no strobe involved)
//        r = unit.strobe(s);            // optional: this cycle's P4 slave strobe; returns rdata / hit (combinational)
//        unit.step(in);                 // the clock edge: the strobe's write / read side effect and this cycle's
//                                       // events land together, with the RTL's priorities
//    `Sh4Timers` wires the five units as v1_soc does and steps them together (`cycle()`).
//  * Functional mode (P4Slave, for Sh4Sys and the bare-metal runner): rd() / wr() act at once (as a strobe in a cycle
//    with no concurrent event); Sh4Timers::tick_icyc(n) advances time with the internal CPG making the enables.  The
//    per-unit tick_icyc() are no-ops: a composite owns the time.
//
// Register offsets are a[27:0] of the P4 address, as v1_p4bus presents them (0xFFD8000C -> 0xFD8000C); only a[17:0]
// is decoded (the block select a[23:18] is the bridge's).  Sizes: 0 byte, 1 word, 2 long.  Every register answers
// only its own size (a wrong size reads 0 with hit = 1 and ignores the write) [HW] (timers-clock 3.0); an offset that
// is not a register is a hole (hit = 0, read 0).
//
// Everything here is written from the spec's text, not from the RTL; the RTL files cite the same rules.
#pragma once
#include <cstdint>
#include <cstring>
#include "sh4_p4if.h"

namespace sh4t {

struct Rst { bool pin = false, por = false, man = false, cpu = false; };
struct Ce  { bool bce = false, pce = false; };
struct Strobe { bool rd = false, wr = false; int size = 0; uint32_t off = 0; uint32_t wdata = 0; bool dma = false; };
struct Resp { uint32_t rdata = 0; bool hit = false; };

enum { SZ_B = 0, SZ_W = 1, SZ_L = 2 };
enum { STS_NORMAL = 0, STS_STANDBY = 1, STS_SLEEP = 2, STS_RESET = 3 };        // status_e
enum { RC_000 = 0, RC_020 = 1, RC_140 = 2 };                                   // rst_code_e

// ================================================================================================================
// CPG: FRQCR / STBCR / STBCR2, the enables, the WDT divider, the power-down state machine
// ================================================================================================================
enum { CPG_RUN = 0, CPG_SLEEP = 1, CPG_DSLEEP = 2, CPG_RESYNC = 3, CPG_STANDBY = 4, CPG_SETTLE = 5, CPG_ENTRY = 6 };
// the power-down handshakes (v1_cpg, SOC.md 2.5, M1b): the CPU clock stops CPG_ENTRY_PCE pce edges after the sleep
// request (the core halts at once; PMCTR counts on), and the core resumes CPG_RESYNC_PCE pce edges after the wake
enum { CPG_ENTRY_PCE = 9, CPG_RESYNC_PCE = 4 };

// FRQCR after a pin power-on reset, by MD2-0 (hm Table 10.3 read through Table 10.4): CKOEN PLL1EN PLL2EN = 1 and the
// divider codes of the mode.  MD 110 / 111 are not in the table: [P] as mode 5 (the DC's).
inline uint16_t frqcr_init(unsigned md) {
  static const uint16_t v[8] = {0x0E1A, 0x0E23, 0x0E13, 0x0E13, 0x0E0A, 0x0E0A, 0x0E0A, 0x0E0A};
  return v[md & 7];
}
// IFC / BFC code -> divider of the PLL1 output (x1 .. x1/8); prohibited codes 110 / 111 [P] as x1/8
inline int ib_div(unsigned code) { static const int d[8] = {1, 2, 3, 4, 6, 8, 8, 8}; return d[code & 7]; }
// PFC code -> divider (x1/2 .. x1/8); prohibited 101-111 [P] as x1/8
inline int p_div(unsigned code) { static const int d[8] = {2, 3, 4, 6, 8, 8, 8, 8}; return d[code & 7]; }
// PMCLK ratio increment per Icyc (debug-ubc-hudi 4.6): time in Bcyc / 24, 12 at 0x0E0A.  Like the enables it treats
// IFC as x1 (SOC.md 9.18: IFC is stored and read back only), so it is 24 / Bdiv of the simulated Bclk [P].
inline int ratio_of(uint16_t frqcr) { return 24 / ib_div((frqcr >> 3) & 7); }

struct Cpg : P4Slave {
  struct In { Rst rst; unsigned md = 5; bool sleep_req = false, wake = false, wake_stby = false, wdt_ovf = false;
              bool p4_wpend = false; };               // p4_wpend: a Pclk write in flight (v1_p4bus pclk_wpend)
  struct Out {
    Ce ce; uint16_t div = 0; int ratio_inc = 0; bool cpu_run = false, cpu_clk = false, dmac_run = false, stby = false;
    unsigned mstp = 0; int status = 0; bool wdt_settle = false;
  };
  // state
  uint16_t frqcr = 0x0E0A;
  uint8_t stbcr = 0, dslp = 0;
  int pcnt = 3, bcnt = 1, pdiv = 4, bdiv = 2;          // enables: down counters and the active dividers
  uint16_t div = 0;                                    // 12-bit WDT divider
  int st = CPG_RUN, rs = 0, es = 0, sp = 0;
  bool deep = false, sleep_q = false;
  bool sleep_pend = false;                             // a sleep edge that met a relocking FRQCR write [P]
  bool ck_on = false;                                  // the RESYNC in progress began in ENTRY (the clock never stopped)
  bool sbx = false;                                    // the settle / resync in progress ends a standby
  Strobe pend; bool has_pend = false;

  bool clk_run() const { return st != CPG_STANDBY && st != CPG_SETTLE; }
  Out out(const In &in) const {
    Out o;
    const bool run = clk_run();
    o.ce.pce = run && pcnt == 0;
    o.ce.bce = run && (bcnt == 0 || pcnt == 0);
    o.div = div;
    o.ratio_inc = ratio_of(frqcr);
    o.cpu_run = st == CPG_RUN;
    o.cpu_clk = st == CPG_RUN || st == CPG_ENTRY || (st == CPG_RESYNC && ck_on);
    o.dmac_run = !(deep || st == CPG_STANDBY || st == CPG_SETTLE) && !(stbcr & 0x10);
    o.stby = st == CPG_STANDBY;
    o.mstp = stbcr & 0x1F;
    if (in.rst.por || in.rst.man) o.status = STS_RESET;
    else if (st == CPG_SLEEP || st == CPG_DSLEEP || (st == CPG_RESYNC && !sbx)) o.status = STS_SLEEP;
    else if (st == CPG_STANDBY || st == CPG_SETTLE) o.status = STS_STANDBY;
    else o.status = STS_NORMAL;
    o.wdt_settle = st == CPG_SETTLE;
    return o;
  }
  // registers: 00 FRQCR (16), 04 STBCR (8), 10 STBCR2 (8)
  static int reg_of(uint32_t off) {
    switch (off & 0x3FFFF) { case 0x00: return 0; case 0x04: return 1; case 0x10: return 2; default: return -1; }
  }
  static int size_of(int r) { return r == 0 ? SZ_W : SZ_B; }
  Resp strobe(const Strobe &s) {
    Resp r;
    const int k = reg_of(s.off);
    if (!(s.rd || s.wr) || k < 0) return r;
    r.hit = true;
    if (s.rd && s.size == size_of(k)) r.rdata = k == 0 ? frqcr : k == 1 ? stbcr : (uint32_t)dslp << 7;
    if (s.wr) { pend = s; has_pend = true; }
    return r;
  }
  void step(const In &in) {
    const bool pce_now = clk_run() && pcnt == 0;
    Strobe w = pend; const bool wr = has_pend && w.wr && w.size == size_of(reg_of(w.off));
    has_pend = false;
    if (in.rst.por) {
      if (in.rst.pin) frqcr = frqcr_init(in.md);
      stbcr = 0; dslp = 0;
      pdiv = p_div(frqcr & 7); bdiv = ib_div((frqcr >> 3) & 7);
      pcnt = pdiv - 1; bcnt = bdiv - 1; div = 0;
      st = CPG_RUN; rs = es = sp = 0; deep = false; sleep_q = false; sleep_pend = false; ck_on = false; sbx = false;
      return;
    }
    // enables: the active dividers change at a pce (the end of a Pclk period, also a Bclk end in every legal FRQCR)
    if (clk_run()) {
      if (pcnt == 0) {
        pdiv = p_div(frqcr & 7); bdiv = ib_div((frqcr >> 3) & 7);
        pcnt = pdiv - 1; bcnt = bdiv - 1;
      } else {
        pcnt--; bcnt = bcnt == 0 ? bdiv - 1 : bcnt - 1;
      }
    }
    if (st != CPG_STANDBY) div = (div + 1) & 0xFFF;
    if (in.rst.man) {
      st = CPG_RUN; rs = es = sp = 0; deep = false; sleep_q = false; sleep_pend = false; ck_on = false; sbx = false;
      return;
    }
    // register writes (land at the end of the strobe cycle).  A FRQCR write that needs a PLL relock stops the chip
    // (SETTLE, the WDT counts): PLL1EN 0 -> 1 is one count, and a PLL2 relock (PLL2 on after the write, and PLL1
    // restarted, BFC changed or PLL2 just enabled) one more (hm 10.5.1-10.5.3) [P for PLL2EN 0 -> 1].
    int phases = 0;
    if (wr) {
      const int k = reg_of(w.off);
      if (k == 0) {
        const uint16_t nv = w.wdata & 0x0FFF;
        const bool pll1_on = !(frqcr & 0x400) && (nv & 0x400);
        const bool pll2 = nv & 0x200;
        const bool pll2_on = !(frqcr & 0x200) && pll2;
        const bool bfc_chg = ((nv >> 3) & 7) != ((frqcr >> 3) & 7);
        phases = (pll1_on ? 1 : 0) + (pll2 && (pll1_on || bfc_chg || pll2_on) ? 1 : 0);
        frqcr = nv;
      } else if (k == 1) stbcr = w.wdata & 0xFF;
      else dslp = (w.wdata >> 7) & 1;
    }
    // FSM
    int nst = st;
    const bool sleep_edge = in.sleep_req && !sleep_q;
    if ((st == CPG_RUN || st == CPG_ENTRY) && phases) {   // the relock wins; a sleep edge (or an entry under way)
      nst = CPG_SETTLE; sp = phases;                          // waits for the settle's end
      if ((st == CPG_RUN && sleep_edge) || st == CPG_ENTRY) sleep_pend = true;
    } else {
      switch (st) {
        case CPG_RUN:                        // the core halts; the clock-stop handshake starts
          if (sleep_edge || sleep_pend) { nst = CPG_ENTRY; es = CPG_ENTRY_PCE; sleep_pend = false; }
          break;
        case CPG_ENTRY:                      // the clock stops at the ENTRY_PCE-th pce with no Pclk write in flight
          // (SOC.md 2.5, 3.1.5 Landing: the STBCR written right before the SLEEP decides the mode; STBY wins over DSLP,
          // hm 9.2.4); a wake before then resynchronises with the CPU clock running
          if (in.wake) { nst = CPG_RESYNC; rs = CPG_RESYNC_PCE; ck_on = true; break; }
          {
            const bool last = es == 0 || (es == 1 && pce_now);
            if (pce_now && es != 0) es--;
            if (last && !in.p4_wpend) {
              nst = (stbcr & 0x80) ? CPG_STANDBY : dslp ? CPG_DSLEEP : CPG_SLEEP; deep = nst == CPG_DSLEEP;
            }
          }
          break;
        case CPG_SLEEP: case CPG_DSLEEP:
          if (in.wake) { nst = CPG_RESYNC; rs = CPG_RESYNC_PCE; ck_on = false; }
          break;
        case CPG_RESYNC:                     // RESYNC_PCE Pcyc: that many pce edges after the wake is seen
          if (pce_now) { if (rs == 1) { nst = CPG_RUN; deep = false; sbx = false; ck_on = false; } else rs--; }
          break;
        case CPG_STANDBY:                    // only wake_stby ends standby
          if (in.wake_stby) { nst = CPG_SETTLE; sp = 1; sbx = true; }
          break;
        case CPG_SETTLE:                     // a standby exit resynchronises like a sleep wake; a relock does not
          if (in.wdt_ovf) { if (sp == 1) { nst = sbx ? CPG_RESYNC : CPG_RUN; rs = CPG_RESYNC_PCE; ck_on = false; } sp--; }
          break;
      }
    }
    if (st == CPG_RUN) sleep_q = in.sleep_req;
    st = nst;
  }
  // P4Slave (functional mode)
  bool rd(uint32_t off, int size, uint32_t &v, bool dma) override {
    Strobe s; s.rd = true; s.size = size; s.off = off; s.dma = dma; Resp r = strobe(s); v = r.rdata; return r.hit;
  }
  bool wr(uint32_t off, int size, uint32_t v, bool dma) override;
  bool is_volatile(uint32_t, int) const override { return false; }
};

// ================================================================================================================
// Reset controller (SOC.md 2.2)
// ================================================================================================================
enum { RK_NONE = 0, RK_CPU = 1, RK_MAN = 2, RK_POR = 3, RK_PIN = 4 };
struct Rstc {
  struct In { bool reset_n = true, sck2 = true, wdt_por = false, wdt_man = false, hudi = false, mhit = false,
              arr_mhit = false, cpu_req = false; };
  // state: the pin synchronisers (no reset: this is the reset), the active kind, the multiple-hit code, the hold
  bool rn1 = false, rn2 = false, sk1 = false, sk2 = false;
  int kind = RK_NONE, hold = 0; bool mh = false;
  Rst rst() const {
    Rst r; r.pin = kind == RK_PIN; r.por = kind >= RK_POR; r.man = kind == RK_MAN; r.cpu = kind != RK_NONE; return r;
  }
  int code() const { return kind >= RK_POR ? RC_000 : (kind == RK_MAN && mh) ? RC_140 : kind != RK_NONE ? RC_020 : RC_000; }
  void step(const In &in) {
    int req = RK_NONE; bool req_mh = false;
    if (!rn2) req = sk2 ? RK_PIN : RK_MAN;
    if ((in.wdt_por || in.hudi) && req < RK_POR) req = RK_POR;
    if (in.wdt_man || in.mhit || in.arr_mhit) { if (req < RK_MAN) req = RK_MAN; }
    if (in.mhit || in.arr_mhit) req_mh = true;
    if (in.cpu_req && req < RK_CPU) req = RK_CPU;
    if (req != RK_NONE) {
      const int prev = hold ? kind : RK_NONE;
      const int m = prev > req ? prev : req;
      mh = m == RK_MAN && ((prev == RK_MAN && mh) || req_mh);
      kind = m; hold = 8;
    } else if (hold) {
      if (--hold == 0) { kind = RK_NONE; mh = false; }
    }
    rn2 = rn1; rn1 = in.reset_n; sk2 = sk1; sk1 = in.sck2;
  }
};

// ================================================================================================================
// WDT (timers-clock 3.2, 4.2, 7.3)
// ================================================================================================================
struct Wdt : P4Slave {
  struct In { Rst rst; uint16_t div = 0; bool settle = false; };
  struct Out { bool settle_ovf = false, iti = false, rst_por = false, rst_man = false; };
  uint8_t cnt = 0, csr = 0;              // WTCSR: 7 TME 6 WT/IT 5 RSTS 4 WOVF 3 IOVF 2:0 CKS
  bool tap_q = false, settle_q = false, reload = false; uint8_t sinit = 0;
  Strobe pend; bool has_pend = false;

  bool cin(const In &in) const {
    const bool tap = (in.div >> ((csr & 7) + 4)) & 1;
    return ((csr & 0x80) || in.settle) && tap;
  }
  bool inc(const In &in) const { return cin(in) && !tap_q; }
  Out out(const In &in) const {
    Out o;
    const bool ovf = inc(in) && cnt == 0xFF;
    o.settle_ovf = ovf && in.settle;
    o.rst_por = ovf && !in.settle && (csr & 0x40) && !(csr & 0x20);
    o.rst_man = ovf && !in.settle && (csr & 0x40) && (csr & 0x20);
    o.iti = (csr & 0x08) && !(csr & 0x40);
    return o;
  }
  static int reg_of(uint32_t off) { switch (off & 0x3FFFF) { case 0x08: return 0; case 0x0C: return 1; default: return -1; } }
  Resp strobe(const Strobe &s) {
    Resp r;
    const int k = reg_of(s.off);
    if (!(s.rd || s.wr) || k < 0) return r;
    r.hit = true;
    if (s.rd && s.size == SZ_B) r.rdata = k == 0 ? cnt : csr;           // read 8 bits only [HW]
    if (s.wr) { pend = s; has_pend = true; }
    return r;
  }
  void step(const In &in) {
    Strobe w = pend; const bool have = has_pend && w.wr; has_pend = false;
    if (in.rst.pin) { cnt = 0; csr = 0; tap_q = settle_q = reload = false; sinit = 0; return; }
    const bool c = cin(in), i = c && !tap_q, ovf = i && cnt == 0xFF;
    const int k = have ? reg_of(w.off) : -1;
    // write: 16 bits with the key in the upper byte [HW]
    const bool wcnt = k == 0 && w.size == SZ_W && ((w.wdata >> 8) & 0xFF) == 0x5A;
    const bool wcsr = k == 1 && w.size == SZ_W && ((w.wdata >> 8) & 0xFF) == 0xA5;
    uint8_t ncnt = cnt;
    if (wcnt) ncnt = w.wdata & 0xFF;                 // [P] a write wins over a count in the same cycle
    else if (reload && in.settle) ncnt = sinit;      // second settle phase restarts from the software value (hm 10.5.2)
    else if (i) ncnt = cnt + 1;
    uint8_t ncsr = wcsr ? (uint8_t)(w.wdata & 0xFF) : csr;   // WOVF / IOVF: 0 clears, 1 sets ([HW] G, tc4)
    if (ovf && !in.settle) ncsr |= (csr & 0x40) ? 0x10 : 0x08; // the overflow wins over a same-cycle write
    if (in.settle && !settle_q) sinit = cnt;
    reload = ovf && in.settle;
    tap_q = c; settle_q = in.settle;
    cnt = ncnt; csr = ncsr;
  }
  bool rd(uint32_t off, int size, uint32_t &v, bool dma) override {
    Strobe s; s.rd = true; s.size = size; s.off = off; s.dma = dma; Resp r = strobe(s); v = r.rdata; return r.hit;
  }
  bool wr(uint32_t off, int size, uint32_t v, bool dma) override;
  bool is_volatile(uint32_t off, int) const override { return reg_of(off) >= 0; }
};

// ================================================================================================================
// TMU (timers-clock 3.3, 4.1, 7.4)
// ================================================================================================================
struct Tmu : P4Slave {
  struct In { Rst rst; Ce ce; bool stby = false, mstp = false, tclk_in = false, rtc_tick = false, dack_ic = false; };
  struct Out { unsigned tuni = 0; bool ticpi2 = false, tclk_out = false, tclk_oe = false, dreq_ic = false;
               uint32_t next_evt = 0xFFFFFFFFu; };
  uint8_t tocr = 0, tstr = 0;
  uint32_t tcor[3] = {~0u, ~0u, ~0u}, tcnt[3] = {~0u, ~0u, ~0u}, tcpr2 = 0;
  uint16_t tcr[3] = {0, 0, 0};           // ch0/1: 8 UNF 5 UNIE 4:3 CKEG 2:0 TPSC; ch2 also 9 ICPF 7:6 ICPE
  uint16_t presc = 0;                    // 10-bit prescaler of Pcyc
  bool t1 = false, t2 = false, t3 = false, rtc_pend = false, dreq = false, tclk_tog = false;
  uint32_t gap = 0, plen = 4;            // next_evt helpers: Icyc since the last pce, observed Pclk period
  Strobe pend; bool has_pend = false;

  static int tpsc(uint16_t t) { return t & 7; }
  bool edge(unsigned ckeg) const {       // the synchronised TCLK edge chosen by CKEG (valid in a pce cycle)
    const bool rise = t2 && !t3, fall = !t2 && t3;
    return (ckeg & 2) ? (rise || fall) : (ckeg & 1) ? fall : rise;
  }
  bool rtc_now(const In &in) const { return (rtc_pend || in.rtc_tick) && (in.ce.pce || in.stby); }
  bool ev(int n, const In &in) const {
    if (!((tstr >> n) & 1)) return false;
    const bool pstop = in.mstp || in.stby;
    const int t = tpsc(tcr[n]);
    if (t <= 4) {
      const unsigned m = (1u << (2 * t + 2)) - 1;          // Pphi/4 .. Pphi/1024: every 4 << 2t Pcyc
      return in.ce.pce && !pstop && (presc & m) == m;
    }
    if (t == 6) return rtc_now(in);
    if (t == 7) return in.ce.pce && !pstop && !(tocr & 1) && edge((tcr[n] >> 3) & 3);
    return false;                                          // TPSC 5: reserved, never counts [HW]
  }
  bool cap(const In &in) const {
    return in.ce.pce && !(in.mstp || in.stby) && !(tocr & 1) && (tcr[2] & 0x80) && edge((tcr[2] >> 3) & 3);
  }
  Out out(const In &in) const {
    Out o;
    for (int n = 0; n < 3; n++) if ((tcr[n] & 0x100) && (tcr[n] & 0x20) && !in.mstp) o.tuni |= 1u << n;
    o.ticpi2 = (tcr[2] & 0x200) && ((tcr[2] >> 6) & 3) == 3 && !in.mstp;
    o.dreq_ic = dreq && !in.mstp;
    o.tclk_oe = tocr & 1;
    o.tclk_out = tclk_tog;
    // next_evt: Icyc to the next underflow of a running Pphi-clocked channel (0 = at the end of this cycle)
    uint64_t best = 0xFFFFFFFFull;
    if (!in.mstp && !in.stby) {
      const uint64_t d0 = in.ce.pce ? 0 : (plen > gap + 1 ? plen - 1 - gap : 0);
      for (int n = 0; n < 3; n++) {
        const int t = tpsc(tcr[n]);
        if (!((tstr >> n) & 1) || t > 4) continue;
        const uint64_t R = 4ull << (2 * t);
        const uint64_t j = (R - 1 - (presc % R)) + (uint64_t)tcnt[n] * R;
        const uint64_t v = d0 + j * plen;
        if (v < best) best = v;
      }
    }
    o.next_evt = (uint32_t)best;
    return o;
  }
  // registers
  enum { R_TOCR, R_TSTR, R_TCOR0, R_TCNT0, R_TCR0, R_TCOR1, R_TCNT1, R_TCR1, R_TCOR2, R_TCNT2, R_TCR2, R_TCPR2 };
  static int reg_of(uint32_t off) { const uint32_t o = off & 0x3FFFF; return (o <= 0x2C && !(o & 3)) ? (int)(o >> 2) : -1; }
  static int size_of(int k) {
    if (k == R_TOCR || k == R_TSTR) return SZ_B;
    if (k == R_TCR0 || k == R_TCR1 || k == R_TCR2) return SZ_W;
    return SZ_L;
  }
  uint32_t reg_val(int k) const {
    switch (k) {
      case R_TOCR: return tocr & 1;
      case R_TSTR: return tstr & 7;
      case R_TCOR0: return tcor[0]; case R_TCNT0: return tcnt[0]; case R_TCR0: return tcr[0] & 0x013F;
      case R_TCOR1: return tcor[1]; case R_TCNT1: return tcnt[1]; case R_TCR1: return tcr[1] & 0x013F;
      case R_TCOR2: return tcor[2]; case R_TCNT2: return tcnt[2]; case R_TCR2: return tcr[2] & 0x03FF;
      default: return tcpr2;
    }
  }
  bool mstp_now = false;                 // the owner sets it before strobe() (register access off while module-stopped)
  Resp strobe(const Strobe &s) {
    Resp r;
    const int k = reg_of(s.off);
    if (!(s.rd || s.wr) || k < 0) return r;
    r.hit = true;
    if (mstp_now) return r;               // [P] reads 0, writes ignored while MSTP2 (like the RTC's MSTP1 [HW])
    if (s.rd && s.size == size_of(k)) r.rdata = reg_val(k);
    if (s.wr && s.size == size_of(k)) { pend = s; has_pend = true; }
    return r;
  }
  void step(const In &in) {
    Strobe w = pend; const bool have = has_pend && w.wr; has_pend = false;
    // next_evt helpers (tb-only); the TCLK output toggle runs whatever the reset
    if (in.rst.por) { gap = 0; plen = 4; } else if (in.ce.pce) { plen = gap + 1; gap = 0; } else gap++;
    if (in.rtc_tick) tclk_tog = !tclk_tog;
    if (in.rst.por || in.rst.man) {
      tocr = 0; tstr = 0;
      for (int n = 0; n < 3; n++) { tcor[n] = ~0u; tcnt[n] = ~0u; tcr[n] = 0; }
      presc = 0; t1 = t2 = t3 = rtc_pend = dreq = false;
      return;                             // TCPR2 is never initialised (hm 12.2.6); tclk_tog follows the RTC
    }
    const bool pstop = in.mstp || in.stby;
    bool e[3]; for (int n = 0; n < 3; n++) e[n] = ev(n, in);
    const bool c = cap(in);
    const int k = have ? reg_of(w.off) : -1;
    // channels
    uint32_t ncnt[3], ncor[3]; uint16_t ncr[3];
    for (int n = 0; n < 3; n++) {
      ncnt[n] = tcnt[n]; ncor[n] = tcor[n]; ncr[n] = tcr[n];
      bool unf = false;
      if (e[n]) { if (tcnt[n] == 0) { ncnt[n] = tcor[n]; unf = true; } else ncnt[n] = tcnt[n] - 1; }
      if (k == R_TCNT0 + 3 * n && !((tstr >> n) & 1)) ncnt[n] = w.wdata;       // ignored while counting [HW]
      if (k == R_TCOR0 + 3 * n) ncor[n] = w.wdata;
      if (k == R_TCR0 + 3 * n) {
        const uint16_t m = n == 2 ? 0x00FF : 0x003F;                            // plain fields
        ncr[n] = (tcr[n] & ~m) | (w.wdata & m);
        if (!(w.wdata & 0x100)) ncr[n] &= ~0x100;                               // UNF: 0 clears, 1 no effect [HW]
        if (n == 2 && !(w.wdata & 0x200)) ncr[n] &= ~0x200;                     // ICPF: same rule [M]
      }
      if (unf) ncr[n] |= 0x100;                                                 // the set wins over a clear
    }
    if (c) {
      if (!(tcr[2] & 0x200)) tcpr2 = tcnt[2];                                   // captured only while ICPF = 0
      ncr[2] |= 0x200;
    }
    // TSTR: written, then cleared in module stop / standby except the channels on the RTC clock [HW] / [M]
    uint8_t nstr = k == R_TSTR ? (uint8_t)(w.wdata & 7) : tstr;
    if (pstop) for (int n = 0; n < 3; n++) if (tpsc(tcr[n]) != 6) nstr &= ~(1u << n);
    if (k == R_TOCR) tocr = w.wdata & 1;
    // DMAC request: raised by a capture unless one is outstanding; dropped by dack_ic
    dreq = dreq ? !in.dack_ic : c;
    // prescaler, TCLK synchroniser, RTC tick latch
    if (in.ce.pce && !pstop) presc = (presc + 1) & 0x3FF;
    if (in.ce.pce) { t3 = t2; t2 = t1; t1 = in.tclk_in; }
    rtc_pend = (rtc_pend || in.rtc_tick) && !(in.ce.pce || in.stby);
    for (int n = 0; n < 3; n++) { tcnt[n] = ncnt[n]; tcor[n] = ncor[n]; tcr[n] = ncr[n]; }
    tstr = nstr;
  }
  bool rd(uint32_t off, int size, uint32_t &v, bool dma) override {
    Strobe s; s.rd = true; s.size = size; s.off = off; s.dma = dma; Resp r = strobe(s); v = r.rdata; return r.hit;
  }
  bool wr(uint32_t off, int size, uint32_t v, bool dma) override;
  bool is_volatile(uint32_t off, int) const override {
    const int k = reg_of(off);
    return k == R_TSTR || k == R_TCNT0 || k == R_TCNT1 || k == R_TCNT2 || k == R_TCR0 || k == R_TCR1 || k == R_TCR2 ||
           k == R_TCPR2;
  }
};

// ================================================================================================================
// RTC (timers-clock 3.4, 4.3, 7.5)
// ================================================================================================================
// BCD helpers.  A counter field carries (and restarts) when its value is >= the field's last value, compared as the
// raw BCD byte ([HW] tc4, also for invalid BCD); otherwise it counts in BCD: a low digit of 9 or more becomes 0 and
// carries.
inline uint32_t bcd_inc2(uint32_t v) { return (v & 0xF) >= 9 ? ((v & 0xF0) + 0x10) & 0xFF : v + 1; }
inline uint32_t bcd_inc4(uint32_t v) {
  uint32_t r = 0, c = 1;
  for (int d = 0; d < 4; d++) {
    uint32_t x = (v >> (4 * d)) & 0xF;
    if (c) { if (x >= 9) { x = 0; c = 1; } else { x++; c = 0; } }
    r |= x << (4 * d);
  }
  return r;                                              // 9999 -> 0000
}
// Gregorian leap rule on the BCD year, digit form: y % 100 != 0 ? (10 d1 + d0) % 4 == 0 : (10 d3 + d2) % 4 == 0
inline bool bcd_leap(uint32_t y) {
  const uint32_t d0 = y & 0xF, d1 = (y >> 4) & 0xF, d2 = (y >> 8) & 0xF, d3 = (y >> 12) & 0xF;
  return (d0 | d1) ? ((2 * d1 + d0) % 4 == 0) : ((2 * d3 + d2) % 4 == 0);
}
inline uint32_t bcd_mlen(uint32_t mon, uint32_t yr) {
  switch (mon) {
    case 0x02: return bcd_leap(yr) ? 0x29 : 0x28;
    case 0x04: case 0x06: case 0x09: case 0x11: return 0x30;
    default: return 0x31;                                // 01 03 05 07 08 10 12, and invalid months [HW] tc4
  }
}

struct Rtc : P4Slave {
  struct In { Rst rst; Ce ce; bool osc = false, mstp = false; };
  struct Out { bool rtc_tick = false, ati = false, pri = false, cui = false; };
  // counters: never reset (the silicon never initialises them; the model and the RTL start at 0)
  uint32_t pre = 0, r64 = 0; bool t2s = false;
  uint32_t sec = 0, min = 0, hr = 0, wk = 0, day = 0, mon = 0, yr = 0;
  uint32_t ar[6] = {0, 0, 0, 0, 0, 0};                   // RSECAR RMINAR RHRAR RWKAR RDAYAR RMONAR (bit 7 ENB)
  bool cf = false, cie = false, aie = false, af = false;
  bool pef = false, rtcen = true, start = true; uint32_t pes = 0;
  bool c128_seen = false;
  Strobe pend; bool has_pend = false;

  static const uint8_t *cmask() { static const uint8_t m[6] = {0x7F, 0x7F, 0x3F, 0x07, 0x3F, 0x1F}; return m; }
  static const uint8_t *amask() { static const uint8_t m[6] = {0xFF, 0xFF, 0xBF, 0x87, 0xBF, 0x9F}; return m; }
  uint32_t cnt(int i) const { switch (i) { case 0: return sec; case 1: return min; case 2: return hr; case 3: return wk;
                                           case 4: return day; default: return mon; } }
  bool match() const {
    bool any = false, all = true;
    for (int i = 0; i < 6; i++) {
      if (!(ar[i] & 0x80)) continue;
      any = true;
      if ((ar[i] & cmask()[i]) != cnt(i)) all = false;
    }
    return any && all;
  }
  bool osc_en(const In &in) const { return in.osc && rtcen; }
  bool c128(const In &in) const { return osc_en(in) && pre == 0xFF; }
  bool c1hz(const In &in) const { return c128(in) && r64 == 0x7F; }
  bool pev(const In &in) const {
    switch (pes) {
      case 1: return osc_en(in) && (pre & 0x7F) == 0x7F;                         // 1/256 s
      case 2: return c128(in) && (r64 & 1) == 1;                                  // 1/64 s
      case 3: return c128(in) && (r64 & 7) == 7;                                  // 1/16 s
      case 4: return c128(in) && (r64 & 0x1F) == 0x1F;                            // 1/4 s
      case 5: return c128(in) && (r64 & 0x3F) == 0x3F;                            // 1/2 s
      case 6: return c1hz(in);                                                    // 1 s
      case 7: return c1hz(in) && t2s;                                             // 2 s
      default: return false;
    }
  }
  Out out(const In &in) const {
    Out o;
    o.rtc_tick = osc_en(in) && (pre & 1);
    o.ati = af && aie && !in.mstp;
    o.pri = pef && pes != 0 && !in.mstp;
    o.cui = cf && cie && !in.mstp;
    return o;
  }
  // 00 R64CNT 04 SEC 08 MIN 0C HR 10 WK 14 DAY 18 MON 1C YR(16) 20-34 alarms 38 RCR1 3C RCR2
  static int reg_of(uint32_t off) { const uint32_t o = off & 0x3FFFF; return (o <= 0x3C && !(o & 3)) ? (int)(o >> 2) : -1; }
  static int size_of(int k) { return k == 7 ? SZ_W : SZ_B; }
  uint32_t reg_val(int k) const {
    switch (k) {
      case 0: return r64 & 0x7F;
      case 1: return sec; case 2: return min; case 3: return hr; case 4: return wk; case 5: return day; case 6: return mon;
      case 7: return yr;
      case 14: return (cf ? 0x80 : 0) | (cie ? 0x10 : 0) | (aie ? 0x08 : 0) | (af ? 1 : 0);
      case 15: return (pef ? 0x80 : 0) | (pes << 4) | (rtcen ? 8 : 0) | (start ? 1 : 0);
      default: return ar[k - 8];
    }
  }
  bool mstp_now = false;
  Resp strobe(const Strobe &s) {
    Resp r;
    const int k = reg_of(s.off);
    if (!(s.rd || s.wr) || k < 0) return r;
    r.hit = true;
    if (mstp_now) return r;                                // MSTP1: reads 0, writes ignored [HW]
    if (s.size != size_of(k)) return r;                    // [P] the size rule, as the TMU / WDT [HW]
    if (s.rd) r.rdata = reg_val(k);
    pend = s; has_pend = true;                             // a write, or an R64CNT read (CF on a colliding carry)
    return r;
  }
  void step(const In &in) {
    Strobe w = pend; const bool have = has_pend; has_pend = false;
    const int k = have ? reg_of(w.off) : -1;
    const bool wr = have && w.wr;
    const bool rd64 = have && w.rd && k == 0;
    const bool cc128 = c128(in), cc1 = c1hz(in), pe = pev(in), am = match();
    const bool adj = wr && k == 15 && (w.wdata & 4), rdiv = wr && k == 15 && (w.wdata & 2);
    const bool start_new = (wr && k == 15) ? (w.wdata & 1) : start;
    // divider chain (osc domain): ADJ / RESET clear the prescaler and R64CNT
    const uint32_t npre = (adj || rdiv) ? 0 : osc_en(in) ? ((pre + 1) & 0xFF) : pre;
    const uint32_t nr64 = (adj || rdiv) ? 0 : cc128 ? ((r64 + 1) & 0x7F) : r64;
    const bool nt2s = (adj || rdiv) ? false : cc1 ? !t2s : t2s;
    // calendar: a counter written this cycle counts from the written value (hm 11.3.1: a carry updates the write)
    uint32_t base[7] = {sec, min, hr, wk, day, mon, yr};
    if (wr && k >= 1 && k <= 6) base[k - 1] = w.wdata & cmask()[k - 1];
    if (wr && k == 7) base[6] = w.wdata & 0xFFFF;
    uint32_t n[7]; for (int i = 0; i < 7; i++) n[i] = base[i];
    bool mc;
    if (adj) { n[0] = 0; mc = start_new && base[0] >= 0x30; }                  // 30-second adjustment
    else {
      const bool si = cc1 && start;
      mc = si && base[0] >= 0x59;
      if (si) n[0] = mc ? 0 : bcd_inc2(base[0]) & 0x7F;
    }
    const bool hc = mc && base[1] >= 0x59;
    if (mc) n[1] = hc ? 0 : bcd_inc2(base[1]) & 0x7F;
    const bool dc = hc && base[2] >= 0x23;
    if (hc) n[2] = dc ? 0 : bcd_inc2(base[2]) & 0x3F;
    if (dc) n[3] = base[3] >= 6 ? 0 : base[3] + 1;
    const uint32_t ml = bcd_mlen(base[5], base[6]);
    const bool moc = dc && base[4] >= ml;
    if (dc) n[4] = moc ? 1 : bcd_inc2(base[4]) & 0x3F;
    const bool yc = moc && base[5] >= 0x12;
    if (moc) n[5] = yc ? 1 : bcd_inc2(base[5]) & 0x1F;
    if (yc) n[6] = bcd_inc4(base[6]);
    // control registers
    bool ncf = cf, ncie = cie, naie = aie, naf = af, npef = pef, nrtcen = rtcen, nstart = start; uint32_t npes = pes;
    if (wr && k == 14) { ncf = w.wdata & 0x80; ncie = w.wdata & 0x10; naie = w.wdata & 0x08; if (!(w.wdata & 1)) naf = false; }
    if (wr && k == 15) { npef = w.wdata & 0x80; npes = (w.wdata >> 4) & 7; nrtcen = w.wdata & 8; nstart = w.wdata & 1; }
    if (cc1 && start) ncf = true;                                               // seconds carry (not the ADJ carry [HW])
    if (rd64 && (c128_seen || cc128)) ncf = true;                                // R64CNT read meets a 128 Hz carry
    if (am && rtcen) naf = true;                    // continuous comparator [HW] tc3, gated by RTCEN [HW] tc4
    if (pe) npef = true;
    uint32_t nar[6]; for (int i = 0; i < 6; i++) nar[i] = ar[i];
    if (wr && k >= 8 && k <= 13) nar[k - 8] = w.wdata & amask()[k - 8];
    // resets: the counters and the alarm values never; ENB, RCR1, RCR2 as hm Table 11.2
    if (in.rst.por) {
      for (int i = 0; i < 6; i++) nar[i] &= 0x7F;
      ncf = ncie = naie = naf = false; npef = false; npes = 0; nrtcen = true; nstart = true;
    } else if (in.rst.man) {
      ncie = naie = false; npes = 0;                                             // CF AF PEF held [P], RTCEN START held
    }
    pre = npre; r64 = nr64; t2s = nt2s;
    // during a reset no strobe arrives; the chain still counts
    sec = n[0]; min = n[1]; hr = n[2]; wk = n[3]; day = n[4]; mon = n[5]; yr = n[6];
    for (int i = 0; i < 6; i++) ar[i] = nar[i];
    cf = ncf; cie = ncie; aie = naie; af = naf; pef = npef; pes = npes; rtcen = nrtcen; start = nstart;
    c128_seen = in.ce.pce ? false : (c128_seen || cc128);
  }
  bool rd(uint32_t off, int size, uint32_t &v, bool dma) override {
    Strobe s; s.rd = true; s.size = size; s.off = off; s.dma = dma; Resp r = strobe(s); v = r.rdata;
    has_pend = false;                                      // functional mode: no carry collides with a read
    return r.hit;
  }
  bool wr(uint32_t off, int size, uint32_t v, bool dma) override;
  bool is_volatile(uint32_t off, int) const override { const int k = reg_of(off); return (k >= 0 && k <= 7) || k == 14 || k == 15; }
};

// ================================================================================================================
// The five units wired as v1_soc wires them (SOC.md 2), for the stand-alone / functional mode and the unit tbs
// ================================================================================================================
struct Sh4Timers {
  Rstc rstc; Cpg cpg; Wdt wdt; Tmu tmu; Rtc rtc;
  // pins / inputs from the rest of the chip (levels, held until changed)
  unsigned md = 5;
  bool reset_n = true, sck2 = true, hudi = false, mhit = false, arr_mhit = false, cpu_rst_req = false;
  bool sleep_req = false, wake = false, wake_stby = false, osc = false, tclk_in = false, dack_ic = false;
  bool p4_wpend = false;                               // a Pclk write in flight (the bridge's pclk_wpend)
  uint64_t icyc = 0;
  // this cycle's outputs (valid after eval())
  Rst rst; Cpg::Out co; Wdt::Out wo; Tmu::Out to; Rtc::Out ro; int rst_code = 0;

  Cpg::In cin() const { Cpg::In i; i.rst = rst; i.md = md; i.sleep_req = sleep_req; i.wake = wake; i.wake_stby = wake_stby;
                        i.wdt_ovf = wo.settle_ovf; i.p4_wpend = p4_wpend; return i; }
  Wdt::In win() const { Wdt::In i; i.rst = rst; i.div = co.div; i.settle = co.wdt_settle; return i; }
  Tmu::In tin() const { Tmu::In i; i.rst = rst; i.ce = co.ce; i.stby = co.stby; i.mstp = (co.mstp >> 2) & 1;
                        i.tclk_in = tclk_in; i.rtc_tick = ro.rtc_tick; i.dack_ic = dack_ic; return i; }
  Rtc::In rin() const { Rtc::In i; i.rst = rst; i.ce = co.ce; i.osc = osc; i.mstp = (co.mstp >> 1) & 1; return i; }
  // combinational outputs of the current cycle, in dependency order
  void eval() {
    rst = rstc.rst(); rst_code = rstc.code();
    Cpg::In ci; ci.rst = rst; co = cpg.out(ci);
    wo = wdt.out(win());
    ro = rtc.out(rin());
    to = tmu.out(tin());
    tmu.mstp_now = (co.mstp >> 2) & 1; rtc.mstp_now = (co.mstp >> 1) & 1;
  }
  // the clock edge
  void edge() {
    const Cpg::In ci = cin(); const Wdt::In wi = win(); const Tmu::In ti = tin(); const Rtc::In ri = rin();
    Rstc::In si; si.reset_n = reset_n; si.sck2 = sck2; si.wdt_por = wo.rst_por; si.wdt_man = wo.rst_man; si.hudi = hudi;
    si.mhit = mhit; si.arr_mhit = arr_mhit; si.cpu_req = cpu_rst_req;
    cpg.step(ci); wdt.step(wi); tmu.step(ti); rtc.step(ri); rstc.step(si);
    icyc++;
  }
  void cycle() { eval(); edge(); }
  void tick_icyc(uint64_t n) { while (n--) cycle(); eval(); }
  // a power-on reset through the pin, as the board does it (SCK2 high)
  void power_on(int cycles = 16) {
    reset_n = false; sck2 = true; for (int i = 0; i < cycles; i++) cycle();
    reset_n = true; for (int i = 0; i < 16; i++) cycle();
    eval();
  }
  // the P4Slave of a p4blk_e value (v1_soc_pkg: P4B_CPG 23, P4B_WDT 24, P4B_RTC 25, P4B_TMU 27); nullptr otherwise
  P4Slave *slave(int blk) {
    switch (blk) { case 23: return &cpg; case 24: return &wdt; case 25: return &rtc; case 27: return &tmu; default: return nullptr; }
  }
};

// functional-mode writes: the strobe, then its effect at once (a cycle with no clock enables, no oscillator tick and
// the divider unchanged).  The WDT's edge detector and the TMU / RTC chains do not move in it.
inline bool Cpg::wr(uint32_t off, int size, uint32_t v, bool dma) {
  Strobe s; s.wr = true; s.size = size; s.off = off; s.wdata = v; s.dma = dma; Resp r = strobe(s);
  if (!has_pend) return r.hit;
  const int pc = pcnt, bc = bcnt, pd = pdiv, bd = bdiv; const uint16_t d = div; const bool sq = sleep_q;
  In i; i.sleep_req = sleep_q; step(i);
  pcnt = pc; bcnt = bc; pdiv = pd; bdiv = bd; div = d; sleep_q = sq;
  return r.hit;
}
inline bool Wdt::wr(uint32_t off, int size, uint32_t v, bool dma) {
  Strobe s; s.wr = true; s.size = size; s.off = off; s.wdata = v; s.dma = dma; Resp r = strobe(s);
  if (!has_pend) return r.hit;
  const bool tq = tap_q, sq = settle_q, rl = reload; In i; i.div = 0; step(i);
  tap_q = tq; settle_q = sq; reload = rl;
  return r.hit;
}
inline bool Tmu::wr(uint32_t off, int size, uint32_t v, bool dma) {
  Strobe s; s.wr = true; s.size = size; s.off = off; s.wdata = v; s.dma = dma; Resp r = strobe(s);
  if (!has_pend) return r.hit;
  const uint32_t g = gap, pl = plen; In i; i.mstp = mstp_now; step(i); gap = g; plen = pl;
  return r.hit;
}
inline bool Rtc::wr(uint32_t off, int size, uint32_t v, bool dma) {
  Strobe s; s.wr = true; s.size = size; s.off = off; s.wdata = v; s.dma = dma; Resp r = strobe(s);
  if (!has_pend) return r.hit;
  const bool cs = c128_seen; In i; i.mstp = mstp_now; step(i); c128_seen = cs;
  return r.hit;
}

}  // namespace sh4t
