// v1_dmac_int_top.sv -- WI-DMAC integration harness: v1_dmac with the real v1_bsc (WI-BSC), v1_p4bus (WI-P4) and
// v1_sci (WI-SER), flat ports for tb/v1_dmac_int.cpp (`make v1-dmac-int`, not part of `make v1-dmac`: it builds the
// other work items' RTL as it is on disk).  It holds the DMAC to the real port conventions its unit testbench's stubs
// only model:
//   * v1_bsc on the DMAC's bq / bs / bwd port (SOC.md 3.3.2: rd_beat = addr[4:3] and one rd_v for a non-line read; a
//     DMAC 8-byte unit is not split),
//     with the CPU requester of the BSC driven from C++ and the external bus answered by a C++ SDRAM board;
//   * v1_p4bus with the CPU as master 0 (C++) and the DMAC as master 1: the CPU reaches the DMAC, BSC and SCI
//     registers through it, the DMAC's on-chip legs reach the real SCI (TDRE request, auto-clear on its own write);
//   * every other P4 slave port is a null register (hit, read 0, no stall), so CPU traffic to the TMU block only
//     exercises the Pclk path.
// Adapted from the independent verifier's vd_top.sv / vp_top.sv (agents/verify/WI-DMAC/int).
module v1_dmac_int_top import v1_soc_pkg::*; (
  input  logic        clk,
  input  logic        rst_pin, rst_por, rst_man, rst_cpu,
  input  logic        bce, pce,
  // CPU: P4 bridge master 0
  input  logic        m0_v, m0_we,
  input  logic [1:0]  m0_size,
  input  logic [31:0] m0_addr, m0_wdata,
  output logic        m0_ack,
  output logic [31:0] m0_rdata,
  // CPU: the BSC's CPU requester (uncached reads, as v1_biu would present them)
  input  logic        c_v,
  input  logic [28:0] c_addr,
  output logic        c_ready, c_rd_v, c_rd_last,
  // DDT (transaction level)
  input  logic        tl_dtr_v,
  input  logic [63:0] tl_dtr,
  input  logic        tl_ch2, tl_trrep,
  // external bus (the board)
  output logic        xb_start, xb_we, xb_beat, xb_dack,
  output logic [2:0]  xb_area,
  output logic [28:0] xb_baddr,
  output logic [7:0]  xb_be,
  output logic [63:0] xb_wdata,
  input  logic        xb_rdy,
  input  logic [63:0] xb_rdata,
  // taps
  output logic        t_acc_v, t_upd_v,
  output logic [1:0]  t_acc_ch, t_upd_ch,
  output logic        t_dma_ready, t_dma_we,
  output logic [1:0]  t_dma_ch,
  output logic [23:0] t_tcr0, t_tcr1, t_tcr2, t_tcr3,
  output logic        t_tdr_dma_wr,            // the SCI's SCTDR1 strobe from the DMAC (not stalled) this cycle
  output logic        t_tdre,                  // the SCI's TDRE request this cycle
  output logic        t_p4w_ack,               // the bridge acks a DMAC write this cycle
  output logic        txd, txd_oe
);
  rst_t   rst;
  clken_t ce;
  assign rst = '{pin: rst_pin, por: rst_por, man: rst_man, cpu: rst_cpu};
  assign ce  = '{bce: bce, pce: pce};

  // ---- the P4 bridge
  p4m_req_t m_req [2];
  p4m_rsp_t m_rsp [2];
  p4s_req_t s_req [PP_N];
  p4s_rsp_t s_rsp [PP_N];
  logic arr_strobe, bpost_bsc;
  assign m_req[0] = '{v: m0_v, we: m0_we, size: m0_size, addr: m0_addr, wdata: m0_wdata, dma: 1'b0};
  assign m0_ack = m_rsp[0].ack;
  assign m0_rdata = m_rsp[0].rdata;
  v1_p4bus u_bus (.clk, .rst, .ce, .m_req, .m_rsp, .last_rdata(32'd0), .s_req, .s_rsp, .arr_strobe, .bpost_bsc,
                 .pclk_wpend());

  // ---- the DMAC
  bq_t  dma_bq;
  bs_t  dma_bs;
  bwd_t dma_bwd;
  ddt_tl_t ddt_tl;
  dreq_t dreq;
  logic dack_ic, bavl_n, tdack_n, ddt_bus_req, ddt_bavl, dmae, sci_dreq_tx, sci_dreq_rx, dma_rel, dma_ses;
  logic [1:0] ddt_id;
  logic [3:0] dmte;
  assign ddt_tl = '{dtr_v: tl_dtr_v, dtr: tl_dtr, ch2_req: tl_ch2, tr_repeat: tl_trrep};
  assign dreq = '{sci_tx: sci_dreq_tx, sci_rx: sci_dreq_rx, scif_tx: 1'b0, scif_rx: 1'b0, tmu_ic: 1'b0};
  v1_dmac u_dmac (
    .clk, .rst, .ce, .run(1'b1), .stby(1'b0), .mstp(1'b0), .pq(s_req[PP_DMAC]), .pr(s_rsp[PP_DMAC]), .p4q(m_req[1]),
    .p4r(m_rsp[1]), .bq(dma_bq), .bs(dma_bs), .bwd(dma_bwd), .dreq, .dack_ic, .dbreq_n(1'b1), .tr_n(1'b1), .bavl_n,
    .tdack_n, .ddt_id, .ddt_tl, .ddt_d(xb_rdata), .ddt_bus_req, .ddt_bavl, .bus_rel(dma_rel), .bus_ses(dma_ses),
    .nmi_evt(1'b0), .dmte,
    .dmae);

  // ---- the BSC
  bq_t  cpu_bq;
  bs_t  cpu_bs;
  bwd_t cpu_bwd;
  xb_out_t xb_o;
  xb_in_t  xb_i;
  logic [19:0] port_o, port_oe;
  logic rcmi, rovi, gpio, back_n;
  commit_rec_t [CT_BSC_N-1:0] btap;
  assign cpu_bq  = '{v: c_v, r: '{we: 1'b0, addr: c_addr, size: BZ_L, lock: 1'b0, master: BM_CPU, ch: 2'd0}};
  assign cpu_bwd = '{wd_data: 64'd0, wd_be: 8'd0};
  assign c_ready = cpu_bs.ready;  assign c_rd_v = cpu_bs.rd_v;  assign c_rd_last = cpu_bs.rd_last;
  assign xb_i = '{rdy: xb_rdy, rdata: xb_rdata};
  v1_bsc u_bsc (
    .clk, .rst, .ce, .pq(s_req[PP_BSC]), .pr(s_rsp[PP_BSC]), .pq_sdmr(s_req[PP_SDMR]), .pr_sdmr(s_rsp[PP_SDMR]),
    .bpost_bsc, .cpu_bq, .cpu_bs, .cpu_bwd, .dma_bq, .dma_bs, .dma_bwd, .xb_o, .xb_i, .breq_n(1'b1), .back_n,
    .ddt_bus_req, .ddt_bavl, .dma_rel, .dma_ses,
    .port_i(20'h01CE3), .port_o, .port_oe, .md(9'h065), .rcmi, .rovi, .gpio, .tap(btap));
  assign xb_start = xb_o.start;  assign xb_we = xb_o.we;  assign xb_beat = xb_o.beat;  assign xb_dack = xb_o.dack;
  assign xb_area = xb_o.area;  assign xb_baddr = xb_o.baddr;  assign xb_be = xb_o.be;  assign xb_wdata = xb_o.wdata;

  // ---- the SCI
  logic irq_eri, irq_rxi, irq_txi, irq_tei, sck_o, sck_oe;
  v1_sci u_sci (.clk, .rst, .ce, .pq(s_req[PP_SCI]), .mstp(1'b0), .stby(1'b0), .pr(s_rsp[PP_SCI]),
                .irq_eri, .irq_rxi, .irq_txi, .irq_tei, .dreq_tx(sci_dreq_tx), .dreq_rx(sci_dreq_rx),
                .txd_o(txd), .txd_oe, .sck_o, .sck_oe, .rxd_i(1'b1), .sck_i(1'b1));

  // ---- every other slave port: a register that reads 0 (hit) with no stall
  always_comb
    for (int k = 0; k < PP_N; k++)
      if (k != int'(PP_DMAC) && k != int'(PP_SCI) && k != int'(PP_BSC) && k != int'(PP_SDMR))
        s_rsp[k] = '{rdata: 32'd0, hit: s_req[k].rd || s_req[k].wr, stall: 1'b0};

  // ---- taps
  assign t_acc_v = u_dmac.acc_v;  assign t_acc_ch = u_dmac.acc_ch;
  assign t_upd_v = u_dmac.upd_v;  assign t_upd_ch = u_dmac.upd_ch;
  assign t_dma_ready = dma_bs.ready;  assign t_dma_we = dma_bq.r.we;  assign t_dma_ch = dma_bq.r.ch;
  assign t_tcr0 = u_dmac.tcr[0];  assign t_tcr1 = u_dmac.tcr[1];  assign t_tcr2 = u_dmac.tcr[2];
  assign t_tcr3 = u_dmac.tcr[3];
  assign t_tdr_dma_wr = s_req[PP_SCI].wr && s_req[PP_SCI].dma && s_req[PP_SCI].off[7:0] == 8'h0C && !s_rsp[PP_SCI].stall;
  assign t_tdre = sci_dreq_tx;
  assign t_p4w_ack = m_rsp[1].ack && m_req[1].we;

  /* verilator lint_off UNUSEDSIGNAL */
  logic unused;
  assign unused = ^{arr_strobe, dack_ic, bavl_n, tdack_n, ddt_id, dmte, dmae, port_o, port_oe, rcmi, rovi, gpio, back_n,
                    btap, xb_o, irq_eri, irq_rxi, irq_txi, irq_tei, sck_o, sck_oe, cpu_bs};
  /* verilator lint_on UNUSEDSIGNAL */
endmodule
