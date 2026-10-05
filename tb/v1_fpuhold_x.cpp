// v1_fpuhold_x.cpp -- make v1-fpuhold, part 4: random FPU programs on the core against the ISS (SoC mode), on
// WI-CORE-A's v1-exc harness (tb/v1_exc_tb.sv + tb/v1_exc_tb.cpp, included read-only: its Case / run_core / run_iss /
// compare / log_handler; its own main is renamed away).  rtl/v1/SOC.md 4.3.1, 4.3.2, 4.3.8, 4.5.
//
// The v1-exc generators use the FPU only through LS-pipe forms (FE forms needed this work item's kill).  These programs
// are FE-heavy: FADD / FSUB / FMUL / FMAC / FDIV / FSQRT / FCMP / FLOAT / FTRC single and double (PR is random and
// changes), FCNVSD / FCNVDS, FIPR / FTRV / FSCA / FSRRA, FRCHG / FSCHG, LDS / STS FPSCR with random modes, rounding and
// exception enables (so D-decided traps, F1 operand traps with V / Z, trap passes), FP loads / stores / moves into the
// F1 bypass, FPUL transfers, MUL / MAC / LDS.L MACL / CLRMAC / STS MACL -- over a register palette of normals, zeros,
// infinities, quiet and signalling NaNs, denormals -- mixed with faulting accesses (misaligned loads, paired with FE
// ops in either lane), TRAPA, H'FFFD, SR.FD, delayed branches with FE ops in their slots, and memory holds (v1_ideal_mem's
// ack-late knob on the buffer: `mah` windows across FDIV / vector / double / multiplier work: hold_pipe without
// hold_div).  The logging handler resumes after every entry.  Every register (both FR banks, FPSCR, FPUL, MACH /
// MACL), the buffer, EXPEVT / TEA and the list of entries must equal the ISS's.  DN = 1 throughout: DN = 0 -- its
// values and the E trap -- is covered by make v1-fpdn (tb/v1_fpdn_tb.* on the units, tb/v1_fpdn_core.cpp on the core,
// both against the console's records) and the unit's E-trap vectors (tb/v1_fpuhold_unit.cpp).
// The reference is tb/sh4_iss.h in SoC mode with SOC.md 4.3.8's trap effects (run_iss_te below: an FPU trap's FPSCR is
// the untrapped execution's cause and flags, E alone for an E trap; the ISS's own trap_fpscr keeps V / Z / E only
// until WI-REF's item 2).  The build binds tb/v1_fpu_reccheck.sv into v1_fpu: every cycle the FPU's commit-log records
// must be its register writes (SOC.md 6.3), else the run stops.
//   tb --n N [--seed S] [-v]      (--only / --list: the included v1-exc directed tests are not run here)
//   tb --dir [-v]                 this work item's directed cases (below), against the ISS
//   --check                       also each program's commit log (SOC.md 6.3: the core's tap with v1_fpu's records)
//                                 through tb/soc_checker against Sh4Sys, as v1-exc --check does (its check_log)
#define main v1_exc_main
#include "v1_exc_tb.cpp"
#undef main

