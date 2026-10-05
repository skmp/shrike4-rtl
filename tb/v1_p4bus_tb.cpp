// v1_p4bus_tb -- unit testbench of the P4 register bus bridge rtl/v1/v1_p4bus.sv (SOC.md 3.1; WI-P4), through
// tb/v1_p4bus_tb.sv.  The slave side is tb/v1_p4bus_tb.h (22 register stubs that check the slave contract); the
// masters are driven here as the LSU and the DMAC drive them (a request held until its ack; the DMAC's request rises
// in the Icyc after a bce, from its Bclk flop, and its next one no earlier than the Icyc after the bce that consumes
// the ack).
//
// Groups (every count is printed; any failure fails the run, exit 1):
//   decode   a hand-written table of addresses (debug-ubc-hudi 2.2 / SOC.md 3.1.2: every block, CCN / PMCTR mirrors,
//            the undriven UBC window, the CPG / WDT split, holes, arrays, reserved P4) -> block, port, off; the
//            bridge's own answers (NONE / RSV 0, UBC_NODRV last_rdata); the C++ model p4bus_ref.h agrees
//   timing   the SOC.md 3.1.5 port-level targets, isolated with MA1 at each of the 4 Pclk phases and back to back:
//            Iclk 3 / 3, array 4, PTEH 4, Bclk read 8 (isolated 8..9), Bclk write 3 / 6, Pclk read 16 (isolated
//            13..16), Pclk read32 20 (17..20), Pclk write 3 / 8 / 12, Pclk write -> read of another module 21..24,
//            the TCNT sample point (3..6 Icyc after issue), App. A.4 (a Pclk write does not delay an Iclk read), the
//            ldCCN / ldTMU loops of debug-ubc-hudi 7.3 (period 5 / 20, event 0x25 2 / 17)
//   post     posted-write ordering (a read after a write on the same path sees the new value), other paths do not
//            wait, bpost_bsc exactly while the Bclk entry holds a BSC / SDMR write
//   stall    random stalls on every Iclk / Bclk port (a Pclk slave never stalls: SOC.md 9.19): identical
//            re-presentation at the next opportunity, one capture, cost +1 / +2 per stall on the Iclk / Bclk path
//   dmac     tie (the DMAC wins), independent paths, err for NONE / UBC_NODRV, the dma flag, 0x1F addressing
//   dmacw    DMAC writes are never posted (SOC.md 3.1.1): acked in the slave's capture cycle on every path (checked for
//            every DMAC write in every group), isolated occupancy by phase, the Pclk period (the bus cycle), behind a
//            CPU longword posted write (WI-DMAC F2), a DMAC write to the BSC does not raise bpost_bsc
//   land     SOC.md 9.19: a Pclk write lands at its slave at the end of its bus cycle (strobe + 8 byte / word, + 12
//            longword); a read strobed in a landing pce meets its slave one pce later (new value, same ack); every
//            bench checks the rule for every Pclk transaction (final_checks), this group the directed cases
//   pcehold  M1a-verify F2: bce / pce withheld (v1_cpg's relock settle) right after a landing FRQCR write, with a
//            read strobed in that landing pce: one presentation after the hold, the slave's value, the ack at
//            max(the return schedule, capture + 1); every bench's landing rule counts pce edges, not Icyc
//   arr      arr_strobe once per array transaction (8 arrays x read / write), never otherwise
//   reset    the SOC.md 3.1.6 rule: manual / CPU-only / power-on resets against un-strobed, strobed, stalled and posted
//            transactions: an acked posted write drains (both reset kinds), a strobed one completes without an ack,
//            one neither strobed nor acked is dropped; the DMAC's un-strobed request on a manual reset (dropped, taken
//            again after it) and on a CPU-only reset (untouched); a reset that orphans a captured Iclk read, then an
//            Iclk write (one strobe, occupancy 3); a reset between a posted write's acceptance (MA1) and its ack (MA3)
//   probe    tools/hw/p4bus/p4b.c: 31 PMCTR-bracketed access sequences at each Pclk phase, exact against the console
//   brackets the earlier console brackets (timers-clock F / T, bsc-memmap 6.4 T1 / T8 / T24): reachable at a phase
//   random   both masters, every block, random sizes / gaps / stalls: data, protocol, completion; the posted paths'
//            slot model (a posted write acks at max(MA3, acceptance); bpost_bsc while the Bclk slot holds a BSC /
//            SDMR write, from its acceptance); a second run also withholds the enables at random (after 30 % of the
//            Pclk landings and in 1 % of the other cycles, 1 .. 24 cycles; the slot model is skipped there)
//   randrst  random traffic with random CPU-only / manual resets: no second strobe, every stalled strobe presented
//            again, an orphaned CPU transaction performed iff it was strobed, every other one performed and acked once
//            with its data
// Options: --seed N, --n N (random transactions), -v.
#include "Vv1_p4bus_tb_top.h"
#include "verilated.h"
#include "v1_p4bus_tb.h"
#include <deque>
#include <memory>
#include <set>
#include <algorithm>

using namespace p4tb;

struct Txn {
  int m = 0; bool we = false; int size = 2; uint32_t addr = 0, wdata = 0; bool dma = false;
  int gap = 0; int64_t at = -1;
  // derived
  int blk = 0, port = 0, cls = 0; bool bridge = false; uint32_t off = 0;
  // results
  int64_t t0 = -1, tack = -1, tcap = -1, tstb = -1; uint32_t rdata = 0; bool err = false; int nack = 0, ncap = 0;
  int64_t tas = -1;                      // the path's address strobe (Pclk: the pa_* tap; the others: the capture)
  int nstb = 0;                          // strobes seen by its slave (stalled ones included)
  uint32_t capval = 0; uint32_t lrd = 0; bool abandon = false;
  bool stb_at_rst = false;               // abandoned by a reset: it had been strobed (so it must still be performed)
  int occ() const { return (int)(tack - t0 + 1); }
};

struct Bench {
  Vv1_p4bus_tb_top *top;
  SlaveSide<Vv1_p4bus_tb_top> ss;
  int64_t cyc = 0;
  std::deque<Txn *> q[2];
  Txn *cur[2] = {nullptr, nullptr};
  int64_t free_at[2] = {0, 0};
  std::vector<Txn *> pend;               // presented / acked, capture not seen yet
  std::vector<std::unique_ptr<Txn>> all;
  uint32_t last_rdata = 0x13579BDF;
  bool r_pin = false, r_por = false, r_man = false, r_cpu = false;
  std::vector<std::pair<int64_t, bool>> bpost_trace;
  std::vector<std::pair<int64_t, bool>> wpend_trace;   // pclk_wpend per cycle (SOC.md 2.5: v1_cpg's sleep wait)
  long arr_pulses = 0;
  bool had_reset = false;                // the bench saw a reset after its power-on reset (skips the slot model)
  bool booted = false;                   // newbench's power-on reset is over
  // withheld clock enables (v1_cpg's relock settle / standby stop bce and pce while Iclk runs; SOC.md 2.1, 2.5): the
  // enable phase counter (cyc - poff) freezes while hold_left > 0, as the CPG's pcnt / bcnt do.  hold_on_land > 0:
  // the next Pclk write that lands starts a hold of that many cycles in the following cycle (the CPG enters SETTLE
  // in the cycle after a relocking FRQCR write lands); rand_hold_pct: a random hold starts in that % of cycles
  int hold_left = 0, hold_on_land = 0, rand_hold_pct = 0, rand_hold_max = 24, rand_land_hold_pct = 0;
  int64_t poff = 0;
  bool had_hold = false;                 // the bench withheld the enables at least once (skips the slot model)
  uint64_t hrng = 0x9E3779B97F4A7C15ull;
  std::vector<int64_t> pces;             // every pce cycle of the bench, in order
  static long nhold, nlate;              // holds; deferred reads whose capture came after their return schedule
  static long nbpost, nwpend;
  static long nland, ndefer;             // Pclk writes landed / reads deferred by a landing (SOC.md 9.19)

  explicit Bench(Vv1_p4bus_tb_top *t) : top(t) {}

  Txn *mk(int m, bool we, uint32_t addr, int size, uint32_t wdata = 0, int gap = 0, int64_t at = -1) {
    all.emplace_back(new Txn());
    Txn *t = all.back().get();
    t->m = m; t->we = we; t->addr = addr; t->size = size; t->wdata = we ? wdata : 0x0BADCAFEu ^ addr;
    t->dma = m == 1; t->gap = gap; t->at = at;
    t->blk = decode(addr, t->dma); t->port = port_of(t->blk); t->cls = clk_class(t->blk);
    t->bridge = t->port == PP_BRIDGE; t->off = slave_off(addr, t->dma);
    q[m].push_back(t);
    return t;
  }

  bool busy() const { return cur[0] || cur[1] || !q[0].empty() || !q[1].empty() || !pend.empty(); }

  void step() {
    const bool held = hold_left > 0;
    bool bce = !held && ((cyc - poff) & 1) == 1, pce = !held && ((cyc - poff) & 3) == 3;
    if (pce) pces.push_back(cyc);
    for (int m = 0; m < 2; m++) {
      if (!cur[m] && !q[m].empty()) {
        Txn *t = q[m].front();
        int64_t ready = std::max(free_at[m] + t->gap, t->at);
        // the DMAC presents from a Bclk flop: its request rises in the Icyc after a bce (SOC.md 3.1.1)
        if (cyc >= ready && (m == 0 || !bce)) {
          cur[m] = t; q[m].pop_front(); t->t0 = cyc; t->lrd = last_rdata;
          if (!t->bridge) pend.push_back(t);
        }
      }
    }
    auto drive = [&](int m, uint8_t &v, uint8_t &we, uint8_t &dma, uint8_t &sz, uint32_t &a, uint32_t &wd) {
      Txn *t = cur[m];
      v = t != nullptr; we = t ? t->we : 0; dma = t ? t->dma : 0; sz = t ? t->size : 0;
      a = t ? t->addr : 0; wd = t ? t->wdata : 0;
    };
    drive(0, top->m0_v, top->m0_we, top->m0_dma, top->m0_size, top->m0_addr, top->m0_wdata);
    drive(1, top->m1_v, top->m1_we, top->m1_dma, top->m1_size, top->m1_addr, top->m1_wdata);
    top->last_rdata = last_rdata;
    if (booted && (r_pin || r_por || r_man || r_cpu)) had_reset = true;
    top->rst_pin = r_pin; top->rst_por = r_por || r_pin; top->rst_man = r_man; top->rst_cpu = r_cpu || r_man || r_por || r_pin;
    top->bce = bce; top->pce = pce;
    top->clk = 0;
    top->eval();
    ss.answer(top, cyc, bce, pce);
    top->eval();

    // strobes (stalled or not) -> the pending transaction they present
    for (int p = 0; p < PP_N; p++) {
      if (!ss.st[p].strobed) continue;
      const uint32_t *f = ss.st[p].cur;
      auto it = std::find_if(pend.begin(), pend.end(), [&](Txn *t) {
        return t->port == p && t->blk == (int)f[3] && t->off == f[4] && t->size == (int)f[2] && t->we == (f[1] != 0) &&
               t->wdata == f[5] && t->dma == (f[6] != 0);
      });
      if (it != pend.end()) { if ((*it)->nstb++ == 0) (*it)->tstb = cyc; }
    }

    // the Pclk path's address strobe (SOC.md 9.19): the pending Pclk transaction it starts, in presentation order
    if (top->pa_v) {
      auto it = std::find_if(pend.begin(), pend.end(), [&](Txn *t) {
        return t->cls == P4K_PCLK && t->tas < 0 && t->blk == (int)top->pa_blk && t->off == top->pa_off &&
               t->size == (int)top->pa_size && t->we == (top->pa_wr != 0) && t->wdata == top->pa_wdata &&
               t->dma == (top->pa_dma != 0);
      });
      TB_CHECK(it != pend.end(), "cyc %ld: a Pclk address strobe (%s off %07x) matches no pending transaction",
               (long)cyc, blk_name((int)top->pa_blk), top->pa_off);
      if (it != pend.end()) (*it)->tas = cyc;
    }

    // captures -> the pending transaction they perform
    bool arr_cap = false;
    for (auto &c : ss.cyc_caps) {
      if (is_array(c.blk)) arr_cap = true;
      auto it = std::find_if(pend.begin(), pend.end(), [&](Txn *t) {
        return t->port == c.port && t->blk == c.blk && t->off == c.off && t->size == c.size && t->we == c.we &&
               t->wdata == c.wdata && t->dma == c.dma;
      });
      if (it == pend.end()) {
        TB_CHECK(false, "cyc %ld: capture on port %d (%s off %07x) matches no pending transaction", (long)cyc, c.port,
                 blk_name(c.blk), c.off);
        continue;
      }
      Txn *t = *it;
      pend.erase(it);
      t->tcap = cyc; t->capval = c.rdata; t->ncap++;
      if (t->cls == P4K_PCLK && t->we) {                                 // a landing (SOC.md 9.19)
        if (hold_on_land > 0) { hold_left = hold_on_land; hold_on_land = 0; }
        else if (rand_land_hold_pct && rnd(100) < (uint64_t)rand_land_hold_pct) hold_left = 1 + (int)rnd(rand_hold_max);
      }
      if (t->cls != P4K_PCLK) t->tas = cyc;      // (Iclk / Bclk: the slave sees the address strobe itself)
      TB_CHECK(t->tas >= 0, "cyc %ld: a Pclk capture (%s off %07x) before its address strobe", (long)cyc,
               blk_name(t->blk), t->off);
    }
    TB_CHECK(top->arr_strobe == arr_cap, "cyc %ld: arr_strobe %d, array capture %d", (long)cyc, top->arr_strobe, arr_cap);
    arr_pulses += top->arr_strobe;

    // acks
    for (int m = 0; m < 2; m++) {
      bool ack = m ? top->m1_ack : top->m0_ack;
      uint32_t rd = m ? top->m1_rdata : top->m0_rdata;
      bool err = m ? top->m1_err : top->m0_err;
      if (!ack) { TB_CHECK(!err, "cyc %ld m%d: err without ack", (long)cyc, m); continue; }
      Txn *t = cur[m];
      TB_CHECK(t != nullptr, "cyc %ld m%d: ack with no request", (long)cyc, m);
      if (!t) continue;
      t->tack = cyc; t->rdata = rd; t->err = err; t->nack++;
      // the DMAC consumes the ack at its next bce and presents its next request in the Icyc after that bce (v1_dmac):
      // not before ack + 2 (and only in a cycle after a bce, above)
      cur[m] = nullptr; free_at[m] = cyc + (m ? 2 : 1);
      bool exp_err = m == 1 && (t->blk == P4B_NONE || t->blk == P4B_UBC_NODRV);
      TB_CHECK(err == exp_err, "m%d %08x: err %d expected %d", m, t->addr, err, exp_err);
      if (t->bridge) {
        uint32_t e = (!t->we && t->blk == P4B_UBC_NODRV && !t->dma) ? t->lrd : 0;
        TB_CHECK(rd == e, "m%d read %08x (%s): %08x expected %08x", m, t->addr, blk_name(t->blk), rd, e);
      } else if (!t->we) {
        TB_CHECK(t->ncap == 1, "m%d read %08x acked with %d captures", m, t->addr, t->ncap);
        TB_CHECK(rd == t->capval, "m%d read %08x: %08x, the slave answered %08x", m, t->addr, rd, t->capval);
      } else if (!posted(t->dma, t->we, t->blk)) {
        // not posted (an Iclk write; every DMAC write, SOC.md 3.1.1): acked in the slave's capture cycle
        TB_CHECK(t->ncap == 1 && t->tcap == cyc, "m%d non-posted write %08x (%s): acked in cycle %ld, %d captures (the "
                 "last in cycle %ld); the ack must be the capture cycle", m, t->addr, blk_name(t->blk), (long)cyc,
                 t->ncap, (long)t->tcap);
      } else {
        // posted: it enters the slot from its first cycle, so it may already have been strobed (a pce / bce in its
        // MA2); its ack is still no earlier than its third cycle
        TB_CHECK(t->ncap <= 1 && cyc - t->t0 >= 2, "m%d posted write %08x: %d captures, acked after %ld cycles", m,
                 t->addr, t->ncap, (long)(cyc - t->t0 + 1));
      }
    }
    bpost_trace.push_back({cyc, (bool)top->bpost_bsc});
    wpend_trace.push_back({cyc, (bool)top->pclk_wpend});
    ss.commit(cyc);
    top->clk = 1;
    top->eval();
    if (held) { hold_left--; poff++; }
    else if (hold_left == 0 && rand_hold_pct && rnd(1000) < (uint64_t)rand_hold_pct * 10) hold_left = 1 + (int)rnd(rand_hold_max);
    if (hold_left > 0 && !held) { had_hold = true; nhold++; }
    cyc++;
  }
  uint64_t rnd(uint64_t n) { hrng ^= hrng << 13; hrng ^= hrng >> 7; hrng ^= hrng << 17; return hrng % n; }
  // the n-th pce strictly after cycle c (-1: none yet)
  int64_t pce_after(int64_t c, int n) const {
    auto it = std::upper_bound(pces.begin(), pces.end(), c);
    for (int k = 1; k < n && it != pces.end(); k++) ++it;
    return it == pces.end() ? -1 : *it;
  }

