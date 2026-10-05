// v1_lsu_tb -- unit testbench of the MA-stage router rtl/v1/v1_lsu.sv (SOC.md 3.2.2, 4.2, 6.3; WI-P4), with the
// real bridge rtl/v1/v1_p4bus.sv behind it (tb/v1_lsu_tb.sv) and tb/v1_p4bus_tb.h's slave stubs.  The core side is
// driven as the MA stage drives it (one access presented until its ack, junk fields while lreq.v = 0); the OC and SQ
// ports are answered by stubs with a per-access latency (0 = hit in MA1; k = ack in cycle k, lrsp.frz in the first
// min(2, k) cycles, the 2-cycle miss path).
//
// Checked in every cycle: the MA port contract (lrsp.ack / frz only while lreq.v, never both; ocq / sqq / p4q valid
// only with lreq.v and only for their route; ocq / sqq equal lreq), the route of every (kind, region), the response
// (data, the moment of the ack), the commit record (MEMW / VOLRD, by the SOC.md 6.3 rule from tb/p4bus_ref.h), the
// PMC event bits 0x09 / 0x0D / 0x25 (no other bit), and last_rdata.
// Groups:
//   route    every dkind x every region class (MEM cached / uncached, A7, OCRAM, SQ, ARR, P4REG untranslated and
//            translated, P4RSV untranslated and translated), sizes B / W / L / Q
//   scif     PREF / OCBI / OCBP / OCBWB / TAS purge of SCFRDR2 (0xFFE80014) leave the FIFO count unchanged; a load pops
//   quad     SZ = 1 FMOV to P4: two longword transactions a, a + 4 (loads assemble {hi, lo}; stores split wdata)
//   lastrd   last_rdata after loads on every route, not stores; 0 after a power-on reset, held by manual / CPU-only;
//            returned through the undriven UBC window
//   records  VOLRD (SOC.md 6.3) for Bclk / Pclk / PMCTR / MMUCR (CCN 0x10, its mirrors, through the UTLB) / UBC_NODRV
//            reads and uncached board reads outside area-3 RAM and the boot ROM (flash included); none for the other
//            Iclk registers (PTEH, PTEA, CCR, PVR, BARA), arrays, area 3, the boot ROM, cached reads; MEMW on every
//            route
//   timing   the SOC.md 3.1.5 costs as MA occupancy through the LSU at every Pclk phase, back to back and isolated;
//            OC hit 1, OC miss passes frz through; event 0x25 = occupancy - 1
//   pmc      0x09 / 0x0D per iteration of the console's pmcsurvey sqw / sqpref loops (the SQ PREF counts in 0x09)
//   random   random kinds / regions / sizes / latencies / gaps, with DMAC traffic on the bridge's master 1 (presented
//            from its Bclk flop: in the Icyc after a bce)
// Options: --seed N, --n N, -v.
#include "Vv1_lsu_tb_top.h"
#include "verilated.h"
#include "v1_p4bus_tb.h"
#include <deque>
#include <memory>
#include <set>
#include <algorithm>

using namespace p4tb;

static uint64_t sq_data(uint32_t va) { return 0x5100000000000000ull | va; }   // the SQ stub's (distinct from the OC)
static uint64_t oc_data(uint32_t va, int size) {                      // the OC stub's load data
  uint64_t v = ((uint64_t)(va * 0x9E3779B1u) << 32) | (va ^ 0xA5A5A5A5u);
  return size == 0 ? v & 0xFF : size == 1 ? v & 0xFFFF : size == 2 ? v & 0xFFFFFFFFull : v;
}

struct BrT {                                                            // one expected bridge transaction
  int owner; int idx;                                                   // owner 0 = LSU access idx (half), 1 = DMAC
  int port, blk; uint32_t off; int size; bool we; uint32_t wdata; bool dma;
  int ncap = 0; uint32_t capval = 0; int64_t tcap = -1;
};

struct Acc {
  int kind = DK_LD, size = 2; uint32_t va = 0; int rg = RG_MEM; uint32_t pa = 0; bool c = true;
  uint64_t wdata = 0; uint8_t be = 0xFF; uint8_t isn = 0; int gap = 0; int64_t at = -1;
  int lat = 0;                                                          // OC / SQ stub latency
  // derived
  int route = 0; bool quad = false; uint32_t base = 0; int nbr = 0; BrT *br[2] = {nullptr, nullptr};
  // results
  int64_t t0 = -1, tack = -1; uint64_t rdata = 0; int nack = 0; int n25 = 0; bool abandon = false;
  uint64_t ev_ack = 0;                                                  // pmc_ev in the ack cycle
  int occ() const { return (int)(tack - t0 + 1); }
};

struct Bench {
  Vv1_lsu_tb_top *top;
  SlaveSide<Vv1_lsu_tb_top> ss;
  int64_t cyc = 0;
  std::deque<Acc *> q;
  Acc *cur = nullptr;
  int64_t free_at = 0;
  std::vector<std::unique_ptr<Acc>> all;
  std::vector<std::unique_ptr<BrT>> brs;
  std::vector<BrT *> pend;
  // DMAC master (random traffic)
  std::deque<BrT *> dq; BrT *dcur = nullptr; long dma_acks = 0; int64_t dfree = 0;
  uint32_t lrd = 0;                                                     // model of last_rdata
  bool r_pin = false, r_man = false, r_cpu = false;
  Rng junk{0xC0FFEE};
  uint8_t isn = 0;

  explicit Bench(Vv1_lsu_tb_top *t) : top(t) {}

