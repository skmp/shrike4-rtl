#!/usr/bin/env python3
"""v1_exc_mutants.py -- planted-bug check of WI-CORE-A steps A2 and A3 (make v1-exc-mutants; rtl/v1/SOC.md 4.2-4.4:
exceptions, interrupts, SLEEP), with the WI-CORE-A verification's surviving mutants and each fix of WI-CORE-A-fixes
reverted (agents/reports/units/WI-CORE-A-fixes.md).

Each mutant copies rtl/v1/v1_core.sv into build/v1_exc_mut/<id>/, replaces exactly one source fragment (the script
refuses a pattern that does not occur exactly once, so a mutant can never silently be the original), builds the
v1-exc testbench against the copy (make v1-exc-build with V1_EXC_CORE / V1_EXC_MDIR overridden) and runs the probe the
mutant names:
  dir         the directed tests of tb/v1_exc_tb.cpp (against the ISS in SoC mode)
  fuzz        300 random programs against the ISS
  fuzzirq     300 random programs with scripted interrupts against the ISS (--fuzz-irq)
  fuzzx       300 programs of the widened generator (--fuzzx: memory holds, register branches, the multiplier)
  bm:<prog>   a bare-metal console probe (tb/bm/soc-design, tb/bm/core-exc) on the core, checked with its check.py
              --level core against the console's numbers (SOC.md Appendix A)
Every probe runs with --check: the commit log of each directed row and fuzz program also goes through tb/soc_checker
(SOC.md 6.3), so the commit-log tap's planted bugs are caught there.  A mutant is caught when the probe fails.  The
unmodified build must pass every probe first (the control).  Exit 1 if the control fails or any mutant survives.

    python3 tb/v1_exc_mutants.py [--jobs N] [--only ID,ID] [--keep]
"""
import argparse, os, shutil, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "build", "v1_exc_mut")
CORE = ["rtl/v1/v1_core.sv", "rtl/v1/v1_fdiv.sv", "rtl/v1/v1_fetch.sv", "rtl/v1/v1_fpu.sv", "rtl/v1/v1_ideal_mem.sv"]
BM = os.path.join(ROOT, "build", "bm")
SUITE = {"c1i": "core-exc", "c1k": "core-exc"}                    # a bm probe's suite (tb/bm/<suite>), soc-design otherwise