// ---- the reference: the ISS in SoC mode with SOC.md 4.3.8's trap effects.  A scalar FPU trap other than E takes the
// untrapped execution's FPSCR cause and flags -- the same instruction run on a copy of the ISS with the enables cleared
// (they only decide the trap), its cause field and flags taken -- where tb/sh4_iss.h's trap_fpscr keeps V / Z / E
// (O / U / I = 0) until WI-REF's item 2.  A vector op's trap FPSCR is its header's (model/fipr, fsca, fsrra: already
// the computed cause); an E trap stays E alone, flags kept.
static uint64_t g_te_fixed = 0;                   // traps whose FPSCR the 4.3.8 rule changed
static uint64_t g_te_oui = 0;                     // FPU traps whose FPSCR cause has O / U / I (the 4.3.8 effect's coverage)
static int iss_step_te(Sh4Iss &iss, std::string &err) {
    using namespace sh4iss;
    if (iss.entry_pending() || iss.asleep) return iss.step();
    Sh4Entry e;
    if (!iss.check(e)) { iss.commit(); return iss.error ? 0 : 1; }
    if (e.kind == Sh4Entry::EXC && e.code == EXP_FPU && e.fpscr_v && !((e.fpscr >> 17) & 1)) {
        const uint16_t op = (uint16_t)iss.m->rd(e.pc, 2);
        const Form f = decode(op, iss.s.fpscr);
        if (f != F_FIPR && f != F_FTRV && f != F_FSCA && f != F_FSRRA) {
            Sh4Iss sh = iss;                                          // the untrapped execution, on a copy
            Sh4FlatEnv fe;
            fe.m = iss.m;
            sh.env = &fe; sh.on_exec = nullptr;
            sh.s.fpscr &= ~(0x1Fu << 7);
            if (!sh.step() || sh.last.kind != Sh4Iss::SK_INSN || sh.last.pc != e.pc) {
                err = "the trap-effect reference could not run the trapping instruction untrapped";
                return 0;
            }
            const uint32_t fs = (iss.s.fpscr & ~0x0003F07Cu) | (sh.s.fpscr & 0x0003F07Cu);
            if (fs != e.fpscr) g_te_fixed++;
            e.fpscr = fs;
        }
        if ((e.fpscr >> 12) & 7) g_te_oui++;
    }
    iss.take_entry(e);
    return 1;
}
// v1_exc_tb.cpp's run_iss (the interrupt injection included) with the step above
static void run_iss_te(const Case &C, IssRes &R, const CoreRes *core = nullptr, uint64_t max_insns = 200000) {
    R.mem = C.mem;
    R.mem.time = nullptr;
    ExcEnv env;
    env.m = &R.mem;
    env.flt_lo = C.flt_lo; env.flt_hi = C.flt_hi; env.flt_rd = C.flt_rd; env.flt_wr = C.flt_wr;
    Sh4Iss iss(&R.mem);
    iss.soc = true;
    iss.env = &env;
    iss.s = C.st;
    uint64_t n = 0, ic0 = 0;
    uint8_t base = 0;
    std::string ierr;
    auto rebase = [&]() {
        const size_t k = env.ents.size();
        ic0 = iss.icount;
        base = 0;
        if (core && k >= 1 && k - 1 < core->ents.size() && core->ents[k - 1].kind != 3)
            base = (uint8_t)(core->ents[k - 1].isn + 2);
    };
    for (; n < max_insns && !R.mem.exited && ierr.empty(); n++) {
        const size_t k = env.ents.size();
        if (core && k < core->ents.size() && core->ents[k].kind == 2) {
            const Ent &e = core->ents[k];
            if ((uint8_t)(iss.icount - ic0) == (uint8_t)(e.isn - base)) {
                char b[200];
                const bool nmi = e.code == 0x1C0;
                if (iss.s.pc != e.spc) {
                    snprintf(b, sizeof b, "interrupt %zu (%03X) at SPC %08X: the ISS is at %08X after as many instructions",
                             k, e.code, e.spc, iss.s.pc);
                    ierr = b;
                } else if (!iss.interruptible()) {
                    snprintf(b, sizeof b, "interrupt %zu (%03X) at %08X: not an interruptible boundary (%s)", k, e.code, e.spc,
                             iss.slot_pending ? "a delay slot" : "a completion entry pending");
                    ierr = b;
                } else if (!nmi && ((iss.s.sr >> 28) & 1) && !iss.asleep) {
                    snprintf(b, sizeof b, "interrupt %zu (%03X) at %08X taken with SR.BL = 1, not asleep", k, e.code, e.spc);
                    ierr = b;
                } else {
                    iss.interrupt(e.code);
                    rebase();
                    continue;
                }
                break;
            }
        }
        std::string terr;
        if (!iss_step_te(iss, terr)) { if (!terr.empty()) ierr = terr; break; }
        if (env.ents.size() != k) rebase();
    }
    R.insns = n;
    R.ok = !iss.error && (iss.asleep || R.mem.exited) && ierr.empty();
    if (iss.error) R.err = std::string("ISS error: ") + iss.errmsg;
    else if (!ierr.empty()) R.err = "ISS / RTL: " + ierr;
    else if (!R.ok) R.err = "the ISS did not reach SLEEP";
    R.s = iss.s;
    R.ents = env.ents;
    R.expevt = env.expevt; R.tra = env.tra; R.tea = env.tea; R.pteh = env.pteh;
    R.brcr = (uint16_t)env.ubc.get(dbg::Ubc::R_BRCR);
    R.sqi = env.sqi;
}