  void idle(int n) { for (int i = 0; i < n; i++) step(); }
  bool run(int64_t limit = 20000) {
    int64_t end = cyc + limit;
    while (busy() && cyc < end) step();
    TB_CHECK(!busy(), "run did not finish (cyc %ld)", (long)cyc);
    idle(12);                          // let posted entries drain
    return !busy();
  }
  // wait until cyc % 4 == ph
  void align(int ph) { while ((cyc & 3) != ph) step(); }

  void final_checks() {
    for (auto &u : all) {
      Txn *t = u.get();
      if (t->abandon) continue;
      TB_CHECK(t->nack == 1, "txn m%d %s %08x: %d acks", t->m, t->we ? "wr" : "rd", t->addr, t->nack);
      TB_CHECK(t->ncap == (t->bridge ? 0 : 1), "txn m%d %s %08x: %d captures", t->m, t->we ? "wr" : "rd", t->addr,
               t->ncap);
    }
    land_checks();
    if (had_hold) return;                // (the slot model below counts regular path clocks)
    // The posted paths' slot, modelled from the observed address strobes and acks (a bench without resets): each path
    // serves its transactions in strobe order; a transaction enters the slot in the first cycle it is presented with
    // the slot free; the slot frees at a read's or a non-posted (DMAC) write's ack + 1, or at a posted (CPU) write's
    // strobe + 5 (Bclk) / + 7 (Pclk): the path clock edge after the strobe + 3.  Checked: a posted write is acked at
    // max(its MA3, its acceptance), and bpost_bsc is 1 exactly while the Bclk slot holds a posted BSC / SDMR write (from
    // its acceptance to its release; a DMAC write to the BSC never raises it).
    if (had_reset) return;
    slot_checks();
  }

  // SOC.md 9.19, the landing rule of the Pclk path (every bench, resets included): a write meets its slave (its
  // capture) at the end of its bus cycle, its transfers + 1 Pcyc after its address strobe: the 2nd pce after it for a
  // byte / word (8 Icyc with regular enables), the 3rd for a longword (12); a read meets its slave at its address
  // strobe, or at the next pce when a write lands in that cycle (the write first: the read returns the new value).
  // A deferred read's ack is max(its return schedule, the cycle after that capture): with regular enables the
  // schedule (a word 11 after the strobe; a longword 11 after its second transfer, which is that pce) always wins;
  // behind a withheld pce (v1_cpg's settle) the ack waits for the capture (M1a-verify F2)
  void land_checks() {
    {
      std::set<int64_t> lands;
      for (auto &u : all) if (u->cls == P4K_PCLK && u->we && u->tcap >= 0) lands.insert(u->tcap);
      for (auto &u : all) {
        Txn *t = u.get();
        if (t->cls != P4K_PCLK || t->tcap < 0) continue;
        const bool def = !t->we && lands.count(t->tas);
        int64_t e = t->we ? pce_after(t->tas, t->size == 2 ? 3 : 2) : def ? pce_after(t->tas, 1) : t->tas;
        TB_CHECK(t->tcap == e, "m%d Pclk %s %08x: address strobe %ld, slave capture %ld, expected %ld (SOC.md 9.19)",
                 t->m, t->we ? "write" : "read", t->addr, (long)t->tas, (long)t->tcap, (long)e);
        if (def && t->tack >= 0) {
          const int64_t sched = t->size == 2 ? t->tcap + 11 : t->tas + 11;
          const int64_t ea = std::max(sched, t->tcap + 1);
          TB_CHECK(t->tack == ea, "m%d deferred Pclk read %08x: strobe %ld, capture %ld, ack %ld, expected %ld (max(the "
                   "return schedule, capture + 1): M1a-verify F2)", t->m, t->addr, (long)t->tas, (long)t->tcap,
                   (long)t->tack, (long)ea);
          nlate += t->tcap + 1 > sched;
        }
        nland += t->we;
        ndefer += def;
      }
    }
  }

  void slot_checks() {
    std::vector<std::pair<int64_t, int64_t>> iv;
    std::vector<std::pair<int64_t, int64_t>> wv;         // Pclk writes: from the cycle after acceptance to the landing
    for (int cls = P4K_BCLK; cls <= P4K_PCLK; cls++) {
      std::vector<Txn *> v;
      for (auto &u : all) if (u->cls == cls && u->tas >= 0 && u->tcap >= 0 && !u->abandon) v.push_back(u.get());
      std::sort(v.begin(), v.end(), [](Txn *a, Txn *b) { return a->tas < b->tas; });
      int64_t free = 0;
      for (Txn *t : v) {
        int64_t acc = std::max(t->t0, free);
        bool pw = posted(t->dma, t->we, t->blk);
        if (pw) {
          TB_CHECK(t->tack == std::max(t->t0 + 2, acc), "m%d posted write %08x: presented %ld, accepted %ld, acked "
                   "%ld (expected max(MA3, acceptance))", t->m, t->addr, (long)t->t0, (long)acc, (long)t->tack);
          if (t->blk == P4B_BSC || t->blk == P4B_SDMR2 || t->blk == P4B_SDMR3) iv.push_back({acc, t->tcap + 4});
        }
        if (cls == P4K_PCLK && t->we) wv.push_back({acc + 1, t->tcap});
        free = pw ? t->tas + (cls == P4K_BCLK ? 5 : 7) : t->tack + 1;
      }
    }
    for (auto &b : bpost_trace) {
      bool e = false;
      for (auto &i : iv) if (b.first >= i.first && b.first <= i.second) e = true;
      TB_CHECK(b.second == e, "cyc %ld: bpost_bsc %d expected %d", (long)b.first, b.second, e);
    }
    nbpost += (long)iv.size();
    // pclk_wpend (-> v1_cpg): 1 exactly while a Pclk write is in flight, from the cycle after the path accepts it to
    // its landing (SOC.md 2.5: a sleep request waits for it)
    for (auto &b : wpend_trace) {
      bool e = false;
      for (auto &i : wv) if (b.first >= i.first && b.first <= i.second) e = true;
      TB_CHECK(b.second == e, "cyc %ld: pclk_wpend %d expected %d", (long)b.first, b.second, e);
    }
    nwpend += (long)wv.size();
  }
};

long Bench::nbpost = 0;
long Bench::nwpend = 0;
long Bench::nland = 0;
long Bench::ndefer = 0;
long Bench::nhold = 0;
long Bench::nlate = 0;
static Vv1_p4bus_tb_top *g_top;
static std::unique_ptr<Bench> newbench() {
  std::unique_ptr<Bench> b(new Bench(g_top));
  // power-on reset for 8 cycles
  b->r_pin = true; b->idle(8); b->r_pin = false; b->idle(4);
  b->booted = true;
  return b;
}

// ------------------------------------------------------------------------------------------------------------------
// decode
// ------------------------------------------------------------------------------------------------------------------
static void t_decode() {
  struct D { uint32_t a; int blk; };
  const D tab[] = {
    {0xFF000000, P4B_CCN_MMU}, {0xFF000004, P4B_CCN_MMU}, {0xFF000008, P4B_CCN_MMU}, {0xFF00000C, P4B_CCN_MMU},
    {0xFF000010, P4B_CCN_MMU}, {0xFF000034, P4B_CCN_MMU}, {0xFF000014, P4B_CCN_UBC}, {0xFF000018, P4B_CCN_UBC},
    {0xFF00001C, P4B_CCN_CCR}, {0xFF000038, P4B_CCN_CCR}, {0xFF00003C, P4B_CCN_CCR}, {0xFF000020, P4B_CCN_EXC},
    {0xFF000024, P4B_CCN_EXC}, {0xFF000028, P4B_CCN_EXC}, {0xFF000084, P4B_CCN_PMCR}, {0xFF000088, P4B_CCN_PMCR},
    {0xFF000030, P4B_CCN_ID}, {0xFF00002C, P4B_CCN_ID}, {0xFF000040, P4B_CCN_ID}, {0xFF000044, P4B_CCN_ID},
    {0xFF0000FC, P4B_CCN_ID}, {0xFF000130, P4B_CCN_ID}, {0xFF020030, P4B_CCN_ID}, {0xFF000188, P4B_CCN_PMCR},
    {0xFF03FF24, P4B_CCN_EXC}, {0xFF100004, P4B_PMCTR}, {0xFF100008, P4B_PMCTR}, {0xFF100010, P4B_PMCTR},
    {0xFF100014, P4B_PMCTR}, {0xFF100000, P4B_PMCTR}, {0xFF120008, P4B_PMCTR}, {0xFF200000, P4B_UBC},
    {0xFF200020, P4B_UBC}, {0xFF2000FC, P4B_UBC}, {0xFF200100, P4B_UBC_NODRV}, {0xFF200200, P4B_UBC_NODRV},
    {0xFF23FFFC, P4B_UBC_NODRV}, {0xFF040030, P4B_NONE}, {0xFF140008, P4B_NONE}, {0xFF240000, P4B_NONE},
    {0xFF300000, P4B_NONE}, {0xFF700000, P4B_NONE}, {0xFFB00000, P4B_NONE}, {0xFFF80000, P4B_NONE},
    {0xFFC40000, P4B_NONE}, {0xFF800000, P4B_BSC}, {0xFF800048, P4B_BSC}, {0xFF800050, P4B_BSC},
    {0xFF900000, P4B_SDMR2}, {0xFF90FFFC, P4B_SDMR2}, {0xFF940190, P4B_SDMR3}, {0xFFA00000, P4B_DMAC},
    {0xFFA00040, P4B_DMAC}, {0xFFC00000, P4B_CPG}, {0xFFC00004, P4B_CPG}, {0xFFC00010, P4B_CPG},
    {0xFFC00008, P4B_WDT}, {0xFFC0000C, P4B_WDT}, {0xFFC00040, P4B_CPG}, {0xFFC08008, P4B_CPG},
    {0xFFC80000, P4B_RTC}, {0xFFC8003C, P4B_RTC}, {0xFFD00000, P4B_INTC}, {0xFFD0000C, P4B_INTC},
    {0xFFD80000, P4B_TMU}, {0xFFD8002C, P4B_TMU}, {0xFFE00000, P4B_SCI}, {0xFFE80014, P4B_SCIF},
    {0xFFF00000, P4B_HUDI}, {0xFFF00008, P4B_HUDI}, {0xF0000000, P4B_ICAA}, {0xF1000000, P4B_ICDA},
    {0xF2000000, P4B_ITLBAA}, {0xF3000000, P4B_ITLBDA}, {0xF3800000, P4B_ITLBDA}, {0xF4000000, P4B_OCAA},
    {0xF5000000, P4B_OCDA}, {0xF6000000, P4B_UTLBAA}, {0xF7000000, P4B_UTLBDA}, {0xF7FFFFFC, P4B_UTLBDA},
    {0xE4000000, P4B_RSV}, {0xEFFFFFFC, P4B_RSV}, {0xF8000000, P4B_RSV}, {0xFE000000, P4B_RSV},
    {0xFEFFFFFC, P4B_RSV},
  };
  auto b = newbench();
  int n = 0;
  std::set<int> blks, ports;
  for (auto &d : tab) {
    TB_CHECK(decode(d.a, false) == d.blk, "p4bus_ref decode %08x: %s expected %s", d.a,
             blk_name(decode(d.a, false)), blk_name(d.blk));
    Txn *t = b->mk(0, false, d.a, 2, 0, 1);
    TB_CHECK(t->blk == d.blk, "decode %08x", d.a);
    blks.insert(d.blk);
    if (!t->bridge) ports.insert(t->port);
    n++;
  }
  b->last_rdata = 0x8CFFFF38;          // a KOS stack word, as ubcbrk A read it
  b->run();
  // the undriven window returns last_rdata, every other bridge block 0 (checked at the acks); check the strobes' off
  for (auto &u : b->all) {
    Txn *t = u.get();
    if (!t->bridge) TB_CHECK(t->ncap == 1 && t->tcap >= t->t0, "decode %08x: no capture", t->addr);
  }
  TB_CHECK(blks.size() == (size_t)P4B_N, "decode table covers %zu of %d blocks", blks.size(), (int)P4B_N);
  TB_CHECK(ports.size() == (size_t)PP_N, "decode table reaches %zu of %d ports", ports.size(), (int)PP_N);
  // DMAC addressing: physical 0x1Fxxxxxx decodes as 0xFF000000 | a[23:0]
  auto b2 = newbench();
  const uint32_t dm[] = {0x1F000030, 0x1FA00040, 0x1FE80010, 0x1F100008, 0x1F800000, 0x1FD80024};
  for (uint32_t a : dm) {
    Txn *t = b2->mk(1, false, a, a == 0x1FE80010 ? 1 : 2, 0, 1);
    TB_CHECK(t->blk == decode(0xFF000000u | (a & 0xFFFFFF), false), "dma decode %08x", a);
  }
  b2->run();
  for (auto &u : b2->all) TB_CHECK(u->ncap == 1 && u->capval == b2->ss.peek(u->blk, u->off) || u->blk == P4B_PMCTR ||
                                   u->blk == P4B_TMU, "dma read %08x", u->addr);
  b->final_checks(); b2->final_checks();
  printf("decode: %d addresses, %zu blocks, %zu ports, bridge answers and DMAC 0x1F addressing checked\n", n,
         blks.size(), ports.size());
}

