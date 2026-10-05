// sh4_dbg.h -- C++ models of the SH7091 debug-side register blocks (rtl/v1/SOC.md 6.1; spec debug-ubc-hudi): the
// UBC, the performance counters (PMCR / PMCTR), the CCN identification slot (PVR and the CCN holes) and the H-UDI
// registers.  Owner: WI-DBG.  Each block is a P4Slave (tb/sh4_p4if.h), seen by its offset a[27:0] as v1_p4bus strobes
// the RTL slave.  Written from the spec, independently of the RTL (rtl/v1/v1_ubc.sv, v1_perfctr.sv, v1_ccn_id.sv,
// v1_hudi.sv); the unit testbenches (tb/v1_ubc_tb.cpp, tb/v1_perfctr_tb.cpp, tb/v1_ccn_id_tb.cpp, tb/v1_hudi_tb.cpp)
// run the RTL against these models cycle by cycle, and tb/bm/debug-ubc-hudi/run_iss.cpp runs them functionally behind
// the ISS (SoC mode).
//
// Two ways to use them:
//   * functional (Sh4Sys, WI-REF): Ubc() / Ubc(-1) has no fetch-side delay; P4Slave::wr applies at once (to the
//     fetch-side copy too, whatever the delay); Pmc::count(code, n) adds events and tick_icyc(n) adds elapsed cycles
//     (event 0x23).  PMCTR reads are volatile (VOLRD-injected).
//   * cycle (the unit testbenches): cycle_mode = true defers a register write to the next clock() (the RTL writes at
//     the end of the strobe cycle); clock() advances one Icyc.  Every query (rd, fetch_tags, op_match, d_match, ctl)
//     returns the value before this cycle's edge, like the RTL's combinational outputs.
#pragma once
#include "sh4_p4if.h"
#include <cstdint>
#include <deque>