static void fpfuzz_case(Case &C, uint64_t seed, std::string &desc) {
    Rng r(seed * 0x2545F491u + 3);
    base_case(C);
    log_handler(C);
    const bool fd = r.u(12) == 0;
    C.st.sr = 0x400000F0u | (fd ? 0x8000u : 0);
    C.st.pc = CODE;
    // the register palette
    static const uint32_t pal[] = {0x3F800000u, 0x40400000u, 0xBF800000u, 0x3EAAAAABu, 0x00000000u, 0x80000000u,
                                   0x7F800000u, 0xFF800000u, 0x7F800001u, 0x7FC00000u, 0x7FBFFFFFu, 0x00000001u,
                                   0x807FFFFFu, 0x7F7FFFFFu, 0x00800000u, 0x4B000000u, 0x4F000000u, 0xCF000001u,
                                   0x3FFFFFFFu, 0x40000000u, 0x41200000u, 0x3DCCCCCDu};
    const unsigned NP = sizeof pal / sizeof *pal;
    for (int b = 0; b < 2; b++) for (int i = 0; i < 16; i++) C.st.fr[b][i] = r.u(3) ? pal[r.u(NP)] : r.next();
    C.st.fpul = r.u(2) ? pal[r.u(NP)] : r.next() % 200 - 100;
    // PR = 1 and SZ = 1 together is reserved (pm 6.4; tp_decode makes the 64-bit FMOVs illegal there, the ISS does
    // not): a program either uses PR (no SZ, no FSCHG) or SZ (PR = 0)
    const bool use_pr = r.u(2);
    auto fpscr_rand = [&]() {
        uint32_t v = 0x00040000u;                                        // DN = 1
        if (r.u(2)) v |= 1;                                              // RM = RZ
        if (use_pr && r.u(3) == 0) v |= 1u << 19;                        // PR
        if (!use_pr && r.u(6) == 0) v |= 1u << 20;                       // SZ
        if (r.u(3) == 0) v |= 1u << 21;                                  // FR
        const unsigned e = r.u(10);
        if (e < 4) v |= 1u << (7 + r.u(5));                              // one enable
        else if (e == 4) v |= (r.u(32)) << 7;                            // several
        return v;
    };
    C.st.fpscr = fpscr_rand();
    C.st.r[8] = BUF + 0x80; C.st.r[9] = BUF + 0x81; C.st.r[10] = BUF + 0x100; C.st.r[11] = BUF + 0x40;
    C.st.r[14] = BUF + 0x40; C.st.gbr = BUF;
    for (int i = 0; i < 8; i++) C.st.r[i] = r.u(2) ? r.next() : r.u(64);
    for (uint32_t a = 0; a < 0x200; a += 4) C.mem.w32(BUF + a, r.u(2) ? pal[r.u(NP)] : r.next());
    // a memory hold on part of the buffer (loads and stores there ack late: `mah`)
    if (r.u(2)) { C.slow_lo = BUF + 0x40 * r.u(4); C.slow_hi = C.slow_lo + 0x40 + 0x40 * r.u(4); C.slow_n = 1 + (int)r.u(20); }
    Sh4Prog p(CODE);
    auto R = [&](int n = 8) { return (unsigned)r.u((unsigned)n); };
    const char *fe2[] = {"FADD FRm,FRn", "FSUB FRm,FRn", "FMUL FRm,FRn", "FDIV FRm,FRn", "FCMP/EQ FRm,FRn",
                         "FCMP/GT FRm,FRn", "FMAC FR0,FRm,FRn", "FMOV FRm,FRn"};
    const char *fe1[] = {"FSQRT FRn", "FABS FRn", "FNEG FRn", "FLOAT FPUL,FRn", "FLDI1 FRn", "FLDI0 FRn", "FSTS FPUL,FRn",
                         "FSRRA FRn"};
    const char *dbl2[] = {"FADD DRm,DRn", "FSUB DRm,DRn", "FMUL DRm,DRn", "FDIV DRm,DRn", "FCMP/EQ DRm,DRn",
                          "FCMP/GT DRm,DRn"};
    int nlit = 0, lab = 0;
    std::vector<std::pair<std::string, uint32_t>> lits;
    const int N = 20 + (int)r.u(30);
    std::function<void(bool)> emit = [&](bool slot) {
        const unsigned k = r.u(slot ? 6 : 40);
        switch (k) {
        case 0: case 1: case 2: case 3: p.I(fe2[r.u(8)], R(16), R(16)); break;
        case 4: case 5: p.I(fe1[r.u(8)], R(16)); break;
        case 6: case 7: p.I(dbl2[r.u(6)], R(8), R(8)); break;          // doubles when PR = 1 (else singles)
        case 8: p.I(r.u(2) ? "FSQRT DRn" : "FLOAT FPUL,DRn", R(8)); break;
        case 9: p.I(r.u(2) ? "FTRC FRm,FPUL" : "FTRC DRm,FPUL", 0, R(r.u(2) ? 16 : 8)); break;
        case 10: p.I(r.u(2) ? "FCNVSD FPUL,DRn" : "FCNVDS DRm,FPUL", R(8), R(8)); break;
        case 11: p.I("FIPR FVm,FVn", R(4), R(4)); break;
        case 12: p.I("FTRV XMTRX,FVn", R(4)); break;
        case 13: p.I("FSCA FPUL,DRn", R(8)); break;
        case 14: p.I(use_pr || r.u(2) ? "FRCHG" : "FSCHG"); break;
        case 15: {                                                        // LDS FPSCR with a random mode / enables
            const std::string l = "fp" + std::to_string(nlit++);
            lits.push_back({l, fpscr_rand()});
            p.ldl(1, l).I("LDS Rm,FPSCR", 0, 1);
            break;
        }
        case 16: p.I("STS FPSCR,Rn", R()); break;
        case 17: if (r.u(2)) p.I("FMOV.S @Rm,FRn", R(16), 8); else p.I("FMOV.S FRm,@Rn", 8, R(16)); break;
        case 18: p.I("MOV Rm,Rn", 11, 14).I("FMOV.S @Rm+,FRn", R(16), 11); break;
        case 19: if (r.u(2)) p.I("FLDS FRm,FPUL", 0, R(16)); else p.I("STS FPUL,Rn", R()); break;
        case 20: p.I("LDS Rm,FPUL", 0, R()); break;
        case 21: case 22: {                                               // a faulting load paired with an FE op
            p.align4();
            if (r.u(2)) p.I("MOV.L @Rm,Rn", R(), 9).I(fe2[r.u(7)], R(16), R(16));
            else p.I(fe2[r.u(7)], R(16), R(16)).I("MOV.L @Rm,Rn", R(), 9);
            break;
        }
        case 23: p.I("TRAPA #imm", 0, 0, r.u(256)); break;
        case 24: p.op(0xFFFD); break;
        case 25: p.I("MUL.L Rm,Rn", R(), R()); break;
        case 26: p.I("MOV Rm,Rn", 12, 14).I("MOV Rm,Rn", 13, 14).I("ADD #imm,Rn", 13, 0, r.u(3) * 2).I("MAC.L @Rm+,@Rn+", 12, 13); break;
        case 27: p.I("MOV Rm,Rn", 11, 14).I("ADD #imm,Rn", 11, 0, r.u(2)).I("LDS.L @Rm+,MACL", 0, 11); break;
        case 28: p.I(r.u(2) ? "CLRMAC" : "STS MACL,Rn", R()); break;
        case 29: p.I("MOV.L @Rm,Rn", R(), 8).I(fe2[r.u(8)], R(16), R(16)); break;   // a (possibly slow) load then FE
        case 30: p.I("MOV.L Rm,@Rn", 8, R()); break;
        case 31: {                                                         // a delayed branch, an FE op in its slot
            const std::string l = "L" + std::to_string(lab++);
            const unsigned f = r.u(3);
            if (f == 0) p.bra("BRA label", l); else p.bcc(f == 1 ? "BT/S" : "BF/S", l);
            emit(true);
            for (unsigned j = 0, n = r.u(2); j < n; j++) p.I("ADD #imm,Rn", R(), 0, 3);
            p.label(l);
            break;
        }
        case 32: p.I("SETT"); break;
        case 33: p.I("MOVT Rn", R()); break;
        case 34: p.I("FMOV.S @Rm,FRn", R(16), 9); break;                  // a misaligned FP load
        default: p.I(fe2[r.u(8)], R(16), R(16)); break;
        }
    };
    for (int k = 0; k < N; k++) emit(false);
    p.op(OP_SLEEP).op(OP_NOP);
    for (auto &l : lits) p.lit(l.first, l.second);
    put_prog(C, p);
    C.maxcyc = 60000;
    C.regions.push_back({BUF, 0x200});
    char b[160];
    snprintf(b, sizeof b, "seed %" PRIu64 "%s FPSCR %08X, %d instructions, hold %d", seed, fd ? " FD" : "", C.st.fpscr, N, C.slow_n);
    desc = b;
}

