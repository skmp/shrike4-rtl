// v1_rtc_tb.sv -- flat-port wrapper around v1_rtc for tb/v1_rtc_tb.cpp (the C++ side drives the reset levels, the
// enables, the 32.768 kHz tick and module stop, and compares the outputs with tb/sh4_timers.h every Icyc).
module v1_rtc_tb
  import v1_soc_pkg::*;
(
  input  logic        clk,
  input  logic        rst_pin, rst_por, rst_man, rst_cpu,
  input  logic        bce, pce,
  input  logic        osc_tick, mstp,
  input  logic        pq_rd, pq_wr, pq_dma,
  input  logic [1:0]  pq_size,
  input  logic [27:0] pq_off,
  input  logic [31:0] pq_wdata,
  output logic [31:0] pr_rdata,
  output logic        pr_hit, pr_stall,
  output logic        rtc_tick, ati, pri, cui
);
  rst_t     rst;
  clken_t   ce;
  p4s_req_t pq;
  p4s_rsp_t pr;
  assign rst = '{pin: rst_pin, por: rst_por, man: rst_man, cpu: rst_cpu};
  assign ce  = '{bce: bce, pce: pce};
  assign pq  = '{rd: pq_rd, wr: pq_wr, size: pq_size, blk: P4B_RTC, off: pq_off, wdata: pq_wdata, dma: pq_dma};
  assign pr_rdata = pr.rdata;
  assign pr_hit   = pr.hit;
  assign pr_stall = pr.stall;
  v1_rtc u (.clk, .rst, .ce, .osc_tick, .mstp, .pq, .pr, .rtc_tick, .ati, .pri, .cui);
endmodule
