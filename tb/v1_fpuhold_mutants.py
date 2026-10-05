#!/usr/bin/env python3
"""v1_fpuhold_mutants.py -- planted-bug check of WI-CORE-F (make v1-fpuhold-mutants; rtl/v1/SOC.md 4.3.1, 4.3.2, 4.3.8,
4.5, 6.3): the FPU kill, the FRCHG / FSCHG undo, the cause-tag repair, the traps and their effects (the computed cause
of a trap pass, E alone, FDIV / FSQRT through the recurrence), the hold split and the result-pass request, the
commit-log record port, and the pre-existing faults this work item's tests found and fixed.

Each mutant copies one RTL file (rtl/v1/v1_fpu.sv, v1_fdiv.sv, v1_core.sv or rtl/fpu_vec/fpu_vec_top.sv) into
build/v1_fpuhold_mut/<id>/, replaces exactly one source fragment (a pattern that does not occur exactly once is refused,
so a mutant can never silently be the original), builds the one testbench its probe needs against the copy and runs it:
  ci3    tb/v1_fpuhold_tb        the console's ci3 loops (SOC.md 4.5 targets)
  ci4    tb/v1_fpuhold_tb --probe the console's ci4 loops (SOC.md 9.3)
  unit   tb/v1_fpuhold_unit      v1_fpu alone: hold rule, held-run values, kill / trap vectors, the trap_d table
  fuzz   tb/v1_fpuhold_x         3000 random FPU programs against the ISS (the FPU's record check bound in: FATAL), each
                                 commit log through tb/soc_checker (--check)
  fhdir  tb/v1_fpuhold_x --dir   the directed F1-trap-vs-older-take and trap-effect cases (--check)
  dir    tb/v1_exc_tb            WI-CORE-A's v1-exc directed tests (SoC-mode ISS)
A mutant is caught when the probe fails.  The unmodified build must pass every probe first (the control).  Exit 1 if
the control fails or any mutant survives.

Not planted (equivalent, argued in agents/reports/units/WI-CORE-F.md): the grant's !hold_pipe term (every pass stage moves
only in unheld cycles), the capture's !hold_div term (one recurrence at a time: a held `done` finds its record
captured), FCMP's T gated by !f1_trap (a taken F1 trap's kill covers it, an untaken one is killed by the older take);
(agents/reports/integration/conform-WI-CORE-F.md) the F1 trap's computed cause f1_cx / db_x against V / Z alone (a V / Z
trap's operation is invalid or a division by zero: its qNaN / infinity is exact, no O / U / I), rec_ovf_err (a monitor of
a structural bound no program reaches).
Build parallelism is capped (the machine rule: -j 12 at most, one wide job at a time): --jobs builds at once, CAP / jobs
Verilator / compiler threads each.
    python3 tb/v1_fpuhold_mutants.py [--jobs N] [--only ID,ID] [--keep]
"""
import argparse, os, shutil, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "build", "v1_fpuhold_mut")
CORE = ["rtl/v1/v1_core.sv", "rtl/v1/v1_fdiv.sv", "rtl/v1/v1_fetch.sv", "rtl/v1/v1_fpu.sv", "rtl/v1/v1_ideal_mem.sv"]
VEC = "rtl/fpu_vec/fpu_vec_top.sv"
FPU, FDIV, CORE_SV = "rtl/v1/v1_fpu.sv", "rtl/v1/v1_fdiv.sv", "rtl/v1/v1_core.sv"

