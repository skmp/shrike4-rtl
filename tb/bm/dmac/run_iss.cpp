// run_iss.cpp -- functional run of the bare-metal DMAC probes (build/bm/dmac/dmac1.bin, dmac2.bin; WI-DMAC,
// README.md) on the golden ISS in SoC mode (tb/sh4_iss.h) with WI-DMAC's model (tb/dmac_ref.h) and WI-EXC's INTC
// model (tb/sh4_intc.h, read only) behind a minimal board: 16 MB of RAM (area 3, mirrored), the sim-control port,
// the boot stub's DMAC / BSC / INTC register state (SOC.md 7.3), the other P4 blocks as plain registers with the size
// rule.  The DMAC moves one unit every 46 instructions (dmac_ref.h stand-alone time: a nominal 46 Icyc per unit,
// spec 3.13), its DMTEn / DMAE levels reach the INTC, and the ISS takes the interrupt when IMASK / BL allow.  PMCTR
// counts instructions.  It checks that the programs, the shim and the model reproduce the console's functional lines
// (check.py --classes functional); timing needs the cycle-accurate v1_soc, S12 needs the operand cache.
//   run_iss <prog.bin> [--out <log>] [--max <instructions>]
#include "sh4_iss.h"
#include "dmac_ref.h"
#include "sh4_intc.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

struct Ram : Sh4Mem {
  std::vector<uint8_t> m = std::vector<uint8_t>(16 << 20, 0);
  uint8_t *at(uint32_t a) { return &m[a & 0xFFFFFF]; }
  uint32_t rd(uint32_t a, int size) override { uint32_t v = 0; memcpy(&v, at(a), size); return v; }
  void wr(uint32_t a, int size, uint32_t v) override { memcpy(at(a), &v, size); }
};

struct Env : Sh4Env, DmacBus {
  Ram *ram = nullptr;
  DmacRef dmac{this};
  Sh4Intc intc;
  std::map<uint32_t, std::pair<int, uint32_t>> regs;   // other P4 registers: address -> (size, value)
  uint32_t expevt = 0, intevt = 0, tra = 0, ccr = 0;
  uint16_t pmcr[2] = {0, 0};
  uint64_t pm_acc[2] = {0, 0}, pm_t0[2] = {0, 0};
  uint64_t icount = 0;
  std::string out;
  bool exited = false;
  uint32_t exit_code = 0;
  int bad_access = 0;

  // ---- the P4 register space: a is 0xFFxxxxxx (the CPU's P4 address, or the DMAC's 0x1F.. as 0xFF000000 | a[23:0])
  uint64_t pmctr(int n) const { return pm_acc[n] + ((pmcr[n] & 0xC000) == 0xC000 ? icount - pm_t0[n] : 0); }
  void pmcr_wr(int n, uint16_t v) {
    bool was = (pmcr[n] & 0xC000) == 0xC000, now = (v & 0xC000) == 0xC000;
    if (was && !now) pm_acc[n] += icount - pm_t0[n];
    if (!now && (v & 0x2000)) pm_acc[n] = 0;                        // CLR clears a stopped counter
    if (!was && now) pm_t0[n] = icount;
    pmcr[n] = v & 0xC13F;
  }
  // returns false for "no module" (P4B_NONE / P4B_UBC_NODRV: the DMAC's class-B err)
  bool p4(uint32_t a, int size, bool wr, uint32_t &v, bool dma) {
    const uint32_t off = a & 0x0FFFFFFF, blk = (a >> 18) & 0x3F, o = a & 0xFF;
    if (!wr) v = 0;
    switch (blk) {
    case 0x28: { uint32_t r = v; bool h = wr ? dmac.wr(off, size, v, dma) : dmac.rd(off, size, r, dma); if (!wr) v = r; (void)h; return true; }
    case 0x34: { uint32_t r = v; wr ? intc.wr(off, size, v, dma) : intc.rd(off, size, r, dma); if (!wr) v = r; return true; }
    case 0x00:                                                        // CCN (a[7:0] only)
      if (o == 0x84 || o == 0x88) { int n = o == 0x88; if (wr) { if (size == 1) pmcr_wr(n, (uint16_t)v); } else v = size == 1 ? pmcr[n] : 0; return true; }
      if (o == 0x1C) { if (wr) { if (size == 2) ccr = v & 0x81A7; } else v = size == 2 ? ccr : 0; return true; }
      if (o == 0x24) { if (!wr) v = size == 2 ? expevt : 0; return true; }
      if (o == 0x28) { if (!wr) v = size == 2 ? intevt : 0; return true; }
      if (o == 0x20) { if (!wr) v = size == 2 ? tra : 0; return true; }
      if (o == 0x30) { if (!wr) v = size == 2 ? 0x040205C1u : 0; return true; }
      return true;
    case 0x04: {                                                      // PMCTR: a[4:2] only, reads at any size
      if (wr) return true;
      const uint32_t r = (a >> 2) & 7;
      v = r == 1 ? (uint32_t)(pmctr(0) >> 32) & 0xFFFF : r == 2 ? (uint32_t)pmctr(0) :
          r == 3 ? (uint32_t)(pmctr(1) >> 32) & 0xFFFF : r >= 4 ? (uint32_t)pmctr(1) : 0;
      return true;
    }
    case 0x08: if ((a & 0x3FF00) != 0) return false;                  // UBC_NODRV: no slave
      /* fall through */
    case 0x20: case 0x24: case 0x25: case 0x30: case 0x32: case 0x36: case 0x38: case 0x3A: case 0x3C: {
      auto it = regs.find(a);
      if (wr) { if (it == regs.end() || it->second.first == size) regs[a] = {size, v}; return true; }
      v = (it != regs.end() && it->second.first == size) ? it->second.second : 0;
      return true;
    }
    default: return false;                                            // P4B_NONE
    }
  }

