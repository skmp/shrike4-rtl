// v1_dmac_int.cpp -- WI-DMAC integration test: v1_dmac with the real v1_bsc, v1_p4bus and v1_sci (tb/v1_dmac_int_top.sv).
// `make v1-dmac-int`.  Not part of `make v1-dmac`: it builds other work items' RTL as it is on disk, so a failure here
// may be theirs; the unit testbench (tb/v1_dmac_tb.cpp) is the DMAC's gate.  Adapted from the independent verifier's
// vd_int.cpp / vp_int.cpp (agents/verify/WI-DMAC/int).  Groups (--only <name>):
//   data     dual-address auto-request copies of every TS at every quadword of a line, burst and cycle steal, through
//            the real BSC and a C++ SDRAM board: the destination must equal the source (WI-DMAC-verify F1; SOC.md
//            3.3.2: rd_beat is addr[4:3] for a 1 / 2 / 4 / 8-byte read, which is one rd_v)
//   single   CH2-DMA (CHCR2 0x12C1, direct channel-2 requests) and a channel-0 DTR: the DACK device gets the bytes
//   cpu      a CPU uncached-read loop on the BSC's CPU port: no CPU access inside a burst (dmac2 T4), strict
//            alternation of one CPU read and one unit in cycle steal (dmac2 T5)
//   preempt  a burst channel preempted by a higher burst channel keeps the bus until it ends (hm 14.3.4; F4)
//   sci      SCI TX by the DMAC (RS 1000, TDRE request, SCTDR1 auto-clear) through the real bridge, with and without
//            CPU byte / word / longword filler writes on the Pclk path: all 40 bytes on TXD, in order, and no SCTDR1
//            write while TDRE = 0.  This rests on SOC.md 3.1.1 (I085): a DMAC write is never posted, it is acked in the
//            slave's capture cycle, after the SCI has applied it (auto-clear), so the DMAC's next TDRE sample sees the
//            cleared flag.  With a posted DMAC write (WI-DMAC-verify F2) the longword-filler runs lose every second
//            byte: the SCTDR1 strobe came 3-5 Icyc after the ack and the DMAC re-sampled the old TDRE.
//   rates    Icyc per unit.  Hard check: a DMAC 8-byte unit is not split, so it costs what a 1 / 2 / 4-byte unit costs
//            (SOC.md 3.3.2, I087 [HW dmac 3.13 obs. 1: every size 46.26-46.46 Icyc in burst]); the absolute values
//            and the 32-byte and CH2 rows are printed for WI-BSC's M3 calibration (dmac1 S10 / S11 / S13).
#include "Vv1_dmac_int_top.h"
#include "verilated.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0, g_checks = 0;
static std::string g_test;
static void failf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  if (g_fail < 80) { printf("FAIL [%s] ", g_test.c_str()); vprintf(fmt, ap); printf("\n"); }
  va_end(ap);
  g_fail++;
}
#define CHECK(c, ...) do { g_checks++; if (!(c)) failf(__VA_ARGS__); } while (0)

struct Sim {
  Vv1_dmac_int_top *t = new Vv1_dmac_int_top;
  uint64_t cyc = 0, bt = 0;
  std::vector<uint8_t> ram = std::vector<uint8_t>(16u << 20, 0);
  // CPU master 0: one register access at a time, else filler writes
  bool m_act = false, m_we = false, m_done = false;
  int m_size = 2;
  uint32_t m_addr = 0, m_wd = 0, m_rd = 0;
  bool filler = false, fill_act = false;
  int fill_gap = 2, fill_size = 2;
  uint64_t fill_next = 0, fills = 0;
  // CPU requester on the BSC: uncached longword reads in a loop
  bool cpu_on = false;
  int cpu_st = 0, cpu_think = 7;
  uint64_t cpu_next = 0;
  // DDT
  int ch2_pending = 0;
  bool dtr_pending = false;
  uint64_t dtr_val = 0;
  std::vector<uint8_t> sink;                 // single address memory -> device bytes
  int c_area = 0;
  bool c_we = false, c_dack = false;
  // logs
  std::string acc;                           // accepted bus requests: 'C' CPU, else the DMAC channel
  // (M6-fix) a CPU accept between a DMAC write's accept and its unit's end (the dual pair holds the bus until its write
  // is done, v1_bsc_arb lk_st 3): counted; with the burst session also holding the bus (dma_ses) this is what still
  // tells a missing pair hold in cycle steal
  bool pair_wr = false;
  int pair_cpu = 0;
  std::vector<uint64_t> upd_cyc;
  // SCI
  int tdr_writes = 0, overruns = 0;
  int u_st = 0, u_prev = 1, bit = 128;
  uint64_t u_t0 = 0;
  uint32_t u_b = 0;
  std::vector<uint8_t> line;

