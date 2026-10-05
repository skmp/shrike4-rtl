// fpu_vec_tb_top.sv -- thin wrapper so the Verilator testbench sees flat ports.
module fpu_vec_tb_top
  import fpu_vec_pkg::*;
(
  input  logic        clk,
  input  logic        rst,
  input  logic        start,
  input  logic [1:0]  op,
  input  logic        rz,
  input  logic [31:0] fva0, fva1, fva2, fva3,
  input  logic [31:0] fvb0, fvb1, fvb2, fvb3,
  input  logic [31:0] fpul,
  input  logic [31:0] frn,
  output logic        busy,
  output logic        res_valid,
  output logic        res_second,
  output logic [31:0] res,
  // internal observation for the structural work: product registers after each step
  output logic [31:0] pr0, pr1, pr2, pr3,
  output logic [2:0]  step
);
  logic [31:0] fva [4], fvb [4];
  assign fva[0] = fva0; assign fva[1] = fva1; assign fva[2] = fva2; assign fva[3] = fva3;
  assign fvb[0] = fvb0; assign fvb[1] = fvb1; assign fvb[2] = fvb2; assign fvb[3] = fvb3;
  // the WI-CORE-F ports: no kill here (isn / kill tied off); st_x is the host's operand trap check
  /* verilator lint_off PINCONNECTEMPTY */
  // (WI-FPDN: dn0 = 0 is FPSCR.DN = 1, the dumps' mode; st_e / res_e unused here -- tb/v1_fpdn_tb covers DN = 0)
  fpu_vec_top u_dut (.clk, .rst, .start, .hold(1'b0), .op(op_e'(op)), .rz, .dn0(1'b0), .fva, .fvb, .fpul, .frn,
                     .isn(8'd0), .kill_v(1'b0), .kill_isn(8'd0), .kill_incl(1'b0), .st_x(), .st_e(),
                     .busy, .res_valid, .res_second, .res, .res_x(), .res_e());
  /* verilator lint_on PINCONNECTEMPTY */
  assign pr0 = 32'(u_dut.pr[0]); assign pr1 = 32'(u_dut.pr[1]); assign pr2 = 32'(u_dut.pr[2]); assign pr3 = 32'(u_dut.pr[3]);
  assign step = u_dut.step;
endmodule