// ---- directed: the F1 operand trap against an older take (SOC.md 4.3.2).  A lane-1 FE op's F1 trap is decided in the
// cycle its lane-0 partner's fault is (EX); the partner is older, it takes at MA next cycle and kills the FE op: the
// trap must not be taken first (it would write its cause) -- ex_stop1 in v1_core's ftake.
static std::vector<TestDef> fh_tests() {
    std::vector<TestDef> v;
    auto D = [&](const char *n, const char *w, std::function<void(Case &)> f) { v.push_back({n, w, f}); };
    D("stop1_ld_fadd_v", "misaligned load ; FADD (sNaN, EN.V) paired in lane 1: the address error, FPSCR unchanged", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.align4().I("MOV.L @Rm,Rn", 1, 9).op(0xF320); });      // FADD FR2,FR3
        C.st.r[9] = BUF + 2; C.st.fpscr = 0x00040800u; C.st.fr[0][2] = 0x7FC00000u;
    });
    D("stop1_ld_fdiv_z", "misaligned load ; FDIV by zero (EN.Z) paired in lane 1: the address error, FPSCR unchanged", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.align4().I("MOV.L @Rm,Rn", 1, 9).op(0xF323); });      // FDIV FR2,FR3
        C.st.r[9] = BUF + 2; C.st.fpscr = 0x00040400u; C.st.fr[0][2] = 0; C.st.fr[0][3] = 0x3F800000u;
    });
    D("stop1_st_fadd_v", "misaligned store ; FADD (sNaN, EN.V) paired in lane 1: the address error, FPSCR unchanged", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.align4().I("MOV.L Rm,@Rn", 9, 1).op(0xF320); });
        C.st.r[9] = BUF + 2; C.st.fpscr = 0x00040800u; C.st.fr[0][2] = 0x7FC00000u;
    });
    D("stop1_control", "ADD ; FADD (sNaN, EN.V) paired in lane 1: the F1 trap is taken", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.align4().I("ADD #imm,Rn", 2, 0, 1).op(0xF320); });
        C.st.fpscr = 0x00040800u; C.st.fr[0][2] = 0x7FC00000u;
    });
    D("stop1_ld_fdiv_dtrap", "misaligned load ; FDIV 1/3 (EN.I, a D-decided trap) paired in lane 1: its trap pass (the "
      "recurrence) is killed by the load's fault, FPSCR unchanged", [](Case &C) {
        prog(C, [](Sh4Prog &p) { p.align4().I("MOV.L @Rm,Rn", 1, 9).op(0xF323); });      // FDIV FR2,FR3
        C.st.r[9] = BUF + 2; C.st.fpscr = 0x00040080u; C.st.fr[0][2] = 0x40400000u; C.st.fr[0][3] = 0x3F800000u;
    });
    // ---- the trap effects of a D-decided trap on the core (SOC.md 4.3.8): the computed cause, flags |= it, the
    // destination unchanged; the handler's first instruction is STS FPSCR,R3 (it waits for the trap pass's cause, which
    // for FDIV / FSQRT comes from the recurrence, as for any cause writer), then SLEEP.  [HW fpdn] rows give the value.
    struct Te { const char *n; const char *w; uint16_t op; uint32_t fpscr; std::map<int, uint32_t> fr; uint32_t hw; };
    for (auto t : std::vector<Te>{
             {"te_fsqrt_i", "FSQRT sqrt 2, EN.I: cause / flag I", 0xF36D, 0x00040080u, {{3, 0x40000000u}}, 0},
             {"te_fsqrt_hw", "FSQRT 1.5, EN.I, DN = 0 [HW fpdn 0x00001084]", 0xF36D, 0x00000080u, {{3, 0x3FC00000u}}, 0x00001084u},
             {"te_fadd_exact_hw", "an exact FADD, EN.I [HW 0x00040080]", 0xF320, 0x00040080u, {{2, 0x3F800000u}, {3, 0x3F800000u}},
              0x00040080u},
             {"te_fadd_i", "FADD 1 + 2^-24, EN.I (RN): cause / flag I", 0xF320, 0x00040080u,
              {{2, 0x33800000u}, {3, 0x3F800001u}}, 0},
             {"te_fmul_o", "FMUL 2^127 x 2^127, EN.O: cause / flag O I", 0xF322, 0x00040200u,
              {{2, 0x7F000000u}, {3, 0x7F000000u}}, 0},
             {"te_fdiv_i", "FDIV 1/3, EN.I: the recurrence's I", 0xF323, 0x00040080u, {{2, 0x40400000u}, {3, 0x3F800000u}}, 0},
             {"te_fdiv_z", "FDIV 1/0, EN.I: Z (the special case, through the recurrence)", 0xF323, 0x00040080u,
              {{2, 0}, {3, 0x3F800000u}}, 0},
             {"te_fdiv_dr_i", "FDIV DR 1/3, EN.I: the recurrence's I", 0xF243, 0x00080080u,
              {{2, 0x3FF00000u}, {3, 0}, {4, 0x40080000u}, {5, 0}}, 0},
             {"te_fmul_dr_hw", "FMUL.D max x 1.5, EN.I [HW fpdn 0x00085094]", 0xF242, 0x00080080u,
              {{2, 0x7FEFFFFFu}, {3, 0xFFFFFFFFu}, {4, 0x3FF80000u}, {5, 0}}, 0x00085094u},
             {"te_fcnvds_o", "FCNVDS 2^128, EN.O: cause / flag O I", 0xF2BD, 0x00080200u, {{2, 0x47F00000u}, {3, 0}}, 0},
         }) {
        D(t.n, t.w, [t](Case &C) {
            prog(C, [t](Sh4Prog &p) { p.I("ADD #imm,Rn", 1, 0, 1).op(t.op).I("ADD #imm,Rn", 2, 0, 1); });
            C.st.fpscr = t.fpscr;
            for (auto &kv : t.fr) C.st.fr[0][kv.first] = kv.second;
            C.mem.w16(VBR0 + 0x100, 0x036A); C.mem.w16(VBR0 + 0x102, OP_SLEEP); C.mem.w16(VBR0 + 0x104, OP_NOP);  // STS FPSCR,R3
            const uint32_t hw = t.hw;
            C.check = [hw](CoreRes &R) -> std::string {
                char b[120];
                if (R.ents.size() != 1 || R.ents[0].code != 0x120) return " want one 0x120 entry";
                if (hw && R.regs[3] != hw) { snprintf(b, sizeof b, " R3 (STS FPSCR) %08X, the console %08X", R.regs[3], hw); return b; }
                return "";
            };
        });
    }
    return v;
}