  // ---- DmacBus: the model's legs
  void ext_read(uint32_t pa, int bytes, uint8_t *buf) override {
    for (int k = 0; k < bytes; k++) buf[k] = ((pa >> 26) & 7) == 3 ? *ram->at(pa + k) : 0;
  }
  void ext_write(uint32_t pa, int bytes, const uint8_t *buf) override {
    if (((pa >> 26) & 7) == 3) for (int k = 0; k < bytes; k++) *ram->at(pa + k) = buf[k];
  }
  bool onchip_read(uint32_t pa, int size, uint32_t &v) override { return p4(0xFF000000u | (pa & 0xFFFFFF), size, false, v, true); }
  bool onchip_write(uint32_t pa, int size, uint32_t v) override { return p4(0xFF000000u | (pa & 0xFFFFFF), size, true, v, true); }
  void dack_unit(int, bool, uint32_t, int) override {}              // no DACK device on this board

  // ---- Sh4Env
  static uint32_t pa(uint32_t va) { return va & 0x1FFFFFFF; }
  int ifetch(uint32_t va, bool, uint16_t &op, bool &) override { op = (uint16_t)ram->rd(pa(va), 2); return 0; }
  int dcheck(uint32_t, int, int, bool, bool, bool &) override { return 0; }
  uint32_t read1(uint32_t va, int size) {
    if ((va >> 29) == 7) {
      uint32_t v = 0;
      if ((va >> 24) == 0xFF) p4(va, size == 1 ? 0 : size == 2 ? 1 : 2, false, v, false);
      return v;
    }
    const uint32_t p = pa(va);
    if ((p >> 26) == 3) return ram->rd(p, size);
    return 0;                                                         // Holly (SB_MDST reads 0: maple idle), others
  }
  void write1(uint32_t va, int size, uint32_t v) {
    if ((va >> 29) == 7) {
      if ((va >> 24) == 0xFF) p4(va, size == 1 ? 0 : size == 2 ? 1 : 2, true, v, false);
      return;
    }
    const uint32_t p = pa(va);
    if ((p >> 26) == 6) {
      if (p == 0x18000000) { exited = true; exit_code = v; }
      if (p == 0x18000004) out += (char)(v & 0xFF);
      return;
    }
    if ((p >> 26) == 3) ram->wr(p, size, v);
  }
  uint64_t dread(uint32_t va, int size, int, bool) override {
    return size == 8 ? (read1(va, 4) | ((uint64_t)read1(va + 4, 4) << 32)) : read1(va, size);
  }
  void dwrite(uint32_t va, int size, uint64_t v, int, bool) override {
    if (size == 8) { write1(va, 4, (uint32_t)v); write1(va + 4, 4, (uint32_t)(v >> 32)); return; }
    write1(va, size, (uint32_t)v);
  }
  void dctl(uint32_t, int, bool) override {}                          // no cache: OCBP / OCBI change nothing
  void ldtlb() override {}
  bool sqmd() override { return false; }
  void entry(const Sh4Entry &e) override {
    if (e.kind == Sh4Entry::IRQ) intevt = e.code & 0xFFF; else expevt = e.code & 0xFFF;
    if (e.tra_v) tra = (e.tra & 0xFF) << 2;
  }

