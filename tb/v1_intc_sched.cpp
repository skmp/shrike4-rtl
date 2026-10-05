// v1_intc_sched.cpp -- WI-EXC's schedule tool (`make v1-intc-sched`; rtl/v1/SOC.md 3.4, 4.4.1, 9.4): re-derives the
// interrupt-boundary tables of tb/v1_intc_progs.h from the CURRENT v1 core and diffs them against that file.
//
// The IMASK synchroniser of rtl/v1/v1_intc.sv is fitted to the console (ci2, excint run 1 C1, excint run 3, the excint2
// PH sweep) through those tables: each console program's own code, run on v1's schedule, gives the D cycles at which
// an interrupt could be taken, relative to the SR write.  The unit testbench (tb/v1_intc_tb.cpp, group progs) replays
// them on v1_intc; this tool makes them reproducible and tells when a core change (WI-CORE-A's LDC SR / RTE / refetch,
// WI-CORE-B's fetch) has made them stale.
//
// How (tb/v1_intc_sched_top.sv: v1_core on v1_ideal_mem, a P4 responder):
//   * the console ELF (tools/hw/exceptions-intc/excint{,2,3}.elf, tools/hw/core-integration/ci2.elf) is loaded as
//     linked (0x8C010000 up, P1), so every snippet has the console's alignment; the snippet is entered at its symbol
//     with the registers its C driver passes (run_asm / ci_call: r4-r6, SR, PR = a SLEEP stub);
//   * each program runs twice from a core reset: the first run warms the operand cache's line states (v1_ideal_mem
//     keeps them across a core reset), the second is traced -- the console's warm trials;
//   * s = the cycle sr_imask (S-stage write-through) shows the lowered IMASK (ci2: the second pass, as ci_run2.out);
//   * a boundary is a cycle with the D leader valid, not a delay slot, not inside a multi-micro-op instruction and no
//     pipeline hold (SOC.md 4.4.1; u_core.q_v[0], sq_on, frz; the slot from the issue trace); its label is
//     (leader PC - base) / 2, or -1 before base (tb/v1_intc_progs.h);
//   * the PH sweep: m = the TCNT1 read's ack cycle; PH_SM[n] = s - m and the chain's first offset PH_FIRST.
// Offsets 0 .. HORIZON (24) are compared, up to the last counted instruction the core issues there (the tables' chains
// are nominally 20 long; a snippet has 16 / 32 / 64 counted adds): the IMASK path reaches the core at s + 9 / s + 10.
//
// usage: sched [--emit] [--only NAME] [-v] [--trace]
//   --emit: print the derived tables as C++ for tb/v1_intc_progs.h; -v: both tables for every program; --trace: the
//   traced run cycle by cycle (issue pair, D leader, sequencer / hold, sr_imask, P4 acks)
// Last line "v1-intc-sched: PASS" (the tables match the core) / "FAIL"; exit 1 on a mismatch or a broken run.
#include "Vv1_intc_sched_top.h"
#include "verilated.h"
#include "svdpi.h"
#include "v1_intc_progs.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>

static const int HORIZON = 24;
static const uint32_t STOP_PC = 0x8C0F0000u;          // PR: a SLEEP stub (outside every console image)
static const uint32_t SCRATCH = 0x8C0F8000u;          // a data word for LDC.L @Rm+,SR
static const uint32_t STACK = 0x8C0FF000u;
static const int MAX_CYC = 6000;

// ---------------------------------------------------------------------------------------------------------------------
// memory: 16 MB at physical 0x0C000000 (P1 / P2 / P0 alias), little endian
// ---------------------------------------------------------------------------------------------------------------------
struct Mem {
  static const uint32_t BASE = 0x0C000000u, SIZE = 16u << 20;
  std::vector<uint8_t> m = std::vector<uint8_t>(SIZE);
  uint64_t bad = 0;
  uint32_t bad_addr = 0;
  uint8_t *at(uint32_t a, unsigned n) {
    a &= 0x1FFFFFFFu;
    if (a < BASE || a - BASE > SIZE - n) return nullptr;
    return &m[a - BASE];
  }
  uint64_t get(uint32_t a, unsigned n) {
    const uint8_t *q = at(a, n);
    if (!q) return 0;
    uint64_t v = 0;
    for (unsigned k = 0; k < n; k++) v |= (uint64_t)q[k] << (8 * k);
    return v;
  }
  void put(uint32_t a, unsigned n, uint64_t v) {
    uint8_t *q = at(a, n);
    if (!q) { if (!bad++) bad_addr = a; return; }
    for (unsigned k = 0; k < n; k++) q[k] = (uint8_t)(v >> (8 * k));
  }
};
static Mem g_mem;

