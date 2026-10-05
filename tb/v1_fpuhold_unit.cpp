// v1_fpuhold_unit.cpp -- make v1-fpuhold, part 2: v1_fpu alone (tb/v1_fpuhold_unit.sv), the C++ side standing in for
// the core (rtl/v1/SOC.md 1.3.2, 4.3.1, 4.3.2, 4.3.8, 4.5).
//
//   hold    the result-pass structure under scripted hold windows (SOC.md 4.5): FDIV FR6,FR7 then a consumer FADD
//           FR7,FR5, and FDIV DR, under random `mah`-like windows (hold_pipe only: the recurrence runs) and `pfz`-like
//           windows (both holds); the quotient's write cycle(s) and the consumer's issue cycle against a C++
//           restatement of the rule (below), and the values.  The ci3 schedule is a special case (a window of 2 or
//           14 / 15 cycles from issue + 2).
//   kill    every record kind killed at every stage it can be killed in: the F1 / F2 latches, the double sequencer,
//           the divider (recurrence, pending request, captured result), the vector records and their stages, FTRV's
//           row sequencer, the multiplier records, FCMP DR's pending compare pass; FRCHG / FSCHG undo; the cause tag
//           repair (an older completion owns the cause again); an older FDIV completing across a younger kill.
//   trap    trap_d against the opcode x enable table (every FE form x every enable pattern); the trap effects (SOC.md
//           4.3.8): a D-decided trap's pass computes the operation and writes the computed cause (flags |= it), or E
//           alone (flags kept), never the destination -- FDIV / FSQRT through the recurrence, with the FPSCR readers
//           held until the cause is written and the divider occupied as by an untrapped divide -- [HW fpdn 0x00001084,
//           0x00085094, 0x00040080]; the F1 operand trap (V / Z / E) of every checked form, its take (the self-kill,
//           the cause write, T gated), a vector op's F1 trap (it runs on: cause written, destination not), a trap
//           overtaken by an older kill (nothing written).
//   records the commit-log record port (SOC.md 6.3, v1_fpu's frec): in every run, each record's ISN is an issued,
//           unkilled instruction's (not a trap pass's, nor a taken F1 trap's own), and tb/v1_fpu_reccheck.sv checks
//           every cycle that the records are exactly the register writes (chk_err); directed vectors check the records
//           of each writer kind (FE, double, divider, vector, multiplier, FRCHG / FSCHG) one by one.
// Reference values: tb/sh4_iss.h's floating-point functions (sh4iss::fp); the trap effects of SOC.md 4.3.8.
//   tb [--only hold,holdval,kill,trap,rec] [--seed S] [--n N] [-v]
#include "Vv1_fpuhold_unit.h"
#include "verilated.h"
#include "sh4_asm.h"
#include "soc_commit.h"
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fp = sh4iss::fp;
static bool g_verbose = false;

// ---------------------------------------------------------------------------------------------------------------
// the harness: one cycle = set the inputs, evaluate (the outputs of this cycle), clock
struct U {
    Vv1_fpuhold_unit *m;
    uint64_t cyc = 0;
    explicit U(Vv1_fpuhold_unit *x) : m(x) {}
    void idle_inputs() {
        m->hold_pipe = 0; m->hold_div = 0; m->s_v0 = 0; m->s_v1 = 0; m->s_op0 = 0x0009; m->s_op1 = 0x0009;
        m->di_v0 = 0; m->di_v1 = 0; m->di_isn0 = 0; m->di_isn1 = 0; m->di_fcmpd = 0; m->di_fcmpd_op = 0;
        m->kill_v = 0; m->kill_isn = 0; m->kill_incl = 0; m->ex_v = 0; m->ex_op = 0x0009; m->ex_isn = 0;
        m->ex_a = 0; m->ex_b = 0; m->mac_ld_a_v = 0; m->mac_ld_b_v = 0; m->def_ld_v = 0; m->dbg_we = 0;
    }
    void eval() { m->clk = 0; m->eval(); }
    void edge() { m->clk = 1; m->eval(); cyc++; }
    void reset(uint32_t fpscr, const std::map<int, uint32_t> &fr, uint32_t mach = 0, uint32_t macl = 0, uint32_t fpul = 0) {
        idle_inputs();
        m->rst = 1;
        eval(); edge();
        auto w = [&](int a, uint32_t v) { m->dbg_we = 1; m->dbg_addr = (uint8_t)a; m->dbg_wdata = v; eval(); edge(); };
        for (int i = 0; i < 32; i++) w(64 + i, 0);
        for (auto &kv : fr) w(64 + kv.first, kv.second);
        w(31, mach); w(32, macl); w(36, fpul); w(35, fpscr);
        m->dbg_we = 0; m->rst = 0;
        eval(); cyc = 0;
    }
    uint32_t rd(int a) { const uint8_t o = m->dbg_raddr; m->dbg_raddr = (uint8_t)a; eval(); uint32_t v = m->dbg_rdata; m->dbg_raddr = o; eval(); return v; }
    uint32_t frg(int i) { return rd(64 + i); }
    uint32_t fpscr() { return rd(35); }
};

static uint16_t OP(const char *f, unsigned n = 0, unsigned m = 0) { return sh4_op(f, n, m); }

// ---------------------------------------------------------------------------------------------------------------
// A tiny in-order D stage over one lane: each instruction issues in the first cycle it is presented, not held and
// not hazarded (fhz0); a D-decided trap dispatches as the trap pass and stops the stream (the entry); FCMP DR takes a
// second D cycle for its compare pass.  Scripted per cycle: the holds, a kill, an EX multiplier micro-op, MAC data.
struct Step { bool hp = false, hd = false; int kill = -1; bool kincl = true; bool ex = false; uint16_t exop = 0;
              int exisn = 0; uint32_t exa = 0, exb = 0; bool lda = false, ldb = false, ldd = false; uint32_t ld = 0; };
struct Prog {
    std::vector<uint16_t> ops;                // issued in order, ISN = base + index
    int isn0 = 10;
    std::function<Step(uint64_t)> script;     // per-cycle holds / kills / EX
    bool take_f1 = true;                      // take v1_fpu's F1 trap (kill > K) when nothing older kills
    int cycles = 60;
    int resume = -1;                          // after the (first) kill, the stream goes on at this op (a handler)
    int dresume = -1;                         // after a D-decided trap, the stream goes on at this op, `dgap` cycles
    int dgap = 1;                             // later (the handler's first instruction; the trap pass runs on)
};
struct Trace { std::vector<int64_t> iss;      // issue cycle of each op (-1: not issued)
               std::vector<int> isn;          // its ISN (a resumed stream's ops take ISNs of their own)
               std::vector<int> trapd;        // the op dispatched as a trap pass
               int64_t f1take = -1; int f1isn = -1;
               bool f1_tp = false;            // trap_f1 raised for a trap pass's ISN (a second trap: never)
               int64_t resumed = -1;          // the kill / D-trap cycle the stream resumed after (Prog::resume / dresume)
               std::vector<std::pair<uint64_t, int>> twe;   // FCMP T writes: cycle, value
               std::vector<uint32_t> fpscr_iss;             // fpscr_now in each op's issue cycle
               struct Kill { uint64_t cyc; int isn; bool incl; };
               std::vector<Kill> kills;
               std::vector<soc::CommitRec> recs;            // v1_fpu's commit-log records (frec), in cycle / slot order
               uint32_t chk = 0; };

// the records of this cycle (v1_fpu's frec: 6 packed commit_rec_t of 125 bits, slot 0 at bit 0)
static void take_recs(U &u, std::vector<soc::CommitRec> &out) {
    for (int k = 0; k < 6; k++) {
        soc::CommitRec r;
        if (soc::unpack_rec(u.m->frec.data(), 125 * k, u.cyc, r)) out.push_back(r);
    }
}

// the record ISN rule (SOC.md 6.3): a record belongs to an issued, unkilled instruction -- never to a trap pass (D-decided
// trap: no destination, its cause has no record), nor to a taken F1 trap's own instruction, nor to one a kill covered,
// from the kill cycle on; the EX micro-ops the script inserts (the multiplier's) count as issued
static std::string g_rec_msg;
static void rec_isn_check(const Prog &P, const Trace &T, const std::set<int> &exisn) {
    for (auto &r : T.recs) {
        std::string why;
        bool issued = exisn.count(r.isn) > 0;
        int64_t icyc = -1;
        for (size_t i = 0; i < T.iss.size(); i++)
            if (T.iss[i] >= 0 && T.isn[i] == r.isn && T.iss[i] <= (int64_t)r.cycle && T.iss[i] > icyc) { issued = true; icyc = T.iss[i]; }
        if (!issued) why = "an instruction that did not issue";
        for (int i : T.trapd) if (T.isn[(size_t)i] == r.isn && T.iss[(size_t)i] == icyc) why = "a trap pass";
        if (T.f1take >= 0 && r.isn == T.f1isn && (int64_t)r.cycle >= T.f1take && icyc < T.f1take) why = "a taken F1 trap's instruction";
        for (auto &k : T.kills)
            if (r.cycle >= k.cyc && icyc < (int64_t)k.cyc && (k.incl ? r.isn >= k.isn : r.isn > k.isn)) why = "a killed instruction";
        if (!why.empty() && g_rec_msg.empty()) {
            char b[160];
            snprintf(b, sizeof b, " record kind %d idx %d isn %d at cycle %" PRIu64 " of %s", r.kind, r.idx, r.isn, r.cycle, why.c_str());
            g_rec_msg = b;
        }
    }
    (void)P;
}

