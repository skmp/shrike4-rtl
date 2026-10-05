// v1_intc_tb.cpp -- unit testbench of WI-EXC: rtl/v1/v1_intc.sv and rtl/v1/v1_excregs.sv (tb/v1_intc_tb.sv) against
// the functional model tb/sh4_intc.h and the console measurements (spec exceptions-intc 6, 8; rtl/v1/SOC.md 3.4, 8.1).
//
// Groups (each one prints its checks; the last line is "v1-intc: PASS" / "v1-intc: FAIL", exit code 1 on a failure):
//   regs     INTC reset values (ICR = 0x8000 / 0x0000 by the NMI pin), write masks [HW: ICR 0xC381, IPRA 0xFFFF,
//            IPRB 0xFFF0, IPRC 0xFFFF], the size rule, holes (IPRD 0x10 [HW: 0]), random accesses vs the model, idle
//            port outputs, the reset kinds (power-on / manual initialise, CPU-only holds)
//   resolve  every on-chip source one-hot at every level 0-15 against every IMASK; equal-level pairs in the table
//            order [HW: TMU1 0x420 before WDT 0x560 at level 10; WDT 12 / TMU1 10; TMU1 12 / WDT 10; level 6 at IMASK 6
//            not taken, 7 taken]; IRL above on-chip at equal level; IRL codes [HW: pins 1101 / 1011 / 1001 ->
//            0x3A0 / 0x360 / 0x320; IRL level 2 taken at IMASK 1, not at 2]; IRLM = 1; MAI; 3000 random states vs
//            the model; BL never reaches the ordinary request; irq.code follows the winner while req stays 1
//   imask    the SOC.md 3.4 port-level target at every phase: IMASK lowered in cycle s -> irq.req at s + 9 (s even) /
//            s + 10 (s odd); raised -> irq.req and `wake` gone from s + 1 [HW ci2 F, irqloop, excint C1i], a request in
//            flight never presented, and a raise-and-lower pulse (the compare is the output register's level against
//            the Iclk input register's IMASK)
//   progs    the console programs replayed on the INTC with v1's issue schedule (boundaries relative to the SR write,
//            tb/v1_intc_progs.h: the v1 core's trace of each program's own code, re-derived by `make v1-intc-sched`):
//            ci2's 12 warm cases, excint run 1 C1 (3), excint run 3 (2 x 8 records), the excint2 PH sweep (12 n x 2
//            trials, phase fixed by the TCNT1 read)
//   timing   on-chip source -> request 4 / 5 ("1 Icyc + 2 Bcyc" [M]), and its drop -> request gone 4 / 5 (the raise
//            compare's level is the output register's); IPR write strobe -> request 6; the `wake` edge
//            (4 / 5, in a ph-1 / ph-3 cycle)
//   irl      the 3-sample noise canceller (2-sample pulses never seen, 3-sample pulses seen), IRL decision 14 / 15
//            ("1 Icyc + 7 Bcyc" [M]), IRLM = 1 codes, IRLM = 1 per-pin cancellers (a toggling IRL3 never holds back
//            IRL0) [P]
//   nmi      edges per NMIE, decision 8 / 9 ("1 Icyc + 4 Bcyc" [M]), NMIB / BL / sleep, nmi_ack clears the latch and
//            NMIB, the 6-Bcyc blind window after an NMIE change [M], MAI, nmi_evt; MAI = 1 in standby: a pin
//            transition generates no NMI (no wake, no nmi_evt, not taken after another exit) [M 19.4.3]; an NMIB write in
//            the nmi_ack cycle loses [P]
//   wake     wake (BL ignored, IMASK applied), standby: IRL sampled on rtc_tick only, ITI excluded, IMASK applied,
//            NMI (with MAI masking it while the pin is low)
//   excregs  TRA / EXPEVT / INTEVT: reset values per rst_code, masks [HW 0x3FC / 0xFFF / 0xFFF], the size rule, the
//            256-byte repeat, excw (EXPEVT vs INTEVT [HW], TRA), idle outputs, a bus (DMAC) write in the excw cycle
//            (lands unless the entry writes the same register)
// usage: tb [--only GROUP] [-v]
#include "Vv1_intc_tb.h"
#include "verilated.h"
#include "sh4_intc.h"
#include "v1_intc_progs.h"
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <set>
#include <string>
#include <vector>

static int g_fail = 0, g_checks = 0, g_verbose = 0;
static void check(bool ok, const char *fmt, ...) {
  g_checks++;
  if (ok) return;
  if (g_fail++ < 40) {
    va_list ap; va_start(ap, fmt);
    printf("  FAIL: "); vprintf(fmt, ap); printf("\n");
    va_end(ap);
  }
}

// =====================================================================================================================
// The harness: one clock, the CPG's enables, a bridge that strobes one register access at a time
// =====================================================================================================================
struct Tb {
  Vv1_intc_tb *m;
  uint64_t cyc = 0;       // the cycle whose inputs are being set
  int ph = 0;             // CPG phase (SOC.md 2.1): bce at ph 1 / 3, pce at ph 3; cleared by a power-on reset
  bool standby = false;   // bce / pce stopped (SOC.md 2.5)
  bool strobing = false;

  Tb() { m = new Vv1_intc_tb; idle_inputs(); }
  ~Tb() { m->final(); delete m; }

  void idle_inputs() {
    m->rst_pin = m->rst_por = m->rst_man = m->rst_cpu = 0; m->rst_code = 0;
    m->p4_rd = m->p4_wr = m->p4_sel_exc = m->p4_dma = 0; m->p4_size = 0; m->p4_off = 0; m->p4_wdata = 0;
    m->irl_n = 0xF; m->nmi_pin = 1; m->src = 0; m->sr_imask = 15; m->sr_bl = 0; m->asleep = 0; m->stby = 0;
    m->rtc_tick = 0; m->irq_ack = 0; m->nmi_ack = 0;
    m->excw_v = m->excw_intr = m->excw_tra_v = m->excw_tea_v = m->excw_ubrk = 0; m->excw_code = 0; m->excw_tra = 0;
    m->excw_tea = 0;
  }
  // evaluate cycle `cyc` with its inputs (clock low): outputs are this cycle's
  void lo() {
    m->bce = !standby && (ph & 1);
    m->pce = !standby && ph == 3;
    m->clk = 0; m->eval();
    if (!strobing) check(m->p4_rdata == 0 && !m->p4_hit && !m->p4_stall, "idle slave outputs nonzero at cycle %llu",
                         (unsigned long long)cyc);
  }
  void hi() { m->clk = 1; m->eval(); cyc++; ph = (ph + 1) & 3; }
  void step(int n = 1) { for (int i = 0; i < n; i++) { lo(); hi(); } }

  // resets: a request level held 8 Icyc (v1_rstc, SOC.md 2.2).  kind: 0 pin POR, 1 manual, 2 CPU-only
  void reset(int kind, int code = -1) {
    m->rst_pin = kind == 0; m->rst_por = kind == 0; m->rst_man = kind == 1; m->rst_cpu = 1;
    m->rst_code = code >= 0 ? code : (kind == 0 ? 0 : 1);
    for (int i = 0; i < 8; i++) { lo(); hi(); if (kind == 0) ph = 0; }   // the CPG's phase restarts on a POR
    m->rst_pin = m->rst_por = m->rst_man = m->rst_cpu = 0;
  }
  void por() { reset(0); }

  // one bridge access: INTC strobes only in a pce cycle; the exception registers are Iclk class (any cycle)
  uint32_t p4(bool exc, bool wr, int size, uint32_t off, uint32_t wdata = 0, bool *hit = nullptr) {
    if (!exc) while (ph != 3) step();
    m->p4_sel_exc = exc; m->p4_rd = !wr; m->p4_wr = wr; m->p4_size = size; m->p4_off = off & 0x0FFFFFFF;
    m->p4_wdata = wdata;
    strobing = true;
    lo();
    const uint32_t r = m->p4_rdata;
    if (hit) *hit = m->p4_hit;
    check(!m->p4_stall, "slave stalled (off %07x)", off);
    hi();
    strobing = false;
    m->p4_rd = m->p4_wr = 0; m->p4_sel_exc = 0;
    return r;
  }
  uint32_t ird(uint32_t reg, int size = 1, bool *hit = nullptr) { return p4(false, false, size, 0x0FD00000u + reg, 0, hit); }
  void iwr(uint32_t reg, uint32_t v, int size = 1) { p4(false, true, size, 0x0FD00000u + reg, v); }
  uint32_t erd(uint32_t reg, int size = 2, bool *hit = nullptr) { return p4(true, false, size, 0x0F000000u + reg, 0, hit); }
  void ewr(uint32_t reg, uint32_t v, int size = 2) { p4(true, true, size, 0x0F000000u + reg, v); }

