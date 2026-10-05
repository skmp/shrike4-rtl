// v1_fpdn_tb.cpp -- make v1-fpdn, the unit part (WI-FPDN; rtl/v1/SOC.md 4.3.8, 8.1, 9.17): FPSCR.DN = 0 in v1_fp_pkg,
// v1_fdiv and the vector unit (tb/v1_fpdn_tb.sv), against the console and against tb/sh4_fpdn.h.
//
//   --hw     every record of the console probe (tools/hw/fpdn: fpdn_p1.bin class table, 7 FPSCR settings; fpdn_p2.bin
//            gradual-underflow values, DN = 0 RN / RZ and DN = 1; fpdn_p3.bin the double forms on the two-word
//            palette) through the RTL: the operation's value / cause / E from the RTL functions, the divider
//            (FDIV / FSQRT: fdiv_spec's E, v1_fdiv's value) and the vector unit (FSRRA), composed into the instruction's
//            effect by sh4fpdn::compose (the trap / FPSCR rules the console shows); must equal the record.  FABS / FNEG
//            (moves) are not RTL functions: skipped.
//   --rand N every value function, the divider and FSRRA's specials against sh4fpdn::eval at the operation level
//            (value, cause, E, T) on N random operands per operation x RN / RZ x DN = 0 / 1: operand classes over both
//            words of a double (a fraction only in the low / high word), denormals, NaNs, and the part 2 generator's
//            tiny-result pairs at other indices.
//   --vec    the vector unit with DN = 0 against the console: model/fipr/hw_specials fipr_dn0_rnrz.bin (1M FIPR vectors
//            with denormal operands and results in the underflow range, RN and RZ, streamed at pitch 1), the DN = 0 and
//            DN = 1 rows of fipr_specials.txt and ftrv_specials.txt (value, and cause / flags / trap composed as
//            model/fipr's full semantics), and model/fsrra/hw_specials/fpu_specials.txt (FSRRA special operands x DN x
//            RM x enables: result, FPSCR, trap).
// With no mode, all three (--rand 200000).  Exit 0 only when everything matches.
#include "Vv1_fpdn_tb.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>
#include "sh4_fpdn.h"
#include "../tools/hw/fpdn/fpdn_vec.h"
#include "../model/sh4_fpu_approx.h"
#include "../model/fipr/hw_specials/vecgen_dn0_ftrv.h"
#undef SNAN                                      // (math.h's; the vector file names its own)
#include "../model/fipr/hw_specials/ftrv_specials_vectors.h"

#ifndef FPDN_ROOT
#define FPDN_ROOT "."
#endif
using namespace sh4fpdn;
static Vv1_fpdn_tb *dut;
static std::string g_root = FPDN_ROOT;
static void tick() { dut->clk = 0; dut->eval(); dut->clk = 1; dut->eval(); }

