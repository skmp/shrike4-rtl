// dmac_ref.h -- functional C++ model of the SH7091 DMAC (rtl/v1/spec/dmac.md; SOC.md 6.1).  Owner: WI-DMAC.
//
// A P4Slave (tb/sh4_p4if.h) for the register block 0xFFA00000 (P4B_DMAC), plus a unit-granular transfer engine that
// moves data through a DmacBus.  No timing: one call of step() is one transfer unit (the RTL's commit + counter
// update).  The same rules as rtl/v1/v1_dmac*.sv, stated once here:
//   * registers: 32-bit access only (other sizes read 0 / write ignored); CHCR masks FF0FFFF5 (ch 0 / 1),
//     FF0AFFF5 (ch 2 / 3); DMATCR 23:0; DMAOR DDT PR AE NMIF DME.  TE / AE / NMIF: never set by the CPU; a write of 0
//     clears one only after a CPU read returned it as 1 (arm bit, cleared by any CPU write to that register).
//     DDT mode: a CPU write to SAR0 / DAR0 / DMATCR0 loads 0, a write to CHCR0 is dropped.
//   * enable = DE & DME & !TE & !AE & !NMIF & a valid code (RS not 0001 / 0111 / 1111, TS <= 100, SM / DM != 11 where
//     used) & running & not in standby.  running = dmac_run (set_running; 0 with MSTP4, in deep sleep and standby).
//   * levels: DMTEn = TE & IE, DMAE = AE, masked by STBCR.MSTP4 alone (set_mstp); deep sleep keeps them (SOC.md 2.4).
//   * class A: DE & DME & a used address register misaligned for TS -> AE (checked before any unit, and whenever the
//     registers change).  Class B: the leg's address does not fit its route (area 7 on an external leg; not 0x1F, or a
//     32-byte unit, on an on-chip leg; any area 7 in single address; no module behind an on-chip address): no access,
//     the unit still completes (junk data = the last CPU write to a DMAC register as of the unit's commit), AE at its
//     end.
//   * priority: PR 00 0>1>2>3, 01 0>2>3>1, 10 2>0>1>3, 11 round robin (top <- ch + 1 after every unit; an arbitration
//     with no candidate resets top to 0).
//   * requests: auto = always; on-chip = the module's level (TMU in burst: one request runs the whole count); external
//     normal mode (ch 0 / 1): a level or an edge event; DDT: the channel-0 flag and the channel 1-3 queues, one request
//     per unit except edge burst (DS = TM = 1: one request runs the whole count; ch0's flag stays set until it ends,
//     and clearing the flag with the note-16 DTR ends that request too).  A queue entry is consumed at its unit's
//     commit; the channel-0 flag is put in service at the commit (srv0: no longer a request) and clears at that unit's
//     end, so a DTR during the unit is refused (H.2, note 9); an initialisation DTR is also refused while a channel-0
//     unit is in flight.  AE / NMIF drop every DDT request (flag, in-service state, queues) and refuse new ones while
//     set (hm 14.3.6, spec 3.6), like the RTL.
//   * DDT DTR decode as hm 14.5.2 note 8 / H.1 (see dtr()).
//   * a unit's context (CHCR, the leg addresses, the class-B junk) is latched at its commit; the counter update at its
//     end applies the latched size / modes to the registers as they are then.
// Lockstep use (the unit testbench): commit(ch) / finish() split the unit at the RTL's commit and update points, and
// cand() exposes the candidate set so the RTL's pick can be checked against pick(cand).  Stand-alone use (Sh4Sys,
// tb/bm/dmac/run_iss.cpp): tick_icyc() runs step(), one whole unit per nominal 46 Icyc.
#pragma once
#include <cstdint>
#include <cstring>
#include "sh4_p4if.h"

