// v1_fpuhold_tb.cpp -- make v1-fpuhold, part 1: the console's ci3 loops on the core (tb/v1_fpuhold_tb.sv; rtl/v1/SOC.md
// 4.5, core-integration 6.3, tools/hw/core-integration/ci3_asm.S).
//
// Each loop is ci3's own code: the load at the 32-byte aligned loop head (TLD: a cached P1 word, the P4 register
// EXPEVT, an uncached P2 word) followed by a body, `dt r13 ; bf`; the FDIV loops (TFD: fdiv fr6,fr7 ; mov.l @r9,r1 ;
// fadd fr7,fr5; TFD2 without the fadd).  The cycles per iteration are the distance between successive issues of the
// loop head in steady state (the last 16 of 48 iterations; the console's is (N2 - N1) / 256 over PMCTR brackets).  The
// P2 read's hold comes from the Bclk-phase model of tb/v1_fpuhold_tb.sv (14 / 15 cycles by phase: caches-sq 3.9.1's
// "14 / 15 by phase" for the one-beat path, and the only hold that fits the six P2 rows without an FPU op -- 15 / 16
// gives stcsr / use 22, console 20); every loop runs from both phases and must settle on the console's integer value
// (the console's .1 fractions are its refresh noise).  Targets [HW tools/hw/core-integration/ci_run3.out, 2026-09-23]:
//   ram  empty 3  nop8 7  nop16 11  ldcsr 9  stcsr 6  use 6  fdiv 15  fdiv2 12
//   p4   empty 5  nop8 9            ldcsr 11 stcsr 8  use 8  fdiv 15
//   p2   empty 18 nop8 22 nop16 26  ldcsr 24 stcsr 20 use 20 fdiv 24  fdiv2 20
// The three FDIV targets are WI-CORE-F's (SOC.md 8.1): 15.0 (P4: the divider runs under `mah`), 24.1 (P2: the result
// pass waits for the end of the hold), 20.1 (no consumer: the next FDIV finds the recurrence free).  The others check
// the P2 / P4 stand-ins against the console, so the FDIV rows compare like with like.
//
// Every run also requires: the core halts (SLEEP after the loop), FR5 / FR7 / R1 / R13 as the loop leaves them (FR7 =
// 1.0 / 1.0 = 1.0 after any number of FDIVs, FR5 = 1 + N), 0 MA port contract violations and 0 v1_fpu testbench
// check hits (a result pass at FS before its quotient, a record killed while writing, a kill in a held cycle).
//   tb [--only NAME[,NAME]] [--trace NAME] [--probe] [-v]
// --probe runs the SOC.md 9.3 prediction loops instead (tb/v1_fpuhold_probe.h): what this RTL predicts for the
// hardware probe tools/hw/core-integration/ci4 (FDIV -> slow load -> consumer at several distances, and the other
// FPU units under the MA hold).
#include "Vv1_fpuhold_tb.h"
#include "Vv1_fpuhold_tb__Dpi.h"
#include "verilated.h"
#include "sh4_asm.h"
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <functional>
#include <set>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// memory: 16 MB of RAM at physical 0x0C000000 (P1 0x8C000000, P2 0xAC000000)
static std::vector<uint8_t> g_ram(16u << 20);
static uint64_t g_bad = 0;
static uint8_t *ptr(uint32_t a) {
    a &= 0x1FFFFFFFu;
    if (a >= 0x0C000000u && a < 0x0D000000u) return &g_ram[a - 0x0C000000u];
    return nullptr;
}
static uint64_t rd(uint32_t a, int n, bool req) {
    uint64_t v = 0;
    for (int k = 0; k < n; k++) { uint8_t *p = ptr(a + k); v |= (uint64_t)(p ? *p : 0) << (8 * k); if (!p && req) g_bad++; }
    return v;
}
unsigned int fh_if_rd(unsigned int a, svBit req, unsigned int) { return (uint32_t)rd(a & ~3u, 4, req); }
unsigned long long fh_dm_rd(unsigned int a, int size, svBit req, unsigned int) { return rd(a, 1 << (size & 3), req); }
void fh_dm_wr(unsigned int a, int size, unsigned long long v) {
    for (int k = 0; k < (1 << (size & 3)); k++) { uint8_t *p = ptr(a + k); if (p) *p = (uint8_t)(v >> (8 * k)); else g_bad++; }
}

// ---------------------------------------------------------------------------------------------------------------
static const uint32_t CODE = 0x8C010000u, SCR = 0x8C020000u, P4_EXPEVT = 0xFF000024u;
static const uint16_t NOP = 0x0009, SLEEP = 0x001B;
static const int ITERS = 48, TAIL = 16;
static int g_p2 = 14;                   // the P2 read's minimum hold (--p2): 14 / 15 by Bclk phase
typedef std::function<void(Sh4Prog &)> Body;