static void run(U &u, const Prog &P, Trace &T) {
    size_t next = 0;
    bool stop = false;
    int fcd = -1;                             // FCMP DR issued: its compare pass is next
    int isnb = P.isn0;                        // the ISN base (a resumed stream: another one)
    int64_t dres = -1;                        // the cycle a D-decided trap's resume starts
    std::set<int> exisn;
    T.iss.assign(P.ops.size(), -1);
    T.isn.assign(P.ops.size(), -1);
    T.fpscr_iss.assign(P.ops.size(), 0);
    for (int c = 0; c < P.cycles; c++) {
        Step s = P.script ? P.script(u.cyc) : Step();
        u.idle_inputs();
        u.m->hold_pipe = s.hp || s.hd; u.m->hold_div = s.hd;
        if (s.ex) {
            u.m->ex_v = 1; u.m->ex_op = s.exop; u.m->ex_isn = (uint8_t)s.exisn; u.m->ex_a = s.exa; u.m->ex_b = s.exb;
            exisn.insert(s.exisn & 0xFF);
        }
        u.m->mac_ld_a_v = s.lda; u.m->mac_ld_b_v = s.ldb; u.m->def_ld_v = s.ldd;
        u.m->mac_ld_a = s.ld; u.m->mac_ld_b = s.ld; u.m->def_ld = s.ld;
        if (dres >= 0 && (int64_t)u.cyc == dres) { stop = false; dres = -1; }
        if (!stop && next < P.ops.size() && fcd < 0) { u.m->s_v0 = 1; u.m->s_op0 = P.ops[next]; }
        u.eval();
        const bool held = s.hp || s.hd;
        for (int i : T.trapd) if (u.m->trap_f1 && u.m->trap_f1_isn == (uint8_t)T.isn[(size_t)i]) T.f1_tp = true;
        // the kill of this cycle: the script's, else the F1 trap's take
        bool kill = false;
        if (s.kill >= 0 && !held) {
            u.m->kill_v = 1; u.m->kill_isn = (uint8_t)s.kill; u.m->kill_incl = s.kincl; kill = true;
            T.kills.push_back({u.cyc, s.kill, s.kincl});
        } else if (P.take_f1 && u.m->trap_f1 && !held && !stop) {
            u.m->kill_v = 1; u.m->kill_isn = u.m->trap_f1_isn; u.m->kill_incl = 0; kill = true;
            T.f1take = (int64_t)u.cyc; T.f1isn = u.m->trap_f1_isn; stop = true;
            T.kills.push_back({u.cyc, T.f1isn, false});
        }
        if (kill) { stop = true; fcd = -1; }       // the entry: nothing more issues (the sequencer is killed too)
        if (kill && P.resume >= 0 && T.resumed < 0) { T.resumed = (int64_t)u.cyc; next = (size_t)P.resume; isnb = P.isn0 + 100; }
        if (!held && !kill) {
            if (fcd >= 0) { u.m->di_fcmpd = 1; u.m->di_fcmpd_op = P.ops[fcd]; fcd = -1; }
            else if (!stop && next < P.ops.size() && !u.m->fhz0) {
                const uint16_t op = P.ops[next];
                u.m->di_v0 = 1; u.m->di_isn0 = (uint8_t)(isnb + next);
                T.iss[next] = (int64_t)u.cyc; T.isn[next] = (isnb + (int)next) & 0xFF; T.fpscr_iss[next] = u.m->fpscr_now;
                if (u.m->trap_d0) {
                    T.trapd.push_back((int)next); stop = true;
                    if (P.dresume >= 0 && T.resumed < 0) {
                        T.resumed = (int64_t)u.cyc; dres = (int64_t)u.cyc + P.dgap; isnb = P.isn0 + 100;
                        next = (size_t)P.dresume - 1;               // (next++ below)
                    }
                }
                else if ((op & 0xF00F) == 0xF004 || (op & 0xF00F) == 0xF005) {
                    if (u.m->md_dec & 1) fcd = (int)next;           // FCMP DR (PR = 1): micro-op 1 next
                }
                next++;
            }
        }
        u.eval();
        if (u.m->t_we && !held) T.twe.push_back({u.cyc, u.m->t_val});
        take_recs(u, T.recs);
        u.edge();
        if (T.resumed >= 0 && P.resume >= 0 && (int64_t)u.cyc == T.resumed + 1) stop = false;
    }
    T.chk = u.m->chk_err;
    rec_isn_check(P, T, exisn);
}

// ---------------------------------------------------------------------------------------------------------------
// hold: the result pass under scripted holds, against the rule restated here (SOC.md 4.5, rtl/v1/v1_fpu.sv):
//   * FDIV (single) issues at c0, starts at the first unheld cycle s > c0 (its F1); FDIV DR starts at c0.  The
//     recurrence advances in cycles with hold_div = 0 (a_1, a_2, ... after the start); N = LAT - 3 iterations.
//   * the request is posted at a_{N-1} (two iterations left); the pass is granted at the first cycle g >= that with
//     hold_pipe = 0 whose previous cycle was not held by `mah` alone; a double's upper word at the next such cycle.
//   * a granted pass is in F1 from g + 1 and moves at the end of each unheld cycle: F1 -> F2 -> FS, and writes FR at
//     the end of its (first unheld) FS cycle.  A consumer of the word at D issues in the first unheld cycle the
//     word's pass is at FS (the write-through).
struct HoldRef { int64_t wr_lo = -1, wr_hi = -1, use = -1; };
static HoldRef hold_ref(const std::vector<std::pair<bool, bool>> &h, int64_t c0, bool dbl) {
    auto hp = [&](int64_t t) { return t >= 0 && t < (int64_t)h.size() && (h[t].first || h[t].second); };
    auto hd = [&](int64_t t) { return t >= 0 && t < (int64_t)h.size() && h[t].second; };
    auto mahq = [&](int64_t t) { return hp(t - 1) && !hd(t - 1); };
    HoldRef R;
    const int N = dbl ? 22 : 9;
    int64_t s = c0;
    if (!dbl) { s = c0 + 1; while (hp(s)) s++; }
    int64_t t = s, post = -1;
    for (int j = 1; j <= N - 1; j++) { t++; while (hd(t)) t++; }
    post = t;
    auto grant = [&](int64_t from) { int64_t g = from; while (hp(g) || mahq(g)) g++; return g; };
    auto fs_end = [&](int64_t g) {           // the cycle whose end writes, and the first cycle at FS
        int64_t x = g + 1;
        while (hp(x)) x++;                    // leaves F1 at the end of x
        x++; while (hp(x)) x++;               // leaves F2 at the end of x
        const int64_t at_fs = x + 1;
        x++; while (hp(x)) x++;               // writes at the end of x
        return std::make_pair(x, at_fs);
    };
    const int64_t g1 = grant(post);
    auto lo = fs_end(g1);
    R.wr_lo = lo.first;
    int64_t first_use = lo.second; while (hp(first_use)) first_use++;
    R.use = first_use;
    if (dbl) { const int64_t g2 = grant(g1 + 1); R.wr_hi = fs_end(g2).first; }
    return R;
}

static std::string test_hold(U &u, uint64_t seed, int n) {
    uint64_t x = seed * 0x9E3779B97F4A7C15ull + 7;
    auto rnd = [&](uint32_t k) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; return (uint32_t)(x % k); };
    int bad = 0, cases = 0;
    std::string first;
    for (int k = 0; k < n; k++) {
        const bool dbl = k % 4 == 3;
        // hold pattern over 80 cycles: 0-3 windows, each `mah`-like (hold_pipe only, 1-20 cycles) or `pfz`-like (both
        // holds, 1-3 cycles), placed anywhere from cycle 0 to 40; k < 8: the ci3 schedules (a window of 2 / 14 / 15
        // cycles from issue + 2)
        std::vector<std::pair<bool, bool>> h(90, {false, false});
        if (k < 8) {
            const int len[8] = {0, 2, 14, 15, 16, 1, 3, 20};
            for (int t = 2; t < 2 + len[k]; t++) h[t].first = true;
        } else {
            const int nw = (int)rnd(4);
            for (int w = 0; w < nw; w++) {
                const bool pfz = rnd(3) == 0;
                const int st = (int)rnd(41), ln = pfz ? 1 + (int)rnd(3) : 1 + (int)rnd(20);
                for (int t = st; t < st + ln && t < 90; t++) { if (pfz) h[t].second = true; else h[t].first = true; }
            }
        }
        if (h[0].first || h[0].second) h[0] = {false, false};    // the divide issues at cycle 0
        const uint32_t a = fp::b32(3.0f), b = fp::b32(1.0f + (float)rnd(100));
        std::map<int, uint32_t> fr = {{5, fp::b32(2.0f)}, {6, a}, {7, b}};
        uint32_t fpscr = 0x00040001u;
        Prog P;
        if (dbl) {
            fpscr |= 1u << 19;                                    // PR = 1
            const double da = 3.0, db = 1.0 + (double)rnd(100) + 1.0 / 7.0;   // (both words change)
            const uint64_t ua = fp::b64(da), ub = fp::b64(db);
            fr = {{6, (uint32_t)(ua >> 32)}, {7, (uint32_t)ua}, {4, (uint32_t)(ub >> 32)}, {5, (uint32_t)ub}};
            P.ops = {OP("FDIV DRm,DRn", 2, 3)};                   // DR4 = DR4 / DR6
        } else P.ops = {OP("FDIV FRm,FRn", 7, 6), OP("FADD FRm,FRn", 5, 7)};   // FR7 = FR7 / FR6; FR5 += FR7
        P.cycles = 80;
        P.script = [&](uint64_t c) { Step s; if (c < h.size()) { s.hp = h[c].first; s.hd = h[c].second; } return s; };
        u.reset(fpscr, fr);
        // watch the destination word(s) change
        std::vector<uint32_t> prev(2, 0);
        const int w0 = dbl ? 5 : 7, w1 = dbl ? 4 : 7;           // DR4: lower word FR5 first, upper FR4
        int64_t chg0 = -1, chg1 = -1;
        {
            // run cycle by cycle (the run() loop inlined, to sample the register after each edge)
            size_t next = 0;
            Trace T; T.iss.assign(P.ops.size(), -1);
            prev[0] = u.frg(w0); prev[1] = u.frg(w1);
            for (int c = 0; c < P.cycles; c++) {
                Step s = P.script(u.cyc);
                u.idle_inputs();
                u.m->hold_pipe = s.hp || s.hd; u.m->hold_div = s.hd;
                if (next < P.ops.size()) { u.m->s_v0 = 1; u.m->s_op0 = P.ops[next]; }
                u.eval();
                if (!(s.hp || s.hd) && next < P.ops.size() && !u.m->fhz0) {
                    u.m->di_v0 = 1; u.m->di_isn0 = (uint8_t)(10 + next); T.iss[next] = (int64_t)u.cyc; next++;
                }
                u.eval();
                const uint64_t now = u.cyc;
                u.edge();
                const uint32_t v0 = u.frg(w0), v1 = u.frg(w1);
                if (v0 != prev[0] && chg0 < 0) chg0 = (int64_t)now;
                if (v1 != prev[1] && chg1 < 0 && w1 != w0) chg1 = (int64_t)now;
                prev[0] = v0; prev[1] = v1;
            }
            const HoldRef R = hold_ref(h, T.iss[0], dbl);
            std::string why;
            if (T.iss[0] != 0) why += " divide issued at " + std::to_string(T.iss[0]);
            if (chg0 != R.wr_lo) why += " word written at " + std::to_string(chg0) + " (rule " + std::to_string(R.wr_lo) + ")";
            if (dbl && chg1 != R.wr_hi) why += " upper at " + std::to_string(chg1) + " (rule " + std::to_string(R.wr_hi) + ")";
            if (!dbl && T.iss[1] != R.use) why += " consumer at " + std::to_string(T.iss[1]) + " (rule " + std::to_string(R.use) + ")";
            // values: the quotient and the sum
            if (dbl) {
                uint32_t cq = 0;
                const uint64_t ua = ((uint64_t)fr[4] << 32) | fr[5], ub = ((uint64_t)fr[6] << 32) | fr[7];
                const uint64_t qq = fp::div_d(ua, ub, true, cq);
                const uint64_t got = ((uint64_t)u.frg(4) << 32) | u.frg(5);
                if (got != qq) why += " quotient";
            } else {
                uint32_t cq = 0, cs = 0;
                const uint32_t q = fp::div_s(fr[7], fr[6], true, cq), sum = fp::add_s(fr[5], q, true, cs);
                if (u.frg(7) != q || u.frg(5) != sum) why += " values";
                const uint32_t cause = (u.fpscr() >> 12) & 0x3F;
                if (cause != cs) why += " cause";
            }
            if (u.m->chk_err) why += " v1_fpu check";
            cases++;
            if (!why.empty()) {
                bad++;
                if (first.empty() || g_verbose) {
                    std::string hs;
                    for (int t = 0; t < 45; t++) hs += h[t].second ? 'P' : h[t].first ? 'M' : '.';
                    char b[400];
                    snprintf(b, sizeof b, "case %d (%s) holds %s:%s", k, dbl ? "FDIV DR" : "FDIV ; FADD", hs.c_str(), why.c_str());
                    if (first.empty()) first = b;
                    if (g_verbose) puts(b);
                }
            }
        }
    }
    printf("hold: %d / %d cases follow the rule\n", cases - bad, cases);
    return bad ? first : "";
}