  ~Sim() { t->final(); delete t; }
  bool bce() const { return cyc & 1; }
  uint8_t &mb(uint32_t a) { return ram[a & 0xFFFFFF]; }

  void tick() {
    t->clk = 0;
    t->bce = bce(); t->pce = (cyc & 3) == 3;
    if (!m_act && filler && !fill_act && cyc >= fill_next) fill_act = true;
    bool use_fill = !m_act && fill_act;
    t->m0_v = m_act || use_fill;
    t->m0_we = m_act ? m_we : 1;
    t->m0_size = m_act ? m_size : fill_size;
    t->m0_addr = m_act ? m_addr : (fill_size == 2 ? 0xFFD80008u : fill_size == 1 ? 0xFFD80010u : 0xFFD80000u);
    t->m0_wdata = m_act ? m_wd : 0;
    if (cpu_on && cpu_st == 0 && cyc >= cpu_next) cpu_st = 1;
    t->c_v = cpu_on && cpu_st == 1; t->c_addr = 0x0C300000u;
    t->tl_dtr_v = bce() && dtr_pending; t->tl_dtr = dtr_val; t->tl_trrep = 0;
    t->tl_ch2 = bce() && ch2_pending > 0;
    t->xb_rdy = 0; t->xb_rdata = 0;
    t->eval();
    if (bce()) {                                 // the board: SDRAM beats answered in the cycle presented
      bt++;
      if (t->xb_start) { c_area = t->xb_area; c_we = t->xb_we; c_dack = t->xb_dack; }
      if (t->xb_beat) {
        uint64_t rd = 0;
        uint32_t ba = t->xb_baddr & ~7u;
        if (c_area != 3) failf("board: a beat outside area 3 (area %d)", c_area);
        for (int k = 0; k < 8; k++) {
          if (c_dack && !c_we) { rd |= (uint64_t)mb(ba + k) << (8 * k); if ((t->xb_be >> k) & 1) sink.push_back(mb(ba + k)); }
          else if (c_we) { if ((t->xb_be >> k) & 1) mb(ba + k) = (uint8_t)(t->xb_wdata >> (8 * k)); }
          else rd |= (uint64_t)mb(ba + k) << (8 * k);
        }
        t->xb_rdy = 1; t->xb_rdata = rd;
      }
      t->eval();
    }
    if (bce()) {
      if (t->c_ready && pair_wr) pair_cpu++;
      if (t->t_dma_ready && t->t_dma_we) pair_wr = true;
      if (t->t_upd_v) pair_wr = false;
      if (t->c_ready) acc += 'C';
      if (t->t_dma_ready) acc += (char)('0' + t->t_dma_ch);
      if (t->t_upd_v) upd_cyc.push_back(cyc);
      if (t->tl_ch2) ch2_pending--;
      if (t->tl_dtr_v) dtr_pending = false;
      if (cpu_on && cpu_st == 1 && t->c_ready) cpu_st = 2;
      if (cpu_on && cpu_st == 2 && t->c_rd_v && t->c_rd_last) { cpu_st = 0; cpu_next = cyc + cpu_think; }
    }
    if (t->t_tdr_dma_wr) { tdr_writes++; if (!t->t_tdre) overruns++; }
    if (t->m0_ack) {
      if (m_act) { m_act = false; m_done = true; m_rd = t->m0_rdata; }
      else if (fill_act) { fill_act = false; fills++; fill_next = cyc + fill_gap; }
    }
    // TXD decoder (8N1): a start bit is a falling edge while idle; sample the middle of each bit
    int tx = t->txd_oe ? t->txd : 1;
    if (u_st == 0) { if (u_prev == 1 && tx == 0) { u_st = 1; u_t0 = cyc; } }
    else {
      uint64_t k = (cyc - u_t0) / (uint64_t)bit, ph = (cyc - u_t0) % (uint64_t)bit;
      if (ph == (uint64_t)bit / 2) {
        if (k >= 1 && k <= 8) u_b |= (uint32_t)tx << (k - 1);
        if (k == 9) { line.push_back((uint8_t)u_b); u_b = 0; u_st = 0; }
      }
    }
    u_prev = tx;
    t->clk = 1;
    t->eval();
    cyc++;
  }
  void ticks(int n) { for (int k = 0; k < n; k++) tick(); }
  uint32_t access(bool we, uint32_t a, int size, uint32_t v = 0) {
    while (fill_act) tick();
    m_act = true; m_we = we; m_addr = a; m_size = size; m_wd = v; m_done = false;
    int g = 0;
    while (!m_done && g++ < 100000) tick();
    if (!m_done) failf("CPU access to %08x never completed", a);
    ticks(3);
    return m_rd;
  }
  void dwr(uint32_t off, uint32_t v) { access(true, 0xFFA00000u | off, 2, v); }
  uint32_t drd(uint32_t off) { return access(false, 0xFFA00000u | off, 2); }
  uint32_t tcr(int ch) const { return ch == 0 ? t->t_tcr0 : ch == 1 ? t->t_tcr1 : ch == 2 ? t->t_tcr2 : t->t_tcr3; }
  bool wait_tcr(int ch, int max = 3000000) {
    for (int k = 0; k < max; k++) { if (tcr(ch) == 0) return true; tick(); }
    failf("channel %d never reached DMATCR 0", ch);
    return false;
  }
  void reset() {
    t->rst_pin = 1; t->rst_por = 1; t->rst_man = 0; t->rst_cpu = 1;
    ticks(16);
    t->rst_pin = 0; t->rst_por = 0; t->rst_cpu = 0;
    ticks(8);
    // the Dreamcast BSC setting (board.md; tb/v1_bsc_tb.cpp): BCR1 A3020008, BCR2 0001, WCR1 01110111, WCR2 618066D8,
    // WCR3 07777777, MCR C00A0E24, PCR 0; refresh stopped (RTCSR 0), so the rates are the refresh-free ones
    access(true, 0xFF800000u, 2, 0xA3020008u); access(true, 0xFF800004u, 1, 0x0001);
    access(true, 0xFF800008u, 2, 0x01110111u); access(true, 0xFF80000Cu, 2, 0x618066D8u);
    access(true, 0xFF800010u, 2, 0x07777777u); access(true, 0xFF800014u, 2, 0xC00A0E24u);
    access(true, 0xFF800018u, 1, 0); access(true, 0xFF800024u, 1, 0xA55E); access(true, 0xFF800020u, 1, 0xA500);
    access(true, 0xFF80001Cu, 1, 0xA500);
    acc.clear(); upd_cyc.clear(); sink.clear(); pair_wr = false; pair_cpu = 0;
  }
};

