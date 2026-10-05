// bm_board.h -- the flat board and the ISS environment for running the bare-metal MMU probe port functionally
// (run_iss.cpp: MMU = tb/mmu_ref.h; run_rtl.cpp: MMU = the Verilated rtl/v1/v1_mmu.sv).  Owner: WI-MMU.
//
// The board: 16 MB of SDRAM at physical 0x0C000000 (area 3), the sim-control exit word at 0x18000000 (board.md 3.10),
// everything else reads 0.  P4: EXPEVT / INTEVT / TRA, CCR / QACR (stored), PVR, PMCR (stored), PMCTR (the instruction
// count: no timing), IC / OC arrays read 0; the store queues with their QACR / UTLB burst address; the OC RAM window.
// The MMU registers and the TLB arrays are the MMU's (MmuPort).  URC steps when an access completes, after its own
// read: a load of MMUCR through area 7 reads the URC before its own increment [HW G: "[3f000010] 00000401 ... (MMUCR
// 00000801)"].
#pragma once
#include "../../sh4_iss.h"
#include "../../mmu_ref.h"
#include <cstdio>
#include <string>
#include <vector>

// what the board needs from an MMU (the model or the RTL)
struct MmuPort {
  virtual ~MmuPort() {}
  virtual mmuref::DXlat data(uint32_t va, int kind, int size, bool md, bool rmw) = 0;   // the EX answer (dxlat)
  virtual int fetch(uint32_t va, bool md, uint32_t &pa, bool &vec400) = 0;               // fx; pa on success
  virtual bool reg_rd(uint32_t off, int szc, uint32_t &v) = 0;                           // CCN_MMU (false: not ours)
  virtual bool reg_wr(uint32_t off, int szc, uint32_t v) = 0;
  virtual uint32_t arr_rd(uint32_t off, int szc) = 0;                                    // off = a[27:0]
  virtual bool arr_wr(uint32_t off, int szc, uint32_t v) = 0;                            // returns arr_mhit
  virtual void commit() = 0;                                                             // ma_commit: URC step
  virtual void ldtlb() = 0;
  virtual void exc_capture(uint32_t tea) = 0;                                            // excw.tea_v
  virtual bool sqmd() = 0;
  virtual bool at() = 0;
  virtual bool counted(uint32_t va, int kind, const mmuref::DXlat &x) = 0;              // URC counts it
  const uint32_t *ccr = nullptr;                   // the program's CCR (the board's copy): ORA, CB, WT, OCE
  bool ccr_ora() const { return ccr && ((*ccr >> 5) & 1); }
  bool ccr_cb() const { return ccr && ((*ccr >> 2) & 1); }
  bool ccr_wt() const { return ccr && ((*ccr >> 1) & 1); }
  bool ccr_oce() const { return ccr && (*ccr & 1); }
};

struct BmBoard : Sh4Env {
  std::vector<uint8_t> ram = std::vector<uint8_t>(16u << 20);
  MmuPort *mmu = nullptr;
  Sh4Iss *cpu = nullptr;
  uint32_t expevt = 0, intevt = 0, tra = 0, ccr = 0, qacr[2] = {0, 0}, exitcode = 0xFFFFFFFFu;
  uint16_t pmcr[2] = {0, 0};
  uint32_t sq[2][8] = {};
  uint8_t ocram[8192] = {};
  long resets = 0, bad = 0;

