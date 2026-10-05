// serial_bm_top.sv -- the RTL side of tb/bm/serial/run_rtl.cpp (WI-SER): v1_scif and v1_sci as the chip has them,
// behind one scalar P4 slave port (sel 0 = P4B_SCI, 1 = P4B_SCIF), with the Dreamcast pin tie-offs (SOC.md 3.6:
// RxD2 = CTS2 = SCK2 = 1, RxD1 = SCK1 = 1).  The C++ side is the bridge: it strobes on pce cycles only.
module serial_bm_top import v1_soc_pkg::*; (
  input  logic        clk, rst_por, pce, mstp_sci, mstp_scif,
  input  logic        s_rd, s_wr, s_dma, sel,
  input  logic [1:0]  s_size,
  input  logic [27:0] s_off,
  input  logic [31:0] s_wdata,
  output logic [31:0] r_rdata,
  output logic        r_hit,
  output logic        f_eri, f_rxi, f_bri, f_txi, f_dreq_tx, f_dreq_rx,
  output logic        c_eri, c_rxi, c_txi, c_tei, c_dreq_tx, c_dreq_rx,
  output logic        txd2, txd2_oe,
  output logic        tap_tx_v,
  output logic [7:0]  tap_tx_byte
);
  rst_t      rst;
  clken_t    ce;
  p4s_req_t  q_sci, q_scif;
  p4s_rsp_t  p_sci, p_scif;
  scif_tap_t tap;
  logic      rts2, rts2_oe, cts2, cts2_oe, txd1, txd1_oe, sck1, sck1_oe;
  always_comb begin
    rst    = '{pin: rst_por, por: rst_por, man: 1'b0, cpu: rst_por};
    ce     = '{bce: pce, pce: pce};
    q_sci  = '{rd: s_rd && !sel, wr: s_wr && !sel, size: s_size, blk: P4B_SCI, off: s_off, wdata: s_wdata, dma: s_dma};
    q_scif = '{rd: s_rd && sel, wr: s_wr && sel, size: s_size, blk: P4B_SCIF, off: s_off, wdata: s_wdata, dma: s_dma};
  end
  v1_scif u_scif (.clk, .rst, .ce, .mstp(mstp_scif), .pq(q_scif), .pr(p_scif),
                  .irq_eri(f_eri), .irq_rxi(f_rxi), .irq_bri(f_bri), .irq_txi(f_txi),
                  .dreq_tx(f_dreq_tx), .dreq_rx(f_dreq_rx),
                  .txd_o(txd2), .txd_oe(txd2_oe), .rts_o(rts2), .rts_oe(rts2_oe), .cts_o(cts2), .cts_oe(cts2_oe),
                  .rxd_i(1'b1), .cts_i(1'b1), .sck_i(1'b1), .tap);
  v1_sci  u_sci  (.clk, .rst, .ce, .mstp(mstp_sci), .stby(1'b0), .pq(q_sci), .pr(p_sci),
                  .irq_eri(c_eri), .irq_rxi(c_rxi), .irq_txi(c_txi), .irq_tei(c_tei),
                  .dreq_tx(c_dreq_tx), .dreq_rx(c_dreq_rx),
                  .txd_o(txd1), .txd_oe(txd1_oe), .sck_o(sck1), .sck_oe(sck1_oe), .rxd_i(1'b1), .sck_i(1'b1));
  assign r_rdata     = p_sci.rdata | p_scif.rdata;
  assign r_hit       = p_sci.hit | p_scif.hit;
  assign tap_tx_v    = tap.tx_v;
  assign tap_tx_byte = tap.tx_byte;
endmodule
