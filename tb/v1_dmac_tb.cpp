// v1_dmac_tb.cpp -- unit testbench of the SH7091 DMAC (rtl/v1/v1_dmac*.sv) against tb/dmac_ref.h and the console
// measurements of rtl/v1/spec/dmac.md (tools/hw/dmac/dmac1.elf.hwlog, dmac2.elf.hwlog).  `make v1-dmac`.
//
// Around the DUT (tb/v1_dmac_tb_top.sv), all in C++, advanced one Icyc at a time (bce every 2nd Icyc, pce every 4th):
//   * BscStub   the DMAC's BSC port: SOC.md 3.3.1 / 3.3.2 handshake (ready / rd_v / wd_req / bwd / done, pulses on
//               bce), configurable latencies and jitter, pipelined DMAC single-address cycles, a lock hold for the
//               dual read -> write pair, and an optional CPU competitor (DMAC > CPU, hand-over only between cycles)
//               for the bus-release checks; protocol checks.  rd_beat as SOC.md 3.3.2 defines it: the quadword
//               index in the line, i.e. addr[4:3] for a 1 / 2 / 4 / 8-byte read too; a read that is not a line is
//               one rd_v.  A quadword (BZ_Q) is served (qmode) 0: as one bus cycle, the default -- the BSC never
//               splits a DMAC 8-byte unit (SOC.md 3.3.2, I087 [HW dmac 3.13 obs. 1]); 1: in the shape of a split
//               CPU BZ_Q, two longword bus cycles behind one rd_v with the whole quadword / one wd_req / one done
//               (a legal handshake that only takes longer; the DMAC must not depend on the BSC's timing).
//   * P4Stub    the bridge as seen by the DMAC master (p4q held until ack, configurable latency), in front of fake
//               on-chip modules (a TMU-like block, SCI and SCIF data registers with TDRE / RDRF / TDFE / RDF levels
//               that auto-clear on the DMAC's own access) and err for P4B_NONE / P4B_UBC_NODRV.
//   * Cpu       the bridge's strobes of the DMAC register slave (bce cycles, re-presented while stall).
//   * lockstep  the reference model (dmac_ref.h) stepped at the DUT's commit / counter-update taps (the pick is checked
//               against the model's priority order, the candidate set against the model's requests in every IDLE
//               Bclk and at every commit, so a request one side never raises shows even if no unit starts, and the
//               DDT channel-0 flag / in-service state after every Bclk); inside one Bclk
//               the model sees a CPU read, NMI / run / standby, class A, the counter update, the commit, a DDT event,
//               a CPU write, standby's TE clear -- the order the RTL's flops give (Tb::tick).  After every Bclk every
//               register and the DDT flag / queues must match; with the DMAC stopped and drained, memory and the
//               device streams must match byte for byte.  DDT events are not driven in a Bclk with a commit or an
//               update (or right after one) in lockstep: the RTL merges a same-Bclk DTR and pop in one flop update.
//   * random    V1_DMAC_RAND programs (--rand N, --seed S, --from K: each program has its own RNG): 1-4 channels of
//               random RS / TS / SM / DM / TM / IE / DS, addresses with misalignment and area-7 / on-chip legs, random
//               DMAOR.PR and DDT, random BSC / bridge latencies, jitter, quadword service shapes and a CPU competitor,
//               and a random stream of DTRs, direct channel-2 requests, TR-only repeats, note-16 clears, DE / DME
//               toggles, AE / NMIF clears, DMATCR rewrites, NMIs and register reads, all in strict lockstep (the
//               registers, the DDT state and the DMTE / DMAE levels compared after every Bclk).
// Every check prints a FAIL line; the run ends with "v1-dmac: PASS" / "v1-dmac: FAIL".  `--only <group>` runs one
// group (access classes transfers priority ddt irq extreq onchip timing fixes cover random; dbg = a trace, only on
// request).  `fixes` holds the regressions of the independent verification (F1-F6 of WI-DMAC-verify.md, and M1b-verify
// F2: a unit in flight when run falls) and `cover`
// the checks its mutation campaign found missing.
#include "Vv1_dmac_tb_top.h"
#include "verilated.h"
#include "dmac_ref.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

static int g_fail = 0, g_checks = 0;
static int g_verbose = 0;
static std::string g_test;
static void failf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  if (g_fail < 60) {
    printf("FAIL [%s] ", g_test.c_str());
    vprintf(fmt, ap);
    printf("\n");
  }
  va_end(ap);
  g_fail++;
}
#define CHECK(c, ...) do { g_checks++; if (!(c)) failf(__VA_ARGS__); } while (0)
#define CHECKEQ(a, b, what) do { uint64_t _a = (uint64_t)(a), _b = (uint64_t)(b); g_checks++; \
    if (_a != _b) failf("%s: got %llx expected %llx", what, (unsigned long long)_a, (unsigned long long)_b); } while (0)

struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 1) {}
  uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
  uint32_t u(uint32_t n) { return n ? (uint32_t)(next() % n) : 0; }
  bool p(int percent) { return u(100) < (uint32_t)percent; }
};

// ---------------------------------------------------------------------------------------------------------------
// memory: sparse, physical (29-bit); unwritten bytes read a fixed function of the address
struct Mem {
  std::unordered_map<uint32_t, std::vector<uint8_t>> pg;
  static uint8_t dflt(uint32_t a) { return (uint8_t)(a * 7 + (a >> 8) * 13 + 0x5A); }
  uint8_t *page(uint32_t a) {
    auto it = pg.find(a >> 12);
    if (it == pg.end()) {
      std::vector<uint8_t> v(4096);
      for (int k = 0; k < 4096; k++) v[k] = dflt((a & ~0xFFFu) + k);
      it = pg.emplace(a >> 12, std::move(v)).first;
    }
    return it->second.data();
  }
  uint8_t rd8(uint32_t a) { a &= 0x1FFFFFFF; return page(a)[a & 0xFFF]; }
  void wr8(uint32_t a, uint8_t v) { a &= 0x1FFFFFFF; page(a)[a & 0xFFF] = v; }
  uint64_t rd64(uint32_t a) { uint64_t v = 0; for (int k = 7; k >= 0; k--) v = (v << 8) | rd8(a + k); return v; }
  uint32_t rd32(uint32_t a) { return (uint32_t)rd8(a) | (uint32_t)rd8(a + 1) << 8 | (uint32_t)rd8(a + 2) << 16 |
                                     (uint32_t)rd8(a + 3) << 24; }
  void wr32(uint32_t a, uint32_t v) { for (int k = 0; k < 4; k++) wr8(a + k, (uint8_t)(v >> 8 * k)); }
  void fill(uint32_t a, uint32_t n, uint8_t v) { for (uint32_t k = 0; k < n; k++) wr8(a + k, v); }
  // every byte of either memory that differs
  static int diff(Mem &x, Mem &y, const char *what, int maxp = 8) {
    int nd = 0;
    std::vector<uint32_t> keys;
    for (auto &kv : x.pg) keys.push_back(kv.first);
    for (auto &kv : y.pg) keys.push_back(kv.first);
    for (uint32_t pgn : keys)
      for (uint32_t k = 0; k < 4096; k++) {
        uint32_t a = (pgn << 12) + k;
        if (x.rd8(a) != y.rd8(a)) {
          if (nd < maxp) failf("%s: byte %08x rtl %02x model %02x", what, a, x.rd8(a), y.rd8(a));
          nd++;
        }
      }
    return nd;
  }
};

// device data a DACK / TDACK device drives for its unit n of channel ch, byte k
static uint8_t dev_byte(int ch, uint64_t n, int k) { return (uint8_t)(0xA0 + ch * 0x11 + n * 7 + k * 3); }

// ---------------------------------------------------------------------------------------------------------------
// fake on-chip modules behind the bridge (physical 0x1Fxxxxxx decodes as 0xFF000000 | a[23:0])
struct OnChip {
  // TMU-like block 0xFFD80000: TOCR 00 (8), TSTR 04 (8), TCOR0 08 (32), TCNT0 0C (32), TCR0 10 (16)
  uint8_t tocr = 0x00, tstr = 0x05;
  uint32_t tcor0 = 0x12345678, tcnt0 = 0x9ABCDEF0;
  uint16_t tcr0 = 0x0123;
  // SCI 0xFFE00000: SCTDR1 0C (8), SCSSR1 10 (8: TDRE 7, RDRF 6), SCRDR1 14 (8)
  // SCIF 0xFFE80000: SCFTDR2 0C (8), SCFSR2 10 (16: TDFE 5, RDF 1), SCFRDR2 14 (8)
  bool tdre = true, rdrf = false, tdfe = true, rdf = false;
  uint8_t scrdr = 0, scfrdr = 0;
  std::vector<uint8_t> sci_tx, scif_tx;                  // bytes the DMAC wrote
  std::deque<uint8_t> sci_rxq, scif_rxq;                 // bytes the line will receive
  std::vector<uint8_t> sci_rx_taken, scif_rx_taken;      // bytes the DMAC read
  // timing (RTL side only): Icyc after a TDR write until TDRE / TDFE rise again, after an RDR read until the next byte
  int tx_delay = 0, rx_delay = 0;
  uint64_t t_tdre = 0, t_tdfe = 0, t_rdrf = 0, t_rdf = 0;
  bool tmu_ic = false;
  int dack_ic_count = 0;
  bool timed = false;

  static int blk(uint32_t a) { return (int)((a >> 18) & 0x3F); }
  static bool is_err(uint32_t a) {
    int b = blk(a);
    bool known = b == 0x00 || b == 0x04 || b == 0x20 || b == 0x24 || b == 0x25 || b == 0x28 || b == 0x30 ||
                 b == 0x32 || b == 0x34 || b == 0x36 || b == 0x38 || b == 0x3A || b == 0x3C;
    if (b == 0x08) return ((a >> 8) & 0x3FF) != 0;              // UBC_NODRV
    return !known;
  }
  void tick(uint64_t now) {
    if (!timed) return;
    if (!tdre && now >= t_tdre) tdre = true;
    if (!tdfe && now >= t_tdfe) tdfe = true;
    if (!rdrf && !sci_rxq.empty() && now >= t_rdrf) { scrdr = sci_rxq.front(); sci_rxq.pop_front(); rdrf = true; }
    if (!rdf && !scif_rxq.empty() && now >= t_rdf) { scfrdr = scif_rxq.front(); scif_rxq.pop_front(); rdf = true; }
  }
  void untimed_fill() {                                  // the model's copy: bytes are available at once
    if (!rdrf && !sci_rxq.empty()) { scrdr = sci_rxq.front(); sci_rxq.pop_front(); rdrf = true; }
    if (!rdf && !scif_rxq.empty()) { scfrdr = scif_rxq.front(); scif_rxq.pop_front(); rdf = true; }
  }
  uint32_t levels() const {
    return (tdre ? 16u : 0) | (rdrf ? 8u : 0) | (tdfe ? 4u : 0) | (rdf ? 2u : 0) | (tmu_ic ? 1u : 0);
  }
  // returns false for err (no module)
  bool rd(uint32_t a, int size, uint32_t &v, bool dma, uint64_t now) {
    v = 0;
    if (is_err(a)) return false;
    uint32_t off = a & 0xFFFFFF;
    switch (off) {
    case 0xD80000: if (size == 0) v = tocr; break;
    case 0xD80004: if (size == 0) v = tstr; break;
    case 0xD80008: if (size == 2) v = tcor0; break;
    case 0xD8000C: if (size == 2) v = tcnt0; break;
    case 0xD80010: if (size == 1) v = tcr0; break;
    case 0xE00010: if (size == 0) v = (tdre ? 0x80 : 0) | (rdrf ? 0x40 : 0); break;
    case 0xE00014: if (size == 0) { v = scrdr; if (dma && rdrf) { sci_rx_taken.push_back(scrdr); rdrf = false;
                                                                  t_rdrf = now + rx_delay; } } break;
    case 0xE80010: if (size == 1) v = (tdfe ? 0x20 : 0) | (rdf ? 0x02 : 0); break;
    case 0xE80014: if (size == 0) { v = scfrdr; if (dma && rdf) { scif_rx_taken.push_back(scfrdr); rdf = false;
                                                                  t_rdf = now + rx_delay; } } break;
    default: break;
    }
    return true;
  }
  bool wr(uint32_t a, int size, uint32_t v, bool dma, uint64_t now) {
    if (is_err(a)) return false;
    uint32_t off = a & 0xFFFFFF;
    switch (off) {
    case 0xD80000: if (size == 0) tocr = (uint8_t)v; break;
    case 0xD80004: if (size == 0) tstr = (uint8_t)v; break;
    case 0xD80008: if (size == 2) tcor0 = v; break;
    case 0xD8000C: if (size == 2) tcnt0 = v; break;
    case 0xD80010: if (size == 1) tcr0 = (uint16_t)v; break;
    case 0xE0000C: if (size == 0) { sci_tx.push_back((uint8_t)v); if (dma) { tdre = false; t_tdre = now + tx_delay; } } break;
    case 0xE8000C: if (size == 0) { scif_tx.push_back((uint8_t)v); if (dma) { tdfe = false; t_tdfe = now + tx_delay; } } break;
    default: break;
    }
    return true;
  }
};

// ---------------------------------------------------------------------------------------------------------------
// the model's bus: its own memory, its own on-chip copy, its own device streams
struct ModelBus : DmacBus {
  Mem mem;
  OnChip oc;
  std::map<int, std::vector<uint8_t>> dev_rx;     // memory -> device bytes per channel
  std::map<int, uint64_t> dev_wn;                  // device -> memory units per channel
  void ext_read(uint32_t pa, int bytes, uint8_t *buf) override { for (int k = 0; k < bytes; k++) buf[k] = mem.rd8(pa + k); }
  void ext_write(uint32_t pa, int bytes, const uint8_t *buf) override { for (int k = 0; k < bytes; k++) mem.wr8(pa + k, buf[k]); }
  bool onchip_read(uint32_t pa, int size, uint32_t &v) override { bool ok = oc.rd(pa, size, v, true, 0); oc.untimed_fill(); return ok; }
  bool onchip_write(uint32_t pa, int size, uint32_t v) override { bool ok = oc.wr(pa, size, v, true, 0); oc.tdre = oc.tdfe = true; return ok; }
  void dack_unit(int ch, bool we, uint32_t pa, int bytes) override {
    if (we) { uint64_t n = dev_wn[ch]++; for (int k = 0; k < bytes; k++) mem.wr8(pa + k, dev_byte(ch, n, k)); }
    else for (int k = 0; k < bytes; k++) dev_rx[ch].push_back(mem.rd8(pa + k));
  }
  void dack_ic() override { oc.dack_ic_count++; }
};

// ---------------------------------------------------------------------------------------------------------------
// the BSC as the DMAC's port sees it
struct BscStub {
  // configuration (Bclk)
  int rd_lat = 5, wr_lat = 2, gap = 1, done_lat = 1, acc_lat = 0, jitter = 0;
  int qmode = 0;                      // BZ_Q service: 0 one cycle (SOC.md 3.3.2), 1 two cycles behind one beat
  bool pipe = true;
  int pipe_gap = 1, max_out = 2;
  bool cpu_on = false;
  int cpu_len = 8, cpu_think = 0;
  Mem *mem = nullptr;
  Rng *rng = nullptr;
  // device streams (single address)
  std::map<int, std::vector<uint8_t>> dev_rx;
  std::map<int, uint64_t> dev_wn;
  // state
  uint64_t bt = 0;                    // Bclk count
  uint64_t busy_until = 0, last_beat = 0, last_acc = 0;
  bool any_acc = false, lock_hold = false;
  uint64_t cpu_free_at = 0;           // the CPU competitor requests again from this Bclk
  uint64_t req_seen = 0;              // Bclk the current DMA request was first seen
  bool req_pend = false;
  struct Txn {
    bool cpu, we, single, qwhole;
    uint32_t addr;
    int size, ch, master, nbeat, bi;
    std::vector<uint64_t> bts;        // beat times (read: rd_v; write: wd_req)
    bool wd_pend;
    int wd_beat;
    uint64_t done_t;
    uint64_t unit_n;
  };
  std::deque<Txn> q;
  struct LogE { uint64_t bt; bool cpu, we, lock; uint32_t addr; int size, master, ch; };
  std::vector<LogE> log;
  // protocol check
  bool pv = false;
  uint64_t pbits = 0;
  // outputs of this cycle
  bool ready = false, rd_v = false, rd_last = false, wd_req = false, done = false;
  int rd_beat = 0, wd_beat = 0;
  uint64_t rd_data = 0;

  int lat(int base) { return base + (jitter && rng ? (int)rng->u(jitter + 1) : 0); }
  // a chip reset: every transaction in flight is dropped (the Bclk count and the configuration stay)
  void reset_state() {
    q.clear(); log.clear(); dev_rx.clear(); dev_wn.clear();
    busy_until = last_beat = last_acc = bt; any_acc = lock_hold = false; cpu_free_at = bt; req_pend = false;
    pv = false; pbits = 0;
  }
  static int nbytes(int size) { static const int b[5] = {1, 2, 4, 8, 32}; return b[size]; }

  // one bce: `top` holds the DUT's outputs of this cycle (bq, bwd)
  void bce(Vv1_dmac_tb_top *t) {
    ready = rd_v = rd_last = wd_req = done = false;
    rd_beat = wd_beat = 0; rd_data = 0;
    bt++;
    uint64_t bits = ((uint64_t)t->bq_we) | ((uint64_t)t->bq_addr << 1) | ((uint64_t)t->bq_size << 30) |
                    ((uint64_t)t->bq_lock << 33) | ((uint64_t)t->bq_master << 34) | ((uint64_t)t->bq_ch << 36);
    if (pv) {                                             // SOC.md 3.3.2: v and r held from rise through ready
      CHECK(t->bq_v, "BSC port: bq.v withdrawn before ready (Bclk %llu)", (unsigned long long)bt);
      if (t->bq_v) CHECK(bits == pbits, "BSC port: bq.r changed while waiting (Bclk %llu)", (unsigned long long)bt);
    }
    // 1. the head transaction's data events of this Bclk
    if (!q.empty()) {
      Txn &x = q.front();
      if (x.cpu) {
        if (bt >= x.done_t) q.pop_front();
      } else if (!x.we) {
        if (x.bi < x.nbeat && bt >= x.bts[x.bi]) {
          uint32_t ba = x.size == 4 ? ((x.addr & ~31u) + (uint32_t)((((x.addr >> 3) + x.bi) & 3) * 8)) : (x.addr & ~7u);
          rd_v = true;
          rd_beat = x.size == 4 ? (int)((((x.addr >> 3) + x.bi) & 3)) : (int)((x.addr >> 3) & 3);   // as v1_bsc
          rd_data = mem->rd64(ba);
          rd_last = x.bi == x.nbeat - 1;
          if (x.single) {                                 // memory -> device: the device takes the unit's lanes
            int nb = nbytes(x.size);
            if (x.size == 4) for (int k = 0; k < 8; k++) dev_rx[x.ch].push_back((uint8_t)(rd_data >> 8 * k));
            else for (int k = 0; k < nb; k++) dev_rx[x.ch].push_back((uint8_t)(rd_data >> 8 * ((x.addr & 7) + k)));
          }
          x.bi++;
          if (rd_last) q.pop_front();
        }
      } else {
        if (x.wd_pend) {                                  // sample the beat's data (bwd valid since the Icyc after wd_req)
          x.wd_pend = false;
          int nb = nbytes(x.size);
          if (x.single) {
            // (M6) the byte enables of a single write's pull are the unit's own, whatever unit the engine runs by then
            const uint8_t sbe = t->bwd_be, swant = x.size == 4 ? 0xFF : (uint8_t)(((1u << nb) - 1) << (x.addr & 7));
            CHECKEQ(sbe, swant, "BSC port: a single-address write's byte enables (its own unit's)");
            if (x.size == 4) { for (int k = 0; k < 8; k++) if (sbe >> k & 1) mem->wr8((x.addr & ~31u) + x.wd_beat * 8 + k, dev_byte(x.ch, x.unit_n, x.wd_beat * 8 + k)); }
            else for (int k = 0; k < nb; k++) if (sbe >> ((x.addr & 7) + k) & 1) mem->wr8(x.addr + k, dev_byte(x.ch, x.unit_n, k));
          } else {
            uint32_t ba = x.size == 4 ? (x.addr & ~31u) + x.wd_beat * 8 : (x.addr & ~7u);
            uint64_t d = t->bwd_data;
            uint8_t be = t->bwd_be;
            if (x.size != 4) {
              uint8_t want = (uint8_t)(((1u << nb) - 1) << (x.addr & 7));
              CHECKEQ(be, want, "BSC port: write byte enables");
            }
            for (int k = 0; k < 8; k++) if (be >> k & 1) mem->wr8(ba + k, (uint8_t)(d >> 8 * k));
            if (g_verbose) printf("  bsc %llu write %08x be %02x data %016llx\n", (unsigned long long)bt, ba, be,
                                  (unsigned long long)d);
          }
        }
        if (x.bi < x.nbeat && bt >= x.bts[x.bi]) {       // late (not lost) when a pipelined write waits for the
          wd_req = true;                                  // previous write's done
          wd_beat = x.bi;
          x.wd_pend = true;
          x.wd_beat = x.bi;
          x.bi++;
          if (x.bi == x.nbeat) x.done_t = bt + 1 + done_lat;
        }
        if (x.bi == x.nbeat && !x.wd_pend && bt >= x.done_t) { done = true; q.pop_front(); }
      }
    }
    // 2. arbitration and acceptance
    bool dma_v = t->bq_v;
    if (dma_v && !req_pend) { req_pend = true; req_seen = bt; }
    bool single_req = t->bq_master == 2 || t->bq_master == 3;
    bool can = false;
    bool free = q.empty() && bt > busy_until;
    if (dma_v && bt >= req_seen + (uint64_t)acc_lat) {
      if (free) can = true;
      else if (pipe && single_req && !q.empty() && !q.front().cpu && q.front().single && (int)q.size() < max_out &&
               q.back().single && bt >= last_acc + (uint64_t)pipe_gap)
        can = true;
    }
    if (can) {
      ready = true;
      req_pend = false;
      Txn x{};
      x.cpu = false; x.we = t->bq_we; x.single = single_req; x.addr = t->bq_addr; x.size = t->bq_size;
      x.ch = t->bq_ch; x.master = t->bq_master; x.bi = 0; x.wd_pend = false;
      x.qwhole = qmode == 1 && x.size == 3;           // the split shape: the second longword cycle, then one beat
      x.nbeat = x.size == 4 ? 4 : 1;                  // a read that is not a line is one rd_v (SOC.md 3.3.2)
      uint64_t first = bt + (uint64_t)lat(x.we ? wr_lat : rd_lat) + (x.qwhole ? (uint64_t)gap + 1 : 0);
      if (first <= last_beat) first = last_beat + 1;
      for (int k = 0; k < x.nbeat; k++) x.bts.push_back(first + (uint64_t)k * gap);
      last_beat = x.bts.back();
      busy_until = last_beat + (x.we ? 1 + done_lat : 0);
      if (x.single && x.we) x.unit_n = dev_wn[x.ch]++;
      x.done_t = ~0ull;
      q.push_back(x);
      log.push_back({bt, false, x.we, (bool)t->bq_lock, x.addr, x.size, x.master, x.ch});
      if (g_verbose) printf("  bsc %llu accept %s %08x size %d ch %d master %d lock %d\n", (unsigned long long)bt,
                            x.we ? "W" : "R", x.addr, x.size, x.ch, x.master, (int)t->bq_lock);
      last_acc = bt; any_acc = true;
      lock_hold = t->bq_lock;
    } else if (cpu_on && free && !dma_v && !lock_hold && bt >= cpu_free_at) {
      Txn x{};
      x.cpu = true; x.done_t = bt + cpu_len;
      busy_until = bt + cpu_len;
      last_beat = busy_until;
      cpu_free_at = bt + cpu_len + 1 + cpu_think;
      q.push_back(x);
      log.push_back({bt, true, false, false, 0, 0, 0, 0});
    }
    pv = dma_v && !ready;
    pbits = bits;
  }
};

