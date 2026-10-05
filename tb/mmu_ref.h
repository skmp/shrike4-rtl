// mmu_ref.h -- functional reference model of the SH7091 MMU (rtl/v1/spec/mmu.md sections 2-3; rtl/v1/SOC.md 6.1).
// Owner: WI-MMU.  Written from the spec's tables and measured values, not from the RTL: the match rule and the PA are
// formed with page masks (the RTL slices fields), the LRUI tables are the spec's pattern tables, URC is the D2 rule.
//
// What it models:
//   * PTEH PTEL PTEA TTB TEA MMUCR with their implemented bits (mmu 2.1, [HW A1 / A2]), TI, the resets (SOC.md 2.3;
//     PTEH held by a manual reset, WI-MMU fix report);
//   * the 64 UTLB and 4 ITLB entries, the memory-mapped arrays with their bit maps and entry decode (mmu 2.2, [HW B1-B4]),
//     the associative UTLB address-array write (mmu 2.2, [HW B5]) with its multiple-hit reset request;
//   * LDTLB (mmu 3.7, [HW C1 / C2]);
//   * the data-access decision of mmu 3.1 / 3.4 for one access (region, PA, attributes, TLB fault, in priority order),
//     with the SOC.md 4.2 split: the address error is the core's and is computed here only by data_full();
//   * the instruction fetch: region poisons (SOC.md 4.1 item 2), ITLB lookup, LRUI, the hardware refill (mmu 3.5);
//   * URC (mmu 3.6), the caches' associative-write lookups (xl, SOC.md 3.8).
// It is a P4Slave pair (tb/sh4_p4if.h): regs() answers block CCN_MMU at off = a[27:0] (a[7:0] decoded), arrays()
// the ITLB / UTLB arrays at off = a[27:0] (off[26:24] = 2 3 6 7).  Timing is not modelled (the unit testbench
// checks the RTL's cycles itself).
#pragma once
#include <cstdint>
#include "sh4_p4if.h"