// ------------------------------------------------------------------------------------------------------------------
// timing: the SOC.md 3.1.5 table
// ------------------------------------------------------------------------------------------------------------------
struct Acc { const char *name; bool we; uint32_t addr; int size; };

// isolated: one access with MA1 at Pclk phase ph (all paths idle); returns the MA occupancy
static int iso(const Acc &a, int ph, int64_t *cap_rel = nullptr) {
  auto b = newbench();
  b->idle(20);
  b->align(ph);
  Txn *t = b->mk(0, a.we, a.addr, a.size, 0x5A5A, 0, b->cyc);
  b->run();
  b->final_checks();
  if (cap_rel) *cap_rel = t->tcap - t->t0;
  return t->occ();
}

// back to back: n accesses, each presented the cycle after the previous ack; returns the steady per-access period
static int b2b(const Acc &a, int ph, int n = 10, int gap = 0, int *occ_last = nullptr) {
  auto b = newbench();
  b->idle(20);
  b->align(ph);
  std::vector<Txn *> v;
  for (int i = 0; i < n; i++) v.push_back(b->mk(0, a.we, a.addr, a.size, 0x1234 + i, i ? gap : 0, i ? -1 : b->cyc));
  b->run();
  b->final_checks();
  int p = (int)(v[n - 1]->t0 - v[n - 2]->t0);
  for (int i = n / 2; i < n - 1; i++)
    TB_CHECK(v[i + 1]->t0 - v[i]->t0 == p, "%s back to back not periodic: %ld vs %d", a.name,
             (long)(v[i + 1]->t0 - v[i]->t0), p);
  if (occ_last) *occ_last = v[n - 1]->occ();
  return p;
}

static std::string setstr(const std::set<int> &s) {
  std::string r = "{";
  for (int x : s) { if (r.size() > 1) r += ","; r += std::to_string(x); }
  return r + "}";
}

static void t_timing() {
  struct Row { Acc a; int b2b; std::set<int> iso; std::set<int> must; };
  const Row rows[] = {
    // Iclk registers: 3 / 3 (bsc-memmap 6.4 CCR / PMCTR / BARA; App. A.4 BARA write)
    {{"CCR rd", false, 0xFF00001C, 2}, 3, {3}, {3}},
    {{"EXPEVT rd", false, 0xFF000024, 2}, 3, {3}, {3}},
    {{"PMCTR rd", false, 0xFF100008, 2}, 3, {3}, {3}},
    {{"BARA rd", false, 0xFF200000, 2}, 3, {3}, {3}},
    {{"BARA wr", true, 0xFF200000, 2}, 3, {3}, {3}},
    {{"PMCR1 wr", true, 0xFF000084, 1}, 3, {3}, {3}},
    {{"PVR rd (CCN id)", false, 0xFF000030, 2}, 3, {3}, {3}},
    {{"hole rd (NONE)", false, 0xFF300000, 2}, 3, {3}, {3}},
    {{"UBC undriven rd", false, 0xFF200100, 2}, 3, {3}, {3}},
    {{"RSV rd", false, 0xE4000000, 2}, 3, {3}, {3}},
    // arrays: 4 (App. A.5: one stall of the synchronous array RAM); writes [P] the same
    {{"OCAA rd", false, 0xF4000000, 2}, 4, {4}, {4}},
    {{"OCDA rd", false, 0xF5000000, 2}, 4, {4}, {4}},
    {{"UTLBAA rd", false, 0xF6000000, 2}, 4, {4}, {4}},
    {{"ITLBAA rd", false, 0xF2000000, 2}, 4, {4}, {4}},
    {{"ICAA rd", false, 0xF0000000, 2}, 4, {4}, {4}},
    {{"UTLBDA wr", true, 0xF7000000, 2}, 4, {4}, {4}},
    // PTEH write: 4 (core-integration 6.1)
    {{"PTEH wr", true, 0xFF000000, 2}, 4, {4}, {4}},
    // Bclk read 8 / 8 (BCR1 PDTRA RTCNT DMAOR SAR1)
    {{"BCR1 rd", false, 0xFF800000, 2}, 8, {8, 9}, {8}},
    {{"PDTRA rd", false, 0xFF800030, 1}, 8, {8, 9}, {8}},
    {{"RTCNT rd", false, 0xFF800020, 1}, 8, {8, 9}, {8}},
    {{"DMAOR rd", false, 0xFFA00040, 2}, 8, {8, 9}, {8}},
    {{"SAR1 rd", false, 0xFFA00010, 2}, 8, {8, 9}, {8}},
    {{"SDMR3 rd (0)", false, 0xFF940190, 0}, 8, {8, 9}, {8}},
    // Bclk write 6 (drain) / 3 (posted)
    {{"RTCOR wr", true, 0xFF800024, 1}, 6, {3}, {3}},
    {{"SAR1 wr", true, 0xFFA00010, 2}, 6, {3}, {3}},
    // Pclk read 8 / 16 bit: 16 / 13-14 (bsc-memmap 6.4, timers-clock 3.0: 14)
    {{"FRQCR rd", false, 0xFFC00000, 1}, 16, {13, 14, 15, 16}, {13, 14}},
    {{"IPRA rd", false, 0xFFD00004, 1}, 16, {13, 14, 15, 16}, {13, 14}},
    {{"TSTR rd", false, 0xFFD80004, 0}, 16, {13, 14, 15, 16}, {13, 14}},
    {{"SCFSR2 rd", false, 0xFFE80010, 1}, 16, {13, 14, 15, 16}, {13, 14}},
    {{"R64CNT rd", false, 0xFFC80000, 0}, 16, {13, 14, 15, 16}, {13, 14}},
    {{"SDIR rd", false, 0xFFF00000, 1}, 16, {13, 14, 15, 16}, {13, 14}},
    // Pclk read 32 bit: 20 / 18 (TCNT TCOR TCPR2)
    {{"TCNT0 rd", false, 0xFFD8000C, 2}, 20, {17, 18, 19, 20}, {18}},
    {{"TCOR0 rd", false, 0xFFD80008, 2}, 20, {17, 18, 19, 20}, {18}},
    {{"TCPR2 rd", false, 0xFFD8002C, 2}, 20, {17, 18, 19, 20}, {18}},
    // Pclk write 16 / 32: 8 / 12 back to back, 3 / 3 isolated (IPRB / TCOR1)
    {{"IPRB wr", true, 0xFFD00008, 1}, 8, {3}, {3}},
    {{"TCR1 wr", true, 0xFFD8001C, 1}, 8, {3}, {3}},
    {{"STBCR wr", true, 0xFFC00004, 0}, 8, {3}, {3}},
    {{"TCOR1 wr", true, 0xFFD80014, 2}, 12, {3}, {3}},
    {{"SCIF SCFTDR2 wr", true, 0xFFE8000C, 0}, 8, {3}, {3}},
  };
  int nrow = 0;
  for (auto &r : rows) {
    std::set<int> got_iso, got_b2b;
    for (int ph = 0; ph < 4; ph++) {
      got_iso.insert(iso(r.a, ph));
      got_b2b.insert(b2b(r.a, ph));
    }
    TB_CHECK(got_b2b.size() == 1 && *got_b2b.begin() == r.b2b, "%s: back to back %s, measured %d", r.a.name,
             setstr(got_b2b).c_str(), r.b2b);
    TB_CHECK(got_iso == r.iso, "%s: isolated by phase %s, expected %s", r.a.name, setstr(got_iso).c_str(),
             setstr(r.iso).c_str());
    for (int x : r.must)
      TB_CHECK(got_iso.count(x), "%s: the measured isolated cost %d is not reachable (%s)", r.a.name, x,
               setstr(got_iso).c_str());
    if (g_verbose) printf("  %-18s b2b %s isolated %s\n", r.a.name, setstr(got_b2b).c_str(), setstr(got_iso).c_str());
    nrow++;
  }

  // Pclk 16-bit write then a 16-bit read of another module (timers-clock T: 23): W's MA1 to R's ack
  std::set<int> wr_rd;
  for (int ph = 0; ph < 4; ph++) {
    auto b = newbench();
    b->idle(20); b->align(ph);
    Txn *w = b->mk(0, true, 0xFFD8001C, 1, 0, 0, b->cyc);                // TCR1
    Txn *r = b->mk(0, false, 0xFFD00004, 1, 0, 0);                       // IPRA
    b->run(); b->final_checks();
    wr_rd.insert((int)(r->tack - w->t0 + 1));
    TB_CHECK(r->tcap > w->tcap, "write -> read: the read was strobed before the write drained");
  }
  // 21..24 by phase (probe w16_r16o: B - A 24..27 = 3 + this); timers-clock T measured 23 at its phase
  TB_CHECK(wr_rd == std::set<int>({21, 22, 23, 24}) && wr_rd.count(23),
           "Pclk wr16 -> rd16 other module: %s, measured 21..24 by phase (T: 23)", setstr(wr_rd).c_str());

  // the TCNT sample point: the strobe (the value's sample) 3..6 Icyc after the load issues (issue = MA1 - 2)
  std::set<int> samp;
  for (int ph = 0; ph < 4; ph++) {
    int64_t rel = 0;
    iso({"TCNT1", false, 0xFFD80018, 2}, ph, &rel);
    samp.insert((int)rel + 2);
  }
  TB_CHECK(*samp.begin() >= 3 && *samp.rbegin() <= 6, "TCNT sample point %s Icyc after issue, about 1 Pcyc",
           setstr(samp).c_str());

  // App. A.4: A (PMCTR) ; W (IPRA / BARA write) ; B (PMCTR): B - A = 6, and B does not wait for the Pclk drain
  for (uint32_t wa : {0xFFD00004u, 0xFF200000u}) {
    for (int ph = 0; ph < 4; ph++) {
      auto b = newbench();
      b->idle(20); b->align(ph);
      Txn *A = b->mk(0, false, 0xFF100008, 2, 0, 0, b->cyc);
      Txn *W = b->mk(0, true, wa, wa == 0xFF200000u ? 2 : 1, 0xF0F0, 0);
      Txn *Bt = b->mk(0, false, 0xFF100008, 2, 0, 0);
      Txn *C = b->mk(0, false, 0xFF100008, 2, 0, 0);
      b->run(); b->final_checks();
      TB_CHECK(Bt->capval - A->capval == 6 && C->capval - Bt->capval == 3 && W->occ() == 3,
               "A.4 %08x: B - A %u, C - B %u, W %d", wa, Bt->capval - A->capval, C->capval - Bt->capval, W->occ());
    }
  }

  // debug-ubc-hudi 7.3 loops (body ; dt ; bf/s ; nop = the access + 2 cycles): ldCCN 5 / 0x25 2, ldTMU 20 / 17
  struct L { const char *n; uint32_t a; int period, occ; } loops[] = {{"ldCCN (PVR)", 0xFF000030, 5, 3},
                                                                     {"ldTMU (TCOR2)", 0xFFD80020, 20, 18}};
  for (auto &l : loops) {
    for (int ph = 0; ph < 4; ph++) {
      int occ = 0;
      int p = b2b({l.n, false, l.a, 2}, ph, 16, 2, &occ);
      TB_CHECK(p == l.period && occ == l.occ, "%s loop: period %d occupancy %d (event 0x25 %d), measured %d / %d / %d",
               l.n, p, occ, occ - 1, l.period, l.occ, l.occ - 1);
    }
  }
  // core-integration 6.1 run 1: 8 accesses back to back + the loop's dt / bf (2 cycles) per iteration.  Whether the
  // loop tail hides in the synchroniser's slack: Pclk 128 / 160 need D1 + D2 <= 13 (Bclk 66 fits D1 + D2 = 6 or 7).
  struct L8 { Acc a; int period; } l8[] = {
    {{"EXPEVT x8", false, 0xFF000024, 2}, 26}, {{"BCR1 x8", false, 0xFF800000, 2}, 66},
    {{"SAR1 x8", false, 0xFFA00010, 2}, 66},   {{"FRQCR x8", false, 0xFFC00000, 1}, 128},
    {{"IPRA x8", false, 0xFFD00004, 1}, 128},  {{"SCFSR2 x8", false, 0xFFE80010, 1}, 128},
    {{"TCNT0 x8", false, 0xFFD8000C, 2}, 160}, {{"PTEH wr x8", true, 0xFF000000, 2}, 34},
    {{"BARA wr x8", true, 0xFF200000, 2}, 26}, {{"IPRA wr x8", true, 0xFFD00004, 1}, 64},
    {{"TCOR1 wr x8", true, 0xFFD80014, 2}, 96}};
  for (auto &l : l8) {
    std::set<int> per;
    for (int ph = 0; ph < 4; ph++) {
      auto b = newbench();
      b->idle(20); b->align(ph);
      std::vector<Txn *> v;
      for (int i = 0; i < 8 * 6; i++)
        v.push_back(b->mk(0, l.a.we, l.a.addr, l.a.size, 0x10 + i, i == 0 ? 0 : (i % 8 == 0 ? 2 : 0), i ? -1 : b->cyc));
      b->run(); b->final_checks();
      per.insert((int)(v[40]->t0 - v[32]->t0));
      per.insert((int)(v[32]->t0 - v[24]->t0));
    }
    TB_CHECK(per.size() == 1 && *per.begin() == l.period, "%s + loop: %s cycles per iteration, measured %d", l.a.name,
             setstr(per).c_str(), l.period);
  }
  printf("timing: %d access rows x 4 phases (isolated, back to back), wr16->rd16 %s, TCNT sample %s, App. A.4, "
         "ldCCN / ldTMU loops, %zu core-integration 6.1 loops (8 per iteration)\n", nrow, setstr(wr_rd).c_str(),
         setstr(samp).c_str(), sizeof(l8) / sizeof(l8[0]));
}

