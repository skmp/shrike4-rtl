// v1_scif.sv -- the SCIF (serial channel 2 with 16-byte FIFOs) of the SH7091 (rtl/v1/SOC.md 3.1.2, 3.5, 3.8; spec
// serial 2.1, 2.2, 3.1-3.6, 7).  Owner: WI-SER.  The Dreamcast's serial port and KOS's dbgio console.
//
// Sub-units: v1_scif_brg (baud rate), 2 x v1_scif_fifo (TX W = 8, RX W = 10 {per, fer, data}), v1_scif_tx,
// v1_scif_rx.  The registers, flags, read-1 latches, error counters, DR timer and pin muxes are here.
//
// ---- one Pcyc (`en` = ce.pce & !mstp; every flop of the unit moves only then), in this order (serial 7.4):
//   1. the register access, if strobed (the P4 bridge strobes a Pclk slave on a pce cycle and never repeats it; this
//      slave never stalls).  A read returns the state at the start of the Pcyc and arms the read-1 latches of the
//      flags it returns as 1; an SCFRDR2 read pops.  A write updates a register, pushes SCFTDR2, or clears flags.
//      Every later step sees the registers after the access ("_a").
//   2. the BRG, then on a base tick: the pending transmit pop, the TX sequencer, the RX sequencer and its push rules.
//   3. the flags: clears from step 1, sets from step 2, then the level sets on the NEXT state (TDFE: T < TTRG; TEND:
//      TE = 0 or (IDLE and T = 0); RDF: R >= RTRG; DR: timer = 240 and 0 < R < RTRG), which is why a clear under a
//      true set condition is refused [HW run 1 S7 / S8 / S10, run 2 D1] and why a DMAC data access that leaves the
//      level false clears the flag while one that leaves it true does not [HW run 1 S17].
//   The pin synchronisers take the pin levels of the state at the START of the Pcyc (so a read right after a write
//   still shows the old pin level [HW run 1 S3 "00f3->00b3 00a2->00f3"]), and the loopback / RTS->CTS paths use the
//   registered TX line and RTS output.
//
// ---- registers (full decode of a[17:0]; a Pclk block does not mirror [HW run 1 S2]; each register answers only its
// own size: another size reads 0 and a write is ignored [HW run 1 S2, run 2 B]; holes 0x28-0x3C read 0, hit = 0).
//   00 SCSMR2 W 0x007B | 04 SCBRR2 B | 08 SCSCR2 W 0x00FA | 0C SCFTDR2 B (W: push; R: 0x00 [HW run 3 H2])
//   10 SCFSR2 W {PER3-0, FER3-0, ER TEND TDFE BRK FER PER RDF DR} | 14 SCFRDR2 B (R: pop; W ignored)
//   18 SCFCR2 W 0x00FF | 1C SCFDR2 W {T, R} | 20 SCSPTR2 W (IO bits as written, data bits = pins) | 24 SCLSR2 W ORER
// ---- flags (serial 3.4): ER TEND TDFE BRK RDF DR (SCFSR2) and ORER (SCLSR2) each have a read-1 latch: set by a read
//   that returns the flag as 1 and surviving reads of other registers [HW run 3 H3, the second case]; it also survives
//   a write of 1 to the bit and new set events [P: run 3 H3's first and third cases re-read SCFSR2 before the write of
//   0 (scif_probe3.c "b = SCFSR2", "e = SCFSR2"), which re-arms the latch, so they do not isolate this]; a write with
//   the bit 0 clears the flag if the latch is set (DR only with R = 0 [HW run 1 S9c]) and clears the latch.  A write
//   of 0 without the latch does nothing [HW run 1 S9b].  The DMAC's own data accesses (p4s_req_t.dma, SOC.md 3.5) clear:
//   SCFTDR2 write -> TEND and TDFE (TDFE re-sets while T < TTRG), SCFRDR2 read -> RDF (re-sets while R >= RTRG) and
//   DR if it leaves R = 0.  A CPU data access never clears a flag by itself.
// ---- module stop (MSTP3): every flop frozen, registers kept [HW run 1 S4]; accesses read 0 / ignored [P serial 9 Q9]
//   and the interrupt / DMAC request outputs are 0 (SOC.md 3.4).  Standby holds (SOC.md 2.3), so no `stby` input.
// ---- reset (power-on or manual): the initial values; FIFO pointers and counts 0, FIFO RAM kept (SOC.md 2.3).
module v1_scif import v1_soc_pkg::*; #(
  parameter int SIM_TICK_DIV = 0                   // sim-only tick override (v1_scif_brg), 0 = off
) (
  input  logic      clk,
  /* verilator lint_off UNUSEDSIGNAL */            // rst.pin (implies por) and rst.cpu (SCIF held, SOC.md 2.3)
  input  rst_t      rst,
  input  clken_t    ce,                            // only pce: every register and the whole unit are Pclk domain
  input  p4s_req_t  pq,                            // blk (one block), off[27:18] (decoded by the bridge)
  /* verilator lint_on UNUSEDSIGNAL */
  input  logic      mstp,                          // STBCR.MSTP3
  output p4s_rsp_t  pr,
  output logic      irq_eri, irq_rxi, irq_bri, irq_txi,
  output logic      dreq_tx, dreq_rx,
  output logic      txd_o, txd_oe, rts_o, rts_oe, cts_o, cts_oe,
  input  logic      rxd_i, cts_i, sck_i,
  output scif_tap_t tap
);
  // ---------------------------------------------------------------------------------------------------------------
  // state
  logic       ires, en;
  logic [7:0] smr, brr, scr, fcr;
  logic       rtsio, rtsdt, ctsio, ctsdt, spbio, spbdt;               // SCSPTR2 bits 7 6 5 4 1 0
  logic       er, tend, tdfe, brk, rdf, dr, orer;
  logic       l_er, l_tend, l_tdfe, l_brk, l_rdf, l_dr, l_orer;       // read-1 latches
  logic [3:0] perc, ferc;                                             // SCFSR2 PER3-0 / FER3-0
  logic [7:0] drt;                                                    // DR idle timer (base ticks, stops at 240)
  logic       pop_pend;                                               // TX: the loaded byte pops on the next tick
  logic [1:0] rxd_s, cts_s, rts_s, sck_s;                             // 2-flop Pphi synchronisers of the pins

  assign ires = rst.por | rst.man;
  assign en   = ce.pce & !mstp;

  // ---------------------------------------------------------------------------------------------------------------
  // sub-units
  logic [4:0] tcnt, rcnt;
  logic [7:0] thead;
  logic [9:0] rhead;
  logic       t_push, t_pop, t_clr, r_push, r_pop, r_clr;
  logic       tick;
  /* verilator lint_off UNUSEDSIGNAL */            // BRG half (smart card), RX MPB and TX start (SCI only); the
  logic       half_unused, rx_mpb_unused, tx_start; // post-access register copies: only some bits steer the units
  logic [7:0] smr_a, scr_a;
  /* verilator lint_on UNUSEDSIGNAL */
  logic       tx_load, tx_done, tx_idle_nx, tx_line;
  logic [7:0] tx_sh;
  logic       rx_start, rx_done, rx_brk, rx_fer, rx_per, rx_busy_nx;
  logic [7:0] rx_data;
  logic [7:0] brr_a, fcr_a;
  logic       te_a, re_a, loop_a, mce_a, avail, block, rx_in;
  logic [7:0] t_data;

  v1_scif_fifo #(.W(8)) u_tf (.clk, .en, .clr(t_clr), .push(t_push), .wd(pq.wdata[7:0]), .pop(t_pop),
                              .head(thead), .cnt(tcnt));
  v1_scif_fifo #(.W(10)) u_rf (.clk, .en, .clr(r_clr), .push(r_push), .wd({rx_per, rx_fer, rx_data}), .pop(r_pop),
                               .head(rhead), .cnt(rcnt));
  v1_scif_brg #(.SIM_TICK_DIV(SIM_TICK_DIV)) u_brg (.clk, .rst(ires), .en, .cks(smr_a[1:0]), .brr(brr_a),
                               .ext(scr_a[1]), .sck(sck_s[1]), .tick, .half(half_unused));
  v1_scif_tx u_tx (.clk, .rst(ires), .en, .tick, .te(te_a), .avail, .block, .data(t_data),
                   .chr(smr_a[6]), .pe(smr_a[5]), .oe(smr_a[4]), .stop(smr_a[3]), .mp(1'b0), .mpbt(1'b0),
                   .start(tx_start), .load(tx_load), .done(tx_done), .idle_nx(tx_idle_nx), .txd(tx_line), .sh_o(tx_sh));
  v1_scif_rx #(.BRK_EN(1'b1)) u_rx (.clk, .rst(ires), .en, .tick, .re(re_a), .rxd(rx_in),
                   .chr(smr_a[6]), .pe(smr_a[5]), .oe(smr_a[4]), .mp(1'b0),
                   .start(rx_start), .done(rx_done), .brk(rx_brk), .data(rx_data), .fer(rx_fer), .per(rx_per),
                   .mpb(rx_mpb_unused), .busy_nx(rx_busy_nx));

  // ---------------------------------------------------------------------------------------------------------------
  // pins (from the registered state)
  logic rts_int;
  always_comb begin
    rts_int = rtsio ? rtsdt : (fcr[3] && rcnt >= 5'd15);             // MCE: RTS2 = 1 at R >= 15 [HW run 1 S15]
    txd_o   = scr[5] ? tx_line : spbdt;                               // TE ? transmitter : port (serial 3.6)
    txd_oe  = !ires && (scr[5] || spbio);
    rts_o   = rts_int;                                                // MD8: driven 0 when MCE = 0 (serial 4)
    rts_oe  = !ires;                                                  // pins are inputs during reset (hm App. E)
    cts_o   = ctsdt;
    cts_oe  = !ires && ctsio;
  end

  // ---------------------------------------------------------------------------------------------------------------
  // register decode and read (state at the start of the Pcyc)
  localparam logic [15:0] IS_BYTE = 16'b0000_0000_0010_1010;         // BRR (1), FTDR (3), FRDR (5)
  logic        strobe, slot, match;
  logic [3:0]  ri;
  logic [15:0] rv, fsr_v, fdr_v, sptr_v;
  logic [4:0]  ttrg, rtrg;

  function automatic logic [4:0] ttrg_of(input logic [1:0] t);        // TTRG 00 8, 01 4, 10 2, 11 1
    unique case (t) 2'd0: return 5'd8; 2'd1: return 5'd4; 2'd2: return 5'd2; default: return 5'd1; endcase
  endfunction
  function automatic logic [4:0] rtrg_of(input logic [1:0] t);        // RTRG 00 1, 01 4, 10 8, 11 14
    unique case (t) 2'd0: return 5'd1; 2'd1: return 5'd4; 2'd2: return 5'd8; default: return 5'd14; endcase
  endfunction

  always_comb begin
    strobe = pq.rd | pq.wr;
    ri     = pq.off[5:2];
    slot   = pq.off[17:6] == 12'd0 && ri <= 4'd9;
    match  = slot && pq.off[1:0] == 2'd0 && pq.size == (IS_BYTE[ri] ? 2'd0 : 2'd1);
    fsr_v  = {perc, ferc, er, tend, tdfe, brk, rcnt != 5'd0 && rhead[8], rcnt != 5'd0 && rhead[9], rdf, dr};
    fdr_v  = {3'd0, tcnt, 3'd0, rcnt};
    sptr_v = {8'd0, rtsio, rts_s[1], ctsio, cts_s[1], 2'd0, spbio, rxd_s[1]};
    unique case (ri)
      4'd0:    rv = {8'd0, smr & 8'h7B};
      4'd1:    rv = {8'd0, brr};
      4'd2:    rv = {8'd0, scr & 8'hFA};
      4'd4:    rv = fsr_v;
      4'd5:    rv = {8'd0, rhead[7:0]};                               // stale RAM word when R = 0 [HW run 1 S14]
      4'd6:    rv = {8'd0, fcr};
      4'd7:    rv = fdr_v;
      4'd8:    rv = sptr_v;
      4'd9:    rv = {15'd0, orer};
      default: rv = '0;                                               // SCFTDR2 reads 0x00 [HW run 3 H2]
    endcase
    pr.rdata = (strobe && !mstp && match) ? {16'd0, rv} : 32'd0;
    pr.hit   = strobe && slot;
    pr.stall = 1'b0;
  end

  // ---------------------------------------------------------------------------------------------------------------
  // next state
  logic        acc_rd, acc_wr, wsel_fsr, dma_tx, dma_rx, t_pop_rq;
  logic [7:0]  smr_n, brr_n, scr_n, fcr_n;
  logic        rtsio_n, rtsdt_n, ctsio_n, ctsdt_n, spbio_n, spbdt_n;
  logic        er_n, tend_n, tdfe_n, brk_n, rdf_n, dr_n, orer_n;
  logic        l_er_n, l_tend_n, l_tdfe_n, l_brk_n, l_rdf_n, l_dr_n, l_orer_n;
  logic        c_er, c_tend, c_tdfe, c_brk, c_rdf, c_dr, c_orer;
  logic [3:0]  perc_a, ferc_a, perc_n, ferc_n;
  logic [7:0]  drt_n;
  logic        pop_pend_n;
  logic [4:0]  t_acc, t_nx, r_acc, r_nx;
  logic        er_set, orer_set;

  always_comb begin
    // ---- 1. register access
    acc_rd = en && strobe && match && pq.rd;
    acc_wr = en && strobe && match && pq.wr;
    smr_n = smr; brr_n = brr; scr_n = scr; fcr_n = fcr;
    {rtsio_n, rtsdt_n, ctsio_n, ctsdt_n, spbio_n, spbdt_n} = {rtsio, rtsdt, ctsio, ctsdt, spbio, spbdt};
    {l_er_n, l_tend_n, l_tdfe_n, l_brk_n, l_rdf_n, l_dr_n, l_orer_n} = {l_er, l_tend, l_tdfe, l_brk, l_rdf, l_dr, l_orer};
    {c_er, c_tend, c_tdfe, c_brk, c_rdf, c_dr, c_orer} = '0;
    wsel_fsr = acc_wr && ri == 4'd4;
    dma_tx   = acc_wr && ri == 4'd3 && pq.dma;
    dma_rx   = acc_rd && ri == 4'd5 && pq.dma;
    if (acc_wr) begin
      unique case (ri)
        4'd0: smr_n = pq.wdata[7:0] & 8'h7B;
        4'd1: brr_n = pq.wdata[7:0];
        4'd2: scr_n = pq.wdata[7:0] & 8'hFA;
        4'd6: fcr_n = pq.wdata[7:0];
        4'd8: {rtsio_n, rtsdt_n, ctsio_n, ctsdt_n, spbio_n, spbdt_n} =
                {pq.wdata[7:4], pq.wdata[1:0]};
        default: ;
      endcase
    end
    if (wsel_fsr) begin                                                // write 0 after read 1
      c_er   = !pq.wdata[7] && l_er;   c_tend = !pq.wdata[6] && l_tend; c_tdfe = !pq.wdata[5] && l_tdfe;
      c_brk  = !pq.wdata[4] && l_brk;  c_rdf  = !pq.wdata[1] && l_rdf;  c_dr   = !pq.wdata[0] && l_dr;
      l_er_n   = l_er   && pq.wdata[7]; l_tend_n = l_tend && pq.wdata[6]; l_tdfe_n = l_tdfe && pq.wdata[5];
      l_brk_n  = l_brk  && pq.wdata[4]; l_rdf_n  = l_rdf  && pq.wdata[1]; l_dr_n   = l_dr   && pq.wdata[0];
    end
    if (acc_wr && ri == 4'd9) begin c_orer = !pq.wdata[0] && l_orer; l_orer_n = l_orer && pq.wdata[0]; end
    if (acc_rd && ri == 4'd4) begin                                    // arm the read-1 latches
      l_er_n = l_er_n || er; l_tend_n = l_tend_n || tend; l_tdfe_n = l_tdfe_n || tdfe;
      l_brk_n = l_brk_n || brk; l_rdf_n = l_rdf_n || rdf; l_dr_n = l_dr_n || dr;
    end
    if (acc_rd && ri == 4'd9) l_orer_n = l_orer_n || orer;
    smr_a = smr_n; brr_a = brr_n; scr_a = scr_n; fcr_a = fcr_n;
    te_a = scr_a[5]; re_a = scr_a[4]; loop_a = fcr_a[0]; mce_a = fcr_a[3];
    ttrg = ttrg_of(fcr_a[5:4]); rtrg = rtrg_of(fcr_a[7:6]);
    // FIFOs, access side: TX push first (a write at T = 16 is ignored [HW run 1 S7 "18 writes -> 16"]), RX pop
    // first; TFRST / RFRST hold the FIFO empty and discard writes [HW run 2 C, run 3 H1: the silicon's short-lived
    // T = 1 after such a write is the documented difference, serial 9 Q2]
    t_push = acc_wr && ri == 4'd3 && !fcr_a[2] && tcnt != 5'd16;
    t_acc  = fcr_a[2] ? 5'd0 : tcnt + {4'd0, t_push};
    r_pop  = acc_rd && ri == 4'd5 && rcnt != 5'd0 && !fcr_a[1];
    r_acc  = fcr_a[1] ? 5'd0 : rcnt - {4'd0, r_pop};
    perc_a = fcr_a[1] ? 4'd0 : perc - {3'd0, r_pop && rhead[9]};
    ferc_a = fcr_a[1] ? 4'd0 : ferc - {3'd0, r_pop && rhead[8]};
    t_clr  = ires || (en && fcr_a[2]);
    r_clr  = ires || (en && fcr_a[1]);
  end

  always_comb begin
    // ---- 2. BRG tick (u_brg), TX
    t_pop_rq = pop_pend && tick;
    t_pop  = t_pop_rq && t_acc != 5'd0 && !fcr_a[2];
    t_nx   = t_acc - {4'd0, t_pop};
    avail  = t_nx != 5'd0 && !fcr_a[2];
    block  = mce_a && (loop_a ? rts_int : cts_s[1]);                    // CTS = 1 blocks a frame start (3.2.5)
    t_data = (tcnt == 5'd0 && t_push) ? pq.wdata[7:0] : thead;         // head written this Pcyc
    rx_in  = loop_a ? tx_line : rxd_s[1];                               // loop before the port mux [HW run 1 S12]
  end

  always_comb begin
    // the pop-pending stage: the head was consumed by the load at S + 16 (serial 3.2.2 step 3) and the count moves one
    // tick later [HW run 2 E1: T - 1 at B0 + 23 = S + 17].  TFRST discards the FIFO, so it also cancels the pending pop:
    // a byte written after the TFRST is a new head that the load never took, and must not be removed at S + 17
    pop_pend_n = !fcr_a[2] && ((pop_pend && !tick) || tx_load);
    // RX: push rules at the stop sample (serial 3.3.3, in priority order); a break frame is never stored
    er_set = 1'b0; orer_set = 1'b0; r_push = 1'b0;
    if (rx_done && !fcr_a[1] && !(orer && !c_orer)) begin               // 1. ORER set: discard [HW run 1 S11]
      if (r_acc == 5'd16) begin                                         // 2. full: ER (with FER) or ORER
        if (rx_fer) er_set = 1'b1; else orer_set = 1'b1;
      end else begin                                                    // 4. push
        r_push = 1'b1; er_set = rx_fer || rx_per;
      end
    end
    r_nx   = r_acc + {4'd0, r_push};
    perc_n = perc_a + {3'd0, r_push && rx_per};                         // 16 wraps to 0 (4 bits)
    ferc_n = ferc_a + {3'd0, r_push && rx_fer};
    // DR timer: cleared by a push and by a start detection, counts ticks while no frame is in progress, stops at 240
    if (rx_start || r_push) drt_n = 8'd0;
    else if (tick && !rx_busy_nx && drt != 8'd240) drt_n = drt + 8'd1;
    else drt_n = drt;
    // ---- 3. flags
    er_n   = (er   && !c_er)   || er_set;
    brk_n  = (brk  && !c_brk)  || rx_brk;
    orer_n = (orer && !c_orer) || orer_set;
    tdfe_n = (tdfe && !(c_tdfe || dma_tx)) || t_nx < ttrg;             // "fewer than" [HW run 2 D1 / D2]
    tend_n = (tend && !(c_tend || dma_tx)) || !te_a || (tx_idle_nx && t_nx == 5'd0);
    rdf_n  = (rdf  && !(c_rdf  || dma_rx)) || r_nx >= rtrg;
    dr_n   = (dr   && !((c_dr || dma_rx) && r_acc == 5'd0)) ||
             (drt_n == 8'd240 && r_nx != 5'd0 && r_nx < rtrg);          // 15 bit times [HW run 2 E4: 240 ticks]
  end

  always_ff @(posedge clk) begin
    if (ires) begin
      smr <= '0; brr <= 8'hFF; scr <= '0; fcr <= '0;
      {rtsio, rtsdt, ctsio, ctsdt, spbio, spbdt} <= '0;
      {er, tend, tdfe, brk, rdf, dr, orer} <= 7'b0110000;              // SCFSR2 = 0x0060, SCLSR2 = 0
      {l_er, l_tend, l_tdfe, l_brk, l_rdf, l_dr, l_orer} <= '0;
      perc <= '0; ferc <= '0; drt <= '0; pop_pend <= 1'b0;
      rxd_s <= 2'b11; cts_s <= 2'b11; rts_s <= 2'b00; sck_s <= 2'b11;  // idle levels [P]
    end else if (en) begin
      smr <= smr_n; brr <= brr_n; scr <= scr_n; fcr <= fcr_n;
      {rtsio, rtsdt, ctsio, ctsdt, spbio, spbdt} <= {rtsio_n, rtsdt_n, ctsio_n, ctsdt_n, spbio_n, spbdt_n};
      {er, tend, tdfe, brk, rdf, dr, orer} <= {er_n, tend_n, tdfe_n, brk_n, rdf_n, dr_n, orer_n};
      {l_er, l_tend, l_tdfe, l_brk, l_rdf, l_dr, l_orer} <= {l_er_n, l_tend_n, l_tdfe_n, l_brk_n, l_rdf_n, l_dr_n, l_orer_n};
      perc <= perc_n; ferc <= ferc_n; drt <= drt_n; pop_pend <= pop_pend_n;
      rxd_s <= {rxd_s[0], rxd_i};
      cts_s <= {cts_s[0], cts_oe ? cts_o : cts_i};                     // the CTS2 pin level (driven or input)
      rts_s <= {rts_s[0], rts_o};
      sck_s <= {sck_s[0], sck_i};
    end
  end

  // ---------------------------------------------------------------------------------------------------------------
  // interrupt and DMAC request levels (serial 3.5; not gated by TE / RE or the interrupt enables for the DMAC [HW
  // run 1 S17]); a module-stopped unit drives 0 (SOC.md 3.4)
  always_comb begin
    irq_eri = !mstp && (scr[6] || scr[3]) && er;
    irq_rxi = !mstp && scr[6] && (rdf || dr);
    irq_bri = !mstp && (scr[6] || scr[3]) && (brk || orer);           // REIE alone gives BRI [HW run 1 I5]
    irq_txi = !mstp && scr[7] && tdfe;
    dreq_tx = !mstp && tdfe;                                            // RS 1010
    dreq_rx = !mstp && (rdf || dr);                                     // RS 1011
    tap.tx_v    = tx_done;                                              // the byte left the shift register
    tap.tx_byte = tx_sh;
    tap.rx_v    = r_push;                                               // the byte entered the receive FIFO
    tap.rx_byte = rx_data;
  end
endmodule