namespace dbg {

// ============================================================================================================ UBC
enum UbcSize { SZ_B = 0, SZ_W = 1, SZ_L = 2, SZ_Q = 3 };   // msz_e

struct UbcChan {
  uint32_t bar = 0;
  uint8_t bamr = 0;    // bits 3:0: 3 BAM2, 2 BASM, 1:0 BAM1:0
  uint8_t basr = 0;
  uint16_t bbr = 0;    // bits 6:0: 6 SZ2, 5:4 ID, 3:2 RW, 1:0 SZ1:0
};

// address bits that are NOT compared, from {BAM2, BAM1, BAM0} (hm 20.2.3; 11x reserved: [P] all)
inline uint32_t ubc_bam_mask(uint8_t bamr) {
  switch (((bamr >> 1) & 4) | (bamr & 3)) {
  case 0: return 0;
  case 1: return 0x3FF;
  case 2: return 0xFFF;
  case 4: return 0xFFFF;
  case 5: return 0xFFFFF;
  default: return 0xFFFFFFFFu;
  }
}

// debug-ubc-hudi 4.1, everything but the data compare.  fetch: an instruction fetch of the halfword at va (a word
// read); otherwise an operand access of `size` (msz_e), write when wr.
inline bool ubc_match(const UbcChan &c, bool fetch, bool wr, int size, uint32_t va, uint8_t asid) {
  unsigned id = (c.bbr >> 4) & 3, rw = (c.bbr >> 2) & 3, sz = ((c.bbr >> 4) & 4) | (c.bbr & 3);
  if (id == 0 || rw == 0) return false;                        // channel disabled
  if (!(fetch ? (id & 1) : (id & 2))) return false;            // instruction / operand
  bool rd = fetch || !wr;
  if (!(rd ? (rw & 1) : (rw & 2))) return false;
  int asz = fetch ? SZ_W : size;
  unsigned code = asz == SZ_Q ? 4 : (unsigned)asz + 1;         // B 001, W 010, L 011, Q 100
  if (sz != 0 && sz != code) return false;
  uint32_t cmp = fetch ? 0xFFFFFFFFu : asz == SZ_Q ? 0xFFFFFFF8u : asz == SZ_L ? 0xFFFFFFFCu
               : asz == SZ_W ? 0xFFFFFFFEu : 0xFFFFFFFFu;
  if (((va ^ c.bar) & cmp & ~ubc_bam_mask(c.bamr)) != 0) return false;
  if (!(c.bamr & 4) && c.basr != asid) return false;           // ASID compared unless BASM (with the MMU off too)
  return true;
}

// channel B data compare on the access's right-aligned data (debug-ubc-hudi 4.1): byte replicated into 15:0 [P],
// word 15:0, long 31:0, quad either 32-bit half [M]
inline bool ubc_data(uint32_t bdrb, uint32_t bdmrb, uint64_t d, int size) {
  auto eq = [&](uint32_t v, uint32_t bits) { return ((v ^ bdrb) & ~bdmrb & bits) == 0; };
  switch (size) {
  case SZ_B: { uint32_t b = (uint32_t)d & 0xFF; return eq(b | (b << 8), 0xFFFF); }
  case SZ_W: return eq((uint32_t)d & 0xFFFF, 0xFFFF);
  case SZ_L: return eq((uint32_t)d, 0xFFFFFFFFu);
  default: return eq((uint32_t)d, 0xFFFFFFFFu) || eq((uint32_t)(d >> 32), 0xFFFFFFFFu);
  }
}

// The UBC: block CCN_UBC (offsets 0x14 BASRA / 0x18 BASRB of the CCN window) and block UBC (0xFF200000-0xFF2000FF).
// Offsets are a[27:0]: CCN = 0xF000000 + x (a[23:18] = 0x00), UBC = 0xF200000 + x (a[23:18] = 0x08).  The undriven
// window 0xFF200100- is the bridge's (it returns last_rdata): a hole here.
struct Ubc : P4Slave {
  UbcChan ch[2];                 // [0] = A, [1] = B (the live registers: EX / S compares)
  uint32_t bdrb = 0, bdmrb = 0;
  uint16_t brcr = 0;             // stored bits only (mask 0xC4C9)
  bool a_seen = false;
  // the fetch unit's copy (fetch compares) and the writes on their way to it
  UbcChan fch[2];
  uint16_t fbrcr = 0;
  int if_dly;                    // write lands at the end of cycle W -> copy from W + if_dly + 1; < 0: no copy delay
  bool cycle_mode = false;
  uint64_t now = 0;              // cycle counter (cycle mode)
  struct Pend { uint64_t t; int reg; uint32_t v; };
  std::deque<Pend> fpend;
  bool w_pend = false; int w_reg = 0; uint32_t w_val = 0;   // cycle mode: this cycle's write

  enum { R_NONE, R_BARA, R_BAMRA, R_BBRA, R_BARB, R_BAMRB, R_BBRB, R_BDRB, R_BDMRB, R_BRCR, R_BASRA, R_BASRB };

  // dly: the fetch-side copy delay of cycle mode (v1_ubc IF_DLY; the unit tb passes 3).  The default -1 is the
  // functional model (no delay), and a functional user (cycle_mode = false) never waits for the copy anyway.
  explicit Ubc(int dly = -1) : if_dly(dly) {}