  Acc *mk(int kind, int size, uint32_t va, int rg, uint32_t pa, bool c = true, uint64_t wdata = 0, int lat = 0,
          int gap = 0, int64_t at = -1) {
    all.emplace_back(new Acc());
    Acc *a = all.back().get();
    a->kind = kind; a->size = size; a->va = va; a->rg = rg; a->pa = pa & 0x1FFFFFFF; a->c = c; a->wdata = wdata;
    a->lat = lat; a->gap = gap; a->at = at; a->isn = isn++; a->be = (uint8_t)(0xFF >> (8 - (1 << size)));
    a->route = lsu_route(kind, rg);
    a->quad = size == 3;
    a->base = lsu_p4addr(rg, va, a->pa);
    if (a->route == R_BR_READ || a->route == R_BR_WRITE) {
      a->nbr = a->quad ? 2 : 1;
      for (int h = 0; h < a->nbr; h++) {
        uint32_t ad = a->base + 4 * h;
        int blk = decode(ad, false);
        if (port_of(blk) == PP_BRIDGE) continue;                          // answered by the bridge itself
        brs.emplace_back(new BrT());
        BrT *b = brs.back().get();
        *b = BrT{0, h, port_of(blk), blk, slave_off(ad, false), a->quad ? 2 : size, a->route == R_BR_WRITE,
                 (uint32_t)(h ? wdata >> 32 : wdata), false};
        a->br[h] = b;
      }
    }
    q.push_back(a);
    return a;
  }
  void dmac(bool we, uint32_t addr, int size, uint32_t wdata) {
    brs.emplace_back(new BrT());
    BrT *b = brs.back().get();
    int blk = decode(addr, true);
    *b = BrT{1, 0, port_of(blk), blk, slave_off(addr, true), size, we, we ? wdata : 0, true};
    b->tcap = addr;                                                       // (the address, until captured)
    dq.push_back(b);
  }
  bool busy() const { return cur || !q.empty() || !pend.empty() || dcur || !dq.empty(); }