# id, file, original fragment, mutated fragment, probe, what it breaks
MUTANTS = [
    # ---- the hold split and the result-pass request (SOC.md 4.5)
    ("div-frozen", CORE_SV, ".hold_div(pfz || !run)", ".hold_div(frz)", "ci3",
     "the divider freezes under mah too (v1's frz): ci3 p4_fdiv 17, p2_fdiv 30"),
    ("fdiv-hold-pipe", FPU, ".clk, .rst, .start(dv_start), .hold(hold_div),", ".clk, .rst, .start(dv_start), .hold(hold_pipe),",
     "ci3", "v1_fdiv on the pipe's hold"),
    ("grant-ack", FPU, "dv_gnt = steal && !hold_pipe && !mahq;", "dv_gnt = steal && !hold_pipe;", "ci3",
     "the result pass granted in the ack cycle: ci3 p2_fdiv 22 (console 24)"),
    ("grant-ack-ci4", FPU, "dv_gnt = steal && !hold_pipe && !mahq;", "dv_gnt = steal && !hold_pipe;", "ci4",
     "the same against ci4: fd 22, fd_n4 24, fd_n8 26, fd_u2 22 (console 24 26 28 24)"),
    ("rq-late", FDIV, "assign rq   = act && cnt == 5'd2;", "assign rq   = act && cnt == 5'd1;", "ci3",
     "the request posted one step late: every quotient one cycle late (ram_fdiv 16)"),
    ("vec-no-hold", FPU, ".clk, .rst, .start(vs_start), .hold(hold_pipe), .op(vs_op)",
     ".clk, .rst, .start(vs_start), .hold(hold_div), .op(vs_op)", "unit",
     "the vector array runs on under mah (its records do not): holdval FIPR / FTRV / FSCA / FSRRA"),
    # ---- the kill (SOC.md 4.3.1)
    ("kill-f1", FPU, "f2 <= (f1.v && !kl(f1.isn)) ? f1_out : '0;", "f2 <= f1.v ? f1_out : '0;", "unit",
     "an FE op killed in F1 goes on (kill_fadd_f1)"),
    ("kill-f1-dir", FPU, "f2 <= (f1.v && !kl(f1.isn)) ? f1_out : '0;", "f2 <= f1.v ? f1_out : '0;", "dir",
     "the same against v1-exc: pk_ld_fadd / pk_trapa_fadd [HW ci]"),
    ("kill-f2", FPU, "fs <= (f2.v && !kl(f2.isn)) ? f2 : '0;", "fs <= f2;", "unit", "an FE op killed in F2 writes at FS"),
    ("kill-db", FPU, "if (db_age[k] == 5'(db_l1[k]) + 5'd2 || kl(db_isn[k])) db_on[k] <= 1'b0;",
     "if (db_age[k] == 5'(db_l1[k]) + 5'd2) db_on[k] <= 1'b0;", "unit", "a killed double writes its words"),
    ("kill-dv", FPU, "if ((dv_dbl[k] ? ps_hi[k] : ps_lo[k]) == 3'd3 || kl(dv_isn[k])) dv_on[k] <= 1'b0;",
     "if ((dv_dbl[k] ? ps_hi[k] : ps_lo[k]) == 3'd3) dv_on[k] <= 1'b0;", "unit", "a killed divide's record writes"),
    ("kill-fdiv", FDIV, "      if (killed) act <= 1'b0;\n", "", "unit", "the recurrence of a killed divide runs on (kill_frees_divider)"),
    ("kill-vq", FPU, "if ((vq[i].age >= vq[i].last && vq[i].age >= vq[i].fs) || kx(vq[i].isn)) vq[i].v <= 1'b0;",
     "if (vq[i].age >= vq[i].last && vq[i].age >= vq[i].fs) vq[i].v <= 1'b0;", "unit", "a killed vector op's record stays (kill_frees_vec_dst)"),
    ("kill-ftrv", FPU, "if (ftrv_row == 2'd3 || kl(ftrv_isn)) ftrv_on <= 1'b0;", "if (ftrv_row == 2'd3) ftrv_on <= 1'b0;",
     "unit", "a killed FTRV's rows keep starting (kill_frees_ftrv_rows: vec_orph_err)"),
    ("kill-mq", FPU, "if (mq[i].eta == 3'd0 || kl(mq[i].isn)) mq[i].v <= 1'b0;", "if (mq[i].eta == 3'd0) mq[i].v <= 1'b0;",
     "dir", "a faulting MACx writer's record writes MACL / MACH (vx_macfault_* [WI-CORE-A verification F2])"),
    ("fcmp-t-kill", FPU, "assign t_we = f1_t_we && !f1_trap && !kl(f1.isn);", "assign t_we = f1_t_we && !f1_trap;", "unit",
     "a killed FCMP writes T (kill_fcmp_t; the core's own !kex[0] is a second gate, v1_exc_mutants.py)"),
    ("kill-fcd", FPU, "if (fcd_v && kl(fcd_isn)) fcd_v <= 1'b0;", "if (1'b0) fcd_v <= 1'b0;", "unit",
     "FCMP DR's pending compare pass survives its kill"),
    ("frchg-undo", FPU, "if (f1.v && kl(f1.isn)) f[21:20] = f[21:20] ^ f1.mfl;", "", "unit",
     "a killed FRCHG / FSCHG in F1 keeps its flip"),
    ("frchg-undo-dir", FPU, "if (f2.v && kl(f2.isn)) f[21:20] = f[21:20] ^ f2.mfl;", "", "dir",
     "a killed FRCHG / FSCHG in F2 keeps its flip (pk_ld_frchg [HW])"),
    ("cz-repair", FPU, "if (kill_v) cz_ctr <= cz_eff;", "if (1'b0) cz_ctr <= cz_eff;", "unit",
     "the cause-tag counter is not taken back (kill_younger_fadd_older_cause)"),
    ("cz-restore", FPU, "if (kill_v && sv && st == cz_eff) f[17:12] = sc;", "if (1'b0) f[17:12] = sc;", "unit",
     "the older writer's skipped cause does not come back (kill_restores_older_cause)"),
    ("cz-lds", FPU, "for (int l = 0; l < 2; l++) if (sl[l].v && sl[l].fscw) sv = 1'b0;", "", "fuzz",
     "a stale cause comes back across an LDS FPSCR"),
    # ---- the traps (SOC.md 4.3.2, 4.3.8)
    ("trap-f1-off", FPU, "trap_f1 = f1_trap || db_trap || vq_trap;", "trap_f1 = db_trap || vq_trap;", "unit",
     "no F1 operand trap of a single op (f1_*)"),
    ("trap-f1-dir", FPU, "trap_f1 = f1_trap || db_trap || vq_trap;", "trap_f1 = db_trap || vq_trap;", "dir",
     "the same against v1-exc: fpe_v_snan [HW ci2: E = 8]"),
    ("trap-db-off", FPU, "trap_f1 = f1_trap || db_trap || vq_trap;", "trap_f1 = f1_trap || vq_trap;", "unit",
     "no F1 trap of a double (f1_fadd_dr_snan_v, f1_fdiv_dr_z)"),
    ("trap-vec-mask", FPU, "if (ftk && vq_trap && vq[i].isn == kill_isn) begin vq[i].dst <= '0; vq[i].idst <= '0; end", "",
     "unit", "a vector op's F1 trap writes its destination (f1_fipr_v_runs_on)"),
    ("trap-pass-cause", FPU, "if (fs.v && fs.tp) begin tcw_v = 1'b1; tcw_c = fs.e ? 6'b100000 : {1'b0, fs.x}; tcw_cz = fs.cz; end",
     "", "unit", "a D-decided trap writes no cause (d_fadd_i_snan, d_fmul_o_ovf)"),
    # ---- the trap effects (SOC.md 4.3.8): the computed cause, E alone, FDIV / FSQRT through the recurrence
    ("tp-oui-zero", FPU, "else begin f1_out.e = f1_vze[0]; f1_out.x = r.x; end",
     "else begin f1_out.e = f1_vze[0]; f1_out.x = '{v: r.x.v, z: r.x.z, o: 1'b0, u: 1'b0, i: 1'b0}; end", "unit",
     "the old rule: a single trap pass writes V / Z only (d_fadd_i_inexact, d_fmul_o_ovf, d_fsqrt... [HW 0x00001084])"),
    ("tp-oui-zero-fuzz", FPU, "else begin f1_out.e = f1_vze[0]; f1_out.x = r.x; end",
     "else begin f1_out.e = f1_vze[0]; f1_out.x = '{v: r.x.v, z: r.x.z, o: 1'b0, u: 1'b0, i: 1'b0}; end", "fhdir",
     "the same on the core against the ISS (te_fadd_i, te_fmul_o)"),
    ("tp-oui-zero-dbl", FPU, "f.tpx = dd_e[s] ? 6'b100000 : {1'b0, dd_r[s].x};",
     "f.tpx = dd_e[s] ? 6'b100000 : {1'b0, dd_r[s].x.v, dd_r[s].x.z, 3'b000};", "unit",
     "a double's trap pass writes V / Z only (d_fmul_dr_o_hw [HW 0x00085094], d_fcnvds_o)"),
    ("tp-e-sel", FPU, "else begin f1_out.e = f1_vze[0]; f1_out.x = r.x; end", "else begin f1_out.e = 1'b0; f1_out.x = r.x; end",
     "unit", "an E trap pass writes the computed cause, not E alone (d_fadd_i_denorm_e)"),
    ("tp-div-norun", FPU, "dv_s_start = 1'b1; dv_s_a = n; dv_s_b = m;", "dv_s_start = !f1.tp; dv_s_a = n; dv_s_b = m;", "unit",
     "a single FDIV / FSQRT trap pass does not run the recurrence: its record never completes (d_fdiv_i, d_fsqrt_i)"),
    ("tp-div-dst", FPU, "dv_dst[j] <= trap_d[s] ? '0 : di_d[s].fw;", "dv_dst[j] <= di_d[s].fw;", "unit",
     "a divide's trap pass writes its quotient (d_fdiv_i: dest)"),
    ("tp-div-dst-core", FPU, "dv_dst[j] <= trap_d[s] ? '0 : di_d[s].fw;", "dv_dst[j] <= di_d[s].fw;", "fhdir",
     "the same on the core against the ISS (te_fdiv_i, te_fdiv_dr_i)"),
    ("tp-dv-e", FPU, "dv_wr_te ? 6'b100000 : {1'b0, dv_wr_x}", "{1'b0, dv_wr_x}", "unit",
     "a divide's E trap pass writes the special case's cause, not E (d_fdiv_i_denorm_e, d_fdiv_dr_i_denorm_e)"),
    ("tp-dv-e-single", FPU, "dv_st[k] <= 1'b1; dv_te[k] <= f1.tp && f1_vze[0];", "dv_st[k] <= 1'b1; dv_te[k] <= 1'b0;", "unit",
     "a single divide's E trap pass loses its E (d_fdiv_i_denorm_e)"),
    ("tp-db-check", FPU, "db_x[0] <= trap_d[s] ? '0 : dd_r[s].x; db_e[0] <= !trap_d[s] && dd_e[s];",
     "db_x[0] <= dd_r[s].x; db_e[0] <= dd_e[s];", "unit",
     "an FDIV DR trap pass raises an F1 operand trap as well (d_fdiv_dr_iz_z, d_fdiv_dr_i_denorm_e: also an F1 trap)"),
    ("tp-fscr-wait", FPU, "      if ((dv_dbl[k] ? ps_hi[k] : ps_lo[k]) < 3'd2) begin pend_st |= dv_dst[k]; pend_wr |= dv_dst[k]; end\n      fscr_busy = 1'b1;",
     "      if ((dv_dbl[k] ? ps_hi[k] : ps_lo[k]) < 3'd2) begin pend_st |= dv_dst[k]; pend_wr |= dv_dst[k]; end\n      fscr_busy = fscr_busy || dv_dst[k] != '0;",
     "unit", "STS / LDS FPSCR do not wait for a divide trap pass's cause (d_fdiv_sts_waits)"),
    ("tp-fscr-wait-core", FPU, "      if ((dv_dbl[k] ? ps_hi[k] : ps_lo[k]) < 3'd2) begin pend_st |= dv_dst[k]; pend_wr |= dv_dst[k]; end\n      fscr_busy = 1'b1;",
     "      if ((dv_dbl[k] ? ps_hi[k] : ps_lo[k]) < 3'd2) begin pend_st |= dv_dst[k]; pend_wr |= dv_dst[k]; end\n      fscr_busy = fscr_busy || dv_dst[k] != '0;",
     "fhdir", "the same on the core: the handler's STS FPSCR reads the cause before the recurrence wrote it (te_fdiv_i)"),
    ("tp-div-busy-f1", FPU, "div_busy_nx = (f1.v && (f1.form == F_FDIV_FRM_FRN || f1.form == F_FSQRT_FRN)) ||",
     "div_busy_nx = (f1.v && !f1.tp && (f1.form == F_FDIV_FRM_FRN || f1.form == F_FSQRT_FRN)) ||", "unit",
     "a trapped FDIV in F1 does not hold the next divide (d_fdiv_div_busy: it abandons the trap pass's recurrence)"),
    # ---- the commit-log record port (SOC.md 6.3)
    ("rec-s-fpr", FPU, "if (ns < 6'd2) frec[ns[0]] = mkrec(v1_soc_pkg::CK_FPR, 8'(w), sl[l].isn, w[0] ? sl[l].fwo : sl[l].fwe);",
     "if (1'b0) frec[ns[0]] = mkrec(v1_soc_pkg::CK_FPR, 8'(w), sl[l].isn, w[0] ? sl[l].fwo : sl[l].fwe);", "fuzz",
     "no record of an LS-pipe FR write (FMOV / FMOV.S / FLDI / FSTS: the bound record check)"),
    ("rec-s-fpscr", FPU, "if (ns < 6'd2) frec[ns[0]] = mkrec(v1_soc_pkg::CK_SYS, IX_FPSCR, sl[l].isn, sl[l].fscv);",
     "if (1'b0) frec[ns[0]] = mkrec(v1_soc_pkg::CK_SYS, IX_FPSCR, sl[l].isn, sl[l].fscv);", "fuzz",
     "no record of LDS / LDS.L FPSCR"),
    ("rec-s-fpul", FPU, "if (ns < 6'd2) frec[ns[0]] = mkrec(v1_soc_pkg::CK_SYS, IX_FPUL, sl[l].isn, sl[l].uv);",
     "if (1'b0) frec[ns[0]] = mkrec(v1_soc_pkg::CK_SYS, IX_FPUL, sl[l].isn, sl[l].uv);", "fuzz",
     "no record of an LS-pipe FPUL write (LDS Rm,FPUL / FLDS / LDS.L: the bound record check)"),
    ("rec-dv", FPU, "if (nf < 6'd2) frec[2 + 32'(nf[0])] = mkrec(v1_soc_pkg::CK_FPR, 8'(w), dv_wr_isn, dv_wr_v);",
     "if (1'b0) frec[2 + 32'(nf[0])] = mkrec(v1_soc_pkg::CK_FPR, 8'(w), dv_wr_isn, dv_wr_v);", "unit",
     "no record of a divider result pass (rec_fdiv, the hold runs' record check)"),
    ("rec-vc-isn", FPU, "vc_live = 1'b1; vc_isn = vq[i].isn;", "vc_live = 1'b1; vc_isn = di_isn[0];", "unit",
     "a vector result's record carries another ISN (rec_fipr, rec_ftrv)"),
    ("rec-db-isn", FPU, "db_wr_m = db_dst[k] & 32'h5555_5555; db_wr_v = db_res[k]; db_wr_isn = db_isn[k];",
     "db_wr_m = db_dst[k] & 32'h5555_5555; db_wr_v = db_res[k]; db_wr_isn = db_isn[0];", "unit",
     "an older double's upper word carries the younger double's ISN (rec_fadd_dr_fadd_dr)"),
    ("rec-frchg-val", FPU, "fscr_after = fpscr_w ^ (32'(yfl) << 20);", "fscr_after = fpscr_w;", "unit",
     "FRCHG's record keeps a younger FRCHG's flip (rec_frchg_frchg)"),
    ("rec-frchg-kill", FPU, "if (fs.mfl != 2'b00 && !kl(fs.isn)) begin", "if (fs.mfl != 2'b00) begin", "unit",
     "a record of an FRCHG killed at FS (kill_frchg_3: the ISN rule, the record check)"),
    ("rec-hold", FPU, "    if (!rst && !hold_pipe) begin\n      // the LS pipe at S", "    if (!rst) begin\n      // the LS pipe at S",
     "unit", "records in held cycles too (rec_held: one record, in its write cycle)"),
    ("rec-mac-both", FPU, "mc_h = mq[i].clr || (mq[i].def ? mq[i].defh : !(mq[i].op == M_MULL || mq[i].op == M_MULSW || mq[i].op == M_MULUW));",
     "mc_h = 1'b1;", "unit", "MUL.L / LDS MACL also report MACH (rec_mul, rec_lds_macl)"),
    ("trap-e-off", FPU, "if (!dn0) f1_vze[0] = 1'b0;", "f1_vze[0] = 1'b0;", "unit", "no E trap (f1_fadd_denorm_e)"),
    ("trap-selfkill", FPU, "return kill_v && yng(i, kill_isn, kill_incl || ftk);", "return kill_v && yng(i, kill_isn, kill_incl);",
     "unit", "a taken F1 trap's own work goes on: the destination is written"),
    ("ftake-stop1", CORE_SV, "&& !ma_tk[0] && !ma_tk[1] && !(trap_f1_lane && ex_stop1);", "&& !ma_tk[0] && !ma_tk[1];",
     "fhdir", "a lane-1 F1 trap is taken before its lane-0 partner's fault (stop1_ld_fadd_v)"),
    ("ftake-conv", CORE_SV, "if (ftake && trap_f1_lane == l[0]) ma[l] <= mk_nop(UK_EXC0, tk_spc, tk_isn);", "", "dir",
     "the F1 trap's micro-op is not converted into EXC0 (fpe_v_snan: no entry)"),
    ("dmw-off", CORE_SV, "wire dtake0 = !dstop && lead && dfault[0] && !dmw[0];", "wire dtake0 = !dstop && lead && dfault[0];",
     "fuzz", "an H'Fxxx is taken as illegal in the FPU mode an LDS FPSCR is about to change"),
    ("fpscr-reset", FPU, "if (!rst_q) fpscr <= FPSCR_RESET;", "", "dir",
     "FPSCR keeps its value over the CPU-only reset (vx_bl1_reset_fpscr)"),
    # ---- the pre-existing faults found by these tests (fixed here)
    ("sts-fpscr-order", FPU, "assign fpscr_now = f1.v ? fpscr_w ^ (32'(f1.mfl) << 20) : fpscr_w;", "assign fpscr_now = fpscr_w;",
     "fuzz", "STS FPSCR sees a younger FRCHG / FSCHG's flip"),
    ("vn-8", FPU, "localparam int VN = 12;", "localparam int VN = 8;", "unit",
     "8 vector records: FTRV ; FIPR ; FTRV drops a row (ftrv_fipr_ftrv)"),
    ("fipr-naninf", VEC, "else if ((ia || ib) && !na && !nb) begin", "else if (ia || ib) begin", "unit",
     "FIPR counts a NaN x inf product as an infinity: V (fipr_nan_inf_cause)"),
    ("fipr-inf-wrap", VEC, "if (fva[i][31] ^ fvb[i][31]) n_ninf = n_ninf + 3'd1; else n_pinf = n_pinf + 3'd1;",
     "if (fva[i][31] ^ fvb[i][31]) n_ninf = {1'b0, n_ninf[1:0] + 2'd1}; else n_pinf = {1'b0, n_pinf[1:0] + 2'd1};",
     "unit", "the 2-bit infinity count: four infinities -> +0 (fipr_four_inf)"),
]


