// v1_ccr.sv -- the CCN cache-control registers CCR, QACR0 and QACR1 (0xFF00001C / 38 / 3C, area 7 0x1F0000xx) and the
// ICI / OCI invalidate-all pulses.  Owner: WI-CACHE (spec caches-sq 2.1, 2.2, 3.7, 7.5; rtl/v1/SOC.md 2.3, 3.1.3, 3.8).
//
// Tags: [HW] measured on the console, [M] hardware manual, [P] a choice this file makes.
//
// CCR (caches-sq 2.1).  Stored bits: IIX 15, ICE 8, OIX 7, ORA 5, CB 2, WT 1, OCE 0 (mask 0x81A7).  ICI (11) and OCI (3)
// are write-1 actions and read 0; the reserved bits read 0 [HW: 0x090D written reads back 0x0105].  The fields go to
// the caches as `ccr` (ccr_t) and to v1_mmu (ORA for the fetch poison and the region decode, CB / WT for the
// untranslated write policy).  A write lands at the end of its (MA3) strobe cycle, so every access issued after it
// sees the new value (caches-sq 7.5; the manual's 4 / 8 instruction rule is a software restriction).
//
// ICI / OCI (caches-sq 2.1, 3.7): a CCR write with bit 11 / bit 3 set pulses `ici` / `oci` for one cycle -- the write's
// own capture cycle -- and the caches clear every IC V bit / every OC V and U bit at the end of that cycle, the same
// edge that writes CCR: a flash clear with no visible stall [HW: a CCR write from P2 costs 6 Icyc with or without
// OCI / ICI].  Tags and data are kept [HW].  No write-back (OCI discards dirty lines [M]).
//
// QACR0 / QACR1 (caches-sq 2.2): only AREA [4:2] exists [HW: 0xFFFFFFFF written reads 0x0000001C]; `qacr0` / `qacr1`
// give v1_sq the external address bits [28:26] of an SQ0 / SQ1 burst with MMUCR.AT = 0 [HW: SQ1 uses QACR1].
//
// Register slave (Iclk class, P4B_CCN_CCR, SOC.md 3.1.2-3.1.6): the CCN window decodes a[7:0] (a[17:8] ignored, the
// 0xFF001000 mirror [HW]), so this port sees offsets 0x1C / 0x38 / 0x3C of any 256-byte repeat.  rdata / hit are
// combinational in the strobe cycle, it never stalls, no read side effects.  32-bit registers: an access of another
// size reads 0 with hit = 1 and ignores the write (SOC.md 3.1.4; caches-sq 2.1 / 2.2 "longword only").  Timing: none of
// its own -- a CCR read's 3 Icyc are the bridge's.
//
// Resets (SOC.md 2.3): CCR = 0 on power-on and manual reset [M, HW]; QACR undefined in hardware, 0 here [P]; a CPU-only
// reset (BL = 1) holds all three.
module v1_ccr import v1_soc_pkg::*; (
  input  logic       clk,
  /* verilator lint_off UNUSEDSIGNAL */          // rst.pin / rst.cpu: pin implies por; CPU-only holds CCR / QACR
  input  rst_t       rst,
  input  p4s_req_t   pq,                         // blk / dma / off[27:8]: one block, a[7:0] decode
  /* verilator lint_on UNUSEDSIGNAL */
  output p4s_rsp_t   pr,
  output ccr_t       ccr,
  output logic       ici, oci,
  output logic [2:0] qacr0, qacr1
);
  localparam logic [31:0] CCR_MASK = 32'h0000_81A7;   // IIX ICE OIX ORA CB WT OCE [HW 0x090D -> 0x0105]

  logic [31:0] ccr_r;                            // the stored bits (CCR_MASK)
  logic        acc, sz32, sel_ccr, sel_q0, sel_q1, wr32;

  always_comb begin
    acc     = pq.rd || pq.wr;
    sz32    = pq.size == 2'd2;
    sel_ccr = pq.off[7:0] == 8'h1C;
    sel_q0  = pq.off[7:0] == 8'h38;
    sel_q1  = pq.off[7:0] == 8'h3C;
    wr32    = pq.wr && sz32;
    pr      = '0;                                // not strobed: rdata 0, hit 0, stall 0
    pr.hit  = acc && (sel_ccr || sel_q0 || sel_q1);
    if (pq.rd && sz32) begin
      if (sel_ccr) pr.rdata = ccr_r;
      if (sel_q0)  pr.rdata = {27'd0, qacr0, 2'b00};
      if (sel_q1)  pr.rdata = {27'd0, qacr1, 2'b00};
    end
    // the invalidate-all pulses: the CCR write's capture cycle (this slave never stalls, so a strobe is a capture)
    ici = wr32 && sel_ccr && pq.wdata[11];
    oci = wr32 && sel_ccr && pq.wdata[3];
    ccr = '{iix: ccr_r[15], ice: ccr_r[8], oix: ccr_r[7], ora: ccr_r[5], cb: ccr_r[2], wt: ccr_r[1], oce: ccr_r[0]};
  end

  always_ff @(posedge clk) begin
    if (rst.por || rst.man) begin
      ccr_r <= '0;
      qacr0 <= '0;                               // [P] undefined in hardware
      qacr1 <= '0;
    end else begin
      if (wr32 && sel_ccr) ccr_r <= pq.wdata & CCR_MASK;
      if (wr32 && sel_q0)  qacr0 <= pq.wdata[4:2];
      if (wr32 && sel_q1)  qacr1 <= pq.wdata[4:2];
    end
  end
endmodule