// ---- the RTL at the operation level ---------------------------------------------------------------------------------
struct RR { u64 v; u32 c; bool e, spec; u32 cmp; };
static RR fn(int op, u64 a, u64 b, u32 c, bool rz, bool dn0, bool sq = false, bool dbl = false) {
    dut->op = op; dut->a = a; dut->b = b; dut->cc = c; dut->rz = rz; dut->dn0 = dn0; dut->sq = sq; dut->dbl = dbl;
    dut->eval();
    return {dut->rr, (u32)dut->x, (bool)dut->e, (bool)dut->spec, (u32)dut->cmp};
}
static R divide(bool sq, bool dbl, u64 a, u64 b, bool rz, bool dn0) {
    dut->d_start = 1; dut->d_sqrt = sq; dut->d_dbl = dbl; dut->d_a = a; dut->d_b = b; dut->d_rz = rz; dut->d_dn0 = dn0;
    tick();
    dut->d_start = 0;
    for (int k = 0; k < 40; k++) {
        tick();
        if (dut->d_done) return {dut->d_r, (u32)dut->d_x};
    }
    fprintf(stderr, "v1_fdiv: no done\n");
    exit(2);
}
// FSRRA through the vector unit: {value, {E, cause}}
static R fsrra_rtl(u32 x, bool rz, bool dn0) {
    dut->v_start = 1; dut->v_op = 2; dut->v_rz = rz; dut->v_dn0 = dn0; dut->v_frn = x;
    tick();
    dut->v_start = 0;
    for (int k = 0; k < 12; k++) {
        if (dut->v_valid) return {dut->v_res, (u32)dut->v_x | (dut->v_e ? C_E : 0u)};
        tick();
    }
    fprintf(stderr, "fpu_vec_top: no FSRRA result\n");
    exit(2);
}
static const u64 DSGN = 0x8000000000000000ull;
// the operation's result as sh4fpdn::eval defines it, computed by the RTL
static OpRes rtl_eval(int op, const u32 in[5], u32 fpscr) {
    const bool rz = fpscr & 1, dn0 = !((fpscr >> 18) & 1);
    const u64 da = ((u64)in[0] << 32) | in[1], db = ((u64)in[2] << 32) | in[3];
    OpRes o{{0, 0}, 0, false};
    RR q{};
    auto val = [&](const RR &x, int wr) { o.r.v = x.v; o.r.c = x.c | (x.e ? C_E : 0u); o.wr = wr; };
    auto div = [&](bool sq, bool dbl, u64 a, u64 b, int wr) {
        const RR sp = fn(11, a, b, 0, rz, dn0, sq, dbl);
        const R d = divide(sq, dbl, a, b, rz, dn0);
        o.r.v = d.v; o.r.c = sp.e ? C_E : d.c; o.wr = wr;
    };
    auto cmp = [&](const RR &x, bool gt) {
        const bool eq = (x.cmp >> 3) & 1, g = (x.cmp >> 2) & 1, veq = (x.cmp >> 1) & 1, vgt = x.cmp & 1;
        o.r.c = (gt ? vgt : veq) ? C_V : 0u; o.t = gt ? g : eq; o.wr = 5;
    };
    switch (op) {
    case FADD_S: val(fn(0, in[0], in[1], 0, rz, dn0), 1); break;
    case FSUB_S: val(fn(0, in[0], in[1] ^ 0x80000000u, 0, rz, dn0), 1); break;
    case FMUL_S: val(fn(1, in[0], in[1], 0, rz, dn0), 1); break;
    case FDIV_S: div(false, false, in[0], in[1], 1); break;
    case FCMPEQ_S: case FCMPGT_S: q = fn(5, in[0], in[1], 0, rz, dn0); cmp(q, op == FCMPGT_S); break;
    case FMAC_S: val(fn(2, in[0], in[1], in[2], rz, dn0), 4); break;
    case FSQRT_S: div(true, false, in[0], 0, 1); break;
    case FTRC_S: val(fn(7, in[0], 0, 0, rz, dn0), 3); break;
    case FADD_D: val(fn(3, da, db, 0, rz, dn0), 2); break;
    case FSUB_D: val(fn(3, da, db ^ DSGN, 0, rz, dn0), 2); break;
    case FMUL_D: val(fn(4, da, db, 0, rz, dn0), 2); break;
    case FDIV_D: div(false, true, da, db, 2); break;
    case FCMPEQ_D: case FCMPGT_D: q = fn(6, da, db, 0, rz, dn0); cmp(q, op == FCMPGT_D); break;
    case FSQRT_D: div(true, true, da, 0, 2); break;
    case FTRC_D: val(fn(8, da, 0, 0, rz, dn0), 3); break;
    case FCNVDS: val(fn(10, da, 0, 0, rz, dn0), 3); break;
    case FCNVSD: val(fn(9, in[4], 0, 0, rz, dn0), 2); break;
    default: break;
    }
    return o;
}
// the instruction's effect with the RTL's operation result (FSRRA: the vector unit, composed as sh4_fsrra_ex)
static bool rtl_exec(int op, const u32 in[5], u32 fpscr, Mach &m) {
    if (op == FABS_S || op == FNEG_S || op == FABS_D || op == FNEG_D) return false;
    if (op == FSRRA_S) {
        for (int i = 0; i < 4; i++) m.fr[i] = in[i];
        m.fpul = in[4]; m.t = 1;
        const R r = fsrra_rtl(in[0], fpscr & 1, !((fpscr >> 18) & 1));
        const u32 en = (fpscr >> 7) & 0x1F;
        m.fpscr = (fpscr & ~0x3F000u) | (r.c << 12) | ((r.c & 0x1F) << 2);
        m.trap = (r.c & C_E) || (r.c & en & 0x1F) || (en & C_I);
        if (!m.trap) m.fr[0] = (u32)r.v;
        return true;
    }
    m = compose(op, in, fpscr, 1, rtl_eval(op, in, fpscr));
    return true;
}

