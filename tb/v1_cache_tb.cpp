// v1_cache_tb.cpp -- unit testbench of the WI-CACHE units rtl/v1/v1_ocache.sv, v1_icache.sv, v1_sq.sv and v1_ccr.sv on
// the WI-0b bus stub tb/v1_bsc_stub.sv (tb/v1_cache_tb.sv flattens the ports), checked against the functional model
// tb/sh4_cache.h.  Owner: WI-CACHE.  `make v1-cache`; the last line is "v1-cache: PASS" / "v1-cache: FAIL".
//
// The drivers behave as the core's MA stage, v1_fetch, v1_mmu and the P4 bridge do (SOC.md 3.1.5, 3.2):
//   * MA: one access presented until its ack (never withdrawn; junk fields while l_v = 0), the next one at the earliest
//     in the cycle after the ack.  The P4 array / CCR accesses take the same MA slot (a strobe re-presented while the
//     slave stalls; arr_strobe pulses in the capture cycle of an array access, as the bridge does).
//   * fetch: a request held while it is not acked and irsp.busy; a cacheable request that waits may be redirected.
//   * v1_mmu: the untranslated address map (region, PA, C, WT from the CCR as v1_mmu computes them with AT = 0), and a
//     combinational xl_* answer (identity, or a small "TLB" map with misses and multiple hits; xl_stall at random for
//     the OC).
//   * the stub's memory: a sparse quadword store with a deterministic pattern where nothing was written; area 7 reads 0
//     and drops writes (the BSC's RG_A7 answer, C16).
// The model applies every access at the RTL's ack (P4 accesses at their capture) and the IC's line fills at the RTL's
// IFILL records (lockstep, SOC.md 6.2), so the RTL's architectural state -- load data, fetched words, the arrays read
// back through the P4 array port, memory after the buffers drained -- must equal the model's.
//
// Checked every cycle: the MA port contract (ack / frz only with the request, never both, the idle unit silent), frz
// exactly in the first two cycles of every access the spec says needs the bus (SOC.md 3.3.1 C2, predicted from the
// model's pre-state; one cycle later behind the V-clear comparator of an OCBI / OCBP / purge hit of the same entry)
// and never otherwise, every P4 slave silent when not strobed, xl_* 0 without xl_req and xl_req only for a longword
// associative address-array write, the stub's handshake checker, the IFILL record's entry, a deadlock bound.  The
// MMU's view of an access (region, PA, C, WT) is taken when it reaches MA (the CCR of that moment).  A reset that kills
// an access applies the SOC.md 2.3 rule (kill_reset).
// Groups (directed first, then random):
//   c1       hits ack in MA1 with the data (load, store, OC RAM, OCBx / PREF / MOVCA.L hits), IC hit in the request cycle
//   c2       the 2-cycle miss path of every access that needs the bus, C3's request in m + 1
//   c6       load miss / uncached load / PREF-P2 / TAS read penalty 14 / 15 by the Bclk phase, the data = the critical
//            beat, ack = critical rd_v + 1, critical-first wrap order
//   lfb      the line-fill buffer: second loads to a filling line (ldw), a hit during the fill (ldhot), stores to a
//            filling line (stst / ldst), store / load after a store miss (stld), store-miss stream, miss during a tail,
//            stored bytes surviving the fill
//   wbb wtb  dirty victims (fill first, then the write-back), dirty OCBP / OCBWB back to back, WTB stream, uncached
//            load after an uncached store
//   ops      the caches-sq 3.3 rows (OCBI data loss, OCBP / OCBWB, MOVCA.L allocation with clean / dirty victims, PREF,
//            write-through rules, P2 aliases, TAS.B)
//   arrays   caches-sq 2.3 formats and every address-array write form, IC arrays, ICI, stall rules, the size rule
//            (SOC.md 3.1.4: a byte / word access to any of the four arrays reads 0 and writes nothing)
//   ocram    ORA with OIX = 0 / 1 (the measured aliasing), the data array showing the RAM, index bit 7 forced
//   index    OIX / IIX entries of caches-sq 6.2
//   ccr      CCR / QACR registers, ICI / OCI pulses, sizes, mirror, resets
//   sq       the SOC.md 4.2 SQ rows, QACR0 / QACR1, AT = 1, busy waits, PREF-PREF
//   ic       fill, critical-word bypass, beats, other-line wait, IFILL, uncached words and redirects, poison, refill
//            stall, the forced miss after arr_strobe, events
//   reset    power-on / manual reset rules
//   fix_*    regressions of the verification findings (WI-CACHE-verify.md): fix_ic (the uncached word's release and
//            busy), fix_ici / fix_oci (ICI / OCI while fills are in flight), fix_ocram (OC RAM behind a fill),
//            fix_stale (a killed uncached read), fix_kill (the reset rule; TAS.B across a manual reset at each point
//            of its locked pair), fix_xl (xl_* idle values; also per cycle)
//   ocbi     the V-clear comparator and the sweep's six OCBI records (86478-86485 against the PREF control)
//   loops    the caches-sq kern.S loops per iteration on the stub against the console (stld, stst, ldst, prefld,
//            stldgap, ldw, streams)
//   sqoc     an SQ burst and an OC fill of the same line, both queued: program order
//   hwrows   the caches-sq 6.1 / 6.3 timing rows the stub can express, as MA-level numbers (printed, with the checks
//            that follow from the stub's fixed numbers)
//   random   random traffic on all ports against the model, with checkpoints (drain, arrays, memory, SQ bursts)
//   inflight random traffic with CCR writes (every field) and access-killing resets injected mid-flight (TAS.B's
//            locked pair included: manual resets, SOC.md 2.3)
// Options: --seed N, --n N (random ops), --only GROUP, -v.
#include "Vv1_cache_tb_top.h"
#include "verilated.h"
#include "sh4_cache.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using namespace sh4cache;

static long g_checks = 0, g_fail = 0;
static bool g_verbose = false;
static int64_t g_watch = -1;                                        // --watch PA: trace every event on that line
static int64_t g_trace_from = -1;                                   // --trace-from CYC: MA ops, kills, bus handshakes
static bool watched(uint32_t pa) { return g_watch >= 0 && ((pa ^ (uint32_t)g_watch) & 0x1FFFFFE0u) == 0; }
#define CHK(cond, ...)                                                                                                 \
  do {                                                                                                                 \
    g_checks++;                                                                                                        \
    if (!(cond)) {                                                                                                     \
      if (g_fail < 60) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); }              \
      g_fail++;                                                                                                        \
    }                                                                                                                  \
  } while (0)

struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed = 0x9E3779B97F4A7C15ull) : s(seed) {}
  uint64_t next() {
    uint64_t z = (s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
  uint32_t n(uint32_t k) { return (uint32_t)(next() % k); }
  bool p(int pct) { return n(100) < (uint32_t)pct; }
};

enum { BRQ_IC_FILL = 0, BRQ_IC_UNC, BRQ_OC_FILL, BRQ_OC_WBB, BRQ_OC_WTB, BRQ_OC_UNC, BRQ_SQ, BRQ_N };
enum { P4B_ICAA = 2, P4B_ICDA = 3, P4B_OCAA = 6, P4B_OCDA = 7, P4B_CCN_CCR = 12 };
enum { FX_NONE = 0, FX_ADDR = 1, FX_TLB_MISS = 2 };
enum { EV_OP_RD = 0x01, EV_OP_WR = 0x02, EV_OC_RD_MISS = 0x04, EV_OC_WR_MISS = 0x05, EV_IC_MISS = 0x08,
       EV_OCRAM = 0x0B, EV_OP_RW = 0x0E, EV_OC_MISS = 0x0F, EV_IC_FILL = 0x21, EV_OC_FILL = 0x22, EV_FRZ_IC = 0x24 };
static const char *kname(int k) {
  static const char *n[] = {"NONE", "LD", "ST", "PREF", "OCBI", "OCBP", "OCBWB", "MOVCA", "RMW_RD", "RMW_WR",
                            "TAS_PURGE", "TAS_RD", "TAS_WR"};
  return k >= 0 && k <= 12 ? n[k] : "?";
}

// ---------------------------------------------------------------------------------------------------------------
// memory: quadwords; a deterministic pattern where nothing was written; area 7 reads 0, writes dropped
struct Mem : Sh4MemPath {
  std::unordered_map<uint32_t, uint64_t> q;
  static uint64_t pat(uint32_t a8) {
    uint64_t z = a8 * 0x9E3779B97F4A7C15ull + 0x1234567ull;
    z = (z ^ (z >> 29)) * 0xBF58476D1CE4E5B9ull;
    return z ^ (z >> 32);
  }
  static bool a7(uint32_t pa) { return ((pa >> 26) & 7) == 7; }
  uint64_t rq(uint32_t a) {
    a &= 0x1FFFFFF8u;
    if (a7(a)) return 0;
    auto it = q.find(a);
    return it == q.end() ? pat(a) : it->second;
  }
  void wq(uint32_t a, uint64_t v, uint8_t be) {
    a &= 0x1FFFFFF8u;
    if (a7(a)) return;
    uint64_t o = rq(a);
    for (int i = 0; i < 8; i++) if (be >> i & 1) o = (o & ~(0xFFull << (8 * i))) | (v & (0xFFull << (8 * i)));
    q[a] = o;
  }
  void read_line(uint32_t pa, uint8_t out[32]) override {
    for (int k = 0; k < 4; k++) { uint64_t v = rq((pa & ~31u) + 8 * k); memcpy(out + 8 * k, &v, 8); }
  }
  const char *who = "";
  void write_line(uint32_t pa, const uint8_t in[32]) override {
    if (watched(pa)) {
      uint64_t v[4]; memcpy(v, in, 32);
      printf("  [watch] %s write_line %08x %016llx %016llx %016llx %016llx\n", who, pa, (unsigned long long)v[0],
             (unsigned long long)v[1], (unsigned long long)v[2], (unsigned long long)v[3]);
    }
    for (int k = 0; k < 4; k++) { uint64_t v; memcpy(&v, in + 8 * k, 8); wq((pa & ~31u) + 8 * k, v, 0xFF); }
  }
  uint64_t read(uint32_t pa, int size) override {
    uint64_t v = rq(pa) >> (8 * (pa & 7));
    return size >= 8 ? v : v & ((1ull << (8 * size)) - 1);
  }
  void write(uint32_t pa, int size, uint64_t v) override {
    if (watched(pa)) printf("  [watch] %s write %08x size %d %llx\n", who, pa, size, (unsigned long long)v);
    uint8_t be = (uint8_t)(((1u << size) - 1) << (pa & 7));
    wq(pa, v << (8 * (pa & 7)), be);
  }
};

// ---------------------------------------------------------------------------------------------------------------
// one MA-slot operation: a cache / SQ access, or a P4 slave access (arrays, CCR)
struct Op {
  bool p4 = false;
  // access
  int kind = DK_LD, size = 2, rg = RG_MEM; uint32_t va = 0, pa = 0; bool c = true, wt = false;
  uint64_t wdata = 0; uint8_t be = 0x0F;
  // P4
  bool wr = false; int blk = 0; uint32_t off = 0; uint32_t p4wd = 0; bool arr = false;
  // scheduling
  int gap = 0; int64_t at = -1; int phase = -1;             // phase: MA1 must be an even (0) / odd (1) cycle
  bool check_frz = true;                                    // the C2 prediction (random traffic: always)
  int shift = 0;                                            // 1: MA1 is the V-clear comparator's cycle (m = MA2)
  // results
  int64_t t0 = -1, tack = -1; int nfrz = 0; int64_t frz_first = -1, frz_last = -1; uint64_t rdata = 0;
  uint64_t mdata = 0; bool pred_nb = false; int stalls = 0; bool done = false;
  int lat() const { return (int)(tack - t0); }
};
using OpP = std::shared_ptr<Op>;

// spec: presented for one cycle and abandoned whatever the answer (SOC.md 3.2.1 ireq_t.spec: the sequential request
// of a redirect cycle, M3)
struct FReq { uint32_t va = 0, pa = 0; bool c = true; int fx = FX_NONE; bool vec400 = false; int stall = 0;
              bool spec = false; };

struct Bench {
  Vv1_cache_tb_top *t;
  Mem rmem, mmem;
  Sh4Cache m;
  Rng rng;
  int64_t cyc = 0;
  bool mmu_at = false;                                      // MMUCR.AT (v1_sq's burst address, the model's)
  // v1_mmu's xl answer: identity, or the tb's small TLB (va >> 10 -> pa >> 10; absent = miss; mhit set)
  bool xl_tlb = false; int xl_stall_pct = 0;
  std::unordered_map<uint32_t, uint32_t> tlb; std::unordered_map<uint32_t, bool> tlb_mhit;
  std::function<uint32_t(uint32_t)> sq_xlat;                // AT = 1: the SQ PREF's translated PA
  // MA agent
  std::deque<OpP> maq; OpP ma; int64_t ma_free = 0;
  // fetch agent
  std::deque<FReq> fq; bool f_on = false; FReq fc; int64_t f_t0 = -1, f_free = 0; int f_gap = 0;
  std::function<bool(FReq &)> f_gen;                        // random fetch generator (nullptr: the queue only)
  std::function<void(const FReq &, uint32_t, int64_t, int64_t)> f_done;
  bool spec_wait = false;                                           // a spec request held behind its ITLB refill
  std::vector<std::pair<int64_t, int64_t>> f_log;           // (t0, tack) of acked fetches
  bool f_redirect_ok = false;
  // observation
  std::vector<int64_t> e_rise[BRQ_N], e_rdy[BRQ_N], e_rdv[BRQ_N], e_done[BRQ_N];
  std::vector<int> e_beat[BRQ_N];
  bool bq_prev[BRQ_N] = {};
  uint64_t ev_ic[64] = {}, ev_oc[64] = {};
  std::vector<std::pair<uint32_t, int>> ifills;
  int64_t last_ici = -1, last_oci = -1;
  bool rpin = false, rman = false, rcpu = false;
  bool extra_arr_strobe = false;
  long n_acc = 0, n_p4 = 0, n_fetch = 0, n_sqwarn = 0;
  // the V-clear comparator (v1_ocache.sv): an OCBI / OCBP / purge hit acked in cycle vc_cyc clears entry vc_idx at the
  // end of the next cycle; a cacheable access to that entry presented then waits one cycle
  int64_t vc_cyc = -10; int vc_idx = -1;
  int64_t last_ifill = -1, last_iix_change = -1000;
  long n_kill_applied = 0, n_kill_dropped = 0, n_shift = 0;
  long n_xl = 0, n_p4_narrow = 0;                           // cycles with xl_req; byte / word array accesses

  explicit Bench(Vv1_cache_tb_top *top, uint64_t seed) : t(top), rng(seed) {
    mmem.who = "model"; rmem.who = "rtl";
    m.attach(&mmem);
    m.set_lockstep(true);                                   // the IC fills only at the RTL's IFILL records
    m.xlat = [this](uint32_t va, bool itlb, uint32_t &pa, bool &mh) { return xl_answer(va, itlb, pa, mh); };
  }

  // ---- the MMU's untranslated view (v1_mmu with AT = 0 / TRANSLATE = 0): region, PA, C, WT
  void untr(Op &o) {
    const uint32_t va = o.va, ccr = m.ccr_;
    if ((va >> 26) == 0x38) o.rg = RG_SQ;
    else if ((ccr & ORA) && (va >> 26) == 0x1F) o.rg = RG_OCRAM;
    else if (((va >> 26) & 7) == 7 && (va >> 29) != 7) o.rg = RG_A7;
    else o.rg = RG_MEM;
    o.pa = va & 0x1FFFFFFFu;
    if (o.rg == RG_SQ && mmu_at && sq_xlat) o.pa = sq_xlat(va);
    o.c = o.rg != RG_OCRAM && (va >> 29) != 5 && (va >> 29) != 7;
    o.wt = (va >> 29) == 4 ? !(ccr & CB) : (ccr & WT) != 0;
  }
  bool xl_answer(uint32_t va, bool itlb, uint32_t &pa, bool &mh) {
    (void)itlb;
    mh = false;
    if (!xl_tlb) { pa = va & 0x1FFFFC00u; return true; }
    auto it = tlb.find(va >> 10);
    if (it == tlb.end()) return false;
    if (tlb_mhit.count(va >> 10)) { mh = true; return false; }
    pa = (it->second << 10) & 0x1FFFFC00u;
    return true;
  }

  // ---- builders
  OpP acc(int kind, uint32_t va, int size = 2, uint64_t wd = 0) {
    auto o = std::make_shared<Op>();
    o->kind = kind; o->va = va; o->size = size; o->wdata = wd;
    o->be = (uint8_t)(0xFF >> (8 - (1 << size)));
    untr(*o);
    return o;
  }
  OpP p4(bool wr, int blk, uint32_t off, uint32_t wd = 0, int size = 2) {
    auto o = std::make_shared<Op>();
    o->p4 = true; o->wr = wr; o->blk = blk; o->off = off & 0x0FFFFFFFu; o->p4wd = wd; o->size = size;
    o->arr = blk == P4B_ICAA || blk == P4B_ICDA || blk == P4B_OCAA || blk == P4B_OCDA;
    return o;
  }
  OpP push(OpP o) { maq.push_back(o); return o; }
  OpP ld(uint32_t va, int size = 2) { return push(acc(DK_LD, va, size)); }
  OpP st(uint32_t va, uint64_t v, int size = 2) { return push(acc(DK_ST, va, size, v)); }
  OpP op(int kind, uint32_t va, int size = 2, uint64_t v = 0) { return push(acc(kind, va, size, v)); }
  OpP ocaa_rd(int e) { return push(p4(false, P4B_OCAA, 0x04000000u | (e << 5))); }
  OpP ocda_rd(int e, int w) { return push(p4(false, P4B_OCDA, 0x05000000u | (e << 5) | (w << 2))); }
  OpP icaa_rd(int e) { return push(p4(false, P4B_ICAA, (uint32_t)e << 5)); }
  OpP icda_rd(int e, int w) { return push(p4(false, P4B_ICDA, 0x01000000u | (e << 5) | (w << 2))); }
  OpP ccr_wr(uint32_t v, uint32_t off = 0x1C, int size = 2) { return push(p4(true, P4B_CCN_CCR, off, v, size)); }
  OpP ccr_rd(uint32_t off = 0x1C, int size = 2) { return push(p4(false, P4B_CCN_CCR, off, 0, size)); }

  // the C2 prediction from the model's state before the access
  bool needs_bus(const Op &o) {
    if (o.rg == RG_OCRAM) return false;
    if (o.rg == RG_SQ) return o.kind == DK_PREF;
    if (o.kind == DK_TAS_PURGE) return false;
    if (o.kind == DK_TAS_RD || o.kind == DK_TAS_WR) return true;
    const bool c = o.c && (m.ccr_ & OCE);
    const bool hit = c && m.oc_hit(m.oc_index(o.va), o.pa);
    if (o.kind == DK_ST || o.kind == DK_RMW_WR || o.kind == DK_MOVCA) return !c || o.wt || !hit;
    return !c || !hit;
  }
  bool quiet_bus() const {
    for (int i = 0; i < BRQ_N; i++) if (bq_prev[i]) return false;
    return true;
  }

  // ---- one Icyc
  void step() {
    const bool bce = (cyc & 1) == 1, pce = (cyc & 3) == 3;
    const bool in_rst = rpin || rman || rcpu;              // the core presents nothing new during a reset
    // start the next MA op
    if (!ma && !maq.empty() && !in_rst) {
      OpP o = maq.front();
      bool ok = cyc >= ma_free + o->gap && cyc >= o->at && (o->phase < 0 || (cyc & 1) == o->phase);
      if (ok) {
        ma = o; maq.pop_front(); o->t0 = cyc;
        if (!o->p4) {
          untr(*o);                                         // v1_mmu maps the access in EX: the CCR / AT of now
          o->pred_nb = needs_bus(*o);
          o->shift = vc_cyc == cyc - 1 && (o->rg == RG_MEM || o->rg == RG_A7) && o->c && (m.ccr_ & OCE) &&
                     m.oc_index(o->va) == vc_idx;
          n_shift += o->shift;
        }
        if (g_trace_from >= 0 && cyc >= g_trace_from)
          printf("  [trace] cyc %ld start %s %s va %08x pa %08x rg %d c %d wt %d shift %d\n", (long)cyc,
                 o->p4 ? (o->wr ? "P4-WR" : "P4-RD") : kname(o->kind), o->p4 ? "" : "", o->p4 ? o->off : o->va, o->pa,
                 o->rg, o->c, o->wt, o->shift);
      }
    }
    // fetch agent: next request
    if (!f_on && cyc >= f_free + f_gap && !in_rst) {
      FReq r;
      if (!fq.empty()) { r = fq.front(); fq.pop_front(); f_on = true; }
      else if (f_gen && f_gen(r)) f_on = true;
      if (f_on) { fc = r; f_t0 = cyc; spec_wait = false; }
    }
    // drive
    Op *o = ma.get();
    const bool acc_v = o && !o->p4;
    t->l_v = acc_v;
    t->l_sq = acc_v ? o->rg == RG_SQ : rng.n(2);
    t->l_kind = acc_v ? o->kind : rng.n(16); t->l_size = acc_v ? o->size : rng.n(4);
    t->l_va = acc_v ? o->va : (uint32_t)rng.next(); t->l_rg = acc_v ? o->rg : rng.n(8);
    t->l_pa = acc_v ? o->pa : (uint32_t)rng.next() & 0x1FFFFFFF; t->l_c = acc_v ? o->c : rng.n(2);
    t->l_wt = acc_v ? o->wt : rng.n(2); t->l_wdata = acc_v ? o->wdata : rng.next(); t->l_be = acc_v ? o->be : rng.n(256);
    const bool p4_v = o && o->p4;
    t->a_rd = p4_v && !o->wr; t->a_wr = p4_v && o->wr;
    t->a_size = p4_v ? o->size : rng.n(4); t->a_blk = p4_v ? o->blk : rng.n(31);
    t->a_off = p4_v ? o->off : (uint32_t)rng.next() & 0x0FFFFFFF; t->a_wdata = p4_v ? o->p4wd : (uint32_t)rng.next();
    t->at = mmu_at;
    t->i_v = f_on; t->i_md = 1; t->i_spec = f_on && fc.spec;
    t->i_va = f_on ? fc.va : (uint32_t)rng.next() & ~3u; t->if_pa = f_on ? fc.pa : (uint32_t)rng.next() & 0x1FFFFFFC;
    t->if_hit = f_on && fc.fx == FX_NONE && fc.stall == 0;
    t->if_c = f_on ? fc.c : rng.n(2); t->if_fx = f_on ? fc.fx : 0; t->if_vec400 = f_on && fc.vec400;
    t->if_stall = f_on && fc.fx == FX_NONE && fc.stall > 0;
    t->arr_strobe = 0;
    t->xl_hit = 0; t->xl_mhit = 0; t->xl_stall = 0; t->xl_pa = 0;
    t->rst_pin = rpin; t->rst_por = rpin; t->rst_man = rman; t->rst_cpu = rpin || rman || rcpu;
    t->bce = bce; t->pce = pce;
    t->m_rdata = 0;
    t->clk = 0;
    t->eval();
    // v1_mmu's combinational xl answer
    CHK(!(t->ic_xl_req && t->oc_xl_req), "cyc %ld: IC and OC xl_req together", (long)cyc);
    if (t->ic_xl_req || t->oc_xl_req) {
      const bool itlb = t->ic_xl_req;
      // only a longword associative address-array write asks v1_mmu (a byte / word one writes nothing, SOC.md 3.1.4)
      n_xl++;
      CHK(p4_v && o->wr && o->size == 2 && (o->off & 8) && o->blk == (itlb ? P4B_ICAA : P4B_OCAA),
          "cyc %ld: %s xl_req without a longword associative address-array write (op: %s blk %d off %07x size %d)",
          (long)cyc, itlb ? "IC" : "OC", p4_v ? (o->wr ? "P4-WR" : "P4-RD") : "none", p4_v ? o->blk : -1,
          p4_v ? o->off : 0, p4_v ? o->size : -1);
      if (itlb) CHK(t->ic_xl_itlb, "cyc %ld: IC xl_req without xl_itlb", (long)cyc);
      uint32_t va = itlb ? t->ic_xl_va : t->oc_xl_va, pa = 0; bool mh = false;
      if (!itlb && xl_stall_pct && rng.p(xl_stall_pct)) { t->xl_stall = 1; }
      else { t->xl_hit = xl_answer(va, itlb, pa, mh); t->xl_mhit = mh; t->xl_pa = pa | (va & 0x3FF); }
      t->eval();
    }
    // SOC.md 3.8: v1_soc ORs the IC's and the OC's xl_* into v1_mmu's port: a unit not asking drives its xl_* to 0
    if (!t->ic_xl_req) CHK(!t->ic_xl_itlb && t->ic_xl_va == 0, "cyc %ld: IC xl_itlb %d / xl_va %08x without xl_req",
                           (long)cyc, t->ic_xl_itlb, t->ic_xl_va);
    if (!t->oc_xl_req) CHK(t->oc_xl_va == 0, "cyc %ld: OC xl_va %08x without xl_req", (long)cyc, t->oc_xl_va);
    // the stub's memory (same-cycle read)
    if (t->m_rd) { t->m_rdata = rmem.rq(t->m_addr); t->eval(); }

    // ---- observe
    // (the stub's sticky flag is undefined until the first power-on reset clears it: +verilator+rand+reset+2 runs)
    if (!rpin) CHK(!t->proto_err, "cyc %ld: the stub's handshake checker fired", (long)cyc);
    for (int i = 0; i < BRQ_N; i++) {
      bool v = (t->bqv >> i) & 1;
      if (v && !bq_prev[i]) e_rise[i].push_back(cyc);
      if ((t->rdy >> i) & 1) e_rdy[i].push_back(cyc);
      if ((t->rdv >> i) & 1) e_rdv[i].push_back(cyc);
      if ((t->dne >> i) & 1) e_done[i].push_back(cyc);
      bq_prev[i] = v && !((t->rdy >> i) & 1);
      if (g_trace_from >= 0 && cyc >= g_trace_from && (((t->rdy | t->dne) >> i) & 1))
        printf("  [trace] cyc %ld bus requester %d%s%s\n", (long)cyc, i, (t->rdy >> i) & 1 ? " ready" : "",
               (t->dne >> i) & 1 ? " done" : "");
    }
    for (int k = 0; k < 64; k++) { ev_ic[k] += (t->ic_ev >> k) & 1; ev_oc[k] += (t->oc_ev >> k) & 1; }
    if (t->ici) last_ici = cyc;
    if (t->oci) last_oci = cyc;
    // unrequested units are silent (SOC.md 3.1.2, 3.2.2)
    if (!acc_v || o->rg == RG_SQ) CHK(!t->o_ack && !t->o_frz && t->o_rdata == 0, "cyc %ld: OC answers with no request", (long)cyc);
    if (!acc_v || o->rg != RG_SQ) CHK(!t->s_ack && !t->s_frz && t->s_rdata == 0, "cyc %ld: SQ answers with no request", (long)cyc);
    {
      const bool sic = p4_v && (o->blk == P4B_ICAA || o->blk == P4B_ICDA);
      const bool soc = p4_v && (o->blk == P4B_OCAA || o->blk == P4B_OCDA);
      const bool scr = p4_v && o->blk == P4B_CCN_CCR;
      if (!sic) CHK(!t->ic_hit && !t->ic_stall && t->ic_rdata == 0, "cyc %ld: IC array port answers unstrobed", (long)cyc);
      if (!soc) CHK(!t->oc_hit && !t->oc_stall && t->oc_rdata == 0, "cyc %ld: OC array port answers unstrobed", (long)cyc);
      if (!scr) CHK(!t->cr_hit && !t->cr_stall && t->cr_rdata == 0, "cyc %ld: CCR port answers unstrobed", (long)cyc);
    }
    // IFILL records: the lockstep IC fill
    if (t->t_v) {
      CHK(t->t_kind == 12, "cyc %ld: IC tap kind %d", (long)cyc, t->t_kind);
      CHK((t->t_addr & 31) == 0, "cyc %ld: IFILL address %08x not a line", (long)cyc, t->t_addr);
      const uint32_t pa = t->t_addr;
      const int want = (m.ccr_ & IIX) ? (int)((((pa >> 25) & 1) << 7) | ((pa >> 5) & 0x7F)) : (int)((pa >> 5) & 0xFF);
      const int other = (m.ccr_ & IIX) ? (int)((pa >> 5) & 0xFF) : (int)((((pa >> 25) & 1) << 7) | ((pa >> 5) & 0x7F));
      // (a fill started before an IIX change keeps the entry it was looked up with)
      CHK(t->t_idx == want || (cyc - last_iix_change < 300 && t->t_idx == other),
          "cyc %ld: IFILL entry %d for PA %08x, expected %d", (long)cyc, t->t_idx, pa, want);
      m.ifill(pa, t->t_idx);
      ifills.push_back({pa, t->t_idx});
      last_ifill = cyc;
    }
    // ---- the fetch (before the MA op: a CCR write captured this cycle takes effect at its end, after this cycle's
    // lookup, so the model sees this cycle's fetch first)
    if (f_on) {
      CHK(!(t->i_ack && fc.stall > 0 && fc.fx == FX_NONE), "cyc %ld: fetch acked during an ITLB refill", (long)cyc);
      if (fc.stall > 0 && fc.fx == FX_NONE) CHK(t->i_busy, "cyc %ld: if_stall without busy", (long)cyc);
      if (fc.fx != FX_NONE) CHK(t->i_ack, "cyc %ld: a poisoned fetch not acked at once", (long)cyc);
      if (t->i_ack) {
        CHK(t->i_fx == fc.fx && t->i_vec400 == (fc.fx != FX_NONE && fc.vec400), "cyc %ld: fetch fx %d/%d, want %d/%d",
            (long)cyc, t->i_fx, t->i_vec400, fc.fx, fc.vec400);
        uint32_t want = 0;
        if (fc.fx == FX_NONE && !fc.spec) want = m.fetch(fc.va, fc.pa, fc.c);
        CHK(fc.spec || t->i_data == want, "cyc %ld: fetch %08x (pa %08x c %d): %08x, model %08x", (long)cyc, fc.va, fc.pa,
            fc.c, t->i_data, want);                                    // (a spec request's word is dropped: any data)
        f_log.push_back({f_t0, cyc});
        if (f_done) f_done(fc, t->i_data, f_t0, cyc);
        n_fetch++;
        f_on = false; f_free = cyc + 1;
      } else if (fc.spec && fc.stall == 0 && !spec_wait) {
        f_on = false; f_free = cyc + 1;                     // abandoned: the fetch has moved on
      } else if (fc.spec) {
        // a spec request behind an ITLB refill: the redirect waits for its answer (SOC.md 4.1 item 4) -- held
        spec_wait = true;
        if (fc.stall > 0) fc.stall--;
        CHK(cyc - f_t0 < 200, "cyc %ld: an abandoned request the redirect waits for was never answered", (long)cyc);
        if (cyc - f_t0 >= 200) { f_on = false; f_free = cyc + 1; }
      } else {
        CHK(cyc - f_t0 < 600, "cyc %ld: fetch %08x never acked", (long)cyc, fc.va);
        if (cyc - f_t0 >= 600) { f_on = false; f_free = cyc + 1; }
        if (fc.stall > 0) fc.stall--;
        else if (f_redirect_ok && !t->i_busy && f_gen && rng.p(3)) {       // a redirect of a waiting cacheable fetch
          FReq r;
          if (f_gen(r)) { fc = r; f_t0 = cyc + 1; }
        }
      }
    } else CHK(!t->i_ack, "cyc %ld: fetch ack with no request", (long)cyc);
    // ---- the MA op
    if (o && !o->p4) {
      const bool sq = o->rg == RG_SQ;
      const bool ack = sq ? t->s_ack : t->o_ack, frz = sq ? t->s_frz : t->o_frz;
      const uint64_t rd = sq ? t->s_rdata : t->o_rdata;
      CHK(!(ack && frz), "cyc %ld: ack and frz together (%s %08x)", (long)cyc, kname(o->kind), o->va);
      const int rel = (int)(cyc - o->t0);
      if (frz) { o->nfrz++; if (o->frz_first < 0) o->frz_first = rel; o->frz_last = rel; }
      if (o->check_frz) {
        const int sh = o->shift;                            // the V-clear comparator's cycle comes first
        if (o->pred_nb) CHK(frz == (rel >= sh && rel < sh + 2), "cyc %ld: %s %08x rg %d: frz %d in MA cycle %d (C2: "
                            "exactly cycles %d and %d)", (long)cyc, kname(o->kind), o->va, o->rg, frz, rel, sh, sh + 1);
        else CHK(!frz, "cyc %ld: %s %08x rg %d: frz without a bus access (MA cycle %d)", (long)cyc, kname(o->kind),
                 o->va, o->rg, rel);
        if (sh && rel == 0) CHK(!ack, "cyc %ld: %s %08x acked in the V-clear comparator's cycle (an OCBI / OCBP / "
                                "purge hit of its entry was acked in the previous cycle)", (long)cyc, kname(o->kind), o->va);
      }
      CHK(rel < 600, "cyc %ld: %s %08x never acked (deadlock?)", (long)cyc, kname(o->kind), o->va);
      if (rel >= 600) { ma.reset(); ma_free = cyc + 1; }
      else if (ack) {
        o->tack = cyc; o->rdata = rd; o->done = true;
        const int sz = 1 << o->size;
        if (g_trace_from >= 0 && cyc >= g_trace_from)
          printf("  [trace] cyc %ld ack %s va %08x\n", (long)cyc, kname(o->kind), o->va);
        if (watched(o->pa) || (g_watch >= 0 && o->rg != RG_SQ && o->rg != RG_OCRAM &&
                               m.oc_index(o->va) == m.oc_index((uint32_t)g_watch | 0x80000000u)))
          printf("  [watch] cyc %ld..%ld %s.%d va %08x pa %08x rg %d c %d wt %d wd %llx rd %llx (ccr %04x)\n",
                 (long)o->t0, (long)cyc, kname(o->kind), sz, o->va, o->pa, o->rg, o->c, o->wt,
                 (unsigned long long)o->wdata, (unsigned long long)rd, m.ccr_);
        if ((o->kind == DK_OCBI || o->kind == DK_OCBP || o->kind == DK_TAS_PURGE) &&
            (o->rg == RG_MEM || o->rg == RG_A7) && o->c && (m.ccr_ & OCE) && m.oc_hit(m.oc_index(o->va), o->pa)) {
          vc_cyc = cyc; vc_idx = m.oc_index(o->va);
        }
        const uint64_t mv = m.access_rg(o->kind, o->rg, o->va, o->pa, o->kind == DK_MOVCA ? 4 : sz, o->wdata, o->c, o->wt);
        o->mdata = mv;
        if (k_read(o->kind))
          CHK(rd == mv, "cyc %ld: %s.%d %08x (pa %08x rg %d c %d): data %016llx, model %016llx", (long)cyc,
              kname(o->kind), sz, o->va, o->pa, o->rg, o->c, (unsigned long long)rd, (unsigned long long)mv);
        else CHK(rd == 0, "cyc %ld: %s %08x: rdata %016llx on a non-read", (long)cyc, kname(o->kind), o->va,
                 (unsigned long long)rd);
        if (sq && k_write(o->kind) && o->kind != DK_MOVCA && o->size < 2) n_sqwarn++;
        n_acc++;
        ma.reset(); ma_free = cyc + 1;
      }
    } else if (o && o->p4) {
      const bool sic = o->blk == P4B_ICAA || o->blk == P4B_ICDA, soc = o->blk == P4B_OCAA || o->blk == P4B_OCDA;
      const bool hit = sic ? t->ic_hit : soc ? t->oc_hit : t->cr_hit;
      const bool stl = sic ? t->ic_stall : soc ? t->oc_stall : t->cr_stall;
      const uint32_t rdv = sic ? t->ic_rdata : soc ? t->oc_rdata : t->cr_rdata;
      if (sic || soc) CHK(hit, "cyc %ld: array strobe %s off %07x: hit = 0", (long)cyc, o->wr ? "wr" : "rd", o->off);
      if (stl) {
        o->stalls++;
        CHK(rdv == 0, "cyc %ld: a stalled strobe returns data", (long)cyc);
        CHK(o->stalls < 600, "cyc %ld: P4 access blk %d off %07x stalled forever", (long)cyc, o->blk, o->off);
        if (o->stalls >= 600) { ma.reset(); ma_free = cyc + 1; }
      } else {
        o->tack = cyc; o->done = true; o->rdata = rdv;
        if (g_watch >= 0 && (o->blk == P4B_OCAA || o->blk == P4B_OCDA) &&
            (int)((o->off >> 5) & 0x1FF) == m.oc_index((uint32_t)g_watch | 0x80000000u))
          printf("  [watch] cyc %ld P4 %s blk %d off %07x wd %08x rd %08x\n", (long)cyc, o->wr ? "wr" : "rd", o->blk,
                 o->off, o->p4wd, rdv);
        P4Slave *s = (sic || soc) ? m.arrays() : m.ccr_slave();
        const uint32_t ccr0 = m.ccr_;
        if (o->wr) {
          s->wr(o->off, o->size, o->p4wd, false);
          if ((ccr0 ^ m.ccr_) & IIX) last_iix_change = cyc;
        } else {
          uint32_t mv = 0; bool h = s->rd(o->off, o->size, mv, false);
          o->mdata = mv;
          CHK(rdv == mv, "cyc %ld: P4 read blk %d off %07x: %08x, model %08x", (long)cyc, o->blk, o->off, rdv, mv);
          if (!sic && !soc) CHK(hit == h, "cyc %ld: CCR port hit %d, model %d (off %07x)", (long)cyc, hit, h, o->off);
        }
        if (o->arr) t->arr_strobe = 1;                                // the bridge's pulse in the capture cycle
        if (o->arr && o->size != 2) n_p4_narrow++;
        n_p4++;
        ma.reset(); ma_free = cyc + 1;
      }
    }
    if (extra_arr_strobe) { t->arr_strobe = 1; extra_arr_strobe = false; }
    // the stub's memory write lands at the end of the cycle
    if (t->m_wr) {
      if (watched(t->m_addr)) printf("  [watch] cyc %ld RTL mem write %08x be %02x %016llx\n", (long)cyc, t->m_addr,
                                     t->m_be, (unsigned long long)t->m_wdata);
      rmem.wq(t->m_addr, t->m_wdata, t->m_be);
    }
    t->clk = 1;
    t->eval();
    cyc++;
  }

  void run(int n) { for (int i = 0; i < n; i++) step(); }
  bool idle() const { return !ma && maq.empty(); }
  void run_ops(int limit = 200000) {
    int64_t end = cyc + limit;
    while ((!idle() || (f_on || !fq.empty())) && cyc < end) step();
    CHK(idle(), "run_ops: ops left after %d cycles", limit);
  }
  void drain(int n = 120) { run_ops(); run(n); }

  // ---- whole-state comparison through the P4 array port (after a drain) and of memory
  void compare_state(const char *where, bool ic = true) {
    std::vector<OpP> rds;
    for (int e = 0; e < 512; e++) { rds.push_back(ocaa_rd(e)); for (int w = 0; w < 8; w++) rds.push_back(ocda_rd(e, w)); }
    if (ic) for (int e = 0; e < 256; e++) { rds.push_back(icaa_rd(e)); for (int w = 0; w < 8; w++) rds.push_back(icda_rd(e, w)); }
    run_ops();
    (void)rds;                                                        // each read is checked against the model
    std::vector<uint32_t> keys;
    for (auto &kv : rmem.q) keys.push_back(kv.first);
    for (auto &kv : mmem.q) keys.push_back(kv.first);
    long bad = 0;
    for (uint32_t a : keys) {
      if (rmem.rq(a) != mmem.rq(a)) {
        if (bad < 5) CHK(false, "%s: memory %08x: RTL %016llx, model %016llx", where, a, (unsigned long long)rmem.rq(a),
                         (unsigned long long)mmem.rq(a));
        bad++;
      }
    }
    if (bad >= 5) CHK(false, "%s: %ld memory quadwords differ", where, bad);
  }
  void reset(bool por) {
    rpin = por; rman = !por; rcpu = true;
    if (m.ccr_ & IIX) last_iix_change = cyc;                // the reset clears CCR (and IIX with it)
    run(8);
    rpin = rman = rcpu = false;
    m.reset(por);
    run(2);
  }
  // A manual (man) or CPU-only reset that kills the access in MA (SOC.md 3.2.2 item 4: the core drops lreq.v).  The
  // WI-CACHE rule (v1_ocache.sv / v1_sq.sv headers): an access killed after its claim -- it allocated its buffer or
  // raised its request, m + 1 or later: the unit's `claimed` flop -- is completed by the RTL, so the model applies it
  // (its load data is dropped); one killed before its claim has no effect.  A P4 access is let finish first (the
  // bridge completes a strobed transaction, SOC.md 3.1.6).  Returns 1 if the killed access was applied.
  int kill_reset(bool man) {
    while (ma && ma->p4) step();
    int applied = -1;
    if (ma) {
      Op &o = *ma;
      const bool claimed = o.rg == RG_SQ ? t->s_claimed : t->o_claimed;
      if (claimed) {
        m.access_rg(o.kind, o.rg, o.va, o.pa, o.kind == DK_MOVCA ? 4 : 1 << o.size, o.wdata, o.c, o.wt);
        n_kill_applied++;
      } else n_kill_dropped++;
      applied = claimed;
      if (g_trace_from >= 0 && cyc >= g_trace_from)
        printf("  [trace] cyc %ld KILL (%s reset) %s va %08x claimed %d\n", (long)cyc, man ? "manual" : "CPU-only",
               kname(o.kind), o.va, claimed);
      o.done = true; o.tack = -1;
      // the core restarts: a killed TAS.B's later micro-ops never issue (an orphan TAS_WR would be exempt from the
      // OC's program-order gate, which holds only inside a real locked pair)
      if (o.kind == DK_TAS_PURGE || o.kind == DK_TAS_RD)
        while (!maq.empty() && !maq.front()->p4 && (maq.front()->kind == DK_TAS_RD || maq.front()->kind == DK_TAS_WR) &&
               maq.front()->va == o.va) maq.pop_front();
      ma.reset(); ma_free = cyc + 1;
    }
    rman = man; rcpu = true;
    if (man && (m.ccr_ & IIX)) last_iix_change = cyc;
    run(8);
    rman = rcpu = false;
    if (man) m.reset(false);
    run(2);
    return applied;
  }
};

// ---------------------------------------------------------------------------------------------------------------
// directed groups
static const uint32_t X = 0x8C078000u;                      // caches-sq 6.2's X (OC entry 0 with OIX = 0)

static void set_ccr(Bench &b, uint32_t v) { b.ccr_wr(v); b.run_ops(); }

// every group starts from a power-on reset with CCR = 0x0105 (KOS: ICE, CB, OCE)
static void fresh(Bench &b, uint32_t ccr = 0x0105) {
  b.drain(60);
  b.reset(true);
  b.mmu_at = b.m.mmu_at = false; b.xl_tlb = false; b.xl_stall_pct = 0;   // (the model's AT too: the SQ bursts)
  b.f_gen = nullptr; b.f_redirect_ok = false; b.f_done = nullptr;
  set_ccr(b, ccr);
  b.drain(4);
}

static void g_c1(Bench &b) {
  fresh(b);
  b.ld(X); b.drain();                                         // the line valid and clean
  auto l = b.ld(X + 4); auto s = b.st(X + 8, 0x11223344); auto l2 = b.ld(X + 8, 1);
  auto pf = b.op(DK_PREF, X + 16); auto mv = b.op(DK_MOVCA, X + 20, 2, 0xCAFE0001);
  auto ow = b.op(DK_OCBWB, X);                              // dirty hit, WBB free: 0
  b.run_ops();
  for (auto &o : {l, s, l2, pf, mv}) CHK(o->lat() == 0 && o->nfrz == 0, "c1: %s %08x hit: ack after %d, frz %d",
                                          kname(o->kind), o->va, o->lat(), o->nfrz);
  CHK(ow->lat() == 0 && ow->nfrz == 0, "c1: dirty OCBWB with a free WBB costs %d", ow->lat());
  CHK(l2->rdata == 0x3344, "c1: load after store %llx", (unsigned long long)l2->rdata);
  b.drain();
  // OCBI hit 0; OCBP miss 2 (one cycle apart: right behind, its entry's V clear would hold it one cycle, group ocbi)
  auto oi = b.op(DK_OCBI, X); auto op = b.op(DK_OCBP, X + 0x4000); op->gap = 1; b.run_ops();
  CHK(oi->lat() == 0, "c1: OCBI hit costs %d", oi->lat());
  CHK(op->lat() == 2 && op->nfrz == 2, "c1: OCBP of an absent line: ack %d frz %d (2 / 2)", op->lat(), op->nfrz);
  // OC RAM: hit timing
  fresh(b, 0x0125);
  auto rs = b.st(0x7C001000u, 0xABCD1234); auto rl = b.ld(0x7C001000u); b.run_ops();
  CHK(rs->lat() == 0 && rl->lat() == 0 && rl->rdata == 0xABCD1234, "c1: OC RAM store / load %d %d %llx", rs->lat(),
      rl->lat(), (unsigned long long)rl->rdata);
  CHK(b.ev_oc[EV_OCRAM] >= 2, "c1: event 0x0B not counted");
}

// C2: every access that needs the bus: frz exactly in MA1 and MA1 + 1, the request raised in MA1 + 1 (C3)
static void g_c2(Bench &b) {
  struct Case { const char *name; int kind; uint32_t va; int size; int rq; int ack; };   // ack: expected lat (-1 any)
  const Case cs[] = {
    {"store miss (copy-back)", DK_ST, X + 0x100, 2, BRQ_OC_FILL, 2},
    {"PREF miss", DK_PREF, X + 0x140, 2, BRQ_OC_FILL, 2},
    {"OCBI absent", DK_OCBI, X + 0x180, 2, -1, 2},
    {"OCBP absent", DK_OCBP, X + 0x180, 2, -1, 2},
    {"OCBWB absent", DK_OCBWB, X + 0x180, 2, -1, 2},
    {"OCBI P2", DK_OCBI, 0xAC078000u, 2, -1, 2},
    {"MOVCA.L absent", DK_MOVCA, X + 0x1C0, 2, -1, 2},
    {"uncached store", DK_ST, 0xAC078200u, 2, BRQ_OC_WTB, 2},
    {"uncached MOVCA.L", DK_MOVCA, 0xAC078240u, 2, BRQ_OC_WTB, 2},
    {"uncached load", DK_LD, 0xAC078280u, 2, BRQ_OC_UNC, -1},
    {"PREF P2", DK_PREF, 0xAC0782C0u, 2, BRQ_OC_UNC, -1},
    {"load miss", DK_LD, X + 0x300, 2, BRQ_OC_FILL, -1},
    {"TAS read", DK_TAS_RD, X + 0x340, 0, BRQ_OC_UNC, -1},
    {"TAS write", DK_TAS_WR, X + 0x340, 0, BRQ_OC_WTB, 2},
    {"SQ PREF", DK_PREF, 0xE0000000u, 2, BRQ_SQ, 2},
  };
  fresh(b);
  b.push(b.p4(true, P4B_CCN_CCR, 0x38, 0x0C)); b.run_ops();
  for (const Case &c : cs) {
    b.drain();
    size_t n0 = b.e_rise[c.rq < 0 ? 0 : c.rq].size();
    auto o = b.op(c.kind, c.va, c.size, 0x5A);
    b.run_ops();
    CHK(o->pred_nb && o->nfrz == 2 && o->frz_first == 0 && o->frz_last == 1, "c2: %s: frz %d cycles from %ld",
        c.name, o->nfrz, (long)o->frz_first);
    if (c.ack >= 0) CHK(o->lat() == c.ack, "c2: %s: ack after %d, expected %d", c.name, o->lat(), c.ack);
    if (c.rq >= 0) {
      CHK(b.e_rise[c.rq].size() > n0 && b.e_rise[c.rq][n0] == o->t0 + 1, "c2: %s: request not raised in MA1 + 1 (C3)",
          c.name);
    }
  }
  // no miss path: hits, the TAS purge, SQ stores, OC RAM
  b.drain();
  b.ld(X + 0x400); b.drain();
  auto h1 = b.op(DK_OCBWB, X + 0x400); auto h2 = b.op(DK_TAS_PURGE, X + 0x400); auto h3 = b.op(DK_TAS_PURGE, X + 0x440);
  auto h4 = b.op(DK_ST, 0xE0000004u); auto h5 = b.op(DK_LD, 0xE0000004u);
  b.run_ops();
  for (auto &o : {h1, h2, h3, h4, h5}) CHK(o->nfrz == 0 && o->lat() == 0, "c2: %s %08x: frz %d ack %d (0 / 0)",
                                            kname(o->kind), o->va, o->nfrz, o->lat());
}

// C6: the load-miss penalty by the Bclk phase; ack = critical rd_v + 1 with the critical beat
static void g_c6(Bench &b) {
  fresh(b);
  for (int ph = 0; ph < 2; ph++) {
    for (int w = 0; w < 8; w += 3) {
      b.drain();
      uint32_t va = X + 0x800 + 0x20 * (ph * 8 + w) + 4 * w;
      size_t n0 = b.e_rdv[BRQ_OC_FILL].size();
      auto o = b.ld(va); o->phase = ph;
      b.run_ops();
      CHK(o->lat() == (ph == 0 ? 14 : 15), "c6: load miss with MA1 in a %s cycle: penalty %d (expected %d)",
          ph ? "bce" : "non-bce", o->lat(), ph == 0 ? 14 : 15);
      CHK(b.e_rdv[BRQ_OC_FILL].size() > n0 && b.e_rdv[BRQ_OC_FILL][n0] + 1 == o->tack,
          "c6: ack %ld not in the Icyc after the critical rd_v (%ld, %zu beats)", (long)o->tack,
          b.e_rdv[BRQ_OC_FILL].size() > n0 ? (long)b.e_rdv[BRQ_OC_FILL][n0] : -1L, b.e_rdv[BRQ_OC_FILL].size() - n0);
      b.drain();
    }
    // uncached load (C7 path) and the locked TAS read
    b.drain();
    auto u = b.ld(0xAC079000u + 8 * ph); u->phase = ph; b.run_ops();
    CHK(u->lat() == (ph == 0 ? 14 : 15), "c6: uncached load penalty %d at phase %d", u->lat(), ph);
    b.drain();
    auto q = b.ld(0xAC079100u, 3); q->phase = ph; b.run_ops();      // a 64-bit uncached load: two longword cycles
    CHK(q->lat() == (ph == 0 ? 14 : 15) + 14, "c6: 64-bit uncached load %d (the stub's second longword +14)", q->lat());
  }
  // critical quadword first (the load's own quadword), then wrap: every word of a line, missed cold, returns its data
  // with the same penalty (the ldw group checks the wrap order through the beat times)
  for (int w = 0; w < 8; w++) {
    b.drain();
    auto o = b.ld(X + 0xA00 + 0x20 * w + 4 * w); o->phase = 0; b.run_ops();
    CHK(o->lat() == 14, "c6: miss on word %d: penalty %d", w, o->lat());
  }
}

// the line-fill buffer
static int ldw_extra(Bench &b, uint32_t base, int w0, int d) {    // load w0 miss, then word w0 + d at once
  b.drain();
  auto a = b.ld(base + 4 * w0); a->phase = 0;
  auto c = b.ld(base + ((4 * w0 + 4 * d) & 31));
  b.run_ops();
  return (int)(c->tack - a->tack) - 1;                            // over a hit second load
}
static void g_lfb(Bench &b) {
  fresh(b);
  // ldw: +1 +3 +3 +5 +5 +7 +7 [HW run 3]; from word 4: the wrap q2 q3 q0 q1
  const int want[7] = {1, 3, 3, 5, 5, 7, 7};
  for (int d = 1; d <= 7; d++) {
    int e = ldw_extra(b, X + 0x1000 + 0x40 * d, 0, d);
    CHK(e == want[d - 1], "lfb ldw w0 then +%d: +%d (expected +%d)", 4 * d, e, want[d - 1]);
  }
  const int dd[7] = {1, 2, 3, -4, -3, -2, -1};
  for (int i = 0; i < 7; i++) {
    int e = ldw_extra(b, X + 0x1400 + 0x40 * i, 4, dd[i]);
    CHK(e == want[i], "lfb ldw w4 then %+d words: +%d (expected +%d)", dd[i], e, want[i]);
  }
  // ldhot: a hit on another line right after the miss's unfreeze: +1
  b.drain();
  b.ld(X + 0x1800); b.drain();
  auto a = b.ld(X + 0x1C00); a->phase = 0; auto h = b.ld(X + 0x1800); b.run_ops();
  CHK(h->tack - a->tack == 2, "lfb ldhot: the hit acks %ld after the miss (expected 2: +1)", (long)(h->tack - a->tack));
  // stst / ldst: a store to the filling line waits for the whole line: ack at r + 22
  b.drain();
  auto s1 = b.st(X + 0x2000, 0xA1); s1->phase = 0; auto s2 = b.st(X + 0x2004, 0xA2); b.run_ops();
  CHK(s2->tack - (s1->t0 + 1) == 22, "lfb stst: second store +%ld over its hit (expected 22 at this phase)",
      (long)(s2->tack - (s1->t0 + 1)));
  b.drain();
  auto l1 = b.ld(X + 0x2400); l1->phase = 0; auto s3 = b.st(X + 0x2404, 0xB2); b.run_ops();
  CHK(s3->tack - (l1->tack + 1) == 8, "lfb ldst: the store +%ld after the load miss (expected 8)",
      (long)(s3->tack - (l1->tack + 1)));
  // stored bytes survive the fill; a load of another word of a store-miss line returns memory [HW run 2]
  b.drain();
  auto s4 = b.st(X + 0x2800, 0xA1A1A1A1u); auto s5 = b.st(X + 0x2804, 0xA2A2A2A2u);
  auto l5 = b.ld(X + 0x2808); auto l6 = b.ld(X + 0x2800); auto l7 = b.ld(X + 0x2804);
  b.run_ops();
  CHK(l6->rdata == 0xA1A1A1A1u && l7->rdata == 0xA2A2A2A2u, "lfb: stored words during the fill: %llx %llx",
      (unsigned long long)l6->rdata, (unsigned long long)l7->rdata);
  (void)s4; (void)s5; (void)l5;                                     // l5: memory under the fill (the model checks it)
  // stld: a load to the stored quadword of a store-miss line (timing reported by hwrows)
  // store-miss stream: two store misses to different entries, back to back: LFB-bound, 22 per line
  b.drain();
  auto m1 = b.st(X + 0x3000, 1); m1->phase = 0; auto m2 = b.st(X + 0x3020, 2); auto m3 = b.st(X + 0x3040, 3);
  b.run_ops();
  std::vector<int64_t> rr;
  for (int64_t c : b.e_rdy[BRQ_OC_FILL]) if (c >= m1->t0) rr.push_back(c);
  CHK(rr.size() >= 3 && rr[1] - rr[0] == 22 && rr[2] - rr[1] == 22,
      "lfb store-miss stream: line requests %ld / %ld apart (expected 22)", rr.size() >= 3 ? (long)(rr[1] - rr[0]) : -1L,
      rr.size() >= 3 ? (long)(rr[2] - rr[1]) : -1L);
  (void)m2; (void)m3;
  // a load miss during the previous fill's tail costs the isolated latency (fq), including a dependent chain
  b.drain();
  auto c1 = b.ld(X + 0x3400); c1->phase = 0; auto c2 = b.ld(X + 0x3420); auto c3 = b.ld(X + 0x3440);
  b.run_ops();
  CHK(c2->lat() == 15 && c3->lat() == 15, "lfb: back-to-back load misses %d / %d (expected 15 / 15: the tail overlaps)",
      c2->lat(), c3->lat());
  // a load miss to the entry a load fill is filling with another line: queued at once (fq on the same entry, M3), the
  // isolated latency like any fill-tail miss [HW core-integration ci4 miss_em 28.137: two lines 16 KB apart, loads
  // only; waiting for the first fill's completion gave 32]; the first line is then gone (its completion sets no V)
  b.drain();
  auto e1 = b.ld(X + 0x3800); e1->phase = 0; auto e2 = b.ld(X + 0x3800 + 0x4000); b.run_ops();
  CHK(e2->lat() == 15, "lfb: a load miss on the entry being filled by a load: %d (expected 15: the same-entry fq)",
      e2->lat());
  auto e3 = b.ld(X + 0x3800); b.run_ops();
  CHK(e3->lat() > 10, "lfb: the replaced line still hit after the same-entry fq (%d)", e3->lat());
  // while the queued fill owns the entry, the first fill's line serves nobody (the reference model has it replaced):
  // PREF A, PREF B (same entry: fq), then a load of A before A's fill has completed misses
  b.drain();
  auto pa = b.op(DK_PREF, X + 0x3A00); pa->phase = 0; auto pb = b.op(DK_PREF, X + 0x3A00 + 0x4000);
  auto la = b.ld(X + 0x3A08); b.run_ops();
  CHK(la->lat() > 20, "lfb: a load of the line the same-entry fq replaces hit the old fill (%d; PREFs acked %ld / %ld)",
      la->lat(), (long)pa->tack, (long)pb->tack);
  // a miss to the entry a store-allocated fill is filling waits for that fill (its bytes stay until it completes)
  b.drain();
  auto s1e = b.st(X + 0x3900, 5); s1e->phase = 0; auto s2e = b.ld(X + 0x3900 + 0x4000); b.run_ops();
  CHK(s2e->tack > s1e->tack + 16, "lfb: a miss on the entry of a store-allocated fill did not wait (%ld)",
      (long)(s2e->tack - s1e->tack));
  // MOVCA.L and a store miss wait for the LFB
  b.drain();
  auto p1 = b.op(DK_PREF, X + 0x3C00); p1->phase = 0; auto mv = b.op(DK_MOVCA, X + 0x3C20, 2, 7);
  b.run_ops();
  CHK(mv->tack >= p1->tack + 20, "lfb: MOVCA.L allocation did not wait for the PREF's fill (%ld)",
      (long)(mv->tack - p1->tack));
  b.compare_state("lfb");
}

static void g_wbb_wtb(Bench &b) {
  fresh(b);
  // dirty victim: the fill first, then the write-back; data and memory right
  b.st(X + 0x10, 0xD1D1D1D1u); b.drain();
  size_t nf = b.e_rdy[BRQ_OC_FILL].size(), nw = b.e_rdy[BRQ_OC_WBB].size();
  auto l = b.ld(X + 0x4010); b.run_ops(); b.drain();
  CHK(b.e_rdy[BRQ_OC_FILL].size() == nf + 1 && b.e_rdy[BRQ_OC_WBB].size() == nw + 1 &&
      b.e_rdy[BRQ_OC_FILL][nf] < b.e_rdy[BRQ_OC_WBB][nw], "wbb: the dirty victim's write-back does not follow the fill");
  CHK((b.rmem.rq(0x0C078010u) & 0xFFFFFFFFull) == 0xD1D1D1D1u, "wbb: victim not written back");
  (void)l;
  // dirty OCBP / OCBWB: 0 isolated, one line per ~12 back to back (the write tenure)
  for (int i = 0; i < 4; i++) b.st(X + 0x5000 + 0x20 * i, i);
  b.drain();
  auto o1 = b.op(DK_OCBWB, X + 0x5000); o1->phase = 0; auto o2 = b.op(DK_OCBWB, X + 0x5020);
  auto o3 = b.op(DK_OCBP, X + 0x5040); auto o4 = b.op(DK_OCBP, X + 0x5060);
  b.run_ops();
  CHK(o1->lat() == 0, "wbb: isolated dirty OCBWB costs %d", o1->lat());
  CHK(o3->tack - o2->tack == 12 && o4->tack - o3->tack == 12, "wbb: dirty OCBx back to back %ld / %ld apart (12)",
      (long)(o3->tack - o2->tack), (long)(o4->tack - o3->tack));
  // WTB: an uncached store stream is one store per write tenure (10); the load after a store waits for it
  b.drain();
  auto w1 = b.st(0xAC07B000u, 1); w1->phase = 0; auto w2 = b.st(0xAC07B004u, 2); auto w3 = b.st(0xAC07B008u, 3);
  b.run_ops();
  CHK(w3->tack - w2->tack == 10, "wtb: uncached store stream %ld per store (10)", (long)(w3->tack - w2->tack));
  b.drain();
  auto ws = b.st(0xAC07B100u, 0x77); auto wl = b.ld(0xAC07B100u); b.run_ops();
  int64_t done_w = -1, rise_u = -1;
  for (int64_t c : b.e_done[BRQ_OC_WTB]) if (c >= ws->t0) { done_w = c; break; }
  for (int64_t c : b.e_rise[BRQ_OC_UNC]) if (c >= ws->t0) { rise_u = c; break; }
  CHK(rise_u > done_w && wl->rdata == 0x77, "wtb: the uncached load did not wait for the WTB (done %ld, request %ld)",
      (long)done_w, (long)rise_u);
  // program order behind a queued fill: a dirty write-back occupies the bus, a store miss's fill waits for it, and an
  // uncached store to that line must not overtake the fill (the BIU drains the WTB before any read): the cached word
  // is memory's value before the uncached store, as program order says
  b.drain();
  b.st(X + 0x6000, 0x11); b.drain();
  const uint64_t old = b.rmem.rq(0x0C07E020u);
  b.op(DK_OCBWB, X + 0x6000); b.st(X + 0x6020 + 8, 0x22); auto us = b.st(0xAC07E024u, 0x33);
  auto lw = b.ld(X + 0x6024); b.run_ops(); b.drain();
  CHK(lw->rdata == (old >> 32), "wtb: an uncached store overtook an older queued fill: cached word %llx, memory had %llx",
      (unsigned long long)lw->rdata, (unsigned long long)(old >> 32));
  int64_t fr = -1, wr = -1;
  for (int64_t c : b.e_rdy[BRQ_OC_FILL]) if (c >= us->t0 - 4 && fr < 0) fr = c;
  for (int64_t c : b.e_rdy[BRQ_OC_WTB]) if (c >= us->t0 && wr < 0) wr = c;
  CHK(fr >= 0 && wr > fr, "wtb: the WTB write (%ld) was accepted before the older fill (%ld)", (long)wr, (long)fr);
  // ... and behind a second line request: PREF B's fill waits for the bus (a write-back on it), PREF C needs the
  // second slot, then TAS.B on C's P2 alias: its locked read and write must not overtake C's fill
  b.drain();
  b.st(X + 0x6100, 0x44); b.drain();
  const uint64_t oldc = b.rmem.rq(0x0C07E1C0u);
  b.op(DK_OCBWB, X + 0x6100); b.op(DK_PREF, X + 0x6180); b.op(DK_PREF, X + 0x61C0);
  b.op(DK_TAS_PURGE, 0xAC07E1C5u, 0); b.op(DK_TAS_RD, 0xAC07E1C5u, 0); b.op(DK_TAS_WR, 0xAC07E1C5u, 0, 0x80);
  auto lc = b.ld(X + 0x61C0, 3); b.run_ops(); b.drain();
  CHK(lc->rdata == oldc, "wtb: TAS.B overtook an older queued fill: cached %016llx, memory had %016llx",
      (unsigned long long)lc->rdata, (unsigned long long)oldc);
  b.compare_state("wbb/wtb");
}

static void g_ops(Bench &b) {
  fresh(b);
  // OCBI of a dirty line: data lost, memory keeps the old value, a reload returns it [HW run 1]
  const uint64_t old = b.rmem.rq(0x0C07C000u) & 0xFFFFFFFFu;
  b.st(0x8C07C000u, 0x0BAD0BADu); b.op(DK_OCBI, 0x8C07C000u); auto r = b.ld(0x8C07C000u); b.run_ops(); b.drain();
  CHK(r->rdata == old, "ops: OCBI dirty: reload %llx, memory had %llx", (unsigned long long)r->rdata,
      (unsigned long long)old);
  // OCBI through the P2 alias of a dirty line: no effect; OCBP through the P0 alias: purged and written back
  b.st(0x8C07C000u, 0x1111u); b.op(DK_OCBI, 0xAC07C000u); auto a1 = b.ocaa_rd(0); b.run_ops();
  CHK(a1->rdata == 0x0C07C003u, "ops: OCBI @P2 alias: entry %08x (0c07c003)", (uint32_t)a1->rdata);
  b.op(DK_OCBP, 0x0C07C000u); auto a2 = b.ocaa_rd(0); b.run_ops(); b.drain();
  CHK(a2->rdata == 0x0C07C000u && (b.rmem.rq(0x0C07C000u) & 0xFFFFFFFF) == 0x1111u,
      "ops: OCBP @P0 alias: entry %08x, memory %llx", (uint32_t)a2->rdata, (unsigned long long)b.rmem.rq(0x0C07C000u));
  // OCBWB of a dirty line: V stays, U <- 0, memory updated [HW run 2: 0x0C078001]
  b.st(X, 0xBBBB0002u); auto aa = b.ocaa_rd(0); auto d0 = b.ocda_rd(0, 0); b.op(DK_OCBWB, X); auto ab = b.ocaa_rd(0);
  b.run_ops(); b.drain();
  CHK(aa->rdata == 0x0C078003u && d0->rdata == 0xBBBB0002u && ab->rdata == 0x0C078001u,
      "ops: dirty / OCBWB entries %08x %08x %08x", (uint32_t)aa->rdata, (uint32_t)d0->rdata, (uint32_t)ab->rdata);
  // MOVCA.L allocation (runs 2, 3): Y = X + 16 KB, stale X data C0DE000k
  for (int vict = 0; vict < 2; vict++) {
    const int ws[4] = {0, 4, 1, 5};
    for (int wi = 0; wi < 4; wi++) {
      const int w = ws[wi];
      b.drain();
      for (int k = 0; k < 8; k++) b.st(X + 4 * k, 0xC0DE0000u + k);
      if (!vict) b.op(DK_OCBWB, X);                            // clean victim X, stale data in the entry
      b.op(DK_MOVCA, X + 0x4000 + 4 * w, 2, 0x1111AAAAu + w);
      std::vector<OpP> rd;
      for (int k = 0; k < 8; k++) rd.push_back(b.ocda_rd(0, k));
      auto e = b.ocaa_rd(0);
      b.run_ops();
      CHK(e->rdata == 0x0C07C003u, "ops: MOVCA.L entry %08x", (uint32_t)e->rdata);
      for (int k = 0; k < 8; k++) {
        uint32_t want = k == w ? 0x1111AAAAu + w : (k == (w ^ 1)) ? 0 : vict ? 0 : 0xC0DE0000u + k;
        CHK(rd[k]->rdata == want, "ops: MOVCA.L w%d %s victim: word %d = %08x (expected %08x)", w,
            vict ? "dirty" : "clean", k, (uint32_t)rd[k]->rdata, want);
      }
      b.op(DK_OCBP, X + 0x4000); b.run_ops(); b.drain();
      if (vict) CHK((b.rmem.rq(0x0C078000u) & 0xFFFFFFFF) == 0xC0DE0000u, "ops: MOVCA.L dirty victim not written back");
    }
  }
  // MOVCA.L then MOVCA.L w3 (a hit: plain store)
  b.drain();
  b.st(X, 5); b.op(DK_MOVCA, X + 0x4000, 2, 0x1111AAAAu); b.op(DK_MOVCA, X + 0x400C, 2, 0x1111BBBBu);
  std::vector<OpP> rd;
  for (int k = 0; k < 8; k++) rd.push_back(b.ocda_rd(0, k));
  b.run_ops();
  const uint32_t w3[8] = {0x1111AAAAu, 0, 0, 0x1111BBBBu, 0, 0, 0, 0};
  for (int k = 0; k < 8; k++) CHK(rd[k]->rdata == w3[k], "ops: MOVCA w0 then w3: word %d %08x", k, (uint32_t)rd[k]->rdata);
  // MOVCA @P2 / PREF @P2 with Y in the entry: no allocation, memory written [HW run 2]
  b.drain();
  b.ld(0x8C07C000u); b.op(DK_OCBWB, 0x8C07C000u); b.op(DK_PREF, 0xAC078000u); b.op(DK_MOVCA, 0xAC078000u, 2, 0x3333CCCCu); auto e2 = b.ocaa_rd(0);
  b.run_ops(); b.drain();
  CHK(e2->rdata == 0x0C07C001u && (b.rmem.rq(0x0C078000u) & 0xFFFFFFFF) == 0x3333CCCCu,
      "ops: P2 PREF / MOVCA: entry %08x", (uint32_t)e2->rdata);
  // write-through (CCR.WT, P0): a store miss does not allocate, a load miss does, a store hit keeps U = 0 [HW run 1]
  set_ccr(b, 0x0107);
  b.drain();
  b.st(0x0C07D000u, 0x57); auto w1 = b.ocaa_rd(0x080); b.ld(0x0C07D000u); b.st(0x0C07D004u, 0x58);
  auto w2 = b.ocaa_rd(0x080); b.run_ops(); b.drain();
  CHK((w1->rdata & 1) == 0 && w2->rdata == 0x0C07D001u && (b.rmem.rq(0x0C07D000u) >> 32) == 0x58,
      "ops: write-through: %08x %08x", (uint32_t)w1->rdata, (uint32_t)w2->rdata);
  set_ccr(b, 0x0105);
  // TAS.B: purge (a dirty line is written back), locked read, write
  b.drain();
  b.st(0x8C07E000u, 0x01u, 0); b.op(DK_TAS_PURGE, 0x8C07E000u, 0); auto tr = b.op(DK_TAS_RD, 0x8C07E000u, 0);
  auto tw = b.op(DK_TAS_WR, 0x8C07E000u, 0, 0x81); b.run_ops(); b.drain();
  CHK(tr->rdata == 0x01 && (b.rmem.rq(0x0C07E000u) & 0xFF) == 0x81, "ops: TAS.B read %llx, memory %llx",
      (unsigned long long)tr->rdata, (unsigned long long)b.rmem.rq(0x0C07E000u));
  (void)tw;
  b.compare_state("ops");
}

static void g_arrays(Bench &b) {
  fresh(b);
  // formats [HW run 2]: after a load 0x0C078001, after a store 0x0C078003; data word 0 new, word 1 memory
  b.ld(X); b.drain(); auto a0 = b.ocaa_rd(0); b.st(X, 0xBBBB0002u); auto a1 = b.ocaa_rd(0); auto d1 = b.ocda_rd(0, 1);
  b.run_ops();
  CHK(a0->rdata == 0x0C078001u && a1->rdata == 0x0C078003u, "arrays: OCAA %08x %08x", (uint32_t)a0->rdata,
      (uint32_t)a1->rdata);
  CHK(d1->rdata == (b.rmem.rq(0x0C078000u) >> 32), "arrays: OCDA w1 %08x", (uint32_t)d1->rdata);
  // the first strobe of an array access stalls: capture one cycle later
  CHK(a0->stalls == 1 && a0->lat() == 1, "arrays: array read stalls %d, captured after %d (1 / 1)", a0->stalls, a0->lat());
  // data-array write: the value changes, U does not, memory keeps its value [HW run 2]
  b.drain();
  b.op(DK_OCBWB, X); b.push(b.p4(true, P4B_OCDA, 0x05000004u, 0x4444000Au)); auto a2 = b.ocaa_rd(0);
  auto l = b.ld(X + 4); b.run_ops(); b.drain();
  CHK(a2->rdata == 0x0C078001u && l->rdata == 0x4444000Au, "arrays: OCDA write %08x %llx", (uint32_t)a2->rdata,
      (unsigned long long)l->rdata);
  // non-associative 0 on a dirty line: written back, invalid; tag | V: written back, valid and clean
  b.st(X, 0x7777000Cu); b.push(b.p4(true, P4B_OCAA, 0x04000000u, 0)); auto a3 = b.ocaa_rd(0); b.run_ops(); b.drain();
  CHK(a3->rdata == 0 && (b.rmem.rq(0x0C078000u) & 0xFFFFFFFF) == 0x7777000Cu,
      "arrays: non-assoc 0 on dirty: %08x", (uint32_t)a3->rdata);
  b.ld(X); b.st(X, 0x7777000Du); b.push(b.p4(true, P4B_OCAA, 0x04000000u, 0x0C078001u)); auto a4 = b.ocaa_rd(0);
  b.run_ops(); b.drain();
  CHK(a4->rdata == 0x0C078001u && (b.rmem.rq(0x0C078000u) & 0xFFFFFFFF) == 0x7777000Du,
      "arrays: non-assoc tag|V on dirty: %08x", (uint32_t)a4->rdata);
  // non-assoc 0xE0000000 | tag, V = 0 reads back the tag only [HW run 1: 0x0C080000]
  b.push(b.p4(true, P4B_OCAA, 0x04000000u, 0xE0000000u | 0x0C080000u)); auto a5 = b.ocaa_rd(0); b.run_ops();
  CHK(a5->rdata == 0x0C080000u, "arrays: non-assoc E0000000|tag: %08x", (uint32_t)a5->rdata);
  // associative: mismatch no-op; U = 0 V = 1 on dirty: written back, valid; P2 / P0 forms match; bit-25 mirror not
  b.drain();
  b.st(X, 0x4444000Au); b.push(b.p4(true, P4B_OCAA, 0x04000008u, 0x8C07C001u)); auto s0 = b.ocaa_rd(0);
  b.push(b.p4(true, P4B_OCAA, 0x04000008u, 0x8C078001u)); auto s1 = b.ocaa_rd(0);
  b.run_ops(); b.drain();
  CHK(s0->rdata == 0x0C078003u && s1->rdata == 0x0C078001u && (b.rmem.rq(0x0C078000u) & 0xFFFFFFFF) == 0x4444000Au,
      "arrays: assoc mismatch %08x, U=0 V=1 %08x", (uint32_t)s0->rdata, (uint32_t)s1->rdata);
  const uint32_t forms[3] = {0xAC078000u, 0x0C078000u, 0x8E078000u};
  for (int i = 0; i < 3; i++) {
    b.ld(X); b.push(b.p4(true, P4B_OCAA, 0x04000008u, forms[i])); auto s = b.ocaa_rd(0); b.run_ops();
    CHK(s->rdata == (i < 2 ? 0x0C078000u : 0x0C078001u), "arrays: assoc data %08x V=0: entry %08x", forms[i],
        (uint32_t)s->rdata);
  }
  // associative with the TLB (AT = 1 view): a UTLB miss is no operation, a hit translates, a multiple hit is nothing
  b.drain();
  b.xl_tlb = true; b.tlb.clear(); b.tlb_mhit.clear();
  b.tlb[0x10000000u >> 10] = 0x0C078000u >> 10;
  b.tlb[0x10001000u >> 10] = 0x0C078000u >> 10; b.tlb_mhit[0x10001000u >> 10] = true;
  b.ld(X); b.push(b.p4(true, P4B_OCAA, 0x04000008u, 0x20000000u)); auto t0 = b.ocaa_rd(0);
  b.push(b.p4(true, P4B_OCAA, 0x04000008u, 0x10001000u)); auto t1 = b.ocaa_rd(0);
  b.push(b.p4(true, P4B_OCAA, 0x04000008u, 0x10000000u)); auto t2 = b.ocaa_rd(0);
  b.run_ops();
  CHK(t0->rdata == 0x0C078001u && t1->rdata == 0x0C078001u && t2->rdata == 0x0C078000u,
      "arrays: assoc via TLB: %08x %08x %08x", (uint32_t)t0->rdata, (uint32_t)t1->rdata, (uint32_t)t2->rdata);
  // xl_stall: the associative write is repeated (stall) until the UTLB port is free
  b.xl_stall_pct = 60;
  b.ld(X); b.push(b.p4(true, P4B_OCAA, 0x04000008u, 0x10000000u)); auto t3 = b.ocaa_rd(0); b.run_ops();
  CHK(t3->rdata == 0x0C078000u, "arrays: assoc with xl_stall: %08x", (uint32_t)t3->rdata);
  b.xl_stall_pct = 0; b.xl_tlb = false;
  // a write-back that finds the WBB full stalls the array write
  b.drain();
  b.st(X + 0x20, 1); b.st(X + 0x40, 2); b.drain();
  b.op(DK_OCBWB, X + 0x20); auto nw = b.push(b.p4(true, P4B_OCAA, 0x04000040u, 0)); b.run_ops();
  CHK(nw->stalls > 3, "arrays: a non-assoc write-back with the WBB full did not wait (%d stalls)", nw->stalls);
  // a strobe stalls while a fill targets the entry
  b.drain();
  auto fl = b.ld(X + 0x60 + 0x4000); auto sa = b.ocaa_rd(3); b.run_ops();
  CHK(sa->tack > fl->tack + 8, "arrays: OCAA read of the entry being filled did not wait");
  // IC arrays: ICAA / ICDA of a filled line, associative mismatch / match (V = 0), ICI keeps tags
  b.drain();
  b.fq.push_back(FReq{0x8C014800u, 0x0C014800u, true}); b.run_ops(); b.drain();
  auto i0 = b.icaa_rd(0x40); auto i1 = b.icda_rd(0x40, 3);
  b.push(b.p4(true, P4B_ICAA, 0x00000808u, 0x8C015800u)); auto i2 = b.icaa_rd(0x40);
  b.push(b.p4(true, P4B_ICAA, 0x00000808u, 0xAC014800u)); auto i3 = b.icaa_rd(0x40);
  b.run_ops();
  CHK(i0->rdata == 0x0C014801u && i2->rdata == 0x0C014801u && i3->rdata == 0x0C014800u,
      "arrays: ICAA %08x, assoc mismatch %08x, match %08x", (uint32_t)i0->rdata, (uint32_t)i2->rdata, (uint32_t)i3->rdata);
  CHK(i1->rdata == (uint32_t)(b.rmem.rq(0x0C014808u) >> 32), "arrays: ICDA word 3 %08x", (uint32_t)i1->rdata);
  b.fq.push_back(FReq{0x8C014800u, 0x0C014800u, true}); b.run_ops(); b.drain();
  set_ccr(b, 0x0905);
  auto i4 = b.icaa_rd(0x40); b.run_ops();
  CHK(i4->rdata == 0x0C014800u, "arrays: ICI keeps the tag: %08x", (uint32_t)i4->rdata);
  // IC non-assoc write and a data-array write, then fetches return the array's contents (a fetch of another line
  // first takes the forced miss the array accesses left)
  b.push(b.p4(true, P4B_ICAA, 0x00000820u, 0x0C014821u)); b.push(b.p4(true, P4B_ICDA, 0x01000820u, 0x12345678u));
  b.run_ops();
  uint32_t got = 0;
  b.f_done = [&](const FReq &, uint32_t d, int64_t, int64_t) { got = d; };
  b.fq.push_back(FReq{0x8C014800u, 0x0C014800u, true}); b.run_ops(); b.drain();
  b.fq.push_back(FReq{0x8C014820u, 0x0C014820u, true}); b.run_ops(); b.drain();
  CHK(got == 0x12345678u, "arrays: a fetch of an array-written IC line returned %08x", got);
  // ... and right after an array access the forced miss refills that line from memory (model and RTL agree through
  // the IFILL record)
  b.push(b.p4(true, P4B_ICDA, 0x01000820u, 0x9ABCDEF0u)); b.run_ops();
  b.fq.push_back(FReq{0x8C014820u, 0x0C014820u, true}); b.run_ops(); b.drain();
  CHK(got == (uint32_t)b.rmem.rq(0x0C014820u), "arrays: the forced miss did not refill the array-written line (%08x)",
      got);
  b.f_done = nullptr;
  // Size (SOC.md 3.1.4, one rule for all eight array slaves): a byte / word access reads 0 and writes nothing -- no
  // write-back of a dirty line, no xl_req for an associative one -- and keeps the array timing (1 stall, capture next)
  b.drain();
  b.ld(X); b.st(X, 0x5A5A0001u); b.fq.push_back(FReq{0x8C014800u, 0x0C014800u, true}); b.run_ops(); b.drain();
  for (int sz = 0; sz < 2; sz++) {
    const uint64_t m0 = b.rmem.rq(0x0C078000u);
    const long xl0 = b.n_xl;
    std::vector<OpP> rd = {b.push(b.p4(false, P4B_OCAA, 0x04000000u, 0, sz)), b.push(b.p4(false, P4B_OCDA, 0x05000000u, 0, sz)),
                           b.push(b.p4(false, P4B_ICAA, 0x00000800u, 0, sz)), b.push(b.p4(false, P4B_ICDA, 0x01000800u, 0, sz))};
    std::vector<OpP> wr = {b.push(b.p4(true, P4B_OCAA, 0x04000000u, 0, sz)),             // non-assoc 0 on a dirty line
                           b.push(b.p4(true, P4B_OCAA, 0x04000008u, 0x8C078000u, sz)),   // assoc, matching, V = 0
                           b.push(b.p4(true, P4B_OCDA, 0x05000000u, 0xDEADBEEFu, sz)),
                           b.push(b.p4(true, P4B_ICAA, 0x00000800u, 0, sz)),
                           b.push(b.p4(true, P4B_ICAA, 0x00000808u, 0x8C014800u, sz)),   // assoc, matching, V = 0
                           b.push(b.p4(true, P4B_ICDA, 0x01000800u, 0xDEADBEEFu, sz))};
    auto oa = b.ocaa_rd(0); auto od = b.ocda_rd(0, 0); auto ia = b.icaa_rd(0x40); auto id = b.icda_rd(0x40, 0);
    b.run_ops(); b.drain();
    for (auto &r : rd)
      CHK(r->rdata == 0 && r->stalls == 1 && r->lat() == 1, "arrays: size %d read blk %d: %08x, %d stalls, captured "
          "after %d (0, 1, 1)", sz, r->blk, (uint32_t)r->rdata, r->stalls, r->lat());
    for (auto &w : wr)
      CHK(w->stalls == 1 && w->lat() == 1, "arrays: size %d write blk %d off %07x: %d stalls, captured after %d (1, 1)",
          sz, w->blk, w->off, w->stalls, w->lat());
    CHK(oa->rdata == 0x0C078003u && od->rdata == 0x5A5A0001u, "arrays: size %d writes changed the OC: OCAA %08x "
        "(0c078003) OCDA %08x (5a5a0001)", sz, (uint32_t)oa->rdata, (uint32_t)od->rdata);
    CHK(ia->rdata == 0x0C014801u && id->rdata == (uint32_t)b.rmem.rq(0x0C014800u), "arrays: size %d writes changed "
        "the IC: ICAA %08x (0c014801) ICDA %08x", sz, (uint32_t)ia->rdata, (uint32_t)id->rdata);
    CHK(b.rmem.rq(0x0C078000u) == m0, "arrays: a size %d OCAA write wrote the dirty line back", sz);
    CHK(b.n_xl == xl0, "arrays: a size %d associative write asked v1_mmu (%ld xl_req cycles)", sz, b.n_xl - xl0);
  }
  // ... and the same longword forms act (the control: they do write back / invalidate)
  { auto w = b.push(b.p4(true, P4B_OCAA, 0x04000008u, 0x8C078000u)); auto oa = b.ocaa_rd(0); b.run_ops(); b.drain();
    CHK(oa->rdata == 0x0C078000u && (uint32_t)b.rmem.rq(0x0C078000u) == 0x5A5A0001u, "arrays: the longword control: "
        "OCAA %08x, memory %08x", (uint32_t)oa->rdata, (uint32_t)b.rmem.rq(0x0C078000u)); (void)w; }
  b.compare_state("arrays");
}

static void g_ocram(Bench &b) {
  // OIX = 0: entry {A13, 1, A[11:5]}, A12 and A[25:14] ignored [HW run 1]
  fresh(b, 0x0125);
  const uint32_t al1[4] = {0x7C000000u, 0x7C001000u, 0x7C004000u, 0x7E001000u};
  const uint32_t al2[4] = {0x7C002000u, 0x7C003000u, 0x7DFFF000u, 0x7FFFF000u};
  b.st(al1[0], 0xA1); b.st(al2[0], 0xA2);
  std::vector<OpP> r;
  for (int i = 0; i < 4; i++) { r.push_back(b.ld(al1[i])); r.push_back(b.ld(al2[i])); }
  auto e128 = b.ocda_rd(128, 0); auto e384 = b.ocda_rd(384, 0); auto aa = b.ocaa_rd(128);
  b.st(0x7C000FE0u, 0xF0); auto e255 = b.ocda_rd(255, 0);
  b.run_ops();
  for (int i = 0; i < 4; i++) CHK(r[2 * i]->rdata == 0xA1 && r[2 * i + 1]->rdata == 0xA2, "ocram OIX=0: %08x %08x",
                                  al1[i], al2[i]);
  CHK(e128->rdata == 0xA1 && e384->rdata == 0xA2 && e255->rdata == 0xF0 && (aa->rdata & 3) == 0,
      "ocram: data array shows the RAM: %08x %08x %08x, tag word %08x", (uint32_t)e128->rdata, (uint32_t)e384->rdata,
      (uint32_t)e255->rdata, (uint32_t)aa->rdata);
  // cached accesses use index bit 7 = 0: 8C001000 (index 0x080 -> 0x000) goes to entry 0
  b.ld(0x8C001000u); auto c0 = b.ocaa_rd(0); b.run_ops();
  CHK(c0->rdata == 0x0C001001u, "ocram: cached index bit 7 not forced to 0: %08x", (uint32_t)c0->rdata);
  // OIX = 1: {A25, 1, A[11:5]}, A[13:12] ignored
  set_ccr(b, 0x01A5);
  b.st(0x7C000000u, 0xB1); b.st(0x7E000000u, 0xB2);
  auto o1 = b.ld(0x7C003000u); auto o2 = b.ld(0x7E002000u); auto o3 = b.ld(0x7DFFF000u); b.run_ops();
  CHK(o1->rdata == 0xB1 && o2->rdata == 0xB2 && o3->rdata == 0xB1, "ocram OIX=1: %llx %llx %llx",
      (unsigned long long)o1->rdata, (unsigned long long)o2->rdata, (unsigned long long)o3->rdata);
  // sizes: byte / word / quad
  b.st(0x7C001008u, 0x1122334455667788ull, 3); auto q1 = b.ld(0x7C00100Au, 1); auto q2 = b.ld(0x7C00100Fu, 0);
  b.run_ops();
  CHK(q1->rdata == 0x5566 && q2->rdata == 0x11, "ocram: sizes %llx %llx", (unsigned long long)q1->rdata,
      (unsigned long long)q2->rdata);
  b.compare_state("ocram");
}

static void g_index(Bench &b) {
  // OIX = 1: 8C07E000 -> entry 0 (0x0C07E001), 8E07E000 -> entry 256 (0x0E07E001) [HW run 1]
  fresh(b, 0x0185);
  b.ld(0x8C07E000u); b.ld(0x8E07E000u); auto e0 = b.ocaa_rd(0); auto e256 = b.ocaa_rd(256); b.run_ops();
  CHK(e0->rdata == 0x0C07E001u && e256->rdata == 0x0E07E001u, "index OIX=1: %08x %08x", (uint32_t)e0->rdata,
      (uint32_t)e256->rdata);
  // IIX = 1: 8C015400 -> IC entry 32, 8E015400 -> entry 160; IIX = 0: 8C015400 -> entry 160
  fresh(b, 0x8105);
  b.ifills.clear();
  b.fq.push_back(FReq{0x8C015400u, 0x0C015400u, true}); b.fq.push_back(FReq{0x8E015400u, 0x0E015400u, true});
  b.run_ops(); b.drain();
  CHK(b.ifills.size() == 2 && b.ifills[0].second == 32 && b.ifills[1].second == 160, "index IIX=1: entries %d %d",
      b.ifills.size() > 0 ? b.ifills[0].second : -1, b.ifills.size() > 1 ? b.ifills[1].second : -1);
  fresh(b, 0x0105);
  b.ifills.clear();
  b.fq.push_back(FReq{0x8C015400u, 0x0C015400u, true}); b.run_ops(); b.drain();
  CHK(b.ifills.size() == 1 && b.ifills[0].second == 160, "index IIX=0: entry %d",
      b.ifills.empty() ? -1 : b.ifills[0].second);
}

static void g_ccr(Bench &b) {
  fresh(b, 0);
  auto w = b.ccr_wr(0x090D); auto r = b.ccr_rd(); b.run_ops();
  CHK(r->rdata == 0x0105, "ccr: 0x090D reads back %08x (0x0105)", (uint32_t)r->rdata);
  CHK(b.last_ici == w->tack && b.last_oci == w->tack, "ccr: ICI / OCI not pulsed in the write's capture cycle");
  CHK(b.t->ccr_f == 0x25, "ccr: fields %02x", b.t->ccr_f);                  // {iix ice oix ora cb wt oce} = ice cb oce
  b.push(b.p4(true, P4B_CCN_CCR, 0x38, 0xFFFFFFFFu)); auto q0 = b.push(b.p4(false, P4B_CCN_CCR, 0x38));
  b.push(b.p4(true, P4B_CCN_CCR, 0x103C, 0x10)); auto q1 = b.push(b.p4(false, P4B_CCN_CCR, 0x3C));
  b.push(b.p4(true, P4B_CCN_CCR, 0x38, 0x0C, 1)); b.push(b.p4(true, P4B_CCN_CCR, 0x3C, 0x0C, 0));   // ignored sizes
  auto q2 = b.push(b.p4(false, P4B_CCN_CCR, 0x38)); auto q4 = b.push(b.p4(false, P4B_CCN_CCR, 0x3C));
  auto rb = b.ccr_rd(0x1C, 0); auto rw = b.ccr_rd(0x1E, 1); b.ccr_wr(0xFFFF, 0x1C, 1); auto r2 = b.ccr_rd();
  auto hole = b.ccr_rd(0x20);
  b.run_ops();
  CHK(q0->rdata == 0x1C && q1->rdata == 0x10 && b.t->qacr0 == 7 && b.t->qacr1 == 4, "ccr: QACR %08x %08x",
      (uint32_t)q0->rdata, (uint32_t)q1->rdata);
  CHK(rb->rdata == 0 && rw->rdata == 0 && r2->rdata == 0x0105 && hole->rdata == 0, "ccr: size rule");
  CHK(q2->rdata == 0x1C && q4->rdata == 0x10, "ccr: a word / byte QACR write was not ignored (%08x %08x)",
      (uint32_t)q2->rdata, (uint32_t)q4->rdata);
  // OCI: V and U cleared without write-back; ICI: IC V cleared
  b.st(X, 0x99); b.drain();
  const uint64_t mem0 = b.rmem.rq(0x0C078000u);
  b.ccr_wr(0x0109); auto a = b.ocaa_rd(0); b.run_ops(); b.drain();
  CHK(a->rdata == 0x0C078000u && b.rmem.rq(0x0C078000u) == mem0, "ccr: OCI %08x (no write-back)", (uint32_t)a->rdata);
  // a manual reset: CCR = QACR = 0, the arrays held
  b.ccr_wr(0x0105); b.ld(X); b.drain();
  b.reset(false);
  auto r3 = b.ccr_rd(); auto q3 = b.push(b.p4(false, P4B_CCN_CCR, 0x38)); auto a3 = b.ocaa_rd(0); b.run_ops();
  CHK(r3->rdata == 0 && q3->rdata == 0 && a3->rdata == 0x0C078001u, "ccr: manual reset CCR %08x QACR %08x entry %08x",
      (uint32_t)r3->rdata, (uint32_t)q3->rdata, (uint32_t)a3->rdata);
}

static void g_sq(Bench &b) {
  fresh(b);
  b.push(b.p4(true, P4B_CCN_CCR, 0x38, 0x0C)); b.push(b.p4(true, P4B_CCN_CCR, 0x3C, 0x04)); b.run_ops();
  // writes: L, Q, MOVCA.L; reads 0; cache ops no-ops, all ack in MA1
  std::vector<OpP> w;
  for (int k = 0; k < 8; k++) w.push_back(b.st(0xE0100000u + 4 * k, 0x5A000000u + k));
  w.push_back(b.op(DK_ST, 0xE0100028u, 3, 0x1111222233334444ull));
  w.push_back(b.op(DK_MOVCA, 0xE0100038u, 2, 0x77777777u));
  auto rd = b.ld(0xE0100004u); auto oi = b.op(DK_OCBI, 0xE0100000u); auto op = b.op(DK_OCBP, 0xE0100020u);
  auto pg = b.op(DK_TAS_PURGE, 0xE0100020u, 0);
  b.run_ops();
  for (auto &o : w) CHK(o->lat() == 0 && o->nfrz == 0, "sq: store %08x ack %d frz %d", o->va, o->lat(), o->nfrz);
  for (auto &o : {rd, oi, op, pg}) CHK(o->lat() == 0 && o->rdata == 0, "sq: %s ack %d data %llx", kname(o->kind),
                                        o->lat(), (unsigned long long)o->rdata);
  // PREF SQ0 at +0x14: the whole queue from offset 0 to {QACR0, A[25:5]} (area 3); PREF SQ1 to QACR1's area 1
  size_t nr = b.e_rdy[BRQ_SQ].size();
  auto p0 = b.op(DK_PREF, 0xE0100014u); auto p1 = b.op(DK_PREF, 0xE0100020u);
  b.run_ops(); b.drain();
  CHK(p0->nfrz == 2 && p0->lat() == 2, "sq: PREF frz %d ack %d (2 / 2)", p0->nfrz, p0->lat());
  CHK(b.e_rdy[BRQ_SQ].size() == nr + 2, "sq: two bursts");
  for (int k = 0; k < 8; k++) {
    uint32_t v0 = (uint32_t)(b.rmem.rq(0x0C100000u + 4 * k) >> (32 * (k & 1)));
    CHK(v0 == 0x5A000000u + k, "sq: SQ0 burst word %d = %08x", k, v0);
  }
  CHK((uint32_t)(b.rmem.rq(0x04100028u)) == 0x33334444u && (uint32_t)(b.rmem.rq(0x04100028u) >> 32) == 0x11112222u &&
      (uint32_t)(b.rmem.rq(0x04100038u)) == 0x77777777u, "sq: SQ1 burst (QACR1, quad, MOVCA.L)");
  // a store to a busy SQ waits until one Bcyc after the burst's done (v1_sq's Bclk release register, M3: done is a bce
  // pulse, the lock clears at the next bce, the store is acked in the Icyc after it); the other SQ does not wait;
  // PREF-PREF: 12 per burst
  b.drain();
  auto pa = b.op(DK_PREF, 0xE0100000u); pa->phase = 0; auto s0 = b.st(0xE0100000u, 1); b.run_ops();
  int64_t dn = -1;
  for (int64_t c : b.e_done[BRQ_SQ]) if (c > pa->t0) { dn = c; break; }
  CHK(s0->tack == dn + 3, "sq: store to the busy SQ0 acked at %ld, burst done at %ld (want done + 3)", (long)s0->tack,
      (long)dn);
  b.drain();
  auto pb = b.op(DK_PREF, 0xE0100000u); auto s1 = b.st(0xE0100020u, 1); b.run_ops();
  CHK(s1->lat() == 0, "sq: a store to the other SQ waited %d", s1->lat());
  (void)pb;
  b.drain();
  auto x0 = b.op(DK_PREF, 0xE0100000u); x0->phase = 0; auto x1 = b.op(DK_PREF, 0xE0100020u);
  auto x2 = b.op(DK_PREF, 0xE0100000u); auto x3 = b.op(DK_PREF, 0xE0100020u); b.run_ops();
  CHK(x2->tack - x1->tack == 12 && x3->tack - x2->tack == 12, "sq: PREF-PREF %ld / %ld per burst (12)",
      (long)(x2->tack - x1->tack), (long)(x3->tack - x2->tack));
  (void)x0;
  // B / W writes: the addressed bytes (the "unmeasured size" assertion prints)
  b.drain();
  b.st(0xE0100001u, 0xEE, 0); b.st(0xE0100006u, 0xDDCC, 1); b.op(DK_PREF, 0xE0100000u); b.run_ops(); b.drain();
  CHK((b.rmem.rq(0x0C100000u) & 0xFFFF00000000FF00ull) == 0xDDCC00000000EE00ull, "sq: byte / word writes %016llx",
      (unsigned long long)b.rmem.rq(0x0C100000u));
  // AT = 1: the burst goes to the UTLB's PA (x.pa)
  b.drain();
  b.mmu_at = true; b.m.mmu_at = true;
  b.sq_xlat = [](uint32_t va) { return 0x0C200000u | (va & 0x3E0u); };
  b.st(0xE0100020u, 0xFACE); b.op(DK_PREF, 0xE0100020u); b.run_ops(); b.drain();
  CHK((uint32_t)b.rmem.rq(0x0C200020u) == 0xFACE, "sq: AT = 1 burst address");
  b.mmu_at = false; b.m.mmu_at = false;
  b.compare_state("sq", false);
}

static void g_ic(Bench &b) {
  fresh(b);
  // a miss: request in m + 1, ack at the critical rd_v + 1 (14 / 15 by phase), the IFILL record at completion
  for (int ph = 0; ph < 2; ph++) {
    b.drain();
    uint32_t va = 0x8C400000u + 0x100 * ph + 8;
    size_t n0 = b.e_rise[BRQ_IC_FILL].size(), nv = b.e_rdv[BRQ_IC_FILL].size();
    int64_t t0 = -1, ta = -1;
    b.f_done = [&](const FReq &, uint32_t, int64_t a, int64_t c) { t0 = a; ta = c; };
    while ((b.cyc & 1) != ph) b.run(1);
    b.fq.push_back(FReq{va, va & 0x1FFFFFFF, true}); b.run_ops();
    CHK(ta - t0 == 14 + ph, "ic: miss penalty %ld at phase %d (expected %d)", (long)(ta - t0), ph, 14 + ph);
    CHK(b.e_rise[BRQ_IC_FILL].size() > n0 && b.e_rise[BRQ_IC_FILL][n0] == t0 + 1, "ic: fill request not in m + 1");
    CHK(b.e_rdv[BRQ_IC_FILL].size() > nv && b.e_rdv[BRQ_IC_FILL][nv] + 1 == ta, "ic: ack not at the critical rd_v + 1");
    // the hit in the request cycle
    b.drain();
    b.fq.push_back(FReq{va, va & 0x1FFFFFFF, true}); b.run_ops();
    CHK(ta == t0, "ic: hit not acked in the request cycle (%ld)", (long)(ta - t0));
  }
  // straight-line fetch through a missing line: each word as its beat arrives; next line waits for completion
  b.drain();
  std::vector<std::pair<int64_t, int64_t>> fl;
  b.f_done = [&](const FReq &, uint32_t, int64_t a, int64_t c) { fl.push_back({a, c}); };
  for (int k = 0; k < 10; k++) b.fq.push_back(FReq{0x8C401000u + 4 * k, 0x0C401000u + 4 * k, true});
  size_t nv = b.e_rdv[BRQ_IC_FILL].size();
  b.run_ops();
  CHK(fl.size() == 10, "ic: straight fetches");
  if (fl.size() == 10 && b.e_rdv[BRQ_IC_FILL].size() >= nv + 8) {
    const int64_t *rv = &b.e_rdv[BRQ_IC_FILL][nv];
    CHK(fl[0].second == rv[0] + 1 && fl[1].second == rv[0] + 2, "ic: q0 words at %ld %ld (rd_v %ld)",
        (long)fl[0].second, (long)fl[1].second, (long)rv[0]);
    // each beat's first word from the beat register in rd_v + 1, the second from the array in rd_v + 2
    CHK(fl[2].second == rv[1] + 1 && fl[3].second == rv[1] + 2 && fl[4].second == rv[2] + 1 &&
        fl[6].second == rv[3] + 1 && fl[7].second == rv[3] + 2, "ic: straight-line words do not follow the beats");
    // the next line looks up after the completion stage (W3 + 1 = rd_v3 + 2): its request in rd_v3 + 4
    CHK(b.e_rise[BRQ_IC_FILL].back() == rv[3] + 4, "ic: the next line's request at %ld, last beat %ld (rd_v3 + 4)",
        (long)b.e_rise[BRQ_IC_FILL].back(), (long)rv[3]);
  }
  // a request the fetch abandons whatever the answer (ireq_t.spec: the sequential word of a redirect cycle) starts no
  // line fill and evicts nothing [HW core-exc c1i ld_n trap 35: RTE's discarded word, the next line, shares its entry
  // with the loop's line; a fill gave 92]; the same word asked normally then misses and fills (M3, WI-SOC)
  b.drain();
  {
    b.f_done = nullptr;
    size_t nf = b.ifills.size(), nr = b.e_rise[BRQ_IC_FILL].size();
    uint64_t zm = b.ev_ic[EV_IC_MISS], zf = b.ev_ic[EV_FRZ_IC];
    FReq s{0x8C40E000u, 0x0C40E000u, true};
    s.spec = true;
    b.fq.push_back(s); b.run_ops(); b.drain();
    CHK(b.ifills.size() == nf && b.e_rise[BRQ_IC_FILL].size() == nr, "ic: an abandoned (spec) request started a fill");
    // and counts neither an IC miss (0x08) nor an IC freeze cycle (0x24) [HW caches-sq 6.1: frzic 0 on warm rows]
    CHK(b.ev_ic[EV_IC_MISS] == zm && b.ev_ic[EV_FRZ_IC] == zf, "ic: an abandoned request counted 0x08 %llu / 0x24 %llu",
        (unsigned long long)(b.ev_ic[EV_IC_MISS] - zm), (unsigned long long)(b.ev_ic[EV_FRZ_IC] - zf));
    // behind an ITLB refill (busy) the redirect waits for the abandoned request's answer: it must come without a fill
    // (M3: a speculative refill then an IC miss hung the core -- mmubm on rmx)
    FReq s2{0x8C40E400u, 0x0C40E400u, true};
    s2.spec = true; s2.stall = 5;
    b.fq.push_back(s2); b.run_ops(); b.drain();
    CHK(b.ifills.size() == nf && b.e_rise[BRQ_IC_FILL].size() == nr, "ic: a waited-for abandoned request started a fill");
    b.fq.push_back(FReq{0x8C40E000u, 0x0C40E000u, true}); b.run_ops(); b.drain();
    CHK(b.e_rise[BRQ_IC_FILL].size() == nr + 1, "ic: the word asked normally after the abandoned one did not fill");
  }
  // events of a miss: 0x08 = 1, 0x21 = 0x24 = the penalty
  b.drain();
  uint64_t m0 = b.ev_ic[EV_IC_MISS], f0 = b.ev_ic[EV_IC_FILL], z0 = b.ev_ic[EV_FRZ_IC];
  int64_t t0 = -1, ta = -1;
  b.f_done = [&](const FReq &, uint32_t, int64_t a, int64_t c) { t0 = a; ta = c; };
  b.fq.push_back(FReq{0x8C402000u, 0x0C402000u, true}); b.run_ops(); b.drain();
  CHK(b.ev_ic[EV_IC_MISS] - m0 == 1 && b.ev_ic[EV_IC_FILL] - f0 == (uint64_t)(ta - t0) &&
      b.ev_ic[EV_FRZ_IC] - z0 == (uint64_t)(ta - t0), "ic: events miss %llu fill %llu frz %llu (penalty %ld)",
      (unsigned long long)(b.ev_ic[EV_IC_MISS] - m0), (unsigned long long)(b.ev_ic[EV_IC_FILL] - f0),
      (unsigned long long)(b.ev_ic[EV_FRZ_IC] - z0), (long)(ta - t0));
  // the forced miss: arr_strobe makes the next lookup of a present line miss and refill it
  b.drain();
  uint64_t mm = b.ev_ic[EV_IC_MISS];
  b.fq.push_back(FReq{0x8C402000u, 0x0C402000u, true}); b.run_ops();
  CHK(b.ev_ic[EV_IC_MISS] == mm && ta == t0, "ic: present line did not hit");
  b.extra_arr_strobe = true; b.run(1);
  b.fq.push_back(FReq{0x8C402004u, 0x0C402004u, true}); b.run_ops(); b.drain();
  CHK(b.ev_ic[EV_IC_MISS] == mm + 1 && ta - t0 >= 14, "ic: arr_strobe did not force a miss (%ld)", (long)(ta - t0));
  b.fq.push_back(FReq{0x8C402008u, 0x0C402008u, true}); b.run_ops();
  CHK(ta == t0, "ic: the forced miss is not cleared by its fill");
  // an OC array read strobes arr_strobe (the bridge) and forces the IC miss as well
  b.drain(); mm = b.ev_ic[EV_IC_MISS];
  b.ocaa_rd(5); b.run_ops();
  b.fq.push_back(FReq{0x8C402000u, 0x0C402000u, true}); b.run_ops(); b.drain();
  CHK(b.ev_ic[EV_IC_MISS] == mm + 1, "ic: an OC array access did not force the IC miss");
  // another line waits for the running fill to complete
  b.drain();
  std::vector<std::pair<int64_t, int64_t>> g;
  b.f_done = [&](const FReq &, uint32_t, int64_t a, int64_t c) { g.push_back({a, c}); };
  size_t nd = b.e_rdv[BRQ_IC_FILL].size();
  b.fq.push_back(FReq{0x8C403000u, 0x0C403000u, true}); b.fq.push_back(FReq{0x8C403400u, 0x0C403400u, true});
  b.run_ops(); b.drain();
  if (g.size() == 2 && b.e_rdv[BRQ_IC_FILL].size() >= nd + 8) {
    int64_t rv3 = b.e_rdv[BRQ_IC_FILL][nd + 3], rv4 = b.e_rdv[BRQ_IC_FILL][nd + 4];
    CHK(rv4 > rv3 + 12 && g[1].second == rv4 + 1, "ic: the second line's fill did not wait for the first");
  } else CHK(false, "ic: two-line case incomplete");
  // uncached words: one BZ_L read each, busy, ack at rd_v + 1; a redirect is held until the read is done
  b.drain();
  size_t nu = b.e_rdy[BRQ_IC_UNC].size();
  std::vector<std::pair<int64_t, int64_t>> u;
  b.f_done = [&](const FReq &, uint32_t, int64_t a, int64_t c) { u.push_back({a, c}); };
  for (int k = 0; k < 3; k++) b.fq.push_back(FReq{0xAC404000u + 4 * k, 0x0C404000u + 4 * k, false});
  b.run_ops();
  CHK(b.e_rdy[BRQ_IC_UNC].size() == nu + 3 && u.size() == 3 && u[2].second - u[1].second == 16,
      "ic: uncached words: %zu reads, %ld per word (16)", b.e_rdy[BRQ_IC_UNC].size() - nu,
      u.size() == 3 ? (long)(u[2].second - u[1].second) : -1L);
  // poison and the ITLB refill stall
  b.drain();
  u.clear();
  b.fq.push_back(FReq{0x80000002u, 0, true, FX_ADDR}); b.fq.push_back(FReq{0x00400000u, 0, true, FX_TLB_MISS, true});
  b.fq.push_back(FReq{0x8C402000u, 0x0C402000u, true, FX_NONE, false, 6});
  b.run_ops();
  CHK(u.size() == 3 && u[0].first == u[0].second && u[1].first == u[1].second && u[2].second - u[2].first == 6,
      "ic: poison / refill stall");
  b.f_done = nullptr;
  // a CPU-only reset while an uncached word is outstanding: the read completes, its word is dropped, the new request
  // (the reset vector's fetch) gets its own read
  b.drain();
  uint32_t gw = 0;
  size_t nu2 = b.e_rdy[BRQ_IC_UNC].size();
  b.f_done = [&](const FReq &, uint32_t d, int64_t, int64_t) { gw = d; };
  b.fq.push_back(FReq{0xAC404100u, 0x0C404100u, false});
  b.run(4);
  b.rcpu = true; b.run(1); b.rcpu = false;
  b.fc = FReq{0xAC404104u, 0x0C404104u, false};                    // the fetch restarts with another word
  b.run_ops(); b.drain();
  CHK(gw == (uint32_t)(b.rmem.rq(0x0C404100u) >> 32) && b.e_rdy[BRQ_IC_UNC].size() == nu2 + 2,
      "ic: after a reset the old uncached word was not dropped (%08x, %zu reads)", gw, b.e_rdy[BRQ_IC_UNC].size() - nu2);
  b.f_done = nullptr;
  // a redirect of a waiting cacheable fetch: the fill it started completes and fills its line
  b.drain();
  b.ifills.clear();
  b.fq.push_back(FReq{0x8C405000u, 0x0C405000u, true});
  b.run(3);
  b.fc = FReq{0x8C405400u, 0x0C405400u, true};                     // redirected while waiting (busy = 0)
  b.run_ops(); b.drain();
  CHK(b.ifills.size() == 2, "ic: the abandoned request's fill did not complete (%zu IFILLs)", b.ifills.size());
  b.compare_state("ic");
}

static void g_reset(Bench &b) {
  fresh(b);
  b.st(X, 1); b.ld(X + 0x20); b.fq.push_back(FReq{0x8C406000u, 0x0C406000u, true}); b.drain();
  b.reset(false);                                                 // manual: V / U held
  set_ccr(b, 0x0105);
  auto a = b.ocaa_rd(0); auto i = b.icaa_rd(0x0); b.run_ops();
  CHK(a->rdata == 0x0C078003u, "reset: manual reset lost the OC state (%08x)", (uint32_t)a->rdata);
  b.st(0xE0000000u, 0x12345678); b.drain();
  b.reset(true);                                                  // power-on: V / U cleared, SQ 0
  set_ccr(b, 0x0105);
  auto a2 = b.ocaa_rd(0); b.push(b.p4(true, P4B_CCN_CCR, 0x38, 0x0C)); b.op(DK_PREF, 0xE0000000u); b.run_ops(); b.drain();
  CHK(a2->rdata == 0x0C078000u, "reset: power-on kept V / U or lost the tag (%08x)", (uint32_t)a2->rdata);
  CHK((uint32_t)b.rmem.rq(0x0C000000u) == 0, "reset: SQ contents not cleared by power-on");
  (void)i;
  b.compare_state("reset");
}

// PMC events of the OC (SOC.md 3.7, caches-sq 3.11, debug-ubc-hudi 4.7)
static void g_events(Bench &b) {
  fresh(b);
  struct Row { const char *name; std::function<void()> ops; int rd, wr, rw, rdm, wrm, miss, ram; };
  const Row rows[] = {
    {"load miss", [&] { b.ld(X + 0x100); }, 1, 0, 1, 1, 0, 1, 0},
    {"load hit", [&] { b.ld(X + 0x104); }, 1, 0, 1, 0, 0, 0, 0},
    {"copy-back store miss", [&] { b.st(X + 0x140, 1); }, 0, 1, 1, 0, 1, 1, 0},
    {"store hit", [&] { b.st(X + 0x144, 1); }, 0, 1, 1, 0, 0, 0, 0},
    {"PREF miss", [&] { b.op(DK_PREF, X + 0x180); }, 0, 0, 0, 1, 0, 1, 0},
    {"MOVCA.L miss", [&] { b.op(DK_MOVCA, X + 0x1C0, 2, 5); }, 0, 1, 1, 0, 0, 0, 0},
    {"uncached load", [&] { b.ld(0xAC078200u); }, 0, 0, 0, 0, 0, 0, 0},
    {"uncached store", [&] { b.st(0xAC078240u, 1); }, 0, 0, 0, 0, 0, 0, 0},
    {"PREF P2", [&] { b.op(DK_PREF, 0xAC078280u); }, 0, 0, 0, 0, 0, 0, 0},
    {"OCBI hit", [&] { b.op(DK_OCBI, X + 0x100); }, 0, 0, 0, 0, 0, 0, 0},
  };
  for (const Row &r : rows) {
    b.drain();
    uint64_t e0[64];
    memcpy(e0, b.ev_oc, sizeof e0);
    r.ops(); b.drain();
    int d[64];
    for (int k = 0; k < 64; k++) d[k] = (int)(b.ev_oc[k] - e0[k]);
    CHK(d[EV_OP_RD] == r.rd && d[EV_OP_WR] == r.wr && d[EV_OP_RW] == r.rw && d[EV_OC_RD_MISS] == r.rdm &&
        d[EV_OC_WR_MISS] == r.wrm && d[EV_OC_MISS] == r.miss && d[EV_OCRAM] == r.ram,
        "events %s: 01 %d 02 %d 0E %d 04 %d 05 %d 0F %d 0B %d", r.name, d[EV_OP_RD], d[EV_OP_WR], d[EV_OP_RW],
        d[EV_OC_RD_MISS], d[EV_OC_WR_MISS], d[EV_OC_MISS], d[EV_OCRAM]);
  }
  // 0x22: a line read's tenure at the BSC, from its accept through its critical word's delivery: 14 for a row-hit load
  // miss at either Bclk phase (penalty 14 / 15) [HW caches-sq 6.1: ocfill 14.25 for chain (15) and ldgap4 (14) misses]
  for (int ph = 0; ph < 2; ph++) {
    b.drain();
    uint64_t f0 = b.ev_oc[EV_OC_FILL];
    auto o = b.ld(X + 0x300 + 0x40 * ph); o->phase = ph; b.run_ops(); b.drain();
    CHK((int)(b.ev_oc[EV_OC_FILL] - f0) == 14, "events: 0x22 counts %d for a miss of penalty %d at phase %d (14)",
        (int)(b.ev_oc[EV_OC_FILL] - f0), o->lat(), ph);
  }
  // 0x0B: OC RAM accesses
  set_ccr(b, 0x0125);
  uint64_t r0 = b.ev_oc[EV_OCRAM];
  b.st(0x7C001000u, 1); b.ld(0x7C001000u); b.op(DK_PREF, 0x7C001000u); b.run_ops();
  CHK(b.ev_oc[EV_OCRAM] - r0 == 2, "events: 0x0B %llu for an OC RAM store + load (+ a PREF: not an access)",
      (unsigned long long)(b.ev_oc[EV_OCRAM] - r0));
}

// ---------------------------------------------------------------------------------------------------------------
// Regressions of the WI-CACHE verification findings (agents/reports/units/WI-CACHE-verify.md; the fix report
// agents/reports/units/WI-CACHE-fix.md).  Each check below failed on the RTL before the fix.
static bool until_c(Bench &b, std::function<bool()> c, int lim = 2000) {
  for (int i = 0; i < lim; i++) { if (c()) return true; b.step(); }
  return c();
}
static bool bq_up(Bench &b, int rq) { return ((b.t->bqv >> rq) & 1) != 0; }

// F1: the IC's uncached word is released whichever branch the request takes; busy is "a read on the bus"
static void fx_ic_busy(Bench &b) {
  // (a) CCR.ICE 0 -> 1 lands while the word of a P1 fetch is on the bus: the held request is acked from a fill, the
  //     word is dropped when it arrives, busy falls (v1_core's exception redirect and every fetch redirect wait for it)
  fresh(b, 0x0005);
  b.fq.push_back(FReq{0x8C400000u, 0x0C400000u, true});
  until_c(b, [&] { return bq_up(b, BRQ_IC_UNC); }, 50);
  CHK(bq_up(b, BRQ_IC_UNC), "fix ic (a): the uncached word was not requested");
  b.ccr_wr(0x0105);
  b.run_ops();
  for (int i = 1; i <= 4; i++) b.fq.push_back(FReq{0x8C400000u + 4u * i, 0x0C400000u + 4u * i, true});
  b.run_ops(); b.run(60);
  CHK(!b.t->i_busy, "fix ic (a): irsp.busy stuck at 1 after ICE was set under an uncached word (F1)");
  // (b) the held uncached request is acked as poisoned (its MMU answer changed) while its word is on the bus; the
  //     next requests are cacheable
  fresh(b);
  b.fq.push_back(FReq{0xAC400000u, 0x0C400000u, false});
  until_c(b, [&] { return bq_up(b, BRQ_IC_UNC); }, 50);
  b.run(2);
  b.fc.fx = FX_TLB_MISS; b.fc.vec400 = true;
  until_c(b, [&] { return !b.f_on; }, 50);
  b.run(40);
  for (int i = 0; i < 3; i++) b.fq.push_back(FReq{0x8C400100u + 4u * i, 0x0C400100u + 4u * i, true});
  b.run_ops(); b.run(40);
  CHK(!b.t->i_busy, "fix ic (b): irsp.busy stuck at 1 after a poisoned ack of the held uncached request (F1)");
  // the word was dropped, not kept: memory changes, then the same word fetched uncached must be read again
  b.st(0xAC400000u, 0x00090009u); b.run_ops(); b.drain();
  b.fq.push_back(FReq{0xAC400000u, 0x0C400000u, false});      // (the bench checks the data against the model)
  b.run_ops(); b.run(20);
  // (c) the word arrives while the fetch presents nothing (a pfz / mah hold, or an exception's take cycle to its
  //     redirect): nothing is on the bus any more, so busy is 0 -- the redirect waiting for it can go -- and the held
  //     request, presented again, takes the kept word without a second read
  fresh(b);
  size_t n0 = b.e_rdy[BRQ_IC_UNC].size(), v0 = b.e_rdv[BRQ_IC_UNC].size();
  uint32_t got = 0;
  b.f_done = [&](const FReq &, uint32_t d, int64_t, int64_t) { got = d; };
  b.fq.push_back(FReq{0xAC400200u, 0x0C400200u, false});
  until_c(b, [&] { return b.e_rdy[BRQ_IC_UNC].size() > n0; }, 50);
  const FReq held = b.fc;
  b.f_on = false;                                             // the fetch halts: no request
  until_c(b, [&] { return b.e_rdv[BRQ_IC_UNC].size() > v0; }, 50);
  b.run(2);
  CHK(!b.t->i_busy, "fix ic (c): busy still 1 with the word arrived and no request presented (a waiting redirect "
      "would never go)");
  b.fq.push_back(held);
  b.run_ops(); b.run(4);
  CHK(b.e_rdy[BRQ_IC_UNC].size() == n0 + 1 && got == (uint32_t)b.rmem.rq(0x0C400200u),
      "fix ic (c): the re-presented request was not served from the kept word (%zu reads, %08x)",
      b.e_rdy[BRQ_IC_UNC].size() - n0, got);
  // (d) ... and a different request drops a kept word and gets its own read
  n0 = b.e_rdy[BRQ_IC_UNC].size(); v0 = b.e_rdv[BRQ_IC_UNC].size();
  b.fq.push_back(FReq{0xAC400300u, 0x0C400300u, false});
  until_c(b, [&] { return b.e_rdy[BRQ_IC_UNC].size() > n0; }, 50);
  b.f_on = false;
  until_c(b, [&] { return b.e_rdv[BRQ_IC_UNC].size() > v0; }, 50);
  b.fq.push_back(FReq{0xAC400308u, 0x0C400308u, false});
  b.run_ops(); b.run(4);
  CHK(b.e_rdy[BRQ_IC_UNC].size() == n0 + 2 && got == (uint32_t)b.rmem.rq(0x0C400308u) && !b.t->i_busy,
      "fix ic (d): a kept word was not dropped for another request (%zu reads, %08x)", b.e_rdy[BRQ_IC_UNC].size() - n0,
      got);
  b.f_done = nullptr;
}

// F2 (IC): an ICI after a fill's IFILL record kills the fill (the model invalidated the line it filled at the
// record); an ICI before the accept leaves it (the model fills at the IFILL that follows)
static void fx_ic_ici(Bench &b) {
  fresh(b);
  // (a) the fetch has its word (the critical-word bypass), the tail beats still arrive: ICI -> the line stays invalid
  size_t nf = b.ifills.size();
  b.fq.push_back(FReq{0x8C401000u, 0x0C401000u, true});
  until_c(b, [&] { return !b.f_on && b.fq.empty() && b.ifills.size() > nf; }, 60);
  b.ccr_wr(0x0105 | ICI);                                     // captured while the beats arrive
  b.run_ops();
  b.drain();
  CHK(b.e_rdv[BRQ_IC_FILL].size() >= 4 && b.last_ici > b.last_ifill &&
      b.last_ici <= b.e_rdv[BRQ_IC_FILL].back() + 1, "fix ici: scenario: the ICI (%ld) is not between the IFILL (%ld) "
      "and the completion stage (last beat %ld + 2)", (long)b.last_ici, (long)b.last_ifill,
      b.e_rdv[BRQ_IC_FILL].empty() ? -1L : (long)b.e_rdv[BRQ_IC_FILL].back());
  b.drain();
  auto a = b.icaa_rd(0x80); b.run_ops();                      // (the bench compares it with the model)
  CHK(a->rdata == 0x0C401000u, "fix ici: ICI after the IFILL: ICAA %08x (0c401000: V = 0, tag kept; F2)",
      (uint32_t)a->rdata);
  nf = b.ifills.size();
  b.fq.push_back(FReq{0x8C401004u, 0x0C401004u, true}); b.run_ops(); b.drain();
  CHK(b.ifills.size() == nf + 1, "fix ici: the invalidated line was not refilled");
  // (b) the fetch still waits for its word when the ICI kills the fill: it is served by a refill of the line
  b.drain();
  nf = b.ifills.size();
  b.fq.push_back(FReq{0x8C401818u, 0x0C401818u, true});
  until_c(b, [&] { return b.ifills.size() > nf; }, 60);
  b.ccr_wr(0x0105 | ICI);
  b.run_ops(); b.drain();
  CHK(b.ifills.size() == nf + 2, "fix ici: a fetch waiting on a killed fill: %zu IFILLs (2: the killed one, the "
      "refill)", b.ifills.size() - nf);
  // an ICI between the miss and the accept: the miss in an odd (bce) cycle m, the request in m + 1, accepted in m + 2;
  // the CCR write captured in m + 1
  b.drain();
  while ((b.cyc & 1) != 1) b.run(1);
  const int64_t m0 = b.cyc;
  b.fq.push_back(FReq{0x8C402400u, 0x0C402400u, true});
  auto w = b.ccr_wr(0x0105 | ICI); w->at = m0 + 1;
  b.run_ops(); b.drain();
  CHK(b.last_ici == m0 + 1 && b.last_ifill == m0 + 2, "fix ici: scenario: ICI at %ld, IFILL at %ld (miss at %ld)",
      (long)b.last_ici, (long)b.last_ifill, (long)m0);
  auto a2 = b.icaa_rd(0x20); b.run_ops();
  CHK(a2->rdata == 0x0C402401u, "fix ici: ICI before the accept: ICAA %08x (0c402401: the fill validates)",
      (uint32_t)a2->rdata);
  b.compare_state("fix ici");
}

// F2 (OC): an OCI kills the fills in flight (their accesses were acked and applied before it)
static void fx_oc_oci(Bench &b) {
  fresh(b);
  // store miss, OCI behind it: invalid, the store's data discarded (a purge writes nothing back)
  b.st(X + 0x3000, 0x5EED5EEDu);
  b.ccr_wr(0x0105 | OCI);
  b.run_ops(); b.drain();
  const uint64_t mem0 = b.rmem.rq(0x0C07B000u);
  auto a = b.ocaa_rd(0x180); b.push(b.p4(true, P4B_OCAA, 0x04003000u, 0)); b.run_ops(); b.drain();
  CHK(a->rdata == 0x0C07B000u && b.rmem.rq(0x0C07B000u) == mem0,
      "fix oci: store-miss fill after OCI: OCAA %08x (0c07b000), memory %016llx (F2)", (uint32_t)a->rdata,
      (unsigned long long)b.rmem.rq(0x0C07B000u));
  // PREF miss, OCI, an uncached store to the line, a cached load: a miss that reads the new memory
  b.op(DK_PREF, X + 0x3400);
  b.ccr_wr(0x0105 | OCI);
  b.st(0xAC07B400u, 0xFEEDF00Du);
  auto l = b.ld(X + 0x3400);
  b.run_ops(); b.drain();
  CHK(l->rdata == 0xFEEDF00Du && l->pred_nb && l->nfrz == 2, "fix oci: load after OCI + uncached store: %08llx, frz %d "
      "(feedf00d, a miss; F2)", (unsigned long long)l->rdata, l->nfrz);
  // a queued second fill (fq) is killed as well: PREF A (on the bus), PREF B (queued), OCI
  b.drain();
  auto pa = b.op(DK_PREF, X + 0x3800); pa->phase = 0; b.op(DK_PREF, X + 0x3840);
  b.ccr_wr(0x0105 | OCI);
  auto ea = b.ocaa_rd(0x1C0); auto eb = b.ocaa_rd(0x1C2);
  auto la = b.ld(X + 0x3804); auto lb = b.ld(X + 0x3844);
  b.run_ops(); b.drain();
  CHK((ea->rdata & 3) == 0 && (eb->rdata & 3) == 0 && la->nfrz == 2 && lb->nfrz == 2,
      "fix oci: f / fq after OCI: %08x %08x, loads frz %d %d", (uint32_t)ea->rdata, (uint32_t)eb->rdata, la->nfrz,
      lb->nfrz);
  b.compare_state("fix oci");
}

// F3: an OC RAM access waits for a fill that still targets its entry (claimed before ORA was set)
static void fx_ocram_fill(Bench &b) {
  fresh(b);
  auto p = b.op(DK_PREF, 0x8C079000u);                        // entry 0x080: RAM entry of 0x7C001000 under ORA
  b.ccr_wr(0x0125);
  b.run_ops();
  CHK(b.cyc < p->tack + 12, "fix ocram: scenario: the fill finished before the RAM store");
  auto s = b.st(0x7C001000u, 0x0A0A0A0Au); auto r = b.ld(0x7C001000u);
  b.run_ops(); b.drain();
  auto r2 = b.ld(0x7C001000u); b.run_ops();
  CHK(r->rdata == 0x0A0A0A0Au && r2->rdata == 0x0A0A0A0Au && s->lat() > 0,
      "fix ocram: RAM word %08llx, after the fill %08llx, the store waited %d (0a0a0a0a twice, > 0; F3)",
      (unsigned long long)r->rdata, (unsigned long long)r2->rdata, s->lat());
  b.compare_state("fix ocram");
}

// F5: a reset kills an uncached read's access; its late data must not reach a later access waiting for a beat
static void fx_stale_unc(Bench &b) {
  for (int pf = 0; pf < 2; pf++) {
    fresh(b);
    b.st(X + 0x40, 0x11111111u); b.drain();
    b.op(DK_OCBWB, X + 0x40);                                 // the write-back keeps the bus ~12 Icyc
    auto u = pf ? b.op(DK_PREF, 0xAC079000u) : b.ld(0xAC079000u);
    until_c(b, [&] { return u->t0 >= 0 && b.cyc >= u->t0 + 3; }, 100);
    CHK(!u->done && b.t->o_claimed, "fix stale: scenario: the uncached read is not outstanding");
    b.kill_reset(true);
    b.ccr_wr(0x0105);
    auto l = b.ld(X + 0x1008);                                // a load miss waits for its critical beat
    b.run_ops(); b.drain();
    CHK(l->rdata == (b.rmem.rq(0x0C079008u) & 0xFFFFFFFFu), "fix stale (%s): the load returned %08llx (F5)",
        pf ? "P2 PREF" : "uncached load", (unsigned long long)l->rdata);
  }
}

// F6: a reset kills an access after its claim: the RTL completes it (the model applies it); before: no effect
static void fx_kill(Bench &b) {
  fresh(b);
  // a load miss killed while waiting for its critical beat: the fill validates its line
  auto l = b.ld(X + 0x2000);
  until_c(b, [&] { return b.t->o_claimed; }, 20);
  CHK(b.kill_reset(true) == 1, "fix kill: the load miss was not claimed");
  b.ccr_wr(0x0105); b.drain();
  auto h = b.ld(X + 0x2004); b.run_ops();
  CHK(h->lat() == 0 && h->nfrz == 0, "fix kill: the killed load's line is not valid (lat %d)", h->lat());
  (void)l;
  // a load miss killed in its first cycle (before its claim): nothing
  b.drain();
  auto l2 = b.ld(X + 0x2400);
  until_c(b, [&] { return l2->t0 >= 0; }, 20);             // its m has been clocked: the claim would come in m + 1
  CHK(b.kill_reset(true) == 0, "fix kill: a load killed in m was claimed");
  b.ccr_wr(0x0105);
  auto m2 = b.ld(X + 0x2404); b.run_ops();
  CHK(m2->nfrz == 2, "fix kill: a load killed before its claim left its line (frz %d)", m2->nfrz);
  // a store miss killed between its claim (m + 1) and its ack (m + 2): the store stands
  b.drain();
  b.st(X + 0x2800, 0x77777777u);
  until_c(b, [&] { return b.t->o_claimed; }, 20);
  CHK(b.kill_reset(false) == 1, "fix kill: the store miss was not claimed");
  b.drain();
  auto l3 = b.ld(X + 0x2800); b.run_ops();
  CHK(l3->rdata == 0x77777777u && l3->lat() == 0, "fix kill: the killed store miss: %08llx lat %d",
      (unsigned long long)l3->rdata, l3->lat());
  // a WTB store and an SQ burst killed after their claims: memory is written
  b.drain();
  b.st(0xAC07A800u, 0x12344321u);
  until_c(b, [&] { return b.t->o_claimed; }, 20);
  CHK(b.kill_reset(true) == 1, "fix kill: the uncached store was not claimed");
  b.ccr_wr(0x0105);
  b.push(b.p4(true, P4B_CCN_CCR, 0x38, 0x0C)); b.run_ops();
  for (int w = 0; w < 8; w++) b.st(0xE0120000u + 4 * w, 0x5000u + w);
  b.op(DK_PREF, 0xE0120000u);
  until_c(b, [&] { return b.t->s_claimed; }, 200);
  CHK(b.kill_reset(false) == 1, "fix kill: the SQ PREF was not claimed");
  b.drain();
  CHK((uint32_t)b.rmem.rq(0x0C07A800u) == 0x12344321u && (uint32_t)b.rmem.rq(0x0C120000u) == 0x5000u &&
      (uint32_t)(b.rmem.rq(0x0C120018u) >> 32) == 0x5007u, "fix kill: killed WTB store / SQ burst did not write memory");
  // TAS.B across a manual reset (SOC.md 2.3: the reset ends the CPU's bus lock, and a locked read presented across it
  // is served unlocked -- the BIU's and the stub's rule), at each point of the locked pair.  After each kill the bus
  // must serve the OC again: an uncached load is acked (a lock kept for the pair's write, which the restarted core
  // never issues, would hold the OC's read port for good).
  auto tas = [&](uint32_t va) {
    b.push(b.acc(DK_TAS_PURGE, va, 0)); auto rd = b.push(b.acc(DK_TAS_RD, va, 0));
    auto wr = b.push(b.acc(DK_TAS_WR, va, 0, 0x80)); return std::make_pair(rd, wr);
  };
  auto bus_back = [&](const char *what) {
    b.ccr_wr(0x0105);
    auto u = b.ld(0xAC07B040u); b.run_ops();
    CHK(u->done && u->tack >= 0 && u->lat() < 60, "fix kill: TAS.B %s: the uncached load after the reset was not "
        "served (lat %d)", what, u->done ? u->lat() : -1);
  };
  const auto n_unc_rdy = [&] { return b.e_rdy[BRQ_OC_UNC].size(); };
  // (a) the locked read claimed but not accepted (the bus busy with a write-back): presented across the reset
  b.drain();
  b.st(X + 0x3000, 0xA5A5A5A5u); b.drain();
  b.op(DK_OCBP, X + 0x3000);
  {
    auto pr = tas(0x8C07B100u);
    const size_t n0 = n_unc_rdy();
    until_c(b, [&] { return b.ma == pr.first && b.t->o_claimed; }, 60);
    CHK(b.ma == pr.first && b.t->o_claimed && n_unc_rdy() == n0 && bq_up(b, BRQ_OC_UNC),
        "fix kill: TAS.B (a): the locked read is not presented and unaccepted at the reset");
    CHK(b.kill_reset(true) == 1, "fix kill: TAS.B (a): the locked read was not claimed");
    CHK(n_unc_rdy() == n0, "fix kill: TAS.B (a): the locked read was accepted during the manual reset");
    bus_back("(a) locked read presented across the reset");
    CHK(n_unc_rdy() >= n0 + 2, "fix kill: TAS.B (a): the locked read was not served after the reset");
  }
  // (b) the locked read accepted (the pair waits for its write): the reset ends the lock
  b.drain();
  {
    auto pr = tas(0x8C07B120u);
    const size_t n0 = n_unc_rdy();
    until_c(b, [&] { return b.ma == pr.first && n_unc_rdy() > n0; }, 60);
    CHK(b.ma == pr.first && n_unc_rdy() == n0 + 1, "fix kill: TAS.B (b): the locked read not accepted and waiting");
    CHK(b.kill_reset(true) == 1, "fix kill: TAS.B (b): the locked read was not claimed");
    bus_back("(b) locked read accepted, pair waiting for its write");
  }
  // (c) the pair's write killed before its claim (in its m): nothing written, the lock ends
  b.drain();
  {
    const uint64_t m0 = b.rmem.rq(0x0C07B140u);
    auto pr = tas(0x8C07B141u);
    until_c(b, [&] { return pr.second->t0 >= 0; }, 80);
    CHK(b.ma == pr.second && !b.t->o_claimed, "fix kill: TAS.B (c): the write not in its first MA cycle");
    CHK(b.kill_reset(true) == 0, "fix kill: TAS.B (c): the write killed in m was claimed");
    bus_back("(c) the pair's write killed before its claim");
    CHK(b.rmem.rq(0x0C07B140u) == m0, "fix kill: TAS.B (c): the unclaimed write reached memory");
  }
  // (d) the pair's write killed after its claim: it stands
  b.drain();
  {
    const uint64_t m0 = b.rmem.rq(0x0C07B160u);
    auto pr = tas(0x8C07B162u);
    until_c(b, [&] { return b.ma == pr.second && b.t->o_claimed; }, 80);
    CHK(b.kill_reset(true) == 1, "fix kill: TAS.B (d): the write was not claimed");
    bus_back("(d) the pair's write killed after its claim");
    CHK(b.rmem.rq(0x0C07B160u) == ((m0 & ~0xFF0000ull) | 0x800000ull), "fix kill: TAS.B (d): the claimed write "
        "did not stand: %016llx (was %016llx)", (unsigned long long)b.rmem.rq(0x0C07B160u), (unsigned long long)m0);
  }
  b.compare_state("fix kill");
}

// F4 is checked every cycle (xl_* idle values); here the case the verifier used: an OC associative write
static void fx_xl(Bench &b) {
  fresh(b);
  b.ld(X); b.drain();
  b.push(b.p4(true, P4B_OCAA, 0x04000008u, 0x8C078000u));
  int seen = 0;
  for (int i = 0; i < 20; i++) { b.step(); if (b.t->oc_xl_req) { seen++; CHK(!b.t->ic_xl_itlb, "fix xl: ic xl_itlb = 1 "
                                                                               "while the OC asks v1_mmu (F4)"); } }
  b.run_ops();
  CHK(seen > 0, "fix xl: no OC xl_req");
}

// F7: the V-clear comparator; the sweep's six OCBI records
static void fx_ocbi(Bench &b) {
  fresh(b);
  const uint32_t A = X + 0x5000;
  // store behind an OCBI hit of its line: MA distance 1 costs 1 + 2, distance 2 costs 2
  for (int d = 1; d <= 3; d++) {
    b.drain();
    b.st(A, 1); b.drain();
    auto oi = b.op(DK_OCBI, A); auto s = b.op(DK_ST, A, 2, 2); s->gap = d - 1;
    b.run_ops();
    CHK(oi->lat() == 0 && s->lat() == (d == 1 ? 3 : 2) && s->frz_first == (d == 1 ? 1 : 0),
        "fix ocbi: OCBI hit then store at MA distance %d: OCBI %d, store %d (frz from %ld); expected 0, %d",
        d, oi->lat(), s->lat(), (long)s->frz_first, d == 1 ? 3 : 2);
  }
  // other entries, OCBI misses, PREF hits: no comparator; OCBP hit: the same comparator for a load of its entry
  b.drain();
  b.st(A, 1); b.ld(A + 0x20); b.drain();
  auto o1 = b.op(DK_OCBI, A); auto h1 = b.ld(A + 0x20);                 // another entry: hit at once
  b.run_ops(); b.drain();
  auto o2 = b.op(DK_OCBI, A); auto s2 = b.st(A, 3);                     // OCBI miss (absent line): no comparator
  b.run_ops(); b.drain();
  auto p3 = b.op(DK_PREF, A); auto s3 = b.st(A + 4, 4);                 // PREF hit: no state write
  b.run_ops(); b.drain();
  auto o4 = b.op(DK_OCBP, A); auto l4 = b.ld(A + 0x4000);               // OCBP hit (dirty), a load of its entry
  b.run_ops(); b.drain();
  CHK(h1->lat() == 0 && o2->lat() == 2 && s2->lat() == 2 && p3->lat() == 0 && s3->lat() == 0 && l4->frz_first == 1,
      "fix ocbi: other entry %d, OCBI miss %d + store %d, PREF hit %d + store %d, OCBP hit + load frz from %ld",
      h1->lat(), o2->lat(), s2->lat(), p3->lat(), s3->lat(), (long)l4->frz_first);
  (void)o1; (void)o4;
  // the sweep loop "OCBI @Rn ; n x add ; MOV.L R0,@(disp,GBR) ; 40 x add ; ..." against its control "PREF @Rn ; ...":
  // the store is at MA distance 1 for n = 0..2 and n - 1 from n = 3; per-iteration extra = the store's and the
  // OCBI's costs.  Console records 86484 (mem:same; 86478-86485 alike) and 87984 (the PREF control), T by n:
  const int t_ocbi[9] = {47, 47, 48, 48, 49, 50, 51, 52, 53}, t_pref[9] = {44, 44, 45, 46, 47, 48, 49, 50, 51};
  for (int n = 0; n <= 8; n++) {
    const int d = n <= 2 ? 1 : n - 1, P = 44;                           // P: the loop's period with hits
    b.drain();
    b.st(A, 0); b.drain();
    std::vector<OpP> oi, st;
    for (int it = 0; it < 8; it++) {
      auto o = b.acc(DK_OCBI, A); o->gap = it ? P - d - 1 : 0; b.push(o); oi.push_back(o);
      auto s = b.acc(DK_ST, A, 2, it); s->gap = d - 1; b.push(s); st.push_back(s);
    }
    b.run_ops();
    const int extra = (int)(oi[7]->t0 - oi[3]->t0) / 4 - P;            // = the OCBI's + the store's costs
    CHK(extra == t_ocbi[n] - t_pref[n], "fix ocbi: sweep record n = %d: extra %d over the PREF control, console %d",
        n, extra, t_ocbi[n] - t_pref[n]);
  }
  b.compare_state("fix ocbi");
}

// F8: the caches-sq kern.S loops at MA level on the stub, per iteration (console minus its 0.17 fixed cost, rounded
// to the loop's Bclk-locked integer; the residual 0.08-0.25 is the refresh share the stub does not have)
static int kloop(Bench &b, std::vector<std::array<int, 3>> body, uint32_t base, int phase) {
  b.drain(100);
  set_ccr(b, 0x0105 | OCI);                                   // the kernel's prep: every line absent, no dirty victim
  b.drain(20);
  std::vector<OpP> first;
  for (int i = 0; i < 48; i++)
    for (size_t k = 0; k < body.size(); k++) {
      auto o = b.acc(body[k][0], base + 32 * i + body[k][1], 2, 0x100 + i);
      o->gap = body[k][2];
      if (i == 0 && k == 0) o->phase = phase;
      b.push(o);
      if (k == 0) first.push_back(o);
    }
  b.run_ops();
  return (int)((first[47]->t0 - first[23]->t0) / 24);
}
static void fx_loops(Bench &b) {
  fresh(b);
  struct L { const char *name; std::vector<std::array<int, 3>> body; int want; double hw; };
  std::vector<L> ls = {
    {"ldstream", {{DK_LD, 0, 2}}, 18, 18.42}, {"ststream", {{DK_ST, 0, 2}}, 22, 22.15},
    {"stld", {{DK_ST, 0, 2}, {DK_LD, 4, 0}}, 22, 22.23}, {"stst", {{DK_ST, 0, 2}, {DK_ST, 4, 0}}, 26, 26.25},
    {"ldst", {{DK_LD, 0, 2}, {DK_ST, 4, 0}}, 26, 26.25},
  };
  const int pld[8] = {20, 20, 20, 20, 20, 24, 26, 29};
  const double pldh[8] = {20.42, 20.42, 20.42, 20.42, 20.42, 24.29, 26.23, 29.20};
  static char nm[40][32];
  int k = 0;
  for (int d = 1; d <= 8; d++, k++) {
    snprintf(nm[k], 32, "prefld d=%d", d);
    ls.push_back({nm[k], {{DK_PREF, 0, 2}, {DK_LD, 0, 3 * d - 1}}, pld[d - 1], pldh[d - 1]});
  }
  for (int g = 1; g <= 8; g++, k++) {
    snprintf(nm[k], 32, "stldgap g=%d", g);
    ls.push_back({nm[k], {{DK_ST, 0, 2}, {DK_LD, 28, 3 * g - 1}}, g <= 7 ? 26 : 29, g <= 7 ? 26.26 : 29.17});
  }
  const int ldwd[7] = {4, 8, 12, 16, 20, 24, 28}, ldw[7] = {20, 22, 22, 24, 24, 26, 26};
  for (int i = 0; i < 7; i++, k++) {
    snprintf(nm[k], 32, "ldw +%d", ldwd[i]);
    ls.push_back({nm[k], {{DK_LD, 0, 2}, {DK_LD, ldwd[i], 0}}, ldw[i], ldw[i] + 0.40});
  }
  uint32_t base = 0x8C100000u;
  for (auto &l : ls)
    for (int ph = 0; ph < 2; ph++) {
      base += 0x800;
      const int got = kloop(b, l.body, base, ph);
      CHK(got == l.want, "loops %s (MA1 phase %d): %d per iteration, expected %d (console %.2f)", l.name, ph, got,
          l.want, l.hw);
    }
  const int ps = kloop(b, {{DK_PREF, 0, 2}}, base + 0x800, 0);
  printf("loops: prefstream %d per line on the stub (console 14.35: the BSC's line-read accept rate, WI-BSC's M3 "
         "calibration)\n", ps);
}

// V12 of the verification: an SQ burst and an OC fill of the same line, each queued behind a write-back
static void fx_sq_oc(Bench &b) {
  fresh(b);
  b.push(b.p4(true, P4B_CCN_CCR, 0x38, 0x0C)); b.run_ops();
  const uint32_t L = 0x0C07C000u;
  for (int w = 0; w < 8; w++) b.st(0xE007C000u + 4 * w, 0x50000000u + w);
  b.st(X + 0x100, 1); b.drain();
  const uint64_t old = b.rmem.rq(L);
  b.op(DK_OCBWB, X + 0x100); b.op(DK_PREF, 0x80000000u | L); b.op(DK_PREF, 0xE007C000u);
  auto la = b.ld(0x80000000u | L, 3);
  b.run_ops(); b.drain();
  CHK(la->rdata == old, "fix sq/oc: an older fill returned %016llx, memory before the younger burst %016llx",
      (unsigned long long)la->rdata, (unsigned long long)old);
  b.op(DK_OCBI, 0x80000000u | L); b.st(X + 0x100, 2); b.drain();
  for (int w = 0; w < 8; w++) b.st(0xE007C000u + 4 * w, 0x60000000u + w);
  b.op(DK_OCBWB, X + 0x100); b.op(DK_PREF, 0xE007C000u);
  auto lb = b.ld(0x80000000u | L);
  b.run_ops(); b.drain();
  CHK(lb->rdata == 0x60000000u, "fix sq/oc: a younger fill returned %08llx, the older burst wrote 60000000",
      (unsigned long long)lb->rdata);
  b.compare_state("fix sq/oc");
}

// the caches-sq 6.1 / 6.3 rows the stub can express, as MA-level numbers (informational; M3 calibrates end to end)
static void g_hwrows(Bench &b) {
  fresh(b);
  struct Row { const char *name; double hw; double rtl; const char *note; };
  std::vector<Row> rows;
  auto per = [&](std::function<void(int)> body, int n, std::function<int64_t()> endc) -> double {
    b.drain();
    int64_t t0 = b.cyc;
    for (int i = 0; i < n; i++) body(i);
    b.run_ops();
    return (double)(endc() - t0) / n;
    (void)t0;
  };
  (void)per;
  // load-miss penalty by phase
  auto m0 = b.ld(X + 0x6000); m0->phase = 0; b.run_ops(); b.drain();
  auto m1 = b.ld(X + 0x6020); m1->phase = 1; b.run_ops(); b.drain();
  rows.push_back({"load miss penalty (even / odd MA1)", 14.5, (m0->lat() + m1->lat()) / 2.0, "C6 14 / 15"});
  // store-miss stream period
  std::vector<OpP> ss;
  for (int i = 0; i < 6; i++) ss.push_back(b.st(X + 0x6100 + 0x20 * i, i));
  b.run_ops(); b.drain();
  rows.push_back({"store-miss stream (per line, ack to ack)", 19.0 + 3, (double)(ss[5]->tack - ss[1]->tack) / 4, "hw 22.15 with the loop"});
  // stst / ldst / stld
  auto s1 = b.st(X + 0x6400, 1); s1->phase = 0; auto s2 = b.st(X + 0x6404, 2); b.run_ops(); b.drain();
  rows.push_back({"stst: second store over its hit", 22.08, (double)(s2->tack - s1->t0 - 1), "match"});
  auto l1 = b.ld(X + 0x6440); l1->phase = 0; auto s3 = b.st(X + 0x6444, 2); b.run_ops(); b.drain();
  rows.push_back({"ldst: store after a load miss (total over hit)", 22.08, (double)(s3->tack - l1->t0 - 1), "match"});
  // (stld and prefld are loop-bound on the console -- the LFB, the fill latency -- so they are compared as whole
  // kern.S loops in group `loops`, where they match: stld 22 against 22.06, prefld d = 1..5 20 against 20.25)
  // dirty OCBx stream, WTB stream, SQ
  for (int i = 0; i < 6; i++) b.st(X + 0x6600 + 0x20 * i, i);
  b.drain();
  std::vector<OpP> ob;
  for (int i = 0; i < 6; i++) ob.push_back(b.op(DK_OCBWB, X + 0x6600 + 0x20 * i));
  b.run_ops(); b.drain();
  rows.push_back({"dirty OCBWB back to back", 12.2, (double)(ob[5]->tack - ob[1]->tack) / 4, "stub line write 12"});
  std::vector<OpP> ws;
  for (int i = 0; i < 6; i++) ws.push_back(b.st(0xAC07B200u + 4 * i, i));
  b.run_ops(); b.drain();
  rows.push_back({"uncached store stream", 10.1, (double)(ws[5]->tack - ws[1]->tack) / 4, "stub single write 10"});
  auto ps = b.st(0xAC07B300u, 1); auto pl = b.ld(0xAC07B300u); b.run_ops(); b.drain();
  rows.push_back({"uncached store -> load (pair)", 28.3, (double)(pl->tack - ps->t0 + 1), "BSC's"});
  b.push(b.p4(true, P4B_CCN_CCR, 0x38, 0x0C)); b.run_ops();
  std::vector<OpP> pp;
  for (int i = 0; i < 6; i++) pp.push_back(b.op(DK_PREF, 0xE0100000u + 0x20 * (i & 1)));
  b.run_ops(); b.drain();
  rows.push_back({"PREF SQ0 ; PREF SQ1 (per burst)", 12.07, (double)(pp[5]->tack - pp[1]->tack) / 4, "stub burst 12"});
  auto pq = b.op(DK_PREF, 0xE0100000u); auto sq = b.st(0xE0100000u, 1); b.run_ops(); b.drain();
  rows.push_back({"PREF SQ0 then store SQ0 (store waits, from the PREF)", 14.12, (double)(sq->tack - pq->t0),
                  "M3: the SQ release vs the real burst's done"});
  printf("hwrows (Icyc; caches-sq 6.1 / 6.3 against this RTL on the stub; MA-level, loop overhead not modelled):\n");
  for (auto &r : rows) printf("  %-54s hw %6.2f  rtl %6.2f  %s\n", r.name, r.hw, r.rtl, r.note);
}

// ---------------------------------------------------------------------------------------------------------------
// random traffic
struct Pool {
  std::vector<uint32_t> lines;                                     // PA of the data lines
  Pool() {
    const uint32_t idx[6] = {0x000, 0x07F, 0x080, 0x0FF, 0x1A5, 0x1FF};
    const uint32_t tg[5] = {0x0C000000u, 0x0C004000u, 0x0C00C000u, 0x0E000000u, 0x0C088000u};
    for (uint32_t i : idx) for (uint32_t t : tg) lines.push_back(t | (i << 5));
  }
};

static void random_ccr(Bench &b, Rng &r) {
  uint32_t v = OCE | ICE;
  if (r.p(70)) v |= CB;
  if (r.p(20)) v |= WT;
  if (r.p(15)) v |= OIX;
  if (r.p(15)) v |= IIX;
  if (r.p(20)) v |= ORA;
  if (r.p(8)) v &= ~OCE;
  if (r.p(8)) v &= ~ICE;
  if (r.p(10)) v |= OCI;
  if (r.p(10)) v |= ICI;
  b.ccr_wr(v);
  b.push(b.p4(true, P4B_CCN_CCR, 0x38, r.n(8) << 2 & 0x1C));
  b.push(b.p4(true, P4B_CCN_CCR, 0x3C, r.n(8) << 2 & 0x1C));
  b.run_ops();
}

// one random MA-slot operation (or TAS.B's three) for the random groups
static void gen_one(Bench &b, Rng &r, const Pool &pool) {
  const int what = r.n(100);
  OpP o;
  if (what < 6) {                                              // P4 array access
    const int e = pool.lines[r.n(pool.lines.size())] >> 5 & 0x1FF;
    const int w = r.n(8);
    switch (r.n(8)) {
    case 0: o = b.p4(false, P4B_OCAA, 0x04000000u | e << 5); break;
    case 1: o = b.p4(false, P4B_OCDA, 0x05000000u | e << 5 | w << 2); break;
    case 2: o = b.p4(true, P4B_OCDA, 0x05000000u | e << 5 | w << 2, (uint32_t)r.next()); break;
    case 3: {                                                  // non-assoc: a pool tag (write-backs stay in the pool)
      uint32_t pa = pool.lines[r.n(pool.lines.size())];
      o = b.p4(true, P4B_OCAA, 0x04000000u | e << 5, (pa & 0x1FFFFC00u) | r.n(4) | (r.p(30) ? 0xE0000000u : 0));
      break;
    }
    case 4: {                                                  // assoc
      uint32_t pa = pool.lines[r.n(pool.lines.size())];
      o = b.p4(true, P4B_OCAA, 0x04000008u | e << 5, (r.p(50) ? 0x80000000u : 0xA0000000u) | pa | r.n(4));
      break;
    }
    case 5: o = b.p4(false, P4B_ICAA, (uint32_t)r.n(256) << 5); break;
    case 6: o = b.p4(false, P4B_ICDA, 0x01000000u | r.n(256) << 5 | w << 2); break;
    default: {
      const int ie = r.n(256);
      o = b.p4(true, P4B_ICAA, (uint32_t)ie << 5 | 8, 0x8C400000u | (r.n(0x2000) << 2));
      break;
    }
    }
    if (r.p(15)) o->size = r.n(2);                             // a byte / word access: reads 0, writes nothing
    o->gap = r.p(70) ? 0 : r.n(20);
    b.push(o);
    return;
  }
  int kind;
  const int kk = r.n(100);
  kind = kk < 30 ? DK_LD : kk < 55 ? DK_ST : kk < 62 ? DK_PREF : kk < 67 ? DK_OCBI : kk < 72 ? DK_OCBP
       : kk < 77 ? DK_OCBWB : kk < 84 ? DK_MOVCA : kk < 87 ? DK_RMW_RD : kk < 90 ? DK_RMW_WR : -1;
  const int rgw = r.n(100);
  uint32_t va;
  int size = kind == DK_MOVCA ? 2 : r.n(4);
  if (kind == DK_RMW_RD || kind == DK_RMW_WR) size = 0;
  if (rgw < 10) {                                              // SQ
    va = 0xE0000000u | 0x00300000u | (r.n(0x800) << 5) | (r.n(8) << 2);
    if (size < 2 && !r.p(3)) size = 2 + r.n(2);
  } else if (rgw < 17 && (b.m.ccr_ & ORA)) {                   // OC RAM
    va = 0x7C000000u | (r.n(0x4000) << 13 & 0x03FFE000u) | (r.n(0x2000) & ~7u);
  } else if (rgw < 20) {                                       // area 7 (untranslated: cacheable space, data 0)
    va = 0x9C000000u | (r.n(0x400) << 5);
  } else {
    uint32_t pa = pool.lines[r.n(pool.lines.size())] | (r.n(32) & ~7u);
    const int s = r.n(20);
    va = (s < 11 ? 0x80000000u : s < 14 ? 0x00000000u : s < 16 ? 0xC0000000u : 0xA0000000u) | pa;
  }
  va &= ~((1u << size) - 1);
  if (kind == DK_MOVCA) va &= ~3u;
  if (kind < 0) {                                              // TAS.B: purge, locked read, write (RG_MEM only)
    uint32_t pa = pool.lines[r.n(pool.lines.size())] | r.n(32);
    uint32_t tv = (r.p(70) ? 0x80000000u : 0xA0000000u) | pa;
    auto a = b.acc(DK_TAS_PURGE, tv, 0); auto c = b.acc(DK_TAS_RD, tv, 0); auto d = b.acc(DK_TAS_WR, tv, 0, 0x80);
    if (a->rg != RG_MEM) return;
    b.push(a); b.push(c); b.push(d);
    return;
  }
  o = b.acc(kind, va, size, r.next());
  o->gap = r.p(60) ? 0 : r.p(80) ? r.n(4) : r.n(40);
  if (r.p(1)) b.extra_arr_strobe = true;
  b.push(o);
}

// the random fetch stream: P1 / P0 / P2 code in 0x0C400000-0x0C407FFF, jumps, poisons, ITLB refill stalls
static std::function<bool(FReq &)> fetch_gen(Rng &r) {
  return [&r](FReq &f) {
    static uint32_t pc = 0x0C400000u;
    if (r.p(12)) pc = 0x0C400000u | (r.n(0x2000) << 2);
    else pc += 4;
    pc = 0x0C400000u | (pc & 0x7FFC);
    const int seg = r.n(10);
    f = FReq{};
    f.va = (seg < 6 ? 0x80000000u : seg < 8 ? 0x00000000u : 0xA0000000u) | pc;
    f.pa = pc; f.c = seg < 8;
    if (r.p(2)) { f.fx = r.p(50) ? FX_ADDR : FX_TLB_MISS; f.vec400 = f.fx == FX_TLB_MISS; }
    else if (r.p(2)) f.stall = 1 + r.n(8);
    return r.p(70);
  };
}

static void g_random(Bench &b, long nops) {
  fresh(b);
  Rng &r = b.rng;
  Pool pool;
  // QACR areas used by the SQ bursts land in 0x0C3xxxxx / 0x043xxxxx / 0x003xxxxx / ... never in the code region
  b.f_redirect_ok = true;
  b.f_gen = fetch_gen(r);
  b.f_gap = 0;
  long done = 0;
  int ckpt = 0;
  while (done < nops) {
    // a burst of random MA ops, then a checkpoint
    const int burst = 150 + r.n(250);
    for (int i = 0; i < burst; i++, done++) {
      gen_one(b, r, pool);
    }
    b.run_ops();
    // checkpoint: drain, compare everything, sometimes new CCR / QACR / AT / xl mode / resets
    auto fg = b.f_gen; b.f_gen = nullptr;
    b.run_ops(); b.drain(150);
    // SQ contents: burst both queues into a scratch area and compare memory
    b.op(DK_PREF, 0xE03F0000u); b.op(DK_PREF, 0xE03F0020u);
    b.drain(150);
    b.compare_state("random checkpoint", (ckpt & 3) == 0);
    ckpt++;
    if (r.p(40)) random_ccr(b, r);
    if (r.p(10)) { b.xl_tlb = r.p(50); b.tlb.clear(); b.tlb_mhit.clear();
      for (int k = 0; k < 12; k++) { uint32_t pa = pool.lines[r.n(pool.lines.size())]; uint32_t va = 0x40000000u | (r.n(64) << 10);
        b.tlb[va >> 10] = pa >> 10; if (r.p(10)) b.tlb_mhit[va >> 10] = true; b.tlb[(0x80000000u | pa) >> 10] = pa >> 10; } }
    b.xl_stall_pct = r.p(30) ? 20 : 0;
    if (r.p(10)) { b.mmu_at = b.m.mmu_at = r.p(50); b.sq_xlat = [](uint32_t va) { return 0x0C380000u | (va & 0x1FFE0u); }; }
    if (r.p(3)) { b.reset(r.p(50)); set_ccr(b, 0x0105); }
    b.f_gen = fg;
    if (g_verbose) printf("  random: %ld ops, %ld accesses, %ld P4, %ld fetches, cyc %ld\n", done, b.n_acc, b.n_p4,
                          b.n_fetch, (long)b.cyc);
  }
  b.f_gen = nullptr;
  b.run_ops(); b.drain(150);
  b.compare_state("random end");
  printf("random: %ld accesses, %ld P4 accesses (%ld byte / word array), %ld fetches, %zu IC fills, %ld cycles\n",
         b.n_acc, b.n_p4, b.n_p4_narrow, b.n_fetch, b.ifills.size(), (long)b.cyc);
}

// F10: random traffic with CCR writes of every field (ICE, ICI, OCI, ORA, OIX, IIX, WT, CB, OCE) and QACR writes in the
// middle of it, and manual / CPU-only resets that kill the access in MA while fills, write-backs, WTB stores, uncached
// reads, SQ bursts and TAS.B's locked pair are in flight (the reset rule: kill_reset).  Checkpoints drain and compare
// everything.
static void g_random_inflight(Bench &b, long nops) {
  fresh(b);
  Rng &r = b.rng;
  Pool pool;
  b.f_redirect_ok = true;
  b.f_gen = fetch_gen(r);
  b.f_gap = 0;
  auto rnd_ccr = [&]() {
    uint32_t v = 0;
    if (r.p(85)) v |= OCE;
    if (r.p(80)) v |= ICE;
    if (r.p(70)) v |= CB;
    if (r.p(20)) v |= WT;
    if (r.p(20)) v |= ORA;
    if (r.p(15)) v |= OIX;
    if (r.p(15)) v |= IIX;
    if (r.p(15)) v |= OCI;
    if (r.p(15)) v |= ICI;
    b.ccr_wr(v)->gap = r.n(3);
  };
  long done = 0, kills = 0, ccrw = 0;
  long tas_rd_pres = 0, tas_rd_acc = 0, tas_wr_pre = 0, tas_wr_post = 0;   // TAS.B kills by the point in its pair
  int ckpt = 0, chunks = 0;
  while (done < nops) {
    const int chunk = 10 + r.n(50);
    for (int i = 0; i < chunk; i++, done++) {
      if (r.p(4)) { rnd_ccr(); ccrw++; continue; }
      if (r.p(1)) { b.push(b.p4(true, P4B_CCN_CCR, r.p(50) ? 0x38 : 0x3C, r.n(8) << 2)); continue; }
      gen_one(b, r, pool);
    }
    // sometimes kill an access in flight: a random point inside the chunk
    if (r.p(30)) {
      const int wait = r.n(60);
      for (int k = 0; k < wait && !b.idle(); k++) b.step();
      // TAS.B anywhere in its locked pair is killed by a manual reset: it ends the CPU's bus lock, and a locked read
      // presented across it (claimed, not yet accepted) is served unlocked (SOC.md 2.3; the stub's rule), so nothing
      // waits for the pair's write that the restarted core never issues.  A CPU-only reset keeps a lock and, being
      // synchronous, never kills a claimed access (SOC.md 2.3): it is not aimed at the pair once the read is claimed.
      if (b.ma && !b.ma->p4) {
        const int kk = b.ma->kind;
        const bool claimed = b.t->o_claimed;
        const bool pair = kk == DK_TAS_WR || (kk == DK_TAS_RD && claimed);
        const bool man = pair || r.p(60);
        if (kk == DK_TAS_RD && claimed) (bq_up(b, BRQ_OC_UNC) ? tas_rd_pres : tas_rd_acc)++;
        if (kk == DK_TAS_WR) (claimed ? tas_wr_post : tas_wr_pre)++;
        b.kill_reset(man);
        kills++;
        if (man && r.p(70)) rnd_ccr();                           // software turns the caches back on
      }
    }
    // keep the traffic going: run until only a couple of ops are left, so buffers stay busy across chunks
    for (int k = 0; k < 20000 && b.maq.size() > 2; k++) b.step();
    if (++chunks % 6 == 0) {
      auto fg = b.f_gen; b.f_gen = nullptr;
      b.run_ops(); b.drain(150);
      b.op(DK_PREF, 0xE03F0000u); b.op(DK_PREF, 0xE03F0020u);
      b.drain(150);
      b.compare_state("random in-flight checkpoint", (ckpt & 3) == 0);
      ckpt++;
      b.f_gen = fg;
    }
  }
  b.f_gen = nullptr;
  b.run_ops(); b.drain(150);
  b.compare_state("random in-flight end");
  printf("random in-flight: %ld ops, %ld CCR writes, %ld kills (%ld applied, %ld dropped), %ld comparator waits\n",
         done, ccrw, kills, b.n_kill_applied, b.n_kill_dropped, b.n_shift);
  printf("random in-flight: TAS.B killed by a manual reset: locked read presented %ld, accepted %ld; its write before "
         "its claim %ld, after %ld\n", tas_rd_pres, tas_rd_acc, tas_wr_pre, tas_wr_post);
}

// ---------------------------------------------------------------------------------------------------------------
int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  uint64_t seed = 1;
  long nops = 60000;
  std::string only;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--seed" && i + 1 < argc) seed = strtoull(argv[++i], nullptr, 0);
    else if (a == "--n" && i + 1 < argc) nops = strtol(argv[++i], nullptr, 0);
    else if (a == "--only" && i + 1 < argc) only = argv[++i];
    else if (a == "-v") g_verbose = true;
    else if (a == "--watch" && i + 1 < argc) g_watch = strtoll(argv[++i], nullptr, 0) & 0x1FFFFFE0;
    else if (a == "--trace-from" && i + 1 < argc) g_trace_from = strtoll(argv[++i], nullptr, 0);
  }
  auto top = std::make_unique<Vv1_cache_tb_top>();
  Bench b(top.get(), seed);
  b.reset(true);
  struct G { const char *name; std::function<void()> fn; };
  std::vector<G> gs = {
    {"c1", [&] { g_c1(b); }}, {"c2", [&] { g_c2(b); }}, {"c6", [&] { g_c6(b); }}, {"lfb", [&] { g_lfb(b); }},
    {"wbb", [&] { g_wbb_wtb(b); }}, {"ops", [&] { g_ops(b); }}, {"arrays", [&] { g_arrays(b); }},
    {"ocram", [&] { g_ocram(b); }}, {"index", [&] { g_index(b); }}, {"ccr", [&] { g_ccr(b); }},
    {"sq", [&] { g_sq(b); }}, {"ic", [&] { g_ic(b); }}, {"reset", [&] { g_reset(b); }}, {"events", [&] { g_events(b); }},
    {"fix_ic", [&] { fx_ic_busy(b); }}, {"fix_ici", [&] { fx_ic_ici(b); }}, {"fix_oci", [&] { fx_oc_oci(b); }},
    {"fix_ocram", [&] { fx_ocram_fill(b); }}, {"fix_stale", [&] { fx_stale_unc(b); }}, {"fix_kill", [&] { fx_kill(b); }},
    {"fix_xl", [&] { fx_xl(b); }}, {"ocbi", [&] { fx_ocbi(b); }}, {"loops", [&] { fx_loops(b); }},
    {"sqoc", [&] { fx_sq_oc(b); }},
    {"hwrows", [&] { g_hwrows(b); }}, {"random", [&] { g_random(b, nops); }},
    {"inflight", [&] { g_random_inflight(b, nops / 2); }},
  };
  for (auto &g : gs) {
    if (!only.empty() && only != g.name) continue;
    long f0 = g_fail, c0 = g_checks;
    g.fn();
    printf("group %-8s %6ld checks, %ld failures\n", g.name, g_checks - c0, g_fail - f0);
  }
  printf("v1-cache tb: %ld checks, %ld failures, %ld cycles, %ld SQ size warnings expected\n", g_checks, g_fail,
         (long)b.cyc, b.n_sqwarn);
  printf("v1-cache: %s\n", g_fail ? "FAIL" : "PASS");
  return g_fail ? 1 : 0;
}
