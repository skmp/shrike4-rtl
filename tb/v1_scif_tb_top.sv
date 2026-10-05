// v1_scif_tb_top.sv -- flat-port wrapper of v1_scif for tb/v1_scif_tb.cpp (WI-SER).  The P4 slave port is driven as
// scalars; `pk_*` are side-effect-free views of the register read values (hierarchical references, testbench only)
// that the C++ side compares with tb/scif_model.h every Pcyc.
module v1_scif_tb_top import v1_soc_pkg::*; #(
  parameter int SIM_TICK_DIV = 0
) (
  input  logic        clk, rst_por, rst_man, pce, mstp,
  input  logic        s_rd, s_wr, s_dma,
  input  logic [1:0]  s_size,
  input  logic [27:0] s_off,
  input  logic [31:0] s_wdata,
  output logic [31:0] r_rdata,
  output logic        r_hit, r_stall,
  output logic        irq_eri, irq_rxi, irq_bri, irq_txi, dreq_tx, dreq_rx,
  output logic        txd_o, txd_oe, rts_o, rts_oe, cts_o, cts_oe,
  input  logic        rxd_i, cts_i, sck_i,
  output logic        tap_tx_v, tap_rx_v,
  output logic [7:0]  tap_tx_byte, tap_rx_byte,
  output logic [15:0] pk_smr, pk_brr, pk_scr, pk_fsr, pk_frdr, pk_fcr, pk_fdr, pk_sptr, pk_lsr,
  output logic        pk_tick, pk_txline
);
  rst_t      rst;
  clken_t    ce;
  p4s_req_t  pq;
  p4s_rsp_t  pr;
  scif_tap_t tap;
  always_comb begin
    rst = '{pin: rst_por, por: rst_por, man: rst_man, cpu: rst_por | rst_man};
    ce  = '{bce: pce, pce: pce};
    pq  = '{rd: s_rd, wr: s_wr, size: s_size, blk: P4B_SCIF, off: s_off, wdata: s_wdata, dma: s_dma};
  end
  v1_scif #(.SIM_TICK_DIV(SIM_TICK_DIV)) u (.clk, .rst, .ce, .mstp, .pq, .pr, .irq_eri, .irq_rxi, .irq_bri, .irq_txi,
    .dreq_tx, .dreq_rx, .txd_o, .txd_oe, .rts_o, .rts_oe, .cts_o, .cts_oe, .rxd_i, .cts_i, .sck_i, .tap);
  assign r_rdata = pr.rdata;
  assign r_hit   = pr.hit;
  assign r_stall = pr.stall;
  assign {tap_tx_v, tap_tx_byte, tap_rx_v, tap_rx_byte} = {tap.tx_v, tap.tx_byte, tap.rx_v, tap.rx_byte};
  assign pk_smr  = {8'd0, u.smr & 8'h7B};
  assign pk_brr  = {8'd0, u.brr};
  assign pk_scr  = {8'd0, u.scr & 8'hFA};
  assign pk_fsr  = u.fsr_v;
  assign pk_frdr = {8'd0, u.rhead[7:0]};
  assign pk_fcr  = {8'd0, u.fcr};
  assign pk_fdr  = u.fdr_v;
  assign pk_sptr = u.sptr_v;
  assign pk_lsr  = {15'd0, u.orer};
  assign pk_tick = u.tick;
  assign pk_txline = u.tx_line;
endmodule