// ---------------------------------------------------------------------------------------------------------------
// holdval: holds change when things happen, never what is written.  Programs over every held unit (the FE pipe, the
// double sequencer, the divider and its result pass, the vector array: FIPR / FTRV / FSCA / FSRRA) under random
// `mah`-like (hold_pipe) and `pfz`-like (both) windows must leave the register file, FPUL and FPSCR the unheld run
// leaves.
static std::string test_holdval(U &u, uint64_t seed, int n) {
    uint64_t x = seed * 0xD1B54A32D192ED03ull + 11;
    auto rnd = [&](uint32_t k) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; return (uint32_t)(x % k); };
    struct PG { const char *name; bool pr; std::vector<uint16_t> ops; };
    const std::vector<PG> progs = {
        // (OP(form, n, m); each result is consumed)
        {"FIPR ; FADD", false, {OP("FIPR FVm,FVn", 1, 0), OP("FADD FRm,FRn", 9, 7), OP("FMUL FRm,FRn", 8, 9)}},
        {"FTRV ; FADD", false, {OP("FTRV XMTRX,FVn", 1), OP("FADD FRm,FRn", 9, 4), OP("FTRV XMTRX,FVn", 0), OP("FADD FRm,FRn", 10, 1)}},
        {"FSCA ; FSRRA", false, {OP("FSCA FPUL,DRn", 1), OP("FSRRA FRn", 9), OP("FADD FRm,FRn", 9, 2), OP("FMUL FRm,FRn", 10, 3)}},
        {"FDIV ; FIPR", false, {OP("FDIV FRm,FRn", 12, 11), OP("FIPR FVm,FVn", 1, 0), OP("FADD FRm,FRn", 7, 12), OP("FSQRT FRn", 13)}},
        {"double", true, {OP("FADD DRm,DRn", 1, 0), OP("FMUL DRm,DRn", 0, 2), OP("FDIV DRm,DRn", 2, 3), OP("FADD DRm,DRn", 3, 2)}},
    };
    static const uint32_t pal[] = {0x3F800000u, 0x40400000u, 0xBF800000u, 0x3EAAAAABu, 0x41200000u, 0x3DCCCCCDu,
                                   0x40000000u, 0xC0A00000u, 0x3FC00000u, 0x42C80000u};
    int bad = 0, cases = 0;
    std::string first;
    for (int k = 0; k < n; k++) {
        const PG &g = progs[(size_t)k % progs.size()];
        std::map<int, uint32_t> fr;
        for (int i = 0; i < 32; i++) fr[i] = pal[rnd(sizeof pal / sizeof *pal)];
        const uint32_t fpscr = 0x00040001u | (g.pr ? 1u << 19 : 0), fpul = rnd(65536);   // DN = 1, RZ
        std::vector<std::pair<bool, bool>> h(100, {false, false});
        const int nw = 1 + (int)rnd(4);
        for (int w = 0; w < nw; w++) {
            const bool pfz = rnd(3) == 0;
            const int st = 1 + (int)rnd(30), ln = pfz ? 1 + (int)rnd(3) : 1 + (int)rnd(16);
            for (int t = st; t < st + ln && t < 100; t++) { if (pfz) h[t].second = true; else h[t].first = true; }
        }
        bool unissued = false;
        auto go = [&](bool held, std::vector<uint32_t> &out) {
            u.reset(fpscr, fr, 0, 0, fpul);
            Prog P; P.ops = g.ops; P.cycles = 100; P.take_f1 = false;
            if (held) P.script = [&](uint64_t c) { Step s; if (c < h.size()) { s.hp = h[c].first; s.hd = h[c].second; } return s; };
            Trace T; run(u, P, T);
            out.clear();
            for (int i = 0; i < 32; i++) out.push_back(u.frg(i));
            out.push_back(u.fpscr()); out.push_back(u.rd(36));
            for (auto c : T.iss) if (c < 0) unissued = true;
            return T.chk;
        };
        std::vector<uint32_t> ref, got;
        const uint32_t c0 = go(false, ref), c1 = go(true, got);
        cases++;
        std::string why;
        for (size_t i = 0; i < ref.size() || i < got.size(); i++) {
            const uint32_t a = i < ref.size() ? ref[i] : 0, b = i < got.size() ? got[i] : 0;
            if (a != b) { char w[64]; snprintf(w, sizeof w, " %s%zu %08X (unheld %08X)", i < 32 ? "FR" : "word ", i, b, a); why += w; break; }
        }
        if (c0 || c1) why += " v1_fpu check";
        if (unissued) why += " an instruction did not issue";
        if (!why.empty()) {
            bad++;
            if (first.empty() || g_verbose) {
                std::string hs;
                for (int t = 0; t < 45; t++) hs += h[t].second ? 'P' : h[t].first ? 'M' : '.';
                const std::string s = "case " + std::to_string(k) + " (" + g.name + ") holds " + hs + ":" + why;
                if (first.empty()) first = s;
                if (g_verbose) puts(s.c_str());
            }
        }
    }
    printf("holdval: %d / %d held runs write what the unheld run writes\n", cases - bad, cases);
    return first;
}

// ---------------------------------------------------------------------------------------------------------------
// kill / trap vectors
struct Vec { std::string name; std::function<std::string(U &)> f; };
static std::vector<Vec> g_vecs;
static void V(const std::string &n, std::function<std::string(U &)> f) { g_vecs.push_back({n, f}); }
static std::string hx(uint32_t v) { char b[16]; snprintf(b, sizeof b, "%08X", v); return b; }
static std::string expect(const char *what, uint32_t got, uint32_t want) {
    return got == want ? "" : std::string(" ") + what + " " + hx(got) + " (want " + hx(want) + ")";
}
static const uint32_t ONE = 0x3F800000u, TWO = 0x40000000u, THREE = 0x40400000u, K_SNAN = 0x7FC00000u, K_QNAN = 0x7F800001u;
static const uint32_t FPSCR0 = 0x00040001u;          // DN = 1, RM = RZ (KOS / reset)

// a kill at cycle `kc` of ISN >= / > `k`
static std::function<Step(uint64_t)> kill_at(uint64_t kc, int k, bool incl = true) {
    return [=](uint64_t c) { Step s; if (c == kc) { s.kill = k; s.kincl = incl; } return s; };
}