# id, original fragment, mutated fragment, probe, what it breaks
# WI-CORE-B (2026-09-23): the fetch poison, its MD and the odd-PC tag suppression moved into v1_fetch (SOC.md 4.1 item
# 2); their planted bugs d1-rte / d1-odd-pre are re-homed in tb/v1_fetchx_mutants.py (make v1-fetchx-mutants), which
# mutates v1_fetch.sv / the fetch MD and runs these same directed rows plus a slow-fetch RTE-slot case.
MUTANTS = [
    ("flag-kill", "if (ex[l].v && !kex[l] && !(l == 1 && ex_stop1)) begin",
     "if (ex[l].v && !(l == 1 && ex_stop1)) begin", "dir",
     "a micro-op killed in the take cycle still writes its end-of-EX flags (TRAPA ; SETT)"),
    ("flag-stop1", "if (ex[l].v && !kex[l] && !(l == 1 && ex_stop1)) begin", "if (ex[l].v && !kex[l]) begin", "dir",
     "no same-cycle gating: a paired SETT after a faulting load writes T"),
    # (fcmp-kill -- the core's !kex[0] on FCMP's F1 T -- is gone: since WI-CORE-F v1_fpu gates t_we with its own ISN kill,
    # so the core's term is a second gate no core-level probe can see; tb/v1_fpuhold_mutants.py plants the removal of
    # v1_fpu's gate (fcmp-t-kill) and its unit vector kill_fcmp_t catches it)
    ("mac-order", "u.maddr = u.step == 3'd0 ? u.b : u.a; sz = 32'd4; end", "u.maddr = u.step == 3'd0 ? u.a : u.b; sz = 32'd4; end",
     "dir", "MAC.L reads @Rm first (v1's order; C3)"),
    ("slot-spc", "tk_spc = in_slot[dl] ? q_pc[dl] - 32'd2 : q_pc[dl];", "tk_spc = q_pc[dl];", "dir",
     "a D-stage fault in a delay slot saves the slot, not the branch"),
    ("trapa-spc", "tk_spc = ma[ml].pc + 32'd2; tk_hit = ma_hit[ml];", "tk_spc = ma[ml].pc; tk_hit = ma_hit[ml];", "dir",
     "TRAPA saves its own PC"),
    # (WI-CORE-F: the S / MA takes' kill is computed as ok_* in a block of its own; pk_* adds the F1 trap to it)
    ("fault-incl", "else if (ma_tk[0]) begin ok_isn = ma[0].isn; ok_incl = ma[0].fx != FX_NONE; end",
     "else if (ma_tk[0]) begin ok_isn = ma[0].isn; ok_incl = 1'b0; end", "dir",
     "a data fault kills only younger instructions: the faulting micro-op completes (@Rm+ updates Rm)"),
    ("fault-off", "if (ma[l].fx != FX_NONE) ma_tkb[l] = 1'b1;", "if (1'b0) ma_tkb[l] = 1'b1;", "dir",
     "a data fault found in EX is never taken"),
    ("vec400", "if (ma[ml].fvec400) tk_vec = VEC_TLB_MISS;", "if (1'b0) tk_vec = VEC_TLB_MISS;", "dir",
     "a TLB miss vectors to VBR + 0x100"),
    ("user-addr", "prv = !ex_smd[l] && (sqw ? sqmd : u.maddr[31]);", "prv = 1'b0;", "dir",
     "no user-mode address error"),
    ("align-w", "((u.msz == 2'd1 && u.maddr[0]) ||", "((1'b0) ||", "dir", "no word alignment check"),
    ("align-ctl", "mis = !(dk_ctl(u.dk) || u.dk == DK_TAS_RD || u.dk == DK_TAS_WR) &&",
     "mis = !(u.dk == DK_TAS_RD || u.dk == DK_TAS_WR) &&", "dir", "PREF / OCBx get an alignment check"),
    ("addr-over-tlb", "if (mis || prv) f = FX_ADDR;\n        else if (!(u.dk == DK_PREF && !sqw)) f = dxl.fx;",
     "if (dxl.fx != FX_NONE && !(u.dk == DK_PREF && !sqw)) f = dxl.fx;\n        else if (mis || prv) f = FX_ADDR;", "dir",
     "a TLB fault outranks the address error"),
    ("pref-drop", "else if (!(u.dk == DK_PREF && !sqw)) f = dxl.fx;", "else if (1'b1) f = dxl.fx;", "dir",
     "a PREF outside the SQ takes its TLB fault"),
    ("rmw-wtype", "u.fwr = dk_write(u.dk) || (dex.rmw && (f == FX_TLB_PROT || f == FX_IPW));", "u.fwr = dk_write(u.dk);",
     "dir", "TAS.B / AND.B protection faults are read type"),
    ("sgr-gate", "&& !ksl[l] && !(exf_v && sl[l].isn == exf_isn)) begin", "&& !ksl[l] && 1'b1) begin", "dir",
     "STC.L SGR's Rn - 4 retires although its store faults"),
    ("bl-reset", "ent_rst <= tk_mhit ? 2'd2 : (tk_gen && sr_rd[28]) ? 2'd1 : 2'd0;", "ent_rst <= tk_mhit ? 2'd2 : 2'd0;",
     "dir", "a general exception with SR.BL = 1 enters instead of resetting"),
    ("trap-d", "trap_d[s] = fpu_trap_d[s];", "trap_d[s] = 1'b0;",
     "dir", "no D-decided FPU trap"),
    ("sleep-kill", "if (tk && !frz) sleep_issued <= 1'b0;", "", "dir",
     "a SLEEP killed by an older take still halts the core (and the wake never ends the sleep state)"),
    ("post-carry", "slot0_q <= 1'b1; dly_cont <= br_taken[1] ? br_tgt[1] : q_pc[1] + 32'd4; post_carry <= tg_post[1];",
     "slot0_q <= 1'b1; dly_cont <= br_taken[1] ? br_tgt[1] : q_pc[1] + 32'd4; post_carry <= 2'b00;", "dir",
     "a delayed branch's post-execution tag is not carried to its slot"),
    ("kill-ma", "assign kill_ma = rst_c || stake_c;", "assign kill_ma = rst_c;", "dir",
     "the data break does not withdraw the younger access in MA1"),
    ("cmf-at-take", "if (exc0_s) h = ent_hit;", "if (tk) h = tk_hit;", "dir",
     "a break's CMF is set at its take even when an older break kills it (AB_sameSPC)"),
    ("exc0-late", "if (tk) exc0_now = !ftake && !tk_lsc && !(sq_on && !sq_kill);", "if (tk) exc0_now = 1'b0;", "bm:c1",
     "EXC0 issues one cycle after t: every entry 1 cycle late (E 7 / 9 against 6 / 8)"),
    ("claim-off", "    end else if (dtake)\n      tk_ncl = dl ?", "    end else if (1'b0)\n      tk_ncl = dl ?", "bm:c1h",
     "no P claim at a D-stage take: a load behind X costs nothing (c1h ld: E 8 against 6)"),
    ("claim-1step", "ent_rc <= tk_lsc ? {tk_ncl, 1'b0} - 4'd1 : 4'd0;", "ent_rc <= tk_lsc ? {1'b0, tk_ncl} - 4'd1 : 4'd0;", "bm:c1h",
     "the claim releases in one step per claimant: P = 1"),
    ("claim-q2", "tk_ncl = dl ? 3'(!c[0].br_dly && q2_mem && !q_dep[0])", "tk_ncl = dl ? 3'(1'b0 && q2_mem && !q_dep[0])",
     "dir", "no claim from the entry behind a follower-candidate take (claim_follower; no console probe has one)"),
    ("claim-ma-near", "tk_ncl = tk_ncl + 3'(qm[0]) + 3'(!q_br[0] && qm[1]) +",
     "tk_ncl = tk_ncl + 3'(qm[0]) + 3'(1'b0) +", "bm:c1i",
     "an MA take claims only for the nearest queue entry (c1i TRAPA ; ADD ; ADD ; load: E 10 against 8)"),
    ("claim-q3", "assign q3_mem = f_w_mem[3];", "assign q3_mem = 1'b0;", "dir",
     "the claim does not reach q3 (claim_q3: H'FFFD behind TAS.B, a load 3 places behind)"),
    ("claim-issued", "if (!brs && y.step == 3'd0 && memop_u(y)) tk_ncl", "if (!brs && k < 2 && y.step == 3'd0 && memop_u(y)) tk_ncl",
     "bm:c1i",
     "an issued younger access does not claim an MA take (c1i TRAPA ; load)"),
    ("claim-single", "ent_rc <= tk_lsc ? {tk_ncl, 1'b0} - 4'd1 : 4'd0;", "ent_rc <= tk_lsc ? 4'd1 : 4'd0;", "dir",
     "one claim whatever the number of claimants (irq_h_two_loads: h = t + 10 [HW c1j ldxld1: + 4])"),
    ("claim-slot", "tk_ncl = tk_ncl + 3'(qm[0]) + 3'(!q_br[0] && qm[1]) +",
     "tk_ncl = tk_ncl + 3'(qm[0]) + 3'((!q_br[0] || c[0].br_dly) && qm[1]) +",
     "bm:c1b", "a delayed branch's slot claims an MA take (c1b B v3 j2 trap: 15 against 13)"),
    ("follower-take", "wire fc1    = s_v[1] && k0 == 3'd1", "wire fc1    = 1'b0 && s_v[1] && k0 == 3'd1", "bm:c1b",
     "the follower candidate takes a cycle later, as the leader (d0: 9 against 8)"),
    ("save-tag", "u.sxw = uk == UK_EXC_SAVE;", "u.sxw = 1'b0;", "bm:c1c",
     "EXC_SAVE carries no system-register tag: STC SPC first in the handler reads early (c1c V5)"),
    ("fl-restore", "{nsr[0], nsr[1], nsr[8], nsr[9]} = ma[l].fl_prev;", "rs = rs;", "dir",
     "the data break at S does not undo the flags the killed MA group wrote (ubc_data_flag)"),
    ("redirect-late", "wire        exc_redir = exc_rd && !frz;", "wire        exc_redir = exc_rd && !frz && exc_sv;", "dir",
     "the entry's redirect never happens"),
    # ---- step A3: interrupts, SLEEP (SOC.md 4.4)
    ("irq-bl", "(irq.nmi || !sr_rd[28] || sleep_issued)", "(irq.nmi || 1'b1 || sleep_issued)", "dir",
     "an ordinary interrupt is taken with SR.BL = 1"),
    ("irq-nmi", "(irq.nmi || !sr_rd[28] || sleep_issued)", "(!sr_rd[28] || sleep_issued)", "dir",
     "an NMI the INTC presents with BL = 1 (NMIB) waits for BL = 0"),
    ("irq-asleep", "(irq.nmi || !sr_rd[28] || sleep_issued)", "(irq.nmi || !sr_rd[28])", "dir",
     "asleep, BL is not ignored: a SLEEP with BL = 1 never wakes"),
    ("irq-slot", "wire ibound = (q_v[0] || sleep_issued || f_starve) && !slot0_q && !sq_on",
     "wire ibound = (q_v[0] || sleep_issued || f_starve) && !sq_on", "dir",
     "an interrupt between a delayed branch and its slot"),
    ("irq-seq", "wire ibound = (q_v[0] || sleep_issued || f_starve) && !slot0_q && !sq_on",
     "wire ibound = (q_v[0] || sleep_issued || f_starve) && !slot0_q", "dir",
     "an interrupt inside a multi-cycle instruction's sequence"),
    # (no mutant for ibound's !ent_blk: an entry flushes the queue at its take and fetch stays stopped until the vector's
    # redirect, so the queue is empty whenever ent_blk is set -- the term is redundant with q_v[0], an equivalent mutant)
    ("irq-prio", "wire itake  = !pk_v && ibound && irq.req", "wire itake  = !pk_v && ibound && !dfault[0] && !trap_d[0] && irq.req", "dir",
     "a D-stage fault of the leader wins over an acceptable interrupt (HW PF: the interrupt first)"),
    ("irq-issue", "wire iss0 = iss0_raw && !dstop && !dfault[0] && !trap_d[0];", "wire iss0 = iss0_raw && !pk_v && !dfault[0] && !trap_d[0];",
     "dir", "the leader also issues in the cycle its interrupt is taken"),
    ("irq-ack-early", "irq_ack = exc0_s && ent_intr && !ent_nmi;", "irq_ack = itake && !irq.nmi;", "dir",
     "irq_ack at the take, not with the entry's architectural write (a killed entry acknowledges)"),
    ("irq-intevt", "excw = '{v: 1'b1, intr: ent_intr,", "excw = '{v: 1'b1, intr: 1'b0,", "dir",
     "an interrupt writes EXPEVT instead of INTEVT"),
    ("irq-vec", "tk_code = irq.code; tk_vec = VEC_INTERRUPT;", "tk_code = irq.code; tk_vec = VEC_GENERAL;", "dir",
     "the interrupt vector is VBR + 0x100"),
    ("irq-code-late", "excw = '{v: 1'b1, intr: ent_intr, code: ent_code,", "excw = '{v: 1'b1, intr: ent_intr, code: ent_intr ? irq.code : ent_code,",
     "dir", "INTEVT is the INTC's code at EXC0's S, not the code latched at the take"),
    ("irq-claim", "      tk_ncl = 3'(qm[0]) + 3'(!q_br[0] && qm[1]) + 3'(!q_br[0] && !q_br[1] && q2_mem) +",
     "      tk_ncl = 3'(1'b0) + 3'(!q_br[0] && qm[1]) + 3'(!q_br[0] && !q_br[1] && q2_mem) +", "dir",
     "the interrupted instruction does not claim (SOC.md 4.3.3's reading; HW c1j ldx: + 2, irq_h_load_leader, c1j_ldx_irq)"),
    ("irq-claim-next", "      tk_ncl = 3'(qm[0]) + 3'(!q_br[0] && qm[1]) + 3'(!q_br[0] && !q_br[1] && q2_mem) +",
     "      tk_ncl = 3'(qm[0]) + 3'(1'b0) + 3'(!q_br[0] && !q_br[1] && q2_mem) +", "dir",
     "the instruction after the interrupted one does not claim (irq_h_load_next: h = t + 8)"),
    ("irq-pmc", "pmc_ev[PMC_INTERRUPT] = irq_ack;", "pmc_ev[PMC_INTERRUPT] = 1'b0;", "dir",
     "PMC 0x16 does not count interrupts"),
    # (SLEEP's k = 4 is v1_pkg's since WI-0b (SOC.md 4.4.3); the planted bug overrides it back to v1's 1 in the core)
    ("sleep-k4", "wire [2:0] k0 = c[0].k;", "wire [2:0] k0 = c[0].form_x == XF_SLEEP ? 3'd1 : c[0].k;", "dir",
     "SLEEP holds D for one cycle, not four (a pending request is taken 1 cycle after it)"),
    ("sleep-req-drain", "sleep_req = sleep_issued && pipe_empty && !itake && run && run_q;",
     "sleep_req = sleep_issued && !itake && run && run_q;", "dir",
     "sleep_req before EX / MA / S are empty"),
    ("sleep-req-take", "sleep_req = sleep_issued && pipe_empty && !itake && run && run_q;",
     "sleep_req = sleep_issued && pipe_empty && run && run_q;", "dir",
     "sleep_req in the cycle the waking interrupt is taken (the CPG stops a core that is entering)"),
    # ---- the WI-CORE-A verification's survivors (agents/verify/WI-CORE-A/vx_mutants.py), now caught by make v1-exc
    ("mhit-as-exc", "ent_rst <= tk_mhit ? 2'd2 : (tk_gen && sr_rd[28]) ? 2'd1 : 2'd0;",
     "ent_rst <= (tk_gen && sr_rd[28]) ? 2'd1 : 2'd0;", "dir",
     "a data TLB multiple hit is taken as an ordinary exception instead of the 0x140 reset (vx_mhit_*)"),
    ("sq-tlb-noreq", "(ma[l].dk == DK_ST && ma[l].maddr[31:26] == 6'b111000 && ma[l].fx != FX_ADDR &&",
     "(1'b0 && ma[l].maddr[31:26] == 6'b111000 && ma[l].fx != FX_ADDR &&", "dir",
     "an SQ store with a TLB fault no longer writes the queue (mmu 3.8 [HW]; sq_st_tlb*: the SQ image)"),
    ("post-bl", "ma_tkb[l] = ma[l].trap || (!ldsr && ((post_b && !ma[l].blb && !bla) || (op_b && !bla))) ||",
     "ma_tkb[l] = ma[l].trap || (!ldsr && (post_b || (op_b && !bla))) ||", "dir",
     "a post-execution break is taken with SR.BL = 1 (vx_ubc_post_bl1)"),
    ("data-bl", "sbrk_c = ubc_ctl.b_live && !sr_pre[28];", "sbrk_c = ubc_ctl.b_live;", "dir",
     "a data break is taken with SR.BL = 1 (vx_ubc_data_bl1)"),
    ("exc0-s-kill", "if (sl[l].v && sl[l].uk == UK_EXC0 && !ksl[l] && !frz) exc0_s = 1'b1;",
     "if (sl[l].v && sl[l].uk == UK_EXC0 && !frz) exc0_s = 1'b1;", "dir",
     "an EXC0 killed at S by an older S-stage take still commits its entry (vx_ubc_data_exc0_beside)"),
    ("stake-restore-mask", "if (!rs && kma[l] && (ma[l].tw || ma[l].qmw || ma[l].sw || ma[l].form == F_FCMP_EQ_FRM_FRN ||",
     "if (!rs && kma[l] && (ma[l].tw || ma[l].form == F_FCMP_EQ_FRM_FRN ||", "dir",
     "the data-break restore ignores Q / M / S writers (vx_ubc_data_flag_s)"),
    ("slot-ill-first", "else if (sr_rd[15] && c[s].fpu_insn) dcode[s] = in_slot[s] ? EXP_SLOT_FPUDIS : EXP_FPU_DIS;\n      else if (in_slot[s] && c[s].slot_ill) dcode[s] = EXP_SLOT_ILL;",
     "else if (in_slot[s] && c[s].slot_ill) dcode[s] = EXP_SLOT_ILL;\n      else if (sr_rd[15] && c[s].fpu_insn) dcode[s] = in_slot[s] ? EXP_SLOT_FPUDIS : EXP_FPU_DIS;", "dir",
     "slot illegal outranks FPU disable (vx_fd_slot_undecoded_f: 0x1A0 instead of 0x820)"),
    ("fetch-stop-off", "assign fetch_stop = ent_act || rst_wait;", "assign fetch_stop = rst_wait;", "dir",
     "fetch keeps running from the take to the vector redirect (the fetch_err monitor, SOC.md 4.1 item 8)"),
    # ---- the fixes (WI-CORE-A-fixes): each reverted must fail its repro
    ("f1-l1req", "&& !kill_ma && !(l == 1 && ma_tkb[0]);", "&& !kill_ma;", "dir",
     "F1: the paired lane-1 access of a lane-0 post-execution break is presented (vx_ubc_post_l1acc_*, sq_post_l1acc)"),
    # (f2-exf / f2-hazard, the interim core-side hiding of a faulting multiplier-record writer, are gone: WI-CORE-F's
    # ISN kill of v1_fpu's multiplier records replaced them; tb/v1_fpuhold_mutants.py plants that kill's removal and
    # the same vx_macfault_* rows catch it)
    ("f3-defer", "stake_c = sbrk_c && (!s_ybr || ent_act);\n    sdef_c = sbrk_c && s_ybr && !ent_act;",
     "stake_c = sbrk_c;\n    sdef_c = 1'b0;", "dir",
     "F3: a data break on an access paired with a delayed branch is taken at S: the slot does not complete "
     "(vx_ubc_data_pair_*: HW c1k)"),
    ("f3-defer-c1k", "stake_c = sbrk_c && (!s_ybr || ent_act);\n    sdef_c = sbrk_c && s_ybr && !ent_act;",
     "stake_c = sbrk_c;\n    sdef_c = 1'b0;", "bm:c1k",
     "F3 against the console's own program: the break at S (tools/hw/core-exc c1k: SPC, R9 at the break)"),
    ("f3-defer-spc", "tk_isn = ma[ml].isn + 8'd1; tk_spc = ma[ml].ftea; // the slot completes",
     "tk_isn = ma[ml].isn + 8'd1; tk_spc = ma[ml].pc - 32'd2; // the slot completes", "dir",
     "F3: the deferred data break saves the branch instead of its continuation (vx_ubc_data_pair_*)"),
    ("f3-pend", "else if (sdef_c) begin dbrk_pend <= 1'b1;", "else if (1'b0) begin dbrk_pend <= 1'b1;", "dir",
     "F3: a slot that reaches MA after the access's S never takes the break (vx_ubc_data_pair_slot_use)"),
    ("f3-restore", "if (stake || tk_dbs)\n", "if (stake)\n", "dir",
     "F3: the slot's killed partner keeps its T write (vx_ubc_data_pair_bfs_nt_flag)"),
    ("f3-entry", "stake_c = sbrk_c && (!s_ybr || ent_act);\n    sdef_c = sbrk_c && s_ybr && !ent_act;",
     "stake_c = sbrk_c && !s_ybr;\n    sdef_c = sbrk_c && s_ybr;", "dir",
     "F3: the slot's own earlier D-stage entry wins over the older data break (vx_ubc_data_pair_slot_ill)"),
    ("f3-entry-spc", "tk_spc = s_ybr ? sl[s_yl].pc : sl[s_yl].ftea;", "tk_spc = sl[s_yl].ftea;", "dir",
     "F3: the S take that kills the slot's entry saves the slot, not its branch (vx_ubc_data_pair_slot_ill)"),
    ("f3-dslot", "if (dbrk_on && !dl && slot0_q) begin", "if (1'b0) begin", "dir",
     "F3: the slot's D-stage trap after the break is known wins over it (vx_ubc_data_pair_slot_ftrap)"),
    # (no mutant for `!sdef_c` in the CMF path: a deferred break always commits -- nothing older is left to kill it --
    # so setting CMFB at the match or at EXC0's S gives the same flag before the handler runs: equivalent)
    ("f4-brstop", "if (br_u(y)) brs = 1'b1;", "if (1'b0) brs = 1'b1;", "dir",
     "F4: an issued delayed branch's issued slot claims an MA take (vx_claim_branch_stop_issued)"),
    ("f5-step0", "if (!brs && y.step == 3'd0 && memop_u(y)) tk_ncl", "if (!brs && memop_u(y)) tk_ncl", "dir",
     "F5: TAS.B / MAC.L with micro-ops in MA and EX claim twice at a data break (vx_claim_data_*)"),
    ("o1-runq", "sleep_req = sleep_issued && pipe_empty && !itake && run && run_q;",
     "sleep_req = sleep_issued && pipe_empty && !itake && run;", "dir",
     "O1: after a spurious wake the core stays asleep with run = 1 (vx_sleep_spurious_wake)"),
    ("d1-poison", "else if (q_fx[s]) begin dcode[s]", "else if (1'b0) begin dcode[s]", "dir",
     "D1: no fetch fault: an odd / user-mode P1 PC runs on (vx_fetch_*)"),
    ("d1-tea", "tk_tea_v = (dtake0 || dtake1) && d_ifx[dl];", "tk_tea_v = 1'b0;", "dir",
     "D1: a fetch fault does not write TEA / PTEH"),
    # ---- the conformance step (agents/reports/integration/conform-WI-CORE-A.md; SOC.md 8.4 WI-CORE-A)
    # (3) the D-stage priority (SOC.md 4.3.2): a fetch multiple hit over the pre-execution break, other fetch faults below
    ("mhit-pre", "if (q_fx[s] && q_fxe[s] == FX_MHIT) begin dcode[s] = fx_expevt(FX_MHIT, 1'b0, 1'b1); d_ifx[s] = 1'b1; end\n"
     "      else if (pre_brk[s]) begin", "if (1'b0) begin dcode[s] = fx_expevt(FX_MHIT, 1'b0, 1'b1); d_ifx[s] = 1'b1; end\n"
     "      else if (pre_brk[s]) begin", "dir",
     "(3) the pre-execution break outranks a fetch multiple hit (cf_fetch_mhit_pre: 0x1E0 instead of the 0x140 reset)"),
    ("fetch-over-pre", "if (q_fx[s] && q_fxe[s] == FX_MHIT) begin dcode[s] = fx_expevt(FX_MHIT, 1'b0, 1'b1);",
     "if (q_fx[s]) begin dcode[s] = fx_expevt(q_fxe[s], 1'b0, 1'b1);", "dir",
     "(3) every fetch fault outranks the pre-execution break (cf_fetch_itlbmiss_pre: 0x0A0 / 0x040 instead of 0x1E0)"),
    # (1) the commit-log tap (SOC.md 6.3), each caught by tb/soc_checker or the testbench's stream checks (--check)
    ("tap-last", "if (last_uop(sl[l]) && !bubble(sl[l])) begin", "if (!bubble(sl[l])) begin", "fuzz",
     "(1) every micro-op retires, not the instruction's last (MAC.L / TAS.B / STC.L retire twice)"),
    ("tap-lanes", "tap.rec[TR_RET + l] = crec(CK_RETIRE,", "tap.rec[TR_RET + 1 - l] = crec(CK_RETIRE,", "fuzz",
     "(1) two RETIREs of one cycle out of program order"),
    ("tap-kill", "if (!rst_c && !frz && sl[l].v && !ksl[l]) begin", "if (!rst_c && !frz && sl[l].v) begin", "dir",
     "(1) a micro-op the take kills in S logs its write (MAC.x @Rn+ with a faulting @Rm: vx_macfault_*)"),
    ("tap-sgr-gate", "if (!(exf_v && sl[l].isn == exf_isn)) begin          // (the write-back's STC.L SGR gate)",
     "if (1'b1) begin", "dir",
     "(1) STC.L SGR's Rn - 4 is logged although the write-back drops it (its store faults in EX)"),
    ("tap-r8", "if (p[4]) return {5'd1, p[2:0]};", "if (p[4]) return {5'd0, p[2:0]};", "fuzz",
     "(1) R8-R15 logged as R0-R7"),
    ("tap-bank", "return {5'd2, p[2:0]};", "return {5'd0, p[2:0]};", "dir",
     "(1) the other bank's R0-R7 (LDC Rm,Rn_BANK) logged as the instruction's own bank (cf_tap_regs)"),
    ("tap-exc0-sr", "if (sl[l].uk == UK_EXC0 && ent_rst == 2'd0) tap.rec[TR_WR + 3 * l + 2]",
     "if (1'b0) tap.rec[TR_WR + 3 * l + 2]", "fuzz",
     "(1) no SR record for an entry's MD / RB / BL write"),
    ("tap-save-isn", "tap.rec[TR_WR + 3 * l]     = srec(SY_SSR, sv_isn, ssr_hold);",
     "tap.rec[TR_WR + 3 * l]     = srec(SY_SSR, sl[l].isn, ssr_hold);", "fuzz",
     "(1) EXC_SAVE's SSR record carries EXC_SAVE's own ISN, not the entry's"),
    ("tap-reset-exc", "{20'd0, RESET_PC, ent_rst == 2'd2 ? EXP_MHIT : EXP_MANUAL}",
     "{20'd0, RESET_PC, EXP_MANUAL}", "dir",
     "(1) a multiple-hit reset logged as 0x020 (vx_mhit_*)"),
    ("tap-sleep", "if (sl[l].form == F_NONE && sl[l].op == 16'h001B) tap.rec[TR_SLP]", "if (1'b0) tap.rec[TR_SLP]", "fuzz",
     "(1) no SLEEP record"),
    ("tap-wake", "if (!rst_c && run && !run_q && sleep_issued) tap.rec[TR_SLP]", "if (1'b0) tap.rec[TR_SLP]", "fuzzirq",
     "(1) no WAKE record"),
    ("tap-fpu", ".trap_f1, .trap_f1_lane, .trap_f1_isn, .frec(fpu_rec),", ".trap_f1, .trap_f1_lane, .trap_f1_isn, .frec(),",
     "fuzz", "(1) v1_fpu's records (FPR / MACH / MACL) not in the tap"),
    ("tap-nisn", "else nx_isn = isn_ctr + 8'((ent_act && ent_rst == 2'd0) || exc_sv);", "else nx_isn = isn_ctr;", "fuzz",
     "(1) next_isn is the next ISN to issue during an entry (EXC_SAVE's, never a record)"),
    ("tap-nisn-l1", "if (tk && !frz) nx_isn = exc0_l1 ? isn_ctr : tk_isn;", "if (tk && !frz) nx_isn = tk_isn;", "fuzz",
     "(1) next_isn skips the leader issuing beside a follower-candidate take's EXC0"),
    ("tap-nisn-issue", "else nx_isn = isn_ctr + 8'((ent_act && ent_rst == 2'd0) || exc_sv);",
     "else nx_isn = isn_ctr + 8'(pop) + 8'((ent_act && ent_rst == 2'd0) || exc_sv);", "fuzz",
     "(1) next_isn counts this cycle's issues as retired"),
    ("tap-isn-reset", "if (rst.por) isn_ctr <= '0;", "isn_ctr <= '0;", "dir",
     "(1) the ISN restarts at a CPU-only reset: the instructions after it reuse open ISNs (cf_tap_reset_isn)"),
    # (2) SLEEP's k = 4 is v1_pkg's (sleep-k4 above, re-anchored)
]