// ------------------------------------------------------------------------------------------------------------------
// posting, ordering, bpost_bsc
// ------------------------------------------------------------------------------------------------------------------
static void t_post() {
  auto b = newbench();
  // same path: a read after a posted write sees the written value (STBCR, IPRA, a 32-bit TCOR)
  Txn *w1 = b->mk(0, true, 0xFFC00004, 0, 0x0F, 0);
  Txn *r1 = b->mk(0, false, 0xFFC00004, 0, 0, 0);
  Txn *w2 = b->mk(0, true, 0xFFD00004, 1, 0x1234, 3);
  Txn *r2 = b->mk(0, false, 0xFFD00004, 1, 0, 0);
  Txn *w3 = b->mk(0, true, 0xFFD80014, 2, 0xCAFEF00D, 0);
  Txn *r3 = b->mk(0, false, 0xFFD80014, 2, 0, 0);
  // a Bclk posted write, then a Pclk read and an Iclk read: neither waits for the Bclk drain
  Txn *w4 = b->mk(0, true, 0xFF800000, 2, 0xA3020008, 5);
  Txn *r4 = b->mk(0, false, 0xFFD00008, 1, 0, 0);
  Txn *w5 = b->mk(0, true, 0xFFA00010, 2, 0x0C001000, 5);
  Txn *r5 = b->mk(0, false, 0xFF000024, 2, 0, 0);
  // SDMR3 MRS byte write, then a BSC read that must wait for it (same path)
  Txn *w6 = b->mk(0, true, 0xFF940190, 0, 0, 5);
  Txn *r6 = b->mk(0, false, 0xFF800014, 2, 0, 0);
  // wrong size: a byte write to IPRA is ignored, a 32-bit read of IPRA reads 0 (the slave's rule, through the bridge)
  Txn *w7 = b->mk(0, true, 0xFFD00004, 0, 0x55, 5);
  Txn *r7 = b->mk(0, false, 0xFFD00004, 1, 0, 0);
  Txn *r8 = b->mk(0, false, 0xFFD00004, 2, 0, 0);
  b->run(); b->final_checks();
  TB_CHECK(r1->rdata == 0x0F && r1->tcap > w1->tcap, "STBCR: read %02x after a posted 0F", r1->rdata);
  TB_CHECK(r2->rdata == 0x1234 && r2->tcap > w2->tcap, "IPRA: read %04x after a posted 1234", r2->rdata);
  TB_CHECK(r3->rdata == 0xCAFEF00D && r3->tcap > w3->tcap, "TCOR1: read %08x after a posted CAFEF00D", r3->rdata);
  TB_CHECK(w4->occ() == 3 && r4->occ() <= 16 && r4->tcap < w4->tcap + 8, "Bclk write then Pclk read: %d / %d",
           w4->occ(), r4->occ());
  TB_CHECK(w5->occ() == 3 && r5->occ() == 3, "Bclk write then Iclk read: %d / %d", w5->occ(), r5->occ());
  TB_CHECK(r6->tcap > w6->tcap && w6->ncap == 1, "SDMR3 write then BCR read: ordering");
  TB_CHECK(r7->rdata == 0x1234 && r8->rdata == 0, "wrong size: IPRA %04x (32-bit view %08x)", r7->rdata, r8->rdata);
  (void)w7;
  // bpost_bsc for the two BSC / SDMR writes, not for the DMAC-register write (checked in final_checks)
  long nbsc = 0;
  for (auto &u : b->all) nbsc += u->we && (u->blk == P4B_BSC || u->blk == P4B_SDMR2 || u->blk == P4B_SDMR3);
  TB_CHECK(nbsc == 2, "bpost: %ld BSC / SDMR writes", nbsc);
  long hi = 0;
  for (auto &p : b->bpost_trace) hi += p.second;
  TB_CHECK(hi > 0, "bpost_bsc never rose");
  // F7 of the WI-P4 verification: bpost_bsc is 1 from the write's acceptance cycle (MA1 on an idle path), before
  // the BSC could start a bus cycle for a later request; every bce / non-bce phase
  for (int ph = 0; ph < 4; ph++) {
    auto c = newbench();
    c->idle(20); c->align(ph);
    Txn *w = c->mk(0, true, 0xFF800014, 2, 0xC00A0E24, 0, c->cyc);        // MCR
    c->step();
    TB_CHECK(c->bpost_trace.back().second, "bpost_bsc low in the MCR write's acceptance cycle (phase %d)", ph);
    c->run(); c->final_checks();
    (void)w;
  }
  printf("post: ordering on each path, other paths do not wait, size rule, bpost_bsc (%ld cycles high; from the "
         "acceptance cycle)\n", hi);
}

// ------------------------------------------------------------------------------------------------------------------
// stalls
// ------------------------------------------------------------------------------------------------------------------
static void t_stall() {
  long nst = 0, ntx = 0;
  const Acc accs[] = {{"CCR", false, 0xFF00001C, 2},   {"BARA wr", true, 0xFF200000, 2}, {"BCR1", false, 0xFF800000, 2},
                      {"SAR1 wr", true, 0xFFA00010, 2}, {"IPRA", false, 0xFFD00004, 1},   {"TCNT0", false, 0xFFD8000C, 2},
                      {"IPRB wr", true, 0xFFD00008, 1}};
  const int base[] = {3, 3, 8, 3, 13, 17, 3}, step[] = {1, 1, 2, 0, 4, 4, 0};
  for (int ai = 0; ai < 7; ai++) {
    for (int k = 0; k < 16; k++) {
      auto b = newbench();
      for (int p = 0; p < PP_N; p++) { b->ss.st[p].rand_stall_pct = 60; b->ss.st[p].rand_stall_max = 3; }
      b->ss.rng = Rng(1000 + 17 * ai + k);
      b->idle(20);
      b->align(2);                     // MA1 right before a pce (and a bce): the minimum isolated cost
      Txn *t = b->mk(0, accs[ai].we, accs[ai].addr, accs[ai].size, 0x77, 0, b->cyc);
      b->run(); b->final_checks();
      int stalls = 0;
      for (int p = 0; p < PP_N; p++) stalls += (int)(b->ss.st[p].strobes - b->ss.st[p].captures);
      nst += stalls; ntx++;
      if (accs[ai].we && t->cls != P4K_ICLK) {
        TB_CHECK(t->occ() == 3, "%s stalled %d: posted write occupancy %d", accs[ai].name, stalls, t->occ());
      } else {
        int e = base[ai] + stalls * step[ai];
        TB_CHECK(t->occ() == e, "%s stalled %d: occupancy %d expected %d", accs[ai].name, stalls, t->occ(), e);
      }
    }
  }
  printf("stall: %ld transactions, %ld stalls answered: identical re-presentation, one capture, +1 / +2 each (a Pclk "
         "slave never stalls, SOC.md 9.19)\n", ntx, nst);
}

// ------------------------------------------------------------------------------------------------------------------
// DMAC master
// ------------------------------------------------------------------------------------------------------------------
static void t_dmac() {
  // tie on every path: both present in the same cycle (the Icyc after a bce, when the DMAC's Bclk flop presents) ->
  // the DMAC is served first
  for (int path = 0; path < 3; path++) {
    uint32_t a0 = path == 0 ? 0xFF00001C : path == 1 ? 0xFF800000 : 0xFFD00004;
    uint32_t a1 = path == 0 ? 0x1F000024 : path == 1 ? 0x1FA00040 : 0x1FD0000C;
    int s0 = path == 2 ? 1 : 2, s1 = path == 2 ? 1 : 2;
    auto b = newbench();
    b->idle(20); b->align(0);          // the cycle after a bce
    Txn *c = b->mk(0, false, a0, s0, 0, 0, b->cyc);
    Txn *d = b->mk(1, false, a1, s1, 0, 0, b->cyc);
    b->run(); b->final_checks();
    TB_CHECK(c->t0 == d->t0, "tie: not presented together");
    TB_CHECK(d->tcap < c->tcap && c->tcap > d->tack - 1, "tie path %d: DMAC cap %ld CPU cap %ld", path, (long)d->tcap,
             (long)c->tcap);
  }
  // a true tie with a CPU posted write (a posted write is offered from its MA1): the CPU's BSC write and a DMAC read
  // on the Bclk path, both presented in the same cycle: the DMAC's read is strobed first, the CPU's write still acks
  // at its MA3 (it waits in its front end meanwhile) and is strobed after the DMAC read
  {
    auto b = newbench();
    b->idle(20); b->align(2);
    Txn *c = b->mk(0, true, 0xFF800024, 1, 0xA55E, 0, b->cyc);          // RTCOR
    Txn *d = b->mk(1, false, 0x1FA00040, 2, 0, 0, b->cyc);              // DMAOR
    b->run(); b->final_checks();
    TB_CHECK(d->tcap < c->tcap && c->nack == 1 && d->nack == 1, "Bclk tie CPU posted write / DMAC read: DMAC cap %ld, "
             "CPU cap %ld", (long)d->tcap, (long)c->tcap);
  }
  // independent paths: a DMAC Pclk read while the CPU runs 4 Iclk reads: 3 each
  {
    auto b = newbench();
    b->idle(20); b->align(1);
    Txn *d = b->mk(1, false, 0x1FD8000C, 2, 0, 0, b->cyc);
    std::vector<Txn *> c;
    for (int i = 0; i < 4; i++) c.push_back(b->mk(0, false, 0xFF000024, 2, 0, 0, i ? -1 : b->cyc));
    b->run(); b->final_checks();
    for (Txn *t : c) TB_CHECK(t->occ() == 3, "CPU Iclk read beside a DMAC Pclk read: %d", t->occ());
    TB_CHECK(d->occ() >= 17 && d->occ() <= 20, "DMAC TCNT read %d", d->occ());
  }
  // err, the dma flag, a DMAC write (not posted: acked in its capture cycle, checked in step())
  {
    auto b = newbench();
    Txn *e1 = b->mk(1, false, 0x1F040030, 2, 0, 1);                      // NONE
    Txn *e2 = b->mk(1, false, 0x1F200100, 2, 0, 1);                      // UBC_NODRV
    Txn *e3 = b->mk(1, true, 0x1F300000, 2, 0xFFFFFFFF, 1);              // NONE, write
    Txn *ok = b->mk(1, false, 0x1F000030, 2, 0, 1);                      // PVR
    Txn *w = b->mk(1, true, 0x1FE8000C, 0, 0x41, 1);                     // SCFTDR2
    Txn *r = b->mk(1, false, 0x1FE80014, 0, 0, 1);                       // SCFRDR2: one pop, dma = 1
    b->run(); b->final_checks();
    TB_CHECK(e1->err && e2->err && e3->err && !ok->err && !w->err && !r->err, "DMAC err flags");
    TB_CHECK(e2->rdata == 0, "DMAC read of the undriven UBC window: %08x (the CPU's last_rdata must not leak)", e2->rdata);
    TB_CHECK(ok->rdata == 0x040205C1, "DMAC PVR read %08x", ok->rdata);
    TB_CHECK(w->ncap == 1 && w->tack == w->tcap, "DMAC SCFTDR2 write: %d captures, acked %ld, captured %ld", w->ncap,
             (long)w->tack, (long)w->tcap);
    TB_CHECK(b->ss.st[PP_SCIF].fifo_pops == 1, "SCIF pops %lu", (unsigned long)b->ss.st[PP_SCIF].fifo_pops);
    bool dma_seen = false;
    for (auto &c : b->ss.caps) if (c.port == PP_SCIF) dma_seen |= c.dma;
    TB_CHECK(dma_seen, "the dma flag did not reach the SCIF");
  }
  printf("dmac: tie on every path (DMAC first), independent paths, err NONE / UBC_NODRV, dma flag\n");
}