namespace mmuref {

// the v1_soc_pkg encodings (fx_e, region_e, dkind_e)
enum Fx : uint8_t { FX_NONE = 0, FX_ADDR = 1, FX_TLB_MISS = 2, FX_TLB_PROT = 3, FX_IPW = 4, FX_MHIT = 5 };
enum Region : uint8_t { RG_MEM = 0, RG_A7 = 1, RG_OCRAM = 2, RG_SQ = 3, RG_ARR = 4, RG_P4REG = 5, RG_P4RSV = 6 };
enum DKind : uint8_t {
  DK_NONE = 0, DK_LD, DK_ST, DK_PREF, DK_OCBI, DK_OCBP, DK_OCBWB, DK_MOVCA, DK_RMW_RD, DK_RMW_WR, DK_TAS_PURGE,
  DK_TAS_RD, DK_TAS_WR
};
inline bool dk_write(int k) { return k == DK_ST || k == DK_MOVCA || k == DK_OCBI || k == DK_RMW_WR || k == DK_TAS_WR; }

// EXPEVT of a fault (exceptions-intc 3.1; SOC.md fx_expevt): wr = write type, ifetch = an instruction fetch
inline uint32_t expevt(Fx fx, bool wr, bool ifetch) {
  bool w = wr && !ifetch;
  switch (fx) {
  case FX_ADDR: return w ? 0x100 : 0x0E0;
  case FX_TLB_MISS: return w ? 0x060 : 0x040;
  case FX_TLB_PROT: return w ? 0x0C0 : 0x0A0;
  case FX_IPW: return 0x080;
  case FX_MHIT: return 0x140;
  default: return 0;
  }
}

struct Ccr { bool ora = false, cb = false, wt = false, oce = true; };   // the CCR bits the MMU uses

inline uint32_t szmask(int sz) {                   // the VA bits a page of this size compares (mmu 3.3)
  static const uint32_t m[4] = {0xFFFFFC00u, 0xFFFFF000u, 0xFFFF0000u, 0xFFF00000u};
  return m[sz & 3];
}

struct UEnt {                                      // a UTLB entry; vpn / ppn hold the address bits in place
  uint32_t vpn = 0;                                // VA[31:10] (bits 9:0 zero)
  uint32_t ppn = 0;                                // PA[28:10] (bits 9:0 and 31:29 zero)
  uint8_t asid = 0, sz = 0, pr = 0, sa = 0;
  bool v = false, d = false, c = false, sh = false, wt = false, tc = false;
};
struct IEnt {                                      // an ITLB entry: no D, no WT, one PR bit (mmu 3.2)
  uint32_t vpn = 0, ppn = 0;
  uint8_t asid = 0, sz = 0, sa = 0;
  bool v = false, pr = false, c = false, sh = false, tc = false;
};

struct DXlat {                                     // v1_mmu's dxlat_t for one access
  Region rg = RG_MEM;
  uint32_t pa = 0;                                 // 29 bits
  bool c = false, wt = false;
  uint8_t sa = 0;
  bool tc = false;
  Fx fx = FX_NONE;
  bool vec400 = false;
  bool counted = false;                            // URC counts it if it completes (mmu 3.6): what the SoC's
                                                   // ma_commit qualification (v1_mmu_pkg mmu_urc_counts) must give
};

struct IXlat {                                     // one fetch lookup
  enum Kind { HIT, FAULT, MISS } kind = HIT;       // MISS: the ITLB missed; the refill decides (fetch() does it)
  uint32_t pa = 0;
  bool c = false;
  Fx fx = FX_NONE;
  bool vec400 = false;
  int ient = -1;                                   // the ITLB entry that hit (or was refilled)
};

struct XlRes { bool hit = false, mhit = false; uint32_t pa = 0; };

// Mutations for the unit testbench's self-test (tb/v1_mmu_tb.cpp --mutate k): each breaks one rule of this model so
// that a check against the RTL must fail.  0 (the default) everywhere else.  1 lrui_use of entry 2, 2 URC ignores
// URB, 3 rmw not checked as a write, 4 PA ignores the page size, 5 an ITLB DA1 bit that does not exist, 6 the
// associative ITLB write ignores AT, 7 (testbench) refill cost 6, 8 refill VPN from the UTLB entry, 9 PREF faults,
// 10 SV ignored, 11 CCR.OCE ignored in dxlat.c, 12 URC counts a faulting access (an SQ store whose check failed).
inline int g_mutate = 0;

// LRUI (mmu 3.5): the spec's pattern tables, written as (mask, value) pairs
inline int lrui_victim(uint32_t l) {
  struct { uint32_t m, v; } t[4] = {{0x38, 0x38}, {0x26, 0x06}, {0x15, 0x01}, {0x0B, 0x00}};  // 111***, 0**11*, *0*0*1, **0*00
  for (int e = 0; e < 4; e++) if ((l & t[e].m) == t[e].v) return e;
  return 3;                                        // prohibited values [?]: the RTL takes entry 3
}
inline uint32_t lrui_use(uint32_t l, int e) {
  static const uint32_t clr[4] = {0x38, 0x06, 0x01, 0x00}, set[4] = {0x00, 0x20, 0x14, 0x0B};
  if (g_mutate == 1 && e == 2) return ((l & ~0x05u) | 0x10u) & 0x3F;   // mutation 1: [2] not set by entry 2
  return ((l & ~clr[e]) | set[e]) & 0x3F;
}
inline uint32_t urc_next(uint32_t urc, uint32_t urb) {   // [HW D2]
  uint32_t n = (urc + 1) & 63;
  return n == urb ? 0 : n;
}


struct Sh4Mmu {
  UEnt utlb[64];
  IEnt itlb[4];
  uint32_t pteh = 0, ptel = 0, ptea = 0, ttb = 0, tea = 0;
  uint32_t lrui = 0, urb = 0, urc = 0;
  bool sqmd = false, sv = false, at = false;
  bool translate = true;                           // false: MMU-0 (v1_mmu TRANSLATE = 0)
  bool arr_mhit = false;                           // set by the last associative write that hit several entries
  Ccr ccr;