static void define_vectors() {
    // ---- FE pipe: an FADD killed in F1 / F2, and the cause tag repair
    for (int at = 1; at <= 2; at++)
        V("kill_fadd_f" + std::to_string(at), [at](U &u) {
            u.reset(FPSCR0 | (0x1Fu << 2), {{4, ONE}, {5, TWO}});         // flags preset: they must stay
            Prog P; P.ops = {OP("FADD FRm,FRn", 5, 4)}; P.script = kill_at((uint64_t)at, 10); P.cycles = 12;
            Trace T; run(u, P, T);
            return expect("FR5", u.frg(5), TWO) + expect("FPSCR", u.fpscr(), FPSCR0 | (0x1Fu << 2)) +
                   expect("cz_ctr", u.m->cz_ctr, 0) + (u.m->idle ? "" : " not idle") + (T.chk ? " check" : "");
        });
    V("kill_younger_fadd_older_cause", [](U &u) {
        // FMUL (inexact: cause I) issued, then FADD (exact) issued and killed: the FMUL's cause must land (its tag is
        // the youngest surviving)
        u.reset(FPSCR0, {{4, THREE}, {5, 0x3EAAAAABu}, {6, ONE}, {7, ONE}});
        Prog P; P.ops = {OP("FMUL FRm,FRn", 5, 4), OP("FADD FRm,FRn", 7, 6)}; P.script = kill_at(2, 11); P.cycles = 12;
        Trace T; run(u, P, T);
        uint32_t c = 0; const uint32_t r = fp::mul_s(0x3EAAAAABu, THREE, true, c);
        return expect("FR5", u.frg(5), r) + expect("FR7", u.frg(7), ONE) +
               expect("FPSCR", u.fpscr(), (FPSCR0 | (c << 12) | ((c & 0x1F) << 2))) + expect("cz_ctr", u.m->cz_ctr, 1);
    });
    // ---- FRCHG / FSCHG undo: killed with the pass in F1, F2, FS
    for (int k = 0; k < 2; k++)
        for (int at = 1; at <= 3; at++)
            V(std::string(k ? "kill_fschg_" : "kill_frchg_") + std::to_string(at), [k, at](U &u) {
                u.reset(0x00040000u, {});
                Prog P; P.ops = {OP(k ? "FSCHG" : "FRCHG")}; P.script = kill_at((uint64_t)at, 10); P.cycles = 10;
                Trace T; run(u, P, T);
                return expect("FPSCR", u.fpscr(), 0x00040000u) + (T.chk ? " check" : "");   // [HW ci2: 0x00040000]
            });
    V("frchg_not_killed", [](U &u) {                       // control: an older kill leaves it
        u.reset(0x00040000u, {});
        Prog P; P.ops = {OP("FRCHG")}; P.script = kill_at(2, 11); P.cycles = 10;
        Trace T; run(u, P, T);
        return expect("FPSCR", u.fpscr(), 0x00240000u);
    });
    // ---- the divider: killed in the recurrence, with the request pending (held), with the result captured
    for (int at : {1, 2, 5, 9, 10, 11})
        V("kill_fdiv_" + std::to_string(at), [at](U &u) {
            u.reset(FPSCR0, {{6, THREE}, {7, ONE}});
            Prog P; P.ops = {OP("FDIV FRm,FRn", 7, 6)}; P.script = kill_at((uint64_t)at, 10); P.cycles = 40;
            Trace T; run(u, P, T);
            return expect("FR7", u.frg(7), ONE) + expect("FPSCR", u.fpscr(), FPSCR0) + expect("cz_ctr", u.m->cz_ctr, 0) +
                   (u.m->idle && !u.m->dv_busy ? "" : " not idle / divider busy") + (T.chk ? " check" : "");
        });
    V("kill_fdiv_request_held", [](U &u) {
        // the request posted under a long `mah`, the result captured, killed before the pass is granted
        u.reset(FPSCR0, {{6, THREE}, {7, ONE}});
        Prog P; P.ops = {OP("FDIV FRm,FRn", 7, 6)}; P.cycles = 50;
        P.script = [](uint64_t c) { Step s; s.hp = c >= 5 && c < 30; if (c == 30) s.kill = 10; return s; };
        Trace T; run(u, P, T);
        return expect("FR7", u.frg(7), ONE) + expect("FPSCR", u.fpscr(), FPSCR0) + (u.m->idle ? "" : " not idle");
    });
    for (int at : {1, 3, 12, 20})
        V("kill_fdiv_dr_" + std::to_string(at), [at](U &u) {
            u.reset(FPSCR0 | (1u << 19), {{4, 0x40000000u}, {5, 0}, {6, 0x40080000u}, {7, 0}});
            Prog P; P.ops = {OP("FDIV DRm,DRn", 3, 2)}; P.script = kill_at((uint64_t)at, 10); P.cycles = 45;
            Trace T; run(u, P, T);
            return expect("FR4", u.frg(4), 0x40000000u) + expect("FR5", u.frg(5), 0) + (u.m->idle ? "" : " not idle");
        });
    V("older_fdiv_completes", [](U &u) {
        // FDIV 1/3 (ISN 10), then FADD (ISN 11) killed: the quotient is written, the cause is the FDIV's (I)
        // [HW ci run 1: FDIV ; trapping FADD, FR7 = 1.5; ci2 fdiv_cause FR9 = 0x3EAAAAAB]
        u.reset(FPSCR0, {{6, THREE}, {7, ONE}, {4, ONE}, {5, ONE}});
        Prog P; P.ops = {OP("FDIV FRm,FRn", 7, 6), OP("FADD FRm,FRn", 5, 4)}; P.script = kill_at(3, 11); P.cycles = 30;
        Trace T; run(u, P, T);
        return expect("FR7", u.frg(7), 0x3EAAAAAAu) + expect("FR5", u.frg(5), ONE) +
               expect("FPSCR", u.fpscr(), FPSCR0 | (1u << 12) | (1u << 2));
    });
    // ---- the double sequencer: FADD DR killed in its F1 passes
    for (int at = 1; at <= 6; at++)
        V("kill_fadd_dr_" + std::to_string(at), [at](U &u) {
            u.reset(FPSCR0 | (1u << 19), {{0, 0x3FF00000u}, {1, 0}, {2, 0x40000000u}, {3, 0}});
            Prog P; P.ops = {OP("FADD DRm,DRn", 1, 0)}; P.script = kill_at((uint64_t)at, 10); P.cycles = 16;
            Trace T; run(u, P, T);
            return expect("FR0", u.frg(0), 0x3FF00000u) + expect("FR1", u.frg(1), 0) + expect("cz_ctr", u.m->cz_ctr, 0) +
                   (u.m->idle ? "" : " not idle");
        });
    // ---- the vector unit: FIPR / FSCA / FSRRA / FTRV killed at each stage
    // (a take kills what issued at most 2 unheld cycles before it -- its own issue + 2, or its S's group + 1 -- so a
    // vector op is killed at age 1 or 2, never in its write cycle; `kill_wr_err` flags the impossible case, below)
    for (int at = 1; at <= 3; at++) {
        V("kill_fipr_" + std::to_string(at), [at](U &u) {
            std::map<int, uint32_t> fr; for (int i = 0; i < 8; i++) fr[i] = ONE;
            u.reset(FPSCR0, fr);
            Prog P; P.ops = {OP("FIPR FVm,FVn", 1, 0)}; P.script = kill_at((uint64_t)at, 10); P.cycles = 14;   // FR7 = FV0 . FV4
            Trace T; run(u, P, T);
            return expect("FR7", u.frg(7), ONE) + expect("FPSCR", u.fpscr(), FPSCR0) + (u.m->idle ? "" : " not idle") +
                   (T.chk ? " check" : "");
        });
        V("kill_ftrv_" + std::to_string(at), [at](U &u) {
            std::map<int, uint32_t> fr; for (int i = 0; i < 4; i++) fr[i] = ONE;
            u.reset(FPSCR0, fr);
            Prog P; P.ops = {OP("FTRV XMTRX,FVn", 0)}; P.script = kill_at((uint64_t)at, 10); P.cycles = 16;
            Trace T; run(u, P, T);
            std::string r;
            for (int i = 0; i < 4; i++) r += expect(("FR" + std::to_string(i)).c_str(), u.frg(i), ONE);
            return r + expect("FPSCR", u.fpscr(), FPSCR0) + (u.m->idle && !u.m->ftrv_on ? "" : " not idle") +
                   (T.chk ? " check" : "");
        });
    }
    // ---- a kill frees the unit: the next instruction that needs it issues in the cycle after the kill (a take's kill
    // cycle issues nothing; the stream resumes as a handler would), with its own value -- the divider's recurrence,
    // the vector records' pending destinations and the FTRV row sequencer are all dropped by the kill
    V("kill_frees_divider", [](U &u) {
        u.reset(FPSCR0, {{6, THREE}, {7, ONE}, {4, TWO}, {5, ONE}});
        Prog P; P.ops = {OP("FDIV FRm,FRn", 7, 6), OP("FDIV FRm,FRn", 5, 4)}; P.script = kill_at(3, 10);
        P.resume = 1; P.cycles = 40;
        Trace T; run(u, P, T);
        uint32_t c = 0; const uint32_t q = fp::div_s(ONE, TWO, true, c);
        return expect("FR7", u.frg(7), ONE) + expect("FR5", u.frg(5), q) +
               (T.iss[1] == T.resumed + 1 ? "" : " FDIV 2 issued at " + std::to_string(T.iss[1]) + ", kill at " +
                std::to_string(T.resumed)) + (T.chk ? " check" : "");
    });
    V("kill_frees_vec_dst", [](U &u) {
        std::map<int, uint32_t> fr; for (int i = 0; i < 8; i++) fr[i] = ONE;
        fr[8] = TWO;
        u.reset(FPSCR0, fr);
        // FIPR FV0,FV4 (FR7) killed at age 1; FADD FR7,FR8 reads FR7: no pending write may hold it
        Prog P; P.ops = {OP("FIPR FVm,FVn", 1, 0), OP("FADD FRm,FRn", 8, 7)}; P.script = kill_at(1, 10);
        P.resume = 1; P.cycles = 20;
        Trace T; run(u, P, T);
        return expect("FR7", u.frg(7), ONE) + expect("FR8", u.frg(8), THREE) +
               (T.iss[1] == T.resumed + 1 ? "" : " FADD issued at " + std::to_string(T.iss[1]) + ", kill at " +
                std::to_string(T.resumed)) + (T.chk ? " check" : "");
    });
    V("kill_frees_ftrv_rows", [](U &u) {
        std::map<int, uint32_t> fr; for (int i = 0; i < 16; i++) fr[i] = ONE;
        u.reset(FPSCR0, fr);
        // FTRV FV0 killed at age 1; FIPR FV4,FV8 (FR11 = 4) needs the array: the rows must not keep it busy
        Prog P; P.ops = {OP("FTRV XMTRX,FVn", 0), OP("FIPR FVm,FVn", 2, 1)}; P.script = kill_at(1, 10);
        P.resume = 1; P.cycles = 24;
        Trace T; run(u, P, T);
        return expect("FR0", u.frg(0), ONE) + expect("FR11", u.frg(11), 0x40800000u) +
               (T.iss[1] == T.resumed + 1 ? "" : " FIPR issued at " + std::to_string(T.iss[1]) + ", kill at " +
                std::to_string(T.resumed)) + (T.chk ? " check" : "");
    });
    // FTRV FV4 ; FIPR FV0,FV12 ; FTRV FV0 back to back (random-program seed 308619): row 3 of the second FTRV is
    // +inf x 0 -> qNaN
    V("ftrv_fipr_ftrv", [](U &u) {
        const uint32_t b0[16] = {0x4F000000u, 0x4F000000u, 0x00000001u, 0x7F800000u, 0x7FBFFFFFu, 0x7EAAAAAAu, 0x7FBFFFFFu,
                                 0x4BAB0FD6u, 0x7FBFFFFFu, 0x20000000u, 0xFF800000u, 0x9DEEBF1Bu, 0x4B000000u, 0x8ECCE351u,
                                 0x80000000u, 0x7FBFFFFFu};
        const uint32_t b1[16] = {0x00800000u, 0x1F4F1EBFu, 0xFF800000u, 0xA0B00B02u, 0xFF800000u, 0x00000000u, 0x41200000u,
                                 0x40000000u, 0x3FFFFFFFu, 0x4F000000u, 0xE6E68E41u, 0x4B2B0FD6u, 0x807FFFFFu, 0x3EAAAAABu,
                                 0x7FC00000u, 0x00000000u};
        std::map<int, uint32_t> fr;
        for (int i = 0; i < 16; i++) { fr[i] = b0[i]; fr[16 + i] = b1[i]; }
        u.reset(0x00050064u, fr);
        Prog P; P.ops = {OP("FTRV XMTRX,FVn", 1), OP("FIPR FVm,FVn", 3, 0), OP("FTRV XMTRX,FVn", 0)}; P.cycles = 30;
        if (getenv("FH_ALONE")) P.ops = {OP("FTRV XMTRX,FVn", 0)};
        if (getenv("FH_NOFIPR")) P.ops = {OP("FTRV XMTRX,FVn", 1), OP("FTRV XMTRX,FVn", 0)};
        if (getenv("FH_NOFTRV1")) P.ops = {OP("FIPR FVm,FVn", 3, 0), OP("FTRV XMTRX,FVn", 0)};
        Trace T; run(u, P, T);
        std::string r;
        char b[80]; snprintf(b, sizeof b, " (issued %lld %lld %lld)", (long long)T.iss[0], (long long)T.iss[1], (long long)T.iss[2]);
        r += expect("FR3", u.frg(3), 0x7FBFFFFFu);
        return r.empty() ? r : r + b;
    });
    // the cause of an older writer that completed while a younger (then killed) one held the tag comes back
    // (random-program seed 114477): FCMP/GT with a qNaN (cause V) reaches FS while a younger FADD holds the counter;
    // the FADD is killed in its F2
    V("kill_restores_older_cause", [](U &u) {
        u.reset(FPSCR0 | (0x01u << 12), {{2, K_QNAN}, {3, ONE}, {4, ONE}, {5, ONE}});   // cause preset: I
        Prog P; P.ops = {OP("FCMP/GT FRm,FRn", 3, 2), OP("FLDI0 FRn", 9), OP("FADD FRm,FRn", 5, 4)}; P.cycles = 14;
        P.script = kill_at(4, 12);                        // the FADD (ISN 12, issued at 2) in its F2
        Trace T; run(u, P, T);
        return expect("FR5", u.frg(5), ONE) + expect("cause", (u.fpscr() >> 12) & 0x3F, 0x10) + (T.chk ? " check" : "");
    });
    // FIPR special cases (model/fipr/sh4_fipr.h): a NaN product is not an infinity; four infinities of one sign
    V("fipr_nan_inf_cause", [](U &u) {
        // products: +inf (inf x 1), qNaN x -inf, 1 x 1, 1 x 1 -> qNaN, no V
        std::map<int, uint32_t> fr = {{0, 0x7F800000u}, {1, K_QNAN}, {2, ONE}, {3, ONE},
                                      {4, ONE}, {5, 0xFF800000u}, {6, ONE}, {7, ONE}};
        u.reset(FPSCR0, fr);
        Prog P; P.ops = {OP("FIPR FVm,FVn", 1, 0)}; P.cycles = 12;
        Trace T; run(u, P, T);
        return expect("FR7", u.frg(7), 0x7FBFFFFFu) + expect("cause", (u.fpscr() >> 12) & 0x3F, 0);
    });
    V("fipr_four_inf", [](U &u) {
        std::map<int, uint32_t> fr;
        for (int i = 0; i < 4; i++) { fr[i] = 0x7F800000u; fr[4 + i] = ONE; }
        u.reset(FPSCR0, fr);
        Prog P; P.ops = {OP("FIPR FVm,FVn", 1, 0)}; P.cycles = 12;
        Trace T; run(u, P, T);
        return expect("FR7", u.frg(7), 0x7F800000u) + expect("cause", (u.fpscr() >> 12) & 0x3F, 0);
    });
    V("kill_in_write_cycle_flagged", [](U &u) {             // the monitor catches a kill no core can make
        std::map<int, uint32_t> fr; for (int i = 0; i < 8; i++) fr[i] = ONE;
        u.reset(FPSCR0, fr);
        Prog P; P.ops = {OP("FIPR FVm,FVn", 1, 0)}; P.script = kill_at(4, 10); P.cycles = 10;
        Trace T; run(u, P, T);
        return T.chk ? std::string() : " kill_wr_err did not fire";
    });
    for (int at : {1, 3, 5}) {
        V("kill_fsca_" + std::to_string(at), [at](U &u) {
            u.reset(FPSCR0, {{2, 0x11111111u}, {3, 0x22222222u}});
            Prog P; P.ops = {OP("FSCA FPUL,DRn", 1)}; P.script = kill_at((uint64_t)at, 10); P.cycles = 14;
            Trace T; run(u, P, T);
            return expect("FR2", u.frg(2), 0x11111111u) + expect("FR3", u.frg(3), 0x22222222u) + expect("FPSCR", u.fpscr(), FPSCR0);
        });
        V("kill_fsrra_" + std::to_string(at), [at](U &u) {
            u.reset(FPSCR0, {{2, 0x40800000u}});
            Prog P; P.ops = {OP("FSRRA FRn", 2)}; P.script = kill_at((uint64_t)at, 10); P.cycles = 14;
            Trace T; run(u, P, T);
            return expect("FR2", u.frg(2), 0x40800000u) + expect("FPSCR", u.fpscr(), FPSCR0);
        });
    }
    // ---- the multiplier: a MUL.L record (inserted from EX) killed at each eta; LDS.L MACL without its data
    for (int at = 2; at <= 5; at++)
        V("kill_mul_" + std::to_string(at), [at](U &u) {
            u.reset(FPSCR0, {}, 0x11111111u, 0x22222222u);
            Prog P; P.cycles = 14;
            P.script = [at](uint64_t c) {
                Step s;
                if (c == 1) { s.ex = true; s.exop = OP("MUL.L Rm,Rn", 2, 3); s.exisn = 10; s.exa = 7; s.exb = 9; }
                if (c == (uint64_t)at) s.kill = 10;
                return s;
            };
            Trace T; run(u, P, T);
            return expect("MACH", u.rd(31), 0x11111111u) + expect("MACL", u.rd(32), 0x22222222u) + (u.m->idle ? "" : " not idle");
        });
    V("mul_not_killed", [](U &u) {                          // control
        u.reset(FPSCR0, {}, 0x11111111u, 0x22222222u);
        Prog P; P.cycles = 14;
        P.script = [](uint64_t c) {
            Step s;
            if (c == 1) { s.ex = true; s.exop = OP("MUL.L Rm,Rn", 2, 3); s.exisn = 10; s.exa = 7; s.exb = 9; }
            if (c == 2) s.kill = 11;
            return s;
        };
        Trace T; run(u, P, T);
        return expect("MACL", u.rd(32), 63);
    });
    V("kill_ldsl_macl", [](U &u) {
        // the faulting LDS.L @Rm+,MACL: its record is inserted from EX, the fault takes at MA the next cycle (kill >= K)
        // [WI-CORE-A verification F2: MACL must stay, exceptions-intc 3.3]
        u.reset(FPSCR0, {}, 0x44440000u, 0x55550000u);
        Prog P; P.cycles = 14;
        P.script = [](uint64_t c) {
            Step s;
            if (c == 1) { s.ex = true; s.exop = OP("LDS.L @Rm+,MACL", 0, 9); s.exisn = 10; }
            if (c == 2) s.kill = 10;
            return s;
        };
        Trace T; run(u, P, T);
        return expect("MACH", u.rd(31), 0x44440000u) + expect("MACL", u.rd(32), 0x55550000u);
    });
    // ---- FCMP DR: its tag's owner between micro-op 0 and the compare pass
    V("kill_fcmp_dr_pending", [](U &u) {
        u.reset(FPSCR0 | (1u << 19), {{0, 0x3FF00000u}, {2, 0x3FF00000u}});
        Prog P; P.ops = {OP("FCMP/EQ DRm,DRn", 1, 0)}; P.script = kill_at(1, 10); P.cycles = 10;
        Trace T; run(u, P, T);
        return expect("cz_ctr", u.m->cz_ctr, 0) + (T.twe.empty() ? "" : " T written") + (u.m->fcd_v ? " pending" : "");
    });
    V("kill_fcmp_t", [](U &u) {                             // FCMP killed in its F1 cycle: no T
        u.reset(FPSCR0, {{4, ONE}, {5, ONE}});
        Prog P; P.ops = {OP("FCMP/EQ FRm,FRn", 5, 4)}; P.script = kill_at(1, 10); P.cycles = 8;
        Trace T; run(u, P, T);
        return T.twe.empty() ? std::string() : " T written";
    });
    V("fcmp_t_control", [](U &u) {
        u.reset(FPSCR0, {{4, ONE}, {5, ONE}});
        Prog P; P.ops = {OP("FCMP/EQ FRm,FRn", 5, 4)}; P.cycles = 8;
        Trace T; run(u, P, T);
        return T.twe.size() == 1 && T.twe[0].second == 1 && T.twe[0].first == 1 ? std::string() : " no T at F1";
    });

    // ---- traps: the F1 operand trap and its take, per form (V / Z / E), and the trap pass of the D-decided trap
    struct F1 { const char *n; uint16_t op; std::map<int, uint32_t> fr; uint32_t fpscr; bool trap; uint32_t cause; };
    // cause = the expected FPSCR cause field (bits 17:12) after the trap
    const std::vector<F1> f1s = {
        {"fadd_snan_v", OP("FADD FRm,FRn", 3, 2), {{2, K_SNAN}, {3, ONE}}, FPSCR0 | 0x800u, true, 0x10},
        {"fadd_snan_nov", OP("FADD FRm,FRn", 3, 2), {{2, K_SNAN}, {3, ONE}}, FPSCR0, false, 0x10},
        {"fadd_qnan_v", OP("FADD FRm,FRn", 3, 2), {{2, K_QNAN}, {3, ONE}}, FPSCR0 | 0x800u, false, 0},
        {"fsub_infinf_v", OP("FSUB FRm,FRn", 3, 2), {{2, 0x7F800000u}, {3, 0x7F800000u}}, FPSCR0 | 0x800u, true, 0x10},
        {"fmul_zinf_v", OP("FMUL FRm,FRn", 3, 2), {{2, 0}, {3, 0x7F800000u}}, FPSCR0 | 0x800u, true, 0x10},
        {"fmac_snan_v", OP("FMAC FR0,FRm,FRn", 3, 2), {{0, K_SNAN}, {2, ONE}, {3, ONE}}, FPSCR0 | 0x800u, true, 0x10},
        {"fdiv_z", OP("FDIV FRm,FRn", 3, 2), {{2, 0}, {3, ONE}}, FPSCR0 | 0x400u, true, 0x08},   // [HW 0x00048420]
        {"fdiv_z_noen", OP("FDIV FRm,FRn", 3, 2), {{2, 0}, {3, ONE}}, FPSCR0, false, 0x08},
        {"fdiv_00_v", OP("FDIV FRm,FRn", 3, 2), {{2, 0}, {3, 0}}, FPSCR0 | 0x800u, true, 0x10},
        {"fsqrt_neg_v", OP("FSQRT FRn", 3), {{3, 0xBF800000u}}, FPSCR0 | 0x800u, true, 0x10},    // [HW 0x00050840]
        {"fsqrt_pos_v", OP("FSQRT FRn", 3), {{3, ONE}}, FPSCR0 | 0x800u, false, 0},
        {"ftrc_big_v", OP("FTRC FRm,FPUL", 0, 3), {{3, 0x7F000000u}}, FPSCR0 | 0x800u, true, 0x10},
        {"fcmpgt_qnan_v", OP("FCMP/GT FRm,FRn", 3, 2), {{2, K_QNAN}, {3, ONE}}, FPSCR0 | 0x800u, true, 0x10},
        {"fcmpeq_qnan_v", OP("FCMP/EQ FRm,FRn", 3, 2), {{2, K_QNAN}, {3, ONE}}, FPSCR0 | 0x800u, false, 0},
        {"fadd_denorm_e", OP("FADD FRm,FRn", 3, 2), {{2, 0x00000001u}, {3, ONE}}, 0x00000001u, true, 0x20},
        {"fadd_denorm_dn1", OP("FADD FRm,FRn", 3, 2), {{2, 0x00000001u}, {3, ONE}}, FPSCR0, false, 0},
        {"fdiv_denorm_e", OP("FDIV FRm,FRn", 3, 2), {{2, 0x00000001u}, {3, ONE}}, 0x00000001u, true, 0x20},
        {"fadd_dr_snan_v", OP("FADD DRm,DRn", 1, 2), {{2, 0x7FF80000u}, {3, 0}, {4, 0x3FF00000u}, {5, 0}}, FPSCR0 | 0x80800u, true, 0x10},
        {"fdiv_dr_z", OP("FDIV DRm,DRn", 1, 2), {{2, 0x3FF00000u}, {3, 0}, {4, 0}, {5, 0}}, FPSCR0 | 0x80400u, true, 0x08},
        {"fcnvds_snan_v", OP("FCNVDS DRm,FPUL", 0, 1), {{2, 0x7FF80000u}, {3, 0}}, FPSCR0 | 0x80800u, true, 0x10},
    };
    for (auto &t : f1s)
        V("f1_" + std::string(t.n), [t](U &u) {
            u.reset(t.fpscr, t.fr);
            const int n = ((t.op >> 8) & 15);                       // FRn / DRn's upper word (the FPUL forms: a source)
            const uint32_t before = u.frg(n), fp0 = t.fpscr;
            Prog P; P.ops = {t.op, OP("FLDI0 FRn", 15)}; P.cycles = 40;
            Trace T; run(u, P, T);
            std::string r;
            if (t.trap) {
                if (T.f1take != 1 || T.f1isn != 10) r += " no F1 trap at issue + 1 (took at " + std::to_string(T.f1take) + ")";
                if (!T.trapd.empty()) r += " decided at D";
                if (u.frg(n) != before && (t.op & 0xF00F) != 0xF03D) r += expect(" dest", u.frg(n), before);
                const uint32_t want = (fp0 & ~0x3F000u) | (t.cause << 12) | ((t.cause & 0x18u) << 2);
                r += expect("FPSCR", u.fpscr(), want);
                if (!T.twe.empty()) r += " T written";
                if (!u.m->idle) r += " not idle";
                if (T.iss[1] >= 0) r += " the next instruction issued";
            } else {
                if (T.f1take >= 0) r += " unexpected F1 trap";
                if (((u.fpscr() >> 12) & 0x3F) != t.cause) r += expect(" cause", (u.fpscr() >> 12) & 0x3F, t.cause);
            }
            return r + (T.chk ? " check" : "");
        });
    V("f1_trap_overtaken", [](U &u) {
        // an older instruction's kill in the F1 trap's cycle (the core takes the older one): nothing of the FADD lands
        u.reset(FPSCR0 | 0x800u, {{2, K_SNAN}, {3, ONE}});
        Prog P; P.ops = {OP("FADD FRm,FRn", 3, 2)}; P.cycles = 12; P.isn0 = 10;
        P.script = [](uint64_t c) { Step s; if (c == 1) s.kill = 9; return s; };
        Trace T; run(u, P, T);
        return expect("FR3", u.frg(3), ONE) + expect("FPSCR", u.fpscr(), FPSCR0 | 0x800u);
    });
    V("f1_fipr_v_runs_on", [](U &u) {
        // FIPR with inf x 0 and EN.V: the F1 trap is taken, the vector op runs on and writes its cause (V), FR3 stays
        // (model/fipr: "on a trap the destination is left unchanged while cause and flag are still updated")
        std::map<int, uint32_t> fr = {{0, 0x7F800000u}, {1, ONE}, {2, ONE}, {3, ONE}, {4, 0}, {5, ONE}, {6, ONE}, {7, ONE}};
        u.reset(FPSCR0 | 0x800u, fr);
        Prog P; P.ops = {OP("FIPR FVm,FVn", 1, 0)}; P.cycles = 14;
        Trace T; run(u, P, T);
        return (T.f1take == 1 ? "" : " no F1 trap") + expect("FR7", u.frg(7), ONE) +
               expect("FPSCR", u.fpscr(), FPSCR0 | 0x800u | (0x10u << 12) | (0x10u << 2));
    });
    V("f1_fsrra_z_runs_on", [](U &u) {
        u.reset(FPSCR0 | 0x400u, {{2, 0}});
        Prog P; P.ops = {OP("FSRRA FRn", 2)}; P.cycles = 14;
        Trace T; run(u, P, T);
        return (T.f1take == 1 ? "" : " no F1 trap") + expect("FR2", u.frg(2), 0) +
               expect("FPSCR", u.fpscr(), FPSCR0 | 0x400u | (0x08u << 12) | (0x08u << 2));
    });
    // the D-decided trap (SOC.md 4.3.8): a trap pass computes the operation on its untrapped path and writes the
    // computed cause {V, Z, O, U, I}, flags |= it; an E trap writes E alone, flags kept; never the destination.
    // `cause` = the expected cause field (bits 17:12); `want` (when nonzero) = the whole FPSCR [HW fpdn].
    struct Dt { const char *n; uint16_t op; std::map<int, uint32_t> fr; uint32_t fpscr; uint32_t cause; uint32_t want; };
    const uint32_t F15 = 0x3FC00000u;                                                      // 1.5
    const std::vector<Dt> dts = {
        {"fadd_i_exact", OP("FADD FRm,FRn", 3, 2), {{2, ONE}, {3, ONE}}, FPSCR0 | 0x80u, 0, 0},
        {"fadd_i_exact_hw", OP("FADD FRm,FRn", 3, 2), {{2, ONE}, {3, ONE}}, 0x00040080u, 0, 0x00040080u},   // [HW]
        {"fadd_i_inexact", OP("FADD FRm,FRn", 3, 2), {{2, ONE}, {3, 0x33800000u}}, FPSCR0 | 0x80u, 0x01, 0},  // 1 + 2^-24
        {"fadd_i_snan", OP("FADD FRm,FRn", 3, 2), {{2, K_SNAN}, {3, ONE}}, FPSCR0 | 0x80u, 0x10, 0},
        {"fmul_o_ovf", OP("FMUL FRm,FRn", 3, 2), {{2, 0x7F000000u}, {3, 0x7F000000u}}, FPSCR0 | 0x200u, 0x05, 0},   // O I
        {"fmul_u_tiny", OP("FMUL FRm,FRn", 3, 2), {{2, 0x00800000u}, {3, 0x3F000001u}}, FPSCR0 | 0x100u, 0x03, 0}, // U I
        {"fmac_i", OP("FMAC FR0,FRm,FRn", 3, 2), {{0, THREE}, {2, 0x3EAAAAABu}, {3, ONE}}, FPSCR0 | 0x80u, 0x01, 0},
        {"fdiv_o_z", OP("FDIV FRm,FRn", 3, 2), {{2, 0}, {3, ONE}}, FPSCR0 | 0x200u, 0x08, 0},
        {"fdiv_i", OP("FDIV FRm,FRn", 3, 2), {{2, THREE}, {3, ONE}}, FPSCR0 | 0x80u, 0x01, 0},              // 1 / 3
        {"fdiv_i_exact", OP("FDIV FRm,FRn", 3, 2), {{2, TWO}, {3, ONE}}, FPSCR0 | 0x80u, 0, 0},             // 1 / 2
        {"fdiv_o_ovf", OP("FDIV FRm,FRn", 3, 2), {{2, 0x00800000u}, {3, 0x7F000000u}}, FPSCR0 | 0x200u, 0x05, 0},
        {"fsqrt_i", OP("FSQRT FRn", 3), {{3, TWO}}, FPSCR0 | 0x80u, 0x01, 0},
        {"fsqrt_i_exact", OP("FSQRT FRn", 3), {{3, 0x40800000u}}, FPSCR0 | 0x80u, 0, 0},                    // sqrt 4
        {"fsqrt_i_hw", OP("FSQRT FRn", 3), {{3, F15}}, 0x00000080u, 0x01, 0x00001084u},                    // [HW fpdn]
        {"float_i", OP("FLOAT FPUL,FRn", 3), {}, FPSCR0 | 0x80u, 0, 0},
        {"fadd_dr_u", OP("FADD DRm,DRn", 1, 2), {{2, 0x3FF00000u}, {3, 0}, {4, 0x3FF00000u}, {5, 0}}, FPSCR0 | 0x80100u, 0, 0},
        {"fmul_dr_o_hw", OP("FMUL DRm,DRn", 1, 2), {{2, 0x7FEFFFFFu}, {3, 0xFFFFFFFFu}, {4, 0x3FF80000u}, {5, 0}},
         0x00080080u, 0x05, 0x00085094u},                                                                    // [HW fpdn]
        {"fdiv_dr_i_z", OP("FDIV DRm,DRn", 1, 2), {{2, 0x3FF00000u}, {3, 0}, {4, 0}, {5, 0}}, FPSCR0 | 0x80080u, 0x08, 0},
        {"fdiv_dr_iz_z", OP("FDIV DRm,DRn", 1, 2), {{2, 0x3FF00000u}, {3, 0}, {4, 0}, {5, 0}}, FPSCR0 | 0x80480u, 0x08, 0},
        {"fdiv_iz_z", OP("FDIV FRm,FRn", 3, 2), {{2, 0}, {3, ONE}}, FPSCR0 | 0x480u, 0x08, 0},   // D (EN.I) over F1 (EN.Z)
        {"fdiv_dr_i", OP("FDIV DRm,DRn", 1, 2), {{2, 0x3FF00000u}, {3, 0}, {4, 0x40080000u}, {5, 0}}, FPSCR0 | 0x80080u, 0x01, 0},
        {"fsqrt_dr_i", OP("FSQRT DRn", 1), {{2, 0x40000000u}, {3, 0}}, FPSCR0 | 0x80080u, 0x01, 0},       // sqrt 2
        {"fcnvds_o", OP("FCNVDS DRm,FPUL", 0, 1), {{2, 0x47F00000u}, {3, 0}}, FPSCR0 | 0x80200u, 0x05, 0},  // 2^128
        {"fadd_i_denorm_e", OP("FADD FRm,FRn", 3, 2), {{2, 0x00000001u}, {3, ONE}}, 0x0000007Du | 0x80u, 0x20, 0},
        {"fdiv_i_denorm_e", OP("FDIV FRm,FRn", 3, 2), {{2, 0x00000001u}, {3, ONE}}, 0x0000007Du | 0x80u, 0x20, 0},
        {"fdiv_dr_i_denorm_e", OP("FDIV DRm,DRn", 1, 2), {{2, 0x00080000u}, {3, 0}, {4, 0x40080000u}, {5, 0}},
         0x0008007Du | 0x80u, 0x20, 0},                                                                      // denormal / 3
    };
    for (auto &t : dts)
        V("d_" + std::string(t.n), [t](U &u) {
            u.reset(t.fpscr, t.fr);
            const int n = ((t.op >> 8) & 15);
            const bool dbl = (t.fpscr >> 19) & 1;
            const uint32_t before = u.frg(n), before1 = u.frg(n | 1), ul = u.rd(36);
            Prog P; P.ops = {t.op}; P.cycles = 50;
            Trace T; run(u, P, T);
            // flags |= the cause, except E: E alone, flags kept
            const uint32_t want = t.want ? t.want
                                : (t.fpscr & ~0x3F000u) | (t.cause << 12) | (t.cause & 0x20u ? 0 : (t.cause & 0x1Fu) << 2);
            return (T.trapd.size() == 1 && T.iss[0] == 0 ? "" : " no D trap") + expect("dest", u.frg(n), before) +
                   (dbl ? expect("dest+1", u.frg(n | 1), before1) : std::string()) + expect("FPUL", u.rd(36), ul) +
                   expect("FPSCR", u.fpscr(), want) + (u.m->idle ? "" : " not idle") +
                   (T.f1take >= 0 || T.f1_tp ? " also an F1 trap" : "") + (T.recs.empty() ? "" : " a record") +
                   (T.chk ? " check" : "");
        });
    // the divider's trap pass is the untrapped divide's schedule: FPSCR readers (STS FPSCR) wait for its cause as for any
    // cause writer, and the next divide waits for the recurrence -- the handler's first instruction issues in the same
    // cycle as after the same divide untrapped (SOC.md 4.3.8)
    for (int k = 0; k < 4; k++) {
        const bool dr = k >= 2, sts = !(k & 1);
        V(std::string("d_fdiv") + (dr ? "_dr" : "") + (sts ? "_sts_waits" : "_div_busy"), [dr, sts](U &u) {
            const std::map<int, uint32_t> fr = dr ? std::map<int, uint32_t>{{2, 0x3FF00000u}, {3, 0}, {4, 0x40080000u}, {5, 0}}
                                                  : std::map<int, uint32_t>{{2, THREE}, {3, ONE}, {8, TWO}, {9, ONE}};
            const uint32_t fp = FPSCR0 | (dr ? 1u << 19 : 0);
            const uint16_t dv = dr ? OP("FDIV DRm,DRn", 1, 2) : OP("FDIV FRm,FRn", 3, 2);
            const uint16_t nx = sts ? OP("STS FPSCR,Rn", 0) : dr ? OP("FDIV DRm,DRn", 4, 2) : OP("FDIV FRm,FRn", 9, 8);
            // untrapped: FDIV ; the next op in the stream
            u.reset(fp, fr);
            Prog A; A.ops = {dv, nx}; A.cycles = 50;
            Trace TA; run(u, A, TA);
            // trapped (EN.I): the FDIV traps at D, the next op is the handler's, issued from 1 cycle after the trap on
            u.reset(fp | 0x80u, fr);
            Prog B; B.ops = {dv, nx}; B.cycles = 50; B.dresume = 1; B.dgap = 1;
            Trace TB; run(u, B, TB);
            std::string r;
            if (TB.trapd.empty() || TB.trapd[0] != 0) r += " no D trap";   // (the handler's FDIV traps as well: EN.I)
            if (TB.iss[1] < 0 || TB.iss[1] != TA.iss[1])
                r += " the next op issued at " + std::to_string(TB.iss[1]) + " (untrapped " + std::to_string(TA.iss[1]) + ")";
            if (sts) r += expect("FPSCR at STS", TB.fpscr_iss[1], fp | 0x80u | (1u << 12) | (1u << 2));
            return r + (TB.chk ? " check" : "");
        });
    }
    V("d_fdiv_killed_older", [](U &u) {
        // the trap pass is killed by an older instruction's take (a lane-0 partner's fault at its MA): nothing lands and
        // the recurrence stops
        u.reset(FPSCR0 | 0x80u, {{2, THREE}, {3, ONE}});
        Prog P; P.ops = {OP("FDIV FRm,FRn", 3, 2)}; P.cycles = 30; P.isn0 = 10;
        P.script = [](uint64_t c) { Step s; if (c == 2) s.kill = 9; return s; };
        Trace T; run(u, P, T);
        return (T.trapd.size() == 1 ? "" : " no D trap") + expect("FR3", u.frg(3), ONE) + expect("FPSCR", u.fpscr(), FPSCR0 | 0x80u) +
               expect("cz_ctr", u.m->cz_ctr, 0) + (u.m->idle && !u.m->dv_busy ? "" : " not idle / divider busy") +
               (T.chk ? " check" : "");
    });
    V("d_fcmp_dr_v", [](U &u) {
        // FCMP/GT DR with a qNaN and EN.V: decided at D (register-file operands); T never written
        u.reset(FPSCR0 | 0x80800u, {{0, 0x7FF00000u}, {1, 1}, {2, 0x3FF00000u}, {3, 0}});
        Prog P; P.ops = {OP("FCMP/GT DRm,DRn", 1, 0)}; P.cycles = 10;
        Trace T; run(u, P, T);
        return (T.trapd.size() == 1 ? "" : " no D trap") + std::string(T.twe.empty() ? "" : " T written") +
               expect("FPSCR", u.fpscr(), FPSCR0 | 0x80800u | (0x10u << 12) | (0x10u << 2));
    });
    V("d_ftrv_v_cause", [](U &u) {
        // FTRV with EN.V: the trap pass runs its four rows with no destination; the cause is the rows' OR (I)
        std::map<int, uint32_t> fr; for (int i = 0; i < 4; i++) fr[i] = TWO;
        u.reset(FPSCR0 | 0x800u, fr);
        Prog P; P.ops = {OP("FTRV XMTRX,FVn", 0)}; P.cycles = 16;
        Trace T; run(u, P, T);
        std::string r = T.trapd.size() == 1 ? "" : " no D trap";
        for (int i = 0; i < 4; i++) r += expect(("FR" + std::to_string(i)).c_str(), u.frg(i), TWO);
        return r + expect("cause", (u.fpscr() >> 12) & 0x3F, 0x03);   // XMTRX = 0: +0 each row -> U I
    });
}

