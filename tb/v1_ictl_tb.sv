// v1_ictl_tb.sv -- flat-port wrapper around tp_decode + v1_pkg::v1_ictl_op for tb/v1_ictl_tb.cpp (WI-0b: the SoC
// decoder facts of rtl/v1/SOC.md 1.3.1).  Purely combinational: the C++ side sets op / md, calls eval() and reads the
// control word the D stage would see.
module v1_ictl_tb import tp_pkg::*; import v1_pkg::*; (
  input  logic [15:0] op,
  input  logic [2:0]  md,          // {FPSCR.FR, SZ, PR}
  output logic        valid,       // tp_decode matched a form
  output logic [7:0]  form,
  // the SoC facts
  output logic        priv, slot_ill, fpu_insn, rmw, memop, fpx_vd, illegal,
  output logic [3:0]  dkind,
  output logic [2:0]  fpx_oui,
  output logic [1:0]  form_x,
  // the v1 fields v1_ictl_op sets for the three extra forms
  output logic [2:0]  cls, k,
  output logic        trap, sleep
);
  dec_t  d;
  ictl_t c;
  tp_decode u_dec (.op, .md, .d);
  always_comb c = v1_ictl_op(d.form, op);
  assign valid = d.valid;
  assign form = d.form;
  assign priv = c.priv;
  assign slot_ill = c.slot_ill;
  assign fpu_insn = c.fpu_insn;
  assign rmw = c.rmw;
  assign memop = c.memop;
  assign fpx_vd = c.fpx_vd;
  assign illegal = c.illegal;
  assign dkind = c.dkind;
  assign fpx_oui = c.fpx_oui;
  assign form_x = c.form_x;
  assign cls = c.cls;
  assign k = c.k;
  assign trap = c.trap;
  assign sleep = c.sleep;
  // the rest of dec_t / ictl_t is v1's and is exercised by `make v1`
  /* verilator lint_off UNUSEDSIGNAL */
  wire unused = ^{d, c};
  /* verilator lint_on UNUSEDSIGNAL */
endmodule