  static bool is_ccn(uint32_t off) { return ((off >> 18) & 0x3F) == 0x00; }
  // the register at off, its size (0 B 1 W 2 L), or R_NONE
  static int decode(uint32_t off, int &rsize) {
    uint32_t o = off & 0xFF;
    if (is_ccn(off)) {
      rsize = 0;
      return o == 0x14 ? R_BASRA : o == 0x18 ? R_BASRB : R_NONE;
    }
    if ((off & 0x3FF00) != 0) return R_NONE;                   // 0xFF200100-: not this slave
    switch (o) {
    case 0x00: rsize = 2; return R_BARA;
    case 0x04: rsize = 0; return R_BAMRA;
    case 0x08: rsize = 1; return R_BBRA;
    case 0x0C: rsize = 2; return R_BARB;
    case 0x10: rsize = 0; return R_BAMRB;
    case 0x14: rsize = 1; return R_BBRB;
    case 0x18: rsize = 2; return R_BDRB;
    case 0x1C: rsize = 2; return R_BDMRB;
    case 0x20: rsize = 1; return R_BRCR;
    default: return R_NONE;
    }
  }
  static uint32_t mask_of(int r) {
    switch (r) {
    case R_BAMRA: case R_BAMRB: return 0x0F;
    case R_BBRA: case R_BBRB: return 0x7F;
    case R_BASRA: case R_BASRB: return 0xFF;
    case R_BRCR: return 0xC4C9;
    default: return 0xFFFFFFFFu;
    }
  }
  uint32_t get(int r) const {
    switch (r) {
    case R_BARA: return ch[0].bar;   case R_BAMRA: return ch[0].bamr; case R_BBRA: return ch[0].bbr;
    case R_BARB: return ch[1].bar;   case R_BAMRB: return ch[1].bamr; case R_BBRB: return ch[1].bbr;
    case R_BDRB: return bdrb;        case R_BDMRB: return bdmrb;      case R_BRCR: return brcr;
    case R_BASRA: return ch[0].basr; case R_BASRB: return ch[1].basr;
    default: return 0;
    }
  }
  static void set_chan(UbcChan *c, uint16_t *br, int r, uint32_t v) {
    switch (r) {
    case R_BARA: c[0].bar = v; break;             case R_BAMRA: c[0].bamr = (uint8_t)v; break;
    case R_BBRA: c[0].bbr = (uint16_t)v; break;   case R_BARB: c[1].bar = v; break;
    case R_BAMRB: c[1].bamr = (uint8_t)v; break;  case R_BBRB: c[1].bbr = (uint16_t)v; break;
    case R_BASRA: c[0].basr = (uint8_t)v; break;  case R_BASRB: c[1].basr = (uint8_t)v; break;
    case R_BRCR: *br = (uint16_t)v; break;
    default: break;
    }
  }
  void apply(int r, uint32_t v) {               // a register write lands (v already masked)
    set_chan(ch, &brcr, r, v);
    if (r == R_BDRB) bdrb = v;
    if (r == R_BDMRB) bdmrb = v;
    if (r == R_BRCR && !(v & 0x0008)) a_seen = false;         // SEQ = 0 clears "A seen" [M]
    if (r == R_BDRB || r == R_BDMRB) return;
    // the fetch-side copy delay exists only in cycle mode (clock() advances `now`); functionally the copy is the
    // register at once, whatever if_dly says
    if (if_dly < 0 || !cycle_mode) set_chan(fch, &fbrcr, r, v);
    else fpend.push_back({now + (uint64_t)if_dly + 1, r, v});
  }
  void sync_copy() {                            // the fetch-side copy as of cycle `now`
    while (!fpend.empty() && fpend.front().t <= now) {
      set_chan(fch, &fbrcr, fpend.front().reg, fpend.front().v);
      fpend.pop_front();
    }
  }

  bool rd(uint32_t off, int size, uint32_t &v, bool) override {
    int rs = -1, r = decode(off, rs);
    v = 0;
    if (r == R_NONE) return false;
    if (size == rs) v = get(r);
    return true;
  }
  bool wr(uint32_t off, int size, uint32_t v, bool) override {
    int rs = -1, r = decode(off, rs);
    if (r == R_NONE) return false;
    if (size != rs) return true;                // wrong size: ignored
    uint32_t m = v & mask_of(r);
    if (cycle_mode) { w_pend = true; w_reg = r; w_val = m; }
    else apply(r, m);
    return true;
  }
  bool is_volatile(uint32_t, int) const override { return false; }