// ---------------------------------------------------------------------------------------------------------------
// the commit-log records of each writer kind (SOC.md 6.3; v1_fpu.sv's head): kind, index and ISN in order; the value is
// the register's final one (each register is written once here) unless given (FPSCR: without cause / flags)
struct ER { int kind; int idx; int isn; int64_t data = -1; };
static std::string recs_are(U &u, const Trace &T, const std::vector<ER> &want) {
    std::string r;
    char b[200];
    if (T.recs.size() != want.size()) { snprintf(b, sizeof b, " %zu records (want %zu)", T.recs.size(), want.size()); r += b; }
    for (size_t i = 0; i < T.recs.size() && i < want.size(); i++) {
        const soc::CommitRec &x = T.recs[i];
        const ER &w = want[i];
        uint32_t v = 0, mask = 0xFFFFFFFFu;
        if (w.kind == soc::CK_FPR) v = u.frg(w.idx);
        else if (w.idx == soc::SY_FPUL) v = u.rd(36);
        else if (w.idx == soc::SY_MACH) v = u.rd(31);
        else if (w.idx == soc::SY_MACL) v = u.rd(32);
        else if (w.idx == soc::SY_FPSCR) { v = u.fpscr(); mask = ~0x0003F07Cu; }
        if (w.data >= 0) v = (uint32_t)w.data;
        if (x.kind != w.kind || x.idx != w.idx || x.isn != w.isn || (((uint32_t)x.data ^ v) & mask) || x.addr || x.be) {
            snprintf(b, sizeof b, " record %zu: kind %d idx %d isn %d data %08X (want %d %d %d %08X)", i, x.kind, x.idx, x.isn,
                     (uint32_t)x.data, w.kind, w.idx, w.isn, v);
            r += b;
        }
    }
    return r;
}

