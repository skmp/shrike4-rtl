// v1_p4bus_tb.h -- shared slave side of the P4 bridge testbenches (tb/v1_p4bus_tb.cpp, tb/v1_lsu_tb.cpp; WI-P4).
//
// Every one of the bridge's 22 slave ports gets a stub: a register file with the SOC.md 3.1.4 size rule (a register
// answers only its own size; another size reads 0 with hit = 1 and ignores the write; a hole reads 0 with hit = 0),
// the special registers the tests need (SCFRDR2 pops a FIFO, TCNT counts down every 16 Icyc, PMCTR returns the cycle
// of its strobe, SDMR is write-only, PVR / SDIR read-only), and a stall policy (the array slaves stall the first strobe
// of every transaction, the MMU stalls a PTEH write once, a test may add random stalls).
//
// The stubs also check the slave-side contract of SOC.md 3.1.5 in every cycle:
//   * a strobe is a read or a write, never both; its blk belongs to the port;
//   * a Bclk-class port is strobed only in bce cycles, a Pclk-class port only in pce cycles;
//   * a strobe answered with stall = 1 is presented again, identical, at the port's next strobe opportunity (the next
//     cycle for Iclk ports, the next bce / pce otherwise), and nothing else is strobed on the port meanwhile;
//   * the slave acts only on the non-stalled strobe ("capture"), which is logged for the master-side checks.
// Unstrobed ports drive junk on rdata (the contract says 0; junk proves the bridge muxes only the strobed port).
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>
#include <string>
#include "p4bus_ref.h"

namespace p4tb {
using namespace p4ref;

static long g_fail = 0;
static int g_verbose = 0;
#define TB_CHECK(cond, ...)                                                                                          \
  do {                                                                                                               \
    if (!(cond)) {                                                                                                   \
      if (g_fail < 40) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); }            \
      g_fail++;                                                                                                      \
    }                                                                                                                \
  } while (0)

