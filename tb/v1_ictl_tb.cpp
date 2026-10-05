// v1_ictl_tb.cpp -- WI-0b unit check of the SoC decoder facts that rtl/v1/v1_pkg.sv adds to ictl_t (rtl/v1/SOC.md
// 1.3.1): priv, slot_ill, fpu_insn, dkind, rmw, memop, fpx_oui / fpx_vd, form_x, illegal.
//
// For every 16-bit code in the FPU modes M0 (PR = SZ = 0), M1 (SZ = 1) and M2 (PR = 1), both FR banks, the RTL answer
// (tp_decode + v1_ictl_op, tb/v1_ictl_tb.sv) is compared with a reference written here from the manual's lists
// (exceptions-intc 3.6-3.8, pm 5.x (10) / (11), pm 10) on top of tb/sh4_iss.h's decoder, which is independent of
// tp_decode (it is pm Section 9's instruction map).  Also checked: tp_decode and the ISS agree on which codes decode,
// and the three v1-side forms keep sane v1 fields (H'0000 is MT NOP, TRAPA is CO + trap, SLEEP is sleep, not trap, and
// CO with k = 4: 4 issue cycles, SOC.md 4.4.3).  PR = SZ = 1 is reserved (sh4_iss.h) and not checked.
//
//   tb                 the check; exit 1 on any mismatch
//   tb --selftest      seven planted decoder bugs (applied to the RTL answer) must each be reported; exit 1 if one is
//                      not
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "Vv1_ictl_tb.h"
#include "verilated.h"
#include "sh4_iss.h"

using namespace sh4iss;                // DK_*: sh4_iss.h's DKind, the C++ mirror of v1_soc_pkg's dkind_e

struct Facts {
    int priv, slot_ill, fpu_insn, dkind, rmw, memop, oui, vd, form_x, illegal;
};
static const char *const kFieldNames[] = {"priv", "slot_ill", "fpu_insn", "dkind", "rmw", "memop",
                                          "fpx_oui", "fpx_vd", "form_x", "illegal"};
static const int kFields = 10;
static int field(const Facts &f, int i) {
    const int v[kFields] = {f.priv, f.slot_ill, f.fpu_insn, f.dkind, f.rmw, f.memop, f.oui, f.vd, f.form_x, f.illegal};
    return v[i];
}
// the v1 fields v1_ictl_op sets for the three v1-side forms (checked below, not against the reference)
struct V1f {
    int cls, k, trap, sleep;
};

template <typename... T> static bool in(Form f, T... fs) { return ((f == fs) || ...); }

