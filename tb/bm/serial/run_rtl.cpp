// run_rtl.cpp -- functional replay of the serial console probes (tb/bm/serial: scif_probe / scif_probe2 / scif_probe3,
// the console's own sources on a KOS shim) against the RTL SCIF and SCI (WI-SER).
//
// The CPU is the golden ISS in SoC mode (tb/sh4_iss.h).  Every access to the SCIF (0xFFE80000) or the SCI (0xFFE00000)
// is a strobe of the Verilated rtl/v1/v1_scif.sv / v1_sci.sv (serial_bm_top.sv) on a pce cycle; the RTL runs every
// Icyc.  Around them sits a minimal chip + board, enough for the probes:
//   * time: one Icyc per instruction, plus the P4 path costs below; PMCTR1 counts Icyc (PMCR1 = 0xC023: elapsed);
//   * the Pclk path (CPG, INTC, SCI, SCIF, ...): one transaction at a time, strobes on pce cycles.  A read strobes at
//     the first pce after its MA1 and acks 11 Icyc later (WI-P4's P_D1 = 1, P_D2 = 11: 16 Icyc back to back); a write
//     is posted (3 Icyc) and drains at the first pce >= MA1 + 3, the path staying busy 4 Icyc after the strobe (8 Icyc
//     per write back to back, serial 2.1 "8.8");  Bclk registers (DMAC) cost 8 / 3, Iclk (CCN, PMC) 3;
//   * STBCR: MSTP0 / MSTP3 drive the SCI / SCIF module stops;  IPRB / IPRC: the SCI / SCIF interrupt levels; a
//     functional INTC takes the highest level > SR.IMASK when SR.BL = 0 and the ISS is interruptible;
//   * DMAC channel 1 (the probe's S17): on-chip requests RS 1000-1011 from the RTL's dreq levels, cycle steal, one
//     unit per request sample, the P4 leg through the Pclk path with p4s_req_t.dma = 1 (the SCIF / SCI auto-clears);
//   * the boot stub's chip state (SOC.md 7.3) for the registers outside the SCIF (STBCR 0x03, DMAOR 0x8201, ...); the
//     SCIF itself gets the stub's writes through its port before the program starts.
// It is a functional check of the RTL against the console logs (check.py --classes functional): the CPU timing here
// is not the chip's, so the Icyc columns of the logs are for v1_soc (WI-SOC).  Not a reference for timing.
//   run_rtl <prog.bin> [--out <log>] [--max <instructions>] [--tx <tap transcript>]
#include "Vserial_bm_top.h"
#include "verilated.h"
#include "sh4_iss.h"
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

// one transaction on the Pclk path
struct PAcc {
  bool v = false, wr = false, dma = false, done = false;
  int size = 0;                       // 0 B, 1 W, 2 L
  uint32_t a = 0, wdata = 0, rdata = 0;
  uint64_t ready = 0;                 // earliest strobe cycle
};

struct Sys : Sh4Env {
  Vserial_bm_top *t = nullptr;
  Ram *ram = nullptr;
  Sh4Iss *iss = nullptr;
  uint64_t cyc = 0;
  // ---- chip registers outside the SCIF / SCI
  uint8_t stbcr = 0x03, stbcr2 = 0;
  uint16_t frqcr = 0x0E0A, icr = 0x8000, ipr[3] = {0, 0, 0};
  uint32_t expevt = 0, intevt = 0, tra = 0, ccr = 0;
  uint16_t pmcr[2] = {0, 0};
  uint64_t pm_acc[2] = {0, 0}, pm_t0[2] = {0, 0};
  uint32_t sar[4] = {}, dar[4] = {}, dmatcr[4] = {}, chcr[4] = {0x000853C3, 0, 0x000012C0, 0}, dmaor = 0x00008201;
  std::map<uint32_t, std::pair<int, uint32_t>> regs;   // other P4 registers: plain storage with the size rule
  // ---- the Pclk path
  PAcc cpu, post, dma;                // CPU read, CPU posted write, DMAC leg
  uint64_t path_free = 0;             // no strobe before this cycle (the previous transaction's ack / release)
  // ---- DMAC channel 1
  int dstate = 0;                     // 0 idle, 1 source leg, 2 destination leg
  uint64_t dnext = 0, dready = 0;
  uint32_t dval = 0;
  long ndma = 0, nirq = 0;
  // ---- board
  std::string out, txlog;
  bool exited = false;
  uint32_t exit_code = 0;

