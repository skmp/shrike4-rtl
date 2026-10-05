// v1_bsc_tb.cpp -- WI-BSC's unit testbench: v1_biu + v1_bsc (tb/v1_bsc_tb.sv) against the register model tb/sh4_bsc.h,
// an SDRAM protocol checker, a board model with RDY# waits, and the measured Dreamcast numbers of
// rtl/v1/spec/bsc-memmap.md 4.7 / 6.  `make v1-bsc` (the agreement with tb/v1_bsc_stub.sv is tb/v1_bsc_agree_tb.sv).
//
// Per Icyc (SOC.md 3.3.2's order): drive this cycle's inputs; eval with clk low; on a bce cycle the board answers the
// bus (xb_rdy / xb_rdata) combinationally; eval; the requester models, checkers and the register model observe the
// outputs of the cycle; rising edge.  bce = odd cycles (FRQCR 0x0E0A).
//
// Requester models (the stub testbench's semantics, SOC.md 3.3.1): a load in MA1 at cycle m raises bq.v in m + 1 and
// is acked in the Icyc after its critical rd_v; a store's WTB entry is loaded at the end of max(m, the previous
// store's done), raises bq.v in the next cycle and acks one cycle after that; back to back, the next access's m is
// the previous ack + 1.  "per" = the distance between consecutive m, as the console kernels measure.
//
// Groups (--only NAME runs one): regs, rfsh, timing, mpx, lanes, idle, random, misc, and the verification findings'
// regressions basic, pcmcia, rdyhold, manlock, breq, idlex, sdmr, model; SOC.md revision 3's dmaq (only the CPU's
// BZ_Q is split, I087), manlock's reset-crossing locked reads (I112) and order (the program-order claim of SOC.md 3.3,
// I116); (M6) turn (the CPU's turn at a DMAC release, v1_bsc_arb cpu_turn).  Every Bcyc of every group also runs the bus checker
// (Tb::bus_check: one owner of the data bus at a time, no cut width split, the RTL's `clash`).  Every check prints
// "FAIL [...]"; the last line is "v1-bsc tb: PASS" / "FAIL".
#include "Vv1_bsc_tb.h"
#include "verilated.h"
#include "sh4_bsc.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

static int g_fail = 0, g_checks = 0;
static std::string g_grp;
static bool g_trace = false;
static uint64_t g_tr0 = ~0ull, g_tr1 = 0;
static void failf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  if (g_fail < 80) {
    printf("FAIL [%s] ", g_grp.c_str());
    vprintf(fmt, ap);
    printf("\n");
  }
  va_end(ap);
  g_fail++;
}
#define CHECK(c, ...) do { g_checks++; if (!(c)) failf(__VA_ARGS__); } while (0)

struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 1) {}
  uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
  uint32_t u(uint32_t n) { return n ? (uint32_t)(next() % n) : 0; }
};

// ---- packed-array helpers (Verilator wide ports)
template <class W> static void wset(W &w, int lsb, int width, uint64_t v) {
  for (int b = 0; b < width; b++) {
    const int bit = lsb + b;
    uint32_t &word = w[bit >> 5];
    if ((v >> b) & 1) word |= 1u << (bit & 31); else word &= ~(1u << (bit & 31));
  }
}
template <class W> static uint64_t wget(const W &w, int lsb, int width) {
  uint64_t v = 0;
  for (int b = 0; b < width; b++) { const int bit = lsb + b; v |= (uint64_t)((w[bit >> 5] >> (bit & 31)) & 1) << b; }
  return v;
}
static void qset(uint64_t &w, int lsb, int width, uint64_t v) {
  const uint64_t m = (width == 64 ? ~0ull : ((1ull << width) - 1)) << lsb;
  w = (w & ~m) | ((v << lsb) & m);
}
template <class T> static uint64_t sget(T w, int lsb, int width) { return ((uint64_t)w >> lsb) & ((1ull << width) - 1); }

enum { BIU_IC_FILL = 0, BIU_IC_UNC, BIU_OC_FILL, BIU_OC_WBB, BIU_OC_WTB, BIU_OC_UNC, BIU_SQ, BIU_N, PORT_DMA = 7 };
enum { BZ_B = 0, BZ_W, BZ_L, BZ_Q, BZ_LINE };
enum { BM_CPU = 0, BM_DMA_DUAL, BM_DMA_SINGLE, BM_DDT };
enum { SDC_NOP = 0, SDC_ACTV, SDC_READ, SDC_READA, SDC_WRIT, SDC_WRITA, SDC_PRE, SDC_PALL, SDC_REF, SDC_SELF, SDC_MRS };
static const int SZB[5] = {1, 2, 4, 8, 32};
static uint8_t lanes_of(int size, uint32_t a) {
  switch (size) { case BZ_B: return (uint8_t)(1u << (a & 7)); case BZ_W: return (uint8_t)(3u << (a & 6));
                  case BZ_L: return (uint8_t)(0xFu << (a & 4)); default: return 0xFF; }
}
static uint64_t lmask(uint8_t be) { uint64_t m = 0; for (int b = 0; b < 8; b++) if ((be >> b) & 1) m |= 0xFFull << (8 * b); return m; }

// ================================================================================================================
// the BSC configuration the tests program (register values)
struct Cfg {
  uint32_t bcr1 = 0xA3020008u, wcr1 = 0x01110111u, wcr2 = 0x618066D8u, wcr3 = 0x07777777u, mcr = 0xC00A0E24u;
  uint16_t bcr2 = 0x0001, pcr = 0, rtcsr = 0x0000, rtcor = 0x005E;
  int cl() const { int c = (wcr2 >> 13) & 7; return c == 0 ? 1 : c > 5 ? 5 : c; }
  int cl2() const { int c = (wcr2 >> 9) & 7; return c == 0 ? 1 : c > 5 ? 5 : c; }
  int tpc() const { return ((mcr >> 19) & 7) + 1; }
  int rcd() const { int c = (mcr >> 16) & 3; return c == 0 ? 2 : c + 1; }
  int trwl() const { int c = (mcr >> 13) & 7; return c > 4 ? 5 : c + 1; }
  int trfc() const { return ((mcr >> 10) & 7) + 4 + 3 * ((mcr >> 27) & 7); }
  bool rasd() const { return (mcr >> 31) & 1; }
  int bl() const { return ((mcr >> 7) & 3) == 3 || ((mcr >> 7) & 3) == 2 ? 8 : 4; }
};

// ================================================================================================================
// the board: memories per area, RDY# waits, the DACK device
struct Board {
  std::vector<uint8_t> ram;                    // area 3: 16 MB, mirrored every 16 MB
  std::map<int, std::vector<uint8_t>> mem;     // other areas: 64 KB each, mirrored
  int rd_waits[8] = {0}, wr_waits[8] = {0};    // RDY# waits (Bcyc) per area (not area 3)
  std::function<int(uint32_t, bool)> waits_fn; // optional per-address waits
  // the cycle
  bool cv = false, cwe = false, cdack = false; int carea = 0, csize = 0, cdid = 0; uint32_t caddr = 0;
  bool open = false; int open_bidx = -1; uint64_t open_t = 0; int open_w = 0;
  uint64_t bclks = 0;
  // logs
  struct Cyc { uint64_t t_start, t_first = 0, t_last = 0; int area, size; bool we, dack; uint32_t addr; int nbeat = 0;
               std::vector<uint64_t> tb; std::vector<uint8_t> be; std::vector<uint32_t> ba; };
  std::vector<Cyc> log;
  std::vector<uint8_t> dack_sink;              // single-address reads (memory -> device)
  uint64_t dev_byte_n = 0;
  static uint8_t dev_byte(uint64_t n) { return (uint8_t)(0xA5 ^ (n * 29) ^ (n >> 8)); }
  Board() : ram(16u << 20, 0) {}
  uint8_t *byte(int area, uint32_t a) {
    if (area == 3) return &ram[a & 0xFFFFFF];
    auto &m = mem[area];
    if (m.empty()) m.assign(65536, 0);
    return &m[a & 0xFFFF];
  }
  // one bce: the chip's outputs of this cycle; returns rdy, rdata
  void bclk(Vv1_bsc_tb *t, uint64_t now, bool &rdy, uint64_t &rdata) {
    bclks++;
    rdy = false; rdata = 0;
    if (t->xb_start) {
      cv = true; cwe = t->xb_we; carea = t->xb_area; csize = t->xb_size; caddr = t->xb_addr; cdack = t->xb_dack;
      cdid = t->xb_dack_id; open = false;
      Cyc c{}; c.t_start = now; c.area = carea; c.size = csize; c.we = cwe; c.dack = cdack; c.addr = caddr;
      log.push_back(c);
    }
    if (!t->xb_beat) return;
    if (!cv) { failf("board: a beat with no cycle at %llu", (unsigned long long)now); return; }
    const uint32_t ba = t->xb_baddr & 0x1FFFFFF8u;
    const uint8_t be = t->xb_be;
    if (!open || t->xb_bidx != open_bidx) {
      open = true; open_bidx = t->xb_bidx; open_t = now;
      open_w = carea == 3 ? 0 : (waits_fn ? waits_fn(ba, cwe) : (cwe ? wr_waits[carea] : rd_waits[carea]));
    }
    if (carea != 3 && now < open_t + 2ull * (uint64_t)open_w) return;             // RDY# held off
    rdy = true; open = false; open_bidx = -1;
    Cyc &c = log.back();
    if (c.nbeat == 0) c.t_first = now;
    c.t_last = now; c.nbeat++; c.tb.push_back(now); c.be.push_back(be); c.ba.push_back(ba);
    if (cdack && carea == 3) {
      if (!cwe) {                                                      // memory -> device
        for (int k = 0; k < 8; k++) if ((be >> k) & 1) { dack_sink.push_back(*byte(3, ba + k)); rdata |= (uint64_t)*byte(3, ba + k) << (8 * k); }
      } else {                                                         // device -> memory: the device drives the data
        for (int k = 0; k < 8; k++) if ((be >> k) & 1) {
          const uint8_t d = dev_byte(dev_byte_n++);
          *byte(3, ba + k) = d; rdata |= (uint64_t)d << (8 * k);
        }
      }
      return;
    }
    for (int k = 0; k < 8; k++) {
      if (!((be >> k) & 1)) continue;
      if (cwe) *byte(carea, ba + k) = (uint8_t)(t->xb_wdata >> (8 * k));
      else rdata |= (uint64_t)*byte(carea, ba + k) << (8 * k);
    }
    if (!cwe) for (int k = 0; k < 8; k++) if (!((be >> k) & 1)) rdata |= (uint64_t)(0x5Au ^ k) << (8 * k);   // junk lanes
  }
};

// ================================================================================================================
// the SDRAM protocol checker (areas 2 / 3): commands on xb_sd_*, data beats on xb_beat
struct SdChk {
  Cfg c;
  bool on = true;
  struct Bank { bool act = false; uint32_t row = 0; int64_t t_act = -100, t_pre = -100, t_wr = -100; };
  Bank b[4];
  int64_t t_ref = -1000, t_pall = -100;
  struct Burst { bool we; int64_t d0, dl; int bank; };
  std::vector<Burst> bursts;
  uint64_t n_cmd = 0, n_ref = 0, n_mrs = 0, n_rd = 0, n_wr = 0;
  void reset() { for (auto &x : b) x = Bank(); t_ref = -1000; t_pall = -100; bursts.clear(); }
  int64_t last_data() const { int64_t l = -100; for (auto &x : bursts) if (x.dl > l) l = x.dl; return l; }
  void cmd(int64_t t, int k, int bank, uint32_t row) {
    n_cmd++;
    if (k != SDC_NOP && t > t_ref && t <= t_ref + c.trfc()) failf("sdram: command %d at %lld inside the refresh window (REF %lld)", k, (long long)t, (long long)t_ref);
    // drop finished bursts
    std::vector<Burst> keep;
    for (auto &x : bursts) if (x.dl >= t) keep.push_back(x);
    bursts.swap(keep);
    Bank &B = b[bank & 3];
    switch (k) {
    case SDC_NOP: break;
    case SDC_ACTV:
      if (B.act) failf("sdram: ACTV to the open bank %d at %lld", bank, (long long)t);
      if (t < B.t_pre + c.tpc()) failf("sdram: ACTV bank %d at %lld, %lld after its precharge (TPC %d)", bank, (long long)t, (long long)(t - B.t_pre), c.tpc());
      B.act = true; B.row = row; B.t_act = t;
      break;
    case SDC_READ: case SDC_READA: case SDC_WRIT: case SDC_WRITA: {
      const bool we = k == SDC_WRIT || k == SDC_WRITA;
      if (!B.act) failf("sdram: %s to the closed bank %d at %lld", we ? "WRIT" : "READ", bank, (long long)t);
      else if (B.row != row) failf("sdram: %s bank %d row %x, open row %x at %lld", we ? "WRIT" : "READ", bank, row, B.row, (long long)t);
      if (t < B.t_act + c.rcd()) failf("sdram: %s bank %d at %lld, %lld after ACTV (RCD %d)", we ? "WRIT" : "READ", bank, (long long)t, (long long)(t - B.t_act), c.rcd());
      const int64_t d0 = we ? t : t + c.cl();
      for (auto &x : bursts) {
        if (d0 <= x.dl) failf("sdram: %s at %lld: its data from %lld overlaps a burst up to %lld (no truncation)", we ? "WRIT" : "READ", (long long)t, (long long)d0, (long long)x.dl);
        if (we && !x.we && d0 <= x.dl + 1) failf("sdram: WRIT data at %lld right after read data at %lld (no turnaround)", (long long)d0, (long long)x.dl);
      }
      bursts.push_back({we, d0, d0 + c.bl() - 1, bank & 3});
      if (we) { n_wr++; B.t_wr = t; } else n_rd++;
      if (k == SDC_READA || k == SDC_WRITA) { B.act = false; B.t_pre = d0 + c.bl() - 1 + (we ? c.trwl() : 0); }
      break;
    }
    case SDC_PRE:
      if (!B.act) failf("sdram: PRE of the closed bank %d at %lld", bank, (long long)t);
      for (auto &x : bursts) if (x.bank == (bank & 3) && !x.we && t < x.dl - 1) failf("sdram: PRE bank %d at %lld before the last-but-one data cycle %lld", bank, (long long)t, (long long)(x.dl - 1));
      if (t <= B.t_wr + c.trwl()) failf("sdram: PRE bank %d at %lld, TRWL after the write at %lld", bank, (long long)t, (long long)B.t_wr);
      B.act = false; B.t_pre = t;
      break;
    case SDC_PALL:
      for (int i = 0; i < 4; i++) {
        for (auto &x : bursts) if (x.bank == i && !x.we && t < x.dl - 1) failf("sdram: PALL at %lld during a burst of bank %d", (long long)t, i);
        if (b[i].act && t <= b[i].t_wr + c.trwl()) failf("sdram: PALL at %lld, TRWL after a write of bank %d", (long long)t, i);
        b[i].act = false; b[i].t_pre = t;
      }
      t_pall = t;
      break;
    case SDC_REF:
      n_ref++;
      for (int i = 0; i < 4; i++) {
        if (b[i].act) failf("sdram: REF at %lld with bank %d open", (long long)t, i);
        if (t < b[i].t_pre + c.tpc()) failf("sdram: REF at %lld, %lld after the precharge of bank %d (TPC %d)", (long long)t, (long long)(t - b[i].t_pre), i, c.tpc());
      }
      if (!bursts.empty()) failf("sdram: REF at %lld during a burst", (long long)t);
      t_ref = t;
      break;
    case SDC_MRS:
      n_mrs++;
      for (int i = 0; i < 4; i++) if (b[i].act) failf("sdram: MRS at %lld with bank %d open", (long long)t, i);
      break;
    default: break;
    }
  }
  // a data beat on the bus of an SDRAM area at Bcyc t
  void beat(int64_t t, bool we) {
    bool ok = false;
    for (auto &x : bursts) if (x.we == we && t >= x.d0 && t <= x.dl) ok = true;
    if (!ok) failf("sdram: a %s beat at %lld outside every %s burst", we ? "write" : "read", (long long)t, we ? "write" : "read");
  }
};

// ================================================================================================================
// requester models
struct Xact {
  int port = 0;
  bool we = false; uint32_t addr = 0; int size = BZ_L; bool lock = false; int master = BM_CPU, ch = 0;
  uint64_t wd[4] = {0, 0, 0, 0}; uint8_t wbe[4] = {0, 0, 0, 0};
  int64_t t_raise = -1, t_ready = -1, t_done = -1, t_crit = -1;
  int nb = 0; int64_t t_rd[4] = {0, 0, 0, 0}; int q_rd[4] = {0, 0, 0, 0}; uint64_t d_rd[4] = {0, 0, 0, 0};
  int nw = 0; int64_t t_wd[4] = {0, 0, 0, 0}; int q_wd[4] = {0, 0, 0, 0};
  uint64_t ex[4] = {0, 0, 0, 0}; uint8_t exm[4] = {0, 0, 0, 0}; bool ex_v = false;
  std::function<void(Xact &)> on_crit, on_done, on_ready;
};

struct Tb;
struct Port {
  Xact *cur = nullptr;
  std::deque<Xact *> fly;
  std::deque<Xact *> q;
  Xact *wd_x = nullptr; int wd_q = -1;
};

struct Tb {
  std::unique_ptr<Vv1_bsc_tb> t;
  uint64_t cyc = 0;
  Board board;
  SdChk sd;
  Cfg cfg;
  sh4bsc::Bsc model;
  bool model_on = false;                        // the register model follows the RTL (reset / strobes / bclk)
  Port port[8];
  std::vector<std::unique_ptr<Xact>> pool;
  // the shadow memory: applied in accept order (= bus order, the BSC is in order)
  Board shadow;
  bool data_check = true;
  // bus observation
  uint64_t n_ref_bus = 0, n_mrs_bus = 0; uint32_t last_mrs = 0;
  std::vector<uint64_t> t_ref_bus;
  int64_t dma_tap_n = 0;
  // p4 strobe of the next bce (driven by p4())
  bool pq_rd = false, pq_wr = false, pq_sd = false; int pq_size = 0; uint32_t pq_off = 0, pq_wdata = 0;
  uint32_t pr_last = 0; bool pr_hit_last = false;
  bool rst_man = false;
  uint32_t port_i = 0x01CE3;
  bool breq_n = true, ddt_req = false, bpost = false;
  bool dma_rel = false;                // (M6) the DMAC releases the bus between its requests (v1_dmac.bus_rel)
  bool dma_ses = false;                // (M6-fix) a DMAC burst session holds the bus (v1_dmac.bus_ses)
  uint16_t md = 0x065;
  std::function<void()> on_cycle;               // a test's hook, after the outputs of every Icyc are known
  // the bus checker (every Bcyc, every group): one owner of the data bus at a time
  bool bus_chk = true;
  int64_t sd_data_until = -1, sd_rd_until = -1;  // the last SDRAM data Bcyc (unseen beats included) / of a read
  bool nsd_open = false; int nsd_area = 0; int64_t nsd_t0 = 0;   // an MPX / basic access on the bus (first T1 .. last beat)
  uint64_t n_cmd[16] = {0};

  Tb() : t(new Vv1_bsc_tb) { trace = g_trace; }
  bool bce() const { return (cyc & 1) == 1; }
  // the memory type of an area as programmed (cfg) and strapped (md), as v1_bsc_pkg's area_mt
  bool sd_area(int a) const { const int tp = (cfg.bcr1 >> 2) & 7; return (a == 3 && (tp == 2 || tp == 3)) || (a == 2 && tp == 3); }
  bool mpx_area(int a) const {
    const int tp = (cfg.bcr1 >> 2) & 7;
    if (a == 7 || sd_area(a)) return false;
    if (a == 0) return (md >> 6) & 1;                                  // A0MPX = MD6 (the RTL's strap convention)
    if ((a == 5 || a == 6) && (cfg.bcr1 & 1)) return false;           // PCMCIA
    if ((a == 2 && tp == 5) || (a == 3 && (tp == 4 || tp == 5))) return false;   // DRAM
    return (cfg.bcr1 >> 17) & 1;                                       // MEMMPX
  }

  Xact *mk(int p, bool we, uint32_t a, int size) {
    pool.emplace_back(new Xact);
    Xact *x = pool.back().get();
    x->port = p; x->we = we; x->addr = a; x->size = size;
    if (size == BZ_LINE || size == BZ_Q) x->addr &= ~7u;
    if (we) for (int q = 0; q < 4; q++) {
      x->wd[q] = 0x1111111111111111ull * (uint64_t)((pool.size() + q) & 0xF) ^ ((uint64_t)a << 20) ^ (uint64_t)q;
      x->wbe[q] = (size == BZ_LINE || size == BZ_Q) ? 0xFF : lanes_of(size, a);
    }
    return x;
  }
  void submit(Xact *x, int64_t t_raise) { x->t_raise = t_raise; port[x->port].q.push_back(x); }