// ---- --hw: the console records ----------------------------------------------------------------------------------------
static std::vector<fpdn_rec_t> load(const std::string &p) {
    std::vector<fpdn_rec_t> v;
    FILE *f = fopen(p.c_str(), "rb");
    if (!f) { fprintf(stderr, "cannot read %s\n", p.c_str()); exit(2); }
    fpdn_rec_t r;
    while (fread(&r, sizeof r, 1, f) == 1) v.push_back(r);
    fclose(f);
    return v;
}
static int run_hw() {
    const std::string d = g_root + "/tools/hw/fpdn/";
    const auto P1 = load(d + "fpdn_p1.bin"), P2 = load(d + "fpdn_p2.bin"), P3 = load(d + "fpdn_p3.bin");
    std::map<std::string, std::pair<long, long>> tab;       // part/op -> (checked, bad)
    long bad = 0, tot = 0, skip = 0, shown = 0;
    auto chk = [&](const char *part, int op, const u32 in[5], u32 fpscr, const fpdn_rec_t &h, const char *lab) {
        Mach m;
        if (!rtl_exec(op, in, fpscr, m)) { skip++; return; }
        const bool ok = m.fr[0] == h.fr[0] && m.fr[1] == h.fr[1] && m.fr[2] == h.fr[2] && m.fr[3] == h.fr[3] &&
                        m.fpul == h.fpul && m.fpscr == h.fpscr && m.t == h.t && m.trap == (h.trap != 0);
        auto &t = tab[std::string(part) + " " + FPDN_OPS[op].name];
        t.first++; tot++;
        if (!ok) {
            t.second++; bad++;
            if (shown++ < 12)
                printf("  MISMATCH %s %s fpscr=%08x %s in=%08x %08x %08x %08x ul=%08x: console fr=%08x %08x %08x %08x "
                       "ul=%08x fpscr=%08x t=%u trap=%x, RTL fr=%08x %08x %08x %08x ul=%08x fpscr=%08x t=%u trap=%d\n",
                       part, FPDN_OPS[op].name, fpscr, lab, in[0], in[1], in[2], in[3], in[4], h.fr[0], h.fr[1], h.fr[2],
                       h.fr[3], h.fpul, h.fpscr, h.t, h.trap, m.fr[0], m.fr[1], m.fr[2], m.fr[3], m.fpul, m.fpscr, m.t, m.trap);
        }
    };
    size_t k = 0;
    for (int op = 0; op < P_NOPS; op++)
        for (int c = 0; c < FPDN_P1_NCFG; c++)
            for (int row = 0; row < fpdn_p1_rows(op); row++, k++) {
                u32 in[5]; fpdn_p1_inputs(op, row, in);
                char lab[48];
                snprintf(lab, sizeof lab, "[%s %s %s]", FPDN_PAL_NAME[row % 14], FPDN_PAL_NAME[(row / 14) % 14], FPDN_PAL_NAME[row / 196]);
                chk("p1", op, in, FPDN_P1_CFG[c] | (FPDN_OPS[op].pr ? 0x80000u : 0), P1.at(k), lab);
            }
    k = 0;
    for (int v = 0; v < V_NOPS; v++)
        for (int c = 0; c < FPDN_P2_NCFG; c++)
            for (u32 i = 0; i < FPDN_P2_N; i++, k++) {
                u32 in[5]; fpdn_p2_inputs(v, i, in);
                const int op = FPDN_VOP[v];
                char lab[24]; snprintf(lab, sizeof lab, "[#%u]", i);
                chk("p2", op, in, FPDN_P2_CFG[c] | (FPDN_OPS[op].pr ? 0x80000u : 0), P2.at(k), lab);
            }
    k = 0;
    for (int q = 0; q < FPDN_P3_NOPS; q++) {
        const int op = FPDN_P3_OP[q];
        for (int c = 0; c < FPDN_P3_NCFG; c++)
            for (int row = 0; row < fpdn_p3_rows(op); row++, k++) {
                u32 in[5]; fpdn_p3_inputs(op, row, in);
                char lab[48]; snprintf(lab, sizeof lab, "[%s %s]", FPDN_PAL3_NAME[row % FPDN_NPAL3], FPDN_PAL3_NAME[row / FPDN_NPAL3]);
                chk("p3", op, in, FPDN_P3_CFG[c] | 0x80000u, P3.at(k), lab);
            }
    }
    for (auto &t : tab) if (t.second.second) printf("  %-18s %6ld / %6ld differ\n", t.first.c_str(), t.second.second, t.second.first);
    printf("hw: %ld console records through the RTL (v1_fp_pkg, v1_fdiv, fpu_vec_top), %ld differ; %ld FABS / FNEG "
           "records skipped (moves)\n", tot, bad, skip);
    return bad ? 1 : 0;
}