static uint32_t SAR(int c) { return 0x10u * c; }
static uint32_t DAR(int c) { return 0x10u * c + 4; }
static uint32_t TCR(int c) { return 0x10u * c + 8; }
static uint32_t CHCR(int c) { return 0x10u * c + 0xC; }
static const uint32_t DMAOR = 0x40;
static uint32_t CH(int dm, int sm, int rs, bool tm, int ts) {
  return (uint32_t)dm << 14 | (uint32_t)sm << 12 | (uint32_t)rs << 8 | (tm ? 0x80u : 0) | (uint32_t)ts << 4;
}
static void prog(Sim &s, int ch, uint32_t sar, uint32_t dar, uint32_t n, uint32_t chcr) {
  (void)s.drd(CHCR(ch)); s.dwr(CHCR(ch), 0);
  s.dwr(SAR(ch), sar); s.dwr(DAR(ch), dar); s.dwr(TCR(ch), n); s.dwr(CHCR(ch), chcr);
}

// ---------------------------------------------------------------------------------------------------------------
static void t_data(Sim &s) {
  static const int tss[5] = {1, 2, 3, 0, 4}, ub[5] = {1, 2, 4, 8, 32};
  for (int tm = 1; tm >= 0; tm--)
    for (int k = 0; k < 5; k++)
      for (int q = 0; q < 4; q++) {
        if (ub[k] == 32 && q) continue;
        char tag[64];
        snprintf(tag, sizeof tag, "data TS=%d (%d B) %s, SAR quadword %d", tss[k], ub[k], tm ? "burst" : "steal", q);
        g_test = tag;
        s.reset();
        s.dwr(DMAOR, 0x8201);
        const int n = 8;
        uint32_t src = 0x0C100000u + (ub[k] == 32 ? 0 : 8u * (uint32_t)q);
        uint32_t dst = 0x0C200000u + (ub[k] == 32 ? 0 : 8u * (uint32_t)((q + 1) & 3));
        for (int i = 0; i < 512; i++) { s.mb(src - 64 + i) = (uint8_t)(0x40 + i * 7 + q); s.mb(dst - 64 + i) = 0xEE; }
        prog(s, 1, src, dst, n, CH(1, 1, 4, tm, tss[k]) | 1);
        s.wait_tcr(1); s.ticks(400);
        int bad = 0, first = -1;
        for (int i = 0; i < n * ub[k]; i++) if (s.mb(dst + i) != s.mb(src + i)) { bad++; if (first < 0) first = i; }
        CHECK(bad == 0, "%d of %d destination bytes differ from the source (first at +%d: %02x, want %02x)", bad,
              n * ub[k], first, first >= 0 ? s.mb(dst + first) : 0, first >= 0 ? s.mb(src + first) : 0);
        CHECK(s.mb(dst + n * ub[k]) == 0xEE && s.mb(dst - 1) == 0xEE, "overrun / underrun");
        CHECK(s.drd(DMAOR) == 0x8201, "no address error");
      }
}

