// v1_ccn_id_tb_top.sv -- flat-port wrapper around rtl/v1/v1_ccn_id.sv for tb/v1_ccn_id_tb.cpp.
module v1_ccn_id_tb_top import v1_soc_pkg::*; (
  input  logic        s_rd, s_wr,
  input  logic [1:0]  s_size,
  input  logic [27:0] s_off,
  input  logic [31:0] s_wdata,
  output logic [31:0] s_rdata,
  output logic        s_hit, s_stall
);
  p4s_req_t pq;
  p4s_rsp_t pr;
  always_comb pq = '{rd: s_rd, wr: s_wr, size: s_size, blk: P4B_CCN_ID, off: s_off, wdata: s_wdata, dma: 1'b0};
  v1_ccn_id u (.pq, .pr);
  assign s_rdata = pr.rdata; assign s_hit = pr.hit; assign s_stall = pr.stall;
endmodule
