// v1_perfctr.sv -- the performance counters PMCR1 / PMCR2 and PMCTR1 / PMCTR2 (SH7091; rtl/v1/SOC.md 3.1.3, 3.7,
// 3.8, App. A.3; spec debug-ubc-hudi 3.3, 4.6, 4.7).  Owner: WI-DBG.  Not in the manual: every rule below was
// measured on the console (debug-ubc-hudi 7: p4probe P, ubcbrk A, ubcevt P; soc-design c1 run 1 section S).
//
// Registers.
//   PMCR1 / PMCR2   CCN offsets 0x84 / 0x88 (port pq_ccn, block P4B_CCN_PMCR; the bridge's decode already ignores
//                   a[17:8], so every CCN mirror reaches here [HW "PMCR2 w16@+100 0015 -> 0015"]).  16-bit only: a 32- or
//                   8-bit access reads 0 and a write of that size is ignored [HW ubcbrk A "w32 -> 0015, w8 -> 0015"].
//                   Stored bits: 0xC13F [HW "ffff->c13f"]: 15 PMEN, 14 PMST, 8 PMCLK, 5:0 PMM (event code).  Bit 13
//                   PMCLR is a command and reads 0.
//   PMCTRnH / L     block P4B_PMCTR (port pq), decoded by a[4:2] only [HW p4probe W / H, ubcbrk A]: 000 reads 0 (not a
//                   register), 001 PMCTR1H = {16'b0, cnt1[47:32]}, 010 PMCTR1L = cnt1[31:0], 011 PMCTR2H, 1xx PMCTR2L.
//                   Read only: writes are ignored [HW "ctr-write ... L 00000000"].  Reads ignore the size [HW]: a
//                   32-bit read returns the register; a 16-bit read returns bits 15:0 at +0 and at +2 [HW "PMCTR2L r16+0
//                   8287 r16+2 828a", "PMCTR1H r16 0001 r16+2 0001"]; an 8-bit read returns bits 7:0 at +0 and 0 for
//                   PMCTR1H = 1 at +3 [HW "r8 01 r8+3 00"].  [P] the 16-bit path drives bits 15:0 on both halves of the
//                   data bus, so a byte read returns bits 7:0 at +0 / +2 and bits 15:8 at +1 / +3 (fits every measured
//                   point).  No latch between H and L [HW; KOS reads H, L, H].
//
// Counting (debug-ubc-hudi 4.6).
//   running_n = PMEN & PMST; both are needed [HW: 0x4023 and 0x8023 count nothing].
//   Each Icyc while the CPU clock runs (`run` = cpu_run, SOC.md 2.5; the counters do not advance in SLEEP [HW
//   timers-clock 4.5.1]): if running_n, cnt_n += ev_qq[PMM_n] * (PMCLK_n ? ratio_inc : 1), 48-bit wrap [P, no overflow
//   flag].  PMCLK = 1 ("ratio mode") adds ratio_inc = 12 per event at I:B = 2 [HW: 3,600,864 against 300,072].
//   ratio_inc comes from v1_cpg: 24 / Bdiv, with IFC taken as x1 like the clock enables (SOC.md 2.1, 9.18); spec
//   debug-ubc-hudi's 24 * Idiv / Bdiv agrees whenever IFC = x1, which is the DC's FRQCR 0x0E0A.
//   PMCLR (bit 13 of a PMCR write) clears the counter only if it was stopped before this write [HW p4probe P: E023
//   written while running does not clear; 2023 from running stops without clearing; stop then clear, as KOS's
//   perf_cntr_clear does, clears].  A stopped counter holds its value and continues from it [HW 3043 -> 3049].
//
// Timing (SOC.md 3.1.5, 3.7, App. A.3).  The measured numbers come from three structures, not from a table:
//   * the event pipeline: `ev` (the OR of the units' pmc_ev bits, SOC.md 3.7) is registered twice (ev_q, ev_qq) and
//     ev_qq is what the adder adds.  So an event of cycle t is in the counter read in cycle t + 3;
//   * the read: the bridge strobes an Iclk read in MA1 and rdata is the counter register in that cycle (combinational);
//   * the write: the bridge strobes an Iclk write in MA3; PMCR changes at the end of that (non-stalled) strobe cycle,
//     and the adder of that same cycle still uses the old running bit.
//   Together they give "PMCR2 <- C023; 4 x PMCTR2L -> 0 3 6 9" (the first read's MA1 is the cycle after the write's
//   MA3: counter 0; reads back to back every 3 Icyc) and "PMCR2 <- 0023; 4 x -> 21 21 21 21" [HW ubcevt P], and the
//   18 issue-count values of App. A.3 (0x13 / 0x14 / 0x23 between two PMCTR1L reads).  tb/v1_perfctr_tb replays
//   both from the scripted MA1 / event schedules of v1's issue rules.
//
// Reset (SOC.md 2.3): any power-on reset clears PMCR and PMCTR [P: the console's power-on values are unknown, every
// program found PMCR1 = C023 left by dcload / KOS]; a manual or CPU-only reset holds them.  The two event registers
// (not architectural) clear on every reset.
//
// `run` gates every counting flop (the adders and the event pipeline).  The register writes come from the bridge and
// are not gated [P] (SOC.md 2.5): a DMAC write to PMCR while the CPU sleeps is kept.
module v1_perfctr import v1_soc_pkg::*; (
  input  logic       clk,
  /* verilator lint_off UNUSEDSIGNAL */          // rst.por (registers) and rst.cpu (event pipeline) only
  input  rst_t       rst,
  /* verilator lint_on UNUSEDSIGNAL */
  input  logic       run,                       // cpu_run: the CPU clock runs
  input  logic [4:0] ratio_inc,                 // PMCLK increment from v1_cpg: 24 / Bdiv (SOC.md 2.1)
  /* verilator lint_off UNUSEDSIGNAL */          // PMCR: 16-bit registers at two offsets (wdata[31:16], dma, blk,
  input  p4s_req_t   pq_ccn,                    // off bits other than 3 unused); PMCTR is read only (wdata, dma,
  output p4s_rsp_t   pr_ccn,                    // blk, off[27:5] unused)
  input  p4s_req_t   pq,
  /* verilator lint_on UNUSEDSIGNAL */
  output p4s_rsp_t   pr,
  input  pmc_ev_t    ev                         // bit k = event k this cycle (SOC.md 3.7 sources)
);
  localparam logic [15:0] PMCR_MASK = 16'hC13F;

  logic [15:0] pmcr [2];                        // stored bits only (masked with 0xC13F)
  logic [47:0] cnt  [2];
  pmc_ev_t     ev_q, ev_qq;                     // the two event registers (App. A.3)

  // ---------------------------------------------------------------------------------------------- PMCR port
  // 0x84 -> PMCR1, 0x88 -> PMCR2 (the bridge sends only these two offsets here)
  logic       c_sel;                            // which PMCR the strobe addresses
  logic       c_ok;                             // right size: a 16-bit access
  logic       c_wr;                             // a PMCR write lands at the end of this cycle
  logic [15:0] c_wd;
  assign c_sel = pq_ccn.off[3];                 // 0x84: off[3] = 0; 0x88: off[3] = 1
  assign c_ok  = pq_ccn.size == 2'd1;
  assign c_wr  = pq_ccn.wr && c_ok;
  assign c_wd  = pq_ccn.wdata[15:0] & PMCR_MASK;

  always_comb begin
    pr_ccn = '0;
    if (pq_ccn.rd || pq_ccn.wr) begin
      pr_ccn.hit = 1'b1;
      if (pq_ccn.rd && c_ok) pr_ccn.rdata = {16'd0, pmcr[c_sel]};
    end
  end

  // ---------------------------------------------------------------------------------------------- PMCTR port
  logic [31:0] t_val;                           // the addressed 32-bit view
  logic        t_reg;                           // a[4:2] != 000
  always_comb begin
    t_reg = pq.off[4:2] != 3'd0;
    unique casez (pq.off[4:2])
      3'b001:  t_val = {16'd0, cnt[0][47:32]};
      3'b010:  t_val = cnt[0][31:0];
      3'b011:  t_val = {16'd0, cnt[1][47:32]};
      3'b1??:  t_val = cnt[1][31:0];
      default: t_val = 32'd0;
    endcase
    pr = '0;
    if (pq.rd || pq.wr) begin
      pr.hit = t_reg;
      if (pq.rd) begin
        unique case (pq.size)
          2'd2:    pr.rdata = t_val;
          2'd1:    pr.rdata = {16'd0, t_val[15:0]};                              // either halfword: bits 15:0
          default: pr.rdata = {24'd0, pq.off[0] ? t_val[15:8] : t_val[7:0]};    // [P] lanes of the replicated half
        endcase
      end
    end
  end

  // ---------------------------------------------------------------------------------------------- counting
  logic [47:0] inc [2];
  logic        running [2];
  always_comb begin
    for (int n = 0; n < 2; n++) begin
      running[n] = pmcr[n][15] && pmcr[n][14];
      // one comparator mux per counter: no event can occur twice in a cycle (one data port, one fetch, two branches
      // never pair), so each event adds 0 or 1 (or the PMCLK ratio)
      inc[n] = !ev_qq[pmcr[n][5:0]] ? 48'd0 : pmcr[n][8] ? {43'd0, ratio_inc} : 48'd1;
    end
  end

  always_ff @(posedge clk) begin
    if (rst.cpu) begin
      ev_q <= '0; ev_qq <= '0;
    end else if (run) begin
      ev_q <= ev; ev_qq <= ev_q;
    end
    if (rst.por) begin
      pmcr[0] <= '0; pmcr[1] <= '0; cnt[0] <= '0; cnt[1] <= '0;
    end else begin
      for (int n = 0; n < 2; n++) begin
        // the adder of this cycle uses this cycle's running bit (a write lands at the end of the cycle)
        if (run && running[n]) cnt[n] <= cnt[n] + inc[n];
        if (c_wr && c_sel == 1'(n)) begin
          pmcr[n] <= c_wd;
          if (pq_ccn.wdata[13] && !running[n]) cnt[n] <= '0;   // PMCLR: only a stopped counter clears
        end
      end
    end
  end
endmodule