  uint8_t asid() const { return (uint8_t)pteh; }
  uint32_t mmucr() const {
    return (lrui << 26) | (urb << 18) | (urc << 10) | ((uint32_t)sqmd << 9) | ((uint32_t)sv << 8) | (uint32_t)at;
  }

  // ---- resets (SOC.md 2.3)
  void reset_por() {
    for (auto &e : utlb) e.v = false;
    for (auto &e : itlb) e.v = false;
    tea = 0;
    reset_common();
  }
  // manual: TEA, PTEH and the TLBs (V included) held -- PTEH so that a multiple-hit reset keeps the VPN / ASID its
  // request wrote (hm 5.6.1 (4)/(5); hm Appendix A: undefined, so holding is legal)
  void reset_man() { uint32_t h = pteh; reset_common(); pteh = h; }
  void reset_common() { pteh = ptel = ptea = ttb = 0; lrui = urb = urc = 0; sqmd = sv = at = false; }

  // ---- matching (mmu 3.3)
  bool asid_ok(bool sh, uint8_t easid, bool md) const {
    return sh || (sv && md && g_mutate != 10) || easid == asid();
  }
  bool umatch(const UEnt &e, uint32_t va, bool md) const {
    return e.v && ((e.vpn ^ va) & szmask(e.sz)) == 0 && asid_ok(e.sh, e.asid, md);
  }
  bool imatch(const IEnt &e, uint32_t va, bool md) const {
    return e.v && ((e.vpn ^ va) & szmask(e.sz)) == 0 && asid_ok(e.sh, e.asid, md);
  }
  static uint32_t pa_of(uint32_t ppn, int sz, uint32_t va) {
    uint32_t m = szmask(sz);
    if (g_mutate == 4) m = 0xFFFFFC00u;
    return ((ppn & m) | (va & ~m)) & 0x1FFFFFFFu;
  }
  int usearch(uint32_t va, bool md, int *nhit) const {   // returns the lowest matching index, -1 for none
    int first = -1, n = 0;
    for (int i = 0; i < 64; i++) if (umatch(utlb[i], va, md)) { if (first < 0) first = i; n++; }
    *nhit = n;
    return first;
  }
  int isearch(uint32_t va, bool md, int *nhit) const {
    int first = -1, n = 0;
    for (int i = 0; i < 4; i++) if (imatch(itlb[i], va, md)) { if (first < 0) first = i; n++; }
    *nhit = n;
    return first;
  }

  // ---- address classes (mmu 3.1; v1_soc_pkg amap_region / amap_region_xl, restated)
  static bool in_sq(uint32_t va) { return (va >> 26) == 0x38; }                  // E0000000-E3FFFFFF
  bool ocram(uint32_t va) const { return ccr.ora && (va >> 26) == 0x1F; }        // 7C000000-7FFFFFFF, ORA = 1
  bool translated_data(uint32_t va) const {                                      // rows 7 and 10 (row 9 excluded)
    if (!at) return false;
    if (in_sq(va)) return true;
    if (ocram(va)) return false;
    return va < 0x80000000u || (va >> 29) == 6;                                  // P0 / U0, P3
  }
  Region region_untr(uint32_t va) const {
    if (in_sq(va)) return RG_SQ;
    if (va >= 0xE4000000u && va < 0xF0000000u) return RG_P4RSV;
    if (va >= 0xF0000000u && va < 0xF8000000u) return RG_ARR;
    if (va >= 0xFF000000u) return RG_P4REG;
    if (va >= 0xF8000000u) return RG_P4RSV;
    if (ocram(va)) return RG_OCRAM;
    if (((va >> 26) & 7) == 7) return RG_A7;
    return RG_MEM;
  }
  static Region region_xl(uint32_t pa) {                                          // a UTLB-translated PA (C16)
    if ((pa >> 24) == 0x1F) return RG_P4REG;
    if ((pa >> 26) == 7) return RG_P4RSV;
    return RG_MEM;
  }

