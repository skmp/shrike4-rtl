// v1_dbg_sched.h -- the cycle schedule of a short straight-line instruction sequence under v1's issue and fetch rules
// with the SoC's P4 bridge timing (rtl/v1/README.md "Issue and pairing", rtl/v1/v1_fetch.sv; rtl/v1/SOC.md 3.1.5,
// 3.2.2).  WI-DBG's unit testbenches (tb/v1_perfctr_tb.cpp, tb/v1_ubc_tb.cpp) use it to script the core side of the
// console experiments they replay: which cycle a P4 read strobes (MA1), which cycle a P4 write strobes (MA3), which
// cycle each fetch word is fetched, when each instruction issues and leaves EX, and the D-stage events 0x13 / 0x14 /
// 0x23 per cycle.  It is a scheduling model of the rules below, not of v1's RTL; the integrated core must produce the
// same schedule (the M1a / M1b replays of these programs, SOC.md 8.3, are the end-to-end check).
//
// Rules modelled (the subset these programs exercise; every one is a v1 rule validated by the sweep, or SOC.md's):
//   * fetch (v1_fetch.sv): one aligned word per cycle into a queue of IQN halfword entries, fetched when the word fits
//     once this cycle's issues have left the queue; a fetched word is in the queue (issuable) the next cycle.  The
//     program starts at a redirect target at a word boundary: word 0 is fetched in cycle 0; its first instruction is
//     target-tagged and never pairs as a follower.
//   * issue: two decode slots; the leader issues when its operands are ready; the follower too when the pair rule
//     allows (MT pairs with anything; EX, LS do not pair with their own class) and it does not read the leader's
//     result (no same-cycle GPR bypass).
//   * operands are taken at the start of EX: an ALU result is ready for an instruction issued one cycle later
//     (latency 1), load data two cycles later (latency 2: the D-stage comparator holds a consumer while the load is in
//     EX).  Store data is an operand like any other.
//   * MA: a cached access (an OC hit) acks in its first MA cycle.  A P4 Iclk-class access occupies MA for 3 cycles
//     (SOC.md 3.1.5: a read strobes in MA1, a write in MA3; ack in MA3); MA1 and MA2 are `mah` cycles, which hold
//     fetch (when fetch_hold is set; SOC.md 4.1 item 8), D and EX.  A PTEH write occupies MA for 4 (`ma_n`, st_pteh:
//     v1_mmu_regs answers its first strobe with stall; its capture is MA4) [HW core-integration 6.1].
//   * events per cycle: 0x13 +1 when an issue group issues, 0x14 +1 when two instructions issue, 0x23 +1 always.
#pragma once
#include <cstdint>
#include <deque>
#include <vector>