  // steady-state request (settles every synchroniser and the IRL filter)
  void settle() { step(40); }
  void run_to_ph(int p) { while (ph != p) step(); }
};

static const char *g_only = nullptr;
static bool want(const char *g) { return !g_only || !strcmp(g_only, g); }
static void group(const char *g) { printf("[%s]\n", g); }

// write the model's register state into the RTL
static void load_regs(Tb &t, const Sh4Intc &md) {
  t.iwr(0x0, md.icr); t.iwr(0x4, md.ipra); t.iwr(0x8, md.iprb); t.iwr(0xC, md.iprc);
}
// compare the RTL's irq outputs with the model's port view (code only when requesting)
static bool same_req(Tb &t, const Sh4Intc &md, int imask, bool bl, bool asleep, std::string *why = nullptr) {
  t.lo();
  const Sh4Intc::Req r = md.port(imask, bl, asleep);
  const bool ok = t.m->irq_req == r.req && t.m->irq_nmi == r.nmi && (!r.req || t.m->irq_code == r.code);
  if (!ok && why) {
    char b[160];
    snprintf(b, sizeof b, "rtl req %d nmi %d code %03x, model req %d nmi %d code %03x", t.m->irq_req, t.m->irq_nmi,
             t.m->irq_code, r.req, r.nmi, r.code);
    *why = b;
  }
  t.hi();
  return ok;
}

// =====================================================================================================================
// regs
// =====================================================================================================================
static void t_regs() {
  group("regs");
  {
    Tb t; t.m->nmi_pin = 1; t.por();
    check(t.ird(0x0) == 0x8000, "ICR after POR, NMI high: %04x (want 8000 [HW])", t.ird(0x0));
    check(t.ird(0x4) == 0 && t.ird(0x8) == 0 && t.ird(0xC) == 0, "IPRA-C after POR not 0");
    t.iwr(0x0, 0xFFFF); t.iwr(0x4, 0xFFFF); t.iwr(0x8, 0xFFFF); t.iwr(0xC, 0xFFFF);
    uint32_t a = t.ird(0x0), b = t.ird(0x4), c = t.ird(0x8), d = t.ird(0xC);
    check(a == 0xC381 && b == 0xFFFF && c == 0xFFF0 && d == 0xFFFF,
          "write masks: ICR %04x IPRA %04x IPRB %04x IPRC %04x (want C381 FFFF FFF0 FFFF [HW])", a, b, c, d);
    printf("  masks after 0xFFFF: ICR %04x IPRA %04x IPRB %04x IPRC %04x\n", a, b, c, d);
    bool hit;
    uint32_t v = t.ird(0x10, 1, &hit);
    check(v == 0 && !hit, "IPRD 0x10 must be a hole (read 0, hit 0) [HW]");
    t.iwr(0x10, 0x1234);
    check(t.ird(0x10) == 0, "a hole kept a write");
    v = t.ird(0x0, 0, &hit); check(v == 0 && hit, "ICR byte read: %x hit %d (size rule: 0, hit 1)", v, hit);
    v = t.ird(0x4, 2, &hit); check(v == 0 && hit, "IPRA long read: %x hit %d (size rule)", v, hit);
    t.iwr(0x4, 0x1111, 0); t.iwr(0x4, 0x2222, 2);
    check(t.ird(0x4) == 0xFFFF, "IPRA took a write of another size");
    v = t.ird(0x2, 1, &hit); check(v == 0 && !hit, "ICR+2 must be a hole");
    v = t.ird(0x1004, 1, &hit); check(v == 0 && !hit, "0xFFD01004 must be a hole (full decode, no mirror)");
    // NMI pin low at reset: ICR 0x0000
    Tb u; u.m->nmi_pin = 0; u.por();
    check(u.ird(0x0) == 0x0000, "ICR after POR, NMI low: %04x (want 0000 [M])", u.ird(0x0));
    // reset kinds: CPU-only holds, manual and power-on initialise
    t.reset(2);
    check(t.ird(0x4) == 0xFFFF && t.ird(0x0) == 0xC381, "a CPU-only reset must hold ICR / IPR");
    t.reset(1);
    check(t.ird(0x4) == 0 && t.ird(0x8) == 0 && t.ird(0x0) == 0x8000, "a manual reset must initialise ICR / IPR");
    t.iwr(0xC, 0xABCD); t.reset(0);
    check(t.ird(0xC) == 0, "a power-on reset must initialise IPRC");
  }
  {  // random accesses vs the model (hit, value, every size, holes)
    Tb t; t.por();
    Sh4Intc md; md.reset();
    std::mt19937 rng(1);
    const uint32_t offs[] = {0x0, 0x4, 0x8, 0xC, 0x2, 0x6, 0x10, 0x14, 0x1C, 0x100, 0x1000, 0x20000, 0x3FFFC};
    int bad = 0;
    for (int i = 0; i < 3000; i++) {
      const uint32_t off = offs[rng() % 13];
      const int size = rng() % 3;
      if (rng() & 1) {
        const uint32_t w = rng();
        t.iwr(off, w, size); md.wr(Sh4Intc::OFF_BASE + off, size, w, false);
      } else {
        bool hit; uint32_t ref = 0;
        const uint32_t v = t.ird(off, size, &hit);
        const bool mh = md.rd(Sh4Intc::OFF_BASE + off, size, ref, false);
        if (v != ref || hit != mh) bad++;
      }
    }
    check(bad == 0, "random register accesses: %d reads differ from sh4_intc.h", bad);
    printf("  3000 random accesses vs sh4_intc.h: %d mismatches\n", bad);
  }
}

// =====================================================================================================================
// resolve
// =====================================================================================================================
static int field_shift(int i, int *reg) {   // (register 0 IPRA 1 IPRB 2 IPRC, shift) of source i
  switch (i) {
    case Sh4Intc::S_HUDI: *reg = 2; return 0;
    case Sh4Intc::S_GPIO: *reg = 2; return 12;
    case Sh4Intc::S_DMTE0: case Sh4Intc::S_DMTE1: case Sh4Intc::S_DMTE2: case Sh4Intc::S_DMTE3:
    case Sh4Intc::S_DMAE: *reg = 2; return 8;
    case Sh4Intc::S_TUNI0: *reg = 0; return 12;
    case Sh4Intc::S_TUNI1: *reg = 0; return 8;
    case Sh4Intc::S_TUNI2: case Sh4Intc::S_TICPI2: *reg = 0; return 4;
    case Sh4Intc::S_ATI: case Sh4Intc::S_PRI: case Sh4Intc::S_CUI: *reg = 0; return 0;
    case Sh4Intc::S_SCI_ERI: case Sh4Intc::S_SCI_RXI: case Sh4Intc::S_SCI_TXI: case Sh4Intc::S_SCI_TEI:
      *reg = 1; return 4;
    case Sh4Intc::S_SCIF_ERI: case Sh4Intc::S_SCIF_RXI: case Sh4Intc::S_SCIF_BRI: case Sh4Intc::S_SCIF_TXI:
      *reg = 2; return 4;
    case Sh4Intc::S_ITI: *reg = 1; return 12;
    default: *reg = 1; return 8;
  }
}
static void set_level(Sh4Intc &md, int i, int lvl) {
  int reg; const int sh = field_shift(i, &reg);
  uint16_t *r = reg == 0 ? &md.ipra : reg == 1 ? &md.iprb : &md.iprc;
  *r = (uint16_t)((*r & ~(15u << sh)) | ((unsigned)lvl << sh));
  md.iprb &= 0xFFF0;
}
static void apply(Tb &t, Sh4Intc &md, int imask) {
  load_regs(t, md);
  t.m->src = md.src; t.m->irl_n = md.irl; t.m->nmi_pin = md.nmi_pin; t.m->sr_imask = imask;
  t.settle();
}

