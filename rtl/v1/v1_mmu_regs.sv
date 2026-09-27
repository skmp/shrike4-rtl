// v1_mmu_regs.sv -- PTEH, PTEL, PTEA, TTB, TEA and MMUCR with their P4 slave (rtl/v1/spec/mmu.md 2.1, 7.6; SOC.md
// 3.1.3 block CCN_MMU, 3.1.4 size rule, 3.1.5 timing, 2.3 resets).  Owner: WI-MMU.
//
// The bridge strobes this slave for CCN offsets 0x00 0x04 0x08 0x0C 0x10 0x34 (a[7:0]; a[17:8] ignored by the
// decode).  Every register is 32 bits: any other access size reads 0 (hit = 1) and ignores the write (SOC.md 3.1.4).
// Implemented bits [HW A1 / A2]: PTEH 0xFFFFFCFF, PTEL 0x1FFFFDFF, PTEA 0x0000000F, TTB / TEA all, MMUCR 0xFCFCFF05
// with TI reading 0.
//
// Timing (Iclk register class, SOC.md 3.1.5): reads are combinational in the strobe cycle (3 Icyc in MA); a write is
// applied in its capture cycle.  A PTEH write answers stall = 1 once (4 Icyc in MA [HW core-integration 6.1]): the
// new ASID drives the UTLB / ITLB compare lines and waits one cycle for them [P, SOC.md 3.1.5].  pteh_st is that one
// cycle of occupancy.  The TLB compare lines take the written ASID in the capture cycle itself (`asid_nx`, a
// write-through, as `at_nx` is for AT's fault gate): the issue group held in EX behind the store leaves EX at the end
// of that cycle, so the very next access is translated with the new ASID, and the translation has no PTEH landing
// window [HW tools/hw/mmu/ptehwin (M2-verify F1): a P0 load 0, 1, 2, 3 or 4 instructions after PTEH.ASID 1 -> 2 is
// translated with the new ASID, cold and warm, 160 of 160 calls; the MMUCR AT reference of the same probe keeps R5's
// 3-instruction window].  pteh_asid itself (the PTEH read, LDTLB's ASID, v1_ubc's ASID compares) is written at the end
// of the capture cycle: the UBC has the landing window of its own registers [HW tools/hw/mmu/ptehubc, M2-fix].
//
// Hardware writes, in priority order within a cycle (mmu 7.6): a software MMUCR write, then the exception capture of
// TEA / PTEH.VPN (the core's excw at EXC0's S; the MMU's own associative multiple hit), then the URC step and the LRUI
// update (different fields: both apply).  The capture beats a software TEA / PTEH write of the same cycle [P] (they
// cannot meet: the writing store is older than the entry and has left MA before EXC0 reaches S).
module v1_mmu_regs import v1_soc_pkg::*; import v1_mmu_pkg::*; (
  input  logic        clk,
  /* verilator lint_off UNUSEDSIGNAL */          // rst.pin is implied by rst.por; a CPU-only reset holds every register
  input  rst_t        rst,
  /* verilator lint_on UNUSEDSIGNAL */
  /* verilator lint_off UNUSEDSIGNAL */          // pq.blk (one block), pq.off[27:8] (a[17:8] ignored), pq.dma
  input  p4s_req_t    pq,
  /* verilator lint_on UNUSEDSIGNAL */
  output p4s_rsp_t    pr,
  /* verilator lint_off UNUSEDSIGNAL */          // excw: only tea_v / tea are the MMU's (EXPEVT etc. are v1_excregs')
  input  excw_t       excw,
  /* verilator lint_on UNUSEDSIGNAL */
  input  logic        cap_v,                     // associative-write multiple hit: TEA / PTEH.VPN <- cap_va
  input  logic [31:0] cap_va,
  input  logic        urc_inc,                   // a counted UTLB access completed (ma_commit, qualified: SOC.md 3.8)
  input  logic        lrui_we,                   // an ITLB use (hit or refill) this cycle
  input  logic [5:0]  lrui_nx,
  output logic [21:0] pteh_vpn,
  output logic [7:0]  pteh_asid,
  output logic [31:0] ptel,                      // stored masked (0x1FFFFDFF)
  output logic [3:0]  ptea,                      // {TC, SA}
  output mmucr_t      mmucr,
  output logic        ti,                        // a MMUCR write with TI = 1 lands at the end of this cycle
  output logic        at_nx,                     // MMUCR.AT at the end of this cycle
  output logic [7:0]  asid_nx                    // PTEH.ASID at the end of this cycle (the compare lines' ASID)
);
  logic [31:0] ttb, tea;
  logic        pteh_st;                          // the PTEH write's first strobe was answered with stall

  // ---- decode (a[7:0])
  logic sel_pteh, sel_ptel, sel_ttb, sel_tea, sel_mmucr, sel_ptea, strobe, sz_ok, hit;
  always_comb begin
    strobe    = pq.rd || pq.wr;
    sel_pteh  = pq.off[7:0] == 8'h00;
    sel_ptel  = pq.off[7:0] == 8'h04;
    sel_ttb   = pq.off[7:0] == 8'h08;
    sel_tea   = pq.off[7:0] == 8'h0C;
    sel_mmucr = pq.off[7:0] == 8'h10;
    sel_ptea  = pq.off[7:0] == 8'h34;
    hit       = sel_pteh || sel_ptel || sel_ttb || sel_tea || sel_mmucr || sel_ptea;
    sz_ok     = pq.size == 2'd2;
  end

  logic stall, wr_now;
  always_comb begin
    stall  = pq.wr && sel_pteh && sz_ok && !pteh_st;
    wr_now = pq.wr && sz_ok && !stall;
    pr = '0;                                                     // not strobed: 0 / 0 / 0 (SOC.md 3.1.2)
    if (strobe && hit) begin
      pr.hit = 1'b1;
      pr.stall = stall;
      if (pq.rd && sz_ok)
        unique case (1'b1)
          sel_pteh:  pr.rdata = {pteh_vpn, 2'b00, pteh_asid};
          sel_ptel:  pr.rdata = ptel;
          sel_ttb:   pr.rdata = ttb;
          sel_tea:   pr.rdata = tea;
          sel_mmucr: pr.rdata = mmucr_rd(mmucr);
          default:   pr.rdata = {28'b0, ptea};
        endcase
    end
    ti    = wr_now && sel_mmucr && pq.wdata[2];
    at_nx = (wr_now && sel_mmucr) ? pq.wdata[0] : mmucr.at;
    asid_nx = (wr_now && sel_pteh) ? pq.wdata[7:0] : pteh_asid;
  end

  always_ff @(posedge clk) begin
    if (rst.por) begin                           // SOC.md 2.3: MMUCR 0, TEA 0 [P], PTEH PTEL PTEA TTB 0 [P]
      mmucr <= '0; tea <= '0; pteh_vpn <= '0; pteh_asid <= '0; ptel <= '0; ptea <= '0; ttb <= '0; pteh_st <= 1'b0;
    end else if (rst.man) begin                  // manual (SOC.md 2.3): MMUCR 0, TEA and PTEH held, PTEL PTEA TTB 0 [P]
      // PTEH is held (hm Appendix A: undefined after a manual reset, so holding is legal): a TLB multiple-hit reset
      // (hm 5.6.1 (4)/(5), SOC.md 2.3 note) writes TEA / PTEH.VPN in its request cycle (excw, or cap_v for an
      // associative write) and v1_rstc raises rst.man from the next cycle for 8 Icyc; clearing PTEH here would lose
      // the VPN and the ASID "at the time of the exception" (SOC.md 2.3 table: PTEH held, the capture survives)
      mmucr <= '0; ptel <= '0; ptea <= '0; ttb <= '0; pteh_st <= 1'b0;
    end else begin                               // CPU-only reset: everything held
      pteh_st <= stall;
      // 3. URC step (a translated access completed) and the LRUI use, unless 1. a MMUCR write lands now
      if (wr_now && sel_mmucr) begin
        mmucr.lrui <= pq.wdata[31:26]; mmucr.urb <= pq.wdata[23:18]; mmucr.urc <= pq.wdata[15:10];
        mmucr.sqmd <= pq.wdata[9]; mmucr.sv <= pq.wdata[8]; mmucr.at <= pq.wdata[0];
      end else begin
        if (urc_inc) mmucr.urc <= urc_next(mmucr.urc, mmucr.urb);
        if (lrui_we) mmucr.lrui <= lrui_nx;
      end
      if (wr_now && sel_ptel) ptel <= pq.wdata & 32'h1FFFFDFF;
      if (wr_now && sel_ptea) ptea <= pq.wdata[3:0];
      if (wr_now && sel_ttb)  ttb <= pq.wdata;
      if (wr_now && sel_pteh) begin pteh_vpn <= pq.wdata[31:10]; pteh_asid <= pq.wdata[7:0]; end
      if (wr_now && sel_tea)  tea <= pq.wdata;
      // 2. the capture: TEA <- VA, PTEH.VPN <- VA[31:10]; ASID is never written by hardware [HW F, G]
      if (excw.tea_v) begin
        tea <= excw.tea; pteh_vpn <= excw.tea[31:10];
      end else if (cap_v) begin
        tea <= cap_va; pteh_vpn <= cap_va[31:10];
      end
    end
  end
endmodule