  // ---- one data access in EX (SOC.md 4.2): what v1_mmu answers (no address errors; those are the core's).
  // at_off_now: a MMUCR write with AT = 0 lands in this cycle (the mmu 7.4 fault gate).
  DXlat data(uint32_t va, int kind, bool md, bool rmw, bool at_off_now = false) const {
    DXlat r;
    r.rg = region_untr(va);
    r.pa = va & 0x1FFFFFFFu;
    uint32_t p = va >> 29;
    r.c = r.rg != RG_OCRAM && p != 5 && p != 7;                                  // P2 and P4 are not cached
    if (!ccr.oce && g_mutate != 11) r.c = false;                                 // cacheable only with CCR.OCE (mmu 3.4)
    r.wt = p == 4 ? !ccr.cb : ccr.wt;                                            // P1 per CB, P0 / U0 / P3 per WT
    if (!translate || !translated_data(va)) return r;
    bool sq = in_sq(va);
    bool wchk = dk_write(kind) || rmw;
    if (g_mutate == 3) wchk = dk_write(kind);
    int n, i = usearch(va, md, &n);
    Fx fx = FX_NONE;
    if (n > 1) fx = FX_MHIT;
    else if (n == 0) fx = FX_TLB_MISS;
    else {
      const UEnt &e = utlb[i];
      r.pa = pa_of(e.ppn, e.sz, va);
      r.rg = sq ? RG_SQ : region_xl(r.pa);
      r.c = e.c && r.rg == RG_MEM && (ccr.oce || g_mutate == 11);
      r.wt = e.wt; r.sa = e.sa; r.tc = e.tc;
      bool prot = (!md && !(e.pr & 2)) || (wchk && !(e.pr & 1));
      if (prot) fx = FX_TLB_PROT;
      else if (wchk && !e.d) fx = FX_IPW;
    }
    // a PREF outside the SQ never faults (pm 10.73 "treated as a NOP"): the failed one is dropped (RG_P4RSV)
    if (kind == DK_PREF && !sq && fx != FX_NONE && g_mutate != 9) { fx = FX_NONE; r.rg = RG_P4RSV; r.c = false; }
    if (at_off_now) fx = FX_NONE;
    r.fx = fx;
    r.vec400 = fx == FX_TLB_MISS;
    // URC (mmu 3.6): a completed access the UTLB translated; never a PREF outside the SQ, never a faulting one
    r.counted = n == 1 && (fx == FX_NONE || g_mutate == 12) && !(kind == DK_PREF && !sq) && !at_off_now;
    return r;
  }

  // the core's address checks too (mmu 3.1 rows 1-3, SOC.md 4.2): alignment, user >= 0x80000000, SQMD
  DXlat data_full(uint32_t va, int kind, int size, bool md, bool rmw) const {
    bool ctl = kind == DK_OCBI || kind == DK_OCBP || kind == DK_OCBWB || kind == DK_PREF || kind == DK_TAS_PURGE ||
               kind == DK_TAS_RD || kind == DK_TAS_WR;
    bool mis = !ctl && ((size == 1 && (va & 1)) || (size == 2 && (va & 3)) || (size == 3 && (va & 7)));
    bool usr = !md && va >= 0x80000000u && !(in_sq(va) && !sqmd);
    if (mis || usr) { DXlat r = data(va, kind, md, rmw); r.fx = FX_ADDR; r.vec400 = false; r.counted = false; return r; }
    return data(va, kind, md, rmw);
  }