namespace dsched {

enum Cls { MT, EX, LS };

struct Ins {
  Cls cls = MT;
  int dst = -1;             // GPR written (-1 none)
  int src[2] = {-1, -1};    // GPRs read
  bool load = false;        // the result is load data (latency 2)
  bool p4 = false;          // a P4 Iclk-class access: 3 MA cycles, read strobe MA1, write strobe MA3
  bool wr = false;          // a store
  int ma_n = 3;             // a P4 access's MA cycles (the write's capture in the last): 4 for a PTEH write
};

inline Ins nop() { return Ins{}; }
inline Ins add(int rn) { Ins i; i.cls = EX; i.dst = rn; i.src[0] = rn; return i; }            // ADD #imm,Rn
inline Ins mov(int rm, int rn) { Ins i; i.cls = MT; i.dst = rn; i.src[0] = rm; return i; }    // MOV Rm,Rn
inline Ins ld(int rm, int rn, bool p4 = false) {                                              // MOV.x @Rm,Rn
  Ins i; i.cls = LS; i.dst = rn; i.src[0] = rm; i.load = true; i.p4 = p4; return i;
}
inline Ins ldpc(int rn) { Ins i; i.cls = LS; i.dst = rn; i.load = true; return i; }           // MOV.x @(d,PC),Rn
inline Ins st(int rm, int rn, bool p4 = false) {                                              // MOV.x Rm,@Rn
  Ins i; i.cls = LS; i.src[0] = rm; i.src[1] = rn; i.wr = true; i.p4 = p4; return i;
}
inline Ins st_pteh(int rm, int rn) { Ins i = st(rm, rn, true); i.ma_n = 4; return i; }       // MOV.L Rm,@Rn to PTEH

struct Sched {
  // results, per instruction (-1: did not happen within the simulated cycles)
  std::vector<int> fetch, issue, ex_last, ma1, ma_last;
  // per cycle
  std::vector<int> ev13, ev14;
  int cycles = 0;
};

inline bool pairable(Cls a, Cls b) { return a == MT || b == MT || a != b; }

inline Sched run(const std::vector<Ins> &p, int iqn = 4, bool fetch_hold = true, int max_cycles = 400) {
  const int n = (int)p.size();
  Sched s;
  s.fetch.assign(n, -1); s.issue.assign(n, -1); s.ex_last.assign(n, -1); s.ma1.assign(n, -1); s.ma_last.assign(n, -1);
  std::deque<int> q;                 // instruction indices in the queue (fetched in an earlier cycle)
  std::vector<int> exs, mas;         // instructions in EX / MA
  int ma_cnt = 0;                    // cycles the MA group has spent in MA, this one included
  int nw = 0;                        // next word to fetch
  const int nwords = (n + 1) / 2;
  for (int t = 0; t < max_cycles; t++) {
    s.ev13.push_back(0); s.ev14.push_back(0);
    // MA bookkeeping for this cycle
    bool p4ma = false;
    int man = 3;
    for (int i : mas) if (p[i].p4) { p4ma = true; man = p[i].ma_n; }
    if (!mas.empty()) {
      ma_cnt++;
      if (ma_cnt == 1) for (int i : mas) s.ma1[i] = t;
    }
    bool mah = p4ma && ma_cnt < man; // MA1, MA2 (MA3 of a PTEH write) of a P4 access
    int pops = 0;
    std::vector<int> issued;
    if (!mah) {
      // ---- D: issue from the two oldest entries
      auto ready = [&](int i) {
        for (int k = 0; k < 2; k++) {
          int r = p[i].src[k];
          if (r < 0) continue;
          for (int j : exs) if (p[j].dst == r && p[j].load) return false;   // a load in EX: latency 2
        }
        return true;
      };
      if (!q.empty() && ready(q[0])) {
        int l = q[0];
        issued.push_back(l);
        if (q.size() >= 2) {
          int f = q[1];
          bool dep = false;
          for (int k = 0; k < 2; k++) dep |= p[l].dst >= 0 && p[f].src[k] == p[l].dst;
          bool tagged = f == 0;      // the redirect target never pairs as a follower
          if (pairable(p[l].cls, p[f].cls) && !dep && !tagged && ready(f)) issued.push_back(f);
        }
      }
      pops = (int)issued.size();
      for (int i : issued) s.issue[i] = t;
      s.ev13[t] = !issued.empty();
      s.ev14[t] = issued.size() == 2;
    }
    // ---- fetch (held by mah when fetch_hold)
    bool can_fetch = !(mah && fetch_hold);
    if (can_fetch && nw < nwords && iqn - ((int)q.size() - pops) >= 2) {
      for (int k = 0; k < 2; k++) if (2 * nw + k < n) s.fetch[2 * nw + k] = t;
      nw++;
    }
    // ---- end of the cycle: the pipeline advances unless held
    if (!mah) {
      for (int i : exs) s.ex_last[i] = t;
      for (int i : mas) s.ma_last[i] = t;
      mas = exs;
      ma_cnt = 0;
      exs = issued;
      for (int k = 0; k < pops; k++) q.pop_front();
    }
    // the word fetched this cycle joins the queue
    if (can_fetch && nw > 0 && s.fetch[2 * (nw - 1)] == t)
      for (int k = 0; k < 2; k++) if (2 * (nw - 1) + k < n) q.push_back(2 * (nw - 1) + k);
    s.cycles = t + 1;
    bool done = true;
    for (int i = 0; i < n; i++) done &= s.ma_last[i] >= 0;
    if (done) break;
  }
  return s;
}

}  // namespace dsched