  static int szc(int size) { return size == 1 ? 0 : size == 2 ? 1 : 2; }
  uint64_t prd(uint32_t pa, int size) {
    pa &= 0x1FFFFFFFu;
    if (pa >= 0x0C000000u && pa + size <= 0x0D000000u) {
      uint64_t v = 0;
      for (int i = 0; i < size; i++) v |= (uint64_t)ram[pa - 0x0C000000u + i] << (8 * i);
      return v;
    }
    return 0;
  }
  void pwr(uint32_t pa, int size, uint64_t v) {
    pa &= 0x1FFFFFFFu;
    if (pa >= 0x0C000000u && pa + size <= 0x0D000000u) {
      for (int i = 0; i < size; i++) ram[pa - 0x0C000000u + i] = (uint8_t)(v >> (8 * i));
    } else if (pa == 0x18000000u && size == 4) exitcode = (uint32_t)v;
  }
  uint32_t p4rd(uint32_t a, int size) {
    uint32_t v = 0;
    if (a >= 0xF0000000u && a < 0xF8000000u) {
      int blk = (a >> 24) & 7;
      if (blk == 2 || blk == 3 || blk == 6 || blk == 7) return mmu->arr_rd(a & 0x0FFFFFFFu, szc(size));
      return 0;                                    // IC / OC arrays: nothing here
    }
    if (a < 0xFF000000u) return 0;
    uint32_t w = (a >> 18) & 0x3F;
    if (w == 0) {                                  // CCN, a[7:0]
      uint32_t o = a & 0xFF;
      if (mmu->reg_rd(o, szc(size), v)) return v;
      if (size == 4) switch (o) {
        case 0x1C: return ccr;
        case 0x20: return tra;
        case 0x24: return expevt;
        case 0x28: return intevt;
        case 0x30: return 0x040205C1u;
        case 0x38: return qacr[0];
        case 0x3C: return qacr[1];
        default: return 0;
      }
      if (size == 2 && (o == 0x84 || o == 0x88)) return pmcr[(o - 0x84) >> 2];
      return 0;
    }
    if (w == 4) {                                  // PMCTR: the instruction count stands in for cycles
      uint32_t o = a & 0x1C;
      if (o == 0x08 || o == 0x10) return (uint32_t)cpu->icount;
      return 0;
    }
    return 0;
  }
  void p4wr(uint32_t a, int size, uint32_t v) {
    if (a >= 0xF0000000u && a < 0xF8000000u) {
      int blk = (a >> 24) & 7;
      if ((blk == 2 || blk == 3 || blk == 6 || blk == 7) && mmu->arr_wr(a & 0x0FFFFFFFu, szc(size), v)) {
        fprintf(stderr, "bm: associative-write multiple hit (a reset)\n");
        bad++;
      }
      return;
    }
    if (a < 0xFF000000u) return;
    uint32_t w = (a >> 18) & 0x3F;
    if (w == 0) {
      uint32_t o = a & 0xFF;
      if (mmu->reg_wr(o, szc(size), v)) return;
      if (size == 4) switch (o) {
        case 0x1C: ccr = v & 0x89AFu; break;
        case 0x20: tra = v & 0x3FCu; break;
        case 0x24: expevt = v & 0xFFFu; break;
        case 0x28: intevt = v & 0xFFFu; break;
        case 0x38: qacr[0] = v & 0x1Cu; break;
        case 0x3C: qacr[1] = v & 0x1Cu; break;
        default: break;
      }
      if (size == 2 && (o == 0x84 || o == 0x88)) pmcr[(o - 0x84) >> 2] = (uint16_t)v;
    }
  }
  uint64_t access(uint32_t va, int size, const mmuref::DXlat &x, bool wr, uint64_t v) {
    using namespace mmuref;
    switch (x.rg) {
    case RG_MEM:
      if (wr) pwr(x.pa, size, v);
      return wr ? 0 : prd(x.pa, size);
    case RG_A7: case RG_P4RSV: return 0;
    case RG_OCRAM: {
      uint32_t o = ((ccr >> 7) & 1) ? (((va >> 25) & 1) << 12) | (va & 0xFFF) : (((va >> 13) & 1) << 12) | (va & 0xFFF);
      if (wr) { for (int i = 0; i < size; i++) ocram[(o + i) & 8191] = (uint8_t)(v >> (8 * i)); return 0; }
      uint64_t r = 0;
      for (int i = 0; i < size; i++) r |= (uint64_t)ocram[(o + i) & 8191] << (8 * i);
      return r;
    }
    case RG_SQ: {
      if (!wr) return 0;                           // an SQ read is junk / 0 (SOC.md 4.2)
      int q = (va >> 5) & 1, i = (va >> 2) & 7;
      sq[q][i] = (uint32_t)v;
      if (size == 8) sq[q][(i + 1) & 7] = (uint32_t)(v >> 32);
      return 0;
    }
    case RG_ARR: case RG_P4REG: {
      uint32_t a = 0xFF000000u | (x.pa & 0x00FFFFFFu);
      if ((va >> 29) == 7) a = va;                 // untranslated P4 address
      if (size == 8) {                             // FMOV quad to P4: two longwords (SOC.md 3.1.4)
        if (wr) { p4wr(a, 4, (uint32_t)v); p4wr(a + 4, 4, (uint32_t)(v >> 32)); return 0; }
        return p4rd(a, 4) | ((uint64_t)p4rd(a + 4, 4) << 32);
      }
      if (wr) { p4wr(a, size, (uint32_t)v); return 0; }
      return p4rd(a, size);
    }
    }
    return 0;
  }
  uint64_t route(uint32_t va, int size, int kind, bool md, bool wr, uint64_t v) {
    mmuref::DXlat x = mmu->data(va, kind, size, md, false);
    uint64_t r = access(va, size, x, wr, v);
    if (mmu->counted(va, kind, x)) mmu->commit();
    return r;
  }
  // ---- Sh4Env
  int ifetch(uint32_t va, bool md, uint16_t &op, bool &vec400) override {
    uint32_t pa = 0;
    int fx = mmu->fetch(va, md, pa, vec400);
    if (fx) return fx;
    op = (uint16_t)prd(pa, 2);
    return 0;
  }
  int dcheck(uint32_t va, int size, int kind, bool md, bool rmw, bool &vec400) override {
    mmuref::DXlat x = mmu->data(va, kind, size, md, rmw);
    vec400 = x.vec400;
    return x.fx;
  }
  uint64_t dread(uint32_t va, int size, int kind, bool md) override { return route(va, size, kind, md, false, 0); }
  void dwrite(uint32_t va, int size, uint64_t v, int kind, bool md) override { route(va, size, kind, md, true, v); }
  void dctl(uint32_t va, int kind, bool md) override {
    using namespace mmuref;
    DXlat x = mmu->data(va, kind, 4, md, false);
    if (kind == DK_PREF && x.rg == RG_SQ && x.fx == FX_NONE) {       // the SQ burst
      int q = (va >> 5) & 1;
      uint32_t pa = mmu->at() ? (x.pa & ~31u) : ((((qacr[q] >> 2) & 7) << 26) | (va & 0x03FFFFE0u));
      for (int i = 0; i < 8; i++) pwr(pa + 4 * i, 4, sq[q][i]);
    }
    if (mmu->counted(va, kind, x)) mmu->commit();
  }
  void ldtlb() override { mmu->ldtlb(); }
  bool sqmd() override { return mmu->sqmd(); }
  void entry(const Sh4Entry &e) override {
    if (e.kind == Sh4Entry::IRQ) intevt = e.code & 0xFFF; else expevt = e.code & 0xFFF;
    if (e.tra_v) tra = (e.tra & 0xFF) << 2;
    if (e.tea_v) mmu->exc_capture(e.tea);
    if (e.kind == Sh4Entry::RESET) { resets++; fprintf(stderr, "bm: reset entry code %03x pc %08x\n", e.code, e.pc); }
  }