static void t_resolve() {
  group("resolve");
  // every source one-hot at every level against every IMASK
  {
    Tb t; t.por();
    int bad = 0;
    for (int i = 0; i < Sh4Intc::S_N; i++)
      for (int lvl = 0; lvl < 16; lvl++) {
        Sh4Intc md; md.reset(); md.src = 1u << i; set_level(md, i, lvl);
        load_regs(t, md); t.m->src = md.src;
        for (int im = 0; im < 16; im++) {
          t.m->sr_imask = im; t.step(14);
          std::string why;
          if (!same_req(t, md, im, false, false, &why)) {
            if (bad++ < 5) printf("  src %d level %d imask %d: %s\n", i, lvl, im, why.c_str());
          }
        }
      }
    check(bad == 0, "one-hot sources x level x IMASK: %d mismatches", bad);
    printf("  25 sources x 16 levels x 16 IMASK: %d mismatches\n", bad);
  }
  // measured pairs [HW excint run 1 C2]
  {
    Tb t; t.por();
    struct { int ta, wd, im; bool tmu, wdt; uint16_t want; const char *nm; } cs[] = {
      {10, 10, 6, true, true, 0x420, "equal level 10: TMU1 before WDT"},
      {10, 12, 6, true, true, 0x560, "WDT 12 / TMU1 10: WDT"},
      {12, 10, 6, true, true, 0x420, "TMU1 12 / WDT 10: TMU1"},
      {6, 0, 6, true, false, 0, "TMU1 level 6 at IMASK 6: none"},
      {7, 0, 6, true, false, 0x420, "TMU1 level 7 at IMASK 6: taken"},
    };
    for (auto &c : cs) {
      Sh4Intc md; md.reset();
      set_level(md, Sh4Intc::S_TUNI1, c.ta); set_level(md, Sh4Intc::S_ITI, c.wd);
      md.src = (c.tmu ? 1u << Sh4Intc::S_TUNI1 : 0) | (c.wdt ? 1u << Sh4Intc::S_ITI : 0);
      apply(t, md, c.im);
      t.lo();
      const bool req = t.m->irq_req; const uint16_t code = t.m->irq_code;
      t.hi();
      check(c.want ? (req && code == c.want) : !req, "%s: req %d code %03x", c.nm, req, code);
      if (g_verbose) printf("  %s: req %d code %03x\n", c.nm, req, code);
    }
    printf("  C2 pairs [HW]: TMU1 / WDT at equal level 10 -> 0x420; 12 / 10 either way -> the higher; level 6 / 7 at "
           "IMASK 6 -> none / taken\n");
  }
  // every equal-level pair: the higher irq_src_t index wins; IRL wins against every on-chip source
  {
    Tb t; t.por();
    int bad = 0;
    for (int a = 0; a < Sh4Intc::S_N; a++)
      for (int b = a + 1; b < Sh4Intc::S_N; b++) {
        int ra, rb; field_shift(a, &ra); int sa = field_shift(a, &ra), sb = field_shift(b, &rb);
        if (ra == rb && sa == sb) continue;                     // same field: covered by the order within it
        Sh4Intc md; md.reset(); set_level(md, a, 9); set_level(md, b, 9);
        md.src = (1u << a) | (1u << b);
        apply(t, md, 3);
        t.lo();
        const uint16_t code = t.m->irq_code; const bool req = t.m->irq_req;
        t.hi();
        if (!req || code != Sh4Intc::src_code(b)) bad++;
      }
    // sources sharing one field (DMAC, TMU2, RTC, SCI, SCIF, REF): the table order inside the module
    const int shared[][5] = {{Sh4Intc::S_DMTE0, Sh4Intc::S_DMTE1, Sh4Intc::S_DMTE2, Sh4Intc::S_DMTE3, Sh4Intc::S_DMAE},
                             {Sh4Intc::S_TUNI2, Sh4Intc::S_TICPI2, -1, -1, -1},
                             {Sh4Intc::S_ATI, Sh4Intc::S_PRI, Sh4Intc::S_CUI, -1, -1},
                             {Sh4Intc::S_SCI_ERI, Sh4Intc::S_SCI_RXI, Sh4Intc::S_SCI_TXI, Sh4Intc::S_SCI_TEI, -1},
                             {Sh4Intc::S_SCIF_ERI, Sh4Intc::S_SCIF_RXI, Sh4Intc::S_SCIF_BRI, Sh4Intc::S_SCIF_TXI, -1},
                             {Sh4Intc::S_RCMI, Sh4Intc::S_ROVI, -1, -1, -1}};
    for (auto &g : shared)
      for (int x = 0; x < 5 && g[x] >= 0; x++)
        for (int y = x + 1; y < 5 && g[y] >= 0; y++) {
          Sh4Intc md; md.reset(); set_level(md, g[x], 5); md.src = (1u << g[x]) | (1u << g[y]);
          apply(t, md, 0);
          t.lo(); const uint16_t code = t.m->irq_code; t.hi();
          if (code != Sh4Intc::src_code(g[x])) bad++;           // the earlier table entry (the higher bit) wins
        }
    // IRL against every on-chip source at equal level (IRL3-0 = 15 - level)
    for (int i = 0; i < Sh4Intc::S_N; i++) {
      Sh4Intc md; md.reset(); set_level(md, i, 11); md.src = 1u << i; md.irl = 15 - 11;
      apply(t, md, 2);
      t.lo(); const uint16_t code = t.m->irq_code; t.hi();
      if (code != 0x200 + 0x20 * (15 - 11)) bad++;
    }
    check(bad == 0, "equal-level ordering: %d mismatches", bad);
    printf("  equal-level order (all 25 x 24 pairs, the shared fields, IRL vs each source): %d mismatches\n", bad);
  }
  // IRL codes [HW excint2 I]; IRLM = 1 [M Table 19.4]; MAI
  {
    Tb t; t.por();
    struct { uint8_t pins; int im; bool req; uint16_t code; const char *nm; } cs[] = {
      {0xD, 1, true, 0x3A0, "Holly level 2 (IRL 1101) at IMASK 1 -> 0x3A0 [HW]"},
      {0xD, 2, false, 0, "Holly level 2 at IMASK 2 -> none [HW]"},
      {0xB, 3, true, 0x360, "Holly level 4 (IRL 1011) at IMASK 3 -> 0x360 [HW]"},
      {0x9, 5, true, 0x320, "Holly level 6 (IRL 1001) at IMASK 5 -> 0x320 [HW]"},
      {0x0, 14, true, 0x200, "IRL 0000 = level 15"},
      {0xF, 0, false, 0, "IRL 1111 = no request"},
    };
    for (auto &c : cs) {
      Sh4Intc md; md.reset(); md.irl = c.pins;
      apply(t, md, c.im);
      t.lo(); const bool req = t.m->irq_req; const uint16_t code = t.m->irq_code; t.hi();
      check(req == c.req && (!req || code == c.code), "%s: req %d code %03x", c.nm, req, code);
    }
    int bad = 0;
    for (int pins = 0; pins < 16; pins++) {
      Sh4Intc md; md.reset(); md.icr = 0x0080; md.irl = (uint8_t)pins;
      apply(t, md, 0);
      std::string why;
      if (!same_req(t, md, 0, false, false, &why)) { bad++; printf("  IRLM=1 pins %x: %s\n", pins, why.c_str()); }
    }
    check(bad == 0, "IRLM = 1: %d mismatches", bad);
    // MAI with the NMI pin low masks IRL and on-chip requests; pin high: not masked.  The pin is low from the reset
    // on and then rises (NMIE = 0: no NMI), so only MAI is seen.
    Tb u; u.m->nmi_pin = 0; u.por();
    Sh4Intc md; md.reset(); md.icr = 0x4000; md.src = 1u << Sh4Intc::S_TUNI0; set_level(md, Sh4Intc::S_TUNI0, 15);
    md.nmi_pin = 0;
    apply(u, md, 0);
    u.lo(); const bool r0 = u.m->irq_req, n0 = u.m->irq_nmi; u.hi();
    u.m->nmi_pin = 1; u.settle();
    u.lo(); const bool r1 = u.m->irq_req, n1 = u.m->irq_nmi; u.hi();
    check(!r0 && r1 && !n0 && !n1, "MAI: request %d with the NMI pin low (want 0), %d high (want 1); nmi %d %d", r0, r1,
          n0, n1);
  }
  // random states vs the model, and BL never reaches an ordinary request
  {
    Tb t; t.por();
    std::mt19937 rng(7);
    int bad = 0, blbad = 0;
    for (int k = 0; k < 3000; k++) {
      Sh4Intc md; md.reset();
      md.ipra = (uint16_t)rng(); md.iprb = (uint16_t)(rng() & 0xFFF0); md.iprc = (uint16_t)rng();
      md.icr = (uint16_t)(rng() & 0x0081);                       // IRLM, bit 0 (MAI / NMI rules: group nmi)
      md.src = rng() & ((1u << 25) - 1);
      if (rng() & 1) md.src &= rng();
      md.irl = (uint8_t)(rng() & 15);
      if (rng() % 3 == 0) md.irl = 0xF;
      const int im = rng() & 15;
      apply(t, md, im);
      std::string why;
      if (!same_req(t, md, im, false, false, &why)) { if (bad++ < 5) printf("  random %d: %s\n", k, why.c_str()); }
      t.m->sr_bl = 1; t.step(3);
      if (!same_req(t, md, im, true, false)) blbad++;
      t.m->sr_bl = 0;
    }
    check(bad == 0, "random resolver states: %d mismatches with sh4_intc.h", bad);
    check(blbad == 0, "SR.BL changed an ordinary request in %d states (BL is the core's)", blbad);
    printf("  3000 random states (IPR, 25 sources, IRL, IRLM, IMASK) vs sh4_intc.h: %d mismatches; BL: %d\n", bad,
           blbad);
  }
  // irq.code follows the winner while irq.req stays 1 (fix WI-EXC-verify finding 4: the package's "stable while req"
  // is a contract wording issue; INTEVT is the winner at acceptance [M 19.4.1], the core latches it at the take).
  // TMU1 level 10 requesting; WDT ITI rises at level 12: the code turns to 0x560 with no gap in req, 4 / 5 cycles
  // after the source (the on-chip path), and back to 0x420 when ITI drops.
  {
    Tb t; t.por();
    t.iwr(0x4, 0x0A00); t.iwr(0x8, 0xC000);
    t.m->sr_imask = 0; t.m->src = 1u << Sh4Intc::S_TUNI1; t.settle();
    int gaps = 0, up = -1, down = -1;
    t.m->src = (1u << Sh4Intc::S_TUNI1) | (1u << Sh4Intc::S_ITI);
    for (int c = 0; c < 20; c++) {
      t.lo(); if (!t.m->irq_req) gaps++; if (up < 0 && t.m->irq_code == 0x560) up = c; t.hi();
    }
    t.m->src = 1u << Sh4Intc::S_TUNI1;
    for (int c = 0; c < 20; c++) {
      t.lo(); if (!t.m->irq_req) gaps++; if (down < 0 && t.m->irq_code == 0x420) down = c; t.hi();
    }
    printf("  code follows the winner: ITI (12) over TMU1 (10) -> 0x560 after %d, back to 0x420 after %d, req gaps %d\n",
           up, down, gaps);
    check(gaps == 0 && up >= 4 && up <= 5 && down >= 4 && down <= 5,
          "irq.code must follow the winner with req held (gaps %d, 0x560 after %d, 0x420 after %d; want 0, 4-5, 4-5)",
          gaps, up, down);
  }
}