  // ---- the shadow: a read's expected data, a write's effect, at its accept
  void shadow_accept(Xact &x) {
    const int area = (x.addr >> 26) & 7;
    if (area == 7) {                                                   // area 7: no cycle, data 0 (C16)
      for (int q = 0; q < 4; q++) { x.ex[q] = 0; x.exm[q] = 0xFF; }
      x.ex_v = !x.we;
      return;
    }
    if (!x.we) {
      if (x.master == BM_DMA_SINGLE || x.master == BM_DDT) { x.ex_v = false; return; }
      for (int q = 0; q < 4; q++) {
        uint32_t ba = x.size == BZ_LINE ? (x.addr & ~31u) + 8 * q : (x.addr & ~7u);
        uint64_t v = 0;
        for (int k = 0; k < 8; k++) v |= (uint64_t)*shadow.byte(area, ba + k) << (8 * k);
        x.ex[q] = v;
        x.exm[q] = x.size == BZ_LINE || x.size == BZ_Q ? 0xFF : lanes_of(x.size, x.addr);
      }
      x.ex_v = true;
      return;
    }
    if (x.master == BM_DMA_SINGLE || x.master == BM_DDT) {           // device -> memory: the board's device data
      const int n = SZB[x.size];
      for (int i = 0; i < n; i++) {
        uint32_t a = x.size == BZ_LINE ? (x.addr & ~31u) + (((x.addr >> 3) & 3) * 8 + i) % 32 : x.addr + i;
        *shadow.byte(area, a) = Board::dev_byte(shadow.dev_byte_n++);
      }
      return;
    }
    for (int q = 0; q < 4; q++) {
      if (x.size != BZ_LINE && q != (int)((x.addr >> 3) & 3)) continue;
      uint32_t ba = x.size == BZ_LINE ? (x.addr & ~31u) + 8 * q : (x.addr & ~7u);
      uint8_t be = x.size == BZ_LINE || x.size == BZ_Q ? x.wbe[q] : (x.wbe[q] & lanes_of(x.size, x.addr));
      for (int k = 0; k < 8; k++) if ((be >> k) & 1) *shadow.byte(area, ba + k) = (uint8_t)(x.wd[q] >> (8 * k));
    }
  }

  // ---- inputs of this cycle
  void drive() {
    auto *v = t.get();
    v->rst_man = rst_man;
    v->bce = bce(); v->pce = (cyc & 3) == 3;
    uint8_t rqv = 0, rqwe = 0, rqlk = 0; uint32_t rqsz = 0; uint64_t rwbe = 0;
    for (int i = 0; i < 8; i++) {
      Port &P = port[i];
      if (!P.cur && !P.q.empty() && P.q.front()->t_raise >= 0 && P.q.front()->t_raise <= (int64_t)cyc) { P.cur = P.q.front(); P.q.pop_front(); }
    }
    for (int i = 0; i < BIU_N; i++) {
      Xact *x = port[i].cur;
      if (x) {
        rqv |= 1 << i; if (x->we) rqwe |= 1 << i; if (x->lock) rqlk |= 1 << i;
        wset(v->rq_addr, 29 * i, 29, x->addr); rqsz |= (uint32_t)x->size << (3 * i);
      } else wset(v->rq_addr, 29 * i, 29, 0);
      // write data: in the Icyc after wd_req only; poison otherwise
      if (port[i].wd_q >= 0 && port[i].wd_x) {
        wset(v->rwd_data, 64 * i, 64, port[i].wd_x->wd[port[i].wd_q]);
        qset(rwbe, 8 * i, 8, port[i].wd_x->wbe[port[i].wd_q]);
      } else { wset(v->rwd_data, 64 * i, 64, 0xDEADBEEFDEADBEEFull); qset(rwbe, 8 * i, 8, 0xFF); }
    }
    v->rq_v = rqv; v->rq_we = rqwe; v->rq_lock = rqlk; v->rq_size = rqsz; v->rwd_be = rwbe;
    Xact *d = port[PORT_DMA].cur;
    v->dq_v = d != nullptr; v->dq_we = d && d->we; v->dq_lock = d && d->lock; v->dq_addr = d ? d->addr : 0;
    v->dq_size = d ? d->size : 0; v->dq_master = d ? d->master : 0; v->dq_ch = d ? d->ch : 0;
    v->dq_rel = dma_rel && d == nullptr;         // v1_dmac's REL / IDLE: released only while it has no request
    v->dq_ses = dma_ses;                         // (M6-fix) v1_dmac's burst session (bown != 0)
    if (port[PORT_DMA].wd_q >= 0 && port[PORT_DMA].wd_x) {
      v->dwd_data = port[PORT_DMA].wd_x->wd[port[PORT_DMA].wd_q]; v->dwd_be = port[PORT_DMA].wd_x->wbe[port[PORT_DMA].wd_q];
    } else { v->dwd_data = 0xDEADBEEFDEADBEEFull; v->dwd_be = 0xFF; }
    const bool s = bce() && (pq_rd || pq_wr);
    v->pq_rd = s && pq_rd; v->pq_wr = s && pq_wr; v->pq_sdmr = pq_sd; v->pq_size = pq_size; v->pq_off = pq_off;
    v->pq_wdata = pq_wdata;
    v->bpost_bsc = bpost; v->breq_n = breq_n; v->ddt_bus_req = ddt_req; v->port_i = port_i; v->md = md;
  }

  // ---- one Icyc
  void step() {
    auto *v = t.get();
    drive();
    v->clk = 0; v->xb_rdy = 0; v->xb_rdata = 0;
    v->eval();
    if (bce()) {
      bool rdy; uint64_t rd;
      // the protocol checker sees this Bcyc's command and beats
      if (v->xb_sd_cmd_v && sd.on) sd.cmd((int64_t)(cyc / 2), v->xb_sd_cmd, v->xb_sd_bank, v->xb_sd_row);
      board.bclk(v, cyc, rdy, rd);
      if ((trace || (cyc >= g_tr0 && cyc < g_tr1)) && (v->xb_start || v->xb_beat || v->xb_sd_cmd_v))
        printf("  %6llu %s%s a%u %07x sz%u %s bidx %u ba %07x be %02x rdy %d cmd %d\n", (unsigned long long)cyc,
               v->xb_start ? "S" : "-", v->xb_beat ? "B" : "-", v->xb_area, v->xb_addr, v->xb_size, v->xb_we ? "w" : "r",
               v->xb_bidx, v->xb_baddr, v->xb_be, rdy, v->xb_sd_cmd_v ? v->xb_sd_cmd : -1);
      v->xb_rdy = rdy; v->xb_rdata = rd;
      v->eval();
      if (v->xb_beat && sd_area(board.carea) && sd.on && rdy) sd.beat((int64_t)(cyc / 2), board.cwe);
      if (bus_chk) bus_check();
      if (v->xb_refresh) { n_ref_bus++; t_ref_bus.push_back(cyc); }
      if (v->xb_mrs) { n_mrs_bus++; last_mrs = v->xb_mrs_val; }
    } else {
      if (v->xb_start || v->xb_beat || v->xb_sd_cmd_v) failf("bus activity in the non-bce cycle %llu", (unsigned long long)cyc);
    }
    observe();
    if (on_cycle) on_cycle();
    // the register model's step: this Bcyc's strobe (compared in observe) then the clock edge
    if (bce() && model_on) {
      model.port_in = port_i;
      model.bclk(false);
    }
    v->clk = 1;
    v->eval();
    cyc++;
  }

  // ---- the bus checker, every Bcyc: an MPX / basic cycle never meets SDRAM data (a READ's data CL .. CL + BL - 1
  //      Bcyc later, unseen beats included; a WRIT's BL Bcyc), an MPX address comes at least one Bcyc after SDRAM read
  //      data (WCR1 "M (1)"), no SDRAM command and no other cycle between an MPX / basic access's first T1 and its last
  //      beat (the pieces of a width split are one access, hm 13.3.1), and never two sequencers at once (v1_bsc's
  //      simulation-only `clash`: their outputs would be ORed)
  void bus_check() {
    auto *v = t.get();
    const int64_t T = (int64_t)(cyc / 2);
    if (v->bus_clash) failf("bus: two sequencers drive the bus in Bcyc %lld", (long long)T);
    if (v->xb_sd_cmd_v) {
      const int k = v->xb_sd_cmd & 15;
      n_cmd[k]++;
      if (nsd_open) failf("bus: SDRAM command %d at Bcyc %lld inside the area-%d access started at Bcyc %lld", k,
                          (long long)T, nsd_area, (long long)nsd_t0);
      if (k == SDC_READ || k == SDC_READA) {
        sd_data_until = std::max<int64_t>(sd_data_until, T + cfg.cl() + cfg.bl() - 1);
        sd_rd_until = std::max<int64_t>(sd_rd_until, T + cfg.cl() + cfg.bl() - 1);
      }
      if (k == SDC_WRIT || k == SDC_WRITA) sd_data_until = std::max<int64_t>(sd_data_until, T + cfg.bl() - 1);
    }
    const bool st_nsd = v->xb_start && !sd_area(v->xb_area);
    const bool bt_nsd = v->xb_beat && board.cv && !sd_area(board.carea);
    if ((st_nsd || bt_nsd) && T <= sd_data_until)
      failf("bus conflict: area %d activity at Bcyc %lld during SDRAM data (until %lld)", st_nsd ? (int)v->xb_area : board.carea,
            (long long)T, (long long)sd_data_until);
    if (st_nsd && mpx_area(v->xb_area) && T < sd_rd_until + 2)
      failf("idle: MPX address at Bcyc %lld, SDRAM read data until %lld (M (1): one idle Bcyc)", (long long)T, (long long)sd_rd_until);
    if (v->xb_start) {
      if (nsd_open && (sd_area(v->xb_area) || (int)v->xb_area != nsd_area))
        failf("bus: an area-%d cycle at Bcyc %lld inside the area-%d access started at Bcyc %lld", (int)v->xb_area, (long long)T,
              nsd_area, (long long)nsd_t0);
      if (st_nsd && !nsd_open) { nsd_open = true; nsd_area = v->xb_area; nsd_t0 = T; }
    }
    if (v->xb_last && nsd_open && board.cv && !sd_area(board.carea)) nsd_open = false;
  }
  void bus_chk_reset() { sd_data_until = sd_rd_until = -1; nsd_open = false; }

  void observe() {
    auto *v = t.get();
    // P4 strobe results
    if (bce() && (pq_rd || pq_wr)) {
      pr_last = v->pr_rdata; pr_hit_last = v->pr_hit;
      CHECK(!v->pr_stall, "the BSC stalled a register strobe");
      if (model_on) {
        uint32_t mv = 0; bool mh;
        if (pq_sd) { if (pq_rd) mh = model.sdmr.rd(pq_off, pq_size, mv, false); else mh = model.sdmr.wr(pq_off, pq_size, pq_wdata, false); }
        else if (pq_rd) mh = model.rd(pq_off, pq_size, mv, false);
        else mh = model.wr(pq_off, pq_size, pq_wdata, false);
        CHECK(v->pr_hit == mh, "reg %s off %07x size %d: hit %d, model %d", pq_rd ? "rd" : "wr", pq_off, pq_size, v->pr_hit, mh);
        if (pq_rd) CHECK(v->pr_rdata == mv, "reg rd off %07x size %d: %08x, model %08x", pq_off, pq_size, v->pr_rdata, mv);
      }
      pq_rd = pq_wr = false;
    }
    // requester strobes
    for (int i = 0; i < 8; i++) {
      bool ready, wd_req, rd_v, rd_last, done; int wd_beat, rd_beat; uint64_t rd_data;
      if (i < BIU_N) {
        ready = (v->rs_ready >> i) & 1; wd_req = (v->rs_wd_req >> i) & 1; rd_v = (v->rs_rd_v >> i) & 1;
        rd_last = (v->rs_rd_last >> i) & 1; done = (v->rs_done >> i) & 1;
        wd_beat = (int)sget(v->rs_wd_beat, 2 * i, 2); rd_beat = (int)sget(v->rs_rd_beat, 2 * i, 2);
        rd_data = wget(v->rs_rd_data, 64 * i, 64);
      } else {
        ready = v->ds_ready; wd_req = v->ds_wd_req; rd_v = v->ds_rd_v; rd_last = v->ds_rd_last; done = v->ds_done;
        wd_beat = v->ds_wd_beat; rd_beat = v->ds_rd_beat; rd_data = v->ds_rd_data;
      }
      Port &P = port[i];
      P.wd_q = -1;
      if ((ready || wd_req || rd_v || rd_last || done) && !bce()) failf("port %d: a strobe in the non-bce cycle %llu", i, (unsigned long long)cyc);
      if (ready) {
        if (!P.cur) { failf("port %d: ready with no request at %llu", i, (unsigned long long)cyc); }
        else {
          Xact *x = P.cur; P.cur = nullptr;
          x->t_ready = (int64_t)cyc;
          shadow_accept(*x);
          P.fly.push_back(x);
          if (x->on_ready) x->on_ready(*x);
        }
      }
      Xact *h = P.fly.empty() ? nullptr : P.fly.front();
      if (wd_req) {
        if (!h || !h->we) failf("port %d: wd_req with no write in flight at %llu", i, (unsigned long long)cyc);
        else { if (h->nw < 4) { h->t_wd[h->nw] = (int64_t)cyc; h->q_wd[h->nw] = wd_beat; h->nw++; } P.wd_x = h; P.wd_q = wd_beat; }
      }
      if (rd_v) {
        if (!h || h->we) failf("port %d: rd_v with no read in flight at %llu", i, (unsigned long long)cyc);
        else {
          if (h->nb < 4) { h->t_rd[h->nb] = (int64_t)cyc; h->q_rd[h->nb] = rd_beat; h->d_rd[h->nb] = rd_data; }
          if (h->nb == 0) { h->t_crit = (int64_t)cyc; if (h->on_crit) h->on_crit(*h); }
          if (data_check && h->ex_v) {
            const int q = h->size == BZ_LINE ? rd_beat : 0;
            const uint64_t want = h->ex[q] & lmask(h->exm[q]);
            CHECK(rd_data == want, "port %d read %08x size %d beat %d (q %d) at %llu: data %016llx, want %016llx", i,
                  h->addr, h->size, h->nb, rd_beat, (unsigned long long)cyc, (unsigned long long)rd_data, (unsigned long long)want);
          }
          if (h->size == BZ_LINE) {
            const int want_q = (int)(((h->addr >> 3) + h->nb) & 3);
            CHECK(rd_beat == want_q, "port %d line %08x beat %d: rd_beat %d, want %d (wrap order)", i, h->addr, h->nb, rd_beat, want_q);
          }
          h->nb++;
          const int want_n = h->size == BZ_LINE ? 4 : 1;
          CHECK(rd_last == (h->nb == want_n), "port %d read %08x: rd_last %d on beat %d", i, h->addr, rd_last, h->nb);
        }
      }
      if (done) {
        if (!h) failf("port %d: done with nothing in flight at %llu", i, (unsigned long long)cyc);
        else {
          h->t_done = (int64_t)cyc;
          if (!h->we) CHECK(h->nb == (h->size == BZ_LINE ? 4 : 1), "port %d read %08x done after %d beats", i, h->addr, h->nb);
          else CHECK(h->nw == (h->size == BZ_LINE ? 4 : 1), "port %d write %08x done after %d wd_req", i, h->addr, h->nw);
          P.fly.pop_front();
          if (h->on_done) h->on_done(*h);
        }
      }
    }
    if (v->tap_v) dma_tap_n++;
  }

  bool trace = false;
  void run(uint64_t n) { for (uint64_t i = 0; i < n; i++) step(); }
  bool idle() const {
    for (int i = 0; i < 8; i++) if (port[i].cur || !port[i].fly.empty() || !port[i].q.empty()) return false;
    return true;
  }
  void dump() {
    for (int p = 0; p < 8; p++) {
      const Port &P = port[p];
      if (P.cur) printf("  port %d presents %s %08x size %d master %d since %lld\n", p, P.cur->we ? "W" : "R", P.cur->addr, P.cur->size, P.cur->master, (long long)P.cur->t_raise);
      for (const Xact *x : P.fly) printf("  port %d in flight %s %08x size %d master %d ready %lld nb %d nw %d\n", p, x->we ? "W" : "R", x->addr, x->size, x->master, (long long)x->t_ready, x->nb, x->nw);
    }
  }
  bool run_until_idle(uint64_t limit = 200000) {
    uint64_t n = 0;
    while (!idle() && n < limit) { step(); n++; }
    if (!idle()) { failf("not idle after %llu cycles (cycle %llu)", (unsigned long long)limit, (unsigned long long)cyc); dump(); return false; }
    return true;
  }
  // idle requesters and a quiet bus (a posted write retires before its data beats are on the bus)
  void settle(uint64_t quiet = 40) {
    run_until_idle();
    uint64_t q = 0, n = 0;
    while (q < quiet && n < 100000) {
      step(); n++;
      if (t->xb_start || t->xb_beat || t->xb_sd_cmd_v) q = 0; else q++;
    }
  }

  // ---- resets and registers
  void reset(bool pin = true) {
    auto *v = t.get();
    v->rst_pin = pin; v->rst_por = 1; v->rst_man = 0; v->rst_cpu = 1;
    for (int i = 0; i < 6; i++) step();
    v->rst_pin = 0; v->rst_por = 0; v->rst_cpu = 0;
    model.cycle = true;
    if (pin) { model.md = md; model.reset_pin(); } else model.reset_por();
    sd.reset();
    bus_chk_reset();
    step();
  }
  // the shadow memory starts equal to the board; the final images compared (after the bus is quiet)
  void shadow_sync() { shadow.ram = board.ram; shadow.mem = board.mem; shadow.dev_byte_n = board.dev_byte_n; }
  long mem_diff(const char *what) {
    long nd = 0;
    for (size_t i = 0; i < board.ram.size(); i++)
      if (board.ram[i] != shadow.ram[i]) { if (nd < 5) failf("%s: RAM byte %06zx board %02x shadow %02x", what, i, board.ram[i], shadow.ram[i]); nd++; }
    for (auto &kv : board.mem) {
      auto &s = shadow.mem[kv.first];
      if (s.empty()) s.assign(65536, 0);
      for (size_t i = 0; i < kv.second.size(); i++)
        if (kv.second[i] != s[i]) { if (nd < 5) failf("%s: area %d byte %04zx board %02x shadow %02x", what, kv.first, i, kv.second[i], s[i]); nd++; }
    }
    return nd;
  }
  void p4(bool wr, uint32_t off, int size, uint32_t v = 0, bool sdmr = false) {
    while (bce()) step();                                             // the strobe goes in the next bce cycle
    pq_rd = !wr; pq_wr = wr; pq_off = off & 0x0FFFFFFF; pq_size = size; pq_wdata = v; pq_sd = sdmr;
    step(); step();
  }
  uint32_t rd(uint32_t off, int size) { p4(false, off, size); return pr_last; }
  void wr(uint32_t off, int size, uint32_t v) { p4(true, off, size, v); }
  void program(const Cfg &c) {
    cfg = c; sd.c = c;
    wr(0xF800000, 2, c.bcr1); wr(0xF800004, 1, c.bcr2); wr(0xF800008, 2, c.wcr1); wr(0xF80000C, 2, c.wcr2);
    wr(0xF800010, 2, c.wcr3); wr(0xF800014, 2, c.mcr); wr(0xF800018, 1, c.pcr);
    wr(0xF800024, 1, 0xA500 | c.rtcor); wr(0xF800020, 1, 0xA500); wr(0xF80001C, 1, 0xA500 | (c.rtcsr & 0xFF));
  }
};

// ================================================================================================================
// load / store programs with the stub testbench's requester semantics
struct Prog {
  Tb &tb;
  std::vector<int64_t> m;                                              // the MA1 cycle of each access
  int64_t last_wtb_done = -1000;
  Xact *wtb_pend = nullptr;                                           // the store waiting for the WTB
  explicit Prog(Tb &t) : tb(t) {}
  // an uncached read (or line fill on OC_FILL) at MA1 cycle mm; `then(ack)` schedules the next access
  void load(int port, uint32_t a, int size, int64_t mm, std::function<void(int64_t)> then) {
    m.push_back(mm);
    Xact *x = tb.mk(port, false, a, size);
    x->on_crit = [then](Xact &xx) { if (then) then(xx.t_crit + 1); };
    tb.submit(x, mm + 1);
  }
  // a store through the WTB at MA1 cycle mm (loaded at max(mm, the previous store's done))
  // (recorded: the cycle the WTB is loaded, x = max(m, the previous store's done); its ack is x + 2)
  void store(uint32_t a, int size, int64_t mm, std::function<void(int64_t)> then) {
    Xact *x = tb.mk(BIU_OC_WTB, true, a, size);
    x->on_done = [this](Xact &xx) { last_wtb_done = xx.t_done; };
    // the WTB is free once the previous store is done: wait for it if needed
    auto go = [this, x, then](int64_t xt) { m.push_back(xt); tb.submit(x, xt + 1); if (then) then(xt + 2); };
    Xact *prev = prev_store;
    prev_store = x;                              // before `go`: it may run the next store at once
    if (prev && prev->t_done < 0) {
      // the previous store is still in flight: load when it completes
      auto old = prev->on_done;
      prev->on_done = [old, go, mm](Xact &xx) { if (old) old(xx); go(std::max<int64_t>(mm, xx.t_done)); };
    } else go(std::max<int64_t>(mm, last_wtb_done));
  }
  Xact *prev_store = nullptr;
};

static Cfg dc_cfg(bool refresh) {
  Cfg c;
  c.rtcsr = refresh ? 0x0010 : 0x0000;         // CKS = CKIO / 16, or stopped
  return c;
}

// the per-access cost of a stream: the distance between consecutive MA1 cycles in steady state
static int64_t per_of(const std::vector<int64_t> &m, int from) {
  if ((int)m.size() < from + 2) return -1;
  const int64_t d = m[from + 1] - m[from];
  for (size_t i = from + 1; i + 1 < m.size(); i++) if (m[i + 1] - m[i] != d) return -1000 - (m[i + 1] - m[i]);
  return d;
}

// ================================================================================================================
// groups
static const uint32_t RAM = 0x0C080000u;     // P2 / physical area 3, a test buffer (bank 0)

// a read / write stream, each access starting at the previous ack + 1; returns the steady per-access cost
static int64_t stream(Tb &tb, int n, std::function<void(Prog &, int, int64_t, std::function<void(int64_t)>)> acc) {
  Prog p(tb);
  std::function<void(int, int64_t)> go = [&](int k, int64_t mm) {
    if (k >= n) return;
    acc(p, k, mm, [&, k](int64_t ack) { go(k + 1, ack + 1); });
  };
  go(0, (int64_t)tb.cyc + 4);
  tb.run_until_idle();
  tb.run(40);
  return per_of(p.m, n / 3);
}

