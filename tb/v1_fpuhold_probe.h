// v1_fpuhold_probe.h -- the SOC.md 9.3 probe loops (tb/v1_fpuhold_tb --probe): what this RTL predicts for the console
// program tools/hw/core-integration/ci4 (ci4_asm.S has the same loop bodies, in the same order).
//
// Each loop: a load at a slow address (P2 uncached, 14 / 15 cycles by Bclk phase; or a cacheable miss: the miss path's
// 2 pfz cycles, then mah to the same ack), around FPU work whose result a consumer after the load uses; every loop has
// a twin with a cached load.  The questions (SOC.md 9.3, core-integration 10.1):
//   fd*     FDIV -> slow load -> FADD(quotient) at several distances (the result pass waits for the hold; is it granted
//           in the ack cycle or one later: the model says one later, from ci3's p2_fdiv)
//   fdm*    FDIV across a cacheable load miss (the miss path's pfz stops the recurrence: C7, never measured for loads)
//   fdd*    FDIV DR under the hold (the double's two result passes)
//   fipr*   FIPR / FTRV / FSCA under the hold (does `mah` hold the vector array passes?)
//   mul*    MUL.L under the hold (the multiplier stages)
//   dbl*    FADD DR under the hold (the double sequencer)
//   efe/eex an independent FE / EX op right after the slow load (is the FE dispatch itself late after `mah`?)
// The model's answer (SOC.md 4.5): hold_pipe = pfz | mah holds every FPU unit but the divider recurrence.
// [HW tools/hw/core-integration/ci4_run1.out, 2026-09-23]: every one of the 34 rows equals this RTL's steady state
// (the table below; the console's .1 fractions are refresh noise): `mah` holds the vector array passes, the multiplier
// stages and the double sequencer; the divider's result pass is granted the cycle after the hold (granted in the ack
// cycle it would give p2_fd 22, fd_n4 24, fd_n8 26, fd_u2 22); the probe's cacheable miss is an SDRAM row miss each
// time (24 / 25, miss_em 28.1), and behind it the FDIV rows are the P2 ones + 10 (the miss path's two pfz cycles stop
// the recurrence or not: no difference, the pass waits for the hold either way).
#pragma once

static const uint32_t PR_BIG = 0x8C040000u;           // the miss window: every load in [BIG, BIG + 0x8000) misses

struct Probe { const char *name; int kind; bool pr; Body body; int hw = -1; };   // kind 0 cached, 1 P2, 2 miss

static std::vector<Probe> probe_loops() {
    auto nops = [](Sh4Prog &p, int n) { for (int k = 0; k < n; k++) p.op(NOP); };
    auto ld = [](Sh4Prog &p) { p.I("MOV.L @Rm,Rn", 1, 9); };
    std::vector<Probe> v;
    for (int kind = 0; kind < 2; kind++) {
        v.push_back({"fd", kind, false, [=](Sh4Prog &p) { p.I("FDIV FRm,FRn", 7, 6); ld(p); p.I("FADD FRm,FRn", 5, 7); }});
        for (int d : {2, 4, 6, 8})
            v.push_back({d == 2 ? "fd_n2" : d == 4 ? "fd_n4" : d == 6 ? "fd_n6" : "fd_n8", kind, false,
                         [=](Sh4Prog &p) { p.I("FDIV FRm,FRn", 7, 6); nops(p, d); ld(p); p.I("FADD FRm,FRn", 5, 7); }});
        v.push_back({"fd_u2", kind, false, [=](Sh4Prog &p) { p.I("FDIV FRm,FRn", 7, 6); ld(p); nops(p, 2); p.I("FADD FRm,FRn", 5, 7); }});
        v.push_back({"fdd", kind, true, [=](Sh4Prog &p) { p.I("FDIV DRm,DRn", 2, 3); ld(p); p.I("FADD DRm,DRn", 4, 2); }});
        v.push_back({"fipr", kind, false, [=](Sh4Prog &p) { p.I("FIPR FVm,FVn", 1, 0); ld(p); p.I("FADD FRm,FRn", 8, 7); }});
        v.push_back({"ftrv", kind, false, [=](Sh4Prog &p) { p.I("FTRV XMTRX,FVn", 1); ld(p); p.I("FADD FRm,FRn", 8, 7); }});
        v.push_back({"fsca", kind, false, [=](Sh4Prog &p) { p.I("FSCA FPUL,DRn", 1); ld(p); p.I("FADD FRm,FRn", 8, 3); }});
        v.push_back({"mul", kind, false, [=](Sh4Prog &p) { p.I("MUL.L Rm,Rn", 5, 6); ld(p); p.I("STS MACL,Rn", 7); }});
        v.push_back({"dbl", kind, true, [=](Sh4Prog &p) { p.I("FADD DRm,DRn", 1, 0); ld(p); p.I("FADD DRm,DRn", 2, 1); }});
        v.push_back({"efe", kind, false, [=](Sh4Prog &p) { ld(p); p.I("FADD FRm,FRn", 8, 4); }});
        v.push_back({"eex", kind, false, [=](Sh4Prog &p) { ld(p); p.I("ADD Rm,Rn", 7, 5); }});
    }
    // the cacheable miss (r9 alternates between two lines of one operand-cache set: r10 = 0x4000, or 0 for the twin)
    v.push_back({"fdm", 0, false, [=](Sh4Prog &p) { p.I("XOR Rm,Rn", 9, 10).I("FDIV FRm,FRn", 7, 6); ld(p); p.I("FADD FRm,FRn", 5, 7); }});
    v.push_back({"fdm", 2, false, [=](Sh4Prog &p) { p.I("XOR Rm,Rn", 9, 10).I("FDIV FRm,FRn", 7, 6); ld(p); p.I("FADD FRm,FRn", 5, 7); }});
    v.push_back({"fdm_n4", 0, false, [=](Sh4Prog &p) { p.I("XOR Rm,Rn", 9, 10).I("FDIV FRm,FRn", 7, 6); nops(p, 4); ld(p); p.I("FADD FRm,FRn", 5, 7); }});
    v.push_back({"fdm_n4", 2, false, [=](Sh4Prog &p) { p.I("XOR Rm,Rn", 9, 10).I("FDIV FRm,FRn", 7, 6); nops(p, 4); ld(p); p.I("FADD FRm,FRn", 5, 7); }});
    v.push_back({"em", 0, false, [=](Sh4Prog &p) { p.I("XOR Rm,Rn", 9, 10); ld(p); }});
    v.push_back({"em", 2, false, [=](Sh4Prog &p) { p.I("XOR Rm,Rn", 9, 10); ld(p); }});
    // the console's values, in this order (ci4_run1.out: ram_fd 14.996 ... miss_em 28.137, rounded)
    static const int hw[] = {15, 15, 15, 15, 15, 15, 31, 7, 10, 10, 8, 14, 4, 4,
                             24, 24, 26, 26, 28, 24, 31, 22, 24, 24, 22, 28, 18, 18,
                             15, 34, 15, 36, 4, 28};
    for (size_t k = 0; k < v.size() && k < sizeof hw / sizeof *hw; k++) v[k].hw = hw[k];
    return v;
}

