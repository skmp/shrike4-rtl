// run_iss.cpp -- functional run of the debug-ubc-hudi bare-metal ports (p4probe / ubcbrk / ubcevt .bin) on the golden
// ISS in SoC mode (tb/sh4_iss.h) with WI-DBG's unit models (tb/sh4_dbg.h: UBC through the ISS's ubc_pre / ubc_post /
// ubc_oper hooks, PMCR / PMCTR, the CCN id slot, the H-UDI) behind a minimal board: 16 MB of RAM, the sim-control port
// (putchar, exit, time = instructions executed), the boot stub's register state (SOC.md 7.3), the other P4 blocks as
// plain registers with the size rule, and the undriven UBC window returning the last operand read (SOC.md 3.1.6).
// It checks that the programs and the models reproduce the console's "functional" lines (check.py --classes
// functional); the "timing" lines need the cycle-accurate v1_soc.  Not a reference for anything timing-related: PMCTR
// counts instructions as cycles.
//   run_iss <prog.bin> [--out <log>] [--max <instructions>]
#include "sh4_iss.h"
#include "sh4_dbg.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

struct Ram : Sh4Mem {
  std::vector<uint8_t> m = std::vector<uint8_t>(16 << 20, 0);
  uint8_t *at(uint32_t a) { return &m[a & 0xFFFFFF]; }
  uint32_t rd(uint32_t a, int size) override {
    uint32_t v = 0;
    memcpy(&v, at(a), size);
    return v;
  }
  void wr(uint32_t a, int size, uint32_t v) override { memcpy(at(a), &v, size); }
};

struct Env : Sh4Env {
  Ram *ram;
  dbg::Ubc ubc{-1};                      // functional: no fetch-side delay
  dbg::Pmc pmc;
  dbg::CcnId ccnid;
  dbg::Hudi hudi;
  std::map<uint32_t, std::pair<int, uint32_t>> regs;   // other P4 registers: address -> (size, value)
  uint32_t expevt = 0, intevt = 0, tra = 0, tea = 0, pteh = 0, ptel = 0, ttb = 0, mmucr = 0, ptea = 0;
  uint32_t ccr = 0, qacr[2] = {0, 0};
  uint32_t last_rdata = 0;
  uint64_t icount = 0;
  std::string out;
  bool exited = false;
  uint32_t exit_code = 0;
  uint64_t ev_a0 = 0, ev_b0 = 0;

  uint8_t asid() const { return pteh & 0xFF; }
  static bool p4(uint32_t va) { return (va >> 29) == 7; }
  static uint32_t pa(uint32_t va) { return va & 0x1FFFFFFF; }

  bool ccn_own(uint32_t o, int size, bool wr, uint32_t &v) {       // CCN registers of the other owners (32-bit)
    uint32_t *r = nullptr;
    uint32_t mask = 0xFFFFFFFFu;
    switch (o) {
    case 0x00: r = &pteh; break;      case 0x04: r = &ptel; break;     case 0x08: r = &ttb; break;
    case 0x0C: r = &tea; break;       case 0x10: r = &mmucr; break;    case 0x34: r = &ptea; break;
    case 0x1C: r = &ccr; mask = 0x81A7; break;                          // ICI / OCI read 0
    case 0x38: r = &qacr[0]; mask = 0x1C; break;  case 0x3C: r = &qacr[1]; mask = 0x1C; break;
    case 0x20: r = &tra; break;       case 0x24: r = &expevt; break;   case 0x28: r = &intevt; break;
    default: return false;
    }
    if (wr) { if (size == 2) *r = v & mask; }
    else v = size == 2 ? *r : 0;
    return true;
  }
  uint32_t p4_access(uint32_t a, int size, bool wr, uint32_t v) {  // size 0 B 1 W 2 L
    if ((a >> 24) != 0xFF) return 0;                                 // arrays, reserved: read 0, write ignored
    uint32_t off = a & 0x0FFFFFFF, blk = (a >> 18) & 0x3F, o = a & 0xFF;
    uint32_t r = 0;
    auto slave = [&](P4Slave &s) { if (wr) s.wr(off, size, v, false); else s.rd(off, size, r, false); return r; };
    if (blk == 0x00) {
      if (o == 0x14 || o == 0x18) return slave(ubc);
      if (o == 0x84 || o == 0x88) return slave(pmc);
      if (ccn_own(o, size, wr, v)) return wr ? 0 : v;
      return slave(ccnid);
    }
    if (blk == 0x04) return slave(pmc);
    if (blk == 0x08) {
      if ((a & 0x3FF00) == 0) return slave(ubc);
      return wr ? 0 : last_rdata;                                    // the undriven window (SOC.md 3.1.6)
    }
    if (blk == 0x3C) return slave(hudi);
    if (blk == 0x24 || blk == 0x25) return 0;                        // SDMR2 / SDMR3
    auto it = regs.find(a);
    if (wr) { if (it == regs.end() || it->second.first == size) regs[a] = {size, v}; return 0; }
    return (it != regs.end() && it->second.first == size) ? it->second.second : 0;
  }