// ------------------------------------------------------------------------------------------------------------------
// dmacw: a DMAC write is never posted (SOC.md 3.1.1, I085): it enters the path's slot at the end of its first cycle
// like a read, is strobed at the path's first bce / pce after that, and is acked in the slave's capture cycle (checked
// for every DMAC write in step()), so the slave has applied it before the DMAC consumes the ack at its next bce.  On
// the Pclk path the slave's capture is the write's landing, the end of its bus cycle (SOC.md 9.19): strobe + 8 (byte,
// word) / + 12 (longword), and the write holds the slot until then.
// ------------------------------------------------------------------------------------------------------------------
static void t_dmacw() {
  // isolated, at both phases of the DMAC's Bclk flop (it presents at cyc % 4 == 0 or 2): occupancy to the ack.
  // Iclk: the front end, strobe and ack in the third cycle (3); Bclk: the next bce (2); Pclk: the first pce (2 / 4),
  // then the landing 8 (byte / word) or 12 (longword) later: 10 / 12, 14 / 16
  struct W { const char *n; uint32_t a; int size; uint32_t off; int blk; std::set<int> occ; };
  const W ws[] = {
    {"BARA (Iclk)", 0x1F200000, 2, 0xF200000, P4B_UBC, {3}},
    {"SAR1 (Bclk)", 0x1FA00010, 2, 0xFA00010, P4B_DMAC, {2}},
    {"BCR1 (Bclk, BSC: no bpost_bsc)", 0x1F800000, 2, 0xF800000, P4B_BSC, {2}},
    {"SCFTDR2 (Pclk byte)", 0x1FE8000C, 0, 0xFE8000C, P4B_SCIF, {10, 12}},
    {"IPRB (Pclk word)", 0x1FD00008, 1, 0xFD00008, P4B_INTC, {10, 12}},
    {"TCOR1 (Pclk longword)", 0x1FD80014, 2, 0xFD80014, P4B_TMU, {14, 16}},
  };
  int n = 0;
  for (auto &w : ws) {
    std::set<int> occ;
    for (int ph : {0, 2}) {
      auto b = newbench();
      b->idle(20); b->align(ph);
      uint32_t v = 0x5A5A5A5Au & size_mask(w.size);
      Txn *t = b->mk(1, true, w.a, w.size, v, 0, b->cyc);
      b->run(); b->final_checks();
      TB_CHECK(t->ncap == 1 && t->tack == t->tcap && t->nack == 1, "DMAC %s write: %d captures, ack %ld, capture %ld",
               w.n, t->ncap, (long)t->tack, (long)t->tcap);
      TB_CHECK(w.blk == P4B_SCIF || b->ss.peek(w.blk, w.off) == v, "DMAC %s write: the slave holds %08x", w.n,
               b->ss.peek(w.blk, w.off));
      occ.insert(t->occ());
      n++;
    }
    TB_CHECK(occ == w.occ, "DMAC %s write: occupancy %s by phase, expected %s", w.n, setstr(occ).c_str(),
             setstr(w.occ).c_str());
  }
  // back to back on the Pclk path: a write holds the slot to its landing (the end of its bus cycle, strobe + 8 / 12,
  // SOC.md 9.19), where it is acked; the DMAC consumes the ack at its next bce and presents the next write in the Icyc
  // after it, which is strobed at the next pce: a period of 12 (word) / 16 (longword)
  for (int lw = 0; lw < 2; lw++) {
    auto b = newbench();
    b->idle(20); b->align(0);
    std::vector<Txn *> v;
    for (int i = 0; i < 8; i++)
      v.push_back(b->mk(1, true, lw ? 0x1FD80014 : 0x1FD00008, lw ? 2 : 1, 0x100 + i, 0, i ? -1 : b->cyc));
    b->run(); b->final_checks();
    std::set<int> per;
    for (int i = 3; i < 8; i++) per.insert((int)(v[i]->tcap - v[i - 1]->tcap));
    TB_CHECK(per == std::set<int>({lw ? 16 : 12}), "DMAC Pclk %s writes back to back: strobe period %s, expected %d",
             lw ? "longword" : "word", setstr(per).c_str(), lw ? 16 : 12);
  }
  // WI-DMAC verification F2: a CPU longword Pclk write in flight (posted: acked at its MA3, its entry busy until strobe
  // + 7, its bus cycle 3 Pcyc), then a DMAC write to SCFTDR2: the DMAC's ack must not come before its own strobe
  for (int ph = 0; ph < 4; ph++) {
    auto b = newbench();
    b->idle(20); b->align(ph);
    Txn *c = b->mk(0, true, 0xFFD80014, 2, 0xCAFEF00D, 0, b->cyc);                 // TCOR1
    Txn *d = b->mk(1, true, 0x1FE8000C, 0, 0x41, 0, b->cyc + 1);                    // SCFTDR2
    b->run(); b->final_checks();
    TB_CHECK(c->tack < d->tack && d->tas >= c->tas + 12 && d->tack == d->tcap && d->tcap == d->tas + 8,
             "CPU TCOR1 posted write, then a DMAC SCFTDR2 write (phase %d): CPU strobe %ld, DMAC strobe %ld capture %ld "
             "ack %ld", ph, (long)c->tas, (long)d->tas, (long)d->tcap, (long)d->tack);
  }
  // the DMAC's next access sees its write (it waits for the ack, which is the capture)
  {
    auto b = newbench();
    Txn *w = b->mk(1, true, 0x1FD00008, 1, 0x4321, 1);
    Txn *r = b->mk(1, false, 0x1FD00008, 1, 0, 0);
    b->run(); b->final_checks();
    TB_CHECK(r->rdata == 0x4321 && r->tcap > w->tack, "DMAC IPRB write then read: %04x", r->rdata);
  }
  printf("dmacw: %d isolated DMAC writes on the 3 paths (acked in the capture cycle; occupancy Iclk 3, Bclk 2, Pclk "
         "10 / 12, longword 14 / 16), back-to-back period 12 / 16, behind a CPU longword posted write at 4 phases, write "
         "then read\n", n);
}

// ------------------------------------------------------------------------------------------------------------------
// land: SOC.md 9.19 (closed at M1a, WI-SOC): a Pclk write takes effect at its slave at the end of its bus cycle, its
// transfers + 1 Pcyc after its strobe (8 Icyc for a byte / word, 12 for a longword), the pce in which the path's next
// strobe may go; a read strobed in that pce meets its slave one pce later, after the write.  [HW] excint C1 ipr_write /
// unie_write k = 15 (warm) and the PI sweep's 17 16 15 14 by phase, each 8 more than a strobe-time write gives with the
// INTC's 6 from its strobe (make v1-soc-m1a).  Every bench checks the rule for every Pclk transaction in
// final_checks(); here the directed cases, each at all 4 Pclk phases.
// ------------------------------------------------------------------------------------------------------------------
static void t_land() {
  std::set<int> ma1_to_land_w, ma1_to_land_l;
  int n = 0;
  for (int ph = 0; ph < 4; ph++) {
    // isolated byte / word / longword writes: the landing's distance from MA1 (the strobe is MA1 + 1..4)
    struct W { uint32_t a; int size; uint32_t v; int blk; uint32_t off; } ws[] = {
      {0xFFC00004, 0, 0x0B, P4B_CPG, 0xFC00004}, {0xFFD00004, 1, 0x0F00, P4B_INTC, 0xFD00004},
      {0xFFD80014, 2, 0xCAFEF00D, P4B_TMU, 0xFD80014}};
    for (auto &w : ws) {
      auto b = newbench();
      b->idle(20); b->align(ph);
      Txn *t = b->mk(0, true, w.a, w.size, w.v, 0, b->cyc);
      b->run(); b->final_checks();
      const int land = w.size == 2 ? 12 : 8;
      TB_CHECK(t->occ() == 3 && t->tcap == t->tas + land && b->ss.peek(w.blk, w.off) == w.v,
               "land: %08x size %d (phase %d): occupancy %d, strobe %ld, capture %ld (want strobe + %d), slave %08x",
               w.a, w.size, ph, t->occ(), (long)(t->tas - t->t0), (long)(t->tcap - t->t0), land, b->ss.peek(w.blk, w.off));
      (w.size == 2 ? ma1_to_land_l : ma1_to_land_w).insert((int)(t->tcap - t->t0));
      n++;
    }
    // a write, then a read of the same register and of another module: the read's strobe falls in the write's landing
    // pce; it meets its slave one pce later and returns the new value; its ack keeps the strobe's schedule (the p4b
    // rows w16_r16 / w16_r16o are the timing)
    for (int other = 0; other < 2; other++) {
      auto b = newbench();
      b->idle(20); b->align(ph);
      Txn *w = b->mk(0, true, 0xFFD8001C, 1, 0x0024, 0, b->cyc);                         // TCR1
      Txn *r = b->mk(0, false, other ? 0xFFD00004 : 0xFFD8001C, 1, 0, 0);                 // IPRA / TCR1
      b->run(); b->final_checks();
      TB_CHECK(r->tas == w->tcap && r->tcap == r->tas + 4 && (other || r->rdata == 0x0024) &&
               r->tack - w->t0 + 1 >= 21 && r->tack - w->t0 + 1 <= 24,
               "land: TCR1 write then %s read (phase %d): write lands %ld, read strobe %ld capture %ld ack %ld, %04x",
               other ? "IPRA" : "TCR1", ph, (long)(w->tcap - w->t0), (long)(r->tas - w->t0), (long)(r->tcap - w->t0),
               (long)(r->tack - w->t0), r->rdata);
      n++;
    }
    // writes back to back: each lands in the next one's strobe cycle, in order (the slave holds the last value)
    {
      auto b = newbench();
      b->idle(20); b->align(ph);
      std::vector<Txn *> v;
      for (int i = 0; i < 4; i++) v.push_back(b->mk(0, true, 0xFFD00008, 1, 0x1000 + i, 0, i ? -1 : b->cyc));   // IPRB
      b->run(); b->final_checks();
      for (int i = 1; i < 4; i++)
        TB_CHECK(v[i]->tas == v[i - 1]->tcap && v[i]->tcap == v[i - 1]->tcap + 8, "land: IPRB writes back to back "
                 "(phase %d): write %d lands %ld, write %d strobe %ld lands %ld", ph, i - 1, (long)v[i - 1]->tcap, i,
                 (long)v[i]->tas, (long)v[i]->tcap);
      TB_CHECK(b->ss.peek(P4B_INTC, 0xFD00008) == 0x1003, "land: IPRB %04x after 4 writes",
               b->ss.peek(P4B_INTC, 0xFD00008));
      n++;
    }
  }
  TB_CHECK(ma1_to_land_w == std::set<int>({9, 10, 11, 12}) && ma1_to_land_l == std::set<int>({13, 14, 15, 16}),
           "land: MA1 -> landing by phase %s (byte / word, want 9..12), %s (longword, want 13..16)",
           setstr(ma1_to_land_w).c_str(), setstr(ma1_to_land_l).c_str());
  printf("land: %d directed benches x 4 phases (MA1 -> landing %s byte / word, %s longword; read after write deferred "
         "one pce with the new value; back-to-back writes in order); every bench so far: %ld Pclk writes landed, %ld "
         "reads deferred\n", n / 4, setstr(ma1_to_land_w).c_str(), setstr(ma1_to_land_l).c_str(), Bench::nland,
         Bench::ndefer);
}