// ---------------------------------------------------------------------------------------------------------------
// the bridge as the DMAC master sees it
struct P4Stub {
  int lat = 6, jitter = 0;
  Rng *rng = nullptr;
  OnChip *oc = nullptr;
  bool pend = false;
  uint64_t ack_at = 0;
  uint32_t addr = 0, wdata = 0;
  int size = 0;
  bool we = false;
  bool ack = false, err = false;
  uint32_t rdata = 0;
  int count = 0;
  void reset_state() { pend = false; }
  void cycle(Vv1_dmac_tb_top *t, uint64_t now) {
    ack = err = false; rdata = 0;
    if (t->p4q_v && !pend) {
      pend = true; ack_at = now + lat + (jitter && rng ? rng->u(jitter + 1) : 0);
      addr = t->p4q_addr; wdata = t->p4q_wdata; size = t->p4q_size; we = t->p4q_we;
      CHECK(t->p4q_dma, "bridge: DMAC request without dma = 1");
      CHECK((addr >> 24) == 0x1F, "bridge: DMAC on-chip leg address %08x not 0x1Fxxxxxx", addr);
    }
    if (pend) {
      CHECK(t->p4q_v && t->p4q_addr == addr && t->p4q_we == we && t->p4q_size == size && (!we || t->p4q_wdata == wdata),
            "bridge: p4q changed before ack");
      if (now >= ack_at) {
        ack = true;
        bool ok = we ? oc->wr(addr, size, wdata, true, now) : oc->rd(addr, size, rdata, true, now);
        err = !ok;
        pend = false;
        count++;
      }
    }
  }
};

// ---------------------------------------------------------------------------------------------------------------
struct Tb {
  Vv1_dmac_tb_top *t;
  uint64_t cyc = 0;
  Rng rng{1};
  Mem mem;
  OnChip oc;
  BscStub bsc;
  P4Stub p4;
  // the reference model
  ModelBus mb;
  DmacRef model{&mb};
  bool ls = false, strict = true;
  std::vector<int> order;                  // channel of every committed unit
  std::vector<uint64_t> commit_bt;         // Bclk of every commit
  std::vector<uint64_t> fin_bt;            // Bclk of every counter update
  // inputs
  bool run = true, stby = false, nmi = false;
  bool mstp = false;                       // STBCR.MSTP4 (module stop: run = 0 and mstp = 1; deep sleep: run = 0 only)
  bool nmi_nb = false;                     // an nmi_evt pulse in the next non-bce Icyc (not modelled in lockstep)
  bool dbreq_n = true, tr_n = true, ddt_bavl = false;
  uint64_t ddt_d = 0;                      // D63:0 (M6: the DTR on the data bus in the DTR cycle)
  uint32_t dreq_force = 0;                 // extra levels (tmu_ic)
  bool use_oc_levels = true;
  // queued DDT events (driven on a bce, deferred while the DUT commits / updates a unit)
  struct Ev { int kind; uint64_t dtr; };  // 0 dtr, 1 ch2_req, 2 tr_repeat, 3 dtr + ch2_req
  std::deque<Ev> evq;
  bool last_fin = false;
  bool ev_at_upd = false;                  // outside lockstep: drive the next DDT event only in a counter-update Bclk
  // the CPU strobe in flight
  bool cpu_act = false, cpu_wr = false, cpu_dma = false;
  uint32_t cpu_off = 0, cpu_wdata = 0, cpu_rdata = 0;
  int cpu_size = 2;
  bool cpu_done = false, cpu_hit = false;
  int stalls = 0;
  // pins observed
  std::vector<std::pair<uint64_t, uint32_t>> pinlog;   // (Bclk, {ddt_id, tdack_n, bavl_n})
  // (M6-fix) per Bclk: the burst session reported to the BSC and what the engine does in that Bclk
  struct SesE { uint64_t bt; bool ses, bqv, idle, acc, upd; int acc_ch, upd_ch; };
  std::vector<SesE> seslog;
  int dack_ic_n = 0;

  Tb() {
    t = new Vv1_dmac_tb_top;
    bsc.mem = &mem; bsc.rng = &rng;
    p4.oc = &oc; p4.rng = &rng;
    reset();
  }
  ~Tb() { t->final(); delete t; }

  bool bce() const { return (cyc & 1) == 1; }

  void drive_inputs() {
    t->bce = bce();
    t->pce = (cyc & 3) == 3;
    t->run = run;
    t->stby = stby;
    t->mstp = mstp;
    t->pq_rd = cpu_act && !cpu_wr;
    t->pq_wr = cpu_act && cpu_wr;
    t->pq_size = cpu_size;
    t->pq_off = 0xFA00000u | cpu_off;
    t->pq_wdata = cpu_wdata;
    t->pq_dma = cpu_dma;
    t->dbreq_n = dbreq_n;
    t->tr_n = tr_n;
    t->ddt_d = ddt_d;
    t->ddt_bavl = ddt_bavl;
    t->dreq_i = (use_oc_levels ? oc.levels() : 0) | dreq_force;
    t->nmi_evt = (nmi && bce()) || (nmi_nb && !bce());
    t->tl_dtr_v = 0; t->tl_ch2_req = 0; t->tl_tr_repeat = 0; t->tl_dtr = 0;
  }

  // one Icyc
  void tick() {
    t->clk = 0;
    oc.tick(cyc);
    drive_inputs();
    // the stubs' responses: first see the DUT's outputs of this cycle
    t->bs_ready = t->bs_wd_req = t->bs_rd_v = t->bs_rd_last = t->bs_done = 0;
    t->p4r_ack = t->p4r_err = 0;
    t->eval();
    if (bce()) {
      bsc.bce(t);
      t->bs_ready = bsc.ready; t->bs_rd_v = bsc.rd_v; t->bs_rd_last = bsc.rd_last; t->bs_rd_beat = bsc.rd_beat;
      t->bs_rd_data = bsc.rd_data; t->bs_wd_req = bsc.wd_req; t->bs_wd_beat = bsc.wd_beat; t->bs_done = bsc.done;
    }
    p4.cycle(t, cyc);
    t->p4r_ack = p4.ack; t->p4r_err = p4.err; t->p4r_rdata = p4.rdata;
    t->eval();
    bool evdrv = false;
    Ev ev_now{-1, 0};
    // In lockstep a DDT event is not driven in a Bclk where the DUT commits or updates a unit (or right after an
    // update): the RTL applies a DTR at the end of its bce against the pre-bce flag / queue while the model applies
    // events one at a time, so a same-Bclk DTR and pop would need the model to reproduce the RTL's merge order.
    // Outside lockstep (the timing checks) events are driven in the first bce they can be.
    if (bce() && !evq.empty() && (!ls || (!t->t_acc_v && !t->t_upd_v && !last_fin)) &&
        (!ev_at_upd || (!ls && t->t_upd_v))) {
      Ev e = evq.front(); evq.pop_front();
      if (e.kind == 0 || e.kind == 3) { t->tl_dtr_v = 1; t->tl_dtr = e.dtr; }
      if (e.kind == 1 || e.kind == 3) t->tl_ch2_req = 1;
      if (e.kind == 2) t->tl_tr_repeat = 1;
      t->eval();
      evdrv = true;
      ev_now = e;
      ev_at_upd = false;
    }
    if (bce() && g_verbose > 1 && (t->t_pop || t->t_acc_v || t->t_upd_v))
      printf("  rtl %llu en %x hold %x pop %x has %x acc %d/%d upd %d/%d\n", (unsigned long long)bsc.bt, t->t_en,
             t->t_hold, t->t_pop, t->t_has, t->t_acc_v, t->t_acc_ch, t->t_upd_v, t->t_upd_ch);
    if (bce()) {
      if (t->t_acc_v) { order.push_back(t->t_acc_ch); commit_bt.push_back(bsc.bt); }
      if (t->t_upd_v) fin_bt.push_back(bsc.bt);
      pinlog.push_back({bsc.bt, (uint32_t)t->ddt_id << 2 | (uint32_t)t->tdack_n << 1 | t->bavl_n});
      seslog.push_back({bsc.bt, (bool)t->bus_ses, (bool)t->bq_v, (bool)t->t_idle, (bool)t->t_acc_v, (bool)t->t_upd_v,
                        (int)t->t_acc_ch, (int)t->t_upd_ch});
      if (t->dack_ic) { dack_ic_n++; oc.tmu_ic = false; }
    }
    // lockstep order inside one Bclk, as the RTL's flops see it: a CPU read returns the registers as they were at the
    // start of the Bclk (every register is a flop), so the model reads first; then the DUT's unit events of this Bclk
    // (class A, the counter update, the commit), then a DDT event, then a CPU write (the RTL stalls a write that
    // collides with an engine update or a channel-0 load, so a write that lands sees the engine's value already
    // written), then standby's TE clear.  run / standby gate the enables in their own Bclk, an NMI too.
    bool cpu_fin = cpu_act && !t->pr_stall;
    if (cpu_act && t->pr_stall) stalls++;
    if (ls && bce()) {                                     // the levels this Bclk's combinational enables already see
      model.set_running(run);
      model.set_mstp(mstp);
      model.stby = stby;
    }
    if (cpu_fin && ls && !cpu_wr) {
      uint32_t v = 0;
      bool h = model.rd(0xFA00000u | cpu_off, cpu_size, v, cpu_dma);
      CHECKEQ(t->pr_hit, h, "lockstep: register hit");
      if (v != t->pr_rdata) {
        char w[96];
        snprintf(w, sizeof w, "lockstep: register read value (offset %02x size %d, cyc %llu)", cpu_off, cpu_size,
                 (unsigned long long)cyc);
        CHECKEQ(t->pr_rdata, v, w);
      } else g_checks++;
    }
    if (ls && bce()) {
      if (nmi) model.nmi();                                // nmi_evt stops the channels in its own bce (NMIF lands at
      model.bce_pre();                                     // its end: the read above saw the old DMAOR)
      if (t->t_upd_v) {
        CHECKEQ(model.u_ch, t->t_upd_ch, "lockstep: channel of the counter update");
        if (g_verbose) printf("  model %llu finish ch %d sar %08x dar %08x tcr %06x chcr %08x ib %08x\n",
                              (unsigned long long)bsc.bt, model.u_ch, model.u_ch >= 0 ? model.sar[model.u_ch] : 0,
                              model.u_ch >= 0 ? model.dar[model.u_ch] : 0, model.u_ch >= 0 ? model.tcr[model.u_ch] : 0,
                              model.u_ch >= 0 ? model.chcr[model.u_ch] : 0, model.ib_data);
        model.finish();
      }
      // the candidate set in every arbitration Bclk, not only at the commits: a request the RTL never raises (or
      // raises without the model) shows here even when no unit ever starts
      if (strict && t->t_idle) {
        unsigned mc = model.cand();
        if (mc != t->t_cand) {
          char w[96];
          snprintf(w, sizeof w, "lockstep: candidate channels in an IDLE Bclk (Bclk %llu)", (unsigned long long)bsc.bt);
          CHECKEQ(t->t_cand, mc, w);
          ls = false;
        } else g_checks++;
      }
      if (ls && t->t_acc_v) {
        unsigned mc = model.cand();
        if (strict) CHECKEQ(mc, t->t_cand, "lockstep: candidate channels at a commit");
        int mp = model.pick(t->t_cand);
        CHECKEQ(mp, t->t_acc_ch, "lockstep: arbiter pick");
        model.commit(t->t_acc_ch);
        if (g_verbose) printf("  model %llu commit ch %d (cand %x)\n", (unsigned long long)bsc.bt, t->t_acc_ch, t->t_cand);
      }
      if (t->t_rr_idle) model.idle();
      if (evdrv && g_verbose)
        printf("  ddt %llu event %d dtr %016llx (model flag0 %d q %d %d %d, rtl has %x)\n", (unsigned long long)bsc.bt,
               ev_now.kind, (unsigned long long)ev_now.dtr, model.flag0, model.q[1], model.q[2], model.q[3], t->t_has);
      if (evdrv) {                                         // the RTL applies a DDT event at the end of its bce
        if (ev_now.kind == 0 || ev_now.kind == 3) model.dtr(ev_now.dtr);
        if (ev_now.kind == 1 || ev_now.kind == 3) model.ch2_req();
        if (ev_now.kind == 2) model.tr_repeat();
      }
    }
    if (bce()) last_fin = t->t_upd_v;                     // (a Bclk flag: the Icyc after the update keeps it)
    if (cpu_fin) {
      cpu_done = true; cpu_act = false; cpu_rdata = t->pr_rdata; cpu_hit = t->pr_hit;
      if (g_verbose && ls) printf("  cpu %llu %s %02x size %d data %08x\n", (unsigned long long)bsc.bt,
                                  cpu_wr ? "wr" : "rd", cpu_off, cpu_size, cpu_wr ? cpu_wdata : t->pr_rdata);
      if (ls && cpu_wr) {
        bool h = model.wr(0xFA00000u | cpu_off, cpu_size, cpu_wdata, cpu_dma);
        CHECKEQ(t->pr_hit, h, "lockstep: register hit");
      }
    }
    if (ls && bce()) model.standby(stby);                 // standby clears TE at the end of the bce
    nmi = false;
    if (!bce()) nmi_nb = false;
    t->clk = 1;
    t->eval();
    cyc++;
    if (ls && (cyc & 1) == 0) compare_regs();
  }
  void ticks(int n) { for (int k = 0; k < n; k++) tick(); }
  void bclks(int n) { ticks(2 * n); }

  uint32_t treg(int idx) const {
    const uint32_t s[4] = {t->t_sar0, t->t_sar1, t->t_sar2, t->t_sar3}, d[4] = {t->t_dar0, t->t_dar1, t->t_dar2, t->t_dar3};
    const uint32_t c[4] = {t->t_chcr0, t->t_chcr1, t->t_chcr2, t->t_chcr3}, n[4] = {t->t_tcr0, t->t_tcr1, t->t_tcr2, t->t_tcr3};
    if (idx == 16) return t->t_dmaor;
    int ch = idx >> 2;
    switch (idx & 3) { case 0: return s[ch]; case 1: return d[ch]; case 2: return n[ch]; default: return c[ch]; }
  }
  void compare_regs() {
    static const char *nm[4] = {"SAR", "DAR", "DMATCR", "CHCR"};
    for (int i = 0; i <= 16; i++) {
      uint32_t r = treg(i), m = model.regval(i);
      g_checks++;
      if (r != m) {
        if (i == 16) failf("lockstep: DMAOR rtl %08x model %08x (cyc %llu)", r, m, (unsigned long long)cyc);
        else failf("lockstep: %s%d rtl %08x model %08x (cyc %llu)", nm[i & 3], i >> 2, r, m, (unsigned long long)cyc);
        ls = false;                                        // one report per divergence
        return;
      }
    }
    unsigned lv = (model.dmte(0) ? 1u : 0) | (model.dmte(1) ? 2u : 0) | (model.dmte(2) ? 4u : 0) |
                  (model.dmte(3) ? 8u : 0);
    g_checks++;
    if (t->dmte != lv || t->dmae != model.dmae()) {
      failf("lockstep: DMTE rtl %x model %x, DMAE rtl %d model %d (cyc %llu)", t->dmte, lv, t->dmae, model.dmae(),
            (unsigned long long)cyc);
      ls = false;
      return;
    }
    unsigned has = (model.flag0 && !model.srv0 ? 1u : 0) | (model.q[1] ? 2u : 0) | (model.q[2] ? 4u : 0) |
                   (model.q[3] ? 8u : 0);
    unsigned qc = (unsigned)model.q[1] | (unsigned)model.q[2] << 3 | (unsigned)model.q[3] << 6;
    if (t->t_flag0 != model.flag0 || t->t_srv0 != model.srv0) {
      failf("lockstep: DDT channel-0 flag rtl %d (in service %d) model %d (%d) (cyc %llu)", t->t_flag0, t->t_srv0,
            model.flag0, model.srv0, (unsigned long long)cyc);
      ls = false;
      return;
    }
    if (g_verbose > 1) {
      static unsigned last = ~0u;
      unsigned now = t->t_has | has << 4 | (model.hold[0] ? 0x100u : 0) | (model.en(0) ? 0x200u : 0) | t->t_chcr0 << 12;
      if (now != last) printf("  has %llu rtl %x model %x hold0 %d en0 %d chcr0 %05x\n", (unsigned long long)bsc.bt,
                              t->t_has, has, model.hold[0], model.en(0), t->t_chcr0);
      last = now;
    }
    if (has != t->t_has || qc != t->t_qcnt) {
      failf("lockstep: DDT state rtl has %x q %03o model has %x q %03o (cyc %llu; model hold %d%d%d%d en %d%d%d%d)",
            t->t_has, t->t_qcnt, has, qc, (unsigned long long)cyc, model.hold[0], model.hold[1], model.hold[2],
            model.hold[3], model.en(0), model.en(1), model.en(2), model.en(3));
      ls = false;
    }
  }

  // ---- the CPU's register accesses (strobes on bce cycles, held while stall; a Bclk-class gap after each)
  uint32_t access(bool wr, uint32_t off, int size, uint32_t v, bool dma = false) {
    while (!bce() || cyc & 2) tick();                      // present in a bce cycle (the Bclk path's strobe)
    cpu_act = true; cpu_wr = wr; cpu_off = off; cpu_size = size; cpu_wdata = v; cpu_dma = dma; cpu_done = false;
    int guard = 0;
    while (!cpu_done) { tick(); if (++guard > 100000) { failf("CPU access to %02x never completed", off); cpu_act = false; break; } }
    ticks(5);
    return cpu_rdata;
  }
  uint32_t rd(uint32_t off, int size = 2) { return access(false, off, size, 0); }
  void wr(uint32_t off, uint32_t v, int size = 2) { access(true, off, size, v); }
  static uint32_t SAR(int c) { return 0x10u * c; }
  static uint32_t DAR(int c) { return 0x10u * c + 4; }
  static uint32_t TCR(int c) { return 0x10u * c + 8; }
  static uint32_t CHCR(int c) { return 0x10u * c + 0xC; }
  static const uint32_t DMAOR = 0x40;
  void ch_stop(int c) { (void)rd(CHCR(c)); wr(CHCR(c), 0); }

  void reset(bool keep_lockstep = false) {
    t->rst_pin = 1; t->rst_por = 1; t->rst_man = 0; t->rst_cpu = 1;
    for (int k = 0; k < 16; k++) { t->clk = 0; drive_inputs(); t->eval(); t->clk = 1; t->eval(); cyc++; }
    t->rst_pin = 0; t->rst_por = 0; t->rst_cpu = 0;
    model.reset_por();
    bsc.reset_state(); p4.reset_state();
    mb.dev_rx.clear(); mb.dev_wn.clear();
    evq.clear(); cpu_act = false; last_fin = false;
    if (!keep_lockstep) ls = false;
    order.clear(); commit_bt.clear(); fin_bt.clear(); pinlog.clear(); seslog.clear();
  }
  void manual_reset(int icyc = 8) {
    t->rst_man = 1; t->rst_cpu = 1;
    ticks(icyc);
    t->rst_man = 0; t->rst_cpu = 0;
    model.reset_manual();
  }
  // run until the DMAC is idle (no unit, no single cycle, nothing requested) for a while
  bool wait_idle(int max_bclk = 2000000) {
    int quiet = 0;
    for (int k = 0; k < max_bclk; k++) {
      bclks(1);
      bool dma_q = false;                                  // (the CPU competitor's cycles do not count)
      for (auto &x : bsc.q) if (!x.cpu) dma_q = true;
      if (!t->t_busy && t->t_cand == 0 && !dma_q && !p4.pend) { if (++quiet > 8) return true; }
      else quiet = 0;
    }
    failf("DMAC never went idle (busy %d cand %x stub queue %zu bridge %d)", t->t_busy, t->t_cand, bsc.q.size(), p4.pend);
    return false;
  }
  void start_lockstep(bool strict_ = true) {
    // the model starts from the DUT's registers (and the same memory / on-chip state)
    for (int i = 0; i < 4; i++) {
      model.sar[i] = treg(4 * i); model.dar[i] = treg(4 * i + 1); model.tcr[i] = treg(4 * i + 2); model.chcr[i] = treg(4 * i + 3);
    }
    uint32_t o = t->t_dmaor;
    model.ddt = o >> 15 & 1; model.pr = o >> 8 & 3; model.ae = o >> 2 & 1; model.nmif = o >> 1 & 1; model.dme = o & 1;
    mb.mem = mem;
    mb.oc = oc;
    ls = true; strict = strict_;
  }
  void end_lockstep() {
    if (ls) {
      Mem::diff(mem, mb.mem, "lockstep memory");
      for (auto &kv : bsc.dev_rx) {
        auto &m = mb.dev_rx[kv.first];
        CHECK(m == kv.second, "lockstep: device stream of channel %d differs (rtl %zu bytes, model %zu)", kv.first,
              kv.second.size(), m.size());
      }
    }
    ls = false;
  }
};

