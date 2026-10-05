// v1_fp_tb_top.sv -- flat-port wrapper for tb/v1_fp_tb.cpp: every v1_fp_pkg function behind one opcode mux
// (combinational, evaluated on its own), plus one v1_fdiv instance with its own ports.
module v1_fp_tb_top
  import v1_fp_pkg::*;
(
  input  logic        clk, rst,
  // combinational functions
  input  logic [3:0]  op,       // 0 fadd_s 1 fmul_s 2 fmac_s 3 fadd_d 4 fmul_d 5 fcmp_s 6 fcmp_d 7 float_s
                                // 8 float_d 9 ftrc_s 10 ftrc_d 11 fcnvsd 12 fcnvds
  input  logic        rz,
  input  logic [63:0] a, b,
  input  logic [31:0] c,
  output logic [63:0] r,
  output logic [4:0]  x,        // {v, z, o, u, i}
  output logic [3:0]  cmp,      // {eq, gt, v_eq, v_gt}
  // divider
  input  logic        d_start, d_sqrt, d_dbl, d_rz,
  input  logic [63:0] d_a, d_b,
  output logic        d_busy, d_done,
  output logic [63:0] d_r,
  output logic [4:0]  d_x
);
  /* verilator lint_off UNUSEDSIGNAL */          // fs.e / fd.e: the FPU error (DN = 0 only; tb/v1_fpdn_tb tests it)
  fs_res_t fs;
  fd_res_t fd;
  /* verilator lint_on UNUSEDSIGNAL */
  fcmp_t   fc;
  always_comb begin
    r = '0; x = '0; cmp = '0; fs = '0; fd = '0; fc = '0;
    case (op)
      4'd0:  begin fs = fadd_s(a[31:0], b[31:0], rz);       r = {32'h0, fs.r}; x = fs.x; end
      4'd1:  begin fs = fmul_s(a[31:0], b[31:0], rz);       r = {32'h0, fs.r}; x = fs.x; end
      4'd2:  begin fs = fmac_s(a[31:0], b[31:0], c, rz);    r = {32'h0, fs.r}; x = fs.x; end
      4'd3:  begin fd = fadd_d(a, b, rz);                   r = fd.r;          x = fd.x; end
      4'd4:  begin fd = fmul_d(a, b, rz);                   r = fd.r;          x = fd.x; end
      4'd5:  begin fc = fcmp_s(a[31:0], b[31:0]);           cmp = fc; end
      4'd6:  begin fc = fcmp_d(a, b);                       cmp = fc; end
      4'd7:  begin fs = float_s(a[31:0], rz);               r = {32'h0, fs.r}; x = fs.x; end
      4'd8:  begin fd = float_d(a[31:0]);                   r = fd.r;          x = fd.x; end
      4'd9:  begin fs = ftrc_s(a[31:0]);                    r = {32'h0, fs.r}; x = fs.x; end
      4'd10: begin fs = ftrc_d(a);                          r = {32'h0, fs.r}; x = fs.x; end
      4'd11: begin fd = fcnvsd(a[31:0]);                    r = fd.r;          x = fd.x; end
      4'd12: begin fs = fcnvds(a, rz);                      r = {32'h0, fs.r}; x = fs.x; end
      default: ;
    endcase
  end

  fexc_t dx;
  // (the WI-CORE-F ports: no kill here; `last` / `rq` are the FPU's result-pass hooks, unused by this test)
  /* verilator lint_off UNUSEDSIGNAL */
  logic  d_last, d_rq;
  /* verilator lint_on UNUSEDSIGNAL */
  // (WI-FPDN: dn0 = 0 is FPSCR.DN = 1, this test's reference; tb/v1_fpdn_tb covers DN = 0)
  v1_fdiv u_div (.clk, .rst, .start(d_start), .hold(1'b0), .sqrt(d_sqrt), .dbl(d_dbl), .rz(d_rz), .dn0(1'b0), .a(d_a), .b(d_b),
                 .isn(8'd0), .kill_v(1'b0), .kill_isn(8'd0), .kill_incl(1'b0),
                 .busy(d_busy), .last(d_last), .rq(d_rq), .done(d_done), .r(d_r), .x(dx));
  assign d_x = dx;
endmodule
