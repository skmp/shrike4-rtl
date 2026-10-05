// v1_fpdn_core.cpp -- make v1-fpdn, part 2 (WI-FPDN; rtl/v1/SOC.md 4.3.8, 9.17): FPSCR.DN = 0 on the v1 core, on
// WI-CORE-A's v1-exc harness (tb/v1_exc_tb.sv + tb/v1_exc_tb.cpp, included read-only: its Case / run_core / compare /
// log_handler; its own main is renamed away).
//
// Every console record of the DN probe (tools/hw/fpdn: fpdn_p1.bin, the class table under 7 FPSCR settings; fpdn_p3.bin,
// the double forms on the two-word palette; fpdn_p2.bin, the gradual-underflow values, every V1_FPDN_P2STEP-th) becomes a
// one-instruction program: FR0..FR3, FPUL, FPSCR (DN, RM, PR, enables, preset cause / flags) and T = 1 loaded through the
// backdoor, the instruction at CODE, SLEEP; the logging handler at VBR + 0x100 skips a trapping instruction.  The core's
// FR0..FR3, FPUL, FPSCR, T and its entries (an FPU trap: EXPEVT 0x120 at CODE) must equal the console's record -- the E
// trap per opcode, its cause / flags, the destination left alone, the DN = 0 values.  The ISS is not used: its DN = 0
// rules are not the console's (report WI-FPDN.md).
// Every FPSCR bit is compared, the trap records' too (SOC.md 4.3.8 "Trap effects", I053): a trap leaves cause = the
// computed V / Z / O / U / I and flags |= them, an E trap cause = E alone with the flags kept.  So an O / U / I enable
// trap (a "possibility" trap without E, the 203 records WI-FPDN's first run had to mask) must report the computed
// O / U / I [HW fpdn: FSQRT(1.5) + EN.I 0x00001084, FMUL.D max x 1.5 + EN.I 0x00085094] -- v1_fpu's trap pass computing
// the operation (WI-CORE-F item 1).  Those records are counted in the summary line, and how many of them differ.
// Then the vector unit through v1_fpu with DN = 0: FIPR FV0,FV4 on every V1_FPDN_FIPRSTEP-th console vector of
// model/fipr/hw_specials/fipr_dn0_rnrz.bin (RN, RZ: FR7 and the cause of model/fipr), and every PR = 0 row of
// fipr_specials.txt and ftrv_specials.txt (FTRV XMTRX,FV0: the four results, FPSCR, the trap).
// --iss: a diagnostic, not part of the gate -- the same scalar programs on the ISS (tb/sh4_iss.h, SoC mode, WI-REF's)
// instead of the core, counted against the console records per operation (the ISS's DN = 0 rules; report WI-FPDN.md).
//   tb [-v] [--p2step N] [--fiprstep N] [--iss]
#define main v1_exc_main
#include "v1_exc_tb.cpp"
#undef main
#include "sh4_fpdn.h"
#include "../tools/hw/fpdn/fpdn_vec.h"
#include "../model/fipr/hw_specials/vecgen_dn0_ftrv.h"
#undef SNAN                                      // (math.h's; the vector file names its own)
#include "../model/fipr/hw_specials/ftrv_specials_vectors.h"

#ifndef FPDN_ROOT
#define FPDN_ROOT "."
#endif
using sh4fpdn::u32;
static std::string g_root = FPDN_ROOT;
static bool g_v = false;

struct Want { u32 fr[8]; u32 fpul, fpscr, t; bool trap; };
struct Tally { long n = 0, bad = 0, oui = 0, oui_bad = 0; };   // oui: O / U / I possibility traps (all compared)
static std::map<std::string, Tally> g_tab;