extern "C" {
unsigned int isched_if_rd(unsigned int a, svBit req, unsigned int) {
  if (req && !g_mem.at(a & ~3u, 4) && !g_mem.bad++) g_mem.bad_addr = a;
  return (unsigned int)g_mem.get(a & ~3u, 4);
}
unsigned long long isched_dm_rd(unsigned int a, int size, svBit, unsigned int) { return g_mem.get(a, 1u << (size & 3)); }
void isched_dm_wr(unsigned int a, int size, unsigned long long v) { g_mem.put(a, 1u << (size & 3), v); }
}

// ---------------------------------------------------------------------------------------------------------------------
// ELF32 (SH, little endian): PT_LOAD segments into the memory, the symbol table
// ---------------------------------------------------------------------------------------------------------------------
struct Elf {
  std::map<std::string, uint32_t> sym;
  std::vector<uint8_t> f;
  uint32_t u32(size_t o) const { return f[o] | f[o + 1] << 8 | f[o + 2] << 16 | (uint32_t)f[o + 3] << 24; }
  uint16_t u16(size_t o) const { return (uint16_t)(f[o] | f[o + 1] << 8); }
  bool read(const std::string &path) {
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) { fprintf(stderr, "sched: cannot open %s\n", path.c_str()); return false; }
    fseek(fp, 0, SEEK_END); const long n = ftell(fp); fseek(fp, 0, SEEK_SET);
    f.resize((size_t)n);
    const bool ok = fread(f.data(), 1, f.size(), fp) == f.size();
    fclose(fp);
    if (!ok || n < 52 || memcmp(f.data(), "\x7f" "ELF", 4) || f[4] != 1 || f[5] != 1) {
      fprintf(stderr, "sched: %s is not a little-endian ELF32\n", path.c_str());
      return false;
    }
    const uint32_t shoff = u32(32);
    const uint16_t shentsize = u16(46), shnum = u16(48);
    for (unsigned i = 0; i < shnum; i++) {
      const size_t sh = shoff + (size_t)i * shentsize;
      if (u32(sh + 4) != 2) continue;                        // SHT_SYMTAB
      const uint32_t off = u32(sh + 16), size = u32(sh + 20), link = u32(sh + 24);
      const uint32_t stroff = u32(shoff + (size_t)link * shentsize + 16);
      for (uint32_t s = 0; s + 16 <= size; s += 16) {
        const char *nm = (const char *)&f[stroff + u32(off + s)];
        if (*nm) sym[nm] = u32(off + s + 4);
      }
    }
    return true;
  }
  void load(Mem &mem) const {
    const uint32_t phoff = u32(28);
    const uint16_t phentsize = u16(42), phnum = u16(44);
    for (unsigned i = 0; i < phnum; i++) {
      const size_t ph = phoff + (size_t)i * phentsize;
      if (u32(ph) != 1) continue;                            // PT_LOAD
      const uint32_t off = u32(ph + 4), va = u32(ph + 8), fsz = u32(ph + 16), msz = u32(ph + 20);
      for (uint32_t k = 0; k < msz; k++) mem.put(va + k, 1, k < fsz ? f[off + k] : 0);
    }
  }
  uint32_t at(const std::string &s) const {
    auto it = sym.find("_" + s);
    if (it == sym.end()) it = sym.find(s);
    if (it == sym.end()) { fprintf(stderr, "sched: no symbol %s\n", s.c_str()); exit(2); }
    return it->second;
  }
};