  // load the image at 0x8C010000, run it to its SLEEP, dump the result block; 0 = clean halt
  int run(const char *bin, const char *dump, const char *who) {
    FILE *f = fopen(bin, "rb");
    if (!f) { perror(bin); return 2; }
    size_t n = fread(&ram[0x00010000], 1, ram.size() - 0x10000, f);
    fclose(f);
    Sh4Iss c(nullptr);
    cpu = &c;
    c.soc = true;
    c.env = this;
    c.reset(true);
    c.s.pc = 0x8C010000u;
    mmu->ccr = &ccr;
    uint64_t steps = 0;
    while (steps < 200000000ull && c.step() && !c.error) steps++;
    printf("%s: %zu bytes loaded, %llu steps, %llu instructions, %s%s, exit word %08x, resets %ld\n", who, n,
           (unsigned long long)steps, (unsigned long long)c.icount, c.asleep ? "asleep" : "not asleep",
           c.error ? (std::string(", ISS error: ") + c.errmsg).c_str() : "", exitcode, resets);
    const uint32_t base = 0x0C0F0000u;
    uint32_t cnt = (uint32_t)prd(base + 4, 4);
    size_t len = 16 + 8 * (size_t)(cnt < 100000 ? cnt : 0);
    FILE *o = fopen(dump, "wb");
    if (!o) { perror(dump); return 2; }
    fwrite(&ram[base - 0x0C000000u], 1, len, o);
    fclose(o);
    cpu = nullptr;
    return (c.asleep && !c.error && !resets && !bad) ? 0 : 1;
  }
};