// ------------------------------------------------------------------------------------------------------------------
// pcehold: the Pclk path with its clock enables withheld (M1a-verify F2 / F4).  v1_cpg stops bce / pce while it
// settles after a PLL-relocking FRQCR write, from the cycle after that write lands (SOC.md 2.1; hm 10.5), and the read
// right behind the write is strobed in that landing pce: it meets its slave at the next pce, after the settle.  At
// each Pclk phase, holds of 0 .. 61 cycles, a word write of FRQCR, then a word read of FRQCR (the new value), of IPRA
// (another module) or a longword read of TCOR1: exactly one slave presentation, at the first pce after the hold (the
// landing pce + hold + 4), the value the slave answered then, and the ack at max(the return schedule, capture + 1):
// a word read's schedule is strobe + 11, so a hold of 7 or more moves its ack; a longword read's schedule starts at
// its second transfer, which is that same pce.  (land_checks checks the same rule on every bench.)
// ------------------------------------------------------------------------------------------------------------------
static void t_pcehold() {
  int n = 0, moved = 0;
  const int holds[] = {0, 1, 3, 6, 7, 8, 11, 20, 61};
  for (int ph = 0; ph < 4; ph++)
    for (int h : holds)
      for (int kind = 0; kind < 3; kind++) {
        auto b = newbench();
        b->idle(20); b->align(ph);
        b->hold_on_land = h;
        Txn *w = b->mk(0, true, 0xFFC00000, 1, 0x0E0A, 0, b->cyc);                        // FRQCR <- 0x0E0A
        Txn *r = kind == 0 ? b->mk(0, false, 0xFFC00000, 1, 0, 0)                          // FRQCR
               : kind == 1 ? b->mk(0, false, 0xFFD00004, 1, 0, 0)                          // IPRA
               :             b->mk(0, false, 0xFFD80014, 2, 0, 0);                         // TCOR1 (read32)
        b->run(); b->final_checks();
        const int64_t ecap = r->tas + h + 4;
        const int64_t eack = kind == 2 ? ecap + 11 : std::max(r->tas + 11, ecap + 1);
        TB_CHECK(r->tas == w->tcap && r->tcap == ecap && r->ncap == 1 && r->nack == 1 && r->tack == eack &&
                 r->rdata == r->capval && (kind != 0 || r->rdata == 0x0E0A),
                 "pcehold: phase %d hold %d, FRQCR write then %s: write lands %ld, read strobe %ld, capture %ld (want "
                 "%ld), %d captures, %d acks, ack %ld (want %ld), rdata %08x (slave %08x)", ph, h,
                 kind == 0 ? "FRQCR read" : kind == 1 ? "IPRA read" : "TCOR1 read32", (long)(w->tcap - w->t0),
                 (long)(r->tas - w->t0), (long)(r->tcap - w->t0), (long)(ecap - w->t0), r->ncap, r->nack,
                 (long)(r->tack - w->t0), (long)(eack - w->t0), r->rdata, r->capval);
        moved += kind != 2 && eack > r->tas + 11;
        n++;
      }
  printf("pcehold: %d benches (4 phases x 9 holds x 3 reads behind a landing FRQCR write): one presentation after the "
         "hold, the slave's value, ack = max(schedule, capture + 1) (%d acks moved by the hold)\n", n, moved);
}

// ------------------------------------------------------------------------------------------------------------------
// arr_strobe, last_rdata
// ------------------------------------------------------------------------------------------------------------------
static void t_arr() {
  auto b = newbench();
  const uint32_t arrs[] = {0xF0000000, 0xF1000004, 0xF2000100, 0xF3000100, 0xF3800100, 0xF4001FE0, 0xF5000008,
                           0xF6003F00, 0xF7000100, 0xF7800100};
  int n = 0;
  for (uint32_t a : arrs) { b->mk(0, true, a, 2, a ^ 0x12345678, 1); b->mk(0, false, a, 2, 0, 1); n += 2; }
  // non-array Iclk accesses in between: no pulse
  b->mk(0, false, 0xFF00001C, 2, 0, 0); b->mk(0, true, 0xFF200000, 2, 1, 0);
  b->run(); b->final_checks();
  TB_CHECK(b->arr_pulses == n, "arr_strobe pulses %ld for %d array transactions", b->arr_pulses, n);
  for (auto &u : b->all)
    if (!u->we && is_array(u->blk)) TB_CHECK(u->rdata == (u->addr ^ 0x12345678), "array read-back %08x", u->addr);
  // last_rdata: the undriven UBC window returns the value presented at its (MA1) strobe
  auto c = newbench();
  const uint32_t vals[] = {0x8CFFFF38, 0x636D6165, 0xFF200000, 0};
  for (uint32_t v : vals) {
    c->last_rdata = v;
    Txn *t = c->mk(0, false, 0xFF200100 + (v & 0xFC), 2, 0, 1);
    c->run();
    TB_CHECK(t->rdata == v, "UBC_NODRV read %08x expected %08x", t->rdata, v);
  }
  c->final_checks();
  printf("arr: %d array transactions, %ld arr_strobe pulses; last_rdata through the undriven UBC window\n", n,
         b->arr_pulses);
}

// ------------------------------------------------------------------------------------------------------------------
// resets
// ------------------------------------------------------------------------------------------------------------------
static void t_reset() {
  // (a) manual reset while a CPU Pclk read is registered but not strobed: dropped, never strobed
  {
    auto b = newbench();
    b->idle(20); b->align(3);          // MA1 in a pce cycle: the strobe is 4 cycles away
    Txn *t = b->mk(0, false, 0xFFE80014, 0, 0, 0, b->cyc);   // SCFRDR2: a strobe would pop the FIFO
    b->step();                          // accepted at the end of MA1
    b->r_man = true; b->cur[0] = nullptr; t->abandon = true;
    b->pend.clear();
    b->idle(8); b->r_man = false; b->idle(30);
    TB_CHECK(b->ss.st[PP_SCIF].fifo_pops == 0 && t->nack == 0, "manual reset: un-strobed read performed (%lu pops)",
             (unsigned long)b->ss.st[PP_SCIF].fifo_pops);
    // the path is usable afterwards
    Txn *u = b->mk(0, false, 0xFFD00004, 1, 0, 0);
    b->run();
    TB_CHECK(u->nack == 1 && u->rdata == 0xF0F0, "after the manual reset: IPRA %04x", u->rdata);
  }
  // (b) manual reset after the strobe: the slave saw it once, the reset CPU gets no ack
  {
    auto b = newbench();
    b->idle(20); b->align(0);
    Txn *t = b->mk(0, false, 0xFFE80014, 0, 0, 0, b->cyc);
    while (t->ncap == 0) b->step();
    b->r_man = true; b->cur[0] = nullptr; t->abandon = true;
    b->idle(8); b->r_man = false; b->idle(30);
    TB_CHECK(b->ss.st[PP_SCIF].fifo_pops == 1 && t->nack == 0, "manual reset after the strobe: %lu pops, %d acks",
             (unsigned long)b->ss.st[PP_SCIF].fifo_pops, t->nack);
  }
  // (c) manual, (d) CPU-only reset after a posted Pclk write's ack (MA3) and before its strobe (MA1 at cyc % 4 == 0:
  //     the strobe would be MA1 + 3): the acked entry drains like the WTB, the write is performed once (its MEMW
  //     record, emitted at the ack, is exact: SOC.md 3.1.6, I058)
  for (int kind = 0; kind < 2; kind++) {
    auto b = newbench();
    b->idle(20); b->align(0);
    Txn *w = b->mk(0, true, 0xFFD00008, 1, 0xBEEF, 0, b->cyc);           // IPRB
    while (w->nack == 0) b->step();                                        // acked at MA3, entry loaded
    TB_CHECK(w->nstb == 0, "(c) setup: the IPRB write was strobed before its ack");
    if (kind == 0) b->r_man = true; else b->r_cpu = true;
    b->idle(8); b->r_man = b->r_cpu = false; b->idle(30);
    uint32_t v = b->ss.peek(P4B_INTC, 0xFD00008);
    TB_CHECK(v == 0xBEEF && w->ncap == 1, "%s reset after a posted write's ack, before its strobe: the write was lost "
             "(IPRB %04x, %d captures)", kind == 0 ? "manual" : "CPU-only", v, w->ncap);
    b->final_checks();
  }
  // (e) manual reset with a strobed DMAC read, the DMAC still presenting: it gets its ack (its unit completes)
  {
    auto b = newbench();
    b->idle(20); b->align(1);
    Txn *d = b->mk(1, false, 0x1FD80024, 2, 0, 0, b->cyc);
    while (d->ncap == 0) b->step();
    b->r_man = true;
    b->idle(8); b->r_man = false;
    b->run();
    TB_CHECK(d->nack == 1 && d->rdata == d->capval, "manual reset: the strobed DMAC read was not acked");
    b->final_checks();
  }
  // (f) power-on reset in the middle of traffic: every path idle, then normal service
  {
    auto b = newbench();
    b->mk(0, true, 0xFFD00008, 1, 0x1111, 0);
    b->mk(0, false, 0xFFD8000C, 2, 0, 0);
    b->idle(6);
    for (auto &u : b->all) u->abandon = true;
    b->r_pin = true; b->cur[0] = nullptr; b->q[0].clear(); b->pend.clear();
    b->idle(8); b->r_pin = false; b->idle(4);
    size_t pops = b->ss.caps.size();
    b->idle(40);
    TB_CHECK(b->ss.caps.size() == pops, "power-on reset: a strobe after the reset");
    Txn *t = b->mk(0, false, 0xFF000030, 2, 0, 0);
    b->run();
    TB_CHECK(t->rdata == 0x040205C1, "after power-on reset: PVR %08x", t->rdata);
  }
  // (g) F1 of the WI-P4 verification: a CPU-only or manual reset orphans a CPU Iclk read already captured (in its
  //     return stages); the next Iclk write (CPU BARA, CPU CCR, DMAC BARA at 0x1F200000) must be strobed exactly once
  //     and acked in its capture cycle (occupancy 3).  A second strobe fails the capture matching in step().
  for (int rk = 0; rk < 3; rk++)
    for (int vic = 0; vic < 3; vic++) {
      auto b = newbench();
      b->idle(20); b->align(0);
      Txn *r = b->mk(0, false, 0xFF00001C, 2, 0, 0, b->cyc);                 // CCR read: strobed + captured in MA1
      b->step();
      TB_CHECK(r->ncap == 1 && r->nack == 0, "orph setup: the CCR read was not captured in its MA1");
      if (rk) {
        if (rk == 1) b->r_cpu = true; else b->r_man = true;
        b->cur[0] = nullptr; r->abandon = true;                               // the reset core withdraws
        b->idle(8); b->r_cpu = b->r_man = false;
      }
      b->idle(6);
      Txn *w = vic == 1 ? b->mk(1, true, 0x1F200000, 2, 0xCAFEBABE, 0)
             : vic == 2 ? b->mk(0, true, 0xFF00001C, 2, 0x909, 0) : b->mk(0, true, 0xFF200000, 2, 0x12345678, 0);
      b->run();
      Txn *w2 = b->mk(0, true, 0xFF20000C, 2, 0x0BADF00D, 1);             // then the path is sane again
      b->run(); b->final_checks();
      TB_CHECK(w->ncap == 1 && w->nack == 1 && w->occ() == 3 && w2->occ() == 3,
               "reset %d, then an Iclk write (victim %d): %d captures, %d acks, occupancy %d / %d", rk, vic, w->ncap,
               w->nack, w->occ(), w2->occ());
    }
  // (h) a CPU-only or manual reset between a posted write's acceptance (MA1) and its ack (MA3): the store did not
  //     complete.  MA1 at cyc % 4 == 0: the strobe would be MA1 + 3, so the entry is dropped (no strobe, no ack); MA1
  //     at cyc % 4 == 2: MA2 is a pce, the write was strobed there and is performed once, with no ack: it lands at
  //     its slave 8 cycles later, during or after the reset (SOC.md 9.19; `tas` is the address strobe)
  for (int kind = 0; kind < 2; kind++)
    for (int early = 0; early < 2; early++) {
      auto b = newbench();
      b->idle(20); b->align(early ? 2 : 0);
      Txn *w = b->mk(0, true, 0xFFD00008, 1, 0xBEEF, 0, b->cyc);              // IPRB (0 before)
      b->step();                                                             // MA1: the slot takes it
      if (early) b->step();                                                  // MA2: the strobe (a pce)
      TB_CHECK((w->tas >= 0) == (bool)early && w->ncap == 0 && w->nack == 0, "posted write before its ack: strobed %d, "
               "%d captures, %d acks", w->tas >= 0, w->ncap, w->nack);
      if (kind) b->r_man = true; else b->r_cpu = true;
      b->cur[0] = nullptr; w->abandon = true;
      if (!early) b->pend.erase(std::remove(b->pend.begin(), b->pend.end(), w), b->pend.end());   // (a strobed one lands)
      b->idle(8); b->r_man = b->r_cpu = false; b->idle(30);
      uint32_t v = b->ss.peek(P4B_INTC, 0xFD00008);
      TB_CHECK(w->nack == 0 && w->ncap == early && v == (early ? 0xBEEFu : 0u),
               "%s reset in a posted write's MA%d: %d captures, %d acks, IPRB %04x", kind ? "manual" : "CPU-only",
               early ? 3 : 2, w->ncap, w->nack, v);
      Txn *u = b->mk(0, true, 0xFFD00008, 1, 0x1234, 0);
      Txn *rd = b->mk(0, false, 0xFFD00008, 1, 0, 0);
      b->run(); b->final_checks();
      TB_CHECK(u->occ() == 3 && rd->rdata == 0x1234, "after the reset: posted write %d, IPRB %04x", u->occ(), rd->rdata);
    }
  // (i) the master keeps presenting a posted write through a CPU-only reset that starts in its MA3 (its ack cycle;
  //     MA1 at cyc % 4 == 0, so it is not strobed yet): no ack while the master is reset (a reset master is never
  //     acked, and the un-acked entry is dropped); afterwards the still-presented request is taken again, performed
  //     once and acked once
  {
    auto b = newbench();
    b->idle(20); b->align(0);
    Txn *w = b->mk(0, true, 0xFFD00008, 1, 0x5AA5, 0, b->cyc);                // IPRB
    b->step(); b->step();                                                     // MA1, MA2
    TB_CHECK(w->ncap == 0 && w->nack == 0, "reset (i) setup: %d captures, %d acks", w->ncap, w->nack);
    b->r_cpu = true;
    for (int i = 0; i < 8; i++) { b->step(); TB_CHECK(w->nack == 0, "posted write acked while its master is reset"); }
    b->r_cpu = false;
    b->run(); b->final_checks();
    TB_CHECK(w->nack == 1 && w->ncap == 1 && b->ss.peek(P4B_INTC, 0xFD00008) == 0x5AA5,
             "posted write presented through a reset: %d acks, %d captures", w->nack, w->ncap);
  }
  // (j) a CPU posted write strobed in its MA2 (MA1 at cyc % 4 == 2), then a CPU-only / manual reset in its MA3; the
  //     reset core withdraws and the next request (an Iclk BARA write) is presented during the reset: the old write is
  //     performed once and never acked, the new one must not inherit its pending ack (performed once, acked once,
  //     after the reset).  Found by the WI-P4 verifier's randrst harness against an intermediate fix.
  for (int kind = 0; kind < 2; kind++) {
    auto b = newbench();
    b->idle(20); b->align(2);
    Txn *w = b->mk(0, true, 0xFFD00008, 1, 0x6BB6, 0, b->cyc);                // IPRB
    b->step(); b->step();                                                     // MA1, MA2 (the strobe)
    TB_CHECK(w->tas >= 0 && w->ncap == 0 && w->nack == 0, "reset (j) setup: strobed %d, %d captures, %d acks",
             w->tas >= 0, w->ncap, w->nack);                                  // (it lands 8 cycles on: SOC.md 9.19)
    if (kind) b->r_man = true; else b->r_cpu = true;
    b->cur[0] = nullptr; w->abandon = true;
    Txn *n = b->mk(0, true, 0xFF200000, 2, 0x77777777, 0, b->cyc);             // presented from the reset's first cycle
    b->idle(8); b->r_man = b->r_cpu = false;
    b->run(); b->final_checks();
    TB_CHECK(w->nack == 0 && w->ncap == 1 && n->nack == 1 && n->ncap == 1 && n->tcap >= n->tack,
             "%s reset after a posted write's strobe, then a new Iclk write: old %d acks / %d captures, new %d acks / "
             "%d captures", kind ? "manual" : "CPU-only", w->nack, w->ncap, n->nack, n->ncap);
  }
  // (k) a manual reset reaches the DMAC's request that is neither strobed nor acked: it is dropped (no strobe while the
  //     reset lasts) and, since the DMAC keeps presenting it, taken again afterwards: one strobe, one ack, after the
  //     reset.  A CPU-only reset leaves it alone: strobed and acked during the reset.  A read (TCR1) and a write (IPRB)
  for (int kind = 0; kind < 2; kind++)
    for (int we = 0; we < 2; we++) {
      auto b = newbench();
      b->idle(20); b->align(0);                    // the DMAC presents at cyc % 4 == 0: its pce strobe is 3 cycles on
      Txn *d = we ? b->mk(1, true, 0x1FD00008, 1, 0x7777, 0, b->cyc) : b->mk(1, false, 0x1FD8001C, 1, 0, 0, b->cyc);
      b->step();                                   // accepted at the end of its first cycle
      TB_CHECK(d->t0 >= 0 && d->nstb == 0, "(k) setup: DMAC request not presented, or strobed at once");
      int64_t r0 = b->cyc;
      if (kind == 0) b->r_man = true; else b->r_cpu = true;
      b->idle(8); b->r_man = b->r_cpu = false;
      int64_t r1 = b->cyc;                         // the first cycle after the reset
      b->run(); b->final_checks();
      // the address strobe decides (the slave sees a Pclk write at its landing, 8 cycles on: SOC.md 9.19)
      bool ok = d->nstb == 1 && d->ncap == 1 && d->nack == 1 && (kind == 0 ? d->tas >= r1 : (d->tas >= r0 && d->tas < r1));
      TB_CHECK(ok, "(k) %s reset, DMAC %s accepted before it, not strobed: strobed %d times (first at %ld; reset %ld..%ld), "
               "%d captures, %d acks", kind == 0 ? "manual" : "CPU-only", we ? "IPRB write" : "TCR1 read", d->nstb,
               (long)d->tas, (long)r0, (long)r1 - 1, d->ncap, d->nack);
      if (we) TB_CHECK(b->ss.peek(P4B_INTC, 0xFD00008) == 0x7777, "(k) DMAC IPRB write not performed");
    }
  // (l) a reset while a strobe is stalled: the transaction has been strobed, so it completes -- the stall rule presents
  //     it again at the next opportunity (the stub fails a stalled strobe that is not re-presented) and the slave
  //     captures it once -- without an ack to the reset CPU.  Bclk (a Pclk slave never stalls, SOC.md 9.19): the BSC
  //     stalls BCR1's first 6 strobes (12 cycles: re-presented every bce), so the capture comes after the 8-cycle
  //     reset; the CPU then presents a PDTRA read, which waits for the path and must be acked with its own data (not
  //     BCR1's).  Iclk: an OC address-array read (stalled at MA1), reset from its MA2.
  for (int kind = 0; kind < 2; kind++) {
    {
      auto b = newbench();
      b->ss.st[PP_BSC].stall_first = 6;
      b->idle(20); b->align(0);
      Txn *t = b->mk(0, false, 0xFF800000, 2, 0, 0, b->cyc);                 // BCR1
      while (t->nstb == 0) b->step();                                        // its first (stalled) strobe
      TB_CHECK(t->ncap == 0, "(l) setup: BCR1 captured at its first strobe");
      if (kind) b->r_man = true; else b->r_cpu = true;
      b->cur[0] = nullptr; t->abandon = true;                                 // the reset core withdraws
      b->idle(8); b->r_man = b->r_cpu = false;
      const int64_t rend = b->cyc;                                           // the first cycle after the reset
      Txn *u = b->mk(0, false, 0xFF800030, 1, 0, 0);                         // PDTRA, after the reset
      b->run(); b->final_checks();
      const uint32_t pdtra = b->ss.peek(P4B_BSC, 0xF800030) & 0xFFFF;
      TB_CHECK(t->nstb == 7 && t->ncap == 1 && t->nack == 0 && u->nack == 1 && u->rdata == pdtra && u->tcap > t->tcap &&
               t->tcap >= rend,
               "(l) %s reset while BCR1's strobe is stalled: %d strobes, %d captures (at %ld, reset over at %ld), %d acks; "
               "then PDTRA %04x (%d acks, want %04x)", kind ? "manual" : "CPU-only", t->nstb, t->ncap, (long)t->tcap,
               (long)rend, t->nack, u->rdata, u->nack, pdtra);
    }
    {
      auto b = newbench();
      b->idle(20); b->align(0);
      Txn *t = b->mk(0, false, 0xF4000100, 2, 0, 0, b->cyc);                 // OCAA: the array stalls MA1's strobe
      b->step();
      TB_CHECK(t->nstb == 1 && t->ncap == 0, "(l) setup: the OCAA read was not stalled at MA1");
      if (kind) b->r_man = true; else b->r_cpu = true;
      b->cur[0] = nullptr; t->abandon = true;
      b->idle(8); b->r_man = b->r_cpu = false;
      Txn *u = b->mk(0, false, 0xFF00001C, 2, 0, 0);                         // CCR
      b->run(); b->final_checks();
      TB_CHECK(t->nstb == 2 && t->ncap == 1 && t->nack == 0 && u->nack == 1 && u->rdata == 0x105,
               "(l) %s reset in a stalled OCAA read's MA2: %d strobes, %d captures, %d acks; then CCR %08x",
               kind ? "manual" : "CPU-only", t->nstb, t->ncap, t->nack, u->rdata);
    }
  }
  printf("reset: a reset drops what is neither strobed nor acked (CPU: every reset; DMAC: manual, then taken again) "
         "and completes the rest (a strobed CPU transaction without an ack; an acked posted write drains, manual and "
         "CPU-only; a stalled strobe is presented again); power-on idles every path; an orphaned Iclk read then an "
         "Iclk write (one strobe)\n");
}