// --check: the commit log through the lockstep checker (v1_exc_tb.cpp's check_log); a log rejected for a reason the
// case names (Case::logx) or one the model cannot mirror is counted, not failed
static LogStat g_lst;
static uint64_t g_log_known = 0, g_log_skip = 0;
static std::string log_verdict(const Case &C, const CoreRes &R) {
    if (!g_log) return "";
    std::string e;
    const int v = check_log(C, R, g_lst, e);
    if (v == 2) { g_log_skip++; return ""; }
    if (v == 1 && C.logx && e.find(C.logx) != std::string::npos) { g_log_known++; return ""; }
    return v == 1 ? " commit log: " + e.substr(0, 1500) : "";
}
static void log_summary() {
    if (!g_log) return;
    printf("commit logs: %" PRIu64 " checked by tb/soc_checker (%" PRIu64 " records: %" PRIu64 " RETIRE, %" PRIu64 " EXC, %" PRIu64
           " register, %" PRIu64 " MEMW, %" PRIu64 " VOLRD), %" PRIu64 " rejected for a named reason, %" PRIu64 " not checkable\n",
           g_lst.progs, g_lst.recs, g_lst.retire, g_lst.exc, g_lst.regs, g_lst.memw, g_lst.volrd, g_log_known, g_log_skip);
}