  // ---- one fetch lookup (SOC.md 4.1 item 2, mmu 3.5); lookup() does not change state, use() applies the LRUI step
  bool fetch_poison(uint32_t va, bool md) const {
    return (!md && (va & 0x80000000u)) || (va >> 29) == 7 || ocram(va);
  }
  IXlat lookup(uint32_t va, bool md) const {
    IXlat r;
    r.pa = va & 0x1FFFFFFFu;
    r.c = (va >> 29) != 5;
    if (fetch_poison(va, md)) { r.kind = IXlat::FAULT; r.fx = FX_ADDR; return r; }
    if (!translate || !at || ocram(va) || !(va < 0x80000000u || (va >> 29) == 6)) return r;
    int n, i = isearch(va, md, &n);
    if (n > 1) { r.kind = IXlat::FAULT; r.fx = FX_MHIT; return r; }
    if (n == 0) { r.kind = IXlat::MISS; return r; }
    const IEnt &e = itlb[i];
    r.ient = i;
    uint32_t pa = pa_of(e.ppn, e.sz, va);
    if (!md && !e.pr) { r.kind = IXlat::FAULT; r.fx = FX_TLB_PROT; return r; }
    if ((pa >> 26) == 7) { r.kind = IXlat::FAULT; r.fx = FX_ADDR; return r; }
    r.pa = pa;
    r.c = e.c;
    return r;
  }
  void use(int e) { lrui = lrui_use(lrui, e); }
  // The hardware refill for a lookup that missed: UTLB search with the fetch VA (mmu 3.5).  Returns the UTLB entry
  // copied (or -1 with *fx = TLB_MISS / MHIT); *ient = the ITLB entry written.
  int refill(uint32_t va, bool md, int *ient, Fx *fx) {
    int n, u = usearch(va, md, &n);
    *ient = -1;
    if (n == 0) { *fx = FX_TLB_MISS; return -1; }
    if (n > 1) { *fx = FX_MHIT; return -1; }
    *fx = FX_NONE;
    int v = lrui_victim(lrui);
    IEnt &d = itlb[v];
    const UEnt &s = utlb[u];
    d.vpn = g_mutate == 8 ? s.vpn : va & 0xFFFFFC00u;   // from the fetch VA, not the UTLB entry [HW E2]
    d.asid = s.asid; d.v = true; d.ppn = s.ppn; d.sz = s.sz; d.sh = s.sh; d.c = s.c; d.sa = s.sa; d.tc = s.tc;
    d.pr = (s.pr & 2) != 0;
    use(v);
    *ient = v;
    return u;
  }
  // functional fetch: lookup, refill on a miss, and the retry (which is a use of the refilled entry)
  IXlat fetch(uint32_t va, bool md) {
    IXlat r = lookup(va, md);
    if (r.kind == IXlat::MISS) {
      int ie; Fx fx;
      if (refill(va, md, &ie, &fx) < 0) { r.kind = IXlat::FAULT; r.fx = fx; r.vec400 = fx == FX_TLB_MISS; return r; }
      r = lookup(va, md);
    }
    if (r.ient >= 0) use(r.ient);
    return r;
  }

  // ---- the caches' associative-write lookups (SOC.md 3.8): privileged match rule, no LRUI / URC
  XlRes xl(uint32_t va, bool use_itlb) const {
    XlRes r;
    r.pa = va & 0x1FFFFFFFu;
    if (!translate || !translated_data(va)) { r.hit = true; return r; }
    int n, i;
    if (use_itlb) {
      i = isearch(va, true, &n);
      if (n == 1) r.pa = pa_of(itlb[i].ppn, itlb[i].sz, va);
    } else {
      i = usearch(va, true, &n);
      if (n == 1) r.pa = pa_of(utlb[i].ppn, utlb[i].sz, va);
    }
    r.hit = n == 1;
    r.mhit = n > 1;
    return r;
  }

  // ---- URC, LDTLB, exception capture
  void urc_step() {
    if (g_mutate == 2) { urc = (urc + 1) & 63; return; }
    urc = urc_next(urc, urb);
  }
  void ldtlb() {
    if (!translate) return;
    UEnt &e = utlb[urc];
    e.vpn = pteh & 0xFFFFFC00u; e.asid = (uint8_t)pteh;
    e.ppn = ptel & 0x1FFFFC00u; e.v = (ptel >> 8) & 1; e.sz = (uint8_t)(((ptel >> 6) & 2) | ((ptel >> 4) & 1));
    e.pr = (ptel >> 5) & 3; e.c = (ptel >> 3) & 1; e.d = (ptel >> 2) & 1; e.sh = (ptel >> 1) & 1; e.wt = ptel & 1;
    e.sa = ptea & 7; e.tc = (ptea >> 3) & 1;
  }
  void exc_capture(uint32_t va) { tea = va; pteh = (va & 0xFFFFFC00u) | (pteh & 0xFFu); }