// ---------------------------------------------------------------------------------------------------------------------
// one program: its code, its entry state, where its counted instructions start
// ---------------------------------------------------------------------------------------------------------------------
struct Spec {
  std::string name;
  std::string elf;
  std::string entry;
  std::vector<std::pair<int, uint32_t>> regs;             // dbg addresses: 0-15 Rn, 24 SR
  std::vector<std::pair<uint32_t, uint32_t>> words;       // memory words written before each run
  int unmask = 0;                                         // which lowering of sr_imask is s (0 = the first)
  std::function<uint32_t(const Elf &)> base;              // the first counted instruction (after the traced run)
  bool ph = false;                                        // PH sweep: also s - m
  int counted = 16;                                       // counted instructions from base (the adds of the snippet)
};

struct Cyc { bool i0, i1, dv, dsq, dfrz, ack; uint32_t i0pc, i1pc, dpc, p4a; int imask; };

static bool delayed_branch(uint16_t op) {
  if ((op & 0xF000) == 0xA000 || (op & 0xF000) == 0xB000) return true;          // BRA, BSR
  if ((op & 0xFF00) == 0x8D00 || (op & 0xFF00) == 0x8F00) return true;          // BT/S, BF/S
  if ((op & 0xF0FF) == 0x0023 || (op & 0xF0FF) == 0x0003) return true;          // BRAF, BSRF
  if ((op & 0xF0FF) == 0x402B || (op & 0xF0FF) == 0x400B) return true;          // JMP, JSR
  return op == 0x000B || op == 0x002B;                                          // RTS, RTE
}

static Vv1_intc_sched_top *g_m;
static uint32_t g_epoch = 0;
static bool g_trace = false;
static void tick() { g_m->clk = 0; g_m->eval(); g_m->clk = 1; g_m->eval(); }

// reset the core (and, first time, the ideal memory), load the context, run to the SLEEP stub; the trace if asked
static bool run(const Spec &sp, const Elf &elf, bool cold, std::vector<Cyc> *tr) {
  for (auto &w : sp.words) g_mem.put(w.first, 4, w.second);
  g_m->mem_epoch = ++g_epoch;
  g_m->rst = 1; g_m->im_rst = cold; g_m->dbg_we = 0;
  tick(); tick();
  g_m->im_rst = 0;
  std::vector<std::pair<int, uint32_t>> ctx = {{24, 0x400000F0u}};   // SR first (the banks follow RB)
  for (auto &r : sp.regs) ctx.push_back(r);
  ctx.push_back({15, STACK});
  ctx.push_back({33, STOP_PC});                                    // PR
  ctx.push_back({34, elf.at(sp.entry)});                           // RESET_PC
  for (auto &r : ctx) { g_m->dbg_we = 1; g_m->dbg_addr = (uint8_t)r.first; g_m->dbg_wdata = r.second; tick(); }
  g_m->dbg_we = 0;
  tick();                                    // RESET_PC lands at the last write's edge; the core takes it one edge later
  g_m->rst = 0;
  for (int c = 0; c < MAX_CYC; c++) {
    const uint64_t bad0 = g_mem.bad;
    g_m->clk = 0; g_m->eval();
    if (g_trace)
      printf("%s", tr ? "" : "  (warm-up)");
    if (g_trace)
      printf("    %4d %c%08x %c%08x  D %d %08x sq %d frz %d  imask %x%s%s\n", c, g_m->i0_valid ? '*' : ' ', g_m->i0_pc,
             g_m->i1_valid ? '*' : ' ', g_m->i1_pc, g_m->d_v, g_m->d_pc, g_m->d_sq, g_m->d_frz, g_m->sr_imask,
             g_m->p4_ack ? "  p4 ack" : "", g_mem.bad != bad0 ? "  OUTSIDE" : "");
    if (g_m->halted) return true;
    if (tr) tr->push_back({(bool)g_m->i0_valid, (bool)g_m->i1_valid, (bool)g_m->d_v, (bool)g_m->d_sq,
                           (bool)g_m->d_frz, (bool)g_m->p4_ack, g_m->i0_pc, g_m->i1_pc, g_m->d_pc, g_m->p4_addr,
                           (int)g_m->sr_imask});
    g_m->clk = 1; g_m->eval();
  }
  fprintf(stderr, "sched: %s did not reach the SLEEP stub in %d cycles\n", sp.name.c_str(), MAX_CYC);
  return false;
}