def sh(cmd, log):
    with open(log, "w") as f:
        return subprocess.run(cmd, cwd=ROOT, stdout=f, stderr=subprocess.STDOUT).returncode


def build(tag, core, jobs):
    mdir = os.path.join(OUT, tag, "obj")
    log = os.path.join(OUT, tag, "build.log")
    os.makedirs(os.path.dirname(log), exist_ok=True)
    rc = sh(["make", "-s", "v1-exc-build", "NJOBS=%d" % jobs, "V1_EXC_MDIR=" + mdir, "V1_EXC_CORE=" + " ".join(core)], log)
    return os.path.join(mdir, "tb") if rc == 0 else None


def run_probe(exe, probe, d):
    log = os.path.join(d, probe.replace(":", "_") + ".log")
    if probe == "dir":
        return sh([exe, "--check"], log)
    if probe == "fuzz":
        return sh([exe, "--fuzz", "300", "--seed", "7", "--check"], log)
    if probe == "fuzzirq":
        return sh([exe, "--fuzz-irq", "300", "--seed", "7", "--check"], log)
    if probe == "fuzzx":
        return sh([exe, "--fuzzx", "300", "--seed", "7", "--check"], log)
    prog = probe.split(":")[1]
    suite = SUITE.get(prog, "soc-design")
    out = os.path.join(d, prog + ".out")
    # a mutant can send a handler astray: the bound is ~5x the longest probe (c1i: 3.9M cycles)
    rc = sh([exe, "--bm", os.path.join(BM, suite, prog + ".bin"), "--out", out, "--maxcyc", "20000000"], log)
    if rc != 0:
        return rc
    return sh(["python3", "tb/bm/%s/check.py" % suite, out, "--level", "core", "--prog", prog], log + ".check")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--jobs", type=int, default=6)
    ap.add_argument("--only", default="")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    only = set(x for x in a.only.split(",") if x)
    muts = [m for m in MUTANTS if not only or m[0] in only]
    ncpu = min(12, os.cpu_count() or 4)          # the machine's build-parallelism cap (12)
    bj = max(1, ncpu // max(1, a.jobs))
    os.makedirs(OUT, exist_ok=True)
    for suite in ("soc-design", "core-exc"):
        if subprocess.run(["make", "-s", "-C", "tb/bm/" + suite], cwd=ROOT, stdout=subprocess.DEVNULL).returncode:
            print("tb/bm/%s: build FAILED" % suite)
            return 1
    probes = sorted(set(m[3] for m in muts))
    exe = build("control", CORE, ncpu)
    if exe is None:
        print("control: build FAILED (%s)" % os.path.join(OUT, "control"))
        return 1
    ok = True
    for p in probes:
        rc = run_probe(exe, p, os.path.join(OUT, "control"))
        print("control  probe %-8s %s" % (p, "pass" if rc == 0 else "FAIL (the unmodified core must pass)"))
        ok &= rc == 0
    if not ok:
        return 1

    def one(m):
        mid, old, new, probe, why = m
        src = os.path.join(ROOT, "rtl", "v1", "v1_core.sv")
        text = open(src).read()
        if text.count(old) != 1:
            return (mid, "BAD PATTERN (occurs %d times)" % text.count(old), why, probe)
        d = os.path.join(OUT, mid)
        os.makedirs(d, exist_ok=True)
        dst = os.path.join(d, "v1_core.sv")
        open(dst, "w").write(text.replace(old, new))
        core = [os.path.relpath(dst, ROOT) if os.path.basename(f) == "v1_core.sv" else f for f in CORE]
        exe = build(mid, core, bj)
        if not exe:
            return (mid, "BUILD FAILED", why, probe)
        rc = run_probe(exe, probe, d)
        return (mid, "caught" if rc != 0 else "SURVIVED", why, probe)

    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        res = list(ex.map(one, muts))
    bad = 0
    for mid, st, why, probe in res:
        print("%-14s %-8s probe %-8s %s" % (mid, st, probe, why))
        bad += st != "caught"
    print("v1-exc-mutants: %d / %d caught" % (len(res) - bad, len(res)))
    if not bad and not a.keep:
        shutil.rmtree(OUT, ignore_errors=True)
    print("v1-exc-mutants: %s" % ("PASS" if not bad else "FAIL"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