// What the DMAC's legs touch.  pa is physical (bits 28:0).
struct DmacBus {
  virtual ~DmacBus() {}
  // an external leg through the BSC: bytes = 1 2 4 8 32, naturally aligned; buf little endian
  virtual void ext_read(uint32_t pa, int bytes, uint8_t *buf) = 0;
  virtual void ext_write(uint32_t pa, int bytes, const uint8_t *buf) = 0;
  // an on-chip leg through the P4 bridge (pa = 0x1Fxxxxxx), size 0 byte 1 word 2 long; false = no module (err)
  virtual bool onchip_read(uint32_t pa, int size, uint32_t &v) = 0;
  virtual bool onchip_write(uint32_t pa, int size, uint32_t v) = 0;
  // a single-address unit: memory <-> the DACK / TDACK device (we: device -> memory); the device moves the data
  virtual void dack_unit(int ch, bool we, uint32_t pa, int bytes) = 0;
  // the TMU's input-capture request is served (per unit in cycle steal, at the last unit in burst)
  virtual void dack_ic() {}
};

struct DmacRef : P4Slave {
  // ---- architectural state
  uint32_t sar[4] = {0, 0, 0, 0}, dar[4] = {0, 0, 0, 0}, tcr[4] = {0, 0, 0, 0}, chcr[4] = {0, 0, 0, 0};
  bool ddt = false, dme = false, ae = false, nmif = false;
  int pr = 0;
  bool te_arm[4] = {false, false, false, false}, ae_arm = false, nmif_arm = false;
  uint32_t ib_data = 0;                         // last CPU write data (class-B junk)
  // requests
  bool flag0 = false;                           // DDT channel-0 request flag
  bool srv0 = false;                            // the flag's unit is in flight (committed, not finished)
  int q[4] = {0, 0, 0, 0};                      // DDT queues (1-3)
  int last_ch = 0;
  bool last_v = false;
  bool hold[4] = {false, false, false, false};  // a request that runs the whole count
  bool edge[2] = {false, false};                // normal-mode DREQ edge latches
  bool level[2] = {false, false};               // normal-mode DREQ levels
  uint32_t onchip = 0;                          // {sci_tx, sci_rx, scif_tx, scif_rx, tmu_ic} (bit 4..0)
  int top = 0;                                  // round-robin head
  bool running = true, stby = false;           // running = v1_cpg's dmac_run (0 with MSTP4, deep sleep, standby)
  bool mstp = false;                            // STBCR.MSTP4: masks DMTE / DMAE (SOC.md 2.4; deep sleep keeps them)
  // the unit in flight (commit .. finish)
  int u_ch = -1;
  uint32_t u_ib = 0;                            // the class-B junk, latched at the commit
  uint32_t u_cc = 0, u_sar = 0, u_dar = 0;      // the unit's CHCR and leg addresses, latched at the commit
  DmacBus *bus = nullptr;
  uint64_t units = 0;

  static const uint32_t WM01 = 0xFF0FFFF5u, WM23 = 0xFF0AFFF5u;

  // ---- field helpers
  static int ts_of(uint32_t c) { return (c >> 4) & 7; }
  static int rs_of(uint32_t c) { return (c >> 8) & 15; }
  static int sm_of(uint32_t c) { return (c >> 12) & 3; }
  static int dm_of(uint32_t c) { return (c >> 14) & 3; }
  static bool tm_of(uint32_t c) { return (c >> 7) & 1; }
  static bool ds_of(uint32_t c) { return (c >> 19) & 1; }
  static bool te_of(uint32_t c) { return (c >> 1) & 1; }
  static bool de_of(uint32_t c) { return c & 1; }
  static bool ie_of(uint32_t c) { return (c >> 2) & 1; }
  static int unit_bytes(int ts) { static const int b[8] = {8, 1, 2, 4, 32, 0, 0, 0}; return b[ts & 7]; }
  static uint32_t amask(int ts) { static const uint32_t m[8] = {7, 0, 1, 3, 31, 0, 0, 0}; return m[ts & 7]; }
  static bool rs_prohib(int rs) { return rs == 1 || rs == 7 || rs == 15; }
  static bool rs_ext(int rs) { return rs == 0 || rs == 2 || rs == 3; }
  static bool rs_auto(int rs) { return rs >= 4 && rs <= 6; }
  static bool rs_onchip(int rs) { return rs >= 8 && rs <= 14; }
  static bool rs_tmu(int rs) { return rs >= 12 && rs <= 14; }
  static bool rs_single(int rs) { return rs == 2 || rs == 3; }
  static bool rs_src_onchip(int rs) { return rs == 6 || rs == 9 || rs == 11 || rs == 14; }
  static bool rs_dst_onchip(int rs) { return rs == 5 || rs == 8 || rs == 10 || rs == 13; }
  static bool uses_sar(int rs) { return rs != 3; }
  static bool uses_dar(int rs) { return rs != 2; }
  static bool a_onchip(uint32_t a) { return ((a >> 24) & 0x1F) == 0x1F; }
  static bool a_area7(uint32_t a) { return ((a >> 26) & 7) == 7; }