// ---------------------------------------------------------------------------------------------------------------
// helpers
static const uint32_t BUFA = 0x0C0725C0u, BUFB = 0x0C0625C0u;       // dmac1's buffers (spec 6)
static uint32_t CH_DM(int x) { return (uint32_t)x << 14; }
static uint32_t CH_SM(int x) { return (uint32_t)x << 12; }
static uint32_t CH_RS(int x) { return (uint32_t)x << 8; }
static const uint32_t CH_TM = 1u << 7, CH_IE = 1u << 2, CH_TE = 1u << 1, CH_DE = 1u;
static uint32_t CH_TS(int x) { return (uint32_t)x << 4; }
static uint64_t mkdtr(int sz, int rw, int id, int md, int cnt, uint32_t addr) {
  return (uint64_t)sz << 61 | (uint64_t)rw << 60 | (uint64_t)id << 58 | (uint64_t)md << 56 | (uint64_t)cnt << 48 | addr;
}
static void fillA(Tb &b, uint32_t salt, uint32_t n = 65536) { for (uint32_t i = 0; i < n / 4; i++) b.mem.wr32(BUFA + 4 * i, i ^ (salt << 24)); }
static void fillB(Tb &b, uint8_t v, uint32_t n = 65536) { b.mem.fill(BUFB, n, v); }
static void prog(Tb &b, int ch, uint32_t sar, uint32_t dar, uint32_t n, uint32_t chcr) {
  b.ch_stop(ch);
  b.wr(Tb::SAR(ch), sar); b.wr(Tb::DAR(ch), dar); b.wr(Tb::TCR(ch), n);
  b.wr(Tb::CHCR(ch), chcr);
}
static uint32_t ttcr(Tb &b, int ch) { return b.treg(4 * ch + 2); }
static bool wait_tcr0(Tb &b, int ch, int max_bclk = 4000000) {
  for (int k = 0; k < max_bclk; k++) { if (ttcr(b, ch) == 0) return true; b.bclks(1); }
  failf("channel %d never reached DMATCR 0", ch);
  return false;
}
static void settle_units(Tb &b) { b.wait_idle(); }

// ---------------------------------------------------------------------------------------------------------------
// 1. register access rules: dmac1 S2 / S3 / S8, dmac2 T1 (values from the console logs)
static void t_access(Tb &b) {
  g_test = "access";
  b.reset();
  b.wr(Tb::DMAOR, 0x8201);
  // S2
  b.ch_stop(1);
  b.wr(Tb::SAR(1), 0x0C123458);
  b.wr(Tb::CHCR(1), CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TS(3));
  CHECKEQ(b.rd(Tb::SAR(1), 1), 0, "S2 r16 SAR1");     CHECKEQ(b.rd(Tb::SAR(1) + 2, 1), 0, "S2 r16 SAR1+2");
  CHECKEQ(b.rd(Tb::SAR(1), 0), 0, "S2 r8 SAR1");      CHECKEQ(b.rd(Tb::SAR(1) + 3, 0), 0, "S2 r8 SAR1+3");
  CHECKEQ(b.rd(Tb::CHCR(1), 1), 0, "S2 r16 CHCR1");   CHECKEQ(b.rd(Tb::CHCR(1) + 2, 1), 0, "S2 r16 CHCR1+2");
  CHECKEQ(b.rd(Tb::CHCR(1), 0), 0, "S2 r8 CHCR1");
  CHECKEQ(b.rd(Tb::DMAOR, 1), 0, "S2 r16 DMAOR");     CHECKEQ(b.rd(Tb::DMAOR + 2, 1), 0, "S2 r16 DMAOR+2");
  CHECKEQ(b.rd(Tb::DMAOR, 0), 0, "S2 r8 DMAOR");      CHECKEQ(b.rd(Tb::DMAOR + 1, 0), 0, "S2 r8 DMAOR+1");
  b.access(true, Tb::SAR(1), 1, 0xBEEF);      CHECKEQ(b.rd(Tb::SAR(1)), 0x0C123458, "S2 after w16 SAR1<-BEEF");
  b.access(true, Tb::SAR(1) + 1, 0, 0x55);    CHECKEQ(b.rd(Tb::SAR(1)), 0x0C123458, "S2 after w8 SAR1+1<-55");
  b.access(true, Tb::CHCR(1), 1, 0x0080);     CHECKEQ(b.rd(Tb::CHCR(1)), 0x00005430, "S2 after w16 CHCR1<-0080");
  b.access(true, Tb::SAR(1) + 2, 1, 0x0D00);  CHECKEQ(b.rd(Tb::SAR(1)), 0x0C123458, "S2 after w16 SAR1+2<-0D00");
  CHECKEQ(b.rd(Tb::DMAOR), 0x8201, "S2 DMAOR");
  b.wr(Tb::CHCR(1), 0);
  // S3: masks
  b.ch_stop(1); b.ch_stop(3);
  b.wr(Tb::SAR(1), 0xFFFFFFFF); CHECKEQ(b.rd(Tb::SAR(1)), 0xFFFFFFFF, "S3 SAR1");
  b.wr(Tb::DAR(1), 0xFFFFFFFF); CHECKEQ(b.rd(Tb::DAR(1)), 0xFFFFFFFF, "S3 DAR1");
  b.wr(Tb::TCR(1), 0xFFFFFFFF); CHECKEQ(b.rd(Tb::TCR(1)), 0x00FFFFFF, "S3 DMATCR1");
  b.wr(Tb::CHCR(1), 0xFFFFFFFE); CHECKEQ(b.rd(Tb::CHCR(1)), 0xFF0FFFF4, "S3 CHCR1 w FFFFFFFE");
  b.wr(Tb::CHCR(1), 0); CHECKEQ(b.rd(Tb::CHCR(1)), 0, "S3 CHCR1 w 0");
  b.wr(Tb::CHCR(3), 0xFFFFFFFE); CHECKEQ(b.rd(Tb::CHCR(3)), 0xFF0AFFF4, "S3 CHCR3 w FFFFFFFE"); b.wr(Tb::CHCR(3), 0);
  b.wr(Tb::TCR(1), 0x01000005); CHECKEQ(b.rd(Tb::TCR(1)), 5, "S3 DMATCR1 w 01000005");
  b.wr(Tb::CHCR(2), 0x12C0);
  uint32_t c2 = b.rd(Tb::CHCR(2));
  b.wr(Tb::CHCR(2), 0xFFFFFFFE); CHECKEQ(b.rd(Tb::CHCR(2)), 0xFF0AFFF4, "S3 CHCR2 w FFFFFFFE");
  b.wr(Tb::CHCR(2), c2 & ~CH_TE); CHECKEQ(b.rd(Tb::CHCR(2)), 0x12C0, "S3 CHCR2 restored");
  // channel 0 in DDT mode: preload SAR0 in normal mode, then a maple-like DTR leaves CHCR0 = 853C3, DAR0 = ...EF40
  b.wr(Tb::DMAOR, 0x0201);
  b.wr(Tb::SAR(0), 0x0C08F600);
  b.wr(Tb::DMAOR, 0x8201);
  b.evq.push_back({0, mkdtr(4, 1, 0, 1, 1, 0x0C08EF20)});
  b.bclks(100); settle_units(b);
  CHECKEQ(b.rd(Tb::CHCR(0)), 0x000853C3, "S3/S0 CHCR0 after a maple DTR (32 B, MD 01, write)");
  CHECKEQ(b.rd(Tb::DAR(0)), 0x0C08EF40, "S0 DAR0 after the unit");
  CHECKEQ(b.rd(Tb::SAR(0)), 0x0C08F600, "S0 SAR0 kept by a write DTR");
  CHECKEQ(b.rd(Tb::TCR(0)), 0, "S0 DMATCR0");
  uint32_t s0v = b.rd(Tb::SAR(0)), t0v = b.rd(Tb::TCR(0)), c0v = b.rd(Tb::CHCR(0)), d0v = b.rd(Tb::DAR(0));
  b.wr(Tb::SAR(0), s0v ^ 0x20); CHECKEQ(b.rd(Tb::SAR(0)), 0, "S3 ch0 DDT: SAR0 after a write");
  b.wr(Tb::TCR(0), t0v ^ 1);    CHECKEQ(b.rd(Tb::TCR(0)), 0, "S3 ch0 DDT: DMATCR0 after a write");
  b.wr(Tb::DAR(0), d0v ^ 0x20); CHECKEQ(b.rd(Tb::DAR(0)), 0, "S3 ch0 DDT: DAR0 after a write");
  b.wr(Tb::CHCR(0), c0v ^ CH_IE); CHECKEQ(b.rd(Tb::CHCR(0)), 0x000853C3, "S3 ch0 DDT: CHCR0 write dropped");
  // S3 DMAOR
  uint32_t o = b.rd(Tb::DMAOR);
  CHECKEQ(o, 0x8201, "S3 DMAOR");
  b.wr(Tb::DMAOR, o | 0xFFFF7CF8); CHECKEQ(b.rd(Tb::DMAOR), 0x8201, "S3 DMAOR w |FFFF7CF8"); b.wr(Tb::DMAOR, o);
  b.wr(Tb::DMAOR, o | 6);          CHECKEQ(b.rd(Tb::DMAOR), 0x8201, "S3 DMAOR w |6");
  b.wr(Tb::DMAOR, o);              CHECKEQ(b.rd(Tb::DMAOR), 0x8201, "S3 DMAOR restored");
  b.wr(Tb::DMAOR, o | 0x300);      CHECKEQ(b.rd(Tb::DMAOR), 0x8301, "S3 DMAOR w |300"); b.wr(Tb::DMAOR, o);
  // T1: DDT CPU writes load 0 and stay 0
  b.wr(Tb::TCR(0), 0);          CHECKEQ(b.rd(Tb::TCR(0)), 0, "T1 w DMATCR0=0");
  b.wr(Tb::SAR(0), 0x0C0DEAD0); CHECKEQ(b.rd(Tb::SAR(0)), 0, "T1 w SAR0");
  b.wr(Tb::DAR(0), 0x0C0BEEF0); CHECKEQ(b.rd(Tb::DAR(0)), 0, "T1 w DAR0");
  b.wr(Tb::TCR(0), 7);          CHECKEQ(b.rd(Tb::TCR(0)), 0, "T1 w DMATCR0=7");
  b.ticks(2000);
  CHECKEQ(b.rd(Tb::SAR(0)), 0, "T1 2000 later SAR0"); CHECKEQ(b.rd(Tb::CHCR(0)), 0x000853C3, "T1 CHCR0");
  // holes and the block decode
  uint32_t hv = b.access(false, 0x44, 2, 0); CHECK(!b.cpu_hit && hv == 0, "offset 0x44 must be a hole (hit %d)", b.cpu_hit);
  b.access(false, 0x100, 2, 0);  CHECK(!b.cpu_hit, "offset 0x100 must be a hole");
  b.access(false, 0x3C, 2, 0);   CHECK(b.cpu_hit, "CHCR3 must hit");
  b.access(false, 0x10000, 2, 0); CHECK(!b.cpu_hit, "0xFFA10000 must not mirror SAR0");
  // a DMAC-mastered access to its own block reads 0 [P]
  uint32_t dv = b.access(false, Tb::DMAOR, 2, 0, true); CHECKEQ(dv, 0, "dma=1 read of DMAOR");

  // S8: the TE flag rule
  b.wr(Tb::DMAOR, 0x8201);
  uint32_t cfg = CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TM | CH_TS(4);
  fillA(b, 5); fillB(b, 0xEE);
  b.ch_stop(1);
  b.wr(Tb::SAR(1), BUFA); b.wr(Tb::DAR(1), BUFB); b.wr(Tb::TCR(1), 4);
  b.wr(Tb::CHCR(1), cfg | CH_DE);
  wait_tcr0(b, 1); b.ticks(500);
  b.wr(Tb::TCR(1), 4);
  b.wr(Tb::CHCR(1), cfg | CH_DE);                         // TE written 0 but never read as 1
  b.ticks(3000);
  CHECKEQ(b.rd(Tb::TCR(1)), 4, "S8 (a) DMATCR");  CHECKEQ(b.rd(Tb::CHCR(1)), 0x54C3, "S8 (a) CHCR");
  b.wr(Tb::TCR(1), 4); b.wr(Tb::CHCR(1), cfg | CH_DE); b.ticks(3000);
  CHECKEQ(b.rd(Tb::TCR(1)), 0, "S8 (b) DMATCR");  CHECKEQ(b.rd(Tb::CHCR(1)), 0x54C3, "S8 (b) CHCR");
  b.wr(Tb::TCR(1), 4); b.wr(Tb::CHCR(1), cfg | CH_TE | CH_DE); b.ticks(3000);
  CHECKEQ(b.rd(Tb::TCR(1)), 4, "S8 (c) DMATCR");  CHECKEQ(b.rd(Tb::CHCR(1)), 0x54C3, "S8 (c) CHCR");
  b.wr(Tb::CHCR(1), 0); CHECKEQ(b.rd(Tb::CHCR(1)), 0, "S8 (d) read + write 0");
  b.wr(Tb::TCR(1), 4); b.wr(Tb::CHCR(1), cfg | CH_DE); wait_tcr0(b, 1); b.ticks(500);
  CHECKEQ(b.rd(Tb::CHCR(1)), 0x54C3, "S8 (e) end CHCR");
  b.wr(Tb::CHCR(1), cfg | CH_TE);  CHECKEQ(b.rd(Tb::CHCR(1)), 0x54C2, "S8 (e) write cfg|TE (DE=0)");
  b.wr(Tb::CHCR(1), 0);            CHECKEQ(b.rd(Tb::CHCR(1)), 0, "S8 (e) write 0");
  // disarm on write: a read of TE = 1, then a write with TE = 1, then a write of 0 without a read keeps TE [P]
  b.wr(Tb::TCR(1), 4); b.wr(Tb::CHCR(1), cfg | CH_DE); wait_tcr0(b, 1); b.ticks(500);
  (void)b.rd(Tb::CHCR(1));
  b.wr(Tb::CHCR(1), cfg | CH_TE);
  b.wr(Tb::CHCR(1), 0);
  CHECKEQ(b.rd(Tb::CHCR(1)), 0x2, "arm cleared by any CHCR write [P]");
  b.wr(Tb::CHCR(1), 0); CHECKEQ(b.rd(Tb::CHCR(1)), 0, "TE cleared after a fresh read");
  // AE / NMIF arming (S9 (iii) handler pattern)
  b.wr(Tb::SAR(1), BUFA + 2); b.wr(Tb::DAR(1), BUFB); b.wr(Tb::TCR(1), 4);
  b.wr(Tb::CHCR(1), CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TM | CH_TS(3) | CH_DE);
  b.ticks(100);
  b.wr(Tb::DMAOR, 0x8201);                                  // AE never read: not cleared
  CHECKEQ(b.rd(Tb::DMAOR), 0x8205, "AE not cleared by a write without a read of 1");
  b.ch_stop(1);                                             // clear DE first (spec 3.8)
  b.wr(Tb::DMAOR, 0x8201);
  CHECKEQ(b.rd(Tb::DMAOR), 0x8201, "AE cleared after the read of 1");
  b.nmi = true; b.ticks(4);
  CHECKEQ(b.rd(Tb::DMAOR), 0x8203, "NMIF set by nmi_evt");
  b.wr(Tb::DMAOR, 0x8201 | 2);                               // NMIF written 1: kept; the write disarms it
  b.wr(Tb::DMAOR, 0x8201);        CHECKEQ(b.rd(Tb::DMAOR), 0x8203, "NMIF: the |2 write disarmed it");
  b.wr(Tb::DMAOR, 0x8201);        CHECKEQ(b.rd(Tb::DMAOR), 0x8201, "NMIF cleared after the read of 1");
}

// ---------------------------------------------------------------------------------------------------------------
// 2. address errors: dmac1 S9 (iii)-(v), dmac2 T3
static void t_classes(Tb &b) {
  g_test = "classes";
  b.reset();
  b.wr(Tb::DMAOR, 0x8201);
  b.start_lockstep(true);
  struct Case { const char *tag; uint32_t sar, dar, chcr, n; uint32_t e_or, e_tcr, e_chcr; int32_t e_dd; uint32_t e_dst; bool dst_any; };
  const uint32_t RAM = 0xFFFFFFFF, RAM1 = 0xFFFFFFFE;
  const Case cs[] = {
    {"RS=6 SAR=TSTR byte SM=0 -> RAM", 0x1FD80004, 0, CH_DM(1) | CH_SM(0) | CH_RS(6) | CH_TS(1), 4, 0x8201, 0, 0x4613, 4, 0x05050505, false},
    {"RS=4 SAR=TSTR byte ext->ext", 0x1FD80004, 0, CH_DM(1) | CH_SM(0) | CH_RS(4) | CH_TS(1), 4, 0x8205, 3, 0x4411, 1, 0xEEEEEE11, false},
    {"RS=4 SAR=TOCR long ext->ext", 0x1FD80000, 0, CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TS(3), 4, 0x8205, 3, 0x5431, 4, 0x00005431, false},
    {"RS=6 SAR=RAM long", RAM, 0, CH_DM(1) | CH_SM(1) | CH_RS(6) | CH_TS(3), 4, 0x8205, 3, 0x5631, 4, 0, true},
    {"RS=6 SAR=1F300000 long", 0x1F300000, 0, CH_DM(1) | CH_SM(0) | CH_RS(6) | CH_TS(3), 4, 0x8205, 3, 0x4631, 4, 0, true},
    {"RS=6 SAR=FFD80004 byte", 0xFFD80004, 0, CH_DM(1) | CH_SM(0) | CH_RS(6) | CH_TS(1), 4, 0x8201, 0, 0x4613, 4, 0x05050505, false},
    {"RS=2 SAR=1FD80004 byte", 0x1FD80004, 0, CH_DM(0) | CH_SM(1) | CH_RS(2) | CH_TS(1), 4, 0x8201, 4, 0x1211, 0, 0xEEEEEEEE, false},
    {"RS=2 SAR=RAM+1 long", RAM1, 0, CH_DM(0) | CH_SM(1) | CH_RS(2) | CH_TS(3), 4, 0x8205, 4, 0x1231, 0, 0xEEEEEEEE, false},
    {"RS=4 DAR+4 32B", RAM, 4, CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TS(4), 2, 0x8205, 2, 0x5441, 0, 0xEEEEEEEE, false},
    // dmac1 S9 (iii) / (iv)
    {"S9 (iii) SAR+2 longword", RAM + 3, 0, CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TM | CH_TS(3), 4, 0x8205, 4, 0x54B1, 0, 0xEEEEEEEE, false},
    {"S9 (iv) SAR 1FD80000 longword", 0x1FD80000, 0, CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TM | CH_TS(3), 4, 0x8205, 3, 0x54B1, 4, 0, true},
  };
  for (const Case &c : cs) {
    g_test = std::string("classes: ") + c.tag;
    b.mem.fill(BUFB, 256, 0xEE); b.mb.mem.fill(BUFB, 256, 0xEE);
    for (int i = 0; i < 64; i++) { b.mem.wr32(BUFA + 4 * i, 0xA0000000u + i); b.mb.mem.wr32(BUFA + 4 * i, 0xA0000000u + i); }
    uint32_t sar = c.sar == RAM ? BUFA : c.sar == RAM1 ? BUFA + 1 : c.sar == RAM + 3 ? BUFA + 2 : c.sar;
    uint32_t dar = BUFB + c.dar;
    prog(b, 1, sar, dar, c.n, c.chcr | CH_DE);
    b.ticks(3000);
    uint32_t r_or = b.rd(Tb::DMAOR), r_tcr = b.rd(Tb::TCR(1)), r_chcr = b.rd(Tb::CHCR(1)), r_dar = b.rd(Tb::DAR(1));
    CHECKEQ(r_or, c.e_or, "DMAOR"); CHECKEQ(r_tcr, c.e_tcr, "DMATCR"); CHECKEQ(r_chcr, c.e_chcr, "CHCR");
    CHECKEQ((int32_t)(r_dar - dar), c.e_dd, "DAR - start");
    uint32_t w = b.mem.rd32(BUFB);
    if (c.dst_any) CHECK(w != 0xEEEEEEEE, "the faulting unit writes its (junk) destination, got %08x", w);
    else CHECKEQ(w, c.e_dst, "destination word");
    CHECKEQ(b.mem.rd32(BUFB + 8), 0xEEEEEEEE, "no overrun");
    b.ch_stop(1);
    uint32_t o = b.rd(Tb::DMAOR); if (o & 4) b.wr(Tb::DMAOR, o & ~4u);
    CHECKEQ(b.rd(Tb::DMAOR), 0x8201, "DMAOR after");
  }
  g_test = "classes";
  CHECKEQ(b.oc.tstr, 5, "T3 TSTR now 05"); CHECKEQ(b.oc.tocr, 0, "T3 TOCR 00");
  // S9 (v): misaligned with DE = 0 -> no AE
  b.wr(Tb::SAR(1), BUFA + 2); b.wr(Tb::TCR(1), 4); b.wr(Tb::CHCR(1), CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TM | CH_TS(3));
  b.ticks(500);
  CHECKEQ(b.rd(Tb::DMAOR), 0x8201, "S9 (v) misaligned DE=0: no AE");
  // clearing AE with DE still 1 and the address still bad re-triggers at once (spec 3.8)
  b.wr(Tb::CHCR(1), CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TM | CH_TS(3) | CH_DE);
  b.ticks(50);
  CHECKEQ(b.rd(Tb::DMAOR), 0x8205, "class A with DE = 1");
  b.wr(Tb::DMAOR, 0x8201); b.ticks(4);
  CHECKEQ(b.rd(Tb::DMAOR), 0x8205, "AE re-triggers while DE = 1 and misaligned");
  b.ch_stop(1); (void)b.rd(Tb::DMAOR); b.wr(Tb::DMAOR, 0x8201);
  b.end_lockstep();
}

