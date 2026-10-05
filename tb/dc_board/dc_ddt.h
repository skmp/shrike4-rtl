// dc_ddt.h -- Holly's side of the SH7091's on-demand data transfer (DDT) interface: board phase 2 (SOC.md 3.3.3,
// 8.3 M6; rtl/v1/spec/dmac.md 3.9, 4.1, 7.4; board.md 4.3; hm 14.5 and Appendix H).  Owner WI-BOARD (M6: WI-SOC,
// integration edit).
//
// Holly is not a system-RAM master: every root-bus DMA (maple, G2-DMA, PVR-DMA, GD-DMA) asks DMAC channel 0 for one
// 32-byte single-address unit at a time, and CH2-DMA asks channel 2 directly.  With DcConfig::ddt (board option
// --ddt) the engines do that through the chip's pins instead of the phase-1 RAM backdoor:
//
//   channel 0, one unit (DdtPort::post):
//     Bclk t      DBREQ# low for one Bclk (never again until BAVL#: hm 14.5.2 note 18)
//     t + k       the chip's BAVL# low (the BSC released the data bus when its current cycle ended)
//     t + k + 2   the DTR on D63:0 (xb_rdata) with TR# low -- two Bclk after BAVL# (hm figure H.3): SZ 100 (32 B),
//                 R/W, ID 00, MD 01, COUNT 1, the unit's RAM address ([HW] dmac1 S1, tools/hw/board/ddtprobe: every
//                 Holly channel-0 DMA; DcBoard::holly_dtr)
//     then        the DMAC's unit: an area-3 cycle with DACK (dack_id 0).  R/W 0 (memory -> Holly): a burst read whose
//                 beats the board hands to the engine (it takes them off the data bus); R/W 1 (Holly -> memory): a burst
//                 write whose data the board drives on xb_rdata at each beat (SOC.md 3.3.3 [P]) and its SDRAM stores
//     end         after the unit's 32 bytes: the next unit may start (the channel-0 request flag refuses a DTR until
//                 its unit ends, hm H.2: Holly waits for it)
//   channel 2 (CH2-DMA, SbCore): DBREQ# and TR# low in the same Bclk = one direct request (figure H.22, note 14), at
//     most one per Bclk, never inside a channel-0 DBREQ .. DTR window (the pins are the same).
// The pace between units is the engine's (Holly's device: G2 bus, texture path, maple bus), given as the earliest
// time of each unit's DBREQ#; the port runs one unit at a time, the earliest due first (posting order among equal
// ones) [P].
// Protocol checks (a violation stops the board, Status::Error): a chip bus cycle start or data beat in the Bclk after
// BAVL# or in the DTR Bclk (the data bus belongs to Holly then); a DACK beat for channel 0 with no unit in flight, or in
// the other direction than its DTR.  TDACK# / ID in each DACK read beat's Bclk are counted (tdack_rd_ok / _bad for
// channel 0, tdack_rd2_ok / _bad for channel 2's CH2-DMA beats, M6-fix).  (M6-fix) A DACK write beat's TDACK# (channel
// 0, R/W 1: Holly drives the data) comes with the chip's data pull, before the beat (v1_dmac: "for a write the beat's
// data request" [P]): every TDACK# Bclk that carries no DACK read beat is a pull of its ID's channel, paired in order
// with that channel's next write beat, which must follow it by WR_K_MIN..WR_K_MAX Bclk (tdack_wr_ok / _bad); a pull no
// beat takes within WR_K_MAX is counted (tdack_orphan).  soc_tb prints them ("soc_tb: DDT TDACK#:"); the M6 checkers
// fail any bad or orphan count.
// (M6-fix) BAVL# is taken as a grant only when it falls after the DBREQ# (low in this Bclk, high in the one before)
// and stays low for exactly that Bclk -- the DMAC's BAVL# is a one-Bclk pulse (v1_dmac_ddt bavl_q).  With DMAOR.DDT
// = 0 the shared pins are DRAK0 / DACK0 at CHCR0.RL / AL (SOC.md 3.3.3): after a manual reset (SOC.md 2.3 zeroes DMAOR
// and CHCR: RL = AL = 0, active high) or while software keeps DDT cleared they idle low, which Holly's side would
// read as BAVL# / TDACK# asserted.  A BAVL# already low when DBREQ# goes out is no grant (bavl_held counts those Bclk),
// nor is one still low in the Bclk after it fell (bavl_false: DDT cleared while the DBREQ# waited; the DBREQ# is spent
// and the board waits for a real grant) [P: whether the console's Holly is edge- or level-sensitive on BAVL# is not
// known].  M6-verify F2: the level rule turned a WDT manual reset during a PVR-DMA into "a chip bus cycle in the Bclk
// after BAVL#".  TDACK# Bclk while BAVL# is low two Bclk running (the DDT = 0 level) are not pulls.
// A unit the chip never serves (AE / NMIF drop DDT requests, DDT switched off, a reset) is given up after
// `serve_timeout` Icyc with a warning [P].
#pragma once
#include <deque>
#include "dc_dev.h"

