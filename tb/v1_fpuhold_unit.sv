// v1_fpuhold_unit.sv -- WI-CORE-F's unit top: v1_fpu alone (make v1-fpuhold, part 2; tb/v1_fpuhold_unit.cpp).
//
// The C++ side is the core: it presents opcodes to the two D slots (decoded here with tp_decode and v1_ictl_op in the
// FPSCR mode v1_fpu reports, as v1_core does), decides the issues (di_v, with their ISNs) from v1_fpu's hazards,
// scripts the two holds (hold_pipe, hold_div) and the kill (kill_v / kill_isn / kill_incl), and can put one multiplier
// micro-op into the EX latch (ex_op / ex_isn with its operands) and deliver MAC / LDS.L load data.  The LS pipe
// (sl / ma) is empty.  Outputs: v1_fpu's hazard, trap and T ports, its commit-log records (frec), the register file
// (dbg), and internal state for the checks (cz_ctr, the divider's records and progress, the result pass grant).
// chk_err counts v1_fpu's own monitors and tb/v1_fpu_reccheck.sv's (the records against the register writes, and
// rec_ovf_err).
module v1_fpuhold_unit import tp_pkg::*; import v1_pkg::*; (
  input  logic        clk,
  input  logic        rst,
  input  logic        hold_pipe, hold_div,
  input  logic        s_v0, s_v1,
  input  logic [15:0] s_op0, s_op1,
  input  logic        di_v0, di_v1,
  input  logic [7:0]  di_isn0, di_isn1,
  input  logic        di_fcmpd,
  input  logic [15:0] di_fcmpd_op,
  input  logic        kill_v,
  input  logic [7:0]  kill_isn,
  input  logic        kill_incl,
  input  logic        ex_v,                   // a multiplier micro-op (micro-op 0) in EX lane 0
  input  logic [15:0] ex_op,
  input  logic [7:0]  ex_isn,
  input  logic [31:0] ex_a, ex_b,             // its Rm / Rn values (ex_res.a / .b)
  input  logic        mac_ld_a_v, mac_ld_b_v, def_ld_v,
  input  logic [31:0] mac_ld_a, mac_ld_b, def_ld,
  input  logic        dbg_we,
  input  logic [7:0]  dbg_addr,
  input  logic [31:0] dbg_wdata,
  input  logic [7:0]  dbg_raddr,
  output logic [31:0] dbg_rdata,
  output logic        fhz0, fhz1, fdep,
  output logic [7:0]  fwhy0, fwhy1,
  output logic        trap_d0, trap_d1,
  output logic        trap_f1, trap_f1_lane,
  output logic [7:0]  trap_f1_isn,
  output v1_soc_pkg::commit_rec_t [5:0] frec,
  output logic        t_we, t_val, t_lane,
  output logic        idle,
  output logic [2:0]  md_dec,
  output logic [31:0] fpscr_now,
  // internal state (checks)
  output logic [4:0]  cz_ctr,
  output logic        dv_busy, dv_rq, dv_done, dv_gnt, steal,
  output logic [1:0]  dv_on,
  output logic        ftrv_on, fcd_v,
  output logic [31:0] chk_err
);
  logic        s_v [2], di_v [2];
  logic [15:0] s_op [2];
  dec_t        d [2];
  /* verilator lint_off UNUSEDSIGNAL */          // the EX micro-op takes exd.form only
  dec_t        exd;
  /* verilator lint_on UNUSEDSIGNAL */
  ictl_t       c [2];
  logic [7:0]  di_isn [2];
  logic        fhz [2], trap_d [2];
  logic [7:0]  fwhy [2];
  logic [2:0]  di_md [2];
  uop_t        ex [2], ma [2], sl [2], ex_res [2];
  logic [31:0] fr_now [32];
  /* verilator lint_off UNUSEDSIGNAL */
  logic [31:0] fpul_now, mach_now, macl_now;
  logic        t_busy, flag_busy, dbg_hit;
  /* verilator lint_on UNUSEDSIGNAL */
  assign s_v[0] = s_v0; assign s_v[1] = s_v1; assign s_op[0] = s_op0; assign s_op[1] = s_op1;
  assign di_v[0] = di_v0; assign di_v[1] = di_v1; assign di_isn[0] = di_isn0; assign di_isn[1] = di_isn1;
  assign di_md[0] = md_dec; assign di_md[1] = md_dec;
  tp_decode u_dec0 (.op(s_op0), .md(md_dec), .d(d[0]));
  tp_decode u_dec1 (.op(s_op1), .md(md_dec), .d(d[1]));
  tp_decode u_decx (.op(ex_op), .md(md_dec), .d(exd));
  assign c[0] = v1_ictl_op(d[0].form, s_op0);
  assign c[1] = v1_ictl_op(d[1].form, s_op1);
  always_comb begin
    ex[0] = '0; ex[1] = '0; ma[0] = '0; ma[1] = '0; sl[0] = '0; sl[1] = '0;
    ex[0].v = ex_v; ex[0].form = exd.form; ex[0].op = ex_op; ex[0].isn = ex_isn;
    ex_res[0] = ex[0]; ex_res[0].a = ex_a; ex_res[0].b = ex_b;
    ex_res[1] = '0;
  end

  v1_fpu u_fpu (
    .clk, .rst, .hold_pipe, .hold_div, .dbg_we, .dbg_addr, .dbg_wdata, .dbg_raddr, .dbg_rdata, .dbg_hit,
    .s_v, .s_d(d), .s_c(c), .s_op, .md_dec, .fhz, .fwhy, .fdep, .trap_d,
    .ex, .ma, .sl, .ex_res,
    .di_v, .di_isn, .di_op(s_op), .di_d(d), .di_c(c), .di_md, .di_fcmpd, .di_fcmpd_op, .di_fcmpd_md(md_dec),
    .kill_v, .kill_isn, .kill_incl, .trap_f1, .trap_f1_lane, .trap_f1_isn, .frec,
    .mac_ld_a_v, .mac_ld_b_v, .def_ld_v, .mac_ld_a, .mac_ld_b, .def_ld, .ex_s(2'b00),
    .fr_now, .fpul_now, .fpscr_now, .mach_now, .macl_now,
    .t_busy, .flag_busy, .t_we, .t_val, .t_lane, .idle
  );
  assign fhz0 = fhz[0]; assign fhz1 = fhz[1]; assign fwhy0 = fwhy[0]; assign fwhy1 = fwhy[1];
  assign trap_d0 = trap_d[0]; assign trap_d1 = trap_d[1];
  assign cz_ctr  = u_fpu.cz_ctr;
  assign dv_busy = u_fpu.dv_busy;
  assign dv_rq   = u_fpu.dv_rq;
  assign dv_done = u_fpu.dv_done;
  assign dv_gnt  = u_fpu.dv_gnt;
  assign steal   = u_fpu.steal;
  assign dv_on   = {u_fpu.dv_on[1], u_fpu.dv_on[0]};
  assign ftrv_on = u_fpu.ftrv_on;
  assign fcd_v   = u_fpu.fcd_v;
  logic [31:0] rc_err;
  v1_fpu_reccheck u_rc (
    .clk, .rst, .frec, .fr(u_fpu.fr), .fpul(u_fpu.fpul), .fpscr(u_fpu.fpscr), .mach(u_fpu.mach), .macl(u_fpu.macl),
    .mfl_busy((u_fpu.f1.v && u_fpu.f1.mfl != 2'b00) || (u_fpu.f2.v && u_fpu.f2.mfl != 2'b00) ||
              (u_fpu.fs.v && u_fpu.fs.mfl != 2'b00)), .ovf(u_fpu.rec_ovf_err),
    .err(rc_err)
  );
  logic [31:0] mon_err;
  always_ff @(posedge clk)
    if (rst) mon_err <= '0;
    else mon_err <= mon_err + 32'(u_fpu.pass_err) + 32'(u_fpu.kill_wr_err) + 32'(u_fpu.kill_hold_err) +
                    32'(u_fpu.vec_orph_err);
  assign chk_err = mon_err + rc_err;
  /* verilator lint_off UNUSEDSIGNAL */
  wire unused = ^{fr_now[0]};
  /* verilator lint_on UNUSEDSIGNAL */
endmodule