// the reference: the manual's lists, keyed on the ISS's form
static Facts reference(uint16_t op, Form f) {
    Facts r{};
    r.illegal = f == F_ILLEGAL && op != 0x0000;                  // H'0000 is NOP [HW] (exceptions-intc 3.7)
    r.form_x = f == F_TRAPA ? 1 : f == F_SLEEP ? 2 : op == 0x0000 ? 3 : 0;
    // exceptions-intc 3.7: privileged
    r.priv = in(f, F_LDC_SR, F_LDC_VBR, F_LDC_SSR, F_LDC_SPC, F_LDC_DBR, F_LDC_BANK, F_LDCL_SR, F_LDCL_VBR, F_LDCL_SSR,
                F_LDCL_SPC, F_LDCL_DBR, F_LDCL_BANK, F_STC_SR, F_STC_VBR, F_STC_SSR, F_STC_SPC, F_STC_SGR, F_STC_DBR,
                F_STC_BANK, F_STCL_SR, F_STCL_VBR, F_STCL_SSR, F_STCL_SPC, F_STCL_SGR, F_STCL_DBR, F_STCL_BANK,
                F_LDTLB, F_RTE, F_SLEEP);
    // exceptions-intc 3.6 / pm 5.x (10): slot-illegal (the user-mode privileged case is not a decoder fact)
    r.slot_ill = r.illegal || in(f, F_BF, F_BFS, F_BT, F_BTS, F_BRA, F_BRAF, F_BSR, F_BSRF, F_JMP, F_JSR, F_RTS, F_RTE,
                                 F_TRAPA, F_LDC_SR, F_LDCL_SR, F_MOVA, F_MOVW_PC, F_MOVL_PC);
    // pm 5.x (11): FPU instructions
    r.fpu_insn = ((op >> 12) == 0xF && op != 0xFFFD) ||
                 in(f, F_LDS_FPSCR, F_LDS_FPUL, F_LDSL_FPSCR, F_LDSL_FPUL, F_STS_FPSCR, F_STS_FPUL, F_STSL_FPSCR,
                    F_STSL_FPUL);
    // access kind of micro-op 0 (SOC.md 1.3.1 / 4.2; STC.L / STS.L PR store in their last micro-op)
    if (in(f, F_MOVW_PC, F_MOVL_PC, F_MOVB_L, F_MOVW_L, F_MOVL_L, F_MOVB_P, F_MOVW_P, F_MOVL_P, F_MOVB_L4, F_MOVW_L4,
           F_MOVL_L4, F_MOVB_L0, F_MOVW_L0, F_MOVL_L0, F_MOVB_LG, F_MOVW_LG, F_MOVL_LG, F_MACL, F_MACW, F_LDCL_SR,
           F_LDCL_GBR, F_LDCL_VBR, F_LDCL_SSR, F_LDCL_SPC, F_LDCL_DBR, F_LDCL_BANK, F_LDSL_MACH, F_LDSL_MACL,
           F_LDSL_PR, F_LDSL_FPSCR, F_LDSL_FPUL, F_FMOVS_L, F_FMOVS_L0, F_FMOVS_P, F_FMOVD_L, F_FMOVD_L0, F_FMOVD_P,
           F_FMOVX_L, F_FMOVX_P, F_FMOVX_L0))
        r.dkind = DK_LD;
    else if (in(f, F_MOVB_S, F_MOVW_S, F_MOVL_S, F_MOVB_M, F_MOVW_M, F_MOVL_M, F_MOVB_S4, F_MOVW_S4, F_MOVL_S4,
                F_MOVB_S0, F_MOVW_S0, F_MOVL_S0, F_MOVB_SG, F_MOVW_SG, F_MOVL_SG, F_STSL_MACH, F_STSL_MACL,
                F_STSL_FPUL, F_STSL_FPSCR, F_FMOVS_S, F_FMOVS_M, F_FMOVS_S0, F_FMOVD_S, F_FMOVD_M, F_FMOVD_S0,
                F_FMOVX_S, F_FMOVX_M, F_FMOVX_S0))
        r.dkind = DK_ST;
    else if (f == F_MOVCA) r.dkind = DK_MOVCA;
    else if (f == F_PREF) r.dkind = DK_PREF;
    else if (f == F_OCBI) r.dkind = DK_OCBI;
    else if (f == F_OCBP) r.dkind = DK_OCBP;
    else if (f == F_OCBWB) r.dkind = DK_OCBWB;
    else if (in(f, F_TST_B, F_AND_B, F_OR_B, F_XOR_B)) r.dkind = DK_RMW_RD;
    else if (f == F_TAS) r.dkind = DK_TAS_PURGE;
    r.rmw = in(f, F_AND_B, F_OR_B, F_XOR_B, F_TAS);
    r.memop = r.dkind != DK_NONE || in(f, F_STCL_SR, F_STCL_GBR, F_STCL_VBR, F_STCL_SSR, F_STCL_SPC, F_STCL_SGR,
                                       F_STCL_DBR, F_STCL_BANK, F_STSL_PR);
    // exceptions-intc 3.8 / pm 10: {O, U, I} decided at D, FTRV's V at D (the F1 operand traps V / Z / E are no
    // decoder fact: v1_fpu takes them from the value functions, SOC.md 4.3.8)
    if (in(f, F_FADD, F_FSUB, F_FMUL, F_FMAC, F_FADD_D, F_FSUB_D, F_FMUL_D, F_FCNVDS, F_FDIV, F_FDIV_D, F_FIPR)) r.oui = 7;
    else if (f == F_FTRV) { r.oui = 7; r.vd = 1; }
    else if (in(f, F_FSQRT, F_FSQRT_D, F_FSRRA, F_FSCA, F_FLOAT)) r.oui = 1;
    return r;
}

// planted decoder bugs for --selftest: each one corrupts the RTL answer for one form
static const char *const kMutants[] = {
    "memop dropped for MOVCA.L", "priv dropped for STC SGR", "slot_ill dropped for MOVA",
    "fpu_insn set for H'FFFD", "U dropped from FDIV's fpx_oui", "TAS.B micro-op 0 as TAS_RD",
    "SLEEP issued in one cycle (k = 1)"};
static const int kNumMutants = 7;
static void mutate(int m, uint16_t op, Form f, Facts &x, V1f &v) {
    switch (m) {
    case 1: if (f == F_MOVCA) x.memop = 0; break;
    case 2: if (f == F_STC_SGR) x.priv = 0; break;
    case 3: if (f == F_MOVA) x.slot_ill = 0; break;
    case 4: if (op == 0xFFFD) x.fpu_insn = 1; break;
    case 5: if (in(f, F_FDIV, F_FDIV_D)) x.oui &= ~2; break;
    case 6: if (f == F_TAS) x.dkind = DK_TAS_RD; break;
    case 7: if (f == F_SLEEP) v.k = 1; break;
    default: break;
    }
}