namespace dcb {

struct DdtUnit {
  bool mem_write = false;            // R/W: 1 = Holly -> memory (DAR0), 0 = memory -> Holly (SAR0)
  uint32_t addr = 0;                 // the unit's system-RAM address (32-byte aligned, 29-bit physical)
  uint64_t not_before = 0;           // the engine's pace: DBREQ# no earlier (board time, Icyc)
  const char *who = "";
  // R/W 0: each read beat's data (8 bytes at byte offset `off` of the unit, lanes `be`)
  std::function<void(uint32_t off, uint64_t data, uint8_t be)> sink;
  // R/W 1: the 8 bytes Holly drives for byte offset `off` of the unit
  std::function<uint64_t(uint32_t off)> source;
  std::function<void()> issued;      // when its DTR is on the bus (an engine's end waits for its last one)
  std::function<void(bool served)> done;     // after the unit's last beat (served) or its time-out (not served)
  std::function<bool()> live;        // still wanted when its turn comes (an aborted engine's units are dropped)
};

struct DdtPort {
  DcCore *c = nullptr;
  enum St { IDLE, WAIT_BAVL, TURN, DTR, XFER } st = IDLE;
  std::deque<DdtUnit> q;
  DdtUnit cur;
  uint64_t dtr_word = 0, t_state = 0, t_state_bavl = 0, stray_until = 0;
  unsigned bytes = 0;
  uint64_t serve_timeout = 400000;   // Icyc from the DTR to the unit's end before Holly gives up [P]
  // statistics
  uint64_t dtrs = 0, units_rd = 0, units_wr = 0, timeouts = 0, ch2_reqs = 0, wait_bavl_icyc = 0;
  uint64_t tdack_rd_ok = 0, tdack_rd_bad = 0, strays = 0;
  // (M6-fix) TDACK# of channel 2's read beats, of channel 0's write beats (paired with the chip's data pulls), pulls
  // without a beat; BAVL# low without a grant (DDT = 0 pin levels, see the head)
  uint64_t tdack_rd2_ok = 0, tdack_rd2_bad = 0, tdack_wr_ok = 0, tdack_wr_bad = 0, tdack_orphan = 0;
  uint64_t wr_k_min = ~0ull, wr_k_max = 0;       // the pull-to-beat distances seen (Bclk)
  uint64_t wr_k_bmin[4] = {~0ull, ~0ull, ~0ull, ~0ull}, wr_k_bmax[4] = {};   // the same per beat of the unit (0..3)
  uint64_t bavl_held = 0, bavl_false = 0;
  bool bavl_last = true, pin_bavl_last = true;   // BAVL# in the previous Bclk (the grant edge; the DDT = 0 level)
  uint64_t bclk_no = 0;
  std::deque<uint64_t> pulls[4];                 // per ID: the Bclk of each TDACK# pull not yet paired with its beat
  bool error = false; std::string error_msg;
  std::vector<std::string> timeout_log;          // the first time-outs: when, which unit, since when (soc_tb prints them)
  uint32_t cpu_pc = 0, dtr_pc = 0;               // the testbench's last retired PC (soc_tb sets it), at the DTR
  // planted bugs (dc_board_test --plant / soc_tb --plant): the DTR one Bclk late, R/W inverted; (M6-fix) BAVL# taken as
  // a level (M6's rule: DDT = 0's DRAK0 idle level is a grant)
  bool plant_late = false, plant_rw = false, late_held = false, plant_bavl_level = false;