static void define_rec_vectors() {
    const int F = soc::CK_FPR, S = soc::CK_SYS;
    struct Rv { const char *n; std::vector<uint16_t> ops; std::map<int, uint32_t> fr; uint32_t fpscr; std::vector<ER> want; };
    const std::vector<Rv> rvs = {
        {"fadd", {OP("FADD FRm,FRn", 3, 2)}, {{2, ONE}, {3, ONE}}, FPSCR0, {{F, 3, 10}}},
        {"fadd_bank1", {OP("FADD FRm,FRn", 3, 2)}, {{18, ONE}, {19, TWO}}, FPSCR0 | (1u << 21), {{F, 19, 10}}},
        {"ftrc", {OP("FTRC FRm,FPUL", 0, 3)}, {{3, THREE}}, FPSCR0, {{S, soc::SY_FPUL, 10}}},
        {"fadd_fmul", {OP("FADD FRm,FRn", 3, 2), OP("FMUL FRm,FRn", 5, 4)}, {{2, ONE}, {3, ONE}, {4, TWO}, {5, THREE}}, FPSCR0,
         {{F, 3, 10}, {F, 5, 11}}},
        {"fadd_dr", {OP("FADD DRm,DRn", 1, 2)}, {{2, 0x3FF00000u}, {3, 1}, {4, 0x3FF00000u}, {5, 0}}, FPSCR0 | (1u << 19),
         {{F, 3, 10}, {F, 2, 10}}},                                                // the lower word first
        {"fadd_dr_fadd_dr", {OP("FADD DRm,DRn", 1, 2), OP("FADD DRm,DRn", 3, 2)},       // back to back: the older's upper
         {{2, 0x3FF00000u}, {3, 1}, {4, 0x3FF00000u}, {5, 0}, {6, 0x40000000u}, {7, 0}},  // word is out after the
         FPSCR0 | (1u << 19), {{F, 3, 10}, {F, 2, 10}, {F, 7, 11}, {F, 6, 11}}},           // younger's issue
        {"fcnvds", {OP("FCNVDS DRm,FPUL", 0, 1)}, {{2, 0x40080000u}, {3, 0}}, FPSCR0 | (1u << 19), {{S, soc::SY_FPUL, 10}}},
        {"fdiv", {OP("FDIV FRm,FRn", 3, 2)}, {{2, THREE}, {3, ONE}}, FPSCR0, {{F, 3, 10}}},
        {"fsqrt", {OP("FSQRT FRn", 3)}, {{3, TWO}}, FPSCR0, {{F, 3, 10}}},
        {"fdiv_dr", {OP("FDIV DRm,DRn", 1, 2)}, {{2, 0x3FF00000u}, {3, 0}, {4, 0x40080000u}, {5, 0}}, FPSCR0 | (1u << 19),
         {{F, 3, 10}, {F, 2, 10}}},
        {"fdiv_fadd", {OP("FDIV FRm,FRn", 3, 2), OP("FADD FRm,FRn", 5, 4)}, {{2, THREE}, {3, ONE}, {4, ONE}, {5, ONE}}, FPSCR0,
         {{F, 5, 11}, {F, 3, 10}}},                                                // the older divide lands later
        {"fipr", {OP("FIPR FVm,FVn", 1, 0)}, {{0, ONE}, {1, TWO}, {2, ONE}, {3, ONE}, {4, ONE}, {5, ONE}, {6, ONE}, {7, ONE}},
         FPSCR0, {{F, 7, 10}}},
        {"ftrv", {OP("FTRV XMTRX,FVn", 1)}, {{4, ONE}, {5, ONE}, {6, ONE}, {7, ONE}, {16, TWO}, {21, TWO}}, FPSCR0,
         {{F, 4, 10}, {F, 5, 10}, {F, 6, 10}, {F, 7, 10}}},
        {"fsca", {OP("FSCA FPUL,DRn", 1)}, {}, FPSCR0, {{F, 2, 10}, {F, 3, 10}}},
        {"fsrra", {OP("FSRRA FRn", 2)}, {{2, 0x40800000u}}, FPSCR0, {{F, 2, 10}}},
        {"frchg", {OP("FRCHG")}, {}, 0x00040000u, {{S, soc::SY_FPSCR, 10, 0x00240000u}}},
        {"frchg_frchg", {OP("FRCHG"), OP("FRCHG")}, {}, 0x00040000u,
         {{S, soc::SY_FPSCR, 10, 0x00240000u}, {S, soc::SY_FPSCR, 11, 0x00040000u}}},
        {"fschg_frchg", {OP("FSCHG"), OP("FRCHG")}, {}, 0x00040000u,
         {{S, soc::SY_FPSCR, 10, 0x00140000u}, {S, soc::SY_FPSCR, 11, 0x00340000u}}},
    };
    for (auto &t : rvs)
        V("rec_" + std::string(t.n), [t](U &u) {
            u.reset(t.fpscr, t.fr, 0, 0, 0x2000);
            Prog P; P.ops = t.ops; P.cycles = 40;
            Trace T; run(u, P, T);
            return recs_are(u, T, t.want) + (T.chk ? " check" : "");
        });
    // the multiplier: an EX micro-op of ISN 10 (MACH / MACL as the instruction writes them)
    struct Mv { const char *n; const char *f; std::vector<ER> want; };
    for (auto &t : std::vector<Mv>{{"mul", "MUL.L Rm,Rn", {{S, soc::SY_MACL, 10}}},
                                   {"mulsw", "MULS.W Rm,Rn", {{S, soc::SY_MACL, 10}}},
                                   {"dmuls", "DMULS.L Rm,Rn", {{S, soc::SY_MACH, 10}, {S, soc::SY_MACL, 10}}},
                                   {"clrmac", "CLRMAC", {{S, soc::SY_MACH, 10}, {S, soc::SY_MACL, 10}}},
                                   {"lds_mach", "LDS Rm,MACH", {{S, soc::SY_MACH, 10}}},
                                   {"lds_macl", "LDS Rm,MACL", {{S, soc::SY_MACL, 10}}}})
        V(std::string("rec_") + t.n, [t](U &u) {
            u.reset(FPSCR0, {}, 0x11111111u, 0x22222222u);
            Prog P; P.cycles = 14;
            const char *f = t.f;
            P.script = [f](uint64_t c) {
                Step s;
                if (c == 1) { s.ex = true; s.exop = OP(f, 2, 3); s.exisn = 10; s.exa = 0x70000007u; s.exb = 0x90000009u; }
                return s;
            };
            Trace T; run(u, P, T);
            return recs_are(u, T, t.want) + (T.chk ? " check" : "");
        });
    // a record only in the cycle the register is written: FADD issued at 0 (F1 1, F2 2) held in F2 over cycles 2-6: it
    // moves to FS at the end of cycle 7 and writes at the end of 8 -- one record, in cycle 8
    V("rec_held", [](U &u) {
        u.reset(FPSCR0, {{2, ONE}, {3, ONE}});
        Prog P; P.ops = {OP("FADD FRm,FRn", 3, 2)}; P.cycles = 20;
        P.script = [](uint64_t c) { Step s; s.hp = c >= 2 && c < 7; return s; };
        Trace T; run(u, P, T);
        std::string r = recs_are(u, T, {{soc::CK_FPR, 3, 10}});
        if (!T.recs.empty() && T.recs[0].cycle != 8) r += " written at " + std::to_string(T.recs[0].cycle) + " (want 8)";
        return r + (T.chk ? " check" : "");
    });
    // no record of a killed instruction, of a trap pass, of a taken F1 trap's instruction (the ISN rule of run())
    V("rec_killed_none", [](U &u) {
        u.reset(FPSCR0, {{2, ONE}, {3, ONE}, {6, THREE}, {7, ONE}});
        Prog P; P.ops = {OP("FDIV FRm,FRn", 7, 6), OP("FADD FRm,FRn", 3, 2)}; P.script = kill_at(2, 11); P.cycles = 30;
        Trace T; run(u, P, T);
        return recs_are(u, T, {{soc::CK_FPR, 7, 10}}) + (T.chk ? " check" : "");
    });
}