static void grp_timing(Tb &tb) {
  g_grp = "timing";
  tb.reset(); tb.program(dc_cfg(false));
  // open the rows the tests use (the row-hit numbers are for an open bank, SOC.md C5)
  for (uint32_t a : {RAM, RAM + 0x400000u, RAM + 0x800u})
    { Xact *x = tb.mk(BIU_OC_UNC, false, a, BZ_L); tb.submit(x, tb.cyc + 2); tb.run_until_idle(); }
  tb.run(20);
  struct Row { const char *name; int64_t want; std::function<void(Prog &, int, int64_t, std::function<void(int64_t)>)> f; };
  auto ld = [](int size, uint32_t stride) {
    return [size, stride](Prog &p, int k, int64_t mm, std::function<void(int64_t)> nx) { p.load(BIU_OC_UNC, RAM + (uint32_t)k * stride, size, mm, nx); };
  };
  const Row rows[] = {
    {"ram rd_l per 16 [HW 4.7]", 16, ld(BZ_L, 4)},
    {"ram rd_w per 16", 16, ld(BZ_W, 2)},
    {"ram rd_b per 16", 16, ld(BZ_B, 1)},
    {"ram rd_q (64-bit FMOV) per 30 [HW 30-32]", 30, ld(BZ_Q, 8)},
    {"ram/row stride 0x800 per 26 [HW]", 26, ld(BZ_L, 0x800)},
    {"oc line-fill stream per 16 [HW oc_miss]", 16, [](Prog &p, int k, int64_t mm, std::function<void(int64_t)> nx) { p.load(BIU_OC_FILL, RAM + 0x2000 + 32u * k, BZ_LINE, mm, nx); }},
    {"ram wr_l per 10 [HW]", 10, [](Prog &p, int k, int64_t mm, std::function<void(int64_t)> nx) { p.store(RAM + 4u * k, BZ_L, mm, nx); }},
    {"ram wr_b per 10 [HW]", 10, [](Prog &p, int k, int64_t mm, std::function<void(int64_t)> nx) { p.store(RAM + 1u * k, BZ_B, mm, nx); }},
    {"ram wr_q per 20 [HW]", 20, [](Prog &p, int k, int64_t mm, std::function<void(int64_t)> nx) { p.store(RAM + 8u * k, BZ_Q, mm, nx); }},
  };
  for (const Row &r : rows) {
    const int64_t per = stream(tb, 24, r.f);
    CHECK(per == r.want, "%s: %lld", r.name, (long long)per);
  }
  // pairs (two accesses per step): the cost of a pair
  struct Pair { const char *name; int64_t want; bool w1; uint32_t a2off; bool w2; };
  const Pair pairs[] = {
    {"ram+bank rd_alt per pair 32 [HW]", 32, false, 0x400000, false},
    {"ram+row rd_alt per pair 52 [HW 52.25]", 52, false, 0x800, false},
    {"ram wr_rd (same address) per pair 26 [HW]", 26, true, 0, false},
    {"ram rd_wr (same address) per pair 26 [HW]", 26, false, 0, true},
    {"ram+row wr_rdalt per pair 44 [HW]", 44, true, 0x800, false},
    {"ram+bank wr_rdalt per pair 26 [HW]", 26, true, 0x400000, false},
  };
  for (const Pair &pp : pairs) {
    Prog p2(tb);
    std::vector<int64_t> ms;
    std::function<void(int, int64_t)> go = [&](int k, int64_t mm) {
      if (k >= 24) return;
      ms.push_back(mm);
      const uint32_t a = RAM + 0x100 + ((k & 1) ? pp.a2off : 0);
      const bool w = (k & 1) ? pp.w2 : pp.w1;
      auto nx = [&, k](int64_t ack) { go(k + 1, ack + 1); };
      if (w) p2.store(a, BZ_L, mm, nx); else p2.load(BIU_OC_UNC, a, BZ_L, mm, nx);
    };
    go(0, (int64_t)tb.cyc + 4);
    tb.run_until_idle(); tb.run(40);
    int64_t pr = ms[12] - ms[10];
    bool steady = true;
    for (int k = 10; k + 2 < 24; k += 2) if (ms[k + 2] - ms[k] != pr) steady = false;
    CHECK(steady && pr == pp.want, "%s: %lld%s", pp.name, (long long)pr, steady ? "" : " (not steady)");
  }
  // C6 / C7: the isolated penalty ack - m = 14 / 15 by the phase of m (bank open)
  for (int ph = 0; ph < 2; ph++) {
    int64_t mm = (int64_t)tb.cyc + 4; if ((mm & 1) != ph) mm++;
    int64_t ack = -1;
    Prog p(tb);
    p.load(BIU_OC_UNC, RAM + 0x40, BZ_L, mm, [&](int64_t a) { ack = a; });
    tb.run_until_idle(); tb.run(20);
    CHECK(ack - mm == (ph ? 15 : 14), "C7 isolated uncached read, m %s bce: penalty %lld, want %d", ph ? "on" : "off", (long long)(ack - mm), ph ? 15 : 14);
  }
  // SQ bursts re-requested at their done: 12 per 32 bytes [HW]
  {
    std::vector<int64_t> rdy;
    std::function<void(int)> go = [&](int k) {
      if (k >= 16) return;
      Xact *x = tb.mk(BIU_SQ, true, RAM + 0x3000 + 32u * k, BZ_LINE);
      x->on_ready = [&](Xact &xx) { rdy.push_back(xx.t_ready); };
      x->on_done = [&, k](Xact &xx) { (void)xx; go(k + 1); };
      tb.submit(x, (int64_t)tb.cyc + 1);
    };
    go(0);
    tb.run_until_idle(); tb.run(20);
    CHECK(rdy.size() == 16 && rdy[10] - rdy[9] == 12 && rdy[15] - rdy[14] == 12, "SQ burst stream: %lld per burst, want 12",
          rdy.size() == 16 ? (long long)(rdy[10] - rdy[9]) : -1LL);
  }
}

// refresh: +24 per refresh at every phase of a read stream, +6 on the first access after one, the period
static void grp_refresh_timing(Tb &tb) {
  g_grp = "rfsh-timing";
  for (int phase = 0; phase < 32; phase += 1) {
    tb.reset(); tb.program(dc_cfg(true));
    { Xact *x = tb.mk(BIU_OC_UNC, false, RAM, BZ_L); tb.submit(x, tb.cyc + 2); tb.run_until_idle(); }
    tb.run(phase);
    const uint64_t r0 = tb.n_ref_bus;
    Prog p(tb);
    const int N = 600;
    std::function<void(int, int64_t)> go = [&](int k, int64_t mm) {
      if (k >= N) return;
      p.load(BIU_OC_UNC, RAM + 4u * (k & 63), BZ_L, mm, [&, k](int64_t ack) { if (k + 1 == N) p.m.push_back(ack + 1); go(k + 1, ack + 1); });
    };
    go(0, (int64_t)tb.cyc + 4);
    tb.run_until_idle(); tb.run(20);
    const int64_t refs = (int64_t)(tb.n_ref_bus - r0);
    int n16 = 0, n40 = 0, nother = 0; int64_t bad = 0;
    for (size_t k = 1; k + 1 < p.m.size(); k++) {
      const int64_t d = p.m[k + 1] - p.m[k];
      if (d == 16) n16++; else if (d == 16 + 24) n40++; else { nother++; bad = d; }
    }
    CHECK(nother == 0 && n40 >= 1 && (n40 == refs || n40 == refs - 1),
          "refresh phase %d: costs 16 x %d, 40 x %d, other x %d (e.g. %lld); %lld refreshes (want every refresh +24)",
          phase, n16, n40, nother, (long long)bad, (long long)refs);
  }
  // the refresh period: RTCOR 0x5E x CKIO/16 = 3008 Icyc [HW 6.2]
  tb.reset(); tb.program(dc_cfg(true));
  tb.t_ref_bus.clear();
  tb.run(20000);
  bool per_ok = tb.t_ref_bus.size() >= 5;
  for (size_t i = 1; i < tb.t_ref_bus.size(); i++) if (tb.t_ref_bus[i] - tb.t_ref_bus[i - 1] != 3008) per_ok = false;
  CHECK(per_ok, "refresh period: %zu refreshes, first gap %lld (want 3008)", tb.t_ref_bus.size(),
        tb.t_ref_bus.size() > 1 ? (long long)(tb.t_ref_bus[1] - tb.t_ref_bus[0]) : -1LL);
  // C9: the first access after a refresh reopens its row: +6
  {
    { Xact *x = tb.mk(BIU_OC_UNC, false, RAM, BZ_L); tb.submit(x, tb.cyc + 2); tb.run_until_idle(); }
    const uint64_t r0 = tb.n_ref_bus;
    while (tb.n_ref_bus == r0) tb.step();
    tb.run(40);
    int64_t mm = (int64_t)tb.cyc + 4; if (!(mm & 1)) mm++;
    int64_t ack = -1;
    Prog p(tb);
    p.load(BIU_OC_UNC, RAM + 0x10, BZ_L, mm, [&](int64_t a) { ack = a; });
    tb.run_until_idle();
    CHECK(ack - mm == 15 + 6, "C9: the first read after a refresh: penalty %lld, want 21 (15 + 6)", (long long)(ack - mm));
    int64_t ack2 = -1, mm2 = (int64_t)tb.cyc + 5; if (!(mm2 & 1)) mm2++;
    Prog p2(tb);
    p2.load(BIU_OC_UNC, RAM + 0x20, BZ_L, mm2, [&](int64_t a) { ack2 = a; });
    tb.run_until_idle();
    CHECK(ack2 - mm2 == 15, "C9: the next read (row open again): penalty %lld, want 15", (long long)(ack2 - mm2));
  }
}

// MPX: the Holly rows of bsc-memmap 4.7 with the board's RDY# waits ((total - 18) / 2, bsc-memmap 5.3)
static void grp_mpx_timing(Tb &tb) {
  g_grp = "mpx-timing";
  tb.reset(); tb.program(dc_cfg(false));
  { Xact *x = tb.mk(BIU_OC_UNC, false, RAM, BZ_L); tb.submit(x, tb.cyc + 2); tb.run_until_idle(); }
  struct R { const char *name; int area; int rdw, wrw; bool we; int64_t want; };
  const R rows[] = {
    {"SB register read per 18 [HW sb_sbrev]", 0, 0, 0, false, 18},
    {"PVR register read per 76 [HW pvr_id] (29 RDY waits)", 0, 29, 0, false, 76},
    {"texture memory read per 94 [HW vram64] (38 RDY waits)", 1, 38, 0, false, 94},
    {"G2 read per 252 [HW aica] (117 RDY waits)", 0, 117, 0, false, 252},
    {"SB register write per 12 [HW sb_iml2nrm wr_l] (no RDY wait)", 0, 0, 0, true, 12},
    {"texture-memory write at the bus rate per 12 [HW vram64top: the first ~4 writes] (no RDY wait)", 1, 0, 0, true, 12},
  };
  for (const R &r : rows) {
    tb.board.rd_waits[r.area] = r.rdw; tb.board.wr_waits[r.area] = r.wrw;
    const uint32_t base = r.area == 0 ? 0x005F6800u : 0x04000000u;
    const int64_t per = stream(tb, 20, [&](Prog &p, int k, int64_t mm, std::function<void(int64_t)> nx) {
      if (r.we) p.store(base + 4u * (k & 7), BZ_L, mm, nx); else p.load(BIU_OC_UNC, base + 4u * (k & 7), BZ_L, mm, nx);
    });
    CHECK(per == r.want, "%s: %lld", r.name, (long long)per);
    tb.board.rd_waits[r.area] = 0; tb.board.wr_waits[r.area] = 0;
  }
  // the SB pairs [HW bscmm2 6.4]: SB read -> write 28, SB write -> RAM read 28, RAM write -> SB read 28
  {
    struct P2 { const char *name; uint32_t a1; bool w1; uint32_t a2; bool w2; int64_t want; };
    const P2 prs[] = {
      {"sb rd_wr (same register) per pair 28 [HW]", 0x005F6910u, false, 0x005F6910u, true, 28},
      {"sb>ram wr_rdalt per pair 28 [HW]", 0x005F6910u, true, RAM + 0x180, false, 28},
      {"ram>sb wr_rdalt per pair 28 [HW]", RAM + 0x180, true, 0x005F689Cu, false, 28},
    };
    for (const P2 &pp : prs) {
      std::vector<int64_t> ms;
      Prog p(tb);
      std::function<void(int, int64_t)> go = [&](int k, int64_t mm) {
        if (k >= 24) return;
        ms.push_back(mm);
        const bool w = (k & 1) ? pp.w2 : pp.w1;
        const uint32_t a = (k & 1) ? pp.a2 : pp.a1;
        auto nx = [&, k](int64_t ack) { go(k + 1, ack + 1); };
        if (w) p.store(a, BZ_L, mm, nx); else p.load(BIU_OC_UNC, a, BZ_L, mm, nx);
      };
      go(0, (int64_t)tb.cyc + 4);
      tb.settle();
      const int64_t pr = ms.size() == 24 ? ms[14] - ms[12] : -1;
      CHECK(ms.size() == 24 && pr == pp.want && ms[22] - ms[20] == pr, "%s: %lld", pp.name, (long long)pr);
    }
  }
  // RAM / SB alternating reads: 16 + 18 = 34 per pair, no extra idle [HW ram/sbrev 34]
  {
    std::vector<int64_t> ms;
    Prog p(tb);
    std::function<void(int, int64_t)> go = [&](int k, int64_t mm) {
      if (k >= 24) return;
      ms.push_back(mm);
      p.load(BIU_OC_UNC, (k & 1) ? 0x005F689Cu : RAM + 0x80, BZ_L, mm, [&, k](int64_t ack) { go(k + 1, ack + 1); });
    };
    go(0, (int64_t)tb.cyc + 4);
    tb.run_until_idle(); tb.run(20);
    CHECK(ms.size() == 24 && ms[14] - ms[12] == 34 && ms[22] - ms[20] == 34, "RAM / SB alternating reads: %lld per pair, want 34",
          ms.size() == 24 ? (long long)(ms[14] - ms[12]) : -1LL);
  }
}

// ================================================================================================================
// registers against tb/sh4_bsc.h
static void grp_regs(Tb &tb) {
  g_grp = "regs";
  tb.model_on = true;
  // straps: every MD combination of the BSC's five bits
  for (int k = 0; k < 32; k++) {
    tb.md = (uint16_t)(((k >> 4) & 1) << 5 | ((k >> 3) & 1) << 7 | ((k >> 2) & 1) << 6 | ((k >> 1) & 1) << 4 | (k & 1) << 3 | 0x005);
    tb.reset(true);
    const uint32_t b1 = tb.rd(0xF800000, 2), b2 = tb.rd(0xF800004, 1);
    CHECK(b1 == ((uint32_t)((k >> 4) & 1) << 31 | (uint32_t)((k >> 3) & 1) << 30 | (uint32_t)((k >> 2) & 1) << 29),
          "straps %02x: BCR1 %08x", k, b1);
    CHECK(b2 == (0x3FFCu | (uint32_t)((k >> 1) & 1) << 15 | (uint32_t)(k & 1) << 14), "straps %02x: BCR2 %04x", k, b2);
    tb.wr(0xF800000, 2, 0xFFFFFFFFu);
    CHECK(tb.rd(0xF800000, 2) >> 29 == (uint32_t)(k >> 2), "straps %02x: BCR1[31:29] written", k);
  }
  tb.md = 0x065;
  tb.reset(true);
  // POR values [bsc-memmap 3.1]
  const struct { uint32_t off; int size; uint32_t v; } por[] = {
    {0xF800000, 2, 0xA0000000u}, {0xF800004, 1, 0x3FFC}, {0xF800008, 2, 0x77777777u}, {0xF80000C, 2, 0xFFFEEFFFu},
    {0xF800010, 2, 0x07777777u}, {0xF800014, 2, 0}, {0xF800018, 1, 0}, {0xF80001C, 1, 0}, {0xF800020, 1, 0},
    {0xF800024, 1, 0}, {0xF800028, 1, 0}, {0xF80002C, 2, 0}, {0xF800040, 2, 0}, {0xF800048, 1, 0}};
  for (auto &r : por) CHECK(tb.rd(r.off, r.size) == r.v, "POR %07x: %08x, want %08x", r.off, tb.pr_last, r.v);
  // the Dreamcast values written and read back (board.md 5.3 / bsc-memmap 6.1)
  tb.program(dc_cfg(true));
  tb.wr(0xF80002C, 2, 0x000A03F0u); tb.wr(0xF800030, 1, 0x0004);
  CHECK(tb.rd(0xF800000, 2) == 0xA3020008u, "BCR1 %08x", tb.pr_last);
  CHECK(tb.rd(0xF800004, 1) == 0x0001, "BCR2 %04x", tb.pr_last);
  CHECK(tb.rd(0xF80000C, 2) == 0x618066D8u, "WCR2 %08x", tb.pr_last);
  CHECK(tb.rd(0xF800014, 2) == 0xC00A0E24u, "MCR %08x", tb.pr_last);
  CHECK(tb.rd(0xF800024, 1) == 0x005E, "RTCOR %04x", tb.pr_last);
  CHECK((tb.rd(0xF800030, 1) & 0x03FF) == 0x00E7, "PDTRA %04x (want 0x?0E7 [HW 6.1]: port 2 latch 1, pins 0 1 5 6 7, cable 00)", tb.pr_last);
  // the measured wrong-size reads [HW 6.2]: all 0
  const struct { uint32_t off; int size; } ws[] = {
    {0xF800000, 1}, {0xF800002, 1}, {0xF800000, 0}, {0xF800001, 0}, {0xF800002, 0}, {0xF800003, 0},
    {0xF800004, 2}, {0xF800006, 1}, {0xF80000C, 1}, {0xF80000E, 1}, {0xF80000F, 0}, {0xF80001C, 2}, {0xF80001C, 0},
    {0xF80001D, 0}, {0xF800024, 2}, {0xF800024, 0}, {0xF800028, 2}, {0xF80002C, 1}, {0xF80002E, 1}, {0xF800030, 2},
    {0xF800030, 0}, {0xF800031, 0}, {0xF800032, 1}, {0xF800044, 2}, {0xF800048, 2},
    {0xF800034, 2}, {0xF800038, 2}, {0xF80003C, 2}, {0xF80004C, 2}, {0xF800050, 1}};
  for (auto &r : ws) CHECK(tb.rd(r.off, r.size) == 0, "wrong-size / hole read %07x size %d: %08x", r.off, r.size, tb.pr_last);
  // holes have hit = 0
  for (uint32_t h : {0xF800034u, 0xF800038u, 0xF80003Cu, 0xF80004Cu, 0xF800050u, 0xF800100u, 0xF801000u}) {
    tb.rd(h, 2);
    CHECK(!tb.pr_hit_last, "hole %07x: hit", h);
  }
  // the measured wrong-size writes: WCR3 by B / W, PCR by L / B are ignored; the right sizes land [HW 6.2]
  tb.wr(0xF800010, 0, 0); tb.wr(0xF800013, 0, 0); tb.wr(0xF800010, 1, 0); tb.wr(0xF800012, 1, 0);
  CHECK(tb.rd(0xF800010, 2) == 0x07777777u, "WCR3 after B / W writes: %08x", tb.pr_last);
  tb.wr(0xF800010, 2, 0x07777700u);
  CHECK(tb.rd(0xF800010, 2) == 0x07777700u, "WCR3 L write: %08x", tb.pr_last);
  tb.wr(0xF800018, 2, 0x1111); tb.wr(0xF800018, 0, 0x11); tb.wr(0xF800019, 0, 0x11);
  CHECK(tb.rd(0xF800018, 1) == 0, "PCR after L / B writes: %04x", tb.pr_last);
  tb.wr(0xF800018, 1, 0x1111);
  CHECK(tb.rd(0xF800018, 1) == 0x1111, "PCR W write: %04x", tb.pr_last);
  tb.wr(0xF800018, 1, 0); tb.wr(0xF800010, 2, 0x07777777u);
  // the keys [HW 6.2]: RFCR 0x0000 / byte ignored, 0xA400 -> 0, 0xA500 -> 0x100, 0xA505 -> 0x105
  tb.wr(0xF800028, 1, 0xA400 | 0x77);
  tb.wr(0xF800028, 1, 0x0000); CHECK((tb.rd(0xF800028, 1) & 0x3FF) >= 0x77, "RFCR no key: %04x", tb.pr_last);
  tb.wr(0xF800028, 0, 0x00);   CHECK((tb.rd(0xF800028, 1) & 0x3FF) >= 0x77, "RFCR byte: %04x", tb.pr_last);
  tb.wr(0xF800028, 1, 0xA400); CHECK(tb.rd(0xF800028, 1) <= 0x001, "RFCR key+000: %04x", tb.pr_last);
  tb.wr(0xF800028, 1, 0xA500); CHECK((tb.rd(0xF800028, 1) & 0x3FE) == 0x100, "RFCR key+100: %04x", tb.pr_last);
  tb.wr(0xF800028, 1, 0xA505); CHECK((tb.rd(0xF800028, 1) & 0x3FE) == 0x104, "RFCR key+105: %04x", tb.pr_last);
  // RTCOR: 0x005D, a byte, 0x5A5D ignored; 0xA55D -> 0x5D; 0xA55E restores
  tb.wr(0xF800024, 1, 0x005D); CHECK(tb.rd(0xF800024, 1) == 0x5E, "RTCOR no key: %04x", tb.pr_last);
  tb.wr(0xF800024, 0, 0x5D);   CHECK(tb.rd(0xF800024, 1) == 0x5E, "RTCOR byte: %04x", tb.pr_last);
  tb.wr(0xF800024, 1, 0x5A5D); CHECK(tb.rd(0xF800024, 1) == 0x5E, "RTCOR key 5A: %04x", tb.pr_last);
  tb.wr(0xF800024, 1, 0xA55D); CHECK(tb.rd(0xF800024, 1) == 0x5D, "RTCOR key A5: %04x", tb.pr_last);
  tb.wr(0xF800024, 1, 0xA55E); CHECK(tb.rd(0xF800024, 1) == 0x5E, "RTCOR restored: %04x", tb.pr_last);
  // CMF / OVF: 1 keeps, 0 clears (with CKS running, CMF gets set by the matches)
  tb.run(4000);
  CHECK((tb.rd(0xF80001C, 1) & 0x80) != 0, "CMF not set after a refresh period: RTCSR %04x", tb.pr_last);
  tb.wr(0xF80001C, 1, 0xA590);
  CHECK((tb.rd(0xF80001C, 1) & 0x80) != 0, "CMF cleared by writing 1: RTCSR %04x", tb.pr_last);
  tb.wr(0xF80001C, 1, 0xA510);
  CHECK((tb.rd(0xF80001C, 1) & 0x80) == 0, "CMF not cleared by writing 0: RTCSR %04x", tb.pr_last);
  // random register traffic: every read compared with the model (sizes, holes, keys, counters)
  Rng rng(7);
  static const uint32_t offs[] = {0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C, 0x30, 0x34,
                                  0x38, 0x3C, 0x40, 0x44, 0x48, 0x4C, 0x50, 0x02, 0x06, 0x1E, 0x22, 0x2E, 0x31, 0x45};
  for (int i = 0; i < 6000; i++) {
    const uint32_t off = 0xF800000 + offs[rng.u(sizeof offs / sizeof offs[0])];
    const int size = (int)rng.u(3);
    if (size == 1 && (off & 1)) continue;
    if (size == 2 && (off & 3)) continue;
    const bool w = rng.u(3) == 0;
    uint32_t v = (uint32_t)rng.next();
    // keep the bus configuration sane (the BSC keeps running): BCR1 / BCR2 / MCR / WCR writes rewrite their own value
    if (w && (off == 0xF80001C || off == 0xF800020 || off == 0xF800024 || off == 0xF800028) && rng.u(2))
      v = (off == 0xF800028 ? 0xA400 : 0xA500) | (v & (off == 0xF800028 ? 0x3FF : 0xFF));
    if (w && off == 0xF80001C) v = (v & ~0x38u) | (rng.u(3) << 3);      // a fast CKS so the counter moves
    if (w && off == 0xF800024 && (v & 0xFF) < 4) v |= 4;
    tb.port_i = (uint32_t)rng.next() & 0xFFFFF;
    tb.p4(w, off, size, v);
    tb.run(rng.u(40));
  }
  // manual reset: everything holds, the counter keeps going
  const uint32_t pctra = tb.rd(0xF80002C, 2);
  tb.rst_man = true; tb.run(20); tb.rst_man = false; tb.run(2);
  CHECK(tb.rd(0xF80002C, 2) == pctra, "manual reset changed PCTRA");
  tb.model_on = false;
}

