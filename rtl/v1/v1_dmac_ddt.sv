// v1_dmac_ddt.sv -- the on-demand data transfer (DDT) module: DTR decode, the channel-0 request flag, the four-entry
// request queues of channels 1-3, the "previous channel" register, the DBREQ / BAVL / TR pin handshake
// (rtl/v1/spec/dmac.md 3.9; hm 14.5, Appendix H; SOC.md 3.3.3).  Owner: WI-DMAC.
//
// Requests reach it two ways, ORed:
//   * the transaction-level testbench port ddt_tl (C15, phase 1): dtr_v / dtr (a DTR as if delivered after BAVL),
//     ch2_req (DBREQ + TR together), tr_repeat (TR alone), one-Icyc pulses on bce cycles;
//   * the pins, sampled at every CKIO rising edge (bce): DBREQ# alone asks the BSC for the data bus (ddt_bus_req; the
//     BSC answers ddt_bavl when the current bus cycle ends) and BAVL# is driven for one Bclk; TR# two Bclk after BAVL#
//     is the DTR cycle: the DTR is D63:0 (ddt_d, the external data bus) sampled at that same edge (hm 14.5.1, figure
//     H.3; board phase 2, SOC.md 3.3.3 -- M6: the testbench's Holly drives it there); DBREQ# and TR# together are a
//     direct channel-2 request (no BAVL#, note 14); TR# alone repeats the previous channel.
// Everything is applied at the end of the bce that carries it, so a DTR latched at the edge ending Bclk 0 is a
// request in Bclk 1, and the engine's arbiter can commit it at the end of Bclk 1: the BSC sees the request in Bclk 2
// (hm figure H.3: DTR in cycle 0, ACTV at about +3).
//
// DTR decode (hm 14.5.2 note 8, H.1, H.2; spec 3.9.3):
//   ID != 00                      one request into that channel's queue (full: ignored, note 7); other fields ignored
//   ID 00, MD 00                  a channel-0 request with the registers already loaded (note 13)
//   ID 00, MD != 00, SZ 111       transfer end: CHCR0.DE <- 0, registers kept, TE not set (notes 8b, 12); refused
//                                 while the channel-0 flag is set (note 9)
//   ID 00, MD 10, SZ 110          clear the channel-0 flag and the channel 1-3 queues (note 16)
//   ID 00, MD != 00, SZ 000-100   channel-0 initialisation (CHCR0 / DMATCR0 / SAR0 or DAR0) and a request; ignored
//                                 while the flag is set (H.2)
//   ID 00, MD != 00, SZ 101 / 110 (other than the note-16 form) reserved: ignored [P]
// A request only sets the flag / grows the queue.  v1_dmac_req decides when one is consumed (pop): per unit for level
// burst and cycle steal, once per whole count for edge burst (hm H.1 note 4).  A TR-only repeat of channel 0 while
// CHCR0.TE = 1 sets a flag the engine can never serve, so later DTRs are ignored until the note-16 clear: the freeze
// of note 17 comes out of the structure.  Switching DDT off keeps this state (note 19).
// The channel-0 flag stays set until its transfer ends (H.2, note 9 "not accepted during the bus cycle"): when the
// engine commits a per-unit (level burst / cycle steal) channel-0 unit the flag goes "in service" (srv0: no longer a
// request to the arbiter, still refusing DTRs) and clears at that unit's counter update.  An initialisation DTR is
// also refused while a channel-0 unit is between its commit and its counter update (u0_busy: the flag may have been
// cleared under it by note 16 or an address error), so the unit's update never lands on registers a later DTR
// loaded.  A request refused this way is ignored, like one into a full queue (note 7).
// AE / NMIF (hm 14.3.6: "acceptance of external requests is suspended while AE is set to 1, so a DMA transfer request
// must be reissued"; spec 3.6: pending requests are dropped): while either is set, or being set in this Bclk
// (halt_now), the flag, its in-service state and the channel 1-3 queues are held empty, and an initialisation DTR is
// refused [P: the manual does not say whether the DDT module's queues are external requests; spec 3.6 reads them so].
module v1_dmac_ddt import v1_soc_pkg::*, v1_dmac_pkg::*; (
  input  logic        clk,
  /* verilator lint_off UNUSEDSIGNAL */          // rst.pin / rst.cpu: pin implies por; a CPU-only reset holds the DMAC
  input  rst_t        rst,
  /* verilator lint_on UNUSEDSIGNAL */
  input  logic        bce,
  input  logic        run,
  input  logic        ddt,                 // DMAOR.DDT
  input  ddt_tl_t     ddt_tl,
  input  logic        dbreq_n, tr_n,       // DREQ0# / DREQ1# pins (DDT meaning while ddt)
  input  logic [63:0] ddt_d,               // D63:0, the external data bus: the DTR in the TR# cycle after BAVL#
  output logic        bavl_q,              // BAVL asserted this Bclk (v1_dmac drives the pin's polarity)
  output logic        ddt_bus_req,         // to the BSC: release the data bus for a DTR
  input  logic        ddt_bavl,            // from the BSC: the data bus is released
  input  logic [3:0]  pop,                 // v1_dmac_req: consume one request of channel n (bce pulse)
  input  logic        srv0_set,            // v1_dmac_req: the flag's unit was committed (the flag goes in service)
  input  logic        srv0_end,            // v1_dmac_req: a channel-0 unit's counter update (an in-service flag clears)
  input  logic        u0_busy,             // v1_dmac_xfer: a channel-0 unit is between its commit and its update
  input  logic        halt_now,            // v1_dmac_regs: AE | NMIF, or one of them being set in this Bclk
  output logic [3:0]  has,                 // a request is pending (channel 0: the flag, not in service; 1-3: queue)
  output logic [3:1][2:0] qcnt,           // queue depths (testbench visibility)
  output logic        ld0_v,               // channel-0 initialisation this bce
  output logic [31:0] ld0_chcr,
  output logic [23:0] ld0_tcr,
  output logic        ld0_sar_we, ld0_dar_we,
  output logic [31:0] ld0_addr,
  output logic        halt0
);
  logic       flag0, srv0;
  logic [3:1][2:0] q;
  logic [1:0] last_ch;
  logic       last_v;
  logic       dbreq_s, tr_s;               // pin samples (the CKIO rising edge)
  logic [63:0] d_s;                        // D63:0 sampled with TR# (the DTR when TR# is the DTR cycle)
  logic       breq_pend;                   // DBREQ accepted, waiting for the BSC's BAVL
  logic [2:0] bh;                          // BAVL history: bh[2] = BAVL was asserted three Bclk ago

  assign has = {q[3] != 3'd0, q[2] != 3'd0, q[1] != 3'd0, flag0 && !srv0};
  assign qcnt = q;
  assign ddt_bus_req = breq_pend;

  // ---- this bce's request events (DDT mode only; the DDT pins mean DREQ0 / DREQ1 otherwise)
  dtr_t d;
  logic dtr_v, dir2, trrep, pin_dtr;
  // the DTR: from the data bus in the DTR cycle (the pins), or the transaction-level port (the unit tbs); both in one
  // Bclk cannot happen on a board (the tb port is not a pin), the port's then wins
  assign pin_dtr = tr_s && bh[2];                                     // TR two Bclk after BAVL: the DTR cycle
`ifndef V1_DMAC_BUG_25
  assign d       = ddt_tl.dtr_v ? dtr_t'(ddt_tl.dtr) : dtr_t'(d_s);
`else                                            // planted bug: the DTR taken from the port only (the data bus ignored)
  assign d       = dtr_t'(ddt_tl.dtr);
`endif
  assign dtr_v   = ddt && bce && run && (ddt_tl.dtr_v || pin_dtr);
  assign dir2    = ddt && bce && run && (ddt_tl.ch2_req || (dbreq_s && tr_s));
  assign trrep   = ddt && bce && run && (ddt_tl.tr_repeat || (tr_s && !dbreq_s && !pin_dtr));

  logic       init0, req0_md00, end0, clr_all, drop;
`ifndef V1_DMAC_BUG_18
  assign init0     = dtr_v && d.id == 2'd0 && d.md != 2'd0 && d.sz <= 3'd4 && !flag0 && !u0_busy && !drop;
`else                                            // planted bug (with v1_dmac_req's): no in-flight check
  assign init0     = dtr_v && d.id == 2'd0 && d.md != 2'd0 && d.sz <= 3'd4 && !flag0 && !drop;
`endif
  assign req0_md00 = dtr_v && d.id == 2'd0 && d.md == 2'd0;
  assign end0      = dtr_v && d.id == 2'd0 && d.md != 2'd0 && d.sz == 3'd7 && !flag0;
  assign clr_all   = dtr_v && d.id == 2'd0 && d.md == 2'b10 && d.sz == 3'd6;
`ifndef V1_DMAC_BUG_19
  assign drop      = halt_now;
`else                                            // planted bug: DDT requests survive AE / NMIF
  assign drop      = 1'b0;
`endif

  assign ld0_v      = init0;
`ifndef V1_DMAC_BUG_6
  assign ld0_chcr   = dtr_chcr0(d);
`else                                            // planted bug: CHCR0.DS not loaded from MD 01
  assign ld0_chcr   = dtr_chcr0(d) & ~32'h0008_0000;
`endif
  assign ld0_tcr    = {16'd0, d.cnt};                                 // COUNT 0 = 16M (DMATCR 0)
  assign ld0_sar_we = !d.rw;
  assign ld0_dar_we = d.rw;
  assign ld0_addr   = d.addr;
  assign halt0      = end0;

  // pushes per channel this bce
  logic [3:1] push;
  always_comb begin
    push = 3'd0;
    if (dtr_v && d.id != 2'd0) push[d.id] = 1'b1;
    if (trrep && last_v && last_ch != 2'd0) push[last_ch] = 1'b1;
  end
  logic [1:0] npush [1:3];                                            // a DTR for channel 2 and a direct request
  always_comb begin
    for (int n = 1; n < 4; n++) npush[n] = {1'b0, push[n]};
    if (dir2) npush[2] = npush[2] + 2'd1;
  end

  always_ff @(posedge clk) begin
    if (rst.por || rst.man) begin                                       // SOC.md 2.3: the DDT queues empty
      flag0 <= 1'b0; srv0 <= 1'b0; q <= '0; last_ch <= 2'd0; last_v <= 1'b0;
      dbreq_s <= 1'b0; tr_s <= 1'b0; breq_pend <= 1'b0; bavl_q <= 1'b0; bh <= 3'd0; d_s <= '0;
    end else if (bce && !run) begin
      // the channel-0 unit's end while `run` is 0: the unit in flight completes its handshakes (v1_dmac), so its
      // counter update ends the flag it put in service, as v1_dmac_req's end events do (the other flag / queue events
      // are the request logic's own Bclk, stopped) [M1b-verify F2's class: without it the flag stays set and in
      // service, channel 0 shows no request and ignores every later DTR until a reset]
`ifndef V1_DMAC_BUG_23
      if (srv0_end && srv0) begin flag0 <= 1'b0; srv0 <= 1'b0; end
`endif
    end else if (bce && run) begin
      dbreq_s <= !dbreq_n;
      tr_s    <= !tr_n;
      if (!tr_n) d_s <= ddt_d;                 // the data bus at the TR# edge (held otherwise: no toggling)
      bh      <= {bh[1:0], bavl_q};
      // data-bus request: DBREQ alone (with TR it is the direct channel-2 request, note 14)
      bavl_q <= 1'b0;
      if (ddt && dbreq_s && !tr_s && !breq_pend) breq_pend <= 1'b1;
      else if (breq_pend && ddt_bavl) begin breq_pend <= 1'b0; bavl_q <= 1'b1; end

      // channel 0 flag: a request sets it only while it is clear (a request while it is set is ignored, note 7)
      if (srv0_set) srv0 <= 1'b1;
      if (clr_all || pop[0] || (srv0_end && srv0)) begin flag0 <= 1'b0; srv0 <= 1'b0; end
      if (init0 || ((req0_md00 || (trrep && last_v && last_ch == 2'd0)) && !flag0)) flag0 <= 1'b1;
      // queues: + pushes (capped at 4: a request into a full queue is ignored, note 7) - pop
      for (int n = 1; n < 4; n++) begin
        logic [3:0] nq;
        nq = {1'b0, q[n]} - {3'd0, pop[n] && q[n] != 3'd0} + {2'd0, npush[n]};
`ifndef V1_DMAC_BUG_8
        q[n] <= clr_all ? 3'd0 : (nq > 4'd4 ? 3'd4 : nq[2:0]);
`else                                            // planted bug: a fifth request is queued
        q[n] <= clr_all ? 3'd0 : (nq > 4'd5 ? 3'd5 : nq[2:0]);
`endif
      end
      // AE / NMIF: every pending request is dropped, and none is accepted while they are set
      if (drop) begin flag0 <= 1'b0; srv0 <= 1'b0; q <= '0; end
      // the previous request channel (TR-only repeats it); a direct channel-2 request does not change it [P]
      if (dtr_v && d.id != 2'd0)          begin last_ch <= d.id; last_v <= 1'b1; end
      else if (init0 || req0_md00)        begin last_ch <= 2'd0; last_v <= 1'b1; end
    end
  end
endmodule
