// v1_mmu_tb.cpp -- WI-MMU's unit testbench (make v1-mmu): rtl/v1/v1_mmu.sv (+ v1_utlb, v1_itlb, v1_mmu_regs,
// v1_mmu_pkg) through tb/v1_mmu_tb.sv, against the reference model tb/mmu_ref.h and the console logs of
// tools/hw/mmu/run{1,2,3}.log (rtl/v1/spec/mmu.md section 6).
//
// Sections (each prints its check count; any failure fails the run):
//   regs    A0-A3: reset values, implemented bits, MMUCR read-back, TI; the size rule; PTEH-write stall (4 Icyc)
//   arrays  B1-B4: array bit maps, V / D sharing, entry decode and aliases; one stall per array access (4 Icyc)
//   assoc   B5: the eight associative-write cases at AT = 0 and AT = 1 (UTLB 20 / ITLB 1, URC unchanged); arr_mhit
//           with TEA / PTEH capture on a multiple hit
//   ldtlb   C1 / C2, the LDTLB tap record, write-through to the EX compare (R6)
//   urc     D2 sequences; exhaustive urc_next over 64 URB x 64 URC through the RTL; ma_commit vs a MMUCR write
//   lrui    E1 / E1b / E1c sequences; exhaustive victim (64 LRUI) and use (64 x 4) through the RTL (refills / hits)
//   refill  E2 copy rules, refill costs (demand 7, UTLB miss / multiple hit poison at 3, speculative abandoned 6 / 2),
//           the ITLBFILL record, xl_stall and the UTLB array stall while the refill owns the UTLB
//   data    F / G / S / U rows (codes, TEA, PA, regions, area 7, SQ), the D1 / R4 URC table (the model's `counted` and
//           the RTL's ma_commit qualification mmu_urc_counts), the AT write timing (R5 0 -> 1 and 1 -> 0), R6
//           array-write visibility, CCR.OCE in dxlat.c, the excw capture (TEA, PTEH.VPN; ASID kept)
//   random  random UTLB / ITLB contents x random lookups (data, fetch with refills, xl) vs mmu_ref.h, >= 1M data
//           vectors (most of them on valid entries: TI only at the end of a batch), periodic full array and MMUCR
//           compares
//   rstc    the TLB multiple-hit reset end to end through the real v1_rstc (the data / fetch one: excw + mhit_req;
//           the associative write's arr_mhit): TEA and PTEH (VPN and the ASID at the exception) survive the manual
//           reset, MMUCR PTEL PTEA TTB are cleared (verifier FIND-1)
//   races   the refill against every other UTLB / ITLB writer landing in each of its cycles (TI, LDTLB, the
//           associative write, an ITLB array write), each against the serial order the RTL must equal on the model,
//           with the ack cycle, the ITLBFILL records and the array stalls (verifier FIND-2 / 3 / 4 / 6)
//   mmu0    TRANSLATE = 0: identity dxlat, poisons, no refill, arrays read 0 with one stall, registers, excw; `at`
//           stays 0 when MMUCR.AT is written 1 (verifier FIND-5)
// Options: --n <random data vectors> (default 1000000), --seed <s>, --mutate <k> (break rule k of the model; the run
// must then FAIL: make v1-mmu checks that it does), --only <section>, -v.
#include "Vv1_mmu_tb.h"
#include "verilated.h"
#include "mmu_ref.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>

using namespace mmuref;

static Vv1_mmu_tb *d;
static uint64_t g_cyc;
static long g_fail, g_checks, s_fail, s_checks;
static int g_verbose;
static Sh4Mmu M;                                   // the model of u1 (TRANSLATE = 1)
static Sh4Mmu M0;                                  // the model of u0 (TRANSLATE = 0)

#define CHECK(cond, ...)                                                                                 \
  do {                                                                                                   \
    g_checks++; s_checks++;                                                                              \
    if (!(cond)) {                                                                                       \
      g_fail++; s_fail++;                                                                                \
      if (g_fail <= 40) {                                                                                \
        printf("FAIL %s:%d cyc %llu: ", __FILE__, __LINE__, (unsigned long long)g_cyc);                  \
        printf(__VA_ARGS__);                                                                             \
        printf("\n");                                                                                    \
      }                                                                                                  \
    }                                                                                                    \
  } while (0)