// one program: `opw` at CODE with the register image; compare with `w`
static bool run_one(Vv1_exc_tb *m, uint16_t opw, const u32 fr[8], const u32 xf[16], u32 fpul, u32 fpscr, const Want &w,
                    int nfr, const std::string &key, const std::string &lab) {
    Case C;
    prog(C, [&](Sh4Prog &p) { p.op(opw); });
    log_handler(C);
    C.iss = false;
    C.st.sr |= 1;                                               // T = 1
    for (int i = 0; i < 8; i++) C.st.fr[0][i] = fr[i];
    if (xf) for (int i = 0; i < 16; i++) C.st.fr[1][i] = xf[i];
    C.st.fpul = fpul;
    C.st.fpscr = fpscr;
    if (w.trap) {
        Ent e{};
        e.kind = 1; e.code = 0x120; e.spc = CODE; e.handler = VBR0 + 0x100;
        C.want.push_back(e);
    }
    CoreRes R;
    run_core(m, C, R);
    std::string d = compare(C, R, nullptr);
    char b[160];
    for (int i = 0; i < nfr; i++)
        if (R.regs[64 + i] != w.fr[i]) { snprintf(b, sizeof b, " FR%d %08X (want %08X)", i, R.regs[64 + i], w.fr[i]); d += b; }
    if (R.regs[36] != w.fpul) { snprintf(b, sizeof b, " FPUL %08X (want %08X)", R.regs[36], w.fpul); d += b; }
    if ((R.regs[35] & 0x003FFFFFu) != (w.fpscr & 0x003FFFFFu)) {
        snprintf(b, sizeof b, " FPSCR %08X (want %08X)", R.regs[35], w.fpscr); d += b;
    }
    if ((R.regs[24] & 1) != w.t) { snprintf(b, sizeof b, " T %u (want %u)", R.regs[24] & 1, w.t); d += b; }
    Tally &t = g_tab[key];
    t.n++;
    if (!d.empty()) {
        t.bad++;
        if (t.bad <= 3 || g_v)
            printf("  FAIL %s %s fpscr=%08x in=%08x %08x %08x %08x ul=%08x:%s\n", key.c_str(), lab.c_str(), fpscr, fr[0], fr[1],
                   fr[2], fr[3], fpul, d.c_str());
        return false;
    }
    return true;
}

// --iss: the same program on the ISS; returns the difference from the console record ("" = equal)
static bool g_iss = false;
static std::map<std::string, std::pair<long, long>> g_isstab;
static void iss_rec(const char *part, int op, const u32 in[5], u32 fpscr, const fpdn_rec_t &h, const std::string &lab) {
    Case C;
    prog(C, [&](Sh4Prog &p) { p.op(FPDN_OPS[op].opw); });
    log_handler(C);
    C.st.sr |= 1;
    for (int i = 0; i < 4; i++) C.st.fr[0][i] = in[i];
    C.st.fpul = in[4];
    C.st.fpscr = fpscr;
    IssRes I;
    run_iss(C, I, nullptr);
    std::string d;
    char b[120];
    if (!I.ok) d = " " + I.err;
    for (int i = 0; i < 4 && I.ok; i++)
        if (I.s.fr[0][i] != h.fr[i]) { snprintf(b, sizeof b, " FR%d %08X (console %08X)", i, I.s.fr[0][i], h.fr[i]); d += b; }
    if (I.ok && I.s.fpul != h.fpul) { snprintf(b, sizeof b, " FPUL %08X (console %08X)", I.s.fpul, h.fpul); d += b; }
    if (I.ok && (I.s.fpscr & 0x003FFFFFu) != h.fpscr) { snprintf(b, sizeof b, " FPSCR %08X (console %08X)", I.s.fpscr, h.fpscr); d += b; }
    if (I.ok && (I.s.sr & 1) != h.t) { snprintf(b, sizeof b, " T %u (console %u)", I.s.sr & 1, h.t); d += b; }
    const bool trap = !I.ents.empty() && I.ents[0].code == 0x120;
    if (I.ok && trap != (h.trap != 0)) { snprintf(b, sizeof b, " trap %d (console %d)", trap, h.trap != 0); d += b; }
    auto &t = g_isstab[std::string(part) + " " + FPDN_OPS[op].name];
    t.first++;
    if (!d.empty()) {
        if (t.second++ < 2 || g_v)
            printf("  ISS %s %s fpscr=%08x %s in=%08x %08x %08x %08x ul=%08x:%s\n", part, FPDN_OPS[op].name, fpscr, lab.c_str(),
                   in[0], in[1], in[2], in[3], in[4], d.c_str());
    }
}

// a scalar console record as a program
static void run_rec(Vv1_exc_tb *m, const char *part, int op, const u32 in[5], u32 fpscr, const fpdn_rec_t &h,
                    const std::string &lab) {
    if (g_iss) { iss_rec(part, op, in, fpscr, h, lab); return; }
    Want w{};
    for (int i = 0; i < 4; i++) w.fr[i] = h.fr[i];
    w.fpul = h.fpul; w.fpscr = h.fpscr; w.t = h.t; w.trap = h.trap != 0;
    // an O / U / I possibility trap without E (the console's record: trap, no E cause, an O / U / I enable the opcode
    // can raise): compared in full like every record, and counted (see the head)
    const u32 en = (fpscr >> 7) & 0x1F;
    const bool oui = op != sh4fpdn::FSRRA_S && h.trap == 0x120 && !((h.fpscr >> 17) & 1) && (en & sh4fpdn::oui_mask(op));
    const u32 fr[8] = {in[0], in[1], in[2], in[3], 0, 0, 0, 0};
    const std::string key = std::string(part) + " " + FPDN_OPS[op].name;
    const bool ok = run_one(m, FPDN_OPS[op].opw, fr, nullptr, in[4], fpscr, w, 4, key, lab);
    if (oui) { Tally &t = g_tab[key]; t.oui++; if (!ok) t.oui_bad++; }
}