// =====================================================================================================================
// imask: the SOC.md 3.4 port-level target
// =====================================================================================================================
// A TMU1 request at level 15 pending with IMASK `from`; IMASK becomes `to` in cycle s (the S-stage write-through) whose
// CPG phase is p; returns L = the first cycle >= s with irq.req == (to < 15), minus s.
static int imask_latency(int p, int from, int to, int lvl = 15, bool bl_noise = false) {
  Tb t; t.por();
  t.iwr(0x4, (uint16_t)(lvl << 8));
  t.m->src = 1u << Sh4Intc::S_TUNI1; t.m->sr_imask = from;
  t.settle();
  t.run_to_ph(p);
  t.m->sr_imask = to;
  const bool want = lvl > to;
  for (int c = 0; c < 40; c++) {
    if (bl_noise) t.m->sr_bl = (c * 7 + 3) % 5 == 0;
    t.lo();
    if ((bool)t.m->irq_req == want) return c;
    t.hi();
  }
  return -1;
}

static void t_imask() {
  group("imask");
  int bad = 0;
  for (int p = 0; p < 4; p++) {
    const int want = (p & 1) ? 10 : 9;
    const int dn = imask_latency(p, 15, 6), dn2 = imask_latency(p, 15, 14), up = imask_latency(p, 6, 15);
    const int up2 = imask_latency(p, 6, 14, 10);            // level 10: 10 > 6 before, 10 <= 14 after
    const int bl = imask_latency(p, 15, 6, 15, true);
    printf("  s at ph %d: IMASK 15->6 %d, 15->14 %d, with BL toggling %d (want %d); raise 6->15 %d, 6->14 at level 10 %d "
           "(want 1)\n", p, dn, dn2, bl, want, up, up2);
    if (dn != want || dn2 != want || bl != want || up != 1 || up2 != 1) bad++;
  }
  check(bad == 0, "IMASK path: a lowering reaches irq.req at s + 9 (s even) / s + 10 (s odd), a raise masks from s + 1");
  // The raise masks at once (SOC.md 3.4, I014) [HW ci2 F nrec = 2, ci2 irqloop budget_left = 9, excint C1i]: irq.req
  // (and `wake`) = the output register's request & (its level > the Iclk input register's IMASK).  At every phase of s:
  //   (a) TMU1 level 10 presented (settled) with IMASK 6; IMASK -> 15 in cycle s: irq.req still 1 in s (the input
  //       register holds the old mask), 0 from s + 1 and never again (the synchroniser drops rq itself at s + 9 / 10);
  //       `wake` (asleep, BL = 1) the same;
  //   (b) TMU1 rises at s - 2 with IMASK 6 (in flight in R1 / R2 when IMASK -> 15 in s): never presented (a raise
  //       delayed like the lowering presents it from s + 2 / 3 to s + 8 / 9);
  //   (c) a raise-and-lower pulse: IMASK 6 -> 15 in s, back to 6 in s + 3.  The request is masked in s + 1 .. s + 3 and
  //       back from s + 4 (rq still holds: the synchroniser has not yet carried the 15 through R2), then drops while
  //       R2's comparison with 15 is in the output register (from s + 9 / 10) and returns with the lowering's own
  //       s + 3 + 9 / 10.  This pins the compare to the live output register against im_i, not to a second delayed path.
  int wbad = 0;
  for (int p = 0; p < 4; p++) {
    Tb t; t.por();
    t.iwr(0x4, 0x0A00);
    t.m->src = 1u << Sh4Intc::S_TUNI1; t.m->sr_imask = 6; t.m->sr_bl = 1; t.m->asleep = 1; t.settle();
    t.run_to_ph(p);
    t.m->sr_imask = 15;
    std::string rq, wk;
    for (int c = 0; c < 20; c++) { t.lo(); rq += t.m->irq_req ? '1' : '0'; wk += t.m->wake ? '1' : '0'; t.hi(); }
    const std::string want = "1" + std::string(19, '0');
    printf("  (a) IMASK 6->15 in s (ph %d), TMU1 level 10 presented: irq.req s..s+19 %s, wake %s (want %s)\n", p,
           rq.c_str(), wk.c_str(), want.c_str());
    if (rq != want || wk != want) wbad++;
  }
  for (int p = 0; p < 4; p++) {
    Tb t; t.por();
    t.iwr(0x4, 0x0A00);
    t.m->sr_imask = 6; t.settle();
    t.run_to_ph(p);
    t.m->src = 1u << Sh4Intc::S_TUNI1;
    t.step(2);
    t.m->sr_imask = 15;
    int seen = -1;
    for (int c = 0; c < 20; c++) { t.lo(); if (seen < 0 && t.m->irq_req) seen = c; t.hi(); }
    printf("  (b) IMASK 6->15 in s (ph %d), TMU1 level 10 rose at s - 2: first irq.req at s + %d (-1 = never; want -1)\n",
           (p + 2) & 3, seen);
    if (seen != -1) wbad++;
  }
  for (int p = 0; p < 4; p++) {
    Tb t; t.por();
    t.iwr(0x4, 0x0A00);
    t.m->src = 1u << Sh4Intc::S_TUNI1; t.m->sr_imask = 6; t.settle();
    t.run_to_ph(p);
    std::string rq;
    for (int c = 0; c < 20; c++) {
      t.m->sr_imask = (c >= 0 && c < 3) ? 15 : 6;
      t.lo(); rq += t.m->irq_req ? '1' : '0'; t.hi();
    }
    // s = c 0 (ph p); the lowering in c 3 (ph p + 3): the 15 reaches rq at 9 (p even) / 10 (p odd), the 6 at 3 + 9 / 10
    const int down = (p & 1) ? 10 : 9, up = 3 + (((p + 3) & 1) ? 10 : 9);
    std::string want;
    for (int c = 0; c < 20; c++) want += (c == 0 || (c >= 4 && c < down) || c >= up) ? '1' : '0';
    printf("  (c) IMASK 6->15 in s (ph %d), back to 6 in s+3: irq.req s..s+19 %s (want %s)\n", p, rq.c_str(),
           want.c_str());
    if (rq != want) wbad++;
  }
  check(wbad == 0, "IMASK raise: masked from s + 1 (irq.req and wake), an in-flight request never presented, the "
        "compare against the input register");
}

// =====================================================================================================================
// progs: the console programs replayed on the INTC
// =====================================================================================================================
// The boundary tables (offset from s, label) of each console program on v1's schedule live in tb/v1_intc_progs.h,
// shared with the schedule tool (`make v1-intc-sched`), which re-derives them from the current v1 core's trace of each
// program's own code (the console ELFs) and fails when they are stale.
// replay: TMU1 at level 15 pending, IMASK 15 -> 14 in cycle s at phase p; the label of the first boundary with irq.req
static int replay(const std::vector<Bnd> &b, int p) {
  Tb t; t.por();
  t.iwr(0x4, 0x0F00);
  t.m->src = 1u << Sh4Intc::S_TUNI1; t.m->sr_imask = 15;
  t.settle();
  t.run_to_ph(p);
  t.m->sr_imask = 14;
  std::vector<bool> req;
  for (int c = 0; c < 64; c++) { t.lo(); req.push_back(t.m->irq_req); t.hi(); }
  for (const Bnd &x : b) if (x.off < 64 && req[x.off]) return x.label;
  return -99;
}

