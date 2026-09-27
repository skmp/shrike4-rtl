// v1_sci.sv -- the SCI (serial channel 1: asynchronous, multiprocessor, clocked synchronous, and the ISO 7816-3 smart
// card extension) of the SH7091 (rtl/v1/SOC.md 2.3, 2.4, 3.5, 3.8; spec serial 2.1, 2.3, 3.7-3.9).  Owner: WI-SER.
// On the Dreamcast the module clock is stopped (STBCR.MSTP0, the boot stub writes STBCR = 0x03) and its pins are the
// MD0 (SCK) / MD7 (TxD) mode straps, so its serial timing could never be measured; its registers, module standby and
// pin reads were [HW serial run 2 A1-A3, run 3 I].
//
// Structure: v1_scif_brg (shared with the SCIF), v1_scif_tx / v1_scif_rx for the asynchronous and multiprocessor
// formats (the SCIF's sequencer and constants: serial 3.7 "same sequencer and timing as the SCIF" [P]), and two small
// sequencers in this file: the clocked-synchronous one (8 bits LSB first, SCK toggling on every base tick) and the
// smart-card one (372 base ticks per etu).  SCTDR1 / SCRDR1 are one-byte double buffers (no FIFO).
//
// ---- one Pcyc (`en` = ce.pce): 1. the register access (reads see the state at the start of the Pcyc and arm the
// read-1 latches of SCSSR1 bits 7-3; writes and the DMAC auto-clears), 2. the BRG and the sequencers on the state
// after the access, 3. the flags: clears, then sets, then the TE = 0 level (TDRE and TEND fixed at 1 while TE = 0
// [HW run 2 A2: "SSR1 86 w00->86"]).
// ---- module stop (MSTP0) and standby (`stby`): the module is held at its initial values, reads 0, writes are
// ignored, except SCSPTR1, which keeps its value [HW run 2 A3: before 01 5a 00 a5 85 00 04 85, during all 00, after
// 00 ff 00 ff 84 00 00 85]; the bus access still completes at its class timing (the bridge's).  Outputs 0 (SOC.md 3.4).
// ---- registers (all 8-bit; another size reads 0 / write ignored [HW run 1 S2]; 0x20-0x30 holes; no mirrors):
//   00 SCSMR1 0xFF | 04 SCBRR1 | 08 SCSCR1 0xFF [HW A2 dc->dc] | 0C SCTDR1 (reads back) | 10 SCSSR1 (7-3 R/(W)0,
//   2-1 R, 0 R/W) | 14 SCRDR1 R | 18 SCSCMR1 0x0D [HW A2] | 1C SCSPTR1 (EIO SPB1IO SPB0IO as written, bit 2 = SCK pin,
//   bit 0 = RxD pin, 6-4 = 0; initialised only by power-on / manual reset [HW A2 f0->85 80->85 00->05]).
// Known difference: the silicon's SCSSR1.MPB reads 1 after RE was 1 for 40 k Icyc with no frame [HW run 2 A2, run 3 I:
// 0x86]; here MPB changes only on a received multiprocessor frame (0x84) [P serial 9 Q5].
module v1_sci import v1_soc_pkg::*; #(
  parameter int SIM_TICK_DIV = 0                   // sim-only tick override (v1_scif_brg), 0 = off
) (
  input  logic     clk,
  /* verilator lint_off UNUSEDSIGNAL */            // rst.pin (implies por) / rst.cpu (held); pq.blk, off[27:18]
  input  rst_t     rst,
  input  clken_t   ce,                             // only pce
  input  p4s_req_t pq,
  /* verilator lint_on UNUSEDSIGNAL */
  input  logic     mstp,                           // STBCR.MSTP0
  input  logic     stby,                           // standby: initialise (except SCSPTR1)
  output p4s_rsp_t pr,
  output logic     irq_eri, irq_rxi, irq_txi, irq_tei,
  output logic     dreq_tx, dreq_rx,
  output logic     txd_o, txd_oe, sck_o, sck_oe,
  input  logic     rxd_i, sck_i
);
  logic ires, mres, en, acc_ok;
  assign ires  = rst.por | rst.man;
  assign mres  = ires | mstp | stby;                // the module's own state: initial values while stopped
  assign en    = ce.pce;
  assign acc_ok = !mstp && !stby;                    // registers accessible

  // ---------------------------------------------------------------------------------------------------------------
  // state
  logic [7:0] smr, brr, scr, tdr, rdr;
  logic       sdir, sinv, smif, mpbt;
  logic       tdre, rdrf, orer, fer, per, tend, mpb;
  logic       l_tdre, l_rdrf, l_orer, l_fer, l_per;
  logic       eio, spb1io, spb1dt, spb0io, spb0dt;           // SCSPTR1 (power-on / manual reset only)
  logic [1:0] rxd_s, sck_s;                                    // pin synchronisers
  logic [3:0] bph;                                             // async: base ticks within the bit (SCK output)
  // clocked synchronous
  logic       sy_run, sy_txo, sy_sck, sy_cont, sy_q;
  logic [3:0] sy_e;
  logic [7:0] sy_tsr, sy_rsr;
  // smart card: transmitter and receiver
  logic [1:0] ct_st;  logic [8:0] ct_t;  logic [3:0] ct_k;  logic [7:0] ct_tsr;  logic ct_oe, ct_bit;
  logic       cr_run; logic [8:0] cr_t;  logic [3:0] cr_k;  logic [7:0] cr_sh;   logic cr_de, cr_deon;

  // ---------------------------------------------------------------------------------------------------------------
  // sub-units
  logic       tick, half;
  logic       tx_start, tx_load, tx_done, tx_line;
  logic       rx_done, rx_fer, rx_per, rx_mpb;
  /* verilator lint_off UNUSEDSIGNAL */            // sub-unit outputs only the SCIF uses (TEND level, tap, DR, break)
  logic       tx_idle_nx_unused, rx_start_unused, rx_brk_unused, rx_busy_nx_unused;
  logic [7:0] tx_sh_unused;
  /* verilator lint_on UNUSEDSIGNAL */
  logic [7:0] rx_data;
  logic [7:0] smr_a, brr_a, tdr_a;
  /* verilator lint_off UNUSEDSIGNAL */            // the post-access SCSCR1 copy: only TE RE MPIE CKE1 steer the units
  logic [7:0] scr_a;
  /* verilator lint_on UNUSEDSIGNAL */
  logic       smif_a, async_a, sync_a, te_a, re_a, mpbt_a, tdre_a;

  v1_scif_brg #(.SIM_TICK_DIV(SIM_TICK_DIV)) u_brg (.clk, .rst(mres), .en, .cks(smr_a[1:0]), .brr(brr_a),
                  .ext(async_a && scr_a[1]), .sck(sck_s[1]), .tick, .half);
  v1_scif_tx u_tx (.clk, .rst(mres), .en, .tick, .te(te_a && async_a), .avail(!tdre_a), .block(1'b0), .data(tdr_a),
                   .chr(smr_a[6]), .pe(smr_a[5]), .oe(smr_a[4]), .stop(smr_a[3]), .mp(smr_a[2]), .mpbt(mpbt_a),
                   .start(tx_start), .load(tx_load), .done(tx_done), .idle_nx(tx_idle_nx_unused), .txd(tx_line),
                   .sh_o(tx_sh_unused));
  v1_scif_rx #(.BRK_EN(1'b0)) u_rx (.clk, .rst(mres), .en, .tick, .re(re_a && async_a), .rxd(rxd_s[1]),
                   .chr(smr_a[6]), .pe(smr_a[5]), .oe(smr_a[4]), .mp(smr_a[2]),
                   .start(rx_start_unused), .done(rx_done), .brk(rx_brk_unused), .data(rx_data), .fer(rx_fer),
                   .per(rx_per), .mpb(rx_mpb), .busy_nx(rx_busy_nx_unused));

  // ---------------------------------------------------------------------------------------------------------------
  // pins (registered state).  Port function: SPB0IO / SPB1IO drive SPB0DT / SPB1DT (hm 15.2.8).
  logic sck_pin;
  always_comb begin
    txd_o = spb0dt; txd_oe = spb0io;
    sck_o = spb1dt; sck_oe = spb1io;
    if (smif) begin                                            // smart card: TxD high-impedance unless driving
      txd_oe = ct_oe || cr_deon; txd_o = !cr_deon && ct_bit;
      if (scr[0])      begin sck_oe = 1'b1; sck_o = half;   end // CKE0: the base clock (372 x the bit rate)
      else if (smr[7]) begin sck_oe = 1'b1; sck_o = scr[1]; end // GM = 1: fixed at CKE1
    end else if (smr[7]) begin                                 // clocked synchronous
      if (scr[5]) begin txd_oe = 1'b1; txd_o = sy_txo; end
      if (!scr[1]) begin sck_oe = 1'b1; sck_o = sy_sck; end    // internal clock: SCK is an output, idle high
      else sck_oe = 1'b0;
    end else begin                                             // asynchronous
      if (scr[5]) begin txd_oe = 1'b1; txd_o = tx_line; end
      if (scr[1]) sck_oe = 1'b0;                               // external clock input (16 x the bit rate)
      else if (scr[0]) begin sck_oe = 1'b1; sck_o = bph[3]; end // bit clock, rising edge at each bit centre
    end
    if (ires) begin txd_oe = 1'b0; sck_oe = 1'b0; end          // mode pins during reset (hm App. E)
    sck_pin = sck_oe ? sck_o : sck_i;
  end

  // ---------------------------------------------------------------------------------------------------------------
  // register decode and read
  logic        strobe, slot, match;
  logic [2:0]  ri;
  logic [7:0]  rv;
  always_comb begin
    strobe = pq.rd | pq.wr;
    ri     = pq.off[4:2];
    slot   = pq.off[17:5] == 13'd0;
    match  = slot && pq.off[1:0] == 2'd0 && pq.size == 2'd0;
    unique case (ri)
      3'd0: rv = smr;
      3'd1: rv = brr;
      3'd2: rv = scr;
      3'd3: rv = tdr;
      3'd4: rv = {tdre, rdrf, orer, fer, per, tend, mpb, mpbt};
      3'd5: rv = rdr;
      3'd6: rv = {4'd0, sdir, sinv, 1'b0, smif};
      default: rv = {eio, 3'd0, spb1io, sck_s[1], spb0io, rxd_s[1]};
    endcase
    pr.rdata = (strobe && match && acc_ok) ? {24'd0, rv} : 32'd0;
    pr.hit   = strobe && slot;
    pr.stall = 1'b0;
  end

  // ---------------------------------------------------------------------------------------------------------------
  // next state
  logic       acc_rd, acc_wr, c_tdre, c_rdrf, c_orer, c_fer, c_per, dma_tdr, dma_rdr;
  logic [7:0] smr_n, brr_n, scr_w, scr_n, tdr_n, rdr_n;
  logic       sdir_n, sinv_n, smif_n, mpbt_n;
  logic       eio_n, spb1io_n, spb1dt_n, spb0io_n, spb0dt_n;
  logic       l_tdre_n, l_rdrf_n, l_orer_n, l_fer_n, l_per_n;
  logic       tdre_c, tend_c, rdrf_c, orer_c, fer_c, per_c;
  logic       tdre_set, tend_set, rdrf_set, orer_set, fer_set, per_set;
  logic       tdre_n, tend_n, rdrf_n, orer_n, fer_n, per_n, mpb_n;
  logic [3:0] bph_n;
  logic       sy_run_n, sy_txo_n, sy_sck_n, sy_cont_n, ev_fall, ev_rise, sy_go;
  logic [3:0] sy_e_n;
  logic [7:0] sy_tsr_n, sy_rsr_n, rx_byte;
  logic [1:0] ct_st_n;  logic [8:0] ct_t_n;  logic [3:0] ct_k_n;  logic [7:0] ct_tsr_n;  logic ct_oe_n, ct_bit_n;
  logic       cr_run_n; logic [8:0] cr_t_n;  logic [3:0] cr_k_n;  logic [7:0] cr_sh_n;   logic cr_de_n, cr_deon_n;
  logic       line, sc_rx_store, sc_per;
  logic [2:0] idx;                                             // bit position (0-7) of cell k (1-8)

  localparam logic [1:0] CT_IDLE = 2'd0, CT_FRM = 2'd1, CT_RETRY = 2'd2;
  localparam logic [8:0] ETU = 9'd372, HALF = 9'd186;          // smart card: 372 base ticks / etu, sample at 186

  always_comb begin
    // ---- 1. register access
    acc_rd = en && strobe && match && acc_ok && pq.rd;
    acc_wr = en && strobe && match && acc_ok && pq.wr;
    smr_n = smr; brr_n = brr; scr_w = scr; tdr_n = tdr;
    {sdir_n, sinv_n, smif_n, mpbt_n} = {sdir, sinv, smif, mpbt};
    {eio_n, spb1io_n, spb1dt_n, spb0io_n, spb0dt_n} = {eio, spb1io, spb1dt, spb0io, spb0dt};
    {l_tdre_n, l_rdrf_n, l_orer_n, l_fer_n, l_per_n} = {l_tdre, l_rdrf, l_orer, l_fer, l_per};
    {c_tdre, c_rdrf, c_orer, c_fer, c_per} = '0;
    dma_tdr = acc_wr && ri == 3'd3 && pq.dma;                   // DMAC write of SCTDR1: TDRE and TEND clear
    dma_rdr = acc_rd && ri == 3'd5 && pq.dma;                   // DMAC read of SCRDR1: RDRF clears
    if (acc_wr) begin
      unique case (ri)
        3'd0: smr_n = pq.wdata[7:0];
        3'd1: brr_n = pq.wdata[7:0];
        3'd2: scr_w = pq.wdata[7:0];
        3'd3: tdr_n = pq.wdata[7:0];
        3'd4: begin
          c_tdre = !pq.wdata[7] && l_tdre; c_rdrf = !pq.wdata[6] && l_rdrf; c_orer = !pq.wdata[5] && l_orer;
          c_fer  = !pq.wdata[4] && l_fer;  c_per  = !pq.wdata[3] && l_per;
          l_tdre_n = l_tdre && pq.wdata[7]; l_rdrf_n = l_rdrf && pq.wdata[6]; l_orer_n = l_orer && pq.wdata[5];
          l_fer_n  = l_fer  && pq.wdata[4]; l_per_n  = l_per  && pq.wdata[3];
          mpbt_n = pq.wdata[0];
        end
        3'd6: {sdir_n, sinv_n, smif_n} = {pq.wdata[3], pq.wdata[2], pq.wdata[0]};
        3'd7: {eio_n, spb1io_n, spb1dt_n, spb0io_n, spb0dt_n} = {pq.wdata[7], pq.wdata[3:0]};
        default: ;                                              // SCRDR1: read-only [HW A2 "RDR1 w33->00"]
      endcase
    end
    if (acc_rd && ri == 3'd4) begin
      l_tdre_n = l_tdre_n || tdre; l_rdrf_n = l_rdrf_n || rdrf; l_orer_n = l_orer_n || orer;
      l_fer_n  = l_fer_n  || fer;  l_per_n  = l_per_n  || per;
    end
    smr_a = smr_n; brr_a = brr_n; scr_a = scr_w; tdr_a = tdr_n; mpbt_a = mpbt_n;
    smif_a  = smif_n;
    sync_a  = !smif_a && smr_a[7];
    async_a = !smif_a && !smr_a[7];
    te_a = scr_a[5]; re_a = scr_a[4];
    tdre_c = tdre && !(c_tdre || dma_tdr);
    tend_c = tend && !(c_tdre || dma_tdr);                      // TEND clears with TDRE (hm 15.2.7)
    rdrf_c = rdrf && !(c_rdrf || dma_rdr);
    orer_c = orer && !c_orer;  fer_c = fer && !c_fer;  per_c = per && !c_per;
    tdre_a = tdre_c || !te_a;
    line   = rxd_s[1];
  end

  always_comb begin
    {tdre_set, tend_set, rdrf_set, orer_set, fer_set, per_set} = '0;
    mpb_n = mpb; scr_n = scr_a; rdr_n = rdr;

    // ---- 2a. asynchronous / multiprocessor (u_tx, u_rx)
    if (tx_load) tdre_set = 1'b1;                                // SCTDR1 -> SCTSR1 at S + 16 [P serial 3.7]
    if (tx_done && tdre_a) tend_set = 1'b1;                      // no data at the end of the stop bit
    bph_n = bph;
    if (tick) bph_n = tx_start ? 4'd0 : bph + 4'd1;
    if (rx_done && !(orer_c || fer_c || per_c)) begin           // an error flag stops reception (hm 15.3.2)
      if (smr_a[2]) mpb_n = rx_mpb;                              // MPB of every received MP frame
      if (!(smr_a[2] && scr_a[3] && !rx_mpb)) begin              // MPIE: skip frames with MPB = 0
        if (smr_a[2] && rx_mpb) scr_n[3] = 1'b0;                 // MPB = 1 clears MPIE
        if (rdrf_c) begin                                        // hm Table 15.13: ORER (+ FER / PER), no transfer
          orer_set = 1'b1; fer_set = rx_fer; per_set = rx_per;
        end else if (rx_fer || rx_per) begin                     // data to SCRDR1 without RDRF
          rdr_n = rx_data; fer_set = rx_fer; per_set = rx_per;
        end else begin
          rdr_n = rx_data; rdrf_set = 1'b1;
        end
      end
    end

    // ---- 2b. clocked synchronous: SCK falls -> the next bit out, rises -> the bit in; TDRE checked at the MSB
    sy_run_n = sy_run; sy_txo_n = sy_txo; sy_sck_n = sy_sck; sy_cont_n = sy_cont; sy_e_n = sy_e;
    sy_tsr_n = sy_tsr; sy_rsr_n = sy_rsr; rx_byte = {line, sy_rsr[6:0]};
    ev_fall  = scr_a[1] ? (sy_q && !sck_s[1]) : tick;            // external clock: SCK input edges
    ev_rise  = scr_a[1] ? (!sy_q && sck_s[1]) : tick;
    sy_go    = (te_a && !tdre_a) || (re_a && !te_a);
    if (!sync_a || !(te_a || re_a) || (!sy_run && orer_c)) begin
      sy_run_n = 1'b0; sy_sck_n = 1'b1; sy_e_n = '0;
    end else if (!sy_run) begin
      if (ev_fall && sy_go) begin                                // first falling edge: load and drive bit 0
        sy_run_n = 1'b1; sy_e_n = 4'd1; sy_sck_n = scr_a[1] ? sy_sck : 1'b0;
        if (te_a) begin sy_tsr_n = tdr_a; sy_txo_n = tdr_a[0]; tdre_set = 1'b1; end
      end
    end else if (!sy_e[0] && ev_fall) begin                      // falling edge: drive bit e/2
      if (!scr_a[1]) sy_sck_n = 1'b0;
      sy_txo_n = sy_tsr[sy_e[3:1]];
      if (sy_e == 4'd14 && te_a) begin
        if (!tdre_a) begin sy_tsr_n = tdr_a; tdre_set = 1'b1; sy_cont_n = 1'b1; end
        else begin tend_set = 1'b1; sy_cont_n = 1'b0; end        // TxD holds the MSB
      end
      sy_e_n = sy_e + 4'd1;
    end else if (sy_e[0] && ev_rise) begin                       // rising edge: sample bit e/2
      if (!scr_a[1]) sy_sck_n = 1'b1;
      sy_rsr_n[sy_e[3:1]] = line;
      if (sy_e == 4'd15) begin                                   // a byte is complete
        if (re_a) begin
          if (rdrf_c) orer_set = 1'b1;                           // overrun: lost; stops TX and RX (hm 15.2.7)
          else begin rdr_n = rx_byte; rdrf_set = 1'b1; end
        end
        if ((te_a ? sy_cont : re_a) && !(orer_c || orer_set)) sy_e_n = 4'd0;
        else begin sy_run_n = 1'b0; sy_e_n = 4'd0; end
      end else sy_e_n = sy_e + 4'd1;
    end

    // ---- 2c. smart card (serial 3.8, hm 17): etu = 372 ticks; transmitter
    ct_st_n = ct_st; ct_t_n = ct_t; ct_k_n = ct_k; ct_tsr_n = ct_tsr; ct_oe_n = ct_oe; ct_bit_n = ct_bit;
    idx = '0;
    if (!smif_a || !te_a) begin
      ct_st_n = CT_IDLE; ct_oe_n = 1'b0; ct_bit_n = 1'b1;
    end else if (tick) begin
      if (ct_st == CT_IDLE) begin
        if (!tdre_a) begin                                       // start bit; SCTDR1 -> SCTSR1 (hm Fig. 17.12)
          ct_st_n = CT_FRM; ct_t_n = '0; ct_k_n = '0; ct_tsr_n = tdr_a; tdre_set = 1'b1;
          ct_oe_n = 1'b1; ct_bit_n = 1'b0;
        end
      end else begin
        ct_t_n = ct_t + 9'd1;
        if (ct_t_n == ETU) begin
          ct_t_n = '0; ct_k_n = ct_k + 4'd1;
          if (ct_k_n >= 4'd1 && ct_k_n <= 4'd8 && ct_st == CT_FRM) begin
            idx = 3'(sdir ? 4'd8 - ct_k_n : ct_k_n - 4'd1);           // SDIR: MSB first
            ct_bit_n = ct_tsr[idx] ^ sinv;                  // SINV: data bits inverted
          end else if (ct_k_n == 4'd9 && ct_st == CT_FRM) ct_bit_n = smr_a[4] ^ (^ct_tsr);   // parity (O/E)
          else if (ct_k_n == 4'd10) begin ct_oe_n = 1'b0; ct_bit_n = 1'b1; end            // release
          else if (ct_k_n == 4'd11 && ct_st == CT_FRM) begin     // sample the DE slot (10.5-11.5 etu) at 11.0 [P]
            if (!line) begin ct_st_n = CT_RETRY; fer_set = 1'b1; end    // ERS
            else if (smr_a[7]) tend_set = 1'b1;                   // GM = 1: TEND at 11.0 etu
          end else if (ct_k_n == 4'd12 && ct_st == CT_FRM && smr_a[7]) ct_st_n = CT_IDLE;
          else if (ct_k_n == 4'd14 && ct_st == CT_RETRY) begin   // resend after >= 2 etu [P 14.0 etu]
            ct_st_n = CT_FRM; ct_k_n = '0; ct_oe_n = 1'b1; ct_bit_n = 1'b0;
          end
        end else if (ct_t_n == HALF && ct_k == 4'd12 && ct_st == CT_FRM && !smr_a[7]) begin
          tend_set = 1'b1; ct_st_n = CT_IDLE;                    // GM = 0: TEND at 12.5 etu
        end
      end
    end
    // smart card receiver: samples at tick 186 of each etu; parity error -> PER and DE low 10.5-11.5 etu
    cr_run_n = cr_run; cr_t_n = cr_t; cr_k_n = cr_k; cr_sh_n = cr_sh; cr_de_n = cr_de; cr_deon_n = cr_deon;
    sc_rx_store = 1'b0; sc_per = 1'b0;
    if (!smif_a || !re_a) begin
      cr_run_n = 1'b0; cr_deon_n = 1'b0; cr_de_n = 1'b0;
    end else if (tick) begin
      if (!cr_run) begin
        if (!line) begin cr_run_n = 1'b1; cr_t_n = '0; cr_k_n = '0; cr_de_n = 1'b0; end
      end else begin
        cr_t_n = cr_t + 9'd1;
        if (cr_t_n == ETU) begin cr_t_n = '0; cr_k_n = cr_k + 4'd1; end
        if (cr_t_n == HALF) begin
          if (cr_k_n == 4'd0) begin if (line) cr_run_n = 1'b0; end               // false start [P]
          else if (cr_k_n <= 4'd8) begin
            idx = 3'(sdir ? 4'd8 - cr_k_n : cr_k_n - 4'd1);
            cr_sh_n[idx] = line ^ sinv;
          end else if (cr_k_n == 4'd9) begin
            sc_per = line != (smr_a[4] ^ (^cr_sh));
            sc_rx_store = 1'b1;
          end else if (cr_k_n == 4'd10) cr_deon_n = cr_de;        // DE from 10.5 etu
          else if (cr_k_n == 4'd11) begin cr_deon_n = 1'b0; cr_run_n = 1'b0; end   // busy until 11.5 etu
        end
      end
    end
    if (sc_rx_store && !orer_c) begin                            // PER does not stop reception here (hm 17.4)
      if (sc_per) begin per_set = 1'b1; cr_de_n = 1'b1; end       // RDRF not set; request a retransfer
      else if (rdrf_c) orer_set = 1'b1;
      else begin rdr_n = cr_sh; rdrf_set = 1'b1; end
    end

    // ---- 3. flags
    tdre_n = tdre_c || tdre_set || !te_a;
    tend_n = tend_c || tend_set || !te_a;
    rdrf_n = rdrf_c || rdrf_set;
    orer_n = orer_c || orer_set;
    fer_n  = fer_c  || fer_set;
    per_n  = per_c  || per_set;
  end

  always_ff @(posedge clk) begin
    if (mres) begin
      smr <= '0; brr <= 8'hFF; scr <= '0; tdr <= 8'hFF; rdr <= '0;
      {sdir, sinv, smif, mpbt} <= '0;
      {tdre, rdrf, orer, fer, per, tend, mpb} <= 7'b1000010;    // SCSSR1 = 0x84
      {l_tdre, l_rdrf, l_orer, l_fer, l_per} <= '0;
      bph <= '0;
      sy_run <= 1'b0; sy_txo <= 1'b1; sy_sck <= 1'b1; sy_cont <= 1'b0; sy_e <= '0; sy_tsr <= '0; sy_rsr <= '0;
      ct_st <= CT_IDLE; ct_t <= '0; ct_k <= '0; ct_tsr <= '0; ct_oe <= 1'b0; ct_bit <= 1'b1;
      cr_run <= 1'b0; cr_t <= '0; cr_k <= '0; cr_sh <= '0; cr_de <= 1'b0; cr_deon <= 1'b0;
    end else if (en) begin
      smr <= smr_n; brr <= brr_n; scr <= scr_n; tdr <= tdr_n; rdr <= rdr_n;
      {sdir, sinv, smif, mpbt} <= {sdir_n, sinv_n, smif_n, mpbt_n};
      {tdre, rdrf, orer, fer, per, tend, mpb} <= {tdre_n, rdrf_n, orer_n, fer_n, per_n, tend_n, mpb_n};
      {l_tdre, l_rdrf, l_orer, l_fer, l_per} <= {l_tdre_n, l_rdrf_n, l_orer_n, l_fer_n, l_per_n};
      bph <= bph_n;
      sy_run <= sy_run_n; sy_txo <= sy_txo_n; sy_sck <= sy_sck_n; sy_cont <= sy_cont_n; sy_e <= sy_e_n;
      sy_tsr <= sy_tsr_n; sy_rsr <= sy_rsr_n;
      ct_st <= ct_st_n; ct_t <= ct_t_n; ct_k <= ct_k_n; ct_tsr <= ct_tsr_n; ct_oe <= ct_oe_n; ct_bit <= ct_bit_n;
      cr_run <= cr_run_n; cr_t <= cr_t_n; cr_k <= cr_k_n; cr_sh <= cr_sh_n; cr_de <= cr_de_n; cr_deon <= cr_deon_n;
    end
  end

  // SCSPTR1 and the pin synchronisers: power-on / manual reset only (kept across module stop and standby)
  always_ff @(posedge clk) begin
    if (ires) begin
      {eio, spb1io, spb1dt, spb0io, spb0dt} <= '0;
      rxd_s <= 2'b11; sck_s <= 2'b11; sy_q <= 1'b1;
    end else if (en) begin
      if (acc_ok) {eio, spb1io, spb1dt, spb0io, spb0dt} <= {eio_n, spb1io_n, spb1dt_n, spb0io_n, spb0dt_n};
      rxd_s <= {rxd_s[0], rxd_i};
      sck_s <= {sck_s[0], sck_pin};
      sy_q  <= sck_s[1];
    end
  end

  // ---------------------------------------------------------------------------------------------------------------
  // interrupt and DMAC request levels (serial 3.9, 3.8); not gated by TIE / RIE / EIO for the DMAC [HW serial 3.5]
  always_comb begin
    irq_eri = acc_ok && scr[6] && (orer || fer || per);
    irq_rxi = acc_ok && scr[6] && rdrf && !eio;
    irq_txi = acc_ok && scr[7] && (smif ? tend : tdre);
    irq_tei = acc_ok && scr[2] && tend && !smif;
    dreq_tx = acc_ok && (smif ? tend : tdre);                    // RS 1000 (smart card: TEND)
    dreq_rx = acc_ok && rdrf;                                     // RS 1001
  end
endmodule