static void t_single(Sim &s) {
  g_test = "single CH2-DMA";
  s.reset();
  s.dwr(DMAOR, 0x8201);
  for (int i = 0; i < 64 * 32; i++) s.mb(0x0C100000u + i) = (uint8_t)(i * 13 + 5);
  prog(s, 2, 0x0C100000u, 0, 64, 0x12C1);
  int sent = 0;
  for (int k = 0; k < 400000 && s.tcr(2) != 0; k++) {
    if (sent < 64 && s.ch2_pending == 0 && sent - (int)(64 - s.tcr(2)) < 4) { s.ch2_pending = 1; sent++; }
    s.tick();
  }
  s.ticks(300);
  bool ok = s.sink.size() == 64u * 32;
  for (size_t i = 0; ok && i < s.sink.size(); i++) if (s.sink[i] != s.mb(0x0C100000u + (uint32_t)i)) ok = false;
  CHECK(ok, "CH2 single address: %zu device bytes, content %s", s.sink.size(), ok ? "ok" : "differs");
  CHECK(s.drd(CHCR(2)) == 0x12C3, "CHCR2 TE after 64 units");
  // a channel-0 maple-style DTR: 32 bytes memory -> device, MD 01, COUNT 2
  g_test = "single channel-0 DTR";
  s.sink.clear();
  s.dtr_val = (uint64_t)4 << 61 | (uint64_t)1 << 56 | (uint64_t)2 << 48 | 0x0C100400u;
  while (!s.bce()) s.tick();
  s.dtr_pending = true;
  s.ticks(600);
  ok = s.sink.size() == 64;
  for (size_t i = 0; ok && i < 64; i++) if (s.sink[i] != s.mb(0x0C100400u + (uint32_t)i)) ok = false;
  CHECK(ok, "channel-0 DTR: %zu device bytes, content %s", s.sink.size(), ok ? "ok" : "differs");
  CHECK(s.drd(CHCR(0)) == 0x000852C3, "CHCR0 after the DTR's units (0x852C3, dmac1 S1)");
}

static void t_cpu(Sim &s) {
  for (int tm = 1; tm >= 0; tm--)
    for (int ts = 4; ts >= 3; ts--) {
      char tag[64];
      snprintf(tag, sizeof tag, "cpu vs %s TS=%d", tm ? "burst" : "cycle steal", ts);
      g_test = tag;
      s.reset();
      s.dwr(DMAOR, 0x8201);
      (void)s.drd(CHCR(1)); s.dwr(CHCR(1), 0);
      s.dwr(SAR(1), 0x0C100000u); s.dwr(DAR(1), 0x0C200000u); s.dwr(TCR(1), 64);
      s.cpu_on = true; s.cpu_st = 0; s.cpu_next = s.cyc;
      s.ticks(200);
      s.acc.clear(); s.pair_cpu = 0;
      s.dwr(CHCR(1), CH(1, 1, 4, tm, ts) | 1);
      s.wait_tcr(1); s.ticks(200);
      s.cpu_on = false; s.ticks(100);
      size_t f = s.acc.find('1'), l = s.acc.rfind('1');
      std::string pat = f == std::string::npos ? "" : s.acc.substr(f, l - f + 1);
      int inside = 0;
      for (char c : pat) inside += c == 'C';
      CHECK(s.pair_cpu == 0, "%s: %d CPU accesses accepted inside a DMAC pair's write (between its accept and its end)",
            tm ? "burst" : "cycle steal", s.pair_cpu);
      if (tm) CHECK(inside == 0, "burst: %d CPU accesses inside the burst: %s", inside, pat.substr(0, 90).c_str());
      else {
        std::string want;
        for (int k = 0; k < 64; k++) want += k ? "C11" : "11";
        CHECK(pat == want, "cycle steal: not strict alternation: %s", pat.substr(0, 90).c_str());
      }
      printf("  %-24s %s%s\n", tag, pat.substr(0, 60).c_str(), pat.size() > 60 ? "..." : "");
    }
}