static void t_progs() {
  group("progs");
  // ---- warm cases with an unknown phase: the measured value must be one the structure gives for some phase of s
  const std::vector<Prog> progs = intc_progs();
  int bad = 0;
  for (const Prog &pg : progs) {
    std::set<int> out;
    int by_ph[4];
    for (int p = 0; p < 4; p++) { by_ph[p] = replay(pg.b, p); out.insert(by_ph[p]); }
    bool ok = out.size() == 2;                                  // two outcomes: s + 9 or s + 10
    for (int v : pg.meas) ok = ok && out.count(v);
    printf("  %-40s model by ph 0..3: %d %d %d %d   console: %d%s  %s\n", pg.name, by_ph[0], by_ph[1], by_ph[2],
           by_ph[3], pg.meas[0], pg.meas.size() > 1 ? " (all trials)" : "", ok ? "ok" : "MISMATCH");
    if (!ok) bad++;
  }
  check(bad == 0, "%d console programs not reproduced", bad);

  // ---- the excint2 PH sweep: the phase is fixed by the TCNT1 read.  PH(n) = ldc r6,sr ; mov #0,r0 ; mov.l @r5,r3
  // (TCNT1, Pclk 32-bit) ; n x add #1,r3 ; ldc r4,sr (IMASK 15 -> 6, TMU1 level 15 pending) ; L: 32 x add #1,r0.
  // v1's schedule (tb/v1_intc_progs.h, traced): s = m + PH_SM[n], m = the TCNT1 read's ack cycle; the adds after L
  // issue at s + PH_FIRST + k.  v1_p4bus (P_D2 = 11) acks a Pclk read 11 cycles after its last transfer (a pce cycle,
  // ph 3), so m is at ph 2.  j = the index of the first add on the next 32-byte instruction-cache line (PH_L[n]).
  // A console value that is not the warm model value must be an IC artefact: cut at the line boundary (v = j < k),
  // or 0 when the next line is 1-4 adds away (its miss started before LDC SR's refetch, which waits for it).
  {
    const int M_PH = 2;
    const int meas[12][2] = {{7, 7}, {0, 7}, {8, 8}, {0, 7}, {8, 8}, {7, 7}, {0, 0}, {0, 0}, {6, 6}, {7, 7}, {7, 8},
                             {6, 6}};
    int warm_exact = 0, ic_cut = 0, ic_block = 0, wrong = 0;
    printf("  excint2 PH sweep (console k per n, two trials; model = warm k with the TCNT1 ack at ph %d):\n", M_PH);
    for (int n = 0; n < 12; n++) {
      const int sm = PH_SM[n];
      const int p = (M_PH + sm) & 3;
      const int k = replay(chain(PH_FIRST, 32), p);
      const int j = (int)(32 - (PH_L[n] & 31)) / 2;
      std::string cls;
      for (int r = 0; r < 2; r++) {
        const int v = meas[n][r];
        const char *c;
        if (v == k) { c = "warm"; warm_exact++; }
        else if (j < k && v == j) { c = "cut"; ic_cut++; }
        else if (j <= 4 && v == 0) { c = "block"; ic_block++; }
        else { c = "WRONG"; wrong++; }
        cls += std::string(r ? " / " : "") + c;
      }
      printf("    n=%-2d s-m=%-2d model k=%d  line at add %-2d  console %d/%d  (%s)\n", n, sm, k, j, meas[n][0],
             meas[n][1], cls.c_str());
    }
    printf("    %d warm-exact, %d cut at a line boundary, %d blocked refetch, %d unexplained\n", warm_exact, ic_cut,
           ic_block, wrong);
    check(wrong == 0, "PH sweep: %d console values neither the warm model value nor an IC artefact", wrong);
    check(warm_exact >= 12, "PH sweep: only %d warm-exact values", warm_exact);
  }
}

// =====================================================================================================================
// timing: on-chip source and IPR write paths
// =====================================================================================================================
static void t_timing() {
  group("timing");
  int bad = 0;
  for (int p = 0; p < 4; p++) {
    Tb t; t.por();
    t.iwr(0x4, 0x0F00); t.m->sr_imask = 0; t.settle();
    t.run_to_ph(p);
    t.m->src = 1u << Sh4Intc::S_TUNI1;                         // the source rises in cycle e (ph p)
    int L = -1;
    for (int c = 0; c < 30 && L < 0; c++) { t.lo(); if (t.m->irq_req) L = c; t.hi(); }
    const int want = (p & 1) ? 4 : 5;
    printf("  on-chip source rising at ph %d -> irq.req after %d (want %d; 1 Icyc + 2 Bcyc = 5 [M])\n", p, L, want);
    if (L != want) bad++;
  }
  // the falling edge through the same registers: WDT ITI (level 12) requesting over a pending TMU1 at level 10 with
  // IMASK 11; ITI drops in cycle f: irq.req falls at f + 4 / 5, the age of rq (the raise compare's level rl rides with
  // rq in the output register, so a lower winner behind it cannot cut the request short)
  for (int p = 0; p < 4; p++) {
    Tb t; t.por();
    t.iwr(0x4, 0x0A00); t.iwr(0x8, 0xC000);
    t.m->sr_imask = 11; t.m->src = (1u << Sh4Intc::S_TUNI1) | (1u << Sh4Intc::S_ITI); t.settle();
    t.run_to_ph(p);
    t.m->src = 1u << Sh4Intc::S_TUNI1;                         // ITI drops in cycle f (ph p); TMU1 (10) stays masked
    int L = -1;
    for (int c = 0; c < 30 && L < 0; c++) { t.lo(); if (!t.m->irq_req) L = c; t.hi(); }
    const int want = (p & 1) ? 4 : 5;
    printf("  on-chip source dropping at ph %d, the next winner at or below IMASK -> irq.req falls after %d (want %d)\n",
           p, L, want);
    if (L != want) bad++;
  }
  {  // an IPR write strobe (pce) enabling a pending source: irq.req 6 cycles after the strobe
    Tb t; t.por();
    t.m->src = 1u << Sh4Intc::S_TUNI1; t.m->sr_imask = 0; t.settle();
    t.run_to_ph(3);
    t.m->p4_wr = 1; t.m->p4_size = 1; t.m->p4_off = 0x0FD00004; t.m->p4_wdata = 0x0F00; t.strobing = true;
    t.lo(); t.hi(); t.strobing = false; t.m->p4_wr = 0;
    int L = -1;
    for (int c = 1; c < 30 && L < 0; c++) { t.lo(); if (t.m->irq_req) L = c; t.hi(); }
    printf("  IPRA write strobe (pce) enabling a pending source -> irq.req after %d (want 6)\n", L);
    if (L != 6) bad++;
  }
  // `wake` (asleep, BL = 1): 4 / 5 cycles after an on-chip source, always in a ph-1 / ph-3 cycle, never in the cycle
  // right after a pce (ph 0) -- the edge v1_cpg's RESYNC counts its 2 Pcyc from (WI-EXC-verify finding 10, WI-CLK)
  for (int p = 0; p < 4; p++) {
    Tb t; t.por(); t.iwr(0x4, 0x0F00); t.m->sr_imask = 0; t.m->sr_bl = 1; t.m->asleep = 1; t.settle();
    t.run_to_ph(p);
    t.m->src = 1u << Sh4Intc::S_TUNI1;
    int L = -1, wph = -1;
    for (int c = 0; c < 30 && L < 0; c++) { t.lo(); if (t.m->wake) { L = c; wph = t.ph; } t.hi(); }
    const int want = (p & 1) ? 4 : 5, want_ph = (p < 2) ? 1 : 3;
    printf("  wake: source at ph %d -> wake after %d, in a ph-%d cycle (want %d, ph %d)\n", p, L, wph, want, want_ph);
    if (L != want || wph != want_ph) bad++;
  }
  check(bad == 0, "source / IPR-write request timing, wake edge phase");
}