  bool code_ok(int n) const {
    uint32_t c = chcr[n];
    int rs = rs_of(c), ts = ts_of(c);
    return !rs_prohib(rs) && ts <= 4 && (!uses_sar(rs) || sm_of(c) != 3) && (!uses_dar(rs) || dm_of(c) != 3);
  }
  bool misaligned(int n) const {
    uint32_t c = chcr[n];
    int rs = rs_of(c), ts = ts_of(c);
    if (!de_of(c) || !dme || ts > 4) return false;
    uint32_t m = amask(ts);
    return (uses_sar(rs) && (sar[n] & m)) || (uses_dar(rs) && (dar[n] & m));
  }
  bool en(int n) const {
    uint32_t c = chcr[n];
    return de_of(c) && dme && !te_of(c) && !ae && !nmif && code_ok(n) && running && !stby;
  }
  bool dmte(int n) const { return !mstp && te_of(chcr[n]) && ie_of(chcr[n]); }
  bool dmae() const { return !mstp && ae; }

  // "one request runs the whole count"
  bool whole(int n) const {
    uint32_t c = chcr[n];
    int rs = rs_of(c);
    bool ext_n = !ddt && rs_ext(rs) && n < 2, ext_d = ddt && rs_ext(rs);
    return tm_of(c) && ((rs_onchip(rs) && rs_tmu(rs)) || ((ext_n || ext_d) && ds_of(c)));
  }
  bool onchip_level(int rs) const {
    switch (rs) {
    case 8: return (onchip >> 4) & 1;
    case 9: return (onchip >> 3) & 1;
    case 10: return (onchip >> 2) & 1;
    case 11: return (onchip >> 1) & 1;
    default: return onchip & 1;
    }
  }
  bool rq(int n) const {
    int rs = rs_of(chcr[n]);
    if (rs_auto(rs)) return true;
    if (rs_onchip(rs)) return hold[n] || onchip_level(rs);
    if (rs_ext(rs) && ddt) return hold[n] || (n == 0 ? (flag0 && !srv0) : q[n] > 0);
    if (rs_ext(rs) && n < 2) return hold[n] || (ds_of(chcr[n]) ? edge[n] : level[n]);
    return false;
  }
  unsigned cand() const {
    for (int n = 0; n < 4; n++) if (misaligned(n)) return 0;
    unsigned c = 0;
    for (int n = 0; n < 4; n++) if (en(n) && rq(n)) c |= 1u << n;
    return c;
  }
  int pri_ch(int k) const {
    static const int o[3][4] = {{0, 1, 2, 3}, {0, 2, 3, 1}, {2, 0, 1, 3}};
    return pr == 3 ? (top + k) & 3 : o[pr][k];
  }
  int pick(unsigned c) const {
    for (int k = 0; k < 4; k++) if (c & (1u << pri_ch(k))) return pri_ch(k);
    return -1;
  }