  // ---- Sh4Env
  int ifetch(uint32_t va, bool, uint16_t &op, bool &) override { op = (uint16_t)ram->rd(pa(va), 2); return 0; }
  int dcheck(uint32_t, int, int, bool, bool, bool &) override { return 0; }
  uint64_t read1(uint32_t va, int size) {
    uint32_t p = pa(va);
    if (p4(va)) return p4_access(va, size == 1 ? 0 : size == 2 ? 1 : 2, false, 0);
    if ((p >> 26) == 6) {                                            // area 6: the sim-control port
      if (p == 0x18000008) return (uint32_t)(icount * 1);
      if (p == 0x1800000C) return (uint32_t)(icount >> 32);
      return 0;
    }
    if ((p >> 26) == 3) return ram->rd(p, size);
    return 0;
  }
  uint64_t dread(uint32_t va, int size, int, bool) override {
    uint64_t v = size == 8 ? (read1(va, 4) | (read1(va + 4, 4) << 32)) : read1(va, size);
    last_rdata = (uint32_t)v;
    return v;
  }
  void write1(uint32_t va, int size, uint32_t v) {
    uint32_t p = pa(va);
    if (p4(va)) { p4_access(va, size == 1 ? 0 : size == 2 ? 1 : 2, true, v); return; }
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
  void entry(const Sh4Entry &e) override {
    if (e.kind == Sh4Entry::IRQ) intevt = e.code & 0xFFF; else expevt = e.code & 0xFFF;
    if (e.tra_v) tra = (e.tra & 0xFF) << 2;
    if (e.tea_v) { tea = e.tea; pteh = (pteh & 0xFFu) | (e.tea & ~0x3FFu); }
  }
  bool ubc_pre(uint32_t pc) override { return ubc.pre(pc, asid()); }
  bool ubc_post(uint32_t pc) override { return ubc.post(pc, asid()); }
  int ubc_oper(uint32_t va, int size, int kind, uint64_t data) override { return ubc.oper(va, size, kind, data, asid()); }
  bool ubc_ubde() override { return ubc.ubde(); }

  // one instruction's events for the functional PMCTR (elapsed = issued = one per instruction, UBC matches)
  void events() {
    pmc.count(0x23, 1);
    pmc.count(0x13, 1);
    if (ubc.n_ev_a != ev_a0) { pmc.count(0x19, ubc.n_ev_a - ev_a0); ev_a0 = ubc.n_ev_a; }
    if (ubc.n_ev_b != ev_b0) { pmc.count(0x1A, ubc.n_ev_b - ev_b0); ev_b0 = ubc.n_ev_b; }
  }
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
  size_t n = fread(ram.at(0x0C010000), 1, (16 << 20) - 0x10000, f);
  fclose(f);
  Env env;
  env.ram = &ram;
  // the boot stub's chip state (SOC.md 7.3; board.md 5.3) for the registers the programs read
  auto reg = [&](uint32_t a, int size, uint32_t v) { env.regs[a] = {size, v}; };
  reg(0xFF800000, 2, 0xA3020008); reg(0xFF800004, 1, 0x0001); reg(0xFF800008, 2, 0x01110111);
  reg(0xFF80000C, 2, 0x618066D8); reg(0xFF800010, 2, 0x07777777); reg(0xFF800014, 2, 0xC00A0E24);
  reg(0xFFC00000, 1, 0x0E0A); reg(0xFFC00004, 0, 0x03); reg(0xFFD00000, 1, 0x8000);
  reg(0xFFE80000, 1, 0x0000); reg(0xFFE80004, 0, 0x0C); reg(0xFFE80008, 1, 0x0030); reg(0xFFE80018, 1, 0x0040);
  reg(0xFFE80020, 1, 0x0011); reg(0xFFA0000C, 2, 0x000853C3); reg(0xFFA0002C, 2, 0x000012C0);
  reg(0xFFA00040, 2, 0x00008201);
  env.pmc.wr(0xF000084, 1, 0xC023, false);
  env.qacr[0] = env.qacr[1] = 4;
  Sh4Iss iss(&ram);
  iss.soc = true;
  iss.env = &env;
  iss.reset(true);
  iss.s.sr = 0x400000F0;                 // every register is 0 after reset(true): no bank swap needed
  iss.s.r[15] = 0x8C00F400; iss.s.pr = 0x8C00F800; iss.s.vbr = 0x8C000000; iss.s.dbr = 0x8C000010;
  iss.s.gbr = 0x8C000000; iss.s.ssr = 0x40000001; iss.s.spc = 0x8C000776; iss.s.fpscr = 0x00040001;
  iss.s.pc = 0x8C010000;
  uint64_t k = 0;
  for (; k < max && !env.exited; k++) {
    if (!iss.step()) break;
    env.icount++;
    if (iss.last.kind == Sh4Iss::SK_INSN) env.events();
  }
  if (outf) { FILE *o = fopen(outf, "w"); fputs(env.out.c_str(), o); fclose(o); }
  else fputs(env.out.c_str(), stdout);
  fprintf(stderr, "run_iss %s: %zu bytes loaded, %llu steps, %s (exit %u)%s%s\n", bin, n, (unsigned long long)k,
          env.exited ? "exited" : iss.error ? "ISS error" : iss.asleep ? "asleep" : "step limit", env.exit_code,
          iss.error ? ": " : "", iss.error ? iss.errmsg : "");
  if (iss.error) fprintf(stderr, "run_iss: pc %08x\n", iss.s.pc);
  return env.exited && env.exit_code == 0 ? 0 : 1;
}