// ---- deterministic random (splitmix64)
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed = 0x9E3779B97F4A7C15ull) : s(seed) {}
  uint64_t next() {
    uint64_t z = (s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
  uint32_t u32() { return (uint32_t)next(); }
  uint32_t n(uint32_t k) { return (uint32_t)(((next() >> 32) * k) >> 32); }
};

inline uint32_t size_mask(int sz) { return sz == 0 ? 0xFFu : sz == 1 ? 0xFFFFu : 0xFFFFFFFFu; }

enum RegKind { K_PLAIN, K_RO, K_FIFO, K_FIFOCNT, K_TCNT, K_PMCTR, K_WONLY };
struct StubReg { int size; uint32_t val; int kind; };

// One captured (non-stalled) strobe.
struct Capture {
  int64_t cyc; int port; int blk; uint32_t off; int size; bool we; uint32_t wdata; bool dma; uint32_t rdata; bool hit;
};

// the canonical register key of a slave offset: the CCN blocks decode a[7:0] (a[17:8] ignored), PMCTR a[4:2]
inline uint64_t reg_key(int blk, uint32_t off) {
  uint32_t o = off;
  if (blk >= P4B_CCN_MMU && blk <= P4B_CCN_ID) o = off & 0xFF;
  else if (blk == P4B_UBC) o = off & 0xFF;
  else if (blk == P4B_PMCTR) o = off & 0x1C;
  return ((uint64_t)blk << 28) | o;
}

struct Stub {
  int port = 0;
  std::map<uint64_t, StubReg> regs;
  bool array = false;                    // every 32-bit offset is a register (sparse contents)
  std::map<uint64_t, uint32_t> arr;
  int stall_first = 0;                   // stalls answered to the first strobe(s) of each transaction
  bool pteh_stall = false;               // PTEH write: one stall
  int rand_stall_pct = 0;                // extra random stalls per strobe
  int rand_stall_max = 3;
  // protocol state
  bool stalled = false;                  // the last strobe was answered with stall = 1
  int64_t stall_cyc = -1;
  uint32_t sf[7] = {};                   // its fields: rd wr size blk off wdata dma
  int stalls_this = 0;                   // stalls answered to the transaction being strobed
  // statistics
  uint64_t fifo_pops = 0, captures = 0, strobes = 0;
  // current-cycle answer
  bool strobed = false, cap = false; uint32_t cur[7] = {}; uint32_t ans_rdata = 0; bool ans_hit = false;

  void add(int blk, uint32_t off, int size, uint32_t val, int kind = K_PLAIN) {
    regs[reg_key(blk, off)] = StubReg{size, val, kind};
  }
  StubReg *find(int blk, uint32_t off) {
    auto it = regs.find(reg_key(blk, off));
    return it == regs.end() ? nullptr : &it->second;
  }
};

inline int port_class(int port) {
  switch (port) {
    case PP_BSC: case PP_SDMR: case PP_DMAC: return P4K_BCLK;
    case PP_CPG: case PP_WDT: case PP_RTC: case PP_INTC: case PP_TMU: case PP_SCI: case PP_SCIF: case PP_HUDI:
      return P4K_PCLK;
    default: return P4K_ICLK;
  }
}

// ---- the register files (debug-ubc-hudi 2.3; values as the console read them under KOS where it matters)
inline void build_stubs(Stub *st) {
  for (int p = 0; p < PP_N; p++) { st[p] = Stub(); st[p].port = p; }
  auto &mmu = st[PP_MMU_REG];
  mmu.add(P4B_CCN_MMU, 0x00, 2, 0x8C062000); mmu.add(P4B_CCN_MMU, 0x04, 2, 0); mmu.add(P4B_CCN_MMU, 0x08, 2, 0);
  mmu.add(P4B_CCN_MMU, 0x0C, 2, 0x8C062002); mmu.add(P4B_CCN_MMU, 0x10, 2, 0); mmu.add(P4B_CCN_MMU, 0x34, 2, 0);
  mmu.pteh_stall = true;
  st[PP_UBC_CCN].add(P4B_CCN_UBC, 0x14, 0, 0); st[PP_UBC_CCN].add(P4B_CCN_UBC, 0x18, 0, 0);
  st[PP_CCR].add(P4B_CCN_CCR, 0x1C, 2, 0x105); st[PP_CCR].add(P4B_CCN_CCR, 0x38, 2, 4);
  st[PP_CCR].add(P4B_CCN_CCR, 0x3C, 2, 4);
  st[PP_EXC].add(P4B_CCN_EXC, 0x20, 2, 0); st[PP_EXC].add(P4B_CCN_EXC, 0x24, 2, 0xE0);
  st[PP_EXC].add(P4B_CCN_EXC, 0x28, 2, 0x400);
  st[PP_PMC_CCN].add(P4B_CCN_PMCR, 0x84, 1, 0xC023); st[PP_PMC_CCN].add(P4B_CCN_PMCR, 0x88, 1, 0x0023);
  st[PP_CCN_ID].add(P4B_CCN_ID, 0x30, 2, 0x040205C1, K_RO);
  for (uint32_t o = 4; o <= 0x1C; o += 4) st[PP_PMCTR].add(P4B_PMCTR, o, 2, 0, K_PMCTR);   // a[4:2] (stub: exact)
  auto &ubc = st[PP_UBC];
  ubc.add(P4B_UBC, 0x00, 2, 0); ubc.add(P4B_UBC, 0x04, 0, 0); ubc.add(P4B_UBC, 0x08, 1, 0); ubc.add(P4B_UBC, 0x0C, 2, 0);
  ubc.add(P4B_UBC, 0x10, 0, 0); ubc.add(P4B_UBC, 0x14, 1, 0); ubc.add(P4B_UBC, 0x18, 2, 0xDBFD4777);
  ubc.add(P4B_UBC, 0x1C, 2, 0xDFFFDBDF); ubc.add(P4B_UBC, 0x20, 1, 0);
  const uint32_t B = 0xF800000;
  auto &bsc = st[PP_BSC];
  uint32_t bl[][3] = {{0x00, 2, 0xA3020008}, {0x04, 1, 0x0001}, {0x08, 2, 0x01110111}, {0x0C, 2, 0x618066D8},
                      {0x10, 2, 0x07777777}, {0x14, 2, 0xC00A0E24}, {0x18, 1, 0}, {0x1C, 1, 0x0094}, {0x20, 1, 0x12},
                      {0x24, 1, 0x005E}, {0x28, 1, 0x0203}, {0x2C, 2, 0x000A03F0}, {0x30, 1, 0x88E7},
                      {0x40, 2, 0}, {0x44, 1, 0x0003}, {0x48, 1, 0}};
  for (auto &r : bl) bsc.add(P4B_BSC, B + r[0], (int)r[1], r[2]);
  // SDMR2 / SDMR3: write-only, every offset of the window (address-encoded)
  const uint32_t D = 0xFA00000;
  for (int c = 0; c < 4; c++)
    for (int r = 0; r < 4; r++) st[PP_DMAC].add(P4B_DMAC, D + 0x10 * c + 4 * r, 2, (c == 1 && r == 0) ? 0xFFE80014 : 0);
  st[PP_DMAC].add(P4B_DMAC, D + 0x40, 2, 0x8201);
  const uint32_t C = 0xFC00000;
  st[PP_CPG].add(P4B_CPG, C + 0x00, 1, 0x0E0A); st[PP_CPG].add(P4B_CPG, C + 0x04, 0, 0x03);
  st[PP_CPG].add(P4B_CPG, C + 0x10, 0, 0x00);
  st[PP_WDT].add(P4B_WDT, C + 0x08, 0, 0); st[PP_WDT].add(P4B_WDT, C + 0x0C, 0, 0);
  const uint32_t R = 0xFC80000;
  for (uint32_t o = 0; o <= 0x18; o += 4) st[PP_RTC].add(P4B_RTC, R + o, 0, 0);
  st[PP_RTC].add(P4B_RTC, R + 0x1C, 1, 0); st[PP_RTC].add(P4B_RTC, R + 0x38, 0, 0); st[PP_RTC].add(P4B_RTC, R + 0x3C, 0, 9);
  const uint32_t I = 0xFD00000;
  st[PP_INTC].add(P4B_INTC, I + 0, 1, 0x8000); st[PP_INTC].add(P4B_INTC, I + 4, 1, 0xF0F0);
  st[PP_INTC].add(P4B_INTC, I + 8, 1, 0); st[PP_INTC].add(P4B_INTC, I + 12, 1, 0x0300);
  const uint32_t T = 0xFD80000;
  auto &tmu = st[PP_TMU];
  tmu.add(P4B_TMU, T + 0x00, 0, 0); tmu.add(P4B_TMU, T + 0x04, 0, 0x05);
  for (int c = 0; c < 3; c++) {
    tmu.add(P4B_TMU, T + 0x08 + 12 * c, 2, 0x0001E70F + c);
    tmu.add(P4B_TMU, T + 0x0C + 12 * c, 2, 0x00800000, K_TCNT);
    tmu.add(P4B_TMU, T + 0x10 + 12 * c, 1, 0x0020);
  }
  tmu.add(P4B_TMU, T + 0x2C, 2, 0xFF9B8012);
  const uint32_t S1 = 0xFE00000;
  for (uint32_t o = 0; o <= 0x1C; o += 4) st[PP_SCI].add(P4B_SCI, S1 + o, 0, o == 4 ? 0xFF : 0);
  const uint32_t S2 = 0xFE80000;
  auto &scif = st[PP_SCIF];
  scif.add(P4B_SCIF, S2 + 0x00, 1, 0); scif.add(P4B_SCIF, S2 + 0x04, 0, 0x0C); scif.add(P4B_SCIF, S2 + 0x08, 1, 0x30);
  scif.add(P4B_SCIF, S2 + 0x0C, 0, 0, K_WONLY); scif.add(P4B_SCIF, S2 + 0x10, 1, 0x0060);
  scif.add(P4B_SCIF, S2 + 0x14, 0, 0, K_FIFO); scif.add(P4B_SCIF, S2 + 0x18, 1, 0x0040);
  scif.add(P4B_SCIF, S2 + 0x1C, 1, 16, K_FIFOCNT); scif.add(P4B_SCIF, S2 + 0x20, 1, 0x0011);
  scif.add(P4B_SCIF, S2 + 0x24, 1, 0);
  const uint32_t H = 0xFF00000;
  st[PP_HUDI].add(P4B_HUDI, H + 0x00, 1, 0xFFFF, K_RO); st[PP_HUDI].add(P4B_HUDI, H + 0x08, 2, 0x5573A3AB, K_RO);
  st[PP_IC_ARR].array = st[PP_OC_ARR].array = st[PP_MMU_ARR].array = true;
  st[PP_IC_ARR].stall_first = st[PP_OC_ARR].stall_first = st[PP_MMU_ARR].stall_first = 1;
}

// The slave side of a Verilated top that has the flattened ports of tb/v1_p4bus_tb.sv (s_rd, s_wr, s_dma, s_size,
// s_blk, s_off, s_wdata; r_rdata, r_hit, r_stall).
template <class M> struct SlaveSide {
  Stub st[PP_N];
  std::vector<Capture> caps;             // every capture, in order
  std::vector<Capture> cyc_caps;         // this cycle's
  Rng rng{0x51A7E};
  int64_t tcnt_base = 0;                 // TCNT = 0x00800000 - (cycle - tcnt_base) / 16
  SlaveSide() { build_stubs(st); }

  static bool opportunity(int cls, int64_t cyc, bool bce, bool pce) {
    (void)cyc;
    return cls == P4K_ICLK ? true : cls == P4K_BCLK ? bce : pce;
  }

  // first half of a cycle: after eval with the strobes valid, answer them.  Returns nothing; sets r_*.
  void answer(M *top, int64_t cyc, bool bce, bool pce) {
    uint32_t rd = top->s_rd, wr = top->s_wr, dma = top->s_dma;
    uint32_t hit = 0, stall = 0;
    cyc_caps.clear();
    for (int p = 0; p < PP_N; p++) {
      Stub &s = st[p];
      s.strobed = s.cap = false;
      bool r = (rd >> p) & 1, w = (wr >> p) & 1;
      int cls = port_class(p);
      if (!r && !w) {
        top->r_rdata[p] = 0xBAD00000u | (uint32_t)p;          // junk on unstrobed ports
        // a stalled strobe must come back at the very next opportunity
        if (s.stalled && s.stall_cyc < cyc && opportunity(cls, cyc, bce, pce)) {
          TB_CHECK(false, "cyc %ld port %d: stalled strobe not re-presented at the next opportunity", (long)cyc, p);
          s.stalled = false;
        }
        continue;
      }
      s.strobed = true;
      s.strobes++;
      uint32_t f[7] = {(uint32_t)r, (uint32_t)w, top->s_size[p], top->s_blk[p], top->s_off[p], top->s_wdata[p],
                       (dma >> p) & 1u};
      memcpy(s.cur, f, sizeof f);
      TB_CHECK(!(r && w), "cyc %ld port %d: read and write strobe together", (long)cyc, p);
      TB_CHECK(port_of((int)f[3]) == p, "cyc %ld port %d: blk %s does not belong to the port", (long)cyc, p,
               blk_name((int)f[3]));
      TB_CHECK(opportunity(cls, cyc, bce, pce), "cyc %ld port %d: strobe outside the port's clock (bce %d pce %d)",
               (long)cyc, p, bce, pce);
      if (s.stalled) {
        TB_CHECK(memcmp(f, s.sf, sizeof f) == 0, "cyc %ld port %d: re-presented strobe differs after a stall",
                 (long)cyc, p);
      } else {
        s.stalls_this = 0;
      }
      // stall decision
      bool stl = false;
      // a Pclk slave never stalls: none does, and the landing rule of SOC.md 9.19 (a write meets its slave at the end
      // of its bus cycle, a read strobed in that cen one cen later) assumes it (v1_p4bus_path LAND)
      if (cls == P4K_PCLK) stl = false;
      else if (s.stalls_this < s.stall_first) stl = true;
      else if (s.pteh_stall && w && f[3] == P4B_CCN_MMU && (f[4] & 0xFF) == 0 && s.stalls_this < 1) stl = true;
      else if (s.rand_stall_pct && s.stalls_this < s.rand_stall_max && (int)rng.n(100) < s.rand_stall_pct) stl = true;
      if (stl) {
        s.stalls_this++;
        s.stalled = true;
        s.stall_cyc = cyc;
        memcpy(s.sf, f, sizeof f);
        top->r_rdata[p] = 0xDEAD0000u | (uint32_t)p;          // junk while stalled: must not be captured
        hit |= 1u << p;
        stall |= 1u << p;
        continue;
      }
      s.stalled = false;
      // the access (the side effects are applied in commit(), at the end of the cycle)
      uint32_t v = 0; bool h = false;
      eval_access(s, (int)f[3], f[4], (int)f[2], w, f[5], cyc, v, h, false);
      s.ans_rdata = w ? 0 : v;
      s.ans_hit = h;
      s.cap = true;
      top->r_rdata[p] = s.ans_rdata;
      if (h) hit |= 1u << p;
      cyc_caps.push_back(Capture{cyc, p, (int)f[3], f[4], (int)f[2], w, f[5], (bool)f[6], s.ans_rdata, h});
    }
    top->r_hit = hit;
    top->r_stall = stall;
  }

  // the register model; apply = false computes the answer, apply = true performs the side effects
  void eval_access(Stub &s, int blk, uint32_t off, int size, bool w, uint32_t wdata, int64_t cyc, uint32_t &v,
                   bool &hit, bool apply) {
    v = 0; hit = false;
    if (s.array) {
      hit = true;
      uint64_t k = ((uint64_t)blk << 28) | (off & ~3u);
      if (w) { if (apply && size == 2) s.arr[k] = wdata; }
      else if (size == 2) { auto it = s.arr.find(k); v = it == s.arr.end() ? (off ^ 0x5A5A0000u) : it->second; }
      return;
    }
    if (blk == P4B_SDMR2 || blk == P4B_SDMR3) { hit = true; return; }       // write-only: reads 0
    StubReg *r = s.find(blk, off);
    if (!r) return;                                                          // a hole: 0, hit = 0
    hit = true;
    bool size_ok = r->size == size || r->kind == K_PMCTR;
    if (w) {
      if (!size_ok || r->kind == K_RO || !apply) return;
      if (r->kind == K_TCNT) { r->val = wdata; return; }
      r->val = wdata & size_mask(size);
      return;
    }
    if (!size_ok) return;
    switch (r->kind) {
      case K_FIFO: {
        StubReg *cnt = s.find(blk, off + 8);                                // SCFDR2 at +8
        v = 0x40 + (uint32_t)(s.fifo_pops & 0x3F);
        if (apply) { s.fifo_pops++; if (cnt && cnt->val) cnt->val--; }
        break;
      }
      case K_TCNT: v = r->val - (uint32_t)((cyc - tcnt_base) / 16); break;
      case K_PMCTR: v = (uint32_t)cyc; break;
      case K_WONLY: v = 0; break;
      default: v = r->val & size_mask(r->size); break;
    }
  }

  // end of the cycle: the captured strobes act
  void commit(int64_t cyc) {
    for (auto &c : cyc_caps) {
      Stub &s = st[c.port];
      uint32_t v; bool h;
      eval_access(s, c.blk, c.off, c.size, c.we, c.wdata, cyc, v, h, true);
      s.captures++;
      caps.push_back(c);
    }
  }

  // direct (backdoor) register reads for the tests
  uint32_t peek(int blk, uint32_t off) {
    Stub &s = st[port_of(blk)];
    StubReg *r = s.find(blk, off);
    return r ? r->val : 0;
  }
};

}  // namespace p4tb