// ---------------------------------------------------------------------------------------------------------------
// 3. transfers: every TS x SM x DM (dmac1 S4 / S5), DMATCR = 0 (S6), suspend / resume (S7)
static void t_transfers(Tb &b) {
  g_test = "transfers S4";
  b.reset();
  b.wr(Tb::DMAOR, 0x8201);
  b.start_lockstep(true);
  static const int tss[5] = {1, 2, 3, 0, 4}, unit[5] = {1, 2, 4, 8, 32};
  static const uint32_t e_chcr[5] = {0x5493, 0x54A3, 0x54B3, 0x5483, 0x54C3};
  for (int k = 0; k < 5; k++) {
    for (uint32_t i = 0; i < 1024; i++) { b.mem.wr32(BUFA + 4 * i, i ^ (k << 24)); b.mb.mem.wr32(BUFA + 4 * i, i ^ (k << 24)); }
    b.mem.fill(BUFB, 4096, 0xEE); b.mb.mem.fill(BUFB, 4096, 0xEE);
    int n = 64, bytes = n * unit[k];
    prog(b, 1, BUFA, BUFB, n, CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TM | CH_TS(tss[k]) | CH_DE);
    wait_tcr0(b, 1); b.ticks(400);
    CHECKEQ(b.rd(Tb::CHCR(1)), e_chcr[k], "S4 CHCR");
    CHECKEQ(b.rd(Tb::SAR(1)), BUFA + bytes, "S4 SAR");
    CHECKEQ(b.rd(Tb::DAR(1)), BUFB + bytes, "S4 DAR");
    bool same = true; int over = 0;
    for (int i = 0; i < bytes; i++) if (b.mem.rd8(BUFA + i) != b.mem.rd8(BUFB + i)) same = false;
    for (int i = bytes; i < bytes + 64; i++) if (b.mem.rd8(BUFB + i) != 0xEE) over++;
    CHECK(same && over == 0, "S4 TS=%d data_ok=%d overrun=%d", tss[k], same, over);
    b.wr(Tb::CHCR(1), 0);
  }
  // S4 address forms: bits 31:29 are carried and ignored; the area-3 image 32 MB up
  static const uint32_t hi[4][2] = {{0x80000000u, 0xA0000000u}, {0xE0000000u, 0}, {0x60000000u, 0x40000000u}, {0, 0x02000000u}};
  for (int k = 0; k < 4; k++) {
    uint32_t sa = BUFA | hi[k][0], da = k == 3 ? (BUFB ^ 0x02000000u) : (BUFB | hi[k][1]);
    prog(b, 1, sa, da, 4, CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TM | CH_TS(4) | CH_DE);
    wait_tcr0(b, 1); b.ticks(400);
    CHECKEQ(b.rd(Tb::SAR(1)), sa + 128, "S4 addr SAR keeps bits 31:29");
    CHECKEQ(b.rd(Tb::DAR(1)), da + 128, "S4 addr DAR keeps bits 31:29");
    CHECKEQ(b.rd(Tb::DMAOR), 0x8201, "S4 addr no AE");
    bool ok = true;
    for (int i = 0; i < 128; i++) if (b.mem.rd8((da & 0x1FFFFFFF) + i) != b.mem.rd8(BUFA + i)) ok = false;
    CHECK(ok, "S4 addr %08x -> %08x data", sa, da);
    b.wr(Tb::CHCR(1), 0);
  }
  // S5: SM / DM
  g_test = "transfers S5";
  static const char *e_sig[3][3] = {
    {".. .. .. .. .. .. .. .. +0 .. .. .. .. .. .. ..", ".. .. .. .. .. .. .. .. +0 +0 +0 +0 +0 +0 .. ..", ".. .. .. +0 +0 +0 +0 +0 +0 .. .. .. .. .. .. .."},
    {".. .. .. .. .. .. .. .. +5 .. .. .. .. .. .. ..", ".. .. .. .. .. .. .. .. +0 +1 +2 +3 +4 +5 .. ..", ".. .. .. +5 +4 +3 +2 +1 +0 .. .. .. .. .. .. .."},
    {".. .. .. .. .. .. .. .. -5 .. .. .. .. .. .. ..", ".. .. .. .. .. .. .. .. +0 -1 -2 -3 -4 -5 .. ..", ".. .. .. -5 -4 -3 -2 -1 +0 .. .. .. .. .. .. .."}};
  for (int sm = 0; sm < 3; sm++)
    for (int dm = 0; dm < 3; dm++) {
      for (int i = 0; i < 64; i++) {
        b.mem.wr32(BUFA + 4 * i, 0x100u + i); b.mem.wr32(BUFB + 4 * i, 0xEEEEEEEE);
        b.mb.mem.wr32(BUFA + 4 * i, 0x100u + i); b.mb.mem.wr32(BUFB + 4 * i, 0xEEEEEEEE);
      }
      prog(b, 1, BUFA + 128, BUFB + 128, 6, CH_DM(dm) | CH_SM(sm) | CH_RS(4) | CH_TM | CH_TS(3) | CH_DE);
      wait_tcr0(b, 1); b.ticks(400);
      std::string sig;
      for (int i = 24; i < 40; i++) {
        uint32_t v = b.mem.rd32(BUFB + 4 * i);
        char t[16];
        if (v == 0xEEEEEEEE) snprintf(t, sizeof t, "..");
        else snprintf(t, sizeof t, "%+d", (int)v - 0x100 - 32);
        if (!sig.empty()) sig += " ";
        sig += t;
      }
      CHECK(sig == e_sig[sm][dm], "S5 SM=%d DM=%d: %s, expected %s", sm, dm, sig.c_str(), e_sig[sm][dm]);
      int dsa = (int)(b.rd(Tb::SAR(1)) - (BUFA + 128)), dda = (int)(b.rd(Tb::DAR(1)) - (BUFB + 128));
      CHECKEQ(dsa, sm == 0 ? 0 : sm == 1 ? 24 : -24, "S5 SAR - start");
      CHECKEQ(dda, dm == 0 ? 0 : dm == 1 ? 24 : -24, "S5 DAR - start");
      b.wr(Tb::CHCR(1), 0);
    }
  // S6: DMATCR = 0 is 16M units
  g_test = "transfers S6";
  b.ch_stop(1);
  b.bsc.log.clear();
  prog(b, 1, BUFA, BUFB, 0, CH_DM(0) | CH_SM(0) | CH_RS(4) | CH_TS(1) | CH_DE);
  b.ticks(20000);
  uint32_t c1 = b.rd(Tb::CHCR(1));
  b.wr(Tb::CHCR(1), c1 & ~CH_DE);
  b.ticks(200);
  uint32_t tcr = b.rd(Tb::TCR(1)), c2 = b.rd(Tb::CHCR(1));
  uint32_t done = 0;
  for (auto &e : b.bsc.log) if (!e.cpu && e.we && e.addr == BUFB) done++;
  CHECKEQ(c1, 0x411, "S6 CHCR while running"); CHECKEQ(c2, 0x410, "S6 CHCR after DE = 0");
  CHECKEQ(tcr, 0x1000000u - done, "S6 DMATCR = 16M - units done");
  CHECK(done > 100, "S6 units done %u", done);
  CHECKEQ(b.rd(Tb::SAR(1)), BUFA, "S6 SAR fixed"); CHECKEQ(b.rd(Tb::DAR(1)), BUFB, "S6 DAR fixed");
  b.wr(Tb::CHCR(1), 0);
  // S7: suspend with DE = 0, resume
  g_test = "transfers S7";
  for (int rep = 0; rep < 3; rep++) {
    for (uint32_t i = 0; i < 8192; i++) { uint32_t v = (i << 8) | 0x11u | (uint32_t)rep << 4; b.mem.wr32(BUFA + 4 * i, v); b.mb.mem.wr32(BUFA + 4 * i, v); }
    b.mem.fill(BUFB, 32768, 0xEE); b.mb.mem.fill(BUFB, 32768, 0xEE);
    uint32_t cfg = CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TS(3) | (rep == 2 ? CH_TM : 0);
    prog(b, 1, BUFA, BUFB, 8192, cfg | CH_DE);
    b.ticks(3000 + 1777 * rep);
    if (rep == 1) { b.wr(Tb::DMAOR, 0x8200); } else b.wr(Tb::CHCR(1), cfg);
    b.ticks(200);                                          // the unit in flight completes (dmac1 read DMATCR later too)
    uint32_t t1 = b.rd(Tb::TCR(1));
    b.ticks(2000);
    uint32_t t2 = b.rd(Tb::TCR(1)), s = b.rd(Tb::SAR(1)), d = b.rd(Tb::DAR(1)), c = b.rd(Tb::CHCR(1));
    uint32_t ndone = 8192 - t2;
    CHECKEQ(t1, t2, "S7 DMATCR stable while suspended");
    CHECKEQ(s - BUFA, 4 * ndone, "S7 SAR - start = 4 x done");
    CHECKEQ(d - BUFB, 4 * ndone, "S7 DAR - start = 4 x done");
    CHECKEQ(c & CH_TE, 0, "S7 TE not set by a suspension");
    int copied = 0, bad = 0;
    for (uint32_t i = 0; i < 8192; i++) { uint32_t v = b.mem.rd32(BUFB + 4 * i); if (v == b.mem.rd32(BUFA + 4 * i)) copied++; else if (v != 0xEEEEEEEE) bad++; }
    CHECK(copied == (int)ndone && bad == 0, "S7 copied %d bad %d done %u", copied, bad, ndone);
    CHECK(ndone > 20 && ndone < 8192, "S7 suspended mid-way (done %u)", ndone);
    if (rep == 1) b.wr(Tb::DMAOR, 0x8201); else b.wr(Tb::CHCR(1), cfg | CH_DE);
    wait_tcr0(b, 1); b.ticks(300);
    CHECKEQ(b.rd(Tb::CHCR(1)), cfg | CH_TE | CH_DE, "S7 resume: CHCR");
    CHECKEQ(b.rd(Tb::SAR(1)), BUFA + 32768, "S7 resume: SAR");
    bool all = true;
    for (uint32_t i = 0; i < 32768; i++) if (b.mem.rd8(BUFB + i) != b.mem.rd8(BUFA + i)) { all = false; break; }
    CHECK(all, "S7 resume: all words copied");
    b.wr(Tb::CHCR(1), 0);
  }
  b.end_lockstep();
}

// ---------------------------------------------------------------------------------------------------------------
// 4. priority: dmac1 S10 and hm figures 14.3 / 14.4
static void s10(Tb &b, const char *tag, uint32_t pr, bool burst3) {
  g_test = std::string("priority S10 ") + tag;
  b.reset();
  b.wr(Tb::DMAOR, 0x8001 | pr << 8);
  b.start_lockstep(true);
  uint32_t cfg = CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TS(3) | CH_DE;
  b.ch_stop(1); b.ch_stop(3);
  b.wr(Tb::SAR(3), BUFA); b.wr(Tb::DAR(3), BUFB); b.wr(Tb::TCR(3), 600);
  b.wr(Tb::SAR(1), BUFA + 32768); b.wr(Tb::DAR(1), BUFB + 32768); b.wr(Tb::TCR(1), 100);
  b.wr(Tb::CHCR(3), cfg | (burst3 ? CH_TM : 0));
  b.ticks(1500);
  size_t before = b.order.size();
  b.wr(Tb::CHCR(1), cfg);
  wait_tcr0(b, 1, 400000); wait_tcr0(b, 3, 400000);
  settle_units(b);
  std::vector<int> o(b.order.begin() + (long)before, b.order.end());
  // the first unit may already have been committed when the CHCR1 write landed
  size_t i0 = 0; while (i0 < o.size() && o[i0] == 3) i0++;
  if (pr != 1) CHECK(i0 <= 2, "%s: channel 1 started %zu units late", tag, i0);
  if (pr == 2) {                                   // CH2 > CH0 > CH1 > CH3: ch1 runs all its units, ch3 frozen
    bool ok = true;
    for (size_t i = i0; i < i0 + 100 && i < o.size(); i++) if (o[i] != 1) ok = false;
    CHECK(ok && o.size() >= i0 + 100, "%s: channel 1 must run its 100 units back to back", tag);
  } else if (pr == 3) {                            // round robin: strict alternation while both run
    bool ok = true;
    for (size_t i = i0; i + 1 < o.size() && i < i0 + 190; i++) if (o[i] == o[i + 1]) ok = false;
    CHECK(ok, "%s: round robin must alternate 1 / 3", tag);
  } else if (pr == 1) {                            // CH0 > CH2 > CH3 > CH1: ch1 waits for ch3
    size_t first1 = 0; while (first1 < o.size() && o[first1] != 1) first1++;
    bool ok = first1 < o.size() && o.size() - first1 == 100;
    for (size_t i = first1; i < o.size(); i++) if (o[i] != 1) ok = false;
    CHECK(ok, "%s: channel 1 only after channel 3 finished (first at %zu of %zu)", tag, first1, o.size());
  }
  b.end_lockstep();
}
static void t_priority(Tb &b) {
  s10(b, "PR=10", 2, false);
  s10(b, "PR=11", 3, false);
  s10(b, "PR=01", 1, false);
  s10(b, "PR=10 ch3 burst", 2, true);
  // burst ownership: ch3 in burst, ch1 (higher, cycle steal) preempts; the bus is not released between ch1's units
  // because the burst owner still has work (hm 14.3.4, figure 14.11)
  g_test = "priority burst hold";
  {
    int rel = 0;
    for (size_t k = 1; k < b.commit_bt.size(); k++) if (b.commit_bt[k] - b.fin_bt[k - 1] > 0 && b.commit_bt[k] != b.fin_bt[k - 1]) rel++;
    CHECKEQ(rel, 0, "commits of the burst session all at a counter update (no release)");
  }
  // hm figure 14.3 / 14.4 with DDT-queued requests on channels 1-3 and the channel-0 flag.  A single-address unit
  // ends when the BSC accepts its cycle, so the stub's acceptance is slowed to let requests arrive during a unit.
  g_test = "priority figure 14.4";
  b.reset();
  b.wr(Tb::DMAOR, 0x8301);
  b.start_lockstep(true);
  for (int c = 1; c < 4; c++) prog(b, c, BUFA + 0x1000 * c, 0, 50, CH_DM(0) | CH_SM(1) | CH_RS(2) | CH_TS(3) | CH_DE);
  b.bsc.acc_lat = 20;
  size_t base = b.order.size();
  b.evq.push_back({0, mkdtr(2, 0, 0, 3, 5, BUFA)});         // channel 0: init DTR = one request (cycle steal)
  b.evq.push_back({0, mkdtr(0, 0, 3, 0, 0, 0)});            // channel 3, at the same time as far as the arbiter goes
  b.bclks(6);
  b.evq.push_back({0, mkdtr(0, 0, 1, 0, 0, 0)});            // channel 1 during channel 0's unit
  b.bclks(300);
  std::vector<int> o(b.order.begin() + (long)base, b.order.end());
  CHECK(o.size() == 3 && o[0] == 0 && o[1] == 1 && o[2] == 3, "figure 14.4 order 0 1 3, got %zu units: %d %d %d",
        o.size(), o.size() > 0 ? o[0] : -1, o.size() > 1 ? o[1] : -1, o.size() > 2 ? o[2] : -1);
  // after a unit on channel 1 the order is 2>3>0>1; an idle arbitration restores 0>1>2>3, so requests for channels 0
  // and 2 arriving together afterwards are served 0 first (without the idle rule: 2 first)
  b.bsc.acc_lat = 0;
  base = b.order.size();
  b.evq.push_back({0, mkdtr(0, 0, 1, 0, 0, 0)});
  b.bclks(100);
  b.evq.push_back({3, mkdtr(0, 0, 0, 0, 0, 0)});            // a channel-0 request (MD 00) and a direct channel-2 one
  b.bclks(100);
  o.assign(b.order.begin() + (long)base, b.order.end());
  CHECK(o.size() == 3 && o[0] == 1 && o[1] == 0 && o[2] == 2, "idle restores 0>1>2>3: got %zu units: %d %d %d",
        o.size(), o.size() > 0 ? o[0] : -1, o.size() > 1 ? o[1] : -1, o.size() > 2 ? o[2] : -1);
  b.end_lockstep();
  settle_units(b);
}
// ---------------------------------------------------------------------------------------------------------------
// 5. DDT: DTR decode, the channel-0 flag, the queues, TR-only, note 16, halt, CH2-DMA (dmac1 S1 / S13, hm 14.5, H)
static void t_ddt(Tb &b) {
  g_test = "ddt decode";
  b.reset();
  b.wr(Tb::DMAOR, 0x8201);
  b.start_lockstep(true);
  for (int sz = 0; sz <= 4; sz++)
    for (int md = 1; md <= 3; md++)
      for (int rw = 0; rw <= 1; rw++) {
        static const int ts[5] = {1, 2, 3, 0, 4}, ub[5] = {1, 2, 4, 8, 32};
        int cnt = 1 + (sz + md + rw) % 3;
        uint32_t addr = 0x0C081000u + 0x100u * (uint32_t)(sz * 8 + md * 2 + rw);
        uint32_t before_other = rw ? b.rd(Tb::SAR(0)) : b.rd(Tb::DAR(0));
        b.evq.push_back({0, mkdtr(sz, rw, 0, md, cnt, addr)});
        b.bclks(3);
        uint32_t e = (md == 1 ? 1u << 19 : 0) | 0x5000u | ((2u | rw) << 8) | (md != 3 ? 0x80u : 0) | ((uint32_t)ts[sz] << 4) | 1u;
        // level burst / cycle steal: the init DTR is one request; the rest come as TR-only repeats (note 3)
        for (int k = 1; k < cnt && md != 1; k++) { b.bclks(40); b.evq.push_back({2, 0}); }
        b.bclks(80); settle_units(b);
        CHECKEQ(b.rd(Tb::CHCR(0)), e | CH_TE, "CHCR0 after the DTR's units");
        CHECKEQ(b.rd(Tb::TCR(0)), 0, "DMATCR0 after the DTR's units");
        CHECKEQ(rw ? b.rd(Tb::DAR(0)) : b.rd(Tb::SAR(0)), addr + (uint32_t)(cnt * ub[sz]), "the DTR's address register");
        CHECKEQ(rw ? b.rd(Tb::SAR(0)) : b.rd(Tb::DAR(0)), before_other, "the other address register is kept");
      }
  // the measured maple values [HW dmac1 S1]: CHCR0 852C3 for a read DTR, 853C3 for a write
  b.evq.push_back({0, mkdtr(4, 0, 0, 1, 1, 0x0C08F5E0)}); b.bclks(80);
  CHECKEQ(b.rd(Tb::CHCR(0)), 0x000852C3, "S1 CHCR0 read DTR");
  b.evq.push_back({0, mkdtr(4, 1, 0, 1, 1, 0x0C08EF40)}); b.bclks(80);
  CHECKEQ(b.rd(Tb::CHCR(0)), 0x000853C3, "S1 CHCR0 write DTR");
  CHECKEQ(b.rd(Tb::SAR(0)), 0x0C08F600, "S1 SAR0 = list + 32 kept across a write DTR");
  CHECKEQ(b.rd(Tb::DAR(0)), 0x0C08EF60, "S1 DAR0 = receive + 32");
  // an init DTR while the channel-0 flag is set (edge burst in flight) is ignored (H.2); the halt too (note 9)
  g_test = "ddt flag";
  b.bsc.rd_lat = 30;
  b.evq.push_back({0, mkdtr(3, 0, 0, 1, 20, 0x0C090000)});
  b.bclks(6);
  b.evq.push_back({0, mkdtr(2, 1, 0, 3, 7, 0x0C0A0000)});
  b.evq.push_back({0, mkdtr(7, 0, 0, 1, 0, 0)});
  b.bclks(40);
  b.bsc.rd_lat = 5;
  settle_units(b);
  CHECKEQ(b.rd(Tb::SAR(0)), 0x0C090000 + 20 * 8, "edge burst ran the whole count");
  // DS (MD 01) | DM 01 | SM 01 | RS 0010 | TM | TS 000 (quad) | TE | DE; the cycle-steal write DTR would give 0x5331
  CHECKEQ(b.rd(Tb::CHCR(0)), 0x00085283, "the second DTR (and the halt) was ignored");
  // halt (SZ 111) between units of a cycle-steal channel-0 transfer
  g_test = "ddt halt";
  b.evq.push_back({0, mkdtr(2, 0, 0, 3, 5, 0x0C0B0000)});
  b.bclks(60);
  b.evq.push_back({2, 0}); b.bclks(60);
  b.evq.push_back({0, mkdtr(7, 0, 0, 3, 0, 0)}); b.bclks(20);
  b.evq.push_back({2, 0}); b.bclks(80);
  CHECKEQ(b.rd(Tb::TCR(0)), 3, "halt: two units done, three left");
  CHECKEQ(b.rd(Tb::CHCR(0)) & 3, 0, "halt: DE cleared, TE not set");
  CHECKEQ(b.rd(Tb::SAR(0)), 0x0C0B0008, "halt: registers kept");
  // queues: four entries, the fifth ignored; TR-only repeat; note-16 clear
  g_test = "ddt queues";
  prog(b, 1, 0x0C0C0000, 0, 50, CH_DM(0) | CH_SM(1) | CH_RS(2) | CH_TS(3));     // DE = 0
  for (int k = 0; k < 5; k++) b.evq.push_back({0, mkdtr(0, 0, 1, 0, 0, 0)});
  b.bclks(20);
  CHECKEQ(b.t->t_qcnt & 7, 4, "queue 1 holds four");
  b.wr(Tb::CHCR(1), CH_DM(0) | CH_SM(1) | CH_RS(2) | CH_TS(3) | CH_DE);
  b.bclks(120);
  CHECKEQ(b.rd(Tb::TCR(1)), 46, "four requests, four units");
  b.evq.push_back({2, 0}); b.evq.push_back({2, 0}); b.bclks(60);
  CHECKEQ(b.rd(Tb::TCR(1)), 44, "TR-only repeats the last channel");
  b.wr(Tb::CHCR(1), CH_DM(0) | CH_SM(1) | CH_RS(2) | CH_TS(3));   // DE = 0 again (CHCR was not re-read: TE is 0)
  for (int k = 0; k < 3; k++) b.evq.push_back({0, mkdtr(0, 0, 3, 0, 0, 0)});
  b.evq.push_back({1, 0});
  b.bclks(20);
  CHECKEQ(b.t->t_qcnt, (1u << 3) | (3u << 6), "queues 2 / 3 before the clear");
  b.evq.push_back({0, mkdtr(6, 0, 0, 2, 0, 0)});
  b.bclks(8);
  CHECKEQ(b.t->t_qcnt, 0, "note 16 clears the queues");
  // CH2-DMA [HW dmac1 S13]: CHCR2 12C1, one direct request per 32 bytes
  g_test = "ddt ch2";
  struct C2 { uint32_t tcr, reqs, e_tcr, e_chcr, e_dsar; };
  const C2 cs[] = {{64, 64, 0, 0x12C3, 2048}, {64, 32, 32, 0x12C1, 1024}, {16, 20, 0, 0x12C3, 512}};
  for (const C2 &c : cs) {
    b.ch_stop(2);
    b.wr(Tb::SAR(2), BUFA); b.wr(Tb::DAR(2), 0x0C0FFEE0); b.wr(Tb::TCR(2), c.tcr); b.wr(Tb::CHCR(2), 0x12C1);
    size_t d0 = b.bsc.dev_rx[2].size();
    uint32_t sent = 0;
    for (int k = 0; k < 4000 && sent < c.reqs; k++) {             // Holly keeps at most four outstanding
      if (((b.t->t_qcnt >> 3) & 7) + (b.evq.size()) < 4) { b.evq.push_back({1, 0}); sent++; }
      b.bclks(1);
    }
    b.bclks(100); settle_units(b);
    CHECKEQ(b.rd(Tb::TCR(2)), c.e_tcr, "S13 DMATCR2");
    CHECKEQ(b.rd(Tb::CHCR(2)), c.e_chcr, "S13 CHCR2");
    CHECKEQ(b.rd(Tb::SAR(2)) - BUFA, c.e_dsar, "S13 SAR2 - start");
    CHECKEQ(b.rd(Tb::DAR(2)), 0x0C0FFEE0, "S13 DAR2 unchanged (single address)");
    CHECKEQ(b.bsc.dev_rx[2].size() - d0, c.e_dsar, "S13 bytes delivered to the device");
    bool ok = b.bsc.dev_rx[2].size() >= d0 + c.e_dsar;
    for (uint32_t k = 0; ok && k < c.e_dsar; k++) if (b.bsc.dev_rx[2][d0 + k] != b.mem.rd8(BUFA + k)) ok = false;
    CHECK(ok, "S13 device bytes = the source");
    if (c.tcr < c.reqs) CHECKEQ((b.t->t_qcnt >> 3) & 7, c.reqs - c.tcr, "requests left queued behind TE");
    b.evq.push_back({0, mkdtr(6, 0, 0, 2, 0, 0)}); b.bclks(4);
  }
  // pins: DBREQ# alone -> ddt_bus_req -> BAVL# one Bclk; DBREQ# + TR# -> a channel-2 request; TR# alone -> repeat.
  // The model sees only transaction-level events, so the lockstep ends here.
  b.end_lockstep();
  g_test = "ddt pins";
  b.ch_stop(2);
  b.wr(Tb::CHCR(2), 0x12C0);
  b.dbreq_n = false; b.bclks(1); b.dbreq_n = true;
  bool breq = false; for (int k = 0; k < 6; k++) { b.bclks(1); breq |= b.t->ddt_bus_req; }
  CHECK(breq, "DBREQ# alone raises ddt_bus_req");
  b.ddt_bavl = true; size_t pl = b.pinlog.size(); b.bclks(3); b.ddt_bavl = false;
  int bav = 0; for (size_t k = pl; k < b.pinlog.size(); k++) bav += !(b.pinlog[k].second & 1);
  CHECKEQ(bav, 1, "BAVL# asserted for one Bclk");
  CHECK(!b.t->ddt_bus_req, "ddt_bus_req drops at BAVL");
  unsigned q2 = (b.t->t_qcnt >> 3) & 7;
  while (b.cyc & 1) b.tick();
  b.dbreq_n = false; b.tr_n = false; b.bclks(1); b.dbreq_n = true; b.tr_n = true; b.bclks(4);
  CHECKEQ((b.t->t_qcnt >> 3) & 7, q2 + 1, "DBREQ# + TR#: one channel-2 request");
  // TDACK / ID with the data beats of a single-address unit
  b.wr(Tb::TCR(2), 1); b.wr(Tb::SAR(2), BUFA); b.wr(Tb::CHCR(2), 0x12C1);
  pl = b.pinlog.size();
  b.bclks(40);
  int td = 0; for (size_t k = pl; k < b.pinlog.size(); k++) if (!(b.pinlog[k].second & 2)) { td++; CHECKEQ(b.pinlog[k].second >> 2, 2, "ID = channel 2 with TDACK#"); }
  CHECKEQ(td, 4, "TDACK# on the four data beats of a 32-byte unit");
}

