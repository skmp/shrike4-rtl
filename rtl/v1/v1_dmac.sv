// v1_dmac.sv -- the SH7091 DMA controller: four channels, DMAOR, auto / on-chip / external / DDT requests, dual and
// single address, burst and cycle steal, fixed and round-robin priority, class-A / class-B address errors, the
// DMTE0-3 / DMAE levels, the DDT module (rtl/v1/spec/dmac.md; SOC.md 1.2, 2.3-2.5, 3.1, 3.3, 3.5, 3.8).
// Owner: WI-DMAC.
//
//   v1_dmac_regs   17 registers, CPU access rules (32-bit only, flag arming, DDT write masking), class-A check
//   v1_dmac_req    per-channel requests: auto, on-chip levels, DREQ level / edge, DDT flag / queues; DRAK
//   v1_dmac_arb    fixed orders and round robin
//   v1_dmac_xfer   the unit sequencer: legs on the BSC port and the P4 bridge, burst / cycle steal, counters, class B
//   v1_dmac_ddt    DTR decode, channel-0 flag, channel 1-3 queues, DBREQ / BAVL / TR
//
// Clocking: everything is Bclk state (hm Appendix A), clocked by clk (Iclk) and advanced on ce.bce (SOC.md 2.1).
// `run` is v1_cpg's dmac_run (SOC.md 2.4 / 2.5, 3.8): 0 while STBCR.MSTP4 = 1, in deep sleep and in standby.  It gates
// the Bclk: while it is 0 no request is sampled or accepted and the registers ignore CPU writes (reads return the held
// values) [P]; a unit already on the bus still completes its handshakes (software stops the DMAC first, hm 14.6 note
// 3), and its end events reach v1_dmac_req / v1_dmac_ddt under bce alone, so its channel leaves service and requests
// again when `run` returns (M1b-verify F2).  Sleep keeps it running (spec 3.11).  `stby` clears every CHCR.TE (hm
// 14.2.4).
//
// Pins (SOC.md 3.3.3): with DMAOR.DDT = 0 they are the normal-mode request / acknowledge pins
//   dbreq_n = DREQ0#, tr_n = DREQ1#, bavl_n = DRAK0, ddt_id[1] = DRAK1 (polarity CHCR.RL: 0 active high),
//   tdack_n = DACK0, ddt_id[0] = DACK1 (polarity CHCR.AL: 0 active high);
// with DDT = 1 they are DBREQ#, TR#, BAVL# (active low), TDACK# (active low) and ID[1:0] (the channel while TDACK).
// DACK / TDACK are driven in the Bclk of each data beat of an external-request unit: every beat of a single-address
// cycle, the read (CHCR.AM = 0) or write (AM = 1) leg of a dual-address one (hm 14.3.4, H.1: TDACK with the read data;
// for a write the beat's data request, 2 Bclk before the device's data [P]).
//
// Interrupt levels (spec 3.7, [HW dmac1 S9 / dmac2 T2]): DMTEn = TE & IE (a level, independent of DE), DMAE = AE.
// They are masked by `mstp` (STBCR.MSTP4) alone: a module-stopped DMAC drives them 0 (SOC.md 2.4, 3.4).  Deep sleep
// and standby stop `run` but keep the levels -- they are the TE / IE / AE flop outputs, which hold without the Bclk --
// so a DMTE pending at deep-sleep entry still reaches the INTC and wakes the CPU (SOC.md 2.4, I088).
module v1_dmac import v1_soc_pkg::*, v1_dmac_pkg::*; (
  input  logic      clk,
  input  rst_t      rst,
  /* verilator lint_off UNUSEDSIGNAL */          // ce.pce: every DMAC flop is Bclk
  input  clken_t    ce,
  /* verilator lint_on UNUSEDSIGNAL */
  input  logic      run,                       // dmac_run: 0 with MSTP4, in deep sleep and in standby
  input  logic      stby,
  input  logic      mstp,                      // STBCR.MSTP4: DMTE / DMAE driven 0 (SOC.md 2.4)
  input  p4s_req_t  pq,
  output p4s_rsp_t  pr,
  output p4m_req_t  p4q,
  input  p4m_rsp_t  p4r,
  output bq_t       bq,
  input  bs_t       bs,
  output bwd_t      bwd,
  input  dreq_t     dreq,
  output logic      dack_ic,
  input  logic      dbreq_n, tr_n,
  output logic      bavl_n, tdack_n,
  output logic [1:0] ddt_id,
  input  ddt_tl_t   ddt_tl,
  input  logic [63:0] ddt_d,                   // D63:0 (the external data bus): the DTR in the DDT DTR cycle (M6)
  output logic      ddt_bus_req,
  input  logic      ddt_bavl,
  output logic      bus_rel,                   // (M6) no bus session (v1_dmac_xfer): the BSC's CPU turn (SOC.md 3.3.2)
  output logic      bus_ses,                   // (M6-fix) a burst session holds the bus (v1_dmac_xfer): no CPU accept
  input  logic      nmi_evt,
  output logic [3:0] dmte,
  output logic      dmae
);
  // ---- registers
  logic [3:0][31:0] sar, dar, chcr;
  logic [3:0][23:0] tcr;
  logic             ddt, dme, ae, nmif, halt_now;
  logic [1:0]       prm;
  logic [3:0]       en, chcr_wr;
  logic [31:0]      ib_data;
  // engine <-> regs
  logic             upd_v, upd_sar_we, upd_dar_we, upd_te, upd_ae;
  logic [1:0]       upd_ch;
  logic [31:0]      upd_sar, upd_dar;
  logic [23:0]      upd_tcr;
  // DDT <-> regs / req
  logic             ld0_v, ld0_sar_we, ld0_dar_we, halt0, bavl_q;
  logic [31:0]      ld0_chcr, ld0_addr;
  logic [23:0]      ld0_tcr;
  logic [3:0]       ddt_has, ddt_pop;
  logic [3:1][2:0]  ddt_qcnt;
  // requests / arbitration / events
  logic [3:0]       rq, cand;
  logic             pick_v, rr_upd, rr_idle, acc_v, ev_rd_end, ev_wr_start, ev_sgl_start, ev_upd;
  logic [1:0]       pick, acc_ch, ech, drak;
  logic             dack_v, busy, u0_busy, srv0_set, srv0_end;
  logic [1:0]       dack_ch;

  v1_dmac_regs u_regs (
    .clk, .rst, .bce(ce.bce), .run, .stby, .pq, .pr,
    .upd_v, .upd_ch, .upd_sar_we, .upd_dar_we, .upd_sar, .upd_dar, .upd_tcr, .upd_te, .upd_ae,
    .ld0_v, .ld0_chcr, .ld0_tcr, .ld0_sar_we, .ld0_dar_we, .ld0_addr, .halt0, .nmi_evt,
    .sar, .dar, .chcr, .tcr, .o_ddt(ddt), .o_dme(dme), .o_ae(ae), .o_nmif(nmif), .o_halt_now(halt_now), .o_pr(prm),
    .en, .ib_data, .chcr_wr
  );

  v1_dmac_ddt u_ddt (
    .clk, .rst, .bce(ce.bce), .run, .ddt, .ddt_tl, .dbreq_n, .tr_n, .ddt_d, .bavl_q, .ddt_bus_req, .ddt_bavl,
    .pop(ddt_pop), .srv0_set, .srv0_end, .u0_busy, .halt_now, .has(ddt_has), .qcnt(ddt_qcnt),
    .ld0_v, .ld0_chcr, .ld0_tcr, .ld0_sar_we, .ld0_dar_we, .ld0_addr, .halt0
  );

  v1_dmac_req u_req (
    .clk, .rst, .bce(ce.bce), .run, .chcr, .en, .ddt, .halt(ae || nmif), .chcr_wr,
    .dreq0_n(dbreq_n), .dreq1_n(tr_n), .dreq, .ddt_has, .ddt_pop, .srv0_set, .srv0_end,
    .acc_v, .acc_ch, .ech, .ev_rd_end, .ev_wr_start, .ev_sgl_start, .ev_upd, .rq, .drak
  );

  v1_dmac_arb u_arb (
    .clk, .rst(rst.por || rst.man), .bce(ce.bce), .pr(prm), .cand, .pick_v, .pick, .rr_upd, .rr_ch(ech), .rr_idle
  );

  v1_dmac_xfer u_xfer (
    .clk, .rst, .bce(ce.bce), .run, .sar, .dar, .chcr, .tcr, .ddt, .en, .ib_data, .rq,
    .cand, .pick_v, .pick, .rr_upd, .rr_idle,
    .acc_v, .acc_ch, .ech, .ev_rd_end, .ev_wr_start, .ev_sgl_start, .ev_upd,
    .upd_v, .upd_ch, .upd_sar_we, .upd_dar_we, .upd_sar, .upd_dar, .upd_tcr, .upd_te, .upd_ae,
    .bq, .bs, .bwd, .p4q, .p4r, .dack_v, .dack_ch, .dack_ic, .busy, .u0_busy, .bus_rel, .bus_ses
  );

  // ---- pins
  logic dack0, dack1;
  assign dack0 = dack_v && dack_ch == 2'd0;
  assign dack1 = dack_v && dack_ch == 2'd1;
  always_comb begin
    if (ddt) begin
      bavl_n  = !bavl_q;
      tdack_n = !dack_v;
      ddt_id  = dack_v ? dack_ch : 2'd0;
    end else begin
      bavl_n    = drak[0] ^ c_rl(chcr[0]);                // DRAK0: active high unless RL
      ddt_id[1] = drak[1] ^ c_rl(chcr[1]);                // DRAK1
      tdack_n   = dack0 ^ c_al(chcr[0]);                  // DACK0: active high unless AL
      ddt_id[0] = dack1 ^ c_al(chcr[1]);                  // DACK1
    end
  end

  // ---- interrupt levels
  logic lv_on;                                   // the levels reach the INTC
`ifdef V1_DMAC_BUG_20                            // planted bug: deep sleep masks the levels too (the old `run` gating)
  assign lv_on = !mstp && run;
`elsif V1_DMAC_BUG_21                            // planted bug: module stop does not mask the levels
  assign lv_on = 1'b1;
`else
  assign lv_on = !mstp;
`endif
  always_comb begin
`ifndef V1_DMAC_BUG_9
    for (int n = 0; n < 4; n++) dmte[n] = lv_on && c_te(chcr[n]) && c_ie(chcr[n]);
`else                                            // planted bug: DMTE needs DE (the console fires it with DE = 0)
    for (int n = 0; n < 4; n++) dmte[n] = lv_on && c_te(chcr[n]) && c_ie(chcr[n]) && c_de(chcr[n]);
`endif
    dmae = lv_on && ae;
  end

  /* verilator lint_off UNUSEDSIGNAL */            // testbench visibility (tb/v1_dmac_tb_top.sv reads them by path)
  logic unused;
  assign unused = ^{ddt_qcnt, busy, dme};
  /* verilator lint_on UNUSEDSIGNAL */
endmodule