  // ---- state maintenance, once per Bclk before the arbiter (the RTL evaluates both on the registers of the Bclk):
  // class A (AE for a misaligned enabled channel; cand() is already empty while one exists), and the end of the
  // whole-count holds of channels that stopped (channel 0's DDT flag with them), and AE / NMIF dropping edge latches
  // and (while the DMAC runs: the DDT module is Bclk state) every DDT request.  The RTL drops the DDT requests in the
  // Bclk AE / NMIF is being set, so each place that sets them drops them at once too.
  void drop_ddt() { flag0 = false; srv0 = false; q[1] = q[2] = q[3] = 0; }
  void class_a() { for (int n = 0; n < 4; n++) if (misaligned(n)) { ae = true; drop_ddt(); } }
  void settle() {
    for (int n = 0; n < 4; n++)
      if (hold[n] && (!en(n) || ae || nmif || (n == 0 && ddt && rs_ext(rs_of(chcr[0])) && !flag0))) {
        hold[n] = false;
        if (n == 0 && ddt && rs_ext(rs_of(chcr[0]))) flag0 = false;
      }
    if (ae || nmif) { edge[0] = edge[1] = false; if (running) drop_ddt(); }
  }
  void bce_pre() { if (running) class_a(); settle(); }

  // ---- P4Slave: the register block, off = a[27:0] as the bridge strobes it (only a[17:0] decodes)
  static bool decode(uint32_t off, int &idx) {
    off &= 0x3FFFF;
    if (off >= 0x44) return false;
    idx = (int)(off >> 2);
    return true;
  }
  uint32_t regval(int idx) const {
    if (idx == 16)
      return (ddt ? 0x8000u : 0) | ((uint32_t)pr << 8) | (ae ? 4u : 0) | (nmif ? 2u : 0) | (dme ? 1u : 0);
    int ch = idx >> 2;
    switch (idx & 3) {
    case 0: return sar[ch];
    case 1: return dar[ch];
    case 2: return tcr[ch];
    default: return chcr[ch];
    }
  }
  bool rd(uint32_t off, int size, uint32_t &v, bool dma) override {
    int idx;
    v = 0;
    if (!decode(off, idx)) return false;
    if (size != 2 || dma) return true;
    v = regval(idx);
    if (!running) return true;
    if (idx == 16) { if (ae) ae_arm = true; if (nmif) nmif_arm = true; }
    else if ((idx & 3) == 3 && te_of(chcr[idx >> 2])) te_arm[idx >> 2] = true;
    return true;
  }
  bool wr(uint32_t off, int size, uint32_t v, bool dma) override {
    int idx;
    if (!decode(off, idx)) return false;
    if (size != 2 || dma || !running) return true;
    ib_data = v;
    if (idx == 16) {
      ddt = (v >> 15) & 1; pr = (v >> 8) & 3; dme = v & 1;
      if (pr != 3) top = 0;
      if (ae_arm && !(v & 4)) ae = false;
      if (nmif_arm && !(v & 2)) nmif = false;
      ae_arm = nmif_arm = false;
    } else {
      int ch = idx >> 2;
      bool masked0 = ddt && ch == 0;
      switch (idx & 3) {
      case 0: sar[ch] = masked0 ? 0 : v; break;
      case 1: dar[ch] = masked0 ? 0 : v; break;
      case 2: tcr[ch] = masked0 ? 0 : (v & 0xFFFFFF); break;
      default:
        if (!masked0) {
          bool te = te_of(chcr[ch]) && !(te_arm[ch] && !(v & 2));
          chcr[ch] = (v & (ch >= 2 ? WM23 : WM01)) | (te ? 2u : 0);
          te_arm[ch] = false;
          hold[ch] = false;
        }
      }
    }
    return true;
  }
  bool is_volatile(uint32_t off, int size) const override {
    (void)size;
    int idx;
    return decode(off, idx);                    // every register moves under a running channel
  }

