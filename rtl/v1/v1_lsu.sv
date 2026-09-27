// v1_lsu.sv -- the MA-stage router of the v1 SoC (rtl/v1/SOC.md 3.2.2, 4.2; owner WI-P4).
//
// The core presents one access micro-op in MA on `lreq` (the MA port contract, SOC.md 3.2.2: presented once, never
// withdrawn after its first cycle, identical until its ack).  The LSU routes it by the region the MMU decided in EX
// (lreq.x.rg) and returns the chosen unit's answer on `lrsp`; it adds no cycle of its own:
//
//   RG_MEM, RG_A7, RG_OCRAM          -> v1_ocache (ocq): every kind, the request forwarded unchanged
//   RG_SQ                            -> v1_sq (sqq): every kind, forwarded unchanged (the SQ column of the 4.2 table
//                                       is the SQ's: loads ack with 0, stores write the queue, PREF bursts ...)
//   RG_ARR, RG_P4REG, RG_P4RSV       -> the P4 bridge (p4q, master 0):
//       LD, RMW_RD, TAS_RD                   a read transaction
//       ST, RMW_WR, TAS_WR, MOVCA            a write transaction
//       PREF, OCBI, OCBP, OCBWB, TAS_PURGE   ack in the first MA cycle, no transaction, cost 0 [P]: so a PREF or an
//                                            OCBP to SCFRDR2 (0xFFE80014) never strobes the SCIF
//       size Q                               two longword transactions, a (FR(2n), wdata[31:0]) then a + 4 [P]
//     The bridge address is the P4 address, or FF000000 | pa[23:0] for a UTLB-translated physical 0x1F access
//     (amap_p4addr; dxlat.pa[23:0] is the P4 offset for RG_P4REG).  Every cost of a bridge access is the bridge's
//     (v1_p4bus: 3 Iclk, 4 array, 8 Bclk read, 16 / 20 Pclk read, 3 posted write): the LSU only waits for p4r.ack.
//
// `last_rdata`: the data of the most recent operand read returned to the core, any route, [31:0] of the right-aligned
// load data (debug-ubc-hudi 2.2.1, [P]); 0 after a power-on reset, held by the other resets.  The bridge returns it
// for reads of the undriven UBC window.
//
// Commit records (SOC.md 6.3), one per access at its ack:
//   MEMW   ST, RMW_WR, TAS_WR, MOVCA on any route: addr = VA, data = wdata, be, idx[2:0] = region
//   VOLRD  a read the reference model cannot compute: every Bclk- or Pclk-class P4 read, every PMCTR read, every
//          MMUCR read (CCN_MMU at a[7:0] = 0x10, any CCN mirror, and 0x1F000010 through the UTLB: its LRUI follows
//          the RTL's speculative ITLB hits, I057), every UBC_NODRV read, and every uncached RG_MEM read outside area-3
//          RAM and outside the boot ROM (physical 0x00000000-0x001FFFFF; flash reads, 0x00200000-0x0021FFFF, are
//          volatile: a flash being programmed answers status words that depend on time, I101); addr = VA, data = the
//          load data, idx = size
// PMC events (debug-ubc-hudi 4.7; the others of SOC.md 3.7's LSU + OC row are v1_ocache's):
//   0x09  one per operand access at its ack, any route: loads, stores, MOVCA, RMW, TAS read / write, and PREF
//         ([HW] debug-ubc-hudi pmcsurvey Q 09: 4 SQ stores count 4 per iteration, 8 SQ stores + PREF count 9; the
//         PREF of the OC and bridge routes is counted the same way [P]).  The cache-block ops OCBI / OCBP / OCBWB and
//         the TAS purge are not counted [P: unmeasured]
//   0x0D  one per data-moving access to a P4 control register (RG_P4REG; arrays and RG_P4RSV are not I/O [HW App.
//         A.5]) at its ack
//   0x25  every cycle an access waits: lreq.v & !lrsp.ack, i.e. every lrsp.frz and every mah cycle (CCN read 2,
//         TMU read 17, uncached SDRAM read 15 [HW])
module v1_lsu import v1_soc_pkg::*; (
  input  logic        clk,
  input  rst_t        rst,
  input  lreq_t       lreq,
  output lrsp_t       lrsp,
  output oc_req_t     ocq,
  input  oc_rsp_t     ocr,
  output sq_req_t     sqq,
  input  sq_rsp_t     sqr,
  output p4m_req_t    p4q,
  /* verilator lint_off UNUSEDSIGNAL */
  input  p4m_rsp_t    p4r,             // err is the DMAC's (master 1): always 0 for the CPU
  /* verilator lint_on UNUSEDSIGNAL */
  output logic [31:0] last_rdata,
  output commit_rec_t [CT_LSU_N-1:0] tap,
  output pmc_ev_t     pmc_ev
);
  typedef enum logic [1:0] { RT_OC = 2'd0, RT_SQ = 2'd1, RT_BR = 2'd2 } route_e;

  route_e      rt;
  logic        rd_k, wr_k, xfer, quad, vol;
  logic [31:0] base;
  p4blk_e      blk;
  logic        qh;                     // the second longword of a quadword bridge access is in progress
  logic [31:0] qlo;                    // the first longword's read data

  /* verilator lint_off UNUSEDSIGNAL */
  wire unused_rst = rst.pin ^ rst.man; // pin implies por; manual keeps last_rdata like CPU-only
  /* verilator lint_on UNUSEDSIGNAL */

  always_comb begin
    unique case (lreq.x.rg)
      RG_SQ:                      rt = RT_SQ;
      RG_ARR, RG_P4REG, RG_P4RSV: rt = RT_BR;
      default:                    rt = RT_OC;       // RG_MEM, RG_A7, RG_OCRAM
    endcase
    rd_k = dk_read(lreq.kind);
    wr_k = lreq.kind == DK_ST || lreq.kind == DK_RMW_WR || lreq.kind == DK_TAS_WR || lreq.kind == DK_MOVCA;
    xfer = rd_k || wr_k;
    quad = lreq.size == 2'(MSZ_Q);
    base = lreq.x.rg == RG_P4REG ? {8'hFF, lreq.x.pa[23:0]} : lreq.va;
    blk  = p4_decode(base, 1'b0);

    ocq   = lreq;
    ocq.v = lreq.v && rt == RT_OC;
    sqq   = lreq;
    sqq.v = lreq.v && rt == RT_SQ;
    p4q   = '{v: lreq.v && rt == RT_BR && xfer, we: wr_k, size: quad ? 2'(MSZ_L) : lreq.size,
              addr: base + (qh ? 32'd4 : 32'd0), wdata: qh ? lreq.wdata[63:32] : lreq.wdata[31:0], dma: 1'b0};

    unique case (rt)
      RT_OC: lrsp = ocr;
      RT_SQ: lrsp = sqr;
      default: begin
        lrsp.frz = 1'b0;
        if (xfer) begin
          lrsp.ack   = p4r.ack && (!quad || qh);
          lrsp.rdata = quad ? {p4r.rdata, qlo} : {32'd0, p4r.rdata};
        end else begin
          lrsp.ack   = 1'b1;                          // PREF / cache op / TAS purge on P4: no transaction
          lrsp.rdata = 64'd0;
        end
      end
    endcase
    if (!lreq.v) lrsp = '0;                           // the contract: ack / frz only while lreq.v

    // VOLRD (SOC.md 6.3): Bclk / Pclk class, PMCTR, MMUCR, UBC_NODRV; uncached board reads but area-3 RAM and the
    // boot ROM
    vol = (lreq.x.rg == RG_P4REG &&
           (p4_class(blk) != P4K_ICLK || blk == P4B_PMCTR || blk == P4B_UBC_NODRV ||
            (blk == P4B_CCN_MMU && base[7:0] == 8'h10))) ||
          (lreq.x.rg == RG_MEM && !lreq.x.c && lreq.x.pa[28:26] != 3'd3 && lreq.x.pa >= 29'h0200000);

    tap = '0;
    if (lreq.v && lrsp.ack) begin
      if (wr_k)
        tap[0] = '{v: 1'b1, kind: CK_MEMW, isn: lreq.isn, idx: {5'd0, lreq.x.rg}, addr: lreq.va,
                   data: lreq.wdata, be: lreq.be};
      else if (rd_k && vol)
        tap[0] = '{v: 1'b1, kind: CK_VOLRD, isn: lreq.isn, idx: {6'd0, lreq.size}, addr: lreq.va,
                   data: lrsp.rdata, be: lreq.be};
    end

    pmc_ev = '0;
    pmc_ev[PMC_OP_ALL]    = lreq.v && lrsp.ack && (xfer || lreq.kind == DK_PREF);
    pmc_ev[PMC_ONCHIP_IO] = lreq.v && lrsp.ack && xfer && lreq.x.rg == RG_P4REG;
    pmc_ev[PMC_FRZ_OC]    = lreq.v && !lrsp.ack;
  end

  always_ff @(posedge clk) begin
    if (rst.cpu || !lreq.v) qh <= 1'b0;
    else if (rt == RT_BR && xfer && quad && p4r.ack) begin
      qh <= !qh;
      if (!qh) qlo <= p4r.rdata;
    end
    if (rst.por) last_rdata <= 32'd0;
    else if (lreq.v && lrsp.ack && rd_k) last_rdata <= lrsp.rdata[31:0];
  end
endmodule
