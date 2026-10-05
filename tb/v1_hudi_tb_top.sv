// v1_hudi_tb_top.sv -- flat-port wrapper around rtl/v1/v1_hudi.sv for tb/v1_hudi_tb.cpp.  SDDR_INIT is the console's
// power-on SDDR content (p4probe D / I), so the replay compares every value it printed.
module v1_hudi_tb_top import v1_soc_pkg::*; (
  input  logic        clk,
  input  logic [3:0]  rst_i,
  input  logic        bce, pce,
  input  logic        s_rd, s_wr,
  input  logic [1:0]  s_size,
  input  logic [27:0] s_off,
  input  logic [31:0] s_wdata,
  output logic [31:0] s_rdata,
  output logic        s_hit, s_stall,
  output logic        irq, rst_req
);
  rst_t     rst;
  clken_t   ce;
  p4s_req_t pq;
  p4s_rsp_t pr;
  assign rst = '{pin: rst_i[3], por: rst_i[2], man: rst_i[1], cpu: rst_i[0]};
  assign ce = '{bce: bce, pce: pce};
  always_comb pq = '{rd: s_rd, wr: s_wr, size: s_size, blk: P4B_HUDI, off: s_off, wdata: s_wdata, dma: 1'b0};
  v1_hudi #(.SDDR_INIT(32'h5573A3AB)) u (.clk, .rst, .ce, .pq, .pr, .irq, .rst_req);
  assign s_rdata = pr.rdata; assign s_hit = pr.hit; assign s_stall = pr.stall;
endmodule