  // ---- request inputs
  void set_onchip(uint32_t levels) { onchip = levels & 31; }
  void dreq_level(int ch, bool v) { level[ch & 1] = v; }
  void dreq_edge(int ch) { if (!ae && !nmif) edge[ch & 1] = true; }
  void nmi() { nmif = true; if (running) drop_ddt(); }
  void standby(bool s) { stby = s; if (s) for (int n = 0; n < 4; n++) { chcr[n] &= ~2u; te_arm[n] = false; } }
  void set_running(bool r) { running = r; }
  void set_mstp(bool m) { mstp = m; }
  void reset_por() { *this = DmacRef(bus); }
  void reset_manual() {
    for (int n = 0; n < 4; n++) { chcr[n] = 0; te_arm[n] = false; hold[n] = false; q[n] = 0; }
    ddt = dme = ae = nmif = false; pr = 0; ae_arm = nmif_arm = false; flag0 = srv0 = false; last_v = false; last_ch = 0;
    edge[0] = edge[1] = false; top = 0; u_ch = -1;
  }
  explicit DmacRef(DmacBus *b = nullptr) : bus(b) {}

  // DDT (hm 14.5.2 note 8, H.1, H.2): returns what it did (for traces)
  static uint32_t dtr_chcr0(uint64_t d) {
    int sz = (int)(d >> 61) & 7, rw = (int)(d >> 60) & 1, md = (int)(d >> 56) & 3;
    static const int ts[5] = {1, 2, 3, 0, 4};
    return (md == 1 ? 1u << 19 : 0) | (1u << 14) | (1u << 12) | ((2u | (uint32_t)rw) << 8) | (md != 3 ? 0x80u : 0) |
           ((uint32_t)ts[sz] << 4) | 1u;
  }
  bool halted() const { return ae || nmif; }  // AE / NMIF: the DDT module accepts no request
  void push(int ch) { if (q[ch] < 4 && !halted()) q[ch]++; }
  void dtr(uint64_t d) {
    if (!ddt || !running) return;
    int sz = (int)(d >> 61) & 7, rw = (int)(d >> 60) & 1, id = (int)(d >> 58) & 3, md = (int)(d >> 56) & 3;
    if (id != 0) { push(id); last_ch = id; last_v = true; return; }
    if (md == 0) { if (!flag0 && !halted()) flag0 = true; last_ch = 0; last_v = true; return; }
    if (sz == 7) { if (!flag0) chcr[0] &= ~1u; return; }
    if (md == 2 && sz == 6) { flag0 = srv0 = false; q[1] = q[2] = q[3] = 0; return; }
    if (sz > 4) return;
    if (flag0 || u_ch == 0 || halted()) return;   // refused: the flag is set, a ch0 unit is in flight, AE / NMIF
    chcr[0] = dtr_chcr0(d);
    tcr[0] = (uint32_t)(d >> 48) & 0xFF;
    if (rw) dar[0] = (uint32_t)d; else sar[0] = (uint32_t)d;
    te_arm[0] = false;
    flag0 = true; last_ch = 0; last_v = true;
  }
  void ch2_req() { if (ddt && running) push(2); }
  void tr_repeat() {
    if (!ddt || !running || !last_v) return;
    if (last_ch == 0) { if (!flag0 && !halted()) flag0 = true; } else push(last_ch);
  }