// ---------------------------------------------------------------------------------------------------------------
// 6. interrupt levels (dmac1 S9, dmac2 T2), standby, module stop, resets
static void t_irq_power(Tb &b) {
  g_test = "irq";
  b.reset();
  b.wr(Tb::DMAOR, 0x8201);
  uint32_t cfg = CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TM | CH_TS(3);
  prog(b, 1, BUFA, BUFB, 4, cfg | CH_IE | CH_DE);
  wait_tcr0(b, 1); b.ticks(200);
  CHECKEQ(b.t->dmte, 2, "S9 (i) DMTE1 with TE & IE");
  CHECKEQ(b.rd(Tb::CHCR(1)), 0x54B7, "S9 (i) CHCR");
  b.wr(Tb::CHCR(1), 0);                                     // read above: clears TE, IE, DE
  CHECKEQ(b.t->dmte, 0, "DMTE drops");
  prog(b, 1, BUFA, BUFB, 4, cfg | CH_DE);
  wait_tcr0(b, 1); b.ticks(200);
  CHECKEQ(b.t->dmte, 0, "S9 (ii) IE = 0: no DMTE");
  CHECKEQ(b.rd(Tb::CHCR(1)), 0x54B3, "S9 (ii) CHCR before");
  b.wr(Tb::TCR(1), 4);
  b.wr(Tb::CHCR(1), cfg | CH_TE | CH_IE | CH_DE);
  CHECKEQ(b.rd(Tb::CHCR(1)), 0x54B7, "S9 (ii) IE set after TE");
  CHECKEQ(b.t->dmte, 2, "S9 (ii) DMTE is a level");
  CHECKEQ(b.rd(Tb::TCR(1)), 4, "TE = 1 blocks the channel");
  b.wr(Tb::CHCR(1), cfg | CH_TE | CH_IE);                   // T2: DE = 0
  CHECKEQ(b.rd(Tb::CHCR(1)), 0x54B6, "T2 CHCR"); CHECKEQ(b.t->dmte, 2, "T2 DMTE with DE = 0");
  b.wr(Tb::CHCR(1), cfg | CH_TE);                           // clearing IE alone drops the request (KOS handler)
  CHECKEQ(b.t->dmte, 0, "IE cleared: no DMTE"); CHECKEQ(b.rd(Tb::CHCR(1)) & CH_TE, CH_TE, "TE kept");
  b.wr(Tb::CHCR(1), 0);
  // DMAE = AE
  prog(b, 1, BUFA + 2, BUFB, 4, cfg | CH_DE); b.ticks(40);
  CHECKEQ(b.t->dmae, 1, "S9 (iii) DMAE with AE");
  b.ch_stop(1); (void)b.rd(Tb::DMAOR); b.wr(Tb::DMAOR, 0x8201);
  CHECKEQ(b.t->dmae, 0, "DMAE drops with AE");
  // standby clears TE (and so DMTE)
  g_test = "power";
  prog(b, 1, BUFA, BUFB, 4, cfg | CH_IE | CH_DE); wait_tcr0(b, 1); b.ticks(100);
  CHECKEQ(b.t->dmte, 2, "DMTE before standby");
  b.stby = true; b.ticks(20); b.stby = false; b.ticks(4);
  CHECKEQ(b.t->dmte, 0, "standby cleared TE");
  CHECKEQ(b.rd(Tb::CHCR(1)), 0x54B5, "standby: CHCR with TE cleared, the rest held");
  b.wr(Tb::CHCR(1), 0);
  // module stop (run = dmac_run = 0 and mstp = 1): no unit starts, writes are dropped, reads work, DMTE / DMAE
  // forced 0 (the standby above cleared TE with DE still 1, so the channel ran on from DMATCR 0 until the CHCR write)
  settle_units(b);
  uint32_t sar_held = b.rd(Tb::SAR(1));
  b.run = false; b.mstp = true;
  b.wr(Tb::SAR(1), 0x12345678);
  CHECKEQ(b.rd(Tb::SAR(1)), sar_held, "module stop: a write is dropped, the read returns the held value");
  b.run = true; b.mstp = false;
  prog(b, 1, BUFA, BUFB, 100, cfg | CH_DE);
  b.ticks(200);
  b.run = false; b.mstp = true;
  size_t n0 = b.order.size();
  b.ticks(2000);
  CHECK(b.order.size() <= n0, "module stop: no new unit (%zu -> %zu)", n0, b.order.size());
  b.run = true; b.mstp = false;
  wait_tcr0(b, 1);
  CHECKEQ(b.rd(Tb::CHCR(1)), cfg | CH_TE | CH_DE, "the transfer finishes after the stop");
  // manual reset: CHCR / DMAOR 0, SAR / DAR / DMATCR held; a unit in flight completes without TE
  prog(b, 1, BUFA, BUFB, 1, cfg | CH_DE);
  b.bsc.rd_lat = 30;
  for (int k = 0; k < 400 && !b.t->t_busy; k++) b.tick();
  b.ticks(6);
  b.manual_reset();
  b.bsc.rd_lat = 5;
  b.bclks(100);
  CHECKEQ(b.rd(Tb::CHCR(1)), 0, "manual reset: CHCR 0, no TE from the unit in flight");
  CHECKEQ(b.rd(Tb::DMAOR), 0, "manual reset: DMAOR 0");
  CHECKEQ(b.rd(Tb::TCR(1)), 0, "manual reset: the unit in flight completed (DMATCR 1 -> 0)");
  CHECKEQ(b.rd(Tb::SAR(1)), BUFA + 4, "manual reset: SAR held and advanced");
  // a manual reset in the middle of a burst session: the unit on the bus completes (counters consistent), no TE, and
  // no unit starts during or after the reset (SOC.md 2.3)
  int late = 0;
  for (int rep = 0; rep < 48; rep++) {                      // the reset's first cycle swept over a unit's length
    b.wr(Tb::DMAOR, 0x8201);
    prog(b, 1, BUFA, BUFB, 100, cfg | CH_DE);
    b.ticks(300 + rep);
    size_t n0 = b.order.size();
    b.manual_reset(8 + (rep & 3));
    b.bclks(200);
    late += (int)(b.order.size() - n0);
    uint32_t t1 = b.rd(Tb::TCR(1)), s1 = b.rd(Tb::SAR(1)), d1 = b.rd(Tb::DAR(1)), c1 = b.rd(Tb::CHCR(1));
    b.bclks(1000);
    CHECKEQ(b.rd(Tb::TCR(1)), t1, "manual reset mid-burst: no unit afterwards");
    CHECKEQ(c1, 0, "manual reset mid-burst: CHCR 0, no TE");
    CHECK(t1 > 0 && t1 < 100, "manual reset mid-burst: stopped mid-way (DMATCR %u)", t1);
    CHECKEQ(s1 - BUFA, 4 * (100 - t1), "manual reset mid-burst: SAR = start + 4 x done");
    CHECKEQ(d1 - BUFB, 4 * (100 - t1), "manual reset mid-burst: DAR = start + 4 x done");
    CHECKEQ(b.rd(Tb::DMAOR), 0, "manual reset mid-burst: DMAOR 0");
  }
  CHECKEQ(late, 0, "manual reset: units accepted at or after the reset's first cycle");
  b.reset();
  CHECKEQ(b.rd(Tb::SAR(1)), 0, "power-on reset: SAR 0 [P]");
}

// ---------------------------------------------------------------------------------------------------------------
// 7. normal-mode external requests (DREQ0# = dbreq_n, DREQ1# = tr_n), DRAK / DACK and their polarities
static void t_extreq(Tb &b) {
  g_test = "dreq level";
  b.reset();
  b.wr(Tb::DMAOR, 0x0201);
  b.start_lockstep(false);
  // channel 0: dual, level, cycle steal, DACK in the read cycle (AM 0), active high (AL 0, RL 0)
  uint32_t cfg = CH_DM(1) | CH_SM(1) | CH_RS(0) | CH_TS(3);
  prog(b, 0, BUFA, BUFB, 50, cfg | CH_DE);
  b.bclks(10);
  CHECKEQ(b.order.size(), 0, "no unit without DREQ");
  while (!(b.cyc & 1)) b.tick();
  b.tick();                                                  // the pin changes in a non-bce Icyc
  b.dbreq_n = false;
  b.model.dreq_level(0, true);
  uint64_t s = b.bsc.bt + 1;                                 // the first bce that samples it low
  for (int k = 0; k < 200 && b.bsc.log.empty(); k++) b.tick();
  CHECK(!b.bsc.log.empty(), "a level DREQ started a unit");
  if (!b.bsc.log.empty()) CHECKEQ(b.bsc.log[0].bt - s, 2, "level DREQ: sampling edge -> request on the BSC port (Bclk)");
  b.bclks(200);
  b.dbreq_n = true; b.model.dreq_level(0, false);
  b.bclks(60);
  uint32_t n = 50 - b.rd(Tb::TCR(0));
  CHECK(n > 3 && n < 50, "units while DREQ0# was low: %u", n);
  int drak = 0, dack = 0;
  for (auto &p : b.pinlog) { drak += p.second & 1; dack += (p.second >> 1) & 1; }
  CHECKEQ(drak, n, "DRAK0 one Bclk per accepted request (active high)");
  CHECKEQ(dack, n, "DACK0 with each read beat (AM 0, active high)");
  b.ch_stop(0);
  // polarity RL = AL = 1 (active low), AM = 1: DACK in the write cycle
  g_test = "dreq edge";
  b.pinlog.clear();
  prog(b, 0, BUFA, BUFB, 50, cfg | (1u << 19) | (1u << 18) | (1u << 17) | (1u << 16) | CH_DE);   // DS RL AM AL
  b.pinlog.clear();                                          // (the idle level of an active-low pin is 1 from here)
  b.bclks(4);
  for (int e = 0; e < 5; e++) {                              // five falling edges -> five units (cycle steal)
    b.dbreq_n = false; b.bclks(3); b.dbreq_n = true; b.bclks(40);
  }
  CHECKEQ(b.rd(Tb::TCR(0)), 45, "one unit per DREQ falling edge");
  b.dbreq_n = false; b.bclks(300); b.dbreq_n = true; b.bclks(40);   // a long low level after the edge: still one unit
  CHECKEQ(b.rd(Tb::TCR(0)), 44, "edge mode: a low level held for 300 Bclk is one request");
  int drak_l = 0, dack_l = 0;
  for (auto &p : b.pinlog) { drak_l += !(p.second & 1); dack_l += !((p.second >> 1) & 1); }
  CHECKEQ(drak_l, 6, "DRAK0 active low (RL)");
  CHECKEQ(dack_l, 6, "DACK0 active low in the write cycle (AL, AM)");
  b.ch_stop(0);
  // edge latency: one Bclk more than level
  size_t l0 = b.bsc.log.size();
  prog(b, 0, BUFA, BUFB, 3, cfg | (1u << 19) | CH_DE);
  while (!(b.cyc & 1)) b.tick();
  b.tick();
  b.dbreq_n = false; s = b.bsc.bt + 1;
  for (int k = 0; k < 200 && b.bsc.log.size() == l0; k++) b.tick();
  if (b.bsc.log.size() > l0) CHECKEQ(b.bsc.log[l0].bt - s, 3, "edge DREQ: sampling edge -> request (Bclk)");
  b.bclks(4); b.dbreq_n = true; b.bclks(60);
  b.ch_stop(0);
  // edge burst: one edge runs the whole count, DRAK once
  g_test = "dreq edge burst";
  b.pinlog.clear();
  prog(b, 0, BUFA, BUFB, 12, cfg | (1u << 19) | CH_TM | CH_DE);
  b.dbreq_n = false; b.bclks(3); b.dbreq_n = true;
  b.bclks(600);
  CHECKEQ(b.rd(Tb::TCR(0)), 0, "edge burst: the whole count");
  int drak_b = 0; for (auto &p : b.pinlog) drak_b += p.second & 1;
  CHECKEQ(drak_b, 1, "edge burst: DRAK once");
  b.ch_stop(0);
  // level burst stops when DREQ goes high
  g_test = "dreq level burst";
  prog(b, 0, BUFA, BUFB, 200, cfg | CH_TM | CH_DE);
  b.dbreq_n = false; b.bclks(150); b.dbreq_n = true; b.bclks(60);
  uint32_t lb = b.rd(Tb::TCR(0));
  CHECK(lb > 0 && lb < 200, "level burst stopped with DREQ high (DMATCR %u)", lb);
  b.bclks(200);
  CHECKEQ(b.rd(Tb::TCR(0)), lb, "and stays stopped");
  b.ch_stop(0);
  // channel 1 on DREQ1# (tr_n), single address memory -> device: DACK1 on ddt_id[0] with every beat
  g_test = "dreq ch1 single";
  b.pinlog.clear();
  prog(b, 1, BUFA, 0, 4, CH_DM(0) | CH_SM(1) | CH_RS(2) | CH_TS(4) | CH_DE);
  b.tr_n = false; b.bclks(200); b.tr_n = true; b.bclks(40);
  CHECKEQ(b.rd(Tb::TCR(1)), 0, "channel 1 on DREQ1#");
  int dk1 = 0; for (auto &p : b.pinlog) dk1 += (p.second >> 2) & 1;
  CHECKEQ(dk1, 16, "DACK1 with every beat of four 32-byte single units");
  CHECKEQ(b.bsc.dev_rx[1].size(), 128, "device got 128 bytes");
  // channels 2 / 3 accept no external request in normal mode
  prog(b, 2, BUFA, BUFB, 4, CH_DM(1) | CH_SM(1) | CH_RS(0) | CH_TS(3) | CH_DE);
  b.dbreq_n = false; b.tr_n = false; b.bclks(100); b.dbreq_n = true; b.tr_n = true;
  CHECKEQ(b.rd(Tb::TCR(2)), 4, "channel 2: no external request in normal mode");
  b.end_lockstep();
}

// ---------------------------------------------------------------------------------------------------------------
// 8. on-chip module requests: SCI TX, SCIF RX, TMU input capture (burst / cycle steal) and dack_ic
static void t_onchip(Tb &b) {
  g_test = "onchip sci tx";
  b.reset();
  b.wr(Tb::DMAOR, 0x8201);
  b.oc.timed = true; b.oc.tx_delay = 90; b.oc.rx_delay = 70;
  for (int i = 0; i < 40; i++) b.mem.wr8(BUFA + i, (uint8_t)(0x30 + i));
  prog(b, 1, BUFA, 0x1FE0000C, 40, CH_DM(0) | CH_SM(1) | CH_RS(8) | CH_TS(1) | CH_DE);
  wait_tcr0(b, 1, 100000); b.bclks(100);
  CHECKEQ(b.oc.sci_tx.size(), 40, "SCI TX: 40 bytes to SCTDR1");
  bool ok = b.oc.sci_tx.size() == 40;
  for (int i = 0; ok && i < 40; i++) if (b.oc.sci_tx[i] != 0x30 + i) ok = false;
  CHECK(ok, "SCI TX bytes in order");
  CHECKEQ(b.rd(Tb::CHCR(1)) & CH_TE, CH_TE, "SCI TX: TE");
  b.wr(Tb::CHCR(1), 0);
  g_test = "onchip scif rx";
  for (int i = 0; i < 24; i++) b.oc.scif_rxq.push_back((uint8_t)(0x80 + i));
  b.mem.fill(BUFB, 64, 0xEE);
  prog(b, 2, 0x1FE80014, BUFB, 24, CH_DM(1) | CH_SM(0) | CH_RS(11) | CH_TS(1) | CH_DE);
  wait_tcr0(b, 2, 100000); b.bclks(100);
  ok = true;
  for (int i = 0; i < 24; i++) if (b.mem.rd8(BUFB + i) != 0x80 + i) ok = false;
  CHECK(ok, "SCIF RX: 24 bytes from SCFRDR2 in order");
  CHECKEQ(b.mem.rd8(BUFB + 24), 0xEE, "SCIF RX: no extra byte");
  b.wr(Tb::CHCR(2), 0);
  g_test = "onchip tmu";
  b.oc.timed = false;
  prog(b, 3, BUFA, BUFB, 6, CH_DM(1) | CH_SM(1) | CH_RS(12) | CH_TS(3) | CH_DE);
  int ic0 = b.dack_ic_n;
  for (int e = 0; e < 3; e++) { b.oc.tmu_ic = true; b.bclks(80); }
  CHECKEQ(b.rd(Tb::TCR(3)), 3, "TMU cycle steal: one unit per capture");
  CHECKEQ(b.dack_ic_n - ic0, 3, "dack_ic per unit");
  b.ch_stop(3);
  prog(b, 3, BUFA, BUFB, 6, CH_DM(1) | CH_SM(1) | CH_RS(12) | CH_TM | CH_TS(3) | CH_DE);
  ic0 = b.dack_ic_n;
  b.oc.tmu_ic = true; b.bclks(300);
  CHECKEQ(b.rd(Tb::TCR(3)), 0, "TMU burst: one capture runs the whole count");
  CHECKEQ(b.dack_ic_n - ic0, 1, "dack_ic once, at the last unit");
  b.ch_stop(3);
}

