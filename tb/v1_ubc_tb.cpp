// v1_ubc_tb -- unit testbench of rtl/v1/v1_ubc.sv through tb/v1_ubc_tb_top.sv.  The C++ side plays the P4 bridge
// (Iclk strobes), the core (fetch words on ifq, the access in EX on opq, its data at S on dq, break points reached on
// hit) and the MMU's ASID.  The RTL runs in lock step with the independent C++ model dbg::Ubc (tb/sh4_dbg.h, cycle
// mode, the same fetch-side copy delay): in every cycle of every section all combinational outputs (ift, opm, d_mb,
// ctl, ctl_seq, ctl_pre_armed, pmc_ev) and every read strobe's rdata / hit are compared with it.
//
// Sections (each prints its counts; any unexpected mismatch fails the run, exit 1):
//   regs    p4probe U / S / W / H and ubcbrk A: read-back masks (BAMR 0F, BBR 7F, BRCR C4C9, BASR FF, 32-bit regs),
//           the size rule for reads and writes (every wrong-size write of p4probe U is ignored, every wrong-size read
//           is 0), holes, the ubcbrk A register line; power-on reset clears, manual / CPU-only reset holds
//   breaks  ubcbrk B / V (51 + 2 cases, the console's own test functions and arguments) through a mini core that
//           fetches every word of the function, runs its instructions in order, raises pre / post / operand / data
//           breaks from the UBC's tags as debug-ubc-hudi 4.3 says, and runs the DBR handler of ubcbrk_s.s (records
//           BRCR, BBRA = BBRB = 0, CMF cleared).  Checked against ubcbrk.out: breaks n, SPC offset, BRCR in the handler,
//           BRCR after, events 0x19 / 0x1A (evA / evB); bl1_pre with SR.BL = 1
//   kinds   the operand-kind mapping of the manual (20.3.1: PREF / OCB* / MOVCA.L size L, which have data, TAS.B) on the
//           RTL and in the model's functional oper(); ctl_pre_armed (the SOC.md 4.1 item 7 input of v1_fetch)
//   seqrace the SEQ state when a BRCR write and a hit share a cycle, with the expectations written out (not the model's):
//           a SEQ = 0 write cancels an A half taken in its own cycle [M 20.3.3 (4)], so a later SEQ = 1 write does not
//           re-arm channel B; a SEQ = 1 rewrite keeps it; a hardware CMFA set beats the write's CMFA bit [P]
//   model   dbg::Ubc used functionally (no clock(): Sh4Sys / run_iss): the default constructor and a delay given
//           without cycle mode both let a pre-execution break fire (writes reach the fetch-side copy at once)
//   settle  SOC.md 9.5 (closed): the arming store's MA3 strobe and every fetch at the cycles v1's rules give
//           (tb/v1_dbg_sched.h) for ubcbrk T (NOP 0-14, ADD 0-8), ubcevt S (ADD 9-24) and the operand settle (NOP
//           0-6).  With the calibrated IF_DLY = 3 and a fetch run-ahead of 5 or 6 halfwords every point matches; the
//           pre-SoC 4-halfword queue, kept as a reference, breaks ADD k = 9 and 10 one fetch too early.  (v1_fetch
//           holds 6 halfwords with its own room rule, SOC.md 4.1 item 5; `make v1-fetchx` replays these rows on the
//           real core with this RTL: 44 / 44.)  The operand points are the same whether the core samples the tag in
//           the first, any or the last EX cycle of the load (checked)
//   settle-dly  the same 44 points on the model alone for every copy delay 1-8 and run-ahead 4 / 5 / 6: IF_DLY = 3 is
//           the only delay that fits all 44 (at run-ahead 5 and 6); at the pre-SoC run-ahead 4 NO delay fits (3, 4
//           and 5 tie at 42 / 44)
//   settle-asid  M2-fix [HW tools/hw/mmu/ptehubc, warm]: a PTEH.ASID 1 -> 2 store (4 MA cycles, landing in MA4) k NOPs
//           before a pre-execution break with the ASID compared decides with the old ASID for k <= 12 and the new
//           from k = 13 (the ubcbrk T boundary of a BBRA store), before an operand-read break for k <= 2 / from k = 3
//           (ubcevt S's): the fetch side sees PTEH.ASID IF_DLY cycles after it lands (the copy, `fasid`), the EX side
//           at once.  Every cycle of every section feeds the model's fetch tags that delayed ASID
//   random  N cycles of random register traffic (all sizes, holes, mirrors), fetch / EX / S queries biased to hit the
//           programmed addresses, hits, run and resets, the ASID changing at random, against the model
// Options: --n <random cycles>, --seed <s>.
#include "Vv1_ubc_tb_top.h"
#include "verilated.h"
#include "sh4_dbg.h"
#include "v1_dbg_sched.h"
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

static Vv1_ubc_tb_top *d;
static dbg::Ubc M(3);
static long g_fail = 0, g_chk = 0;
static uint64_t g_cyc = 0;
static long g_match[7];                         // coverage: ift a / b, opm a / b, d_mb, events, pre_armed