  // Holly reset (SB_SFRES): every unit dropped; a chip unit already committed may still run its DACK beats
  void reset() { st = IDLE; q.clear(); cur = DdtUnit(); bytes = 0; stray_until = c->now + serve_timeout; }
  // (M6-fix) the write beat's TDACK# window: the chip raises TDACK# with the BSC's data pull (v1_dmac_xfer dack_v on
  // bs.wd_req); v1_bsc pulls a write's beats one per Bcyc from the Bcyc after its accept and its SDRAM sequencer puts
  // them on the bus one per Bcyc from the WRIT, so all four beats of a unit share one distance, 3..7 Bclk with the
  // SDRAM commands ahead of the WRIT (measured on the chip: ddtx's 98 and ddtmix's 358 write units, every beat of a
  // unit at its first beat's distance) [P: the console's TDACK# position for Holly -> memory units is not measured]
  static constexpr uint64_t WR_K_MIN = 3, WR_K_MAX = 7;
  uint64_t unit_k = 0;                           // the distance of the current write unit's first beat
  // (M6-fix) once per Bclk, before this Bclk's beat is handled: the chip's TDACK# / ID / BAVL# pins; rd_beat: a DACK read
  // beat is on the bus in this Bclk (its TDACK# is that beat's, checked by beat / rd2)
  void pins(bool tdack_l, uint8_t id, bool rd_beat, bool bavl_n) {
    bclk_no++;
    const bool ddt_off = !bavl_n && !pin_bavl_last;       // BAVL# low two Bclk running: DDT = 0 (DRAK0's level)
    pin_bavl_last = bavl_n;
    if (ddt_off) {                                          // the last Bclk's TDACK# was that level too, not a pull
      for (auto &p : pulls) while (!p.empty() && p.back() + 1 >= bclk_no) p.pop_back();
      return;
    }
    for (auto &p : pulls) while (!p.empty() && bclk_no - p.front() > WR_K_MAX) { tdack_orphan++; p.pop_front(); }
    if (tdack_l && !rd_beat) pulls[id & 3].push_back(bclk_no);
  }
  // a channel-2 (CH2-DMA) read beat in this Bclk: TDACK# low with ID 2
  void rd2(bool tdack_ok) { (tdack_ok ? tdack_rd2_ok : tdack_rd2_bad)++; }
  // a write beat of channel ch in this Bclk (beat `bi` of its unit; `first`: the unit's first beat): pair it with that
  // channel's oldest pull; a later beat of the unit must be at the first one's distance
  void wr_pull(int ch, unsigned bi, bool first) {
    auto &p = pulls[ch & 3];
    if (p.empty()) { tdack_wr_bad++; return; }
    const uint64_t k = bclk_no - p.front();
    p.pop_front();
    if (first) unit_k = k;
    else if (k != unit_k) { tdack_wr_bad++; return; }
    if (k < wr_k_min) wr_k_min = k;
    if (k > wr_k_max) wr_k_max = k;
    if (k < wr_k_bmin[bi & 3]) wr_k_bmin[bi & 3] = k;
    if (k > wr_k_bmax[bi & 3]) wr_k_bmax[bi & 3] = k;
    (k >= WR_K_MIN && k <= WR_K_MAX ? tdack_wr_ok : tdack_wr_bad)++;
  }
  void post(DdtUnit u) { q.push_back(std::move(u)); }
  // the next unit to start: the earliest `not_before` that has come, posting order among equal ones (each engine posts
  // its own units in order; a maple response posted ahead with a later time does not block another engine's unit)
  int pick() {
    int best = -1;
    for (size_t i = 0; i < q.size(); i++) {
      if (q[i].live && !q[i].live()) continue;
      if (q[i].not_before > c->now) continue;
      if (best < 0 || q[i].not_before < q[(size_t)best].not_before) best = (int)i;
    }
    // drop the dead ones
    for (size_t i = 0; i < q.size();) {
      if (q[i].live && !q[i].live()) { if ((int)i < best) best--; q.erase(q.begin() + (long)i); } else i++;
    }
    return best;
  }
  bool idle() const { return st == IDLE && q.empty(); }
  // may a CH2-DMA direct request (DBREQ# + TR#) go out in this Bclk?  Not inside a channel-0 handshake
  bool ch2_ok() const { return st == IDLE || st == XFER; }
  static uint64_t dtr(bool mem_write, uint32_t addr) {
    return (4ull << 61) | ((uint64_t)(mem_write ? 1 : 0) << 60) | (0ull << 58) | (1ull << 56) | (1ull << 48) | addr;
  }
  void fail(const std::string &m) { if (!error) { error = true; error_msg = m; c->log("DDT protocol error: %s", m.c_str()); } }