  // ---- the core-side interface
  bool seq() const { return brcr & 0x0008; }
  bool b_live() const { return !seq() || a_seen; }
  bool pcba() const { return fbrcr & 0x0400; }  // from the fetch-side copy [P], like the RTL
  bool pcbb() const { return fbrcr & 0x0040; }
  bool ubde() const { return brcr & 0x0001; }
  bool dbeb() const { return brcr & 0x0080; }
  // a pre-execution instruction break is armed (the fetch-side copy): ID[0], RW[0], PCBx = 0 (v1_ubc ctl_pre_armed)
  bool pre_armed() const {
    auto one = [](const UbcChan &c, bool pcb) { return (c.bbr & 0x10) && (c.bbr & 0x04) && !pcb; };
    return one(fch[0], pcba()) || one(fch[1], pcbb());
  }
  // fetch side: tags {a, b} of the two halfwords of the word at va (bit h = halfword h)
  void fetch_tags(uint32_t va, uint8_t asid, unsigned &a, unsigned &b) {
    sync_copy();
    a = b = 0;
    for (int h = 0; h < 2; h++) {
      uint32_t hv = (va & ~3u) | (uint32_t)(2 * h);
      if (ubc_match(fch[0], true, false, SZ_W, hv, asid)) a |= 1u << h;
      if (ubc_match(fch[1], true, false, SZ_W, hv, asid)) b |= 1u << h;
    }
  }
  // EX side (channel B without the data; with DBEB an access without data never matches B)
  bool op_match(int c, uint32_t va, int size, bool wr, bool nodata, uint8_t asid) const {
    if (!ubc_match(ch[c], false, wr, size, va, asid)) return false;
    return !(c == 1 && dbeb() && nodata);
  }
  // S side: channel B's data compare (0 unless DBEB)
  bool d_match(uint64_t d, int size) const { return dbeb() && ubc_data(bdrb, bdmrb, d, size); }
  // break points reached (hit_a / hit_b as the core reports them); returns the events {0x19, 0x1A}
  void hit(bool ha, bool hb, bool &ev_a, bool &ev_b) {
    bool live = b_live(), s = seq();
    ev_a = ha;
    ev_b = hb && live;
    if (hb && live) { brcr |= 0x4000; if (s) a_seen = false; }
    else if (ha && s) a_seen = true;
    if (ha && !s) brcr |= 0x8000;
  }
  // cycle mode: the end of the cycle.  Order as the RTL's edge: the register write, then the hits (a hardware CMF set
  // wins), then time advances (the copy sees writes whose time has come)
  void clock(bool run, bool ha, bool hb, bool &ev_a, bool &ev_b) {
    bool live = b_live(), s = seq();            // the hits see the state before this edge
    ha = ha && run; hb = hb && run;
    ev_a = ha;
    ev_b = hb && live;
    const bool wseq0 = w_pend && w_reg == R_BRCR && !(w_val & 0x0008);
    if (w_pend) { apply(w_reg, w_val); w_pend = false; }
    if (hb && live) { brcr |= 0x4000; if (s) a_seen = false; }
    else if (ha && s) a_seen = true;
    if (ha && !s) brcr |= 0x8000;
    if (wseq0) a_seen = false;                  // a SEQ = 0 write also cancels an A half of its own cycle [P]
    now++;
  }
  void reset_por() {
    ch[0] = ch[1] = fch[0] = fch[1] = UbcChan();
    bdrb = bdmrb = 0; brcr = fbrcr = 0; a_seen = false; fpend.clear(); w_pend = false;
  }