// ------------------------------------------------------------------------------------------------------------------
// random traffic
// ------------------------------------------------------------------------------------------------------------------
// holds: also withhold bce / pce at random (after 30 % of the Pclk landings, as v1_cpg's relock settle does, and in
// 1 % of the other cycles), 1 .. 24 cycles each (the slot model is then skipped; the landing rule, the deferred
// reads' acks and every data / protocol check still apply)
static void t_random(uint64_t seed, int n, bool holds = false) {
  auto b = newbench();
  Rng r(seed);
  if (holds) { b->rand_land_hold_pct = 30; b->rand_hold_pct = 1; b->rand_hold_max = 24; b->hrng ^= seed * 0x2545F491ull; }
  const long late0 = Bench::nlate, hold0 = Bench::nhold;
  for (int p = 0; p < PP_N; p++) { b->ss.st[p].rand_stall_pct = 20; b->ss.st[p].rand_stall_max = 2; }
  struct A { uint32_t a; int size; bool w_ok; };
  const A pool[] = {
    {0xFF000000, 2, true}, {0xFF000004, 2, true}, {0xFF00001C, 2, true}, {0xFF000024, 2, false}, {0xFF000030, 2, false},
    {0xFF000084, 1, true}, {0xFF000014, 0, true}, {0xFF000050, 2, true}, {0xFF100008, 2, false}, {0xFF200000, 2, true},
    {0xFF200008, 1, true}, {0xFF200100, 2, true}, {0xFF300000, 1, true}, {0xE8000000, 2, true}, {0xF4000100, 2, true},
    {0xF6000200, 2, true}, {0xF1000040, 2, true}, {0xFF800000, 2, true}, {0xFF800030, 1, true}, {0xFF940190, 0, true},
    {0xFFA00010, 2, true}, {0xFFA00040, 2, true}, {0xFFC00000, 1, true}, {0xFFC00004, 0, true}, {0xFFC0000C, 0, false},
    {0xFFC80038, 0, true}, {0xFFD00004, 1, true}, {0xFFD8000C, 2, false}, {0xFFD80014, 2, true}, {0xFFE00004, 0, true},
    {0xFFE80010, 1, true}, {0xFFE80014, 0, false}, {0xFFE8000C, 0, true}, {0xFFF00000, 1, false},
  };
  const int NP = sizeof(pool) / sizeof(pool[0]);
  for (int i = 0; i < n; i++) {
    int m = r.n(4) == 0 ? 1 : 0;
    const A &a = pool[r.n(NP)];
    uint32_t addr = a.a;
    if (m == 1) {
      if (addr >> 24 != 0xFF) continue;                                   // the DMAC reaches only 0x1F / P4 regs
      addr = 0x1F000000u | (addr & 0xFFFFFF);
    }
    int size = r.n(8) == 0 ? (int)r.n(3) : a.size;                        // some wrong sizes
    bool we = a.w_ok && r.n(3) == 0;
    b->mk(m, we, addr, size, r.u32() & size_mask(size), (int)r.n(6));
  }
  b->run(std::max<int64_t>(200000, 40LL * n));        // the budget scales with n (F6 of the WI-P4 verification)
  b->final_checks();
  long caps = (long)b->ss.caps.size(), st = 0;
  for (int p = 0; p < PP_N; p++) st += (long)(b->ss.st[p].strobes - b->ss.st[p].captures);
  if (holds)
    printf("random with withheld enables: %zu transactions, %ld captures, %ld stalls, %ld cycles, %ld holds, %ld "
           "deferred reads acked after their schedule\n", b->all.size(), caps, st, (long)b->cyc, Bench::nhold - hold0,
           Bench::nlate - late0);
  else
    printf("random: %zu transactions, %ld captures, %ld stalls, %ld cycles\n", b->all.size(), caps, st, (long)b->cyc);
}

// ------------------------------------------------------------------------------------------------------------------
// brackets: PMCTR ; accesses ; PMCTR as the console programs measure it (A and B are PMCTR1L reads; the stub returns
// the cycle of the strobe, which is the read's MA1).  ph = cyc % 4 of A's MA1 (3 = a pce cycle).
// ------------------------------------------------------------------------------------------------------------------
struct BAcc { bool we; uint32_t addr; int size; };
static int bracket(int ph, const std::vector<BAcc> &xs) {
  auto b = newbench();
  b->idle(24); b->align(ph);
  Txn *A = b->mk(0, false, 0xFF100008, 2, 0, 0, b->cyc);
  for (auto &x : xs) b->mk(0, x.we, x.addr, x.size, x.we ? 0xFFFFFFFFu & size_mask(x.size) : 0, 0);
  Txn *B = b->mk(0, false, 0xFF100008, 2, 0, 0);
  b->run(); b->final_checks();
  if (A->ncap != 1 || B->ncap != 1) return -1;
  return (int)(B->capval - A->capval);
}
static std::vector<BAcc> rep(const std::vector<BAcc> &v, int n) {
  std::vector<BAcc> r;
  for (int i = 0; i < n; i++) r.insert(r.end(), v.begin(), v.end());
  return r;
}
static const BAcc W16{true, 0xFFD8001C, 1}, W32{true, 0xFFD80014, 2}, R16{false, 0xFFD8001C, 1},
                  R16O{false, 0xFFD00004, 1}, R32{false, 0xFFD80014, 2};