static std::vector<fpdn_rec_t> load(const std::string &p) {
    std::vector<fpdn_rec_t> v;
    FILE *f = fopen(p.c_str(), "rb");
    if (!f) { fprintf(stderr, "cannot read %s\n", p.c_str()); exit(2); }
    fpdn_rec_t r;
    while (fread(&r, sizeof r, 1, f) == 1) v.push_back(r);
    fclose(f);
    return v;
}

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    u32 p2step = 8, fiprstep = 256;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "-v") g_v = true;
        else if (a == "--p2step" && i + 1 < argc) p2step = (u32)atol(argv[++i]);
        else if (a == "--fiprstep" && i + 1 < argc) fiprstep = (u32)atol(argv[++i]);
        else if (a == "--root" && i + 1 < argc) g_root = argv[++i];
        else if (a == "--iss") g_iss = true;
        else if (a[0] == '+') continue;
        else { fprintf(stderr, "usage: %s [-v] [--p2step N] [--fiprstep N] [--root DIR] [--iss]\n", argv[0]); return 2; }
    }
    auto m = std::make_unique<Vv1_exc_tb>();
    const std::string d = g_root + "/tools/hw/fpdn/";
    // ---- the scalar console records
    {
        const auto P1 = load(d + "fpdn_p1.bin"), P2 = load(d + "fpdn_p2.bin"), P3 = load(d + "fpdn_p3.bin");
        size_t k = 0;
        for (int op = 0; op < P_NOPS; op++)
            for (int c = 0; c < FPDN_P1_NCFG; c++)
                for (int row = 0; row < fpdn_p1_rows(op); row++, k++) {
                    u32 in[5]; fpdn_p1_inputs(op, row, in);
                    char lab[48];
                    snprintf(lab, sizeof lab, "[%s %s %s]", FPDN_PAL_NAME[row % 14], FPDN_PAL_NAME[(row / 14) % 14], FPDN_PAL_NAME[row / 196]);
                    run_rec(m.get(), "p1", op, in, FPDN_P1_CFG[c] | (FPDN_OPS[op].pr ? 0x80000u : 0), P1.at(k), lab);
                }
        k = 0;
        for (int q = 0; q < FPDN_P3_NOPS; q++) {
            const int op = FPDN_P3_OP[q];
            for (int c = 0; c < FPDN_P3_NCFG; c++)
                for (int row = 0; row < fpdn_p3_rows(op); row++, k++) {
                    u32 in[5]; fpdn_p3_inputs(op, row, in);
                    char lab[48]; snprintf(lab, sizeof lab, "[%s %s]", FPDN_PAL3_NAME[row % FPDN_NPAL3], FPDN_PAL3_NAME[row / FPDN_NPAL3]);
                    run_rec(m.get(), "p3", op, in, FPDN_P3_CFG[c] | 0x80000u, P3.at(k), lab);
                }
        }
        k = 0;
        for (int v = 0; v < V_NOPS; v++)
            for (int c = 0; c < FPDN_P2_NCFG; c++)
                for (u32 i = 0; i < FPDN_P2_N; i++, k++) {
                    if (i % p2step) continue;
                    u32 in[5]; fpdn_p2_inputs(v, i, in);
                    const int op = FPDN_VOP[v];
                    char lab[24]; snprintf(lab, sizeof lab, "[#%u]", i);
                    run_rec(m.get(), "p2", op, in, FPDN_P2_CFG[c] | (FPDN_OPS[op].pr ? 0x80000u : 0), P2.at(k), lab);
                }
    }
    if (g_iss) {
        long n = 0, bad = 0;
        for (auto &t : g_isstab) {
            n += t.second.first; bad += t.second.second;
            if (t.second.second) printf("  %-22s %6ld records, the ISS differs in %ld\n", t.first.c_str(), t.second.first, t.second.second);
        }
        printf("iss: %ld console records as programs on the ISS (SoC mode): %ld differ\n", n, bad);
        return 0;
    }
    // ---- FIPR FV0,FV4 (0xF4ED) with DN = 0 on the console vectors
    {
        const std::string p = g_root + "/model/fipr/hw_specials/fipr_dn0_rnrz.bin";
        FILE *f = fopen(p.c_str(), "rb");
        if (!f) { fprintf(stderr, "cannot read %s\n", p.c_str()); return 2; }
        std::vector<u32> hw(2 * (size_t)DN0_N);
        if (fread(hw.data(), 4, hw.size(), f) != hw.size()) { fprintf(stderr, "short %s\n", p.c_str()); return 2; }
        fclose(f);
        for (u32 i = 0; i < DN0_N; i += fiprstep)
            for (u32 rz = 0; rz < 2; rz++) {
                u32 fr[8];
                dn0_vec(i, fr, fr + 4);
                u32 fs = rz, r = 0;
                sh4_fipr_ex(fr, fr + 4, &fs, &r);
                Want w{};
                for (int j = 0; j < 8; j++) w.fr[j] = fr[j];
                w.fr[7] = hw[2 * (size_t)i + rz];
                w.fpul = 0x77770000u; w.fpscr = fs; w.t = 1; w.trap = false;
                char lab[24]; snprintf(lab, sizeof lab, "[#%u]", i);
                run_one(m.get(), 0xF4ED, fr, nullptr, 0x77770000u, rz, w, 8, "vec FIPR dn0", lab);
            }
    }
    // ---- the FIPR / FTRV special-operand rows (PR = 0)
    {
        char line[512];
        FILE *f = fopen((g_root + "/model/fipr/hw_specials/fipr_specials.txt").c_str(), "r");
        if (!f) { fprintf(stderr, "cannot read fipr_specials.txt\n"); return 2; }
        while (fgets(line, sizeof line, f)) {
            u32 cfg, v, fr[8], res, fp;
            char tr[16];
            if (sscanf(line, "FIPR cfg=%x v=%u a=%x,%x,%x,%x b=%x,%x,%x,%x -> res=%x fpscr=%x %*s %*s %15s", &cfg, &v, &fr[0],
                       &fr[1], &fr[2], &fr[3], &fr[4], &fr[5], &fr[6], &fr[7], &res, &fp, tr) != 13) continue;
            if (cfg & 0x80000u) continue;
            Want w{};
            for (int j = 0; j < 8; j++) w.fr[j] = fr[j];
            w.fr[7] = res; w.fpul = 0x77770000u; w.fpscr = fp; w.t = 1; w.trap = strncmp(tr, "TRAP", 4) == 0;
            char lab[24]; snprintf(lab, sizeof lab, "[v=%u]", v);
            run_one(m.get(), 0xF4ED, fr, nullptr, 0x77770000u, cfg, w, 8, "vec FIPR special", lab);
        }
        fclose(f);
        f = fopen((g_root + "/model/fipr/hw_specials/ftrv_specials.txt").c_str(), "r");
        if (!f) { fprintf(stderr, "cannot read ftrv_specials.txt\n"); return 2; }
        while (fgets(line, sizeof line, f)) {
            u32 cfg, t, res[4], fp;
            char tr[16];
            if (sscanf(line, "FTRV cfg=%x t=%u -> res=%x,%x,%x,%x fpscr=%x %*s %*s %15s", &cfg, &t, &res[0], &res[1], &res[2],
                       &res[3], &fp, tr) != 8) continue;
            if (cfg & 0x80000u || t >= NT) continue;
            u32 fr[8], xf[16];
            for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) xf[4 * c + r] = TS[t].row[r][c];
            for (int r = 0; r < 4; r++) { fr[r] = TS[t].v[r]; fr[4 + r] = 0x3F800000u + ((u32)(4 + r) << 16); }
            Want w{};
            for (int j = 0; j < 8; j++) w.fr[j] = fr[j];
            for (int r = 0; r < 4; r++) w.fr[r] = res[r];
            w.fpul = 0x77770000u; w.fpscr = fp; w.t = 1; w.trap = strncmp(tr, "TRAP", 4) == 0;
            char lab[24]; snprintf(lab, sizeof lab, "[t=%u]", t);
            run_one(m.get(), 0xF1FD, fr, xf, 0x77770000u, cfg, w, 8, "vec FTRV special", lab);
        }
        fclose(f);
    }
    long n = 0, bad = 0, oui = 0, oui_bad = 0;
    for (auto &t : g_tab) {
        n += t.second.n; bad += t.second.bad; oui += t.second.oui; oui_bad += t.second.oui_bad;
        if (t.second.bad || g_v)
            printf("  %-22s %6ld programs, %ld differ (O / U / I possibility traps: %ld, %ld differ)\n", t.first.c_str(),
                   t.second.n, t.second.bad, t.second.oui, t.second.oui_bad);
    }
    printf("core: %ld programs on the v1 core (p1 / p3 every record, p2 every %u-th, FIPR DN = 0 every %u-th x RN / RZ, "
           "the FIPR / FTRV special rows): %ld differ.  O / U / I possibility traps, compared in full (cause and flags, SOC.md "
           "4.3.8): %ld, of which %ld differ\n", n, p2step, fiprstep, bad, oui, oui_bad);
    printf("v1-fpdn core: %s\n", bad ? "FAIL" : "PASS");
    return bad ? 1 : 0;
}