  static bool rtl_blk(uint32_t a) { return (a & 0xFFFC0000u) == 0xFFE00000u || (a & 0xFFFC0000u) == 0xFFE80000u; }
  static bool pclk_blk(uint32_t a) { return (a >> 24) == 0xFF && ((a >> 18) & 0x3F) >= 0x30 && ((a >> 18) & 0x3F) < 0x3C; }

  // ---- the RTL clock: one Icyc.  At a pce cycle the path strobes at most one transaction (DMAC first, SOC.md 3.1.1).
  void tick() {
    const bool pce = (cyc & 3) == 3;
    PAcc *s = nullptr;
    if (pce && cyc >= path_free) {
      if (dma.v && !dma.done && cyc >= dma.ready) s = &dma;
      else if (post.v && !post.done && cyc >= post.ready) s = &post;
      else if (cpu.v && !cpu.done && cyc >= cpu.ready) s = &cpu;
    }
    t->s_rd = t->s_wr = t->s_dma = 0;
    if (s && rtl_blk(s->a)) {
      t->sel = (s->a & 0xFFFC0000u) == 0xFFE80000u;
      t->s_rd = !s->wr; t->s_wr = s->wr; t->s_dma = s->dma; t->s_size = s->size;
      t->s_off = s->a & 0x0FFFFFFF; t->s_wdata = s->wdata;
    }
    t->mstp_sci = stbcr & 1; t->mstp_scif = (stbcr >> 3) & 1;
    t->pce = pce; t->clk = 0; t->eval();
    if (s) {
      s->rdata = rtl_blk(s->a) ? t->r_rdata : pclk_reg(s->a, s->size, s->wr, s->wdata);
      s->done = true;
      path_free = cyc + (s->wr ? 4 : 11) + 1;
    }
    if (t->tap_tx_v) txlog += (char)t->tap_tx_byte;
    t->clk = 1; t->eval();
    cyc++;
    dmac();
  }
  void ticks(uint64_t n) { for (uint64_t i = 0; i < n; i++) tick(); }

  // ---- the plain Pclk registers (CPG, INTC, TMU, RTC, WDT: only what the probes touch has behaviour)
  uint32_t pclk_reg(uint32_t a, int size, bool wr, uint32_t v) {
    struct R { uint32_t a; int size; } ;
    auto rw8 = [&](uint8_t &r) -> uint32_t { if (size != 0) return 0; if (wr) r = (uint8_t)v; return r; };
    auto rw16 = [&](uint16_t &r) -> uint32_t { if (size != 1) return 0; if (wr) r = (uint16_t)v; return r; };
    switch (a) {
    case 0xFFC00000: return wr ? 0 : rw16(frqcr);                                  // FRQCR: held (writes ignored here)
    case 0xFFC00004: return rw8(stbcr);
    case 0xFFC00010: return rw8(stbcr2);
    case 0xFFD00000: return rw16(icr);
    case 0xFFD00004: return rw16(ipr[0]);
    case 0xFFD00008: return rw16(ipr[1]);
    case 0xFFD0000C: return rw16(ipr[2]);
    default: break;
    }
    auto it = regs.find(a);
    if (wr) { if (it == regs.end() || it->second.first == size) regs[a] = {size, v}; return 0; }
    return (it != regs.end() && it->second.first == size) ? it->second.second : 0;
  }

  // ---- a CPU access on the Pclk path (the MA stage waits: the ISS instruction completes afterwards)
  uint32_t pclk_cpu(bool wr, int size, uint32_t a, uint32_t v) {
    while (post.v && !post.done) tick();                  // one posted entry: a later access waits for its drain
    post.v = false;
    if (wr) {
      post = PAcc(); post.v = true; post.wr = true; post.size = size; post.a = a; post.wdata = v; post.ready = cyc + 3;
      ticks(3);                                            // MA: the write moves into the entry
      return 0;
    }
    cpu = PAcc(); cpu.v = true; cpu.size = size; cpu.a = a; cpu.ready = cyc + 1;
    while (!cpu.done) tick();
    ticks(10);                                             // the return synchroniser to the ack
    cpu.v = false;
    return cpu.rdata;
  }