  void step() {
    bool bce = (cyc & 1) == 1, pce = (cyc & 3) == 3;
    TB_CHECK(cyc == 0 || top->last_rdata == lrd, "cyc %ld: last_rdata %08x expected %08x", (long)cyc, top->last_rdata, lrd);
    if (!cur && !q.empty()) {
      Acc *a = q.front();
      if (cyc >= std::max(free_at + a->gap, a->at)) {
        cur = a; q.pop_front(); a->t0 = cyc;
        for (int h = 0; h < 2; h++) if (a->br[h]) pend.push_back(a->br[h]);
      }
    }
    // the DMAC presents from a Bclk flop (SOC.md 3.1.1): in the Icyc after a bce, and after an ack not before the Icyc
    // after the bce that consumes it (ack + 2)
    if (!dcur && !dq.empty() && !bce && cyc >= dfree) { dcur = dq.front(); dq.pop_front(); pend.push_back(dcur); }
    // drive lreq (junk fields while v = 0)
    Acc *a = cur;
    top->l_v = a != nullptr;
    top->l_kind = a ? a->kind : junk.n(16); top->l_size = a ? a->size : junk.n(4);
    top->l_va = a ? a->va : junk.u32(); top->l_rg = a ? a->rg : junk.n(8); top->l_pa = a ? a->pa : junk.u32() & 0x1FFFFFFF;
    top->l_c = a ? a->c : junk.n(2); top->l_wt = 0; top->l_arch = 0;
    top->l_wdata = a ? a->wdata : junk.next(); top->l_be = a ? a->be : junk.n(256); top->l_isn = a ? a->isn : junk.n(256);
    // DMAC master
    top->m1_v = dcur != nullptr; top->m1_we = dcur ? dcur->we : 0; top->m1_dma = 1; top->m1_size = dcur ? dcur->size : 0;
    top->m1_addr = dcur ? (0x1F000000u | (dcur->off & 0xFFFFFF)) : 0; top->m1_wdata = dcur ? dcur->wdata : 0;
    top->rst_pin = r_pin; top->rst_por = r_pin; top->rst_man = r_man; top->rst_cpu = r_pin || r_man || r_cpu;
    top->bce = bce; top->pce = pce;
    top->clk = 0;
    top->eval();

    // the OC / SQ stubs and the bridge's slaves answer
    int64_t el = a ? cyc - a->t0 : 0;
    bool unit_ack = a && el == a->lat, unit_frz = a && el < std::min(2, a->lat);
    uint64_t unit_d = (a && dk_reads(a->kind)) ? (a->route == R_OC ? oc_data(a->va, a->size) : sq_data(a->va)) : 0;
    top->ocr_ack = top->oc_v && unit_ack; top->ocr_frz = top->oc_v && unit_frz; top->ocr_rdata = top->oc_v ? unit_d : 0;
    top->sqr_ack = top->sq_v && unit_ack; top->sqr_frz = top->sq_v && unit_frz; top->sqr_rdata = top->sq_v ? unit_d : 0;
    ss.answer(top, cyc, bce, pce);
    top->eval();

    // ---- contract and routing
    uint64_t ev = top->pmc_ev;
    if (!a) {
      TB_CHECK(!top->lr_ack && !top->lr_frz && top->lr_rdata == 0, "cyc %ld: lrsp without lreq.v", (long)cyc);
      TB_CHECK(!top->oc_v && !top->sq_v && !top->p4_v, "cyc %ld: a unit request without lreq.v", (long)cyc);
      TB_CHECK(!top->t_v && ev == 0, "cyc %ld: record / event without lreq.v", (long)cyc);
    } else {
      TB_CHECK(!(top->lr_ack && top->lr_frz), "cyc %ld: ack and frz together", (long)cyc);
      TB_CHECK(top->oc_v == (a->route == R_OC) && (!top->oc_v || top->oc_same), "cyc %ld: ocq for kind %d rg %d",
               (long)cyc, a->kind, a->rg);
      TB_CHECK(top->sq_v == (a->route == R_SQ) && (!top->sq_v || top->sq_same), "cyc %ld: sqq for kind %d rg %d",
               (long)cyc, a->kind, a->rg);
      bool br = a->route == R_BR_READ || a->route == R_BR_WRITE;
      TB_CHECK(top->p4_v == br, "cyc %ld: p4q.v %d for kind %d rg %d", (long)cyc, top->p4_v, a->kind, a->rg);
      if (top->p4_v) {
        int h = qhalf;
        TB_CHECK(top->p4_we == (a->route == R_BR_WRITE) && top->p4_size == (a->quad ? 2 : a->size) && !top->p4_dma,
                 "cyc %ld: p4q fields", (long)cyc);
        TB_CHECK(top->p4_addr == a->base + 4 * h && top->p4_wdata == (uint32_t)(h ? a->wdata >> 32 : a->wdata),
                 "cyc %ld: p4q addr %08x wdata %08x (half %d)", (long)cyc, top->p4_addr, top->p4_wdata, h);
      }
      if (a->route == R_OC || a->route == R_SQ)
        TB_CHECK(top->lr_frz == unit_frz, "cyc %ld: frz not passed through", (long)cyc);
      else TB_CHECK(!top->lr_frz, "cyc %ld: frz on a non-OC route", (long)cyc);
      if (a->route == R_OC || a->route == R_SQ)
        TB_CHECK(top->lr_ack == unit_ack, "cyc %ld: ack %d, the unit acked %d", (long)cyc, top->lr_ack, unit_ack);
      if (a->route == R_BR_NOP) TB_CHECK(top->lr_ack && el == 0, "cyc %ld: a no-transaction P4 op not acked in MA1",
                                         (long)cyc);
    }

    // ---- captures
    for (auto &c : ss.cyc_caps) {
      auto it = std::find_if(pend.begin(), pend.end(), [&](BrT *t) {
        return t->port == c.port && t->blk == c.blk && t->off == c.off && t->size == c.size && t->we == c.we &&
               t->wdata == c.wdata && t->dma == c.dma;
      });
      if (it == pend.end()) {
        TB_CHECK(false, "cyc %ld: capture on port %d (%s off %07x %s) matches nothing", (long)cyc, c.port,
                 blk_name(c.blk), c.off, c.we ? "wr" : "rd");
        continue;
      }
      BrT *t = *it; pend.erase(it);
      t->ncap++; t->capval = c.rdata; t->tcap = cyc;
    }
    // ---- DMAC ack
    if (top->m1_ack) {
      TB_CHECK(dcur != nullptr, "cyc %ld: DMAC ack without request", (long)cyc);
      // a DMAC write is never posted: its ack is its capture cycle (SOC.md 3.1.1)
      if (dcur && dcur->we) TB_CHECK(dcur->ncap == 1 && dcur->tcap == cyc, "cyc %ld: DMAC write %s off %07x acked "
                                     "without its capture in this cycle (%d captures)", (long)cyc, blk_name(dcur->blk),
                                     dcur->off, dcur->ncap);
      dcur = nullptr; dma_acks++; dfree = cyc + 2;
    }

    // ---- the LSU ack: data, record, events, last_rdata
    bool ack = a && top->lr_ack;
    if (a && !ack) a->n25++;
    uint64_t e09 = 1ull << 0x09, e0d = 1ull << 0x0D, e25 = 1ull << 0x25;
    uint64_t exp_ev = (a && !ack) ? e25 : 0;
    if (ack) {
      exp_ev |= lsu_ev_op_all(a->kind) ? e09 : 0;
      exp_ev |= lsu_ev_onchip_io(a->kind, a->rg) ? e0d : 0;
    }
    TB_CHECK(ev == exp_ev, "cyc %ld: pmc_ev %016lx expected %016lx (kind %d rg %d)", (long)cyc, (unsigned long)ev,
             (unsigned long)exp_ev, a ? a->kind : -1, a ? a->rg : -1);
    if (ack) {
      uint64_t d = top->lr_rdata, e = 0;
      if (a->route == R_OC || a->route == R_SQ) e = unit_d;
      else if (a->route == R_BR_READ) {
        for (int h = 0; h < a->nbr; h++) {
          uint32_t v;
          if (a->br[h]) {
            TB_CHECK(a->br[h]->ncap == 1, "access %08x half %d acked before its capture", a->va, h);
            v = a->br[h]->capval;
          } else {
            v = decode(a->base + 4 * h, false) == P4B_UBC_NODRV ? lrd : 0;
          }
          e |= (uint64_t)v << (32 * h);
        }
      } else if (a->route == R_BR_WRITE) {
        for (int h = 0; h < a->nbr; h++)
          if (a->br[h] && clk_class(a->br[h]->blk) == P4K_ICLK)
            TB_CHECK(a->br[h]->ncap == 1, "Iclk write %08x half %d acked before its capture", a->va, h);
      }
      TB_CHECK(d == e, "cyc %ld: kind %d rg %d va %08x: rdata %016lx expected %016lx", (long)cyc, a->kind, a->rg, a->va,
               (unsigned long)d, (unsigned long)e);
      // record
      bool memw = kind_writes_data(a->kind), vol = lsu_volatile(a->kind, a->rg, a->va, a->pa, a->c);
      if (memw) {
        TB_CHECK(top->t_v && top->t_kind == 5 && top->t_isn == a->isn && top->t_idx == a->rg && top->t_addr == a->va &&
                 top->t_data == a->wdata && top->t_be == a->be, "cyc %ld: MEMW record for kind %d rg %d", (long)cyc,
                 a->kind, a->rg);
      } else if (vol) {
        TB_CHECK(top->t_v && top->t_kind == 6 && top->t_isn == a->isn && top->t_idx == a->size &&
                 top->t_addr == a->va && top->t_data == d, "cyc %ld: VOLRD record for kind %d rg %d va %08x",
                 (long)cyc, a->kind, a->rg, a->va);
      } else {
        TB_CHECK(!top->t_v, "cyc %ld: unexpected record kind %d for kind %d rg %d va %08x", (long)cyc, top->t_kind,
                 a->kind, a->rg, a->va);
      }
      a->tack = cyc; a->rdata = d; a->nack++; a->ev_ack = ev;
      if (dk_reads(a->kind)) lrd = (uint32_t)d;
      cur = nullptr; free_at = cyc + 1;
    } else {
      TB_CHECK(!top->t_v, "cyc %ld: record without an ack", (long)cyc);
    }
    // quad bookkeeping: the bridge's ack of the first longword moves the LSU to a + 4; the LSU's own ack ends it
    if (ack || !cur) qhalf = 0;
    else if (a->quad && top->p4_ack) { TB_CHECK(qhalf == 0, "cyc %ld: a third longword", (long)cyc); qhalf = 1; }
    if (!a || !top->p4_v) TB_CHECK(!top->p4_ack || !a, "cyc %ld: bridge ack without p4q.v", (long)cyc);
    ss.commit(cyc);
    if (r_pin) lrd = 0;
    top->clk = 1;
    top->eval();
    cyc++;
  }
  static bool dk_reads(int k) { return kind_reads(k); }
  int qhalf = 0;                         // the LSU presents the quad's second longword (after the bridge acked the first)

