// v1_fpu_reccheck_bind.sv -- tb/v1_fpu_reccheck.sv bound into every v1_fpu of a build whose top is not WI-CORE-F's
// (make v1-fpuhold part 4: the random FPU programs on WI-CORE-A's v1-exc top, tb/v1_fpuhold_x.cpp).  FATAL: the first
// mismatch between the FPU's commit-log records and its register writes stops the run (the program's report then
// never says "0 differ").
/* verilator lint_off PINCONNECTEMPTY */
bind v1_fpu v1_fpu_reccheck #(.FATAL(1'b1)) u_rc (
  .clk, .rst, .frec, .fr, .fpul, .fpscr, .mach, .macl,
  .mfl_busy((f1.v && f1.mfl != 2'b00) || (f2.v && f2.mfl != 2'b00) || (fs.v && fs.mfl != 2'b00)), .ovf(rec_ovf_err),
  .err()
);
/* verilator lint_on PINCONNECTEMPTY */