  // ---- DMAC channel 1: cycle steal, on-chip requests (dmac spec; only what the probes use)
  bool dreq(int rs) const {
    switch (rs) {
    case 0x8: return t->c_dreq_tx;   case 0x9: return t->c_dreq_rx;
    case 0xA: return t->f_dreq_tx;   case 0xB: return t->f_dreq_rx;
    default: return false;
    }
  }
  static int tsz(uint32_t ch) { static const int s[8] = {8, 1, 2, 4, 32, 1, 1, 1}; return s[(ch >> 4) & 7]; }
  static uint32_t step(uint32_t a, int m, int n) { return m == 1 ? a + n : m == 2 ? a - n : a; }
  static bool onchip(uint32_t pa) { return (pa >> 24) == 0x1F; }
  void dmac() {
    const uint32_t ch = chcr[1];
    const bool en = (ch & 1) && !(ch & 2) && (dmaor & 1) && !(dmaor & 6);
    if (!en) { dstate = 0; dma.v = false; return; }
    const int n = tsz(ch);
    if (dstate == 0) {
      if (cyc < dnext || !dreq((ch >> 8) & 0xF)) return;
      dstate = 1;
      const uint32_t src = sar[1] & 0x1FFFFFFF;
      if (onchip(src)) { dma = PAcc(); dma.v = true; dma.dma = true; dma.size = n == 1 ? 0 : n == 2 ? 1 : 2;
                         dma.a = 0xFF000000u | (src & 0xFFFFFF); dma.ready = cyc + 2; }
      else { dval = ram->rd(src, n > 4 ? 4 : n); dready = cyc + 12; }       // a memory read leg (row hit)
      return;
    }
    if (dstate == 1) {
      if (dma.v) { if (!dma.done) return; dval = dma.rdata; dma.v = false; }
      else if (cyc < dready) return;
      dstate = 2;
      const uint32_t dst = dar[1] & 0x1FFFFFFF;
      if (onchip(dst)) { dma = PAcc(); dma.v = true; dma.wr = true; dma.dma = true; dma.size = n == 1 ? 0 : n == 2 ? 1 : 2;
                         dma.a = 0xFF000000u | (dst & 0xFFFFFF); dma.wdata = dval; dma.ready = cyc + 1; }
      else { ram->wr(dst, n > 4 ? 4 : n, dval); dready = cyc + 4; }
      return;
    }
    if (dma.v) { if (!dma.done) return; dma.v = false; }
    else if (cyc < dready) return;
    sar[1] = step(sar[1], (ch >> 12) & 3, n);
    dar[1] = step(dar[1], (ch >> 14) & 3, n);
    dmatcr[1] = (dmatcr[1] - 1) & 0xFFFFFF;
    if (!dmatcr[1]) chcr[1] |= 2;                          // TE
    dstate = 0; dnext = cyc + 8;                          // the channel's turnaround before it samples again
    ndma++;
  }
  uint32_t dmac_reg(uint32_t o, int size, bool wr, uint32_t v) {
    if (size != 2) return 0;
    if (o == 0x40) { if (wr) dmaor = (dmaor & ~0x8307u) | (v & 0x8307u); return wr ? 0 : dmaor; }
    if (o >= 0x40) return 0;
    const int c = o >> 4;
    uint32_t *r = nullptr;
    switch (o & 0xC) { case 0x0: r = &sar[c]; break; case 0x4: r = &dar[c]; break; case 0x8: r = &dmatcr[c]; break;
                       default: r = &chcr[c]; break; }
    if (!wr) return *r;
    if ((o & 0xC) == 0xC) *r = (v & ~2u) | (*r & v & 2u);  // TE: cleared by writing 0 (the probe writes 0)
    else if ((o & 0xC) == 0x8) *r = v & 0xFFFFFF;
    else *r = v;
    if (c == 1 && (o & 0xC) == 0xC) { dstate = 0; dma.v = false; dnext = cyc; }
    return 0;
  }