struct Loop {
    std::string name;
    uint32_t addr;                  // R9: the load's address
    Body pre, body;                 // before the loop head / the loop body after the load (TLD) or the whole body
    bool tld;                       // TLD form: mov.l @r9,r1 at the head, then the body
    int target;                     // console cycles per iteration (the integer the loop settles on)
};

struct Run { bool halted = false; double per = 0; int pmin = 0, pmax = 0; uint32_t fr5 = 0, fr7 = 0, r13 = 0;
             uint32_t proto = 0, fpuerr = 0; uint64_t bad = 0; std::vector<uint64_t> heads; };

static void tick(Vv1_fpuhold_tb *m) { m->clk = 0; m->eval(); m->clk = 1; m->eval(); }

static bool build(const Loop &L, Sh4Prog &p) {
    p.ldl(9, "addr").I("STC SR,Rn", 12);
    for (int f = 4; f <= 7; f++) p.I("FLDI1 FRn", f);
    if (L.pre) L.pre(p);
    p.bra("BRA label", "L").op(NOP);
    while (p.pc() & 31) p.op(NOP);
    p.label("L");
    if (L.tld) p.I("MOV.L @Rm,Rn", 1, 9);
    L.body(p);
    p.I("DT Rn", 13).bcc("BF", "L");
    p.op(SLEEP).op(NOP).op(NOP);
    p.lit("addr", L.addr);
    return p.link();
}

static void run(Vv1_fpuhold_tb *m, const Loop &L, int p2_min, int ph, bool trace, Run &R) {
    Sh4Prog p(CODE);
    if (!build(L, p)) { R.halted = false; return; }
    std::fill(g_ram.begin(), g_ram.end(), 0);
    for (size_t k = 0; k < p.w.size(); k++) fh_dm_wr(CODE + 2 * (uint32_t)k, 1, p.w[k]);
    fh_dm_wr(SCR, 2, 0x12345678u);
    g_bad = 0;
    static uint32_t epoch = 0;
    m->mem_epoch = ++epoch;
    m->p2_min = (uint8_t)p2_min; m->p2_ph = (uint8_t)ph;
    m->rst = 1; m->dbg_we = 0;
    tick(m); tick(m);
    const std::vector<std::pair<int, uint32_t>> ctx = {{34, CODE}, {24, 0x400000F0u}, {35, 0x00040001u}, {13, ITERS}};
    for (auto &c : ctx) { m->dbg_we = 1; m->dbg_addr = (uint8_t)c.first; m->dbg_wdata = c.second; tick(m); }
    m->dbg_we = 0;
    m->rst = 0;
    const uint32_t head = p.at("L");
    uint64_t cyc = 0;
    for (; cyc < 20000; cyc++) {
        m->clk = 0; m->eval();
        if (m->halted) break;
        if (m->i0_valid && m->i0_pc == head) R.heads.push_back(cyc);
        if (trace)
            printf("%6" PRIu64 " %s%08X %s%08X%s\n", cyc, m->i0_valid ? "i0 " : "   ", m->i0_valid ? m->i0_pc : 0,
                   m->i1_valid ? "i1 " : "   ", m->i1_valid ? m->i1_pc : 0, m->mah_o ? "  mah" : "");
        m->clk = 1; m->eval();
    }
    R.halted = m->halted;
    for (int k = 0; k < 32; k++) tick(m);
    auto reg = [&](int i) { m->dbg_raddr = (uint8_t)i; m->clk = 0; m->eval(); return m->dbg_rdata; };
    R.fr5 = reg(64 + 5); R.fr7 = reg(64 + 7); R.r13 = reg(13);
    R.proto = m->proto_err; R.fpuerr = m->fpu_err; R.bad = g_bad;
    if (R.heads.size() >= (size_t)TAIL + 1) {
        const size_t n = R.heads.size();
        R.pmin = 1 << 30; R.pmax = 0;
        for (size_t k = n - TAIL; k < n; k++) {
            const int d = (int)(R.heads[k] - R.heads[k - 1]);
            R.pmin = std::min(R.pmin, d); R.pmax = std::max(R.pmax, d);
        }
        R.per = (double)(R.heads[n - 1] - R.heads[n - 1 - TAIL]) / TAIL;
    }
}

