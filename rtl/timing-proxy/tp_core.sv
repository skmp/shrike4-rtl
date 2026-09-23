// tp_core.sv -- D stage and the interlock state of the SH7091 timing proxy (see README.md).
//
// Issue.  Two decoders look at the two oldest instructions of the queue: slot 0, the leader (oldest un-issued
// instruction), and slot 1, the follower.  Every cycle the leader issues unless an interlock holds it; the follower
// issues with it when the two may pair, the follower does not depend on the leader in a way the bypass cannot cover in
// the same cycle, and nothing holds the follower itself.  A held leader is a pipeline freeze, counted by PRFC0 event
// 0x28 (a CPU-side resource: GPR, T, SR flags, system registers, the load/shift conflict) or 0x29 (an FPU-side
// resource: FP registers, FPUL, FPSCR, MACH/MACL, the dispatch holds, the divider, the FTRV group lock).  A held
// follower is not, and neither are multi-cycle issue and the whole-pipeline freezes below.
//
// Interlocks.  The D stage compares each slot's register fields with the destination tags that travel down the
// pipeline registers; which stages a reader compares against is the stage at which it can take the value.
//   CPU  EX / MA tags by producer kind.  An ALU result is on the bypass the next cycle (no tag); a load's value comes
//        from MA's bypass (tag in EX); a late transfer (STS FPUL / FPSCR / MACH / MACL,Rn) from the write-back (tag in
//        EX and MA).  T from the integer flag bypass is there the next cycle; an FCMP in F1 holds T readers one more
//        (FCMP DR's compare leaves D in its second cycle).  STC SR reads the architectural SR, written at the end of
//        MA: an SR-flag write in EX or MA holds it.  The integer load in MA, which SHAD / SHLD collide with, is the
//        same EX / MA flag.
//   FPU  The LS pipe's E1 / E2 tags (FP move or load) and the FE pipe's F1 / F2 tags.  Vector and double operations
//        run more passes than F1 / F2 hold: a short schedule keeps the words whose pass is still to enter F1
//        ("pending"), each operation's write-order tag, and the pair tag a double puts into F1 with its last pass.
//        Readers differ in where they take an FR word: bypass readers (FE operands, FP moves) behind F2, store data
//        behind F1 (a late capture of the FE result bus, so one cycle earlier), register-file readers (vector ops,
//        doubles) only after the write, one cycle after bypass for FE results and three after issue for LS ones.
//        FPUL is bit 32 of the same tags.  The divider is a unit of its own: a destination register holding the
//        words of the divide in flight, and its own result path (a rounding pass, then the write port).
//   A tag leaves by moving down the pipe, never because a younger writer of the same register issued, so a younger,
//   faster writer does not release a reader early (the comparators see every stage; the sweep does not constrain it).
// Structural resources are small counters: the system-register write window, the multiplier (MACH / MACL), the
// dispatch holds after vector / double operations, the divider's release, FPSCR cause / mode, the FTRV group lock.
//
// Multi-cycle instructions (CO group, issue cost k) hold D for k cycles; nothing issues beside them.  Taken branches
// redirect the fetch unit in their last D cycle; LDC SR / LDC.L SR make it refetch the instructions behind them.
// Whole-pipeline freezes stop every stage and the fetch unit, and every tag and counter holds: LDTLB, OCBI and OCBP
// write the UTLB / a cache tag for 2 cycles after their issue cycle, and an instruction that accesses memory in its
// first cycle, issued right behind OCBWB, meets the still-busy operand cache in MA and costs one cycle.
module tp_core import tp_pkg::*; (
  input  logic        clk,
  input  logic        rst,
  input  logic        q0_v, q1_v,
  input  tr_t         q0, q1,
  input  logic        q0_tgt, q1_tgt,
  output logic [1:0]  pop,
  output logic        redirect,
  output logic        refetch,
  output logic        freeze,             // whole-pipeline freeze this cycle (the fetch unit holds too)
  output logic        i0_valid, i1_valid,
  output logic [31:0] i0_seq, i1_seq,
  output logic        ev_par, ev_frzreg, ev_frzfpu,
  output logic [31:0] dbg_d0_seq, dbg_d1_seq,
  output logic [7:0]  dbg_why0, dbg_why1
);
  // ------------------------------------------------------------------------------------------------------------
  // decode
  tr_t  q [2];
  dec_t d [2];
  tc_t  c [2];
  assign q[0] = q0;
  assign q[1] = q1;
  tp_decode u_dec0 (.op(q0.op), .md(q0.md), .d(d[0]));
  tp_decode u_dec1 (.op(q1.op), .md(q1.md), .d(d[1]));
  assign c[0] = tp_timing(d[0].form);
  assign c[1] = tp_timing(d[1].form);

  // ------------------------------------------------------------------------------------------------------------
  // state
  localparam int SCH = 5;             // pass schedule depth: FADD / FSUB / FMUL DR's upper word enters F1 at issue + 6
  // D-stage sequencer, whole-pipeline freeze
  logic [2:0]  dbusy;                 // remaining D cycles of a multi-cycle instruction
  logic        pend_rd, pend_rf;      // ... a redirect / refetch is due in its last D cycle
  logic        pend_fcmp, pend_fl;    // ... FCMP DR's compare / its SR-flag write leaves D in its last D cycle
  logic [1:0]  pfrz;                  // freeze cycles pending
  logic        oc_bsy;                // OCBWB issued last cycle: the operand cache is still busy
  // CPU pipeline tags
  logic [15:0] ex_ld;                 // GPR destination of a load in EX (value from MA's bypass)
  logic [15:0] ex_xf, ma_xf;          // GPR destination of a late transfer in EX / MA (value from the write-back)
  logic        ex_gld, ma_gld;        // an integer GPR load in EX / MA (SHAD / SHLD collide with it in MA)
  logic        t_f1;                  // an FCMP compare in F1
  logic        fl_ex, fl_ma;          // an SR-flag write in EX / MA
  logic [1:0]  sx_cnt;                // system-register write window
  // FPU pipeline tags: 32 FR words (physical bank * 16 + i), FPUL as bit 32
  logic [32:0] e1_mv, e1_ld, e2_ls;   // LS pipe: an FP move / load in E1; either in E2
  logic [32:0] f1_fe, f2_fe;          // FE pipe: results in F1 / F2 (single ops, the last passes of vector / double ops)
  logic [32:0] sch  [1:SCH];          // vector / double passes: the words entering F1 in k cycles
  logic [32:0] wsch [1:SCH];          //   write-order tag of an operation whose last pass enters F1 in k cycles
  logic [32:0] pst  [1:SCH];          //   the pair tag a double puts into F1 with its last pass ...
  logic [32:0] f1_pr;                 //   ... in F1: store data and STS FPUL,Rn wait for the whole pair
  logic [31:0] src_f1;                // double FADD / FSUB / FMUL source words read in F1 (writers wait: anti-dependency)
  // divider: its own destination register and result path
  logic [4:0]  div_cnt;               // cycles to its release: the next FDIV / FSQRT may issue at 0
  logic        div_dbl;               // PR = 1 divide: two result passes, the lower word first
  logic [31:0] div_dst;               // destination words of the divide in flight
  logic [31:0] dv_pair;               // result path, rounding stage: the pair tag of the last pass (every access waits)
  logic [31:0] dv_rnd, dv_wr;         //   the words of a result pass in the rounding stage / at the write port
  // structural
  logic [2:0]  mac_cnt;               // multiplier / MACH:MACL busy
  logic [4:0]  arr_cnt, fe_cnt;       // dispatch holds (array class / FE arithmetic)
  logic [4:0]  fpc_c;                 // FPSCR cause / flags
  logic [2:0]  fpm_c;                 // FPSCR mode
  logic [2:0]  ftrv_c;                // FTRV destination-group lock
  logic [1:0]  ftrv_grp;

  wire frz = pfrz != 0;
  assign freeze = frz;
  // The divider's sequencer inhibits FE-group issue in the cycle before each of its result passes (a coarse, class-
  // level inhibit: it also refuses FIPR / FTRV / FSCA / FSRRA, whose next cycle is in the multiplier array, and the
  // divide result's own path overlaps a vector pass without a stall, so this is not an occupancy of F1).
  wire div_lo_nx = div_dbl && div_cnt == 5'd2;                  // the lower-word pass enters rounding next cycle
  wire div_up_nx = div_cnt == 5'd1;                              // the upper / only pass enters rounding next cycle
  wire div_inh = div_lo_nx || div_up_nx;

  function automatic logic [31:0] grp_mask(input logic [1:0] g);   // FV group, both banks (bank bit not compared)
    return (32'hf << {g, 2'b00}) | (32'hf << {1'b1, g, 2'b00});
  endfunction
  function automatic logic [31:0] xmtrx(input logic [2:0] md);
    return md[2] ? 32'h0000_ffff : 32'hffff_0000;
  endfunction
  function automatic int wpos(input logic [2:0] width, input int w);
    return width == 3'd4 ? (w & 3) : width == 3'd2 ? (w & 1) : 0;
  endfunction

  logic [32:0] pend, wpend;           // any pass / write-order tag still in the schedule
  always_comb begin
    pend = '0;
    wpend = '0;
    for (int k = 1; k <= SCH; k++) begin pend = pend | sch[k]; wpend = wpend | wsch[k]; end
  end

  // ------------------------------------------------------------------------------------------------------------
  // interlocks of each slot against the pipeline
  //   CPU side  0 GPR  1 T  2 system-register window  3 SR flags (STC SR)  4 load in MA vs SHAD / SHLD
  //   FPU side  5 FP register  6 FPUL  7 FPSCR  8 multiplier / MAC  9 dispatch hold  10 divider busy
  //             11 divider's FE-issue inhibit  12 FTRV group lock
  localparam int NW = 13;
  logic [NW-1:0] why [2];
  always_comb begin
    for (int s = 0; s < 2; s++) begin
      logic [31:0] frd, frf, acc;
      frd = c[s].rf_rd ? 32'd0 : d[s].fs;                         // bypass readers
      frf = c[s].rf_rd ? d[s].fs : 32'd0;                         // register-file readers
      acc = frd | frf | d[s].fd | d[s].fw;
      why[s] = '0;
      why[s][0] = |((d[s].ga | d[s].gd | d[s].gs) & (ex_ld | ex_xf | ma_xf));   // every GPR role reads in D
      why[s][1] = c[s].rd_t && t_f1;
      why[s][2] = s == 0 && c[s].sx_r && sx_cnt != 0;           // checked on the leader only
      why[s][3] = c[s].stc_sr && (fl_ex || fl_ma);
      why[s][4] = c[s].shad && ma_gld;
      why[s][5] = |(frd & (e1_ld[31:0] | f1_fe[31:0] | f2_fe[31:0] | dv_wr | pend[31:0])) ||
                  |(frf & (e1_mv[31:0] | e1_ld[31:0] | e2_ls[31:0] | f1_fe[31:0] | f2_fe[31:0] | dv_wr | pend[31:0])) ||
                  |(d[s].fd & (e1_ld[31:0] | f1_fe[31:0] | f1_pr[31:0] | pend[31:0])) ||
                  |(d[s].fw & (wpend[31:0] | src_f1)) ||
                  (div_cnt != 0 && |(acc & div_dst)) || |(acc & dv_pair);
      unique case (c[s].fpul_rd)
        U_D:     why[s][6] = e1_ld[32] || f1_fe[32] || f2_fe[32] || pend[32];
        U_RF:    why[s][6] = e1_mv[32] || e1_ld[32] || e2_ls[32] || f1_fe[32] || f2_fe[32] || pend[32];
        U_ST:    why[s][6] = e1_ld[32] || f1_fe[32] || f1_pr[32] || pend[32];
        // STS FPUL,Rn takes FPUL at its own late point: a single FTRC in F1 is already there (latency 1)
        U_STS:   why[s][6] = e1_ld[32] || f1_pr[32] || pend[32];
        default: why[s][6] = 1'b0;
      endcase
      if (d[s].sw[SYS_FPUL] && wpend[32]) why[s][6] = 1'b1;
      why[s][7] = (c[s].fpu_dec && fpm_c != 0) || (c[s].rd_fpscr_c && fpc_c != 0);
      // the multiplier is the FPU's array: a D-stage issue interlock on its busy count, per reader kind
      why[s][8] = (c[s].mac_rd == 2'd1 && mac_cnt > 3'd2) || (c[s].mac_rd == 2'd2 && mac_cnt > 3'd1) ||
                  (c[s].fe && mac_cnt > 3'd2);
      why[s][9] = (c[s].arr && arr_cnt != 0) || (c[s].fe_arith && fe_cnt != 0);
      why[s][10] = c[s].set_div != 0 && div_cnt != 0;
      why[s][11] = c[s].fe && div_inh;
      why[s][12] = c[s].fp_ls && ftrv_c != 0 && |(acc & grp_mask(ftrv_grp));
    end
  end

  // ------------------------------------------------------------------------------------------------------------
  // the follower against the leader, in the same cycle
  //   0 GPR  1 T  2 FP register (RAW by reader kind, WAW, WAR)  3 FPUL  4 FPSCR mode  5 two branches
  //   6 follower is a redirect target  7 FTRV group vs the older LS FP op's field  8 leader is a taken branch
  //   without a slot  9 class
  logic [9:0] dep;
  always_comb begin
    logic [15:0] gw0;
    logic [31:0] frd1, frf1, facc1;
    logic mtl0, mtl1;
    int p;
    dep = '0;
    p = 0;
    facc1 = '0;
    mtl0 = 1'b0;
    mtl1 = 1'b0;
    gw0 = d[0].gwl | d[0].gwu | (c[0].lat_gw != 0 ? d[0].gw : 16'd0);
    dep[0] = |(gw0 & (d[1].ga | d[1].gd | d[1].gs));
    dep[1] = c[0].wr_t && c[1].rd_t;
    frd1 = c[1].rf_rd ? 32'd0 : d[1].fs;
    frf1 = c[1].rf_rd ? d[1].fs : 32'd0;
    for (int w = 0; w < 32; w++) begin
      p = wpos(c[0].fw_width, w);
      if (d[0].fw[w]) begin
        if ((c[0].f_rd[p] != 0 && frd1[w]) || (c[0].f_rf[p] != 0 && frf1[w]) || (c[0].f_st[p] != 0 && d[1].fd[w]) ||
            (c[0].f_wr[p] != 0 && d[1].fw[w]))
          dep[2] = 1'b1;
      end
    end
    if (c[0].f_war != 0 && |(d[0].fs & d[1].fw)) dep[2] = 1'b1;                    // double arithmetic sources
    if (c[0].ftrv && |(d[1].fw & xmtrx(q[0].md))) dep[2] = 1'b1;                   // FTRV rereads XMTRX
    facc1 = d[1].fs | d[1].fd | d[1].fw;
    if (c[0].ftrv && c[1].fp_ls && |(facc1 & grp_mask(q[0].op[11:10]))) dep[2] = 1'b1;
    if (d[0].sw[SYS_FPUL]) begin
      unique case (c[1].fpul_rd)
        U_D:     dep[3] = c[0].u_rd != 0;
        U_RF:    dep[3] = c[0].u_rf != 0;
        U_ST:    dep[3] = c[0].u_st != 0;
        U_STS:   dep[3] = c[0].u_sts != 0;
        default: dep[3] = 1'b0;
      endcase
      if (d[1].sw[SYS_FPUL] && c[0].u_wr != 0) dep[3] = 1'b1;
    end
    dep[4] = c[0].lat_fpscr_m != 0 && c[1].fpu_dec;
    dep[5] = (c[0].br_pc || c[0].br_reg) && (c[1].br_pc || c[1].br_reg);
    dep[6] = q1_tgt;
    // FTRV's own 2-bit group comparator (the one of its destination lock, bank bit not compared), applied to the FP
    // field op[11:8] of an older non-store LS FP instruction.  For every such form but FLDS that field is a
    // destination the RAW check already covers; FLDS FRm,FPUL (its source) is the case the data shows.
    dep[7] = c[0].fp_ls && d[0].fd == 0 && c[1].ftrv && q[0].op[11:10] == q[1].op[11:10];
    dep[8] = c[0].br_pc && !c[0].br_dly && q[0].tk;
    mtl0 = c[0].cls == C_MT || c[0].cls == C_BR;
    mtl1 = c[1].cls == C_MT || c[1].cls == C_BR;
    dep[9] = !(c[0].k == 3'd1 && c[1].k == 3'd1 && c[0].cls != C_CO && c[1].cls != C_CO &&
               (mtl0 || mtl1 || c[0].cls != c[1].cls));
  end

  // ------------------------------------------------------------------------------------------------------------
  // issue
  wire cpu0 = |why[0][4:0];
  wire fpu0 = |why[0][NW-1:5];
  wire lead = q0_v && dbusy == 0 && !frz;
  wire iss0 = lead && !cpu0 && !fpu0;
  wire iss1 = iss0 && q1_v && dep == 0 && why[1] == 0;

  assign pop = {1'b0, iss0} + {1'b0, iss1};
  assign i0_valid = iss0;
  assign i1_valid = iss1;
  assign i0_seq = q0.seq;
  assign i1_seq = q1.seq;
  assign ev_par = iss0 && iss1;
  assign ev_frzreg = lead && !iss0 && cpu0;
  assign ev_frzfpu = lead && !iss0 && !cpu0 && fpu0;

  wire tk0 = q0.tk && (c[0].br_pc || c[0].br_reg);
  wire tk1 = q1.tk && c[1].br_pc;
  assign redirect = (iss0 && tk0 && c[0].br_pc) || (iss1 && tk1) || (dbusy == 3'd1 && pend_rd && !frz);
  assign refetch = dbusy == 3'd1 && pend_rf && !frz;

  assign dbg_d0_seq = q0_v ? q0.seq : 32'hffff_ffff;
  assign dbg_d1_seq = q1_v ? q1.seq : 32'hffff_ffff;
  assign dbg_why0 = {|why[0][12:9], |why[0][8:7], |why[0][6:5], why[0][4:0]};
  assign dbg_why1 = {iss0 && q1_v && dep != 0, |why[1][12:7], |why[1][6:5], why[1][4:0]};

  // ROM contract (tp_timing.svh): a redirect or refetch is raised in the last D cycle of a multi-cycle CO instruction
  // only; a freeze follows a single issue cycle; a vector / double pass fits the schedule; the anti-dependency window
  // of double arithmetic is the one cycle its sources spend in F1
  always_ff @(posedge clk)
    if (!rst) for (int s = 0; s < 2; s++) if (s == 0 ? iss0 : iss1) begin
      if ((c[s].br_reg || c[s].refetch) && (s != 0 || c[s].k < 3'd2 || c[s].cls != C_CO))
        $error("tp_core: register branch / refetch form %0d must be a multi-cycle CO leader", d[s].form);
      if (c[s].frz_n != 0 && c[s].k != 3'd1)
        $error("tp_core: freezing form %0d must have one issue cycle", d[s].form);
      if (c[s].f_war != 0 && c[s].f_war != 2'd2)
        $error("tp_core: form %0d: the anti-dependency window is one cycle (f_war 2)", d[s].form);
      if (c[s].fe && c[s].rf_rd)
        for (int w = 0; w < 32; w++)
          if (d[s].fw[w] && c[s].set_div == 0 && int'(c[s].f_rf[wpos(c[s].fw_width, w)]) - 3 > SCH)
            $error("tp_core: form %0d: a pass beyond the %0d-slot schedule", d[s].form, SCH);
    end

  // ------------------------------------------------------------------------------------------------------------
  // state update
  function automatic logic [4:0] ld(input logic [4:0] l);      // counter value for "may issue at distance l"
    return l == 5'd0 ? 5'd0 : l - 5'd1;
  endfunction
  function automatic logic [4:0] mx(input logic [4:0] a, input logic [4:0] b);
    return a > b ? a : b;
  endfunction

  always_ff @(posedge clk) begin
    if (rst) begin
      dbusy <= '0; pend_rd <= 1'b0; pend_rf <= 1'b0; pend_fcmp <= 1'b0; pend_fl <= 1'b0; pfrz <= '0; oc_bsy <= 1'b0;
      ex_ld <= '0; ex_xf <= '0; ma_xf <= '0; ex_gld <= 1'b0; ma_gld <= 1'b0; t_f1 <= 1'b0; fl_ex <= 1'b0;
      fl_ma <= 1'b0; sx_cnt <= '0;
      e1_mv <= '0; e1_ld <= '0; e2_ls <= '0; f1_fe <= '0; f2_fe <= '0; f1_pr <= '0; src_f1 <= '0;
      for (int k = 1; k <= SCH; k++) begin sch[k] <= '0; wsch[k] <= '0; pst[k] <= '0; end
      div_cnt <= '0; div_dbl <= 1'b0; div_dst <= '0; dv_pair <= '0; dv_rnd <= '0; dv_wr <= '0;
      mac_cnt <= '0; arr_cnt <= '0; fe_cnt <= '0; fpc_c <= '0; fpm_c <= '0; ftrv_c <= '0; ftrv_grp <= '0;
    end else begin
      // ---- the D-stage sequencer (holds during a freeze)
      if (iss0 && c[0].k > 3'd1) begin
        dbusy <= c[0].k - 3'd1;
        pend_rd <= tk0 && c[0].br_reg;
        pend_rf <= c[0].refetch;
        pend_fcmp <= c[0].wr_t && c[0].fe;
        pend_fl <= c[0].wr_flags;
      end else if (dbusy != 0 && !frz) begin
        dbusy <= dbusy - 3'd1;
        if (dbusy == 3'd1) begin pend_rd <= 1'b0; pend_rf <= 1'b0; pend_fcmp <= 1'b0; pend_fl <= 1'b0; end
      end
      // ---- whole-pipeline freeze: the tag / UTLB write after the issue cycle (frz_n), plus one cycle when a mem1
      //      instruction issues right behind OCBWB (it reaches MA while the operand cache is still busy)
      if (frz) pfrz <= pfrz - 2'd1;
      else pfrz <= (iss0 ? c[0].frz_n : 2'd0) + (iss1 ? c[1].frz_n : 2'd0) +
                   2'(oc_bsy && ((iss0 && c[0].mem1) || (iss1 && c[1].mem1)));

      if (!frz) begin
        // ---- everything moves one stage; then what issues this cycle enters (the follower after the leader)
        logic [15:0] nld, nxf;
        logic ntf1, nflex;
        logic [1:0] nsx;
        logic [2:0] nmac;
        logic [4:0] narr, nfe, ndiv, nfpc;
        logic [2:0] nfpm, nftc;
        logic [1:0] nftg;
        logic [32:0] ne1mv, ne1ld, nf1, nf1pr;
        logic [31:0] nsrc, ndd;
        logic ndbl;
        logic [32:0] nsch [1:SCH], nwsch [1:SCH], npst [1:SCH];
        nld = '0; nxf = '0;
        ntf1 = dbusy == 3'd1 && pend_fcmp;          // FCMP DR: the compare enters F1 after its last D cycle
        nflex = dbusy == 3'd1 && pend_fl;           // a multi-cycle flag writer's SR write enters EX after it
        nsx = sx_cnt == 0 ? 2'd0 : sx_cnt - 2'd1;
        nmac = mac_cnt == 0 ? 3'd0 : mac_cnt - 3'd1;
        narr = ld(arr_cnt); nfe = ld(fe_cnt); ndiv = ld(div_cnt); nfpc = ld(fpc_c);
        nfpm = fpm_c == 0 ? 3'd0 : fpm_c - 3'd1;
        nftc = ftrv_c == 0 ? 3'd0 : ftrv_c - 3'd1;
        nftg = ftrv_grp;
        ne1mv = '0; ne1ld = '0; nsrc = '0; ndd = div_dst; ndbl = div_dbl;
        nf1 = sch[1]; nf1pr = pst[1];                // the passes due now enter F1
        for (int k = 1; k < SCH; k++) begin nsch[k] = sch[k + 1]; nwsch[k] = wsch[k + 1]; npst[k] = pst[k + 1]; end
        nsch[SCH] = '0; nwsch[SCH] = '0; npst[SCH] = '0;

        for (int s = 0; s < 2; s++) begin
          if (s == 0 ? iss0 : iss1) begin
            logic [32:0] opw, opfw;
            int elast;
            // GPR: the destination enters EX tagged with the stage its value becomes bypassable
            for (int i = 0; i < 16; i++) begin
              logic [2:0] l;
              l = 3'd0;
              if (d[s].gw[i]) l = c[s].lat_gw;
              if (d[s].gwu[i] && l < 3'd1) l = 3'd1;
              if (d[s].gwl[i]) l = 3'd2 > l ? 3'd2 : l;
              if (l == 3'd2) nld[i] = 1'b1;
              if (l == 3'd3) nxf[i] = 1'b1;
            end
            // T: only an FCMP is late (the integer flag bypass has it the next cycle); SR flags: a single-cycle
            // writer's write enters EX now
            if (c[s].wr_t && c[s].fe && c[s].k == 3'd1) ntf1 = 1'b1;
            if (c[s].wr_flags && c[s].k == 3'd1) nflex = 1'b1;
            if (c[s].sx_w) nsx = 2'd2;
            // multiplier, dispatch holds, divider release, FPSCR, FTRV lock
            if (c[s].lat_mac != 0 && c[s].lat_mac - 3'd1 > nmac) nmac = c[s].lat_mac - 3'd1;
            narr = mx(narr, ld(c[s].set_arr));
            nfe = mx(nfe, ld(c[s].set_fe));
            ndiv = mx(ndiv, ld(c[s].set_div));
            if (c[s].set_div != 0) begin ndd = d[s].fw; ndbl = c[s].fw_width == 3'd2; end
            nfpc = mx(nfpc, ld(c[s].lat_fpscr_c));
            if (c[s].lat_fpscr_m != 0 && c[s].lat_fpscr_m - 3'd1 > nfpm) nfpm = c[s].lat_fpscr_m - 3'd1;
            if (c[s].ftrv) begin nftc = 3'd4; nftg = q[s].op[11:10]; end
            if (c[s].f_war != 0) nsrc = nsrc | d[s].fs;
            // FP destinations (FPUL = bit 32): single FE results enter F1, LS results E1, a divide goes to the
            // divider (its destination register was loaded above); vector / double passes are scheduled at the
            // cycle they enter F1, their register-file write time FS (f_rf) minus 2
            opw = '0; opfw = '0; elast = 0;
            for (int w = 0; w <= 32; w++) begin
              logic wr;
              int p, e;
              wr = w == 32 ? d[s].sw[SYS_FPUL] : d[s].fw[w];
              p = w == 32 ? 0 : wpos(c[s].fw_width, w);
              e = int'(w == 32 ? c[s].u_rf : c[s].f_rf[p]) - 2;
              if (wr && c[s].set_div == 0) begin
                if (c[s].fe && !c[s].rf_rd) nf1[w] = 1'b1;
                else if (!c[s].fe && c[s].mem1) ne1ld[w] = 1'b1;
                else if (!c[s].fe) ne1mv[w] = 1'b1;
                else begin
                  if (e <= 1) nf1[w] = 1'b1; else nsch[e - 1][w] = 1'b1;
                  // write order: a later writer waits for the operation's last pass to enter F1.  FTRV's tag is
                  // DR(4n) only (elements 0 and 1): the data-forced H3 rule -- an FE writer of FV element 2 / 3 is
                  // not held behind FTRV (one record; LS writers are held by the group lock anyway)
                  if (w == 32 ? c[s].u_wr != 0 : !c[s].ftrv || p < 2) opw[w] = 1'b1;
                  opfw[w] = 1'b1;
                  if (e > elast) elast = e;
                end
              end
            end
            if (elast >= 2) begin
              nwsch[elast - 1] = nwsch[elast - 1] | opw;
              if (c[s].fe_arith && c[s].rf_rd) npst[elast - 1] = npst[elast - 1] | opfw;   // doubles only
            end
          end
        end
        ex_ld <= nld; ex_xf <= nxf; ma_xf <= ex_xf;
        ex_gld <= (iss0 && |d[0].gwl) || (iss1 && |d[1].gwl);         // an integer GPR load
        ma_gld <= ex_gld;
        t_f1 <= ntf1; fl_ex <= nflex; fl_ma <= fl_ex;
        sx_cnt <= nsx; mac_cnt <= nmac; arr_cnt <= narr; fe_cnt <= nfe; fpc_c <= nfpc; fpm_c <= nfpm;
        ftrv_c <= nftc; ftrv_grp <= nftg;
        e1_mv <= ne1mv; e1_ld <= ne1ld; e2_ls <= e1_mv | e1_ld;
        f1_fe <= nf1; f2_fe <= f1_fe; f1_pr <= nf1pr; src_f1 <= nsrc;
        for (int k = 1; k <= SCH; k++) begin sch[k] <= nsch[k]; wsch[k] <= nwsch[k]; pst[k] <= npst[k]; end
        div_cnt <= ndiv; div_dst <= ndd; div_dbl <= ndbl;
        dv_pair <= div_up_nx ? div_dst : '0;
        dv_rnd <= div_up_nx ? (div_dbl ? div_dst & 32'h5555_5555 : div_dst) :
                  div_lo_nx ? div_dst & 32'haaaa_aaaa : '0;
        dv_wr <= dv_rnd;
        oc_bsy <= (iss0 && c[0].oc_busy) || (iss1 && c[1].oc_busy);
      end
    end
  end
endmodule