  // One Bclk, after the chip's outputs of this Bclk were sampled (bavl_n: BAVL#; xb_busy: a cycle start or a data beat
  // on the bus in this Bclk).  Drives *dbreq_n / *tr_n and, in the DTR Bclk, *rdata (D63:0).  Returns true when this
  // Bclk's pins are a channel-0 handshake (a CH2-DMA direct request must not share them).
  bool bclk(bool bavl_n, bool xb_busy, bool *dbreq_n, bool *tr_n, uint64_t *rdata, bool *drove_d) {
    *drove_d = false;
    const bool bavl_fall = !bavl_n && bavl_last;   // (M6-fix) BAVL# fell in this Bclk
    bavl_last = bavl_n;
    switch (st) {
    case IDLE: {
      const int k = q.empty() ? -1 : pick();
      if (k >= 0) {
        cur = std::move(q[(size_t)k]); q.erase(q.begin() + k); bytes = 0;
        if (plant_rw) cur.mem_write = !cur.mem_write;
        dtr_word = dtr(cur.mem_write, cur.addr & 0x1FFFFFE0u);
        *dbreq_n = false; st = WAIT_BAVL; t_state = c->now;
        return true;
      }
      return false;
    }
    case WAIT_BAVL:
      if (bavl_fall || (plant_bavl_level && !bavl_n)) { st = TURN; wait_bavl_icyc += c->now - t_state; t_state_bavl = t_state; t_state = c->now; }
      else {
        if (!bavl_n) { if (bavl_held++ == 0) c->warn("DDT: BAVL# low with no falling edge after %s's DBREQ#: not a "
                                                     "grant (DMAOR.DDT = 0: the pins are DRAK0 / DACK0)", cur.who); }
        if (c->now - t_state > serve_timeout) {
          timeouts++; c->warn("DDT: no BAVL# for %s's DBREQ# after %llu Icyc: unit given up", cur.who,
                              (unsigned long long)(c->now - t_state));
          note_timeout(bavl_n ? "no BAVL# for its DBREQ#" : "no BAVL# for its DBREQ# (BAVL# held low: DMAOR.DDT = 0)");
          finish(false);
        }
      }
      return true;
    case TURN:                                   // BAVL# + 1: the data bus turns round to Holly
      if (!bavl_n && !plant_bavl_level) {        // (M6-fix) still low: no one-Bclk grant but DDT = 0's DRAK0 level
        if (bavl_false++ == 0) c->warn("DDT: BAVL# low for more than one Bclk after %s's DBREQ#: not a grant (DMAOR.DDT "
                                       "cleared while it waited)", cur.who);
        st = WAIT_BAVL; t_state = t_state_bavl;
        return true;
      }
      if (xb_busy) fail("a chip bus cycle in the Bclk after BAVL# (the data bus is Holly's)");
      if (plant_late && !late_held) { late_held = true; return true; }   // planted: every DTR one Bclk late
      late_held = false; st = DTR;
      return true;
    case DTR:                                    // BAVL# + 2: the DTR on D63:0 with TR#
      if (xb_busy) fail("a chip bus cycle in the DTR Bclk (the data bus is Holly's)");
      *tr_n = false; *rdata = dtr_word; *drove_d = true; dtrs++;
      st = XFER; t_state = c->now; dtr_pc = cpu_pc;
      if (cur.issued) cur.issued();
      return true;
    case XFER:
      if (c->now - t_state > serve_timeout) {
        timeouts++; c->warn("DDT: %s's unit at %08X not served %llu Icyc after its DTR: given up", cur.who, cur.addr,
                            (unsigned long long)(c->now - t_state));
        note_timeout("the chip never ran the unit its DTR asked for");
        finish(false);
      }
      return false;
    }
    return false;
  }
  // a DACK beat of the chip's channel-0 unit (area 3, dack_id 0), at its completion.  Read beats: `d` is the memory's
  // data; write beats: returns Holly's data for the beat.  tdack_ok: TDACK# low with ID 00 in this Bclk.
  uint64_t beat(const Beat &b, uint64_t d, bool tdack_ok) {
    if (st != XFER) {
      if (c->now < stray_until) { strays++; return 0; }       // a unit Holly gave up on (a reset, a time-out)
      fail("a channel-0 DACK beat with no DDT unit in flight"); return 0;
    }
    if (b.we != cur.mem_write) fail("a channel-0 DACK beat in the other direction than the DTR's R/W");
    const uint32_t off = b.baddr & 0x18u;
    unsigned n = 0; for (int k = 0; k < 8; k++) n += (b.be >> k) & 1;
    uint64_t r = 0;
    if (!b.we) {
      if (!tdack_ok) tdack_rd_bad++; else tdack_rd_ok++;
      if (cur.sink) cur.sink(off, d, b.be);
    } else {
      (void)tdack_ok;                                     // (M6-fix) a write beat's TDACK# is its pull's (wr_pull)
      wr_pull(0, off >> 3, bytes == 0);
      r = cur.source ? cur.source(off) : 0;
    }
    bytes += n;
    if (bytes >= 32) finish(true);
    return r;
  }
  void note_timeout(const char *why) {
    if (timeout_log.size() >= 8) return;
    char m[200];
    snprintf(m, sizeof m, "Icyc %llu: %s unit %08X (R/W %d), %s since Icyc %llu (the CPU's last retired PC then %08X)",
             (unsigned long long)c->now, cur.who, cur.addr, cur.mem_write ? 1 : 0, why, (unsigned long long)t_state,
             st == XFER ? dtr_pc : cpu_pc);
    timeout_log.push_back(m);
  }
  void finish(bool served) {
    DdtUnit u = std::move(cur); cur = DdtUnit(); st = IDLE; bytes = 0;
    if (!served) stray_until = c->now + serve_timeout;
    if (served) (u.mem_write ? units_wr : units_rd)++;
    if (u.done) u.done(served);
  }
};

}  // namespace dcb