// ---------------------------------------------------------------------------------------------------------------
// 9. timing structure (with a fixed-latency BSC stub)
static std::vector<uint64_t> periods(const std::vector<uint64_t> &v, size_t skip) {
  std::vector<uint64_t> p;
  for (size_t k = skip + 1; k < v.size(); k++) p.push_back(v[k] - v[k - 1]);
  return p;
}
static void t_timing(Tb &b) {
  g_test = "timing burst vs cycle steal";
  uint64_t per[2] = {0, 0};
  for (int tm = 1; tm >= 0; tm--) {
    b.reset();
    b.wr(Tb::DMAOR, 0x8201);
    prog(b, 1, BUFA, BUFB, 40, CH_DM(1) | CH_SM(1) | CH_RS(4) | (tm ? CH_TM : 0) | CH_TS(4) | CH_DE);
    wait_tcr0(b, 1); settle_units(b);
    auto p = periods(b.fin_bt, 2);
    bool flat = true; for (auto x : p) if (x != p[0]) flat = false;
    CHECK(flat && !p.empty(), "steady unit period (tm %d)", tm);
    per[tm] = p.empty() ? 0 : p[0];
  }
  // stub: read 5 + 4 beats, write 2 + 4 beats + done 1 -> 16 Bclk of BSC; the DMAC adds RDW->WR 1 and fin->RD 1 (+2
  // REL + IDLE in cycle steal)
  CHECKEQ(per[1], 17, "burst: Bclk per 32-byte dual unit with the stub (15 BSC + 2 DMAC)");
  CHECKEQ(per[0] - per[1], 2, "cycle steal - burst = REL + IDLE = 2 Bclk (measured +2.1 Bcyc, dmac1 S10)");
  // two channels in round robin, cycle steal: the same period as one (the release is per unit, not per channel)
  g_test = "timing round robin";
  b.reset();
  b.wr(Tb::DMAOR, 0x8301);
  prog(b, 1, BUFA, BUFB, 20, CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TS(4));
  prog(b, 3, BUFA + 0x2000, BUFB + 0x2000, 20, CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TS(4));
  b.wr(Tb::DMAOR, 0x8300);
  b.wr(Tb::CHCR(1), CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TS(4) | CH_DE);
  b.wr(Tb::CHCR(3), CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TS(4) | CH_DE);
  b.order.clear(); b.fin_bt.clear();
  b.wr(Tb::DMAOR, 0x8301);
  settle_units(b);
  auto p = periods(b.fin_bt, 1);
  bool flat = !p.empty(); for (auto x : p) if (x != per[0]) flat = false;
  CHECK(flat, "round robin cycle steal: every unit %llu Bclk like a lone channel", (unsigned long long)per[0]);
  bool alt = true; for (size_t k = 1; k < b.order.size(); k++) if (b.order[k] == b.order[k - 1]) alt = false;
  CHECK(alt && b.order.size() == 40, "round robin alternates (%zu units)", b.order.size());
  // a CPU competitor: cycle steal gives strict alternation, burst keeps the bus
  for (int tm = 0; tm < 2; tm++) {
    g_test = tm ? "timing CPU vs burst" : "timing CPU vs cycle steal";
    b.reset();
    b.wr(Tb::DMAOR, 0x8201);
    b.bsc.cpu_on = true; b.bsc.cpu_len = 8;
    b.bsc.log.clear();
    prog(b, 1, BUFA, BUFB, 30, CH_DM(1) | CH_SM(1) | CH_RS(4) | (tm ? CH_TM : 0) | CH_TS(3) | CH_DE);
    wait_tcr0(b, 1);
    b.bsc.cpu_on = false;
    settle_units(b);
    std::string pat;
    size_t first = 0, last = 0;
    for (size_t k = 0; k < b.bsc.log.size(); k++) if (!b.bsc.log[k].cpu) { if (!first) first = k + 1; last = k; }
    for (size_t k = first - 1; k <= last; k++) pat += b.bsc.log[k].cpu ? 'C' : b.bsc.log[k].we ? 'w' : 'r';
    if (tm) CHECK(pat.find('C') == std::string::npos, "burst: no CPU cycle inside the burst (%s)", pat.c_str());
    else {
      std::string want;
      for (int k = 0; k < 30; k++) want += k ? "Crw" : "rw";
      CHECK(pat == want, "cycle steal: one CPU cycle between units (%s)", pat.c_str());
    }
  }
  // start latencies: a CHCR write / a DTR -> the request on the BSC port 2 Bclk later
  g_test = "timing start";
  b.reset();
  b.wr(Tb::DMAOR, 0x8201);
  b.ch_stop(1); b.wr(Tb::SAR(1), BUFA); b.wr(Tb::DAR(1), BUFB); b.wr(Tb::TCR(1), 1);
  b.bsc.log.clear();
  while (!(b.bce() && !(b.cyc & 2))) b.tick();
  b.cpu_act = true; b.cpu_wr = true; b.cpu_off = Tb::CHCR(1); b.cpu_size = 2; b.cpu_wdata = CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TS(3) | CH_DE;
  b.cpu_dma = false; b.cpu_done = false;
  while (!b.cpu_done) b.tick();
  uint64_t w = b.bsc.bt;
  b.bclks(20);
  if (!b.bsc.log.empty()) CHECKEQ(b.bsc.log[0].bt - w, 2, "CHCR write strobe -> first request (Bclk)");
  b.bsc.log.clear();
  b.evq.push_back({0, mkdtr(2, 0, 0, 3, 1, BUFA)});
  uint64_t d0 = b.bsc.bt;
  b.bclks(20);
  if (!b.bsc.log.empty()) CHECKEQ(b.bsc.log[0].bt - (d0 + 1), 2, "DTR -> first request (Bclk)");
  // single-address pipelining (CH2-DMA): the BSC's data beats, not the DMAC, bound the rate
  g_test = "timing single pipelining";
  b.ch_stop(2);
  b.wr(Tb::SAR(2), BUFA); b.wr(Tb::TCR(2), 64); b.wr(Tb::CHCR(2), 0x12C1);
  b.bsc.log.clear();
  for (int k = 0; k < 4; k++) b.evq.push_back({1, 0});
  int sent = 4;
  for (int k = 0; k < 3000 && ttcr(b, 2) != 0; k++) {
    if (sent < 64 && ((b.t->t_qcnt >> 3) & 7) + b.evq.size() < 4) { b.evq.push_back({1, 0}); sent++; }
    b.bclks(1);
  }
  settle_units(b);
  std::vector<uint64_t> acc;
  for (auto &e : b.bsc.log) if (!e.cpu) acc.push_back(e.bt);
  auto sp = periods(acc, 4);
  double mean = 0; for (auto x : sp) mean += (double)x; mean /= sp.empty() ? 1 : (double)sp.size();
  CHECK(acc.size() == 64 && mean > 3.9 && mean < 4.3, "pipelined 32-byte singles every %.2f Bclk (4 data beats)", mean);
}

// ---------------------------------------------------------------------------------------------------------------
// 10. regressions of the independent verification (agents/reports/units/WI-DMAC-verify.md F1-F6; the repros are the
// verifier's V1 / V2 / V11 / V12 and its stubq / vd_int data cases, rewritten here)

// F1: 1 / 2 / 4 / 8 / 32-byte dual units at every quadword of a line, both ends of a quadword, burst and cycle steal,
// with each of the stub's quadword service shapes.  The stub reports rd_beat = addr[4:3] (SOC.md 3.3.2), so a DMAC that
// reads rd_beat != 0 as "the second half of a split quadword" loses the low lanes (the destination gets the previous
// unit's buffer).  In strict lockstep (memory compared with the model at the end).
static void fix_rdbeat(Tb &b) {
  static const int tss[5] = {1, 2, 3, 0, 4}, ub[5] = {1, 2, 4, 8, 32};
  for (int qm = 0; qm < 2; qm++) {
    b.reset();
    b.bsc.qmode = qm;
    b.wr(Tb::DMAOR, 0x8201);
    b.start_lockstep(true);
    for (int tm = 1; tm >= 0; tm--)
      for (int k = 0; k < 5; k++)
        for (int q = 0; q < 4; q++)
          for (int end = 0; end < 2; end++) {
            if (ub[k] == 32 && (q || end)) continue;
            if (ub[k] == 8 && end) continue;
            char tag[96];
            snprintf(tag, sizeof tag, "fix F1 rd_beat: qmode %d %s TS=%d quadword %d %s", qm, tm ? "burst" : "steal",
                     tss[k], q, end ? "high end" : "low end");
            g_test = tag;
            const int n = 6;
            uint32_t off = ub[k] >= 8 ? 0 : (end ? 8u - (uint32_t)ub[k] : 0);
            uint32_t src = 0x0C100000u + (ub[k] == 32 ? 0 : 8u * (uint32_t)q + off);
            uint32_t dst = 0x0C200000u + (ub[k] == 32 ? 0 : 8u * (uint32_t)((q + 1) & 3) + off);
            for (int i = -64; i < 448; i++) {
              uint8_t v = (uint8_t)(0x40 + i * 7 + q * 3 + k);
              b.mem.wr8(src + i, v); b.mb.mem.wr8(src + i, v);
              b.mem.wr8(dst + i, 0xEE); b.mb.mem.wr8(dst + i, 0xEE);
            }
            prog(b, 1, src, dst, n, CH_DM(1) | CH_SM(1) | CH_RS(4) | (tm ? CH_TM : 0) | CH_TS(tss[k]) | CH_DE);
            wait_tcr0(b, 1); settle_units(b);
            int bad = 0, first = -1;
            for (int i = 0; i < n * ub[k]; i++) if (b.mem.rd8(dst + i) != b.mem.rd8(src + i)) { bad++; if (first < 0) first = i; }
            CHECK(bad == 0, "%d of %d destination bytes differ from the source (first +%d: %02x, want %02x)", bad,
                  n * ub[k], first, first >= 0 ? b.mem.rd8(dst + first) : 0, first >= 0 ? b.mem.rd8(src + first) : 0);
            CHECKEQ(b.mem.rd8(dst + n * ub[k]), 0xEE, "no overrun");
            CHECKEQ(b.mem.rd8(dst - 1), 0xEE, "no underrun");
            b.wr(Tb::CHCR(1), 0);
          }
    b.end_lockstep();
  }
  b.bsc.qmode = 0;
}

// F3: a DDT channel-0 request is the flag, and it stays set until its unit ends (hm H.2, 14.5.2 note 9).  The unit's
// single cycle waits 30 Bclk for the BSC; DTRs arriving meanwhile are refused.  Strict lockstep with the model.
static void fix_ddt0(Tb &b) {
  const uint32_t A1 = 0x0C081000u, A2 = 0x0C082000u;
  for (int md = 2; md <= 3; md++) {                          // level burst and cycle steal: one request per unit
    g_test = md == 3 ? "fix F3 ddt0 cycle steal" : "fix F3 ddt0 level burst";
    b.reset();
    b.wr(Tb::DMAOR, 0x8201);
    b.start_lockstep(true);
    // (a) the verifier's V1: an initialisation DTR during the unit of the previous one is ignored
    b.bsc.acc_lat = 30;
    b.evq.push_back({0, mkdtr(2, 0, 0, md, 1, A1)});        // longword, memory -> device, COUNT 1
    size_t n0 = b.order.size();
    for (int k = 0; k < 20 && b.order.size() == n0; k++) b.bclks(1);
    CHECKEQ(b.order.size(), n0 + 1, "the first DTR's unit was committed");
    b.bclks(3);
    CHECK(b.t->t_busy && !b.t->t_upd_v, "the unit is still in flight");
    CHECK(b.t->t_flag0 && b.t->t_srv0 && !(b.t->t_has & 1), "during the unit the flag is set and in service (no request)");
    b.evq.push_back({0, mkdtr(2, 0, 0, md, 3, A2)});        // a second init DTR, COUNT 3: refused (flag set)
    b.bclks(4);
    CHECKEQ(b.treg(0), A1, "SAR0 not reloaded by a DTR during the unit");
    CHECKEQ(ttcr(b, 0), 1, "DMATCR0 not reloaded by a DTR during the unit");
    b.bsc.acc_lat = 0;
    settle_units(b);
    CHECKEQ(b.rd(Tb::SAR(0)), A1 + 4, "SAR0 = A1 + one unit");
    CHECKEQ(b.rd(Tb::TCR(0)), 0, "DMATCR0 = 0: the first DTR's unit counted against its own count");
    CHECKEQ(b.rd(Tb::CHCR(0)) & 3, 3, "TE, DE");
    CHECKEQ(b.order.size(), n0 + 1, "one unit: the refused DTR started nothing");
    CHECK(b.bsc.dev_rx[0].size() == 4 && b.bsc.dev_rx[0][0] == b.mem.rd8(A1), "the device got A1's longword");
    CHECK(!b.t->t_flag0, "the flag clears at the unit's end");
    // (b) after the unit an init DTR is taken again
    b.evq.push_back({0, mkdtr(2, 0, 0, md, 3, A2)});
    b.bclks(30);
    CHECKEQ(b.rd(Tb::SAR(0)), A2 + 4, "a DTR after the unit's end is taken: one unit from A2");
    CHECKEQ(b.rd(Tb::TCR(0)), 2, "DMATCR0 3 -> 2");
    // (c) a TR-only repeat during the unit is ignored; one after it asks for the next unit
    b.bsc.acc_lat = 30;
    b.evq.push_back({2, 0});
    for (int k = 0; k < 20 && !b.t->t_busy; k++) b.bclks(1);
    b.bclks(3);
    b.evq.push_back({2, 0});                                // during the unit: ignored
    b.bclks(4);
    CHECK(b.t->t_busy && b.t->t_srv0, "the TR-only came during the unit");
    b.bsc.acc_lat = 0;
    b.bclks(60); settle_units(b);
    CHECKEQ(b.rd(Tb::TCR(0)), 1, "TR-only: one unit, the repeat during it ignored");
    CHECKEQ(b.rd(Tb::SAR(0)), A2 + 8, "SAR0 after the second unit");
    CHECK(!b.t->t_flag0, "no request left");
    b.evq.push_back({2, 0});
    b.bclks(60); settle_units(b);
    CHECKEQ(b.rd(Tb::TCR(0)), 0, "TR-only after the unit: the last unit");
    CHECKEQ(b.rd(Tb::SAR(0)), A2 + 12, "SAR0 after the third unit");
    // (d) note 16 clears the flag during a unit; an init DTR in the same unit is still refused (a unit in flight)
    b.bsc.acc_lat = 30;
    b.evq.push_back({0, mkdtr(2, 0, 0, md, 1, A1 + 0x100)});
    for (int k = 0; k < 20 && !b.t->t_busy; k++) b.bclks(1);
    b.bclks(3);
    b.evq.push_back({0, mkdtr(6, 0, 0, 2, 0, 0)});          // note 16
    b.bclks(3);
    CHECK(!b.t->t_flag0 && b.t->t_busy, "note 16 cleared the flag, the unit is still in flight");
    b.evq.push_back({0, mkdtr(2, 0, 0, md, 3, A2 + 0x100)});
    b.bclks(4);
    CHECKEQ(b.treg(0), A1 + 0x100, "an init DTR during a channel-0 unit is refused even with the flag clear");
    b.bsc.acc_lat = 0;
    settle_units(b);
    CHECKEQ(b.rd(Tb::SAR(0)), A1 + 0x104, "SAR0 after the in-flight unit");
    CHECKEQ(b.rd(Tb::TCR(0)), 0, "DMATCR0 after the in-flight unit");
    b.evq.push_back({0, mkdtr(2, 0, 0, md, 1, A2 + 0x100)});
    b.bclks(30); settle_units(b);
    CHECKEQ(b.rd(Tb::SAR(0)), A2 + 0x104, "an init DTR after the unit is taken");
    b.end_lockstep();
    // (e) the unit's end Bclk is still "during the transfer": a TR-only repeat or an init DTR driven in the very Bclk of
    // the counter update is ignored (the flag clears at the end of that Bclk) [P]
    for (int kind = 0; kind < 2; kind++) {
      b.evq.push_back({0, mkdtr(2, 0, 0, md, 4, A1 + 0x200)});
      b.bclks(40); settle_units(b);
      uint32_t t0 = b.rd(Tb::TCR(0)), s0 = b.rd(Tb::SAR(0));
      CHECKEQ(t0, 3, "boundary: one unit of the COUNT 4 DTR");
      b.evq.push_back({2, 0});                              // the next unit
      b.bclks(1);
      if (kind == 0) b.evq.push_back({2, 0});               // a TR-only in that unit's update Bclk
      else b.evq.push_back({0, mkdtr(2, 0, 0, md, 9, A2 + 0x200)});
      b.ev_at_upd = true;
      b.bclks(60); settle_units(b);
      CHECK(!b.ev_at_upd, "the event was driven in the update Bclk");
      CHECKEQ(b.rd(Tb::TCR(0)), 2, kind ? "an init DTR in the update Bclk is ignored" : "a TR-only in the update Bclk is ignored");
      CHECKEQ(b.rd(Tb::SAR(0)), s0 + 4, "SAR0: one more unit");
      b.ev_at_upd = false; b.evq.clear();
    }
  }
}

// F4: hm 14.3.4 "the bus is not released to the CPU until channel 1 transfer ends", whether the preempting higher
// channel runs in cycle steal or burst; with a third, even higher burst channel on top.  The BSC stub's CPU competitor
// takes any gap; no CPU cycle may fall inside the lowest burst channel's session.
static void fix_preempt(Tb &b) {
  for (int v = 0; v < 3; v++) {
    g_test = v == 0 ? "fix F4 preempt: ch0 cycle steal over ch1 burst" : v == 1 ? "fix F4 preempt: ch0 burst over ch1 burst"
                                                                          : "fix F4 preempt: ch0 over ch1 over ch2 bursts";
    b.reset();
    b.wr(Tb::DMAOR, 0x0001);                                // PR = 00: 0 > 1 > 2 > 3 (normal mode: CHCR0 writable)
    b.start_lockstep(true);
    int low = v == 2 ? 2 : 1;
    uint32_t cl = CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TM | CH_TS(3);
    prog(b, low, BUFA, BUFB, 60, cl);
    if (v == 2) prog(b, 1, BUFA + 0x2000, BUFB + 0x2000, 10, cl);
    uint32_t c0 = CH_DM(1) | CH_SM(1) | CH_RS(4) | (v ? CH_TM : 0) | CH_TS(3);
    prog(b, 0, BUFA + 0x4000, BUFB + 0x4000, 8, c0);
    b.bsc.cpu_on = true; b.bsc.cpu_len = 8;
    b.bsc.log.clear();
    b.wr(Tb::CHCR(low), cl | CH_DE);
    b.bclks(120);
    if (v == 2) { b.wr(Tb::CHCR(1), cl | CH_DE); b.bclks(40); }
    b.wr(Tb::CHCR(0), c0 | CH_DE);
    wait_tcr0(b, low);
    b.bclks(60);
    b.bsc.cpu_on = false; settle_units(b);
    std::string pat; size_t first = 0, last = 0; bool any = false;
    for (size_t k = 0; k < b.bsc.log.size(); k++)
      if (!b.bsc.log[k].cpu && b.bsc.log[k].ch == low) { if (!any) first = k; last = k; any = true; }
    for (size_t k = first; any && k <= last; k++) pat += b.bsc.log[k].cpu ? 'C' : (char)('0' + b.bsc.log[k].ch);
    size_t c = pat.find('C');
    CHECK(any && c == std::string::npos, "a CPU cycle inside channel %d's burst session: ...%s...", low,
          c == std::string::npos ? "" : pat.substr(c > 12 ? c - 12 : 0, 30).c_str());
    CHECK(pat.find('0') != std::string::npos, "channel 0 ran inside the session");
    bool cpu_after = false;                                 // the bus is released at the session's end
    for (size_t k = last + 1; k < b.bsc.log.size(); k++) if (b.bsc.log[k].cpu) cpu_after = true;
    CHECK(cpu_after, "the CPU gets the bus after the session");
    b.end_lockstep();
  }
  // the session is the bus tenure: a burst channel that has not run since the bus was taken does not hold it for a
  // cycle-steal channel, even if it held an earlier session (the mask is cleared at every release)
  g_test = "fix F4 session: a released burst channel holds nothing";
  b.reset();
  b.wr(Tb::DMAOR, 0x0001);
  b.start_lockstep(true);
  uint32_t cb = CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TM | CH_TS(3), cc = CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TS(3);
  prog(b, 1, BUFA, BUFB, 5, cb | CH_DE);                  // an earlier burst session of channel 1
  wait_tcr0(b, 1); settle_units(b);
  prog(b, 0, BUFA + 0x4000, BUFB + 0x4000, 12, cc);
  prog(b, 1, BUFA, BUFB, 20, cb);
  b.bsc.cpu_on = true; b.bsc.cpu_len = 8;
  b.wr(Tb::DMAOR, 0x0000);                                // both enabled at once with DME
  b.wr(Tb::CHCR(1), cb | CH_DE); b.wr(Tb::CHCR(0), cc | CH_DE);
  b.bsc.log.clear();
  b.wr(Tb::DMAOR, 0x0001);
  wait_tcr0(b, 0); wait_tcr0(b, 1);
  b.bsc.cpu_on = false; settle_units(b);
  {
    std::string pat;
    for (auto &e : b.bsc.log) pat += e.cpu ? 'C' : (char)('0' + e.ch);
    size_t l0 = pat.rfind('0');
    int cpu_in0 = 0;
    for (size_t k = pat.find('0'); k != std::string::npos && k < l0; k++) cpu_in0 += pat[k] == 'C';
    CHECK(cpu_in0 >= 10, "channel 0 (cycle steal) releases the bus after each unit while channel 1 waits: %d CPU cycles "
          "(%s)", cpu_in0, pat.substr(0, 80).c_str());
  }
  b.end_lockstep();
}

// F5: SOC.md 3.3.2 -- bq.r is held until ready: a single-address request waits 40 Bclk for the BSC while the CPU
// switches DMAOR.DDT; bq.r.master (DACK vs DDT) is the unit's, latched at the commit (the stub checks bq.r)
static void fix_master(Tb &b) {
  for (int from_ddt = 0; from_ddt < 2; from_ddt++) {
    g_test = from_ddt ? "fix F5 master held (DDT -> normal)" : "fix F5 master held (normal -> DDT)";
    b.reset();
    uint32_t c1 = CH_DM(0) | CH_SM(1) | CH_RS(2) | CH_TS(3);
    b.wr(Tb::DMAOR, from_ddt ? 0x8201u : 0x0201u);
    prog(b, 1, BUFA, 0, 4, c1);
    b.bsc.acc_lat = 40;
    b.wr(Tb::CHCR(1), c1 | CH_DE);
    if (from_ddt) b.evq.push_back({0, mkdtr(0, 0, 1, 0, 0, 0)}); else b.tr_n = false;
    for (int k = 0; k < 40 && !b.t->bq_v; k++) b.bclks(1);
    CHECK(b.t->bq_v, "a single-address request is waiting on the BSC port");
    int m0 = b.t->bq_master;
    CHECKEQ(m0, from_ddt ? 3 : 2, "master code of the waiting request");
    b.wr(Tb::DMAOR, from_ddt ? 0x0201u : 0x8201u);
    CHECK(!b.t->bq_v || b.t->bq_master == m0, "bq.r.master changed from %d to %d while bq.v waited", m0, b.t->bq_master);
    b.bclks(60);
    b.tr_n = true; b.bsc.acc_lat = 0;
    b.ch_stop(1); settle_units(b);
    bool seen = false;
    for (auto &e : b.bsc.log) if (!e.cpu) { seen = true; CHECKEQ(e.master, m0, "accepted with the committed master"); break; }
    CHECK(seen, "the request was accepted");
  }
}