// =====================================================================================================================
// irl: noise canceller, decision time, IRLM = 1
// =====================================================================================================================
static void t_irl() {
  group("irl");
  int bad = 0;
  // decision time from a pin change in cycle e: 14 (e odd, a bce cycle) / 15
  for (int p = 0; p < 4; p++) {
    Tb t; t.por(); t.m->sr_imask = 0; t.settle();
    t.run_to_ph(p);
    t.m->irl_n = 0x9;                                          // level 6
    int L = -1;
    for (int c = 0; c < 40 && L < 0; c++) { t.lo(); if (t.m->irq_req) L = c; t.hi(); }
    const int want = (p & 1) ? 14 : 15;
    printf("  IRL change at ph %d -> irq.req after %d (want %d; 1 Icyc + 7 Bcyc = 15 [M])\n", p, L, want);
    if (L != want) bad++;
  }
  // pulses: a level must be sampled on three consecutive Bcyc to count [M 19.2.2]
  for (int len = 1; len <= 8; len++)
    for (int p = 0; p < 4; p++) {
      Tb t; t.por(); t.m->sr_imask = 0; t.settle();
      t.run_to_ph(p);
      int samples = 0;
      bool seen = false;
      for (int c = 0; c < 60; c++) {
        t.m->irl_n = c < len ? 0x9 : 0xF;
        t.lo();
        if (c < len && t.m->bce) samples++;
        if (t.m->irq_req) seen = true;
        t.hi();
      }
      const bool want = samples >= 3;
      if (seen != want) { bad++; printf("  IRL pulse of %d cycles from ph %d (%d samples): seen %d\n", len, p, samples, seen); }
    }
  printf("  pulses of 1-8 Icyc at every phase: recognised exactly when three bce samples see them\n");
  // a changing level: two levels alternating every 2 Bcyc never settle; the old value stays
  {
    Tb t; t.por(); t.m->sr_imask = 0; t.m->irl_n = 0xB; t.settle();   // level 4 recognised
    for (int c = 0; c < 64; c++) { t.m->irl_n = (c / 4) & 1 ? 0x9 : 0x5; t.step(); }
    t.lo(); const uint16_t code = t.m->irq_code; const bool req = t.m->irq_req; t.hi();
    check(req && code == 0x360, "IRL toggling every 2 Bcyc must leave the last stable level (0x360): %03x", code);
  }
  // IRLM = 1: four independent requests, one canceller per pin [P] (WI-EXC-verify finding 8).  IRL0 held low (level
  // 13 > IMASK 12) while IRL3 toggles every 2 Bcyc: IRL0 is recognised at its own decision time (14 / 15); with IRLM = 0
  // the same pins are one encoded level that never holds three samples, so nothing new is seen
  {
    int L[2] = {-1, -1};
    for (int irlm = 0; irlm < 2; irlm++) {
      Tb t; t.por();
      t.iwr(0x0, irlm ? 0x0080 : 0x0000);
      t.m->sr_imask = 12; t.settle();
      t.run_to_ph(0);
      for (int c = 0; c < 400; c++) {
        t.m->irl_n = (uint8_t)(0xE & ((c / 4) & 1 ? 0x7 : 0xF));
        t.lo(); if (L[irlm] < 0 && t.m->irq_req) L[irlm] = c; t.hi();
      }
    }
    printf("  IRL0 steady low, IRL3 toggling every 2 Bcyc (IMASK 12): IRLM = 1 request after %d (want 15), IRLM = 0 %d "
           "(want none: -1)\n", L[1], L[0]);
    check(L[1] == 15 && L[0] == -1, "IRLM = 1 per-pin canceller: IRL0 after %d (want 15), IRLM = 0: %d (want -1)", L[1],
          L[0]);
    // per pin, the canceller still rejects a glitch: IRL1 low for 2 bce samples while IRL0 toggles -> no level 10
    Tb t; t.por(); t.iwr(0x0, 0x0080); t.m->sr_imask = 9; t.settle();
    t.run_to_ph(0);
    bool seen = false;
    for (int c = 0; c < 200; c++) {
      const uint8_t irl1 = (c >= 20 && c < 24) ? 0 : 1;          // low in cycles 20-23: 2 bce samples (ph 1, 3)
      t.m->irl_n = (uint8_t)(0xC | (irl1 << 1) | ((c / 2) & 1));
      t.lo(); seen |= t.m->irq_req; t.hi();
    }
    check(!seen, "IRLM = 1: a 2-sample IRL1 glitch was recognised");
  }
  check(bad == 0, "IRL filter / decision time");
}