def sh(cmd, log):
    with open(log, "w") as f:
        return subprocess.run(cmd, cwd=ROOT, stdout=f, stderr=subprocess.STDOUT).returncode


def build(tag, probe, core, vec, jobs):
    d = os.path.join(OUT, tag)
    os.makedirs(d, exist_ok=True)
    log = os.path.join(d, "build_%s.log" % probe)
    common = ["NJOBS=%d" % jobs, "V1_FH_CORE=" + " ".join(core), "V1_FH_VEC=" + vec]
    if probe in ("ci3", "ci4"):
        rc = sh(["make", "-s", "v1-fpuhold-build", "V1_FH_MDIR=" + os.path.join(d, "obj")] + common, log)
        return os.path.join(d, "obj", "tb") if rc == 0 else None
    if probe == "unit":
        rc = sh(["make", "-s", "v1-fpuhold-unit-build", "V1_FH_UMDIR=" + os.path.join(d, "uobj")] + common, log)
        return os.path.join(d, "uobj", "tb") if rc == 0 else None
    if probe in ("fuzz", "fhdir"):
        rc = sh(["make", "-s", "v1-fpuhold-x-build", "V1_FH_XMDIR=" + os.path.join(d, "xobj")] + common, log)
        return os.path.join(d, "xobj", "tb") if rc == 0 else None
    # dir: WI-CORE-A's v1-exc (its RTL list names fpu_vec_top itself: vector mutants use the other probes)
    rc = sh(["make", "-s", "v1-exc-build", "NJOBS=%d" % jobs, "V1_EXC_MDIR=" + os.path.join(d, "eobj"),
             "V1_EXC_CORE=" + " ".join(core)], log)
    return os.path.join(d, "eobj", "tb") if rc == 0 else None