// F6: hm 14.3.6 / spec 3.6 -- AE / NMIF drop the pending DDT requests (channel 1-3 queues, the channel-0 flag) and
// none is accepted while they are set.  Strict lockstep with the model.
static void fix_ddt_halt(Tb &b) {
  for (int v = 0; v < 3; v++) {
    g_test = v == 0 ? "fix F6 queue across AE" : v == 1 ? "fix F6 queue across NMIF" : "fix F6 channel-0 flag across NMIF";
    b.reset();
    b.wr(Tb::DMAOR, 0x8201);
    b.start_lockstep(true);
    uint32_t c1 = CH_DM(0) | CH_SM(1) | CH_RS(2) | CH_TS(3);
    prog(b, 1, BUFA, 0, 10, c1);                            // DE = 0
    if (v < 2) {
      b.evq.push_back({0, mkdtr(0, 0, 1, 0, 0, 0)}); b.evq.push_back({0, mkdtr(0, 0, 1, 0, 0, 0)});
      b.bclks(10);
      CHECKEQ(b.t->t_qcnt & 7, 2, "two requests queued for channel 1");
    } else {
      b.wr(Tb::DMAOR, 0x8200);                              // DME = 0: the init DTR loads channel 0 and waits
      b.evq.push_back({0, mkdtr(2, 0, 0, 3, 4, BUFA)});
      b.bclks(10);
      CHECK(b.t->t_flag0, "the channel-0 flag is set");
    }
    if (v == 0) {
      prog(b, 3, BUFA + 2, BUFB, 4, CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TS(3) | CH_DE);   // class A on channel 3
      b.bclks(20);
      CHECKEQ(b.rd(Tb::DMAOR) & 4, 4, "AE");
      b.ch_stop(3);
    } else {
      b.nmi = true; b.bclks(4);
      CHECKEQ(b.rd(Tb::DMAOR) & 2, 2, "NMIF");
    }
    CHECK(b.t->t_qcnt == 0 && !b.t->t_flag0, "the DDT requests were dropped");
    b.evq.push_back({0, mkdtr(0, 0, 1, 0, 0, 0)});          // a request while the flag is set: not accepted
    b.evq.push_back({1, 0});
    b.bclks(10);
    CHECKEQ(b.t->t_qcnt, 0, "no request accepted while AE / NMIF is set");
    uint32_t o = b.rd(Tb::DMAOR); b.wr(Tb::DMAOR, (o & ~6u) | 1u);
    CHECKEQ(b.rd(Tb::DMAOR) & 7, 1, "AE / NMIF cleared, DME = 1");
    b.wr(Tb::CHCR(1), c1 | CH_DE);
    b.bclks(200);
    CHECKEQ(b.rd(Tb::TCR(1)), 10, "no unit on channel 1 from requests of before the halt");
    if (v == 2) CHECKEQ(b.rd(Tb::TCR(0)), 4, "no unit on channel 0 from the flag of before the halt");
    b.evq.push_back({0, mkdtr(0, 0, 1, 0, 0, 0)});          // a request issued again runs
    b.bclks(60);
    CHECKEQ(b.rd(Tb::TCR(1)), 9, "a re-issued request runs");
    b.ch_stop(1); settle_units(b);
    b.end_lockstep();
  }
}

// M1b-verify F2 (agents/reports/integration/M1b-verify.md; the SoC repro agents/verify/M1b/adv dmadslp): a unit in
// flight when `run` falls (STBCR.MSTP4, deep sleep) still completes its handshakes, and its end must take the channel
// out of service -- else the channel's on-chip request stays masked until a reset.  (a) SCIF TX (RS 1010, byte, cycle
// steal: KOS's setup) with the unit's write leg held in the bridge when run falls, 1 / 5 / 9 Bclk after the 3rd
// commit: deep sleep, then the transfer goes on after the wake; MSTP4, then a reprogrammed 16-byte transfer runs (SOC.md
// 2.4).  (b) a DDT channel-0 unit ending while run = 0 clears the flag it put in service (lockstep: the flag and its
// in-service state are compared after every Bclk), and a later initialisation DTR is taken.
static void fix_stop_inflight(Tb &b) {
  for (int v = 0; v < 2; v++)
    for (int d = 1; d <= 9; d += 4) {
      const bool mstp = v == 1;
      char nm[96];
      snprintf(nm, sizeof nm, "fix M1b-F2 on-chip unit in flight at %s (+%d Bclk)", mstp ? "MSTP4" : "deep sleep", d);
      g_test = nm;
      b.reset();
      b.oc = OnChip();
      b.oc.timed = true; b.oc.tx_delay = 90;
      b.p4.lat = 40;                                       // the write leg waits in the bridge: the unit is in flight
      b.wr(Tb::DMAOR, 0x8201);
      for (int i = 0; i < 16; i++) b.mem.wr8(BUFA + i, (uint8_t)(0x40 + i));
      prog(b, 3, BUFA, 0x1FE8000C, 16, CH_DM(0) | CH_SM(1) | CH_RS(10) | CH_TS(1) | CH_DE);
      for (int k = 0; k < 100000 && b.order.size() < 3; k++) b.tick();
      b.bclks(d);
      CHECK(b.t->t_busy, "a unit is in flight when run falls");
      size_t n1 = b.order.size();
      b.run = false; b.mstp = mstp;
      for (int k = 0; k < 4000 && b.t->t_busy; k++) b.tick();
      CHECK(!b.t->t_busy, "the unit in flight completes while run = 0");
      b.bclks(400);
      CHECKEQ(b.order.size(), n1, "no unit starts while run = 0");
      b.run = true; b.mstp = false; b.ticks(8);
      uint8_t base = 0x40;
      if (mstp) {                                          // SOC.md 2.4: the module-stopped DMAC is reprogrammed
        b.ch_stop(3); settle_units(b);                   // (the old transfer resumed with run: stop it, drain it)
        b.oc.scif_tx.clear();
        base = 0x80;
        for (int i = 0; i < 16; i++) b.mem.wr8(BUFA + 0x100 + i, (uint8_t)(base + i));
        prog(b, 3, BUFA + 0x100, 0x1FE8000C, 16, CH_DM(0) | CH_SM(1) | CH_RS(10) | CH_TS(1) | CH_DE);
      }
      bool done = false;
      for (int k = 0; k < 20000 && !done; k++) { b.bclks(1); done = ttcr(b, 3) == 0; }
      CHECK(done, "the channel requests again after run returns (DMATCR3 %u)", ttcr(b, 3));
      b.bclks(100);
      bool ok = b.oc.scif_tx.size() == 16;
      for (size_t i = 0; ok && i < 16; i++) if (b.oc.scif_tx[i] != base + i) ok = false;
      CHECK(ok, "SCFTDR2 got the 16 bytes once each, in order (got %zu)", b.oc.scif_tx.size());
      b.ch_stop(3);
      b.p4.lat = 6;
    }
  for (int v = 0; v < 2; v++) {
    const bool mstp = v == 1;
    g_test = mstp ? "fix M1b-F2 ddt0 unit in flight at MSTP4" : "fix M1b-F2 ddt0 unit in flight at deep sleep";
    const uint32_t A1 = 0x0C081000u, A2 = 0x0C082000u;
    b.reset();
    b.wr(Tb::DMAOR, 0x8201);
    b.start_lockstep(true);
    b.bsc.acc_lat = 30;
    b.evq.push_back({0, mkdtr(2, 0, 0, 3, 1, A1)});        // cycle steal, longword, memory -> device, COUNT 1
    size_t n0 = b.order.size();
    for (int k = 0; k < 40 && b.order.size() == n0; k++) b.bclks(1);
    CHECKEQ(b.order.size(), n0 + 1, "the DTR's unit was committed");
    b.bclks(2);
    CHECK(b.t->t_busy && b.t->t_flag0 && b.t->t_srv0, "the unit is in flight, the flag set and in service");
    b.run = false; b.mstp = mstp;
    for (int k = 0; k < 4000 && b.t->t_busy; k++) b.tick();
    CHECK(!b.t->t_busy, "the unit completes while run = 0");
    b.bclks(20);
    CHECK(!b.t->t_flag0 && !b.t->t_srv0, "the flag cleared at the unit's end (flag %d in service %d)", b.t->t_flag0,
          b.t->t_srv0);
    b.bsc.acc_lat = 0;
    b.run = true; b.mstp = false; b.ticks(8);
    b.evq.push_back({0, mkdtr(2, 0, 0, 3, 1, A2)});
    b.bclks(60); settle_units(b);
    CHECKEQ(b.rd(Tb::SAR(0)), A2 + 4, "a later initialisation DTR is taken: one unit from A2");
    b.end_lockstep();
  }
}

static void t_fixes(Tb &b) {
  fix_rdbeat(b);
  fix_ddt0(b);
  fix_preempt(b);
  fix_master(b);
  fix_ddt_halt(b);
  fix_stop_inflight(b);
}

// ---------------------------------------------------------------------------------------------------------------
// 11. coverage the verifier's mutation campaign found missing (its V3-V10, rewritten here, and five more)
static void cov_regs(Tb &b) {                                // byte / word writes do not disarm; dma reads do not arm
  g_test = "cover registers";
  b.reset();
  b.wr(Tb::DMAOR, 0x8201);
  uint32_t cfg = CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TM | CH_TS(3);
  prog(b, 1, BUFA, BUFB, 2, cfg | CH_DE); wait_tcr0(b, 1); b.ticks(200);
  (void)b.rd(Tb::CHCR(1));
  b.access(true, Tb::CHCR(1), 1, 0);
  b.access(true, Tb::CHCR(1) + 3, 0, 0);
  b.wr(Tb::CHCR(1), 0);
  CHECKEQ(b.rd(Tb::CHCR(1)), 0, "a TE read, ignored byte / word writes, then a longword 0 clears TE");
  prog(b, 1, BUFA, BUFB, 2, cfg | CH_DE); wait_tcr0(b, 1); b.ticks(200);
  (void)b.access(false, Tb::CHCR(1), 2, 0, true);
  b.wr(Tb::CHCR(1), 0);
  CHECKEQ(b.rd(Tb::CHCR(1)), CH_TE, "a dma = 1 read does not arm TE");
  b.wr(Tb::CHCR(1), 0);
  for (uint32_t off = 0x44; off < 0x100; off += 4) { b.access(false, off, 2, 0); CHECK(!b.cpu_hit, "offset %02x is a hole", off); }
  for (uint32_t off = 0x40; off < 0x44; off++) {
    uint32_t v = b.access(false, off, 0, 0);
    CHECK(b.cpu_hit && v == 0, "byte read of DMAOR+%u: hit %d value %x", off - 0x40, b.cpu_hit, v);
  }
  b.reset();
  for (int i = 0; i < 17; i++) CHECKEQ(b.rd(i == 16 ? Tb::DMAOR : (uint32_t)(4 * i)), 0, "register after a power-on reset");
  // a CPU CHCR write colliding with the unit's counter update stalls and lands after it
  g_test = "cover write / update collision";
  b.wr(Tb::DMAOR, 0x8201);
  int hits = 0;
  for (int off = 0; off < 60 && hits < 4; off++) {
    b.ch_stop(1);
    b.wr(Tb::SAR(1), BUFA); b.wr(Tb::DAR(1), BUFB); b.wr(Tb::TCR(1), 1);
    b.wr(Tb::CHCR(1), cfg | CH_DE);
    b.ticks(off);
    while (!b.bce() || (b.cyc & 2)) b.tick();
    int st0 = b.stalls;
    b.access(true, Tb::CHCR(1), 2, cfg | CH_IE | CH_DE);
    if (b.stalls != st0) {
      hits++;
      CHECKEQ(b.rd(Tb::CHCR(1)) & (CH_TE | CH_IE), CH_TE | CH_IE, "the colliding write landed after the update");
    }
    settle_units(b);
  }
  CHECK(hits > 0, "a collision was produced");
}

static void cov_onchip(Tb &b) {                              // RS 1001 / 1010 / 1101 / 1110
  g_test = "cover on-chip sources";
  b.reset();
  b.wr(Tb::DMAOR, 0x8201);
  b.oc.timed = true; b.oc.tx_delay = 90; b.oc.rx_delay = 70;
  for (int i = 0; i < 20; i++) b.oc.sci_rxq.push_back((uint8_t)(0xC0 + i));
  b.mem.fill(BUFB, 64, 0xEE);
  prog(b, 1, 0x1FE00014, BUFB, 20, CH_DM(1) | CH_SM(0) | CH_RS(9) | CH_TS(1) | CH_DE);
  wait_tcr0(b, 1, 200000); b.bclks(100);
  bool ok = true; for (int i = 0; i < 20; i++) if (b.mem.rd8(BUFB + i) != 0xC0 + i) ok = false;
  CHECK(ok, "RS 1001 SCI RX: 20 bytes from SCRDR1 in order");
  CHECKEQ(b.oc.sci_rx_taken.size(), 20, "RS 1001: 20 DMAC reads of SCRDR1");
  CHECKEQ(b.mem.rd8(BUFB + 20), 0xEE, "RS 1001: no extra byte");
  b.wr(Tb::CHCR(1), 0);
  for (int i = 0; i < 20; i++) b.mem.wr8(BUFA + i, (uint8_t)(0x60 + i));
  prog(b, 2, BUFA, 0x1FE8000C, 20, CH_DM(0) | CH_SM(1) | CH_RS(10) | CH_TS(1) | CH_DE);
  wait_tcr0(b, 2, 200000); b.bclks(100);
  ok = b.oc.scif_tx.size() == 20; for (size_t i = 0; ok && i < 20; i++) if (b.oc.scif_tx[i] != 0x60 + i) ok = false;
  CHECK(ok, "RS 1010 SCIF TX: 20 bytes to SCFTDR2 in order (got %zu)", b.oc.scif_tx.size());
  b.wr(Tb::CHCR(2), 0);
  b.oc.timed = false;
  b.mem.wr32(BUFA, 0x11111111); b.mem.wr32(BUFA + 4, 0x22222222); b.mem.wr32(BUFA + 8, 0x33333333);
  prog(b, 3, BUFA, 0x1FD80008, 3, CH_DM(0) | CH_SM(1) | CH_RS(13) | CH_TS(3) | CH_DE);
  for (int e = 0; e < 3; e++) { b.oc.tmu_ic = true; b.bclks(80); }
  CHECKEQ(b.rd(Tb::TCR(3)), 0, "RS 1101: three captures, three units");
  CHECKEQ(b.oc.tcor0, 0x33333333, "RS 1101: TCOR0 holds the last word (on-chip write leg)");
  CHECKEQ(b.rd(Tb::DMAOR), 0x8201, "RS 1101: no address error");
  b.ch_stop(3);
  b.oc.tcnt0 = 0xCAFEF00D; b.mem.fill(BUFB, 16, 0xEE);
  prog(b, 3, 0x1FD8000C, BUFB, 2, CH_DM(1) | CH_SM(0) | CH_RS(14) | CH_TS(3) | CH_DE);
  for (int e = 0; e < 2; e++) { b.oc.tmu_ic = true; b.bclks(80); }
  CHECKEQ(b.mem.rd32(BUFB), 0xCAFEF00D, "RS 1110: TCNT0 copied to memory (on-chip read leg)");
  CHECKEQ(b.mem.rd32(BUFB + 4), 0xCAFEF00D, "RS 1110: second unit");
  CHECKEQ(b.rd(Tb::DMAOR), 0x8201, "RS 1110: no address error");
  b.ch_stop(3);
}

static void cov_pins(Tb &b) {
  // a DREQ0# edge latched before an address error starts no unit after AE is cleared (hm 14.3.6); control without AE
  g_test = "cover edge dropped by AE";
  for (int with_ae = 0; with_ae < 2; with_ae++) {
    b.reset();
    b.wr(Tb::DMAOR, 0x0201);
    uint32_t cfg = CH_DM(1) | CH_SM(1) | CH_RS(0) | CH_TS(3) | (1u << 19);
    prog(b, 0, BUFA, BUFB, 10, cfg);
    b.dbreq_n = false; b.bclks(3); b.dbreq_n = true; b.bclks(3);
    if (with_ae) {
      prog(b, 1, BUFA + 2, BUFB, 4, CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TS(3) | CH_DE);
      b.ticks(50);
      CHECKEQ(b.rd(Tb::DMAOR) & 4, 4, "AE set by the misaligned channel 1");
      b.ch_stop(1);
      b.wr(Tb::DMAOR, 0x0201);
    }
    b.wr(Tb::CHCR(0), cfg | CH_DE);
    b.bclks(200);
    CHECKEQ(b.rd(Tb::TCR(0)), with_ae ? 10 : 9, with_ae ? "an edge latched before AE starts no unit" : "control: one unit");
  }
  // DACK in the read cycle (AM 0) or the write cycle (AM 1)
  g_test = "cover AM";
  for (int am = 0; am < 2; am++) {
    b.reset();
    b.wr(Tb::DMAOR, 0x0201);
    prog(b, 0, BUFA, BUFB, 6, CH_DM(1) | CH_SM(1) | CH_RS(0) | CH_TS(3) | (am ? (1u << 17) : 0) | CH_DE);
    b.dbreq_n = false;
    int dack = 0, on_rd = 0, on_wr = 0;
    for (int k = 0; k < 600; k++) {
      b.tick();
      if (!(b.cyc & 1) && !b.pinlog.empty() && ((b.pinlog.back().second >> 1) & 1)) {
        dack++; if (b.bsc.rd_v) on_rd++; if (b.bsc.wd_req) on_wr++;
      }
    }
    b.dbreq_n = true; b.bclks(50);
    CHECK(dack > 0, "AM %d: DACK0 driven", am);
    if (am) CHECK(on_wr == dack && on_rd == 0, "AM 1: DACK0 %d Bclk, write beats %d, read beats %d", dack, on_wr, on_rd);
    else CHECK(on_rd == dack && on_wr == 0, "AM 0: DACK0 %d Bclk, read beats %d, write beats %d", dack, on_rd, on_wr);
  }
  // channel-1 pins take CHCR1's RL / AL, not CHCR0's
  g_test = "cover channel-1 polarity";
  b.reset();
  b.wr(Tb::DMAOR, 0x0201);
  prog(b, 0, BUFA, BUFB, 4, CH_DM(1) | CH_SM(1) | CH_RS(0) | CH_TS(3));
  uint32_t c1 = CH_DM(1) | CH_SM(1) | CH_RS(0) | CH_TS(3) | (1u << 18) | (1u << 16);
  prog(b, 1, BUFA, BUFB, 4, c1);
  b.bclks(4);
  CHECKEQ(b.t->ddt_id, 3, "idle {DRAK1, DACK1} with CHCR1.RL = AL = 1");
  CHECKEQ(b.t->bavl_n, 0, "idle DRAK0 with CHCR0.RL = 0");
  CHECKEQ(b.t->tdack_n, 0, "idle DACK0 with CHCR0.AL = 0");
  b.wr(Tb::CHCR(1), c1 | CH_DE);
  b.tr_n = false;
  int d1 = 0, k1 = 0;
  size_t pl = b.pinlog.size();
  b.bclks(200);
  for (size_t k = pl; k < b.pinlog.size(); k++) { uint32_t v = b.pinlog[k].second >> 2; if (!(v & 2)) d1++; if (!(v & 1)) k1++; }
  b.tr_n = true; b.bclks(40);
  CHECK(d1 > 0 && k1 > 0, "DRAK1 / DACK1 asserted low (%d / %d Bclk)", d1, k1);
  // level-DREQ burst: re-sampled one cycle after the write starts, so the burst keeps the bus while DREQ0# is low
  // (hm 14.3.5); re-sampled only at the unit's end it would release the bus after every unit
  g_test = "cover level burst holds the bus";
  b.reset();
  b.wr(Tb::DMAOR, 0x0201);
  prog(b, 0, BUFA, BUFB, 200, CH_DM(1) | CH_SM(1) | CH_RS(0) | CH_TM | CH_TS(3) | CH_DE);
  b.bsc.cpu_on = true; b.bsc.cpu_len = 8;
  b.bsc.log.clear();
  b.dbreq_n = false; b.bclks(300); b.dbreq_n = true; b.bclks(60);
  b.bsc.cpu_on = false;
  {
    std::string pat; size_t first = 0, last = 0; bool any = false;
    for (size_t k = 0; k < b.bsc.log.size(); k++) if (!b.bsc.log[k].cpu) { if (!any) first = k; last = k; any = true; }
    for (size_t k = first; any && k <= last; k++) pat += b.bsc.log[k].cpu ? 'C' : b.bsc.log[k].we ? 'w' : 'r';
    CHECK(any && pat.size() > 20 && pat.find('C') == std::string::npos, "level burst: no CPU cycle inside (%s)",
          pat.substr(0, 60).c_str());
  }
  b.ch_stop(0); settle_units(b);
}

static void cov_power(Tb &b) {
  // the levels (SOC.md 2.4, 3.4, I088): module stop (run = 0, mstp = 1) forces DMTE / DMAE to 0; deep sleep (run = 0,
  // mstp = 0) keeps them, so a DMTE pending at deep-sleep entry still wakes the CPU; the mask is the mstp input alone
  // (run = 1 with mstp = 1 masks too: not a state the CPG produces, it isolates the two inputs)
  g_test = "cover module stop / deep sleep levels";
  b.reset();
  b.wr(Tb::DMAOR, 0x8201);
  uint32_t cfg = CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TM | CH_TS(3);
  prog(b, 1, BUFA, BUFB, 2, cfg | CH_IE | CH_DE); wait_tcr0(b, 1); b.ticks(200);
  CHECKEQ(b.t->dmte, 2, "DMTE1 with TE & IE");
  b.run = false; b.mstp = true; b.ticks(8);
  CHECKEQ(b.t->dmte, 0, "DMTE1 0 while module-stopped");
  b.run = true; b.mstp = false; b.ticks(8);
  CHECKEQ(b.t->dmte, 2, "DMTE1 back after the stop");
  b.run = false; b.ticks(8);
  CHECKEQ(b.t->dmte, 2, "DMTE1 kept in deep sleep (run = 0, mstp = 0)");
  b.ticks(400);
  CHECKEQ(b.t->dmte, 2, "DMTE1 still up 200 Bclk into deep sleep");
  b.run = true; b.mstp = true; b.ticks(8);
  CHECKEQ(b.t->dmte, 0, "DMTE1 0 with mstp = 1 alone");
  b.mstp = false; b.ticks(8);
  (void)b.rd(Tb::CHCR(1)); b.wr(Tb::CHCR(1), 0);
  prog(b, 1, BUFA + 2, BUFB, 2, cfg | CH_DE); b.ticks(60);
  CHECKEQ(b.t->dmae, 1, "DMAE with AE");
  b.run = false; b.mstp = true; b.ticks(8);
  CHECKEQ(b.t->dmae, 0, "DMAE 0 while module-stopped");
  b.mstp = false; b.ticks(8);
  CHECKEQ(b.t->dmae, 1, "DMAE kept in deep sleep (run = 0, mstp = 0)");
  b.run = true; b.mstp = true; b.ticks(8);
  CHECKEQ(b.t->dmae, 0, "DMAE 0 with mstp = 1 alone");
  b.mstp = false;
  // deep sleep stops the channels like a module stop (dmac_run = 0) but a TE set just before it keeps DMTE; the
  // transfer goes on when run returns, in lockstep with the model (levels compared after every Bclk)
  g_test = "cover deep sleep lockstep";
  b.reset();
  b.wr(Tb::DMAOR, 0x8201);
  b.start_lockstep(true);
  prog(b, 2, BUFA, BUFB, 1, cfg | CH_IE | CH_DE);
  prog(b, 1, BUFA + 0x400, BUFB + 0x400, 60, cfg | CH_IE | CH_DE);
  b.ticks(300);
  b.run = false;
  size_t n0 = b.order.size();
  uint32_t ds_tcr = b.treg(6);
  b.ticks(1200);
  CHECK(b.order.size() <= n0 + 1, "deep sleep: no new unit (%zu -> %zu)", n0, b.order.size());
  CHECKEQ(b.t->dmte & 4, 4, "deep sleep: DMTE2 (TE set before it) kept");
  CHECK(ds_tcr > 1 && b.treg(6) + 1 >= ds_tcr, "deep sleep: channel 1 stops (DMATCR %u -> %u)", ds_tcr, b.treg(6));
  b.run = true;
  wait_tcr0(b, 1); settle_units(b);
  CHECKEQ(b.t->dmte, 6, "after the wake: DMTE1 and DMTE2");
  b.end_lockstep();
  // standby clears the TE arm bits with TE
  g_test = "cover standby arm bits";
  b.reset();
  b.wr(Tb::DMAOR, 0x8201);
  prog(b, 1, BUFA, BUFB, 2, cfg | CH_DE); wait_tcr0(b, 1); b.ticks(200);
  (void)b.rd(Tb::CHCR(1));
  b.stby = true; b.ticks(20); b.stby = false; b.ticks(8);
  b.wr(Tb::TCR(1), 2);
  b.ticks(20);
  for (int k = 0; k < 20000 && !(b.treg(7) & CH_TE); k++) b.tick();
  b.wr(Tb::CHCR(1), cfg);
  CHECKEQ(b.rd(Tb::CHCR(1)) & CH_TE, CH_TE, "TE set after the standby is not cleared without a new read");
  // an nmi_evt pulse in a non-bce Icyc still sets NMIF and stops the channels
  g_test = "cover NMI in a non-bce Icyc";
  b.reset();
  b.wr(Tb::DMAOR, 0x8201);
  prog(b, 1, BUFA, BUFB, 5000, CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TS(3) | CH_DE);
  b.bclks(100);
  while (b.bce()) b.tick();
  b.nmi_nb = true; b.tick();
  b.bclks(40);
  uint32_t t1 = ttcr(b, 1);
  b.bclks(200);
  CHECKEQ(b.rd(Tb::DMAOR), 0x8203, "NMIF set by a non-bce nmi_evt");
  CHECKEQ(ttcr(b, 1), t1, "the channel stopped");
  CHECK(t1 > 0 && t1 < 5000, "stopped mid-way (DMATCR %u)", t1);
  // SAR / DAR arithmetic is 32 bits: the carry out of bit 28 goes into bits 31:29 [P] (bits 31:29 are ignored by
  // the bus).  Only an area-7 address can carry out of 29 bits, and it is a class-B unit that still advances.
  g_test = "cover address carry";
  for (int d = 0; d < 2; d++) {
    b.reset();
    b.wr(Tb::DMAOR, 0x8201);
    prog(b, 1, d ? BUFA : 0x1FFFFFFCu, d ? 0x3FFFFFFCu : BUFB, 4, CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TS(3) | CH_DE);
    b.bclks(200); settle_units(b);
    CHECKEQ(b.rd(Tb::DMAOR), 0x8205, "class B (area 7 on an external leg)");
    CHECKEQ(b.rd(Tb::TCR(1)), 3, "the faulting unit counts");
    if (d) CHECKEQ(b.rd(Tb::DAR(1)), 0x40000000u, "DAR 3FFFFFFC + 4 carries into bit 29");
    else CHECKEQ(b.rd(Tb::SAR(1)), 0x20000000u, "SAR 1FFFFFFC + 4 carries into bit 29");
  }
}