static int fh_dir(Vv1_exc_tb *m, bool verbose) {
    int np = 0, nf = 0;
    for (auto &t : fh_tests()) {
        Case C;
        t.mk(C);
        CoreRes R;
        run_core(m, C, R);
        IssRes I;
        run_iss_te(C, I, &R);
        std::string d = compare(C, R, &I);
        if (C.check) d += C.check(R);
        d += log_verdict(C, R);
        if (d.empty()) np++; else nf++;
        if (!d.empty() || verbose) {
            printf("%s %-24s %s", d.empty() ? "pass " : "FAIL ", t.name.c_str(), d.c_str());
            if (verbose) { printf("  ["); for (auto &e : R.ents) printf(" %s;", ent_str(e).c_str()); printf("]"); }
            printf("\n");
        }
    }
    log_summary();
    printf("v1-fpuhold directed: %d pass, %d fail (FPU traps with O / U / I in their cause %" PRIu64 ", trap FPSCRs "
           "changed by SOC.md 4.3.8's rule %" PRIu64 ")\n", np, nf, g_te_oui, g_te_fixed);
    return nf ? 1 : 0;
}

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    long n = 1000;
    uint64_t seed = 1;
    bool verbose = false, list = false, dir = false;
    int cut = -1;                                     // debug: a SLEEP at instruction `cut` (bisecting a difference)
    uint32_t iss_at = 0;                              // debug: print the ISS's FP registers when it reaches this PC
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--n") && i + 1 < argc) n = atol(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = strtoull(argv[++i], nullptr, 0);
        else if (!strcmp(argv[i], "--trace")) g_trace = true;
        else if (!strcmp(argv[i], "-v")) verbose = true;
        else if (!strcmp(argv[i], "--list")) list = true;
        else if (!strcmp(argv[i], "--dir")) dir = true;
        else if (!strcmp(argv[i], "--check")) g_log = true;
        else if (!strcmp(argv[i], "--cut") && i + 1 < argc) cut = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--iss-at") && i + 1 < argc) iss_at = (uint32_t)strtoul(argv[++i], nullptr, 16);
    }
    auto m = std::make_unique<Vv1_exc_tb>();
    if (dir) return fh_dir(m.get(), verbose);
    int fp = 0, ff = 0;
    uint64_t nent = 0, nhold = 0;
    std::map<uint32_t, uint64_t> bycode;
    for (long k = 0; k < n; k++) {
        Case C;
        std::string desc;
        fpfuzz_case(C, seed + (uint64_t)k, desc);
        if (cut >= 0) C.mem.w16(CODE + 2 * (uint32_t)cut, OP_SLEEP);
        if (iss_at) {                                 // debug: the ISS's FP state at the first arrival at iss_at
            PhysMem mm = C.mem; ExcEnv env; env.m = &mm;
            env.flt_lo = C.flt_lo; env.flt_hi = C.flt_hi; env.flt_rd = C.flt_rd; env.flt_wr = C.flt_wr;
            Sh4Iss iss(&mm); iss.soc = true; iss.env = &env; iss.s = C.st;
            for (int q = 0; q < 100000 && iss.s.pc != iss_at; q++) if (!iss.step()) break;
            printf("ISS at %08X: FPSCR %08X FPUL %08X\n", iss.s.pc, iss.s.fpscr, iss.s.fpul);
            for (int b2 = 0; b2 < 2; b2++) { printf("  bank %d:", b2); for (int i = 0; i < 16; i++) printf(" %08X", iss.s.fr[b2][i]); printf("\n"); }
        }
        if (list) {                                   // the program, disassembled in its initial FPU mode
            printf("%s  FR bank0:", desc.c_str());
            for (int i = 0; i < 16; i++) printf(" %08X", C.st.fr[0][i]);
            printf("\n");
            for (uint32_t a = CODE; a < CODE + 0x100; a += 2) {
                const uint16_t op = (uint16_t)C.mem.rd(a, 2);
                printf("  %08X %04X %s\n", a, op, Sh4Iss::disasm(op, a, C.st.fpscr).c_str());
                if (op == OP_SLEEP) { for (uint32_t b = a + 2; b < a + 0x30; b += 4) printf("  %08X .long %08X\n", b, C.mem.rd(b, 4)); break; }
            }
        }
        CoreRes R;
        run_core(m.get(), C, R);
        IssRes I;
        run_iss_te(C, I, &R);
        const std::string d = compare(C, R, &I) + log_verdict(C, R);
        if (C.slow_n) nhold++;
        nent += R.ents.size();
        for (auto &e : R.ents) bycode[e.kind == 3 ? 0x1000 | e.code : e.code]++;
        if (d.empty()) { fp++; if (verbose) printf("ok   %s\n", desc.c_str()); }
        else { ff++; if (ff <= 20) printf("FAIL fpfuzz %s:%s\n", desc.c_str(), d.c_str()); }
    }
    log_summary();
    printf("v1-fpuhold fpfuzz: %d / %ld programs equal to the ISS (%" PRIu64 " entries:", fp, n, nent);
    for (auto &b : bycode) printf(" %s%03X x%" PRIu64, b.first & 0x1000 ? "RESET" : "", b.first & 0xFFF, b.second);
    printf("), %d differ; %" PRIu64 " with a memory hold; FPU traps with O / U / I in their cause %" PRIu64
           ", trap FPSCRs changed by SOC.md 4.3.8's rule (the ISS's own trap_fpscr: V / Z / E) %" PRIu64 "\n", ff, nhold,
           g_te_oui, g_te_fixed);
    (void)v1_exc_main;
    return ff ? 1 : 0;
}