static void t_preempt(Sim &s) {
  for (int tm0 = 0; tm0 < 2; tm0++) {
    g_test = tm0 ? "preempt: ch0 burst over ch1 burst" : "preempt: ch0 cycle steal over ch1 burst";
    s.reset();
    s.dwr(DMAOR, 0x0001);                                   // PR = 00, normal mode (CHCR0 writable)
    prog(s, 1, 0x0C100000u, 0x0C200000u, 60, CH(1, 1, 4, true, 3));
    prog(s, 0, 0x0C140000u, 0x0C240000u, 8, CH(1, 1, 4, tm0, 3));
    s.cpu_on = true; s.cpu_st = 0; s.cpu_next = s.cyc;
    s.acc.clear();
    s.dwr(CHCR(1), CH(1, 1, 4, true, 3) | 1);
    s.ticks(300);
    s.dwr(CHCR(0), CH(1, 1, 4, tm0, 3) | 1);
    s.wait_tcr(1); s.ticks(200);
    s.cpu_on = false; s.ticks(100);
    size_t f = s.acc.find('1'), l = s.acc.rfind('1');
    std::string pat = f == std::string::npos ? "" : s.acc.substr(f, l - f + 1);
    size_t c = pat.find('C');
    CHECK(c == std::string::npos && pat.find('0') != std::string::npos,
          "a CPU cycle inside channel 1's burst session (or no channel 0): ...%s...",
          pat.substr(c != std::string::npos && c > 20 ? c - 20 : 0, 50).c_str());
    printf("  %-42s %s\n", g_test.c_str(), pat.substr(0, 70).c_str());
  }
}

static int sci_run(Sim &s, bool filler, int fsz, int gap) {
  s.reset();
  s.tdr_writes = s.overruns = 0; s.line.clear(); s.u_st = 0; s.u_prev = 1;
  for (int i = 0; i < 64; i++) s.mb(0x0C000000u + i) = (uint8_t)(0x30 + i);
  // SCI: asynchronous 8N1, SCBRR1 0 (a bit every 128 Icyc), TE
  s.access(true, 0xFFE00008u, 0, 0x00); s.access(true, 0xFFE00000u, 0, 0x00); s.access(true, 0xFFE00004u, 0, 0x00);
  s.access(true, 0xFFE00008u, 0, 0x20);
  s.dwr(DMAOR, 0x0201);
  prog(s, 1, 0x0C000000u, 0x1FE0000Cu, 40, 0);
  s.filler = filler; s.fill_size = fsz; s.fill_gap = gap; s.fill_next = s.cyc;
  s.dwr(CHCR(1), 0x00001811u);                                // SM +, RS 1000 (SCI TXI), TS byte, cycle steal, DE
  for (int k = 0; k < 600000 && s.tcr(1) != 0; k++) s.tick();
  s.ticks(3000);
  s.filler = false; s.ticks(200);
  int good = 0;
  for (size_t i = 0; i < s.line.size() && i < 40; i++) good += s.line[i] == 0x30 + i;
  printf("  sci filler %-3s size %d gap %d: %d SCTDR1 writes, %d with TDRE = 0, TXD %zu bytes (%d in place)\n",
         filler ? "on" : "off", fsz, gap, s.tdr_writes, s.overruns, s.line.size(), good);
  return s.overruns + (good == 40 && s.line.size() == 40 ? 0 : 1);
}
static void t_sci(Sim &s) {
  g_test = "sci no filler";
  CHECK(sci_run(s, false, 0, 0) == 0, "SCI TX by the DMAC without CPU traffic: bytes lost or overwritten");
  for (int fsz = 0; fsz < 3; fsz++)
    for (int gap : {0, 3}) {
      char tag[64];
      snprintf(tag, sizeof tag, "sci filler size %d gap %d", fsz, gap);
      g_test = tag;
      CHECK(sci_run(s, true, fsz, gap) == 0, "SCI TX by the DMAC with CPU %s filler writes: bytes lost or SCTDR1 "
            "written while TDRE = 0 (SOC.md 3.1.1: a DMAC write is acked in the slave's capture cycle, never posted)",
            fsz == 2 ? "longword" : fsz == 1 ? "word" : "byte");
    }
}