// =====================================================================================================================
// nmi
// =====================================================================================================================
static void t_nmi() {
  group("nmi");
  int bad = 0;
  // decision time and edge select
  for (int nmie = 0; nmie < 2; nmie++)
    for (int p = 0; p < 4; p++) {
      Tb t; t.m->nmi_pin = !nmie; t.por();
      t.iwr(0x0, nmie ? 0x0100 : 0x0000);
      t.step(20);                                              // past the NMIE blind window
      t.run_to_ph(p);
      t.m->nmi_pin = nmie;                                     // the selected edge in cycle e
      int L = -1, evts = 0;
      for (int c = 0; c < 30; c++) { t.lo(); if (t.m->nmi_evt) evts++; if (L < 0 && t.m->irq_req) L = c; t.hi(); }
      t.lo(); const bool nmi = t.m->irq_nmi; const uint16_t code = t.m->irq_code; t.hi();
      const int want = (p & 1) ? 8 : 9;
      if (L != want || !nmi || code != 0x1C0 || evts != 1) {
        bad++;
        printf("  NMIE=%d edge at ph %d: L %d (want %d) nmi %d code %03x nmi_evt pulses %d\n", nmie, p, L, want, nmi,
               code, evts);
      }
    }
  printf("  NMI edge (NMIE 0 falling / 1 rising, every phase) -> irq.req after 8 / 9 (1 Icyc + 4 Bcyc = 9 [M]), code "
         "0x1C0, one nmi_evt\n");
  {  // the other edge is ignored
    Tb t; t.m->nmi_pin = 0; t.por(); t.step(20);
    t.m->nmi_pin = 1; t.step(30);                              // rising with NMIE = 0
    t.lo(); const bool n = t.m->irq_nmi; t.hi();
    check(!n, "a rising edge with NMIE = 0 was detected");
  }
  {  // BL / NMIB / sleep, and nmi_ack
    Tb t; t.m->nmi_pin = 1; t.por(); t.step(20);
    t.m->sr_bl = 1; t.m->nmi_pin = 0; t.step(30);
    t.lo(); const bool r_bl = t.m->irq_req, pend = t.m->irq_nmi; t.hi();
    t.m->sr_bl = 0; t.lo(); const bool r_now = t.m->irq_req; t.hi();         // BL cleared: at once
    t.m->sr_bl = 1; t.m->asleep = 1; t.lo(); const bool r_sleep = t.m->irq_req; t.hi();
    t.m->asleep = 0;
    t.iwr(0x0, 0x0200); t.lo(); const bool r_nmib = t.m->irq_req; t.hi();   // NMIB = 1: accepted with BL = 1
    check(!r_bl && pend, "NMI with BL = 1, NMIB = 0 must be held pending (req 0, nmi 1): req %d nmi %d", r_bl, pend);
    check(r_now, "NMI must be requested in the same cycle BL clears");
    check(r_sleep, "NMI must be accepted while asleep with BL = 1");
    check(r_nmib, "NMI must be accepted with BL = 1 and NMIB = 1");
    t.m->nmi_ack = 1; t.step(); t.m->nmi_ack = 0;
    t.lo(); const bool after = t.m->irq_nmi; t.hi();
    const uint32_t icr = t.ird(0x0);
    check(!after && !(icr & 0x0200), "nmi_ack must clear the latch (nmi %d) and ICR.NMIB (ICR %04x)", after, icr);
    t.step(30);
    t.lo(); const bool again = t.m->irq_nmi; t.hi();
    check(!again, "one edge, one NMI: it came back after nmi_ack");
  }
  {  // the NMIE blind window: a write changing NMIE blanks the detector for 6 of its (bce) cycles [M 19.2.1]
    int first_seen = -1, last_blind = -1;
    for (int d = 0; d < 24; d++) {                             // edge d cycles after the NMIE write strobe
      Tb t; t.m->nmi_pin = 0; t.por(); t.step(20);
      t.iwr(0x0, 0x0100);                                      // NMIE 0 -> 1 (strobe in a pce cycle)
      t.step(d);
      t.m->nmi_pin = 1;
      t.step(30);
      t.lo(); const bool n = t.m->irq_nmi; t.hi();
      if (n && first_seen < 0) first_seen = d;
      if (!n) last_blind = d;
    }
    printf("  NMIE 0 -> 1 then a rising edge d cycles after the write: missed for d <= %d, seen from d = %d\n",
           last_blind, first_seen);
    // the pin is sampled at bce (n1 n2); the edge reaches the detector 2-3 cycles later; 6 detector cycles are blind
    check(first_seen > 0 && last_blind == first_seen - 1 && first_seen >= 8 && first_seen <= 12,
          "NMIE blind window: first detected edge at d = %d (want a 6-Bcyc window after the write)", first_seen);
    Tb t; t.m->nmi_pin = 1; t.por(); t.step(20);
    t.iwr(0x0, 0x0000);                                        // a write that does not change NMIE: no blind window
    t.m->nmi_pin = 0; t.step(30);
    t.lo(); const bool n = t.m->irq_nmi; t.hi();
    check(n, "an ICR write that keeps NMIE must not blank the detector");
  }
  {  // MAI: NMI edges are still accepted in normal operation [M 19.4.3]
    Tb t; t.m->nmi_pin = 1; t.por(); t.iwr(0x0, 0x4000); t.step(20);
    t.m->nmi_pin = 0; t.step(30);
    t.lo(); const bool n = t.m->irq_nmi, r = t.m->irq_req; t.hi();
    check(n && r, "MAI = 1: a falling NMI edge must still be taken (nmi %d req %d)", n, r);
  }
  // MAI = 1 in standby: a pin transition generates no NMI [M 19.4.3, hm.txt 27231-27236] (WI-EXC-verify finding 2).
  // Pin 1 -> 0 -> 1 in standby, NMIE 0 (the falling edge) and NMIE 1 (the rising edge): wake_stby stays 0 while the pin
  // is low and after it rises again, nmi_evt never pulses; and the model agrees.
  for (int nmie = 0; nmie < 2; nmie++) {
    Tb t; t.m->nmi_pin = 1; t.por();
    t.iwr(0x0, 0x4000 | (nmie ? 0x0100 : 0));
    t.step(40);
    t.standby = true; t.m->stby = 1; t.step(10);
    Sh4Intc md; md.reset(); md.icr = (uint16_t)(0x4000 | (nmie ? 0x0100 : 0));
    bool w_low = false, w_high = false; int evts = 0;
    t.m->nmi_pin = 0; md.set_nmi_pin(false, true);
    for (int c = 0; c < 40; c++) { t.lo(); w_low |= t.m->wake_stby; evts += t.m->nmi_evt; t.hi(); }
    t.m->nmi_pin = 1; md.set_nmi_pin(true, true);
    for (int c = 0; c < 40; c++) { t.lo(); w_high |= t.m->wake_stby; evts += t.m->nmi_evt; t.hi(); }
    printf("  standby, MAI = 1, NMIE = %d, pin 1->0->1: wake_stby low %d / high %d, nmi_evt %d, model pending %d (want "
           "0 0 0 0)\n", nmie, w_low, w_high, evts, md.nmi_pend);
    check(!w_low && !w_high && evts == 0 && !md.nmi_pend && !md.wake_stby(15),
          "MAI = 1 in standby: an NMI pin transition must generate no NMI (NMIE %d: wake_stby %d / %d, nmi_evt %d, "
          "model %d)", nmie, w_low, w_high, evts, md.nmi_pend);
  }
  {  // ... and it is not taken later either: the edge in standby, then standby ends by the RTC alarm (ATI 0x480)
    Tb t; t.m->nmi_pin = 1; t.por();
    t.iwr(0x0, 0x4000); t.iwr(0x4, 0x000F);
    t.m->sr_imask = 0; t.m->sr_bl = 1; t.step(40);
    t.standby = true; t.m->stby = 1;
    int evts = 0;
    t.m->nmi_pin = 0;
    for (int c = 0; c < 10; c++) { t.lo(); evts += t.m->nmi_evt; t.hi(); }
    t.m->nmi_pin = 1;
    for (int c = 0; c < 4; c++) { t.lo(); evts += t.m->nmi_evt; t.hi(); }
    t.m->src = 1u << Sh4Intc::S_ATI;
    t.lo(); const bool wk = t.m->wake_stby; t.hi();
    t.standby = false; t.m->stby = 0; t.m->asleep = 1;
    t.step(40);
    t.lo(); const bool nmi = t.m->irq_nmi, req = t.m->irq_req; const uint16_t code = t.m->irq_code; t.hi();
    check(wk && !nmi && req && code == 0x480 && evts == 0,
          "MAI standby edge then an ATI exit: wake_stby %d, then nmi %d req %d code %03x, nmi_evt %d (want 1, 0 1 480, "
          "0)", wk, nmi, req, code, evts);
    // MAI = 0: the same edge in standby is an NMI and wakes (the gate is MAI's, not standby's)
    Tb u; u.m->nmi_pin = 1; u.por(); u.step(40);
    u.standby = true; u.m->stby = 1;
    u.m->nmi_pin = 0; int ev0 = 0; bool w0 = false;
    for (int c = 0; c < 20; c++) { u.lo(); ev0 += u.m->nmi_evt; w0 |= u.m->wake_stby; u.hi(); }
    check(w0 && ev0 == 1, "MAI = 0 in standby: a falling NMI edge must wake (wake_stby %d, nmi_evt %d)", w0, ev0);
  }
  {  // an ICR write of NMIB = 1 strobed in the nmi_ack cycle: acceptance wins, NMIB reads 0 [P] (finding 9)
    Tb t; t.m->nmi_pin = 1; t.por(); t.step(20);
    t.m->nmi_pin = 0; t.step(30);
    t.run_to_ph(3);
    t.m->nmi_ack = 1;
    t.m->p4_sel_exc = 0; t.m->p4_wr = 1; t.m->p4_size = 1; t.m->p4_off = 0x0FD00000; t.m->p4_wdata = 0x0200;
    t.strobing = true; t.step(); t.strobing = false;
    t.m->nmi_ack = 0; t.m->p4_wr = 0;
    const uint32_t icr = t.ird(0x0);
    check(!(icr & 0x0200), "NMIB written in the nmi_ack cycle: ICR %04x (want NMIB 0: the acceptance clears it) [P]",
          icr);
  }
  check(bad == 0, "NMI decision time / edge select");
}

// =====================================================================================================================
// wake: sleep and standby
// =====================================================================================================================
static void t_wake() {
  group("wake");
  {  // sleep: wake follows the request above IMASK whatever BL is
    Tb t; t.por(); t.iwr(0x4, 0xF000); t.m->sr_imask = 14; t.m->sr_bl = 1; t.m->asleep = 1; t.settle();
    t.lo(); const bool w0 = t.m->wake; t.hi();
    t.m->src = 1u << Sh4Intc::S_TUNI0; t.settle();
    t.lo(); const bool w1 = t.m->wake, r1 = t.m->irq_req; t.hi();
    t.m->sr_imask = 15; t.settle();
    t.lo(); const bool w2 = t.m->wake; t.hi();
    check(!w0 && w1 && r1 && !w2, "wake: none %d, level 15 > IMASK 14 with BL = 1 %d (req %d), at IMASK 15 %d", w0, w1,
          r1, w2);
  }
  auto standby = [](Tb &t) { t.standby = true; t.m->stby = 1; };
  {  // standby: IRL is sampled on rtc_tick only
    Tb t; t.por(); t.m->sr_imask = 5; t.settle(); standby(t);
    t.m->irl_n = 0x9;                                          // level 6 > 5
    t.step(200);
    t.lo(); const bool no_tick = t.m->wake_stby; t.hi();
    int ticks = 0;
    bool with_tick = false;
    for (int c = 0; c < 800 && !with_tick; c++) {              // one 32 kHz tick every 64 Icyc (compressed)
      t.m->rtc_tick = c % 64 == 0; if (t.m->rtc_tick) ticks++;
      t.lo(); with_tick = t.m->wake_stby; t.hi();
    }
    t.m->rtc_tick = 0;
    printf("  standby: IRL level 6 at IMASK 5: wake_stby %d without rtc_tick, %d after %d ticks\n", no_tick,
           with_tick, ticks);
    check(!no_tick && with_tick && ticks >= 5 && ticks <= 6,
          "standby IRL on rtc_tick: no tick %d, tick %d after %d ticks (want 2-flop sync + 3 samples)", no_tick,
          with_tick, ticks);
  }
  {  // standby: IRL at or below IMASK does not wake; RTC ATI wakes; WDT ITI does not
    Tb t; t.por(); t.iwr(0x4, 0x0008); t.iwr(0x8, 0xF000); t.m->sr_imask = 6; t.settle(); standby(t);
    t.m->src = 1u << Sh4Intc::S_ITI; t.step(10);
    t.lo(); const bool iti = t.m->wake_stby; t.hi();
    t.m->src = 1u << Sh4Intc::S_ATI; t.step(2);
    t.lo(); const bool ati = t.m->wake_stby; t.hi();
    t.m->src = 0; t.m->irl_n = 0x9;                            // level 6 = IMASK 6
    for (int c = 0; c < 10; c++) { t.m->rtc_tick = 1; t.step(); t.m->rtc_tick = 0; t.step(5); }
    t.lo(); const bool irl_eq = t.m->wake_stby; t.hi();
    check(!iti && ati && !irl_eq, "standby wake: ITI %d (want 0), ATI level 8 > 6 %d (want 1), IRL level 6 at IMASK 6 "
          "%d (want 0)", iti, ati, irl_eq);
  }
  {  // standby: NMI; MAI with the pin low masks it there
    Tb t; t.m->nmi_pin = 1; t.por(); t.step(20); standby(t);
    t.m->nmi_pin = 0; t.step(8);
    t.lo(); const bool nmi = t.m->wake_stby; t.hi();
    Tb u; u.m->nmi_pin = 1; u.por(); u.iwr(0x0, 0x4000); u.step(20); standby(u);
    u.m->nmi_pin = 0; u.step(8);
    u.lo(); const bool mai = u.m->wake_stby; u.hi();
    check(nmi && !mai, "standby NMI: wake_stby %d (want 1), with MAI and the pin low %d (want 0)", nmi, mai);
    Tb v; v.por(); v.m->sr_imask = 0; v.iwr(0x4, 0x000F); v.settle();
    v.m->src = 1u << Sh4Intc::S_ATI; v.step(3);
    v.lo(); const bool not_stby = v.m->wake_stby; v.hi();
    check(!not_stby, "wake_stby must be 0 outside standby");
  }
}

