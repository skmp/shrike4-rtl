// v1_fpdn_tb.sv -- WI-FPDN unit top (tb/v1_fpdn_tb.cpp): every v1_fp_pkg function with FPSCR.DN (`dn0` = DN == 0)
// behind one opcode mux (combinational, evaluated on its own), one v1_fdiv and one fpu_vec_top with their dn0 inputs.
module v1_fpdn_tb
  import v1_fp_pkg::*;
  import fpu_vec_pkg::*;
(
  input  logic        clk, rst,
  // combinational functions
  input  logic [3:0]  op,       // 0 fadd_s 1 fmul_s 2 fmac_s 3 fadd_d 4 fmul_d 5 fcmp_s 6 fcmp_d 7 ftrc_s 8 ftrc_d
                                // 9 fcnvsd 10 fcnvds 11 fdiv_spec (sq, dbl) 12 float_s 13 float_d
  input  logic        rz, dn0, sq, dbl,
  input  logic [63:0] a, b,
  input  logic [31:0] cc,       // FMAC's addend
  output logic [63:0] rr,       // the result
  output logic [4:0]  x,        // {v, z, o, u, i}
  output logic        e,        // the FPU error
  output logic        spec,     // fdiv_spec: a special case
  output logic [3:0]  cmp,      // {eq, gt, v_eq, v_gt}
  // divider
  input  logic        d_start, d_sqrt, d_dbl, d_rz, d_dn0,
  input  logic [63:0] d_a, d_b,
  output logic        d_busy, d_done,
  output logic [63:0] d_r,
  output logic [4:0]  d_x,
  // vector unit
  input  logic        v_start,
  input  logic [1:0]  v_op,     // 0 FIPR 1 FSCA 2 FSRRA
  input  logic        v_rz, v_dn0,
  input  logic [31:0] fva0, fva1, fva2, fva3, fvb0, fvb1, fvb2, fvb3, v_fpul, v_frn,
  output logic        v_valid, v_second,
  output logic [31:0] v_res,
  output logic [4:0]  v_x,
  output logic        v_e,
  output logic [4:0]  v_stx,    // the start operands' special cause (combinational, the start cycle)
  output logic        v_ste
);
  fs_res_t fs;
  fd_res_t fd;
  fcmp_t   fc;
  dspec_t  ds;
  always_comb begin
    rr = '0; x = '0; e = 1'b0; spec = 1'b0; cmp = '0; fs = '0; fd = '0; fc = '0; ds = '0;
    case (op)
      4'd0:  begin fs = fadd_s(a[31:0], b[31:0], rz, dn0);       rr = {32'h0, fs.r}; x = fs.x; e = fs.e; end
      4'd1:  begin fs = fmul_s(a[31:0], b[31:0], rz, dn0);       rr = {32'h0, fs.r}; x = fs.x; e = fs.e; end
      4'd2:  begin fs = fmac_s(a[31:0], b[31:0], cc, rz, dn0);    rr = {32'h0, fs.r}; x = fs.x; e = fs.e; end
      4'd3:  begin fd = fadd_d(a, b, rz, dn0);                   rr = fd.r;          x = fd.x; e = fd.e; end
      4'd4:  begin fd = fmul_d(a, b, rz, dn0);                   rr = fd.r;          x = fd.x; e = fd.e; end
      4'd5:  begin fc = fcmp_s(a[31:0], b[31:0], dn0);           cmp = fc; end
      4'd6:  begin fc = fcmp_d(a, b, dn0);                       cmp = fc; end
      4'd7:  begin fs = ftrc_s(a[31:0]);                         rr = {32'h0, fs.r}; x = fs.x; e = fs.e; end
      4'd8:  begin fs = ftrc_d(a);                               rr = {32'h0, fs.r}; x = fs.x; e = fs.e; end
      4'd9:  begin fd = fcnvsd(a[31:0], dn0);                    rr = fd.r;          x = fd.x; e = fd.e; end
      4'd10: begin fs = fcnvds(a, rz, dn0);                      rr = {32'h0, fs.r}; x = fs.x; e = fs.e; end
      4'd11: begin ds = fdiv_spec(a, b, sq, dbl, dn0);           rr = ds.r; x = ds.x; e = ds.e; spec = ds.spec; end
      4'd12: begin fs = float_s(a[31:0], rz);                    rr = {32'h0, fs.r}; x = fs.x; e = fs.e; end
      4'd13: begin fd = float_d(a[31:0]);                        rr = fd.r;          x = fd.x; e = fd.e; end
      default: ;
    endcase
  end

  fexc_t dx;
  /* verilator lint_off UNUSEDSIGNAL */          // the result-pass hooks: the FPU's, not this test's
  logic  d_last, d_rq;
  /* verilator lint_on UNUSEDSIGNAL */
  v1_fdiv u_div (.clk, .rst, .start(d_start), .hold(1'b0), .sqrt(d_sqrt), .dbl(d_dbl), .rz(d_rz), .dn0(d_dn0),
                 .a(d_a), .b(d_b), .isn(8'd0), .kill_v(1'b0), .kill_isn(8'd0), .kill_incl(1'b0),
                 .busy(d_busy), .last(d_last), .rq(d_rq), .done(d_done), .r(d_r), .x(dx));
  assign d_x = dx;

  logic [31:0] fva [4], fvb [4];
  assign fva[0] = fva0; assign fva[1] = fva1; assign fva[2] = fva2; assign fva[3] = fva3;
  assign fvb[0] = fvb0; assign fvb[1] = fvb1; assign fvb[2] = fvb2; assign fvb[3] = fvb3;
  /* verilator lint_off UNUSEDSIGNAL */
  logic        v_busy;
  /* verilator lint_on UNUSEDSIGNAL */
  fpu_vec_top u_vec (.clk, .rst, .start(v_start), .hold(1'b0), .op(op_e'(v_op)), .rz(v_rz), .dn0(v_dn0), .fva, .fvb,
                     .fpul(v_fpul), .frn(v_frn), .isn(8'd0), .kill_v(1'b0), .kill_isn(8'd0), .kill_incl(1'b0),
                     .st_x(v_stx), .st_e(v_ste), .busy(v_busy), .res_valid(v_valid), .res_second(v_second),
                     .res(v_res), .res_x(v_x), .res_e(v_e));
endmodule