// ---- --rand: the operation level against sh4_fpdn.h ----------------------------------------------------------------------
struct Rng { u64 s; explicit Rng(u64 x) : s(x * 0x9E3779B97F4A7C15ull + 7) {} u64 next() { s = fpdn_mix(s); return s; }
             u32 u(u32 n) { return (u32)(next() % n); } };
static u32 rnd_s(Rng &r) {
    const u32 s = r.u(2) << 31, k = r.u(16);
    const u32 m = fpdn_man_s(r.next(), (int)r.u(5));
    switch (k) {
    case 0: return s;                                             // zero
    case 1: case 2: case 3: return s | (m ? m : 1);                 // denormal
    case 4: return s | 0x7F800000u;                                 // inf
    case 5: return s | 0x7F800000u | ((m & 0x3FFFFFu) ? (m & 0x3FFFFFu) : 1);   // quiet NaN (bit 22 = 0)
    case 6: return s | 0x7FC00000u | (m & 0x3FFFFFu);               // signalling NaN
    case 7: case 8: return s | ((1 + r.u(30)) << 23) | m;           // near the minimum normal
    case 9: return s | ((200 + r.u(55)) << 23) | m;                 // large
    default: return s | ((1 + r.u(254)) << 23) | m;
    }
}
static u64 rnd_d(Rng &r) {
    const u64 s = (u64)r.u(2) << 63, k = r.u(20);
    const u64 m = fpdn_man_d(r.next(), (int)r.u(5)), lo = r.u(2) ? r.next() & 0xFFFFFFFFull : (1ull << r.u(32));
    const u64 E = 0x7FF0000000000000ull;
    switch (k) {
    case 0: return s;
    case 1: case 2: return s | (m ? m : 1);                         // denormal
    case 3: return s | (lo ? lo : 1);                               // denormal, fraction in the low word only
    case 4: return s | (((u64)(1 + r.u(0xFFFFF)) << 32) | (r.u(2) ? lo : 0));   // denormal, high word
    case 5: return s | E;
    case 6: return s | E | (lo ? lo : 1);                           // quiet NaN, low word only
    case 7: return s | E | ((u64)(1 + r.u(0x7FFFF)) << 32) | (r.u(2) ? lo : 0);   // quiet NaN, high word
    case 8: return s | E | 0x0008000000000000ull | (m & 0x0007FFFFFFFFFFFFull);    // signalling NaN
    case 9: case 10: return s | ((u64)(1 + r.u(60)) << 52) | m;     // near the minimum normal
    case 11: return s | ((u64)(1 + r.u(2046)) << 52) | (m & ~0xFFFFFFFFull);      // a normal with a zero low word
    default: return s | ((u64)(1 + r.u(2046)) << 52) | m;
    }
}
static int run_rand(long n) {
    static const int OPS[] = {FADD_S, FSUB_S, FMUL_S, FDIV_S, FCMPEQ_S, FCMPGT_S, FMAC_S, FSQRT_S, FTRC_S, FADD_D, FSUB_D,
                              FMUL_D, FDIV_D, FCMPEQ_D, FCMPGT_D, FSQRT_D, FTRC_D, FCNVDS, FCNVSD, FSRRA_S};
    static const int P2OF[NOPS] = {V_FADD_S, V_FSUB_S, V_FMUL_S, V_FDIV_S, V_FCMPEQ_S, V_FCMPGT_S, V_FMAC_S, -1, -1, -1, -1, -1,
                                   V_FADD_D, V_FSUB_D, V_FMUL_D, V_FDIV_D, V_FCMPEQ_D, V_FCMPGT_D, -1, -1, -1, -1, V_FCNVDS, -1};
    long bad = 0, tot = 0, ne = 0, nden = 0;
    std::map<std::string, long> byop;
    for (int op : OPS) {
        Rng r(1000 + (u64)op);
        const bool dbl = op >= FADD_D && op != FCNVSD && op != FSRRA_S;
        const long nop = (op == FDIV_S || op == FSQRT_S || op == FDIV_D || op == FSQRT_D || op == FSRRA_S) ? n / 4 : n;
        for (long i = 0; i < nop; i++) {
            u32 in[5];
            if (P2OF[op] >= 0 && (i & 3) == 0) fpdn_p2_inputs(P2OF[op], FPDN_P2_N + (u32)i, in);   // tiny results
            else if (dbl) { const u64 a = rnd_d(r), b = rnd_d(r); fpdn_load_d(in, K_BIN_D, a, b); }
            else { in[0] = rnd_s(r); in[1] = rnd_s(r); in[2] = rnd_s(r); in[3] = 0; in[4] = rnd_s(r); }
            const u32 fpscr = (r.u(2) ? 0x00040000u : 0) | r.u(2) | (dbl ? 0x80000u : 0);
            OpRes w, g;
            if (op == FSRRA_S) {
                u32 f = fpscr & ~0x80000u, x = in[0];
                sh4_fsrra_ex(in[0], &f, &x);
                w.r.c = (f >> 12) & 0x3F; w.r.v = x; w.wr = 1;
                const R q = fsrra_rtl(in[0], fpscr & 1, !((fpscr >> 18) & 1));
                g.r = q; g.wr = 1;
                if (w.r.c & C_E) { w.r.v = 0; g.r.v = 0; }         // the destination is not written
            } else { w = eval(op, in, fpscr); g = rtl_eval(op, in, fpscr); }
            const bool e = w.r.c & C_E;
            const bool ok = (w.r.c == g.r.c) && (e || w.r.v == g.r.v) && w.t == g.t;
            tot++; ne += e; nden += !e && (w.r.c & C_U);
            if (!ok) {
                bad++; byop[FPDN_OPS[op].name]++;
                if (bad <= 12)
                    printf("  MISMATCH %s fpscr=%08x in=%08x %08x %08x %08x ul=%08x: model v=%016llx c=%02x t=%d, RTL "
                           "v=%016llx c=%02x t=%d\n", FPDN_OPS[op].name, fpscr, in[0], in[1], in[2], in[3], in[4],
                           (unsigned long long)w.r.v, w.r.c, w.t, (unsigned long long)g.r.v, g.r.c, g.t);
            }
        }
    }
    for (auto &b : byop) printf("  %-10s %ld differ\n", b.first.c_str(), b.second);
    printf("rand: %ld operations against sh4_fpdn.h (DN = 0 / 1, RN / RZ; %ld E, %ld underflowing results), %ld differ\n",
           tot, ne, nden, bad);
    return bad ? 1 : 0;
}

