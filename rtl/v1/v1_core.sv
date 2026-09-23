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
//   * the operand cache port (MA): OCBI / OCBP pair like any LS instruction, then their tag write after the lookup
//     holds the whole pipeline -- fetch, D, the sequencer, EX / MA / S and the FPU -- for 2 cycles.  OCBWB keeps the
//     port one cycle longer: an access in EX right behind it (a load, store, PREF, cache-block op or LDTLB) would need
//     it in its MA cycle, so the pipeline holds for one cycle.  LDTLB (CO, one cycle) writes the UTLB the same way
//     as a tag write: 2 cycles of whole-pipeline freeze.  The freezes add up (OCBWB ; OCBI costs 3).
module v1_core import tp_pkg::*; import v1_pkg::*; import v1_alu_pkg::*; (
  input  logic        clk,
  input  logic        rst,
  input  logic        dbg_we,
  input  logic [7:0]  dbg_addr,
  input  logic [31:0] dbg_wdata,
  input  logic [7:0]  dbg_raddr,
  output logic [31:0] dbg_rdata,
  output logic        if_req,
  output logic [31:0] if_addr,
  input  logic [31:0] if_data,
  output logic        dm_req, dm_we,
  output logic [1:0]  dm_size,
  output logic [31:0] dm_addr,
  output logic [63:0] dm_wdata,
  input  logic [63:0] dm_rdata,
  output logic        halted,
  output logic [31:0] cnt_cycles, cnt_par, cnt_frzreg, cnt_frzfpu,
  output logic        i0_valid, i1_valid,
  output logic [31:0] i0_pc, i1_pc
);
  // ============================================================================================================
  // architectural state of the CPU side (the FPU side lives in v1_fpu)
  logic [31:0] gpr [24];                       // R0-R7 bank 0, R0-R7 bank 1, R8-R15
  logic [31:0] sr, gbr, vbr, ssr, spc, sgr, dbr, pr;
  logic [31:0] reset_pc;
  logic        err;                            // TRAPA / unknown opcode issued

  wire rb = sr[30] & sr[29];                   // MD & RB: which bank R0-R7 are

  // ============================================================================================================
  // pipeline latches
  uop_t ex [2], ma [2], sl [2];
  uop_t ex_res [2];                             // the EX latch with operands and EX results filled in
  logic fpu_idle;

  // ============================================================================================================
  // fetch
  logic        q_v [3];
  logic [31:0] q_pc [2];
  logic [15:0] q_op [2];
  logic        q_tgt [2];
  logic [1:0]  pop;
  logic        redirect, rd_keep, rd_slot_pend, refetch;
  logic [31:0] rd_target, rd_slot_pc, rf_pc;
  logic        sleep_hit;

  v1_fetch u_fetch (
    .clk, .rst, .reset_pc, .halt(sleep_hit || pfz),
    .if_req, .if_addr, .if_data,
    .q0_v(q_v[0]), .q1_v(q_v[1]), .q2_v(q_v[2]), .q0_pc(q_pc[0]), .q1_pc(q_pc[1]), .q0_op(q_op[0]), .q1_op(q_op[1]),
    .q0_tgt(q_tgt[0]), .q1_tgt(q_tgt[1]), .pop,
    .redirect, .target(rd_target), .keep(rd_keep), .slot_pend(rd_slot_pend), .slot_pc(rd_slot_pc),
    .refetch, .rf_pc
  );

  // ============================================================================================================
  // FPU
  logic [2:0]  md_dec;                         // FPSCR {FR, SZ, PR} as the decoder sees it
  logic [31:0] fr_now [32];                    // FP register file, write-through
  logic [31:0] fpul_now, fpscr_now, mach_now, macl_now;
  logic        fhz [2];                        // FPU resource hazard of each D slot
  logic [7:0]  fwhy [2];
  logic        fdep;                           // the follower depends on the leader through an FPU resource
  logic        t_busy, flag_busy;              // FCMP's T not yet delivered / its flags not yet at S
  logic        fpu_t_we, fpu_t_val, fpu_t_lane;
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
  logic        frz;                            // back-end freeze (= pfz)
  // Whole-pipeline freeze (fetch, D, sequencer, EX / MA / S, FPU all hold): the operand cache's tag write after an
  // OCBI / OCBP lookup, or LDTLB's UTLB write (2 cycles after the issue cycle), and one cycle when an access reaches EX while OCBWB, one
  // stage ahead in MA, still holds the cache port for its second cycle
  logic [1:0]  tfrz;                           // tag-write freeze cycles still to come
  logic        oc_coll, oc_waited;             // OCBWB collision this cycle / already paid for this pair
  wire         pfz = tfrz != 2'd0 || oc_coll;

  // ============================================================================================================
  // D stage: the two decode slots
  dec_t   d [2];
  ictl_t  c [2];
  logic   s_v [2];
  tp_decode u_dec0 (.op(q_op[0]), .md(md_dec), .d(d[0]));
  tp_decode u_dec1 (.op(q_op[1]), .md(md_dec), .d(d[1]));
  assign c[0] = v1_ictl(d[0].form);
  assign c[1] = v1_ictl(d[1].form);

  // the micro-op sequencer of multi-cycle instructions
  logic        sq_on;
  logic [2:0]  sq_step, sq_k;
  logic [31:0] sq_pc;
  logic [15:0] sq_op;
  form_e       sq_form;
  ictl_t       sq_c;
  dec_t        sq_d;
  logic        sq_rb;
  logic [2:0]  sq_md;
  logic [31:0] sq_target;                     // RTE: SPC; JMP-class: taken from EX of micro-op 0
  logic [31:0] sq_addr;                        // @(R0,GBR) / TAS.B / STC.L address from micro-op 0
  logic [31:0] sq_data;                        // STC.L / STS.L PR: the register value read in D of micro-op 0
  logic [31:0] rmw_tmp;                        // byte loaded by micro-op 0 of TST.B / AND.B / OR.B / XOR.B / TAS.B
  logic        sq_last;
  assign sq_last = sq_on && sq_step == sq_k - 3'd1;
  assign frz = pfz;

  // ---- register reads in D (write-through from this cycle's S writes)
  logic [31:0] gpr_rd [24];
  logic [31:0] sr_rd, gbr_rd, vbr_rd, ssr_rd, spc_rd, dbr_rd, pr_rd;
  always_comb begin
    for (int i = 0; i < 24; i++) gpr_rd[i] = gpr[i];
    sr_rd = sr; gbr_rd = gbr; vbr_rd = vbr; ssr_rd = ssr; spc_rd = spc; dbr_rd = dbr; pr_rd = pr;
    for (int l = 0; l < 2; l++) begin
      if (sl[l].v) begin
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
    end
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

  // ---- the follower against the leader in the same cycle
  logic pair_ok, dep1;
  always_comb begin
    logic mt0, mt1, br0, br1, tk0;
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

  // ---- issue
  wire busy = sq_on || pfz;
  wire lead = s_v[0] && !busy && !sleep_hit;
  wire cpu0 = |cwhy[0];
  wire iss0 = lead && !cpu0 && !fhz[0];
  wire iss1 = iss0 && s_v[1] && pair_ok && !dep1 && !(|cwhy[1]) && !fhz[1];
  assign s_v[0] = q_v[0];
  assign s_v[1] = q_v[1];
  assign pop = {1'b0, iss0} + {1'b0, iss1};
  assign i0_valid = iss0;
  assign i1_valid = iss1;
  assign i0_pc = q_pc[0];
  assign i1_pc = q_pc[1];

  // ---- branches in D: PC-relative targets from the D-stage adder; register targets from EX of micro-op 0
  logic [31:0] br_tgt [2];
  logic        br_taken [2];
  logic [31:0] jmp_tgt_ex;                     // target computed by micro-op 0 of a JMP-class branch now in EX
  always_comb begin
    for (int s = 0; s < 2; s++) begin
      logic [31:0] disp;
      disp = (q_op[s][15:12] == 4'hA || q_op[s][15:12] == 4'hB) ? {{19{q_op[s][11]}}, q_op[s][11:0], 1'b0}
                                                                 : {{23{q_op[s][7]}}, q_op[s][7:0], 1'b0};
      br_tgt[s] = q_pc[s] + 32'd4 + disp;
      br_taken[s] = c[s].br_pc && (!c[s].br_cond || t_now == c[s].br_t);
    end
  end

  always_comb begin
    redirect = 1'b0; rd_target = '0; rd_keep = 1'b0; rd_slot_pend = 1'b0; rd_slot_pc = '0;
    refetch = 1'b0; rf_pc = '0;
    if (iss1 && br_taken[1]) begin
      // the branch paired as follower: its delay slot is the next queue entry
      redirect = 1'b1; rd_target = br_tgt[1];
      if (c[1].br_dly) begin rd_keep = q_v[2]; rd_slot_pend = !q_v[2]; rd_slot_pc = q_pc[1] + 32'd2; end
    end else if (iss0 && br_taken[0]) begin
      redirect = 1'b1; rd_target = br_tgt[0];
      if (c[0].br_dly && !iss1) begin rd_keep = q_v[1]; rd_slot_pend = !q_v[1]; rd_slot_pc = q_pc[0] + 32'd2; end
    end else if (sq_last && sq_c.br_reg) begin
      // JMP JSR BRAF BSRF RTS: micro-op 0 is in EX and has the target; RTE took SPC in micro-op 0
      redirect = 1'b1; rd_target = sq_c.seq == SQ_RTE ? sq_target : jmp_tgt_ex;
      rd_keep = q_v[0]; rd_slot_pend = !q_v[0]; rd_slot_pc = sq_pc + 32'd2;
    end else if (sq_last && sq_c.refetch) begin
      refetch = 1'b1; rf_pc = sq_pc + 32'd2;
    end
  end

  // ---- building a micro-op in D
  function automatic uop_t mk_uop(input logic [31:0] pc, input logic [15:0] op, input dec_t dd, input ictl_t cc,
                                  input logic [2:0] step, input logic lane, input logic [2:0] md);
    uop_t u;
    logic [4:0] pm, pn;
    u = '0;
    u.v = 1'b1; u.pc = pc; u.op = op; u.form = dd.form; u.step = step; u.lane = lane; u.rb = rb; u.md = md;
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
      F_STC_SGR_RN, F_STC_L_SGR_AT_PRE_RN: u.cr = sgr;
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
    if (cc.seq == SQ_MAC) begin                   // MAC.x: micro-op 0 walks @Rm+, micro-op 1 @Rn+
      u.w1 = 1'b1; u.w1k = GR_EX; u.w1r = step == 3'd0 ? pm : pn;
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

  // micro-ops entering EX this cycle
  uop_t ex_in [2];
  always_comb begin
    ex_in[0] = '0; ex_in[1] = '0;
    if (iss0) ex_in[0] = mk_uop(q_pc[0], q_op[0], d[0], c[0], 3'd0, 1'b0, md_dec);
    if (iss1) ex_in[1] = mk_uop(q_pc[1], q_op[1], d[1], c[1], 3'd0, 1'b1, md_dec);
    // later micro-ops of a multi-cycle instruction (lane 0; nothing pairs with them)
    if (sq_on && !frz) begin
      logic go;
      unique case (sq_c.seq)
        SQ_MAC:  go = sq_step == 3'd1;             // the second load (@Rn+)
        SQ_TSTB: go = sq_step == 3'd2;             // T from the loaded byte
        SQ_BOPB: go = sq_step == 3'd3;             // store the modified byte
        SQ_TASB: go = sq_step == 3'd4;             // store byte | 0x80, T
        SQ_SX2:  go = sq_step == 3'd1 && stcl_form(sq_form);   // STC.L / STS.L PR: the store (cycle 2)
        SQ_SX3:  go = sq_step == 3'd2 && stcl_form(sq_form);   // STC.L SGR: the store
        default: go = 1'b0;
      endcase
      if (go) begin
        ex_in[0] = mk_uop(sq_pc, sq_op, sq_d, sq_c, sq_step, 1'b0, sq_md);
      end
    end
  end

  // OCBWB in MA keeps the cache port for one more cycle: an access in EX (the first micro-op of a load, store, PREF,
  // cache-block op or LDTLB) would need it in its MA cycle, so the pipeline holds for one cycle, once
  always_comb begin
    logic ocb_ma, use_ex;
    ocb_ma = 1'b0; use_ex = 1'b0;
    for (int l = 0; l < 2; l++) begin
      if (ma[l].v && ma[l].step == 3'd0 && ma[l].form == F_OCBWB_AT_RN) ocb_ma = 1'b1;
      if (ex[l].v && ex[l].step == 3'd0 && (ex_res[l].mrd || ex_res[l].mwr || v1_ictl(ex[l].form).oc_use)) use_ex = 1'b1;
    end
    oc_coll = ocb_ma && use_ex && !oc_waited;
  end

  // FPU dispatch of this cycle's issues
  always_comb begin
    di_v[0] = iss0; di_op[0] = q_op[0]; di_d[0] = d[0]; di_c[0] = c[0]; di_md[0] = md_dec;
    di_v[1] = iss1; di_op[1] = q_op[1]; di_d[1] = d[1]; di_c[1] = c[1]; di_md[1] = md_dec;
    di_fcmpd = sq_on && sq_c.seq == SQ_FCMPD && sq_step == 3'd1;
    di_fcmpd_op = sq_op; di_fcmpd_md = sq_md;
  end

  // ============================================================================================================
  // EX stage
  //   operands are corrected by the bypass: from MA (EX results of the previous cycle), from S (EX results and load
  //   data), and in lane 1 from lane 0 of the same cycle when lane 0 is MOV Rm,Rn (a copy of its source)
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

  // FP register as the LS pipe reads it in EX: the register file (write-through) or an LS result not written yet
  function automatic logic [31:0] frd_ex(input int w);
    logic [31:0] x;
    x = fr_now[w];
    for (int l = 0; l < 2; l++) if (sl[l].v && sl[l].fwm[w]) x = w[0] ? sl[l].fwo : sl[l].fwe;
    for (int l = 0; l < 2; l++) if (ma[l].v && ma[l].fwm[w] && ma[l].fk != FP_LOAD) x = w[0] ? ma[l].fwo : ma[l].fwe;
    return x;
  endfunction
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
      logic [31:0] a, b, cc, imm8s, imm8z, sz, n4, n8, n16, frv;
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
        // read-modify-write on @(R0,GBR) and TAS.B: micro-op 0 loads the byte, the last one stores it
        F_TST_B_IMM_AT_R0_GBR, F_AND_B_IMM_AT_R0_GBR, F_OR_B_IMM_AT_R0_GBR, F_XOR_B_IMM_AT_R0_GBR, F_TAS_B_AT_RN: begin
          if (u.step == 3'd0) begin
            u.mrd = 1; u.msz = 2'd0; u.maddr = u.form == F_TAS_B_AT_RN ? u.b : u.cr + u.c;
          end else if (u.form != F_TST_B_IMM_AT_R0_GBR) begin
            u.mwr = 1; u.msz = 2'd0; u.maddr = sq_addr; u.mwd = {32'd0, ao.r};
          end
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
        // MAC.x: two loads
        F_MAC_L_AT_RM_INC_AT_RN_INC: begin u.mrd = 1; u.msz = 2'd2; u.maddr = u.step == 3'd0 ? u.a : u.b; sz = 32'd4; end
        F_MAC_W_AT_RM_INC_AT_RN_INC: begin u.mrd = 1; u.msz = 2'd1; u.msx = 1; u.maddr = u.step == 3'd0 ? u.a : u.b; sz = 32'd2; end
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
        default: ;
      endcase
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

  // ============================================================================================================
  // MA stage: the data memory (one access per cycle: two LS instructions never pair) and the FPU -> CPU transfers
  uop_t ma_res [2];
  logic [31:0] mac_ld_a, mac_ld_b, def_ld;      // load data for the multiplier (MAC operands, LDS.L MACx)
  logic        mac_ld_a_v, mac_ld_b_v, def_ld_v;
  always_comb begin
    dm_req = 1'b0; dm_we = 1'b0; dm_size = 2'd0; dm_addr = '0; dm_wdata = '0;
    mac_ld_a_v = 1'b0; mac_ld_b_v = 1'b0; def_ld_v = 1'b0; mac_ld_a = '0; mac_ld_b = '0; def_ld = '0;
    for (int l = 0; l < 2; l++) begin
      uop_t u;
      logic [31:0] ld;
      u = ma[l];
      if (u.v && (u.mrd || u.mwr)) begin
        dm_req = 1'b1; dm_we = u.mwr; dm_size = u.msz; dm_addr = u.maddr & 32'h1FFF_FFFF; dm_wdata = u.mwd;
      end
      unique case (u.msz)
        2'd0: ld = u.msx ? {{24{dm_rdata[7]}}, dm_rdata[7:0]} : {24'd0, dm_rdata[7:0]};
        2'd1: ld = u.msx ? {{16{dm_rdata[15]}}, dm_rdata[15:0]} : {16'd0, dm_rdata[15:0]};
        default: ld = dm_rdata[31:0];
      endcase
      if (u.v && u.mrd) begin
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
          F_FMOV_AT_R0_RM_XDN: begin u.fwe = dm_rdata[31:0]; u.fwo = dm_rdata[63:32]; end
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
  // pipeline advance and write-back
  logic sleep_issued;
  always_ff @(posedge clk) begin
    if (rst) begin
      for (int l = 0; l < 2; l++) begin ex[l] <= '0; ma[l] <= '0; sl[l] <= '0; end
      sq_on <= 1'b0; sq_step <= '0; sq_k <= '0; tfrz <= '0; oc_waited <= 1'b0;
      sleep_issued <= 1'b0; err <= 1'b0;
      cnt_cycles <= '0; cnt_par <= '0; cnt_frzreg <= '0; cnt_frzfpu <= '0;
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
          8'd34: reset_pc <= dbg_wdata;
          default: ;
        endcase
      end
    end else begin
      // ---- counters (frozen once SLEEP has issued)
      if (!sleep_issued) begin
        cnt_cycles <= cnt_cycles + 32'd1;
        if (iss0 && iss1) cnt_par <= cnt_par + 32'd1;
        if (lead && !iss0 && cpu0) cnt_frzreg <= cnt_frzreg + 32'd1;
        if (lead && !iss0 && !cpu0 && fhz[0]) cnt_frzfpu <= cnt_frzfpu + 32'd1;
      end
      if ((iss0 && q_op[0] == 16'h001B) || (iss1 && q_op[1] == 16'h001B)) sleep_issued <= 1'b1;
      if ((iss0 && c[0].trap && q_op[0] != 16'h001B) || (iss1 && c[1].trap && q_op[1] != 16'h001B)) begin
        sleep_issued <= 1'b1; err <= 1'b1;
      end
      // ---- the sequencer
      if (iss0 && c[0].k > 3'd1) begin
        sq_on <= 1'b1; sq_step <= 3'd1; sq_k <= c[0].k;
        sq_pc <= q_pc[0]; sq_op <= q_op[0]; sq_form <= d[0].form; sq_c <= c[0]; sq_d <= d[0]; sq_rb <= rb;
        sq_md <= md_dec;
        if (d[0].form == F_RTE) sq_target <= spc_rd;
      end else if (sq_on && !pfz) begin
        if (sq_step == sq_k - 3'd1) sq_on <= 1'b0;
        sq_step <= sq_step + 3'd1;
      end
      // ---- operand-cache freezes
      if (tfrz != 2'd0) tfrz <= tfrz - 2'd1;
      else if (!oc_coll) tfrz <= ((iss0 && c[0].tag_wr) || (iss1 && c[1].tag_wr)) ? 2'd2 : 2'd0;
      oc_waited <= oc_coll;
      // the address of the read-modify-write forms, from micro-op 0 in EX
      for (int l = 0; l < 2; l++)
        if (ex[l].v && ex[l].step == 3'd0 && (ex[l].form == F_TST_B_IMM_AT_R0_GBR || ex[l].form == F_AND_B_IMM_AT_R0_GBR ||
            ex[l].form == F_OR_B_IMM_AT_R0_GBR || ex[l].form == F_XOR_B_IMM_AT_R0_GBR || ex[l].form == F_TAS_B_AT_RN ||
            stcl_form(ex[l].form))) begin
          sq_addr <= ex_res[l].maddr;
          sq_data <= ex[l].cr;
        end
      for (int l = 0; l < 2; l++)
        if (ma[l].v && ma[l].mrd && ma[l].step == 3'd0 && (ma[l].form == F_TST_B_IMM_AT_R0_GBR ||
            ma[l].form == F_AND_B_IMM_AT_R0_GBR || ma[l].form == F_OR_B_IMM_AT_R0_GBR ||
            ma[l].form == F_XOR_B_IMM_AT_R0_GBR || ma[l].form == F_TAS_B_AT_RN))
          rmw_tmp <= {24'd0, dm_rdata[7:0]};

      if (!frz) begin
        // ---- write-back (S): lane 1 is younger and wins
        for (int l = 0; l < 2; l++) begin
          if (sl[l].v) begin
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
        end
        // ---- flags: CPU writers at the end of EX, FCMP at the end of F1 (the younger of the same issue cycle wins)
        begin
          logic [31:0] nsr;
          nsr = sr;
          for (int l = 0; l < 2; l++) if (sl[l].v && sl[l].crw == CR_SR) nsr = sl[l].crv;
          if (fpu_t_we && !fpu_t_lane) nsr[0] = fpu_t_val;
          for (int l = 0; l < 2; l++) begin
            if (ex[l].v && ex_res[l].tw) nsr[0] = ex_res[l].tv;
            if (ex[l].v && ex_res[l].qmw) begin nsr[8] = ex_res[l].qv; nsr[9] = ex_res[l].mv; end
            if (ex[l].v && ex_res[l].sw) nsr[1] = ex_res[l].sv;
            if (l == 0 && fpu_t_we && fpu_t_lane) nsr[0] = fpu_t_val;
          end
          sr <= nsr;
        end
        // ---- advance
        for (int l = 0; l < 2; l++) begin
          sl[l] <= ma_res[l];
          ma[l] <= ex_res[l];
          ex[l] <= ex_in[l];
        end
      end
    end
  end

  assign sleep_hit = sleep_issued;
  assign halted = sleep_issued && !ex[0].v && !ex[1].v && !ma[0].v && !ma[1].v && !sl[0].v && !sl[1].v && fpu_idle;

  // ============================================================================================================
  // FPU
  v1_fpu u_fpu (
    .clk, .rst, .frz,
    .dbg_we, .dbg_addr, .dbg_wdata, .dbg_raddr, .dbg_rdata(fpu_dbg_rdata), .dbg_hit(fpu_dbg_hit),
    .s_v, .s_d(d), .s_c(c), .s_op(q_op), .md_dec, .fhz, .fwhy, .fdep,
    .ex, .ma, .sl,
    .di_v, .di_op, .di_d, .di_c, .di_md, .di_fcmpd, .di_fcmpd_op, .di_fcmpd_md,
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
      8'd34: dbg_rdata = q_v[0] ? q_pc[0] : reset_pc;
      8'd37: dbg_rdata = {31'd0, err};
      default: dbg_rdata = fpu_dbg_rdata;
    endcase
  end
endmodule