  void idle(int n) { for (int i = 0; i < n; i++) step(); }
  void run(int64_t limit = 50000) {
    int64_t end = cyc + limit;
    while (busy() && cyc < end) step();
    TB_CHECK(!busy(), "run did not finish (cyc %ld)", (long)cyc);
    idle(16);
  }
  void align(int ph) { while ((cyc & 3) != ph) step(); }
  void final_checks() {
    for (auto &u : all) if (!u->abandon) TB_CHECK(u->nack == 1, "access kind %d rg %d va %08x: %d acks", u->kind, u->rg,
                                                   u->va, u->nack);
    for (auto &b : brs) TB_CHECK(b->ncap == 1, "bridge transaction %s off %07x (owner %d): %d captures",
                                 blk_name(b->blk), b->off, b->owner, b->ncap);
  }
};

static Vv1_lsu_tb_top *g_top;
static std::unique_ptr<Bench> newbench() {
  std::unique_ptr<Bench> b(new Bench(g_top));
  b->r_pin = true; b->idle(8); b->r_pin = false; b->idle(4);
  return b;
}

// ------------------------------------------------------------------------------------------------------------------
struct Reg { int rg; uint32_t va, pa; bool c; const char *name; };
static const Reg REGIONS[] = {
  {RG_MEM, 0x8C010000, 0x0C010000, true, "MEM cached"},       {RG_MEM, 0xA05F6800, 0x005F6800, false, "MEM uncached a0"},
  {RG_A7, 0x9F000024, 0x1F000024, true, "A7"},                  {RG_OCRAM, 0x7C001000, 0x1C001000, true, "OCRAM"},
  {RG_SQ, 0xE0000020, 0x00000020, false, "SQ"},                 {RG_ARR, 0xF4001000, 0x14001000, false, "ARR (OCAA)"},
  {RG_P4REG, 0xFF200000, 0x1F200000, false, "P4REG BARA"},     {RG_P4REG, 0x1F000030, 0x1F000030, false, "P4REG xlat PVR"},
  {RG_P4REG, 0xFFD00004, 0x1FD00004, false, "P4REG IPRA"},     {RG_P4REG, 0xFF800000, 0x1F800000, false, "P4REG BCR1"},
  {RG_P4RSV, 0xE4000000, 0x04000000, false, "P4RSV"},          {RG_P4RSV, 0x1D000000, 0x1D000000, false, "P4RSV xlat"},
};

static void t_route() {
  auto b = newbench();
  int n = 0;
  for (auto &r : REGIONS)
    for (int k = DK_LD; k <= DK_TAS_WR; k++)
      for (int sz = 0; sz < 4; sz++) {
        if (sz == 3 && k != DK_LD && k != DK_ST) continue;                 // quad: FMOV only
        int size = sz;
        if (r.rg == RG_P4REG && r.va == 0xFFD00004) size = sz == 3 ? 3 : 1; // IPRA is 16-bit
        b->mk(k, size, r.va, r.rg, r.pa, r.c, 0x1122334455667788ull ^ ((uint64_t)k << 40) ^ n, n % 3, 1);
        n++;
      }
  b->run(); b->final_checks();
  printf("route: %d accesses (every kind x %zu regions x sizes), contract / route / data / records / events checked\n",
         n, sizeof(REGIONS) / sizeof(REGIONS[0]));
}

