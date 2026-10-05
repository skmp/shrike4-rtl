// v1_tmu_tb.sv -- flat-port wrapper around v1_tmu for tb/v1_tmu_tb.cpp (the C++ side drives every input, including
// the reset levels and the clock enables, and compares the outputs with tb/sh4_timers.h every Icyc).
module v1_tmu_tb
  import v1_soc_pkg::*;
(
  input  logic        clk,
  input  logic        rst_pin, rst_por, rst_man, rst_cpu,
  input  logic        bce, pce,
  input  logic        stby, mstp,
  input  logic        pq_rd, pq_wr, pq_dma,
  input  logic [1:0]  pq_size,
  input  logic [27:0] pq_off,
  input  logic [31:0] pq_wdata,
  output logic [31:0] pr_rdata,
  output logic        pr_hit, pr_stall,
  input  logic        tclk_in,
  output logic        tclk_out, tclk_oe,
  input  logic        rtc_tick,
  output logic [2:0]  tuni,
  output logic        ticpi2,
  output logic        dreq_ic,
  input  logic        dack_ic,
  output logic [31:0] next_evt
);
  rst_t     rst;
  clken_t   ce;
  p4s_req_t pq;
  p4s_rsp_t pr;
  assign rst = '{pin: rst_pin, por: rst_por, man: rst_man, cpu: rst_cpu};
  assign ce  = '{bce: bce, pce: pce};
  assign pq  = '{rd: pq_rd, wr: pq_wr, size: pq_size, blk: P4B_TMU, off: pq_off, wdata: pq_wdata, dma: pq_dma};
  assign pr_rdata = pr.rdata;
  assign pr_hit   = pr.hit;
  assign pr_stall = pr.stall;
  v1_tmu u (.clk, .rst, .ce, .stby, .mstp, .pq, .pr, .tclk_in, .tclk_out, .tclk_oe, .rtc_tick, .tuni, .ticpi2,
            .dreq_ic, .dack_ic, .next_evt);
endmodule
