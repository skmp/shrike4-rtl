// run_iss.cpp -- functional run of the bare-metal p4b probe (tb/bm/p4bus, WI-P4) on the golden ISS in SoC mode
// (tb/sh4_iss.h) behind a minimal board: 16 MB of RAM in area 3, the sim-control port (putchar, exit), and the P4
// registers the probe touches, decoded as v1_p4bus decodes them (tb/p4bus_ref.h): PMCTR counts executed instructions
// (a functional counter, not a clock), the TMU / INTC / PMC registers are plain registers with the SOC.md 3.1.4 size
// rule (a register answers only its own size), at their power-on values.  It shows that the port builds and runs to
// completion (exit code 0) and that the program restores TCR1 / TCOR1 / TCNT1; check.py --level struct checks its
// output.  Its B - A numbers are instruction counts: only the cycle-accurate v1_soc gives the console's.
//   run_iss <p4b.bin> [--out <log>] [--max <instructions>]
#include "sh4_iss.h"
#include "p4bus_ref.h"
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
  Ram *ram = nullptr;
  std::map<uint32_t, std::pair<int, uint32_t>> regs;   // P4 address -> (size in bytes, value)
  uint64_t icount = 0, p4_reads = 0, p4_writes = 0;
  std::string out;
  bool exited = false;
  uint32_t exit_code = 0;
  uint32_t expevt = 0, intevt = 0;

  static bool p4(uint32_t va) { return (va >> 29) == 7; }
  static uint32_t pa(uint32_t va) { return va & 0x1FFFFFFF; }

  uint32_t p4_access(uint32_t a, int size, bool wr, uint32_t v) {
    const int blk = p4ref::decode(a, false);
    if (blk == p4ref::P4B_PMCTR) return wr ? 0 : (uint32_t)icount;              // read only; instructions as cycles
    if (blk == p4ref::P4B_CCN_EXC && !wr) return (a & 0xFF) == 0x24 ? expevt : (a & 0xFF) == 0x28 ? intevt : 0;
    if (p4ref::port_of(blk) == p4ref::PP_BRIDGE) return 0;
    auto it = regs.find(a);
    if (it == regs.end() || it->second.first != size) return 0;                   // a hole, or another size
    if (wr) { it->second.second = v & (size == 4 ? 0xFFFFFFFFu : (1u << (8 * size)) - 1); return 0; }
    return it->second.second;
  }

  // ---- Sh4Env
  int ifetch(uint32_t va, bool, uint16_t &op, bool &) override { op = (uint16_t)ram->rd(pa(va), 2); return 0; }
  int dcheck(uint32_t, int, int, bool, bool, bool &) override { return 0; }
  uint64_t read1(uint32_t va, int size) {
    const uint32_t p = pa(va);
    if (p4(va)) { p4_reads++; return p4_access(va, size, false, 0); }
    if ((p >> 26) == 3) return ram->rd(p, size);
    return 0;
  }
  uint64_t dread(uint32_t va, int size, int, bool) override {
    return size == 8 ? (read1(va, 4) | (read1(va + 4, 4) << 32)) : read1(va, size);
  }
  void write1(uint32_t va, int size, uint32_t v) {
    const uint32_t p = pa(va);
    if (p4(va)) { p4_writes++; p4_access(va, size, true, v); return; }
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
  }
};

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: run_iss <p4b.bin> [--out log] [--max n]\n"); return 2; }
  const char *bin = argv[1], *outf = nullptr;
  uint64_t max = 200000000ull;
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
  // the registers p4b.c touches, at their power-on values (hm Table A.1; SOC.md 7.3 leaves the TMU and IPRA at reset)
  auto reg = [&](uint32_t a, int size, uint32_t v) { env.regs[a] = {size, v}; };
  reg(0xFF000084, 2, 0); reg(0xFF000088, 2, 0);                                 // PMCR1 / PMCR2
  reg(0xFFD80000, 1, 0); reg(0xFFD80004, 1, 0);                                 // TOCR, TSTR
  reg(0xFFD80014, 4, 0xFFFFFFFF); reg(0xFFD80018, 4, 0xFFFFFFFF); reg(0xFFD8001C, 2, 0);   // TCOR1 TCNT1 TCR1
  reg(0xFFD00000, 2, 0x8000); reg(0xFFD00004, 2, 0); reg(0xFFD00008, 2, 0); reg(0xFFD0000C, 2, 0);   // ICR IPRA-C
  Sh4Iss iss(&ram);
  iss.soc = true;
  iss.env = &env;
  iss.reset(true);
  // the boot stub's CPU state (SOC.md 7.3)
  iss.s.sr = 0x400000F0;
  iss.s.r[15] = 0x8C00F400; iss.s.pr = 0x8C00F800; iss.s.vbr = 0x8C000000; iss.s.dbr = 0x8C000010;
  iss.s.gbr = 0x8C000000; iss.s.ssr = 0x40000001; iss.s.spc = 0x8C000776; iss.s.fpscr = 0x00040001;
  iss.s.pc = 0x8C010000;
  uint64_t k = 0;
  for (; k < max && !env.exited; k++) {
    if (!iss.step()) break;
    env.icount++;
  }
  if (outf) { FILE *o = fopen(outf, "w"); fputs(env.out.c_str(), o); fclose(o); }
  else fputs(env.out.c_str(), stdout);
  fprintf(stderr, "run_iss %s: %zu bytes loaded, %llu steps, %llu P4 reads, %llu P4 writes, %s (exit %u)%s%s\n", bin, n,
          (unsigned long long)k, (unsigned long long)env.p4_reads, (unsigned long long)env.p4_writes,
          env.exited ? "exited" : iss.error ? "ISS error" : iss.asleep ? "asleep" : "step limit", env.exit_code,
          iss.error ? ": " : "", iss.error ? iss.errmsg : "");
  if (iss.error) fprintf(stderr, "run_iss: pc %08x\n", iss.s.pc);
  return env.exited && env.exit_code == 0 ? 0 : 1;
}