static void t_scif() {
  auto b = newbench();
  for (int k : {DK_PREF, DK_OCBI, DK_OCBP, DK_OCBWB, DK_TAS_PURGE}) b->mk(k, 2, 0xFFE80014, RG_P4REG, 0x1FE80014, false, 0, 0, 1);
  b->run();
  uint64_t pops0 = b->ss.st[PP_SCIF].fifo_pops;
  long strobes0 = (long)b->ss.st[PP_SCIF].strobes;
  Acc *ld = b->mk(DK_LD, 0, 0xFFE80014, RG_P4REG, 0x1FE80014, false, 0, 0, 1);
  b->run(); b->final_checks();
  TB_CHECK(pops0 == 0 && strobes0 == 0, "PREF / OCBx / TAS purge of SCFRDR2: %lu pops, %ld strobes",
           (unsigned long)pops0, strobes0);
  TB_CHECK(b->ss.st[PP_SCIF].fifo_pops == 1 && ld->rdata == 0x40, "a load of SCFRDR2: %lu pops, data %02lx",
           (unsigned long)b->ss.st[PP_SCIF].fifo_pops, (unsigned long)ld->rdata);
  printf("scif: PREF / OCBI / OCBP / OCBWB / TAS purge to 0xFFE80014: FIFO count unchanged; a load pops once\n");
}

static void t_quad() {
  auto b = newbench();
  Acc *l1 = b->mk(DK_LD, 3, 0xFF000000, RG_P4REG, 0x1F000000, false, 0, 0, 1);          // PTEH, PTEL
  Acc *s1 = b->mk(DK_ST, 3, 0xFF200000, RG_P4REG, 0x1F200000, false, 0xAAAABBBBCCCCDDDDull, 0, 1);  // BARA, BAMRA
  Acc *l2 = b->mk(DK_LD, 3, 0xFF200000, RG_P4REG, 0x1F200000, false, 0, 0, 1);
  Acc *s2 = b->mk(DK_ST, 3, 0xFFD80008, RG_P4REG, 0x1FD80008, false, 0x0000000100000002ull, 0, 1);  // TCOR0, TCNT0
  Acc *l3 = b->mk(DK_LD, 3, 0xFFD80008, RG_P4REG, 0x1FD80008, false, 0, 0, 1);
  Acc *o = b->mk(DK_LD, 3, 0x8C000100, RG_MEM, 0x0C000100, true, 0, 0, 1);               // OC: one request
  Acc *s3 = b->mk(DK_ST, 3, 0xE0000008, RG_SQ, 0x00000008, false, 0x0102030405060708ull, 0, 1);  // SQ: one request
  b->run(); b->final_checks();
  TB_CHECK(l1->rdata == ((uint64_t)0 << 32 | 0x8C062000u), "quad PTEH / PTEL %016lx", (unsigned long)l1->rdata);
  TB_CHECK(l2->rdata == 0xCCCCDDDDull, "quad BARA / BAMRA read-back %016lx (BAMRA is 8-bit: 0)", (unsigned long)l2->rdata);
  TB_CHECK((uint32_t)l3->rdata == 2, "quad TCOR0 read-back %016lx", (unsigned long)l3->rdata);
  TB_CHECK(l1->occ() == 6 && s1->occ() == 6, "quad Iclk occupancy %d / %d (two 3-cycle transactions)", l1->occ(),
           s1->occ());
  TB_CHECK(l1->br[0]->off == 0xF000000 && l1->br[1]->off == 0xF000004, "quad halves at a, a + 4");
  (void)s2; (void)o; (void)s3;
  printf("quad: two longword transactions a, a + 4 (loads {hi, lo}, stores split), OC / SQ one request each\n");
}

static void t_lastrd() {
  auto b = newbench();
  b->mk(DK_LD, 2, 0x8C001000, RG_MEM, 0x0C001000, true, 0, 0, 1);          // OC hit
  b->mk(DK_ST, 2, 0x8C001004, RG_MEM, 0x0C001004, true, 0x55, 0, 1);       // a store does not update it
  b->mk(DK_PREF, 2, 0x8C001040, RG_MEM, 0x0C001040, true, 0, 2, 1);
  Acc *u1 = b->mk(DK_LD, 2, 0xFF200100, RG_P4REG, 0x1F200100, false, 0, 0, 1);   // undriven UBC window
  b->mk(DK_LD, 2, 0xFF000030, RG_P4REG, 0x1F000030, false, 0, 0, 1);      // PVR
  Acc *u2 = b->mk(DK_LD, 2, 0xFF200204, RG_P4REG, 0x1F200204, false, 0, 0, 1);
  b->mk(DK_LD, 1, 0x8C002002, RG_MEM, 0x0C002002, true, 0, 3, 1);          // a miss (lat 3)
  b->mk(DK_LD, 2, 0xE0000000, RG_SQ, 0, false, 0, 0, 1);                   // SQ load (the stub's data)
  Acc *u3 = b->mk(DK_LD, 2, 0xFF23FFFC, RG_P4REG, 0x1F23FFFC, false, 0, 0, 1);
  b->run();
  TB_CHECK(u1->rdata == (uint32_t)oc_data(0x8C001000, 2), "UBC_NODRV after an OC load: %08lx", (unsigned long)u1->rdata);
  TB_CHECK(u2->rdata == 0x040205C1, "UBC_NODRV after a PVR load: %08lx", (unsigned long)u2->rdata);
  TB_CHECK(u3->rdata == (uint32_t)sq_data(0xE0000000), "UBC_NODRV after an SQ load: %08lx", (unsigned long)u3->rdata);
  // resets: manual and CPU-only hold it, power-on clears it
  b->mk(DK_LD, 2, 0x8C003000, RG_MEM, 0x0C003000, true, 0, 0, 1);
  b->run();
  uint32_t v = b->top->last_rdata;
  b->r_man = true; b->idle(8); b->r_man = false; b->idle(2);
  TB_CHECK(b->top->last_rdata == v && v != 0, "manual reset changed last_rdata");
  b->r_cpu = true; b->idle(8); b->r_cpu = false; b->idle(2);
  TB_CHECK(b->top->last_rdata == v, "CPU-only reset changed last_rdata");
  b->r_pin = true; b->idle(8); b->r_pin = false; b->idle(2);
  TB_CHECK(b->top->last_rdata == 0, "power-on reset: last_rdata %08x", b->top->last_rdata);
  b->final_checks();
  printf("lastrd: loads on every route update it, stores / PREF do not; UBC_NODRV returns it; POR 0, manual / CPU held\n");
}