// the refresh timer: every CKS x RTCOR, RFCR limits, OVF, RCMI / ROVI (compared with the model every Bcyc)
static void grp_rfsh(Tb &tb) {
  g_grp = "rfsh";
  tb.model_on = true;
  tb.reset(true);
  static const uint32_t divs[8] = {0, 4, 16, 64, 256, 1024, 2048, 4096};
  for (int cks = 1; cks <= 7; cks++) {
    for (int cor : {0, 1, 0x5E, 0xFF}) {
      if (cks >= 5 && cor > 1) continue;                               // long periods: the short ones cover the rule
      tb.wr(0xF800024, 1, 0xA500 | cor);
      tb.wr(0xF800020, 1, 0xA500);
      tb.wr(0xF800028, 1, 0xA400);
      tb.wr(0xF80001C, 1, 0xA500 | (cks << 3) | 0x42);                 // CMIE, OVIE
      // follow RTCNT: it must equal RTCOR in exactly one Bcyc per period
      const uint64_t period = cor == 0 ? 1 : (uint64_t)cor * divs[cks];
      const uint64_t m0 = tb.model.matches;
      const uint64_t span = std::min<uint64_t>(period * 3 + 20, 200000) * 2;
      tb.run(span);
      const uint64_t got = tb.model.matches - m0;
      tb.rd(0xF800020, 1); tb.rd(0xF800028, 1); tb.rd(0xF80001C, 1);   // compared with the model at the strobe
      if (cor != 0) CHECK(got >= span / 2 / period - 1 && got <= span / 2 / period + 1,
                          "CKS %d RTCOR %02x: %llu matches in %llu Bcyc, want one per %llu", cks, cor,
                          (unsigned long long)got, (unsigned long long)(span / 2), (unsigned long long)period);
      CHECK(tb.t->rcmi == tb.model.rcmi(), "CKS %d RTCOR %02x: RCMI %d model %d", cks, cor, tb.t->rcmi, tb.model.rcmi());
    }
  }
  // RFCR limits: 1024 and 512, OVF, ROVI
  for (int lm = 0; lm < 2; lm++) {
    tb.wr(0xF800024, 1, 0xA501);                                       // a match every tick
    tb.wr(0xF800020, 1, 0xA500);
    tb.wr(0xF800028, 1, 0xA400 | (lm ? 0x1F0 : 0x3F0));
    tb.wr(0xF80001C, 1, 0xA500 | (1 << 3) | 0x02 | lm);              // CKS /4, OVIE, LMTS
    tb.run(4 * 2 * 40);
    const uint32_t csr = tb.rd(0xF80001C, 1), rf = tb.rd(0xF800028, 1);
    CHECK((csr & 0x04) != 0, "LMTS %d: OVF not set (RTCSR %04x, RFCR %04x)", lm, csr, rf);
    CHECK(rf < 0x40, "LMTS %d: RFCR %04x did not wrap at the limit", lm, rf);
    CHECK(tb.t->rovi == 1, "LMTS %d: ROVI not asserted", lm);
    tb.wr(0xF80001C, 1, 0xA500);                                       // OVF written 0, CKS 0: cleared, stopped
    CHECK((tb.rd(0xF80001C, 1) & 0x04) == 0 && tb.t->rovi == 0, "LMTS %d: OVF not cleared by writing 0", lm);
  }
  tb.model_on = false;
}

// ================================================================================================================
// MPX: every AnW code, read and write, single and burst, RDY waits 0..200; the size code
static void grp_mpx(Tb &tb) {
  g_grp = "mpx";
  tb.reset(); tb.program(dc_cfg(false));
  Rng rng(11);
  for (int code = 0; code < 8; code++) {
    Cfg c = dc_cfg(false);
    c.wcr2 = (c.wcr2 & ~(7u << 6)) | ((uint32_t)code << 6);             // A1W
    tb.program(c);
    for (int we = 0; we < 2; we++)
      for (int line = 0; line < 2; line++)
        for (int rw : {0, 1, 5, 200}) {
          tb.board.rd_waits[1] = rw; tb.board.wr_waits[1] = rw;
          const uint32_t a = 0x04000000u + 0x100u * rng.u(64) + (line ? 0 : 4 * rng.u(8));
          Xact *x = tb.mk(line ? (we ? BIU_SQ : BIU_OC_FILL) : (we ? BIU_OC_WTB : BIU_OC_UNC), we, a, line ? BZ_LINE : BZ_L);
          const size_t nlog = tb.board.log.size();
          tb.submit(x, (int64_t)tb.cyc + 2);
          tb.settle();
          if (tb.board.log.size() != nlog + 1) { failf("A1W %d %s %s: %zu bus cycles", code, we ? "write" : "read", line ? "burst" : "single", tb.board.log.size() - nlog); continue; }
          const Board::Cyc &cy = tb.board.log.back();
          const int w1 = we ? (code & 3) : ((code & 3) == 0 ? 1 : (code & 3));
          const int w2 = (code >> 2) & 1;
          const int nb = line ? 4 : 1;
          CHECK(cy.size == (line ? 4 : 2), "A1W %d: size code %d", code, cy.size);
          CHECK(cy.nbeat == nb, "A1W %d: %d beats", code, cy.nbeat);
          if (cy.nbeat == nb) {
            // first beat presented at Tm1 + 1 + w1 Bcyc, completing rw Bcyc later; later beats w2 + 1 after + rw
            const int64_t f = (int64_t)(cy.tb[0] - cy.t_start) / 2;
            CHECK(f == 1 + w1 + rw, "A1W %d %s %s RDY %d: first data %lld Bcyc after Tm1, want %d", code, we ? "wr" : "rd",
                  line ? "burst" : "single", rw, (long long)f, 1 + w1 + rw);
            for (int k = 1; k < nb; k++) {
              const int64_t g = (int64_t)(cy.tb[k] - cy.tb[k - 1]) / 2;
              CHECK(g == 1 + w2 + rw, "A1W %d burst beat %d: %lld Bcyc after the previous, want %d", code, k, (long long)g, 1 + w2 + rw);
            }
          }
        }
  }
  tb.board.rd_waits[1] = tb.board.wr_waits[1] = 0;
  // size codes: B 0, W 1, L 2, a Q = two L cycles, a line = burst 4
  tb.program(dc_cfg(false));
  const struct { int size; int ncyc; int code; } sc[] = {{BZ_B, 1, 0}, {BZ_W, 1, 1}, {BZ_L, 1, 2}, {BZ_Q, 2, 2}, {BZ_LINE, 1, 4}};
  for (auto &s : sc) {
    const size_t n0 = tb.board.log.size();
    Xact *x = tb.mk(s.size == BZ_LINE ? BIU_OC_FILL : BIU_OC_UNC, false, 0x04000100u, s.size);
    tb.submit(x, (int64_t)tb.cyc + 2); tb.settle();
    CHECK(tb.board.log.size() == n0 + s.ncyc && tb.board.log.back().size == s.code, "MPX size %d: %zu cycles, code %d", s.size,
          tb.board.log.size() - n0, tb.board.log.empty() ? -1 : tb.board.log.back().size);
  }
}

// ================================================================================================================
// byte lanes: 8 / 16 / 32 / 64-bit basic areas, every size and alignment, the split order
static void grp_lanes(Tb &tb) {
  g_grp = "lanes";
  tb.reset();
  Rng rng(3);
  for (int wid = 0; wid < 4; wid++) {                                 // BCR2 code: 00 64, 01 8, 10 16, 11 32
    Cfg c = dc_cfg(false);
    c.bcr1 &= ~(1u << 17);                                            // MEMMPX off: areas 1, 2, 4-6 are SRAM
    c.bcr2 = (uint16_t)((wid << 2) | 1);                              // A1SZ (area 1)
    c.wcr2 = (c.wcr2 & ~(7u << 6)) | (1u << 6);                       // A1W 1 wait
    tb.program(c);
    const int wb = wid == 0 ? 8 : wid == 1 ? 1 : wid == 2 ? 2 : 4;
    for (int size = BZ_B; size <= BZ_LINE; size++)
      for (int al = 0; al < 8; al++) {
        if (size == BZ_W && (al & 1)) continue;
        if (size == BZ_L && (al & 3)) continue;
        if ((size == BZ_Q || size == BZ_LINE) && al) continue;
        const uint32_t a = 0x04000000u + 0x40u * rng.u(256) + (size == BZ_LINE ? 8u * rng.u(4) : (uint32_t)al);
        // write then read back, and look at the pieces on the bus
        for (int we = 1; we >= 0; we--) {
          const size_t n0 = tb.board.log.size();
          Xact *x = tb.mk(size == BZ_LINE ? (we ? BIU_SQ : BIU_OC_FILL) : (we ? BIU_OC_WTB : BIU_OC_UNC), we != 0, a, size);
          tb.submit(x, (int64_t)tb.cyc + 2); tb.settle();
          const int sb = SZB[size];
          const int pieces_q = size == BZ_Q ? 2 : 1;                   // a Q is two L halves
          const int pb = std::min(size == BZ_Q ? 4 : sb, wb);
          const int npieces = size == BZ_LINE ? 32 / std::min(8, wb) : (size == BZ_Q ? pieces_q * std::max(1, 4 / wb) : std::max(1, sb / wb));
          int beats = 0; uint32_t addrs = 0; bool order = true; int64_t prev = -1;
          for (size_t i = n0; i < tb.board.log.size(); i++) for (size_t k = 0; k < tb.board.log[i].ba.size(); k++) {
            beats++;
            const uint8_t be = tb.board.log[i].be[k];
            int lo = 0; while (lo < 8 && !((be >> lo) & 1)) lo++;
            const int64_t byte_addr = (int64_t)tb.board.log[i].ba[k] + lo;
            if (size != BZ_LINE && prev >= 0 && byte_addr <= prev) order = false;
            prev = byte_addr;
            int nbit = 0; for (int b = 0; b < 8; b++) nbit += (be >> b) & 1;
            if (nbit > pb && !(size == BZ_LINE && wb == 8)) order = false;
            addrs++;
          }
          (void)addrs;
          CHECK(beats == npieces, "width %d size %d align %d %s: %d pieces on the bus, want %d", wb, size, al, we ? "wr" : "rd", beats, npieces);
          CHECK(order, "width %d size %d align %d %s: pieces out of order or wider than the bus", wb, size, al, we ? "wr" : "rd");
        }
        // a DMAC Q is not split into halves (SOC.md 3.3.2): one access, the bus's width split -- 8 / 4 / 2 / 1 pieces of
        // the bus width, lowest address first, one rd_v / wd_req; the data through the shadow
        if (size == BZ_Q)
          for (int we = 1; we >= 0; we--) {
            const size_t n0 = tb.board.log.size();
            Xact *x = tb.mk(PORT_DMA, we != 0, a + 0x20u, BZ_Q);
            x->master = BM_DMA_DUAL;
            tb.submit(x, (int64_t)tb.cyc + 2); tb.settle();
            const int want = std::max(1, 8 / wb);
            int beats = 0; bool order = true; int64_t prev = -1;
            for (size_t i = n0; i < tb.board.log.size(); i++) for (size_t k = 0; k < tb.board.log[i].ba.size(); k++) {
              beats++;
              const uint8_t be = tb.board.log[i].be[k];
              int lo = 0; while (lo < 8 && !((be >> lo) & 1)) lo++;
              int nbit = 0; for (int b = 0; b < 8; b++) nbit += (be >> b) & 1;
              const int64_t byte_addr = (int64_t)tb.board.log[i].ba[k] + lo;
              if (byte_addr <= prev || nbit != std::min(8, wb)) order = false;
              prev = byte_addr;
            }
            CHECK(beats == want && order && x->t_done >= 0 && (we ? x->nw : x->nb) == 1,
                  "width %d DMAC Q %s: %d pieces (want %d), in order and bus-wide %d, done %d after %d %s", wb, we ? "wr" : "rd",
                  beats, want, order, x->t_done >= 0, we ? x->nw : x->nb, we ? "wd_req" : "rd_v");
          }
      }
  }
  tb.program(dc_cfg(false));
}

// ================================================================================================================
// the idle rule (WCR1): gaps between bus cycles from different requesters that both wait
static void grp_idle(Tb &tb) {
  g_grp = "idle";
  tb.reset();
  Rng rng(5);
  for (int it = 0; it < 300; it++) {
    Cfg c = dc_cfg(false);
    c.wcr1 = (uint32_t)rng.next() & 0x77777777u & ~0x70007000u;       // A3IW = DMAIW = 0 (RAS-down rule)
    tb.program(c);
    // two accesses queued at once (a line on one port, a single on another), the second waits for the bus
    const int a1 = (int)rng.u(3) == 0 ? 3 : (int)rng.u(2);            // area 0 / 1 (MPX) or 3 (SDRAM)
    const int a2 = (int)rng.u(3) == 0 ? 3 : (int)rng.u(2);
    const bool w1 = rng.u(2), w2 = rng.u(2);
    const uint32_t b1 = a1 == 3 ? RAM + 0x100 : (uint32_t)a1 << 26 | 0x100, b2 = a2 == 3 ? RAM + 0x200 : (uint32_t)a2 << 26 | 0x200;
    // a read then a write on one port each: the BIU's rules decide the order; we look at the bus only
    Xact *x1 = tb.mk(w1 ? BIU_SQ : BIU_OC_FILL, w1, b1, BZ_LINE);
    Xact *x2 = tb.mk(w2 ? BIU_OC_WTB : BIU_IC_UNC, w2, b2, BZ_L);
    const size_t n0 = tb.board.log.size();
    tb.submit(x1, (int64_t)tb.cyc + 2);
    tb.submit(x2, (int64_t)tb.cyc + 4);
    tb.settle();
    if (tb.board.log.size() < n0 + 2) continue;
    const Board::Cyc &p = tb.board.log[tb.board.log.size() - 2], &n = tb.board.log.back();
    const bool pmpx = p.area != 3, nmpx = n.area != 3;
    const uint32_t m = sh4bsc::w8((c.wcr1 >> (4 * p.area)) & 7);
    uint32_t idle;
    if (!p.we) idle = nmpx ? std::max<uint32_t>(m, 1) : (p.area != n.area ? m : (n.we ? m : 0));
    else idle = p.area == n.area ? 0 : (nmpx ? std::max<uint32_t>(m, 1) : m);
    // the next cycle's first bus cycle: MPX Tm1 = its start; SDRAM: its first command (start - CL for a read, - 1 for
    // a row-hit write's Tnop): use the start for MPX only; for SDRAM the command stream is the protocol checker's
    (void)pmpx;
    if (nmpx) {
      const int64_t gap = (int64_t)(n.t_start - p.t_last) / 2 - 1;
      CHECK(gap >= (int64_t)idle, "WCR1 %08x: area %d %s -> area %d %s: %lld idle Bcyc, want >= %u", c.wcr1, p.area,
            p.we ? "wr" : "rd", n.area, n.we ? "wr" : "rd", (long long)gap, idle);
    }
  }
  // exact: an SB read waiting behind an SB read: the MPX minimum of one idle Bcyc (WCR1 A0IW = 1: 1)
  tb.program(dc_cfg(false));
  {
    Xact *x1 = tb.mk(BIU_OC_FILL, false, 0x005F6800u, BZ_LINE), *x2 = tb.mk(BIU_IC_UNC, false, 0x005F6900u, BZ_L);
    const size_t n0 = tb.board.log.size();
    tb.submit(x1, (int64_t)tb.cyc + 2); tb.submit(x2, (int64_t)tb.cyc + 3);
    tb.settle();
    if (tb.board.log.size() == n0 + 2) {
      const int64_t gap = (int64_t)(tb.board.log[n0 + 1].t_start - tb.board.log[n0].t_last) / 2 - 1;
      CHECK(gap == 1, "MPX read -> MPX read (A0IW 1): %lld idle Bcyc, want exactly 1", (long long)gap);
    } else failf("MPX idle: %zu cycles", tb.board.log.size() - n0);
  }
}

