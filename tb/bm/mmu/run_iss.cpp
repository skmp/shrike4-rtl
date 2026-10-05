// run_iss.cpp -- run the bare-metal MMU probe port (build/bm_mmu/mmubm.bin) on the reference ISS (tb/sh4_iss.h, SoC
// mode) with the MMU model tb/mmu_ref.h and the flat board of bm_board.h, and dump the result block for
// `expect.py check`.  Owner: WI-MMU.  Together with run_rtl.cpp (the same run with the RTL MMU) it validates the port,
// the model, the RTL and expect.json against the console logs before the SoC exists: every functional record must
// come out (the pipeline / fetch / cache / timing / PMC records are skipped: a functional run has none of those).
//   usage: run_iss <mmubm.bin> <dump.bin>
#include "bm_board.h"

using namespace mmuref;

struct ModelPort : MmuPort {
  Sh4Mmu m;
  void sync_ccr() { m.ccr.ora = ccr_ora(); m.ccr.cb = ccr_cb(); m.ccr.wt = ccr_wt(); m.ccr.oce = ccr_oce(); }
  DXlat data(uint32_t va, int kind, int size, bool md, bool rmw) override {
    (void)size;
    sync_ccr();
    return m.data(va, kind, md, rmw);
  }
  int fetch(uint32_t va, bool md, uint32_t &pa, bool &vec400) override {
    sync_ccr();
    IXlat r = m.fetch(va, md);
    if (r.kind == IXlat::FAULT) { vec400 = r.vec400; return r.fx; }
    pa = r.pa;
    return 0;
  }
  bool reg_rd(uint32_t off, int szc, uint32_t &v) override { return m.reg_rd(off, szc, v); }
  bool reg_wr(uint32_t off, int szc, uint32_t v) override { return m.reg_wr(off, szc, v); }
  uint32_t arr_rd(uint32_t off, int szc) override { uint32_t v; m.arr_rd(off, szc, v); return v; }
  bool arr_wr(uint32_t off, int szc, uint32_t v) override { m.arr_wr(off, szc, v); return m.arr_mhit; }
  void commit() override { m.urc_step(); }
  void ldtlb() override { m.ldtlb(); }
  void exc_capture(uint32_t tea) override { m.exc_capture(tea); }
  bool sqmd() override { return m.sqmd; }
  bool at() override { return m.at; }
  bool counted(uint32_t, int, const DXlat &x) override { return x.counted; }
};

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: run_iss <mmubm.bin> <dump.bin>\n"); return 2; }
  static ModelPort mp;
  static BmBoard b;
  mp.m.reset_por();
  b.mmu = &mp;
  return b.run(argv[1], argv[2], "run_iss (ISS + tb/mmu_ref.h)");
}