static void t_records() {
  struct R { int kind, size; uint32_t va, pa; int rg; bool c; bool vol; const char *n; };
  const R rs[] = {
    {DK_LD, 2, 0xFF800000, 0x1F800000, RG_P4REG, false, true, "BCR1 (Bclk)"},
    {DK_LD, 1, 0xFFD00004, 0x1FD00004, RG_P4REG, false, true, "IPRA (Pclk)"},
    {DK_LD, 2, 0xFFD8000C, 0x1FD8000C, RG_P4REG, false, true, "TCNT0 (Pclk 32)"},
    {DK_LD, 2, 0xFF100008, 0x1F100008, RG_P4REG, false, true, "PMCTR1L"},
    {DK_LD, 2, 0xFF200100, 0x1F200100, RG_P4REG, false, true, "UBC_NODRV"},
    {DK_LD, 2, 0x1FD80024, 0x1FD80024, RG_P4REG, false, true, "TCNT2 through the UTLB"},
    {DK_RMW_RD, 0, 0xFFE80010, 0x1FE80010, RG_P4REG, false, true, "RMW read of a Pclk register"},
    {DK_LD, 2, 0xA05F6800, 0x005F6800, RG_MEM, false, true, "Holly SB (area 0, uncached)"},
    {DK_LD, 2, 0xA5000000, 0x05000000, RG_MEM, false, true, "VRAM (area 1, uncached)"},
    {DK_LD, 2, 0xB0000000, 0x10000000, RG_MEM, false, true, "area 4 uncached"},
    {DK_LD, 2, 0xB8000008, 0x18000008, RG_MEM, false, true, "area 6 uncached (sim control)"},
    {DK_LD, 2, 0xA0220000, 0x00220000, RG_MEM, false, true, "area 0 just past the flash"},
    {DK_LD, 2, 0xFF00001C, 0x1F00001C, RG_P4REG, false, false, "CCR (Iclk)"},
    // MMUCR: its LRUI follows the RTL's speculative ITLB hits (tb/mmu_ref.h calls it volatile), so every MMUCR read is
    // a VOLRD (SOC.md 6.3, I057: the WI-P4 verification's F5); the other CCN_MMU registers are not
    {DK_LD, 2, 0xFF000010, 0x1F000010, RG_P4REG, false, true, "MMUCR (CCN 0x10)"},
    {DK_LD, 2, 0xFF000110, 0x1F000110, RG_P4REG, false, true, "MMUCR mirror (a[17:8] ignored)"},
    {DK_LD, 2, 0xFF03FF10, 0x1F03FF10, RG_P4REG, false, true, "MMUCR mirror (top of the CCN window)"},
    {DK_LD, 2, 0x1F000010, 0x1F000010, RG_P4REG, false, true, "MMUCR through the UTLB"},
    {DK_LD, 2, 0xFF000000, 0x1F000000, RG_P4REG, false, false, "PTEH (CCN_MMU, not MMUCR)"},
    {DK_LD, 2, 0xFF000034, 0x1F000034, RG_P4REG, false, false, "PTEA (CCN_MMU, not MMUCR)"},
    {DK_LD, 2, 0xFF00000C, 0x1F00000C, RG_P4REG, false, false, "TEA (CCN_MMU, not MMUCR)"},
    {DK_LD, 2, 0xFF000030, 0x1F000030, RG_P4REG, false, false, "PVR"},
    {DK_LD, 2, 0xFF200000, 0x1F200000, RG_P4REG, false, false, "BARA"},
    {DK_LD, 2, 0xF4000000, 0x14000000, RG_ARR, false, false, "OC address array"},
    {DK_LD, 2, 0xE4000000, 0x04000000, RG_P4RSV, false, false, "reserved P4"},
    {DK_LD, 2, 0xAC000000, 0x0C000000, RG_MEM, false, false, "area 3 uncached"},
    {DK_LD, 2, 0xA0000000, 0x00000000, RG_MEM, false, false, "boot ROM"},
    {DK_LD, 2, 0xA01FFFFC, 0x001FFFFC, RG_MEM, false, false, "boot ROM end"},
    // flash reads are volatile (SOC.md 6.3, I101: a flash being programmed answers time-dependent status words)
    {DK_LD, 2, 0xA0200000, 0x00200000, RG_MEM, false, true, "flash start"},
    {DK_LD, 1, 0xA0210002, 0x00210002, RG_MEM, false, true, "flash (word)"},
    {DK_LD, 2, 0xA021FFFC, 0x0021FFFC, RG_MEM, false, true, "flash end"},
    {DK_LD, 2, 0x80200000, 0x00200000, RG_MEM, true, false, "flash cached (no VOLRD: the OC route)"},
    {DK_LD, 2, 0x805F6800, 0x005F6800, RG_MEM, true, false, "area 0 cached"},
    {DK_LD, 2, 0xE0000000, 0x00000000, RG_SQ, false, false, "SQ"},
    {DK_LD, 2, 0x9F000024, 0x1F000024, RG_A7, true, false, "area 7 untranslated"},
    {DK_LD, 2, 0x7C000000, 0x1C000000, RG_OCRAM, true, false, "OC RAM"},
    {DK_PREF, 2, 0xFFD80004, 0x1FD80004, RG_P4REG, false, false, "PREF of a Pclk register"},
  };
  auto b = newbench();
  int nv = 0;
  for (auto &r : rs) {
    TB_CHECK(lsu_volatile(r.kind, r.rg, r.va, r.pa, r.c) == r.vol, "p4bus_ref VOLRD rule: %s", r.n);
    b->mk(r.kind, r.size, r.va, r.rg, r.pa, r.c, 0, 0, 1);
    nv += r.vol;
  }
  // MEMW on every route (idx = region)
  for (auto &g : REGIONS)
    for (int k : {DK_ST, DK_MOVCA, DK_RMW_WR, DK_TAS_WR}) b->mk(k, k == DK_ST ? 2 : k == DK_MOVCA ? 2 : 0, g.va, g.rg, g.pa,
                                                                 g.c, 0xFEEDFACE12345678ull, 0, 1);
  b->run(); b->final_checks();
  printf("records: %zu reads (%d VOLRD by the SOC.md 6.3 rule), MEMW for ST / MOVCA / RMW / TAS on %zu regions\n",
         sizeof(rs) / sizeof(rs[0]), nv, sizeof(REGIONS) / sizeof(REGIONS[0]));
}

