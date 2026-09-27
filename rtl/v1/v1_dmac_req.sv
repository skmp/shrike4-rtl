// v1_dmac_req.sv -- per-channel transfer requests: auto, on-chip module (SCI / SCIF / TMU), external DREQ (level /
// edge) and DDT; acceptance, re-sampling, the "whole count" hold of edge-burst requests, and DRAK
// (rtl/v1/spec/dmac.md 3.2, 3.9.3; hm 14.3.2, 14.3.5, 14.3.6; SOC.md 3.3.3, 3.5).  Owner: WI-DMAC.
//
// rq[n] is the channel's request as the arbiter (v1_dmac_arb, through v1_dmac_xfer) sees it in this Bclk.  It is
// gated there with the channel enables.  Sources, by CHCRn.RS:
//   auto (0100 0101 0110)   rq = 1: "the transfer begins when DE and DME are set" (hm 14.3.2).  Nothing is consumed,
//                           so a fixed-priority auto channel monopolises the DMAC even in cycle steal [HW dmac1 S10
//                           PR = 10 / 01].
//   on-chip (1000-1110)     the module's level (SCI TDRE / RDRF, SCIF TDFE / RDF|DR, TMU2 input capture), registered
//                           once (Pclk module -> Bclk).  Accepting it puts the channel "in service": the level is not
//                           sampled again until the unit's end, by which time the unit's own access to the data
//                           register has cleared the flag (SOC.md 3.5 auto-clear) or dack_ic has dropped the TMU's.
//                           TMU in burst mode: one request runs the whole count ("discontinued on the last transfer in
//                           burst mode", hm 14.3.2).  SCI / SCIF are cycle-steal-only sources: with TM = 1 they still
//                           take one request per unit [P] (spec 9.6: KOS sets burst for SCI TX; unmeasured).
//   external, normal mode   DREQ0# = dbreq_n (channel 0), DREQ1# = tr_n (channel 1), sampled at the CKIO rising edge
//   (0000 0010 0011;        (every bce).  Level (DS = 0): rq = the sample, re-sampled after acceptance only from the
//    channels 0, 1)         re-sampling point: one cycle after the first write cycle starts (dual) / the bus cycle
//                           starts (single) (hm 14.3.5).  Edge (DS = 1): a falling edge sets a latch one Bclk after the
//                           sample shows it low, so an edge request reaches the arbiter one Bclk later than a level one
//                           (the manual's 4 vs 5 CKIO from the sampling edge to the first DMAC bus cycle; the rest of
//                           the 4 / 5 is the arbiter Bclk and the BSC front end).  Cycle steal: the latch is consumed
//                           per unit and edges are ignored until the read cycle ends (dual) / the bus cycle starts
//                           (single).  Burst: the first edge runs the whole count (hm 14.3.5 items 4, 5).
//                           Channels 2 and 3 accept no external request in normal mode (hm Table 14.4 note 1).
//   external, DDT mode      the DDT module's channel-0 flag / channel 1-3 queue (v1_dmac_ddt).  Level burst and cycle
//                           steal consume one request per unit; edge burst (DS = 1, TM = 1) one per whole count
//                           (hm H.1 note 4).  A channel 1-3 queue entry is consumed when its unit is committed.  The
//                           channel-0 flag is the request itself and stays set until its transfer ends (H.2: "no
//                           further transfer requests are accepted between DTR format acceptance and the end of the
//                           data transfer"; 14.5.2 note 9: not accepted "during the bus cycle"): a per-unit request
//                           is put in service at the commit (srv0_set: v1_dmac_ddt then shows no request) and the flag
//                           clears at that unit's counter update (srv0_end); an edge burst's flag clears when the
//                           channel stops.
// "Whole count" holds end when the channel stops: TE, DE / DME cleared ("a transfer request is regarded as issued
// until a stop request is detected", hm 14.3.6 item 2), AE, NMIF (pending external and on-chip requests are dropped,
// hm 14.3.6), or a CPU write of the channel's CHCR [P].  A channel-0 DDT edge burst also ends when its DDT request
// flag is cleared by the note-16 DTR (hm 14.5.2 note 16 clears "the request"; the flag is that request) [P]: the
// channel stops at the next unit boundary, so a later channel-0 DTR is a new transfer.
// DRAK: a one-Bclk pulse at every accepted external request in normal mode (hm 14.3.5), so once per burst for edge.
module v1_dmac_req import v1_soc_pkg::*, v1_dmac_pkg::*; (
  input  logic             clk,
  /* verilator lint_off UNUSEDSIGNAL */          // rst.pin / rst.cpu: pin implies por; a CPU-only reset holds the DMAC
  input  rst_t             rst,
  /* verilator lint_on UNUSEDSIGNAL */
  input  logic             bce,
  input  logic             run,
  input  logic [3:0][31:0] chcr,
  input  logic [3:0]       en,
  input  logic             ddt,
  input  logic             halt,              // AE | NMIF: drop the pending external / on-chip requests
  input  logic [3:0]       chcr_wr,
  input  logic             dreq0_n, dreq1_n,  // normal-mode DREQ pins
  input  dreq_t            dreq,
  input  logic [3:0]       ddt_has,
  output logic [3:0]       ddt_pop,
  output logic             srv0_set,          // a channel-0 DDT unit that consumes the flag at its end was committed
  output logic             srv0_end,          // the counter update of a channel-0 unit
  // engine events (bce pulses) for the unit in flight on channel ech
  input  logic             acc_v,             // a unit of channel acc_ch was committed (the request is accepted)
  input  logic [1:0]       acc_ch,
  input  logic [1:0]       ech,
  input  logic             ev_rd_end,         // dual: the read leg finished
  input  logic             ev_wr_start,       // dual: the write leg was accepted by the bus
  input  logic             ev_sgl_start,      // single: the bus cycle was accepted
  input  logic             ev_upd,            // the unit's counter update (its end)
  output logic [3:0]       rq,
  output logic [1:0]       drak               // DRAK0 / DRAK1 active this Bclk (normal mode)
);
  logic [1:0] pin_s, pin_p;                   // DREQ sampled low (= request); previous sample (edge detector)
  logic [1:0] edg;                            // falling edge latched
  logic [4:0] oc_s;                           // on-chip levels {sci_tx, sci_rx, scif_tx, scif_rx, tmu_ic}
  logic [3:0] inserv, hold;
  logic [3:0] hold_end, hold_drop, hold_live;  // a whole-count hold ends this Bclk / dropped by note 16 / still valid
  logic [1:0] drak_q;
  assign drak = drak_q;

  function automatic logic oc_lvl(input logic [4:0] s, input logic [3:0] rs);
    unique case (rs)
      4'b1000: return s[4];                   // SCI TXI: TDRE
      4'b1001: return s[3];                   // SCI RXI: RDRF
      4'b1010: return s[2];                   // SCIF TXI: TDFE
      4'b1011: return s[1];                   // SCIF RXI: RDF | DR
      default: return s[0];                   // TMU2 input capture (1100 1101 1110)
    endcase
  endfunction

  // the kind of request of each channel, and whether one request runs the whole count
  logic [3:0] k_auto, k_oc, k_ext, k_ddt, whole;
  always_comb begin
    for (int n = 0; n < 4; n++) begin
      logic [3:0] rs;
      rs = c_rs(chcr[n]);
      k_auto[n] = rs_auto(rs);
      k_oc[n]   = rs_onchip(rs);
      k_ddt[n]  = ddt && rs_ext(rs);
      k_ext[n]  = !ddt && rs_ext(rs) && n < 2;
      whole[n]  = c_tm(chcr[n]) && ((k_oc[n] && rs_tmu(rs)) || ((k_ext[n] || k_ddt[n]) && c_ds(chcr[n])));
    end
  end

  always_comb begin
    for (int n = 0; n < 4; n++) begin
      if (k_auto[n])     rq[n] = 1'b1;
      else if (k_oc[n])  rq[n] = hold_live[n] || (oc_lvl(oc_s, c_rs(chcr[n])) && !inserv[n]);
      else if (k_ddt[n]) rq[n] = hold_live[n] || ddt_has[n];
      else if (k_ext[n]) rq[n] = hold_live[n] || (c_ds(chcr[n]) ? edg[n] : (pin_s[n] && !inserv[n]));
      else               rq[n] = 1'b0;
    end
  end

  // DDT consumption: channels 1-3 one queue entry at the commit (per unit, or once per whole count); channel 0 at the
  // end of its unit (srv0_end, applied by v1_dmac_ddt to a flag put in service by srv0_set) or, for a whole count,
  // when the hold ends
  always_comb begin
    hold_drop = {3'd0, hold[0] && k_ddt[0] && !ddt_has[0]};         // note 16 cleared the flag behind the hold
    for (int n = 0; n < 4; n++) hold_end[n] = (hold[n] && (!en[n] || halt || chcr_wr[n])) || hold_drop[n];
    hold_live = hold & ~hold_drop;
  end
  always_comb begin
    ddt_pop = 4'd0;
    for (int n = 1; n < 4; n++)
      if (k_ddt[n] && acc_v && acc_ch == 2'(n) && !(whole[n] && hold[n])) ddt_pop[n] = 1'b1;
    if (k_ddt[0] && hold_end[0]) ddt_pop[0] = 1'b1;
`ifndef V1_DMAC_BUG_18
    srv0_set = k_ddt[0] && acc_v && acc_ch == 2'd0 && !whole[0];
`else                                            // planted bug: the flag is consumed at the commit (a DTR arriving
    srv0_set = 1'b0;                             // during the unit's bus cycle reloads channel 0 under it)
    if (k_ddt[0] && acc_v && acc_ch == 2'd0 && !whole[0]) ddt_pop[0] = 1'b1;
`endif
    srv0_end = ev_upd && ech == 2'd0;
  end

  // the unit in flight ends while `run` is 0: a unit committed before `run` fell (MSTP4, deep sleep, standby) still
  // completes its handshakes (v1_dmac), and its end must take the channel out of service -- otherwise its on-chip /
  // level request stays masked until a reset, reprogramming or not (hm 14.6 note 3 leaves only the interrupted
  // transfer unguaranteed; SOC.md 2.4: the module-stopped DMAC is reprogrammed afterwards) [M1b-verify F2:
  // agents/verify/M1b/adv dmadslp, its unit's end at c3703 with run = 0]
`ifndef V1_DMAC_BUG_22
  wire end_run = 1'b1;
`else                                            // planted bug: the end events gated by `run` as well (the channel
  wire end_run = run;                            // stays in service for good)
`endif

  always_ff @(posedge clk) begin
    if (rst.por || rst.man) begin
      pin_s <= 2'd0; pin_p <= 2'd0; edg <= 2'd0; oc_s <= 5'd0; inserv <= 4'd0; hold <= 4'd0; drak_q <= 2'd0;
    end else if (bce) begin
      // the re-sampling point of the unit in flight (and in any case its end): the unit's own events, under bce alone
      for (int n = 0; n < 4; n++)
        if (end_run && ech == 2'(n) && (ev_upd ||
                         (k_ext[n] && !c_ds(chcr[n]) && (ev_wr_start || ev_sgl_start)) ||
                         (k_ext[n] &&  c_ds(chcr[n]) && (ev_rd_end || ev_sgl_start))))
          inserv[n] <= 1'b0;
      // the Bclk of the request logic proper: sampling, acceptance, holds, DRAK, edges
      if (run) begin
        pin_s  <= {!dreq1_n, !dreq0_n};
        pin_p  <= pin_s;
        oc_s   <= {dreq.sci_tx, dreq.sci_rx, dreq.scif_tx, dreq.scif_rx, dreq.tmu_ic};
        drak_q <= 2'd0;
        for (int n = 0; n < 4; n++) begin
          if (hold_end[n]) hold[n] <= 1'b0;
          if (acc_v && acc_ch == 2'(n)) begin    // (after the clear above: a unit accepted at the last one's end wins)
            if (whole[n]) hold[n] <= 1'b1;
            else if (k_oc[n] || k_ext[n]) inserv[n] <= 1'b1;
            if (k_ext[n] && !hold[n]) drak_q[n[0]] <= 1'b1;
          end
        end
        // edge latches: set by a falling edge while the channel samples; consumed per unit; dropped by AE / NMIF
        for (int n = 0; n < 2; n++) begin
          if (acc_v && acc_ch == 2'(n)) edg[n] <= 1'b0;
`ifndef V1_DMAC_BUG_11
          else if (pin_s[n] && !pin_p[n] && !inserv[n] && !hold[n]) edg[n] <= 1'b1;
`else                                            // planted bug: a low level re-arms the edge latch
          else if (pin_s[n] && !inserv[n] && !hold[n]) edg[n] <= 1'b1;
`endif
          if (halt) edg[n] <= 1'b0;
        end
      end
    end
  end
endmodule
