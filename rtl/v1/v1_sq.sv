// v1_sq.sv -- the store queues SQ0 / SQ1 of the SH7091 (0xE0000000-0xE3FFFFFF) and their PREF bursts.  Owner: WI-CACHE
// (spec caches-sq 2.4, 3.8, 7.3; rtl/v1/SOC.md 3.2.2, 3.3.1, 3.8, 4.2).
//
// Tags: [HW] measured on the console, [M] hardware manual, [P] a choice this file makes.
//
// State: two 32-byte queues sq[n][lw] (n = VA[5], lw = VA[4:2]), a burst waiting for the BIU (`pend`) and the burst on
// the bus (`act`), each with its queue number.  SQ n is busy while a burst of it is pending or on the bus.
//
// The MA port (v1_lsu forwards every RG_SQ access here unchanged; the SOC.md 3.2.2 contract: act only in cycles with
// sqq.v, a presented request is identical until its ack).  The SOC.md 4.2 SQ column:
//   LD, RMW_RD, TAS_RD               ack in MA1, data 0, no side effect [HW: SQ reads return junk (0x00090009 once,
//                                    otherwise 0), no exception; v1 returns 0]
//   ST, RMW_WR, TAS_WR, MOVCA        write the queue: L (and MOVCA.L) one longword, Q (FMOV pair) two longwords lw, lw + 1
//                                    [M 4.6.2]; B / W (and TAS.B, AND.B ...) write the addressed bytes [P] and print
//                                    "%Warning-SQ: unmeasured size" (the SOC.md 4.2 testbench assertion).  Cost 0: ack in
//                                    MA1 [HW: 8 stores + loop overhead = 10 Icyc] -- unless that SQ is busy: then the
//                                    write waits (mah) until its burst has ended [M 4.6.3, HW: PREF SQ0 ; store SQ0 waits].
//                                    A write to the other SQ does not wait.
//   PREF                             the burst: the 2-cycle miss path (lrsp.frz in MA1 and MA1 + 1, SOC.md 3.3.1 C2), the
//                                    burst request raised in MA1 + 1 (C3), then mah until the BIU accepts it (`ready`):
//                                    one outstanding CPU burst (C8 "a posted write acks in m + 2 when its buffer is
//                                    free").  [HW: PREF of an SQ costs the 2-cycle freeze, more while the bus is busy]
//   OCBI, OCBP, OCBWB, TAS_PURGE     ack in MA1, no-op, cost 0 [P]
// The SQMD / user-mode address error is the core's (EX, SOC.md 4.2), so v1_sq has no `sqmd` input (SOC.md 3.8).  A
// store whose TLB check failed still writes the queue (mmu 3.8 [HW]): the core presents it; this unit does not look at
// x.fx.
//
// The burst (caches-sq 3.8.2): one 32-byte line write (BZ_LINE, from offset 0 in address order: the critical beat is
// quadword 0) of sq[n] to
//   MMUCR.AT = 0:  {QACRn[4:2], VA[25:5], 00000}   [HW: SQ1 -> area 1 with QACR1 = 4 while SQ0 -> area 3 with QACR0 =
//                  0x0C in the same run; VA[5] appears as address bit 5; VA[4:2] ignored (PREF @SQ + 0x14 sends the
//                  whole queue from offset 0)]
//   MMUCR.AT = 1:  {PA[28:5], 00000} of the UTLB translation the MMU made in EX (x.pa: PPN per SZ, PA[9:5] = VA[9:5])
// Beat k carries {sq[n][2k + 1], sq[n][2k]} (little endian), all eight lanes, in the Icyc after its wd_req.
//
// Timing, all from structure (every number Icyc; the stub's / BSC's times are theirs):
//   * SQ write 0: the queue is a register file written at the end of MA1.
//   * PREF 2 + the wait for `ready`: the frz pair is the miss sequencer (compare, then request), the rest is the BIU's
//     one-request port.  [HW PREF SQ0 ; PREF SQ1: 12.1 per burst = the bus, the second PREF waits for its accept]
//   * busy: set when the PREF claims `pend`; the burst's `done` (a Bclk event, the BSC's) clears it through one Bclk
//     register `rls` -- the queue's lock is Bclk state, so the core sees it free one Bcyc after the done Bcyc (M3,
//     WI-SOC).  A store waiting for the queue is acked 2 Icyc later than at `done` itself: PREF SQ0 ; store SQ0 18,
//     SQ0 only (8 stores + PREF) 22 and the SQ0 / SQ1 alternation 26 per pair [HW caches-sq 6.1: 18.23 / 22.23 / 26.46;
//     at `done` itself they were 16 / 20 / 24]; bursts back to back are the bus's (PREF SQ0 ; PREF SQ1 12 per burst,
//     24.14 per pair [HW], unchanged: the second PREF waits for the BIU's accept, not for the lock).
//
// Resets (SOC.md 2.3): power-on: queues 0 [P: undefined], no burst.  Manual: a burst already requested or on the bus
// completes (the BIU accepts nothing new during the reset, so `pend` stays presented and is taken afterwards [P]);
// the MA sequencer restarts.  CPU-only: held.
module v1_sq import v1_soc_pkg::*; (
  input  logic       clk,
  /* verilator lint_off UNUSEDSIGNAL */          // rst.pin (implied by por); x fields other than pa; isn / arch; ce.pce
  input  rst_t       rst,
  input  clken_t     ce,                         // Bclk: the queue lock's release register `rls`
  input  sq_req_t    sqq,
  /* verilator lint_on UNUSEDSIGNAL */
  output sq_rsp_t    sqr,
  input  logic [2:0] qacr0, qacr1,
  input  logic       at,
  output bq_t        bq,
  /* verilator lint_off UNUSEDSIGNAL */          // a write burst: no read beats; rd_last / wd_beat order is linear
  input  bs_t        bs,
  /* verilator lint_on UNUSEDSIGNAL */
  output bwd_t       bwd
);
  logic [31:0] sq [2][8];

  typedef struct packed {
    logic        v;
    logic        n;                                   // which queue
    logic [28:5] pa;                                  // the burst's line address
  } burst_t;
  burst_t      pend;                                  // requested, waiting for `ready`
  logic        act_v, act_n;                          // on the bus: serving wd_req until done
  logic        rls_v, rls_n;                          // its done has come: the lock is released at the next bce
  logic [1:0]  wq;                                    // the beat of the last wd_req (bwd is valid the Icyc after)

  // the presented request's own state (reset when it is not presented or acked)
  logic [1:0]  wc;                                    // cycles presented, saturating at 2 (0 = MA1)
  logic        claimed;                               // this PREF owns `pend`
  logic        accepted;                              // ... and the BIU has taken it

  // ---- decode
  logic        n, busy_n, is_wr, is_pref, wr_now, claim, ack;
  logic [2:0]  lw;
  logic [28:5] bpa;
  always_comb begin
    n       = sqq.va[5];
    lw      = sqq.va[4:2];
    is_wr   = sqq.kind == DK_ST || sqq.kind == DK_RMW_WR || sqq.kind == DK_TAS_WR || sqq.kind == DK_MOVCA;
    is_pref = sqq.kind == DK_PREF;
    busy_n  = (pend.v && pend.n == n) || (act_v && act_n == n) || (rls_v && rls_n == n);
    bpa     = at ? sqq.x.pa[28:5] : {(n ? qacr1 : qacr0), sqq.va[25:5]};
    // a store writes when its queue is free; PREF claims the request port in MA1 + 1 (or when it frees)
    wr_now  = sqq.v && is_wr && !busy_n;
    claim   = sqq.v && is_pref && wc != 2'd0 && !claimed && !pend.v;
  end
  // the MA response, in a block of its own: it reads bs.ready (a PREF acks in its request's accept cycle), and the
  // decode above feeds bq, which v1_biu's winner selection reads combinationally (C3) -- with both in one block the
  // lint saw a combinational loop bq -> BIU -> v1_bsc -> ready -> bq (UNOPTFLAT) that the logic does not have (M3-fix,
  // M3-verify F11)
  always_comb begin
    ack     = sqq.v && (is_pref ? (wc == 2'd2 && claimed && (accepted || bs.ready)) : (!is_wr || wr_now));
    sqr       = '0;
    sqr.ack   = ack;
    sqr.frz   = sqq.v && is_pref && wc != 2'd2;       // the 2-cycle miss path (C2)
    sqr.rdata = 64'd0;                                // reads (LD / RMW_RD / TAS_RD): 0
  end

  // ---- bus side
  always_comb begin
    bq          = '0;
    bq.v        = pend.v || claim;                    // C3: on bq in the claim cycle, then held by `pend`
    bq.r.we     = 1'b1;
    bq.r.addr   = claim ? {bpa, 5'b00000} : {pend.pa, 5'b00000};
    bq.r.size   = BZ_LINE;
    bq.r.lock   = 1'b0;
    bq.r.master = BM_CPU;
    bq.r.ch     = 2'd0;
    bwd.wd_data = {sq[act_n][{wq, 1'b1}], sq[act_n][{wq, 1'b0}]};
    bwd.wd_be   = 8'hFF;
  end

  // the bytes a B / W store writes into one longword (the right-aligned data at VA[1:0])
  function automatic logic [31:0] merge_lw(input logic [31:0] old, input logic [63:0] wd, input logic [7:0] be,
                                           input logic [1:0] a);
    logic [31:0] r;
    r = old;
    for (int b = 0; b < 4; b++)
      if (be[b] && 32'(a) + b < 4) r[8 * (int'(a) + b) +: 8] = wd[8 * b +: 8];
    return r;
  endfunction

  always_ff @(posedge clk) begin
    if (rst.por) begin
      for (int i = 0; i < 2; i++) for (int j = 0; j < 8; j++) sq[i][j] <= '0;
      pend <= '0; act_v <= 1'b0; act_n <= 1'b0; rls_v <= 1'b0; rls_n <= 1'b0; wq <= '0;
      wc <= '0; claimed <= 1'b0; accepted <= 1'b0;
    end else begin
      // the presented request's sequencer (a reset of the core drops the request; the queues and bursts stay)
      if (!sqq.v || ack || rst.cpu) begin
        wc <= '0; claimed <= 1'b0; accepted <= 1'b0;
      end else begin
        if (wc != 2'd2) wc <= wc + 2'd1;
        if (claim) claimed <= 1'b1;
        if ((claimed || claim) && bs.ready) accepted <= 1'b1;
      end
      // queue writes
      if (wr_now) begin
        if (sqq.kind == DK_MOVCA || sqq.size == 2'(MSZ_L)) sq[n][lw] <= sqq.wdata[31:0];
        else if (sqq.size == 2'(MSZ_Q)) begin
          sq[n][{lw[2:1], 1'b0}] <= sqq.wdata[31:0];
          sq[n][{lw[2:1], 1'b1}] <= sqq.wdata[63:32];
        end else sq[n][lw] <= merge_lw(sq[n][lw], sqq.wdata, sqq.be, sqq.va[1:0]);
      end
      // the burst: requested at the claim, on the bus from `ready` to `done`
      if (bs.wd_req) wq <= bs.wd_beat;
      if (ce.bce && rls_v) rls_v <= 1'b0;                              // the Bcyc after the done: free
      if (act_v && bs.done) begin act_v <= 1'b0; rls_v <= 1'b1; rls_n <= act_n; end   // done is a bce pulse
      if (claim && !bs.ready) pend <= '{v: 1'b1, n: n, pa: bpa};
      if (bq.v && bs.ready) begin                    // the BIU starts a write only after the previous one's done
        pend.v <= 1'b0;
        act_v  <= 1'b1;
        act_n  <= claim ? n : pend.n;
      end
    end
  end

  // the SOC.md 4.2 testbench assertion: a B / W write into the SQ is an unmeasured size [P]
  always_ff @(posedge clk)
    if (!rst.por && wr_now && sqq.kind != DK_MOVCA && sqq.size != 2'(MSZ_L) && sqq.size != 2'(MSZ_Q))
      $display("%%Warning-SQ: unmeasured size: kind %0d size %0d written into SQ%0d at %08x", sqq.kind, sqq.size,
               n, sqq.va);
endmodule
