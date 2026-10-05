// run_iss.cpp -- functional run of the bsc-memmap bare-metal ports (bscmm / bscmm2 .bin) on the golden ISS in SoC mode
// (tb/sh4_iss.h) with WI-BSC's register model (tb/sh4_bsc.h) behind the BSC block (0xFF80xxxx, the SDMR windows),
// and a minimal board: 16 MB of RAM (area 3, mirrored every 16 MB), the sim-control port, the boot stub's register
// state (SOC.md 7.3; board.md 5.3) written through the model's own P4 port, the CCN registers the programs read
// (PVR, PRR, CCR with its read mask, QACR), PMCTR counting instructions.  It checks that the programs and the model
// reproduce the console's "functional" lines (check.py --classes functional): register values, the size rule, the
// write keys, holes, the WCR3 / PCR write sizes.  The timing lines need the cycle-accurate v1_soc (SOC.md M3); the
// board lines need DcBoard.  Time: one Icyc per instruction (the refresh timer runs at that rate).
//   run_iss <prog.bin> [--out <log>] [--max <instructions>]
#include "sh4_iss.h"
#include "sh4_bsc.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

struct Ram : Sh4Mem {
  std::vector<uint8_t> m = std::vector<uint8_t>(16 << 20, 0);
  uint8_t *at(uint32_t a) { return &m[a & 0xFFFFFF]; }
  uint32_t rd(uint32_t a, int size) override { uint32_t v = 0; memcpy(&v, at(a), size); return v; }
  void wr(uint32_t a, int size, uint32_t v) override { memcpy(at(a), &v, size); }
};

struct Env : Sh4Env {
  Ram *ram;
  sh4bsc::Bsc bsc;
  uint32_t ccr = 0, qacr[2] = {4, 4}, pmcr[2] = {0xC023, 0};
  uint64_t icount = 0, pm0 = 0;
  std::string out;
  bool exited = false;
  uint32_t exit_code = 0;
  static uint32_t pa(uint32_t va) { return va & 0x1FFFFFFF; }