def run_probe(exe, probe, d):
    log = os.path.join(d, probe + ".log")
    if probe == "ci4":
        return sh([exe, "--probe"], log)
    if probe == "fuzz":
        return sh([exe, "--n", "3000", "--seed", "7", "--check"], log)
    if probe == "fhdir":
        return sh([exe, "--dir", "--check"], log)
    return sh([exe], log)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--only", default="")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    only = set(x for x in a.only.split(",") if x)
    muts = [m for m in MUTANTS if not only or m[0] in only]
    ncpu = min(8, os.cpu_count() or 4)               # the build threads in use at once (the machine's -j cap is 12)
    bj = max(1, ncpu // max(1, a.jobs))
    os.makedirs(OUT, exist_ok=True)
    if subprocess.run(["make", "-s", "-C", "tb/bm/core-integration"], cwd=ROOT, stdout=subprocess.DEVNULL).returncode:
        print("tb/bm/core-integration: build FAILED")
        return 1
    ok = True
    for p in sorted(set(m[4] for m in muts)):
        exe = build("control", p, CORE, VEC, ncpu)
        rc = run_probe(exe, p, os.path.join(OUT, "control")) if exe else 1
        print("control  probe %-5s %s" % (p, "pass" if rc == 0 else "FAIL (the unmodified RTL must pass)"))
        ok &= rc == 0
    if not ok:
        return 1

    def one(m):
        mid, f, old, new, probe, why = m
        text = open(os.path.join(ROOT, f)).read()
        if text.count(old) != 1:
            return (mid, "BAD PATTERN (occurs %d times)" % text.count(old), why, probe)
        d = os.path.join(OUT, mid)
        os.makedirs(d, exist_ok=True)
        dst = os.path.join(d, os.path.basename(f))
        open(dst, "w").write(text.replace(old, new))
        rel = os.path.relpath(dst, ROOT)
        core = [rel if c == f else c for c in CORE]
        vec = rel if f == VEC else VEC
        exe = build(mid, probe, core, vec, bj)
        if not exe:
            return (mid, "BUILD FAILED", why, probe)
        rc = run_probe(exe, probe, d)
        return (mid, "caught" if rc != 0 else "SURVIVED", why, probe)

    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        res = list(ex.map(one, muts))
    bad = 0
    for mid, st, why, probe in res:
        print("%-16s %-8s probe %-5s %s" % (mid, st, probe, why))
        bad += st != "caught"
    print("v1-fpuhold-mutants: %d / %d caught" % (len(res) - bad, len(res)))
    if not bad and not a.keep:
        shutil.rmtree(OUT, ignore_errors=True)
    print("v1-fpuhold-mutants: %s" % ("PASS" if not bad else "FAIL"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