static double mean_period(const std::vector<uint64_t> &v, size_t skip) {
  if (v.size() < skip + 2) return -1;
  return (double)(v.back() - v[skip]) / (double)(v.size() - 1 - skip);
}
static void t_rates(Sim &s) {
  g_test = "rates";
  static const int tss[5] = {1, 2, 3, 0, 4}, ub[5] = {1, 2, 4, 8, 32};
  for (int tm = 1; tm >= 0; tm--) {
    double r[5];
    for (int k = 0; k < 5; k++) {
      s.reset();
      s.dwr(DMAOR, 0x8201);
      prog(s, 1, 0x0C100000u, 0x0C200000u, 200, CH(1, 1, 4, tm, tss[k]) | 1);
      s.wait_tcr(1); s.ticks(100);
      r[k] = mean_period(s.upd_cyc, 10);
      printf("  rate %-11s %2d B: %.2f Icyc / unit (M3 target, console dmac1 %s)\n", tm ? "burst" : "cycle steal",
             ub[k], r[k], tm ? "S11: 46.26-46.46" : "S10: 50.0-50.6 (S11 52.19-52.39 while polling)");
    }
    // M3 (WI-SOC, SOC.md 8.3 "throughput 46.3 per unit at every size, cycle steal 50-52"): SDRAM -> SDRAM with both
    // legs in one bank's other rows, no refresh in this bench: burst 23 Bcyc per unit [HW dmac1 S11 46.26-46.46 with
    // the refresh share], cycle steal 25 Bcyc [HW dmac1 S10 50.0-50.6 with the CPU idle; S11's 52.19-52.39 was taken
    // while the CPU polled DMATCR, whose cost is not modelled]
    for (int k = 0; k < 5; k++) {
      const double lo = tm ? 45.9 : 49.9, hi = tm ? 46.5 : 50.7;
      CHECK(r[k] >= lo && r[k] <= hi, "%s %d B: %.2f Icyc per unit, the M3 target is %.1f-%.1f", tm ? "burst" : "cycle steal",
            ub[k], r[k], lo, hi);
    }
    // the unsplit 8-byte unit (SOC.md 3.3.2, I087): one bus cycle per leg like a 1 / 2 / 4-byte unit, so the same
    // cost; a split 8-byte unit (two longword cycles per leg) costs 24 Icyc more with v1_bsc
    for (int k = 0; k < 3; k++)
      CHECK(r[3] >= r[k] - 1.0 && r[3] <= r[k] + 1.0, "%s: an 8-byte unit costs %.2f Icyc against %.2f for %d B "
            "(SOC.md 3.3.2: a DMAC 8-byte unit is not split; [HW dmac 3.13 obs. 1])", tm ? "burst" : "cycle steal",
            r[3], r[k], ub[k]);
  }
  s.reset();
  s.dwr(DMAOR, 0x8201);
  prog(s, 2, 0x0C100000u, 0, 1024, 0x12C1);
  int sent = 0;
  for (int k = 0; k < 400000 && s.tcr(2) != 0; k++) {
    if (sent < 1024 && s.ch2_pending == 0 && sent - (int)(1024 - s.tcr(2)) < 4) { s.ch2_pending = 1; sent++; }
    s.tick();
  }
  printf("  rate CH2 single 32 B: %.2f Icyc / unit (M3 target, console dmac1 S13: 11.53)\n",
         mean_period(s.upd_cyc, 16));
}

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  std::string only;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i];
  }
  struct T { const char *name; void (*fn)(Sim &); };
  const T tests[] = {{"data", t_data}, {"single", t_single}, {"cpu", t_cpu}, {"preempt", t_preempt}, {"sci", t_sci},
                     {"rates", t_rates}};
  for (const T &x : tests) {
    if (!only.empty() && only != x.name) continue;
    Sim *s = new Sim;
    int f0 = g_fail, c0 = g_checks;
    x.fn(*s);
    printf("%-8s %6d checks, %d failed\n", x.name, g_checks - c0, g_fail - f0);
    delete s;
  }
  printf("v1-dmac-int: %d checks, %d failed\n", g_checks, g_fail);
  printf("v1-dmac-int: %s\n", g_fail ? "FAIL" : "PASS");
  return g_fail ? 1 : 0;
}
