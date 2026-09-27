// v1_core.sv -- SH7091 CPU core, functional and cycle accurate (no caches, no MMU, no BSC).  See README.md / ARCH.md.
//
// Pipeline:   I  -- fetch, instruction queue (v1_fetch)
//             D  -- decode two instructions, read registers, find hazards against the later stages, pair, issue
//             EX -- ALU / shifter, address adder, T; FPU dispatch; multiplier operands
//             MA -- data memory (answers like a cache hit); FPU / MAC -> CPU transfers are sampled here
//             S  -- register write-back (write-through: a read in the same cycle sees it)
// Two issue lanes run EX / MA / S side by side (lane 0 = the older instruction of its issue cycle).  The FPU
// (v1_fpu) has its own pipe (F1 F2 FS, the vector sequencer, the double-precision sequencer, the divider, the
// multiplier) and its own hazard logic; it answers "may this D-stage instruction issue" for the FPU resources.
//
// Every stall is a pipeline side effect, derived where the comparator is (the timing numbers are the SH7091's, measured
// by the timings2 sweep; rtl/timing-proxy reproduces them as a table, this core as mechanics):
//   * a GPR result is on the bypass network from the end of the stage that produces it (EX: ALU, MOV, address update,
//     STC; MA: load data); operands are taken at the start of EX, so a consumer can issue one cycle after an EX result
//     (latency 1, and never in the same pair) and two cycles after a load (latency 2: the D-stage comparator holds it
//     while the load is in EX).  STS FPUL / FPSCR / MACx,Rn sample the FPU in MA and have no bypass: their Rn reaches
//     the register file at S, so consumers are held while they are in EX or MA (latency 3).
//   * MOV Rm,Rn is a copy: a paired follower that reads Rn takes Rm's value (latency 0).
//   * T is read in D, with a bypass from the EX stage of the CPU (latency 1 from CMP / DT / ... to ADDC or a branch);
//     FCMP's T comes over the FPU -> CPU flag path at the end of F1, without a bypass (latency 2).
//   * STC SR / STC.L SR read the architectural SR, whose flags are written at S: they wait while a flag writer is in
//     EX or MA (or FCMP in F1 / F2): latency 3 = the writer's last issue cycle + 3.
//   * system registers (SSR SPC DBR VBR GBR banked R0-R7 S PR) are written at S with write-through, no bypass; one tag
//     rides the writer's first micro-op, and a reader in the leader slot waits while that tag is in EX or MA
//     (latency 3; coarse: no register number compare).  A GBR-relative MOV that pairs as follower is not checked.
//   * SHAD / SHLD use the barrel shifter in EX; an integer load aligns its data with the same shifter in its S stage,
//     so a shift is held for the one cycle in which a load is in MA (the manual's "load latency 3 to SHAD").
//   * the decoder quirks the silicon has (SWAP.B/W reads Rn; LDC Rm,SR / LDC Rm,GBR also read R[op[7:4]]) are in the
//     operand decoder (tp_decode.sv), so they meet the same comparators.
//   * the memory system (rtl/v1/SOC.md 3.2): fetch through `ifq` / `ifr`; a data access presents its address in EX
//     (`dex`, answered by the MMU in the same cycle: `dxl`) and its request in MA (`lreq` / `lrsp`: the MA port
//     contract of SOC.md 3.2.2, once per access, never withdrawn, the ack latched as `ma_done`).  Two holds:
//       - `pfz` holds the whole pipeline -- fetch, D, the sequencer, EX / MA / S and the FPU: the operand cache's
//         miss path (`lrsp.frz`, 2 cycles: OCBI / OCBP / OCBWB / PREF / store / MOVCA.L of an absent line, measured 2),
//         LDTLB's UTLB write (`lt_frz`, 2 cycles in LDTLB's own MA stage), and the OCBWB port collision (`oc_coll`:
//         OCBWB keeps the cache port one cycle longer, so an access in EX right behind it -- a load, store, PREF,
//         cache-block op or LDTLB, which would need the port in its MA cycle -- holds the pipeline for one cycle,
//         charged in the OCBWB's ack cycle).  The freezes add up (OCBWB ; OCBI costs 3).
//       - `mah` (an access presented and not answered) holds the same stages (every longer memory wait).
//
// Exceptions (SOC.md 4.3; WI-CORE-A step A2).  Every entry is a take in one cycle t, a kill, and an occupancy chain
// through the ordinary pipeline latches, so every hold of the pipeline delays it by construction:
//   * the take cycle t: a D-stage fault (illegal, privileged, slot illegal, FPU disable, UBC pre-execution break, a
//     fetch fault of an odd or user-mode P1-P4 PC) in the first cycle its instruction would issue; a D-decided FPU
//     trap (v1_fpu's trap_d: an enabled O / U / I, FTRV's V) in its issue cycle; the F1 operand trap (v1_fpu's
//     trap_f1: V / Z / E) in the instruction's first F1 cycle, where its own EX micro-op becomes EXC0 in place (S
//     still at issue + 3, kill > K, no claim [P]; WI-CORE-F); a data fault (decided in EX from the address and the
//     MMU's answer, carried by the micro-op), TRAPA and the UBC post-execution / operand-address breaks in the MA
//     cycle the micro-op leaves; the UBC data break in the access's first S cycle, or, when the access's group ends in
//     a one-cycle delayed branch, in its slot's MA cycle (the slot completes [HW c1k]).  The oldest take in program
//     order wins.
//   * the kill: every latch whose ISN is younger than (or equal to, for the re-execution types) the excepting
//     instruction's, the fetch queue, the sequencer, the redirects; the writes of a killed micro-op in the take cycle
//     (S-stage writes, end-of-EX flag writes, FCMP's F1 T) are gated.
//   * the entry chain: an optional release of the operand-address claim (`ent_rc`, a down-counter of 2 steps per
//     claimant: P = 2 when an already fetched memory instruction follows in program order: issued, or in the queue up
//     to q3, not past a branch; tools/hw/core-exc c1i: 1, 2 and 3 places behind; an interrupt counts each claimant, the
//     interrupted instruction included, c1j), EXC0 through EX / MA / S (at S: SSR / SGR captured, SR.MD
//     RB BL <- 1, the `excw` pulse), the `exc_rd` flop, the redirect to the vector, EXC_SAVE issued in the vector's
//     fetch cycle (at S: SSR SPC SGR written; its system-register tag holds STC SSR / SPC / SGR for two cycles), then
//     v1's redirect-to-issue.  Unheld: handler issue h = t + 6 (+ 2 with the claim) -- measured D-stage 6, commit 8
//     from issue (t = D + 2), +2 with a memory instruction behind (SOC.md App. A).
//
// Interrupts, SLEEP, privilege (SOC.md 4.4; WI-CORE-A step A3).  The core keeps no interrupt state of its own: `irq`
// (v1_intc: the IMASK compare, every synchroniser, the NMI's BL / NMIB rule) is used combinationally in D, and the
// core applies SR.BL there, at once, from the architectural SR with the S-stage write-through (`sr_imask` / `sr_bl`
// leave on the same write-through):
//   * the boundary (`ibound`): the D leader is valid, not a delay slot (RTE's included), no sequence runs, no hold, no
//     entry in progress; asleep, the instruction after SLEEP whether it is in the queue or not.  An interrupt is taken
//     (`itake`) at a boundary with irq.req and (NMI, or BL = 0, or asleep): t = that D cycle, the leader does not issue,
//     the entry is a D-stage take (kills nothing, EXC0 in lane 0; the claim: 2 per fetched memory instruction from the
//     leader on [HW c1j: + 2 at X, 1 or 2 after X, + 4 with two, SOC.md 9.1's interrupt part]), with
//     the code latched at t (a request that vanishes later still enters with it [M 19.2.3]), VBR + 0x600, SPC = the
//     leader (a branch target after a slot [HW]; SLEEP + 2 asleep [HW]).  An older MA / S take wins (program order); an
//     interrupt wins over a D-stage fault of the same leader [HW excint2 PF].  irq_ack / nmi_ack, INTEVT (`excw.intr`)
//     and PMC 0x16 / 0x17 go with EXC0's S.  Measured: BL cleared with a request pending -> taken at the next leader,
//     k = 0, handler 6 after its would-issue cycle (ci2 irq_bl dt 14, irqloop 29.0 against ldcloop 9.0); IMASK lowered
//     -> taken at the first boundary >= s + 9 / 10 (v1_intc), ci2 k = 7 / 7 / 5 6 5 5 3 3 2 3 2 2.
//   * SLEEP: CO, 4 issue cycles (the sequencer's step counter holds D for 3 more cycles), then `sleep_issued`: fetch
//     and D stop; with EX / MA / S empty the core raises `sleep_req` (the FPU need not drain, C14) and v1_cpg drops
//     `run`, the clock enable of everything here; `asleep` ignores BL for the wake, which is an ordinary take at the
//     next boundary once `run` is back (SPC = SLEEP + 2 [HW 36 of 36]).  Any take ends the sleep state: an older
//     fault kills the SLEEP, the waking interrupt completes it, a break on the SLEEP itself must not hang (SOC.md 9.18).
//   * privilege: the D-stage priority mux (dcode): a privileged form (ictl.priv) with SR.MD = 0 is 0x180, 0x1A0 in a
//     delay slot, from the architectural SR with the write-through (LDC SR refetches; RTE's slot and target see the
//     restored MD).
module v1_core import tp_pkg::*; import v1_pkg::*; import v1_alu_pkg::*; import v1_soc_pkg::*; (
  input  logic        clk,
  input  rst_t        rst,
  input  logic        run,                     // cpu_run: the clock enable of the whole core (SOC.md 2.5)
  input  logic [31:0] reset_pc,                // RESET_PC (the core-only harness sets it)
  // v1's register backdoor (core-only harness)
  input  logic        dbg_we,
  input  logic [7:0]  dbg_addr,
  input  logic [31:0] dbg_wdata,
  input  logic [7:0]  dbg_raddr,
  output logic [31:0] dbg_rdata,
  // memory system (SOC.md 3.2)
  output ireq_t       ifq,
  input  irsp_t       ifr,
  output dex_t        dex,
  input  dxlat_t      dxl,
  output lreq_t       lreq,
  input  lrsp_t       lrsp,
  output logic        ldtlb,                   // LDTLB's first MA cycle: v1_mmu writes UTLB[URC] at its end
  output logic        ma_commit,               // an arch access acked in MA (URC)
  input  logic        at, sqmd,                // MMUCR, for the EX address checks
  // interrupts and exceptions (SOC.md 3.4)
  input  irq_t        irq,
  output logic [3:0]  sr_imask,
  output logic        sr_bl, asleep, irq_ack, nmi_ack,
  output excw_t       excw,
  output logic        sleep_req, cpu_rst_req, mhit_req,
  // UBC core-side ports (debug-ubc-hudi 8.2)
  output ubc_ifq_t    ubc_ifq,
  input  ubc_ift_t    ubc_ift,
  output ubc_opq_t    ubc_opq,
  input  ubc_opm_t    ubc_opm,
  output ubc_dq_t     ubc_dq,
  input  logic        ubc_d_mb,
  input  ubc_ctl_t    ubc_ctl,
  input  logic        ubc_seq,                 // BRCR.SEQ, v1_ubc.ctl_seq (SOC.md 3.8: ubc_ctl_t stays as it is, the two
                                               // extra v1_ubc outputs are wired by v1_soc).  With SEQ = 1 a channel-A
                                               // match only arms channel B
  input  logic        ubc_pre_armed,           // v1_ubc.ctl_pre_armed, passed to v1_fetch (SOC.md 3.8, 4.1 item 7): a
                                               // pre-execution break is armed; 0 while the UBC is idle
  output ubc_hit_t    ubc_hit,
  // performance-counter events (SOC.md 3.7) and the testbench tap
  output pmc_ev_t     pmc_ev,
  output core_tap_t   tap
);
  // ============================================================================================================
  // architectural state of the CPU side (the FPU side lives in v1_fpu)
  logic [31:0] gpr [24];                       // R0-R7 bank 0, R0-R7 bank 1, R8-R15
  logic [31:0] sr, gbr, vbr, ssr, spc, sgr, dbr, pr;

  wire rb = sr[30] & sr[29];                   // MD & RB: which bank R0-R7 are
  wire rst_c = rst.cpu;                        // every reset kind implies the CPU reset (v1_soc_pkg rst_t)
  logic rst_q;                                 // rst_c last cycle: the first cycle of a reset loads the reset values

  // ============================================================================================================
  // pipeline latches
  uop_t ex [2], ma [2], sl [2];
  uop_t ex_res [2];                             // the EX latch with operands and EX results filled in
  uop_t ex_fin [2];                             // ex_res with the EX-stage checks (faults, UBC operand tags) applied
  logic fpu_idle;
  logic ex_smd [2];                             // SR.MD each EX micro-op was decoded with (dex.md; RTE slot: the new MD)

  // ============================================================================================================
  // fetch (v1_fetch, SOC.md 1.3.2 / 4.1; WI-CORE-B): the request `ifq` is acked in the request cycle on an IC hit (v1's
  // timing); a miss leaves the queue unfilled and D starves (hm 8.3: nothing freezes).  Every queue entry carries its
  // fetch fault, the UBC's fetch-side A / B tags and the redirect-target and delay-slot tags; the entries behind the
  // leader are pre-decoded for the P claim.  The fetch is held by pfz / mah / run = 0, asleep, and from an entry's
  // take cycle to its redirect (SOC.md 4.1 item 8: `hold`, and `hold_ma` for the MA unit's same-cycle answer, which
  // the fetch sees one cycle late); the take flushes it (`flush`), the entry's vector redirect is
  // `exc_redirect` (it waits for an outstanding uncached word / ITLB refill: `f_busy_wait`, counted in F).
  logic        q_v [3];
  logic [31:0] q_pc [2];
  logic [15:0] q_op [2];
  logic        q_tgt [2];
  logic [4:0]  q_tag [3];                      // {ubc_a, ubc_b, slot, fx != NONE, vec400} of q0 - q2
  fx_e         q_fxe [2];                      // the fetch fault of q0 / q1
  logic        f_q1_mem, f_q2_mem;             // (v1_fetch's frozen pre-decode outputs: w_mem[1], w_mem[2])
  logic [3:0]  f_w_mem;                        // the claim window (the queue + the word read this cycle): memory insn
  logic [2:0]  f_w_br;                         // ... a branch (the claim's scan stops after it)
  logic [15:0] f_w_op2, f_w_op3;               // ... the opcodes of positions 2 / 3
  logic [1:0]  pop;
  logic        redirect, rd_keep, rd_slot_pend, refetch;
  logic [31:0] rd_target, rd_slot_pc, rf_pc;
  logic        f_flush, f_exc_rd, f_skip, f_md, f_busy_wait, f_ev, f_ev_all;
  logic [31:0] f_fpc;                          // the next instruction when the window is empty (interrupt SPC)
  logic        sleep_hit;
  logic        sleep_issued;                   // SLEEP issued: fetch and D stop; asleep (SOC.md 4.4.3)
  logic [31:0] slp_cont;                       // the instruction after SLEEP in program order: the waking SPC
  logic [31:0] sr_rd;                          // SR as D reads it (S write-through), below
  logic        fetch_stop;                     // an entry: no fetch from the cycle after t until its redirect
  logic [31:0] exc_vector;                     // the entry's vector (below)

  v1_fetch u_fetch (
    .clk, .rst(rst_c), .reset_pc, .md(f_md),
    // the holds known at the start of the cycle (asleep, the entry's fetch stop, run = 0, LDTLB's freeze in MA) stop
    // the request; the MA unit's answer (mah, the miss path's frz, the OCBWB collision) is seen from its second cycle
    .hold(sleep_hit || !run || fetch_stop || (lt_frz && !kill_ma)), .hold_ma(mah || oc_coll || (lreq.v && lrsp.frz)),
    .ifq, .ifr, .ubc_q(ubc_ifq), .ubc_t(ubc_ift),
    .q0_v(q_v[0]), .q1_v(q_v[1]), .q2_v(q_v[2]), .q0_pc(q_pc[0]), .q1_pc(q_pc[1]), .q0_op(q_op[0]), .q1_op(q_op[1]),
    .q0_tgt(q_tgt[0]), .q1_tgt(q_tgt[1]), .q0_tag(q_tag[0]), .q1_tag(q_tag[1]), .q0_fx(q_fxe[0]), .q1_fx(q_fxe[1]),
    .q1_mem(f_q1_mem), .q2_mem(f_q2_mem), .w_mem(f_w_mem), .w_br(f_w_br), .w_op2(f_w_op2), .w_op3(f_w_op3),
    .q2_tag(q_tag[2]),
    .pop, .redirect, .keep(rd_keep), .slot_pend(rd_slot_pend), .target(rd_target), .slot_pc(rd_slot_pc),
    .refetch, .rf_pc, .exc_redirect(f_exc_rd), .exc_target(exc_vector), .flush(f_flush), .skip(f_skip),
    .ubc_pre_armed, .fpc(f_fpc), .busy_wait(f_busy_wait), .ev_fetch(f_ev), .ev_fetch_all(f_ev_all)
  );

  // ============================================================================================================
  // FPU
  logic [2:0]  md_dec;                         // FPSCR {FR, SZ, PR} as the decoder sees it
  logic [31:0] fr_now [32];                    // FP register file, write-through
  logic [31:0] fpul_now, fpscr_now, mach_now, macl_now;
  logic        fhz [2];                        // FPU resource hazard of each D slot
  logic        fhzc [2];                       // (= fhz: the name the issue logic reads)
  logic [7:0]  fwhy [2];
  logic        fdep;                           // the follower depends on the leader through an FPU resource
  logic        t_busy, flag_busy;              // FCMP's T not yet delivered / its flags not yet at S
  logic        fpu_t_we, fpu_t_val, fpu_t_lane;
  logic        trap_f1, trap_f1_lane;          // v1_fpu's F1 operand trap: the instruction in EX lane trap_f1_lane
  logic [7:0]  trap_f1_isn;
  logic [7:0]  di_isn [2];
  logic [31:0] fpu_dbg_rdata;
  logic        fpu_dbg_hit;
  // micro-ops issued this cycle (to the FPU's dispatch and hazard state)
  logic        di_v [2];
  logic [15:0] di_op [2];
  dec_t        di_d [2];
  ictl_t       di_c [2];
  logic [2:0]  di_md [2];
  logic        di_fcmpd;                       // FCMP DR's compare pass (micro-op 1)
  logic [15:0] di_fcmpd_op;
  logic [2:0]  di_fcmpd_md;
  logic        frz;                            // everything holds this cycle: pfz | mah | !run (v1_fpu's hold_pipe;
                                               // its hold_div, the divider recurrence, is pfz | !run: SOC.md 4.5)
  // The two holds of SOC.md 3.2.2 (C7).  pfz: whole-pipeline freeze (fetch, D, sequencer, EX / MA / S, FPU):
  //   lt_frz   LDTLB's UTLB write: LDTLB occupies MA for 3 cycles and the first two freeze (measured 2, the tag-write
  //            freeze of v1, now MA-relative so that it serialises with the operand cache's: OCBI ; LDTLB = 2 + 2)
  //   oc_coll  OCBWB keeps the cache port one more cycle: in its ack cycle an access in EX holds for one cycle
  //            (measured 1: OCBWB ; load / store / PREF / cache op / LDTLB at distance 0)
  //   lrsp.frz the operand cache's 2-cycle miss path (measured 2: OCBI / OCBP / OCBWB / PREF / store / MOVCA.L of an
  //            absent line)
  // mah: the MA access is presented and neither answered nor in the miss path (every longer wait).
  // A UBC data break at S kills the group in MA (kill_ma): its LDTLB freeze does not start (`lt_frz` is masked).
  logic        lt_frz, oc_coll, mah;
  logic [1:0]  lt_n;                           // cycles LDTLB has spent in MA (the UTLB write's occupancy counter)
  logic        kill_ma;
  wire         pfz = (lt_frz && !kill_ma) || oc_coll || (lreq.v && lrsp.frz);

  // ============================================================================================================
  // D stage: the two decode slots
  dec_t   d [2];
  ictl_t  c [2];
  logic   s_v [2];
  tp_decode u_dec0 (.op(q_op[0]), .md(md_dec), .d(d[0]));
  tp_decode u_dec1 (.op(q_op[1]), .md(md_dec), .d(d[1]));
  assign c[0] = v1_ictl_op(d[0].form, q_op[0]);
  assign c[1] = v1_ictl_op(d[1].form, q_op[1]);
  // the claim window (SOC.md 4.3.4 item 2): v1_fetch's pre-decode of the queue from q0 on, continued by the word it
  // reads in this cycle -- what "already fetched at t" means [HW c1i, c1j, c1b j0: see v1_fetch]; up to 3 behind the
  // excepting instruction [HW c1i: a load 2 and 3 places behind claims].  A poisoned entry has no opcode (v1_fetch
  // gives it op = 0, a NOP to the decoder): no claim, no branch, no tags beyond the UBC's (a break outranks the fault).
  // A follower-candidate take's claimants (positions 2, 3) do not claim when they read a register the leader issuing
  // in the take cycle writes (`q_dep`) [P, the one console case that tells: c1b d0 (A = a PMCTR read at a BRA target,
  // X = H'FFFD its follower, then a store of A's result, fetched in the take cycle: + 0) against j0 (the same at a JSR
  // target with a PC-relative load, independent: + 2); a store of a result loaded one cycle before t claims (c1i st,
  // c1b d2)].
  // The fetch poison of SOC.md 4.1 item 2 rides the entry (q_fxe): an odd PC or MD = 0 at or above 0x80000000 (v1_fetch,
  // with the MD the entry was fetched with: SOC.md 4.1 item 9, the RTE slot's old MD included), and v1_mmu's region /
  // ITLB faults; it is taken at D (SOC.md 4.3.2 priority: after the pre-execution break) with its own code / vector.
  logic   q2_mem, q3_mem;
  dec_t   dw [2];                               // positions 2 / 3 of the claim window, decoded for q_dep
  logic   q_dep [2];                            // ... reads a register the leader issuing this cycle writes
  tp_decode u_decw2 (.op(f_w_op2), .md(md_dec), .d(dw[0]));
  tp_decode u_decw3 (.op(f_w_op3), .md(md_dec), .d(dw[1]));
  logic   q_br [3];                             // q0 - q2 is a branch: the entries after it (after its slot) are not
                                                // the program order [P]
  logic   q_fx [2];                             // q0 / q1 is poisoned
  logic   qm [2];                               // q0 / q1 is a memory instruction (the claim)
  assign q_fx[0] = q_v[0] && q_fxe[0] != FX_NONE;
  assign q_fx[1] = q_v[1] && q_fxe[1] != FX_NONE;
  assign qm[0] = f_w_mem[0];
  assign qm[1] = f_w_mem[1];
  assign q2_mem = f_w_mem[2];
  assign q3_mem = f_w_mem[3];
  assign q_br[0] = f_w_br[0];
  assign q_br[1] = f_w_br[1];
  assign q_br[2] = f_w_br[2];

  // the micro-op sequencer of multi-cycle instructions
  logic        sq_on;
  logic [2:0]  sq_step, sq_k;
  logic [31:0] sq_pc;
  logic [15:0] sq_op;
  form_e       sq_form;
  ictl_t       sq_c;
  dec_t        sq_d;
  logic [2:0]  sq_md;
  logic [31:0] sq_target;                     // RTE: SPC; JMP-class: taken from EX of micro-op 0
  logic [31:0] sq_addr;                        // @(R0,GBR) / TAS.B / STC.L address from micro-op 0
  logic [31:0] sq_data;                        // STC.L / STS.L PR: the register value read in D of micro-op 0
  logic [31:0] rmw_tmp;                        // byte loaded by the read of TST.B / AND.B / OR.B / XOR.B / TAS.B
  logic        sq_last;
  logic [7:0]  sq_isn;                         // the sequenced instruction's ISN
  logic        sq_smd;                         // and its SR.MD
  logic        sq_sbl;                         // and its SR.BL before (uop_t.blb of its later micro-ops)
  logic        sq_slot;                        // it is a delay slot
  logic [31:0] sq_cont;                        // the instruction after it in program order (a break's SPC)
  logic [1:0]  sq_post;                        // its UBC post-execution tag {A, B}
  assign sq_last = sq_on && sq_step == sq_k - 3'd1;
  // the fetch's MD (SOC.md 4.1 item 9): the architectural SR.MD with the write-through, except during RTE's sequence,
  // whose slot is fetched with the old MD (pm 10.78) also when it is fetched after RTE's SR write (a slow fetch, or its
  // slot_pend redirect) -- every other word fetched then is run-ahead that RTE's redirect drops
  assign f_md = (sq_on && sq_c.seq == SQ_RTE) ? sq_smd : sr_rd[30];
  // the fetch has a request outstanding since last cycle (presented and not acked): the window may starve
  logic f_starve_q;
  always_ff @(posedge clk) f_starve_q <= !rst_c && ifq.v && !ifr.ack;
  assign mah = lreq.v && !lrsp.ack && !lrsp.frz;
  assign frz = pfz || mah || !run;

  // ---- issue sequence number (SOC.md 4.3.1): bookkeeping only, no timing decision reads it.  A pair takes n, n + 1.
  logic [7:0]  isn_ctr;
  // "younger than or equal to K" = (isn - K) mod 256 < 128 (core-integration 3.4)
  function automatic logic yng(input logic [7:0] isn, input logic [7:0] k, input logic incl);
    logic [7:0] dd;
    dd = isn - k;
    return !dd[7] && (incl || dd != 8'd0);
  endfunction

  // ---- the exception entry (SOC.md 4.3.4): state flops (the always_ff below writes them)
  logic        ent_act;                        // an entry from its take cycle to its redirect: D and fetch stopped
  logic        ent_pend;                       // EXC0 not issued yet
  logic [3:0]  ent_rc;                         // the operand-address claim's release: steps left (2 per claimant)
  logic        exc_rd;                         // EXC0 has passed S: redirect when fetch is free
  logic        exc_sv;                         // the redirect happened: EXC_SAVE issues this cycle
  logic        rst_wait;                       // a BL = 1 exception or a multiple hit requested a reset: stop
  logic        exc0_s;                         // EXC0 leaves S this cycle (the entry's architectural writes)
  ubc_dq_t     s_dq;                           // the S-stage access's data for the UBC's data compare
  // the entry record, latched at t
  logic [11:0] ent_code;
  logic [11:0] ent_vec;
  logic        ent_ubrk, ent_trapa, ent_tea_v, ent_tra_v;
  logic        ent_intr, ent_nmi;              // an interrupt entry (INTEVT, irq_ack / nmi_ack at EXC0's S); the NMI
  logic [31:0] ent_spc, ent_tea;
  logic [7:0]  ent_tra, ent_isn;
  logic [1:0]  ent_rst;                        // instead of an entry: 1 the CPU-only reset (SR.BL = 1), 2 a multiple hit
  logic [1:0]  ent_hit;                        // UBC tags of the take: CMF when the entry commits (EXC0's S)
  // SSR / SPC / SGR as EXC0 saw them at S, written by EXC_SAVE at its S (the handler's first instruction may already
  // start a new entry while EXC_SAVE is in flight, so the save does not read the entry record)
  logic [31:0] ssr_hold, sgr_hold, spc_hold;
  logic [7:0]  sv_isn;                         // the entry's ISN, for EXC_SAVE's commit-log records (SOC.md 6.3)

  // ---- register reads in D (write-through from this cycle's S writes)
  logic [31:0] gpr_rd [24];
  logic [31:0] gbr_rd, vbr_rd, ssr_rd, spc_rd, sgr_rd, dbr_rd, pr_rd;
  logic [31:0] sr_pre;                         // SR with the S writes of everything but EXC0 (what SSR saves)
  always_comb begin
    for (int i = 0; i < 24; i++) gpr_rd[i] = gpr[i];
    sr_rd = sr; gbr_rd = gbr; vbr_rd = vbr; ssr_rd = ssr; spc_rd = spc; sgr_rd = sgr; dbr_rd = dbr; pr_rd = pr;
    for (int l = 0; l < 2; l++) begin
      if (sl[l].v && sl[l].uk == UK_NORM) begin
        if (sl[l].w1) gpr_rd[sl[l].w1r] = sl[l].w1v;
        if (sl[l].w2) gpr_rd[sl[l].w2r] = sl[l].w2v;
        unique case (sl[l].crw)
          CR_SR:   sr_rd = sl[l].crv;
          CR_GBR:  gbr_rd = sl[l].crv;
          CR_VBR:  vbr_rd = sl[l].crv;
          CR_SSR:  ssr_rd = sl[l].crv;
          CR_SPC:  spc_rd = sl[l].crv;
          CR_DBR:  dbr_rd = sl[l].crv;
          CR_PR:   pr_rd = sl[l].crv;
          CR_BANK: gpr_rd[sl[l].bkr] = sl[l].crv;
          default: ;
        endcase
      end
      if (sl[l].v && sl[l].uk == UK_EXC_SAVE) begin ssr_rd = ssr_hold; spc_rd = spc_hold; sgr_rd = sgr_hold; end
    end
    sr_pre = sr_rd;
    for (int l = 0; l < 2; l++) if (sl[l].v && sl[l].uk == UK_EXC0 && ent_rst == 2'd0) sr_rd = sr_rd | 32'h7000_0000;
  end

  // ---- T / Q / M / S as D sees them: the flag register, or the result of a CPU flag writer in EX this cycle
  logic t_now, q_now, m_now, s_now;
  always_comb begin
    t_now = sr[0]; s_now = sr[1]; q_now = sr[8]; m_now = sr[9];
    for (int l = 0; l < 2; l++) begin
      if (ex[l].v && ex_res[l].tw) t_now = ex_res[l].tv;
      if (ex[l].v && ex_res[l].qmw) begin q_now = ex_res[l].qv; m_now = ex_res[l].mv; end
      if (ex[l].v && ex_res[l].sw) s_now = ex_res[l].sv;
    end
  end

  // ---- CPU hazards of each slot (comparators against the EX / MA destination tags)
  logic [23:0] src_g [2], dst_g [2], late_g;
  logic        sx_busy, fl_busy, gld_ma;
  logic [4:0]  cwhy [2];
  always_comb begin
    // GPRs whose value is not on a bypass path yet: load data while the load is in EX, FPU / MAC transfers while the
    // transfer is in EX or MA
    late_g = '0;
    sx_busy = 1'b0; fl_busy = flag_busy; gld_ma = 1'b0;
    for (int l = 0; l < 2; l++) begin
      if (ex[l].v) begin
        if (ex[l].w2) late_g[ex[l].w2r] = 1'b1;
        if (ex[l].w1 && ex[l].w1k == GR_S) late_g[ex[l].w1r] = 1'b1;
        if (ex[l].sxw) sx_busy = 1'b1;
        if (ex[l].flagop) fl_busy = 1'b1;
      end
      if (ma[l].v) begin
        if (ma[l].w1 && ma[l].w1k == GR_S) late_g[ma[l].w1r] = 1'b1;
        if (ma[l].sxw) sx_busy = 1'b1;
        if (ma[l].flagop) fl_busy = 1'b1;
        if (ma[l].gld) gld_ma = 1'b1;
      end
    end
    for (int s = 0; s < 2; s++) begin
      src_g[s] = gmask(d[s].ga | d[s].gd | d[s].gs, rb);
      dst_g[s] = gmask(d[s].gwl | d[s].gwu | (c[s].mov ? 16'd0 : d[s].gw), rb);
      cwhy[s][0] = |(src_g[s] & late_g);
      cwhy[s][1] = c[s].rd_t && t_busy;
      cwhy[s][2] = s == 0 && c[s].sx_r && sx_busy;
      cwhy[s][3] = c[s].stc_sr && fl_busy;
      cwhy[s][4] = c[s].shad && gld_ma;
    end
  end

  // the claim window's positions 2 / 3 against the leader issuing this cycle (a follower-candidate take's dependence
  // exception, above)
  assign q_dep[0] = |(gmask(dw[0].ga | dw[0].gd | dw[0].gs, rb) & dst_g[0]);
  assign q_dep[1] = |(gmask(dw[1].ga | dw[1].gd | dw[1].gs, rb) & dst_g[0]);

  // ---- the follower against the leader in the same cycle
  logic pair_ok, dep1, tk0;
  always_comb begin
    logic mt0, mt1, br0, br1;
    mt0 = c[0].cls == K_MT || c[0].cls == K_BR;
    mt1 = c[1].cls == K_MT || c[1].cls == K_BR;
    br0 = c[0].br_pc || c[0].br_reg;
    br1 = c[1].br_pc || c[1].br_reg;
    tk0 = c[0].br_pc && (!c[0].br_cond || t_now == c[0].br_t);
    pair_ok = c[0].k == 3'd1 && c[1].k == 3'd1 && c[0].cls != K_CO && c[1].cls != K_CO &&
              (mt0 || mt1 || c[0].cls != c[1].cls) && !(br0 && br1) && !q_tgt[1] &&
              !(tk0 && !c[0].br_dly);                               // the fall-through of a taken BT / BF
    dep1 = |(dst_g[0] & src_g[1]) ||                                // no same-cycle GPR bypass (MOV aside)
           (c[0].wr_t && c[1].rd_t) ||                              // T from EX, read in D
           fdep;
  end

  // ---- branches in D: PC-relative targets from the D-stage adder; register targets from EX of micro-op 0
  logic [31:0] br_tgt [2];
  logic        br_taken [2];
  logic        br_skip [2];                    // a taken BT / BF with disp 0: the skip form (SOC.md 4.1 item 6)
  logic [31:0] jmp_tgt_ex;                     // target computed by micro-op 0 of a JMP-class branch now in EX
  always_comb begin
    for (int s = 0; s < 2; s++) begin
      logic [31:0] disp;
      disp = (q_op[s][15:12] == 4'hA || q_op[s][15:12] == 4'hB) ? {{19{q_op[s][11]}}, q_op[s][11:0], 1'b0}
                                                                 : {{23{q_op[s][7]}}, q_op[s][7:0], 1'b0};
      br_tgt[s] = q_pc[s] + 32'd4 + disp;
      br_taken[s] = c[s].br_pc && (!c[s].br_cond || t_now == c[s].br_t);
      br_skip[s] = br_taken[s] && c[s].br_cond && !c[s].br_dly && q_op[s][7:0] == 8'd0;
    end
  end

  // ============================================================================================================
  // D-stage exception checks (SOC.md 4.3.2, 4.4.2; exceptions-intc 3.5-3.8; debug-ubc-hudi 4.3).  Delay slots: the
  // leader is a slot when the last instruction issued was a delayed branch whose slot did not pair (`slot0_q`, v1's
  // keep / slot_pend paths); the follower candidate is a slot when the leader is a one-cycle delayed branch (BRA BSR
  // BT/S BF/S; the register-target branches issue their slot after their sequence).
  logic        slot0_q;                        // the next leader is a delay slot
  logic [31:0] dly_cont;                       // the continuation after that slot: the target, or slot + 2 (BT/S / BF/S
                                               // not taken)
  logic [1:0]  post_carry;                     // a delayed branch's UBC post tag, for its slot's micro-op
  logic        in_slot [2];
  logic [1:0]  tg_pre [3], tg_post [2];        // the entry's fetch-side UBC tags {A, B}, by PCBx (pre / post)
  logic        pre_brk [3];                    // a pre-execution break of this entry is requested (SR.BL before: 0)
  logic        dfault [2];
  logic [11:0] dcode [2];
  logic        d_ubrk [2];                     // the D fault is a user break
  logic        d_ifx [2];                      // the D fault is a fetch fault (TEA = the instruction's address)
  logic [1:0]  d_hit [2];                      // UBC tags reaching their break point with that fault
  logic        trap_d [2];                     // D-decided FPU trap (v1_fpu: enables x opcode, FCMP DR's V)
  logic [31:0] cont_d [2];                     // the instruction after this one in program order
  always_comb begin
    in_slot[0] = slot0_q;
    in_slot[1] = c[0].br_dly && c[0].k == 3'd1;
    for (int s = 0; s < 3; s++) begin
      tg_pre[s]  = {q_tag[s][4] && !ubc_ctl.pcba, q_tag[s][3] && !ubc_ctl.pcbb} & {2{q_v[s]}};
      // SEQ = 1: a channel-A match arms channel B instead of breaking; channel B breaks when live (debug-ubc-hudi 4.3)
      pre_brk[s] = ((tg_pre[s][1] && !ubc_seq) || (tg_pre[s][0] && ubc_ctl.b_live)) && !sr_rd[28];
    end
    for (int s = 0; s < 2; s++) begin
      tg_post[s] = {q_tag[s][4] &&  ubc_ctl.pcba, q_tag[s][3] &&  ubc_ctl.pcbb} & {2{q_v[s]}};
      cont_d[s] = q_pc[s] + 32'd2;
      if (c[s].br_pc && !c[s].br_dly && br_taken[s]) cont_d[s] = br_tgt[s];
    end
    if (in_slot[0]) cont_d[0] = dly_cont;
    if (in_slot[1]) cont_d[1] = br_taken[0] ? br_tgt[0] : q_pc[0] + 32'd4;
    // the priority mux of one instruction (SOC.md 4.3.2 D-stage priority; hm Table 5.2; the order of tb/sh4_iss.h
    // check()): a fetch multiple hit (the 0x140 reset, priority level 1 [M]), the pre-execution break (level 2), the
    // other fetch faults (a poisoned entry: q_fx, v1_fetch's / v1_mmu's code), FPU disable, slot illegal, illegal,
    // privileged in user mode, then a pre-execution break on the slot of this delayed branch (taken at the branch, which
    // does not execute [HW slot_ds_pre])
    for (int s = 0; s < 2; s++) begin
      dfault[s] = 1'b1; d_ubrk[s] = 1'b0; d_hit[s] = 2'b00; d_ifx[s] = 1'b0;
      if (q_fx[s] && q_fxe[s] == FX_MHIT) begin dcode[s] = fx_expevt(FX_MHIT, 1'b0, 1'b1); d_ifx[s] = 1'b1; end
      else if (pre_brk[s]) begin dcode[s] = EXP_UBRK; d_ubrk[s] = 1'b1; d_hit[s] = tg_pre[s]; end
      else if (q_fx[s]) begin dcode[s] = fx_expevt(q_fxe[s], 1'b0, 1'b1); d_ifx[s] = 1'b1; end
      else if (sr_rd[15] && c[s].fpu_insn) dcode[s] = in_slot[s] ? EXP_SLOT_FPUDIS : EXP_FPU_DIS;
      else if (in_slot[s] && c[s].slot_ill) dcode[s] = EXP_SLOT_ILL;
      else if (c[s].illegal) dcode[s] = EXP_ILLEGAL;
      else if (!sr_rd[30] && c[s].priv) dcode[s] = in_slot[s] ? EXP_SLOT_ILL : EXP_ILLEGAL;
      else if (c[s].br_dly && (s == 0 ? q_v[1] && pre_brk[1] : q_v[2] && pre_brk[2])) begin
        // (the slot not in the queue yet: v1_fetch's ubc_pre_armed hold keeps the branch until it is, SOC.md 4.1)
        dcode[s] = EXP_UBRK; d_ubrk[s] = 1'b1; d_hit[s] = s == 0 ? tg_pre[1] : tg_pre[2];
      end else begin dfault[s] = 1'b0; dcode[s] = 12'h000; end
    end
  end

  // ============================================================================================================
  // the take (SOC.md 4.3.2).  Program order: the S-stage data break, then MA lane 0, MA lane 1, then D lane 0, lane 1.
  // A take in S or MA kills (`pk_*`); a D-stage take kills nothing in the latches (its instruction never issued).
  logic        stake_c;                        // a UBC data break at S (combinational; withdraws the MA request)
  logic        sbrk_c;                         // a channel-B data match at S that breaks (SR.BL = 0, channel B live)
  logic        sdef_c;                         // ... whose group ends in a one-cycle delayed branch: the slot's break
  logic        dbrk_pend;                      // a deferred data break waits for the slot to reach MA
  logic [1:0]  dbrk_hitq;                      // its match flags (CMFB), set when it commits
  logic        dbrk_on;                        // the deferred data break is armed this cycle
  logic [1:0]  dbrk_hit;
  logic        stake, matake;                  // pipeline takes committing this cycle
  logic [1:0]  s_brk_hit;                      // data-break match at S (CMFB), taken or not
  logic        s_dq_l;                         // the lane of the S-stage access (s_dq)
  logic        ma_tk [2];                      // this MA lane takes
  logic        ma_tkb [2];                     // ... but for a break decided by the SR word the micro-op loads (below)
  logic        ma_ldb [2];                     // this lane's UBC break waits for the SR word it loads (LDC.L @Rm+,SR)
  logic [1:0]  ma_hit [2];                     // this MA lane reaches a post / operand break point (CMF)
  logic        pk_v, pk_incl;                  // the pipeline kill of this cycle: ISN >= / > pk_isn
  logic        ftake;                          // the F1 operand trap is taken this cycle (below)
  logic [7:0]  pk_isn;
  logic        kex [2], kma [2], ksl [2];      // this latch is killed this cycle
  logic        ok_v, ok_incl;                  // ... the S / MA takes' part of it (the FPU's EX / MA view, below)
  logic [7:0]  ok_isn;
  logic        kexo [2], kmao [2];
  logic        sq_kill;                        // the sequencer's instruction is killed
  logic        s_yl;                           // the youngest instruction of the S group (not an EXC0 beside it)
  logic        s_ybr;                          // ... is a one-cycle delayed branch (its slot is not in the group)
  assign s_yl = sl[1].v && sl[1].uk == UK_NORM;
  assign s_ybr = sl[s_yl].uk == UK_NORM && v1_ictl(sl[s_yl].form).br_dly && v1_ictl(sl[s_yl].form).k == 3'd1;
  always_comb begin
    // ---- S: channel B with DBEB compares the access's data at S (debug-ubc-hudi 4.3: "the access's whole issue group
    // completes; every instruction issued in a later cycle is cancelled"), BL after the instruction.  A block of its
    // own: kill_ma (from stake_c) gates the MA request, which the MA commit point below reads through frz.
    // When the group ends in a one-cycle delayed branch (BRA BSR BT/S BF/S paired behind the access), the branch's
    // slot completes too -- a branch and its slot are one unit -- and the break is taken at the slot's commit point
    // (its MA cycle, completion type, like a post-execution break on the branch): SPC = the branch's continuation (the
    // target, or slot + 2 not taken).  [HW tools/hw/core-exc c1k, 2026-09-23: a load paired with BRA / BSR / taken
    // BT/S / not-taken BF/S -> SPC = target / target / target / slot + 2, the slot's ADD done at the break, 4 of 4
    // each; the load paired with an ADD, BRA next: SPC = the BRA.  WI-CORE-A F3]
    s_brk_hit = 2'b00;
    sbrk_c = 1'b0;
    if (s_dq.v && sl[s_dq_l].v && sl[s_dq_l].ubc_op[0] && ubc_ctl.dbeb && ubc_d_mb) begin
      s_brk_hit = 2'b01;
      sbrk_c = ubc_ctl.b_live && !sr_pre[28];             // (an EXC0 beside it in S is younger: its BL write too)
    end
    // (an entry already started behind the group -- the slot's own D-stage fault, taken before the break was known --
    // is younger: the break takes at S and kills it, the slot suppressed with its branch: SPC = the branch [P])
    stake_c = sbrk_c && (!s_ybr || ent_act);
    sdef_c = sbrk_c && s_ybr && !ent_act;
    stake = stake_c && run;                    // no hold remains: the kill withdraws the MA request (below)
    dbrk_on = sdef_c || dbrk_pend;             // the slot takes it at its MA cycle (this one, or a later one)
    dbrk_hit = sdef_c ? s_brk_hit : dbrk_hitq;
  end
  always_comb begin
    // ---- MA: the commit point
    for (int l = 0; l < 2; l++) begin
      logic post_b, op_b, ldsr, bla;
      ma_tkb[l] = 1'b0; ma_ldb[l] = 1'b0; ma_hit[l] = 2'b00;
      post_b = (ma[l].ubc_post[1] && !ubc_seq) || (ma[l].ubc_post[0] && ubc_ctl.b_live);
      op_b   = (ma[l].ubc_op[1] && !ubc_seq) || (ma[l].ubc_op[0] && !ubc_ctl.dbeb && ubc_ctl.b_live);
      // SR.BL after the instruction: the SR this cycle sees (the S write-through of every older micro-op; for the last
      // micro-op of LDC Rm,SR / LDC.L @Rm+,SR -- the bubble that carries their post tag, 3 cycles behind micro-op 0 --
      // that includes their own SR write), or, on the micro-op that writes SR at its own S, the value it writes: the
      // word LDC.L @Rm+,SR loads (`ldsr`: its operand break is decided with the MA data, ma_ldb below)
      ldsr = ma[l].crw == CR_SR && ma[l].mrd;
      bla  = ma[l].crw == CR_SR ? ma[l].crv[28] : sr_rd[28];
      if (ma[l].v && ma[l].uk == UK_NORM) begin
        if (ma[l].fx != FX_NONE) ma_tkb[l] = 1'b1;
        else begin
          ma_hit[l] = ma[l].ubc_post | {ma[l].ubc_op[1], ma[l].ubc_op[0] && !ubc_ctl.dbeb};
          // TRAPA; a UBC post-execution break, masked by SR.BL before (ma.blb) or after, and an operand-address break,
          // masked by BL after [M hm 20.3.9 (3) b-d: LDC Rm,SR / LDC.L @Rm+,SR BL 0 -> 1: operand M, post M; 1 -> 0:
          // operand A, post M; M1b-verify F1]; the deferred data break at the slot (its BL test was the access's, at S)
          ma_tkb[l] = ma[l].trap || (!ldsr && ((post_b && !ma[l].blb && !bla) || (op_b && !bla))) ||
                      (dbrk_on && ma[l].slot);
          ma_ldb[l] = ldsr && ((post_b && !ma[l].blb) || op_b);
        end
      end
    end
  end
  always_comb begin                           // (a block of its own: lane 0's take gates lane 1's MA request)
    matake = !frz && !stake_c && (ma_tk[0] || ma_tk[1]);
  end
  always_comb begin                           // the S / MA takes' kill (a block of its own: without the F1 trap)
    ok_v = stake || matake; ok_incl = 1'b1; ok_isn = '0;
    if (stake) ok_isn = sl[s_yl].isn + 8'd1;              // the access's group completes
    else if (ma_tk[0]) begin ok_isn = ma[0].isn; ok_incl = ma[0].fx != FX_NONE; end
    else if (ma_tk[1]) begin ok_isn = ma[1].isn; ok_incl = ma[1].fx != FX_NONE; end
    for (int l = 0; l < 2; l++) begin
      kexo[l] = ok_v && ex[l].v && yng(ex[l].isn, ok_isn, ok_incl);
      kmao[l] = ok_v && ma[l].v && yng(ma[l].isn, ok_isn, ok_incl);
    end
  end
  always_comb begin
    // ---- the kill
    pk_v = ok_v || ftake; pk_isn = ok_isn; pk_incl = ok_incl;
    if (!stake && !ma_tk[0] && !ma_tk[1]) begin pk_isn = trap_f1_isn; pk_incl = 1'b0; end  // the F1 trap: its own
                                                                                           // micro-op is the EXC0
    for (int l = 0; l < 2; l++) begin
      kex[l] = pk_v && ex[l].v && yng(ex[l].isn, pk_isn, pk_incl);
      kma[l] = pk_v && ma[l].v && yng(ma[l].isn, pk_isn, pk_incl);
      ksl[l] = pk_v && sl[l].v && yng(sl[l].isn, pk_isn, pk_incl);
    end
    sq_kill = pk_v && sq_on && yng(sq_isn, pk_isn, pk_incl);
  end
  assign kill_ma = rst_c || stake_c;           // SOC.md 3.2.2 rule 4: reset, and the channel-B data break at S

  // ---- issue
  wire busy = sq_on || frz;
  wire ent_blk = ent_act || exc_sv || rst_wait;
  wire lead = s_v[0] && !busy && !sleep_hit && !ent_blk;
  wire cpu0 = |cwhy[0];
  wire iss0_raw = lead && !cpu0 && !fhzc[0];
  wire iss1_raw = iss0_raw && s_v[1] && pair_ok && !dep1 && !(|cwhy[1]) && !fhzc[1];
  assign s_v[0] = q_v[0];
  assign s_v[1] = q_v[1];
  // the leader's issue-cycle count: the sequencer's step counter holds D for k cycles.  SLEEP is CO with k = 4 in
  // v1_pkg's v1_ictl_op (SOC.md 4.4.3, 1.3.1; hm Table 8.3; exceptions-intc 3.15), like every multi-cycle CO form
  wire [2:0] k0 = c[0].k;
  // ---- interrupt acceptance (SOC.md 4.4.1, C14): the boundary is a D cycle whose leader could be interrupted -- valid
  // (asleep: the instruction after SLEEP, fetched or not), not a delay slot (never between a delayed branch, RTE
  // included, and its slot [HW excint C1h / PB / PS: 72 of 72]), no sequence running (a multi-micro-op instruction is
  // never split), no hold (pfz, mah, run = 0), no entry in progress.  irq.req & irq.nmi is acceptable as the INTC
  // presents it (BL / NMIB applied there); an ordinary request needs SR.BL = 0 (with the S write-through: the leader
  // after an LDC SR / RTE that clears BL is taken at once [HW: k = 0, 24 of 24 + 8 of 8]), or asleep (BL ignored [M
  // 9.3.2]).  Program order: an older MA / S take wins (pk_v); the interrupt wins over the leader's own D-stage fault
  // or D-decided FPU trap [HW excint2 PF: SPC = the H'FFFD, which traps after the RTE].
  // With the window empty while the fetch waits -- an I-miss not yet answered for a cycle, or a redirect waiting for an
  // outstanding uncached word / ITLB refill -- the next instruction (v1_fetch's fpc) is a boundary too: "the interrupt
  // is taken while D starves on an I-miss, with SPC = the missing instruction" [HW core-integration 3.7, cold code]
  // (the stall is the registered `f_starve_q`, so the take never feeds back into this cycle's request)
  wire f_starve = !q_v[0] && (f_busy_wait || f_starve_q);
  wire ibound = (q_v[0] || sleep_issued || f_starve) && !slot0_q && !sq_on && !frz && !ent_blk;
  wire itake  = !pk_v && ibound && irq.req && (irq.nmi || !sr_rd[28] || sleep_issued);
  wire dstop  = pk_v || itake;                 // no D-stage take and no issue this cycle
  // D-stage takes (SOC.md 4.3.2): the leader in the first cycle it would issue (no data comparators [P]); the
  // follower candidate in the cycle its leader issues (a one-cycle leader that is not a taken BT / BF, and q1 not a
  // branch target: the instruction that would issue next, whatever its pairing class -- `d0`); a D-decided FPU trap
  // in its issue cycle, through the issue gate.
  wire fc1    = s_v[1] && k0 == 3'd1 && !q_tgt[1] && !(tk0 && !c[0].br_dly);
  // An H'Fxxx code is decoded in the FPU's mode (FR / SZ / PR): while an LDS FPSCR is on its way to S that mode is not
  // known (v1_fpu's fwhy[2], the decoder lock that holds every F-group opcode), so an H'Fxxx the stale mode leaves
  // undecoded is not taken as illegal / slot illegal until the new mode is in (it may decode then).  H'FFFD is illegal
  // in every mode.  Likewise the follower candidate of a leader that changes the mode (LDS FPSCR, FRCHG, FSCHG: the
  // pair v1_fpu refuses, fdep).  [P, WI-CORE-F: program order, tb/sh4_iss.h; the D take applies no comparators,
  // SOC.md 4.3.2]
  logic dmw [2];
  always_comb for (int s = 0; s < 2; s++)
    dmw[s] = (dcode[s] == EXP_ILLEGAL || dcode[s] == EXP_SLOT_ILL) && c[s].illegal && q_op[s][15:12] == 4'hF &&
             q_op[s] != 16'hFFFD && (fwhy[s][2] || (s == 1 && c[0].fscr_mode));
  wire dtake0 = !dstop && lead && dfault[0] && !dmw[0];
  wire ftrap0 = !dstop && iss0_raw && !dfault[0] && trap_d[0];
  wire dtake1 = !dstop && iss0_raw && !dfault[0] && !trap_d[0] && fc1 && dfault[1] && !dmw[1];
  wire ftrap1 = !dstop && iss1_raw && !dfault[0] && !trap_d[0] && !dfault[1] && trap_d[1];
  wire dtake  = dtake0 || ftrap0 || dtake1 || ftrap1;
  wire dlane  = dtake1 || ftrap1;              // the D take is the follower candidate
  wire iss0 = iss0_raw && !dstop && !dfault[0] && !trap_d[0];
  wire iss1 = iss1_raw && iss0 && !dfault[1] && !trap_d[1];
  assign pop = {1'b0, iss0} + {1'b0, iss1};

  // ---- the take record of this cycle
  logic        tk;                             // a take (pipeline or D) this cycle
  logic        tk_gen;                         // a general exception (BL = 1: the CPU-only reset instead)
  logic        tk_mhit;                        // a TLB multiple hit (the 0x140 reset instead)
  logic [11:0] tk_code, tk_vec;
  logic        tk_ubrk, tk_trapa, tk_tea_v, tk_tra_v, tk_lsc;
  logic [2:0]  tk_ncl;                         // claimants: the fetched memory instructions the entry waits for
  logic        tk_intr, tk_nmi;                // an interrupt take (irq.code latched here, SOC.md 4.3.4 item 1)
  logic        tk_dbs;                         // the deferred data break, taken at its slot's MA cycle
  logic [31:0] tk_spc, tk_tea;
  logic [7:0]  tk_tra, tk_isn, tk_k;           // EXC0's ISN; the excepting instruction's ISN
  logic [1:0]  tk_hit;                         // UBC tags reaching their break point with the take
  logic        exc0_now;                       // EXC0 issues this cycle
  logic        exc0_l1;                        // in lane 1 (the take's own cycle, a follower-candidate take)
  /* verilator lint_off UNUSEDSIGNAL */         // the function reads v, uk and form
  function automatic logic memop_u(input uop_t u);
    return u.v && u.uk == UK_NORM && v1_ictl(u.form).memop;
  endfunction
  function automatic logic br_u(input uop_t u);             // a micro-op of a branch (PC-relative or register target)
    return u.v && u.uk == UK_NORM && (v1_ictl(u.form).br_pc || v1_ictl(u.form).br_reg);
  endfunction
  /* verilator lint_on UNUSEDSIGNAL */
  always_comb begin
    logic ml, dl;
    ml = !ma_tk[0]; dl = dlane;
    tk = pk_v || dtake || itake;
    tk_gen = 1'b1; tk_mhit = 1'b0; tk_code = '0; tk_vec = VEC_GENERAL; tk_ubrk = 1'b0; tk_trapa = 1'b0;
    tk_tea_v = 1'b0; tk_tra_v = 1'b0; tk_spc = '0; tk_tea = '0; tk_tra = '0; tk_isn = '0; tk_k = '0; tk_hit = '0;
    tk_intr = 1'b0; tk_nmi = 1'b0;
    tk_dbs = matake && dbrk_on && ma[ml].slot;
    if (stake) begin
      tk_k = sl[s_yl].isn; tk_isn = pk_isn;
      tk_code = EXP_UBRK; tk_ubrk = 1'b1; tk_gen = 1'b0; tk_hit = s_brk_hit;
      tk_spc = s_ybr ? sl[s_yl].pc : sl[s_yl].ftea;        // the first instruction issued after the group (a slot:
                                                           // its branch)
    end else if (matake) begin
      tk_k = ma[ml].isn;
      if (tk_dbs) begin                                    // the deferred data break, at the slot's MA cycle
        tk_code = EXP_UBRK; tk_ubrk = 1'b1; tk_gen = 1'b0; tk_hit = dbrk_hit | ma_hit[ml];
        if (ma[ml].fx != FX_NONE) begin                    // the slot faults: it is suppressed, and with it its
          tk_isn = ma[ml].isn; tk_spc = ma[ml].pc - 32'd2; // branch (SPC = the branch, as for any slot entry) [P]
        end else begin
          tk_isn = ma[ml].isn + 8'd1; tk_spc = ma[ml].ftea; // the slot completes: its continuation [HW c1k]
        end
      end else if (ma[ml].fx != FX_NONE) begin             // data fault: re-execution type, the instruction again
        tk_isn = ma[ml].isn;
        tk_mhit = ma[ml].fx == FX_MHIT;
        tk_code = fx_expevt(ma[ml].fx, ma[ml].fwr, 1'b0);
        if (ma[ml].fvec400) tk_vec = VEC_TLB_MISS;
        tk_spc = ma[ml].slot ? ma[ml].pc - 32'd2 : ma[ml].pc;  // a slot's fault saves the branch [HW]
        tk_tea_v = 1'b1; tk_tea = ma[ml].ftea;
      end else if (ma[ml].trap) begin                      // TRAPA: completion type (a contending post / operand
        tk_isn = ma[ml].isn + 8'd1;                        // break is suppressed; its flag is set, 4.4)
        tk_code = EXP_TRAPA; tk_trapa = 1'b1; tk_tra_v = 1'b1; tk_tra = ma[ml].op[7:0];
        tk_spc = ma[ml].pc + 32'd2; tk_hit = ma_hit[ml];
      end else begin                                       // UBC post-execution / operand-address break
        tk_isn = ma[ml].isn + 8'd1;
        tk_code = EXP_UBRK; tk_ubrk = 1'b1; tk_gen = 1'b0; tk_hit = ma_hit[ml];
        tk_spc = ma[ml].ftea;                              // the next instruction (the target after a slot)
      end
    end else if (ftake) begin                              // the F1 operand trap (SOC.md 4.3.2): the instruction's
      tk_k = trap_f1_isn; tk_isn = tk_k;                   // micro-op in EX becomes EXC0 in place, with its ISN
      tk_code = EXP_FPU;
      tk_spc = ex[trap_f1_lane].slot ? ex[trap_f1_lane].pc - 32'd2 : ex[trap_f1_lane].pc;   // a slot: the branch
      // the slot of a deferred data break traps at F1: the older break wins and the slot is suppressed with its
      // branch (SPC = the branch, as for the slot's D-stage trap above) [P]
      if (dbrk_on && ex[trap_f1_lane].slot) begin
        tk_code = EXP_UBRK; tk_ubrk = 1'b1; tk_gen = 1'b0; tk_hit = dbrk_hit;
      end
    end else if (itake) begin                              // interrupt (SOC.md 4.3.5): no general exception, so
      tk_k = isn_ctr; tk_isn = tk_k;                       // never the BL = 1 reset; EXC0 takes the leader's ISN
      tk_intr = 1'b1; tk_nmi = irq.nmi; tk_gen = 1'b0;
      tk_code = irq.code; tk_vec = VEC_INTERRUPT;
      tk_spc = sleep_issued ? slp_cont : q_v[0] ? q_pc[0] : f_fpc;  // the leader (or the instruction the empty window
                                                           // waits for); asleep SLEEP + 2 (its continuation) [HW]
    end else if (dtake) begin
      tk_k = isn_ctr + {7'd0, dl}; tk_isn = tk_k;
      tk_code = (dtake0 || dtake1) ? dcode[dl] : EXP_FPU;
      tk_ubrk = (dtake0 || dtake1) && d_ubrk[dl]; tk_gen = !tk_ubrk;
      tk_hit = (dtake0 || dtake1) ? d_hit[dl] : 2'b00;
      tk_spc = in_slot[dl] ? q_pc[dl] - 32'd2 : q_pc[dl];  // a slot's fault saves the branch [HW]
      tk_tea_v = (dtake0 || dtake1) && d_ifx[dl]; tk_tea = q_pc[dl];   // a fetch fault: TEA = the instruction [HW]
      if ((dtake0 || dtake1) && d_ifx[dl]) begin           // its vector (ITLB miss: + 0x400) and the multiple hit's reset
        if (dl ? q_tag[1][0] : q_tag[0][0]) tk_vec = VEC_TLB_MISS;
        tk_mhit = q_fxe[dl] == FX_MHIT;
      end
      // the slot of a deferred data break faults (or traps) at D: the older break wins, and the slot is suppressed with
      // its branch (SPC = the branch, as for any slot entry) [P]
      if (dbrk_on && !dl && slot0_q) begin
        tk_code = EXP_UBRK; tk_ubrk = 1'b1; tk_gen = 1'b0; tk_hit = tk_hit | dbrk_hit; tk_tea_v = 1'b0;
      end
    end
  end
  // (a block of its own: the claim reads v1_fetch's word of this cycle, whose request reads this cycle's redirects,
  // which read the take above)
  always_comb begin
    logic ml, dl;
    logic brs;                                             // the claim scan has passed a younger branch
    uop_t y;
    ml = !ma_tk[0]; dl = dlane; brs = 1'b0; y = '0;
    tk_ncl = '0;
    // the P claim (SOC.md 4.3.4 item 2): every memory instruction already fetched after the excepting one in program
    // order holds a claim on the operand-address path, and the entry waits 2 release steps per claimant (`ent_rc`):
    //   * an MA / S take: the issued younger access micro-ops (EX, MA: each holds its own), the queue from q0 on;
    //   * a D-stage take: the queue behind the excepting instruction (q1 on; q2 on for a follower-candidate take);
    //   * an interrupt: the D leader itself (it is not excepting: it runs again after the handler) and the queue after
    //     it;
    //   * program order ends at a branch: nothing after a branch claims, its delay slot included.
    // [HW] c1 / c1h / c1i (A2): + 2 with one memory instruction 1, 2 or 3 places behind, every access form, issued or
    // queued, and a faulting access does not claim for itself (dae E = trap E = 8); c1j (A3, tools/hw/core-exc,
    // 2026-09-23, interrupts): + 2 with a load, store, PC-relative load or PREF at the interrupted instruction or 1 / 2
    // after it, + 4 with loads at it and after it (the claims add), + 0 with the load 3 after it (not yet fetched at
    // t); c1b's TRAPA as a JSR target, a PC-relative load (issued) then RTS with a store in its slot: + 2 only, which
    // under the additive release is the branch stop excluding the slot.  "Fetched at t" is v1_fetch's claim window: the
    // queue and the word read in the take cycle (a refetched odd-halfword X and the word X + 2 .. X + 4 read at t: the
    // console's "8-byte block"; c1i's H'FFFD with the access 2 / 3 behind; c1b j0), with the follower-candidate
    // exception q_dep (c1b d0) [WI-CORE-B].
    // An MA / S take scans the younger in-flight micro-ops in program order (MA lane 0, lane 1, EX lane 0, lane 1),
    // then the queue: the branch stop applies to issued micro-ops as to queued ones (an issued delayed branch's issued
    // slot does not claim, nor anything after it), and a claimant is an instruction, counted at its first micro-op
    // (TAS.B / MAC.L with micro-ops in MA and EX claim once, as in the queue) [P, WI-CORE-A F4 / F5; unmeasured].
    // "Younger" is the pipeline position, not the ISN (SOC.md 4.3.1: no timing decision reads it): everything in MA
    // and EX behind the S group, or behind the taking MA lane (a later micro-op of the taking instruction itself is
    // never a first micro-op)
    if (stake || matake) begin
      for (int k = 0; k < 4; k++) begin                    // (nothing after a younger branch claims)
        y = k < 2 ? ma[k] : ex[k - 2];
        if (y.v && y.uk == UK_NORM && (stake || k >= 2 || (k == 1 && !ml))) begin
          if (!brs && y.step == 3'd0 && memop_u(y)) tk_ncl = tk_ncl + 3'd1;
          if (br_u(y)) brs = 1'b1;
        end
      end
      if (!brs)
        tk_ncl = tk_ncl + 3'(qm[0]) + 3'(!q_br[0] && qm[1]) +
                 3'(!q_br[0] && !q_br[1] && q2_mem) + 3'(!q_br[0] && !q_br[1] && !q_br[2] && q3_mem);
    end else if (dtake)
      tk_ncl = dl ? 3'(!c[0].br_dly && q2_mem && !q_dep[0]) + 3'(!c[0].br_dly && !q_br[2] && q3_mem && !q_dep[1])
                  : 3'(qm[1]) + 3'(!q_br[1] && q2_mem) + 3'(!q_br[1] && !q_br[2] && q3_mem);
    else if (itake)
      tk_ncl = 3'(qm[0]) + 3'(!q_br[0] && qm[1]) + 3'(!q_br[0] && !q_br[1] && q2_mem) +
               3'(!q_br[0] && !q_br[1] && !q_br[2] && q3_mem);
    tk_lsc = tk_ncl != 3'd0;
    // EXC0 issues in the first unheld cycle >= t with the claim released and the sequencer free (a completion-type
    // take in a multi-step instruction lets the instruction finish [P])
    exc0_l1 = tk && dtake && dl;
    // (an F1 trap's EXC0 is the converted micro-op: nothing issues from D for it)
    if (tk) exc0_now = !ftake && !tk_lsc && !(sq_on && !sq_kill);
    else exc0_now = ent_pend && ent_rc == 4'd0 && !frz && !sq_on;
  end

  // ---- the vector (SOC.md 4.3.5): VBR + offset with the S write-through, or DBR for a user break with BRCR.UBDE
  assign exc_vector = (ent_ubrk && ubc_ctl.ubde) ? dbr_rd : vbr_rd + {20'd0, ent_vec};
  // (the wait for an outstanding uncached fetch / ITLB refill, irsp.busy, is v1_fetch's: SOC.md 4.1 item 4; EXC_SAVE
  // issues in the vector's fetch cycle, after it: f_busy_wait)
  wire        exc_redir = exc_rd && !frz;

  always_comb begin
    redirect = 1'b0; rd_target = '0; rd_keep = 1'b0; rd_slot_pend = 1'b0; rd_slot_pc = '0;
    refetch = 1'b0; rf_pc = '0; f_flush = 1'b0; f_exc_rd = 1'b0; f_skip = 1'b0;
    if (tk && !frz) begin
      // the take flushes the fetch queue and every pending redirect (a branch of this cycle is younger than the
      // take, or its target is: a slot fault); fetch then stops until the entry's redirect
      f_flush = 1'b1;
    end else if (exc_redir) begin
      f_exc_rd = 1'b1;                                     // (v1_fetch waits for an outstanding busy word: F)
    end else if (iss1 && br_taken[1] && !br_skip[1]) begin
      // the branch paired as follower: its delay slot is the next queue entry
      redirect = 1'b1; rd_target = br_tgt[1];
      if (c[1].br_dly) begin rd_keep = q_v[2]; rd_slot_pend = !q_v[2]; rd_slot_pc = q_pc[1] + 32'd2; end
    end else if (iss0 && br_taken[0] && !br_skip[0]) begin
      redirect = 1'b1; rd_target = br_tgt[0];
      if (c[0].br_dly && !iss1) begin rd_keep = q_v[1]; rd_slot_pend = !q_v[1]; rd_slot_pc = q_pc[0] + 32'd2; end
    end else if (sq_last && sq_c.br_reg && !frz) begin
      // JMP JSR BRAF BSRF RTS: micro-op 0 is in EX and has the target; RTE took SPC in micro-op 0.  The sequencer's
      // last D cycle is the one in which it steps (not a held cycle)
      redirect = 1'b1; rd_target = sq_c.seq == SQ_RTE ? sq_target : jmp_tgt_ex;
      rd_keep = q_v[0]; rd_slot_pend = !q_v[0]; rd_slot_pc = sq_pc + 32'd2;
    end else if (sq_last && sq_c.refetch && !frz) begin
      refetch = 1'b1; rf_pc = sq_pc + 32'd2;
    end else
      // a taken BT / BF with disp 0 skips the next instruction without a redirect (SOC.md 4.1 item 6): v1_fetch nullifies
      // it [HW debug-ubc-hudi 4.7.1: btt / bfshort 5 cycles and 4 issue groups; a redirect would cost a bubble more]
      f_skip = (iss0 && br_skip[0]) || (iss1 && br_skip[1]);
  end

  // ---- building a micro-op in D
  /* verilator lint_off UNUSEDSIGNAL */       // the function reads the fields its forms need
  function automatic uop_t mk_uop(input logic [31:0] pc, input logic [15:0] op, input dec_t dd, input ictl_t cc,
                                  input logic [2:0] step, input logic lane, input logic [2:0] md,
                                  input logic [7:0] isn, input logic slot, input logic [31:0] cont);
    uop_t u;
    logic [4:0] pm, pn;
    u = '0;
    u.v = 1'b1; u.pc = pc; u.op = op; u.form = dd.form; u.step = step; u.lane = lane; u.rb = rb; u.md = md;
    u.isn = isn; u.slot = slot;
    // ftea: until EX finds a fault (then the faulting address) the instruction after this one in program order --
    // the SPC of a completion-type break on it (UBC post-execution / operand-address, the data break's group)
    u.ftea = cont;
    u.trap = cc.form_x == XF_TRAPA && step == 3'd0;
    pm = gphys(op[7:4], rb);
    pn = gphys(op[11:8], rb);
    u.pa = pm; u.pb = pn; u.pc0 = gphys(4'd0, rb);
    u.a = gpr_rd[pm]; u.b = gpr_rd[pn]; u.c = gpr_rd[u.pc0];
    u.tin = t_now; u.qin = q_now; u.min = m_now; u.sin = s_now;
    // system register read (STC / STS / RTS / RTE / GBR-relative)
    unique case (dd.form)
      F_STC_SR_RN, F_STC_L_SR_AT_PRE_RN: u.cr = {sr_rd[31:10], m_now, q_now, sr_rd[7:2], s_now, t_now};
      F_STC_GBR_RN, F_STC_L_GBR_AT_PRE_RN, F_MOV_B_AT_D_GBR_R0, F_MOV_W_AT_D_GBR_R0, F_MOV_L_AT_D_GBR_R0,
      F_MOV_B_R0_AT_D_GBR, F_MOV_W_R0_AT_D_GBR, F_MOV_L_R0_AT_D_GBR, F_TST_B_IMM_AT_R0_GBR, F_AND_B_IMM_AT_R0_GBR,
      F_OR_B_IMM_AT_R0_GBR, F_XOR_B_IMM_AT_R0_GBR: u.cr = gbr_rd;
      F_STC_VBR_RN, F_STC_L_VBR_AT_PRE_RN: u.cr = vbr_rd;
      F_STC_SSR_RN, F_STC_L_SSR_AT_PRE_RN: u.cr = ssr_rd;
      F_STC_SPC_RN, F_STC_L_SPC_AT_PRE_RN: u.cr = spc_rd;
      F_STC_SGR_RN, F_STC_L_SGR_AT_PRE_RN: u.cr = sgr_rd;
      F_STC_DBR_RN, F_STC_L_DBR_AT_PRE_RN: u.cr = dbr_rd;
      F_STC_RM_BANK_RN, F_STC_L_RM_BANK_AT_PRE_RN: u.cr = gpr_rd[gbank(op[6:4], rb)];
      F_STS_PR_RN, F_STS_L_PR_AT_PRE_RN, F_RTS: u.cr = pr_rd;
      F_RTE: u.cr = ssr_rd;
      default: u.cr = '0;
    endcase
    // tags for the hazard comparators
    u.sxw = cc.sx_w && step == 3'd0;
    u.gld = cc.gld;
    u.flagop = cc.wr_flags && !cc.t_fpu &&
               ((cc.seq == SQ_TSTB && step == 3'd2) || (cc.seq == SQ_TASB && step == 3'd4) ||
                (cc.seq != SQ_TSTB && cc.seq != SQ_TASB && step == 3'd0));
    // destinations (the result values come in EX / MA)
    if (step == 3'd0 && cc.seq != SQ_MAC) begin
      logic [23:0] m1, mu, ml;
      m1 = gmask(dd.gw, rb);
      mu = gmask(dd.gwu, rb);
      ml = gmask(dd.gwl, rb);
      for (int i = 0; i < 24; i++) begin
        if (m1[i]) begin u.w1 = 1'b1; u.w1r = 5'(i); u.w1k = cc.gres; end
        if (mu[i] && !ml[i]) begin u.w1 = 1'b1; u.w1r = 5'(i); u.w1k = GR_EX; end   // @Rm+ with n == m: the load wins
        if (ml[i]) begin u.w2 = 1'b1; u.w2r = 5'(i); end
      end
    end
    if (cc.seq == SQ_MAC) begin                   // MAC.x (C3): micro-op 0 walks @Rn+, micro-op 1 @Rm+
      u.w1 = 1'b1; u.w1k = GR_EX; u.w1r = step == 3'd0 ? pn : pm;
    end
    // LS-pipe FP results
    if (cc.fp_ls || dd.form == F_LDS_RM_FPUL || dd.form == F_LDS_L_AT_RM_INC_FPUL) begin
      u.fk = cc.fprod;
      u.fwm = dd.fw;
      u.uw = dd.sw[SYS_FPUL];
    end
    u.fscw = dd.form == F_LDS_RM_FPSCR || dd.form == F_LDS_L_AT_RM_INC_FPSCR;
    // system-register writes at S
    unique case (dd.form)
      F_LDC_RM_SR, F_LDC_L_AT_RM_INC_SR, F_RTE: u.crw = step == 3'd0 ? CR_SR : CR_NONE;
      F_LDC_RM_GBR, F_LDC_L_AT_RM_INC_GBR: u.crw = step == 3'd0 ? CR_GBR : CR_NONE;
      F_LDC_RM_VBR, F_LDC_L_AT_RM_INC_VBR: u.crw = CR_VBR;
      F_LDC_RM_SSR, F_LDC_L_AT_RM_INC_SSR: u.crw = CR_SSR;
      F_LDC_RM_SPC, F_LDC_L_AT_RM_INC_SPC: u.crw = CR_SPC;
      F_LDC_RM_DBR, F_LDC_L_AT_RM_INC_DBR: u.crw = CR_DBR;
      F_LDC_RM_RN_BANK, F_LDC_L_AT_RM_INC_RN_BANK: begin u.crw = CR_BANK; end
      F_LDS_RM_PR, F_LDS_L_AT_RM_INC_PR, F_BSR_LABEL, F_JSR_AT_RN, F_BSRF_RN: u.crw = step == 3'd0 ? CR_PR : CR_NONE;
      default: u.crw = CR_NONE;
    endcase
    if (u.crw == CR_BANK) u.bkr = gbank(op[6:4], rb);            // the bank register (written as a GPR at S)
    return u;
  endfunction
  /* verilator lint_on UNUSEDSIGNAL */

  // a micro-op of the entry sequence (EXC0, EXC_SAVE) or a tag bubble: no operands, no results, no hazard tags
  // (EXC_SAVE carries the system-register write tag).  Form NOP, so every form-keyed path ignores it.
  function automatic uop_t mk_nop(input logic [1:0] uk, input logic [31:0] pc, input logic [7:0] isn);
    uop_t u;
    u = '0;
    u.v = 1'b1; u.uk = uk; u.pc = pc; u.op = 16'h0009; u.form = F_NOP; u.isn = isn; u.rb = rb; u.md = md_dec;
    u.sxw = uk == UK_EXC_SAVE;
    return u;
  endfunction

  // micro-ops entering EX this cycle
  uop_t ex_in [2];
  logic ex_in_smd [2];
  always_comb begin
    logic go;
    go = 1'b0;
    ex_in[0] = '0; ex_in[1] = '0;
    ex_in_smd[0] = sr_rd[30]; ex_in_smd[1] = sr_rd[30];
    if (iss0) begin
      ex_in[0] = mk_uop(q_pc[0], q_op[0], d[0], c[0], 3'd0, 1'b0, md_dec, isn_ctr, in_slot[0], cont_d[0]);
      // UBC post-execution tag: a one-cycle non-branch instruction carries its own; a delayed branch hands it to its
      // slot (post_carry; SPC = the target, or slot + 2 not taken [HW slot_br_post, bfsnt_br_post]); a multi-cycle
      // instruction's rides its last micro-op (the sequencer)
      if (c[0].k == 3'd1 && !c[0].br_dly) ex_in[0].ubc_post = tg_post[0] | (in_slot[0] ? post_carry : 2'b00);
      // SR.BL before the instruction: D's SR (with the S write-through; an SR writer is CO with k >= 4, so no older SR
      // write is still in EX / MA when the next instruction issues)
      ex_in[0].blb = sr_rd[28];
    end
    if (iss1) begin
      ex_in[1] = mk_uop(q_pc[1], q_op[1], d[1], c[1], 3'd0, 1'b1, md_dec, isn_ctr + 8'd1, in_slot[1], cont_d[1]);
      if (!c[1].br_dly) ex_in[1].ubc_post = tg_post[1] | (in_slot[1] ? tg_post[0] : 2'b00);
      ex_in[1].blb = sr_rd[28];                  // (the leader never writes SR: SR writers are CO)
    end
    // later micro-ops of a multi-cycle instruction (lane 0; nothing pairs with them)
    if (sq_on && !frz && !sq_kill) begin
      unique case (sq_c.seq)
        SQ_MAC:  go = sq_step == 3'd1;             // the second load (@Rm+)
        SQ_TSTB: go = sq_step == 3'd2;             // T from the loaded byte
        SQ_BOPB: go = sq_step == 3'd3;             // store the modified byte
        SQ_TASB: go = sq_step == 3'd1 || sq_step == 3'd4;   // the locked read; store byte | 0x80, T
        SQ_SX2:  go = sq_step == 3'd1 && stcl_form(sq_form);   // STC.L / STS.L PR: the store (cycle 2)
        SQ_SX3:  go = sq_step == 3'd2 && stcl_form(sq_form);   // STC.L SGR: the store
        default: go = 1'b0;
      endcase
      if (go) begin
        ex_in[0] = mk_uop(sq_pc, sq_op, sq_d, sq_c, sq_step, 1'b0, sq_md, sq_isn, sq_slot, sq_cont);
        ex_in_smd[0] = sq_smd;
        ex_in[0].blb = sq_sbl;
      end
      // the post-execution tag of a multi-cycle instruction rides its last micro-op; a last step with no micro-op
      // (a D-only cycle) sends a bubble that carries only the tag [P]
      if (sq_last && !sq_c.br_dly && sq_post != 2'b00) begin
        // (the bubble keeps the step it stands for: step != 0 on a NOP form marks it for the commit-log tap, which
        // retires the instruction at its own last micro-op, never at the bubble)
        if (!go) begin ex_in[0] = mk_nop(UK_NORM, sq_pc, sq_isn); ex_in[0].step = sq_step; end
        ex_in[0].ubc_post = sq_post; ex_in[0].ftea = sq_cont; ex_in[0].slot = sq_slot; ex_in[0].blb = sq_sbl;
      end
    end
    // the entry sequence (SOC.md 4.3.4 items 3, 6)
    if (exc0_now) begin
      ex_in[exc0_l1] = mk_nop(UK_EXC0, tk ? tk_spc : ent_spc, tk ? tk_isn : ent_isn);
      ex_in_smd[exc0_l1] = sr_rd[30];
    end
    if (exc_sv && !frz && !f_busy_wait) ex_in[0] = mk_nop(UK_EXC_SAVE, exc_vector, isn_ctr);
  end

  // OCBWB keeps the cache port for one more cycle after its ack: an access in EX (a load, store, PREF, cache-block op
  // -- any micro-op with an access kind -- or LDTLB, whose UTLB write uses the same MA port) would need it in its MA
  // cycle, so in the OCBWB's ack cycle the whole pipeline holds for one cycle (measured 1, sweep: OCBWB ; B at n = 0).
  // Charged once per OCBWB, in its ack cycle, however long it waited (SOC.md 3.2.2): an acked request is never
  // presented again (ma_done), so there is only one ack cycle.
  always_comb begin
    logic use_ex;
    use_ex = 1'b0;
    for (int l = 0; l < 2; l++)
      if (ex[l].v && (ex_res[l].dk != DK_NONE || ex[l].form == F_LDTLB)) use_ex = 1'b1;
    oc_coll = lreq.v && lrsp.ack && lreq.kind == DK_OCBWB && use_ex;
  end

  // FPU dispatch of this cycle's issues (a D-stage take never dispatches: iss0 / iss1 are gated).  A D-decided FPU
  // trap dispatches as a trap pass (di_v with v1_fpu's trap_d): it writes the trap's cause at FS (SOC.md 4.3.8)
  always_comb begin
    di_v[0] = iss0 || ftrap0; di_op[0] = q_op[0]; di_d[0] = d[0]; di_c[0] = c[0]; di_md[0] = md_dec;
    di_v[1] = iss1 || ftrap1; di_op[1] = q_op[1]; di_d[1] = d[1]; di_c[1] = c[1]; di_md[1] = md_dec;
    di_isn[0] = isn_ctr; di_isn[1] = isn_ctr + 8'd1;
    di_fcmpd = sq_on && sq_c.seq == SQ_FCMPD && sq_step == 3'd1 && !sq_kill;
    di_fcmpd_op = sq_op; di_fcmpd_md = sq_md;
  end

  // ============================================================================================================
  // EX stage
  //   operands are corrected by the bypass: from MA (EX results of the previous cycle), from S (EX results and load
  //   data), and in lane 1 from lane 0 of the same cycle when lane 0 is MOV Rm,Rn (a copy of its source)
  /* verilator lint_off UNUSEDSIGNAL */       // the function reads the fields its forms need
  function automatic logic [31:0] byp(input logic [4:0] r, input logic [31:0] v, input logic l1, input uop_t e0);
    logic [31:0] x;
    x = v;
    for (int l = 0; l < 2; l++) begin
      if (sl[l].v && sl[l].w1 && sl[l].w1r == r) x = sl[l].w1v;
      if (sl[l].v && sl[l].w2 && sl[l].w2r == r) x = sl[l].w2v;
    end
    for (int l = 0; l < 2; l++)
      if (ma[l].v && ma[l].w1 && ma[l].w1k == GR_EX && ma[l].w1r == r) x = ma[l].w1v;
    if (l1 && e0.v && e0.form == F_MOV_RM_RN && e0.w1r == r) x = e0.a;
    return x;
  endfunction
  /* verilator lint_on UNUSEDSIGNAL */

  // FP register as the LS pipe reads it in EX: the register file (write-through) or an LS result not written yet
  /* verilator lint_off UNUSEDSIGNAL */       // the function reads the fields its forms need
  function automatic logic [31:0] frd_ex(input int w);
    logic [31:0] x;
    x = fr_now[w];
    for (int l = 0; l < 2; l++) if (sl[l].v && sl[l].fwm[w]) x = w[0] ? sl[l].fwo : sl[l].fwe;
    for (int l = 0; l < 2; l++) if (ma[l].v && ma[l].fwm[w] && ma[l].fk != FP_LOAD) x = w[0] ? ma[l].fwo : ma[l].fwe;
    return x;
  endfunction
  /* verilator lint_on UNUSEDSIGNAL */
  function automatic logic [31:0] fpul_ex();
    logic [31:0] x;
    x = fpul_now;
    for (int l = 0; l < 2; l++) if (sl[l].v && sl[l].uw) x = sl[l].uv;
    for (int l = 0; l < 2; l++) if (ma[l].v && ma[l].uw && ma[l].fk != FP_LOAD) x = ma[l].uv;
    return x;
  endfunction

  uop_t e0res;                                  // lane 0's EX result, for lane 1's MOV steer
  always_comb begin
    jmp_tgt_ex = '0;
    e0res = '0;
    for (int l = 0; l < 2; l++) begin
      uop_t u;
      alu_out_t ao;
      alu_op_e aop;
      logic [31:0] a, b, cc, imm8s, imm8z, sz, n4, n8, frv;
      logic [4:0] fn, fm;
      u = ex[l];
      // lane 1 may take lane 0's MOV Rm,Rn result: the value lane 0 read after ITS bypass (ex_res[0] is complete here)
      a = byp(u.pa, u.a, l == 1, e0res);
      b = byp(u.pb, u.b, l == 1, e0res);
      cc = byp(u.pc0, u.c, l == 1, e0res);
      u.a = a; u.b = b; u.c = cc;          // the register values; a / b below are the ALU inputs and may be replaced
      imm8s = {{24{u.op[7]}}, u.op[7:0]};
      imm8z = {24'd0, u.op[7:0]};
      n4 = {28'd0, u.op[3:0]};
      n8 = imm8z;
      fn = {u.md[2], u.op[11:8]};               // physical FR number of FRn (bank = FPSCR.FR)
      fm = {u.md[2], u.op[7:4]};
      aop = A_MOV;
      ao = '0;
      // ---- integer ALU forms
      unique case (u.form)
        F_MOV_IMM_RN:    begin aop = A_MOV;   a = imm8s; end
        F_MOV_RM_RN:     aop = A_MOV;
        F_MOVT_RN:       aop = A_MOVT;
        F_SWAP_B_RM_RN:  aop = A_SWAPB;
        F_SWAP_W_RM_RN:  aop = A_SWAPW;
        F_XTRCT_RM_RN:   aop = A_XTRCT;
        F_ADD_RM_RN:     aop = A_ADD;
        F_ADD_IMM_RN:    begin aop = A_ADD;   a = imm8s; end
        F_ADDC_RM_RN:    aop = A_ADDC;
        F_ADDV_RM_RN:    aop = A_ADDV;
        F_CMP_EQ_IMM_R0: begin aop = A_CMPEQ; a = imm8s; b = cc; end
        F_CMP_EQ_RM_RN:  aop = A_CMPEQ;
        F_CMP_HS_RM_RN:  aop = A_CMPHS;
        F_CMP_GE_RM_RN:  aop = A_CMPGE;
        F_CMP_HI_RM_RN:  aop = A_CMPHI;
        F_CMP_GT_RM_RN:  aop = A_CMPGT;
        F_CMP_PZ_RN:     aop = A_CMPPZ;
        F_CMP_PL_RN:     aop = A_CMPPL;
        F_CMP_STR_RM_RN: aop = A_CMPSTR;
        F_DIV1_RM_RN:    aop = A_DIV1;
        F_DIV0S_RM_RN:   aop = A_DIV0S;
        F_DIV0U:         aop = A_DIV0U;
        F_DT_RN:         aop = A_DT;
        F_EXTS_B_RM_RN:  aop = A_EXTSB;
        F_EXTS_W_RM_RN:  aop = A_EXTSW;
        F_EXTU_B_RM_RN:  aop = A_EXTUB;
        F_EXTU_W_RM_RN:  aop = A_EXTUW;
        F_NEG_RM_RN:     aop = A_NEG;
        F_NEGC_RM_RN:    aop = A_NEGC;
        F_SUB_RM_RN:     aop = A_SUB;
        F_SUBC_RM_RN:    aop = A_SUBC;
        F_SUBV_RM_RN:    aop = A_SUBV;
        F_AND_RM_RN:     aop = A_AND;
        F_AND_IMM_R0:    begin aop = A_AND;   a = imm8z; b = cc; end
        F_NOT_RM_RN:     aop = A_NOT;
        F_OR_RM_RN:      aop = A_OR;
        F_OR_IMM_R0:     begin aop = A_OR;    a = imm8z; b = cc; end
        F_TST_RM_RN:     aop = A_TST;
        F_TST_IMM_R0:    begin aop = A_TST;   a = imm8z; b = cc; end
        F_XOR_RM_RN:     aop = A_XOR;
        F_XOR_IMM_R0:    begin aop = A_XOR;   a = imm8z; b = cc; end
        F_ROTL_RN:       aop = A_ROTL;
        F_ROTR_RN:       aop = A_ROTR;
        F_ROTCL_RN:      aop = A_ROTCL;
        F_ROTCR_RN:      aop = A_ROTCR;
        F_SHAD_RM_RN:    aop = A_SHAD;
        F_SHAL_RN:       aop = A_SHAL;
        F_SHAR_RN:       aop = A_SHAR;
        F_SHLD_RM_RN:    aop = A_SHLD;
        F_SHLL_RN:       aop = A_SHLL;
        F_SHLR_RN:       aop = A_SHLR;
        F_SHLL2_RN:      aop = A_SHLL2;
        F_SHLR2_RN:      aop = A_SHLR2;
        F_SHLL8_RN:      aop = A_SHLL8;
        F_SHLR8_RN:      aop = A_SHLR8;
        F_SHLL16_RN:     aop = A_SHLL16;
        F_SHLR16_RN:     aop = A_SHLR16;
        F_CLRT:          aop = A_CLRT;
        F_SETT:          aop = A_SETT;
        F_TST_B_IMM_AT_R0_GBR: begin aop = A_TST; a = imm8z; b = {24'd0, rmw_tmp[7:0]}; end
        F_AND_B_IMM_AT_R0_GBR: begin aop = A_AND; a = imm8z; b = {24'd0, rmw_tmp[7:0]}; end
        F_OR_B_IMM_AT_R0_GBR:  begin aop = A_OR;  a = imm8z; b = {24'd0, rmw_tmp[7:0]}; end
        F_XOR_B_IMM_AT_R0_GBR: begin aop = A_XOR; a = imm8z; b = {24'd0, rmw_tmp[7:0]}; end
        F_TAS_B_AT_RN:         begin aop = A_TAS;  b = {24'd0, rmw_tmp[7:0]}; end
        default: ;
      endcase
      ao = v1_alu(aop, a, b, u.tin, u.qin, u.min);
      if (u.w1 && u.w1k == GR_EX) u.w1v = ao.r;
      if (u.form == F_MOVA_AT_D_PC_R0) u.w1v = {u.pc[31:2], 2'b00} + 32'd4 + {22'd0, u.op[7:0], 2'b00};
      u.tw = 1'b0; u.qmw = 1'b0; u.sw = 1'b0;
      if (ao.wr_t && (u.step == 3'd0 || u.form == F_TST_B_IMM_AT_R0_GBR || u.form == F_TAS_B_AT_RN) &&
          !(u.form == F_TST_B_IMM_AT_R0_GBR && u.step != 3'd2) && !(u.form == F_TAS_B_AT_RN && u.step != 3'd4)) begin
        u.tw = 1'b1; u.tv = ao.t;
      end
      if (ao.wr_qm) begin u.qmw = 1'b1; u.qv = ao.q; u.mv = ao.m; end
      if (u.form == F_CLRS) begin u.sw = 1'b1; u.sv = 1'b0; end
      if (u.form == F_SETS) begin u.sw = 1'b1; u.sv = 1'b1; end
      // ---- memory access
      sz = 32'd0;
      unique case (u.form)
        // loads
        F_MOV_B_AT_RM_RN, F_MOV_B_AT_RM_INC_RN: begin u.mrd = 1; u.msz = 2'd0; u.msx = 1; u.maddr = u.a; sz = 32'd1; end
        F_MOV_W_AT_RM_RN, F_MOV_W_AT_RM_INC_RN: begin u.mrd = 1; u.msz = 2'd1; u.msx = 1; u.maddr = u.a; sz = 32'd2; end
        F_MOV_L_AT_RM_RN, F_MOV_L_AT_RM_INC_RN: begin u.mrd = 1; u.msz = 2'd2; u.maddr = u.a; sz = 32'd4; end
        F_MOV_B_AT_D_RM_R0: begin u.mrd = 1; u.msz = 2'd0; u.msx = 1; u.maddr = u.a + n4; end
        F_MOV_W_AT_D_RM_R0: begin u.mrd = 1; u.msz = 2'd1; u.msx = 1; u.maddr = u.a + {n4[30:0], 1'b0}; end
        F_MOV_L_AT_D_RM_RN: begin u.mrd = 1; u.msz = 2'd2; u.maddr = u.a + {n4[29:0], 2'b00}; end
        F_MOV_B_AT_R0_RM_RN: begin u.mrd = 1; u.msz = 2'd0; u.msx = 1; u.maddr = u.a + u.c; end
        F_MOV_W_AT_R0_RM_RN: begin u.mrd = 1; u.msz = 2'd1; u.msx = 1; u.maddr = u.a + u.c; end
        F_MOV_L_AT_R0_RM_RN: begin u.mrd = 1; u.msz = 2'd2; u.maddr = u.a + u.c; end
        F_MOV_B_AT_D_GBR_R0: begin u.mrd = 1; u.msz = 2'd0; u.msx = 1; u.maddr = u.cr + n8; end
        F_MOV_W_AT_D_GBR_R0: begin u.mrd = 1; u.msz = 2'd1; u.msx = 1; u.maddr = u.cr + {n8[30:0], 1'b0}; end
        F_MOV_L_AT_D_GBR_R0: begin u.mrd = 1; u.msz = 2'd2; u.maddr = u.cr + {n8[29:0], 2'b00}; end
        F_MOV_W_AT_D_PC_RN: begin u.mrd = 1; u.msz = 2'd1; u.msx = 1; u.maddr = u.pc + 32'd4 + {n8[30:0], 1'b0}; end
        F_MOV_L_AT_D_PC_RN: begin u.mrd = 1; u.msz = 2'd2; u.maddr = {u.pc[31:2], 2'b00} + 32'd4 + {n8[29:0], 2'b00}; end
        // stores
        F_MOV_B_RM_AT_RN: begin u.mwr = 1; u.msz = 2'd0; u.maddr = u.b; u.mwd = {32'd0, u.a}; end
        F_MOV_W_RM_AT_RN: begin u.mwr = 1; u.msz = 2'd1; u.maddr = u.b; u.mwd = {32'd0, u.a}; end
        F_MOV_L_RM_AT_RN, F_MOVCA_L_R0_AT_RN: begin
          u.mwr = 1; u.msz = 2'd2; u.maddr = u.b; u.mwd = {32'd0, u.form == F_MOVCA_L_R0_AT_RN ? u.c : u.a};
        end
        F_MOV_B_RM_AT_PRE_RN: begin u.mwr = 1; u.msz = 2'd0; u.maddr = u.b - 32'd1; u.mwd = {32'd0, u.a}; end
        F_MOV_W_RM_AT_PRE_RN: begin u.mwr = 1; u.msz = 2'd1; u.maddr = u.b - 32'd2; u.mwd = {32'd0, u.a}; end
        F_MOV_L_RM_AT_PRE_RN: begin u.mwr = 1; u.msz = 2'd2; u.maddr = u.b - 32'd4; u.mwd = {32'd0, u.a}; end
        F_MOV_B_R0_AT_D_RN: begin u.mwr = 1; u.msz = 2'd0; u.maddr = u.a + n4; u.mwd = {32'd0, u.c}; end
        F_MOV_W_R0_AT_D_RN: begin u.mwr = 1; u.msz = 2'd1; u.maddr = u.a + {n4[30:0], 1'b0}; u.mwd = {32'd0, u.c}; end
        F_MOV_L_RM_AT_D_RN: begin u.mwr = 1; u.msz = 2'd2; u.maddr = u.b + {n4[29:0], 2'b00}; u.mwd = {32'd0, u.a}; end
        F_MOV_B_RM_AT_R0_RN: begin u.mwr = 1; u.msz = 2'd0; u.maddr = u.b + u.c; u.mwd = {32'd0, u.a}; end
        F_MOV_W_RM_AT_R0_RN: begin u.mwr = 1; u.msz = 2'd1; u.maddr = u.b + u.c; u.mwd = {32'd0, u.a}; end
        F_MOV_L_RM_AT_R0_RN: begin u.mwr = 1; u.msz = 2'd2; u.maddr = u.b + u.c; u.mwd = {32'd0, u.a}; end
        F_MOV_B_R0_AT_D_GBR: begin u.mwr = 1; u.msz = 2'd0; u.maddr = u.cr + n8; u.mwd = {32'd0, u.c}; end
        F_MOV_W_R0_AT_D_GBR: begin u.mwr = 1; u.msz = 2'd1; u.maddr = u.cr + {n8[30:0], 1'b0}; u.mwd = {32'd0, u.c}; end
        F_MOV_L_R0_AT_D_GBR: begin u.mwr = 1; u.msz = 2'd2; u.maddr = u.cr + {n8[29:0], 2'b00}; u.mwd = {32'd0, u.c}; end
        // read-modify-write on @(R0,GBR): micro-op 0 loads the byte, the last one stores it.  TAS.B (C5): micro-op 0
        // purges the line (the first access: the write-permission check), micro-op 1 is the locked read, micro-op 4
        // the locked write
        F_TST_B_IMM_AT_R0_GBR, F_AND_B_IMM_AT_R0_GBR, F_OR_B_IMM_AT_R0_GBR, F_XOR_B_IMM_AT_R0_GBR: begin
          if (u.step == 3'd0) begin
            u.mrd = 1; u.msz = 2'd0; u.maddr = u.cr + u.c;
          end else if (u.form != F_TST_B_IMM_AT_R0_GBR) begin
            u.mwr = 1; u.msz = 2'd0; u.maddr = sq_addr; u.mwd = {32'd0, ao.r};
          end
        end
        F_TAS_B_AT_RN: begin
          u.msz = 2'd0;
          if (u.step == 3'd0) u.maddr = u.b;
          else if (u.step == 3'd1) begin u.mrd = 1; u.maddr = sq_addr; end
          else begin u.mwr = 1; u.maddr = sq_addr; u.mwd = {32'd0, ao.r}; end
        end
        // control-register loads and stores
        F_LDC_L_AT_RM_INC_SR, F_LDC_L_AT_RM_INC_GBR, F_LDC_L_AT_RM_INC_VBR, F_LDC_L_AT_RM_INC_SSR,
        F_LDC_L_AT_RM_INC_SPC, F_LDC_L_AT_RM_INC_DBR, F_LDC_L_AT_RM_INC_RN_BANK, F_LDS_L_AT_RM_INC_MACH,
        F_LDS_L_AT_RM_INC_MACL, F_LDS_L_AT_RM_INC_PR, F_LDS_L_AT_RM_INC_FPSCR, F_LDS_L_AT_RM_INC_FPUL: begin
          u.mrd = 1; u.msz = 2'd2; u.maddr = u.b; sz = 32'd4;      // LDC.L / LDS.L @Rm+: Rm is op[11:8]
        end
        // STC.L / STS.L PR: micro-op 0 forms the address and decrements Rn; the last micro-op stores (the memory
        // port is used in the second issue cycle, not the first) with the address and value carried from micro-op 0
        F_STC_L_SR_AT_PRE_RN, F_STC_L_GBR_AT_PRE_RN, F_STC_L_VBR_AT_PRE_RN, F_STC_L_SSR_AT_PRE_RN,
        F_STC_L_SPC_AT_PRE_RN, F_STC_L_SGR_AT_PRE_RN, F_STC_L_DBR_AT_PRE_RN, F_STC_L_RM_BANK_AT_PRE_RN,
        F_STS_L_PR_AT_PRE_RN: begin
          if (u.step == 3'd0) u.maddr = u.b - 32'd4;
          else begin u.mwr = 1; u.msz = 2'd2; u.maddr = sq_addr; u.mwd = {32'd0, sq_data}; end
        end
        F_STS_L_MACH_AT_PRE_RN: begin u.mwr = 1; u.msz = 2'd2; u.maddr = u.b - 32'd4; u.mwd = {32'd0, mach_now}; end
        F_STS_L_MACL_AT_PRE_RN: begin u.mwr = 1; u.msz = 2'd2; u.maddr = u.b - 32'd4; u.mwd = {32'd0, macl_now}; end
        F_STS_L_FPUL_AT_PRE_RN: begin u.mwr = 1; u.msz = 2'd2; u.maddr = u.b - 32'd4; u.mwd = {32'd0, fpul_ex()}; end
        F_STS_L_FPSCR_AT_PRE_RN: begin u.mwr = 1; u.msz = 2'd2; u.maddr = u.b - 32'd4; u.mwd = {32'd0, fpscr_now}; end
        // MAC.x (C3, SOC.md 4.2): micro-op 0 reads @Rn (op[11:8], operand b), micro-op 1 @Rm -- the first access is
        // checked first [HW: both misaligned -> TEA = Rn; Rm misaligned -> TEA = Rm, Rn not incremented]
        F_MAC_L_AT_RM_INC_AT_RN_INC: begin u.mrd = 1; u.msz = 2'd2; u.maddr = u.step == 3'd0 ? u.b : u.a; sz = 32'd4; end
        F_MAC_W_AT_RM_INC_AT_RN_INC: begin u.mrd = 1; u.msz = 2'd1; u.msx = 1; u.maddr = u.step == 3'd0 ? u.b : u.a; sz = 32'd2; end
        // FP loads and stores (SZ = 1: 64 bits, FR(2n) at the lower address)
        F_FMOV_S_AT_RM_FRN, F_FMOV_S_AT_RM_INC_FRN: begin u.mrd = 1; u.msz = 2'd2; u.maddr = u.a; sz = 32'd4; end
        F_FMOV_S_AT_R0_RM_FRN: begin u.mrd = 1; u.msz = 2'd2; u.maddr = u.a + u.c; end
        F_FMOV_AT_RM_DRN, F_FMOV_AT_RM_INC_DRN, F_FMOV_AT_RM_XDN, F_FMOV_AT_RM_INC_XDN: begin
          u.mrd = 1; u.msz = 2'd3; u.maddr = u.a; sz = 32'd8;
        end
        F_FMOV_AT_R0_RM_DRN, F_FMOV_AT_R0_RM_XDN: begin u.mrd = 1; u.msz = 2'd3; u.maddr = u.a + u.c; end
        F_FMOV_S_FRM_AT_RN, F_FMOV_S_FRM_AT_PRE_RN, F_FMOV_S_FRM_AT_R0_RN: begin
          u.mwr = 1; u.msz = 2'd2;
          u.maddr = u.form == F_FMOV_S_FRM_AT_RN ? u.b : u.form == F_FMOV_S_FRM_AT_PRE_RN ? u.b - 32'd4 : u.b + u.c;
          u.mwd = {32'd0, frd_ex(int'(fm))};
        end
        F_FMOV_DRM_AT_RN, F_FMOV_DRM_AT_PRE_RN, F_FMOV_DRM_AT_R0_RN, F_FMOV_XDM_AT_RN, F_FMOV_XDM_AT_PRE_RN,
        F_FMOV_XDM_AT_R0_RN: begin
          logic [4:0] p;
          p = {(u.form == F_FMOV_XDM_AT_RN || u.form == F_FMOV_XDM_AT_PRE_RN || u.form == F_FMOV_XDM_AT_R0_RN)
               ? ~u.md[2] : u.md[2], u.op[7:5], 1'b0};
          u.mwr = 1; u.msz = 2'd3;
          u.maddr = (u.form == F_FMOV_DRM_AT_RN || u.form == F_FMOV_XDM_AT_RN) ? u.b :
                    (u.form == F_FMOV_DRM_AT_PRE_RN || u.form == F_FMOV_XDM_AT_PRE_RN) ? u.b - 32'd8 : u.b + u.c;
          u.mwd = {frd_ex(int'(p) + 1), frd_ex(int'(p))};    // FR(2n) at the lower address
        end
        // cache-block operations: the address only (@Rn, Rn = op[11:8]); the size is not used [P: longword]
        F_PREF_AT_RN, F_OCBI_AT_RN, F_OCBP_AT_RN, F_OCBWB_AT_RN: begin u.msz = 2'd2; u.maddr = u.b; end
        default: ;
      endcase
      // ---- the access kind of this micro-op (SOC.md 3.0, 4.2): what it asks of the memory system in MA.  The
      // read-modify-write forms read in micro-op 0 and write in their last; TAS.B purges, reads, writes (C5).  Every
      // access is an architectural UTLB access (arch: TAS.B's three included).
      u.dk = DK_NONE;
      if (u.mrd)
        u.dk = (u.form == F_TST_B_IMM_AT_R0_GBR || u.form == F_AND_B_IMM_AT_R0_GBR || u.form == F_OR_B_IMM_AT_R0_GBR ||
                u.form == F_XOR_B_IMM_AT_R0_GBR) ? DK_RMW_RD : u.form == F_TAS_B_AT_RN ? DK_TAS_RD : DK_LD;
      else if (u.mwr)
        u.dk = u.form == F_MOVCA_L_R0_AT_RN ? DK_MOVCA : u.form == F_TAS_B_AT_RN ? DK_TAS_WR :
               (u.form == F_AND_B_IMM_AT_R0_GBR || u.form == F_OR_B_IMM_AT_R0_GBR || u.form == F_XOR_B_IMM_AT_R0_GBR)
               ? DK_RMW_WR : DK_ST;
      else
        unique case (u.form)
          F_PREF_AT_RN:  u.dk = DK_PREF;
          F_OCBI_AT_RN:  u.dk = DK_OCBI;
          F_OCBP_AT_RN:  u.dk = DK_OCBP;
          F_OCBWB_AT_RN: u.dk = DK_OCBWB;
          F_TAS_B_AT_RN: u.dk = u.step == 3'd0 ? DK_TAS_PURGE : DK_NONE;
          default: ;
        endcase
      u.arch = u.v && u.dk != DK_NONE;
      // address register updates (@Rm+ post-increment, @-Rn pre-decrement), produced in EX
      if (u.w1 && u.w1k == GR_EX && d_is_upd(u.form)) begin
        unique case (u.form)
          F_MOV_B_RM_AT_PRE_RN, F_MOV_W_RM_AT_PRE_RN, F_MOV_L_RM_AT_PRE_RN, F_FMOV_S_FRM_AT_PRE_RN,
          F_FMOV_DRM_AT_PRE_RN, F_FMOV_XDM_AT_PRE_RN, F_STC_L_SR_AT_PRE_RN, F_STC_L_GBR_AT_PRE_RN,
          F_STC_L_VBR_AT_PRE_RN, F_STC_L_SSR_AT_PRE_RN, F_STC_L_SPC_AT_PRE_RN, F_STC_L_SGR_AT_PRE_RN,
          F_STC_L_DBR_AT_PRE_RN, F_STC_L_RM_BANK_AT_PRE_RN, F_STS_L_PR_AT_PRE_RN, F_STS_L_MACH_AT_PRE_RN,
          F_STS_L_MACL_AT_PRE_RN, F_STS_L_FPUL_AT_PRE_RN, F_STS_L_FPSCR_AT_PRE_RN: u.w1v = u.maddr;
          default: u.w1v = u.maddr + sz;                      // post-increment: the address plus the size
        endcase
      end
      // ---- system-register values written at S (from a GPR; the .L forms get theirs in MA)
      unique case (u.form)
        // LDC / LDS Rm,<reg>: Rm is op[11:8] (operand b)
        F_LDC_RM_SR: u.crv = b & 32'h7000_83F3;
        F_LDC_RM_GBR, F_LDC_RM_VBR, F_LDC_RM_SSR, F_LDC_RM_SPC, F_LDC_RM_DBR, F_LDC_RM_RN_BANK, F_LDS_RM_PR: u.crv = b;
        F_BSR_LABEL, F_JSR_AT_RN, F_BSRF_RN: u.crv = u.pc + 32'd4;
        F_RTE: u.crv = u.cr;                                   // SR <- SSR
        default: ;
      endcase
      // ---- GPR results that are system-register copies (STC / STS PR), produced in EX
      unique case (u.form)
        F_STC_SR_RN, F_STC_GBR_RN, F_STC_VBR_RN, F_STC_SSR_RN, F_STC_SPC_RN, F_STC_SGR_RN, F_STC_DBR_RN,
        F_STC_RM_BANK_RN, F_STS_PR_RN: u.w1v = u.cr;
        default: ;
      endcase
      // ---- LS-pipe FP results known in EX (moves, constants, sign logic, FPUL transfers)
      frv = '0;
      unique case (u.form)
        F_FLDI0_FRN: u.fwe = 32'h0000_0000;
        F_FLDI1_FRN: u.fwe = 32'h3F80_0000;
        F_FMOV_FRM_FRN: frv = frd_ex(int'(fm));
        F_FABS_FRN: begin frv = frd_ex(int'(fn)); frv[31] = 1'b0; end
        F_FNEG_FRN: begin frv = frd_ex(int'(fn)); frv[31] = ~frv[31]; end
        F_FABS_DRN: begin frv = frd_ex(int'({u.md[2], u.op[11:9], 1'b0})); frv[31] = 1'b0; end
        F_FNEG_DRN: begin
          logic [31:0] t;
          t = frd_ex(int'({u.md[2], u.op[11:9], 1'b0}));
          frv = {~t[31], t[30:0]};
        end
        F_FSTS_FPUL_FRN: frv = fpul_ex();
        default: ;
      endcase
      unique case (u.form)
        F_FMOV_FRM_FRN, F_FABS_FRN, F_FNEG_FRN, F_FSTS_FPUL_FRN: begin u.fwe = frv; u.fwo = frv; end
        F_FLDI0_FRN, F_FLDI1_FRN: u.fwo = u.fwe;
        F_FABS_DRN, F_FNEG_DRN: u.fwe = frv;
        F_FMOV_DRM_DRN, F_FMOV_XDM_DRN, F_FMOV_DRM_XDN, F_FMOV_XDM_XDN: begin
          logic [4:0] p;
          p = {(u.form == F_FMOV_XDM_DRN || u.form == F_FMOV_XDM_XDN) ? ~u.md[2] : u.md[2], u.op[7:5], 1'b0};
          u.fwe = frd_ex(int'(p)); u.fwo = frd_ex(int'(p) + 1);
        end
        F_FLDS_FRM_FPUL: u.uv = frd_ex(int'(fn));      // FLDS FRm,FPUL: the register field is op[11:8]
        F_LDS_RM_FPUL: u.uv = b;
        F_LDS_RM_FPSCR: u.fscv = b & 32'h003F_FFFF;
        default: ;
      endcase
      // ---- JMP-class target (micro-op 0), taken over by the D stage in the branch's second D cycle
      if (u.v && l == 0 && u.step == 3'd0) begin
        unique case (u.form)
          F_JMP_AT_RN, F_JSR_AT_RN: jmp_tgt_ex = b;
          F_BRAF_RN, F_BSRF_RN: jmp_tgt_ex = u.pc + 32'd4 + b;
          F_RTS: jmp_tgt_ex = u.cr;
          default: ;
        endcase
      end
      ex_res[l] = u;
      if (l == 0) e0res = u;
    end
  end

  function automatic logic d_is_upd(input form_e f);
    unique case (f)
      F_MOV_B_AT_RM_INC_RN, F_MOV_W_AT_RM_INC_RN, F_MOV_L_AT_RM_INC_RN, F_MOV_B_RM_AT_PRE_RN, F_MOV_W_RM_AT_PRE_RN,
      F_MOV_L_RM_AT_PRE_RN, F_FMOV_S_AT_RM_INC_FRN, F_FMOV_S_FRM_AT_PRE_RN, F_FMOV_AT_RM_INC_DRN, F_FMOV_AT_RM_INC_XDN,
      F_FMOV_DRM_AT_PRE_RN, F_FMOV_XDM_AT_PRE_RN, F_LDC_L_AT_RM_INC_SR, F_LDC_L_AT_RM_INC_GBR, F_LDC_L_AT_RM_INC_VBR,
      F_LDC_L_AT_RM_INC_SSR, F_LDC_L_AT_RM_INC_SPC, F_LDC_L_AT_RM_INC_DBR, F_LDC_L_AT_RM_INC_RN_BANK,
      F_LDS_L_AT_RM_INC_MACH, F_LDS_L_AT_RM_INC_MACL, F_LDS_L_AT_RM_INC_PR, F_LDS_L_AT_RM_INC_FPSCR,
      F_LDS_L_AT_RM_INC_FPUL, F_STC_L_SR_AT_PRE_RN, F_STC_L_GBR_AT_PRE_RN, F_STC_L_VBR_AT_PRE_RN,
      F_STC_L_SSR_AT_PRE_RN, F_STC_L_SPC_AT_PRE_RN, F_STC_L_SGR_AT_PRE_RN, F_STC_L_DBR_AT_PRE_RN,
      F_STC_L_RM_BANK_AT_PRE_RN, F_STS_L_PR_AT_PRE_RN, F_STS_L_MACH_AT_PRE_RN, F_STS_L_MACL_AT_PRE_RN,
      F_STS_L_FPUL_AT_PRE_RN, F_STS_L_FPSCR_AT_PRE_RN, F_MAC_L_AT_RM_INC_AT_RN_INC, F_MAC_W_AT_RM_INC_AT_RN_INC:
        return 1'b1;
      default: return 1'b0;
    endcase
  endfunction

  // ---- the EX address phase (C2, SOC.md 4.2): the access micro-op in EX drives `dex`; v1_mmu answers `dxl` in the
  // same cycle, and the UBC its operand-address compare
  always_comb begin
    dex = '0;
    ubc_opq = '0;
    for (int l = 0; l < 2; l++)
      if (ex[l].v && ex_res[l].dk != DK_NONE) begin
        dex = '{v: 1'b1, kind: ex_res[l].dk, size: ex_res[l].msz, va: ex_res[l].maddr, md: ex_smd[l],
                rmw: ex[l].step == 3'd0 && v1_ictl(ex[l].form).rmw, arch: ex_res[l].arch};
        // the SOC.md 3.8 per-kind table: LD / ST their size with data; PREF / OCBP / OCBWB read and OCBI write, size L,
        // no data; MOVCA.L a write of size L with data (R0); RMW / TAS read / write size B; a quadword FMOV one Q
        // access; TAS.B's purge presents nothing (v = 0).  (The EX view: msz is L for PREF / OCBx and MOVCA.L, B for the
        // RMW / TAS forms; nodata = dk_ctl, which MOVCA is not.)
        ubc_opq = '{v: ex_res[l].dk != DK_TAS_PURGE, wr: dk_write(ex_res[l].dk), size: ex_res[l].msz,
                    nodata: dk_ctl(ex_res[l].dk), va: ex_res[l].maddr};
      end
  end

  // ---- the EX checks (SOC.md 4.2, C2 / C4 / C5): the core's address error, else the MMU's TLB fault; the faulting
  // micro-op carries fx / fwr / ftea / fvec400 to MA, where it makes no access and the commit point takes it.
  //   * alignment: W odd, L & 3, Q & 7 (MOVCA.L is L); never for OCBI / OCBP / OCBWB / PREF / TAS.B [HW];
  //   * user mode (MD = 0): VA >= 0x80000000, except the SQ window with SQMD = 0; the SQ window with SQMD = 1 [HW];
  //   * PREF (C4): no alignment check, no TLB fault outside the SQ window (a failed translation there drops the PREF
  //     silently: no access), the user-mode error as read type [HW u_pref_p1];
  //   * write type per dk_write; the first RMW / TAS.B access checks write permission (rmw), so its protection and
  //     initial-page-write faults are write type (C5), a miss read type [HW mmu];
  //   * the UBC operand-address tags {A, B} (debug-ubc-hudi 4.2: compared in EX against the live registers).
  // Also the flags before this micro-op's end-of-EX write (fl_prev, for the S-stage take's restore), in v1's write
  // order: the S-stage SR write, lane 0's FCMP T and flag writes, lane 1's (`ex_flp`, a block of its own: it reads
  // v1_fpu's F1 T, and v1_fpu's EX view reads ex_fin's faults; the pipeline latch MA takes both).
  logic [3:0] ex_flp [2];
  always_comb begin
    logic [3:0] fl;                              // {T, S, Q, M}
    fl = {sr[0], sr[1], sr[8], sr[9]};
    for (int l = 0; l < 2; l++) if (sl[l].v && sl[l].uk == UK_NORM && sl[l].crw == CR_SR)
      fl = {sl[l].crv[0], sl[l].crv[1], sl[l].crv[8], sl[l].crv[9]};
    for (int l = 0; l < 2; l++) begin
      ex_flp[l] = fl;
      if (fpu_t_we && fpu_t_lane == l[0]) fl[3] = fpu_t_val;
      if (ex_res[l].tw) fl[3] = ex_res[l].tv;
      if (ex_res[l].sw) fl[2] = ex_res[l].sv;
      if (ex_res[l].qmw) begin fl[1] = ex_res[l].qv; fl[0] = ex_res[l].mv; end
    end
  end
  always_comb begin
    logic       sqw, mis, prv;
    fx_e        f;
    uop_t       u;
    sqw = 1'b0; mis = 1'b0; prv = 1'b0; f = FX_NONE; u = '0;
    for (int l = 0; l < 2; l++) begin
      u = ex_res[l];
      if (ex[l].v && u.dk != DK_NONE && u.uk == UK_NORM) begin
        sqw = u.maddr[31:26] == 6'b111000;
        mis = !(dk_ctl(u.dk) || u.dk == DK_TAS_RD || u.dk == DK_TAS_WR) &&
              ((u.msz == 2'd1 && u.maddr[0]) || (u.msz == 2'd2 && u.maddr[1:0] != 2'd0) ||
               (u.msz == 2'd3 && u.maddr[2:0] != 3'd0));
        prv = !ex_smd[l] && (sqw ? sqmd : u.maddr[31]);
        f = FX_NONE;
        if (mis || prv) f = FX_ADDR;
        else if (!(u.dk == DK_PREF && !sqw)) f = dxl.fx;
        else if (dxl.fx != FX_NONE) u.dk = DK_NONE;              // a PREF that fails translation: dropped
        if (f != FX_NONE) begin
          u.fx = f;
          u.fwr = dk_write(u.dk) || (dex.rmw && (f == FX_TLB_PROT || f == FX_IPW));
          u.fvec400 = f == FX_TLB_MISS && dxl.vec400;
          u.ftea = u.maddr;
        end
        if (u.dk != DK_TAS_PURGE) u.ubc_op = {ubc_opm.a, ubc_opm.b};
      end
      ex_fin[l] = u;
    end
  end
  // Same-cycle gating (SOC.md 4.2 [HW exceptions-intc 7.3: a paired SETT and FCMP's T were cancelled]): a fault or a
  // break known at the end of EX in lane 0 -- the data fault, the UBC operand-address tag, a post-execution tag --
  // stops the end-of-EX flag writes of the younger lane-1 micro-op and FCMP's F1 T in lane 1 in that cycle.
  // A fault found in EX also stops the S-stage writes of the same instruction's earlier micro-ops in that cycle: STC.L
  // SGR's Rn - 4 (micro-op 0) is in S when its store micro-op is in EX (the others' are in S at the take, SOC.md
  // 4.3.1), so re-execution finds Rn unchanged [HW exceptions-intc 3.3: @-Rn unchanged]
  logic       exf_v;
  logic [7:0] exf_isn;
  always_comb begin
    exf_v = 1'b0; exf_isn = '0;
    for (int l = 0; l < 2; l++)
      if (ex[l].v && ex_fin[l].uk == UK_NORM && ex_fin[l].fx != FX_NONE && !exf_v) begin
        exf_v = 1'b1; exf_isn = ex[l].isn;
      end
  end
  wire ex_stop1 = ex[0].v && ex_fin[0].uk == UK_NORM &&
                  (ex_fin[0].fx != FX_NONE ||
                   ((((ex_fin[0].ubc_op[1] || ex_fin[0].ubc_post[1]) && !ubc_seq) ||
                     (((ex_fin[0].ubc_op[0] && !ubc_ctl.dbeb) || ex_fin[0].ubc_post[0]) && ubc_ctl.b_live)) &&
                    !sr_rd[28]));
  // The F1 operand trap (SOC.md 4.3.2; v1_fpu's trap_f1, WI-CORE-F): taken in an unheld cycle with no older take (S,
  // MA); a lane-1 FE op whose lane-0 partner faults or breaks in EX is stopped like its flag writes (ex_stop1: the
  // partner takes at MA next cycle and kills it) [HW exceptions-intc 7.3; P for the trap itself].  The EX micro-op
  // becomes EXC0 in place (the advance below), kill > K, D does not issue, fetch stops.
  assign ftake = trap_f1 && !frz && !stake_c && !ma_tk[0] && !ma_tk[1] && !(trap_f1_lane && ex_stop1);

  // ============================================================================================================
  // MA stage: the data memory (one access per cycle: two LS instructions never pair) and the FPU -> CPU transfers.
  // The access micro-op presents `lreq` (SOC.md 3.2.2): once, identical until its ack, never after it.  The ack's data
  // is taken in the ack cycle and also latched (`ma_done` in the micro-op, the data in `ma_rdq`), so an ack that
  // arrives while the pipeline holds (OCBWB's port collision, `run` = 0) is kept until MA advances.  Load data
  // leaves MA at the end of the cycle it is valid in: the ack cycle, or any later cycle with `ma_done`.
  // A faulting access makes no request, except an SQ store with a TLB fault, which still writes the queue [HW mmu 3.8].
  // A lane-1 access whose lane-0 partner takes at MA in the same cycle makes none either: the take (a UBC post-execution
  // break on the non-access leader; TRAPA and an access leader never pair with one) is completion type and kills the
  // follower (kill > K, SOC.md 4.3.2), so its store must not happen, its cache op must not act and URC must not count
  // it, and the take does not wait for its ack (no `mah`) [SOC.md 3.2.2 rule 1; WI-CORE-A F1].
  uop_t ma_res [2];
  logic [31:0] mac_ld_a, mac_ld_b, def_ld;      // load data for the multiplier (MAC operands, LDS.L MACx)
  logic        mac_ld_a_v, mac_ld_b_v, def_ld_v;
  dxlat_t      ma_x;                            // the MMU's answer for the MA access, latched at the end of its EX
  logic [63:0] ma_rdq;                          // the acked data of the MA access (valid with ma_done)
  logic        ma_acc [2];                      // this lane of MA holds the access micro-op
  logic [63:0] ma_rd;                           // the MA access's data this cycle (live ack or latched)
  logic        ma_dv;                           // ma_rd is valid (acked now or before)
  always_comb begin
    lreq = '0;
    for (int l = 0; l < 2; l++) begin
      ma_acc[l] = ma[l].v && ma[l].dk != DK_NONE;
      if (ma_acc[l]) begin
        lreq.v     = (ma[l].fx == FX_NONE ||
                      (ma[l].dk == DK_ST && ma[l].maddr[31:26] == 6'b111000 && ma[l].fx != FX_ADDR &&
                       ma[l].fx != FX_MHIT)) && !ma[l].ma_done && !kill_ma && !(l == 1 && ma_tkb[0]);
        lreq.kind  = ma[l].dk;
        lreq.size  = ma[l].msz;
        lreq.va    = ma[l].maddr;
        lreq.x     = ma_x;
        lreq.wdata = ma[l].mwd;
        lreq.be    = ma[l].msz == 2'd0 ? 8'h01 : ma[l].msz == 2'd1 ? 8'h03 : ma[l].msz == 2'd2 ? 8'h0F : 8'hFF;
        lreq.isn   = ma[l].isn;
        lreq.arch  = ma[l].arch;
      end
    end
  end
  always_comb begin
    ma_rd = lrsp.rdata; ma_dv = 1'b0;
    for (int l = 0; l < 2; l++)
      if (ma_acc[l]) begin
        if (ma[l].ma_done) ma_rd = ma_rdq;
        ma_dv = ma[l].ma_done || (lreq.v && lrsp.ack);
      end
    ma_commit = lreq.v && lrsp.ack && lreq.arch;
  end
  // the MA take: LDC.L @Rm+,SR's operand break tests SR.BL after the instruction, the BL of the word it loads (hm
  // 20.3.9 (3) c / d), so it is decided with the MA data, valid in the unheld cycle a take needs (`matake` is gated by
  // frz, which holds while the access waits for its ack).  A block of its own, after the MA request: lane 1's request
  // is gated by lane 0's `ma_tkb` -- an SR load is CO and never has a lane-1 partner, so the data-decided part can
  // never gate one, and reading it there would loop through the answer
  always_comb for (int l = 0; l < 2; l++) ma_tk[l] = ma_tkb[l] || (ma_ldb[l] && !ma_rd[28]);

  always_comb begin
    mac_ld_a_v = 1'b0; mac_ld_b_v = 1'b0; def_ld_v = 1'b0; mac_ld_a = '0; mac_ld_b = '0; def_ld = '0;
    for (int l = 0; l < 2; l++) begin
      uop_t u;
      logic [31:0] ld;
      u = ma[l];
      unique case (u.msz)
        2'd0: ld = u.msx ? {{24{ma_rd[7]}}, ma_rd[7:0]} : {24'd0, ma_rd[7:0]};
        2'd1: ld = u.msx ? {{16{ma_rd[15]}}, ma_rd[15:0]} : {16'd0, ma_rd[15:0]};
        default: ld = ma_rd[31:0];
      endcase
      if (u.v && u.mrd && !kma[l]) begin
        if (u.w2) u.w2v = ld;
        unique case (u.form)
          F_LDC_L_AT_RM_INC_SR: u.crv = ld & 32'h7000_83F3;
          F_LDC_L_AT_RM_INC_GBR, F_LDC_L_AT_RM_INC_VBR, F_LDC_L_AT_RM_INC_SSR, F_LDC_L_AT_RM_INC_SPC,
          F_LDC_L_AT_RM_INC_DBR, F_LDC_L_AT_RM_INC_RN_BANK, F_LDS_L_AT_RM_INC_PR: u.crv = ld;
          F_LDS_L_AT_RM_INC_FPUL: u.uv = ld;
          F_LDS_L_AT_RM_INC_FPSCR: u.fscv = ld & 32'h003F_FFFF;
          F_LDS_L_AT_RM_INC_MACH, F_LDS_L_AT_RM_INC_MACL: begin def_ld_v = 1'b1; def_ld = ld; end
          F_MAC_L_AT_RM_INC_AT_RN_INC, F_MAC_W_AT_RM_INC_AT_RN_INC: begin
            if (u.step == 3'd0) begin mac_ld_a_v = 1'b1; mac_ld_a = ld; end
            else begin mac_ld_b_v = 1'b1; mac_ld_b = ld; end
          end
          F_FMOV_S_AT_RM_FRN, F_FMOV_S_AT_RM_INC_FRN, F_FMOV_S_AT_R0_RM_FRN: begin u.fwe = ld; u.fwo = ld; end
          F_FMOV_AT_RM_DRN, F_FMOV_AT_RM_INC_DRN, F_FMOV_AT_RM_XDN, F_FMOV_AT_RM_INC_XDN, F_FMOV_AT_R0_RM_DRN,
          F_FMOV_AT_R0_RM_XDN: begin u.fwe = ma_rd[31:0]; u.fwo = ma_rd[63:32]; end
          default: ;
        endcase
      end
      // FPU / MAC -> CPU transfers are sampled here (their Rn is written at S with no bypass before)
      unique case (u.form)
        F_STS_MACH_RN: u.w1v = mach_now;
        F_STS_MACL_RN: u.w1v = macl_now;
        F_STS_FPUL_RN: u.w1v = fpul_now;
        F_STS_FPSCR_RN: u.w1v = fpscr_now;
        default: ;
      endcase
      ma_res[l] = u;
    end
  end

  // ============================================================================================================
  // LDTLB's UTLB write (SOC.md 4.2): LDTLB (CO, no data access) occupies MA for 3 cycles; `ldtlb` strobes v1_mmu in the
  // first, which writes UTLB[URC] at its end, and the write holds the whole pipeline in the first two (measured 2).
  // lt_n counts LDTLB's MA cycles (it runs while the freeze it causes holds everything else).
  wire lt_ma = ma[0].v && ma[0].form == F_LDTLB;
  assign lt_frz = lt_ma && lt_n != 2'd2;
  assign ldtlb  = run && !rst_c && lt_ma && lt_n == 2'd0 && !kill_ma;

  // ============================================================================================================
  // pipeline advance and write-back
  always_ff @(posedge clk) begin
    rst_q <= rst_c;
    if (rst_c) begin
      for (int l = 0; l < 2; l++) begin ex[l] <= '0; ma[l] <= '0; sl[l] <= '0; end
      sq_on <= 1'b0; sq_step <= '0; sq_k <= '0; lt_n <= '0; s_dq <= '0; s_dq_l <= 1'b0;
      // the ISN runs on through every reset but the power-on one: the core's own resets (SR.BL = 1, multiple hit) are
      // commit-log records in lockstep (SOC.md 6.2, 6.3), and the instructions after one must not reuse the ISNs of the
      // expectations still open before it
      if (rst.por) isn_ctr <= '0;
      sleep_issued <= 1'b0;
      ent_act <= 1'b0; ent_pend <= 1'b0; ent_rc <= '0; exc_rd <= 1'b0; exc_sv <= 1'b0; rst_wait <= 1'b0;
      ent_rst <= '0; ent_intr <= 1'b0; ent_nmi <= 1'b0;
      slot0_q <= 1'b0; post_carry <= '0; dbrk_pend <= 1'b0;
      // the reset values (SOC.md 2.3, exceptions-intc 3.2 / 3.16): SR = 0x700000F0 (MD RB BL, IMASK 15, FD 0), VBR = 0,
      // loaded in a reset's first cycle; the backdoor (the core-only harness's register load) writes after them.
      // PC = reset_pc is v1_fetch's; FPSCR = 0x00040001 is v1_fpu's (its own reset's first cycle)
      if (!rst_q) begin sr <= RESET_SR; vbr <= '0; end
      if (dbg_we) begin
        if (dbg_addr < 8'd16) gpr[gphys(dbg_addr[3:0], sr[30] & sr[29])] <= dbg_wdata;
        else if (dbg_addr < 8'd24) gpr[gbank(dbg_addr[2:0], sr[30] & sr[29])] <= dbg_wdata;
        else unique case (dbg_addr)
          8'd24: sr <= dbg_wdata;
          8'd25: gbr <= dbg_wdata;
          8'd26: vbr <= dbg_wdata;
          8'd27: ssr <= dbg_wdata;
          8'd28: spc <= dbg_wdata;
          8'd29: sgr <= dbg_wdata;
          8'd30: dbr <= dbg_wdata;
          8'd33: pr <= dbg_wdata;
          default: ;                                           // 34 (RESET_PC) is the reset_pc input's
        endcase
      end
    end else begin
      // ---- the MA port (SOC.md 3.2.2 rule 2).  The unit runs on its own clock, so its ack is taken whatever the core's
      // own state: in a cycle that holds (the OCBWB port collision, or run = 0) the ack is latched in the micro-op
      // (`ma_done`, which drops `lreq.v`: the unit never sees the request again) with its data (`ma_rdq`), and the
      // micro-op leaves MA with them when the pipeline next advances.  These flops are not under the `run` enable.
      for (int l = 0; l < 2; l++)
        if (frz && ma_acc[l] && lreq.v && lrsp.ack) ma[l].ma_done <= 1'b1;
      if (lreq.v && lrsp.ack) ma_rdq <= lrsp.rdata;
    end
    if (!rst_c && run) begin                                   // cpu_run is the clock enable of everything else
      // ---- SLEEP (SOC.md 4.4.3; CO, so always lane 0): asleep from its issue; any take ends it -- an older take kills
      // the SLEEP, the waking interrupt completes it (SPC = its continuation), a completion-type break on the SLEEP
      // itself resumes after it [P, SOC.md 9.18: must not hang]
      if (iss0 && c[0].form_x == XF_SLEEP) begin sleep_issued <= 1'b1; slp_cont <= cont_d[0]; end
      if (tk && !frz) sleep_issued <= 1'b0;
      // ---- the issue sequence number
      if (tk && !frz) isn_ctr <= tk_isn + 8'd1;
      else if (exc_sv && !frz && !f_busy_wait) isn_ctr <= isn_ctr + 8'd1;     // EXC_SAVE takes one
      else isn_ctr <= isn_ctr + 8'(pop);
      // ---- the sequencer
      if (iss0 && k0 > 3'd1) begin
        sq_on <= 1'b1; sq_step <= 3'd1; sq_k <= k0;
        sq_pc <= q_pc[0]; sq_op <= q_op[0]; sq_form <= d[0].form; sq_c <= c[0]; sq_d <= d[0];
        sq_md <= md_dec; sq_isn <= isn_ctr; sq_smd <= sr_rd[30]; sq_sbl <= sr_rd[28];
        sq_slot <= in_slot[0]; sq_cont <= cont_d[0]; sq_post <= tg_post[0] | (in_slot[0] ? post_carry : 2'b00);
        if (d[0].form == F_RTE) sq_target <= spc_rd;
      end else if (sq_kill && !frz) sq_on <= 1'b0;
      else if (sq_on && !frz) begin
        if (sq_step == sq_k - 3'd1) sq_on <= 1'b0;
        sq_step <= sq_step + 3'd1;
      end
      // ---- LDTLB's MA occupancy
      if (!frz) lt_n <= 2'd0;
      else if (lt_frz) lt_n <= lt_n + 2'd1;
      if (!frz) ma_x <= dxl;                                   // the EX access's translation goes to MA with it
      // the address of the read-modify-write forms, from micro-op 0 in EX
      for (int l = 0; l < 2; l++)
        if (ex[l].v && ex[l].step == 3'd0 && (ex[l].form == F_TST_B_IMM_AT_R0_GBR || ex[l].form == F_AND_B_IMM_AT_R0_GBR ||
            ex[l].form == F_OR_B_IMM_AT_R0_GBR || ex[l].form == F_XOR_B_IMM_AT_R0_GBR || ex[l].form == F_TAS_B_AT_RN ||
            stcl_form(ex[l].form))) begin
          sq_addr <= ex_res[l].maddr;
          sq_data <= ex[l].cr;
        end
      for (int l = 0; l < 2; l++)
        if (ma[l].v && ma[l].mrd && ma_dv && (ma[l].dk == DK_RMW_RD || ma[l].dk == DK_TAS_RD))
          rmw_tmp <= {24'd0, ma_rd[7:0]};

      if (!frz) begin
        // ---- write-back (S): lane 1 is younger and wins.  A micro-op killed this cycle writes nothing (an earlier
        // micro-op of a faulting two-access instruction: MAC's @Rn+, STC.L's Rn - 4)
        for (int l = 0; l < 2; l++) begin
          if (sl[l].v && sl[l].uk == UK_NORM && !ksl[l] && !(exf_v && sl[l].isn == exf_isn)) begin
            if (sl[l].w1) gpr[sl[l].w1r] <= sl[l].w1v;
            if (sl[l].w2) gpr[sl[l].w2r] <= sl[l].w2v;
            unique case (sl[l].crw)
              CR_SR:   sr <= sl[l].crv;
              CR_GBR:  gbr <= sl[l].crv;
              CR_VBR:  vbr <= sl[l].crv;
              CR_SSR:  ssr <= sl[l].crv;
              CR_SPC:  spc <= sl[l].crv;
              CR_DBR:  dbr <= sl[l].crv;
              CR_PR:   pr <= sl[l].crv;
              CR_BANK: gpr[sl[l].bkr] <= sl[l].crv;
              default: ;
            endcase
          end
          // EXC0 at S (SOC.md 4.3.4 item 4): SSR / SGR as every older writer left them (write-through); EXC_SAVE at S
          // (item 8): SSR, SPC, SGR
          if (sl[l].v && sl[l].uk == UK_EXC0 && !ksl[l] && ent_rst == 2'd0) begin
            ssr_hold <= sr_pre; sgr_hold <= gpr_rd[5'd23]; spc_hold <= ent_spc; sv_isn <= ent_isn;
          end
          if (sl[l].v && sl[l].uk == UK_EXC_SAVE) begin ssr <= ssr_hold; spc <= spc_hold; sgr <= sgr_hold; end
        end
        // ---- flags: CPU writers at the end of EX, FCMP at the end of F1 (the younger of the same issue cycle wins).
        // A killed EX micro-op writes none; a lane-0 fault or break stops lane 1's (ex_stop1).  EXC0 at S sets MD RB
        // BL.  The UBC data break at S restores the flags the killed MA group wrote last cycle (fl_prev of the oldest
        // one that wrote) [P]; so does the deferred data break at its slot for a killed partner of the slot in MA (a
        // not-taken BT/S / BF/S slot paired with the next instruction, whose flags landed before the break was known).
        begin
          logic [31:0] nsr;
          logic        rs;
          nsr = sr;
          for (int l = 0; l < 2; l++) if (sl[l].v && sl[l].uk == UK_NORM && !ksl[l] && sl[l].crw == CR_SR) nsr = sl[l].crv;
          for (int l = 0; l < 2; l++)
            if (sl[l].v && sl[l].uk == UK_EXC0 && !ksl[l] && ent_rst == 2'd0) nsr[30:28] = 3'b111;
          rs = 1'b0;
          if (stake || tk_dbs)
            for (int l = 0; l < 2; l++)
              if (!rs && kma[l] && (ma[l].tw || ma[l].qmw || ma[l].sw || ma[l].form == F_FCMP_EQ_FRM_FRN ||
                  ma[l].form == F_FCMP_GT_FRM_FRN)) begin
                rs = 1'b1;
                {nsr[0], nsr[1], nsr[8], nsr[9]} = ma[l].fl_prev;
              end
          if (fpu_t_we && !fpu_t_lane && !kex[0]) nsr[0] = fpu_t_val;
          for (int l = 0; l < 2; l++) begin
            if (ex[l].v && !kex[l] && !(l == 1 && ex_stop1)) begin
              if (ex_res[l].tw) nsr[0] = ex_res[l].tv;
              if (ex_res[l].qmw) begin nsr[8] = ex_res[l].qv; nsr[9] = ex_res[l].mv; end
              if (ex_res[l].sw) nsr[1] = ex_res[l].sv;
            end
            if (l == 0 && fpu_t_we && fpu_t_lane && !kex[1] && !ex_stop1) nsr[0] = fpu_t_val;
          end
          sr <= nsr;
        end
        // ---- advance (killed micro-ops leave as bubbles)
        s_dq <= '0;
        for (int l = 0; l < 2; l++) begin
          sl[l] <= kma[l] ? '0 : ma_res[l];
          ma[l] <= kex[l] ? '0 : ex_fin[l];
          if (!kex[l]) ma[l].fl_prev <= ex_flp[l];
          if (ftake && trap_f1_lane == l[0]) ma[l] <= mk_nop(UK_EXC0, tk_spc, tk_isn);   // the F1 trap's EXC0
          ex[l] <= ex_in[l];
          ex_smd[l] <= ex_in_smd[l];
          if (ma_acc[l] && !kma[l] && (dk_read(ma[l].dk) || ma[l].mrd || ma[l].mwr)) begin
            s_dq <= '{v: 1'b1, wr: ma[l].mwr, size: ma[l].msz, data: ma[l].mwr ? ma[l].mwd : ma_rd};
            s_dq_l <= l[0];
          end
        end
        // ---- delay slots: the next leader is the slot of a delayed branch that issued without it; its continuation
        // and the branch's UBC post tag go with it
        if (iss0) begin slot0_q <= 1'b0; post_carry <= 2'b00; end
        if (iss0 && c[0].br_dly && c[0].k == 3'd1 && !iss1) begin
          slot0_q <= 1'b1; dly_cont <= br_taken[0] ? br_tgt[0] : q_pc[0] + 32'd4; post_carry <= tg_post[0];
        end
        if (iss1 && c[1].br_dly) begin
          slot0_q <= 1'b1; dly_cont <= br_taken[1] ? br_tgt[1] : q_pc[1] + 32'd4; post_carry <= tg_post[1];
        end
        if (sq_last && sq_c.br_dly && !sq_kill) begin
          slot0_q <= 1'b1; dly_cont <= rd_target; post_carry <= sq_post;
        end
        // ---- a data break deferred to the slot (sdef_c): armed until the slot takes it (any take ends it)
        if (tk) dbrk_pend <= 1'b0;
        else if (sdef_c) begin dbrk_pend <= 1'b1; dbrk_hitq <= s_brk_hit; end
        // ---- the entry (SOC.md 4.3.4)
        if (tk) begin
          slot0_q <= 1'b0; post_carry <= 2'b00;
          ent_code <= tk_code; ent_vec <= tk_vec; ent_ubrk <= tk_ubrk; ent_trapa <= tk_trapa;
          ent_intr <= tk_intr; ent_nmi <= tk_nmi;
          ent_spc <= tk_spc; ent_tea_v <= tk_tea_v; ent_tea <= tk_tea; ent_tra_v <= tk_tra_v; ent_tra <= tk_tra;
          ent_isn <= tk_isn; ent_hit <= tk_hit;
          // SR.BL = 1 at t (SOC.md 4.3.7): a general exception requests the CPU-only reset instead of an entry; a TLB
          // multiple hit requests the 0x140 reset.  EXC0 still runs to S, so every older instruction has retired when
          // the request goes out (there: `exc0_s`)
          ent_rst <= tk_mhit ? 2'd2 : (tk_gen && sr_rd[28]) ? 2'd1 : 2'd0;
          exc_rd <= 1'b0; exc_sv <= 1'b0;
          ent_act <= 1'b1;
          ent_pend <= !exc0_now && !ftake;
          // the claim release: 2 steps per claimant, t the first (measured P = 2 per memory instruction: A2's c1 /
          // c1h / c1i, c1j's + 4 with two)
          ent_rc <= tk_lsc ? {tk_ncl, 1'b0} - 4'd1 : 4'd0;
        end else if (ent_pend) begin
          if (exc0_now) ent_pend <= 1'b0;
          else if (ent_rc != 4'd0) ent_rc <= ent_rc - 4'd1;  // one step per unheld cycle
        end
        if (exc0_s && !tk) begin
          if (ent_rst == 2'd0) exc_rd <= 1'b1;
          else rst_wait <= 1'b1;                               // the reset request went out: wait for rst
        end
        if (exc_redir && !tk) begin exc_rd <= 1'b0; exc_sv <= 1'b1; ent_act <= 1'b0; end
        if (exc_sv && !tk && !f_busy_wait) exc_sv <= 1'b0;
      end
    end
  end

  assign sleep_hit = sleep_issued;
  assign fetch_stop = ent_act || rst_wait;
  wire   pipe_empty = !ex[0].v && !ex[1].v && !ma[0].v && !ma[1].v && !sl[0].v && !sl[1].v;
  // run as it was last cycle (a flop outside the run enable): sleep_req is not presented in the first cycle run is back
  // after a wake, so a core that is still asleep then -- the waking request vanished during the CPG's RESYNC, and no
  // interrupt is taken at the first D cycle -- raises it again one cycle later, a new rising edge for a CPG that takes
  // the edge (v1_cpg) as for one that takes the level (SOC.md 2.5); the CPU powers down again instead of idling awake
  // with PMCTR counting [P, unmeasured; WI-CORE-A O1]
  logic  run_q;
  always_ff @(posedge clk) run_q <= rst_c || run;

  // ============================================================================================================
  // ports the SoC's other units read (SOC.md 3.4, C14): the SR the INTC compares against, with the S-stage
  // write-through; `asleep` (BL ignored for the wake, in the INTC's NMI rule and in `itake`); `sleep_req` once SLEEP has
  // issued and EX / MA / S are empty (the FPU need not drain; v1_cpg takes its rising edge), not in the cycle the
  // waking interrupt is taken and not in the first cycle after a wake (run_q); the acknowledges with the entry's
  // architectural write (EXC0's S).
  always_comb begin
    logic [1:0] h;
    exc0_s = 1'b0;
    for (int l = 0; l < 2; l++) if (sl[l].v && sl[l].uk == UK_EXC0 && !ksl[l] && !frz) exc0_s = 1'b1;
    ubc_dq   = s_dq;
    sr_imask = sr_rd[7:4];                                     // architectural SR with the S-stage write-through
    sr_bl    = sr_rd[28];
    asleep   = sleep_issued;
    sleep_req = sleep_issued && pipe_empty && !itake && run && run_q;
    irq_ack = exc0_s && ent_intr && !ent_nmi;
    nmi_ack = exc0_s && ent_intr && ent_nmi;
    // the entry's writes outside the CPU (SOC.md 3.4): EXPEVT (INTEVT for an interrupt), TRA, TEA / PTEH.VPN, at
    // EXC0's S; a multiple hit writes TEA / PTEH only, at its take, with the reset request
    excw = '0;
    if (exc0_s && ent_rst == 2'd0)
      excw = '{v: 1'b1, intr: ent_intr, code: ent_code, tra_v: ent_tra_v, tra: ent_tra, tea_v: ent_tea_v, tea: ent_tea,
               ubrk: ent_ubrk};
    cpu_rst_req = exc0_s && ent_rst == 2'd1;
    mhit_req = exc0_s && ent_rst == 2'd2;
    if (mhit_req) excw = '{v: 1'b0, intr: 1'b0, code: EXP_MHIT, tra_v: 1'b0, tra: '0, tea_v: 1'b1, tea: ent_tea, ubrk: 1'b0};
    // UBC condition-match flags: a tagged instruction reached its break point, taken or not (debug-ubc-hudi 4.3).  A
    // break that is taken sets its flags when its entry commits (EXC0's S): an older break taken meanwhile kills it and
    // its flags are never set [HW AB_sameSPC: A post on i, B pre on i + 1 -> CMFA only].  A tag that does not break
    // (SR.BL = 1, SEQ's A half, a B match with channel B not live) sets them where it is reached: a pre-execution tag
    // when its instruction issues, a post / operand tag when its micro-op leaves MA, a data match at S.
    ubc_hit = '0;
    h = 2'b00;
    if (!frz) begin
      if (exc0_s) h = ent_hit;
      if (iss0) h = h | tg_pre[0];                             // pre tags that did not break (BL = 1, SEQ's A half)
      if (iss1) h = h | tg_pre[1];
      for (int l = 0; l < 2; l++)                              // post / operand tags leaving MA without a take
        if (ma[l].v && ma[l].uk == UK_NORM && ma[l].fx == FX_NONE && !kma[l] && !(matake && ma_tk[l]))
          h = h | ma_hit[l];
      if (!stake && !sdef_c) h = h | s_brk_hit;                // a data match that did not break
      ubc_hit = '{hit_a: h[1], hit_b: h[0]};
    end
  end

  // ---- performance-counter events of the D stage (SOC.md 3.7, debug-ubc-hudi 4.7).  0x23 / 0x14 / 0x28 / 0x29 are
  // v1's four validated counters (the sweep); the others follow debug-ubc-hudi's definitions [P].
  logic br_bub;                                                // a taken PC-relative branch's target has not issued
  logic br_pcrel;                                              // a PC-relative branch redirects this cycle
  always_comb begin
    logic brc, brt, skip0;
    pmc_ev = '0;
    pmc_ev[PMC_ELAPSED]    = run;
    pmc_ev[PMC_PAR_ISSUED] = iss0 && iss1;
    pmc_ev[PMC_FRZ_REG]    = lead && !iss0_raw && cpu0;
    pmc_ev[PMC_FRZ_FPU]    = lead && !iss0_raw && !cpu0 && fhzc[0];
    pmc_ev[PMC_ISSUED]     = iss0;                             // one per issue group (a sequence counts once)
    pmc_ev[PMC_FPU_ISSUED] = (iss0 && q_op[0][15:12] == 4'hF) || (iss1 && q_op[1][15:12] == 4'hF);
    brc = 1'b0; brt = 1'b0; br_pcrel = 1'b0; skip0 = 1'b0;
    for (int s = 0; s < 2; s++) begin
      if (s == 0 ? iss0 : iss1) begin
        // a taken BT / BF with disp 0 skips one instruction and is not a branch (debug-ubc-hudi 4.7.1)
        skip0 = br_skip[s];
        if ((c[s].br_pc || c[s].br_reg) && d[s].form != F_BSR_LABEL && d[s].form != F_BSRF_RN &&
            d[s].form != F_JSR_AT_RN && d[s].form != F_RTS && d[s].form != F_RTE && !skip0) begin
          brc = 1'b1;
          brt = c[s].br_reg || br_taken[s];
        end
        pmc_ev[PMC_SUBR] = pmc_ev[PMC_SUBR] || d[s].form == F_BSR_LABEL || d[s].form == F_BSRF_RN ||
                           d[s].form == F_JSR_AT_RN;
        if (br_taken[s] && !skip0) br_pcrel = 1'b1;
      end
    end
    pmc_ev[PMC_BR_ISSUED] = brc;
    pmc_ev[PMC_BR_TAKEN]  = brt;
    pmc_ev[PMC_FRZ_BR]    = br_bub && !iss0 && !frz && !pmc_ev[PMC_FRZ_REG] && !pmc_ev[PMC_FRZ_FPU];
    pmc_ev[PMC_TRAPA]     = exc0_s && ent_trapa;               // entries count at EXC0's S (SOC.md 3.7)
    pmc_ev[PMC_INTERRUPT] = irq_ack;                           // interrupts accepted; the NMI counts in 0x17 [P]
    pmc_ev[PMC_NMI]       = nmi_ack;
    pmc_ev[PMC_IFETCH]     = f_ev;                             // v1_fetch: IC accesses (a miss's replays included)
    pmc_ev[PMC_IFETCH_ALL] = f_ev_all;                         // v1_fetch: fetch words looked up
    if (!run) pmc_ev = '0;
  end
  always_ff @(posedge clk) begin
    if (rst_c) br_bub <= 1'b0;
    else if (run) begin
      if (redirect || refetch) br_bub <= br_pcrel;
      else if (iss0 && q_tgt[0]) br_bub <= 1'b0;
    end
  end

  // ---- the commit-log tap (SOC.md 6.3; tb/soc_commit.h): what each cycle commits, in program order, from the stage
  // latches that commit it.  Slots of tap.rec (CT_CORE_N = 16):
  //   0-1    RETIRE, lane 0 then lane 1 (program order): an instruction retires at its last micro-op's S, in an unheld
  //          cycle, not killed (addr = PC, data = opcode, idx = lane).  A multi-step instruction's last micro-op is the
  //          last one its sequence issues (MAC.x 1, TST.B 2, AND.B / OR.B / XOR.B 3, TAS.B 4, STC.L / STS.L PR 1, STC.L
  //          SGR 2, every other form 0: its later steps are D cycles only); the post-execution tag's bubble never
  //          retires.  An instruction the take kills never retires; TRAPA and an instruction carrying a post-execution /
  //          operand break retire before their entry (completion type: kill > K).
  //   2-4    lane 0's S-stage writes, 5-7 lane 1's: GPR (w1, w2, a bank register: idx = the register as the
  //          instruction saw it -- 0-15 R0-R15 of its bank, 16-23 the other bank's R0-R7), SYS (SR GBR VBR SSR SPC
  //          DBR PR); exactly the writes the S stage makes (the kill and the STC.L SGR gate included).  An entry's
  //          register records carry the EXC / IRQ record's ISN: EXC0's SR (MD RB BL <- 1) in its lane, EXC_SAVE's SSR /
  //          SPC / SGR (sv_isn).  A reset the core requested writes none.
  //   8      the entry at EXC0's S: EXC / IRQ (addr = SPC, data = {take age, handler, EXPEVT / INTEVT}); for a reset
  //          the core requested (SR.BL = 1: 0x020, a multiple hit: 0x140) an EXC record with handler 0xA0000000.  The
  //          take age (data[55:44], `ent_age`) is the number of Icyc from the take (t, SOC.md 4.3.2) to this record,
  //          saturating at 4095: 3 unheld (EXC0 in EX t+1, MA t+2, S t+3), more by the claim release and the holds
  //          (SOC.md 4.3.3 F + P).  It exists for the lockstep checker, which needs the cycle an interrupt was accepted
  //          in (the INTC registers' landing window, SOC.md 6.2; M1a-verify F1); it is a tap-only counter.
  //   9      SLEEP (with its RETIRE: addr = its PC) or WAKE (the first cycle `run` is back while asleep).
  //   10-15  v1_fpu's record port `frec` (WI-CORE-F, SOC.md 1.3.2): FPR / FPUL / FPSCR / MACH / MACL, each with its
  //          writer's ISN, in the cycle the register is written (its slots: the LS pipe at S, the FE side, MACH, MACL).
  // T / S / Q / M written at the end of EX and FPSCR's cause / flags have no record (SOC.md 6.3 checker rules).
  // tap.next_isn: the ISN of the next record that steps the model (RETIRE / EXC / IRQ) after this cycle's -- the oldest
  // instruction or entry not yet retired, counting this cycle's (the stamp of the DMAW / IFILL / ITLBFILL / LDTLB
  // records, SOC.md 6.3).  It is read from the pipeline positions in program order (S, MA, EX lanes), then an entry
  // whose EXC0 has not issued, then the D stage's next ISN (EXC_SAVE's ISN is never a record: skipped).
  localparam int TR_RET = 0, TR_WR = 2, TR_ENT = 8, TR_SLP = 9, TR_FPU = 10, TR_FPU_N = CT_CORE_N - TR_FPU;
  commit_rec_t [TR_FPU_N-1:0] fpu_rec;         // v1_fpu's record port (below)
  // the entry record's take age: Icyc since the take latched the entry (the same condition as ent_code's latch: a take
  // in an unheld, running cycle), counted in every Icyc, held or not (the checker compares it with record cycles)
  logic [11:0] ent_age;
  always_ff @(posedge clk)
    ent_age <= (!rst_c && run && !frz && tk) ? 12'd1 : (ent_age == 12'hFFF ? ent_age : ent_age + 12'd1);
  logic [7:0] nx_isn;
  /* verilator lint_off UNUSEDSIGNAL */         // the functions read the fields they need
  // the last micro-op of its instruction (and not the post tag's bubble, a NOP form with step != 0)
  function automatic logic last_uop(input uop_t u);
    unique case (v1_ictl(u.form).seq)
      SQ_MAC:  return u.step == 3'd1;
      SQ_TSTB: return u.step == 3'd2;
      SQ_BOPB: return u.step == 3'd3;
      SQ_TASB: return u.step == 3'd4;
      SQ_SX2:  return u.step == (stcl_form(u.form) ? 3'd1 : 3'd0);
      SQ_SX3:  return u.step == (stcl_form(u.form) ? 3'd2 : 3'd0);
      default: return u.step == 3'd0;
    endcase
  endfunction
  function automatic logic bubble(input uop_t u);
    return u.uk == UK_NORM && u.form == F_NOP && u.step != 3'd0;
  endfunction
  /* verilator lint_on UNUSEDSIGNAL */
  // a physical GPR number -> the record's index, as an instruction whose bank is rb saw it
  function automatic logic [7:0] gview(input logic [4:0] p, input logic rbk);
    if (p[4]) return {5'd1, p[2:0]};                            // R8-R15: 8-15
    if (p[3] == rbk) return {5'd0, p[2:0]};                     // R0-R7 of the instruction's bank: 0-7
    return {5'd2, p[2:0]};                                      // the other bank's R0-R7: 16-23
  endfunction
  function automatic commit_rec_t crec(input ck_e k, input logic [7:0] isn, input logic [7:0] idx,
                                       input logic [31:0] addr, input logic [63:0] data);
    return '{v: 1'b1, kind: k, isn: isn, idx: idx, addr: addr, data: data, be: 8'd0};
  endfunction
  function automatic commit_rec_t srec(input sysreg_e r, input logic [7:0] isn, input logic [31:0] v);
    return crec(CK_SYS, isn, 8'(r), 32'd0, {32'd0, v});
  endfunction
  always_comb begin
    logic f;
    uop_t u;
    logic kil, stp;
    f = 1'b0; nx_isn = '0;
    for (int k = 0; k < 6; k++) begin
      u = k < 2 ? sl[k] : k < 4 ? ma[k - 2] : ex[k - 4];
      kil = k < 2 ? ksl[k] : k < 4 ? kma[k - 2] : kex[k - 4];
      stp = k < 2 && !frz && (u.uk == UK_EXC0 || (u.uk == UK_NORM && last_uop(u) && !bubble(u)));
      if (!f && u.v && !kil && !stp && u.uk != UK_EXC_SAVE && !bubble(u)) begin f = 1'b1; nx_isn = u.isn; end
    end
    if (!f) begin
      if (tk && !frz) nx_isn = exc0_l1 ? isn_ctr : tk_isn;   // (the leader issuing beside a lane-1 EXC0 is older; an
                                                            // F1 trap's EXC0 is the EX micro-op, found above)
      else if (ent_pend) nx_isn = ent_isn;
      // an entry (not a reset) whose EXC_SAVE has not issued: EXC_SAVE will take isn_ctr
      else nx_isn = isn_ctr + 8'((ent_act && ent_rst == 2'd0) || exc_sv);
    end
  end
  always_comb begin
    tap = '{i0_v: iss0, i0_pc: q_pc[0], i1_v: iss1, i1_pc: q_pc[1], next_isn: nx_isn,
            halted: sleep_issued && pipe_empty && fpu_idle, asleep: sleep_issued, rec: '0};
    for (int l = 0; l < 2; l++) begin
      if (!rst_c && !frz && sl[l].v && !ksl[l]) begin
        if (sl[l].uk == UK_NORM) begin
          if (last_uop(sl[l]) && !bubble(sl[l])) begin
            tap.rec[TR_RET + l] = crec(CK_RETIRE, sl[l].isn, 8'(l), sl[l].pc, {48'd0, sl[l].op});
            if (sl[l].form == F_NONE && sl[l].op == 16'h001B) tap.rec[TR_SLP] = crec(CK_SLEEP, sl[l].isn, 8'd0, sl[l].pc, '0);
          end
          if (!(exf_v && sl[l].isn == exf_isn)) begin          // (the write-back's STC.L SGR gate)
            if (sl[l].w1) tap.rec[TR_WR + 3 * l] = crec(CK_GPR, sl[l].isn, gview(sl[l].w1r, sl[l].rb), '0, {32'd0, sl[l].w1v});
            if (sl[l].w2) tap.rec[TR_WR + 3 * l + 1] = crec(CK_GPR, sl[l].isn, gview(sl[l].w2r, sl[l].rb), '0, {32'd0, sl[l].w2v});
            unique case (sl[l].crw)
              CR_SR:   tap.rec[TR_WR + 3 * l + 2] = srec(SY_SR, sl[l].isn, sl[l].crv);
              CR_GBR:  tap.rec[TR_WR + 3 * l + 2] = srec(SY_GBR, sl[l].isn, sl[l].crv);
              CR_VBR:  tap.rec[TR_WR + 3 * l + 2] = srec(SY_VBR, sl[l].isn, sl[l].crv);
              CR_SSR:  tap.rec[TR_WR + 3 * l + 2] = srec(SY_SSR, sl[l].isn, sl[l].crv);
              CR_SPC:  tap.rec[TR_WR + 3 * l + 2] = srec(SY_SPC, sl[l].isn, sl[l].crv);
              CR_DBR:  tap.rec[TR_WR + 3 * l + 2] = srec(SY_DBR, sl[l].isn, sl[l].crv);
              CR_PR:   tap.rec[TR_WR + 3 * l + 2] = srec(SY_PR, sl[l].isn, sl[l].crv);
              CR_BANK: tap.rec[TR_WR + 3 * l + 2] = crec(CK_GPR, sl[l].isn, gview(sl[l].bkr, sl[l].rb), '0, {32'd0, sl[l].crv});
              default: ;
            endcase
          end
        end
        // the entry's register writes: EXC0 (SR.MD / RB / BL <- 1), EXC_SAVE (SSR, SPC, SGR), with the entry's ISN
        if (sl[l].uk == UK_EXC0 && ent_rst == 2'd0) tap.rec[TR_WR + 3 * l + 2] = srec(SY_SR, sl[l].isn, sr_pre | 32'h7000_0000);
        if (sl[l].uk == UK_EXC_SAVE) begin
          tap.rec[TR_WR + 3 * l]     = srec(SY_SSR, sv_isn, ssr_hold);
          tap.rec[TR_WR + 3 * l + 1] = srec(SY_SPC, sv_isn, spc_hold);
          tap.rec[TR_WR + 3 * l + 2] = srec(SY_SGR, sv_isn, sgr_hold);
        end
      end
    end
    if (exc0_s)
      tap.rec[TR_ENT] = ent_rst == 2'd0 ? crec(ent_intr ? CK_IRQ : CK_EXC, ent_isn, 8'd0, ent_spc, {8'd0, ent_age, exc_vector, ent_code})
                      : crec(CK_EXC, ent_isn, 8'd0, ent_spc, {20'd0, RESET_PC, ent_rst == 2'd2 ? EXP_MHIT : EXP_MANUAL});
    if (!rst_c && run && !run_q && sleep_issued) tap.rec[TR_SLP] = crec(CK_WAKE, nx_isn, 8'd0, '0, '0);
    for (int k = 0; k < TR_FPU_N; k++) tap.rec[TR_FPU + k] = fpu_rec[k];
  end

  // ============================================================================================================
  // FPU (v1_fpu, SOC.md 1.3.2, 4.3.8, 4.5; WI-CORE-F).  The kill of this cycle (pk_*) reaches every FPU record by ISN
  // (kill_v only in an unheld cycle, when the take commits); the latches it reads are also filtered by the S / MA takes
  // (a killed EX / MA micro-op is not an FP bypass source and inserts no multiplier record).  Not by the F1 trap: it
  // kills only what is younger than the trapping micro-op (never an older EX / MA latch, the bypass sources of F1),
  // v1_fpu's record inserts check kill_v themselves and a kill cycle issues nothing (dstop), so its hazards are moot;
  // filtered by it, the view would feed trap_f1 back into itself (a combinational loop).  Its holds: `frz` (pfz | mah |
  // run = 0) for the FE pipe, the vector passes, the multiplier and the double sequencer; pfz | run = 0 for the divider
  // recurrence, which runs on under `mah` (its result pass waits for the FE pipe).
  uop_t ex_f [2], ma_f [2];
  always_comb begin
    for (int l = 0; l < 2; l++) begin
      ex_f[l] = kexo[l] ? '0 : ex[l];
      ma_f[l] = kmao[l] ? '0 : ma[l];
    end
  end
  always_comb for (int s = 0; s < 2; s++) fhzc[s] = fhz[s];
  logic fpu_trap_d [2];                        // v1_fpu's D-decided trap of each slot, to the D-stage take
  always_comb for (int s = 0; s < 2; s++) trap_d[s] = fpu_trap_d[s];
  v1_fpu u_fpu (
    .clk, .rst(rst_c), .hold_pipe(frz), .hold_div(pfz || !run),
    .dbg_we, .dbg_addr, .dbg_wdata, .dbg_raddr, .dbg_rdata(fpu_dbg_rdata), .dbg_hit(fpu_dbg_hit),
    .s_v, .s_d(d), .s_c(c), .s_op(q_op), .md_dec, .fhz, .fwhy, .fdep, .trap_d(fpu_trap_d),
    .ex(ex_f), .ma(ma_f), .sl,
    .di_v, .di_isn, .di_op, .di_d, .di_c, .di_md, .di_fcmpd, .di_fcmpd_op, .di_fcmpd_md,
    .kill_v(pk_v && !frz), .kill_isn(pk_isn), .kill_incl(pk_incl),
    .trap_f1, .trap_f1_lane, .trap_f1_isn, .frec(fpu_rec),
    .ex_res, .mac_ld_a_v, .mac_ld_a, .mac_ld_b_v, .mac_ld_b, .def_ld_v, .def_ld, .sr_s(sr[1]),
    .fr_now, .fpul_now, .fpscr_now, .mach_now, .macl_now,
    .t_busy, .flag_busy, .t_we(fpu_t_we), .t_val(fpu_t_val), .t_lane(fpu_t_lane), .idle(fpu_idle)
  );

  // ============================================================================================================
  // register backdoor
  always_comb begin
    dbg_rdata = '0;
    if (dbg_raddr < 8'd16) dbg_rdata = gpr[gphys(dbg_raddr[3:0], rb)];
    else if (dbg_raddr < 8'd24) dbg_rdata = gpr[gbank(dbg_raddr[2:0], rb)];
    else unique case (dbg_raddr)
      8'd24: dbg_rdata = sr;
      8'd25: dbg_rdata = gbr;
      8'd26: dbg_rdata = vbr;
      8'd27: dbg_rdata = ssr;
      8'd28: dbg_rdata = spc;
      8'd29: dbg_rdata = sgr;
      8'd30: dbg_rdata = dbr;
      8'd33: dbg_rdata = pr;
      8'd34: dbg_rdata = q_v[0] ? q_pc[0] : f_fpc;
      8'd37: dbg_rdata = '0;
      default: dbg_rdata = fpu_dbg_rdata;
    endcase
  end

  // Inputs the later steps use; v1_fetch's frozen outputs the core does not need (q1_mem: the core decodes q1 itself;
  // the slot / fault bits of q_tag: slot0_q and q_fxe carry them).
  /* verilator lint_off UNUSEDSIGNAL */
  wire unused_a2 = ^{at, f_q1_mem, f_q2_mem, q_tag[0][2:1], q_tag[1][2:1], q_tag[2][2:0], dw[0], dw[1],
                     rst.pin, rst.man, fwhy[0], fwhy[1], fpu_dbg_hit,                // v1's hazard-reason / debug outputs
                     rmw_tmp[31:8], sr_rd[9:8], sr_rd[1:0]};                            // (flags reach D via t_now...)
  /* verilator lint_on UNUSEDSIGNAL */
endmodule