static std::string setstr(const std::set<int> &s) {
  std::string r = "{";
  for (int x : s) { if (r.size() > 1) r += ","; r += std::to_string(x); }
  return r + "}";
}

static void t_timing() {
  struct T { const char *n; int kind, size; uint32_t va; int rg; int lat; int b2b; std::set<int> iso; };
  const T ts[] = {
    {"OC hit", DK_LD, 2, 0x8C000000, RG_MEM, 0, 1, {1}},
    {"OC miss (stub 15)", DK_LD, 2, 0xAC000000, RG_MEM, 15, 16, {16}},
    {"SQ store", DK_ST, 2, 0xE0000000, RG_SQ, 0, 1, {1}},
    {"CCR rd", DK_LD, 2, 0xFF00001C, RG_P4REG, 0, 3, {3}},
    {"BARA wr", DK_ST, 2, 0xFF200000, RG_P4REG, 0, 3, {3}},
    {"PTEH wr", DK_ST, 2, 0xFF000000, RG_P4REG, 0, 4, {4}},
    {"OCAA rd", DK_LD, 2, 0xF4000000, RG_ARR, 0, 4, {4}},
    {"UTLBDA wr", DK_ST, 2, 0xF7000000, RG_ARR, 0, 4, {4}},
    {"BCR1 rd", DK_LD, 2, 0xFF800000, RG_P4REG, 0, 8, {8, 9}},
    {"SAR1 wr", DK_ST, 2, 0xFFA00010, RG_P4REG, 0, 6, {3}},
    {"FRQCR rd", DK_LD, 1, 0xFFC00000, RG_P4REG, 0, 16, {13, 14, 15, 16}},
    {"TCNT0 rd", DK_LD, 2, 0xFFD8000C, RG_P4REG, 0, 20, {17, 18, 19, 20}},
    {"IPRB wr", DK_ST, 1, 0xFFD00008, RG_P4REG, 0, 8, {3}},
    {"TCOR1 wr", DK_ST, 2, 0xFFD80014, RG_P4REG, 0, 12, {3}},
    {"PREF P4", DK_PREF, 2, 0xFFE80014, RG_P4REG, 0, 1, {1}},
    {"quad CCN rd", DK_LD, 3, 0xFF000000, RG_P4REG, 0, 6, {6}},
  };
  int rows = 0;
  for (auto &t : ts) {
    std::set<int> iso, b2b, ev;
    for (int ph = 0; ph < 4; ph++) {
      {
        auto b = newbench(); b->idle(20); b->align(ph);
        Acc *a = b->mk(t.kind, t.size, t.va, t.rg, t.va & 0x1FFFFFFF, t.rg == RG_MEM && t.lat == 0, 0x1234, t.lat, 0, b->cyc);
        b->run(); b->final_checks();
        iso.insert(a->occ());
        ev.insert(a->n25 - (a->occ() - 1));
      }
      {
        auto b = newbench(); b->idle(20); b->align(ph);
        std::vector<Acc *> v;
        for (int i = 0; i < 10; i++)
          v.push_back(b->mk(t.kind, t.size, t.va, t.rg, t.va & 0x1FFFFFFF, t.rg == RG_MEM && t.lat == 0, 0x1234 + i, t.lat,
                            0, i ? -1 : b->cyc));
        b->run(); b->final_checks();
        b2b.insert((int)(v[9]->t0 - v[8]->t0));
      }
    }
    TB_CHECK(b2b.size() == 1 && *b2b.begin() == t.b2b, "%s: back to back %s expected %d", t.n, setstr(b2b).c_str(), t.b2b);
    TB_CHECK(iso == t.iso, "%s: isolated %s expected %s", t.n, setstr(iso).c_str(), setstr(t.iso).c_str());
    TB_CHECK(ev.size() == 1 && *ev.begin() == 0, "%s: event 0x25 is not occupancy - 1", t.n);
    if (g_verbose) printf("  %-18s b2b %s isolated %s\n", t.n, setstr(b2b).c_str(), setstr(iso).c_str());
    rows++;
  }
  printf("timing: %d rows x 4 phases through the LSU (MA occupancy = the bridge's SOC.md 3.1.5 costs; 0x25 = occ - 1)\n",
         rows);
}

