// v1_bsc_rfsh.sv -- the BSC refresh timer: RTCSR, RTCNT, RTCOR, RFCR, the refresh request, RCMI / ROVI
// (rtl/v1/spec/bsc-memmap.md 3.2, 4.5; hm 13.2.9-13.2.13, 13.3.5).  Owner: WI-BSC.  Bclk domain: everything moves at
// the end of a bce cycle.
//
//   * Prescaler: a free-running 12-bit Bcyc counter [P]; RTCNT counts when the CKS-selected tap wraps: CKIO/4 ... /4096,
//     CKS = 0 stops it.  DC: CKS 010, one count per 16 Bcyc = 32 Icyc [HW bsc-memmap 6.2: 33.4 incl. polling jitter].
//   * Compare: RTCNT and RTCOR are compared in every Bcyc while the clock is selected [P: not with CKS = 0].  The
//     Bcyc after the count reached RTCOR is the match: CMF <- 1, RFCR + 1, a refresh request (MCR.RFSH = 1,
//     RMODE = 0), and RTCNT <- 0 at its end.  So RTCOR is visible for exactly one Bcyc [HW 6.2: <= ~22 Icyc] and the
//     period is RTCOR ticks: 0x5E x 32 = 3008 Icyc [HW 6.2: 731 refreshes in 2,200,418 Icyc].  RTCOR = 0 matches in
//     every Bcyc [P].
//   * RFCR counts matches; when the count reaches the LMTS limit (1024 / 512) it clears and sets OVF [HW 6.2: 0x3FF
//     wraps to 0 with OVF set].
//   * The request is one flag: a match while one is pending replaces it (no queue); it clears when the refresh starts
//     (`take`, hm Figure 13.37).
//   * CMF / OVF: written 0 clears, written 1 keeps; a match in the same Bcyc as the clearing write wins [P].
//   * RCMI = CMF & CMIE, ROVI = OVF & OVIE: levels to the INTC (INTEVT 0x580 / 0x5A0).
//   * Resets (SOC.md 2.3): a power-on reset clears everything; a manual reset holds the registers and RTCNT keeps
//     counting, refresh continues.
module v1_bsc_rfsh import v1_soc_pkg::*; (
  input  logic        clk,
  /* verilator lint_off UNUSEDSIGNAL */          // rst.pin / man / cpu: only a power-on reset clears the timer
  input  rst_t        rst,
  input  clken_t      ce,
  /* verilator lint_on UNUSEDSIGNAL */
  input  logic        wr_rtcsr, wr_rtcnt, wr_rtcor, wr_rfcr,
  input  logic [9:0]  wr_rdat,
  output logic [15:0] rtcsr, rtcnt, rtcor, rfcr,
  input  logic        rfsh_en,                   // MCR.RFSH & !MCR.RMODE
  input  logic        take,                      // the refresh cycle starts this Bcyc
  output logic        req,
  output logic        rcmi, rovi
);
  logic [11:0] pre;
  logic [7:0]  cnt, cor;
  logic [9:0]  rfc;
  logic        cmf, cmie, ovf, ovie, lmts;
  logic [2:0]  cks;

  assign rtcsr = {8'd0, cmf, cmie, cks, ovf, ovie, lmts};
  assign rtcnt = {8'd0, cnt};
  assign rtcor = {8'd0, cor};
  assign rfcr  = {6'd0, rfc};
  assign rcmi  = cmf && cmie;
  assign rovi  = ovf && ovie;

  // the count tick: the selected prescaler tap wraps in this Bcyc
  logic tick;
  always_comb begin
    unique case (cks)
      3'd1:    tick = pre[1:0]  == '1;           // CKIO / 4
      3'd2:    tick = pre[3:0]  == '1;           // / 16 (the DC)
      3'd3:    tick = pre[5:0]  == '1;           // / 64
      3'd4:    tick = pre[7:0]  == '1;           // / 256
      3'd5:    tick = pre[9:0]  == '1;           // / 1024
      3'd6:    tick = pre[10:0] == '1;           // / 2048
      3'd7:    tick = pre == '1;                 // / 4096
      default: tick = 1'b0;                      // stopped
    endcase
  end
  wire match = cks != 3'd0 && cnt == cor;
  wire [10:0] rfc_n = {1'b0, rfc} + 11'd1;
  wire rfc_ovf = rfc_n >= (lmts ? 11'd512 : 11'd1024);

  always_ff @(posedge clk) begin
    if (rst.por) begin
      pre <= '0; cnt <= '0; cor <= '0; rfc <= '0;
      cmf <= 1'b0; cmie <= 1'b0; ovf <= 1'b0; ovie <= 1'b0; lmts <= 1'b0; cks <= '0;
      req <= 1'b0;
    end else if (ce.bce) begin
      pre <= pre + 12'd1;
      // the count
      if (wr_rtcnt)   cnt <= wr_rdat[7:0];
      else if (match) cnt <= '0;
      else if (tick)  cnt <= cnt + 8'd1;
      if (wr_rtcor) cor <= wr_rdat[7:0];
      // RFCR and OVF
      if (wr_rfcr) rfc <= wr_rdat;
      else if (match) rfc <= rfc_ovf ? 10'd0 : rfc_n[9:0];
      // RTCSR
      if (wr_rtcsr) begin
        cmie <= wr_rdat[6]; cks <= wr_rdat[5:3]; ovie <= wr_rdat[1]; lmts <= wr_rdat[0];
        if (!wr_rdat[7]) cmf <= 1'b0;
        if (!wr_rdat[2]) ovf <= 1'b0;
      end
      if (match) cmf <= 1'b1;
      if (match && !wr_rfcr && rfc_ovf) ovf <= 1'b1;
      // the refresh request flag
      if (take) req <= 1'b0;
      if (match && rfsh_en) req <= 1'b1;
      if (!rfsh_en) req <= 1'b0;
    end
  end
endmodule