  // ---- the P4 area
  uint32_t p4(uint32_t a, int size, bool wr, uint32_t v) {  // size 0 B 1 W 2 L
    if ((a >> 24) != 0xFF) return 0;
    const uint32_t blk = (a >> 18) & 0x3F;
    if (pclk_blk(a)) return pclk_cpu(wr, size, a, v);
    if (blk == 0x28) { ticks(wr ? 3 : 8); return dmac_reg(a & 0xFF, size, wr, v); }       // DMAC (Bclk)
    ticks(3);                                                                             // Iclk class
    const uint64_t now = cyc - 3;                                                         // PMCTR sampled in MA1
    if (blk == 0x04) {                                                                    // PMCTR1H / 1L / 2H / 2L
      const int k = ((a >> 2) & 7) >= 3 ? 1 : 0;
      const uint64_t c = pm_acc[k] + (((pmcr[k] & 0xC000) == 0xC000) ? now - pm_t0[k] : 0);
      const unsigned r = (a >> 2) & 7;
      return wr ? 0 : r == 1 || r == 3 ? (uint32_t)(c >> 32) & 0xFFFF : r == 0 ? 0 : (uint32_t)c;
    }
    if (blk == 0x00) {
      const uint32_t o = a & 0xFF;
      if (o == 0x84 || o == 0x88) {
        const int k = o == 0x88;
        if (size != 1) return 0;
        if (!wr) return pmcr[k];
        const bool was = (pmcr[k] & 0xC000) == 0xC000, now_run = (v & 0xC000) == 0xC000;
        if (was && !now_run) pm_acc[k] += now - pm_t0[k];
        if (!was && now_run) pm_t0[k] = now;
        if ((v & 0x2000) && !now_run) pm_acc[k] = 0;
        pmcr[k] = (uint16_t)(v & ~0x2000);
        return 0;
      }
      uint32_t *r = o == 0x1C ? &ccr : o == 0x20 ? &tra : o == 0x24 ? &expevt : o == 0x28 ? &intevt : nullptr;
      if (r) { if (size != 2) return 0; if (wr) { if (o == 0x1C) *r = v & 0x81A7; } return wr ? 0 : *r; }
      return 0;
    }
    auto it = regs.find(a);
    if (wr) { if (it == regs.end() || it->second.first == size) regs[a] = {size, v}; return 0; }
    return (it != regs.end() && it->second.first == size) ? it->second.second : 0;
  }

