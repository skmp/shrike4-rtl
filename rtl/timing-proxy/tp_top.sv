// tp_top.sv -- SH7091 (SH-4) pipeline timing proxy, top level.  See README.md.
//
// A trace-driven, cycle-level model of the CPU and FPU pipelines: fetch, the two-wide D stage (pairing, interlocks,
// multi-cycle issue), the integer and FP pipelines with their stage locks, and the PRFC0 events the sweep measured.
// No caches (every access hits), no MMU, no bus: memory-access time never varies.  The cache-block operations' fixed
// pipeline cost (OCBI / OCBP freeze, OCBWB's busy operand cache) is modelled; their bus traffic is not.
//
// Trace port: the next two instructions in program order (t0 older).  The functional side supplies what only
// execution knows: whether a branch is taken, and FPSCR.FR / SZ / PR.  t_take says how many the fetch unit took.
module tp_top import tp_pkg::*; (
  input  logic        clk,
  input  logic        rst,
  input  logic        t0_valid, t1_valid,
  input  logic [31:0] t0_pc, t1_pc,
  input  logic [15:0] t0_op, t1_op,
  input  logic [2:0]  t0_md, t1_md,
  input  logic        t0_tk, t1_tk,
  input  logic [31:0] t0_seq, t1_seq,
  output logic [1:0]  t_take,
  // issue report: an instruction issued this cycle (the first issue cycle of a multi-cycle one)
  output logic        i0_valid, i1_valid,
  output logic [31:0] i0_seq, i1_seq,
  // PRFC0 events this cycle: 0x14 parallel issue, 0x28 freeze by CPU register, 0x29 freeze by FPU
  output logic        ev_par, ev_frzreg, ev_frzfpu,
  // debug: the D-stage window and why each slot did not issue (tp_core WHY_* codes)
  output logic [31:0] dbg_d0_seq, dbg_d1_seq,
  output logic [7:0]  dbg_why0, dbg_why1
);
  tr_t t0, t1, q0, q1;
  assign t0 = '{seq: t0_seq, pc: t0_pc, op: t0_op, md: t0_md, tk: t0_tk};
  assign t1 = '{seq: t1_seq, pc: t1_pc, op: t1_op, md: t1_md, tk: t1_tk};

  logic q0_v, q1_v, q0_tgt, q1_tgt, redirect, refetch, freeze;
  logic [1:0] pop;

  tp_fetch u_fetch (
    .clk, .rst,
    .t0_v(t0_valid), .t1_v(t1_valid), .t0, .t1, .take(t_take),
    .q0_v, .q1_v, .q0, .q1, .q0_tgt, .q1_tgt, .pop, .redirect, .refetch, .freeze
  );

  tp_core u_core (
    .clk, .rst,
    .q0_v, .q1_v, .q0, .q1, .q0_tgt, .q1_tgt, .pop, .redirect, .refetch, .freeze,
    .i0_valid, .i1_valid, .i0_seq, .i1_seq,
    .ev_par, .ev_frzreg, .ev_frzfpu,
    .dbg_d0_seq, .dbg_d1_seq, .dbg_why0, .dbg_why1
  );
endmodule