// ================================================================================================================
// random traffic on every port and area, random SDRAM timing, refresh, the DMAC (dual and single address), area 7:
// data (shadow memory), the SDRAM protocol checker, handshakes, the final memory image
static int g_rcfg = -1;
static void grp_random(Tb &tb, uint64_t seed, int n) {
  g_grp = "random";
  for (int cfgi = 0; cfgi < 6; cfgi++) {
    Rng rng(seed * 16 + (uint64_t)cfgi);
    if (g_rcfg >= 0 && cfgi != g_rcfg) continue;
    for (int p = 0; p < 8; p++) tb.port[p] = Port();                   // drop a previous config's stuck requests
    tb.reset();
    Cfg c = dc_cfg(true);
    const bool b32 = cfgi == 3;
    c.mcr = (c.mcr & ~((7u << 19) | (3u << 16) | (7u << 13) | (7u << 10) | (3u << 7) | (1u << 31))) |
            ((uint32_t)rng.u(3) << 19) | ((uint32_t)(1 + rng.u(2)) << 16) | ((uint32_t)rng.u(2) << 13) |
            ((uint32_t)rng.u(4) << 10) | (b32 ? 3u << 7 : 0) | (cfgi == 4 ? 0 : 1u << 31);
    c.wcr2 = (c.wcr2 & ~(7u << 13)) | ((uint32_t)(2 + rng.u(2)) << 13);        // CL 2 / 3
    c.rtcor = (uint16_t)(20 + rng.u(40));
    c.rtcsr = 0x0008;                                                  // CKS /4: frequent refreshes
    tb.program(c);
    for (int a : {0, 1, 2, 4, 5, 6}) { tb.board.rd_waits[a] = (int)rng.u(4); tb.board.wr_waits[a] = (int)rng.u(3); }
    // the shadow starts equal to the board
    tb.shadow.ram = tb.board.ram; tb.shadow.mem = tb.board.mem; tb.shadow.dev_byte_n = tb.board.dev_byte_n;
    int issued = 0;
    auto rnd_addr = [&](int size) -> uint32_t {
      const int r = (int)rng.u(10);
      uint32_t a;
      if (r < 6) a = 0x0C000000u + (rng.u(4) << 22) + (rng.u(3) << 11) + rng.u(2048);   // banks x rows
      else if (r < 8) a = (rng.u(2) ? 0x04000000u : 0x00000000u) + rng.u(4096);
      else if (r < 9) a = 0x10000000u + rng.u(4096);
      else a = 0x1C000000u + rng.u(4096);                              // area 7
      if (size == BZ_W) a &= ~1u;
      if (size == BZ_L) a &= ~3u;
      if (size == BZ_Q || size == BZ_LINE) a &= ~7u;
      return a;
    };
    const uint64_t c_start = tb.cyc;
    bool stuck = false;
    while (issued < n) {
      if (tb.cyc - c_start > (uint64_t)n * 400 + 100000) {
        failf("cfg %d: stuck after %d accesses (cycle %llu)", cfgi, issued, (unsigned long long)tb.cyc);
        tb.dump();
        stuck = true;
        break;
      }
      // a new request on an idle port now and then
      for (int p = 0; p < 8; p++) {
        Port &P = tb.port[p];
        if (P.cur || !P.q.empty() || P.fly.size() > 0 || rng.u(6) != 0) continue;
        bool we; int size;
        switch (p) {
        case BIU_IC_FILL: case BIU_OC_FILL: we = false; size = BZ_LINE; break;
        case BIU_OC_WBB: case BIU_SQ: we = true; size = BZ_LINE; break;
        case BIU_OC_WTB: we = true; size = (int)rng.u(4); break;
        case PORT_DMA: we = rng.u(2); size = (int)rng.u(5); break;
        default: we = false; size = (int)rng.u(4); break;
        }
        uint32_t a = rnd_addr(size);
        if (we && (a >> 26) == 7) a &= 0x03FFFFFF, a |= 0x0C000000u;   // keep area-7 writes out (no data)
        Xact *x = tb.mk(p, we, a, size);
        if (p == PORT_DMA && rng.u(3) == 0 && (a >> 26) == 3) x->master = BM_DMA_SINGLE, x->ch = (int)rng.u(4);
        tb.submit(x, (int64_t)tb.cyc + 1);
        issued++;
      }
      tb.step();
    }
    if (stuck) continue;
    if (!tb.run_until_idle(400000)) continue;
    tb.settle();
    // the final images
    long nd = 0;
    for (size_t i = 0; i < tb.board.ram.size(); i++) if (tb.board.ram[i] != tb.shadow.ram[i]) { if (nd < 5) failf("cfg %d: RAM byte %06zx board %02x shadow %02x", cfgi, i, tb.board.ram[i], tb.shadow.ram[i]); nd++; }
    for (auto &kv : tb.board.mem) {
      auto &s = tb.shadow.mem[kv.first];
      if (s.empty()) s.assign(65536, 0);
      for (size_t i = 0; i < kv.second.size(); i++) if (kv.second[i] != s[i]) { if (nd < 5) failf("cfg %d: area %d byte %04zx board %02x shadow %02x", cfgi, kv.first, i, kv.second[i], s[i]); nd++; }
    }
    CHECK(nd == 0, "cfg %d: %ld bytes differ between the board and the shadow", cfgi, nd);
    CHECK(tb.sd.n_ref > 0, "cfg %d: no refresh in the random run", cfgi);
    printf("  random cfg %d (MCR %08x WCR2 %08x): %d accesses, %llu SDRAM commands, %llu refreshes, %llu DMAW records\n",
           cfgi, c.mcr, c.wcr2, n, (unsigned long long)tb.sd.n_cmd, (unsigned long long)tb.sd.n_ref, (unsigned long long)tb.dma_tap_n);
  }
}

// ================================================================================================================
// BREQ / BACK, the DDT hand-over, bpost_bsc, SDMR, ports and GPIO, manual reset of a running transaction
static void grp_misc(Tb &tb) {
  g_grp = "misc";
  tb.reset(); tb.program(dc_cfg(false));
  // SDMR3 byte write to 0xFF940190 with MRSET: one MRS with the mode value 0x32 (CL3 / BL4 / 64-bit) [bsc-memmap 3.2]
  {
    const uint64_t m0 = tb.n_mrs_bus;
    tb.p4(true, 0xF940190, 0, 0, true);
    tb.run(20);
    CHECK(tb.n_mrs_bus == m0 + 1 && tb.last_mrs == 0x32, "SDMR3 0xFF940190: %llu MRS, value %03x (want 0x032)",
          (unsigned long long)(tb.n_mrs_bus - m0), tb.last_mrs);
    tb.p4(false, 0xF940190, 0, 0, true);
    CHECK(tb.pr_last == 0 && tb.pr_hit_last, "SDMR read: %08x hit %d", tb.pr_last, tb.pr_hit_last);
  }
  // bpost_bsc holds new accesses
  {
    tb.bpost = true;
    Xact *x = tb.mk(BIU_OC_UNC, false, RAM, BZ_L);
    tb.submit(x, (int64_t)tb.cyc + 2);
    tb.run(40);
    CHECK(x->t_ready < 0, "bpost_bsc: an access was accepted");
    tb.bpost = false;
    tb.run_until_idle();
    CHECK(x->t_ready >= 0, "bpost_bsc: the access never started");
  }
  // BREQ / BACK with BREQEN: after the current access, PALL (rows open), BACK# low; nothing on the bus; back after
  {
    Cfg c = dc_cfg(false); c.bcr1 |= 1u << 19; tb.program(c);
    Xact *x = tb.mk(BIU_OC_FILL, false, RAM, BZ_LINE);
    tb.submit(x, (int64_t)tb.cyc + 2);
    tb.run(3);
    tb.breq_n = false;
    bool saw_back = false; uint64_t pall0 = tb.sd.n_cmd;
    for (int i = 0; i < 60; i++) { tb.step(); if (!tb.t->back_n) saw_back = true; }
    CHECK(saw_back, "BREQ: BACK# never asserted");
    CHECK(x->t_done >= 0, "BREQ: the running access did not complete before the release");
    Xact *y = tb.mk(BIU_OC_UNC, false, RAM + 0x40, BZ_L);
    tb.submit(y, (int64_t)tb.cyc + 1);
    tb.run(40);
    CHECK(y->t_ready < 0, "BREQ: an access was accepted while the bus was released");
    tb.breq_n = true;
    tb.run_until_idle();
    CHECK(y->t_done >= 0 && tb.t->back_n, "BREQ: the bus did not come back");
    (void)pall0;
    tb.program(dc_cfg(false));
  }
  // DDT: ddt_bus_req -> one ddt_bavl pulse once the bus is free
  {
    Xact *x = tb.mk(BIU_OC_FILL, false, RAM, BZ_LINE);
    tb.submit(x, (int64_t)tb.cyc + 2);
    tb.run(4);
    tb.ddt_req = true;
    int pulses = 0; int64_t t_b = -1;
    for (int i = 0; i < 60 && pulses == 0; i++) { tb.step(); if (tb.t->ddt_bavl) { pulses++; t_b = (int64_t)tb.cyc; } }
    tb.ddt_req = false;
    tb.run_until_idle();
    CHECK(pulses == 1 && x->t_done >= 0 && t_b > x->t_crit, "DDT: %d BAVL pulses at %lld (the fill's beats until %lld)", pulses, (long long)t_b, (long long)x->t_done);
  }
  // ports and GPIO: outputs, input sampling, PORTEN, the GPIO level
  {
    tb.wr(0xF80002C, 2, 0x00000005u);                                   // PA0, PA1 outputs
    tb.wr(0xF800030, 1, 0x0002);
    tb.run(4);
    CHECK((tb.t->port_o & 3) == 2 && (tb.t->port_oe & 0xFFFFF) == 3, "port A: out %05x oe %05x", tb.t->port_o, tb.t->port_oe);
    tb.port_i = 0x00000;
    tb.run(4);
    CHECK((tb.rd(0xF800030, 1) & 0xFFFF) == 0x0002, "PDTRA: latch for outputs, pins for inputs: %04x", tb.pr_last);
    tb.wr(0xF800048, 1, 0x0100);                                        // PA8 interrupt input, pin low
    tb.run(4);
    CHECK(tb.t->gpio == 1, "GPIO: the level did not rise");
    tb.port_i = 0x00100; tb.run(4);
    CHECK(tb.t->gpio == 0, "GPIO: the level did not fall");
    tb.wr(0xF800048, 1, 0); tb.wr(0xF80002C, 2, 0); tb.port_i = 0x01CE3;
    Cfg c = dc_cfg(false); c.bcr2 = 0; tb.program(c);
    CHECK((tb.t->port_oe & 0xFFFFF) == 0, "PORTEN = 0: port_oe %05x", tb.t->port_oe);
    tb.program(dc_cfg(false));
  }
  // a manual reset in the middle of a line read: it completes, nothing new starts until the reset ends
  {
    Xact *x = tb.mk(BIU_OC_FILL, false, RAM + 0x200, BZ_LINE), *y = tb.mk(BIU_OC_UNC, false, RAM + 0x300, BZ_L);
    tb.submit(x, (int64_t)tb.cyc + 2);
    tb.run(5);
    tb.rst_man = true;
    tb.submit(y, (int64_t)tb.cyc + 1);
    tb.run(30);
    CHECK(x->t_done >= 0 && y->t_ready < 0, "manual reset: running fill done %d, new read accepted %d", x->t_done >= 0, y->t_ready >= 0);
    tb.rst_man = false;
    tb.run_until_idle();
    CHECK(y->t_done >= 0, "manual reset: the waiting read never ran");
  }
  // the DMAC: a dual 32-byte read + write pair (locked), a single-address write (device -> memory) with DMAW records
  {
    Xact *r = tb.mk(PORT_DMA, false, RAM + 0x400, BZ_LINE);
    r->master = BM_DMA_DUAL; r->lock = true;
    tb.submit(r, (int64_t)tb.cyc + 2);
    Xact *c1 = tb.mk(BIU_OC_UNC, false, RAM + 0x480, BZ_L);
    tb.submit(c1, (int64_t)tb.cyc + 4);
    tb.run(30);
    Xact *w = tb.mk(PORT_DMA, true, RAM + 0x500, BZ_LINE);
    w->master = BM_DMA_DUAL;
    tb.submit(w, (int64_t)tb.cyc + 1);
    tb.run_until_idle();
    CHECK(c1->t_ready > w->t_ready, "DMAC locked pair: the CPU read (ready %lld) went between the pair (write %lld)", (long long)c1->t_ready, (long long)w->t_ready);
    const int64_t n0 = tb.dma_tap_n;
    Xact *s = tb.mk(PORT_DMA, true, RAM + 0x600, BZ_LINE);
    s->master = BM_DMA_SINGLE; s->ch = 2;
    tb.submit(s, (int64_t)tb.cyc + 1);
    tb.settle();                                                        // done precedes the last beats (posted)
    CHECK(tb.dma_tap_n - n0 == 4, "DMAW: %lld records for a 32-byte single-address write, want 4", (long long)(tb.dma_tap_n - n0));
  }
  // DMAC single-address reads (CH2-DMA): the next unit is presented right after the previous ready; the SDRAM keeps the
  // bursts back to back (hm table 13.16): 4 Bcyc = 8 Icyc per 32 bytes at best [P; HW dmac1 S13 5.77 Bcyc with Holly]
  {
    std::vector<int64_t> rdy;
    const int NU = 12;
    std::function<void(int)> go = [&](int k) {
      if (k >= NU) return;
      Xact *x = tb.mk(PORT_DMA, false, RAM + 0x800 + 32u * k, BZ_LINE);
      x->master = BM_DMA_SINGLE; x->ch = 2;
      x->on_ready = [&, k](Xact &xx) { rdy.push_back(xx.t_ready); go(k + 1); };
      tb.submit(x, (int64_t)tb.cyc + 1);
    };
    const size_t s0 = tb.board.dack_sink.size();
    go(0);
    tb.run_until_idle(); tb.run(20);
    CHECK(rdy.size() == (size_t)NU && rdy[8] - rdy[7] == 8 && tb.board.dack_sink.size() - s0 == 32u * NU,
          "DMAC single-address stream: %lld Icyc per unit (want 8), %zu bytes to the DACK sink",
          rdy.size() > 8 ? (long long)(rdy[8] - rdy[7]) : -1LL, tb.board.dack_sink.size() - s0);
  }
  // refresh requests do not queue (hm 13.3.5): ~8 compare matches while a long MPX read holds the bus give one
  // refresh after it, the next one only at the next match
  {
    Cfg c = dc_cfg(false);
    c.rtcor = 6; c.rtcsr = 0x0008;                                     // CKS /4: a match every 24 Bcyc
    tb.program(c);
    tb.board.rd_waits[1] = 200;
    const size_t r0 = tb.t_ref_bus.size();
    Xact *x = tb.mk(BIU_OC_UNC, false, 0x04000200u, BZ_L);
    tb.submit(x, (int64_t)tb.cyc + 2);
    tb.run_until_idle();
    const int64_t t_end = x->t_done;
    tb.run(200);
    tb.board.rd_waits[1] = 0;
    // matches during the hold: one per 48 Icyc; REFs in the two periods after it: the one pending request and at most
    // the two matches of those periods (queued requests would add one per match of the hold)
    int during = 0, win = 0;
    for (size_t i = r0; i < tb.t_ref_bus.size(); i++) {
      const int64_t t = (int64_t)tb.t_ref_bus[i];
      if (t > x->t_ready && t <= t_end) during++;
      if (t > t_end && t <= t_end + 96) win++;
    }
    const int64_t hold = t_end - x->t_ready;
    CHECK(hold / 48 >= 6 && during == 0 && win >= 1 && win <= 3,
          "refresh not queued: %lld matches during a %lld-Icyc bus hold, %d REFs during it, %d in the 96 Icyc after it "
          "(want 0 and 1-3)", (long long)(hold / 48), (long long)hold, during, win);
    tb.program(dc_cfg(false));
  }
  // TAS.B: after the locked read only the WTB's write may follow; an SQ burst asked for in between waits for the pair
  {
    Xact *rd = tb.mk(BIU_OC_UNC, false, RAM + 0x700, BZ_B);
    rd->lock = true;
    Xact *sq = tb.mk(BIU_SQ, true, RAM + 0x740, BZ_LINE);
    Xact *wr = tb.mk(BIU_OC_WTB, true, RAM + 0x700, BZ_B);
    rd->on_crit = [&](Xact &x) { tb.submit(wr, x.t_crit + 6); };        // the TAS write, a little late
    tb.submit(rd, (int64_t)tb.cyc + 2);
    tb.submit(sq, (int64_t)tb.cyc + 4);                                  // the SQ asks during the pair
    tb.settle();
    CHECK(rd->t_ready >= 0 && wr->t_ready > rd->t_ready && sq->t_ready > wr->t_ready,
          "TAS pair: locked read %lld, its write %lld, an SQ burst asked in between %lld (want read < write < SQ)",
          (long long)rd->t_ready, (long long)wr->t_ready, (long long)sq->t_ready);
  }
  // area 7 (C16): no bus cycle, read 0, the row-hit latency
  {
    const size_t n0 = tb.board.log.size();
    int64_t mm = (int64_t)tb.cyc + 4; if (!(mm & 1)) mm++;
    int64_t ack = -1;
    Prog p(tb);
    p.load(BIU_OC_UNC, 0x1F000024u, BZ_L, mm, [&](int64_t a) { ack = a; });
    tb.run_until_idle();
    CHECK(tb.board.log.size() == n0 && ack - mm == 15, "area 7: %zu bus cycles, penalty %lld (want 0, 15)", tb.board.log.size() - n0, (long long)(ack - mm));
  }
}

// ================================================================================================================
// the independent verification's findings (agents/reports/units/WI-BSC-verify.md), one group each; every check here
// failed on the RTL / model before the fix step (agents/reports/units/WI-BSC-fix.md)

// a DC SDRAM address: bank a[23:22], row a[21:11], 8-byte column a[10:3] (MCR AMX 4, 64-bit)
static uint32_t ram_a(int bank, int row, int col8) {
  return 0x0C000000u | ((uint32_t)bank << 22) | ((uint32_t)row << 11) | ((uint32_t)col8 << 3);
}
static const int W8[8] = {0, 1, 2, 3, 6, 9, 12, 15};
static int64_t bnow(const Tb &tb) { return (int64_t)(tb.cyc / 2); }

// the basic interface: accesses of more than 63 Bcyc next to SDRAM traffic and refresh (the bus length is 12 bits,
// v1_bsc_pkg blen_t), width splits never cut [finding 1]
static void grp_basic(Tb &tb) {
  g_grp = "basic";
  // an 8-bit SRAM line write (32 pieces x 3 Bcyc = 96 Bcyc), then a DMAC SDRAM read raised after its accept: the
  // SDRAM access starts after the last piece (verify repro `longbasic`)
  {
    tb.md = 0x02D;                                                     // MD6 0: area 0 normal memory; MD4:3 01: 8-bit
    tb.reset();
    Cfg c = dc_cfg(false); c.bcr1 = 0x03020008u; c.wcr3 = 0; c.wcr2 = 0x618066C8u;    // A0W 1, no setup / hold
    tb.program(c);
    tb.shadow_sync();
    int64_t t_last = -1, t_sd = -1; int np = 0;
    tb.on_cycle = [&] {
      auto *v = tb.t.get();
      if (!tb.bce()) return;
      if (v->xb_beat && v->xb_rdy && tb.board.carea == 0) { t_last = bnow(tb); np++; }
      if (t_sd < 0 && v->xb_sd_cmd_v) t_sd = bnow(tb);
    };
    Xact *w = tb.mk(BIU_SQ, true, 0x00001000u, BZ_LINE);
    tb.submit(w, (int64_t)tb.cyc + 1);
    while (w->t_ready < 0) tb.step();
    Xact *d = tb.mk(PORT_DMA, false, ram_a(0, 1, 0), BZ_L);
    d->master = BM_DMA_DUAL;
    tb.submit(d, (int64_t)tb.cyc + 2);
    tb.settle();
    tb.on_cycle = nullptr;
    CHECK(np == 32 && t_sd > t_last && d->t_done >= 0,
          "8-bit SRAM line write: %d pieces, the last at Bcyc %lld; the DMAC's SDRAM access: first command at %lld (want after "
          "the last piece), done %d", np, (long long)t_last, (long long)t_sd, d->t_done >= 0);
    CHECK(tb.mem_diff("longbasic") == 0, "8-bit SRAM line write + DMAC read: memory differs");
  }
  // random accesses on narrow buses (area 0 SRAM 8 / 16 / 32-bit with setup, 3 waits and hold -- 9 Bcyc a piece, an
  // 8-bit line 288 Bcyc; area 1 MPX 32-bit) mixed with DMAC SDRAM accesses and a refresh every 12 Bcyc: the pieces of
  // each access in order and uncut (the bus checker), the data (the shadow), the final memory (verify `narrow`)
  for (int w : {1, 2, 3, 4}) {
    Rng r(100 + (uint64_t)w);
    tb.md = w < 4 ? (uint16_t)(0x025 | ((w & 1) << 3) | (((w >> 1) & 1) << 4)) : 0x065;
    tb.reset();
    Cfg c = dc_cfg(false);
    c.rtcor = 3; c.rtcsr = 0x0008;                                     // a compare match every 12 Bcyc
    if (w < 4) c.bcr1 = 0x03020008u; else c.bcr2 = 0x000D;             // w 4: A1SZ 11 (32-bit MPX)
    tb.program(c);
    tb.shadow_sync();
    const uint32_t base = w < 4 ? 0x00001000u : 0x04001000u;
    const int area = (int)(base >> 26), wb = w == 1 ? 1 : w == 2 ? 2 : 4;
    std::vector<uint32_t> st;                                          // the area's first cycles (T1 / Tm1), in order
    tb.on_cycle = [&] {
      auto *v = tb.t.get();
      if (tb.bce() && v->xb_start && (int)v->xb_area == area) st.push_back(v->xb_addr);
    };
    std::vector<Xact *> xs;
    const uint64_t r0 = tb.n_ref_bus;
    bool ok = true;
    for (int i = 0; i < 120 && ok; i++) {
      const int size = (int)r.u(5);
      const bool we = r.u(2);
      uint32_t a = base + 8 * r.u(32);
      if (size == BZ_B) a += r.u(8); else if (size == BZ_W) a += 2 * r.u(4); else if (size == BZ_L) a += 4 * r.u(2);
      const int port = we ? (size == BZ_LINE ? BIU_SQ : BIU_OC_WTB) : (size == BZ_LINE ? BIU_OC_FILL : BIU_OC_UNC);
      Xact *x = tb.mk(port, we, a, size);
      tb.submit(x, (int64_t)tb.cyc + 1);
      xs.push_back(x);
      if (r.u(3) == 0) {
        Xact *d = tb.mk(PORT_DMA, r.u(2) != 0, ram_a((int)r.u(4), 3, (int)r.u(64)), BZ_L);
        d->master = BM_DMA_DUAL;
        tb.submit(d, (int64_t)tb.cyc + 2);
      }
      ok = tb.run_until_idle(40000);
    }
    tb.settle();
    tb.on_cycle = nullptr;
    // the expected first cycles: accept order is bus order; a piece is min(size, width) bytes (a Q is two L halves;
    // a line on SRAM wraps from the critical quadword; a line on MPX is one burst)
    std::vector<Xact *> ord(xs);
    std::stable_sort(ord.begin(), ord.end(), [](Xact *p, Xact *q) { return p->t_ready < q->t_ready; });
    std::vector<uint32_t> want;
    for (Xact *x : ord) {
      if (x->size == BZ_LINE) {
        if (w == 4) { want.push_back(x->addr); continue; }
        for (int k = 0; k < 32 / wb; k++) want.push_back((x->addr & ~31u) | ((x->addr + (uint32_t)(k * wb)) & 31u));
        continue;
      }
      const int sb = x->size == BZ_Q ? 4 : SZB[x->size], pb = std::min(sb, wb), n = std::max(1, sb / wb);
      for (int h = 0; h < (x->size == BZ_Q ? 2 : 1); h++)
        for (int k = 0; k < (w == 4 ? 1 : n); k++) want.push_back(x->addr + 4u * (uint32_t)h + (uint32_t)(k * pb));
    }
    size_t bad = st.size() == want.size() ? 0 : 1;
    for (size_t k = 0; k < std::min(st.size(), want.size()); k++) if (st[k] != want[k]) bad++;
    CHECK(ok && bad == 0, "narrow width %d bytes%s: %zu first cycles, want %zu; %zu differ", wb, w == 4 ? " (MPX)" : "",
          st.size(), want.size(), bad);
    CHECK(tb.n_ref_bus > r0, "narrow width %d: no refresh", wb);
    char what[40]; snprintf(what, sizeof what, "narrow width %d", wb);
    CHECK(tb.mem_diff(what) == 0, "%s: memory differs", what);
  }
  tb.md = 0x065; tb.reset(); tb.program(dc_cfg(false));
}

