// v1_dmac_regs.sv -- the 17 DMAC registers, their CPU access rules, the flags and the class-A address check
// (rtl/v1/spec/dmac.md 2, 3.7, 3.8; SOC.md 2.3, 3.1.4-3.1.6).  Owner: WI-DMAC.
//
// Register slave (SOC.md 3.1.2 P4B_DMAC, a Bclk block, full decode of a[17:0]):
//   0x00 + 0x10n SARn   32 bits R/W                        0x08 + 0x10n DMATCRn  bits 23:0 (31:24 read 0)
//   0x04 + 0x10n DARn   32 bits R/W                        0x0C + 0x10n CHCRn    mask FF0FFFF5 / FF0AFFF5 + TE
//   0x40         DMAOR  DDT (15), PR (9:8), AE (2), NMIF (1), DME (0); everything else reads 0
//   0x44-0x3FFFF        holes: hit = 0 (the bridge reads 0, a write is dropped)       [spec 2.1: not probed, [P]]
// * Only 32-bit accesses work.  A byte / word read of any register returns 0 with hit = 1, a byte / word write is
//   ignored [HW dmac1 S2: SAR1, CHCR1 and DMAOR at every byte / halfword offset].
// * The flags TE (CHCR bit 1), AE and NMIF (DMAOR bits 2, 1): the CPU cannot set them [HW S3: DMAOR | 6 read 8201];
//   a write of 0 clears one only if a CPU read returned it as 1 before [HW S8 (a) / (b), S9 (iii)].  Mechanism: a
//   per-flag arm bit, set by a CPU read that returns the flag as 1, cleared by any CPU write to that register (spec
//   2.2; the disarm-on-write part is [P], consistent with S8 (e): "write cfg|TE" then "write 0" clears only because
//   the read in between re-armed it).
// * DDT mode (DMAOR.DDT = 1): a CPU write to SAR0 / DAR0 / DMATCR0 loads 0 [HW dmac1 S3, dmac2 T1]; a CPU write to
//   CHCR0 is dropped [HW S3].  The DTR loads channel 0 through the ld0 port.
// * Reset (SOC.md 2.3): power-on: everything 0 (SAR / DAR / DMATCR are undefined on the silicon; 0 [P]).  Manual:
//   CHCR / DMAOR / arm bits 0, SAR / DAR / DMATCR held (the engine may still finish the unit in flight).  CPU-only:
//   held.  Standby: every CHCR.TE cleared (hm 14.2.4 "in standby mode"), the rest held.
//
// Timing (all Bclk): the bridge strobes this Bclk slave in a bce cycle; rdata / hit are combinational in the strobe
// cycle and a write or a read side effect (arming) lands at the end of it, visible to the engine from the next Icyc.
// The register file has one write port per Bclk shared by the CPU and the engine: when the engine (a unit's counter
// update, a DTR load, a DDT halt, a flag being set) writes the register a CPU write targets in the same bce, the CPU
// strobe answers stall = 1 and lands one Bclk later, after the engine's value [P] (spec 7.2: "register updates from
// the sequencer take priority over a CPU write in the same Bcyc"; the stall keeps the CPU's value instead of losing
// it).  A strobe presented in a non-bce cycle (only after a stall) is stalled to the next bce.
//
// The class-A check (spec 3.8): while DE & DME, a channel whose used address register(s) are not aligned to the unit
// size sets AE at the next bce, before any request [HW S9 (iii): SAR + 2 longword -> AE, DMATCR 4 -> 4; T3: DAR + 4
// with 32 B; "RS=2 SAR=RAM+1" with no request; S9 (v): DE = 0 -> no AE].  In that bce no channel is enabled, so no
// unit can start with a misaligned address.
module v1_dmac_regs import v1_soc_pkg::*, v1_dmac_pkg::*; (
  input  logic              clk,
  /* verilator lint_off UNUSEDSIGNAL */          // rst.pin / rst.cpu: pin implies por; a CPU-only reset holds the DMAC
  input  rst_t              rst,
  /* verilator lint_on UNUSEDSIGNAL */
  input  logic              bce,               // ce.bce: the Bclk edge
  input  logic              run,               // dmac_run (0 with MSTP4, in deep sleep, in standby): the Bclk
  input  logic              stby,
  /* verilator lint_off UNUSEDSIGNAL */          // pq.blk: one block (P4B_DMAC); the decode is off[17:0]
  input  p4s_req_t          pq,
  /* verilator lint_on UNUSEDSIGNAL */
  output p4s_rsp_t          pr,
  // the engine's counter update, one bce pulse per unit (v1_dmac_xfer)
  input  logic              upd_v,
  input  logic [1:0]        upd_ch,
  input  logic              upd_sar_we, upd_dar_we,
  input  logic [31:0]       upd_sar, upd_dar,
  input  logic [23:0]       upd_tcr,
  input  logic              upd_te,            // DMATCR reached 0: TE <- 1
  input  logic              upd_ae,            // class-B address error at this unit's end: AE <- 1
  // the DDT module's channel-0 load (an initialisation DTR) and halt (a transfer-end DTR), bce pulses
  input  logic              ld0_v,
  input  logic [31:0]       ld0_chcr,
  input  logic [23:0]       ld0_tcr,
  input  logic              ld0_sar_we, ld0_dar_we,
  input  logic [31:0]       ld0_addr,
  input  logic              halt0,             // CHCR0.DE <- 0
  input  logic              nmi_evt,           // NMI accepted by the INTC: NMIF <- 1 (any Icyc)
  output logic [3:0][31:0]  sar, dar, chcr,
  output logic [3:0][23:0]  tcr,
  output logic              o_ddt, o_dme, o_ae, o_nmif,
  output logic              o_halt_now,        // AE | NMIF, or one of them being set in this bce (the DDT drop)
  output logic [1:0]        o_pr,
  output logic [3:0]        en,                // channel n may start a unit in this bce
  output logic [31:0]       ib_data,           // the last CPU write data (what a class-B leg transfers, spec 3.8)
  output logic [3:0]        chcr_wr            // a CPU write to CHCRn lands in this cycle
);
  // ---- state
  logic [3:0][31:0] sar_q, dar_q, chcr_q;
  logic [3:0][23:0] tcr_q;
  logic             ddt_q, dme_q, ae_q, nmif_q;
  logic [1:0]       pr_q;
  logic [3:0]       te_arm;
  logic             ae_arm, nmif_arm;
  logic [31:0]      ib_q;
  logic             nmi_pend;                  // an nmi_evt seen in a non-bce Icyc, applied at the next bce

  assign sar = sar_q;  assign dar = dar_q;  assign chcr = chcr_q;  assign tcr = tcr_q;
  assign o_ddt = ddt_q;  assign o_dme = dme_q;  assign o_ae = ae_q;  assign o_nmif = nmif_q;  assign o_pr = pr_q;
  assign ib_data = ib_q;

  // ---- class A and the channel enables
  logic [3:0] misal, code_ok;
  logic       nmi_now;
  always_comb begin
    for (int n = 0; n < 4; n++) begin
      logic [3:0] rs;
      logic [2:0] ts;
      logic [4:0] m;
      rs = c_rs(chcr_q[n]);
      ts = c_ts(chcr_q[n]);
      m  = ts_amask(ts);