  // ---- registers (block CCN_MMU): 32-bit only, other sizes read 0 / ignore (SOC.md 3.1.4)
  bool reg_rd(uint32_t off, int size, uint32_t &v) const {
    v = 0;
    uint32_t o = off & 0xFF;
    bool hit = o == 0x00 || o == 0x04 || o == 0x08 || o == 0x0C || o == 0x10 || o == 0x34;
    if (!hit || size != 2) return hit;
    switch (o) {
    case 0x00: v = pteh; break;
    case 0x04: v = ptel; break;
    case 0x08: v = ttb; break;
    case 0x0C: v = tea; break;
    case 0x10: v = mmucr(); break;
    default: v = ptea; break;
    }
    return true;
  }
  bool reg_wr(uint32_t off, int size, uint32_t v) {
    uint32_t o = off & 0xFF;
    bool hit = o == 0x00 || o == 0x04 || o == 0x08 || o == 0x0C || o == 0x10 || o == 0x34;
    if (!hit || size != 2) return hit;
    switch (o) {
    case 0x00: pteh = v & 0xFFFFFCFFu; break;
    case 0x04: ptel = v & 0x1FFFFDFFu; break;
    case 0x08: ttb = v; break;
    case 0x0C: tea = v; break;
    case 0x10:
      lrui = v >> 26; urb = (v >> 18) & 63; urc = (v >> 10) & 63; sqmd = (v >> 9) & 1; sv = (v >> 8) & 1; at = v & 1;
      if ((v & 4) && translate) {                  // TI: V <- 0 everywhere, all else kept [HW A3]
        for (auto &e : utlb) e.v = false;
        for (auto &e : itlb) e.v = false;
      }
      break;
    default: ptea = v & 0xF; break;
    }
    return true;
  }