// FR5 after the loop: 1.0 + N x FR7 (1.0) for the FDIV loops with the FADD, else 1.0
static uint32_t f32(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

static std::vector<Loop> ci3_loops() {
    const uint32_t P2 = SCR + 0x20000000u;
    struct B { const char *n; Body b; };
    const std::vector<B> bodies = {
        {"empty", [](Sh4Prog &) {}},
        {"nop8", [](Sh4Prog &p) { for (int k = 0; k < 8; k++) p.op(NOP); }},
        {"nop16", [](Sh4Prog &p) { for (int k = 0; k < 16; k++) p.op(NOP); }},
        {"ldcsr", [](Sh4Prog &p) { p.I("LDC Rm,SR", 0, 12).op(NOP); }},
        {"stcsr", [](Sh4Prog &p) { p.I("STC SR,Rn", 2).op(NOP); }},
        {"use", [](Sh4Prog &p) { p.I("ADD Rm,Rn", 2, 1); }},
    };
    // console values (ci_run3.out): -1 = the console did not run that row
    const int tgt[3][6] = {{3, 7, 11, 9, 6, 6}, {5, 9, -1, 11, 8, 8}, {18, 22, 26, 24, 20, 20}};
    const char *an[3] = {"ram", "p4", "p2"};
    const uint32_t aa[3] = {SCR, P4_EXPEVT, P2};
    std::vector<Loop> v;
    for (int a = 0; a < 3; a++)
        for (size_t b = 0; b < bodies.size(); b++)
            if (tgt[a][b] >= 0) v.push_back({std::string(an[a]) + "_" + bodies[b].n, aa[a], nullptr, bodies[b].b, true, tgt[a][b]});
    const int fd[3] = {15, 15, 24}, fd2[3] = {12, -1, 20};
    for (int a = 0; a < 3; a++) {
        v.push_back({std::string(an[a]) + "_fdiv", aa[a], nullptr,
                     [](Sh4Prog &p) { p.I("FDIV FRm,FRn", 7, 6).I("MOV.L @Rm,Rn", 1, 9).I("FADD FRm,FRn", 5, 7); }, false, fd[a]});
        if (fd2[a] >= 0)
            v.push_back({std::string(an[a]) + "_fdiv2", aa[a], nullptr,
                         [](Sh4Prog &p) { p.I("FDIV FRm,FRn", 7, 6).I("MOV.L @Rm,Rn", 1, 9); }, false, fd2[a]});
    }
    return v;
}

#include "v1_fpuhold_probe.h"

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    std::set<std::string> only;
    std::string trace;
    bool verbose = false, probe = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--only") && i + 1 < argc) {
            std::string s = argv[++i];
            for (size_t a = 0, b; a <= s.size(); a = b + 1) { b = s.find(',', a); if (b == std::string::npos) b = s.size(); only.insert(s.substr(a, b - a)); }
        } else if (!strcmp(argv[i], "--trace") && i + 1 < argc) trace = argv[++i];
        else if (!strcmp(argv[i], "--probe")) probe = true;
        else if (!strcmp(argv[i], "-v")) verbose = true;
        else if (!strcmp(argv[i], "--p2") && i + 1 < argc) g_p2 = atoi(argv[++i]);
    }
    auto m = std::make_unique<Vv1_fpuhold_tb>();
    if (probe) return probe_main(m.get(), only, trace);
    int nfail = 0, n = 0;
    for (auto &L : ci3_loops()) {
        if (!only.empty() && !only.count(L.name)) continue;
        for (int ph = 0; ph < 2; ph++) {
            Run R;
            run(m.get(), L, g_p2, ph, trace == L.name && ph == 0, R);
            n++;
            std::string why;
            const bool fdivu = L.name.find("_fdiv") != std::string::npos && L.name.find("fdiv2") == std::string::npos;
            const uint32_t fr5 = fdivu ? f32(1.0f + ITERS) : f32(1.0f);
            if (!R.halted) why += " did not halt;";
            if (R.pmin != R.pmax) why += " not periodic;";
            if (R.pmin != L.target) why += " period " + std::to_string(R.pmin) + " != console " + std::to_string(L.target) + ";";
            if (R.fr5 != fr5 || R.fr7 != f32(1.0f) || R.r13 != 0) why += " registers;";
            if (R.proto || R.fpuerr || R.bad) why += " proto / fpu check / bad access;";
            printf("%-12s ph %d  %6.2f (min %d max %d)  console %2d  %s\n", L.name.c_str(), ph, R.per, R.pmin, R.pmax,
                   L.target, why.empty() ? "ok" : ("FAIL" + why).c_str());
            if (verbose && !why.empty()) printf("   FR5 %08X FR7 %08X R13 %u proto %u fpu %u bad %" PRIu64 "\n", R.fr5, R.fr7, R.r13, R.proto, R.fpuerr, R.bad);
            nfail += !why.empty();
        }
    }
    printf("v1-fpuhold ci3: %d / %d runs match the console\n", n - nfail, n);
    return nfail ? 1 : 0;
}
