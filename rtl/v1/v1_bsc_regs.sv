// v1_bsc_regs.sv -- the BSC register block on the P4 bus (rtl/v1/spec/bsc-memmap.md 3; SOC.md 3.1, 3.8).  Owner:
// WI-BSC.  It decodes both slave ports of v1_bsc (PP_BSC: 0xFF800000-, PP_SDMR: 0xFF900000- / 0xFF940000-), holds the
// configuration registers BCR1 BCR2 WCR1 WCR2 WCR3 MCR PCR and the MD strap latches, and forwards the refresh-timer
// and port writes to v1_bsc_rfsh / v1_bsc_port, which own those registers.
//
//   * Size rule [HW bsc-memmap 6.2]: a register answers only an access of its own size at its own address; any other
//     size (or a sub-address of a 32-bit register) reads 0 with hit = 1 and ignores the write.  Offsets that are not a
//     register (0x34 0x38 0x3C 0x4C, 0x50 and up, anything with off[17:7] != 0) are holes: hit = 0, read 0 [HW].
//   * Write keys [HW 6.2]: RTCSR / RTCNT / RTCOR need 0xA5 in bits 15:8, RFCR 101001 in bits 15:10 (so 0xA500 writes
//     0x100); byte writes and other keys are ignored.  The keyed writes are word writes by the size rule anyway.
//   * Reserved bits read 0 (the masks below); BCR1[31:29] = {MD5, MD7, MD6} and BCR2[15:14] = {MD4, MD3} are the
//     straps latched while the RESET pin is low (SOC.md 2.2), read only (hm 13.2.1-13.2.2).
//   * Resets (SOC.md 2.3): a power-on reset loads the POR values; a manual reset holds everything.
//   * Timing: a Bclk-class slave.  It never stalls; rdata / hit are combinational in the strobe cycle and a write lands
//     at the end of it (v1_p4bus makes the measured 8-read / 6-write costs [HW bsc-memmap 6.4]).
//   * SDMR2 / SDMR3: write only, address encoded, in 0xFF900000-0xFF90FFFF / 0xFF940000-0xFF94FFFF (bsc-memmap 2.3,
//     3.1; the rest of each 256 KB P4 block is a hole: hit = 0, no command, SOC.md 3.1.2).  A write of any size [P]
//     (the manual says byte) requests one SDRAM mode command (PALL / MRS by MCR.MRSET) with X = off[12:0]; v1_bsc
//     issues it after the current bus cycle.  A read returns 0 with hit = 1 and has no effect (SOC.md 9.18).
//   * MD straps (SOC.md 2.2, 3.6): md[k] is the value the strap loads into its register bit, not the inverted pin
//     level of hm Appendix C / 13.3.8 (hm 13.2.1's bit tables): BCR1.ENDIAN / MASTER / A0MPX = MD5 / MD7 / MD6,
//     BCR2.A0SZ = MD4:MD3.  The DC is md = 0x065 (tb/dc_board), which gives the measured BCR1[31:29] = 101.
module v1_bsc_regs import v1_soc_pkg::*, v1_bsc_pkg::*; (
  input  logic        clk,
  /* verilator lint_off UNUSEDSIGNAL */          // rst.man / rst.cpu: every BSC register holds on those (SOC.md 2.3)
  input  rst_t        rst,
  /* verilator lint_on UNUSEDSIGNAL */
  /* verilator lint_off UNUSEDSIGNAL */          // md[8], md[2:0] are other units' straps; pq.blk / dma: one block;
  input  logic [8:0]  md,                       // pq_sdmr: off[15:0] carries the value, the data and size are ignored
  input  p4s_req_t    pq,
  output p4s_rsp_t    pr,
  input  p4s_req_t    pq_sdmr,
  /* verilator lint_on UNUSEDSIGNAL */
  output p4s_rsp_t    pr_sdmr,
  output bregs_t      regs,
  // the refresh timer's registers (v1_bsc_rfsh)
  input  logic [15:0] rtcsr, rtcnt, rtcor, rfcr,
  output logic        wr_rtcsr, wr_rtcnt, wr_rtcor, wr_rfcr,
  output logic [9:0]  wr_rdat,                   // the value field of a keyed write
  // the port registers (v1_bsc_port)
  input  logic [31:0] pctra, pctrb,
  input  logic [15:0] pdtra, pdtrb, gpioic,
  output logic        wr_pctra, wr_pdtra, wr_pctrb, wr_pdtrb, wr_gpioic,
  output logic [31:0] wr_pdat,
  // an SDRAM mode command (one-cycle pulse)
  output logic        sdmr_v,
  output logic        sdmr_a3,                   // 1 area 3 (SDMR3), 0 area 2 (SDMR2)
  output logic [12:0] sdmr_x
);
  localparam logic [31:0] BCR1_WM = 32'h033E_FFFD;   // writable: 25 24 21-17 15-2 0 (31-29 RO straps, 28-26 23-22 16 1 rsv)
  localparam logic [15:0] BCR2_WM = 16'h3FFD;        // 13-2, 0 (15-14 RO straps, 1 reserved)
  localparam logic [31:0] WCR1_WM = 32'h7777_7777;
  localparam logic [31:0] WCR2_WM = 32'hFFFE_EFFF;   // 16, 12 reserved
  localparam logic [31:0] WCR3_WM = 32'h0777_7777;   // 31-27 23 19 15 11 7 3 reserved
  localparam logic [31:0] MCR_WM  = 32'hF8BB_FFFF;   // 26-24 22 18 reserved

  logic [4:0]  mdl;                              // {MD5, MD7, MD6, MD4, MD3}
  logic [28:0] bcr1;                             // [31:29] are the straps (mdl)
  logic [31:0] wcr1, wcr2, wcr3, mcr;
  logic [13:0] bcr2;                             // [15:14] are the straps
  logic [15:0] pcr;

  always_comb begin                              // (field by field: a pattern key that is also an identifier here is read as an expression by Quartus 17.0)
    regs = '0;
    regs.bcr1 = {mdl[4:2], bcr1}; regs.bcr2 = {mdl[1:0], bcr2}; regs.wcr1 = wcr1; regs.wcr2 = wcr2; regs.wcr3 = wcr3;
    regs.mcr = mcr; regs.pcr = pcr;
  end

  // ---- decode: register index by off[6:2], its size, and whether this access is its own size at its own address
  typedef enum logic [4:0] {
    R_NONE, R_BCR1, R_BCR2, R_WCR1, R_WCR2, R_WCR3, R_MCR, R_PCR, R_RTCSR, R_RTCNT, R_RTCOR, R_RFCR, R_PCTRA, R_PDTRA,
    R_PCTRB, R_PDTRB, R_GPIOIC
  } reg_e;
  reg_e        sel;
  logic [1:0]  rsz;                              // the register's size (msz_e)
  always_comb begin
    sel = R_NONE; rsz = 2'd2;
    if (pq.off[17:7] == 11'd0) begin
      unique case (pq.off[6:2])
        5'h00: begin sel = R_BCR1;   rsz = 2'd2; end
        5'h01: begin sel = R_BCR2;   rsz = 2'd1; end
        5'h02: begin sel = R_WCR1;   rsz = 2'd2; end
        5'h03: begin sel = R_WCR2;   rsz = 2'd2; end
        5'h04: begin sel = R_WCR3;   rsz = 2'd2; end
        5'h05: begin sel = R_MCR;    rsz = 2'd2; end
        5'h06: begin sel = R_PCR;    rsz = 2'd1; end
        5'h07: begin sel = R_RTCSR;  rsz = 2'd1; end
        5'h08: begin sel = R_RTCNT;  rsz = 2'd1; end
        5'h09: begin sel = R_RTCOR;  rsz = 2'd1; end
        5'h0A: begin sel = R_RFCR;   rsz = 2'd1; end
        5'h0B: begin sel = R_PCTRA;  rsz = 2'd2; end
        5'h0C: begin sel = R_PDTRA;  rsz = 2'd1; end
        5'h10: begin sel = R_PCTRB;  rsz = 2'd2; end
        5'h11: begin sel = R_PDTRB;  rsz = 2'd1; end
        5'h12: begin sel = R_GPIOIC; rsz = 2'd1; end
        default: sel = R_NONE;                   // 0x34 0x38 0x3C 0x4C and up: holes [HW]
      endcase
    end
  end
  wire strobe = pq.rd || pq.wr;
  wire own    = sel != R_NONE && pq.size == rsz && pq.off[1:0] == 2'b00;   // the size rule [HW]
  wire wr_own = pq.wr && own;
  wire [15:0] wd16 = pq.wdata[15:0];
  wire key_a5 = wd16[15:8] == 8'hA5;
  wire key_rf = wd16[15:10] == 6'b101001;

  // ---- read
  always_comb begin
    pr = '0;
    if (strobe && sel != R_NONE) begin
      pr.hit = 1'b1;
      if (own && pq.rd) begin
        unique case (sel)
          R_BCR1:   pr.rdata = regs.bcr1;
          R_BCR2:   pr.rdata = {16'd0, regs.bcr2};
          R_WCR1:   pr.rdata = wcr1;
          R_WCR2:   pr.rdata = wcr2;
          R_WCR3:   pr.rdata = wcr3;
          R_MCR:    pr.rdata = mcr;
          R_PCR:    pr.rdata = {16'd0, pcr};
          R_RTCSR:  pr.rdata = {16'd0, rtcsr};
          R_RTCNT:  pr.rdata = {16'd0, rtcnt};
          R_RTCOR:  pr.rdata = {16'd0, rtcor};
          R_RFCR:   pr.rdata = {16'd0, rfcr};
          R_PCTRA:  pr.rdata = pctra;
          R_PDTRA:  pr.rdata = {16'd0, pdtra};
          R_PCTRB:  pr.rdata = pctrb;
          R_PDTRB:  pr.rdata = {16'd0, pdtrb};
          R_GPIOIC: pr.rdata = {16'd0, gpioic};
          default:  pr.rdata = '0;
        endcase
      end
    end
  end

  // ---- forwarded writes (keyed ones only with their key)
  assign wr_rtcsr  = wr_own && sel == R_RTCSR && key_a5;
  assign wr_rtcnt  = wr_own && sel == R_RTCNT && key_a5;
  assign wr_rtcor  = wr_own && sel == R_RTCOR && key_a5;
  assign wr_rfcr   = wr_own && sel == R_RFCR  && key_rf;
  assign wr_rdat   = sel == R_RFCR ? wd16[9:0] : {2'b00, wd16[7:0]};
  assign wr_pctra  = wr_own && sel == R_PCTRA;
  assign wr_pdtra  = wr_own && sel == R_PDTRA;
  assign wr_pctrb  = wr_own && sel == R_PCTRB;
  assign wr_pdtrb  = wr_own && sel == R_PDTRB;
  assign wr_gpioic = wr_own && sel == R_GPIOIC;
  assign wr_pdat   = pq.wdata;

  // ---- the configuration registers
  always_ff @(posedge clk) begin
    if (rst.pin) mdl <= {md[5], md[7], md[6], md[4], md[3]};
    if (rst.por) begin
      bcr1 <= '0;
      bcr2 <= 14'h3FFC;
      wcr1 <= 32'h7777_7777;
      wcr2 <= 32'hFFFE_EFFF;
      wcr3 <= 32'h0777_7777;
      mcr  <= '0;
      pcr  <= '0;
    end else if (wr_own) begin
      unique case (sel)
        R_BCR1:  bcr1 <= pq.wdata[28:0] & BCR1_WM[28:0];
        R_BCR2:  bcr2 <= wd16[13:0] & BCR2_WM[13:0];
        R_WCR1:  wcr1 <= pq.wdata & WCR1_WM;
        R_WCR2:  wcr2 <= pq.wdata & WCR2_WM;
        R_WCR3:  wcr3 <= pq.wdata & WCR3_WM;
        R_MCR:   mcr  <= pq.wdata & MCR_WM;
        R_PCR:   pcr  <= wd16;
        default: ;
      endcase
    end
  end

  // ---- SDMR2 / SDMR3: the 64 KB windows 0xFF90xxxx / 0xFF94xxxx
  wire sdmr_in = pq_sdmr.off[17:16] == 2'b00;
  always_comb begin
    pr_sdmr = '0;
    if ((pq_sdmr.rd || pq_sdmr.wr) && sdmr_in) pr_sdmr.hit = 1'b1;
  end
  assign sdmr_v  = pq_sdmr.wr && sdmr_in;
  assign sdmr_a3 = pq_sdmr.blk == P4B_SDMR3;
  assign sdmr_x  = pq_sdmr.off[12:0];
endmodule