  // ---- arrays (mmu 2.2).  off = a[27:0]: off[26:24] 2 ITLB AA, 3 ITLB DA (off[23] = DA2), 6 UTLB AA (off[7] = A),
  // 7 UTLB DA (off[23] = DA2).  Entry: ITLB off[9:8], UTLB off[13:8].
  static uint32_t u_aa(const UEnt &e) { return e.vpn | ((uint32_t)e.d << 9) | ((uint32_t)e.v << 8) | e.asid; }
  static uint32_t u_da1(const UEnt &e) {
    return e.ppn | ((uint32_t)e.v << 8) | ((uint32_t)(e.sz & 2) << 6) | ((uint32_t)e.pr << 5) |
           ((uint32_t)(e.sz & 1) << 4) | ((uint32_t)e.c << 3) | ((uint32_t)e.d << 2) | ((uint32_t)e.sh << 1) |
           (uint32_t)e.wt;
  }
  static uint32_t i_aa(const IEnt &e) { return e.vpn | ((uint32_t)e.v << 8) | e.asid; }
  static uint32_t i_da1(const IEnt &e) {
    uint32_t v = e.ppn | ((uint32_t)e.v << 8) | ((uint32_t)(e.sz & 2) << 6) | ((uint32_t)e.pr << 6) |
                 ((uint32_t)(e.sz & 1) << 4) | ((uint32_t)e.c << 3) | ((uint32_t)e.sh << 1);
    if (g_mutate == 5) v |= (uint32_t)e.pr << 5;
    return v;
  }
  bool arr_rd(uint32_t off, int size, uint32_t &v) const {
    v = 0;
    if (!translate || size != 2) return true;
    int blk = (off >> 24) & 7;
    bool da2 = (off >> 23) & 1;
    const IEnt &ie = itlb[(off >> 8) & 3];
    const UEnt &ue = utlb[(off >> 8) & 63];
    switch (blk) {
    case 2: v = i_aa(ie); break;
    case 3: v = da2 ? (uint32_t)((ie.tc << 3) | ie.sa) : i_da1(ie); break;
    case 6: v = u_aa(ue); break;
    case 7: v = da2 ? (uint32_t)((ue.tc << 3) | ue.sa) : u_da1(ue); break;
    default: break;
    }
    return true;
  }
  bool arr_wr(uint32_t off, int size, uint32_t v) {
    arr_mhit = false;
    if (!translate || size != 2) return true;
    int blk = (off >> 24) & 7;
    bool da2 = (off >> 23) & 1;
    IEnt &ie = itlb[(off >> 8) & 3];
    UEnt &ue = utlb[(off >> 8) & 63];
    switch (blk) {
    case 2: ie.vpn = v & 0xFFFFFC00u; ie.v = (v >> 8) & 1; ie.asid = (uint8_t)v; break;
    case 3:
      if (da2) { ie.sa = v & 7; ie.tc = (v >> 3) & 1; }
      else {
        ie.ppn = v & 0x1FFFFC00u; ie.v = (v >> 8) & 1; ie.sz = (uint8_t)(((v >> 6) & 2) | ((v >> 4) & 1));
        ie.pr = (v >> 6) & 1; ie.c = (v >> 3) & 1; ie.sh = (v >> 1) & 1;
      }
      break;
    case 6:
      if (off & 0x80) assoc(v);
      else { ue.vpn = v & 0xFFFFFC00u; ue.d = (v >> 9) & 1; ue.v = (v >> 8) & 1; ue.asid = (uint8_t)v; }
      break;
    case 7:
      if (da2) { ue.sa = v & 7; ue.tc = (v >> 3) & 1; }
      else {
        ue.ppn = v & 0x1FFFFC00u; ue.v = (v >> 8) & 1; ue.sz = (uint8_t)(((v >> 6) & 2) | ((v >> 4) & 1));
        ue.pr = (v >> 5) & 3; ue.c = (v >> 3) & 1; ue.d = (v >> 2) & 1; ue.sh = (v >> 1) & 1; ue.wt = v & 1;
      }
      break;
    default: break;
    }
    return true;
  }
  // The associative write (mmu 2.2, hm 3.7.4, [HW B5]): VPN = data[31:10], PTEH.ASID, the privileged match rule.
  void assoc(uint32_t v) {
    uint32_t va = v & 0xFFFFFC00u;
    int n, u = usearch(va, true, &n);
    if (n == 1) { utlb[u].d = (v >> 9) & 1; utlb[u].v = (v >> 8) & 1; }
    int ni = 0;
    if (at || g_mutate == 6)                       // the ITLB side only with AT = 1 [HW B5]
      for (int i = 0; i < 4; i++)
        if (imatch(itlb[i], va, true)) { ni++; itlb[i].v = (v >> 8) & 1; }
    if (n > 1 || (at && ni > 1)) { arr_mhit = true; exc_capture(va); }
  }

  // ---- P4Slave adapters
  struct RegPort : P4Slave {
    Sh4Mmu *m;
    explicit RegPort(Sh4Mmu *mm) : m(mm) {}
    bool rd(uint32_t off, int size, uint32_t &v, bool) override { return m->reg_rd(off, size, v); }
    bool wr(uint32_t off, int size, uint32_t v, bool) override { return m->reg_wr(off, size, v); }
    // MMUCR.LRUI follows speculative fetches the model cannot see: the checker takes the RTL's value
    bool is_volatile(uint32_t off, int) const override { return (off & 0xFF) == 0x10; }
  };
  struct ArrPort : P4Slave {
    Sh4Mmu *m;
    explicit ArrPort(Sh4Mmu *mm) : m(mm) {}
    bool rd(uint32_t off, int size, uint32_t &v, bool) override { return m->arr_rd(off, size, v); }
    bool wr(uint32_t off, int size, uint32_t v, bool) override { return m->arr_wr(off, size, v); }
    bool is_volatile(uint32_t, int) const override { return false; }   // ITLB refills are injected (ITLBFILL)
  };
  RegPort reg_port{this};
  ArrPort arr_port{this};
  P4Slave *regs() { return &reg_port; }
  P4Slave *arrays() { return &arr_port; }

  Sh4Mmu() = default;
  Sh4Mmu(const Sh4Mmu &o) = delete;
  Sh4Mmu &operator=(const Sh4Mmu &o) = delete;
};

}  // namespace mmuref