`ifdef V1_DMAC_BUG_3                             // planted bug: class A checks single-address channels only
      m = rs_uses_sar(rs) && !rs_uses_dar(rs) ? m : 5'd0;
`endif
      misal[n] = c_de(chcr_q[n]) && dme_q && ts_ok(ts) &&
                 ((rs_uses_sar(rs) && (sar_q[n][4:0] & m) != 5'd0) || (rs_uses_dar(rs) && (dar_q[n][4:0] & m) != 5'd0));
      // prohibited codes are stored but never start a channel [P] (spec 2.5; the silicon's behaviour is unmeasured)
      code_ok[n] = !rs_prohib(rs) && ts_ok(ts) && (!rs_uses_sar(rs) || c_sm(chcr_q[n]) != 2'b11) &&
                   (!rs_uses_dar(rs) || c_dm(chcr_q[n]) != 2'b11);
    end
  end
  assign nmi_now = nmi_evt || nmi_pend;
  always_comb
    for (int n = 0; n < 4; n++)
      en[n] = c_de(chcr_q[n]) && dme_q && !c_te(chcr_q[n]) && !ae_q && !nmif_q && code_ok[n] && !stby && run &&
              misal == 4'd0 && !nmi_now;

  // flag set events of this bce (they are engine writes to DMAOR / CHCR for the stall rule)
  logic ae_set, nmif_set;
  // (class A needs the Bclk; a class-B unit that completes its handshakes during a module stop still sets AE)
  assign ae_set   = bce && !ae_q && ((run && misal != 4'd0) || (upd_v && upd_ae));
  assign nmif_set = bce && !nmif_q && nmi_now;
  assign o_halt_now = ae_q || nmif_q || ae_set || nmif_set;

  // ---- the CPU port
  logic        strobe, hitreg, cpu, sz32;
  logic [4:0]  idx;
  logic [1:0]  rch;                            // channel of a channel register (idx[3:2])
  logic        is_or;                          // DMAOR
  logic        collide, acc, wr_ok, rd_ok;
  logic [31:0] rval;
  assign strobe = pq.rd || pq.wr;
  assign idx    = pq.off[6:2];
  assign hitreg = pq.off[17:7] == 11'd0 && idx <= 5'd16;
  assign cpu    = !pq.dma;
  assign sz32   = pq.size == 2'd2;
  assign rch    = idx[3:2];
  assign is_or  = idx == 5'd16;

  always_comb begin
    unique case (idx[1:0])
      2'd0:    rval = sar_q[rch];
      2'd1:    rval = dar_q[rch];
      2'd2:    rval = {8'h00, tcr_q[rch]};
      default: rval = chcr_q[rch];
    endcase
    if (is_or) rval = {16'h0000, ddt_q, 5'b00000, pr_q, 5'b00000, ae_q, nmif_q, dme_q};
  end

  // the engine writes this register in this bce
  always_comb begin
    collide = 1'b0;
    if (is_or)                              collide = ae_set || nmif_set;
    else begin
      if (upd_v && upd_ch == rch)           collide = 1'b1;
      if (rch == 2'd0 && (ld0_v || halt0))  collide = 1'b1;
    end
  end

  always_comb begin
    pr = '0;
    if (strobe) begin
      pr.hit   = hitreg;
      pr.stall = !bce || (pq.wr && hitreg && collide);
      if (pq.rd && hitreg && sz32 && cpu) pr.rdata = rval;       // wrong size or a DMAC-mastered access: 0 [P]
    end
  end
  assign acc   = strobe && !pr.stall && hitreg && sz32 && cpu && run;   // run = 0 (MSTP4): reads return the held
  assign wr_ok = acc && pq.wr;                                          // values, writes are dropped [P]
  assign rd_ok = acc && pq.rd;

  always_comb begin
    chcr_wr = 4'd0;
    if (wr_ok && !is_or && idx[1:0] == 2'd3 && !(ddt_q && rch == 2'd0)) chcr_wr[rch] = 1'b1;
  end

  // ---- the registers
  always_ff @(posedge clk) begin
    if (rst.por) begin
      sar_q <= '0; dar_q <= '0; tcr_q <= '0; chcr_q <= '0;
      ddt_q <= 1'b0; dme_q <= 1'b0; ae_q <= 1'b0; nmif_q <= 1'b0; pr_q <= 2'd0;
      te_arm <= 4'd0; ae_arm <= 1'b0; nmif_arm <= 1'b0; ib_q <= 32'd0; nmi_pend <= 1'b0;
    end else begin
      // nmi_evt may come in any Icyc; NMIF is a Bclk flop
      if (bce) nmi_pend <= 1'b0;
      else if (nmi_evt) nmi_pend <= 1'b1;

      // CPU read side effect: arm the flags read as 1 (at the end of the capture cycle, SOC.md 3.1.6)
      if (rd_ok) begin
        if (is_or) begin
          if (ae_q)   ae_arm   <= 1'b1;
          if (nmif_q) nmif_arm <= 1'b1;
        end else if (idx[1:0] == 2'd3 && c_te(chcr_q[rch])) te_arm[rch] <= 1'b1;
      end

      // CPU write (never the same register as an engine write of this bce: that one stalled)
      if (wr_ok) begin
        ib_q <= pq.wdata;
        if (is_or) begin
          ddt_q <= pq.wdata[15];
          pr_q  <= pq.wdata[9:8];
          dme_q <= pq.wdata[0];
          if (ae_arm && !pq.wdata[2])   ae_q   <= 1'b0;
          if (nmif_arm && !pq.wdata[1]) nmif_q <= 1'b0;
          ae_arm <= 1'b0; nmif_arm <= 1'b0;
        end else begin
          unique case (idx[1:0])
`ifndef V1_DMAC_BUG_2
            2'd0: sar_q[rch] <= (ddt_q && rch == 2'd0) ? 32'd0 : pq.wdata;
`else                                            // planted bug: SAR0 takes the CPU value in DDT mode
            2'd0: sar_q[rch] <= pq.wdata;
`endif
            2'd1: dar_q[rch] <= (ddt_q && rch == 2'd0) ? 32'd0 : pq.wdata;
            2'd2: tcr_q[rch] <= (ddt_q && rch == 2'd0) ? 24'd0 : pq.wdata[23:0];
            default: if (!(ddt_q && rch == 2'd0)) begin
              chcr_q[rch] <= (pq.wdata & (rch[1] ? CHCR_WM23 : CHCR_WM01)) |
`ifndef V1_DMAC_BUG_1
                             {30'd0, c_te(chcr_q[rch]) && !(te_arm[rch] && !pq.wdata[1]), 1'b0};
`else                                            // planted bug (make v1-dmac-bugs): no arming for TE
                             {30'd0, c_te(chcr_q[rch]) && pq.wdata[1], 1'b0};
`endif
              te_arm[rch] <= 1'b0;
            end
          endcase
        end
      end

      // engine: one unit's counter update (UPD)
      if (upd_v) begin
        if (upd_sar_we) sar_q[upd_ch] <= upd_sar;
        if (upd_dar_we) dar_q[upd_ch] <= upd_dar;
        tcr_q[upd_ch] <= upd_tcr;
        if (upd_te) chcr_q[upd_ch][1] <= 1'b1;
      end
      // DDT: channel-0 initialisation and halt.  (The initialisation never meets a channel-0 update: v1_dmac_ddt
      // refuses it while a channel-0 unit is between its commit and its update Bclk, inclusive.)
      if (ld0_v) begin
        chcr_q[0] <= ld0_chcr;
        tcr_q[0]  <= ld0_tcr;
        if (ld0_sar_we) sar_q[0] <= ld0_addr;
        if (ld0_dar_we) dar_q[0] <= ld0_addr;
        te_arm[0] <= 1'b0;
      end
      if (halt0) chcr_q[0][0] <= 1'b0;
      if (ae_set)   ae_q   <= 1'b1;
      if (nmif_set) nmif_q <= 1'b1;

      // standby clears every TE (a level: the Bclk is stopped then)
`ifndef V1_DMAC_BUG_10
      if (stby) begin
`else                                            // planted bug: standby keeps TE
      if (1'b0) begin
`endif
        for (int n = 0; n < 4; n++) chcr_q[n][1] <= 1'b0;
        te_arm <= 4'd0;
      end
      if (rst.man) begin                         // manual reset: CHCR, DMAOR and the arm bits; SAR / DAR / DMATCR held
        chcr_q <= '0;
        ddt_q <= 1'b0; dme_q <= 1'b0; ae_q <= 1'b0; nmif_q <= 1'b0; pr_q <= 2'd0;
        te_arm <= 4'd0; ae_arm <= 1'b0; nmif_arm <= 1'b0; nmi_pend <= 1'b0;
      end
    end
  end
endmodule
