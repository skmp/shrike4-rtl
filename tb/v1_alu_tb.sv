// v1_alu_tb.sv -- flat-port wrapper around the unit-A packages (rtl/v1/v1_alu.sv, v1_mul.sv) for tb/v1_alu_tb.cpp.
// Purely combinational: the C++ side sets the inputs, calls eval() and reads the outputs.
module v1_alu_tb (
  // v1_alu
  input  logic [5:0]  op,
  input  logic [31:0] a, b,
  input  logic        t, q, m,
  output logic [31:0] r,
  output logic        ot, oq, om, owr_t, owr_qm,
  // v1_mul
  input  logic [2:0]  mop,
  input  logic [31:0] ma, mb,
  input  logic        ms,
  input  logic [63:0] macc,
  output logic [63:0] mres
);
  v1_alu_pkg::alu_out_t o;
  always_comb o = v1_alu_pkg::v1_alu(v1_alu_pkg::alu_op_e'(op), a, b, t, q, m);
  assign r = o.r;
  assign ot = o.t;
  assign oq = o.q;
  assign om = o.m;
  assign owr_t = o.wr_t;
  assign owr_qm = o.wr_qm;
  always_comb mres = v1_mul_pkg::v1_mul(v1_mul_pkg::mul_op_e'(mop), ma, mb, ms, macc);
endmodule