  uint32_t p4(uint32_t a, int size, bool wr, uint32_t v) {           // size 0 B 1 W 2 L
    if ((a >> 24) != 0xFF) return 0;                                 // arrays, reserved
    const uint32_t off = a & 0x0FFFFFFF, blk = (a >> 18) & 0x3F, o = a & 0xFF;
    uint32_t r = 0;
    if (blk == 0x20) { if (wr) bsc.wr(off, size, v, false); else bsc.rd(off, size, r, false); return r; }
    if (blk == 0x24 || blk == 0x25) { if (wr) bsc.sdmr.wr(off, size, v, false); return 0; }
    if (blk == 0x00) {                                               // CCN
      if (o == 0x30) return !wr && size == 2 ? 0x040205C1u : 0;     // PVR
      if (o == 0x1C) { if (wr && size == 2) ccr = v & 0x81A7; return !wr && size == 2 ? ccr : 0; }
      if (o == 0x38 || o == 0x3C) { uint32_t &q = qacr[o == 0x3C]; if (wr && size == 2) q = v & 0x1C; return !wr && size == 2 ? q : 0; }
      if (o == 0x84 || o == 0x88) {                                  // PMCR1 / 2 (16 bit): a clear restarts PMCTR
        uint32_t &p = pmcr[o == 0x88];
        if (wr && size == 1) { if (o == 0x84 && (v & 0x2000)) pm0 = icount; p = v & 0xC13F; }
        return !wr && size == 1 ? p : 0;
      }
      return 0;
    }
    if (blk == 0x04) return wr ? 0 : (uint32_t)(icount - pm0);       // PMCTR: instructions since the clear
    return 0;
  }
  int ifetch(uint32_t va, bool, uint16_t &op, bool &) override { op = (uint16_t)ram->rd(pa(va), 2); return 0; }
  int dcheck(uint32_t, int, int, bool, bool, bool &) override { return 0; }
  uint64_t read1(uint32_t va, int size) {
    const uint32_t p = pa(va);
    if ((va >> 29) == 7) return p4(va, size == 1 ? 0 : size == 2 ? 1 : 2, false, 0);
    if ((p >> 26) == 3) return ram->rd(p, size);
    return 0;
  }
  uint64_t dread(uint32_t va, int size, int, bool) override {
    return size == 8 ? (read1(va, 4) | (read1(va + 4, 4) << 32)) : read1(va, size);
  }
  void write1(uint32_t va, int size, uint32_t v) {
    const uint32_t p = pa(va);
    if ((va >> 29) == 7) { p4(va, size == 1 ? 0 : size == 2 ? 1 : 2, true, v); return; }
    if ((p >> 26) == 6) {
      if (p == 0x18000000) { exited = true; exit_code = v; }
      if (p == 0x18000004) out += (char)(v & 0xFF);
      return;
    }
    if ((p >> 26) == 3) ram->wr(p, size, v);
  }
  void dwrite(uint32_t va, int size, uint64_t v, int, bool) override {
    if (size == 8) { write1(va, 4, (uint32_t)v); write1(va + 4, 4, (uint32_t)(v >> 32)); return; }
    write1(va, size, (uint32_t)v);
  }
  void dctl(uint32_t, int, bool) override {}
  void ldtlb() override {}
  bool sqmd() override { return false; }
  void entry(const Sh4Entry &) override {}
};

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: run_iss <prog.bin> [--out log] [--max n]\n"); return 2; }
  const char *bin = argv[1], *outf = nullptr;
  uint64_t max = 400000000ull;
  for (int i = 2; i < argc; i++) {
    if (!strcmp(argv[i], "--out") && i + 1 < argc) outf = argv[++i];
    else if (!strcmp(argv[i], "--max") && i + 1 < argc) max = strtoull(argv[++i], 0, 0);
  }
  Ram ram;
  FILE *f = fopen(bin, "rb");
  if (!f) { perror(bin); return 2; }
  const size_t n = fread(ram.at(0x0C010000), 1, (16 << 20) - 0x10000, f);
  fclose(f);
  Env env;
  env.ram = &ram;
  // the chip after a pin power-on reset with the DC straps, then the boot stub's BSC writes (board.md 5.3)
  env.bsc.md = 0x065;
  env.bsc.port_in = 0x01CE3;
  env.bsc.reset_pin();
  auto w = [&](uint32_t off, int size, uint32_t v) { env.bsc.wr(off, size, v, false); env.bsc.bclk(); };
  w(0xF800000, 2, 0xA3020008); w(0xF800004, 1, 0x0001); w(0xF800008, 2, 0x01110111); w(0xF80000C, 2, 0x618066D8);
  w(0xF800010, 2, 0x07777777); w(0xF800014, 2, 0xC00A0E24); w(0xF800018, 1, 0x0000); w(0xF800024, 1, 0xA55E);
  w(0xF80001C, 1, 0xA510); w(0xF80002C, 2, 0x000A03F0); w(0xF800030, 1, 0x0004); w(0xF800040, 2, 0);
  w(0xF800048, 1, 0);
  env.bsc.sdmr.wr(0xF940190, 0, 0, false);
  Sh4Iss iss(&ram);
  iss.soc = true;
  iss.env = &env;
  iss.reset(true);
  iss.s.sr = 0x400000F0;
  iss.s.r[15] = 0x8C00F400; iss.s.pr = 0x8C00F800; iss.s.vbr = 0x8C000000; iss.s.dbr = 0x8C000010;
  iss.s.gbr = 0x8C000000; iss.s.ssr = 0x40000001; iss.s.spc = 0x8C000776; iss.s.fpscr = 0x00040001;
  iss.s.pc = 0x8C010000;
  uint64_t k = 0;
  for (; k < max && !env.exited; k++) {
    if (!iss.step()) break;
    env.icount++;
    env.bsc.port_in = 0x01CE3;
    if (env.icount & 1) env.bsc.bclk(env.bsc.rf_req);                // one Icyc per instruction, a Bcyc every two
  }
  if (outf) { FILE *o = fopen(outf, "w"); fputs(env.out.c_str(), o); fclose(o); }
  else fputs(env.out.c_str(), stdout);
  fprintf(stderr, "run_iss %s: %zu bytes loaded, %llu steps, %s (exit %u)%s%s\n", bin, n, (unsigned long long)k,
          env.exited ? "exited" : iss.error ? "ISS error" : iss.asleep ? "asleep" : "step limit", env.exit_code,
          iss.error ? ": " : "", iss.error ? iss.errmsg : "");
  if (iss.error) fprintf(stderr, "run_iss: pc %08x\n", iss.s.pc);
  return env.exited && env.exit_code == 0 ? 0 : 1;
}