  // the DMAC's interrupt levels into the INTC (irq_src_t bits)
  void levels() {
    uint32_t s = 0;
    if (dmac.dmte(0)) s |= 1u << Sh4Intc::S_DMTE0;
    if (dmac.dmte(1)) s |= 1u << Sh4Intc::S_DMTE1;
    if (dmac.dmte(2)) s |= 1u << Sh4Intc::S_DMTE2;
    if (dmac.dmte(3)) s |= 1u << Sh4Intc::S_DMTE3;
    if (dmac.dmae()) s |= 1u << Sh4Intc::S_DMAE;
    intc.src = s;
  }
};

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: run_iss <prog.bin> [--out log] [--max n]\n"); return 2; }
  const char *bin = argv[1], *outf = nullptr;
  uint64_t max = 600000000ull;
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
  // the boot stub's chip state (SOC.md 7.3; board.md 5.3): the DMAC through its own register port (channels before
  // DMAOR: CHCR0's TE is not writable, so it reads 0x853C1), the registers the programs read
  uint32_t v;
  v = 0x000853C3; env.p4(0xFFA0000C, 2, true, v, false);
  v = 0x000012C0; env.p4(0xFFA0002C, 2, true, v, false);
  v = 0x00008201; env.p4(0xFFA00040, 2, true, v, false);
  auto reg = [&](uint32_t a, int size, uint32_t val) { env.regs[a] = {size, val}; };
  reg(0xFF800000, 2, 0xA3020008); reg(0xFF800004, 1, 0x0001); reg(0xFF800008, 2, 0x01110111);
  reg(0xFF80000C, 2, 0x618066D8); reg(0xFF800010, 2, 0x07777777); reg(0xFF800014, 2, 0xC00A0E24);
  reg(0xFFC00000, 1, 0x0E0A); reg(0xFFC00004, 0, 0x03);
  Sh4Iss iss(&ram);
  iss.soc = true;
  iss.env = &env;
  iss.reset(true);
  iss.s.sr = 0x400000F0;
  iss.s.r[15] = 0x8C00F400; iss.s.pr = 0x8C00F800; iss.s.vbr = 0x8C000000; iss.s.dbr = 0x8C000010;
  iss.s.gbr = 0x8C000000; iss.s.ssr = 0x40000001; iss.s.spc = 0x8C000776; iss.s.fpscr = 0x00040001;
  iss.s.pc = 0x8C010000;
  uint64_t k = 0, nirq = 0;
  for (; k < max && !env.exited; k++) {
    if (!iss.step() && !iss.asleep) break;
    env.icount++;
    env.dmac.tick_icyc(1);
    env.levels();
    const int imask = (iss.s.sr >> 4) & 15;
    const bool bl = (iss.s.sr >> 28) & 1;
    if (iss.interruptible() && env.intc.accept(imask, bl, iss.asleep)) {
      iss.interrupt(env.intc.port(imask, bl, iss.asleep).code);
      nirq++;
    }
  }
  if (outf) { FILE *o = fopen(outf, "w"); fputs(env.out.c_str(), o); fclose(o); }
  else fputs(env.out.c_str(), stdout);
  fprintf(stderr, "run_iss %s: %zu bytes loaded, %llu steps, %llu interrupts, %llu DMA units, %s (exit %u)%s%s\n", bin,
          n, (unsigned long long)k, (unsigned long long)nirq, (unsigned long long)env.dmac.units,
          env.exited ? "exited" : iss.error ? "ISS error" : iss.asleep ? "asleep" : "step limit", env.exit_code,
          iss.error ? ": " : "", iss.error ? iss.errmsg : "");
  if (iss.error) fprintf(stderr, "run_iss: pc %08x\n", iss.s.pc);
  return env.exited && env.exit_code == 0 ? 0 : 1;
}