  // ---- functional break points: the semantics of Sh4Env::ubc_pre / ubc_post / ubc_oper (tb/sh4_iss.h SoC mode).
  // Each call is that break point being reached by an executed instruction: it sets CMFx / "A seen", counts the
  // events 0x19 / 0x1A (n_ev_a / n_ev_b, for Pmc::count) and returns the break request.  The ISS applies SR.BL, the
  // delay-slot rules and the priorities.  asid = PTEH.ASID.
  uint64_t n_ev_a = 0, n_ev_b = 0;
  bool reach(bool a, bool b) {                  // this break point matched channel A / B
    bool live = b_live(), s = seq();
    n_ev_a += a;
    n_ev_b += b && live;
    if (b && live) { brcr |= 0x4000; if (s) a_seen = false; }
    else if (a && s) a_seen = true;
    if (a && !s) brcr |= 0x8000;
    return (a && !s) || (b && live);
  }
  bool pre(uint32_t pc, uint8_t asid) {         // before the instruction at pc issues
    sync_copy();
    bool a = !pcba() && ubc_match(fch[0], true, false, SZ_W, pc, asid);
    bool b = !pcbb() && ubc_match(fch[1], true, false, SZ_W, pc, asid);
    return (a || b) && reach(a, b);
  }
  bool post(uint32_t pc, uint8_t asid) {        // after the instruction at pc completed
    sync_copy();
    bool a = pcba() && ubc_match(fch[0], true, false, SZ_W, pc, asid);
    bool b = pcbb() && ubc_match(fch[1], true, false, SZ_W, pc, asid);
    return (a || b) && reach(a, b);
  }
  // one operand access of an executed instruction: size in bytes (1 2 4 8; the ISS passes 0 for the cache-control
  // kinds); kind = dkind_e (sh4iss::DKind); data = the loaded or stored value.  Returns 0 none, 1 address break, 2 data
  // break (channel B with DBEB).  The kinds as SOC.md 3.8's ubc_opq table presents them [M 20.3.1, 20.3.5 (3)]: PREF,
  // OCBP, OCBWB are reads and OCBI a write, all size L with no data; MOVCA.L is a size-L write WITH its data (R0: the
  // manual's no-data list is PREF, OCBP, OCBWB, OCBI only; spec debug-ubc-hudi 4.1's erratum of 2026-09-23); TAS.B is
  // its read and its write (byte), its purge step is not a UBC access.
  int oper(uint32_t va, int size, int kind, uint64_t data, uint8_t asid) {
    if (kind == 10) return 0;                                   // TAS_PURGE
    const bool wr = kind == 2 || kind == 7 || kind == 4 || kind == 9 || kind == 12;   // ST MOVCA OCBI RMW_WR TAS_WR
    const bool nodata = kind == 3 || kind == 4 || kind == 5 || kind == 6;             // PREF OCBI OCBP OCBWB
    const bool as_l = kind >= 3 && kind <= 7;                   // PREF OCBI OCBP OCBWB MOVCA: longword
    const int msz = as_l ? SZ_L : size == 1 ? SZ_B : size == 2 ? SZ_W : size == 4 ? SZ_L : SZ_Q;
    bool a = op_match(0, va, msz, wr, nodata, asid);
    bool bm = op_match(1, va, msz, wr, nodata, asid);
    bool b = bm && (!dbeb() || ubc_data(bdrb, bdmrb, data, msz));
    if (!(a || b)) return 0;
    bool s = seq();
    if (!reach(a, b)) return 0;
    return (b && dbeb() && !(a && !s)) ? 2 : 1;
  }
};

// ============================================================================================================ PMC
// PMCR1 / PMCR2 (CCN 0x84 / 0x88, 16-bit, mask 0xC13F) and PMCTR (0xFF100000 block, a[4:2] decode).  Offsets a[27:0]:
// CCN = 0xF000000 + x, PMCTR = 0xF100000 + x (a[23:18] = 0x04).
struct Pmc : P4Slave {
  uint16_t pmcr[2] = {0, 0};
  uint64_t cnt[2] = {0, 0};     // 48 bits
  uint64_t ev_q = 0, ev_qq = 0; // the two event registers
  bool cycle_mode = false;
  bool w_pend = false; int w_n = 0; uint16_t w_val = 0;
  int ratio_inc = 12;           // functional mode's PMCLK increment (24 / Bdiv, IFC as x1: SOC.md 2.1; 12 at 0x0E0A)
  static constexpr uint64_t M48 = (1ull << 48) - 1;