// PCMCIA (area 5, A56PCM, 8-bit): waits = WCR2.A5W {0..15} + PCR.A5PCW {0 15 30 50}, up to 65 [finding 5, verify
// `pcmcia`]
static void grp_pcmcia(Tb &tb) {
  g_grp = "pcmcia";
  for (int pcw = 0; pcw < 4; pcw++)
    for (int aw : {0, 3, 7}) {
      tb.md = 0x065; tb.reset();
      Cfg c = dc_cfg(false);
      c.bcr1 = 0xA3000009u; c.bcr2 = 0x0401; c.wcr3 = 0;
      c.wcr2 = (0x618066D8u & ~(7u << 23)) | ((uint32_t)aw << 23);
      c.pcr = (uint16_t)(pcw << 14);
      tb.program(c);
      int64_t t1 = -1, t2 = -1;
      tb.on_cycle = [&] {
        auto *v = tb.t.get();
        if (!tb.bce()) return;
        if (t1 < 0 && v->xb_start && v->xb_area == 5) t1 = bnow(tb);
        if (t2 < 0 && v->xb_beat && v->xb_rdy && tb.board.carea == 5) t2 = bnow(tb);
      };
      Xact *x = tb.mk(BIU_OC_UNC, false, 0x14000010u, BZ_B);
      tb.submit(x, (int64_t)tb.cyc + 1);
      tb.settle();
      tb.on_cycle = nullptr;
      const int want = W8[aw] + (pcw == 0 ? 0 : pcw == 1 ? 15 : pcw == 2 ? 30 : 50);
      CHECK(t1 >= 0 && t2 - t1 - 1 == want, "PCMCIA A5W %d A5PCW %d: %lld wait Bcyc between T1 and T2, want %d", aw, pcw,
            (long long)(t2 - t1 - 1), want);
    }
  tb.md = 0x065; tb.reset(); tb.program(dc_cfg(false));
}

// an MPX data cycle held by RDY# is the bus's last busy Bcyc only when RDY# comes: with A0IW 0 or 1 the next SDRAM
// access's first command is exactly A0IW Bcyc after the held cycle's end [finding 3, verify `rdyhold`]
static void grp_rdyhold(Tb &tb) {
  g_grp = "rdyhold";
  for (int we = 0; we < 2; we++)
    for (int iw : {0, 1}) {
      tb.md = 0x065; tb.reset();
      Cfg c = dc_cfg(false); c.wcr1 = 0x01110110u | (uint32_t)iw;
      tb.program(c);
      tb.shadow_sync();
      { Xact *o = tb.mk(BIU_OC_UNC, false, ram_a(0, 1, 0), BZ_L); tb.submit(o, (int64_t)tb.cyc + 1); tb.settle(); }
      tb.board.rd_waits[0] = tb.board.wr_waits[0] = 20;
      int64_t t_mpx = -1, t_sd = -1;
      tb.on_cycle = [&] {
        auto *v = tb.t.get();
        if (!tb.bce()) return;
        if (v->xb_beat && v->xb_rdy && tb.board.carea == 0) t_mpx = bnow(tb);
        if (t_sd < 0 && v->xb_sd_cmd_v) t_sd = bnow(tb);
      };
      Xact *a = tb.mk(we ? BIU_OC_WTB : BIU_OC_UNC, we != 0, 0x005F6800u, BZ_L);
      tb.submit(a, (int64_t)tb.cyc + 1);
      while (a->t_ready < 0) tb.step();
      Xact *b = tb.mk(PORT_DMA, false, ram_a(0, 1, 8), BZ_L);
      b->master = BM_DMA_DUAL;
      tb.submit(b, (int64_t)tb.cyc + 1);
      tb.settle();
      tb.on_cycle = nullptr;
      tb.board.rd_waits[0] = tb.board.wr_waits[0] = 0;
      CHECK(t_mpx > 0 && t_sd == t_mpx + 1 + iw && b->t_done >= 0,
            "MPX %s held by RDY# until Bcyc %lld (A0IW %d): the DMAC's SDRAM READ at %lld, want %lld", we ? "write" : "read",
            (long long)t_mpx, iw, (long long)t_sd, (long long)(t_mpx + 1 + iw));
    }
}

// a manual reset inside a locked pair: a CPU pair's lock goes (the core restarts; the TAS write never comes), a DMAC
// pair's stays until its write (the DMAC completes the unit, SOC.md 2.3) [finding 2, verify `manlock`]
static void grp_manlock(Tb &tb) {
  g_grp = "manlock";
  {
    tb.md = 0x065; tb.reset(); tb.program(dc_cfg(true)); tb.shadow_sync();
    Xact *r = tb.mk(BIU_OC_UNC, false, ram_a(3, 9, 1) + 1, BZ_B);
    r->lock = true;
    tb.submit(r, (int64_t)tb.cyc + 1);
    while (r->t_crit < 0) tb.step();
    tb.rst_man = true; tb.run(8); tb.rst_man = false;
    const uint64_t ref0 = tb.n_ref_bus;
    Xact *x = tb.mk(BIU_OC_UNC, false, ram_a(0, 9, 1), BZ_L), *y = tb.mk(BIU_IC_FILL, false, ram_a(1, 9, 0), BZ_LINE);
    tb.submit(x, (int64_t)tb.cyc + 4); tb.submit(y, (int64_t)tb.cyc + 4);
    tb.run(12000);
    CHECK(x->t_done >= 0 && y->t_done >= 0, "CPU locked read, manual reset before its write: later CPU accesses %s / %s",
          x->t_done >= 0 ? "done" : "never accepted", y->t_done >= 0 ? "done" : "never accepted");
    CHECK(tb.n_ref_bus > ref0, "CPU locked read, manual reset: no refresh in 12000 Icyc after it");
  }
  {
    tb.reset(); tb.program(dc_cfg(true)); tb.shadow_sync();
    Xact *r = tb.mk(PORT_DMA, false, ram_a(2, 5, 0), BZ_LINE);
    r->master = BM_DMA_DUAL; r->lock = true;
    tb.submit(r, (int64_t)tb.cyc + 1);
    while (r->t_crit < 0) tb.step();
    tb.rst_man = true; tb.run(8); tb.rst_man = false;
    Xact *c1 = tb.mk(BIU_OC_UNC, false, ram_a(0, 5, 3), BZ_L);
    tb.submit(c1, (int64_t)tb.cyc + 1);
    tb.run(40);
    Xact *w = tb.mk(PORT_DMA, true, ram_a(2, 6, 0), BZ_LINE);
    w->master = BM_DMA_DUAL;
    tb.submit(w, (int64_t)tb.cyc + 1);
    tb.settle();
    CHECK(w->t_done >= 0 && c1->t_done >= 0 && c1->t_ready > w->t_ready,
          "DMAC locked pair across a manual reset: its write accepted at %lld, a CPU read asked for meanwhile at %lld (want "
          "after the write)", (long long)w->t_ready, (long long)c1->t_ready);
  }
  // SOC.md 2.3 (revision 3, I112): a CPU locked read still presented across rst.man -- claimed before the reset, not yet
  // accepted -- is served unlocked: xb_lock 0, and later CPU reads go on with no pair write (v1_biu's `mr`).  It waits
  // behind an MPX read held 60 Bcyc by RDY# (one CPU critical beat outstanding), and the reset falls in that wait.
  // xb_lock of the first bus cycle at each address is recorded.
  std::map<uint32_t, int> lk_of;
  auto lock_tap = [&] {
    auto *v = tb.t.get();
    if (tb.bce() && v->xb_start && !lk_of.count(v->xb_addr)) lk_of[v->xb_addr] = v->xb_lock;
  };
  auto fresh = [&] {                                                   // each case on its own: no request left over
    for (int p = 0; p < 8; p++) tb.port[p] = Port();
    tb.rst_man = false; tb.reset(); tb.program(dc_cfg(true)); tb.shadow_sync();
    tb.on_cycle = lock_tap;
  };
  auto across = [&](Xact *r) {                                         // r presented, then 8 Icyc of rst.man
    tb.board.rd_waits[1] = 60;
    Xact *m = tb.mk(BIU_IC_UNC, false, 0x04000100u, BZ_L);
    tb.submit(m, (int64_t)tb.cyc + 1);
    while (m->t_ready < 0) tb.step();
    tb.submit(r, (int64_t)tb.cyc + 1);
    tb.run(12);
    tb.rst_man = true; tb.run(8); tb.rst_man = false;
    const int64_t t_end = (int64_t)tb.cyc;
    while (r->t_ready < 0 && tb.cyc < (uint64_t)t_end + 2000) tb.step();
    tb.board.rd_waits[1] = 0;
    return r->t_ready > t_end && m->t_crit > t_end;                    // accepted after the reset, behind m
  };
  {
    tb.md = 0x065; fresh();
    Xact *r = tb.mk(BIU_OC_UNC, false, ram_a(3, 9, 1) + 3, BZ_B);
    r->lock = true;
    const bool ok = across(r);
    Xact *x = tb.mk(BIU_OC_UNC, false, ram_a(0, 9, 1), BZ_L), *y = tb.mk(BIU_IC_FILL, false, ram_a(1, 9, 0), BZ_LINE);
    tb.submit(x, (int64_t)tb.cyc + 1); tb.submit(y, (int64_t)tb.cyc + 1);
    tb.run(3000);
    tb.on_cycle = nullptr;
    CHECK(ok && lk_of.count(r->addr) && lk_of[r->addr] == 0,
          "CPU locked read presented across a manual reset: accepted after it %d, xb_lock %d (want 0: served unlocked)", ok,
          lk_of.count(r->addr) ? lk_of[r->addr] : -1);
    CHECK(r->t_done >= 0 && x->t_done >= 0 && y->t_done >= 0,
          "CPU locked read presented across a manual reset, then two CPU reads with no pair write: %s / %s / %s",
          r->t_done >= 0 ? "done" : "stuck", x->t_done >= 0 ? "done" : "never done", y->t_done >= 0 ? "done" : "never done");
  }
  // the reset mark ends at the read's ready: the same port's next locked read, presented in the cycle after that ready
  // (bq.v never drops), is a new TAS.B -- it locks, and an SQ burst asked for in between waits for its write
  {
    fresh();
    Xact *r = tb.mk(BIU_OC_UNC, false, ram_a(3, 9, 5) + 2, BZ_B), *r2 = tb.mk(BIU_OC_UNC, false, ram_a(3, 9, 6) + 1, BZ_B);
    r->lock = true; r2->lock = true;
    Xact *sq = tb.mk(BIU_SQ, true, ram_a(2, 9, 8), BZ_LINE), *w2 = tb.mk(BIU_OC_WTB, true, r2->addr, BZ_B);
    Tb *T = &tb;
    r->on_ready = [T, r2](Xact &) { T->submit(r2, (int64_t)T->cyc); };  // queued: presented in the next cycle
    r2->on_ready = [T, sq](Xact &) { T->submit(sq, (int64_t)T->cyc + 2); };
    r2->on_crit = [T, w2](Xact &xx) { T->submit(w2, xx.t_crit + 6); };   // the TAS write, a little late
    const bool ok = across(r);
    tb.settle();
    tb.on_cycle = nullptr;
    CHECK(ok && r2->t_ready > r->t_ready && lk_of.count(r->addr) && lk_of.count(r2->addr) && lk_of[r->addr] == 0 &&
          lk_of[r2->addr] == 1,
          "locked read across the reset (xb_lock %d), the same port's next locked read right after its ready (xb_lock %d; "
          "want 0, 1)", lk_of.count(r->addr) ? lk_of[r->addr] : -1, lk_of.count(r2->addr) ? lk_of[r2->addr] : -1);
    CHECK(w2->t_ready > r2->t_ready && sq->t_ready > w2->t_ready,
          "the TAS.B after the reset: locked read %lld, its write %lld, an SQ burst asked in between %lld (want read < write "
          "< SQ)", (long long)r2->t_ready, (long long)w2->t_ready, (long long)sq->t_ready);
  }
  // a DMAC locked read presented during rst.man (the DMAC completes its unit, SOC.md 2.3): accepted after the reset with
  // its lock (xb_lock 1); a CPU read asked for meanwhile waits for the DMAC's write
  {
    fresh();
    tb.rst_man = true;
    Xact *r = tb.mk(PORT_DMA, false, ram_a(2, 7, 0), BZ_LINE);
    r->master = BM_DMA_DUAL; r->lock = true;
    tb.submit(r, (int64_t)tb.cyc + 1);
    tb.run(8); tb.rst_man = false;
    const int64_t t_end = (int64_t)tb.cyc;
    while (r->t_crit < 0 && tb.cyc < (uint64_t)t_end + 2000) tb.step();
    Xact *c1 = tb.mk(BIU_OC_UNC, false, ram_a(0, 7, 3), BZ_L);
    tb.submit(c1, (int64_t)tb.cyc + 1);
    tb.run(40);
    Xact *w = tb.mk(PORT_DMA, true, ram_a(2, 8, 0), BZ_LINE);
    w->master = BM_DMA_DUAL;
    tb.submit(w, (int64_t)tb.cyc + 1);
    tb.settle();
    tb.on_cycle = nullptr;
    CHECK(r->t_ready > t_end && lk_of.count(r->addr) && lk_of[r->addr] == 1 && c1->t_ready > w->t_ready && c1->t_done >= 0,
          "DMAC locked read presented across a manual reset: accepted at %lld (reset end %lld), xb_lock %d (want 1), a CPU "
          "read at %lld, the DMAC write at %lld (want after it)", (long long)r->t_ready, (long long)t_end,
          lk_of.count(r->addr) ? lk_of[r->addr] : -1, (long long)c1->t_ready, (long long)w->t_ready);
  }
  for (int p = 0; p < 8; p++) tb.port[p] = Port();
  tb.rst_man = false; tb.reset(); tb.program(dc_cfg(false));
}

// BREQ# asserted again inside the 2-Bcyc regain window: the bus is taken back and released again, never left with no
// owner; a refresh while released negates BACK# and runs once the bus is back [finding 6, verify `breq`]
static void grp_breq(Tb &tb) {
  g_grp = "breq";
  {
    tb.md = 0x065; tb.reset();
    Cfg c = dc_cfg(false); c.bcr1 |= 1u << 19;                         // BREQEN
    tb.program(c); tb.shadow_sync();
    tb.breq_n = false;
    tb.run(40);
    CHECK(!tb.t->back_n, "BREQ: BACK# not asserted");
    Xact *y = tb.mk(BIU_OC_UNC, false, ram_a(1, 2, 8), BZ_L);
    tb.submit(y, (int64_t)tb.cyc + 1);
    tb.run(100);
    CHECK(y->t_ready < 0, "BREQ: an access was accepted while the bus was released");
    while (!tb.bce()) tb.step();
    tb.breq_n = true; tb.run(2); tb.breq_n = false;                    // BREQ# high for one Bcyc only
    int nhigh = 0, nlow = 0; bool prev = tb.t->back_n;
    tb.on_cycle = [&] {
      if ((bool)tb.t->back_n != prev) { if (tb.t->back_n) nhigh++; else nlow++; prev = tb.t->back_n; }
    };
    tb.run(400);
    tb.on_cycle = nullptr;
    CHECK(nhigh >= 1 && nlow >= 1 && !tb.t->back_n && y->t_ready < 0,
          "BREQ# low again one Bcyc after its negation: BACK# high %d time(s), low again %d time(s), now %d (want the bus "
          "re-granted)", nhigh, nlow, (int)tb.t->back_n);
    tb.breq_n = true;
    tb.run_until_idle(4000);
    CHECK(y->t_done >= 0 && tb.t->back_n, "BREQ: the bus did not come back after BREQ# high");
  }
  {
    tb.reset();
    Cfg c = dc_cfg(false); c.bcr1 |= 1u << 19; c.rtcor = 0x10; c.rtcsr = 0x0008;   // a compare match every 64 Bcyc
    tb.program(c);
    tb.breq_n = false;
    tb.run(40);
    CHECK(!tb.t->back_n, "BREQ (refresh on): BACK# not asserted");
    const uint64_t r0 = tb.n_ref_bus;
    int64_t t_neg = -1;
    tb.on_cycle = [&] { if (t_neg < 0 && tb.t->back_n) t_neg = (int64_t)tb.cyc; };
    tb.run(400);
    tb.on_cycle = nullptr;
    CHECK(t_neg > 0 && tb.n_ref_bus == r0, "refresh while released: BACK# negated at %lld, %llu REF while released",
          (long long)t_neg, (unsigned long long)(tb.n_ref_bus - r0));
    tb.breq_n = true;
    tb.run(80);
    CHECK(tb.n_ref_bus > r0, "refresh while released: no refresh after the bus came back");
  }
  tb.breq_n = true;
}

// WCR1 idles, exact: every MPX pair (an access, then a DMAC access raised at its accept) on random WCR1 values, and
// across a refresh that runs between the two [findings 7, 8, 9, verify `idle` / `idle-rf`]
static void grp_idlex(Tb &tb) {
  g_grp = "idlex";
  Rng r(11);
  int n_floor = 0, n_ww = 0;
  for (int it = 0; it < 400; it++) {
    tb.md = 0x065; tb.reset();
    Cfg c = dc_cfg(false);
    uint32_t wcr1 = 0;
    for (int a = 0; a < 7; a++) wcr1 |= r.u(8) << (4 * a);
    wcr1 &= ~(7u << 12);                                               // A3IW 0 (RAS down)
    c.wcr1 = wcr1;
    tb.program(c);
    static const int areas[6] = {0, 1, 2, 4, 5, 6};
    const int pa = areas[r.u(6)], na = r.u(3) == 0 ? pa : areas[r.u(6)];
    const bool pw = r.u(2) != 0, nw = r.u(2) != 0;
    struct C { int64_t tm1 = -1, tlast = -1; };
    std::vector<C> cs;
    tb.on_cycle = [&] {
      auto *v = tb.t.get();
      if (!tb.bce()) return;
      if (v->xb_start) { C cc; cc.tm1 = bnow(tb); cs.push_back(cc); }
      if (v->xb_beat && v->xb_rdy && v->xb_last && !cs.empty()) cs.back().tlast = bnow(tb);
    };
    Xact *a = tb.mk(pw ? BIU_OC_WTB : BIU_OC_UNC, pw, (uint32_t)pa << 26 | 0x100u, BZ_L);
    tb.submit(a, (int64_t)tb.cyc + 1);
    while (a->t_ready < 0) tb.step();
    Xact *b = tb.mk(PORT_DMA, nw, (uint32_t)na << 26 | 0x200u, BZ_L);
    b->master = BM_DMA_DUAL;
    tb.submit(b, (int64_t)tb.cyc);
    tb.settle();
    tb.on_cycle = nullptr;
    if (cs.size() != 2) { failf("WCR1 %08x: %zu bus cycles, want 2", wcr1, cs.size()); continue; }
    const int64_t gap = cs[1].tm1 - cs[0].tlast - 1;
    const int M = W8[(wcr1 >> (4 * pa)) & 7];
    // hm 13.2.3 for MPX: after a read, and after a write before another area's MPX address, "M (1)"; a write, then
    // the same area: none, except two MPX writes, which get M (1) too [P, v1_bsc_pkg idle_cyc]
    int want = (!pw || pa != na || nw) ? std::max(M, 1) : 0;
    if (pw && pa == na && nw && M > 1) n_ww++;
    // the front end: first bus cycles at least 3 Bcyc apart [P, v1_bsc.sv]
    const int64_t plen = cs[0].tlast - cs[0].tm1 + 1;
    if (want < 3 - plen) { want = (int)(3 - plen); n_floor++; }
    CHECK(gap == want, "WCR1 %08x: area %d %s -> area %d %s: %lld idle Bcyc, want %d", wcr1, pa, pw ? "wr" : "rd", na,
          nw ? "wr" : "rd", (long long)gap, want);
  }
  printf("  idlex: 400 pairs, %d set by the front-end floor, %d MPX write -> write with AnIW > 1\n", n_floor, n_ww);
  // across a refresh: an MPX read held 40 Bcyc by RDY# (a compare match every 16 Bcyc is pending at its end), REF right
  // after its data; the next MPX address waits for the refresh's last command Bcyc and for A0IW counted from the data
  for (int iw : {3, 6, 9, 12, 15}) {
    tb.md = 0x065; tb.reset();
    Cfg c = dc_cfg(false);
    const int code = iw == 3 ? 3 : iw == 6 ? 4 : iw == 9 ? 5 : iw == 12 ? 6 : 7;
    c.wcr1 = 0x01110110u | (uint32_t)code;
    c.rtcor = 4; c.rtcsr = 0x0008;
    tb.program(c);
    tb.board.rd_waits[0] = 40;
    int64_t t_last1 = -1, t_tm1_2 = -1, t_ref = -1; int nst = 0;
    tb.on_cycle = [&] {
      auto *v = tb.t.get();
      if (!tb.bce()) return;
      if (v->xb_start && v->xb_area == 0) { nst++; if (nst == 2) t_tm1_2 = bnow(tb); }
      if (nst == 1 && v->xb_beat && v->xb_rdy && tb.board.carea == 0) { t_last1 = bnow(tb); tb.board.rd_waits[0] = 0; }
      if (nst == 1 && t_last1 >= 0 && v->xb_sd_cmd_v && v->xb_sd_cmd == SDC_REF) t_ref = bnow(tb);
    };
    Xact *a = tb.mk(BIU_OC_UNC, false, 0x005F6800u, BZ_L);
    tb.submit(a, (int64_t)tb.cyc + 1);
    while (a->t_ready < 0) tb.step();
    Xact *b = tb.mk(PORT_DMA, false, 0x005F6804u, BZ_L);
    b->master = BM_DMA_DUAL;
    tb.submit(b, (int64_t)tb.cyc + 1);
    tb.settle();
    tb.on_cycle = nullptr;
    tb.board.rd_waits[0] = 0;
    const int64_t gap = t_tm1_2 - t_last1 - 1, rf_end = t_ref + tb.cfg.trfc() - t_last1;
    CHECK(t_ref >= 0 && gap == std::max<int64_t>(iw, rf_end),
          "A0IW %d: MPX read, REF at +%lld between, next MPX address %lld Bcyc after the read's data (want max(A0IW, the "
          "refresh's end %lld))", iw, (long long)(t_ref - t_last1), (long long)gap, (long long)rf_end);
  }
  tb.md = 0x065; tb.reset(); tb.program(dc_cfg(false));
}