static int probe_main(Vv1_fpuhold_tb *m, const std::set<std::string> &only, const std::string &trace) {
    const char *kn[3] = {"ram", "p2", "miss"};
    int bad = 0;
    for (auto &P : probe_loops()) {
        const std::string name = std::string(kn[P.kind]) + "_" + P.name;
        if (!only.empty() && !only.count(name)) continue;
        const uint32_t addr = P.kind == 0 ? (P.name[0] == 'e' && P.name[1] == 'm' ? PR_BIG : SCR)
                            : P.kind == 1 ? SCR + 0x20000000u : PR_BIG;
        const bool miss = P.kind == 2, xr = P.name[0] == 'e' && P.name[1] == 'm' ? true : std::string(P.name).rfind("fdm", 0) == 0;
        Loop L{name, addr, [=](Sh4Prog &p) {
                   for (int f : {0, 1, 2, 3, 8}) p.I("FLDI1 FRn", f);
                   p.I("MOV #imm,Rn", 5, 0, 3).I("MOV #imm,Rn", 6, 0, 3);
                   if (xr) { p.I("MOV #imm,Rn", 10, 0, miss ? 0x40 : 0); p.I("SHLL8 Rn", 10); }
                   if (P.pr) p.I("MOV #imm,Rn", 1, 0, 12).I("SHLL16 Rn", 1).I("ADD #imm,Rn", 1, 0, 1).I("LDS Rm,FPSCR", 0, 1);
               }, P.body, false, -1};
        m->miss_lo = miss ? PR_BIG : 0; m->miss_hi = miss ? PR_BIG + 0x8000u : 0;
        m->miss_min = 24;                              // a row miss [HW ci4 miss_em 28.1]
        Run R[2];
        for (int ph = 0; ph < 2; ph++) run(m, L, g_p2, ph, trace == name && ph == 0, R[ph]);
        const bool ok = R[0].halted && R[1].halted && !R[0].proto && !R[1].proto && !R[0].fpuerr && !R[1].fpuerr;
        const bool hw = R[0].pmin == P.hw && R[0].pmax == P.hw && R[1].pmin == P.hw && R[1].pmax == P.hw;
        printf("P %-12s %6.2f %6.2f  (min %d max %d / min %d max %d)  console %2d  %s\n", name.c_str(), R[0].per,
               R[1].per, R[0].pmin, R[0].pmax, R[1].pmin, R[1].pmax, P.hw, !ok ? "RUN FAILED" : hw ? "ok" : "DIFFERS");
        bad += !ok || !hw;
    }
    m->miss_lo = 0; m->miss_hi = 0;
    printf("v1-fpuhold ci4 (SOC.md 9.3): %d loop(s) differ from the console\n", bad);
    return bad ? 1 : 0;
}