  static bool is_ccn(uint32_t off) { return ((off >> 18) & 0x3F) == 0x00; }
  bool running(int n) const { return (pmcr[n] & 0xC000) == 0xC000; }
  uint32_t ctr_view(uint32_t off) const {
    switch ((off >> 2) & 7) {
    case 1: return (uint32_t)(cnt[0] >> 32) & 0xFFFF;
    case 2: return (uint32_t)cnt[0];
    case 3: return (uint32_t)(cnt[1] >> 32) & 0xFFFF;
    case 0: return 0;
    default: return (uint32_t)cnt[1];
    }
  }
  bool rd(uint32_t off, int size, uint32_t &v, bool) override {
    v = 0;
    if (is_ccn(off)) {
      uint32_t o = off & 0xFF;
      if (o != 0x84 && o != 0x88) return false;
      if (size == 1) v = pmcr[o == 0x88];
      return true;
    }
    uint32_t t = ctr_view(off);
    if (size == 2) v = t;
    else if (size == 1) v = t & 0xFFFF;                         // either halfword: bits 15:0 [HW]
    else v = (off & 1) ? (t >> 8) & 0xFF : t & 0xFF;            // [P] bytes of the replicated low half
    return ((off >> 2) & 7) != 0;
  }
  void apply(int n, uint16_t v) {               // a PMCR write lands
    if ((v & 0x2000) && !running(n)) cnt[n] = 0;              // PMCLR only clears a stopped counter [HW]
    pmcr[n] = v & 0xC13F;
  }
  bool wr(uint32_t off, int size, uint32_t v, bool) override {
    if (!is_ccn(off)) return ((off >> 2) & 7) != 0;           // PMCTR is read only [HW]
    uint32_t o = off & 0xFF;
    if (o != 0x84 && o != 0x88) return false;
    if (size != 1) return true;                                // wrong size: ignored [HW]
    if (cycle_mode) { w_pend = true; w_n = o == 0x88; w_val = (uint16_t)v; }
    else apply(o == 0x88, (uint16_t)v);
    return true;
  }
  bool is_volatile(uint32_t off, int) const override { return !is_ccn(off); }
  // functional mode: n events of `code` (the ISS's instruction-level events), and n elapsed cycles
  void count(int code, uint64_t n) {
    for (int k = 0; k < 2; k++)
      if (running(k) && (pmcr[k] & 0x3F) == code)
        cnt[k] = (cnt[k] + n * ((pmcr[k] & 0x0100) ? (uint64_t)ratio_inc : 1)) & M48;
  }
  void tick_icyc(uint64_t n) override { count(0x23, n); }
  // cycle mode: one Icyc.  The adder uses this cycle's running bits and ev_qq; the PMCR write lands at the edge.
  void clock(uint64_t ev, bool run, int ratio) {
    if (run) {
      for (int k = 0; k < 2; k++)
        if (running(k) && ((ev_qq >> (pmcr[k] & 0x3F)) & 1))
          cnt[k] = (cnt[k] + ((pmcr[k] & 0x0100) ? (uint64_t)ratio : 1)) & M48;
      ev_qq = ev_q;
      ev_q = ev;
    }
    if (w_pend) { apply(w_n, w_val); w_pend = false; }
  }
  void reset_por() { pmcr[0] = pmcr[1] = 0; cnt[0] = cnt[1] = 0; ev_q = ev_qq = 0; w_pend = false; }
  void reset_other() { ev_q = ev_qq = 0; }     // manual / CPU-only: registers held
};

// ============================================================================================================ CCN id
// Every CCN offset no other owner claims: PVR (0x30, 32-bit read only) and holes.
struct CcnId : P4Slave {
  uint32_t pvr = 0x040205C1;    // [HW] SH7750 (version field 0x0205)
  bool rd(uint32_t off, int size, uint32_t &v, bool) override {
    v = 0;
    if ((off & 0xFF) != 0x30) return false;
    if (size == 2) v = pvr;
    return true;
  }
  bool wr(uint32_t off, int, uint32_t, bool) override { return (off & 0xFF) == 0x30; }   // [P] ignored
  bool is_volatile(uint32_t, int) const override { return false; }
};

// ============================================================================================================ H-UDI
// SDIR (0x00, 16-bit, reads 0xFFFF), SDDR (0x08, 32-bit; SDDRH 16-bit at 0x08, SDDRL at 0x0A), every CPU write
// ignored [HW: the DC holds the TAP in reset].  Offsets a[27:0] = 0xFF00000 + x; full decode inside the block.
struct Hudi : P4Slave {
  uint32_t sddr = 0;            // the RTL's SDDR_INIT (the console's power-on content was 5573A3AB)
  bool rd(uint32_t off, int size, uint32_t &v, bool) override {
    uint32_t o = off & 0x3FFFF;
    v = 0;
    if ((o >> 1) == 0) { if (size == 1 && o == 0) v = 0xFFFF; return true; }   // SDIR spans +0-1; +2 / +3: holes
    if ((o >> 2) == 2) {
      if (size == 2 && o == 8) v = sddr;
      else if (size == 1 && o == 8) v = sddr >> 16;
      else if (size == 1 && o == 10) v = sddr & 0xFFFF;
      return true;
    }
    return false;
  }
  bool wr(uint32_t off, int, uint32_t, bool) override {
    uint32_t o = off & 0x3FFFF;
    return (o >> 1) == 0 || (o >> 2) == 2;
  }
  bool is_volatile(uint32_t, int) const override { return false; }
};

}  // namespace dbg