struct Derived { bool ok = false; std::vector<Bnd> b; int sm = -1; std::string why; };

static Derived derive(const Spec &sp) {
  Derived d;
  Elf elf;
  if (!elf.read(sp.elf)) { d.why = "ELF"; return d; }
  std::fill(g_mem.m.begin(), g_mem.m.end(), 0);
  g_mem.bad = 0;
  elf.load(g_mem);
  g_mem.put(STOP_PC, 2, 0x001B);                                   // SLEEP (halts the harness)
  for (uint32_t a = STOP_PC + 2; a < STOP_PC + 32; a += 2) g_mem.put(a, 2, 0x0009);
  std::vector<Cyc> tr;
  if (!run(sp, elf, true, nullptr) || !run(sp, elf, false, &tr)) { d.why = "run"; return d; }
  if (g_mem.bad) {
    char b[64]; snprintf(b, sizeof b, "%llu accesses outside the memory, first %08x", (unsigned long long)g_mem.bad,
                         g_mem.bad_addr);
    d.why = b; return d;
  }
  // s: the requested lowering of sr_imask
  int s = -1, k = 0;
  for (size_t c = 1; c < tr.size(); c++)
    if (tr[c].imask < tr[c - 1].imask && k++ == sp.unmask) { s = (int)c; break; }
  if (s < 0) { d.why = "no IMASK lowering"; return d; }
  const uint32_t base = sp.base(elf);
  // the delay-slot term: the leader is a slot when the last instruction issued before this cycle was a delayed
  // branch and the leader is the instruction after it
  uint32_t last_pc = 0; bool last_db = false;
  for (int c = 0; c < (int)tr.size(); c++) {
    const Cyc &y = tr[c];
    const bool slot = last_db && y.dpc == last_pc + 2;
    const int off = c - s;
    const int label = y.dpc >= base ? (int)(y.dpc - base) / 2 : -1;
    if (off >= 0 && off <= HORIZON && y.dv && !y.dsq && !y.dfrz && !slot && label < sp.counted)
      d.b.push_back({off, label});
    for (int l = 0; l < 2; l++) {
      if (!(l ? y.i1 : y.i0)) continue;
      const uint32_t pc = l ? y.i1pc : y.i0pc;
      if (pc == last_pc) continue;                               // a multi-micro-op instruction's next micro-op
      last_pc = pc; last_db = delayed_branch((uint16_t)g_mem.get(pc, 2));
    }
  }
  if (sp.ph) {                                                   // m: the TCNT1 read's ack before s
    int m = -1;
    for (int c = 0; c < s; c++) if (tr[c].ack && (tr[c].p4a >> 16) == 0xFFD8) m = c;
    if (m < 0) { d.why = "no TCNT1 ack"; return d; }
    d.sm = s - m;
  }
  d.ok = true;
  return d;
}