// SDMR2 / SDMR3: the 64 KB windows (the rest of each 256 KB block is a hole), and no mode command for an area that is
// not SDRAM -- the side band has no chip select [findings 10, 11, verify `regs` / `sdmr2`]
static void grp_sdmr(Tb &tb) {
  g_grp = "sdmr";
  tb.md = 0x065; tb.reset();
  tb.model_on = true;                                                  // hit bits compared with tb/sh4_bsc.h
  tb.program(dc_cfg(false));
  tb.shadow_sync();
  uint64_t m0 = tb.n_mrs_bus;
  tb.p4(true, 0xF940190, 0, 0, true); tb.run(20);
  CHECK(tb.n_mrs_bus == m0 + 1 && tb.last_mrs == 0x32, "SDMR3 0xFF940190: %llu MRS, value %03x", (unsigned long long)(tb.n_mrs_bus - m0),
        tb.last_mrs);
  for (uint32_t off : {0xF950190u, 0xF910190u, 0xF970000u, 0xF930FFCu}) {
    const uint64_t m = tb.n_mrs_bus;
    tb.p4(true, off, 0, 0, true);
    const bool hw = tb.pr_hit_last;
    tb.run(20);
    tb.p4(false, off, 0, 0, true);
    const bool hr = tb.pr_hit_last;
    CHECK(tb.n_mrs_bus == m && !hw && !hr, "SDMR decode 0xF%07x: %llu MRS, hit %d / %d (want a hole)", off,
          (unsigned long long)(tb.n_mrs_bus - m), hw, hr);
  }
  // SDMR2 with area 2 = MPX (the DC), MRSET 0: nothing on the side band, area 3's open row stays a row hit
  { Xact *o = tb.mk(BIU_OC_UNC, false, ram_a(0, 1, 0), BZ_L); tb.submit(o, (int64_t)tb.cyc + 1); tb.settle(); }
  Cfg c = dc_cfg(false); c.mcr = 0x800A0E24u;
  tb.program(c);
  uint64_t p0 = tb.n_cmd[SDC_PALL]; m0 = tb.n_mrs_bus;
  tb.p4(true, 0xF900000, 0, 0, true); tb.run(20);
  CHECK(tb.n_cmd[SDC_PALL] == p0 && tb.n_mrs_bus == m0, "SDMR2 with area 2 = MPX: %llu PALL, %llu MRS on the side band",
        (unsigned long long)(tb.n_cmd[SDC_PALL] - p0), (unsigned long long)(tb.n_mrs_bus - m0));
  {
    Xact *x = tb.mk(BIU_OC_UNC, false, ram_a(0, 1, 8), BZ_L);
    tb.submit(x, (int64_t)tb.cyc + 1); tb.settle();
    CHECK(x->t_crit - x->t_ready == 12, "after SDMR2 (area 2 MPX) the area-3 row-hit read: rd_v at +%lld, want 12",
          (long long)(x->t_crit - x->t_ready));
  }
  // SDMR3 with MRSET 0: a PALL, and the next read opens its bank again (+3 Bcyc)
  p0 = tb.n_cmd[SDC_PALL];
  tb.p4(true, 0xF940000, 0, 0, true); tb.run(20);
  CHECK(tb.n_cmd[SDC_PALL] == p0 + 1, "SDMR3 with MRSET 0: %llu PALL", (unsigned long long)(tb.n_cmd[SDC_PALL] - p0));
  {
    Xact *x = tb.mk(BIU_OC_UNC, false, ram_a(0, 1, 16), BZ_L);
    tb.submit(x, (int64_t)tb.cyc + 1); tb.settle();
    CHECK(x->t_crit - x->t_ready == 18, "after SDMR3's PALL the read: rd_v at +%lld, want 18 (closed bank)",
          (long long)(x->t_crit - x->t_ready));
  }
  // DRAMTP 011 (areas 2 and 3 SDRAM): SDMR2 issues its command (after a PALL through SDMR3: an MRS needs every bank
  // idle, hm 13.3.5 power-on sequence -- software's job, the BSC does not precharge for it)
  tb.p4(true, 0xF940000, 0, 0, true); tb.run(20);
  c = dc_cfg(false); c.bcr1 = 0xA302000Cu;
  tb.program(c);
  m0 = tb.n_mrs_bus;
  tb.p4(true, 0xF900190, 0, 0, true); tb.run(20);
  CHECK(tb.n_mrs_bus == m0 + 1, "SDMR2 with area 2 SDRAM: %llu MRS", (unsigned long long)(tb.n_mrs_bus - m0));
  tb.model_on = false;
  tb.reset(); tb.program(dc_cfg(false));
}

// ================================================================================================================
// SOC.md 3.3.2 (revision 3, I087): only the CPU's BZ_Q is split into two longword cycles.  A DMAC 8-byte unit is one
// access: one 64-bit cycle on the DC (so a dual unit costs what every other unit size costs [HW dmac 3.13 obs. 1]), the
// width split of one access on a narrower bus (two data cycles of one SDRAM / MPX cycle on a 32-bit bus [P]).  Each
// case: the bus cycles (starts, size codes, beats and lanes, READ / WRIT commands), one rd_v / wd_req / done at the
// requester, the data (the accept-order shadow), and the memory image at the end.
// (M6, WI-SOC) the CPU's turn (v1_bsc_arb cpu_turn; SOC.md 3.3.2): a DMAC single-address 32-byte read stream whose
// requests come 2 Bcyc after each accept (Holly's paced CH2-DMA requests) and a CPU SB-register read (area 0: WCR1's
// A3 -> A0 idle) raised after the 4th unit's accept.  The DMAC released between its requests (v1_dmac.bus_rel): the
// CPU's read is accepted before the 6th unit (one CPU access at a break: hm 14.3.4, figure 14.11's note; [HW dmac1
// S13]: the console's CPU polled ISTNRM through a 64 KB CH2-DMA).  The DMAC in a burst session (no release): the
// stream keeps the bus to its end, the read after the last unit [HW dmac2 T4].
static void grp_turn(Tb &tb) {
  g_grp = "turn";
  tb.md = 0x065; tb.reset(); tb.program(dc_cfg(false)); tb.shadow_sync();
  { Xact *o = tb.mk(BIU_OC_UNC, false, ram_a(0, 4, 0), BZ_L); tb.submit(o, (int64_t)tb.cyc + 1); tb.settle(); }
  for (int rel = 0; rel < 2; rel++) {
    tb.dma_rel = rel != 0;
    std::vector<int64_t> rdy;
    const int NU = 12;
    Xact *cpu = nullptr;
    std::function<void(int, int64_t)> go = [&](int k, int64_t t) {
      if (k >= NU) return;
      Xact *x = tb.mk(PORT_DMA, false, RAM + 0x1000 + 32u * k, BZ_LINE);
      x->master = BM_DDT; x->ch = 2;
      x->on_ready = [&, k](Xact &xx) {
        rdy.push_back(xx.t_ready);
        if (k == 3) { cpu = tb.mk(BIU_OC_UNC, false, 0x005F689Cu, BZ_L); tb.submit(cpu, xx.t_ready + 1); }
        go(k + 1, xx.t_ready + 4);
      };
      tb.submit(x, t);
    };
    go(0, (int64_t)tb.cyc + 1);
    tb.run_until_idle(); tb.run(40);
    for (int p = 0; p < 8; p++) tb.port[p] = Port();
    const bool all = rdy.size() == (size_t)NU && cpu && cpu->t_ready >= 0;
    CHECK(all, "turn (%s): %zu of %d DMAC units accepted, CPU read %s", rel ? "released" : "burst", rdy.size(), NU,
          cpu && cpu->t_ready >= 0 ? "accepted" : "never accepted");
    if (!all) continue;
    int before = 0; for (int64_t r : rdy) before += r < cpu->t_ready;
    printf("  turn (%s): the CPU's SB read accepted at %lld, after %d of the %d DMAC units\n", rel ? "released" : "burst",
           (long long)cpu->t_ready, before, NU);
    if (rel) CHECK(before <= 5, "turn (released): the CPU's read waited for %d DMAC units (want it at the next break, <= 5)", before);
    else CHECK(before == NU, "turn (burst): the CPU's read took the bus inside the burst session (after %d of %d units)", before, NU);
  }
  tb.dma_rel = false;
  // (M6-fix, M6-verify F1) a burst session with gaps between the DMAC's own requests (v1_dmac.bus_ses: the dual unit
  // that resumes a burst after a higher-priority single unit holds its read until that unit's beats are done): 12
  // single-address line reads 20 Bcyc apart, the same CPU SB read raised after the 4th.  Without the session the CPU
  // takes a gap (the control: the gap is long enough for its WCR1 idle and its accept); with it (dma_ses = 1 until the
  // last unit's accept, when v1_dmac_xfer releases) no CPU access is accepted inside the session (hm 14.3.4: "the bus
  // is not released to the CPU until channel 1 transfer ends"; ddtmix G / H: 21 / 1,002 CPU reads inside the burst
  // before the fix).  Planted bug V1_BSC_BUG_SES / mutant M6-fix
  for (int ses = 0; ses < 2; ses++) {
    std::vector<int64_t> rdy;
    const int NU = 12;
    Xact *cpu = nullptr;
    tb.dma_ses = ses != 0;
    std::function<void(int, int64_t)> go = [&](int k, int64_t t) {
      if (k >= NU) return;
      Xact *x = tb.mk(PORT_DMA, false, RAM + 0x1000 + 32u * k, BZ_LINE);
      x->master = BM_DDT; x->ch = 2;
      x->on_ready = [&, k](Xact &xx) {
        rdy.push_back(xx.t_ready);
        if (k == 3) { cpu = tb.mk(BIU_OC_UNC, false, 0x005F689Cu, BZ_L); tb.submit(cpu, xx.t_ready + 1); }
        if (k == NU - 1) tb.dma_ses = false;                    // the session's last unit: the engine releases
        go(k + 1, xx.t_ready + 40);
      };
      tb.submit(x, t);
    };
    go(0, (int64_t)tb.cyc + 1);
    tb.run_until_idle(); tb.run(40);
    for (int p = 0; p < 8; p++) tb.port[p] = Port();
    tb.dma_ses = false;
    const bool all = rdy.size() == (size_t)NU && cpu && cpu->t_ready >= 0;
    CHECK(all, "turn (gaps, %s): %zu of %d DMAC units accepted, CPU read %s", ses ? "session" : "no session", rdy.size(),
          NU, cpu && cpu->t_ready >= 0 ? "accepted" : "never accepted");
    if (!all) continue;
    int before = 0; for (int64_t r : rdy) before += r < cpu->t_ready;
    printf("  turn (gaps, %s): the CPU's SB read accepted at %lld, after %d of the %d DMAC units\n",
           ses ? "session" : "no session", (long long)cpu->t_ready, before, NU);
    if (ses) CHECK(before == NU, "turn (gaps, session): the CPU's read took a gap inside the burst session (after %d of %d "
                   "units)", before, NU);
    else CHECK(before <= 5, "turn (gaps, no session): the CPU's read waited for %d DMAC units (the control: want the "
               "next gap, <= 5)", before);
  }
  // (M6-fix) the CPU's own locked pair across a session's start: a TAS.B read accepted, then the DMAC's burst session
  // begins (dma_ses = 1, its first unit asked for) before the pair's write -- the write must still be accepted (inside
  // the lock, before the DMAC's unit) or the two wait for each other forever
  {
    Xact *rd = tb.mk(BIU_OC_UNC, false, RAM + 0x700, BZ_B);
    rd->lock = true;
    Xact *wr = tb.mk(BIU_OC_WTB, true, RAM + 0x700, BZ_B);
    Xact *du = tb.mk(PORT_DMA, false, RAM + 0x1400, BZ_LINE);
    du->master = BM_DDT; du->ch = 2;
    du->on_ready = [&](Xact &) { tb.dma_ses = false; };
    rd->on_ready = [&](Xact &x) { tb.dma_ses = true; tb.submit(du, x.t_ready + 1); };
    rd->on_crit = [&](Xact &x) { tb.submit(wr, x.t_crit + 6); };          // the TAS write, a little late
    tb.submit(rd, (int64_t)tb.cyc + 2);
    tb.run_until_idle(); tb.run(40);
    for (int p = 0; p < 8; p++) tb.port[p] = Port();
    tb.dma_ses = false;
    CHECK(rd->t_ready >= 0 && wr->t_ready > rd->t_ready && du->t_ready > wr->t_ready,
          "turn (session inside a TAS.B pair): locked read %lld, its write %lld, the DMAC's unit %lld (want read < write "
          "< unit)", (long long)rd->t_ready, (long long)wr->t_ready, (long long)du->t_ready);
  }
  CHECK(tb.mem_diff("turn") == 0, "turn: memory differs");
}

static void grp_dmaq(Tb &tb) {
  g_grp = "dmaq";
  struct Obs { size_t ncyc = 0; int size = -1, nbeat = 0; std::vector<uint8_t> be; uint64_t nrd = 0, nwr = 0; };
  auto one = [&](int port, bool we, uint32_t a, int size) {
    const size_t n0 = tb.board.log.size();
    const uint64_t r0 = tb.n_cmd[SDC_READ] + tb.n_cmd[SDC_READA], w0 = tb.n_cmd[SDC_WRIT] + tb.n_cmd[SDC_WRITA];
    Xact *x = tb.mk(port, we, a, size);
    if (port == PORT_DMA) x->master = BM_DMA_DUAL;
    tb.submit(x, (int64_t)tb.cyc + 1);
    tb.settle();
    Obs o;
    o.ncyc = tb.board.log.size() - n0;
    for (size_t i = n0; i < tb.board.log.size(); i++) {
      if (i == n0) o.size = tb.board.log[i].size;
      o.nbeat += tb.board.log[i].nbeat;
      for (uint8_t b : tb.board.log[i].be) o.be.push_back(b);
    }
    o.nrd = tb.n_cmd[SDC_READ] + tb.n_cmd[SDC_READA] - r0; o.nwr = tb.n_cmd[SDC_WRIT] + tb.n_cmd[SDC_WRITA] - w0;
    CHECK(x->t_done >= 0 && (we ? x->nw : x->nb) == 1, "%s %s Q %08x: done %d after %d %s (want one)",
          port == PORT_DMA ? "DMAC" : "CPU", we ? "write" : "read", a, x->t_done >= 0, we ? x->nw : x->nb, we ? "wd_req" : "rd_v");
    return o;
  };
  auto bes = [](const Obs &o) { std::string s; char b[8]; for (uint8_t e : o.be) { snprintf(b, sizeof b, " %02x", e); s += b; } return s; };
  // (a) the DC: 64-bit SDRAM (area 3) and 64-bit MPX (area 1)
  tb.md = 0x065; tb.reset(); tb.program(dc_cfg(false)); tb.shadow_sync();
  { Xact *o = tb.mk(BIU_OC_UNC, false, ram_a(0, 4, 0), BZ_L); tb.submit(o, (int64_t)tb.cyc + 1); tb.settle(); }
  for (int we = 1; we >= 0; we--) {
    Obs d = one(PORT_DMA, we, ram_a(0, 4, 8), BZ_Q);
    CHECK(d.ncyc == 1 && d.size == 3 && d.nbeat == 1 && d.be.size() == 1 && d.be[0] == 0xFF && (we ? d.nwr : d.nrd) == 1,
          "DC SDRAM, DMAC Q %s: %zu bus cycles, size code %d, %d beats (be%s), %llu %s (want 1, 3, 1 (ff), 1)",
          we ? "write" : "read", d.ncyc, d.size, d.nbeat, bes(d).c_str(), (unsigned long long)(we ? d.nwr : d.nrd), we ? "WRIT" : "READ");
    Obs c = one(we ? BIU_OC_WTB : BIU_OC_UNC, we, ram_a(0, 4, 12), BZ_Q);
    CHECK(c.ncyc == 2 && c.size == 2 && (we ? c.nwr : c.nrd) == 2,
          "DC SDRAM, CPU Q %s: %zu bus cycles, size code %d, %llu %s (want 2 longword cycles)", we ? "write" : "read",
          c.ncyc, c.size, (unsigned long long)(we ? c.nwr : c.nrd), we ? "WRIT" : "READ");
    Obs m = one(PORT_DMA, we, 0x04000140u, BZ_Q);
    CHECK(m.ncyc == 1 && m.size == 3 && m.nbeat == 1 && m.be.size() == 1 && m.be[0] == 0xFF,
          "DC MPX, DMAC Q %s: %zu bus cycles, size code %d, %d beats (be%s) (want 1, 3, 1 (ff))", we ? "write" : "read",
          m.ncyc, m.size, m.nbeat, bes(m).c_str());
  }
  // (b) the cost of a DMAC dual unit on the DC does not depend on its size: a locked read, its write in the Icyc after
  //     the read's done, the next unit in the Icyc after the write's done (burst mode); per unit = the distance between
  //     consecutive reads' accepts (the BSC's share; the DMAC's own turnaround is v1_dmac's) [HW dmac 3.13: 46.26 /
  //     46.26 / 46.27 / 46.30 Icyc for 1 / 2 / 4 / 8 bytes with the DMAC]
  {
    int64_t per[5] = {0, 0, 0, 0, 0};
    for (int size = BZ_B; size <= BZ_LINE; size++) {
      std::vector<int64_t> rdy;
      const int NU = 10;
      std::function<void(int)> go = [&](int k) {
        if (k >= NU) return;
        Xact *r = tb.mk(PORT_DMA, false, ram_a(1, 10, 4 * k), size);
        r->master = BM_DMA_DUAL; r->lock = true;
        r->on_done = [&, k](Xact &rr) {
          rdy.push_back(rr.t_ready);
          Xact *w = tb.mk(PORT_DMA, true, ram_a(1, 12, 4 * k), size);
          w->master = BM_DMA_DUAL;
          w->on_done = [&, k](Xact &) { go(k + 1); };
          tb.submit(w, rr.t_done + 1);
        };
        tb.submit(r, (int64_t)tb.cyc + 1);
      };
      go(0);
      tb.run_until_idle(); tb.run(40);
      for (int p = 0; p < 8; p++) tb.port[p] = Port();                // a stuck unit's callbacks die with this loop
      per[size] = rdy.size() == (size_t)NU ? rdy[NU - 1] - rdy[NU - 2] : -1;
      bool steady = rdy.size() == (size_t)NU;
      for (int k = 3; steady && k + 1 < NU; k++) if (rdy[k + 1] - rdy[k] != per[size]) steady = false;
      CHECK(steady, "DMAC dual units of size %d: not a steady stream (%zu units)", size, rdy.size());
    }
    printf("  dmaq: DC SDRAM dual unit (row miss both legs) per B / W / L / Q / 32 B: %lld / %lld / %lld / %lld / %lld Icyc\n",
           (long long)per[0], (long long)per[1], (long long)per[2], (long long)per[3], (long long)per[4]);
    CHECK(per[BZ_Q] > 0 && per[BZ_Q] == per[BZ_L] && per[BZ_B] == per[BZ_L] && per[BZ_W] == per[BZ_L],
          "DMAC dual unit cost by size: B %lld W %lld L %lld Q %lld Icyc (want one cost for 1-8 bytes)", (long long)per[0],
          (long long)per[1], (long long)per[2], (long long)per[3]);
  }
  CHECK(tb.mem_diff("dmaq DC") == 0, "dmaq, the DC: memory differs");
  // (c) 32-bit SDRAM (MCR.SZ 11): a DMAC Q is one READ / WRIT with the burst's first two beats (low, high longword) in
  //     consecutive Bcyc; a CPU Q stays two longword cycles
  {
    tb.reset();
    Cfg c = dc_cfg(false); c.mcr |= 3u << 7;
    tb.program(c); tb.shadow_sync();
    { Xact *o = tb.mk(BIU_OC_UNC, false, ram_a(0, 4, 0), BZ_L); tb.submit(o, (int64_t)tb.cyc + 1); tb.settle(); }
    for (int we = 1; we >= 0; we--) {
      const size_t n0 = tb.board.log.size();
      Obs d = one(PORT_DMA, we, ram_a(0, 4, 8), BZ_Q);
      const Board::Cyc *cy = tb.board.log.size() == n0 + 1 ? &tb.board.log.back() : nullptr;
      CHECK(d.ncyc == 1 && d.size == 3 && d.nbeat == 2 && d.be.size() == 2 && d.be[0] == 0x0F && d.be[1] == 0xF0 && cy &&
            cy->tb[1] - cy->tb[0] == 2 && cy->ba[0] == cy->ba[1] && (we ? d.nwr : d.nrd) == 1,
            "32-bit SDRAM, DMAC Q %s: %zu bus cycles, size code %d, %d beats (be%s), %llu %s (want 1, 3, 2 in consecutive "
            "Bcyc (0f f0), 1)", we ? "write" : "read", d.ncyc, d.size, d.nbeat, bes(d).c_str(),
            (unsigned long long)(we ? d.nwr : d.nrd), we ? "WRIT" : "READ");
      Obs q = one(we ? BIU_OC_WTB : BIU_OC_UNC, we, ram_a(0, 4, 12), BZ_Q);
      CHECK(q.ncyc == 2 && q.size == 2 && (we ? q.nwr : q.nrd) == 2,
            "32-bit SDRAM, CPU Q %s: %zu bus cycles, size code %d, %llu %s (want 2 longword cycles)", we ? "write" : "read",
            q.ncyc, q.size, (unsigned long long)(we ? q.nwr : q.nrd), we ? "WRIT" : "READ");
    }
    CHECK(tb.mem_diff("dmaq 32-bit SDRAM") == 0, "dmaq, 32-bit SDRAM: memory differs");
  }
  // (d) 32-bit MPX (area 1, A1SZ 11): a DMAC Q is one Tm1 with size code Q and two data cycles (0f, f0)
  {
    tb.reset();
    Cfg c = dc_cfg(false); c.bcr2 = 0x000D;
    tb.program(c); tb.shadow_sync();
    for (int we = 1; we >= 0; we--) {
      Obs d = one(PORT_DMA, we, 0x04000180u, BZ_Q);
      CHECK(d.ncyc == 1 && d.size == 3 && d.nbeat == 2 && d.be.size() == 2 && d.be[0] == 0x0F && d.be[1] == 0xF0,
            "32-bit MPX, DMAC Q %s: %zu bus cycles, size code %d, %d beats (be%s) (want 1, 3, 2 (0f f0))", we ? "write" : "read",
            d.ncyc, d.size, d.nbeat, bes(d).c_str());
      Obs q = one(we ? BIU_OC_WTB : BIU_OC_UNC, we, 0x040001C0u, BZ_Q);
      CHECK(q.ncyc == 2 && q.size == 2, "32-bit MPX, CPU Q %s: %zu bus cycles, size code %d (want 2 longword cycles)",
            we ? "write" : "read", q.ncyc, q.size);
    }
    CHECK(tb.mem_diff("dmaq 32-bit MPX") == 0, "dmaq, 32-bit MPX: memory differs");
  }
  tb.md = 0x065; tb.reset(); tb.program(dc_cfg(false));
}