  // ---- the engine
  // accept the request of channel ch (the arbiter's pick): consume it as the RTL does at its commit, and latch the
  // unit's context (mode, size, leg addresses, the class-B junk).  The legs use the latched addresses; the counter
  // update at the unit's end applies the latched size / modes to whatever SAR / DAR / DMATCR hold then (a DTR or a CPU
  // write in between is not undone), as v1_dmac_xfer does.
  void commit(int ch) {
    u_ch = ch;
    u_ib = ib_data;
    u_cc = chcr[ch];
    u_sar = sar[ch];
    u_dar = dar[ch];
    int rs = rs_of(chcr[ch]);
    bool wh = whole(ch);
    if (ddt && rs_ext(rs)) {
      if (!wh) { if (ch == 0) srv0 = true; else if (q[ch] > 0) q[ch]--; }
      else if (!hold[ch] && ch != 0 && q[ch] > 0) q[ch]--;
    }
    if (!ddt && rs_ext(rs) && ch < 2 && ds_of(chcr[ch])) edge[ch] = false;
    if (wh) hold[ch] = true;
  }
  // move the unit and update the channel (the RTL's UPD)
  void finish() {
    int c = u_ch;
    if (c < 0) return;
    u_ch = -1;
    uint32_t cc = u_cc;
    int rs = rs_of(cc), ts = ts_of(cc), bytes = unit_bytes(ts);
    bool err = false;
    if (rs_single(rs)) {
      uint32_t a = rs == 3 ? u_dar : u_sar;
      if (a_area7(a)) err = true;
      else if (bus) bus->dack_unit(c, rs == 3, a & 0x1FFFFFFFu, bytes);
    } else {
      uint8_t buf[32];
      // read leg
      uint32_t a = u_sar;
      bool rd_p4 = rs_src_onchip(rs);
      bool ok = rd_p4 ? (a_onchip(a) && ts != 4) : !a_area7(a);
      if (ok && rd_p4) {
        uint32_t lo = 0, hi = 0;
        int sz = ts == 1 ? 0 : ts == 2 ? 1 : 2;
        if (bus && !bus->onchip_read(a & 0x1FFFFFFFu, sz, lo)) err = true;
        if (ts == 0 && bus && !bus->onchip_read((a & 0x1FFFFFFFu) + 4, 2, hi)) err = true;
        uint64_t v = ((uint64_t)hi << 32) | lo;
        memcpy(buf, &v, 8);
      } else if (ok) {
        if (bus) bus->ext_read(a & 0x1FFFFFFFu, bytes, buf);
      } else {
        err = true;
        for (int k = 0; k < 32; k += 4) memcpy(buf + k, &u_ib, 4);
      }
      // write leg
      uint32_t d = u_dar;
      bool wr_p4 = rs_dst_onchip(rs);
      ok = wr_p4 ? (a_onchip(d) && ts != 4) : !a_area7(d);
      if (ok && wr_p4) {
        uint64_t v;
        memcpy(&v, buf, 8);
        int sz = ts == 1 ? 0 : ts == 2 ? 1 : 2;
        uint32_t lo = (uint32_t)v & (sz == 0 ? 0xFFu : sz == 1 ? 0xFFFFu : 0xFFFFFFFFu);
        if (bus && !bus->onchip_write(d & 0x1FFFFFFFu, sz, lo)) err = true;
        if (ts == 0 && bus && !bus->onchip_write((d & 0x1FFFFFFFu) + 4, 2, (uint32_t)(v >> 32))) err = true;
      } else if (ok) {
        if (bus) bus->ext_write(d & 0x1FFFFFFFu, bytes, buf);
      } else err = true;
    }
    // counters (only the registers the mode uses)
    if (uses_sar(rs)) sar[c] += sm_of(cc) == 1 ? bytes : sm_of(cc) == 2 ? (uint32_t)-bytes : 0;
    if (uses_dar(rs)) dar[c] += dm_of(cc) == 1 ? bytes : dm_of(cc) == 2 ? (uint32_t)-bytes : 0;
    bool te = tcr[c] == 1;
    tcr[c] = (tcr[c] - 1) & 0xFFFFFF;
    if (te) chcr[c] |= 2u;
    if (c == 0 && srv0) flag0 = srv0 = false;   // the channel-0 request ends with its unit
    if (err) { ae = true; if (running) drop_ddt(); }
    if (rs_tmu(rs) && (!tm_of(cc) || te) && bus) bus->dack_ic();
    if (pr == 3) top = (c + 1) & 3;
    units++;
  }
  // round robin: an arbitration that found nothing
  void idle() { top = 0; }
  // one whole unit (stand-alone use): returns the channel, or -1 when nothing can run
  int step() {
    bce_pre();
    int ch = pick(cand());
    if (ch < 0) { idle(); return -1; }
    commit(ch);
    finish();
    return ch;
  }
  // stand-alone time: a nominal 46 Icyc per unit (burst SDRAM -> SDRAM, spec 3.13); not a timing reference
  uint64_t credit = 0;
  void tick_icyc(uint64_t n) override {
    credit += n;
    while (credit >= 46) { credit -= 46; if (step() < 0) { credit = 0; break; } }
  }
};
