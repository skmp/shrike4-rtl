// v1_perfctr_tb_top.sv -- flat-port wrapper around rtl/v1/v1_perfctr.sv for tb/v1_perfctr_tb.cpp (the C++ side is the
// bridge, the core's event sources, the CPG's ratio_inc and the reset controller).
module v1_perfctr_tb_top import v1_soc_pkg::*; (
  input  logic        clk,
  input  logic [3:0]  rst_i,                   // {pin, por, man, cpu}
  input  logic        run,
  input  logic [4:0]  ratio_inc,
  // PMCR port (block P4B_CCN_PMCR)
  input  logic        c_rd, c_wr,
  input  logic [1:0]  c_size,
  input  logic [27:0] c_off,
  input  logic [31:0] c_wdata,
  output logic [31:0] c_rdata,
  output logic        c_hit, c_stall,
  // PMCTR port (block P4B_PMCTR)
  input  logic        t_rd, t_wr,
  input  logic [1:0]  t_size,
  input  logic [27:0] t_off,
  input  logic [31:0] t_wdata,
  output logic [31:0] t_rdata,
  output logic        t_hit, t_stall,
  input  logic [63:0] ev
);
  rst_t     rst;
  p4s_req_t pq_ccn, pq;
  p4s_rsp_t pr_ccn, pr;
  assign rst = '{pin: rst_i[3], por: rst_i[2], man: rst_i[1], cpu: rst_i[0]};
  always_comb begin
    pq_ccn = '{rd: c_rd, wr: c_wr, size: c_size, blk: P4B_CCN_PMCR, off: c_off, wdata: c_wdata, dma: 1'b0};
    pq     = '{rd: t_rd, wr: t_wr, size: t_size, blk: P4B_PMCTR,    off: t_off, wdata: t_wdata, dma: 1'b0};
  end
  v1_perfctr u (.clk, .rst, .run, .ratio_inc, .pq_ccn, .pr_ccn, .pq, .pr, .ev(pmc_ev_t'(ev)));
  assign c_rdata = pr_ccn.rdata; assign c_hit = pr_ccn.hit; assign c_stall = pr_ccn.stall;
  assign t_rdata = pr.rdata;     assign t_hit = pr.hit;     assign t_stall = pr.stall;
endmodule