  // ---- Sh4Env
  static uint32_t pa(uint32_t va) { return va & 0x1FFFFFFF; }
  int ifetch(uint32_t va, bool, uint16_t &op, bool &) override { op = (uint16_t)ram->rd(pa(va), 2); return 0; }
  int dcheck(uint32_t, int, int, bool, bool, bool &) override { return 0; }
  uint64_t read1(uint32_t va, int size) {
    if ((va >> 29) == 7) return p4(va, size == 1 ? 0 : size == 2 ? 1 : 2, false, 0);
    const uint32_t p = pa(va);
    if ((p >> 26) == 3) return ram->rd(p, size);
    return 0;
  }
  uint64_t dread(uint32_t va, int size, int, bool) override {
    return size == 8 ? (read1(va, 4) | (read1(va + 4, 4) << 32)) : read1(va, size);
  }
  void write1(uint32_t va, int size, uint32_t v) {
    if ((va >> 29) == 7) { p4(va, size == 1 ? 0 : size == 2 ? 1 : 2, true, v); return; }
    const uint32_t p = pa(va);
    if ((p >> 26) == 6) {                                    // the sim-control port (board.md 3.10)
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
  }

  // ---- the INTC (functional): SCI at IPRB[7:4], SCIF at IPRC[7:4]; within a module the hm Table 19.5 order
  uint32_t pending(int &lvl) const {
    const int ls = (ipr[1] >> 4) & 15, lf = (ipr[2] >> 4) & 15;
    uint32_t sci = t->c_eri ? 0x4E0 : t->c_rxi ? 0x500 : t->c_txi ? 0x520 : t->c_tei ? 0x540 : 0;
    uint32_t scif = t->f_eri ? 0x700 : t->f_rxi ? 0x720 : t->f_bri ? 0x740 : t->f_txi ? 0x760 : 0;
    if (!ls) sci = 0;
    if (!lf) scif = 0;
    if (sci && (!scif || ls >= lf)) { lvl = ls; return sci; }
    lvl = lf;
    return scif;
  }
};

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  if (argc < 2) { fprintf(stderr, "usage: run_rtl <prog.bin> [--out log] [--max n] [--tx file]\n"); return 2; }
  const char *bin = argv[1], *outf = nullptr, *txf = nullptr;
  uint64_t max = 400000000ull;
  for (int i = 2; i < argc; i++) {
    if (!strcmp(argv[i], "--out") && i + 1 < argc) outf = argv[++i];
    else if (!strcmp(argv[i], "--max") && i + 1 < argc) max = strtoull(argv[++i], 0, 0);
    else if (!strcmp(argv[i], "--tx") && i + 1 < argc) txf = argv[++i];
  }
  Ram ram;
  FILE *f = fopen(bin, "rb");
  if (!f) { perror(bin); return 2; }
  size_t n = fread(ram.at(0x0C010000), 1, (16 << 20) - 0x10000, f);
  fclose(f);
  Sys sys;
  sys.ram = &ram;
  sys.t = new Vserial_bm_top;
  // power-on reset, then the boot stub's SCIF writes through the port (SOC.md 7.3: SCSCR2 = 0, SCFCR2 = 6, SCSMR2 = 0,
  // SCBRR2 = 0x0C, SCFCR2 = 0x40, SCSPTR2 = 0x11, SCSCR2 = 0x30), STBCR = 0x03 already set above
  sys.t->rst_por = 1; sys.ticks(16); sys.t->rst_por = 0; sys.ticks(4);
  const struct { uint32_t a; int size; uint32_t v; } stub[] = {
    {0xFFE80008, 1, 0x0000}, {0xFFE80018, 1, 0x0006}, {0xFFE80000, 1, 0x0000}, {0xFFE80004, 0, 0x0C},
    {0xFFE80018, 1, 0x0040}, {0xFFE80020, 1, 0x0011}, {0xFFE80008, 1, 0x0030}};
  for (auto &w : stub) sys.pclk_cpu(true, w.size, w.a, w.v);
  sys.ticks(64);
  Sh4Iss iss(&ram);
  iss.soc = true;
  iss.env = &sys;
  sys.iss = &iss;
  iss.reset(true);
  iss.s.sr = 0x400000F0;
  iss.s.r[15] = 0x8C00F400; iss.s.pr = 0x8C00F800; iss.s.vbr = 0x8C000000; iss.s.dbr = 0x8C000010;
  iss.s.gbr = 0x8C000000; iss.s.ssr = 0x40000001; iss.s.spc = 0x8C000776; iss.s.fpscr = 0x00040001;
  iss.s.pc = 0x8C010000;
  uint64_t k = 0;
  for (; k < max && !sys.exited; k++) {
    int lvl = 0;
    const uint32_t code = sys.pending(lvl);
    if (code && lvl > (int)((iss.s.sr >> 4) & 15) && !((iss.s.sr >> 28) & 1) && iss.interruptible()) {
      iss.interrupt(code);
      sys.nirq++;
      sys.ticks(12);                                       // the entry (not a reference)
      continue;
    }
    if (!iss.step()) { if (iss.asleep) { sys.tick(); continue; } break; }
    sys.tick();                                            // one Icyc per instruction (its accesses added their own)
  }
  if (outf) { FILE *o = fopen(outf, "w"); fputs(sys.out.c_str(), o); fclose(o); }
  else fputs(sys.out.c_str(), stdout);
  if (txf) { FILE *o = fopen(txf, "wb"); fwrite(sys.txlog.data(), 1, sys.txlog.size(), o); fclose(o); }
  fprintf(stderr, "run_rtl %s: %zu bytes, %llu steps, %llu Icyc, %ld interrupts, %ld DMAC units, %zu SCIF frames: %s (exit %u)%s%s\n",
          bin, n, (unsigned long long)k, (unsigned long long)sys.cyc, sys.nirq, sys.ndma, sys.txlog.size(),
          sys.exited ? "exited" : iss.error ? "ISS error" : "step limit", sys.exit_code, iss.error ? ": " : "",
          iss.error ? iss.errmsg : "");
  if (iss.error) fprintf(stderr, "run_rtl: pc %08x\n", iss.s.pc);
  sys.t->final();
  delete sys.t;
  return sys.exited && sys.exit_code == 0 ? 0 : 1;
}