// The P4 bridge probe of open question 9.13 (tools/hw/p4bus/p4b.c, p4b.elf.hwlog, 2026-09-23): a Pclk read aligns the
// pipeline, then k fillers put the bracket's first read at each Pclk phase p = 0..3 (p0: A's MA1 is a pce cycle; the
// rd16 row is the phase meter).  Min of 8 runs; the two k per phase agree (two one-off outliers, w16x1 and w16x4 at
// one k each, disagree with their other k and are left out).  Every row must match at every phase.
static void t_probe() {
  struct Row { const char *n; std::vector<BAcc> body; int hw[4]; };
  const std::vector<Row> rows = {
    {"none", {}, {3, 3, 3, 3}},
    {"rd16", {R16}, {16, 19, 18, 17}},
    {"rd16o", {R16O}, {16, 19, 18, 17}},
    {"rd32", {R32}, {20, 23, 22, 21}},
    {"rd16x16", rep({R16}, 16), {256, 259, 258, 257}},
    {"rd32x16", rep({R32}, 16), {320, 323, 322, 321}},
    {"rd16_rd16o", {R16, R16O}, {32, 35, 34, 33}},
    {"w16x1", {W16}, {6, 6, 6, 6}},
    {"w16x2", rep({W16}, 2), {12, 15, 14, 13}},
    {"w16x3", rep({W16}, 3), {20, 23, 22, 21}},
    {"w16x4", rep({W16}, 4), {28, 31, 30, 29}},
    {"w16x16", rep({W16}, 16), {124, 127, 126, 125}},
    {"w32x1", {W32}, {6, 6, 6, 6}},
    {"w32x2", rep({W32}, 2), {12, 15, 14, 13}},
    {"w32x3", rep({W32}, 3), {24, 27, 26, 25}},
    {"w32x4", rep({W32}, 4), {36, 39, 38, 37}},
    {"w32x16", rep({W32}, 16), {180, 183, 182, 181}},
    {"w32_w16", {W32, W16}, {12, 15, 14, 13}},
    {"w32_w16_w16", {W32, W16, W16}, {24, 27, 26, 25}},
    {"w16_w32", {W16, W32}, {12, 15, 14, 13}},
    {"w16_w32_w16", {W16, W32, W16}, {20, 23, 22, 21}},
    {"w32_r16o", {W32, R16O}, {28, 31, 30, 29}},
    {"w32_r32", {W32, R32}, {32, 35, 34, 33}},
    {"w32x4_r16o", {W32, W32, W32, W32, R16O}, {64, 67, 66, 65}},
    {"w16_r16", {W16, R16}, {24, 27, 26, 25}},
    {"w16_r16o", {W16, R16O}, {24, 27, 26, 25}},
    {"w16_r16x2", rep({W16, R16}, 2), {48, 51, 50, 49}},
    {"w16_r16x3", rep({W16, R16}, 3), {72, 75, 74, 73}},
    {"w16_r16x8", rep({W16, R16}, 8), {192, 195, 194, 193}},
    {"w16_r16ox8", rep({W16, R16O}, 8), {192, 195, 194, 193}},
    {"w32_r32x8", rep({W32, R32}, 8), {256, 259, 258, 257}},
  };
  int nbad = 0;
  for (auto &r : rows) {
    int got[4];
    for (int p = 0; p < 4; p++) got[p] = bracket((3 + p) & 3, r.body);
    bool ok = true;
    for (int p = 0; p < 4; p++) ok = ok && got[p] == r.hw[p];
    TB_CHECK(ok, "probe %s: RTL %d %d %d %d, console %d %d %d %d (phases p0..p3)", r.n, got[0], got[1], got[2], got[3],
             r.hw[0], r.hw[1], r.hw[2], r.hw[3]);
    nbad += !ok;
    if (g_verbose) printf("  %-12s %4d %4d %4d %4d  (console %4d %4d %4d %4d)\n", r.n, got[0], got[1], got[2], got[3],
                          r.hw[0], r.hw[1], r.hw[2], r.hw[3]);
  }
  printf("probe: %zu p4b rows x 4 Pclk phases, exact against the console (%d rows off)\n", rows.size(), nbad);
}

// The console brackets measured before the probe, at a phase the program did not control (best of N): the console
// value must be one of the RTL's four.  timers-clock F (tc.elf.hwlog: 16 accesses), T (tc2.elf.hwlog: isolated),
// bsc-memmap 6.4 (T1 / T8 / T24 = 1 / 8 / 24 accesses).
static void t_brackets() {
  struct Row { const char *n; std::vector<BAcc> body; int hw; };
  const BAcc PM{false, 0xFF100008, 2}, IEV{false, 0xFF000028, 2}, TC1{false, 0xFFD80018, 2}, TC2{false, 0xFFD80024, 2},
             TSTR{false, 0xFFD80004, 0}, TOCR{false, 0xFFD80000, 0}, FRQ{false, 0xFFC00000, 1}, STB{false, 0xFFC00004, 0},
             WTC{false, 0xFFC00008, 0}, R64{false, 0xFFC80000, 0}, RC2{false, 0xFFC8003C, 0}, WTC1{true, 0xFFD80018, 2},
             WTOC{true, 0xFFD80000, 0}, BCR{false, 0xFF800000, 2}, PDT{false, 0xFF800030, 1}, CCR{false, 0xFF00001C, 2},
             TC0{false, 0xFFD8000C, 2};
  const std::vector<Row> rows = {
    {"F rd32 PMCTR0L", rep({PM}, 16), 51}, {"F rd32 INTEVT", rep({IEV}, 16), 51}, {"F rd32 TCNT1", rep({TC1}, 16), 320},
    {"F rd32 TCOR1", rep({R32}, 16), 320}, {"F rd16 TCR1", rep({R16}, 16), 256}, {"F rd8 TSTR", rep({TSTR}, 16), 256},
    {"F rd8 TOCR", rep({TOCR}, 16), 256}, {"F rd32 TCNT2", rep({TC2}, 16), 320}, {"F rd16 FRQCR", rep({FRQ}, 16), 256},
    {"F rd8 STBCR", rep({STB}, 16), 256}, {"F rd8 WTCNT", rep({WTC}, 16), 256}, {"F rd8 R64CNT", rep({R64}, 16), 256},
    {"F rd8 RCR2", rep({RC2}, 16), 256}, {"F rd16 IPRA", rep({R16O}, 16), 256}, {"F wr32 TCOR1", rep({W32}, 16), 181},
    {"F wr32 TCNT1", rep({WTC1}, 16), 181}, {"F wr16 TCR1", rep({W16}, 16), 124}, {"F wr8 TOCR", rep({WTOC}, 16), 124},
    {"F wr16/rd16 TCR1 pair", rep({W16, R16}, 8), 192},
    {"T rd32 TCNT1", {TC1}, 21}, {"T rd16 TCR1", {R16}, 17}, {"T wr32 TCOR1", {W32}, 6}, {"T wr16 TCR1", {W16}, 6},
    {"T wr16 TCR1 + rd16 IPRA", {W16, R16O}, 26}, {"T rd32 PMCTR", {PM}, 6},
    {"bsc BCR1 T1", {BCR}, 11}, {"bsc BCR1 T8", rep({BCR}, 8), 67}, {"bsc BCR1 T24", rep({BCR}, 24), 195},
    {"bsc PDTRA T1", {PDT}, 11}, {"bsc PDTRA T8", rep({PDT}, 8), 67}, {"bsc PDTRA T24", rep({PDT}, 24), 195},
    {"bsc CCR T1", {CCR}, 6}, {"bsc CCR T8", rep({CCR}, 8), 27}, {"bsc CCR T24", rep({CCR}, 24), 75},
    {"bsc TCNT0 T1", {TC0}, 21}, {"bsc TCNT0 T8", rep({TC0}, 8), 161}, {"bsc TCNT0 T24", rep({TC0}, 24), 481},
  };
  int nbad = 0;
  for (auto &r : rows) {
    std::set<int> got;
    for (int p = 0; p < 4; p++) got.insert(bracket(p, r.body));
    bool ok = got.count(r.hw) > 0;
    TB_CHECK(ok, "bracket %s: RTL by phase %s, console %d", r.n, setstr(got).c_str(), r.hw);
    nbad += !ok;
    if (g_verbose) printf("  %-26s RTL %-20s console %d\n", r.n, setstr(got).c_str(), r.hw);
  }
  printf("brackets: %zu console brackets (timers-clock F / T, bsc-memmap 6.4) reachable at some phase (%d not)\n",
         rows.size(), nbad);
}

// ------------------------------------------------------------------------------------------------------------------
// randrst: random two-master traffic with random stalls and random CPU-only / manual resets (8 cycles each).  The CPU
// withdraws at a reset; the DMAC keeps presenting.  Invariants: every capture belongs to exactly one pending
// transaction (a second strobe of a completed one fails in step()); every stalled strobe is presented again (the
// stubs); the reset rule (SOC.md 3.1.6, p4ref::survives_reset): a CPU transaction the reset orphaned is performed
// exactly when it had been strobed (once) and never acked; every other transaction -- an acked posted write, every
// DMAC transaction -- is performed once and acked once with its captured data; nothing hangs.
// ------------------------------------------------------------------------------------------------------------------
static void t_randrst(uint64_t seed, int ncyc) {
  auto b = newbench();
  Rng r(seed);
  for (int p = 0; p < PP_N; p++) { b->ss.st[p].rand_stall_pct = 10; b->ss.st[p].rand_stall_max = 2; }
  struct A { uint32_t a; int size; bool w_ok; };
  const A pool[] = {
    {0xFF00001C, 2, true}, {0xFF000030, 2, false}, {0xFF100008, 2, false}, {0xFF200000, 2, true}, {0xFF20000C, 2, true},
    {0xFF200100, 2, false}, {0xFF300000, 2, true}, {0xF4000100, 2, true}, {0xF6000200, 2, true}, {0xFF800000, 2, true},
    {0xFFA00010, 2, true}, {0xFFC00004, 0, true}, {0xFFD00004, 1, true}, {0xFFD8000C, 2, false},
    {0xFFD80014, 2, true}, {0xFFE80010, 1, true}, {0xFF000000, 2, true},
  };
  const int NPOOL = sizeof(pool) / sizeof(pool[0]);
  long nres = 0, orphaned = 0;
  int64_t end = b->cyc + ncyc, next_rst = b->cyc + 50 + r.n(300);
  while (b->cyc < end) {
    for (int m = 0; m < 2; m++)
      while (b->q[m].size() < 2) {
        const A &x = pool[r.n(NPOOL)];
        uint32_t ad = x.a;
        if (m == 1) { if (ad >> 24 != 0xFF) continue; ad = 0x1F000000u | (ad & 0xFFFFFF); }
        bool we = x.w_ok && r.n(3) == 0;
        b->mk(m, we, ad, x.size, r.u32() & size_mask(x.size), (int)r.n(4));
      }
    if (b->cyc == next_rst) {
      bool man = r.n(2);
      nres++;
      if (man) b->r_man = true; else b->r_cpu = true;
      // the CPU's transactions not acked are orphaned (no ack may come): each completes iff it was strobed (cur[0] may
      // be captured already, e.g. a read in its return stages: no longer pending); an acked posted write is not
      // orphaned (it drains).  A dropped DMAC request is presented again: not orphaned either.
      if (b->cur[0]) {
        Txn *t = b->cur[0];
        t->abandon = true; t->stb_at_rst = survives_reset(t->nstb > 0 || t->tas >= 0, false);   // (Pclk: address strobe)
        b->cur[0] = nullptr; orphaned++;
      }
      for (Txn *t : b->pend)
        if (t->m == 0 && t->nack == 0) { t->abandon = true; t->stb_at_rst = survives_reset(t->nstb > 0 || t->tas >= 0, false); }
      b->idle(8);
      b->r_man = b->r_cpu = false;
      // an orphaned transaction never strobed will never be captured; a strobed one stays pending until its capture
      b->pend.erase(std::remove_if(b->pend.begin(), b->pend.end(), [](Txn *t) { return t->abandon && !t->stb_at_rst; }),
                    b->pend.end());
      next_rst = b->cyc + 50 + r.n(400);
      continue;
    }
    b->step();
  }
  b->run(200000);
  b->land_checks();                    // SOC.md 9.19 under random resets: a strobed write still lands at strobe + 8 / 12
  long bad = 0, n = 0;
  for (auto &u : b->all) {
    Txn *t = u.get();
    if (t->t0 < 0) continue;
    n++;
    bool ok;
    if (t->abandon) ok = t->nack == 0 && t->ncap == (t->stb_at_rst ? 1 : 0);
    else if (t->bridge) ok = t->nack == 1 && t->ncap == 0;
    else ok = t->nack == 1 && t->ncap == 1 && (t->we || t->rdata == t->capval);
    TB_CHECK(ok, "randrst %s m%d %08x: acks %d captures %d rdata %08x capture %08x", t->we ? "wr" : "rd", t->m, t->addr,
             t->nack, t->ncap, t->rdata, t->capval);
    bad += !ok;
  }
  printf("randrst seed %lu: %ld transactions, %ld resets (%ld CPU requests orphaned), %ld bad\n", (unsigned long)seed,
         n, nres, orphaned, bad);
}

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  uint64_t seed = 1; int n = 4000;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = strtoull(argv[++i], nullptr, 0);
    else if (!strcmp(argv[i], "--n") && i + 1 < argc) n = atoi(argv[++i]);
    else if (!strcmp(argv[i], "-v")) g_verbose = 1;
  }
  g_top = new Vv1_p4bus_tb_top;
  t_decode();
  t_timing();
  t_post();
  t_stall();
  t_dmac();
  t_dmacw();
  t_land();
  t_pcehold();
  t_arr();
  t_reset();
  t_probe();
  t_brackets();
  t_random(seed, n);
  t_random(seed + 1, n, true);
  for (uint64_t k = 0; k < 4; k++) t_randrst(seed * 4 + k, std::max(15 * n, 20000));
  delete g_top;
  printf("land totals: %ld Pclk writes landed at strobe + 8 / + 12, %ld reads deferred one pce by a landing (every "
         "bench, SOC.md 9.19); pclk_wpend checked over %ld writes (acceptance + 1 .. landing)\n", Bench::nland,
         Bench::ndefer, Bench::nwpend);
  if (g_fail) { printf("v1-p4bus: FAIL (%ld)\n", g_fail); return 1; }
  printf("v1-p4bus: PASS\n");
  return 0;
}