// the table as offset -> label over 0 .. h
static std::map<int, int> as_map(const std::vector<Bnd> &b, int h = HORIZON) {
  std::map<int, int> r;
  for (auto &x : b) if (x.off >= 0 && x.off <= h) r[x.off] = x.label;
  return r;
}
static int last_off(const std::vector<Bnd> &b) {
  int h = -1;
  for (auto &x : b) h = x.off > h ? x.off : h;
  return h;
}
static std::string show(const std::map<int, int> &m) {
  std::string s;
  char b[24];
  for (auto &kv : m) { snprintf(b, sizeof b, " %d:%d", kv.first, kv.second); s += b; }
  return s;
}
// compress a boundary list into the pre / chain / {off,label} form of tb/v1_intc_progs.h
static std::string emit(const std::vector<Bnd> &b) {
  std::vector<std::string> parts;
  size_t i = 0;
  while (i < b.size()) {
    size_t j = i;
    char t[96];
    if (b[i].label == -1) {
      while (j + 1 < b.size() && b[j + 1].label == -1 && b[j + 1].off == b[j].off + 1) j++;
      snprintf(t, sizeof t, "pre(%d, %d)", b[i].off, b[j].off);
    } else {
      while (j + 1 < b.size() && b[j + 1].label == b[j].label + 1 && b[j + 1].off == b[j].off + 1) j++;
      if (j == i && i + 1 < b.size()) snprintf(t, sizeof t, "std::vector<Bnd>{{%d, %d}}", b[i].off, b[i].label);
      else snprintf(t, sizeof t, "chain(%d, 20, %d)", b[i].off, b[i].label);
      if (j + 1 == b.size()) j = b.size() - 1;
    }
    parts.push_back(t);
    i = j + 1;
  }
  std::string s = parts.back();
  for (int k = (int)parts.size() - 2; k >= 0; k--) s = "cat(" + parts[k] + ", " + s + ")";
  return s;
}

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  bool emit_tables = false, verbose = false;
  const char *only = nullptr;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--emit")) emit_tables = true;
    else if (!strcmp(argv[i], "-v")) verbose = true;
    else if (!strcmp(argv[i], "--trace")) g_trace = true;
    else if (!strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i];
  }
  const std::string HW = "tools/hw/", EI = HW + "exceptions-intc/", CI = HW + "core-integration/ci2.elf";
  // the console drivers' arguments: excint / excint3 sr6 = 0x40000061 (KOS SR 40000001 | IMASK 6), sr15 = 0x400000F1,
  // PMCTR0L 0xFF100008; excint2 TCNT1 0xFFD80018; ci2: _ci_param = SR with IMASK 14, r13 = PMCTR (the snippet's LDL)
  const uint32_t SR6 = 0x40000061u, SR15 = 0x400000F1u, PM = 0xFF100008u, TCNT1 = 0xFFD80018u;
  auto ci2_out = [](int idx) { return [idx](const Elf &e) { return (uint32_t)g_mem.get(e.at("ci_out") + 4 * idx, 4); }; };
  auto sym = [](const char *s) { return [s](const Elf &e) { return e.at(s); }; };
  std::vector<Spec> specs;
  // ci2's snippets record their labels with OUTADDR: _ci_out[0] = the adds after LDC SR / the RTE target (3:), and in
  // the BRA cases _ci_out[3] = the branch target (5:)
  Elf ci2e;
  if (!ci2e.read(CI)) return 2;
  auto ci2 = [&](const std::string &name, const std::string &entry, int out_idx) {
    specs.push_back({name, CI, entry, {}, {{ci2e.at("ci_param"), 0x400000E0u}}, 1, ci2_out(out_idx), false});
  };
  ci2("ci2 LDC SR ; 16 adds", "t2_irq_ldc", 0);
  ci2("ci2 RTE ; slot ; target adds", "t2_irq_rte", 0);
  ci2("ci2 LDC SR ; BRA ; slot ; target", "t2_irq_br0", 3);
  for (int k = 1; k < 10; k++)
    ci2("ci2 LDC SR ; " + std::to_string(k) + " NOP ; BRA", "t2_irq_br" + std::to_string(k), 3);
  specs.push_back({"excint C1 LDC SR ; 64 adds", EI + "excint.elf", "t_chain_ldcsr", {{4, SR6}, {5, PM}}, {}, 0,
                   sym("L_cl1"), false, 64});
  specs.push_back({"excint C1 LDC.L @Rm+,SR ; 64 adds", EI + "excint.elf", "t_chain_ldclsr", {{4, SCRATCH}, {5, PM}},
                   {{SCRATCH, SR6}}, 0, sym("L_cll1"), false, 64});
  specs.push_back({"excint C1 RTE ; nop ; target adds", EI + "excint.elf", "t_chain_rte", {{4, SR6}, {5, PM}}, {}, 0,
                   sym("L_cr1"), false, 64});
  specs.push_back({"excint3 RTE ; nop ; PMCTR read ; adds", EI + "excint3.elf", "t_rteirq",
                   {{4, SR6}, {5, PM}, {6, SR15}}, {}, 0, sym("L_r3c"), false, 64});
  specs.push_back({"excint3 LDC SR ; PMCTR read ; adds", EI + "excint3.elf", "t_ldcirq",
                   {{4, SR6}, {5, PM}, {6, SR15}}, {}, 0, sym("L_l3c"), false, 64});
  static std::vector<std::string> phn;
  for (int n = 0; n < 12; n++) { phn.push_back("PH " + std::to_string(n)); phn.push_back("t_ph" + std::to_string(n));
                                 phn.push_back("L_ph" + std::to_string(n)); }
  for (int n = 0; n < 12; n++)
    specs.push_back({phn[3 * n], EI + "excint2.elf", phn[3 * n + 1], {{4, SR6}, {5, TCNT1}, {6, SR15}}, {}, 0,
                     [n](const Elf &e) { return e.at(phn[3 * n + 2]); }, true, 32});

  g_m = new Vv1_intc_sched_top;
  int bad = 0, runs = 0;
  std::map<std::string, Prog> table;
  for (auto &p : intc_progs()) table[p.name] = p;
  std::vector<std::string> emitted;
  int ph_sm[12], ph_first = -1;
  bool ph_ok = true;
  for (auto &sp : specs) {
    if (only && sp.name.find(only) == std::string::npos) continue;
    runs++;
    const Derived d = derive(sp);
    if (!d.ok) { printf("  %-40s BROKEN RUN (%s)\n", sp.name.c_str(), d.why.c_str()); bad++; continue; }
    std::map<int, int> got = as_map(d.b);
    std::map<int, int> want;
    if (sp.ph) {
      const int n = atoi(sp.name.c_str() + 3);
      Elf e; e.read(sp.elf);
      if (e.at(phn[3 * n + 2]) != PH_L[n]) {
        printf("  %-40s PH_L[%d] %08x != the ELF's L_ph%d %08x\n", sp.name.c_str(), n, PH_L[n], n, e.at(phn[3 * n + 2]));
        bad++;
      }
      want = as_map(chain(PH_FIRST, 32));
      ph_sm[n] = d.sm;
      if (!got.empty()) ph_first = got.begin()->first;
      const bool ok = got == want && d.sm == PH_SM[n];
      printf("  %-40s s - m %d (table %d), first add at s + %d (table %d)  %s\n", sp.name.c_str(), d.sm, PH_SM[n],
             got.empty() ? -1 : got.begin()->first, PH_FIRST, ok ? "ok" : "STALE");
      if (!ok) { bad++; ph_ok = false; printf("      core: %s\n      table:%s\n", show(got).c_str(), show(want).c_str()); }
      continue;
    }
    auto it = table.find(sp.name);
    if (it == table.end()) { printf("  %-40s no such entry in tb/v1_intc_progs.h\n", sp.name.c_str()); bad++; continue; }
    // compared up to the last counted instruction the core issued inside the horizon (the tables' chains are nominal)
    const int h = std::min(HORIZON, std::min(last_off(d.b), last_off(it->second.b)));
    got = as_map(d.b, h);
    want = as_map(it->second.b, h);
    const bool ok = got == want;
    printf("  %-40s %s\n", sp.name.c_str(), ok ? "ok" : "STALE");
    if (!ok || verbose) printf("      core: %s\n      table:%s\n", show(got).c_str(), show(want).c_str());
    if (!ok) bad++;
    char line[512];
    std::string meas;
    for (size_t i = 0; i < it->second.meas.size(); i++) meas += (i ? ", " : "") + std::to_string(it->second.meas[i]);
    snprintf(line, sizeof line, "    {\"%s\", %s, {%s}},", sp.name.c_str(), emit(d.b).c_str(), meas.c_str());
    emitted.push_back(line);
  }
  if (emit_tables) {
    printf("---- tb/v1_intc_progs.h, from the current core (the chains run past the horizon: 20 entries each)\n");
    for (auto &l : emitted) printf("%s\n", l.c_str());
    if (!only) {
      printf("static const int PH_SM[12] = {");
      for (int n = 0; n < 12; n++) printf("%s%d", n ? ", " : "", ph_sm[n]);
      printf("};\nstatic const int PH_FIRST = %d;\n", ph_first);
    }
  }
  (void)ph_ok;
  delete g_m;
  printf("v1-intc-sched: %d programs, %d stale or broken\n", runs, bad);
  printf("v1-intc-sched: %s\n", bad ? "FAIL" : "PASS");
  return bad ? 1 : 0;
}
