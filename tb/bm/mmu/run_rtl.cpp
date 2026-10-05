// run_rtl.cpp -- the functional run of the bare-metal MMU probe port with the RTL MMU: the reference ISS (tb/sh4_iss.h,
// SoC mode) and the flat board of bm_board.h, with every MMU question answered by the Verilated rtl/v1/v1_mmu.sv
// (through tb/v1_mmu_tb.sv, instance u1, TRANSLATE = 1) at its ports: dex -> dxlat (combinational), the fetch request
// held until the MMU acks it (ITLB refills run their cycles), the P4 register and array slaves strobed until they stop
// stalling, ldtlb / ma_commit / excw pulses.  Owner: WI-MMU.  Each ISS step is a few RTL cycles: this is a functional
// check of the RTL against the console logs, not a timing run.
//   ma_commit: pulsed when the RTL's own qualification says so -- v1_mmu_pkg::mmu_urc_counts on the lreq of the access
//   (tb/v1_mmu_tb.sv urc_cnt, sampled with the access's dxlat): the qualification the SoC must apply to the core's
//   ma_commit (WI-MMU's CONTRACT note), so this run checks it against the console's URC records (D1, R4, S, G).
//   usage: run_rtl <mmubm.bin> <dump.bin>
#include "Vv1_mmu_tb.h"
#include "verilated.h"
#include "bm_board.h"

using namespace mmuref;

struct RtlPort : MmuPort {
  Vv1_mmu_tb *d = nullptr;
  uint64_t cyc = 0;
  long stuck = 0;
  bool last_cnt = false;                           // urc_cnt of the last data() lookup
  void ev() {
    d->ccr_ora = ccr_ora(); d->ccr_cb = ccr_cb(); d->ccr_wt = ccr_wt(); d->ccr_oce = ccr_oce();
    d->clk = 0; d->eval();
  }
  void tick() { d->clk = 1; d->eval(); d->clk = 0; d->eval(); cyc++; }
  void idle() {
    d->rst_por = d->rst_man = d->rst_cpu = 0;
    d->reg_rd = d->reg_wr = 0; d->reg_size = 2; d->arr_rd = d->arr_wr = 0; d->arr_size = 2;
    d->if_v = 0; d->dex_v = 0; d->ma_commit = 0; d->ldtlb = 0; d->excw_tea_v = 0; d->xl_req = 0;
  }
  void reset() { idle(); d->rst_por = 1; ev(); tick(); tick(); d->rst_por = 0; ev(); }
  DXlat data(uint32_t va, int kind, int size, bool md, bool rmw) override {
    d->dex_v = 1; d->dex_va = va; d->dex_kind = kind; d->dex_md = md; d->dex_rmw = rmw;
    d->dex_size = size == 1 ? 0 : size == 2 ? 1 : size == 8 ? 3 : 2;
    ev();
    DXlat x;
    x.rg = (Region)d->dxl_rg; x.pa = d->dxl_pa; x.c = d->dxl_c; x.wt = d->dxl_wt; x.sa = d->dxl_sa; x.tc = d->dxl_tc;
    x.fx = (Fx)d->dxl_fx; x.vec400 = d->dxl_vec400;
    last_cnt = d->urc_cnt;
    d->dex_v = 0; ev();
    return x;
  }
  int fetch(uint32_t va, bool md, uint32_t &pa, bool &vec400) override {
    d->if_v = 1; d->if_va = va & ~3u; d->if_md = md;
    int fx = 0;
    for (int k = 0; ; k++) {
      ev();
      if (d->if_hit) { pa = d->if_pa | (va & 2); break; }
      if (d->if_fx) { fx = d->if_fx; vec400 = d->if_vec400; break; }
      if (!d->if_stall || k > 32) { stuck++; fx = FX_ADDR; break; }
      tick();
    }
    tick();                                        // the ack cycle (the LRUI use of a hit lands at its end)
    d->if_v = 0; ev();
    return fx;
  }
  static bool ours(uint32_t o) { return o == 0x00 || o == 0x04 || o == 0x08 || o == 0x0C || o == 0x10 || o == 0x34; }
  uint32_t reg_acc(bool wr, uint32_t off, int szc, uint32_t v) {
    d->reg_rd = !wr; d->reg_wr = wr; d->reg_size = szc; d->reg_off = off; d->reg_wdata = v;
    uint32_t r = 0;
    for (int k = 0; ; k++) {
      ev();
      if (!d->reg_stall) { r = d->reg_rdata; tick(); break; }
      if (k > 8) { stuck++; break; }
      tick();
    }
    d->reg_rd = d->reg_wr = 0; ev();
    return r;
  }
  bool reg_rd(uint32_t off, int szc, uint32_t &v) override {
    if (!ours(off & 0xFF)) return false;
    v = reg_acc(false, off, szc, 0);
    return true;
  }
  bool reg_wr(uint32_t off, int szc, uint32_t v) override {
    if (!ours(off & 0xFF)) return false;
    reg_acc(true, off, szc, v);
    return true;
  }
  bool arr_acc(bool wr, uint32_t off, int szc, uint32_t v, uint32_t &r) {
    static const int blk[8] = {0, 0, 4, 5, 0, 0, 8, 9};
    d->arr_rd = !wr; d->arr_wr = wr; d->arr_size = szc; d->arr_blk = blk[(off >> 24) & 7]; d->arr_off = off;
    d->arr_wdata = v;
    bool mh = false;
    for (int k = 0; ; k++) {
      ev();
      if (!d->arr_stall) { r = d->arr_rdata; mh = d->arr_mhit; tick(); break; }
      if (k > 8) { stuck++; break; }
      tick();
    }
    d->arr_rd = d->arr_wr = 0; ev();
    return mh;
  }
  uint32_t arr_rd(uint32_t off, int szc) override { uint32_t r = 0; arr_acc(false, off, szc, 0, r); return r; }
  bool arr_wr(uint32_t off, int szc, uint32_t v) override { uint32_t r; return arr_acc(true, off, szc, v, r); }
  void pulse(unsigned char &s) { s = 1; ev(); tick(); s = 0; ev(); }
  void commit() override { pulse(d->ma_commit); }
  void ldtlb() override { pulse(d->ldtlb); }
  void exc_capture(uint32_t tea) override { d->excw_tea = tea; pulse(d->excw_tea_v); }
  bool sqmd() override { ev(); return d->sqmd; }
  bool at() override { ev(); return d->at; }
  // the access just looked up by data() (route / dctl call data(), then counted() for the same access)
  bool counted(uint32_t, int, const DXlat &) override { return last_cnt; }
};

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  if (argc < 3) { fprintf(stderr, "usage: run_rtl <mmubm.bin> <dump.bin>\n"); return 2; }
  static RtlPort rp;
  static BmBoard b;
  rp.d = new Vv1_mmu_tb;
  rp.reset();
  b.mmu = &rp;
  int rc = b.run(argv[1], argv[2], "run_rtl (ISS + rtl/v1/v1_mmu.sv)");
  printf("run_rtl: %llu RTL cycles, %ld stuck handshakes\n", (unsigned long long)rp.cyc, rp.stuck);
  delete rp.d;
  return rc || rp.stuck ? 1 : 0;
}