static void cov_ddt(Tb &b) {
  // TR# two Bclk after BAVL# is the DTR cycle, not a TR-only repeat (hm 14.5.1, H figures): scan TR# 1..4 Bclk after
  // BAVL#; only 2 must leave the last channel's queue alone
  g_test = "cover pin DTR cycle";
  b.reset();
  b.wr(Tb::DMAOR, 0x8201);
  prog(b, 1, BUFA, 0, 50, CH_DM(0) | CH_SM(1) | CH_RS(2) | CH_TS(3));   // DE = 0: the queue is not consumed
  b.evq.push_back({0, mkdtr(0, 0, 1, 0, 0, 0)});                         // the last channel is 1
  b.bclks(10);
  // (M6) the DTR cycle now takes D63:0: keep a channel-0 "transfer end" DTR there (SZ 111, MD 01: no queue, no flag,
  // the last channel kept), so the DTR cycle is told from a repeat by channel 1's queue alone
  b.ddt_d = mkdtr(7, 0, 0, 1, 0, 0);
  std::string got;
  for (int k = 1; k <= 4; k++) {
    unsigned q0 = b.t->t_qcnt & 7;
    b.dbreq_n = false; b.bclks(1); b.dbreq_n = true;
    for (int i = 0; i < 6; i++) b.bclks(1);
    b.ddt_bavl = true;
    size_t pl = b.pinlog.size();
    int guard = 0;
    while (guard++ < 100) { b.tick(); if (b.pinlog.size() > pl && !(b.pinlog.back().second & 1)) break; }
    b.ddt_bavl = false;
    b.ticks(2 * (k - 1));
    b.tr_n = false; b.ticks(2); b.tr_n = true;
    b.bclks(6);
    unsigned q1 = b.t->t_qcnt & 7;
    got += q1 == q0 ? 'D' : q1 == q0 + 1 ? 'R' : '?';
  }
  b.ddt_d = 0;
  CHECK(got == "RDRR", "TR# 1..4 Bclk after BAVL#: %s (R = TR-only repeat, D = the DTR cycle), expected RDRR", got.c_str());
  // (M6) a single-address write's data pulls after the engine moved on: a channel-0 DDT unit R/W 1 (32 bytes, Holly ->
  // memory) accepted while channel 1 copies word units by auto request (cycle steal), the stub pulling the write's data
  // 8 Bclk after its accept -- by then channel 1's next unit is committed.  The pulls must carry the 32-byte unit's own
  // byte enables (0xFF; the stub checks every pull) and all 32 of the device's bytes must land (dmac1 under KOS at M6:
  // a maple response written as 8 + 2 + 2 + 2 bytes; planted bug V1_DMAC_BUG_26)
  g_test = "cover single write lanes";
  {
    b.reset();
    b.wr(Tb::DMAOR, 0x8201);
    const int wl0 = b.bsc.wr_lat;
    b.bsc.wr_lat = 8;
    fillA(b, 3); fillB(b, 0xEE);
    const uint32_t da = BUFB + 0x800;
    prog(b, 1, BUFA, BUFB, 64, CH_DM(1) | CH_SM(1) | CH_RS(4) | CH_TS(2) | CH_DE);     // word units, cycle steal
    b.bclks(12);
    b.evq.push_back({0, mkdtr(4, 1, 0, 1, 1, da)});
    b.bclks(400); settle_units(b);
    int good = 0;
    for (int k = 0; k < 32; k++) good += b.mem.rd8(da + k) == dev_byte(0, 0, k);   // the device's first unit
    CHECKEQ(good, 32, "the 32-byte single write's bytes all landed");
    CHECKEQ(b.rd(Tb::DAR(0)), da + 0x20u, "channel 0's unit counted (DAR0 + 32)");
    b.bsc.wr_lat = wl0;
  }
  // (M6-fix, M6-verify F1) the burst session reported to the BSC (bus_ses, v1_bsc_arb keeps the CPU off the bus in its
  // gaps): channel 1 copies 24 longword units by auto-request burst (TM 1) and a channel-0 DDT unit (R/W 0, 32 bytes;
  // DMAOR.PR 10: channel 0 above channel 1) arrives 10 Bclk into it: the engine keeps the session, commits the single
  // unit at a channel-1 unit's end and then channel 1's next unit, whose read waits (bq.v low) while the single cycle's
  // beats move -- the gap in which, before the fix, v1_bsc accepted a waiting CPU access (ddtmix G: 21 CPU reads inside
  // a burst).  bus_ses must be 1 in every Bclk after channel 1's first commit through its last counter update (the
  // test requires such a gap: a Bclk there with no request while the engine is busy) and 0 after it; the same copy in
  // cycle steal (TM 0) reports a session only for the channel-0 unit itself (a DDT DTR loads CHCR0 with TM = 1: its one
  // unit is a burst session of one unit).  Planted bug V1_DMAC_BUG_27 (bus_ses never 1)
  g_test = "cover burst session";
  for (int tm = 1; tm >= 0; tm--) {
    b.reset();
    b.wr(Tb::DMAOR, 0x8201);
    fillA(b, 5); fillB(b, 0xEE);
    b.seslog.clear();
    prog(b, 1, BUFA, BUFB, 24, CH_DM(1) | CH_SM(1) | CH_RS(4) | (tm ? CH_TM : 0u) | CH_TS(3) | CH_DE);
    b.bclks(10);
    b.evq.push_back({0, mkdtr(4, 0, 0, 1, 1, BUFA + 0x400)});
    wait_tcr0(b, 1, 20000);
    settle_units(b);
    uint64_t c1 = ~0ull, f1 = 0, c0 = ~0ull, f0 = 0;
    for (auto &e : b.seslog) {
      if (e.acc && e.acc_ch == 1 && c1 == ~0ull) c1 = e.bt;
      if (e.acc && e.acc_ch == 0 && c0 == ~0ull) c0 = e.bt;
      if (e.upd && e.upd_ch == 1) f1 = e.bt;
      if (e.upd && e.upd_ch == 0 && !f0) f0 = e.bt;
    }
    CHECK(c1 != ~0ull && f1 > c1 && c0 > c1 && c0 < f1, "burst session (TM %d): channel 1 committed at %llu, ended at %llu, "
          "the channel-0 unit committed at %llu (want inside)", tm, (unsigned long long)c1, (unsigned long long)f1,
          (unsigned long long)c0);
    int in_ses = 0, gaps = 0, off_in = 0, on_out = 0, cs_on = 0;
    for (auto &e : b.seslog) {
      const bool inside = e.bt > c1 && e.bt <= f1;
      if (inside) { in_ses++; off_in += !e.ses; gaps += !e.bqv && !e.idle; }
      else on_out += e.ses;
      cs_on += e.ses && !(e.bt > c0 && e.bt <= f0);             // (cycle steal) not the channel-0 unit's own session
    }
    if (tm) {
      CHECK(gaps > 0, "burst session: no Bclk without a request inside the burst (%d Bclk): the test does not reach the "
            "gap", in_ses);
      CHECKEQ(off_in, 0, "burst session: Bclk inside the burst with bus_ses = 0 (the BSC may give them to the CPU)");
      CHECKEQ(on_out, 0, "burst session: Bclk outside the burst with bus_ses = 1");
    } else {
      CHECK(f0 > c0, "cycle steal: the channel-0 unit committed at %llu, counted at %llu", (unsigned long long)c0,
            (unsigned long long)f0);
      CHECKEQ(cs_on, 0, "cycle steal: Bclk with bus_ses = 1 outside the channel-0 unit");
    }
    CHECKEQ(b.rd(Tb::TCR(1)), 0u, "channel 1 ended");
  }
  // (M6, board phase 2: SOC.md 3.3.3) the DTR itself comes from D63:0 in that cycle: a channel-0 initialisation DTR
  // driven on the data bus with TR# two Bclk after BAVL# loads CHCR0 / DMATCR0 / DAR0 (the dmac1 S1 maple response
  // form, 0x853C1); the same word on the bus one Bclk early or late (TR# at 1 or 3 Bclk: a TR-only repeat) loads
  // nothing, and a word left on the bus outside the TR# cycle is never taken.  Planted bug V1_DMAC_BUG_25 (the data
  // bus ignored) fails the first
  g_test = "cover DTR on the data bus";
  for (int k = 1; k <= 3; k++) {
    b.reset();
    b.wr(Tb::DMAOR, 0x8201);
    const uint32_t da = 0x0C08EF20u + 0x20u * k;
    const uint64_t w = mkdtr(4, 1, 0, 1, 1, da);
    b.ddt_d = 0xFFFFFFFFFFFFFFFFull;                                 // junk on the bus until the DTR cycle
    b.dbreq_n = false; b.bclks(1); b.dbreq_n = true;
    for (int i = 0; i < 6; i++) b.bclks(1);
    b.ddt_bavl = true;
    size_t pl = b.pinlog.size();
    int guard = 0;
    while (guard++ < 100) { b.tick(); if (b.pinlog.size() > pl && !(b.pinlog.back().second & 1)) break; }
    b.ddt_bavl = false;
    b.ticks(2 * (k - 1));
    b.ddt_d = w; b.tr_n = false; b.ticks(2); b.tr_n = true; b.ddt_d = 0xFFFFFFFFFFFFFFFFull;
    b.bclks(3);
    const uint32_t c0 = b.t->t_chcr0, d0 = b.t->t_dar0;
    b.bclks(60); settle_units(b);
    b.ddt_d = 0;
    if (k == 2) {
      CHECKEQ(c0 & ~2u, 0x853C1u, "the data-bus DTR loads CHCR0 (TE aside)");
      CHECK(d0 == da || d0 == da + 0x20u, "the data-bus DTR loads DAR0 (R/W = 1): %08x", d0);
      CHECKEQ(b.rd(Tb::DAR(0)), da + 0x20u, "one 32-byte unit moved: DAR0 + 32");
      CHECKEQ(b.rd(Tb::CHCR(0)), 0x853C3u, "the unit's end sets TE");
    } else {
      CHECKEQ(b.t->t_chcr0, 0u, "TR# off the DTR cycle: CHCR0 not loaded from the bus");
      CHECKEQ(b.t->t_dar0, 0u, "TR# off the DTR cycle: DAR0 not loaded from the bus");
    }
  }
  // the single-address master codes on the BSC port: DDT mode -> BM_DDT (TDACK + ID), normal mode -> BM_DMA_SINGLE
  g_test = "cover single-address master";
  b.reset();
  b.wr(Tb::DMAOR, 0x8201);
  b.ch_stop(2); b.wr(Tb::SAR(2), BUFA); b.wr(Tb::TCR(2), 2); b.wr(Tb::CHCR(2), 0x12C1);
  b.evq.push_back({1, 0}); b.evq.push_back({1, 0});
  b.bclks(80); settle_units(b);
  int nd = 0; for (auto &e : b.bsc.log) if (!e.cpu) { nd++; CHECKEQ(e.master, 3, "DDT single address: BM_DDT"); }
  CHECKEQ(nd, 2, "two DDT single cycles");
  b.wr(Tb::DMAOR, 0x0201);
  b.bsc.log.clear();
  prog(b, 1, BUFA, 0, 2, CH_DM(0) | CH_SM(1) | CH_RS(2) | CH_TS(3) | CH_DE);
  b.tr_n = false; b.bclks(60); b.tr_n = true; settle_units(b);
  nd = 0; for (auto &e : b.bsc.log) if (!e.cpu) { nd++; CHECKEQ(e.master, 2, "normal-mode single address: BM_DMA_SINGLE"); }
  CHECKEQ(nd, 2, "two DACK single cycles");
}

static void t_cover(Tb &b) {
  cov_regs(b);
  cov_onchip(b);
  cov_pins(b);
  cov_power(b);
  cov_ddt(b);
}

// ---------------------------------------------------------------------------------------------------------------
// 12. random programs in strict lockstep with dmac_ref.h
static uint32_t rnd_addr(Rng &r, int ts, int rs, bool src, int mis_pct, int a7_pct) {
  static const int ub[8] = {8, 1, 2, 4, 32, 1, 1, 1};
  bool onchip = src ? DmacRef::rs_src_onchip(rs) : DmacRef::rs_dst_onchip(rs);
  uint32_t a;
  if (onchip) {
    static const uint32_t regs[6] = {0x1FD80000, 0x1FD80004, 0x1FD80008, 0x1FD8000C, 0x1FD80010, 0x1F300000};
    a = regs[r.u(6)];
    if (r.p(10)) a = 0x0C040000 + 32 * r.u(64);
  } else {
    a = 0x0C040000u + (uint32_t)ub[ts] * r.u(2048);
    if (r.p(a7_pct)) a = r.p(50) ? 0x1FD80004 : 0x1C000000;
  }
  if (r.p(mis_pct)) a += 1 + r.u(3);
  if (r.p(15)) a |= (uint32_t)r.u(8) << 29;
  return a;
}
static int g_rand_from = 0;                                  // --from N: start at program N (each has its own RNG)
static void t_random(Tb &b, int nprog, uint64_t seed) {
  g_test = "random";
  for (int pi = g_rand_from; pi < nprog; pi++) {
    Rng r(seed * 1000003u + (uint64_t)pi);
    char tag[48]; snprintf(tag, sizeof tag, "random #%d", pi);
    g_test = tag;
    if (g_verbose) printf("=== %s (seed %llu)\n", tag, (unsigned long long)seed);
    b.reset();
    b.rng = Rng(seed * 1000 + pi);
    b.bsc.rd_lat = 2 + r.u(8); b.bsc.wr_lat = 1 + r.u(5); b.bsc.gap = 1 + r.u(2); b.bsc.done_lat = r.u(3);
    b.bsc.jitter = r.u(3); b.bsc.qmode = (int)r.u(2); b.bsc.cpu_on = r.p(30); b.bsc.pipe = r.p(70);
    b.p4.lat = 2 + r.u(10); b.p4.jitter = r.u(4);
    bool ddt = r.p(40);
    int pr = (int)r.u(4);
    b.wr(Tb::DMAOR, (ddt ? 0x8000u : 0) | (uint32_t)pr << 8);
    int nch = 1 + (int)r.u(4);
    for (int c = 0; c < 4; c++) {
      if (c >= nch || (ddt && c == 0)) continue;
      int rs, ts = (int)r.u(5);
      int kind = (int)r.u(10);
      if (ddt && kind < 4) rs = kind < 2 ? 2 : (kind == 2 ? 3 : 0);
      else rs = 4 + (kind % 3 == 0 ? (int)r.u(3) : 0);
      uint32_t chcr = CH_DM((int)r.u(3)) | CH_SM((int)r.u(3)) | CH_RS(rs) | (r.p(50) ? CH_TM : 0) | CH_TS(ts) |
                      (r.p(30) ? CH_IE : 0) | (ddt && r.p(30) ? (1u << 19) : 0);
      uint32_t sar = rnd_addr(r, ts, rs, true, 3, 3), dar = rnd_addr(r, ts, rs, false, 3, 3);
      b.ch_stop(c);
      b.wr(Tb::SAR(c), sar); b.wr(Tb::DAR(c), dar); b.wr(Tb::TCR(c), 1 + r.u(24)); b.wr(Tb::CHCR(c), chcr | CH_DE);
    }
    b.start_lockstep(true);
    b.wr(Tb::DMAOR, (ddt ? 0x8000u : 0) | (uint32_t)pr << 8 | 1u);
    int steps = 40 + (int)r.u(80);
    for (int s = 0; s < steps; s++) {
      int ev = (int)r.u(100);
      if (ddt && ev < 20) {
        int id = (int)r.u(4);
        if (id == 0) b.evq.push_back({0, mkdtr((int)r.u(5), (int)r.u(2), 0, 1 + (int)r.u(3), 1 + (int)r.u(4),
                                               0x0C050000u + 32 * r.u(512))});
        else b.evq.push_back({0, mkdtr(0, 0, id, 0, 0, 0)});
      } else if (ddt && ev < 26) b.evq.push_back({1, 0});
      else if (ddt && ev < 30) b.evq.push_back({2, 0});
      else if (ddt && ev < 31) b.evq.push_back({0, mkdtr(6, 0, 0, 2, 0, 0)});
      else if (ev < 36) { int c = (int)r.u(4); uint32_t v = b.rd(Tb::CHCR(c)); b.wr(Tb::CHCR(c), v ^ CH_DE); }
      else if (ev < 39) { uint32_t o = b.rd(Tb::DMAOR); b.wr(Tb::DMAOR, (o ^ 1u) & ~6u); }
      else if (ev < 41) { uint32_t o = b.rd(Tb::DMAOR); b.wr(Tb::DMAOR, o & ~6u); }   // clears AE / NMIF when read as 1
      else if (ev < 43) { int c = (int)r.u(4); b.wr(Tb::TCR(c), 1 + r.u(12)); }
      else if (ev < 44) b.nmi = true;
      else if (ev < 50) (void)b.rd((uint32_t)(r.u(17) * 4));
      b.ticks(10 + (int)r.u(120));
    }
    // quiesce: stop the DDT traffic and the DMAC (DME = 0; a 16M-unit channel would run on), let the units drain.  A
    // single-address unit ends at the BSC's acceptance, before its data beats move, so memory and the device streams
    // are compared only once nothing is in flight.
    b.evq.clear();
    { uint32_t o = b.rd(Tb::DMAOR); b.wr(Tb::DMAOR, o & ~7u); }
    b.wait_idle();
    b.end_lockstep();
  }
  g_test = "random";
}

static void t_dbg(Tb &b) {
  g_test = "dbg";
  b.reset();
  b.wr(Tb::DMAOR, 0x8201);
  g_verbose = 1;
  b.evq.push_back({0, mkdtr(0, 0, 0, 1, 2, 0x0C081200)});
  for (int k = 0; k < 40; k++) {
    b.bclks(1);
    printf("bt %llu busy %d cand %x has %x acc %d upd %d tcr0 %x chcr0 %x bqv %d\n", (unsigned long long)b.bsc.bt, b.t->t_busy, b.t->t_cand,
           b.t->t_has, b.t->t_acc_v, b.t->t_upd_v, b.t->t_tcr0, b.t->t_chcr0, b.t->bq_v);
  }
  g_verbose = 0;
}

// ---------------------------------------------------------------------------------------------------------------
int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  int nrand = 150;
  uint64_t seed = 1;
  std::string only;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--rand") && i + 1 < argc) nrand = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = strtoull(argv[++i], nullptr, 0);
    else if (!strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i];
    else if (!strcmp(argv[i], "--from") && i + 1 < argc) g_rand_from = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-v")) g_verbose = 1;
    else if (!strcmp(argv[i], "-vv")) g_verbose = 2;
  }
  struct T { const char *name; std::function<void(Tb &)> fn; };
  std::vector<T> tests = {
    {"access", t_access}, {"classes", t_classes}, {"transfers", t_transfers}, {"priority", t_priority},
    {"ddt", t_ddt}, {"irq", t_irq_power}, {"extreq", t_extreq}, {"onchip", t_onchip}, {"timing", t_timing},
    {"fixes", t_fixes}, {"cover", t_cover},
    {"random", [&](Tb &b) { t_random(b, nrand, seed); }},
    {"dbg", t_dbg},
  };
  for (auto &t : tests) {
    if (only.empty() ? !strcmp(t.name, "dbg") : only != t.name) continue;   // (dbg: a trace, only on request)
    Tb *b = new Tb;
    int f0 = g_fail, c0 = g_checks;
    t.fn(*b);
    printf("%-10s %6d checks, %d failed\n", t.name, g_checks - c0, g_fail - f0);
    delete b;
  }
  printf("v1-dmac: %d checks, %d failed\n", g_checks, g_fail);
  printf("v1-dmac: %s\n", g_fail ? "FAIL" : "PASS");
  return g_fail ? 1 : 0;
}