// PMC event counts per loop iteration against the console (debug-ubc-hudi pmcsurvey.out, 2026-09-23, Q rows 09 / 0d):
// sqw = 4 SQ stores: 0x09 4, 0x0D 0; sqpref = 8 SQ stores + PREF @r5 (the SQ PREF: frz 2, then mah until the BIU takes
// the burst; the stub acks after 11): 0x09 9 [HW: the PREF counts once], 0x0D 0.  Also debug-ubc-hudi 4.7's ldCCN /
// ldTMU (0x09 1, 0x0D 1 per load) and the App. A.5 array read (0x09 1, 0x0D 0).
static void t_pmc() {
  struct L { const char *n; int n09, n0d; };
  const L ls[] = {{"sqw", 4, 0}, {"sqpref", 9, 0}, {"ldCCN", 1, 1}, {"ldTMU", 1, 1}, {"OCAA rd", 1, 0}};
  for (int shape = 0; shape < 5; shape++) {
    auto b = newbench();
    std::vector<Acc *> v;
    if (shape <= 1) {
      int nst = shape ? 8 : 4;
      for (int i = 0; i < nst; i++) v.push_back(b->mk(DK_ST, 2, 0xE0000000 + 4 * i, RG_SQ, 4 * i, false, 0x11, 0, 1));
      if (shape) v.push_back(b->mk(DK_PREF, 2, 0xE0000000, RG_SQ, 0, false, 0, 11, 1));
    } else if (shape == 2) v.push_back(b->mk(DK_LD, 2, 0xFF000030, RG_P4REG, 0x1F000030, false, 0, 0, 1));
    else if (shape == 3) v.push_back(b->mk(DK_LD, 2, 0xFFD80020, RG_P4REG, 0x1FD80020, false, 0, 0, 1));
    else v.push_back(b->mk(DK_LD, 2, 0xF4000000, RG_ARR, 0x14000000, false, 0, 0, 1));
    b->run(); b->final_checks();
    int n09 = 0, n0d = 0;
    for (Acc *a : v) { n09 += (a->ev_ack >> 0x09) & 1; n0d += (a->ev_ack >> 0x0D) & 1; }
    TB_CHECK(n09 == ls[shape].n09 && n0d == ls[shape].n0d, "PMC %s: 0x09 %d 0x0D %d per iteration, console %d / %d",
             ls[shape].n, n09, n0d, ls[shape].n09, ls[shape].n0d);
  }
  printf("pmc: 0x09 / 0x0D per iteration of pmcsurvey sqw (4 / 0), sqpref (9 / 0: the PREF counts), ldCCN / ldTMU "
         "(1 / 1), an array read (1 / 0)\n");
}

static void t_random(uint64_t seed, int n) {
  auto b = newbench();
  Rng r(seed);
  for (int p = 0; p < PP_N; p++) { b->ss.st[p].rand_stall_pct = 15; b->ss.st[p].rand_stall_max = 2; }
  const int NR = sizeof(REGIONS) / sizeof(REGIONS[0]);
  const uint32_t dm[] = {0x1FE80010, 0x1FD80024, 0x1FA00010, 0x1F000024, 0x1F100008, 0x1FC00000, 0x1F800030};
  const int dsz[] = {1, 2, 2, 2, 2, 1, 1};
  for (int i = 0; i < n; i++) {
    const Reg &g = REGIONS[r.n(NR)];
    int k = 1 + r.n(12);
    int sz = r.n(4);
    if (sz == 3 && k != DK_LD && k != DK_ST) sz = 2;
    uint32_t va = g.va + ((g.rg == RG_MEM || g.rg == RG_SQ) ? (r.n(64) << 3) : 0);
    b->mk(k, sz, va, g.rg, g.pa + (va - g.va), g.c, r.next(), r.n(4) ? 0 : 1 + r.n(20), r.n(5));
    if (r.n(6) == 0) { int j = r.n(7); b->dmac(r.n(3) == 0 && j >= 2 && j != 4, dm[j], dsz[j], r.u32()); }
  }
  b->run(std::max<int64_t>(400000, 150LL * n)); b->final_checks();
  printf("random: %zu accesses, %zu bridge transactions (%ld DMAC), %ld cycles\n", b->all.size(), b->brs.size(),
         b->dma_acks, (long)b->cyc);
}

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  uint64_t seed = 7; int n = 3000;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = strtoull(argv[++i], nullptr, 0);
    else if (!strcmp(argv[i], "--n") && i + 1 < argc) n = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-v")) g_verbose = 1;
  }
  g_top = new Vv1_lsu_tb_top;
  t_route();
  t_scif();
  t_quad();
  t_lastrd();
  t_records();
  t_timing();
  t_pmc();
  t_random(seed, n);
  delete g_top;
  if (g_fail) { printf("v1-lsu: FAIL (%ld)\n", g_fail); return 1; }
  printf("v1-lsu: PASS\n");
  return 0;
}