// ================================================================================================================
// SOC.md 3.3 (revision 3, I116): each requester presents its own requests in program order -- the OC presents a WTB
// store, a WBB load or an uncached read only when no older line request of its own waits for the BIU's accept, a line
// fill or an uncached read only with the WTB empty, and an uncached read (a load, a P2 PREF, TAS.B's read) blocks the
// core until its data (v1_ocache) -- so the WTB-first and WBB-line rules never let a write overtake an older read of
// that requester.  Random OC programs on six shared lines (SDRAM rows in every bank, and MPX): blocking load misses and
// non-blocking fills (PREF, copy-back store misses), with or without a dirty victim (the fill and its WBB load in one
// claim), uncached loads, WTB stores, WBB write-backs (OCBWB / OCBP), TAS.B pairs; the IC, the SQ and the DMAC run on
// lines of their own for contention, with refresh every 32 Bcyc and MPX RDY# waits.  Checked: at every accept of an OC
// write, no older OC read still waits for its accept (the order), and every OC read returns the program-order image
// of its line (the data).  Coverage: OC writes accepted while a younger OC read waited (the rules at work).  The
// negative control drops the requester rule (a store / write-back / uncached read presented while an older fill
// waits): the checker must then see writes overtake older reads.
struct OrdOc {
  Tb &tb;
  Rng rng;
  bool po;                                     // the SOC.md 3.3 presentation rule
  std::vector<uint32_t> lines;
  std::map<uint32_t, uint8_t> img;             // the program-order image of the OC's lines
  uint64_t seq = 0;
  int64_t t_next = 0;                          // the core issues its next op at or after this cycle; -1 blocked
  int pend = -1;                               // the op the core holds (it waits for its resources)
  Xact *fill = nullptr;                        // the OC's last line request
  bool wtb = false, wbb = false;
  struct R { Xact *x; uint64_t s; uint8_t exp[32]; };
  std::vector<R *> rpend;                      // OC reads not yet accepted
  std::vector<std::unique_ptr<R>> rs;
  long ops = 0, n_rd = 0, n_wr = 0, viol = 0, bad = 0, younger = 0;
  OrdOc(Tb &t, uint64_t seed, bool p) : tb(t), rng(seed), po(p) {}
  static int area(uint32_t a) { return (int)(a >> 26) & 7; }
  uint8_t &at(uint32_t a) {
    auto it = img.find(a);
    if (it == img.end()) it = img.emplace(a, *tb.board.byte(area(a), a)).first;
    return it->second;
  }
  void rd(Xact *x) {
    rs.emplace_back(new R);
    R *r = rs.back().get();
    r->x = x; r->s = seq++;
    for (int k = 0; k < 32; k++) r->exp[k] = at((x->addr & ~31u) + (uint32_t)k);
    rpend.push_back(r);
    n_rd++;
    x->on_ready = [this, r](Xact &) { rpend.erase(std::find(rpend.begin(), rpend.end(), r)); };
    x->on_done = [this, r](Xact &xx) {
      for (int k = 0; k < xx.nb && k < 4; k++) {
        const int q = xx.size == BZ_LINE ? xx.q_rd[k] : (int)((xx.addr >> 3) & 3);
        const uint8_t m = xx.size == BZ_LINE ? 0xFF : lanes_of(xx.size, xx.addr);
        uint64_t want = 0;
        for (int b = 0; b < 8; b++) want |= (uint64_t)r->exp[8 * q + b] << (8 * b);
        if ((xx.d_rd[k] & lmask(m)) != (want & lmask(m))) {
          if (po && bad < 5) failf("OC read %08x size %d (program order %llu) beat %d: %016llx, the program-order image %016llx",
                                   xx.addr, xx.size, (unsigned long long)r->s, k, (unsigned long long)(xx.d_rd[k] & lmask(m)),
                                   (unsigned long long)(want & lmask(m)));
          bad++;
        }
      }
    };
  }
  void wr(Xact *x) {
    const uint64_t s = seq++;
    for (int q = 0; q < 4; q++) {
      if (x->size != BZ_LINE && q != (int)((x->addr >> 3) & 3)) continue;
      const uint8_t be = x->size == BZ_LINE ? x->wbe[q] : (x->wbe[q] & lanes_of(x->size, x->addr));
      for (int b = 0; b < 8; b++) if ((be >> b) & 1) at((x->addr & ~31u) + 8u * (uint32_t)q + (uint32_t)b) = (uint8_t)(x->wd[q] >> (8 * b));
    }
    n_wr++;
    x->on_ready = [this, s](Xact &xx) {
      bool yng = false;
      for (R *r : rpend) {
        if (r->s < s) {
          if (po && viol < 5) failf("OC write %08x (program order %llu) accepted at %llu before the older read %08x (%llu)",
                                    xx.addr, (unsigned long long)s, (unsigned long long)tb.cyc, r->x->addr, (unsigned long long)r->s);
          viol++;
        } else yng = true;
      }
      if (yng) younger++;
    };
  }
  uint32_t pick(int size, uint32_t line) {
    uint32_t a = line + rng.u(32);
    if (size == BZ_W) a &= ~1u;
    if (size == BZ_L) a &= ~3u;
    if (size == BZ_Q) a &= ~7u;
    return a;
  }
  // one cycle of the core: issue the held op when its resources allow (v1_ocache's claim conditions)
  void cycle(int64_t now) {
    if (t_next < 0 || now < t_next) return;
    if (pend < 0) pend = (int)rng.u(100);
    const bool fwait = fill && fill->t_ready < 0;                    // a line request waits for its accept
    const uint32_t line = lines[rng.u((uint32_t)lines.size())];
    const int k = pend;
    if (k < 35) {                                                      // a fill: 20 blocking load miss, 15 PREF / store miss
      const bool victim = (k % 3) == 0;
      if (fwait || wtb || (victim && wbb)) return;                     // fill_ok: one waiting line request, WTB empty, WBB free
      Xact *f = tb.mk(BIU_OC_FILL, false, line + 8u * rng.u(4), BZ_LINE);
      rd(f);
      fill = f;
      if (k < 20) { t_next = -1; f->on_crit = [this](Xact &x) { t_next = x.t_crit + 1; }; } else t_next = now + 2;
      tb.submit(f, now + 1);
      if (victim) {                                                    // the dirty victim, loaded into the WBB in the claim
        uint32_t v = line;
        while (v == line) v = lines[rng.u((uint32_t)lines.size())];
        Xact *w = tb.mk(BIU_OC_WBB, true, v, BZ_LINE);
        wr(w);
        wbb = true;
        w->on_done = [this](Xact &) { wbb = false; };
        tb.submit(w, now + 1);
      }
    } else if (k < 55 || k >= 97) {                                    // an uncached read (blocking); TAS.B's is locked
      const bool tas = k >= 97;
      if ((po && fwait) || wtb) return;                                // c_ur: WTB empty, no line request waiting
      const int size = tas ? BZ_B : (int)rng.u(4);
      const uint32_t a = pick(size, line);
      Xact *r = tb.mk(BIU_OC_UNC, false, a, size);
      r->lock = tas;
      rd(r);
      t_next = -1;
      r->on_crit = [this, tas, a](Xact &x) {
        if (!tas) { t_next = x.t_crit + 1; return; }
        Xact *s = tb.mk(BIU_OC_WTB, true, a, BZ_B);                  // TAS.B's write: the WTB (k_tw), right after the data
        wr(s);
        wtb = true;
        s->on_done = [this](Xact &) { wtb = false; };
        tb.submit(s, x.t_crit + 1);
        t_next = x.t_crit + 3;
      };
      tb.submit(r, now + 1);
    } else if (k < 87) {                                               // a store through the WTB (posted)
      if (wtb || (po && fwait)) return;
      const int size = (int)rng.u(4);
      Xact *s = tb.mk(BIU_OC_WTB, true, pick(size, line), size);
      wr(s);
      wtb = true;
      s->on_done = [this](Xact &) { wtb = false; };
      tb.submit(s, now + 1);
      t_next = now + 2;
    } else {                                                           // a write-back (OCBWB / OCBP of a dirty line)
      if (wbb || (po && fwait)) return;
      Xact *w = tb.mk(BIU_OC_WBB, true, line, BZ_LINE);
      wr(w);
      wbb = true;
      w->on_done = [this](Xact &) { wbb = false; };
      tb.submit(w, now + 1);
      t_next = now + 2;
    }
    pend = -1;
    ops++;
  }
};

static void grp_order(Tb &tb) {
  g_grp = "order";
  for (int run = 0; run < 3; run++) {
    const bool po = run < 2;                                           // run 2: the negative control
    const int NOPS = po ? 4000 : 1500;
    for (int p = 0; p < 8; p++) tb.port[p] = Port();
    tb.md = 0x065; tb.reset();
    Cfg c = dc_cfg(true); c.rtcor = 8; c.rtcsr = 0x0008;               // a refresh every 32 Bcyc
    tb.program(c); tb.shadow_sync();
    Rng rng(700 + (uint64_t)run);
    for (int a : {0, 1, 2, 4, 5, 6}) { tb.board.rd_waits[a] = (int)rng.u(4); tb.board.wr_waits[a] = (int)rng.u(3); }
    OrdOc oc(tb, 900 + (uint64_t)run, po);
    oc.lines = {ram_a(0, 20, 0), ram_a(0, 21, 4), ram_a(1, 20, 8), ram_a(2, 22, 12), 0x04002000u, 0x04002060u};
    const uint32_t icl[4] = {ram_a(0, 30, 0), ram_a(1, 31, 16), ram_a(3, 30, 32), 0x04003000u};
    bool ic_busy = false, sq_busy = false, dma_busy = false;
    const uint64_t c0 = tb.cyc;
    while (oc.ops < NOPS && tb.cyc - c0 < (uint64_t)NOPS * 400) {
      const int64_t now = (int64_t)tb.cyc;
      oc.cycle(now);
      if (!ic_busy && rng.u(6) == 0) {                                 // the IC: a line fill or an uncached word, blocking
        const bool fl = rng.u(3) != 0;
        Xact *x = tb.mk(fl ? BIU_IC_FILL : BIU_IC_UNC, false, icl[rng.u(4)] + (fl ? 8u * rng.u(4) : 4u * rng.u(8)), fl ? BZ_LINE : BZ_L);
        ic_busy = true;
        x->on_crit = [&](Xact &) { ic_busy = false; };
        tb.submit(x, now + 1);
      }
      if (!sq_busy && rng.u(20) == 0) {                                // the SQ: a burst
        Xact *x = tb.mk(BIU_SQ, true, ram_a(3, 40 + (int)rng.u(2), 4 * (int)rng.u(8)), BZ_LINE);
        sq_busy = true;
        x->on_done = [&](Xact &) { sq_busy = false; };
        tb.submit(x, now + 1);
      }
      if (!dma_busy && rng.u(25) == 0) {                               // the DMAC: dual units of 4 / 8 / 32 bytes
        static const int dsz[3] = {BZ_L, BZ_Q, BZ_LINE};
        const int size = dsz[rng.u(3)];
        Xact *x = tb.mk(PORT_DMA, rng.u(2) != 0, ram_a(2, 50 + (int)rng.u(2), 4 * (int)rng.u(16)) + (size == BZ_L ? 4u * rng.u(2) : 0u), size);
        x->master = BM_DMA_DUAL;
        dma_busy = true;
        x->on_done = [&](Xact &) { dma_busy = false; };
        tb.submit(x, now + 1);
      }
      tb.step();
    }
    const bool fin = oc.ops >= NOPS;
    tb.run_until_idle(400000);
    tb.settle();
    printf("  order run %d (%s): %ld OC ops (%ld reads, %ld writes), %ld OC writes accepted while a younger OC read waited, "
           "%ld writes before an older read, %ld reads off the program-order image\n", run,
           po ? "SOC.md 3.3 program order" : "negative control", oc.ops, oc.n_rd, oc.n_wr, oc.younger, oc.viol, oc.bad);
    CHECK(fin, "order run %d: the OC issued only %ld of %d ops", run, oc.ops, NOPS);
    if (po) {
      CHECK(oc.viol == 0 && oc.bad == 0, "order run %d: %ld OC writes accepted before an older OC read, %ld reads off the "
            "program-order image (want 0, 0)", run, oc.viol, oc.bad);
      CHECK(oc.younger > 0, "order run %d: no OC write overtook a younger waiting OC read (the rules never fired)", run);
      CHECK(tb.mem_diff("order") == 0, "order run %d: memory differs", run);
    } else {
      CHECK(oc.viol > 0 && oc.bad > 0, "negative control: %ld writes before an older read, %ld reads off the image (want "
            "both > 0: without the requester rule the WTB-first rule reorders)", oc.viol, oc.bad);
    }
  }
  for (int p = 0; p < 8; p++) tb.port[p] = Port();
  tb.md = 0x065; tb.reset(); tb.program(dc_cfg(false));
}

// tb/sh4_bsc.h in functional mode, advanced as Sh4Sys does it (tick_icyc(1) per instruction), and its pins read live
// [finding 4, verify `model`]
static void grp_model() {
  g_grp = "model";
  auto mk = [] {
    std::unique_ptr<sh4bsc::Bsc> b(new sh4bsc::Bsc);
    b->md = 0x065; b->reset_pin();
    b->wr(0xF800024, 1, 0xA55E, false);                                // RTCOR 0x5E
    b->wr(0xF80001C, 1, 0xA510, false);                                // CKS = CKIO / 16
    return b;
  };
  auto state = [](sh4bsc::Bsc &b) {
    uint32_t cnt = 0, csr = 0, rf = 0;
    b.rd(0xF800020, 1, cnt, false); b.rd(0xF80001C, 1, csr, false); b.rd(0xF800028, 1, rf, false);
    return ((uint64_t)cnt << 32) | ((uint64_t)csr << 16) | rf;
  };
  auto a = mk(), b2 = mk(), c = mk();
  for (int i = 0; i < 10000; i++) a->tick_icyc(1);
  for (int i = 0; i < 5000; i++) b2->tick_icyc(2);
  Rng r(3);
  for (uint64_t n = 0; n < 10000;) { const uint64_t k = std::min<uint64_t>(1 + r.u(7), 10000 - n); c->tick_icyc(k); n += k; }
  CHECK(a->matches == 3 && b2->matches == 3 && c->matches == 3 && state(*a) == state(*b2) && state(*a) == state(*c),
        "functional mode, 10000 Icyc: %llu / %llu / %llu matches (tick_icyc 1 / 2 / mixed; want 3), states %llx %llx %llx",
        (unsigned long long)a->matches, (unsigned long long)b2->matches, (unsigned long long)c->matches,
        (unsigned long long)state(*a), (unsigned long long)state(*b2), (unsigned long long)state(*c));
  sh4bsc::Bsc p;
  p.md = 0x065; p.reset_pin();
  p.wr(0xF80002C, 2, 0x00000005u, false);                              // PA0, PA1 outputs, the rest inputs
  p.wr(0xF800030, 1, 0x0002, false);
  p.wr(0xF800048, 1, 0x0100, false);                                   // GPIOIC: PA8
  uint32_t v = 0;
  p.port_in = 0x01FE3; p.rd(0xF800030, 1, v, false);
  CHECK(v == ((0x1FE3u & ~3u) | 2u) && !p.gpio(), "functional PDTRA with pins 0x1FE3: %04x, GPIO %d", v, p.gpio());
  p.port_in = 0xA0000; p.rd(0xF800030, 1, v, false);
  CHECK(v == 2u && p.gpio(), "functional PDTRA with pins 0xA0000: %04x, GPIO %d (PA8 low)", v, p.gpio());
  p.rd(0xF800044, 1, v, false);
  CHECK(v == 0xAu, "functional PDTRB with pins 0xA0000: %x", v);
}

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  std::string only;
  uint64_t seed = 1; int nrand = 3000;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i];
    else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = strtoull(argv[++i], nullptr, 0);
    else if (!strcmp(argv[i], "--n") && i + 1 < argc) nrand = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--trace")) g_trace = true;
    else if (!strcmp(argv[i], "--trace-win") && i + 2 < argc) { g_tr0 = strtoull(argv[++i], nullptr, 0); g_tr1 = strtoull(argv[++i], nullptr, 0); }
    else if (!strcmp(argv[i], "--cfg") && i + 1 < argc) g_rcfg = atoi(argv[++i]);
  }
  auto want = [&](const char *g) { return only.empty() || only == g; };
  {
    Tb tb;
    if (want("regs"))   { grp_regs(tb);   printf("group regs: %d checks, %d failures so far\n", g_checks, g_fail); }
    if (want("rfsh"))   { grp_rfsh(tb);   printf("group rfsh: %d checks, %d failures so far\n", g_checks, g_fail); }
  }
  {
    Tb tb;
    if (want("timing")) { grp_timing(tb); grp_refresh_timing(tb); grp_mpx_timing(tb);
                          printf("group timing: %d checks, %d failures so far\n", g_checks, g_fail); }
  }
  {
    Tb tb;
    if (want("mpx"))    { grp_mpx(tb);    printf("group mpx: %d checks, %d failures so far\n", g_checks, g_fail); }
    if (want("lanes"))  { grp_lanes(tb);  printf("group lanes: %d checks, %d failures so far\n", g_checks, g_fail); }
    if (want("idle"))   { grp_idle(tb);   printf("group idle: %d checks, %d failures so far\n", g_checks, g_fail); }
    if (want("misc"))   { grp_misc(tb);   printf("group misc: %d checks, %d failures so far\n", g_checks, g_fail); }
  }
  {
    Tb tb;
    if (want("random")) { grp_random(tb, seed, nrand); printf("group random: %d checks, %d failures so far\n", g_checks, g_fail); }
  }
  {
    Tb tb;
    if (want("basic"))   { grp_basic(tb);   printf("group basic: %d checks, %d failures so far\n", g_checks, g_fail); }
    if (want("pcmcia"))  { grp_pcmcia(tb);  printf("group pcmcia: %d checks, %d failures so far\n", g_checks, g_fail); }
    if (want("rdyhold")) { grp_rdyhold(tb); printf("group rdyhold: %d checks, %d failures so far\n", g_checks, g_fail); }
    if (want("manlock")) { grp_manlock(tb); printf("group manlock: %d checks, %d failures so far\n", g_checks, g_fail); }
    if (want("breq"))    { grp_breq(tb);    printf("group breq: %d checks, %d failures so far\n", g_checks, g_fail); }
    if (want("idlex"))   { grp_idlex(tb);   printf("group idlex: %d checks, %d failures so far\n", g_checks, g_fail); }
    if (want("sdmr"))    { grp_sdmr(tb);    printf("group sdmr: %d checks, %d failures so far\n", g_checks, g_fail); }
  }
  {
    Tb tb;
    if (want("dmaq"))    { grp_dmaq(tb);    printf("group dmaq: %d checks, %d failures so far\n", g_checks, g_fail); }
    if (want("turn"))    { grp_turn(tb);    printf("group turn: %d checks, %d failures so far\n", g_checks, g_fail); }
    if (want("order"))   { grp_order(tb);   printf("group order: %d checks, %d failures so far\n", g_checks, g_fail); }
  }
  if (want("model"))     { grp_model();     printf("group model: %d checks, %d failures so far\n", g_checks, g_fail); }
  printf("v1-bsc tb: %d checks, %d failures\n", g_checks, g_fail);
  printf("v1-bsc tb: %s\n", g_fail ? "FAIL" : "PASS");
  return g_fail ? 1 : 0;
}