static uint64_t g_rs = 0x13198A2E03707344ull;
static inline uint64_t rnd64() {
  uint64_t z = (g_rs += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}
static inline uint32_t rndn(uint32_t n) { return (uint32_t)((rnd64() >> 32) * n >> 32); }

static void chk(const char *what, uint64_t got, uint64_t exp) {
  g_chk++;
  if (got != exp) {
    g_fail++;
    if (g_fail <= 40)
      printf("FAIL cyc %llu %s: got %llx expected %llx\n", (unsigned long long)g_cyc, what, (unsigned long long)got,
             (unsigned long long)exp);
  }
}

// ---------------------------------------------------------------------------------------------------- one Icyc
enum { P_NONE, P_UBC, P_CCN };
struct In {
  int rst = 0;              // {pin, por, man, cpu}
  bool run = true;
  uint8_t asid = 0;
  int port = P_NONE; bool wr = false; int size = 2; uint32_t off = 0, wdata = 0;
  bool if_v = false; uint32_t if_va = 0;
  bool op_v = false, op_wr = false, op_nodata = false; int op_size = 2; uint32_t op_va = 0;
  bool d_v = false, d_wr = false; int d_size = 2; uint64_t d_data = 0;
  bool hit_a = false, hit_b = false;
};
struct Out {
  uint32_t rdata = 0; bool hit = false;
  unsigned ift_a = 0, ift_b = 0; bool opm_a = false, opm_b = false, d_mb = false;
  bool pcba, pcbb, ubde, dbeb, b_live, seq, pre_armed, ev_a, ev_b;
};

// PTEH.ASID as the fetch-side copy holds it: the ASID input of IF_DLY (3) cycles ago (a PTEH store reaches the copy
// like a UBC register write: v1_ubc fasid; a power-on reset clears it with PTEH)
static const int ASID_DLY = 3;
static uint8_t g_fasid[ASID_DLY];
static Out cycle(const In &in) {
  d->clk = 0;
  d->rst_i = in.rst; d->run = in.run; d->asid = in.asid;
  d->u_rd = in.port == P_UBC && !in.wr; d->u_wr = in.port == P_UBC && in.wr;
  d->c_rd = in.port == P_CCN && !in.wr; d->c_wr = in.port == P_CCN && in.wr;
  d->s_size = in.size; d->s_off = in.off; d->s_wdata = in.wdata;
  d->if_v = in.if_v; d->if_va = in.if_va;
  d->op_v = in.op_v; d->op_wr = in.op_wr; d->op_nodata = in.op_nodata; d->op_size = in.op_size; d->op_va = in.op_va;
  d->d_v = in.d_v; d->d_wr = in.d_wr; d->d_size = in.d_size; d->d_data = in.d_data;
  d->hit_a = in.hit_a; d->hit_b = in.hit_b;
  d->eval();
  Out o;
  o.ift_a = d->ift_a; o.ift_b = d->ift_b; o.opm_a = d->opm_a; o.opm_b = d->opm_b; o.d_mb = d->d_mb;
  o.pcba = d->pcba; o.pcbb = d->pcbb; o.ubde = d->ubde; o.dbeb = d->dbeb; o.b_live = d->b_live; o.seq = d->seq;
  o.pre_armed = d->pre_armed;
  o.ev_a = (d->pmc_ev >> 0x19) & 1; o.ev_b = (d->pmc_ev >> 0x1A) & 1;
  chk("stall", d->u_stall | d->c_stall, 0);
  chk("pmc_ev other bits", d->pmc_ev & ~((1ull << 0x19) | (1ull << 0x1A)), 0);
  if (in.port == P_NONE) chk("idle port", d->u_rdata | d->c_rdata | d->u_hit | d->c_hit, 0);
  if (!in.rst) {
    M.sync_copy();
    unsigned ma = 0, mb = 0;
    if (in.if_v) M.fetch_tags(in.if_va, g_fasid[ASID_DLY - 1], ma, mb);
    chk("ift.a", o.ift_a, ma); chk("ift.b", o.ift_b, mb);
    bool oa = in.op_v && M.op_match(0, in.op_va, in.op_size, in.op_wr, in.op_nodata, in.asid);
    bool ob = in.op_v && M.op_match(1, in.op_va, in.op_size, in.op_wr, in.op_nodata, in.asid);
    chk("opm.a", o.opm_a, oa); chk("opm.b", o.opm_b, ob);
    chk("d_mb", o.d_mb, in.d_v && M.d_match(in.d_data, in.d_size));
    chk("ctl.pcba", o.pcba, M.pcba()); chk("ctl.pcbb", o.pcbb, M.pcbb());
    chk("ctl.ubde", o.ubde, M.ubde()); chk("ctl.dbeb", o.dbeb, M.dbeb());
    chk("ctl.b_live", o.b_live, M.b_live()); chk("ctl_seq", o.seq, M.seq());
    chk("ctl_pre_armed", o.pre_armed, M.pre_armed());
    if (in.port != P_NONE) {
      o.rdata = in.port == P_UBC ? d->u_rdata : d->c_rdata;
      o.hit = in.port == P_UBC ? d->u_hit : d->c_hit;
      uint32_t mv = 0;
      bool mh = in.wr ? M.wr(in.off, in.size, in.wdata, false) : M.rd(in.off, in.size, mv, false);
      if (!in.wr) chk("rdata vs model", o.rdata, mv);
      chk("hit vs model", o.hit, mh);
    }
    g_match[0] += o.ift_a != 0; g_match[1] += o.ift_b != 0; g_match[2] += o.opm_a; g_match[3] += o.opm_b;
    g_match[4] += o.d_mb; g_match[5] += o.ev_a || o.ev_b; g_match[6] += o.pre_armed;
  }
  d->clk = 1; d->eval();
  d->clk = 0; d->eval();
  if (in.rst & 4) memset(g_fasid, 0, sizeof g_fasid);
  else { for (int k = ASID_DLY - 1; k > 0; k--) g_fasid[k] = g_fasid[k - 1]; g_fasid[0] = in.asid; }
  if (in.rst & 4) M.reset_por();
  else {
    bool ea = false, eb = false;
    M.clock(in.run, in.hit_a, in.hit_b, ea, eb);
    chk("event 0x19", o.ev_a, ea); chk("event 0x1A", o.ev_b, eb);
  }
  g_cyc++;
  return o;
}

// ---- bridge-side helpers (offsets a[27:0]: UBC 0xF200000 + x, CCN 0xF000000 + x)
static const uint32_t UB = 0xF200000, CC = 0xF000000;
enum { BARA = 0x00, BAMRA = 0x04, BBRA = 0x08, BARB = 0x0C, BAMRB = 0x10, BBRB = 0x14, BDRB = 0x18, BDMRB = 0x1C,
       BRCR = 0x20, BASRA = 0x14, BASRB = 0x18 };
static uint8_t g_asid = 0;
static void idle(int k) { In i; i.asid = g_asid; for (int n = 0; n < k; n++) cycle(i); }
static void wr(int port, uint32_t o, int size, uint32_t v) {
  In i; i.asid = g_asid; i.port = port; i.wr = true; i.size = size; i.off = (port == P_UBC ? UB : CC) + o; i.wdata = v;
  cycle(i);
}
static uint32_t rd(int port, uint32_t o, int size, bool *hit = nullptr) {
  In i; i.asid = g_asid; i.port = port; i.size = size; i.off = (port == P_UBC ? UB : CC) + o;
  Out r = cycle(i);
  if (hit) *hit = r.hit;
  return r.rdata;
}
static void reset(int kind) { In i; i.rst = kind; for (int n = 0; n < 8; n++) cycle(i); }

// ---------------------------------------------------------------------------------------------------- regs
static void sec_regs() {
  long f0 = g_fail, c0 = g_chk;
  reset(0xF);
  // U: 32-bit registers keep every bit
  for (int r : {BARA, BARB, BDRB, BDMRB})
    for (uint32_t v : {0xFFFFFFFFu, 0u, 0x5A5AA5A5u}) { wr(P_UBC, r, 2, v); chk("U 32-bit read-back", rd(P_UBC, r, 2), v); }
  // "BAMRA ff->0f 00->00 BAMRB ff->0f 00->00 BASRA ff->ff 00->00 BASRB ff->ff 00->00"
  for (int r : {BAMRA, BAMRB}) { wr(P_UBC, r, 0, 0xFF); chk("U BAMR ff", rd(P_UBC, r, 0), 0x0F);
                                 wr(P_UBC, r, 0, 0x00); chk("U BAMR 00", rd(P_UBC, r, 0), 0x00); }
  for (int r : {BASRA, BASRB}) { wr(P_CCN, r, 0, 0xFF); chk("U BASR ff", rd(P_CCN, r, 0), 0xFF);
                                 wr(P_CCN, r, 0, 0x00); chk("U BASR 00", rd(P_CCN, r, 0), 0x00); }
  // "BBRA: 0043->0043 003c->003c ff80->0000 0000->0000", BBRB the same
  for (int r : {BBRA, BBRB})
    for (auto p : std::vector<std::pair<uint32_t, uint32_t>>{{0x43, 0x43}, {0x3c, 0x3c}, {0xff80, 0}, {0, 0}}) {
      wr(P_UBC, r, 1, p.first); chk("U BBR read-back", rd(P_UBC, r, 1), p.second);
    }
  // "BRCR: ffff->c4c9 0000->0000 8000->8000 4000->4000 c000->c000 0000->0000"
  for (auto p : std::vector<std::pair<uint32_t, uint32_t>>{{0xffff, 0xc4c9}, {0, 0}, {0x8000, 0x8000}, {0x4000, 0x4000},
                                                            {0xc000, 0xc000}, {0, 0}}) {
    wr(P_UBC, BRCR, 1, p.first); chk("U BRCR read-back", rd(P_UBC, BRCR, 1), p.second);
  }
  // wrong-size writes: "BARA=11111111 w16 2222, w16@+2 3333, w8 44, w8@+3 55 -> 11111111"
  wr(P_UBC, BARA, 2, 0x11111111);
  wr(P_UBC, BARA, 1, 0x2222); wr(P_UBC, BARA + 2, 1, 0x3333); wr(P_UBC, BARA, 0, 0x44); wr(P_UBC, BARA + 3, 0, 0x55);
  chk("U wsize BARA", rd(P_UBC, BARA, 2), 0x11111111);
  // "BAMRA w16 0005 -> 00, w32 00000006 -> 00 | BBRA w32 00000014 -> 0000, w8 24 -> 0000"
  wr(P_UBC, BAMRA, 0, 0); wr(P_UBC, BAMRA, 1, 5); wr(P_UBC, BAMRA, 2, 6); chk("U wsize BAMRA", rd(P_UBC, BAMRA, 0), 0);
  wr(P_UBC, BBRA, 1, 0); wr(P_UBC, BBRA, 2, 0x14); wr(P_UBC, BBRA, 0, 0x24); chk("U wsize BBRA", rd(P_UBC, BBRA, 1), 0);
  // "BRCR w32 00000400 -> 0000, w8 08 -> 0000, w8@+1 04 -> 0000 | BASRA w32 000000aa -> 00, w16 00bb -> 00"
  wr(P_UBC, BRCR, 1, 0); wr(P_UBC, BRCR, 2, 0x400); wr(P_UBC, BRCR, 0, 0x08); wr(P_UBC, BRCR + 1, 0, 0x04);
  chk("U wsize BRCR", rd(P_UBC, BRCR, 1), 0);
  wr(P_CCN, BASRA, 0, 0); wr(P_CCN, BASRA, 2, 0xaa); wr(P_CCN, BASRA, 1, 0xbb); chk("U wsize BASRA", rd(P_CCN, BASRA, 0), 0);
  // wrong-size reads: "BARA=5a5aa5a5 r16 0000 r16+2 0000 r8 00 00 00 00"
  wr(P_UBC, BARA, 2, 0x5a5aa5a5);
  chk("U rsize BARA r16", rd(P_UBC, BARA, 1), 0); chk("U rsize BARA r16+2", rd(P_UBC, BARA + 2, 1), 0);
  for (int b = 0; b < 4; b++) chk("U rsize BARA r8", rd(P_UBC, BARA + b, 0), 0);
  // "BBRA=0043 r32 00000000 r8 00 r8+1 00 r16+2 0000 | BAMRA=0d r16 0000 r32 00000000 | BASRA=a5 r16 0000 r32 00000000
  //  | BRCR=0408 r32 00000000 r8 00 r8+1 00"
  wr(P_UBC, BBRA, 1, 0x43); wr(P_UBC, BAMRA, 0, 0x0d); wr(P_CCN, BASRA, 0, 0xa5); wr(P_UBC, BRCR, 1, 0x0408);
  chk("U rsize BBRA r32", rd(P_UBC, BBRA, 2), 0); chk("U rsize BBRA r8", rd(P_UBC, BBRA, 0), 0);
  chk("U rsize BBRA r8+1", rd(P_UBC, BBRA + 1, 0), 0); chk("U rsize BBRA r16+2", rd(P_UBC, BBRA + 2, 1), 0);
  chk("U rsize BAMRA r16", rd(P_UBC, BAMRA, 1), 0); chk("U rsize BAMRA r32", rd(P_UBC, BAMRA, 2), 0);
  chk("U rsize BASRA r16", rd(P_CCN, BASRA, 1), 0); chk("U rsize BASRA r32", rd(P_CCN, BASRA, 2), 0);
  chk("U rsize BRCR r32", rd(P_UBC, BRCR, 2), 0); chk("U rsize BRCR r8", rd(P_UBC, BRCR, 0), 0);
  chk("U rsize BRCR r8+1", rd(P_UBC, BRCR + 1, 0), 0);
  chk("U BRCR 0408 kept", rd(P_UBC, BRCR, 1), 0x0408);
  // W / H: the KOS state (zeros, BDRB DBFD4777, BDMRB DFFFDBDF) read as a 32-bit sweep; holes 0x24-0xFC at every size
  wr(P_UBC, BARA, 2, 0); wr(P_UBC, BAMRA, 0, 0); wr(P_UBC, BBRA, 1, 0); wr(P_UBC, BARB, 2, 0); wr(P_UBC, BAMRB, 0, 0);
  wr(P_UBC, BBRB, 1, 0); wr(P_UBC, BDRB, 2, 0xdbfd4777); wr(P_UBC, BDMRB, 2, 0xdfffdbdf); wr(P_UBC, BRCR, 1, 0);
  static const uint32_t wsweep[16] = {0, 0, 0, 0, 0, 0, 0xdbfd4777, 0xdfffdbdf, 0, 0, 0, 0, 0, 0, 0, 0};
  for (int k = 0; k < 16; k++) chk("W ff200000 sweep", rd(P_UBC, 4 * k, 2), wsweep[k]);
  for (uint32_t o = 0x24; o < 0x100; o += 4) {
    bool h;
    for (int s = 0; s < 3; s++) { chk("H UBC hole", rd(P_UBC, o, s, &h), 0); chk("H UBC hole hit", h, 0); }
  }
  // ubcbrk A: "UBC regs 11111111 02 0000 44444444 05 0000 77777777 88888888 0008 (BASRA 33 BASRB 66)"
  wr(P_UBC, BARA, 2, 0x11111111); wr(P_UBC, BAMRA, 0, 0x02); wr(P_CCN, BASRA, 0, 0x33);
  wr(P_UBC, BARB, 2, 0x44444444); wr(P_UBC, BAMRB, 0, 0x05); wr(P_CCN, BASRB, 0, 0x66);
  wr(P_UBC, BDRB, 2, 0x77777777); wr(P_UBC, BDMRB, 2, 0x88888888); wr(P_UBC, BRCR, 1, 0x0008);
  const uint32_t exp[] = {0x11111111, 0x02, 0, 0x44444444, 0x05, 0, 0x77777777, 0x88888888, 0x0008};
  const int reg[] = {BARA, BAMRA, BBRA, BARB, BAMRB, BBRB, BDRB, BDMRB, BRCR};
  const int sz[] = {2, 0, 1, 2, 0, 1, 2, 2, 1};
  for (int k = 0; k < 9; k++) chk("A UBC regs", rd(P_UBC, reg[k], sz[k]), exp[k]);
  chk("A BASRA", rd(P_CCN, BASRA, 0), 0x33); chk("A BASRB", rd(P_CCN, BASRB, 0), 0x66);
  // resets: manual and CPU-only hold every register; power-on clears every one
  reset(0x3); reset(0x1);
  for (int k = 0; k < 9; k++) chk("manual / CPU reset holds", rd(P_UBC, reg[k], sz[k]), exp[k]);
  reset(0x5);
  for (int k = 0; k < 9; k++) chk("power-on reset clears", rd(P_UBC, reg[k], sz[k]), 0);
  chk("power-on reset clears BASRA", rd(P_CCN, BASRA, 0), 0);
  printf("regs: %ld checks, %ld failures\n", g_chk - c0, g_fail - f0);
}

// ---------------------------------------------------------------------------------------------------- breaks
// The console's test functions (tools/hw/debug-ubc-hudi/ubcbrk_s.s), instruction by instruction.
enum Kind { K_PLAIN, K_BR_T, K_BR_NT, K_SKIP0, K_BL1, K_BL0, K_LD, K_ST, K_LDSP, K_STSP, K_RTS };
struct FIns { int off; Kind k; int tgt = 0; int size = 2; bool pairs_next = true; };
struct Func { const char *name; uint32_t base; int len; std::vector<FIns> ins; };
static const uint32_t SP = 0x8CFFFE28, BUF = 0x8C0A0400;
static std::map<uint32_t, uint32_t> mem;

static Func F_lin{"t_lin", 0x8C011000, 24, {{0, K_STSP}, {2, K_PLAIN}, {4, K_PLAIN}, {6, K_PLAIN}, {8, K_PLAIN},
  {10, K_PLAIN}, {12, K_PLAIN}, {14, K_PLAIN}, {16, K_PLAIN}, {18, K_PLAIN}, {20, K_RTS}, {22, K_LDSP}}};
static Func F_slot{"t_slot", 0x8C011020, 20, {{0, K_STSP}, {2, K_PLAIN}, {4, K_BR_T, 12}, {6, K_PLAIN}, {8, K_PLAIN},
  {10, K_PLAIN}, {12, K_PLAIN}, {14, K_PLAIN}, {16, K_RTS}, {18, K_LDSP}}};
static Func F_bfs{"t_bfs", 0x8C011040, 20, {{0, K_STSP}, {2, K_PLAIN}, {4, K_PLAIN}, {6, K_BR_NT}, {8, K_PLAIN},
  {10, K_PLAIN}, {12, K_PLAIN}, {14, K_PLAIN}, {16, K_RTS}, {18, K_LDSP}}};
static Func F_seq{"t_seq", 0x8C011060, 26, {{0, K_STSP}, {2, K_PLAIN}, {4, K_PLAIN}, {6, K_SKIP0}, {8, K_PLAIN},
  {10, K_PLAIN}, {12, K_PLAIN}, {14, K_PLAIN}, {16, K_PLAIN}, {18, K_PLAIN}, {20, K_PLAIN}, {22, K_RTS}, {24, K_LDSP}}};
static Func F_ld{"t_ld", 0x8C011080, 28, {{0, K_STSP}, {2, K_PLAIN}, {4, K_LD, 0, 2}, {6, K_PLAIN}, {8, K_PLAIN},
  {10, K_PLAIN}, {12, K_PLAIN}, {14, K_PLAIN}, {16, K_PLAIN}, {18, K_PLAIN}, {20, K_PLAIN}, {22, K_PLAIN}, {24, K_RTS},
  {26, K_LDSP}}};
static Func F_ldw{"t_ldw", 0x8C0110A0, 28, {{0, K_STSP}, {2, K_PLAIN}, {4, K_LD, 0, 1}, {6, K_PLAIN}, {8, K_PLAIN},
  {10, K_PLAIN}, {12, K_PLAIN}, {14, K_PLAIN}, {16, K_PLAIN}, {18, K_PLAIN}, {20, K_PLAIN}, {22, K_PLAIN}, {24, K_RTS},
  {26, K_LDSP}}};
static Func F_st{"t_st", 0x8C0110C0, 28, {{0, K_STSP}, {2, K_PLAIN}, {4, K_ST, 0, 2}, {6, K_PLAIN}, {8, K_PLAIN},
  {10, K_PLAIN}, {12, K_PLAIN}, {14, K_PLAIN}, {16, K_PLAIN}, {18, K_PLAIN}, {20, K_PLAIN}, {22, K_PLAIN}, {24, K_RTS},
  {26, K_LDSP}}};
static Func F_ldu{"t_ldu", 0x8C0110E0, 28, {{0, K_STSP}, {2, K_PLAIN}, {4, K_LD, 0, 2, false}, {6, K_PLAIN},
  {8, K_PLAIN}, {10, K_PLAIN}, {12, K_PLAIN}, {14, K_PLAIN}, {16, K_PLAIN}, {18, K_PLAIN}, {20, K_PLAIN}, {22, K_PLAIN},
  {24, K_RTS}, {26, K_LDSP}}};
static Func F_bl{"t_bl", 0x8C011100, 28, {{0, K_STSP}, {2, K_PLAIN}, {4, K_PLAIN}, {6, K_BL1}, {8, K_PLAIN},
  {10, K_PLAIN}, {12, K_PLAIN}, {14, K_PLAIN}, {16, K_PLAIN}, {18, K_BL0}, {20, K_PLAIN}, {22, K_PLAIN}, {24, K_RTS},
  {26, K_LDSP}}};
static Func F_mask{"t_mask", 0x8C011400, 274, {{0, K_STSP}, {2, K_PLAIN}, {4, K_PLAIN}, {6, K_BR_T, 266}, {8, K_PLAIN},
  {266, K_PLAIN}, {268, K_PLAIN}, {270, K_RTS}, {272, K_LDSP}}};

struct Rec { uint32_t count = 0, spc = 0, brcr = 0; };

// Run fn(a0, a1) on the mini core.  bl: SR.BL state at entry.  Returns the handler record.
static void run_func(const Func &f, uint32_t a0, uint32_t a1, Rec &rec, long ev[2]) {
  // fetch every word of the function first (the fetch runs ahead of issue; tags ride the words, the unexecuted ones are
  // dropped with the queue)
  std::map<int, std::pair<bool, bool>> tag;          // halfword offset -> {A, B}
  for (int w = 0; w < (f.len + 3) / 4; w++) {
    In i; i.asid = g_asid; i.if_v = true; i.if_va = f.base + 4 * w;
    Out o = cycle(i);
    for (int h = 0; h < 2; h++) tag[4 * w + 2 * h] = {(o.ift_a >> h) & 1, (o.ift_b >> h) & 1};
  }
  bool armed = true, bl = false;
  int post_pend = 0;                                  // 1 A / 2 B: a post break waits for the delay slot
  uint32_t post_spc = 0;
  auto idx = [&](int off) { for (size_t k = 0; k < f.ins.size(); k++) if (f.ins[k].off == off) return (int)k; return -1; };
  auto hit = [&](bool a, bool b) {
    In i; i.asid = g_asid; i.hit_a = a; i.hit_b = b;
    Out o = cycle(i);
    ev[0] += o.ev_a; ev[1] += o.ev_b;
  };
  auto handler = [&](uint32_t spc) {                  // _ubc_dbr_handler (and KOS's vbr_hnd: the same UBC accesses)
    idle(3);
    rec.count++; rec.spc = spc;
    rec.brcr = rd(P_UBC, BRCR, 1);
    wr(P_UBC, BBRA, 1, 0); wr(P_UBC, BBRB, 1, 0);
    uint32_t b = rd(P_UBC, BRCR, 1);
    wr(P_UBC, BRCR, 1, b & 0x3FFF);
    armed = false;
    idle(6);
  };
  int i = 0, slot_of = -1;                            // slot_of: index of the branch whose slot i is
  int pending_jump = -1;
  bool rts_seen = false;
  while (i >= 0 && i < (int)f.ins.size()) {
    const FIns &I = f.ins[i];
    uint32_t pc = f.base + I.off;
    bool is_slot = slot_of >= 0;
    // ---- pre-execution check (D stage)
    if (armed && !bl) {
      Out c = cycle(In());
      bool ta = tag[I.off].first, tb = tag[I.off].second;
      bool pre_a = ta && !c.pcba, pre_b = tb && !c.pcbb;
      if (pre_a || pre_b) {
        bool brk = (pre_a && !c.seq) || (pre_b && c.b_live);
        hit(pre_a, pre_b);
        if (brk) { handler(is_slot ? f.base + f.ins[slot_of].off : pc); continue; }   // re-executes, unarmed
      }
    } else if (armed && bl) {                          // BL = 1: the break is masked, the flag is set [HW bl1_pre]
      Out c = cycle(In());
      bool ta = tag[I.off].first, tb = tag[I.off].second;
      if ((ta && !c.pcba) || (tb && !c.pcbb)) hit(ta && !c.pcba, tb && !c.pcbb);
    }
    // ---- execute: operand accesses (EX compare, then the S-stage data compare)
    bool op_a = false, op_b = false, dm = false, dbeb = false;
    if (I.k == K_LD || I.k == K_ST || I.k == K_LDSP || I.k == K_STSP) {
      bool w = I.k == K_ST || I.k == K_STSP;
      uint32_t va = (I.k == K_LD || I.k == K_ST) ? a0 : SP - 4;
      uint64_t data = w ? (I.k == K_ST ? a1 : 0x8C0DEAD0) : mem[va & ~3u];
      if (I.size == 1 && !w) data = (va & 2) ? data >> 16 : data & 0xFFFF;
      In e; e.asid = g_asid; e.op_v = true; e.op_wr = w; e.op_size = I.size; e.op_va = va;
      Out oe = cycle(e);
      op_a = oe.opm_a; op_b = oe.opm_b;
      In s; s.asid = g_asid; s.d_v = true; s.d_wr = w; s.d_size = I.size; s.d_data = data;
      Out os = cycle(s);
      dm = os.d_mb; dbeb = os.dbeb;
      if (w) mem[va & ~3u] = (uint32_t)data;
    }
    if (I.k == K_BL1) bl = true;
    if (I.k == K_BL0) bl = false;
    if (armed && (op_a || op_b)) {
      Out c = cycle(In());
      bool b_ok = op_b && (!dbeb || dm);
      bool brk = (op_a && !c.seq) || (b_ok && c.b_live);
      if (op_a || b_ok) hit(op_a, b_ok);
      if (brk && !bl) {
        // address break: SPC = access + 2; data break (B with DBEB): the access's whole issue group completes
        uint32_t spc = (b_ok && dbeb && !(op_a && !c.seq)) ? pc + (I.pairs_next ? 4 : 2) : pc + 2;
        handler(spc);
        i = idx((int)(spc - f.base));
        slot_of = -1;
        continue;
      }
    }
    // ---- post-execution check
    int next = i + 1;
    if (I.k == K_BR_T) pending_jump = idx(I.tgt);
    if (I.k == K_SKIP0 && a0 == 0) next = i + 2;                       // BT disp 0 taken: nullifies the next one
    if (I.k == K_RTS) rts_seen = true;
    if (armed && !bl) {
      Out c = cycle(In());
      bool ta = tag[I.off].first, tb = tag[I.off].second;
      bool po_a = ta && c.pcba, po_b = tb && c.pcbb;
      if (po_a || po_b) {
        bool delayed = I.k == K_BR_T || I.k == K_BR_NT || I.k == K_RTS;
        if (delayed) { post_pend = (po_a ? 1 : 0) | (po_b ? 2 : 0); }
        else {
          bool brk = (po_a && !c.seq) || (po_b && c.b_live);
          hit(po_a, po_b);
          if (brk || post_pend) {
            uint32_t spc = is_slot ? (pending_jump >= 0 ? f.base + f.ins[pending_jump].off : pc + 2) : pc + 2;
            handler(spc);
            post_pend = 0;
            if (is_slot) { i = pending_jump >= 0 ? pending_jump : next; slot_of = -1; pending_jump = -1; continue; }
            i = next; continue;
          }
        }
      } else if (is_slot && post_pend) {
        Out c2 = cycle(In());
        bool brk = ((post_pend & 1) && !c2.seq) || ((post_pend & 2) && c2.b_live);
        hit(post_pend & 1, (post_pend & 2) != 0);
        post_pend = 0;
        if (brk) {
          handler(pending_jump >= 0 ? f.base + f.ins[pending_jump].off : pc + 2);
          i = pending_jump >= 0 ? pending_jump : next; slot_of = -1; pending_jump = -1; continue;
        }
      }
    }
    // ---- control flow
    if (is_slot) {
      slot_of = -1;
      if (rts_seen) break;
      if (pending_jump >= 0) { i = pending_jump; pending_jump = -1; continue; }
    }
    if (I.k == K_BR_T || I.k == K_BR_NT || I.k == K_RTS) slot_of = i;
    i = next;
  }
}

struct Ucfg {
  uint32_t bara = 0; uint8_t bamra = 0, basra = 0; uint16_t bbra = 0;
  uint32_t barb = 0; uint8_t bamrb = 0, basrb = 0; uint16_t bbrb = 0;
  uint32_t bdrb = 0, bdmrb = 0; uint16_t brcr = 0;
};
static void ubc_arm(const Ucfg &u) {
  wr(P_UBC, BBRA, 1, 0); wr(P_UBC, BBRB, 1, 0);
  wr(P_UBC, BARA, 2, u.bara); wr(P_UBC, BAMRA, 0, u.bamra); wr(P_CCN, BASRA, 0, u.basra);
  wr(P_UBC, BARB, 2, u.barb); wr(P_UBC, BAMRB, 0, u.bamrb); wr(P_CCN, BASRB, 0, u.basrb);
  wr(P_UBC, BDRB, 2, u.bdrb); wr(P_UBC, BDMRB, 2, u.bdmrb);
  wr(P_UBC, BRCR, 1, u.brcr);
  wr(P_UBC, BBRA, 1, u.bbra); wr(P_UBC, BBRB, 1, u.bbrb);
  idle(16);                                            // ubc_wait
}

struct Case {
  const char *name; Ucfg u; const Func *f; uint32_t a0, a1; bool two; uint32_t a2;
  // console (ubcbrk.out)
  int n; int spc; uint32_t brcr_h, after; int eva, evb; bool spc_core;   // spc_core: the offset is the core's pairing
};

static void sec_breaks() {
  long f0 = g_fail, c0 = g_chk;
  reset(0xF);
  g_asid = 0x00;                                       // PTEH 8c063400 under KOS
  const uint16_t U = 0x0001, SEQ = 0x0008, PCBB = 0x0040, DBEB = 0x0080, PCBA = 0x0400;
  const uint32_t lin_a = F_lin.base + 6, lin_b = F_lin.base + 8;
  std::vector<Case> cs;
  auto C = [&](const char *n, Ucfg u, const Func *f, uint32_t a0, uint32_t a1, int nn, int spc, uint32_t bh,
               uint32_t af, int ea, int eb, bool two = false, uint32_t a2 = 0, bool core = false) {
    cs.push_back({n, u, f, a0, a1, two, a2, nn, spc, bh, af, ea, eb, core});
  };
  Ucfg u;
  u = Ucfg(); u.bara = lin_b; u.bamra = 0x04; u.bbra = 0x0014; u.brcr = U;
  C("pcA_pre", u, &F_lin, 0, 0, 1, 8, 0x8001, 0x0001, 1, 0);
  u.brcr = U | PCBA;             C("pcA_post", u, &F_lin, 0, 0, 1, 10, 0x8401, 0x0401, 1, 0);
  u.brcr = U; u.bbra = 0x0016;   C("pcA_pre_szW", u, &F_lin, 0, 0, 1, 8, 0x8001, 0x0001, 1, 0);
  u.bbra = 0x0017;               C("pcA_pre_szL", u, &F_lin, 0, 0, 0, -1, 0, 0x0001, 0, 0);
  u.bbra = 0x0054;               C("pcA_pre_szQ", u, &F_lin, 0, 0, 0, -1, 0, 0x0001, 0, 0);
  u.bbra = 0x0018;               C("pcA_pre_W", u, &F_lin, 0, 0, 0, -1, 0, 0x0001, 0, 0);
  u.bbra = 0x001C;               C("pcA_pre_RW", u, &F_lin, 0, 0, 1, 8, 0x8001, 0x0001, 1, 0);
  u.bbra = 0x0034;               C("pcA_pre_IDboth", u, &F_lin, 0, 0, 1, 8, 0x8001, 0x0001, 1, 0);
  u.bbra = 0x0024;               C("pcA_asop", u, &F_lin, 0, 0, 0, -1, 0, 0x0001, 0, 0);
  u.bbra = 0x0014; u.bara = lin_b + 1;         C("pcA_odd", u, &F_lin, 0, 0, 0, -1, 0, 0x0001, 0, 0);
  u.bara = lin_b | 0x20000000;                 C("pcA_P1vsA1", u, &F_lin, 0, 0, 0, -1, 0, 0x0001, 0, 0);
  u = Ucfg(); u.barb = lin_b; u.bamrb = 0x04; u.bbrb = 0x0014; u.brcr = U;
  C("pcB_pre", u, &F_lin, 0, 0, 1, 8, 0x4001, 0x0001, 0, 1);
  u.brcr = U | PCBB;             C("pcB_post", u, &F_lin, 0, 0, 1, 10, 0x4041, 0x0041, 0, 1);
  u.brcr = U | PCBB | DBEB; u.bdrb = 0x12345678; C("pcB_post_dbeb", u, &F_lin, 0, 0, 1, 10, 0x40C1, 0x00C1, 0, 1);
  u = Ucfg(); u.bara = lin_a; u.bamra = 0x04; u.bbra = 0x0014; u.barb = lin_b; u.bamrb = 0x04; u.bbrb = 0x0014;
  u.brcr = U | PCBA;             C("AB_sameSPC", u, &F_lin, 0, 0, 1, 8, 0x8401, 0x0401, 1, 0);
  u = Ucfg(); u.bamra = 0x04; u.bbra = 0x0014; u.brcr = U;
  u.bara = F_slot.base + 6;                    C("slot_ds_pre", u, &F_slot, 0, 0, 1, 4, 0x8001, 0x0001, 1, 0);
  u.brcr = U | PCBA;                           C("slot_ds_post", u, &F_slot, 0, 0, 1, 12, 0x8401, 0x0401, 1, 0);
  u.bara = F_slot.base + 4;                    C("slot_br_post", u, &F_slot, 0, 0, 1, 12, 0x8401, 0x0401, 1, 0);
  u.brcr = U;                                  C("slot_br_pre", u, &F_slot, 0, 0, 1, 4, 0x8001, 0x0001, 1, 0);
  u.bara = F_slot.base + 8;                    C("slot_overrun", u, &F_slot, 0, 0, 0, -1, 0, 0x0001, 0, 0);
  u.bara = F_slot.base + 12;                   C("slot_target", u, &F_slot, 0, 0, 1, 12, 0x8001, 0x0001, 1, 0);
  u.bara = F_bfs.base + 6; u.brcr = U | PCBA;  C("bfsnt_br_post", u, &F_bfs, 0, 0, 1, 10, 0x8401, 0x0401, 1, 0);
  u.bara = F_bfs.base + 8; u.brcr = U;         C("bfsnt_ds_pre", u, &F_bfs, 0, 0, 1, 6, 0x8001, 0x0001, 1, 0);
  u.brcr = U | PCBA;                           C("bfsnt_ds_post", u, &F_bfs, 0, 0, 1, 10, 0x8401, 0x0401, 1, 0);
  u = Ucfg(); u.bara = F_seq.base + 8; u.bamra = 0x04; u.bbra = 0x0014; u.barb = F_seq.base + 16; u.bamrb = 0x04;
  u.bbrb = 0x0014; u.brcr = U | SEQ;
  C("seq_Bonly_thenAB", u, &F_seq, 0, 0, 1, 16, 0x4009, 0x0009, 1, 1, true, 1);
  C("seq_AB", u, &F_seq, 1, 0, 1, 16, 0x4009, 0x0009, 1, 1);
  u.brcr = U;                                  C("indep_AB", u, &F_seq, 1, 0, 1, 8, 0x8001, 0x0001, 1, 0);
  u = Ucfg(); u.bara = F_mask.base + 266; u.bamra = 0x05; u.bbra = 0x0014; u.brcr = U;
  C("mask10_pre", u, &F_mask, 0, 0, 1, 0, 0x8001, 0x0001, 1, 0);
  u.bamra = 0x04;                              C("mask0_mid", u, &F_mask, 0, 0, 1, 266, 0x8001, 0x0001, 1, 0);
  u = Ucfg(); u.bara = lin_b; u.bamra = 0x00; u.basra = g_asid; u.bbra = 0x0014; u.brcr = U;
  C("asid_match", u, &F_lin, 0, 0, 1, 8, 0x8001, 0x0001, 1, 0);
  u.basra = g_asid ^ 0x01;                     C("asid_mismatch", u, &F_lin, 0, 0, 0, -1, 0, 0x0001, 0, 0);
  u.basra = g_asid ^ 0x80;                     C("asid_mism80", u, &F_lin, 0, 0, 0, -1, 0, 0x0001, 0, 0);
  u = Ucfg(); u.bara = BUF; u.bamra = 0x04; u.bbra = 0x0024; u.brcr = U;
  C("opA_rd", u, &F_ld, BUF, 0, 1, 6, 0x8001, 0x0001, 1, 0);
  u.bbra = 0x0027;                             C("opA_rd_L", u, &F_ld, BUF, 0, 1, 6, 0x8001, 0x0001, 1, 0);
  u.bbra = 0x0026;                             C("opA_rd_W_onL", u, &F_ld, BUF, 0, 0, -1, 0, 0x0001, 0, 0);
  u.bbra = 0x0026;                             C("opA_rd_W_onW", u, &F_ldw, BUF, 0, 1, 6, 0x8001, 0x0001, 1, 0);
  u.bbra = 0x0024; u.bara = BUF + 2;           C("opA_rd_anysz+2", u, &F_ld, BUF, 0, 1, 6, 0x8001, 0x0001, 1, 0);
  u.bbra = 0x0027; u.bara = BUF + 2;           C("opA_rd_L+2", u, &F_ld, BUF, 0, 1, 6, 0x8001, 0x0001, 1, 0);
  u.bara = BUF; u.bbra = 0x0028;               C("opA_wr", u, &F_st, BUF, 0x55, 1, 6, 0x8001, 0x0001, 1, 0);
  u.bbra = 0x0024;                             C("opA_rd_onwr", u, &F_st, BUF, 0xA5A5A5A5, 0, -1, 0, 0x0001, 0, 0);
  u.bbra = 0x0014;                             C("opA_ifetch_onld", u, &F_ld, BUF, 0, 0, -1, 0, 0x0001, 0, 0);
  u.bara = BUF + 0x10; u.bamra = 0x05; u.bbra = 0x0024;
  C("opA_mask10", u, &F_ld, BUF, 0, 1, 6, 0x8001, 0x0001, 1, 0);
  u.bara = BUF | 0x20000000; u.bamra = 0x04;   C("opA_P1vsA1", u, &F_ld, BUF, 0, 0, -1, 0, 0x0001, 0, 0);
  u.bara = BUF; u.bbra = 0x0024;               C("opA_rd_use", u, &F_ldu, BUF, 0, 1, 6, 0x8001, 0x0001, 1, 0);
  u = Ucfg(); u.barb = BUF; u.bamrb = 0x04; u.bbrb = 0x002B; u.bdrb = 0xA5A5A5A5; u.bdmrb = 0; u.brcr = U | DBEB;
  C("opB_wr_data", u, &F_st, BUF, 0xA5A5A5A5, 1, 8, 0x4081, 0x0081, 0, 1, false, 0, true);
  C("opB_wr_nodata", u, &F_st, BUF, 0x5A5A5A5A, 0, -1, 0, 0x0081, 0, 0);
  u.bdrb = 0x0000A5A5; u.bdmrb = 0xFFFF0000;
  C("opB_wr_masked", u, &F_st, BUF, 0x1234A5A5, 1, 8, 0x4081, 0x0081, 0, 1, false, 0, true);
  u.bdrb = 0xA5A5A5A5; u.bdmrb = 0; u.bbrb = 0x0027;
  C("opB_rd_data", u, &F_ld, BUF, 0, 1, 8, 0x4081, 0x0081, 0, 1, false, 0, true);
  C("opB_rd_data_use", u, &F_ldu, BUF, 0, 1, 6, 0x4081, 0x0081, 0, 1, false, 0, true);
  u.brcr = U;                                  C("opB_rd_nodbeb", u, &F_ld, BUF, 0, 1, 6, 0x4001, 0x0001, 0, 1);
  // V: the VBR path (UBDE = 0, KOS's handler does the same UBC accesses)
  u = Ucfg(); u.bara = lin_b; u.bamra = 0x04; u.bbra = 0x0014; u.brcr = 0;
  C("V vbr_pre", u, &F_lin, 0, 0, 1, 8, 0x8000, 0x0000, 1, 0);
  u.brcr = PCBA;                               C("V vbr_post", u, &F_lin, 0, 0, 1, 10, 0x8400, 0x0400, 1, 0);
  // BL = 1: masked, flag only
  u = Ucfg(); u.bara = F_bl.base + 14; u.bamra = 0x04; u.bbra = 0x0014; u.brcr = U;
  C("bl1_pre", u, &F_bl, 0, 0, 0, -1, 0, 0x8001, 1, 0);

  int nok = 0;
  for (auto &c : cs) {
    // memory the functions see (buf[0] = A5A5A5A5 before the operand groups, as ubcbrk.c sets it)
    if (!strncmp(c.name, "opA_rd", 6) && !strcmp(c.name, "opA_rd")) mem[BUF] = 0xA5A5A5A5;
    if (!strcmp(c.name, "opB_wr_data") || !strcmp(c.name, "opB_rd_data")) mem[BUF] = 0xA5A5A5A5;
    Rec rec;
    long ev[2] = {0, 0};
    ubc_arm(c.u);
    run_func(*c.f, c.a0, c.a1, rec, ev);
    uint32_t cnt1 = rec.count;
    if (c.two) run_func(*c.f, c.a2, c.a1, rec, ev);
    wr(P_UBC, BBRA, 1, 0); wr(P_UBC, BBRB, 1, 0); idle(16);           // ubc_off
    uint32_t after = rd(P_UBC, BRCR, 1);
    wr(P_UBC, BRCR, 1, 0); idle(16);
    int spc = rec.count ? (int)(rec.spc - c.f->base) : -1;
    bool ok = (int)rec.count == c.n && rec.brcr == c.brcr_h && after == c.after && ev[0] == c.eva && ev[1] == c.evb &&
              spc == c.spc && (!c.two || cnt1 == 0);
    nok += ok;
    char sp[40];
    if (rec.count) snprintf(sp, sizeof sp, "%s+%d", c.f->name, spc); else snprintf(sp, sizeof sp, "-");
    printf("breaks: %-18s n=%u spc=%-11s brcr@h=%04x after=%04x evA=%ld evB=%ld  %s%s\n", c.name, rec.count, sp,
           rec.brcr, after, ev[0], ev[1], ok ? "ok" : "MISMATCH vs console", c.spc_core ? " (SPC: core pairing)" : "");
    chk("break count vs console", rec.count, c.n);
    chk("BRCR in the handler vs console", rec.brcr, c.brcr_h);
    chk("BRCR after vs console", after, c.after);
    chk("evA vs console", ev[0], c.eva);
    chk("evB vs console", ev[1], c.evb);
    chk("SPC offset vs console", (uint32_t)spc, (uint32_t)c.spc);
    if (c.two) chk("first call breaks", cnt1, 0);
  }
  printf("breaks: %d / %zu cases as on the console; %ld checks, %ld failures\n", nok, cs.size(), g_chk - c0,
         g_fail - f0);
}

// ---------------------------------------------------------------------------------------------------- kinds
// The operand-access mapping the core presents on opq [M 20.3.1, 20.3.5 (3)] (v1_ubc.sv header): PREF / OCBP / OCBWB
// read, OCBI write, all size L with no data; MOVCA.L a size-L write with data (R0); TAS.B its read and write (byte),
// the purge nothing.  Each case drives the RTL's EX and S ports with the mapped fields (lock-step compared with the
// model in cycle()) and checks the model's functional oper() (the ISS path: sizes as the ISS passes them) against the
// expected break.  Also ctl_pre_armed for instruction / operand / post configurations.
static void sec_kinds() {
  long f0 = g_fail, c0 = g_chk;
  reset(0xF);
  g_asid = 0;
  struct K { const char *n; int kind, iss_size, op_size; bool wr, nodata; uint64_t data; uint16_t bbrb; uint16_t brcr;
             int exp; };
  const uint16_t DBEB = 0x0080, U = 0x0001;
  // BBRB: ID = operand (10), RW = 11 either / 01 read / 10 write, SZ = 000 any / 011 L / 100 Q / 001 B
  const std::vector<K> ks = {
    {"PREF any-size",          3, 0, 2, false, true,  0,           0x002C, U,        1},
    {"PREF SZ=L",              3, 0, 2, false, true,  0,           0x002F, U,        1},
    {"PREF SZ=Q (no)",         3, 0, 2, false, true,  0,           0x006C, U,        0},
    {"PREF RW=write (no)",     3, 0, 2, false, true,  0,           0x0028, U,        0},
    {"PREF with DBEB (no)",    3, 0, 2, false, true,  0,           0x002C, U | DBEB, 0},
    {"OCBP read",              5, 0, 2, false, true,  0,           0x0024, U,        1},
    {"OCBWB read",             6, 0, 2, false, true,  0,           0x0024, U,        1},
    {"OCBI RW=read (no)",      4, 0, 2, true,  true,  0,           0x0024, U,        0},
    {"OCBI RW=write",          4, 0, 2, true,  true,  0,           0x0028, U,        1},
    {"OCBI with DBEB (no)",    4, 0, 2, true,  true,  0,           0x002C, U | DBEB, 0},
    {"MOVCA.L SZ=L",           7, 4, 2, true,  false, 0x5A5A5A5A,  0x002B, U,        1},
    {"MOVCA.L DBEB data",      7, 4, 2, true,  false, 0xA5A5A5A5,  0x002C, U | DBEB, 2},
    {"MOVCA.L DBEB no data",   7, 4, 2, true,  false, 0x5A5A5A5A,  0x002C, U | DBEB, 0},
    {"TAS.B purge (none)",    10, 0, 0, false, true,  0,           0x002C, U,        0},
    {"TAS.B read SZ=B",       11, 1, 0, false, false, 0x00,        0x002D, U,        1},
    {"TAS.B write SZ=B",      12, 1, 0, true,  false, 0x80,        0x002D, U,        1},
    {"FMOV quad hi half",      1, 8, 3, false, false, 0xA5A5A5A500000000ull, 0x006C, U | DBEB, 2},
  };
  int nok = 0;
  for (auto &k : ks) {
    Ucfg u; u.barb = BUF; u.bamrb = 0x04; u.bbrb = k.bbrb; u.bdrb = 0xA5A5A5A5; u.bdmrb = 0; u.brcr = k.brcr;
    ubc_arm(u);
    int got = M.oper(BUF, k.iss_size, k.kind, k.data, g_asid);
    // undo the model's functional side effects on CMF (the RTL has none from oper()): clear via a BRCR write
    wr(P_UBC, BRCR, 1, k.brcr);
    if (k.kind != 10) {                                  // the RTL sees what the core presents (the purge: nothing)
      In e; e.asid = g_asid; e.op_v = true; e.op_wr = k.wr; e.op_size = k.op_size; e.op_va = BUF; e.op_nodata = k.nodata;
      Out oe = cycle(e);
      In s; s.asid = g_asid; s.d_v = !k.nodata; s.d_wr = k.wr; s.d_size = k.op_size; s.d_data = k.data;
      Out os = cycle(s);
      int rtl = oe.opm_b ? ((os.dbeb && !k.nodata) ? (os.d_mb ? 2 : 0) : 1) : 0;
      chk("kinds: RTL with the manual's mapping", rtl, k.exp);
    }
    chk("kinds: model oper() vs manual", got, k.exp);
    nok += got == k.exp;
    printf("kinds: %-24s model %d expected %d\n", k.n, got, k.exp);
  }
  // ctl_pre_armed: instruction pre (A or B), not post, not operand
  struct P { const char *n; uint16_t bbra, bbrb, brcr; bool exp; };
  const std::vector<P> ps = {{"A instruction pre", 0x0014, 0, U, true}, {"A instruction post", 0x0014, 0, U | 0x0400, false},
                             {"A either, pre", 0x0034, 0, U, true}, {"A operand", 0x0024, 0, U, false},
                             {"A instruction write-only", 0x0018, 0, U, false}, {"B instruction pre", 0, 0x001C, U, true},
                             {"B instruction post", 0, 0x0014, U | 0x0040, false}, {"idle", 0, 0, U, false}};
  for (auto &p : ps) {
    Ucfg u; u.bara = 0x8C012000; u.barb = 0x8C012000; u.bamra = u.bamrb = 0x04; u.bbra = p.bbra; u.bbrb = p.bbrb;
    u.brcr = p.brcr;
    ubc_arm(u);
    Out o = cycle(In());
    chk("kinds: ctl_pre_armed", o.pre_armed, p.exp);
    printf("kinds: pre_armed %-26s %d (expected %d)\n", p.n, o.pre_armed, p.exp);
  }
  wr(P_UBC, BBRA, 1, 0); wr(P_UBC, BBRB, 1, 0); wr(P_UBC, BRCR, 1, 0); idle(8);
  printf("kinds: %d / %zu operand kinds as the manual says; %ld checks, %ld failures\n", nok, ks.size(), g_chk - c0,
         g_fail - f0);
}

// ---------------------------------------------------------------------------------------------------- settle (9.5)
// Replays one settle function on the schedule of tb/v1_dbg_sched.h: the BBRA store's MA3 strobe, every fetch word, and
// (operand form) the load's EX cycles.  Returns whether the target breaks.
static bool settle_one(int k, bool add_fill, bool operand, int iqn, int *rel, bool *samplings_agree = nullptr) {
  using namespace dsched;
  std::vector<Ins> p = {st(5, 4, true)};
  for (int j = 0; j < k; j++) p.push_back(operand || !add_fill ? nop() : add(1));
  if (operand) { p.push_back(ld(6, 0)); for (int j = 0; j < 4; j++) p.push_back(add(1)); }
  else { p.push_back(nop()); p.push_back(nop()); p.push_back(nop()); }       // target ; rts ; nop
  Sched s = run(p, iqn, true);
  const uint32_t base = 0x8C012000;
  const int tgt = k + 1;
  Ucfg u;
  u.bara = operand ? BUF : base + 2 * tgt; u.bamra = 0x04; u.bbra = 0; u.brcr = 1;
  ubc_arm(u);
  bool brk = false;
  bool b_first = false, b_any = false, first = true;   // operand: the tag sampled in the first / any EX cycle
  for (int t = 0; t < s.cycles; t++) {
    In i; i.asid = g_asid;
    if (t == s.ma_last[0]) { i.port = P_UBC; i.wr = true; i.size = 1; i.off = UB + BBRA; i.wdata = operand ? 0x24 : 0x14; }
    int w = -1;
    for (int j = 0; j < (int)p.size(); j += 2) if (s.fetch[j] == t) w = j / 2;
    if (w >= 0) { i.if_v = true; i.if_va = base + 4 * w; }
    bool in_ex = operand && t > s.issue[tgt] && t <= s.ex_last[tgt];
    if (in_ex) { i.op_v = true; i.op_size = 2; i.op_va = BUF; }
    Out o = cycle(i);
    if (!operand && w == tgt / 2) brk = (o.ift_a >> (tgt & 1)) & 1;
    if (in_ex) {
      if (first) b_first = o.opm_a;
      first = false;
      b_any |= o.opm_a;
      if (t == s.ex_last[tgt]) brk = o.opm_a;                            // the tag latched when EX advances
    }
  }
  if (samplings_agree) *samplings_agree = !operand || (b_first == brk && b_any == brk);
  if (rel) *rel = operand ? s.ex_last[tgt] - s.ma_last[0] : s.fetch[tgt] - s.ma_last[0];
  wr(P_UBC, BBRA, 1, 0); idle(8);
  return brk;
}

// the 44 console settle points [HW ubcbrk T, ubcevt S]
struct SettlePt { const char *set; bool add, opd; int k; bool brk; };
static std::vector<SettlePt> settle_points() {
  std::vector<SettlePt> pts;
  for (int k = 0; k <= 14; k++) pts.push_back({"ubcbrk T NOP", false, false, k, k >= 13});
  for (int k = 0; k <= 8; k++) pts.push_back({"ubcbrk T ADD", true, false, k, false});
  for (int k : {9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20, 22, 24}) pts.push_back({"ubcevt S ADD", true, false, k, k >= 11});
  for (int k = 0; k <= 6; k++) pts.push_back({"ubcevt S operand", false, true, k, k >= 3});
  return pts;
}

static void sec_settle() {
  long f0 = g_fail, c0 = g_chk;
  reset(0xF);
  g_asid = 0;
  const std::vector<SettlePt> pts = settle_points();
  std::set<std::pair<std::string, int>> known = {{"ubcevt S ADD", 9}, {"ubcevt S ADD", 10}};
  bool pass = true;
  for (int iqn : {5, 6, 4}) {
    int match = 0, n = 0, nsamp = 0;
    std::string miss;
    for (auto &p : pts) {
      int rel = 0;
      bool agree = true;
      bool b = settle_one(p.k, p.add, p.opd, iqn, &rel, &agree);
      chk("settle: operand tag sampled in the first / any / last EX cycle agree", agree, 1);
      nsamp += p.opd && agree;
      n++;
      if (b == p.brk) { match++; continue; }
      char s[96];
      snprintf(s, sizeof s, " [%s k=%d: %s, target %s at MA3%+d]", p.set, p.k, b ? "breaks" : "no break",
               p.opd ? "EX" : "fetched", rel);
      miss += s;
      bool is_known = iqn == 4 && known.count({p.set, p.k});
      if (!is_known) pass = false;
    }
    printf("settle: fetch run-ahead %d halfwords: %d / %d points as on the console (operand points with the same "
           "break for first / any / last EX-cycle sampling: %d / 7)%s\n", iqn, match, n, nsamp, miss.empty() ? "" : ";");
    if (!miss.empty()) printf("settle:   mismatches:%s%s\n", miss.c_str(),
                              iqn == 4 ? "  (known: the pre-SoC 4-halfword queue; v1_fetch holds 6, SOC.md 9.5)" : "");
  }
  chk("settle vs console (IQN 5 and 6 exact; IQN 4 only the known ADD 9 / 10)", pass, 1);
  printf("settle: %ld checks, %ld failures\n", g_chk - c0, g_fail - f0);
}

// ---------------------------------------------------------------------------------------------------- settle-asid
// The console's ptehubc points (M2-fix; M2-verify F6) on the schedule of tb/v1_dbg_sched.h: the PTEH store occupies MA
// 4 cycles (st_pteh) and v1_mmu's PTEH.ASID -- this module's `asid` -- changes at the end of its capture cycle (MA4).
// Returns whether the target breaks (BASRA = basra, ASID compared).
static bool asid_one(int k, bool operand, int basra, int iqn, int *rel) {
  using namespace dsched;
  std::vector<Ins> p = {st_pteh(5, 4)};
  for (int j = 0; j < k; j++) p.push_back(nop());
  if (operand) { p.push_back(ld(6, 0)); for (int j = 0; j < 4; j++) p.push_back(add(1)); }
  else { p.push_back(nop()); p.push_back(nop()); p.push_back(nop()); }       // target ; rts ; nop
  Sched s = run(p, iqn, true);
  const uint32_t base = 0x8C012000;
  const int tgt = k + 1;
  g_asid = 1;
  Ucfg u;
  u.bara = operand ? BUF : base + 2 * tgt; u.bamra = 0x00; u.basra = (uint8_t)basra; u.bbra = operand ? 0x24 : 0x14;
  u.brcr = 1;
  ubc_arm(u);
  idle(8);                                                             // the copies (registers, ASID) settle
  bool brk = false;
  for (int t = 0; t < s.cycles; t++) {
    In i; i.asid = t > s.ma_last[0] ? 2 : 1;                          // the landing: the end of MA4
    int w = -1;
    for (int j = 0; j < (int)p.size(); j += 2) if (s.fetch[j] == t) w = j / 2;
    if (w >= 0) { i.if_v = true; i.if_va = base + 4 * w; }
    bool in_ex = operand && t > s.issue[tgt] && t <= s.ex_last[tgt];
    if (in_ex) { i.op_v = true; i.op_size = 2; i.op_va = BUF; }
    Out o = cycle(i);
    if (!operand && w == tgt / 2) brk = (o.ift_a >> (tgt & 1)) & 1;
    if (in_ex && t == s.ex_last[tgt]) brk = o.opm_a;                   // the tag latched when EX advances
  }
  if (rel) *rel = operand ? s.ex_last[tgt] - s.ma_last[0] : s.fetch[tgt] - s.ma_last[0];
  g_asid = 2;
  wr(P_UBC, BBRA, 1, 0); idle(8);
  g_asid = 0; idle(8);
  return brk;
}

static void sec_settle_asid() {
  long f0 = g_fail, c0 = g_chk;
  reset(0xF);
  for (int iqn : {5, 6}) {
    int match = 0, n = 0;
    std::string miss;
    for (int opd = 0; opd < 2; opd++)
      for (int basra : {2, 1})
        for (int k = 0; k <= (opd ? 6 : 14); k++) {
          const bool nw = opd ? k >= 3 : k >= 13;                      // the console: the new ASID from here
          const bool want = basra == 2 ? nw : !nw;
          int rel = 0;
          const bool b = asid_one(k, opd, basra, iqn, &rel);
          n++;
          chk("settle-asid vs console [HW ptehubc]", b, want);
          if (b == want) { match++; continue; }
          char t[96];
          snprintf(t, sizeof t, " [%s BASRA %d k=%d: %s, target %s at MA4%+d]", opd ? "operand" : "pre", basra, k,
                   b ? "breaks" : "no break", opd ? "EX" : "fetched", rel);
          miss += t;
        }
    printf("settle-asid: fetch run-ahead %d halfwords: %d / %d points as on the console (ptehubc N / O / P / Q, "
           "warm)%s\n", iqn, match, n, miss.empty() ? "" : ";");
    if (!miss.empty()) printf("settle-asid:   mismatches:%s\n", miss.c_str());
  }
  printf("settle-asid: %ld checks, %ld failures\n", g_chk - c0, g_fail - f0);
}

// ---------------------------------------------------------------------------------------------------- settle-dly
// Open question 9.5 on the model alone: the 44 console points for every fetch-side copy delay and run-ahead.  The
// schedule and the break decision are those of settle_one (fetch tag of the target's word; the operand tag in the
// load's last EX cycle, which the settle section shows is sampling-independent).
static bool settle_model(const SettlePt &pt, int iqn, int dly) {
  using namespace dsched;
  std::vector<Ins> p = {st(5, 4, true)};
  for (int j = 0; j < pt.k; j++) p.push_back(pt.opd || !pt.add ? nop() : add(1));
  if (pt.opd) { p.push_back(ld(6, 0)); for (int j = 0; j < 4; j++) p.push_back(add(1)); }
  else { p.push_back(nop()); p.push_back(nop()); p.push_back(nop()); }
  Sched s = run(p, iqn, true);
  const uint32_t base = 0x8C012000;
  const int tgt = pt.k + 1;
  dbg::Ubc m(dly);
  m.cycle_mode = true;
  bool ea, eb;
  auto wr_m = [&](uint32_t off, int size, uint32_t v) { m.wr(off, size, v, false); m.clock(true, 0, 0, ea, eb); };
  wr_m(UB + BARA, 2, pt.opd ? BUF : base + 2 * tgt); wr_m(UB + BAMRA, 0, 0x04); wr_m(UB + BRCR, 1, 0x0001);
  for (int n = 0; n < 16; n++) m.clock(true, 0, 0, ea, eb);
  bool brk = false;
  for (int t = 0; t < s.cycles; t++) {
    if (t == s.ma_last[0]) m.wr(UB + BBRA, 1, pt.opd ? 0x24 : 0x14, false);
    int w = -1;
    for (int j = 0; j < (int)p.size(); j += 2) if (s.fetch[j] == t) w = j / 2;
    if (!pt.opd && w == tgt / 2) { unsigned a, b; m.fetch_tags(base + 4 * w, 0, a, b); brk = (a >> (tgt & 1)) & 1; }
    if (pt.opd && t == s.ex_last[tgt]) brk = m.op_match(0, BUF, 2, false, false, 0);
    m.clock(true, 0, 0, ea, eb);
  }
  return brk;
}

static void sec_settle_dly() {
  long f0 = g_fail, c0 = g_chk;
  const std::vector<SettlePt> pts = settle_points();
  printf("settle-dly: console points matched (of %zu) per fetch-side copy delay, model only\n", pts.size());
  printf("settle-dly:   run-ahead |");
  for (int dly = 1; dly <= 8; dly++) printf(" dly%d", dly);
  printf("\n");
  for (int iqn : {4, 5, 6}) {
    int fits = 0, fit_dly = -1, best = 0;
    std::vector<int> row;
    for (int dly = 1; dly <= 8; dly++) {
      int match = 0;
      for (auto &p : pts) match += settle_model(p, iqn, dly) == p.brk;
      row.push_back(match);
      best = std::max(best, match);
      if (match == (int)pts.size()) { fits++; fit_dly = dly; }
    }
    printf("settle-dly:   %d halfwords |", iqn);
    for (int m : row) printf(" %4d", m);
    printf("   %s\n", fits == 1 ? "exactly one delay fits" : fits ? "several delays fit" : "no delay fits");
    if (iqn == 4) {
      // the pre-SoC 4-halfword queue: no delay reaches 44; 3 and 4 tie at the best score (ADD 9 / 10 vs NOP 13 / 14;
      // 5 too)
      chk("settle-dly: run-ahead 4: no copy delay fits all points", fits, 0);
      chk("settle-dly: run-ahead 4: IF_DLY 3 at the best score", row[2], best);
      chk("settle-dly: run-ahead 4: IF_DLY 3 and 4 tie", row[2], row[3]);
    } else {
      chk("settle-dly: run-ahead 5 / 6: exactly one copy delay fits", fits, 1);
      chk("settle-dly: run-ahead 5 / 6: it is IF_DLY = 3", fit_dly, 3);
    }
  }
  printf("settle-dly: %ld checks, %ld failures\n", g_chk - c0, g_fail - f0);
}

// ---------------------------------------------------------------------------------------------------- seqrace
static void sec_seqrace() {
  long f0 = g_fail, c0 = g_chk;
  reset(0xF);
  g_asid = 0;
  const uint16_t U = 0x0001, SEQ = 0x0008;
  // a BRCR write strobe and the core's hits in the same cycle
  auto brcr_hit = [&](uint16_t v, bool ha, bool hb) {
    In i; i.asid = g_asid; i.port = P_UBC; i.wr = true; i.size = 1; i.off = UB + BRCR; i.wdata = v;
    i.hit_a = ha; i.hit_b = hb;
    return cycle(i);
  };
  auto hit = [&](bool ha, bool hb) { In i; i.asid = g_asid; i.hit_a = ha; i.hit_b = hb; return cycle(i); };
  auto live = [&]() { In i; i.asid = g_asid; return cycle(i).b_live; };
  auto brcr = [&]() { return rd(P_UBC, BRCR, 1); };
  // 1. the verifier's repro: {SEQ = 0 write + A hit under the old SEQ = 1}, then SEQ = 1 again: no A since -> B is not
  //    live, a B hit neither breaks nor sets CMFB nor counts 0x1A
  wr(P_UBC, BRCR, 1, SEQ | U);
  chk("seqrace 1: SEQ, nothing seen: b_live", live(), 0);
  Out o = brcr_hit(U, true, false);
  chk("seqrace 1: the A half still counts 0x19", o.ev_a, 1);
  chk("seqrace 1: SEQ = 0 after the write: b_live", live(), 1);
  chk("seqrace 1: no CMFA (the A was under SEQ = 1)", brcr(), U);
  wr(P_UBC, BRCR, 1, SEQ | U);
  chk("seqrace 1: SEQ = 1 re-written: B not live (A seen was cleared)", live(), 0);
  o = hit(false, true);
  chk("seqrace 1: B without A: no 0x1A", o.ev_b, 0);
  chk("seqrace 1: B without A: no CMFB", brcr(), SEQ | U);
  // 2. A seen a cycle before the SEQ = 0 write: cleared as well
  hit(true, false);
  chk("seqrace 2: A seen: b_live", live(), 1);
  wr(P_UBC, BRCR, 1, U);
  wr(P_UBC, BRCR, 1, SEQ | U);
  chk("seqrace 2: SEQ = 0 then 1: B not live", live(), 0);
  // 3. a SEQ = 1 re-write in the cycle of an A hit keeps "A seen": B is live, a B hit completes the sequence
  brcr_hit(SEQ | U, true, false);
  chk("seqrace 3: SEQ rewrite + A: b_live", live(), 1);
  o = hit(false, true);
  chk("seqrace 3: B after A: 0x1A", o.ev_b, 1);
  chk("seqrace 3: B after A: CMFB only", brcr(), 0x4000 | SEQ | U);
  chk("seqrace 3: sequence completed: b_live", live(), 0);
  // 4. SEQ = 0 -> 1 in the cycle of an A hit: the hit is under SEQ = 0 (CMFA, a hardware set beating the write's 0
  //    [P]) and leaves no "A seen"
  wr(P_UBC, BRCR, 1, U);
  brcr_hit(SEQ | U, true, false);
  chk("seqrace 4: A under the old SEQ = 0 sets CMFA", brcr(), 0x8000 | SEQ | U);
  chk("seqrace 4: no A seen under the new SEQ = 1", live(), 0);
  // 5. {SEQ = 0 write + B hit completing a sequence}: CMFB set (hardware wins), nothing left seen
  wr(P_UBC, BRCR, 1, SEQ | U);
  hit(true, false);
  o = brcr_hit(U, false, true);
  chk("seqrace 5: B completes: 0x1A", o.ev_b, 1);
  chk("seqrace 5: CMFB beats the write", brcr(), 0x4000 | U);
  wr(P_UBC, BRCR, 1, SEQ | U);
  chk("seqrace 5: then SEQ = 1: B not live", live(), 0);
  wr(P_UBC, BRCR, 1, 0); idle(8);
  printf("seqrace: %ld checks, %ld failures\n", g_chk - c0, g_fail - f0);
}

// ---------------------------------------------------------------------------------------------------- model
// dbg::Ubc used functionally (Sh4Sys, run_iss: P4Slave::wr, then pre() / post() with no clock()).  Verifier finding:
// the default constructor used to be the cycle model's delay 3, which queued every write to the fetch-side copy for a
// clock() that never came, so instruction breaks never fired.
static void sec_model() {
  long f0 = g_fail, c0 = g_chk;
  dbg::Ubc u_def, u_func(-1), u_dly(3);             // u_dly: a delay given without cycle mode
  const char *nm[3] = {"Ubc()", "Ubc(-1)", "Ubc(3), cycle_mode = false"};
  dbg::Ubc *us[3] = {&u_def, &u_func, &u_dly};
  for (int v = 0; v < 3; v++) {
    dbg::Ubc &u = *us[v];
    const uint32_t PC = 0x8C010008;
    u.wr(CC + BASRA, 0, 0x00, false);
    u.wr(UB + BARA, 2, PC, false);
    u.wr(UB + BAMRA, 0, 0x04, false);                  // BASM
    u.wr(UB + BRCR, 1, 0x0001, false);                 // UBDE
    u.wr(UB + BBRA, 1, 0x0014, false);                 // instruction, read, pre-execution
    bool brk = u.pre(PC, 0x00);
    chk("model: functional pre-execution break fires at once", brk, 1);
    printf("model: dbg::%-28s functional pre-execution break %s\n", nm[v], brk ? "fires" : "NEVER fires");
  }
  printf("model: %ld checks, %ld failures\n", g_chk - c0, g_fail - f0);
}

// ---------------------------------------------------------------------------------------------------- random
static void sec_random(long n) {
  long f0 = g_fail, c0 = g_chk;
  long m0[7];
  memcpy(m0, g_match, sizeof m0);
  reset(0xF);
  static const uint32_t bases[] = {0x8C012340, 0x0C012340, 0xAC012340, 0xFF200000, 0x7C001000, 0x00000000};
  static const uint8_t asids[] = {0x00, 0x01, 0x80};
  auto addr = [&]() { return bases[rndn(6)] + (rndn(4) ? rndn(0x400) : (uint32_t)rnd64() & 0xFFFFF); };
  auto sel = [&](uint32_t v) { return rndn(8) ? v : (uint32_t)rnd64(); };
  uint32_t data_pool[4] = {0xA5A5A5A5, 0x1234A5A5, 0x0000FFFF, 0x5A5A5A5A};
  for (long i = 0; i < n; i++) {
    In c;
    c.run = rndn(32) != 0;
    c.asid = asids[rndn(3)];
    if (rndn(300000) == 0) { reset(rndn(3) == 0 ? 0x5 : rndn(2) ? 0x3 : 0x1); continue; }
    if (rndn(4) == 0) {                                // register traffic
      bool ccn = rndn(8) == 0;
      c.port = ccn ? P_CCN : P_UBC;
      c.wr = rndn(3) != 0;
      static const int regs[] = {0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20};
      static const int rsz[] = {2, 0, 1, 2, 0, 1, 2, 2, 1};
      int r = rndn(9);
      uint32_t o = ccn ? (rndn(2) ? 0x14 : 0x18) : (rndn(16) ? regs[r] : 4 * rndn(64));
      c.size = rndn(8) ? (ccn ? 0 : rsz[r]) : rndn(3);
      if (rndn(16) == 0) o += c.size == 0 ? rndn(4) : c.size == 1 ? 2 : 0;
      c.off = (ccn ? CC + (rndn(4) ? 0 : (rndn(0x400) << 8)) : UB) + o;
      switch (ccn ? -1 : r) {
      case 0: case 3: c.wdata = sel(addr() & ~1u); break;
      case 1: case 4: c.wdata = rndn(4) ? (rndn(2) ? 0x04 : rndn(16)) : (uint32_t)rnd64(); break;
      case 2: case 5: {                               // mostly enabled: ID != 0, RW != 0
        uint32_t id = 1 + rndn(3), rw = 1 + rndn(3), sz = rndn(3) ? 0 : rndn(8);
        c.wdata = rndn(8) ? ((sz & 4) << 4) | (id << 4) | (rw << 2) | (sz & 3) : (uint32_t)rnd64();
        break;
      }
      case 6: c.wdata = data_pool[rndn(4)]; break;
      case 7: c.wdata = rndn(2) ? 0 : rndn(2) ? 0xFFFF0000 : (uint32_t)rnd64(); break;
      case 8: c.wdata = rndn(2) ? (uint32_t)rnd64() : (0x0001 | (rndn(2) ? 0x0008 : 0) | (rndn(2) ? 0x0400 : 0) |
                                                       (rndn(2) ? 0x0040 : 0) | (rndn(2) ? 0x0080 : 0)); break;
      default: c.wdata = rndn(2) ? asids[rndn(3)] : (uint32_t)rnd64(); break;
      }
    }
    if (rndn(2)) { c.if_v = true; c.if_va = addr() & ~3u; }
    if (rndn(2)) {
      c.op_v = true; c.op_wr = rndn(2); c.op_size = rndn(4); c.op_nodata = rndn(6) == 0;
      c.op_va = addr() & ~((1u << c.op_size) - 1) & (c.op_size == 3 ? ~7u : ~0u);
    }
    if (rndn(2)) {
      c.d_v = true; c.d_wr = rndn(2); c.d_size = rndn(4);
      c.d_data = rndn(2) ? ((uint64_t)data_pool[rndn(4)] << (rndn(2) ? 32 : 0)) | data_pool[rndn(4)] : rnd64();
      if (rndn(4) == 0) c.d_data = 0xA5A5A5A5A5A5A5A5ull >> (8 * rndn(8));
    }
    if (rndn(6) == 0) { c.hit_a = rndn(2); c.hit_b = rndn(2); }
    cycle(c);
  }
  printf("random: %ld cycles, %ld checks, %ld failures; matches: ift.a %ld ift.b %ld opm.a %ld opm.b %ld d_mb %ld, "
         "event cycles %ld, pre_armed cycles %ld\n", n, g_chk - c0, g_fail - f0, g_match[0] - m0[0], g_match[1] - m0[1],
         g_match[2] - m0[2], g_match[3] - m0[3], g_match[4] - m0[4], g_match[5] - m0[5], g_match[6] - m0[6]);
}

int main(int argc, char **argv) {
  long n = 2000000;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--n") && i + 1 < argc) n = atol(argv[++i]);
    else if (!strcmp(argv[i], "--seed") && i + 1 < argc) g_rs = strtoull(argv[++i], 0, 0);
  }
  Verilated::commandArgs(argc, argv);
  d = new Vv1_ubc_tb_top;
  M.cycle_mode = true;
  sec_regs();
  sec_breaks();
  sec_kinds();
  sec_seqrace();
  sec_model();
  sec_settle();
  sec_settle_asid();
  sec_settle_dly();
  sec_random(n);
  printf("v1-ubc: %ld checks, %ld failures\n", g_chk, g_fail);
  printf("v1-ubc: %s\n", g_fail ? "FAIL" : "PASS");
  delete d;
  return g_fail ? 1 : 0;
}