// ---- --vec: the vector unit with DN = 0 against the console --------------------------------------------------------------
static u32 fipr_issue_one(const u32 a[4], const u32 b[4], bool rz, bool dn0, u32 &x) {
    dut->v_op = 0; dut->v_rz = rz; dut->v_dn0 = dn0;
    dut->fva0 = a[0]; dut->fva1 = a[1]; dut->fva2 = a[2]; dut->fva3 = a[3];
    dut->fvb0 = b[0]; dut->fvb1 = b[1]; dut->fvb2 = b[2]; dut->fvb3 = b[3];
    dut->v_start = 1; tick(); dut->v_start = 0;
    for (int k = 0; k < 8; k++) {
        if (dut->v_valid) { x = dut->v_x | (dut->v_e ? C_E : 0u); return dut->v_res; }
        tick();
    }
    fprintf(stderr, "fpu_vec_top: no FIPR result\n");
    exit(2);
}
static int run_vec() {
    long bad = 0;
    // 1. FIPR with DN = 0 on the 1M console vectors, RN and RZ, streamed at pitch 1 (a start every cycle)
    {
        const std::string p = g_root + "/model/fipr/hw_specials/fipr_dn0_rnrz.bin";
        FILE *f = fopen(p.c_str(), "rb");
        if (!f) { fprintf(stderr, "cannot read %s\n", p.c_str()); return 1; }
        std::vector<u32> hw(2 * (size_t)DN0_N);
        if (fread(hw.data(), 4, hw.size(), f) != hw.size()) { fprintf(stderr, "short %s\n", p.c_str()); return 1; }
        fclose(f);
        long nb = 0, nc = 0, n = 0;
        std::vector<std::pair<u32, int>> pend;            // (index, rz) in issue order
        size_t got = 0;
        auto collect = [&]() {
            if (!dut->v_valid) return;
            const auto q = pend.at(got++);
            u32 a[4], b[4], fs = (u32)q.second, r = 0;
            dn0_vec(q.first, a, b);
            const u32 want = hw[2 * (size_t)q.first + (size_t)q.second];
            if (dut->v_res != want) { if (nb++ < 5) printf("  FIPR DN=0 #%u rz %d: RTL %08x console %08x\n", q.first, q.second, dut->v_res, want); }
            // the cause: model/fipr's (I, U for a zero / denormal-range result, O)
            sh4_fipr_ex(a, b, &fs, &r);
            if ((u32)dut->v_x != ((fs >> 12) & 0x1F) || dut->v_e) { if (nc++ < 5) printf("  FIPR DN=0 #%u rz %d: cause %02x, model %02x\n", q.first, q.second, (u32)dut->v_x, (fs >> 12) & 0x1F); }
            n++;
        };
        for (u32 i = 0; i < DN0_N; i++)
            for (int rz = 0; rz < 2; rz++) {
                u32 a[4], b[4];
                dn0_vec(i, a, b);
                dut->v_op = 0; dut->v_rz = rz; dut->v_dn0 = 1;
                dut->fva0 = a[0]; dut->fva1 = a[1]; dut->fva2 = a[2]; dut->fva3 = a[3];
                dut->fvb0 = b[0]; dut->fvb1 = b[1]; dut->fvb2 = b[2]; dut->fvb3 = b[3];
                dut->v_start = 1;
                pend.push_back({i, rz});
                tick();
                collect();
            }
        dut->v_start = 0;
        for (int k = 0; k < 8; k++) { tick(); collect(); }
        printf("vec: FIPR DN = 0, %ld of %u console values (RN, RZ) streamed at pitch 1: %ld values differ, %ld causes differ "
               "from model/fipr\n", n, 2 * DN0_N, nb, nc);
        bad += nb + nc + (n != 2L * DN0_N);
    }
    // 2. fipr_specials.txt / ftrv_specials.txt: value, cause, flags, trap (PR = 0 rows; PR = 1 is v1_fpu's no-op)
    {
        long n = 0, nb = 0;
        char line[512];
        const std::string p = g_root + "/model/fipr/hw_specials/fipr_specials.txt";
        FILE *f = fopen(p.c_str(), "r");
        if (!f) { fprintf(stderr, "cannot read %s\n", p.c_str()); return 1; }
        while (fgets(line, sizeof line, f)) {
            u32 cfg, v, a[4], b[4], res, fp;
            char tr[16];
            if (sscanf(line, "FIPR cfg=%x v=%u a=%x,%x,%x,%x b=%x,%x,%x,%x -> res=%x fpscr=%x %*s %*s %15s", &cfg, &v, &a[0],
                       &a[1], &a[2], &a[3], &b[0], &b[1], &b[2], &b[3], &res, &fp, tr) != 13) continue;
            if (cfg & 0x80000u) continue;
            u32 x;
            const u32 r = fipr_issue_one(a, b, cfg & 1, !((cfg >> 18) & 1), x);
            const u32 en = (cfg >> 7) & 0x1F, f2 = (cfg & ~0x3F000u) | (x << 12) | ((x & 0x1F) << 2);
            const bool trap = (en & 7) || (en & x & 16);
            const u32 dst = trap ? b[3] : r;
            const bool ok = dst == res && f2 == fp && trap == (strncmp(tr, "TRAP", 4) == 0);
            n++;
            if (!ok) { if (nb++ < 5) printf("  FIPR special cfg=%08x v=%u: RTL res=%08x fpscr=%08x trap=%d, console %s", cfg, v, dst, f2, trap, line); }
        }
        fclose(f);
        const std::string p2 = g_root + "/model/fipr/hw_specials/ftrv_specials.txt";
        f = fopen(p2.c_str(), "r");
        if (!f) { fprintf(stderr, "cannot read %s\n", p2.c_str()); return 1; }
        long nt = 0;
        while (fgets(line, sizeof line, f)) {
            u32 cfg, t, res[4], fp;
            char tr[16];
            if (sscanf(line, "FTRV cfg=%x t=%u -> res=%x,%x,%x,%x fpscr=%x %*s %*s %15s", &cfg, &t, &res[0], &res[1], &res[2],
                       &res[3], &fp, tr) != 8) continue;
            if (cfg & 0x80000u || t >= NT) continue;
            u32 c = 0, out[4];
            for (int i = 0; i < 4; i++) {                   // row i: the matrix row is recoded (port B), the vector port A
                u32 x;
                out[i] = fipr_issue_one(TS[t].v, TS[t].row[i], cfg & 1, !((cfg >> 18) & 1), x);
                c |= x;
            }
            const u32 en = (cfg >> 7) & 0x1F, f2 = (cfg & ~0x3F000u) | (c << 12) | ((c & 0x1F) << 2);
            const bool trap = (en & (1 | 2 | 4 | 16)) != 0;
            bool ok = f2 == fp && trap == (strncmp(tr, "TRAP", 4) == 0);
            for (int i = 0; i < 4; i++) ok = ok && (trap ? TS[t].v[i] : out[i]) == res[i];
            nt++;
            if (!ok) { if (nb++ < 10) printf("  FTRV special cfg=%08x t=%u: RTL %08x %08x %08x %08x fpscr=%08x, console %s", cfg, t, out[0], out[1], out[2], out[3], f2, line); }
        }
        fclose(f);
        printf("vec: %ld FIPR and %ld FTRV console special-operand rows (DN = 0 and 1, RN / RZ, enables): %ld differ\n", n, nt, nb);
        bad += nb;
    }
    // 3. FSRRA special operands (model/fsrra/hw_specials/fpu_specials.txt: x DN RM EN)
    {
        long n = 0, nb = 0;
        char line[512];
        const std::string p = g_root + "/model/fsrra/hw_specials/fpu_specials.txt";
        FILE *f = fopen(p.c_str(), "r");
        if (!f) { fprintf(stderr, "cannot read %s\n", p.c_str()); return 1; }
        while (fgets(line, sizeof line, f)) {
            u32 x, dn, rm, en, res, fp;
            char tr[16];
            if (sscanf(line, "FSRRA x=%x DN=%u RM=%u EN=%x -> res=%x fpscr=%x %*s %*s %15s", &x, &dn, &rm, &en, &res, &fp, tr) != 7) continue;
            const u32 cfg = (dn << 18) | en | rm;
            Mach m;
            const u32 in[5] = {x, 0, 0, 0, 0};
            rtl_exec(FSRRA_S, in, cfg, m);
            const bool ok = m.fr[0] == res && m.fpscr == fp && m.trap == (strncmp(tr, "TRAP", 4) == 0);
            n++;
            if (!ok) { if (nb++ < 5) printf("  FSRRA special: RTL res=%08x fpscr=%08x trap=%d, console %s", m.fr[0], m.fpscr, m.trap, line); }
        }
        fclose(f);
        printf("vec: %ld FSRRA console special-operand rows (DN = 0 and 1): %ld differ\n", n, nb);
        bad += nb;
    }
    return bad ? 1 : 0;
}

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    bool hw = false, vec = false, any = false;
    long rnd = 0;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--hw") { hw = true; any = true; }
        else if (a == "--vec") { vec = true; any = true; }
        else if (a == "--rand" && i + 1 < argc) { rnd = atol(argv[++i]); any = true; }
        else if (a == "--root" && i + 1 < argc) g_root = argv[++i];
        else if (a[0] == '+') continue;
        else { fprintf(stderr, "usage: %s [--hw] [--rand N] [--vec] [--root DIR]\n", argv[0]); return 2; }
    }
    if (!any) { hw = true; vec = true; rnd = 200000; }
    dut = new Vv1_fpdn_tb;
    dut->clk = 0; dut->rst = 1; dut->d_start = 0; dut->v_start = 0;
    tick(); tick(); dut->rst = 0; tick();
    int fail = 0;
    if (hw) fail |= run_hw();
    if (rnd) fail |= run_rand(rnd);
    if (vec) fail |= run_vec();
    printf("v1-fpdn unit: %s\n", fail ? "FAIL" : "PASS");
    delete dut;
    return fail;
}