// one full pass; returns the number of mismatching (code, mode) points
static long run(Vv1_ictl_tb &t, int mutant, bool verbose) {
    static const unsigned kModes[3][2] = {{0, 0}, {0, 1}, {1, 0}};     // {PR, SZ}: M0, M1, M2
    long bad = 0, points = 0, per_field[kFields] = {}, valid_bad = 0, v1_bad = 0;
    for (int mi = 0; mi < 3; mi++)
        for (unsigned fr = 0; fr < 2; fr++)
            for (unsigned op = 0; op < 0x10000; op++) {
                const unsigned pr = kModes[mi][0], sz = kModes[mi][1];
                t.op = op;
                t.md = (fr << 2) | (sz << 1) | pr;
                t.eval();
                const Form f = decode(op, (pr << 19) | (sz << 20) | (fr << 21));
                Facts rtl{t.priv, t.slot_ill, t.fpu_insn, t.dkind, t.rmw, t.memop, t.fpx_oui, t.fpx_vd, t.form_x,
                          t.illegal};
                V1f v1{t.cls, t.k, t.trap, t.sleep};
                mutate(mutant, op, f, rtl, v1);
                const Facts ref = reference(op, f);
                bool pb = false;
                points++;
                for (int i = 0; i < kFields; i++)
                    if (field(rtl, i) != field(ref, i)) {
                        if (verbose && per_field[i] < 4)
                            printf("  %-8s op %04X M%d FR%u (%s): rtl %d ref %d\n", kFieldNames[i], op, mi, fr,
                                   form_info(f).name, field(rtl, i), field(ref, i));
                        per_field[i]++;
                        pb = true;
                    }
                // tp_decode and the ISS decode the same codes (SLEEP / TRAPA are v1_ictl_op's)
                const bool iss_valid = f != F_ILLEGAL && f != F_SLEEP && f != F_TRAPA;
                if ((t.valid != 0) != iss_valid) {
                    if (verbose && valid_bad < 4)
                        printf("  valid    op %04X M%d: tp_decode %d, ISS %s\n", op, mi, t.valid, form_info(f).name);
                    valid_bad++;
                    pb = true;
                }
                // the v1 fields of the three v1-side forms and of an illegal code (K_MT = 0, K_CO = 4); SLEEP is
                // issued for 4 cycles (SOC.md 4.4.3)
                bool v1ok = true;
                if (op == 0x0000) v1ok = v1.cls == 0 && v1.k == 1 && !v1.trap && !v1.sleep;
                else if (f == F_TRAPA) v1ok = v1.cls == 4 && v1.k == 1 && v1.trap && !v1.sleep;
                else if (f == F_SLEEP) v1ok = v1.cls == 4 && v1.k == 4 && !v1.trap && v1.sleep;
                else if (f == F_ILLEGAL) v1ok = v1.trap && !v1.sleep;
                else v1ok = !v1.sleep;
                if (!v1ok) {
                    if (verbose && v1_bad < 4)
                        printf("  v1 flds  op %04X M%d (%s): cls %d k %d trap %d sleep %d\n", op, mi,
                               form_info(f).name, v1.cls, v1.k, v1.trap, v1.sleep);
                    v1_bad++;
                    pb = true;
                }
                bad += pb;
            }
    if (verbose) {
        printf("v1-ictl: %ld points (65536 codes x M0 M1 M2 x FR 0 / 1), %ld mismatching\n", points, bad);
        for (int i = 0; i < kFields; i++)
            if (per_field[i]) printf("  %-8s %ld mismatches\n", kFieldNames[i], per_field[i]);
        if (valid_bad) printf("  valid    %ld mismatches (tp_decode vs sh4_iss.h)\n", valid_bad);
        if (v1_bad) printf("  v1 flds  %ld mismatches\n", v1_bad);
    }
    return bad;
}

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    bool selftest = argc > 1 && !strcmp(argv[1], "--selftest");
    auto ctx = std::make_unique<VerilatedContext>();
    Vv1_ictl_tb t(ctx.get());
    if (!selftest) {
        long bad = run(t, 0, true);
        printf("%s\n", bad ? "v1-ictl: FAIL" : "v1-ictl: PASS");
        return bad ? 1 : 0;
    }
    int missed = 0;
    for (int m = 1; m <= kNumMutants; m++) {
        long bad = run(t, m, false);
        printf("v1-ictl selftest: planted bug %d (%s): %ld mismatching points -> %s\n", m, kMutants[m - 1], bad,
               bad ? "caught" : "MISSED");
        missed += bad == 0;
    }
    printf("%s\n", missed ? "v1-ictl selftest: FAIL" : "v1-ictl selftest: PASS");
    return missed ? 1 : 0;
}
