// v1_sci_tb_top.sv -- flat-port wrapper of v1_sci for tb/v1_sci_tb.cpp (WI-SER); `pk_*` are side-effect-free views of
// the register read values (hierarchical references, testbench only), compared with tb/scif_model.h every Pcyc.
module v1_sci_tb_top import v1_soc_pkg::*; #(
  parameter int SIM_TICK_DIV = 0
) (
  input  logic        clk, rst_por, rst_man, pce, mstp, stby,
  input  logic        s_rd, s_wr, s_dma,
  input  logic [1:0]  s_size,
  input  logic [27:0] s_off,
  input  logic [31:0] s_wdata,
  output logic [31:0] r_rdata,
  output logic        r_hit, r_stall,
  output logic        irq_eri, irq_rxi, irq_txi, irq_tei, dreq_tx, dreq_rx,
  output logic        txd_o, txd_oe, sck_o, sck_oe,
  input  logic        rxd_i, sck_i,
  output logic [7:0]  pk_smr, pk_brr, pk_scr, pk_tdr, pk_ssr, pk_rdr, pk_scmr, pk_sptr,
  output logic        pk_tick
);
  rst_t     rst;
  clken_t   ce;
  p4s_req_t pq;
  p4s_rsp_t pr;
  always_comb begin
    rst = '{pin: rst_por, por: rst_por, man: rst_man, cpu: rst_por | rst_man};
    ce  = '{bce: pce, pce: pce};
    pq  = '{rd: s_rd, wr: s_wr, size: s_size, blk: P4B_SCI, off: s_off, wdata: s_wdata, dma: s_dma};
  end
  v1_sci #(.SIM_TICK_DIV(SIM_TICK_DIV)) u (.clk, .rst, .ce, .mstp, .stby, .pq, .pr, .irq_eri, .irq_rxi, .irq_txi,
    .irq_tei, .dreq_tx, .dreq_rx, .txd_o, .txd_oe, .sck_o, .sck_oe, .rxd_i, .sck_i);
  assign r_rdata = pr.rdata;
  assign r_hit   = pr.hit;
  assign r_stall = pr.stall;
  assign pk_smr  = u.smr;
  assign pk_brr  = u.brr;
  assign pk_scr  = u.scr;
  assign pk_tdr  = u.tdr;
  assign pk_ssr  = {u.tdre, u.rdrf, u.orer, u.fer, u.per, u.tend, u.mpb, u.mpbt};
  assign pk_rdr  = u.rdr;
  assign pk_scmr = {4'd0, u.sdir, u.sinv, 1'b0, u.smif};
  assign pk_sptr = {u.eio, 3'd0, u.spb1io, u.sck_s[1], u.spb0io, u.rxd_s[1]};
  assign pk_tick = u.tick;
endmodule