// trap_d against the opcode x enable table (exceptions-intc 3.8, pm 10; tb/sh4_iss.h fpx_oui): every FE form, every
// enable pattern, PR = 0 and 1
static std::string test_trapd_table(U &u) {
    int bad = 0, n = 0;
    std::string first;
    for (int pr = 0; pr < 2; pr++)
        for (uint32_t en = 0; en < 32; en++) {
            const uint32_t fpscr = FPSCR0 | ((uint32_t)pr << 19) | (en << 7);
            u.reset(fpscr, {});
            for (uint32_t op = 0xF000; op <= 0xFFFF; op++) {
                u.m->s_v0 = 1; u.m->s_op0 = (uint16_t)op; u.eval();
                using namespace sh4iss;
                const Form f = decode((uint16_t)op, fpscr);
                uint32_t oui = 0; bool vd = false;
                switch (f) {
                case F_FADD: case F_FSUB: case F_FMUL: case F_FDIV: case F_FMAC: case F_FADD_D: case F_FSUB_D: case F_FMUL_D:
                case F_FDIV_D: case F_FCNVDS: case F_FIPR: oui = 7; break;
                case F_FTRV: oui = 7; vd = true; break;
                case F_FSQRT: case F_FSQRT_D: case F_FLOAT: case F_FSRRA: case F_FSCA: oui = 1; break;
                default: break;
                }
                const bool want = (en & oui) || (vd && (en & 16));
                n++;
                if ((bool)u.m->trap_d0 != want) {
                    bad++;
                    if (first.empty()) { char b[96]; snprintf(b, sizeof b, " op %04X PR %d EN %02X: trap_d %d want %d", op, pr, en, u.m->trap_d0, want); first = b; }
                }
            }
        }
    printf("trap_d table: %d / %d (opcode, PR, enables) agree with the table\n", n - bad, n);
    return first;
}

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    std::set<std::string> only;
    uint64_t seed = 1;
    int nh = 3000;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--only") && i + 1 < argc) {
            std::string s = argv[++i];
            for (size_t a = 0, b; a <= s.size(); a = b + 1) { b = s.find(',', a); if (b == std::string::npos) b = s.size(); only.insert(s.substr(a, b - a)); }
        } else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = strtoull(argv[++i], nullptr, 0);
        else if (!strcmp(argv[i], "--n") && i + 1 < argc) nh = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-v")) g_verbose = true;
    }
    auto m = std::make_unique<Vv1_fpuhold_unit>();
    U u(m.get());
    int fails = 0;
    auto want = [&](const char *p) { return only.empty() || only.count(p); };
    if (want("hold")) { std::string r = test_hold(u, seed, nh); if (!r.empty()) { printf("FAIL hold: %s\n", r.c_str()); fails++; } }
    if (want("holdval")) {
        g_rec_msg.clear();
        std::string r = test_holdval(u, seed, nh);
        if (!g_rec_msg.empty()) r += " records:" + g_rec_msg;
        if (!r.empty()) { printf("FAIL holdval: %s\n", r.c_str()); fails++; }
    }
    if (want("kill") || want("trap") || want("rec")) {
        define_vectors();
        define_rec_vectors();
        int np = 0, nf = 0;
        for (auto &v : g_vecs) {
            const bool isk = v.name.rfind("kill", 0) == 0 || v.name.find("_not_killed") != std::string::npos ||
                             v.name.find("control") != std::string::npos || v.name == "older_fdiv_completes";
            const bool isr = v.name.rfind("rec_", 0) == 0;
            if (!want(isr ? "rec" : isk ? "kill" : "trap") && !only.count(v.name)) continue;
            g_rec_msg.clear();
            std::string r = v.f(u);
            if (!g_rec_msg.empty() && v.name != "kill_in_write_cycle_flagged") r += " records:" + g_rec_msg;
            if (r.empty()) np++; else { nf++; printf("FAIL %-28s%s\n", v.name.c_str(), r.c_str()); }
            if (g_verbose && r.empty()) printf("pass %s\n", v.name.c_str());
        }
        printf("kill / trap vectors: %d pass, %d fail\n", np, nf);
        fails += nf;
    }
    if (want("trap")) { std::string r = test_trapd_table(u); if (!r.empty()) { printf("FAIL trap_d table:%s\n", r.c_str()); fails++; } }
    printf("v1-fpuhold unit: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
