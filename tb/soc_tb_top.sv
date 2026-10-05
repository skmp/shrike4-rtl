// soc_tb_top.sv -- the v1 SoC testbench top (rtl/v1/SOC.md 7.1; owner WI-SOC): v1_soc with every pin as a scalar port
// for tb/soc_tb.cpp (no DPI, no struct layout on the C++ side), and the testbench taps flattened.
//
// Pins: v1_soc's own (SOC.md 3.6), ddt_tl_t split into its four fields.  Taps: soc_tap_t split into its fields; the
// commit records stay packed arrays of commit_rec_t (125 bits each, record k at bit 125 * k: tb/soc_commit.h
// unpack_rec reads them).  A few internal nets the C++ side needs and soc_tap_t does not carry are read hierarchically
// (not pins; SOC.md 7.5 "taps, not pins"):
//   * the reset levels and every request into v1_rstc: the lockstep run ends at a reset the core did not request
//     (SOC.md 6.2), and the direct-boot run ends at a WDT / H-UDI reset ("reset requested", SOC.md 2.2 / 7.4);
//   * the clock enables, cpu_run and stby: the board is called on bce cycles (SOC.md 3.3.2); the tb's clock checks;
//     a run that sits in standby with no wake source ends;
//   * v1_tmu's next-event hint (G8) and v1_cpg's FRQCR (the MD strap check);
//   * protocol monitors on the boundaries v1_soc is the first to join (each violation counted; tb/soc_tb.cpp fails
//     the run on any): the MA port contract (SOC.md 3.2.2: lrsp.ack / frz only while lreq.v and never both; a presented
//     request stays presented, identical, until its ack, a reset aside, and is not presented again after it), the
//     xl_* OR's precondition (SOC.md 3.8: the IC's and the OC's requests are exclusive, and each drives xl_req /
//     xl_itlb / xl_va 0 while not asking), the P4 slave rule (SOC.md 3.1.2: a slave drives rdata = 0, hit = 0,
//     stall = 0 in every cycle it is not strobed), and the fetch port (SOC.md 3.2.1, live on imx / rmx since M2: an
//     ITLB refill's busy; M2-verify F5): ifr.ack only while ifq.v, and a request presented and not acked while busy
//     is presented again unchanged (va, md) whenever ifq.v until its ack -- the fetch holds ireq stable while busy and
//     not acked, and a redirect waits for it (the busy of its last presentation decides, as v1_fetch's rq_busy);
//   * v1_core's take (an entry latched this cycle: tk in an unheld, running cycle): tb/soc_tb.cpp checks every EXC /
//     IRQ record's take age (data[55:44], SOC.md 6.3) against it -- the lockstep checker times the INTC registers'
//     landing window with that age (SOC.md 6.2; M1a-verify F1), so the counter is checked by an independent tap;
//   * (M3-fix, from the M3 verification's m3mon.sv, M3-verify F6) the memory system's boundaries: the BSC's strobes
//     (ready, wd_req, rd_v, rd_last, done) on its CPU and DMAC ports only in bce cycles (SOC.md 3.3.2); the DMAC's
//     request held, unchanged, from bq.v's rise through its ready (SOC.md 3.3.2); lrsp.frz exactly 2 cycles, once per
//     access (SOC.md 3.2.2, 3.3.1 C2); arr_strobe exactly in an array port's capture cycle (SOC.md 3.1.5); and the
//     SDRAM command stream on the chip's pins (xb_sd_*; DcBoard answers beats by address and ignores it), from the
//     first MRS on, against the DC's MCR 0xC00A0E24 / WCR2 0x618066D8 (TPC 2, RCD 3, TRWL 1, TRFC 7 Bcyc, CL 3, BL 4,
//     hm 13.3.5; the boot stub's values, SOC.md 7.3): ACTV only to a closed bank, TPC after its precharge; READ / WRIT
//     only to the open row, RCD after its ACTV; PRE only of an open bank; REF, SELF and MRS only with every bank closed
//     (REF / SELF TPC after the last precharge); nothing inside a refresh's TRFC;
//   * the ISN of v1_core's MA lane-0 micro-op: the LDTLB record's stamp.  SOC.md 6.3 stamps LDTLB with next_isn, "the
//     LDTLB's own ISN in its first MA cycle"; but that cycle is lt_frz (pfz), which holds S, so the older instructions
//     in S have not retired and next_isn is theirs (a WI-SOC contract issue).  `ldtlb` is raised only with LDTLB in MA
//     lane 0 (v1_core lt_ma), so this is the LDTLB's own ISN.
module soc_tb_top import v1_soc_pkg::*; #(
  parameter bit IDEAL_MEM = 1'b0,
  parameter bit TRANSLATE = 1'b0
) (
  input  logic        clk,
  input  logic        reset_n,
  input  logic [8:0]  md,
  output logic        xb_start,
  output logic [2:0]  xb_area,
  output logic [28:0] xb_addr,
  output logic        xb_we,
  output logic [2:0]  xb_size,
  output logic        xb_lock, xb_dack,
  output logic [1:0]  xb_dack_id,
  output logic        xb_beat,
  output logic [1:0]  xb_bidx,
  output logic [28:0] xb_baddr,
  output logic [7:0]  xb_be,
  output logic [63:0] xb_wdata,
  output logic        xb_last,
  output logic        xb_sd_cmd_v,
  output logic [3:0]  xb_sd_cmd,
  output logic [1:0]  xb_sd_bank,
  output logic [12:0] xb_sd_row,
  output logic        xb_refresh, xb_mrs,
  output logic [11:0] xb_mrs_val,
  input  logic        xb_rdy,
  input  logic [63:0] xb_rdata,
  input  logic        breq_n,
  output logic        back_n,
  input  logic        dbreq_n, tr_n,
  output logic        bavl_n, tdack_n,
  output logic [1:0]  ddt_id,
  input  logic        ddt_dtr_v,
  input  logic [63:0] ddt_dtr,
  input  logic        ddt_ch2_req, ddt_tr_repeat,
  input  logic [3:0]  irl_n,
  input  logic        nmi,
  output logic        txd2_o, txd2_oe, rts2_o, rts2_oe, cts2_o, cts2_oe,
  input  logic        rxd2_i, cts2_i, sck2_i,
  output logic        txd1_o, txd1_oe, sck1_o, sck1_oe,
  input  logic        rxd1_i, sck1_i,
  input  logic [19:0] port_i,
  output logic [19:0] port_o, port_oe,
  input  logic        tclk_i,
  output logic        tclk_o, tclk_oe,
  input  logic        rtc_osc,
  output logic [1:0]  status,
  output logic        im_if_req,
  output logic [31:0] im_if_addr,
  input  logic [31:0] im_if_data,
  output logic        im_dm_req, im_dm_we,
  output logic [1:0]  im_dm_size,
  output logic [28:0] im_dm_addr,
  output logic [63:0] im_dm_wdata,
  input  logic [63:0] im_dm_rdata,
  // ---- soc_tap_t, field by field
  output logic        t_i0_v,
  output logic [31:0] t_i0_pc,
  output logic        t_i1_v,
  output logic [31:0] t_i1_pc,
  output logic [7:0]  t_next_isn,
  output logic        t_halted, t_asleep,
  output logic [CT_CORE_N*125-1:0] t_rec_core,
  output logic [CT_LSU_N*125-1:0]  t_rec_lsu,
  output logic [CT_IC_N*125-1:0]   t_rec_ic,
  output logic [CT_MMU_N*125-1:0]  t_rec_mmu,
  output logic [CT_BSC_N*125-1:0]  t_rec_bsc,
  output logic [63:0] t_pmc,
  output logic        t_scif_tx_v, t_scif_rx_v,
  output logic [7:0]  t_scif_tx_byte, t_scif_rx_byte,
  output logic        t_p4_strobe, t_p4_we,
  output logic [4:0]  t_p4_blk,
  output logic        t_biu_start,
  // ---- hierarchical taps (testbench only)
  output logic        h_rst_pin, h_rst_por, h_rst_man, h_rst_cpu,
  output logic [1:0]  h_rst_code,
  output logic        h_req_cpu, h_req_mhit, h_req_arr_mhit, h_req_wdt_por, h_req_wdt_man, h_req_hudi,
  output logic        h_bce, h_pce, h_cpu_run, h_stby,
  output logic        h_cpu_clk, h_cpg_wpend,          // v1_cpg's CPU clock and its p4_wpend input (M1b)
  output logic [31:0] h_tmu_next_evt,
  output logic [6:0]  h_biu_rqv,                  // bus trace (M3): the BIU's requesters' bq.v (v1_bsc_pkg BIU_*),
  output logic [2:0]  h_biu_win,                  // the one presented to the BSC, and its `ready`
  output logic        h_biu_rdy,
  output logic [15:0] h_frqcr,
  output logic [7:0]  h_scif_brr, h_scif_smr,     // SCBRR2 / SCSMR2 (M4-fix: the UART monitor's bit time, SOC.md 7.4)
  output logic [7:0]  h_ma_isn,
  output logic        h_take,
  // ---- protocol monitors (below): violations counted since time 0, and the first one's cycle
  output logic [31:0] m_ma_err, m_xl_err, m_p4s_err, m_if_err,
  output logic [31:0] m_bce_err, m_dmahs_err, m_frz_err, m_arr_err, m_sd_err,
  output logic [31:0] m_sd_cmds, m_sd_refs,      // (coverage of the SDRAM monitor: commands and refreshes checked)
  output logic [63:0] m_first
);
  soc_tap_t tap;
  status_e  st;

  v1_soc #(.IDEAL_MEM(IDEAL_MEM), .TRANSLATE(TRANSLATE)) u_soc (
    .clk, .reset_n, .md,
    .xb_start, .xb_area, .xb_addr, .xb_we, .xb_size, .xb_lock, .xb_dack, .xb_dack_id, .xb_beat, .xb_bidx, .xb_baddr,
    .xb_be, .xb_wdata, .xb_last, .xb_sd_cmd_v, .xb_sd_cmd, .xb_sd_bank, .xb_sd_row, .xb_refresh, .xb_mrs, .xb_mrs_val,
    .xb_rdy, .xb_rdata, .breq_n, .back_n,
    .dbreq_n, .tr_n, .bavl_n, .tdack_n, .ddt_id,
    .ddt_tl('{dtr_v: ddt_dtr_v, dtr: ddt_dtr, ch2_req: ddt_ch2_req, tr_repeat: ddt_tr_repeat}),
    .irl_n, .nmi,
    .txd2_o, .txd2_oe, .rts2_o, .rts2_oe, .cts2_o, .cts2_oe, .rxd2_i, .cts2_i, .sck2_i,
    .txd1_o, .txd1_oe, .sck1_o, .sck1_oe, .rxd1_i, .sck1_i,
    .port_i, .port_o, .port_oe, .tclk_i, .tclk_o, .tclk_oe, .rtc_osc, .status(st),
    .im_if_req, .im_if_addr, .im_if_data, .im_dm_req, .im_dm_we, .im_dm_size, .im_dm_addr, .im_dm_wdata, .im_dm_rdata,
    .tap
  );
  assign status = st;

  assign t_i0_v     = tap.core.i0_v;
  assign t_i0_pc    = tap.core.i0_pc;
  assign t_i1_v     = tap.core.i1_v;
  assign t_i1_pc    = tap.core.i1_pc;
  assign t_next_isn = tap.core.next_isn;
  assign t_halted   = tap.core.halted;
  assign t_asleep   = tap.core.asleep;
  assign t_rec_core = tap.core.rec;
  assign t_rec_lsu  = tap.lsu;
  assign t_rec_ic   = tap.ic;
  assign t_rec_mmu  = tap.mmu;
  assign t_rec_bsc  = tap.bsc;
  assign t_pmc      = tap.pmc;
  assign t_scif_tx_v    = tap.scif.tx_v;
  assign t_scif_tx_byte = tap.scif.tx_byte;
  assign t_scif_rx_v    = tap.scif.rx_v;
  assign t_scif_rx_byte = tap.scif.rx_byte;
  assign t_p4_strobe = tap.p4_strobe;
  assign t_p4_we     = tap.p4_we;
  assign t_p4_blk    = tap.p4_blk;
  assign t_biu_start = tap.biu_start;

  // hierarchical taps
  assign h_rst_pin      = u_soc.rst.pin;
  assign h_rst_por      = u_soc.rst.por;
  assign h_rst_man      = u_soc.rst.man;
  assign h_rst_cpu      = u_soc.rst.cpu;
  assign h_rst_code     = u_soc.rst_code;
  assign h_req_cpu      = u_soc.cpu_rst_req;
  assign h_req_mhit     = u_soc.mhit_req;
  assign h_req_arr_mhit = u_soc.arr_mhit;
  assign h_req_wdt_por  = u_soc.wdt_rst_por;
  assign h_req_wdt_man  = u_soc.wdt_rst_man;
  assign h_req_hudi     = u_soc.hudi_rst;
  assign h_bce          = u_soc.ce.bce;
  assign h_pce          = u_soc.ce.pce;
  assign h_cpu_run      = u_soc.cpu_run;
  assign h_stby         = u_soc.stby;
  assign h_cpu_clk      = u_soc.cpu_clk;
  assign h_cpg_wpend    = u_soc.u_cpg.p4_wpend;
  assign h_tmu_next_evt = u_soc.tmu_next_evt;
  generate
    if (IDEAL_MEM) begin : g_hbiu                   // no BIU in this configuration
      assign h_biu_rqv = '0;
      assign h_biu_win = '0;
      assign h_biu_rdy = 1'b0;
    end else begin : g_hbiu
      for (genvar k = 0; k < 7; k++) begin : g_rq
        assign h_biu_rqv[k] = u_soc.g_real.biu_rq[k].v;
      end
      assign h_biu_win = u_soc.g_real.u_biu.win;
      assign h_biu_rdy = u_soc.cpu_bs.ready;
    end
  endgenerate
  assign h_frqcr        = {4'd0, u_soc.u_cpg.frqcr};     // FRQCR[15:12] read 0
  assign h_scif_brr     = u_soc.u_scif.brr;
  assign h_scif_smr     = u_soc.u_scif.smr;
  assign h_ma_isn       = u_soc.u_core.ma[0].isn;
  assign h_take         = !u_soc.u_core.rst_c && u_soc.u_core.run && !u_soc.u_core.frz && u_soc.u_core.tk;

  // ---- protocol monitors
  lreq_t       lq_q;                                         // last cycle's presented request
  logic        lq_v_q, lq_ack_q, rst_q;
  logic        ma_acked;                                   // the acked access micro-op is still in MA (the core holds)
  logic [63:0] cyc;
  logic        ma_bad, xl_bad, p4_bad, if_bad;
  logic [3:0]  ma_why;                                      // which MA-port rule (the report of the first five)
  logic        fq_v_q, fq_busy_q, fq_md_q;                 // the outstanding fetch request (presented, not acked)
  logic [31:0] fq_va_q;
  always_comb begin
    ma_bad = (u_soc.lrsp.ack || u_soc.lrsp.frz) && !u_soc.lreq.v;
    ma_bad = ma_bad || (u_soc.lrsp.ack && u_soc.lrsp.frz);
    // held from last cycle (not acked, no reset since): presented again, every field identical
    ma_bad = ma_bad || (lq_v_q && !lq_ack_q && !rst_q && !u_soc.rst.cpu && (!u_soc.lreq.v || u_soc.lreq != lq_q));
    // acked: never presented again while its micro-op stays in MA (u_core.frz; tb/v1_tb_top.sv's rule).  Not "the same
    // ISN, kind and address as the last ack": MAC.W / MAC.L @Rm+,@Rn+ with Rm = Rn as addresses reads the same word in
    // its two micro-ops, one right after the other (sweep-full, e.g. id 2115 MOV Rm,Rn ; MAC.W)
    ma_bad = ma_bad || (ma_acked && !rst_q && !u_soc.rst.cpu && u_soc.lreq.v);
    ma_why = {ma_acked && !rst_q && !u_soc.rst.cpu && u_soc.lreq.v,
              lq_v_q && !lq_ack_q && !rst_q && !u_soc.rst.cpu && (!u_soc.lreq.v || u_soc.lreq != lq_q),
              u_soc.lrsp.ack && u_soc.lrsp.frz, (u_soc.lrsp.ack || u_soc.lrsp.frz) && !u_soc.lreq.v};
    xl_bad = u_soc.ic_xl_req && u_soc.oc_xl_req;
    xl_bad = xl_bad || (!u_soc.ic_xl_req && (u_soc.ic_xl_itlb || u_soc.ic_xl_va != 32'd0));
    xl_bad = xl_bad || (!u_soc.oc_xl_req && u_soc.oc_xl_va != 32'd0);
    p4_bad = 1'b0;
    for (int k = 0; k < PP_N; k++)
      if (!(u_soc.p4s_q[k].rd || u_soc.p4s_q[k].wr) && u_soc.p4s_r[k] != '0) p4_bad = 1'b1;
    if_bad = u_soc.ifr.ack && !u_soc.ifq.v;
    if_bad = if_bad || (fq_v_q && fq_busy_q && !rst_q && !u_soc.rst.cpu && u_soc.ifq.v &&
                        (u_soc.ifq.va != fq_va_q || u_soc.ifq.md != fq_md_q));
  end
  // ---- the memory system's boundaries (M3-fix; see the header)
  localparam logic [63:0] SD_TPC = 64'd2, SD_RCD = 64'd3, SD_TRFC = 64'd7;
  localparam logic [63:0] SD_RDA_PRE = 64'd6;               // READA: the auto-precharge at CL 3 + BL 4 - 1
  localparam logic [63:0] SD_WRA_PRE = 64'd4;               // WRITA: at BL 4 - 1 + TRWL 1
  logic        bce_bad, dmahs_bad, frz_bad, arr_bad, sd_bad;
  logic        dq_pend_q;                                  // the DMAC's request: presented, not yet ready
  breq_t       dq_r;
  logic [2:0]  frz_run;                                    // cycles of the current lrsp.frz run (saturating)
  logic        frz_had;                                    // this access already had its frz run
  logic        arr_cap;
  logic        b_act [4];
  logic [12:0] b_row [4];
  logic [63:0] b_tact [4], b_tpre [4], t_ref, bcyc;
  logic        sd_on;
  string       sd_why;
  logic [1:0]  sb;                                         // the command's bank
  always_comb begin
    bce_bad = !u_soc.ce.bce && (u_soc.cpu_bs.ready || u_soc.cpu_bs.wd_req || u_soc.cpu_bs.rd_v || u_soc.cpu_bs.rd_last ||
                                u_soc.cpu_bs.done || u_soc.dma_bs.ready || u_soc.dma_bs.wd_req || u_soc.dma_bs.rd_v ||
                                u_soc.dma_bs.rd_last || u_soc.dma_bs.done);
    dmahs_bad = dq_pend_q && !u_soc.rst.por && !u_soc.rst.man && (!u_soc.dma_bq.v || u_soc.dma_bq.r != dq_r);
    frz_bad = !u_soc.rst.cpu && ((u_soc.lrsp.frz && frz_run == 3'd0 && frz_had) ||
                                 (!u_soc.lrsp.frz && frz_run != 3'd0 && frz_run != 3'd2));
    arr_cap = 1'b0;
    for (int k = 0; k < PP_N; k++)
      if ((k == int'(PP_IC_ARR) || k == int'(PP_OC_ARR) || k == int'(PP_MMU_ARR)) && (u_soc.p4s_q[k].rd || u_soc.p4s_q[k].wr) &&
          !u_soc.p4s_r[k].stall)
        arr_cap = 1'b1;
    arr_bad = !u_soc.rst.por && u_soc.arr_strobe != arr_cap;
    // the SDRAM command of this Bcyc against the bank state
    sd_bad = 1'b0; sd_why = "";
    sb = u_soc.xb_o.sd_bank;
    if (u_soc.ce.bce && !u_soc.rst.por && sd_on && u_soc.xb_o.sd_cmd_v && u_soc.xb_o.sd_cmd != v1_bsc_pkg::SDC_NOP) begin
      if (bcyc > t_ref && bcyc <= t_ref + SD_TRFC) begin sd_bad = 1'b1; sd_why = "a command inside a refresh's TRFC"; end
      unique case (u_soc.xb_o.sd_cmd)
        v1_bsc_pkg::SDC_ACTV:
          if (b_act[sb]) begin sd_bad = 1'b1; sd_why = "ACTV to an open bank"; end
          else if (bcyc < b_tpre[sb] + SD_TPC) begin sd_bad = 1'b1; sd_why = "ACTV before TPC"; end
        v1_bsc_pkg::SDC_READ, v1_bsc_pkg::SDC_READA, v1_bsc_pkg::SDC_WRIT, v1_bsc_pkg::SDC_WRITA:
          if (!b_act[sb]) begin sd_bad = 1'b1; sd_why = "READ / WRIT to a closed bank"; end
          else if (b_row[sb] != u_soc.xb_o.sd_row) begin sd_bad = 1'b1; sd_why = "READ / WRIT to another row"; end
          else if (bcyc < b_tact[sb] + SD_RCD) begin sd_bad = 1'b1; sd_why = "READ / WRIT before RCD"; end
        v1_bsc_pkg::SDC_PRE:
          if (!b_act[sb]) begin sd_bad = 1'b1; sd_why = "PRE of a closed bank"; end
        v1_bsc_pkg::SDC_REF, v1_bsc_pkg::SDC_SELF, v1_bsc_pkg::SDC_MRS:
          for (int i = 0; i < 4; i++)
            if (b_act[i]) begin sd_bad = 1'b1; sd_why = "REF / SELF / MRS with a bank open"; end
            else if (u_soc.xb_o.sd_cmd != v1_bsc_pkg::SDC_MRS && bcyc < b_tpre[i] + SD_TPC) begin
              sd_bad = 1'b1; sd_why = "REF / SELF before TPC";
            end
        default: ;
      endcase
    end
  end
  always_ff @(posedge clk) begin
    if (cyc == 64'd0) begin
      m_bce_err <= '0; m_dmahs_err <= '0; m_frz_err <= '0; m_arr_err <= '0; m_sd_err <= '0; m_sd_cmds <= '0;
      m_sd_refs <= '0; dq_pend_q <= 1'b0; frz_run <= '0; frz_had <= 1'b0; sd_on <= 1'b0; bcyc <= '0;
      t_ref <= 64'hFFFF_FFFF_FFFF_0000;
      for (int i = 0; i < 4; i++) begin b_act[i] <= 1'b0; b_row[i] <= '0; b_tact[i] <= '0; b_tpre[i] <= '0; end
    end else begin
      if (bce_bad) m_bce_err <= m_bce_err + 32'd1;
      if (dmahs_bad) m_dmahs_err <= m_dmahs_err + 32'd1;
      if (frz_bad) m_frz_err <= m_frz_err + 32'd1;
      if (arr_bad) m_arr_err <= m_arr_err + 32'd1;
      if (sd_bad) m_sd_err <= m_sd_err + 32'd1;
      if ((bce_bad && m_bce_err < 5) || (dmahs_bad && m_dmahs_err < 5) || (frz_bad && m_frz_err < 5) ||
          (arr_bad && m_arr_err < 5))
        $display("soc_tb_top monitor: cycle %0d:%s%s%s%s", cyc, bce_bad ? " a BSC strobe outside bce" : "",
                 dmahs_bad ? " the DMAC's request changed before ready" : "",
                 frz_bad ? " an frz run not of 2 cycles, or a second one" : "",
                 arr_bad ? " arr_strobe and the array capture disagree" : "");
      if (sd_bad && m_sd_err < 5)
        $display("soc_tb_top monitor: cycle %0d Bcyc %0d SDRAM command %0d bank %0d row %h: %s", cyc, bcyc,
                 u_soc.xb_o.sd_cmd, u_soc.xb_o.sd_bank, u_soc.xb_o.sd_row, sd_why);
      // the DMAC port's request
      if (u_soc.rst.por || u_soc.rst.man) dq_pend_q <= 1'b0;
      else dq_pend_q <= u_soc.dma_bq.v && !u_soc.dma_bs.ready;
      dq_r <= u_soc.dma_bq.r;
      // frz runs
      if (u_soc.rst.cpu) begin frz_run <= '0; frz_had <= 1'b0; end
      else begin
        if (u_soc.lrsp.frz) frz_run <= frz_run == 3'd7 ? frz_run : frz_run + 3'd1;
        else frz_run <= '0;
        if (!u_soc.lreq.v || u_soc.lrsp.ack) frz_had <= 1'b0;
        else if (!u_soc.lrsp.frz && frz_run != 3'd0) frz_had <= 1'b1;
      end
      // the SDRAM bank state, one command per Bcyc
      if (u_soc.ce.bce) begin
        bcyc <= bcyc + 64'd1;
        if (u_soc.rst.por) begin
          for (int i = 0; i < 4; i++) b_act[i] <= 1'b0;
          sd_on <= 1'b0;
        end else if (u_soc.xb_o.sd_cmd_v && u_soc.xb_o.sd_cmd != v1_bsc_pkg::SDC_NOP) begin
          if (sd_on) m_sd_cmds <= m_sd_cmds + 32'd1;
          unique case (u_soc.xb_o.sd_cmd)
            v1_bsc_pkg::SDC_ACTV: begin b_act[sb] <= 1'b1; b_row[sb] <= u_soc.xb_o.sd_row; b_tact[sb] <= bcyc; end
            v1_bsc_pkg::SDC_READA: begin b_act[sb] <= 1'b0; b_tpre[sb] <= bcyc + SD_RDA_PRE; end
            v1_bsc_pkg::SDC_WRITA: begin b_act[sb] <= 1'b0; b_tpre[sb] <= bcyc + SD_WRA_PRE; end
            v1_bsc_pkg::SDC_PRE: begin b_act[sb] <= 1'b0; b_tpre[sb] <= bcyc; end
            v1_bsc_pkg::SDC_PALL: for (int i = 0; i < 4; i++) begin b_act[i] <= 1'b0; b_tpre[i] <= bcyc; end
            v1_bsc_pkg::SDC_REF: begin t_ref <= bcyc; if (sd_on) m_sd_refs <= m_sd_refs + 32'd1; end
            v1_bsc_pkg::SDC_MRS: sd_on <= 1'b1;
            default: ;
          endcase
        end
      end
    end
  end

  always_ff @(posedge clk) begin
    if (cyc == 64'd0) begin m_ma_err <= '0; m_xl_err <= '0; m_p4s_err <= '0; m_if_err <= '0; m_first <= '0; end
    cyc      <= cyc + 64'd1;
    rst_q    <= u_soc.rst.cpu;
    lq_v_q   <= u_soc.lreq.v;
    lq_ack_q <= u_soc.lrsp.ack;
    lq_q     <= u_soc.lreq;
    ma_acked <= !u_soc.rst.cpu && cyc != 64'd0 && ((u_soc.lreq.v && u_soc.lrsp.ack) || ma_acked) && u_soc.u_core.frz;
    if (u_soc.rst.cpu || cyc == 64'd0) fq_v_q <= 1'b0;
    else if (u_soc.ifq.v && !u_soc.ifr.ack) begin
      fq_v_q <= 1'b1; fq_va_q <= u_soc.ifq.va; fq_md_q <= u_soc.ifq.md; fq_busy_q <= u_soc.ifr.busy;
    end else if (u_soc.ifq.v) fq_v_q <= 1'b0;                  // acked
    if (!u_soc.rst.cpu && cyc != 64'd0) begin
      if (ma_bad) m_ma_err <= m_ma_err + 32'd1;
      if (ma_bad && m_ma_err < 5)
        $display("soc_tb_top monitor: cycle %0d MA-port rule %b (3 re-presented after ack, 2 held request changed, 1 ack+frz, 0 ack/frz without v): lreq v %0d kind %0d size %0d va %h isn %0d be %h rg %0d | last v %0d ack %0d kind %0d va %h isn %0d | ack %0d frz %0d",
                 cyc, ma_why, u_soc.lreq.v, u_soc.lreq.kind, u_soc.lreq.size, u_soc.lreq.va, u_soc.lreq.isn, u_soc.lreq.be,
                 u_soc.lreq.x.rg, lq_v_q, lq_ack_q, lq_q.kind, lq_q.va, lq_q.isn, u_soc.lrsp.ack, u_soc.lrsp.frz);
      if (xl_bad) m_xl_err <= m_xl_err + 32'd1;
      if (p4_bad) m_p4s_err <= m_p4s_err + 32'd1;
      if (if_bad) m_if_err <= m_if_err + 32'd1;
      if ((ma_bad || xl_bad || p4_bad || if_bad || bce_bad || dmahs_bad || frz_bad || arr_bad || sd_bad) &&
          m_first == 64'd0) m_first <= cyc;
    end
  end
  initial cyc = 64'd0;
endmodule