// =====================================================================================================================
// excregs
// =====================================================================================================================
static void t_excregs() {
  group("excregs");
  Tb t; t.por();
  check(t.erd(0x24) == 0x000 && t.erd(0x20) == 0 && t.erd(0x28) == 0, "power-on: EXPEVT 000, TRA / INTEVT 0");
  t.ewr(0x20, 0xFFFFFFFF); t.ewr(0x24, 0xFFFFFFFF); t.ewr(0x28, 0xFFFFFFFF);
  uint32_t a = t.erd(0x20), b = t.erd(0x24), c = t.erd(0x28);
  printf("  masks after 0xFFFFFFFF: TRA %08x EXPEVT %08x INTEVT %08x\n", a, b, c);
  check(a == 0x3FC && b == 0xFFF && c == 0xFFF, "masks TRA %x EXPEVT %x INTEVT %x (want 3FC FFF FFF [HW])", a, b, c);
  bool hit;
  uint32_t v = t.erd(0x24, 1, &hit); check(v == 0 && hit, "EXPEVT word read: %x hit %d (size rule)", v, hit);
  v = t.erd(0x24, 0, &hit); check(v == 0 && hit, "EXPEVT byte read: %x hit %d (size rule)", v, hit);
  t.ewr(0x24, 0x123, 1); t.ewr(0x24, 0x45, 0);
  check(t.erd(0x24) == 0xFFF, "EXPEVT took a write of another size");
  check(t.erd(0x1024) == 0xFFF && t.erd(0x3FF28) == 0xFFF, "the CCN window repeats every 0x100 (a[17:8] ignored)");
  // excw: general exception -> EXPEVT only; interrupt -> INTEVT only [HW]; TRAPA -> TRA
  auto excw = [&](bool vld, bool intr, uint16_t code, bool trav, uint8_t tra, bool teav) {
    t.m->excw_v = vld; t.m->excw_intr = intr; t.m->excw_code = code; t.m->excw_tra_v = trav; t.m->excw_tra = tra;
    t.m->excw_tea_v = teav; t.m->excw_tea = 0x8C001234;
    t.step();
    t.m->excw_v = t.m->excw_intr = t.m->excw_tra_v = t.m->excw_tea_v = 0;
  };
  t.ewr(0x24, 0x060); t.ewr(0x28, 0x400); t.ewr(0x20, 0x080);   // the KOS / dcload leftovers [HW]
  excw(true, false, 0x160, true, 0x5A, false);
  a = t.erd(0x20); b = t.erd(0x24); c = t.erd(0x28);
  check(a == 0x168 && b == 0x160 && c == 0x400, "TRAPA #5A: TRA %x EXPEVT %x INTEVT %x (want 168 160 400 [HW])", a, b, c);
  excw(true, true, 0x420, false, 0, false);
  b = t.erd(0x24); c = t.erd(0x28);
  check(b == 0x160 && c == 0x420, "interrupt: EXPEVT %x (kept) INTEVT %x (want 160 420 [HW])", b, c);
  excw(true, false, 0x0E0, false, 0, true);
  a = t.erd(0x20); b = t.erd(0x24); c = t.erd(0x28);
  check(a == 0x168 && b == 0x0E0 && c == 0x420, "address error: TRA %x EXPEVT %x INTEVT %x", a, b, c);
  excw(false, false, 0x000, false, 0, true);                    // multiple hit: TEA only (v1_mmu)
  check(t.erd(0x24) == 0x0E0, "excw with v = 0 must not write EXPEVT");
  // resets: CPU-only (BL = 1 exception) 020, multiple hit 140, manual 020, TRA / INTEVT held; power-on clears
  t.reset(2, 1);
  check(t.erd(0x24) == 0x020 && t.erd(0x20) == 0x168 && t.erd(0x28) == 0x420, "CPU-only reset: EXPEVT 020, others held");
  t.reset(2, 2);
  check(t.erd(0x24) == 0x140, "multiple-hit reset: EXPEVT 140");
  t.ewr(0x24, 0x555);
  t.reset(1, 1);
  check(t.erd(0x24) == 0x020 && t.erd(0x28) == 0x420, "manual reset: EXPEVT 020, INTEVT held");
  t.reset(0);
  check(t.erd(0x24) == 0 && t.erd(0x20) == 0 && t.erd(0x28) == 0, "power-on reset: all 0");
  // holes of this port (never strobed by the bridge, but decoded): hit 0
  t.erd(0x2C, 2, &hit); check(!hit, "offset 0x2C must not hit");
  // a bus write in the cycle of an excw pulse (the DMAC's on-chip leg can strobe CCN_EXC in any cycle; WI-EXC-verify
  // finding 6): it lands in every register the entry does not write; on the same register the entry wins
  auto collide = [&](bool intr, uint16_t code, bool trav, uint8_t tra, uint32_t reg, uint32_t wv) {
    t.m->excw_v = 1; t.m->excw_intr = intr; t.m->excw_code = code; t.m->excw_tra_v = trav; t.m->excw_tra = tra;
    t.m->p4_sel_exc = 1; t.m->p4_wr = 1; t.m->p4_size = 2; t.m->p4_off = 0x0F000000u + reg; t.m->p4_wdata = wv;
    t.m->p4_dma = 1;
    t.strobing = true; t.step(); t.strobing = false;
    t.m->excw_v = t.m->excw_intr = t.m->excw_tra_v = 0;
    t.m->p4_sel_exc = 0; t.m->p4_wr = 0; t.m->p4_dma = 0;
  };
  t.ewr(0x20, 0); t.ewr(0x24, 0); t.ewr(0x28, 0);
  collide(false, 0x0E0, false, 0, 0x20, 0x3FC);                 // excw EXPEVT, DMAC TRA
  a = t.erd(0x20); b = t.erd(0x24);
  check(a == 0x3FC && b == 0x0E0, "excw EXPEVT + bus TRA in one cycle: TRA %x EXPEVT %x (want 3FC 0E0)", a, b);
  collide(true, 0x420, false, 0, 0x24, 0x555);                  // excw INTEVT, DMAC EXPEVT
  b = t.erd(0x24); c = t.erd(0x28);
  check(b == 0x555 && c == 0x420, "excw INTEVT + bus EXPEVT in one cycle: EXPEVT %x INTEVT %x (want 555 420)", b, c);
  collide(false, 0x160, true, 0x12, 0x28, 0x7A0);               // excw TRAPA (EXPEVT + TRA), DMAC INTEVT
  a = t.erd(0x20); b = t.erd(0x24); c = t.erd(0x28);
  check(a == 0x48 && b == 0x160 && c == 0x7A0, "TRAPA excw + bus INTEVT: TRA %x EXPEVT %x INTEVT %x (want 48 160 7A0)",
        a, b, c);
  collide(false, 0x0E0, false, 0, 0x24, 0x123);                 // the same register: the entry wins
  collide(true, 0x600, false, 0, 0x28, 0x456);
  collide(false, 0x160, true, 0x21, 0x20, 0x3FC);
  a = t.erd(0x20); b = t.erd(0x24); c = t.erd(0x28);
  check(a == 0x84 && b == 0x160 && c == 0x600, "excw and a bus write to the same register: TRA %x EXPEVT %x INTEVT %x "
        "(want the entry's 84 160 600)", a, b, c);
}

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--only") && i + 1 < argc) g_only = argv[++i];
    else if (!strcmp(argv[i], "-v")) g_verbose = 1;
  }
  if (want("regs")) t_regs();
  if (want("resolve")) t_resolve();
  if (want("imask")) t_imask();
  if (want("progs")) t_progs();
  if (want("timing")) t_timing();
  if (want("irl")) t_irl();
  if (want("nmi")) t_nmi();
  if (want("wake")) t_wake();
  if (want("excregs")) t_excregs();
  printf("%d checks, %d failures\n", g_checks, g_fail);
  printf("v1-intc: %s\n", g_fail ? "FAIL" : "PASS");
  return g_fail ? 1 : 0;
}