// ---------------- deterministic random
static uint64_t g_rs = 0x243F6A8885A308D3ull;
static inline uint64_t rnd64() {
  uint64_t z = (g_rs += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}
static inline uint32_t rnd32() { return (uint32_t)rnd64(); }
static inline uint32_t rndn(uint32_t n) { return (uint32_t)((rnd64() >> 32) * n >> 32); }

// ---------------- the cycle
static void idle() {                               // every input but clk to its idle value
  d->rst_por = d->rst_man = d->rst_cpu = 0;
  d->rst_sel = 0; d->reset_n = 1; d->sck2 = 1; d->mhit_req = 0;   // v1_rstc idle (used by the rstc section only)
  d->reg_rd = d->reg_wr = 0; d->reg_size = 2; d->reg_off = 0; d->reg_wdata = 0;
  d->arr_rd = d->arr_wr = 0; d->arr_size = 2; d->arr_blk = 0; d->arr_off = 0; d->arr_wdata = 0;
  d->if_v = 0; d->if_va = 0; d->if_md = 1;
  d->dex_v = 0; d->dex_kind = 0; d->dex_size = 2; d->dex_va = 0; d->dex_md = 1; d->dex_rmw = 0; d->dex_arch = 0;
  d->ma_commit = 0; d->ldtlb = 0; d->excw_tea_v = 0; d->excw_tea = 0;
  d->xl_req = 0; d->xl_itlb = 0; d->xl_va = 0;
}
static void ev() { d->clk = 0; d->eval(); }
static void tick() { d->clk = 1; d->eval(); d->clk = 0; d->eval(); g_cyc++; }

static void reset_por() {
  idle(); d->rst_por = 1; ev(); tick(); tick(); d->rst_por = 0; ev();
  M.reset_por(); M0.reset_por();
}
static void set_ccr(bool ora, bool cb, bool wt, bool oce = true) {
  d->ccr_ora = ora; d->ccr_cb = cb; d->ccr_wt = wt; d->ccr_oce = oce;
  M.ccr.ora = M0.ccr.ora = ora; M.ccr.cb = M0.ccr.cb = cb; M.ccr.wt = M0.ccr.wt = wt; M.ccr.oce = M0.ccr.oce = oce;
}

// ---------------- the P4 slave ports: the strobe is held until u1 answers stall = 0 (SOC.md 3.1.5)
struct Acc { uint32_t v = 0, v0 = 0; int stalls = 0, stalls0 = -1; bool hit = false, hit0 = false; bool mhit = false; };

static Acc reg_acc(bool wr, uint32_t off, uint32_t wdata, int size = 2) {
  Acc a;
  d->reg_rd = !wr; d->reg_wr = wr; d->reg_size = size; d->reg_off = off & 0x0FFFFFFF; d->reg_wdata = wdata;
  for (int k = 0; k < 8; k++) {
    ev();
    if (a.stalls0 < 0 && !d->reg_stall0) { a.stalls0 = k; a.v0 = d->reg_rdata0; a.hit0 = d->reg_hit0; }
    if (!d->reg_stall) { a.v = d->reg_rdata; a.hit = d->reg_hit; tick(); break; }
    a.stalls++;
    tick();
  }
  d->reg_rd = d->reg_wr = 0; d->reg_size = 2;
  ev();
  if (wr) { M.reg_wr(off, size, wdata); M0.reg_wr(off, size, wdata); }
  return a;
}
static uint32_t rreg(uint32_t off) {
  Acc a = reg_acc(false, off, 0);
  uint32_t mv, mv0;
  M.reg_rd(off, 2, mv); M0.reg_rd(off, 2, mv0);
  CHECK(a.v == mv, "reg %02x read %08x model %08x", off & 0xFF, a.v, mv);
  CHECK(a.v0 == mv0, "MMU-0 reg %02x read %08x model %08x", off & 0xFF, a.v0, mv0);
  CHECK(a.stalls == 0 && a.hit, "reg %02x read: stalls %d hit %d (an Iclk register never stalls a read)", off & 0xFF,
        a.stalls, a.hit);
  return a.v;
}
static void wreg(uint32_t off, uint32_t v) {
  Acc a = reg_acc(true, off, v);
  int want = (off & 0xFF) == 0x00 ? 1 : 0;         // a PTEH write stalls once (4 Icyc in MA)
  CHECK(a.stalls == want && a.stalls0 == want, "reg %02x write: stalls %d / %d, want %d", off & 0xFF, a.stalls,
        a.stalls0, want);
}
enum { R_PTEH = 0x00, R_PTEL = 0x04, R_TTB = 0x08, R_TEA = 0x0C, R_MMUCR = 0x10, R_PTEA = 0x34 };

static int blk_of(uint32_t addr) {
  switch (addr >> 24) {
  case 0xF2: return 4;                             // P4B_ITLBAA
  case 0xF3: return 5;                             // P4B_ITLBDA
  case 0xF6: return 8;                             // P4B_UTLBAA
  default: return 9;                               // P4B_UTLBDA
  }
}
static int g_arr_extra_ok;                         // the refill may add stalls (test sets it)
static Acc arr_acc(bool wr, uint32_t addr, uint32_t wdata, int size = 2) {
  Acc a;
  d->arr_rd = !wr; d->arr_wr = wr; d->arr_size = size; d->arr_blk = blk_of(addr); d->arr_off = addr & 0x0FFFFFFF;
  d->arr_wdata = wdata;
  for (int k = 0; k < 12; k++) {
    ev();
    if (a.stalls0 < 0 && !d->arr_stall0) { a.stalls0 = k; a.v0 = d->arr_rdata0; a.hit0 = d->arr_hit0; }
    if (!d->arr_stall) { a.v = d->arr_rdata; a.hit = d->arr_hit; a.mhit = d->arr_mhit; tick(); break; }
    a.stalls++;
    tick();
  }
  d->arr_rd = d->arr_wr = 0;
  ev();
  if (wr) { M.arr_wr(addr & 0x0FFFFFFF, size, wdata); M0.arr_wr(addr & 0x0FFFFFFF, size, wdata); }
  CHECK(a.hit && a.hit0, "array %08x: hit %d / %d", addr, a.hit, a.hit0);
  CHECK(g_arr_extra_ok ? a.stalls >= 1 : a.stalls == 1, "array %08x: %d stalls (one: the synchronous RAM, 4 Icyc)",
        addr, a.stalls);
  CHECK(a.stalls0 == 1, "MMU-0 array %08x: %d stalls", addr, a.stalls0);
  return a;
}
static uint32_t rarr(uint32_t addr) {
  Acc a = arr_acc(false, addr, 0);
  uint32_t mv;
  M.arr_rd(addr & 0x0FFFFFFF, 2, mv);
  CHECK(a.v == mv, "array read %08x: %08x model %08x", addr, a.v, mv);
  CHECK(a.v0 == 0, "MMU-0 array read %08x: %08x (reads 0)", addr, a.v0);
  return a.v;
}
static bool warr(uint32_t addr, uint32_t v) {      // returns arr_mhit of the capture cycle
  Acc a = arr_acc(true, addr, v);
  CHECK(a.mhit == M.arr_mhit, "array write %08x <- %08x: arr_mhit %d model %d", addr, v, a.mhit, M.arr_mhit);
  return a.mhit;
}

#define ITLB_AA(e)  (0xF2000000u | ((uint32_t)(e) << 8))
#define ITLB_DA1(e) (0xF3000000u | ((uint32_t)(e) << 8))
#define ITLB_DA2(e) (0xF3800000u | ((uint32_t)(e) << 8))
#define UTLB_AA(e)  (0xF6000000u | ((uint32_t)(e) << 8))
#define UTLB_ASSOC  0xF6000080u
#define UTLB_DA1(e) (0xF7000000u | ((uint32_t)(e) << 8))
#define UTLB_DA2(e) (0xF7800000u | ((uint32_t)(e) << 8))
#define M_AT      1u
#define M_TI      4u
#define M_SV      0x100u
#define M_SQMD    0x200u
#define M_URC(x)  (((uint32_t)(x) & 63u) << 10)
#define M_URB(x)  (((uint32_t)(x) & 63u) << 18)
#define M_LRUI(x) (((uint32_t)(x) & 63u) << 26)
#define DA1_B9    0xFFFFFDFFu                      // DA1 bit 9 reads undefined on the silicon; the RTL returns 0 (mmu 2.2)

enum { SZ1K = 0, SZ4K = 1, SZ64K = 2, SZ1M = 3 };
// mmutest.c utlb_set / umap / hard_clear, through the array port (RTL and model)
static void utlb_set(int e, uint32_t va, uint32_t pa, int sz, int pr, int c, int dd, int sh, int wt, int asid, int sa,
                     int tc) {
  warr(UTLB_DA2(e), ((uint32_t)tc << 3) | (uint32_t)sa);
  warr(UTLB_DA1(e), (pa & 0x1FFFFC00u) | (1u << 8) | (((uint32_t)sz & 2u) << 6) | (((uint32_t)sz & 1u) << 4) |
                        ((uint32_t)pr << 5) | ((uint32_t)c << 3) | ((uint32_t)dd << 2) | ((uint32_t)sh << 1) |
                        (uint32_t)wt);
  warr(UTLB_AA(e), (va & 0xFFFFFC00u) | ((uint32_t)dd << 9) | (1u << 8) | ((uint32_t)asid & 0xFFu));
}
static uint32_t VA(uint32_t pa) { return 0x40000000u | (pa & 0x1FFFFFFFu); }
static void umap(int e, uint32_t pa, int pr, int dd) { utlb_set(e, VA(pa), pa, SZ4K, pr, 1, dd, 0, 0, 0, 0, 0); }
static void hard_clear() {
  for (int e = 0; e < 64; e++) { warr(UTLB_AA(e), 0); warr(UTLB_DA1(e), 0); warr(UTLB_DA2(e), 0); }
  for (int e = 0; e < 4; e++) { warr(ITLB_AA(e), 0); warr(ITLB_DA1(e), 0); warr(ITLB_DA2(e), 0); }
  wreg(R_MMUCR, M_TI);
  wreg(R_MMUCR, 0);
}
// the console's buffers (run1.log: "code 0c090000 data 0c0b0000 chase 0c0d0000 sq 0c073d20")
static const uint32_t CODE0 = 0x0C090000u, DATA0 = 0x0C0B0000u, SQBUF = 0x0C073D20u;
static uint32_t CODE(int k) { return CODE0 + 0x1000u * (uint32_t)k; }
static uint32_t DATA(int k) { return DATA0 + 0x1000u * (uint32_t)k; }

// ---------------- the fetch port: present the request until the MMU acks it (if_hit, or a poison), as v1_fetch
// holds ireq while busy (SOC.md 3.2.1).  Returns the cycles from the request to the ack (0 = the request cycle).
struct FRes { int cyc = -1; bool hit = false; uint32_t pa = 0; bool c = false; int fx = 0; bool vec400 = false;
              bool fill = false; int fill_idx = -1; uint32_t fill_va = 0; int fill_u = -1; int fill_cyc = -1; };
static FRes fetch_rtl(uint32_t va, bool md, int limit = 16) {
  FRes r;
  d->if_v = 1; d->if_va = va; d->if_md = md;
  for (int k = 0; k <= limit; k++) {
    ev();
    if (d->tap1_v) {
      r.fill = true; r.fill_idx = d->tap1_idx; r.fill_va = d->tap1_addr; r.fill_u = (int)(d->tap1_data & 63);
      r.fill_cyc = k;
      CHECK(d->tap1_kind == 14, "ITLBFILL record kind %d", d->tap1_kind);
    }
    if (d->if_hit || d->if_fx) {
      r.cyc = k; r.hit = d->if_hit; r.pa = d->if_pa; r.c = d->if_c; r.fx = d->if_fx; r.vec400 = d->if_vec400;
      CHECK(!d->if_stall, "if_stall with an ack");
      CHECK(!(d->if_hit && d->if_fx), "if_hit with a poison");
      tick();
      break;
    }
    CHECK(d->if_stall, "fetch %08x: neither ack nor if_stall at +%d", va, k);
    tick();
  }
  d->if_v = 0;
  ev();
  CHECK(r.cyc >= 0, "fetch %08x never acked", va);
  return r;
}
// the same fetch on the model, and the comparison; expect_cyc < 0: do not check the cycles
static int g_refill_cost = 7;                      // demand refill (mutation 7 breaks the expectation)
static FRes fetch_chk(uint32_t va, bool md, const char *what) {
  IXlat pre = M.lookup(va, md);
  int nu;
  M.usearch(va, md, &nu);                          // a refill happens iff the UTLB search hits exactly once
  bool refill = pre.kind == IXlat::MISS && nu == 1;
  FRes r = fetch_rtl(va, md);
  IXlat m = M.fetch(va, md);
  int want = pre.kind != IXlat::MISS ? 0 : refill ? g_refill_cost : 3;
  CHECK(r.cyc == want, "%s: fetch %08x acked after %d, want %d", what, va, r.cyc, want);
  CHECK(r.hit == (m.kind == IXlat::HIT), "%s: fetch %08x hit %d model %d", what, va, r.hit, m.kind == IXlat::HIT);
  CHECK(r.fx == m.fx, "%s: fetch %08x fx %d model %d", what, va, r.fx, m.fx);
  CHECK(r.vec400 == m.vec400, "%s: fetch %08x vec400 %d", what, va, r.vec400);
  if (r.hit) {
    CHECK(r.pa == m.pa, "%s: fetch %08x pa %08x model %08x", what, va, r.pa, m.pa);
    CHECK(r.c == m.c, "%s: fetch %08x c %d model %d", what, va, r.c, m.c);
  }
  if (refill) {
    CHECK(r.fill && r.fill_cyc == 3 && r.fill_va == va && r.fill_idx == m.ient,
          "%s: ITLBFILL record %d at +%d va %08x idx %d (model %d)", what, r.fill, r.fill_cyc, r.fill_va, r.fill_idx,
          m.ient);
  } else {
    CHECK(!r.fill, "%s: an ITLBFILL record without a refill", what);
  }
  return r;
}
static char ipage(int e) {                         // mmutest.c ipage(): the code page an ITLB entry maps
  uint32_t a = rarr(ITLB_AA(e));
  if (!(a & 0x100)) return '-';
  uint32_t v = a & 0xFFFFF000u;
  for (int k = 0; k < 16; k++) if (v == (VA(CODE(k)) & 0xFFFFF000u)) return (char)(k < 10 ? '0' + k : 'a' + k - 10);
  return '?';
}
static std::string ipages() { std::string s; for (int e = 0; e < 4; e++) s += ipage(e); return s; }
static uint32_t lrui_now() { return rreg(R_MMUCR) >> 26; }
static uint32_t urc_now() { return (rreg(R_MMUCR) >> 10) & 63; }

// ---------------- the data side: one EX access (combinational; no clock)
struct DRes { int rg, fx, sa; uint32_t pa; bool c, wt, tc, vec400, cnt; };
static DRes dex_rtl(uint32_t va, int kind, bool md, bool rmw, int size = 2, bool v = true) {
  d->dex_v = v; d->dex_va = va; d->dex_kind = kind; d->dex_md = md; d->dex_rmw = rmw; d->dex_size = size;
  ev();
  DRes r{d->dxl_rg, d->dxl_fx, d->dxl_sa, d->dxl_pa, (bool)d->dxl_c, (bool)d->dxl_wt, (bool)d->dxl_tc,
         (bool)d->dxl_vec400, (bool)d->urc_cnt};
  return r;
}
static void dex_cmp(const DRes &r, const DXlat &m, bool v, const char *what, uint32_t va, int kind) {
  CHECK(r.fx == (v ? m.fx : 0) && r.vec400 == (v && m.vec400), "%s: va %08x kind %d fx %d/%d model %d/%d", what, va,
        kind, r.fx, r.vec400, m.fx, m.vec400);
  // the ma_commit qualification (mmu_urc_counts on the lreq this access would present) against the model's rule
  CHECK(!v || r.cnt == m.counted, "%s: va %08x kind %d: mmu_urc_counts %d, model counted %d", what, va, kind, r.cnt,
        m.counted);
  CHECK(r.rg == m.rg && r.pa == m.pa, "%s: va %08x kind %d rg %d pa %08x model %d %08x", what, va, kind, r.rg, r.pa,
        m.rg, m.pa);
  CHECK(r.c == m.c && r.wt == m.wt && r.sa == m.sa && r.tc == m.tc, "%s: va %08x attrs c%d wt%d sa%d tc%d model "
        "c%d wt%d sa%d tc%d", what, va, r.c, r.wt, r.sa, r.tc, m.c, m.wt, m.sa, m.tc);
}
static DRes dex_chk(uint32_t va, int kind, bool md, bool rmw, const char *what) {
  DRes r = dex_rtl(va, kind, md, rmw);
  dex_cmp(r, M.data(va, kind, md, rmw), true, what, va, kind);
  return r;
}
// the core's view (SOC.md 4.2): address error first, else dxl.fx; EXPEVT with the write type of the access (a TLB
// protection violation on an rmw read is a write, C5)
static uint32_t core_expevt(uint32_t va, int kind, int size, bool md, bool rmw, int *fx_out = nullptr) {
  DXlat f = M.data_full(va, kind, size, md, rmw);
  DRes r = dex_rtl(va, kind, md, rmw, size);
  if (f.fx != FX_ADDR) CHECK(r.fx == f.fx, "va %08x kind %d: RTL fx %d model %d", va, kind, r.fx, f.fx);
  int fx = f.fx == FX_ADDR ? FX_ADDR : r.fx;
  if (fx_out) *fx_out = fx;
  bool wr = dk_write(kind) || (rmw && fx == FX_TLB_PROT);
  return expevt((Fx)fx, wr, false);
}

// ---------------- sections
static void section(const char *nm) { s_fail = s_checks = 0; if (g_verbose) printf("-- %s\n", nm); }
static void section_end(const char *nm) {
  printf("  %-7s %8ld checks, %ld failed\n", nm, s_checks, s_fail);
}

// A0-A3, the size rule, PTEH write stall
static void t_regs() {
  section("regs");
  reset_por();
  static const uint32_t regs[5] = {R_PTEH, R_PTEL, R_PTEA, R_TTB, R_TEA};
  // A0 (power-on: the RTL's 0 [P]; the console under KOS read 0 too)
  for (uint32_t r : regs) CHECK(rreg(r) == 0, "A0 reg %02x after power-on", r);
  CHECK(rreg(R_MMUCR) == 0, "A0 MMUCR");
  // A1 [HW run1.log]: write ffffffff / 0 / 5a5aa5a5
  static const uint32_t exp[5][3] = {{0xfffffcff, 0, 0x5a5aa4a5}, {0x1ffffdff, 0, 0x1a5aa5a5}, {0x0000000f, 0, 5},
                                     {0xffffffff, 0, 0x5a5aa5a5}, {0xffffffff, 0, 0x5a5aa5a5}};
  for (int i = 0; i < 5; i++) {
    wreg(regs[i], 0xFFFFFFFFu); CHECK(rreg(regs[i]) == exp[i][0], "A1 reg %02x ffffffff", regs[i]);
    wreg(regs[i], 0);           CHECK(rreg(regs[i]) == exp[i][1], "A1 reg %02x 0", regs[i]);
    wreg(regs[i], 0x5A5AA5A5u); CHECK(rreg(regs[i]) == exp[i][2], "A1 reg %02x 5a5aa5a5", regs[i]);
  }
  // A2
  wreg(R_MMUCR, M_LRUI(0x3F) | M_URB(0x3F) | M_URC(0x3F) | M_SQMD | M_SV | M_TI);
  CHECK(rreg(R_MMUCR) == 0xfcfcff00u, "A2 fcfcff04");
  CHECK(d->sqmd && !d->at, "A2 sqmd / at outputs");
  wreg(R_MMUCR, M_LRUI(0x07) | M_URB(0x15) | M_URC(0x2A));
  CHECK(rreg(R_MMUCR) == 0x1c54a800u, "A2 1c54a800");
  wreg(R_MMUCR, M_LRUI(0x38) | M_TI);
  CHECK(rreg(R_MMUCR) == 0xe0000000u, "A2 e0000004");
  wreg(R_MMUCR, 0);
  // A3: TI clears V only
  warr(UTLB_DA2(5), 0xD); warr(UTLB_DA1(5), 0x0ABCD000u | 0x1FFu); warr(UTLB_AA(5), 0x12345000u | 0x300u | 0x33u);
  warr(ITLB_DA2(2), 0xA); warr(ITLB_DA1(2), 0x0BCDE000u | 0x1FFu); warr(ITLB_AA(2), 0x23456000u | 0x100u | 0x44u);
  const uint32_t before[6] = {0x12345333, 0x0abcd1ff, 0x0d, 0x23456144, 0x0bcde1da, 0x0a};
  const uint32_t after[6] = {0x12345233, 0x0abcd0ff, 0x0d, 0x23456044, 0x0bcde0da, 0x0a};
  const uint32_t addr[6] = {UTLB_AA(5), UTLB_DA1(5), UTLB_DA2(5), ITLB_AA(2), ITLB_DA1(2), ITLB_DA2(2)};
  for (int i = 0; i < 6; i++) CHECK(rarr(addr[i]) == before[i], "A3 before %08x", addr[i]);
  wreg(R_MMUCR, M_TI);
  for (int i = 0; i < 6; i++) CHECK(rarr(addr[i]) == after[i], "A3 after %08x", addr[i]);
  CHECK(rreg(R_MMUCR) == 0, "A3 MMUCR after TI");
  // the size rule (SOC.md 3.1.4): a byte / word access reads 0 with hit = 1, a byte write is ignored
  wreg(R_TTB, 0x11223344u);
  for (int sz = 0; sz < 2; sz++) {
    Acc a = reg_acc(false, R_TTB, 0, sz);
    CHECK(a.v == 0 && a.hit && a.stalls == 0, "size %d read of TTB: %08x hit %d", sz, a.v, a.hit);
    reg_acc(true, R_TTB, 0xFFFFFFFFu, sz);
  }
  CHECK(rreg(R_TTB) == 0x11223344u, "a byte / word write reached TTB");
  // a hole offset of the CCN window is not ours (the bridge never strobes it; hit = 0)
  Acc h = reg_acc(false, 0x30, 0);
  CHECK(!h.hit && h.v == 0, "offset 0x30 (PVR, not the MMU's): hit %d", h.hit);
  // a[17:8] of the CCN window are ignored by the decode
  CHECK(reg_acc(false, 0x3FF00 | R_TTB, 0).v == 0x11223344u, "TTB at a[17:8] != 0");
  // asid output follows PTEH.ASID; excw TEA / PTEH.VPN capture keeps the ASID [HW F, G: PTEH 2c0b3012]
  wreg(R_PTEH, 0x00000312u);
  CHECK(d->asid == 0x12 && d->asid0 == 0x12, "asid output");
  d->excw_tea_v = 1; d->excw_tea = 0x2C0B3000u; ev(); tick(); d->excw_tea_v = 0; ev();
  M.exc_capture(0x2C0B3000u); M0.exc_capture(0x2C0B3000u);
  CHECK(rreg(R_PTEH) == 0x2c0b3012u, "PTEH after a miss with PTEH.ASID 12 [HW]: %08x", M.pteh);
  CHECK(rreg(R_TEA) == 0x2C0B3000u, "TEA capture");
  // "PTEH preset 00000300, miss at 4c0b73fc: PTEH 4c0b7000 TEA 4c0b73fc" (bits 9:8 do not exist)
  wreg(R_PTEH, 0x300);
  d->excw_tea_v = 1; d->excw_tea = 0x4C0B73FCu; ev(); tick(); d->excw_tea_v = 0; ev();
  M.exc_capture(0x4C0B73FCu); M0.exc_capture(0x4C0B73FCu);
  CHECK(rreg(R_PTEH) == 0x4c0b7000u && rreg(R_TEA) == 0x4c0b73fcu, "PTEH / TEA after the miss at 4c0b73fc");
  // "AT0 st16 P1 misaligned: TEA 8c0b0001 PTEH 8c0b0078 (preset 12345678)"
  wreg(R_PTEH, 0x12345678u);
  d->excw_tea_v = 1; d->excw_tea = 0x8C0B0001u; ev(); tick(); d->excw_tea_v = 0; ev();
  M.exc_capture(0x8C0B0001u); M0.exc_capture(0x8C0B0001u);
  CHECK(rreg(R_PTEH) == 0x8c0b0078u && rreg(R_TEA) == 0x8c0b0001u, "AT0 address error TEA / PTEH");
  // resets (SOC.md 2.3): manual: MMUCR 0, TEA and PTEH held (WI-MMU fix, FIND-1), PTEL PTEA TTB 0; CPU-only: held
  wreg(R_MMUCR, M_URC(5) | M_AT); wreg(R_TTB, 0x55u);
  idle(); d->rst_cpu = 1; ev(); tick(); d->rst_cpu = 0; ev();
  CHECK(rreg(R_MMUCR) == (M_URC(5) | M_AT) && rreg(R_TTB) == 0x55u && rreg(R_TEA) == 0x8c0b0001u, "CPU-only reset");
  idle(); d->rst_man = 1; ev(); tick(); d->rst_man = 0; ev();
  M.reset_man(); M0.reset_man();
  CHECK(rreg(R_MMUCR) == 0 && rreg(R_TTB) == 0 && rreg(R_TEA) == 0x8c0b0001u && rreg(R_PTEH) == 0x8c0b0078u,
        "manual reset (PTEH held)");
  section_end("regs");
}

// B1-B4
static void t_arrays() {
  section("arrays");
  reset_por();
  hard_clear();
  // B1: ITLB entry 1 (values with DA1 bit 9 masked: undefined on the silicon, 0 in the RTL)
  struct { uint32_t wa, wv, ra; uint32_t exp; bool m9; } B1[] = {
    {ITLB_AA(1), 0xFFFFFFFFu, ITLB_AA(1), 0xfffffdff, false}, {0, 0, ITLB_DA1(1), 0x00000100, false},
    {ITLB_DA1(1), 0, ITLB_AA(1), 0xfffffcff, false}, {0, 0, ITLB_DA1(1), 0x00000000, false},
    {ITLB_DA1(1), 0xFFFFFFFFu, ITLB_DA1(1), 0x1ffffdda, false}, {0, 0, ITLB_AA(1), 0xfffffdff, false},
    {ITLB_DA2(1), 0xFFFFFFFFu, ITLB_DA2(1), 0x0000000f, false}, {0, 0, ITLB_DA1(1), 0x1ffffdda, false},
    {ITLB_AA(1), 0, ITLB_AA(1), 0x00000000, false}, {0, 0, ITLB_DA1(1), 0x1ffffeda, true},
    {0, 0, ITLB_DA2(1), 0x0000000f, false},
    // B2: UTLB entry 37
    {UTLB_AA(37), 0xFFFFFFFFu, UTLB_AA(37), 0xffffffff, false}, {0, 0, UTLB_DA1(37), 0x00000104, false},
    {UTLB_DA1(37), 0, UTLB_AA(37), 0xfffffcff, false}, {0, 0, UTLB_DA1(37), 0x00000000, false},
    {UTLB_DA1(37), 0xFFFFFFFFu, UTLB_DA1(37), 0x1ffffdff, false}, {0, 0, UTLB_AA(37), 0xffffffff, false},
    {UTLB_DA2(37), 0xFFFFFFFFu, UTLB_DA2(37), 0x0000000f, false}, {0, 0, UTLB_DA1(37), 0x1ffffdff, false},
    {UTLB_AA(37), 0x200u, UTLB_AA(37), 0x00000200, false}, {0, 0, UTLB_DA1(37), 0x1ffffeff, true},
    {UTLB_AA(37), 0, UTLB_AA(37), 0x00000000, false}, {0, 0, UTLB_DA1(37), 0x1ffffefb, true},
    {0, 0, UTLB_DA2(37), 0x0000000f, false}};
  for (auto &b : B1) {
    if (b.wa) warr(b.wa, b.wv);
    uint32_t v = rarr(b.ra);
    uint32_t m = b.m9 ? DA1_B9 : 0xFFFFFFFFu;
    CHECK((v & m) == (b.exp & m), "B1/B2 read %08x = %08x, console %08x", b.ra, v, b.exp);
  }
  // B3: ITLB entries by addr[9:8] only
  for (int e = 0; e < 4; e++) warr(ITLB_AA(e), (0x10000000u * (uint32_t)(e + 1)) | (uint32_t)(0x11 * (e + 1)));
  static const uint32_t b3[4] = {0x10000011, 0x20000022, 0x30000033, 0x40000044};
  for (int e = 0; e < 4; e++) {
    CHECK(rarr(ITLB_AA(e)) == b3[e], "B3 ITLB%d (e<<8)", e);
    CHECK(rarr(0xF2000000u | ((uint32_t)e << 16)) == 0x10000011u, "B3 ITLB%d (e<<16) reads entry 0", e);
    CHECK(rarr(ITLB_AA(e) | 0x80u) == b3[e], "B3 ITLB%d (|80)", e);
    CHECK(rarr(ITLB_AA(e) | 0x00FFFC00u) == b3[e], "B3 ITLB%d (|ffc00)", e);
  }
  // B4: UTLB entries by addr[13:8] only; A (addr[7]) ignored on reads; DA2 is addr[23]
  for (int e = 0; e < 64; e++) warr(UTLB_AA(e), ((uint32_t)e << 12) | (uint32_t)e);
  int bad = 0;
  for (int e = 0; e < 64; e++) if (rarr(UTLB_AA(e)) != (((uint32_t)e << 12) | (uint32_t)e)) bad++;
  CHECK(bad == 0, "B4 UTLB decode e<<8: %d mismatches", bad);
  CHECK(rarr(0xF6000000u | (5u << 8) | 0x00FFC000u) == 0x5005u, "B4 aa5|ffc000");
  CHECK(rarr(UTLB_AA(5) | 0x7Fu) == 0x5005u, "B4 aa5|7f");
  CHECK(rarr(UTLB_AA(5) | 0x80u) == 0x5005u, "B4 aa5|80 (A ignored on reads)");
  warr(UTLB_DA2(9), 0x5);
  CHECK(rarr(0xF7800000u | (9u << 8) | 0x7FC000u) == 5u, "B4 da2-9|7fc000");
  CHECK(rarr(0xF7000000u | (5u << 8) | 0x7FC000u) == rarr(UTLB_DA1(5)), "B4 da1-5|7fc000");
  // a non-long access reads 0 (hit) and a non-long write is ignored (the RTL's size rule, mmu 2.2 "undefined")
  Acc a = arr_acc(false, UTLB_AA(5), 0, 1);
  CHECK(a.v == 0, "UTLB AA word read: %08x", a.v);
  arr_acc(true, UTLB_AA(5), 0xFFFFFFFFu, 0);
  CHECK(rarr(UTLB_AA(5)) == 0x5005u, "a byte write reached UTLB AA 5");
  hard_clear();
  section_end("arrays");
}

// B5 and the associative multiple hit
static void t_assoc() {
  section("assoc");
  reset_por();
  const uint32_t X0 = 0x2A000000u;
  auto setup = [&](int uv, int ud, int uasid, int ush, int iv) {
    warr(UTLB_DA2(20), 0);
    warr(UTLB_DA1(20), 0x0C000000u | ((uint32_t)uv << 8) | (1u << 4) | (3u << 5) | ((uint32_t)ud << 2) |
                           ((uint32_t)ush << 1));
    warr(UTLB_AA(20), X0 | ((uint32_t)ud << 9) | ((uint32_t)uv << 8) | (uint32_t)uasid);
    warr(ITLB_DA2(1), 0);
    warr(ITLB_DA1(1), 0x0C000000u | ((uint32_t)iv << 8) | (1u << 4) | (1u << 6));
    warr(ITLB_AA(1), X0 | ((uint32_t)iv << 8));
  };
  // run1.log B5: {UTLB20 aa, da1, ITLB1 aa, da1} per case, AT = 0 then AT = 1
  static const uint32_t exp[2][8][4] = {
    {{0x2a000300, 0x0c000374, 0x2a000100, 0x0c000350}, {0x2a000000, 0x0c000270, 0x2a000100, 0x0c000350},
     {0x2a000000, 0x0c000270, 0x2a000100, 0x0c000350}, {0x2a000155, 0x0c000170, 0x2a000100, 0x0c000350},
     {0x2a000055, 0x0c000072, 0x2a000100, 0x0c000350}, {0x2a000000, 0x0c000270, 0x2a000100, 0x0c000350},
     {0x2a000300, 0x0c000374, 0x2a000000, 0x0c000250}, {0x2a000100, 0x0c000370, 0x2a000100, 0x0c000350}},
    {{0x2a000300, 0x0c000374, 0x2a000100, 0x0c000350}, {0x2a000000, 0x0c000270, 0x2a000000, 0x0c000250},
     {0x2a000000, 0x0c000270, 0x2a000000, 0x0c000250}, {0x2a000155, 0x0c000170, 0x2a000000, 0x0c000250},
     {0x2a000055, 0x0c000072, 0x2a000000, 0x0c000250}, {0x2a000000, 0x0c000270, 0x2a000000, 0x0c000250},
     {0x2a000300, 0x0c000374, 0x2a000000, 0x0c000250}, {0x2a000100, 0x0c000370, 0x2a000100, 0x0c000350}}};
  struct { int re; int uv, ud, uasid, ush, iv; uint32_t data; } C[8] = {
    {1, 1, 0, 0, 0, 1, X0 | 0x300}, {0, 0, 0, 0, 0, 0, X0},      // b follows a without a new setup
    {1, 1, 0, 0, 0, 1, (X0 + 0x800)}, {1, 1, 0, 0x55, 0, 1, X0}, {1, 1, 0, 0x55, 1, 1, X0},
    {1, 0, 0, 0, 0, 1, X0}, {1, 1, 0, 0, 0, 0, X0 | 0x300}, {1, 1, 1, 0, 0, 1, X0 | 0x100}};
  for (int at = 0; at < 2; at++) {
    hard_clear();
    wreg(R_PTEH, 0);
    for (int c = 0; c < 8; c++) {
      if (C[c].re) { wreg(R_MMUCR, 0); setup(C[c].uv, C[c].ud, C[c].uasid, C[c].ush, C[c].iv); }
      if (at) wreg(R_MMUCR, M_AT);
      uint32_t u0 = urc_now();
      bool mh = warr(UTLB_ASSOC, C[c].data);
      CHECK(!mh, "B5 %c AT=%d: arr_mhit", 'a' + c, at);
      CHECK(urc_now() == u0, "B5 %c AT=%d: URC changed", 'a' + c, at);
      uint32_t v[4] = {rarr(UTLB_AA(20)), rarr(UTLB_DA1(20)), rarr(ITLB_AA(1)), rarr(ITLB_DA1(1))};
      for (int k = 0; k < 4; k++) {
        uint32_t m = (k & 1) ? DA1_B9 : 0xFFFFFFFFu;
        CHECK((v[k] & m) == (exp[at][c][k] & m), "B5 case %c AT=%d word %d: %08x console %08x", 'a' + c, at, k, v[k],
              exp[at][c][k]);
      }
    }
  }
  // a multiple hit: two UTLB entries map X0 (4K and 1M) -> arr_mhit, TEA / PTEH.VPN <- the VPN, nothing written
  wreg(R_MMUCR, 0);
  hard_clear();
  wreg(R_PTEH, 0x21);
  utlb_set(3, X0, 0x0C000000u, SZ4K, 3, 1, 1, 0, 0, 0x21, 0, 0);
  utlb_set(9, X0, 0x0C000000u, SZ1M, 3, 1, 1, 1, 0, 0x00, 0, 0);
  bool mh = warr(UTLB_ASSOC | (7u << 8), X0 | 0x400 | 0x000);
  CHECK(mh, "associative write hitting two UTLB entries: no arr_mhit");
  CHECK(rarr(UTLB_AA(3)) == (X0 | 0x300 | 0x21) && rarr(UTLB_AA(9)) == (X0 | 0x300), "multiple hit wrote an entry");
  CHECK(rreg(R_TEA) == (X0 | 0x400) && rreg(R_PTEH) == (X0 | 0x400 | 0x21), "multiple hit: TEA %08x PTEH %08x",
        M.tea, M.pteh);
  // an ITLB multiple hit counts only with AT = 1
  hard_clear();
  wreg(R_PTEH, 0);
  warr(ITLB_DA1(0), 0x0C000000u | 0x100 | 0x10 | 0x40); warr(ITLB_AA(0), X0 | 0x100);
  warr(ITLB_DA1(2), 0x0C000000u | 0x100 | 0x90 | 0x40); warr(ITLB_AA(2), X0 | 0x100);
  CHECK(!warr(UTLB_ASSOC, X0 | 0x100), "ITLB double match with AT = 0 raised arr_mhit");
  wreg(R_MMUCR, M_AT);
  CHECK(warr(UTLB_ASSOC, X0 | 0x100), "ITLB double match with AT = 1: no arr_mhit");
  wreg(R_MMUCR, 0);
  hard_clear();
  section_end("assoc");
}

// C1 / C2, the LDTLB record, write-through
static void t_ldtlb() {
  section("ldtlb");
  reset_por();
  hard_clear();
  auto ldtlb = [&]() {
    d->ldtlb = 1; ev();
    CHECK(d->tap0_v && d->tap0_kind == 13 && d->tap0_idx == M.urc, "LDTLB record v%d kind %d idx %d (URC %d)",
          d->tap0_v, d->tap0_kind, d->tap0_idx, M.urc);
    CHECK(!d->tap0_v0, "MMU-0 LDTLB record");
    tick(); d->ldtlb = 0; ev();
    M.ldtlb(); M0.ldtlb();
  };
  wreg(R_PTEH, 0xFFFFFFFFu); wreg(R_PTEL, 0xFFFFFFFFu); wreg(R_PTEA, 0xFFFFFFFFu);
  wreg(R_MMUCR, M_URC(0x2A));
  ldtlb();
  CHECK(rarr(UTLB_AA(0x2A)) == 0xffffffffu, "C1 aa");
  CHECK((rarr(UTLB_DA1(0x2A)) & DA1_B9) == (0x1fffffffu & DA1_B9), "C1 da1");
  CHECK(rarr(UTLB_DA2(0x2A)) == 0xfu, "C1 da2");
  CHECK(rreg(R_MMUCR) == 0x0000a800u, "C1 MMUCR (URC not changed)");
  wreg(R_PTEH, 0x87654021u); wreg(R_PTEL, 0x0ABCD000u | 0x100u | 0x80u | 0x40u | 0x04u | 0x01u); wreg(R_PTEA, 6);
  wreg(R_MMUCR, M_URC(0x15) | M_URB(0x10));
  ldtlb();
  CHECK(rarr(UTLB_AA(0x15)) == 0x87654321u, "C2 aa");
  CHECK((rarr(UTLB_DA1(0x15)) & DA1_B9) == (0x0abcd3c5u & DA1_B9), "C2 da1");
  CHECK(rarr(UTLB_DA2(0x15)) == 6u, "C2 da2");
  CHECK(rreg(R_MMUCR) == 0x00405400u, "C2 MMUCR");
  // R6: the entry an LDTLB writes is seen by the access in EX in the same cycle (and the next)
  hard_clear();
  wreg(R_MMUCR, M_AT | M_URC(7));
  wreg(R_PTEH, 0x4C0B0000u); wreg(R_PTEL, 0x0C0B0000u | 0x100 | 0x10 | 0x60 | 0x04); wreg(R_PTEA, 0);
  DRes r = dex_rtl(0x4C0B0010u, DK_LD, true, false);
  CHECK(r.fx == FX_TLB_MISS, "before LDTLB: fx %d", r.fx);
  d->ldtlb = 1;
  r = dex_rtl(0x4C0B0010u, DK_LD, true, false);
  CHECK(r.fx == FX_NONE && r.pa == 0x0C0B0010u, "R6: LDTLB not visible in its own cycle (fx %d pa %08x)", r.fx, r.pa);
  tick(); d->ldtlb = 0; M.ldtlb(); M0.ldtlb();
  r = dex_rtl(0x4C0B0010u, DK_LD, true, false);
  CHECK(r.fx == FX_NONE, "after LDTLB: fx %d", r.fx);
  d->dex_v = 0;
  wreg(R_MMUCR, 0);
  hard_clear();
  section_end("ldtlb");
}

// URC: D2, exhaustive urc_next, ma_commit against a MMUCR write
static void t_urc() {
  section("urc");
  reset_por();
  auto commit = [&]() { d->ma_commit = 1; ev(); tick(); d->ma_commit = 0; ev(); M.urc_step(); M0.urc_step(); };
  static const struct { int urb, urc; int seq[8]; } D2[6] = {
    {8, 6, {7, 0, 1, 2, 3, 4, 5, 6}}, {8, 61, {62, 63, 0, 1, 2, 3, 4, 5}}, {0, 62, {63, 0, 1, 2, 3, 4, 5, 6}},
    {1, 0, {0, 0, 0, 0, 0, 0, 0, 0}}, {63, 61, {62, 0, 1, 2, 3, 4, 5, 6}}, {2, 1, {0, 1, 0, 1, 0, 1, 0, 1}}};
  for (auto &t : D2) {
    wreg(R_MMUCR, M_AT | M_URB(t.urb) | M_URC(t.urc));
    for (int i = 0; i < 8; i++) {
      commit();
      CHECK(urc_now() == (uint32_t)t.seq[i], "D2 URB %d URC %d step %d: %u console %d", t.urb, t.urc, i, M.urc,
            t.seq[i]);
    }
  }
  for (int urb = 0; urb < 64; urb++)
    for (int urc = 0; urc < 64; urc++) {
      wreg(R_MMUCR, M_URB(urb) | M_URC(urc));
      commit();
      uint32_t want = urc_next((uint32_t)urc, (uint32_t)urb);   // the spec rule (mmu 3.6) directly
      uint32_t got = urc_now();
      CHECK(got == want, "urc_next(%d, URB %d) = %u, want %u", urc, urb, got, want);
    }
  // a MMUCR write lands over a URC step of the same cycle (mmu 7.6 priority 1 over 3)
  wreg(R_MMUCR, M_URC(9));
  d->ma_commit = 1;
  reg_acc(true, R_MMUCR, M_URC(20), 2);
  d->ma_commit = 0; ev();
  CHECK(urc_now() == 20, "MMUCR write vs URC step: URC %u (the write wins)", (rreg(R_MMUCR) >> 10) & 63);
  wreg(R_MMUCR, 0);
  section_end("urc");
}

// LRUI: E1 / E1b / E1c, exhaustive victim and use
static void e_setup5() { hard_clear(); for (int k = 0; k < 5; k++) umap(k, CODE(k), 3, 1); }
static void t_lrui() {
  section("lrui");
  reset_por();
  // E1 (run1.log): page:LRUI-after[ITLB e0..e3]
  static const int seq[13] = {0, 1, 2, 3, 0, 4, 1, 0, 2, 3, 4, 4, 1};
  static const char *e1 = "p0:0b[---0] p1:1e[--10] p2:38[-210] p3:00[3210] p0:0b[3210] p4:1e[3240] p1:38[3140] "
                          "p0:3b[3140] p2:03[2140] p3:16[2130] p4:30[2430] p4:30[2430] p1:3b[2431] ";
  e_setup5();
  wreg(R_MMUCR, M_AT);
  std::string s;
  for (int i = 0; i < 13; i++) {
    fetch_chk(VA(CODE(seq[i])), true, "E1");
    char b[40];
    snprintf(b, sizeof b, "p%d:%02x[%s] ", seq[i], lrui_now(), ipages().c_str());
    s += b;
  }
  CHECK(s == e1, "E1:\n    RTL     %s\n    console %s", s.c_str(), e1);
  // E1b: all four valid (fill 0..3 from LRUI 0: [3210], LRUI 00), then LRUI forced, fetch p4
  static const struct { uint32_t lv, after; const char *pages; } E1b[7] = {
    {0x38, 0x00, "4210"}, {0x06, 0x20, "3410"}, {0x01, 0x14, "3240"}, {0x00, 0x0b, "3214"}, {0x3f, 0x07, "4210"},
    {0x1e, 0x38, "3410"}, {0x07, 0x21, "3410"}};
  for (auto &t : E1b) {
    e_setup5();
    wreg(R_MMUCR, M_AT);
    for (int p = 0; p < 4; p++) fetch_chk(VA(CODE(p)), true, "E1b fill");
    CHECK(lrui_now() == 0 && ipages() == "3210", "E1b fill: lrui %02x itlb %s", lrui_now(), ipages().c_str());
    wreg(R_MMUCR, M_AT | M_LRUI(t.lv));
    fetch_chk(VA(CODE(4)), true, "E1b");
    CHECK(lrui_now() == t.after && ipages() == t.pages, "E1b set %02x: lrui %02x itlb %s, console %02x %s", t.lv,
          lrui_now(), ipages().c_str(), t.after, t.pages);
  }
  // E1c: invalid entries are not preferred
  e_setup5();
  wreg(R_MMUCR, M_AT);
  fetch_chk(VA(CODE(3)), true, "E1c");
  CHECK(ipages() == "---3", "E1c after p3: %s", ipages().c_str());
  wreg(R_MMUCR, M_AT);
  fetch_chk(VA(CODE(0)), true, "E1c");
  CHECK(ipages() == "---0" && lrui_now() == 0x0b, "E1c: itlb %s lrui %02x (console [- - - 0] 0b)", ipages().c_str(),
        lrui_now());
  // exhaustive: victim for every LRUI value (the refill of a fifth page) and use for every (LRUI, entry) (a hit)
  for (uint32_t l = 0; l < 64; l++) {
    e_setup5();
    wreg(R_MMUCR, M_AT);
    for (int p = 0; p < 4; p++) fetch_chk(VA(CODE(p)), true, "victim fill");
    wreg(R_MMUCR, M_AT | M_LRUI(l));
    FRes r = fetch_chk(VA(CODE(4)), true, "victim");
    int want = lrui_victim(l);
    CHECK(r.fill_idx == want, "lrui_victim(%02x): refilled entry %d, want %d", l, r.fill_idx, want);
    CHECK(lrui_now() == lrui_use(l, want), "LRUI after the refill of %02x: %02x", l, lrui_now());
    for (int e = 0; e < 4; e++) {
      wreg(R_MMUCR, M_AT | M_LRUI(l));
      // entry e maps page 3 - e after the fill, except the victim, which now maps page 4
      int page = e == want ? 4 : 3 - e;
      fetch_chk(VA(CODE(page)) + 0x40, true, "use");
      CHECK(lrui_now() == lrui_use(l, e), "lrui_use(%02x, e%d) = %02x, want %02x", l, e, lrui_now(), lrui_use(l, e));
    }
  }
  wreg(R_MMUCR, 0);
  hard_clear();
  section_end("lrui");
}

// E2, refill costs, xl_stall, the array stall while the refill owns the UTLB
static void t_refill() {
  section("refill");
  reset_por();
  // E2 [HW]: 4K page fetched at +0xC00, SH 1, ASID 5A, WT 1, SA 5, TC 1
  hard_clear();
  utlb_set(0, VA(CODE(0)), CODE(0), SZ4K, 3, 1, 1, 1, 1, 0x5A, 5, 1);
  wreg(R_PTEH, 0); wreg(R_TEA, 0x7EA7EA00u);
  wreg(R_MMUCR, M_AT);
  fetch_chk(VA(CODE(0)) + 0xC00, true, "E2");
  CHECK(rarr(ITLB_AA(3)) == 0x4c090d5au, "E2 ITLB3 aa %08x", rarr(ITLB_AA(3)));
  CHECK((rarr(ITLB_DA1(3)) & DA1_B9) == 0x0c09015au, "E2 ITLB3 da1");
  CHECK(rarr(ITLB_DA2(3)) == 0x0du, "E2 ITLB3 da2");
  CHECK(rarr(UTLB_AA(0)) == 0x4c09035au && (rarr(UTLB_DA1(0)) & DA1_B9) == 0x0c09017fu, "E2 UTLB0");
  CHECK(rreg(R_TEA) == 0x7ea7ea00u && rreg(R_PTEH) == 0 && urc_now() == 0, "E2: the refill touched TEA / PTEH / URC");
  wreg(R_MMUCR, 0);
  hard_clear();
  utlb_set(5, 0x6C000000u, 0x0C000000u, SZ1M, 2, 1, 1, 0, 0, 0, 0, 0);
  wreg(R_MMUCR, M_AT);
  fetch_chk(0x6C090800u, true, "E2 1M");
  CHECK(rarr(ITLB_AA(3)) == 0x6c090900u && (rarr(ITLB_DA1(3)) & DA1_B9) == (0x0c0003d8u & DA1_B9), "E2 1M ITLB3");
  // U: a user fetch from a PR 01 page: refilled with PR 0, then the protection violation
  wreg(R_MMUCR, 0);
  hard_clear();
  umap(10, CODE(10), 1, 1);
  wreg(R_MMUCR, M_AT);
  FRes u = fetch_chk(VA(CODE(10)), false, "U fetch PR01");
  CHECK(u.fx == FX_TLB_PROT && u.cyc == g_refill_cost, "U fetch PR01: fx %d after %d", u.fx, u.cyc);
  int e10 = -1;
  for (int e = 0; e < 4; e++) if ((rarr(ITLB_AA(e)) & 0xFFFFF100u) == (VA(CODE(10)) | 0x100)) e10 = e;
  CHECK(e10 >= 0 && (rarr(ITLB_DA1(e10)) & DA1_B9) == (0x0c09a318u & DA1_B9), "U ITLB entry for the PR01 page");
  // costs: a UTLB miss answers the poison at m + 3; a UTLB multiple hit during the refill likewise (MHIT, 0x140)
  wreg(R_MMUCR, 0);
  hard_clear();
  wreg(R_MMUCR, M_AT);
  FRes r = fetch_chk(VA(CODE(7)), true, "UTLB miss");
  CHECK(r.fx == FX_TLB_MISS && r.vec400 && r.cyc == 3, "UTLB miss: fx %d vec400 %d at +%d", r.fx, r.vec400, r.cyc);
  utlb_set(1, VA(CODE(7)), CODE(7), SZ4K, 3, 1, 1, 0, 0, 0, 0, 0);
  utlb_set(2, VA(CODE(7)), CODE(7), SZ64K, 3, 1, 1, 0, 0, 0, 0, 0);
  r = fetch_chk(VA(CODE(7)) + 4, true, "UTLB multiple hit");
  CHECK(r.fx == FX_MHIT && !r.vec400 && r.cyc == 3, "refill multiple hit: fx %d at +%d", r.fx, r.cyc);
  // the ITLB itself with two matches: MHIT at once
  warr(ITLB_DA1(0), CODE(8) | 0x100 | 0x10 | 0x40); warr(ITLB_AA(0), VA(CODE(8)) | 0x100);
  warr(ITLB_DA1(1), CODE(8) | 0x100 | 0x90 | 0x40); warr(ITLB_AA(1), VA(CODE(8)) | 0x100);
  r = fetch_chk(VA(CODE(8)), true, "ITLB multiple hit");
  CHECK(r.fx == FX_MHIT && r.cyc == 0, "ITLB multiple hit: fx %d at +%d", r.fx, r.cyc);
  // region poisons: at once, whatever the TLBs hold (SOC.md 4.1 item 2)
  r = fetch_chk(0xE0001000u, true, "fetch P4/SQ");
  CHECK(r.fx == FX_ADDR && r.cyc == 0, "fetch from the SQ: fx %d", r.fx);
  r = fetch_chk(0x8C010000u, false, "user fetch P1");
  CHECK(r.fx == FX_ADDR, "user fetch from P1 [HW U]: fx %d", r.fx);
  set_ccr(true, false, false);
  r = fetch_chk(0x7C000100u, true, "fetch OC RAM");
  CHECK(r.fx == FX_ADDR, "fetch from the OC RAM window with ORA = 1: fx %d", r.fx);
  set_ccr(false, false, false);
  r = fetch_chk(0x8C010000u, true, "priv fetch P1");
  CHECK(r.hit && r.pa == 0x0C010000u && r.c, "P1 fetch: hit %d pa %08x c %d", r.hit, r.pa, r.c);
  r = fetch_chk(0xAC010000u, true, "priv fetch P2");
  CHECK(r.hit && !r.c, "P2 fetch is not cacheable");
  // a translated fetch into area 7 is poisoned [P]
  utlb_set(4, 0x3F000000u, 0x1F000000u, SZ1M, 3, 0, 1, 0, 0, 0, 0, 0);
  r = fetch_chk(0x3F000100u, true, "fetch area 7");
  CHECK(r.fx == FX_ADDR && r.cyc == g_refill_cost, "translated fetch into area 7: fx %d after %d", r.fx, r.cyc);
  // the speculative cases at the MMU port (mmu 3.5 R1 / V): the request abandoned by a redirect one cycle after the
  // miss; the redirect waits for busy (if_stall) to fall.  Cost = the target's fetch cycle minus the no-miss one.
  for (int mapped = 1; mapped >= 0; mapped--) {
    wreg(R_MMUCR, 0);
    hard_clear();
    umap(5, CODE(5), 3, 1);
    if (mapped) umap(6, CODE(6), 3, 1);
    wreg(R_MMUCR, M_AT);
    fetch_chk(VA(CODE(5)) + 0xFFC, true, "R1 rts page");
    // cycle m: the fetch unit asks for the next page's first word; at m + 1 the RTS redirects
    uint64_t m = g_cyc;
    d->if_v = 1; d->if_va = VA(CODE(6)); d->if_md = 1;
    int acked = -1;
    for (int k = 0; k < 16 && acked < 0; k++) {
      ev();
      if (d->if_hit || d->if_fx) acked = k;
      tick();
    }
    d->if_v = 0; ev();
    int target_fetch = acked + 1;                  // the redirect's target is fetched the cycle after busy falls
    int cost = target_fetch - 2;                   // without the miss: redirect at m + 1, target at m + 2
    CHECK(cost == (mapped ? 6 : 2), "speculative %s: cost %d (console %s)", mapped ? "refill" : "UTLB miss", cost,
          mapped ? "6: 9 -> 15" : "2-4: 9 -> 11..13");
    (void)m;
    if (mapped) { IXlat x = M.fetch(VA(CODE(6)), true); (void)x; }
    else M.lookup(VA(CODE(6)), true);
    CHECK(ipages() == (mapped ? "--65" : "---5"), "E4 / R1 itlb [%s] (console [- - %s 5])", ipages().c_str(),
          mapped ? "6" : "-");
  }
  // xl_stall: the UTLB's port A is the refill's in RF_SRCH; a UTLB array access stalls once more in SRCH / RD
  wreg(R_MMUCR, 0);
  hard_clear();
  umap(1, CODE(1), 3, 1);
  umap(2, DATA(2), 3, 1);
  wreg(R_MMUCR, M_AT);
  d->if_v = 1; d->if_va = VA(CODE(1)); d->if_md = 1;
  ev();                                            // cycle m: the miss
  CHECK(d->if_stall, "refill m: if_stall");
  d->xl_req = 1; d->xl_itlb = 0; d->xl_va = VA(DATA(2)) + 0x20;
  ev();
  CHECK(!d->xl_stall && d->xl_hit, "xl in cycle m: stall %d hit %d", d->xl_stall, d->xl_hit);
  tick(); ev();                                    // m + 1: RF_SRCH
  CHECK(d->xl_stall && !d->xl_hit, "xl during the refill's search: stall %d hit %d", d->xl_stall, d->xl_hit);
  d->xl_itlb = 1; ev();
  CHECK(!d->xl_stall, "an ITLB xl lookup stalled during the UTLB search");
  d->xl_req = 0; d->xl_itlb = 0;
  d->arr_rd = 1; d->arr_blk = 9; d->arr_off = UTLB_DA1(2) & 0x0FFFFFFF; d->arr_size = 2; ev();
  CHECK(d->arr_stall, "UTLB array strobe in SRCH");
  tick(); ev();                                    // m + 2: RF_RD (the array's second strobe)
  CHECK(d->arr_stall, "UTLB array strobe in the refill's data read: no stall");
  tick(); ev();                                    // m + 3: RF_WR: the array captures
  CHECK(!d->arr_stall && d->arr_rdata == ((DATA(2) & 0x1FFFFC00u) | 0x100 | 0x10 | 0x60 | 0x08 | 0x04),
        "UTLB array read after the refill's use: stall %d data %08x", d->arr_stall, d->arr_rdata);
  tick(); d->arr_rd = 0; ev();
  for (int k = 0; k < 8 && !d->if_hit; k++) { tick(); ev(); }
  CHECK(d->if_hit, "refill never completed");
  tick(); d->if_v = 0; ev();
  M.fetch(VA(CODE(1)), true);
  wreg(R_MMUCR, 0);
  hard_clear();
  section_end("refill");
}

// F / G / S / U, D1 / R4, R5, R6, excw
static void t_data() {
  section("data");
  reset_por();
  hard_clear();
  const uint32_t d0 = VA(DATA(0)), d1 = VA(DATA(1)), d2 = VA(DATA(2)), d3 = VA(DATA(3)), d5 = VA(DATA(5)),
                 un = VA(DATA(7));
  umap(16, DATA(0), 3, 1); umap(17, DATA(1), 0, 1); umap(18, DATA(2), 2, 1);
  umap(19, DATA(3), 1, 0); umap(20, DATA(4), 0, 0); umap(21, DATA(5), 3, 0);
  wreg(R_MMUCR, M_AT);
  // F rows (run1.log): the first faulting access of the instruction gives EXPEVT and TEA
  struct Acc1 { int kind, size; uint32_t va; bool rmw; };
  struct Row { const char *nm; std::vector<Acc1> acc; uint32_t ev, tea; };
  const std::vector<Row> F = {
    {"ld32 unmapped", {{DK_LD, 2, un + 0x124, 0}}, 0x040, 0x4c0b7124},
    {"st32 unmapped", {{DK_ST, 2, un + 0x124, 0}}, 0x060, 0x4c0b7124},
    {"ld8 unmapped", {{DK_LD, 0, un + 0x3FF, 0}}, 0x040, 0x4c0b73ff},
    {"ld16 unmapped +1", {{DK_LD, 1, un + 1, 0}}, 0x0e0, 0x4c0b7001},
    {"st32 unmapped +2", {{DK_ST, 2, un + 2, 0}}, 0x100, 0x4c0b7002},
    {"ld32 dp0 +2", {{DK_LD, 2, d0 + 2, 0}}, 0x0e0, 0x4c0b0002},
    {"ldf64 dp0 +4", {{DK_LD, 3, d0 + 4, 0}}, 0x0e0, 0x4c0b0004},
    {"ldf64 unmapped", {{DK_LD, 3, un, 0}}, 0x040, 0x4c0b7000},
    {"ld32 dp1 PR00", {{DK_LD, 2, d1, 0}}, 0, 0},
    {"st32 dp1 PR00", {{DK_ST, 2, d1, 0}}, 0x0c0, 0x4c0b1000},
    {"ld32 dp2 PR10", {{DK_LD, 2, d2, 0}}, 0, 0},
    {"st32 dp2 PR10", {{DK_ST, 2, d2, 0}}, 0x0c0, 0x4c0b2000},
    {"ld32 dp3 PR01 D0", {{DK_LD, 2, d3, 0}}, 0, 0},
    {"st32 dp3 PR01 D0", {{DK_ST, 2, d3, 0}}, 0x080, 0x4c0b3000},
    {"st32 dp4 PR00 D0", {{DK_ST, 2, VA(DATA(4)), 0}}, 0x0c0, 0x4c0b4000},
    {"st8 dp5 PR11 D0", {{DK_ST, 0, d5 + 3, 0}}, 0x080, 0x4c0b5003},
    {"st32 dp0 PR11 D1", {{DK_ST, 2, d0 + 0x100, 0}}, 0, 0},
    {"and.b gbr unmapped", {{DK_RMW_RD, 0, un, 1}, {DK_RMW_WR, 0, un, 0}}, 0x040, 0x4c0b7000},
    {"and.b gbr dp1 PR00", {{DK_RMW_RD, 0, d1 + 0x10, 1}, {DK_RMW_WR, 0, d1 + 0x10, 0}}, 0x0c0, 0x4c0b1010},
    {"and.b gbr dp3 D0", {{DK_RMW_RD, 0, d3 + 0x10, 1}, {DK_RMW_WR, 0, d3 + 0x10, 0}}, 0x080, 0x4c0b3010},
    {"tst.b gbr dp1 PR00", {{DK_RMW_RD, 0, d1 + 0x10, 0}}, 0, 0},
    {"tst.b gbr unmapped", {{DK_RMW_RD, 0, un, 0}}, 0x040, 0x4c0b7000},
    {"tas.b unmapped", {{DK_TAS_PURGE, 0, un + 5, 1}, {DK_TAS_RD, 0, un + 5, 0}, {DK_TAS_WR, 0, un + 5, 0}}, 0x040,
     0x4c0b7005},
    {"tas.b dp1 PR00", {{DK_TAS_PURGE, 0, d1 + 0x11, 1}, {DK_TAS_RD, 0, d1 + 0x11, 0}, {DK_TAS_WR, 0, d1 + 0x11, 0}},
     0x0c0, 0x4c0b1011},
    {"tas.b dp3 D0", {{DK_TAS_PURGE, 0, d3 + 0x11, 1}, {DK_TAS_RD, 0, d3 + 0x11, 0}, {DK_TAS_WR, 0, d3 + 0x11, 0}},
     0x080, 0x4c0b3011},
    {"ocbi unmapped", {{DK_OCBI, 2, un + 0x40, 0}}, 0x060, 0x4c0b7040},
    {"ocbp unmapped", {{DK_OCBP, 2, un + 0x40, 0}}, 0x040, 0x4c0b7040},
    {"ocbwb unmapped", {{DK_OCBWB, 2, un + 0x40, 0}}, 0x040, 0x4c0b7040},
    {"pref unmapped", {{DK_PREF, 2, un + 0x40, 0}}, 0, 0},
    {"movca unmapped", {{DK_MOVCA, 2, un + 0x40, 0}}, 0x060, 0x4c0b7040},
    {"ocbi dp1 PR00", {{DK_OCBI, 2, d1 + 0x40, 0}}, 0x0c0, 0x4c0b1040},
    {"ocbi dp3 D0", {{DK_OCBI, 2, d3 + 0x40, 0}}, 0x080, 0x4c0b3040},
    {"ocbp dp1 PR00", {{DK_OCBP, 2, d1 + 0x40, 0}}, 0, 0},
    {"ocbwb dp1 PR00", {{DK_OCBWB, 2, d1 + 0x40, 0}}, 0, 0},
    {"movca dp1 PR00", {{DK_MOVCA, 2, d1 + 0x40, 0}}, 0x0c0, 0x4c0b1040},
    {"movca dp3 D0", {{DK_MOVCA, 2, d3 + 0x40, 0}}, 0x080, 0x4c0b3040},
    {"pref dp1 PR00", {{DK_PREF, 2, d1 + 0x40, 0}}, 0, 0},
    {"ocbi misaligned dp0+3", {{DK_OCBI, 2, d0 + 0x203, 0}}, 0, 0},
    {"mac.l dp0, unmapped", {{DK_LD, 2, un + 8, 0}, {DK_LD, 2, d0 + 0x80, 0}}, 0x040, 0x4c0b7008},
    {"mac.l unmapped, dp0", {{DK_LD, 2, d0 + 0x80, 0}, {DK_LD, 2, un + 8, 0}}, 0x040, 0x4c0b7008},
    {"mov.l @r4+ unmapped", {{DK_LD, 2, un + 0x20, 0}}, 0x040, 0x4c0b7020},
    {"mov.l r5,@-r4 unmapped", {{DK_ST, 2, un + 0x20, 0}}, 0x060, 0x4c0b7020}};
  for (auto &row : F) {
    uint32_t ev = 0, tea = 0;
    for (auto &a : row.acc) {
      int fx;
      uint32_t e = core_expevt(a.va, a.kind, a.size, true, a.rmw, &fx);
      if (e) { ev = e; tea = a.va; break; }
    }
    CHECK(ev == row.ev && tea == row.tea, "F %s: ev %03x tea %08x, console %03x %08x", row.nm, ev, tea, row.ev,
          row.tea);
  }
  // a PREF whose translation fails is dropped: region RG_P4RSV (the LSU acks it with no transaction) [HW run 3 P]
  DRes r = dex_chk(un + 0x40, DK_PREF, true, false, "pref unmapped");
  CHECK(r.rg == RG_P4RSV && r.fx == FX_NONE, "PREF unmapped: rg %d fx %d", r.rg, r.fx);
  r = dex_chk(d1 + 0x40, DK_PREF, true, false, "pref mapped");
  CHECK(r.rg == RG_MEM && r.pa == DATA(1) + 0x40 && r.c, "PREF of a mapped P0 line fills through the translation");
  // D1 / R4: which accesses URC counts (the model's `counted`; the RTL steps on ma_commit, checked in t_urc)
  struct { const char *nm; uint32_t va; int kind; bool rmw; int n; } D1[] = {
    {"P1 load", DATA(0) | 0x80000000u, DK_LD, 0, 0}, {"P0 load", d0, DK_LD, 0, 1}, {"P0 store", d0 + 0x40, DK_ST, 0, 1},
    {"P0 pref", d0 + 0x60, DK_PREF, 0, 0}, {"P0 ocbwb", d0 + 0x60, DK_OCBWB, 0, 1}, {"P0 ocbp", d0 + 0x60, DK_OCBP, 0, 1},
    {"P0 ocbi", d0 + 0x60, DK_OCBI, 0, 1}, {"P0 movca", d0 + 0x60, DK_MOVCA, 0, 1},
    {"P0 load TLB miss", VA(DATA(7)), DK_LD, 0, 0}, {"P0 store prot", d1, DK_ST, 0, 0},
    {"P0 and.b read", d0 + 0x10, DK_RMW_RD, 1, 1}, {"P0 and.b write", d0 + 0x10, DK_RMW_WR, 0, 1},
    {"P0 tas.b purge", d0 + 0x11, DK_TAS_PURGE, 1, 1}, {"P0 store D0 (IPW)", d3, DK_ST, 0, 0},
    {"P2 load", DATA(0) | 0xA0000000u, DK_LD, 0, 0}, {"P4 register", 0xFF000010u, DK_LD, 0, 0},
    {"SQ store unmapped (queue written, 0x060)", 0xE0000040u, DK_ST, 0, 0}};
  for (auto &t : D1) {
    DXlat m = M.data(t.va, t.kind, true, t.rmw);
    CHECK((int)m.counted == t.n, "D1 %s: counted %d, console +%d", t.nm, m.counted, t.n);
    DRes x = dex_chk(t.va, t.kind, true, t.rmw, "D1");
    CHECK((int)x.cnt == t.n, "D1 %s: mmu_urc_counts %d, console +%d", t.nm, x.cnt, t.n);
  }
  // with AT = 0 nothing counts
  wreg(R_MMUCR, 0);
  CHECK(!dex_chk(d0, DK_LD, true, false, "D1 AT0").cnt, "D1 P0 load with AT = 0 counted");
  wreg(R_MMUCR, M_AT);
  // CCR.OCE: dxlat.c is the page's C AND OCE (mmu 3.4), for translated and untranslated accesses alike
  set_ccr(false, false, false, false);
  CHECK(!dex_chk(d0, DK_LD, true, false, "OCE0 P0").c && !dex_chk(DATA(0) | 0x80000000u, DK_LD, true, false,
        "OCE0 P1").c, "CCR.OCE = 0: a cacheable access");
  set_ccr(false, false, false, true);
  CHECK(dex_chk(d0, DK_LD, true, false, "OCE1 P0").c && dex_chk(DATA(0) | 0x80000000u, DK_LD, true, false,
        "OCE1 P1").c && !dex_chk(DATA(0) | 0xA0000000u, DK_LD, true, false, "OCE1 P2").c, "CCR.OCE = 1: cacheability");
  // G rows (run1.log): page sizes, low bits, ASID / SH / SV, P3, area 7
  wreg(R_MMUCR, 0);
  hard_clear();
  const uint32_t pd = DATA(0);
  umap(16, DATA(0), 3, 1);
  const uint32_t v1k = 0x30000000u | pd, v64 = 0x50000000u | (pd & 0x0FFF0000u), v1m = 0x60000000u | (pd & 0x0FF00000u);
  utlb_set(24, v1k, pd, SZ1K, 3, 0, 1, 0, 0, 0, 0, 0);
  utlb_set(25, v64, pd & 0x1FFF0000u, SZ64K, 3, 0, 1, 0, 0, 0, 0, 0);
  utlb_set(26, v1m, pd & 0x1FF00000u, SZ1M, 3, 0, 1, 0, 0, 0, 0, 0);
  wreg(R_MMUCR, M_AT);
  struct { const char *nm; uint32_t va, pa; } G[] = {
    {"1K +0", v1k, pd}, {"1K +3fc", v1k + 0x3FC, pd + 0x3FC}, {"1K +400 (miss)", v1k + 0x400, 0xFFFFFFFFu},
    {"4K dp0 +ffc", VA(DATA(0)) + 0xFFC, pd + 0xFFC}, {"4K dp6 (miss)", VA(DATA(6)), 0xFFFFFFFFu},
    {"64K +0", v64 + (pd & 0xFFFF), pd}, {"64K +fffc", v64 + 0xFFFC, (pd & 0x1FFF0000u) + 0xFFFC},
    {"64K +10000 (miss)", v64 + 0x10000, 0xFFFFFFFFu}, {"1M +off", v1m + (pd & 0xFFFFF), pd},
    {"1M +ffffc", v1m + 0xFFFFC, (pd & 0x1FF00000u) + 0xFFFFC}, {"1M +100000 (miss)", v1m + 0x100000, 0xFFFFFFFFu}};
  for (auto &g : G) {
    DRes x = dex_chk(g.va, DK_LD, true, false, g.nm);
    if (g.pa == 0xFFFFFFFFu) CHECK(x.fx == FX_TLB_MISS && x.vec400, "G %s: fx %d", g.nm, x.fx);
    else CHECK(x.fx == FX_NONE && x.pa == g.pa, "G %s: fx %d pa %08x want %08x", g.nm, x.fx, x.pa, g.pa);
  }
  wreg(R_MMUCR, 0); hard_clear();
  const uint32_t vx = 0x08000000u | DATA(1), px = DATA(1);
  utlb_set(27, vx | 0x400, px, SZ4K, 3, 0, 1, 0, 0, 0, 0, 0);
  wreg(R_MMUCR, M_AT);
  for (uint32_t o : {0x000u, 0x400u, 0xC00u}) {
    DRes x = dex_chk(vx + o, DK_LD, true, false, "G VPN|400");
    CHECK(x.fx == FX_NONE && x.pa == px + o, "G 4K VPN|400 +%x: pa %08x", o, x.pa);
  }
  wreg(R_MMUCR, 0); hard_clear();
  utlb_set(27, vx, px | 0x400, SZ4K, 3, 0, 1, 0, 0, 0, 0, 0);
  wreg(R_MMUCR, M_AT);
  for (uint32_t o : {0x000u, 0x800u}) {
    DRes x = dex_chk(vx + o, DK_LD, true, false, "G PPN|400");
    CHECK(x.fx == FX_NONE && x.pa == px + o, "G 4K PPN|400 +%x: pa %08x (console: pa + 0)", o, x.pa);
  }
  wreg(R_MMUCR, 0); hard_clear();
  utlb_set(25, v64, (pd & 0x1FFF0000u) | 0xFC00u, SZ64K, 3, 0, 1, 0, 0, 0, 0, 0);
  wreg(R_MMUCR, M_AT);
  r = dex_chk(v64 + 0x100, DK_LD, true, false, "G 64K PPN|fc00");
  CHECK(r.pa == (pd & 0x1FFF0000u) + 0x100, "G 64K PPN|fc00: pa %08x", r.pa);
  wreg(R_MMUCR, 0); hard_clear();
  const uint32_t va_ = 0x20000000u | DATA(2), pa_ = DATA(2);
  utlb_set(28, va_, pa_, SZ4K, 3, 0, 1, 0, 0, 0x12, 0, 0);
  wreg(R_PTEH, 0);
  wreg(R_MMUCR, M_AT);
  CHECK(dex_chk(va_, DK_LD, true, false, "G ASID 00").fx == FX_TLB_MISS, "G ASID12 SH0, PTEH.ASID 00: not a miss");
  wreg(R_PTEH, 0x12);
  CHECK(dex_chk(va_, DK_LD, true, false, "G ASID 12").pa == pa_, "G ASID12 SH0, PTEH.ASID 12");
  CHECK(dex_chk(va_ + 0x1000, DK_LD, true, false, "G miss").fx == FX_TLB_MISS, "G PTEH.ASID 12, miss elsewhere");
  wreg(R_PTEH, 0);
  // PTEH has no landing window for the TLBs (M2-fix F1) [HW tools/hw/mmu/ptehwin: the very next load after PTEH.ASID
  // 1 -> 2 is translated with the new ASID]: in a PTEH write's capture cycle the UTLB compare (the access held in EX
  // behind the store) and the ITLB compare already use the written ASID; in its stall cycle the old one.  The asid
  // output (v1_ubc's) is the register: the old ASID in the capture cycle, the new one after it [HW ptehubc: the UBC
  // takes PTEH.ASID at its landing like one of its own registers]
  warr(ITLB_DA2(1), 0);
  warr(ITLB_DA1(1), (pa_ & 0x1FFFFC00u) | 0x100u | 0x40u | 0x10u | 0x08u);        // V, PR 1, SZ 4K, C
  warr(ITLB_AA(1), (va_ & 0xFFFFFC00u) | 0x100u | 0x12u);                         // ASID 12, SH 0
  d->reg_rd = 0; d->reg_wr = 1; d->reg_size = 2; d->reg_off = R_PTEH; d->reg_wdata = 0x00000012u;
  {
    DRes s = dex_rtl(va_, DK_LD, true, false);                                     // the first strobe
    CHECK(d->reg_stall && s.fx == FX_TLB_MISS && d->asid == 0x00 && d->asid0 == 0x00,
          "PTEH write, stall cycle: stall %d fx %d asid %02x / %02x (want the old ASID 00)", d->reg_stall, s.fx,
          d->asid, d->asid0);
    tick();
    d->if_v = 1; d->if_va = va_; d->if_md = 1;
    s = dex_rtl(va_, DK_LD, true, false);                                          // the capture cycle
    CHECK(!d->reg_stall && s.fx == FX_NONE && s.pa == pa_,
          "PTEH write, capture cycle: stall %d fx %d pa %08x (want the TLB compare with the written ASID 12: no "
          "landing window)", d->reg_stall, s.fx, s.pa);
    CHECK(d->asid == 0x00 && d->asid0 == 0x00, "PTEH write, capture cycle: the asid output (the UBC's) %02x / %02x, "
          "want the register's 00 until the landing", d->asid, d->asid0);
    CHECK(d->if_hit && !d->if_stall && d->if_pa == pa_, "PTEH write, capture cycle: the fetch's ITLB compare hit %d "
          "stall %d pa %08x (want the written ASID 12)", d->if_hit, d->if_stall, d->if_pa);
    tick();
    d->reg_wr = 0; d->dex_v = 0; d->if_v = 0; ev();
    M.reg_wr(R_PTEH, 2, 0x00000012u); M0.reg_wr(R_PTEH, 2, 0x00000012u);
    M.fetch(va_, true); M0.fetch(va_, true);                                       // (the hit's LRUI use)
    CHECK(d->asid == 0x12 && d->asid0 == 0x12, "after the PTEH write: the asid output %02x / %02x, want 12", d->asid,
          d->asid0);
  }
  CHECK(rreg(R_PTEH) == 0x12u, "PTEH after the write-through check");
  wreg(R_PTEH, 0);
  wreg(R_MMUCR, 0); hard_clear();
  utlb_set(28, va_, pa_, SZ4K, 3, 0, 1, 1, 0, 0x12, 0, 0);
  wreg(R_MMUCR, M_AT);
  CHECK(dex_chk(va_, DK_LD, true, false, "G SH1").fx == FX_NONE, "G ASID12 SH1, PTEH.ASID 00");
  wreg(R_MMUCR, M_SV | M_TI);
  utlb_set(28, va_, pa_, SZ4K, 3, 0, 1, 0, 0, 0x12, 0, 0);
  wreg(R_MMUCR, M_SV | M_AT);
  CHECK(dex_chk(va_, DK_LD, true, false, "G SV").fx == FX_NONE, "G SV=1 priv, ASID12 SH0, PTEH 00");
  CHECK(dex_chk(va_, DK_LD, false, false, "G SV user").fx == FX_TLB_MISS, "SV=1 user mode compares the ASID [M]");
  wreg(R_MMUCR, M_TI); hard_clear();
  const uint32_t v3 = 0xC0000000u | DATA(3);
  utlb_set(29, v3, DATA(3), SZ4K, 3, 0, 1, 0, 0, 0, 0, 0);
  wreg(R_MMUCR, M_AT);
  CHECK(dex_chk(v3, DK_LD, true, false, "G P3").pa == DATA(3), "G P3 mapped");
  CHECK(dex_chk(v3 + 0x1000, DK_LD, true, false, "G P3 miss").fx == FX_TLB_MISS, "G P3 next page (miss)");
  wreg(R_MMUCR, 0); hard_clear();
  utlb_set(30, 0x3F000000u, 0x1F000000u, SZ1M, 1, 0, 1, 0, 0, 0, 0, 0);
  wreg(R_MMUCR, M_AT);
  r = dex_chk(0x3F00000Cu, DK_LD, true, false, "G area 7 TEA");
  CHECK(r.rg == RG_P4REG && r.pa == 0x1F00000Cu && r.fx == FX_NONE && !r.c, "G area 7 via TLB: rg %d pa %08x", r.rg,
        r.pa);
  CHECK(M.data(0x3F000010u, DK_LD, true, false).counted, "G area 7 access counts for URC [HW]");
  utlb_set(31, 0x3C000000u, 0x1C000000u, SZ1M, 3, 0, 1, 0, 0, 0, 0, 0);
  r = dex_chk(0x3C000010u, DK_LD, true, false, "translated 1C");
  CHECK(r.rg == RG_P4RSV, "translated PA 1C: rg %d (RG_P4RSV [P])", r.rg);
  // S rows: SQ with the MMU on (1 MB, PA 0c000000 -> the buffer at 0c073d20)
  const uint32_t SQA = 0xE0000000u | (SQBUF & 0x000FFFE0u);
  struct { const char *nm; int map, pr, dd; uint32_t st, pf; } S[] = {
    {"no SQ mapping", 0, 1, 1, 0x060, 0x040}, {"1M PR01 D1", 1, 1, 1, 0, 0}, {"1M PR00 D1", 1, 0, 1, 0x0c0, 0},
    {"1M PR01 D0", 1, 1, 0, 0x080, 0}, {"1M PR10 D1", 1, 2, 1, 0x0c0, 0}};
  for (auto &s : S) {
    wreg(R_MMUCR, 0); hard_clear();
    if (s.map) utlb_set(32, 0xE0000000u, SQBUF & 0x1FF00000u, SZ1M, s.pr, 0, s.dd, 0, 0, 0, 0, 0);
    wreg(R_MMUCR, M_AT);
    uint32_t e1 = core_expevt(SQA + 4, DK_ST, 2, true, false);
    uint32_t e2 = core_expevt(SQA, DK_PREF, 2, true, false);
    CHECK(e1 == s.st && e2 == s.pf, "S %s: st %03x pref %03x, console %03x %03x", s.nm, e1, e2, s.st, s.pf);
    DRes x = dex_rtl(SQA, DK_PREF, true, false);
    CHECK(x.rg == RG_SQ, "S %s: SQ region %d", s.nm, x.rg);
    if (!e2) CHECK(x.pa == SQBUF, "S %s: PREF burst PA %08x, console %08x", s.nm, x.pa, SQBUF);
  }
  // U rows (user mode)
  wreg(R_MMUCR, 0); hard_clear();
  umap(16, DATA(0), 3, 1); umap(17, DATA(1), 0, 1); umap(18, DATA(2), 2, 1); umap(19, DATA(3), 1, 0);
  umap(21, DATA(5), 3, 0);
  utlb_set(32, 0xE0000000u, SQBUF & 0x1FF00000u, SZ1M, 3, 0, 1, 0, 0, 0, 0, 0);
  wreg(R_MMUCR, M_AT);
  struct { const char *nm; int kind; uint32_t va; uint32_t ev; } U[] = {
    {"ld dp1 PR00", DK_LD, d1, 0x0a0}, {"ld dp3 PR01", DK_LD, d3, 0x0a0}, {"ld dp2 PR10", DK_LD, d2, 0},
    {"st dp2 PR10", DK_ST, d2, 0x0c0}, {"st dp5 PR11 D0", DK_ST, d5, 0x080}, {"st dp0 PR11 D1", DK_ST, d0 + 0x300, 0},
    {"ld P1", DK_LD, DATA(0) | 0x80000000u, 0x0e0}, {"st SQ SQMD0", DK_ST, SQA, 0}};
  for (auto &u : U) {
    uint32_t e = core_expevt(u.va, u.kind, 2, false, false);
    CHECK(e == u.ev, "U %s: %03x console %03x", u.nm, e, u.ev);
  }
  wreg(R_MMUCR, M_AT | M_SQMD);
  CHECK(core_expevt(SQA, DK_ST, 2, false, false) == 0x100, "U st SQ SQMD1");
  // R5: MMUCR AT timing.  The MMUCR write lands at the end of its capture cycle: an access in EX in that cycle still
  // uses the old AT (0 -> 1: untranslated; the console's n = 0..2), one in EX later the new AT (n >= 3); after 1 -> 0
  // even the access in EX in the capture cycle raises no TLB exception.
  wreg(R_MMUCR, 0); hard_clear();
  umap(16, DATA(0), 3, 1);
  d->dex_v = 1; d->dex_va = un; d->dex_kind = DK_LD; d->dex_md = 1;
  d->reg_wr = 1; d->reg_off = R_MMUCR; d->reg_wdata = M_AT; d->reg_size = 2; ev();
  CHECK(d->dxl_fx == FX_NONE && d->dxl_pa == DATA(7), "R5 0->1: the load in EX in the capture cycle translated");
  tick(); d->reg_wr = 0; ev(); M.reg_wr(R_MMUCR, 2, M_AT); M0.reg_wr(R_MMUCR, 2, M_AT);
  CHECK(d->dxl_fx == FX_TLB_MISS, "R5 0->1: the next load not translated (fx %d)", d->dxl_fx);
  d->reg_wr = 1; d->reg_wdata = 0; ev();
  CHECK(d->dxl_fx == FX_NONE, "R5 1->0: a TLB exception in the capture cycle (fx %d)", d->dxl_fx);
  d->dex_va = d0; ev();
  CHECK(d->dxl_fx == FX_NONE && d->dxl_pa == DATA(0), "R5 1->0: a mapped load in the capture cycle");
  tick(); d->reg_wr = 0; d->dex_v = 0; ev(); M.reg_wr(R_MMUCR, 2, 0); M0.reg_wr(R_MMUCR, 2, 0);
  // R6: a UTLB address-array write (V = 1) is visible to the access in EX in its capture cycle
  wreg(R_MMUCR, M_AT);
  warr(UTLB_DA1(40), DATA(6) | 0x100 | 0x10 | 0x60 | 0x04);
  d->dex_v = 1; d->dex_va = VA(DATA(6)) + 8; d->dex_kind = DK_LD; d->dex_md = 1;
  d->arr_wr = 1; d->arr_blk = 8; d->arr_off = UTLB_AA(40) & 0x0FFFFFFF; d->arr_wdata = VA(DATA(6)) | 0x300;
  d->arr_size = 2; ev();
  CHECK(d->arr_stall && d->dxl_fx == FX_TLB_MISS, "R6: first strobe");
  tick(); ev();
  CHECK(!d->arr_stall && d->dxl_fx == FX_NONE && d->dxl_pa == DATA(6) + 8, "R6: the capture cycle's EX access does "
        "not see the new entry (fx %d)", d->dxl_fx);
  tick(); d->arr_wr = 0; d->dex_v = 0; ev();
  M.arr_wr(UTLB_AA(40) & 0x0FFFFFFF, 2, VA(DATA(6)) | 0x300);
  M0.arr_wr(UTLB_AA(40) & 0x0FFFFFFF, 2, VA(DATA(6)) | 0x300);
  // TI is a UTLB write too: the access in EX in its capture cycle misses
  d->dex_v = 1; d->dex_va = VA(DATA(6)) + 8; d->reg_wr = 1; d->reg_off = R_MMUCR; d->reg_wdata = M_AT | M_TI; ev();
  CHECK(d->dxl_fx == FX_TLB_MISS, "TI capture cycle: the EX access still hits");
  tick(); d->reg_wr = 0; d->dex_v = 0; ev();
  M.reg_wr(R_MMUCR, 2, M_AT | M_TI); M0.reg_wr(R_MMUCR, 2, M_AT | M_TI);
  // dex.v = 0 never reports a fault
  d->dex_va = un; d->dex_kind = DK_LD; ev();
  CHECK(d->dxl_fx == FX_NONE && !d->dxl_vec400, "dex.v = 0 with a fault");
  wreg(R_MMUCR, 0);
  hard_clear();
  section_end("data");
}

// random UTLB / ITLB contents x random lookups against the model
static uint32_t rnd_vpn(const std::vector<uint32_t> &bases) {
  uint32_t b = bases[rndn((uint32_t)bases.size())];
  return (b + (rndn(8) << 12) + (rndn(4) << 10)) & 0xFFFFFC00u;
}
static uint8_t g_cur_asid;                         // the batch's usual PTEH.ASID
static int g_mh;                                   // the batch allows overlapping UTLB entries
static void randomize(std::vector<uint32_t> &bases) {
  bases.clear();
  static const uint32_t hi[] = {0x00000000u, 0x0C000000u, 0x4C000000u, 0x7C000000u, 0xC0000000u, 0xDC000000u,
                                0xE0000000u, 0x3F000000u, 0x1C000000u};
  for (int i = 0; i < 6; i++) bases.push_back((hi[rndn(9)] | (rnd32() & 0x03F00000u)) & 0xFFF00000u);
  static const uint8_t asids[4] = {0x00, 0x12, 0x55, 0xFF};
  const uint8_t cur = asids[rndn(4)];              // this batch's PTEH.ASID: most entries carry it
  g_cur_asid = cur;
  auto easid = [&]() { return rndn(3) ? cur : asids[rndn(4)]; };
  int mh = rndn(4) == 0;                           // one batch in four allows overlapping entries
  g_mh = mh;
  for (int e = 0; e < 64; e++) {
    uint32_t vpn = rnd_vpn(bases);
    int sz = (int)rndn(4);
    if (!mh) vpn = (vpn & 0xFFF00000u) | ((uint32_t)e << 14) | (rnd32() & 0x3C00u);   // distinct 16K slots
    if (!mh && sz >= 2) sz = (int)rndn(2);
    uint32_t pa = rnd32() & 0x1FFFFC00u;
    if (rndn(8) == 0) pa = 0x1F000000u | (rnd32() & 0x00FFFC00u);
    uint32_t da1 = pa | ((rndn(10) != 0) << 8) | (((uint32_t)sz & 2) << 6) | (rndn(4) << 5) | (((uint32_t)sz & 1) << 4) |
                   (rndn(2) << 3) | (rndn(2) << 2) | (rndn(4) == 0 ? 2u : 0u) | rndn(2);
    warr(UTLB_DA2(e), rnd32());
    warr(UTLB_DA1(e), da1);
    warr(UTLB_AA(e), vpn | (da1 & 0x100) | (((da1 >> 2) & 1) << 9) | easid());
  }
  for (int e = 0; e < 4; e++) {
    uint32_t vpn = rnd_vpn(bases);
    if (!mh) vpn = (vpn & 0xFFF00000u) | ((uint32_t)(64 + e) << 14);
    int sz = (int)(mh ? rndn(4) : rndn(2));
    warr(ITLB_DA2(e), rnd32());
    warr(ITLB_DA1(e), (rnd32() & 0x1FFFFC00u) | ((rndn(4) != 0) << 8) | (((uint32_t)sz & 2) << 6) |
                          (((uint32_t)sz & 1) << 4) | (rndn(2) << 6) | (rndn(2) << 3) | (rndn(4) == 0 ? 2u : 0u));
    warr(ITLB_AA(e), vpn | ((rndn(4) != 0) << 8) | easid());
  }
  wreg(R_PTEH, (rnd32() & 0xFFFFFC00u) | cur);
  wreg(R_MMUCR, (rndn(8) ? M_AT : 0) | (rndn(3) == 0 ? M_SV : 0) | M_URB(rndn(64)) | M_URC(rndn(64)) |
                    M_LRUI(rndn(64)));
  set_ccr(rndn(2), rndn(2), rndn(2), rndn(4) != 0);
}
static uint32_t rnd_va(const std::vector<uint32_t> &bases) {
  switch (rndn(12)) {
  case 0: case 1: case 2: case 3: case 5: case 9: case 10: {   // a (preferably valid) entry's page, random offset
    const UEnt *e = &M.utlb[rndn(64)];
    for (int k = 0; k < 4 && !e->v; k++) e = &M.utlb[rndn(64)];
    return (e->vpn & szmask(e->sz)) | (rnd32() & ~szmask(e->sz));
  }
  case 4: { const IEnt &e = M.itlb[rndn(4)]; return (e.vpn & szmask(e.sz)) | (rnd32() & ~szmask(e.sz)); }
  case 6: return rnd_vpn(bases) | (rnd32() & 0x3FFu);
  case 7: return rnd32();
  case 8: return 0xE0000000u | (rnd32() & 0x03FFFFFFu);
  default: return (rnd32() & 0x9FFFFFFFu);
  }
}
static void compare_arrays(const char *what) {
  for (int e = 0; e < 64; e++) { rarr(UTLB_AA(e)); rarr(UTLB_DA1(e)); rarr(UTLB_DA2(e)); }
  for (int e = 0; e < 4; e++) { rarr(ITLB_AA(e)); rarr(ITLB_DA1(e)); rarr(ITLB_DA2(e)); }
  rreg(R_MMUCR); rreg(R_PTEH); rreg(R_TEA);
  (void)what;
}
static long g_n = 1000000;
static void t_random() {
  section("random");
  reset_por();
  std::vector<uint32_t> bases;
  long ndata = 0, nfetch = 0, nrefill = 0, nxl = 0, nmhit = 0, nfault = 0, nassoc = 0, nldtlb = 0, nxhit = 0, ntI = 0;
  long nfx[8] = {0}, nuntr = 0;
  while (ndata < g_n) {
    randomize(bases);
    for (int it = 0; it < 4000 && ndata < g_n; it++) {
      // data: a batch of combinational lookups (no clock)
      for (int k = 0; k < 8; k++) {
        uint32_t va = rnd_va(bases);
        int kind = rndn(3) ? (int)(1 + rndn(2)) : (int)rndn(13);   // loads / stores, then every kind
        bool md = rndn(4) != 0, rmw = rndn(3) == 0, v = rndn(16) != 0;
        DRes r = dex_rtl(va, kind, md, rmw, (int)rndn(4), v);
        DXlat m = M.data(va, kind, md, rmw);
        dex_cmp(r, m, v, "random data", va, kind);
        ndata++;
        if (m.fx == FX_MHIT) nmhit++;
        if (m.fx != FX_NONE) nfault++;
        nfx[m.fx & 7]++;
        if (!M.translated_data(va)) nuntr++;
        if (v && m.counted) nxhit++;
      }
      // xl (combinational)
      {
        uint32_t va = rnd_va(bases);
        bool it_ = rndn(2);
        d->xl_req = 1; d->xl_itlb = it_; d->xl_va = va; ev();
        XlRes x = M.xl(va, it_);
        CHECK(!d->xl_stall, "xl stalled with the refill idle");
        CHECK(d->xl_hit == x.hit && d->xl_mhit == x.mhit && (!x.hit || d->xl_pa == x.pa),
              "xl %08x itlb %d: hit %d mhit %d pa %08x model %d %d %08x", va, it_, d->xl_hit, d->xl_mhit, d->xl_pa,
              x.hit, x.mhit, x.pa);
        CHECK(d->arr_mhit == (x.mhit && d->xl_req), "xl multiple hit without arr_mhit");
        d->xl_req = 0; ev();
        nxl++;
      }
      // a completed counted access: the core's ma_commit
      if (rndn(4) == 0) {
        uint32_t va = rnd_va(bases);
        int kind = (int)rndn(13);
        DXlat m = M.data(va, kind, true, false);
        if (m.counted) { d->ma_commit = 1; ev(); tick(); d->ma_commit = 0; ev(); M.urc_step(); M0.urc_step(); }
      }
      // fetch (clocked: hits update LRUI, misses run the refill)
      if (rndn(2) == 0) {
        uint32_t va = rnd_va(bases) & ~3u;
        bool md = rndn(4) != 0;
        IXlat pre = M.lookup(va, md);
        fetch_chk(va, md, "random fetch");
        nfetch++;
        if (pre.kind == IXlat::MISS) nrefill++;
      }
      // occasionally: LDTLB, an associative write, TI, a new PTEH / MMUCR
      switch (rndn(64)) {
      case 0: {
        // outside the overlap batches LDTLB keeps the entry in its own 16 KB slot (URC's), 1 KB / 4 KB
        uint32_t vpn = rnd_vpn(bases), ptel = rnd32();
        if (!g_mh) { vpn = (vpn & 0xFFF00000u) | (M.urc << 14) | (rnd32() & 0x3C00u); ptel &= ~0x80u; }
        wreg(R_PTEH, vpn | (uint32_t)(rndn(4) ? g_cur_asid : rndn(256)));
        wreg(R_PTEL, ptel);
        wreg(R_PTEA, rnd32());
        d->ldtlb = 1; ev(); tick(); d->ldtlb = 0; ev(); M.ldtlb();
        nldtlb++;
        break;
      }
      case 1: {
        uint32_t va = rnd_va(bases);
        warr(UTLB_ASSOC | (rndn(64) << 8), (va & 0xFFFFFC00u) | (rndn(4) << 8));
        nassoc++;
        break;
      }
      case 2: wreg(R_PTEH, (rnd32() & 0xFFFFFC00u) | (rndn(4) ? g_cur_asid : rndn(256))); break;
      case 3: wreg(R_MMUCR, (M.mmucr() & ~(M_SV | M_AT)) | (rndn(2) ? M_SV : 0) | (rndn(8) ? M_AT : 0)); break;
      default: break;
      }
    }
    compare_arrays("random batch");
    if (rndn(2)) { wreg(R_MMUCR, M.mmucr() | M_TI); compare_arrays("random batch after TI"); ntI++; }
  }
  printf("  random: %ld data (%ld counted for URC, %ld faults, %ld multiple hits), %ld fetch (%ld refills), "
         "%ld xl, %ld LDTLB, %ld associative writes, %ld TI\n", ndata, nxhit, nfault, nmhit, nfetch, nrefill, nxl,
         nldtlb, nassoc, ntI);
  printf("  random: data by answer: %ld untranslated, %ld translated with no fault, TLB miss %ld, protection %ld, "
         "initial page write %ld, multiple hit %ld\n", nuntr, nfx[FX_NONE] - nuntr, nfx[FX_TLB_MISS], nfx[FX_TLB_PROT],
         nfx[FX_IPW], nfx[FX_MHIT]);
  wreg(R_MMUCR, 0);
  section_end("random");
}

// ---------------- the TLB multiple-hit reset through v1_rstc (verifier FIND-1)
static void rstc_wait(const char *what) {          // the manual reset v1_rstc raises the cycle after the request
  int n = 0;
  while (!d->rs_cpu && n < 4) { tick(); ev(); n++; }
  CHECK(d->rs_man && !d->rs_por && d->rs_code == 2, "%s: no manual reset with RC_140 (man %d por %d code %d)", what,
        d->rs_man, d->rs_por, d->rs_code);
  int len = 0;
  while (d->rs_cpu && len < 40) { tick(); ev(); len++; }
  CHECK(len >= 8 && !d->rs_cpu, "%s: the reset lasted %d cycles", what, len);
  M.reset_man(); M0.reset_man();
}
static void t_rstc() {
  section("rstc");
  reset_por();
  hard_clear();
  idle(); ev();
  CHECK(!d->rs_cpu, "v1_rstc in reset before the test");
  d->rst_sel = 1; ev();
  // 1. a data / fetch multiple hit: at the take point the core pulses excw {tea_v} and mhit_req (SOC.md 2.3 note);
  // hm 5.6.1 (4)/(5): TEA = VA, PTEH.VPN = VA[31:10], PTEH.ASID the ASID at the exception; then the manual reset
  wreg(R_PTEH, 0x55); wreg(R_PTEL, 0x1234u); wreg(R_PTEA, 5); wreg(R_TTB, 0x77u);
  wreg(R_MMUCR, M_AT | M_SV | M_URC(5) | M_LRUI(0x2A));
  d->excw_tea_v = 1; d->excw_tea = 0x4C0B3124u; d->mhit_req = 1; ev(); tick();
  d->excw_tea_v = 0; d->mhit_req = 0; ev();
  M.exc_capture(0x4C0B3124u); M0.exc_capture(0x4C0B3124u);
  rstc_wait("data multiple hit");
  CHECK(rreg(R_PTEH) == 0x4C0B3055u && rreg(R_TEA) == 0x4C0B3124u, "data multiple hit: after the reset PTEH %08x TEA "
        "%08x, want 4c0b3055 4c0b3124", rreg(R_PTEH), rreg(R_TEA));
  CHECK(rreg(R_MMUCR) == 0 && rreg(R_PTEL) == 0 && rreg(R_PTEA) == 0 && rreg(R_TTB) == 0,
        "data multiple hit: MMUCR / PTEL / PTEA / TTB not cleared by the manual reset");
  // 2. an associative address-array write hitting two UTLB entries: v1_mmu captures TEA / PTEH.VPN from the written
  // address and raises arr_mhit into v1_rstc
  const uint32_t X0 = 0x2A000000u;
  wreg(R_PTEH, 0x21);
  utlb_set(3, X0, 0x0C000000u, SZ4K, 3, 1, 1, 0, 0, 0x21, 0, 0);
  utlb_set(9, X0, 0x0C000000u, SZ1M, 3, 1, 1, 1, 0, 0x00, 0, 0);
  wreg(R_MMUCR, M_URC(9));
  bool mh = warr(UTLB_ASSOC | (7u << 8), X0 | 0x400);
  CHECK(mh, "associative double hit: no arr_mhit");
  rstc_wait("associative multiple hit");
  CHECK(rreg(R_PTEH) == (X0 | 0x400 | 0x21) && rreg(R_TEA) == (X0 | 0x400), "associative multiple hit: after the reset "
        "PTEH %08x TEA %08x", rreg(R_PTEH), rreg(R_TEA));
  CHECK(rreg(R_MMUCR) == 0, "associative multiple hit: MMUCR %08x", rreg(R_MMUCR));
  CHECK(rarr(UTLB_AA(3)) == (X0 | 0x300 | 0x21) && rarr(UTLB_AA(9)) == (X0 | 0x300),
        "the manual reset or the multiple hit changed a UTLB entry");
  d->rst_sel = 0; ev();
  hard_clear();
  section_end("rstc");
}

// ---------------- the refill against the other UTLB / ITLB writers (verifier FIND-2 / 3 / 4 / 6)
struct RaceRes {
  int ack = -1; bool hit = false; uint32_t pa = 0; int fx = 0; bool vec400 = false;
  int nrec = 0, rec_cyc = -1, rec_idx = -1;
  int stalls = 0, cap = -99;
};
enum { RW_TI, RW_LDTLB, RW_ARR };
// A privileged fetch of va requested from cycle 0 (cycle m of the refill) and held until the MMU acks it, and one
// other writer starting in cycle `at` (may be before m): RW_TI a MMUCR write of wv (captured in its strobe cycle),
// RW_LDTLB one ldtlb cycle, RW_ARR an array write of wv to addr held until it captures.  The model is not updated:
// the caller applies the serial order the RTL must equal.
static RaceRes race_rtl(uint32_t va, int kind, int at, uint32_t addr, uint32_t wv) {
  RaceRes r;
  bool wr_on = false, wr_done = false;
  for (int k = std::min(at, 0); k < 24 && (r.ack < 0 || !wr_done); k++) {
    d->if_v = k >= 0 && r.ack < 0; d->if_va = va; d->if_md = 1;
    if (k == at) {
      wr_on = true;
      if (kind == RW_TI) { d->reg_wr = 1; d->reg_off = R_MMUCR; d->reg_wdata = wv; d->reg_size = 2; }
      else if (kind == RW_LDTLB) d->ldtlb = 1;
      else { d->arr_wr = 1; d->arr_size = 2; d->arr_blk = blk_of(addr); d->arr_off = addr & 0x0FFFFFFF; d->arr_wdata = wv; }
    }
    ev();
    if (d->tap1_v) { r.nrec++; r.rec_cyc = k; r.rec_idx = d->tap1_idx; }
    if (d->if_v) {
      if (d->if_hit || d->if_fx) { r.ack = k; r.hit = d->if_hit; r.pa = d->if_pa; r.fx = d->if_fx; r.vec400 = d->if_vec400; }
      else CHECK(d->if_stall, "race fetch %08x: neither ack nor if_stall at +%d", va, k);
    }
    bool end_wr = false;
    if (wr_on) {
      if (kind == RW_ARR) { if (d->arr_stall) r.stalls++; else { r.cap = k; end_wr = true; } }
      else { CHECK(kind != RW_TI || !d->reg_stall, "race: a MMUCR write stalled"); r.cap = k; end_wr = true; }
    }
    tick();
    if (end_wr) { wr_on = false; wr_done = true; d->reg_wr = 0; d->ldtlb = 0; d->arr_wr = 0; }
  }
  d->if_v = 0; ev();
  CHECK(r.ack >= 0 && wr_done, "race fetch %08x: ack %d writer done %d", va, r.ack, wr_done);
  return r;
}
static int itlb_vmask() {                          // the ITLB V bits (read through the arrays: compared with the model)
  int m = 0;
  for (int e = 0; e < 4; e++) if (rarr(ITLB_AA(e)) & 0x100) m |= 1 << e;
  return m;
}
static void model_mmucr(uint32_t v) { M.reg_wr(R_MMUCR, 2, v); M0.reg_wr(R_MMUCR, 2, v); }
static void t_races() {
  section("races");
  reset_por();
  const int UE = 7;
  const uint32_t va = VA(CODE(3)) + 0x40, pa = CODE(3) + 0x40;
  // every scenario: UTLB 7 maps va's page, the ITLB is empty, AT = 1, LRUI 0 (the victim is ITLB entry 3)
  auto setup = [&](uint32_t mmucr) { wreg(R_MMUCR, 0); hard_clear(); umap(UE, CODE(3), 3, 1); wreg(R_MMUCR, mmucr); };
  auto check_fetch = [&](const RaceRes &r, const IXlat &m, int ack, int nrec, const char *what, int at) {
    CHECK(r.ack == ack, "%s at +%d: fetch acked at +%d, want +%d", what, at, r.ack, ack);
    CHECK(r.hit == (m.kind == IXlat::HIT) && r.fx == m.fx && r.vec400 == m.vec400 && (!r.hit || r.pa == m.pa),
          "%s at +%d: fetch hit %d fx %d pa %08x, the serial order gives hit %d fx %d pa %08x", what, at, r.hit, r.fx,
          r.pa, m.kind == IXlat::HIT, m.fx, m.pa);
    CHECK(r.nrec == nrec && (nrec == 0 || (r.rec_cyc == 3 && r.rec_idx == 3)), "%s at +%d: %d ITLBFILL records (last "
          "at +%d idx %d), want %d at +3 idx 3", what, at, r.nrec, r.rec_cyc, r.rec_idx, nrec);
    compare_arrays(what);                          // every array word, MMUCR (LRUI, URC), PTEH, TEA vs the model
  };
  // FIND-2 / FIND-6: TI (MMUCR <- AT | TI) landing in the refill's cycle m .. m + 4.  m: before the search (a UTLB miss
  // at +3); m + 1 .. m + 3: RF_WR writes nothing and the retry at m + 7 refills from the invalidated UTLB (a UTLB miss
  // at +10), both "TI, then the fetch"; m + 4 (RS1): the fill happened, TI clears it, the retry misses likewise.
  for (int at = 0; at <= 4; at++) {
    setup(M_AT);
    RaceRes r = race_rtl(va, RW_TI, at, 0, M_AT | M_TI);
    IXlat m;
    if (at <= 3) { model_mmucr(M_AT | M_TI); m = M.fetch(va, true); }
    else {
      int ie; Fx fx;
      M.lookup(va, true); M.refill(va, true, &ie, &fx);
      model_mmucr(M_AT | M_TI); m = M.fetch(va, true);
    }
    check_fetch(r, m, at == 0 ? 3 : 10, at == 4 ? 1 : 0, "TI", at);
    CHECK(itlb_vmask() == 0 && !(rarr(UTLB_AA(UE)) & 0x100), "TI at +%d: a valid TLB entry survived TI", at);
  }
  // FIND-3: LDTLB into the matched entry (URC = 7) landing in m .. m + 3: at m the search sees the new entry (VPN
  // 50000000: a UTLB miss for va); from m + 1 on the ITLB gets the old entry (refill, then LDTLB) -- never va's VPN
  // with the new entry's PPN
  for (int at = 0; at <= 3; at++) {
    setup(M_AT | M_URC(UE));
    wreg(R_PTEH, 0x50000000u);
    wreg(R_PTEL, 0x0D000000u | 0x100 | 0x10 | 0x60 | 0x08 | 0x04);
    wreg(R_PTEA, 0);
    RaceRes r = race_rtl(va, RW_LDTLB, at, 0, 0);
    IXlat m;
    if (at == 0) { M.ldtlb(); M0.ldtlb(); m = M.fetch(va, true); }
    else { m = M.fetch(va, true); M.ldtlb(); M0.ldtlb(); }
    check_fetch(r, m, at == 0 ? 3 : 7, at == 0 ? 0 : 1, "LDTLB", at);
    CHECK(at == 0 || (r.hit && r.pa == pa), "LDTLB at +%d: fetch %08x translated to %08x, want %08x (the old entry)",
          at, va, r.pa, pa);
  }
  // FIND-4: an associative UTLB write invalidating va's page (V 0, AT = 1: the ITLB side too) whose first strobe is
  // at m - 1 .. m + 3.  From m - 1 it captures in m (before the search: a UTLB miss at +3); every later one waits for
  // the refill's ITLB write (RF_SRCH / RF_RD own the UTLB, RF_WR the ITLB) and captures at m + 4, so it invalidates the
  // refilled entry too; the retry misses and refills from the invalidated UTLB (+10).
  for (int at = -1; at <= 3; at++) {
    setup(M_AT);
    RaceRes r = race_rtl(va, RW_ARR, at, UTLB_ASSOC, VA(CODE(3)));
    IXlat m;
    if (at < 0) { M.arr_wr(UTLB_ASSOC & 0x0FFFFFFF, 2, VA(CODE(3))); m = M.fetch(va, true); }
    else {
      int ie; Fx fx;
      M.lookup(va, true); M.refill(va, true, &ie, &fx);
      M.arr_wr(UTLB_ASSOC & 0x0FFFFFFF, 2, VA(CODE(3))); m = M.fetch(va, true);
    }
    check_fetch(r, m, at < 0 ? 3 : 10, at < 0 ? 0 : 1, "associative write", at);
    int want_cap = at < 0 ? 0 : 4;
    CHECK(r.cap == want_cap && r.stalls == want_cap - at, "associative write from +%d: captured at +%d after %d stalls, "
          "want +%d after %d", at, r.cap, r.stalls, want_cap, want_cap - at);
    CHECK(itlb_vmask() == 0 && !(rarr(UTLB_AA(UE)) & 0x100), "associative write from +%d: a valid entry survived", at);
  }
  // FIND-6: an ITLB address-array write to the victim (entry 3 <- 12345000, V 0) whose first strobe is at m + 2 or
  // m + 3 waits for the refill's ITLB write (RF_WR) and lands at m + 4: refill (ITLBFILL at +3), then the array write;
  // the retry misses entry 3 and refills entry 2 (LRUI 0B after the first refill): a second record, a hit at +14
  for (int at = 2; at <= 3; at++) {
    setup(M_AT);
    RaceRes r = race_rtl(va, RW_ARR, at, ITLB_AA(3), 0x12345000u);
    int ie; Fx fx;
    M.lookup(va, true); M.refill(va, true, &ie, &fx);
    M.arr_wr(ITLB_AA(3) & 0x0FFFFFFF, 2, 0x12345000u);
    IXlat m = M.fetch(va, true);
    CHECK(r.ack == 14 && r.hit && r.pa == pa && r.nrec == 2 && r.rec_idx == 2, "ITLB array write from +%d: ack +%d hit "
          "%d pa %08x, %d records (last idx %d)", at, r.ack, r.hit, r.pa, r.nrec, r.rec_idx);
    CHECK(r.hit == (m.kind == IXlat::HIT) && m.ient == 2, "ITLB array write from +%d: the model's retry entry %d", at,
          m.ient);
    CHECK(r.cap == 4 && r.stalls == 4 - at, "ITLB array write from +%d: captured at +%d after %d stalls, want +4",
          at, r.cap, r.stalls);
    compare_arrays("ITLB array write");
    CHECK(rarr(ITLB_AA(3)) == 0x12345000u, "ITLB array write from +%d: entry 3 AA %08x", at, rarr(ITLB_AA(3)));
  }
  wreg(R_MMUCR, 0);
  hard_clear();
  section_end("races");
}

// TRANSLATE = 0
static void t_mmu0() {
  section("mmu0");
  reset_por();
  // registers work (the reads of rreg() compare u0 with M0 too); AT = 1 does not translate
  wreg(R_MMUCR, M_AT | M_SQMD);
  // FIND-5: MMU-0 exports at = 0 whatever MMUCR.AT holds (v1_sq would take an SQ burst's address from the identity
  // x.pa); MMUCR reads AT back (rreg compares u0 with M0)
  CHECK(!d->at0 && d->sqmd0 && d->at, "MMU-0 at / sqmd outputs: at0 %d sqmd0 %d (u1 at %d)", d->at0, d->sqmd0, d->at);
  CHECK((rreg(R_MMUCR) & 1) == 1, "MMU-0 MMUCR.AT read-back");
  d->dex_v = 1; d->dex_va = 0xE0000040u; d->dex_kind = DK_ST; d->dex_md = 1; ev();
  CHECK(!d->at0 && d->dxl_rg0 == RG_SQ, "MMU-0 SQ store with AT = 1: at0 %d rg %d", d->at0, d->dxl_rg0);
  d->dex_v = 0; ev();
  for (int i = 0; i < 200000; i++) {
    uint32_t va = rnd32();
    int kind = (int)rndn(13);
    bool md = rndn(2), rmw = rndn(2);
    set_ccr(rndn(2), rndn(2), rndn(2), rndn(2));
    d->dex_v = 1; d->dex_va = va; d->dex_kind = kind; d->dex_md = md; d->dex_rmw = rmw;
    d->if_v = 1; d->if_va = va & ~3u; d->if_md = md;
    d->xl_req = 1; d->xl_va = va; d->xl_itlb = rndn(2);
    ev();
    DXlat m = M0.data(va, kind, md, rmw);
    CHECK(d->dxl_rg0 == m.rg && d->dxl_pa0 == m.pa && d->dxl_c0 == m.c && d->dxl_wt0 == m.wt && d->dxl_fx0 == 0 &&
              !d->dxl_vec4000 && d->dxl_sa0 == 0 && !d->dxl_tc0,
          "MMU-0 dxl %08x: rg %d pa %08x c %d wt %d fx %d", va, d->dxl_rg0, d->dxl_pa0, d->dxl_c0, d->dxl_wt0,
          d->dxl_fx0);
    IXlat f = M0.lookup(va & ~3u, md);
    CHECK(!d->if_stall0 && d->if_fx0 == f.fx && d->if_hit0 == (f.kind == IXlat::HIT) &&
              (!d->if_hit0 || (d->if_pa0 == f.pa && d->if_c0 == f.c)),
          "MMU-0 fetch %08x md %d: hit %d fx %d stall %d pa %08x", va, md, d->if_hit0, d->if_fx0, d->if_stall0,
          d->if_pa0);
    CHECK(d->xl_hit0 && !d->xl_mhit0 && !d->xl_stall0 && d->xl_pa0 == (va & 0x1FFFFFFFu) && !d->arr_mhit0,
          "MMU-0 xl %08x", va);
  }
  idle(); ev();
  // arrays read 0 with one stall (checked by rarr), writes ignored; no LDTLB record; no PMC events
  warr(UTLB_AA(3), 0xFFFFFFFFu);
  rarr(UTLB_AA(3));
  CHECK(!d->pmc_any, "a PMC event bit");
  // excw capture works in MMU-0 (M1a)
  d->excw_tea_v = 1; d->excw_tea = 0x12345678u; ev(); tick(); d->excw_tea_v = 0; ev();
  M.exc_capture(0x12345678u); M0.exc_capture(0x12345678u);
  rreg(R_TEA); rreg(R_PTEH);
  wreg(R_MMUCR, 0);
  section_end("mmu0");
}

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  const char *only = nullptr;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--n") && i + 1 < argc) g_n = atol(argv[++i]);
    else if (!strcmp(argv[i], "--seed") && i + 1 < argc) g_rs ^= strtoull(argv[++i], nullptr, 0) * 0x9E3779B97F4A7C15ull;
    else if (!strcmp(argv[i], "--mutate") && i + 1 < argc) g_mutate = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i];
    else if (!strcmp(argv[i], "-v")) g_verbose = 1;
  }
  if (g_mutate == 7) g_refill_cost = 6;            // the timing expectation itself broken
  M0.translate = false;
  d = new Vv1_mmu_tb;
  idle(); set_ccr(false, false, false);
  d->clk = 0; d->eval();
  struct { const char *nm; void (*f)(); } T[] = {{"regs", t_regs}, {"arrays", t_arrays}, {"assoc", t_assoc},
                                                  {"ldtlb", t_ldtlb}, {"urc", t_urc}, {"lrui", t_lrui},
                                                  {"refill", t_refill}, {"data", t_data}, {"random", t_random},
                                                  {"rstc", t_rstc}, {"races", t_races}, {"mmu0", t_mmu0}};
  printf("v1-mmu%s:\n", g_mutate ? (" (mutation " + std::to_string(g_mutate) + ")").c_str() : "");
  for (auto &t : T) if (!only || !strcmp(only, t.nm)) t.f();
  printf("v1-mmu: %ld checks, %ld failed, %llu cycles -> %s\n", g_checks, g_fail, (unsigned long long)g_cyc,
         g_fail ? "FAIL" : "PASS");
  delete d;
  return g_fail ? 1 : 0;
}
